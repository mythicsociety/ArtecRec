// SDK scanning-procedure endurance test: runs Artec's own tracking/registration recording pipeline
// (the same work Studio does when recording) and reports fps, registration failures and memory every 10 s.
// usage: sdkscan.exe <seconds> [texture 0|1] [registration icp|hybrid|texture]
#include <artec/sdk/base/Log.h>
#include <artec/sdk/base/IModel.h>
#include <artec/sdk/base/IScan.h>
#include <artec/sdk/base/AlgorithmWorkset.h>
#include <artec/sdk/base/IJob.h>
#include <artec/sdk/base/RefBase.h>
#include <artec/sdk/capturing/IScanner.h>
#include <artec/sdk/capturing/IArrayScannerId.h>
#include <artec/sdk/scanning/IScanningProcedure.h>
#include <artec/sdk/scanning/IScanningProcedureObserver.h>
#include <artec/sdk/base/Uuid.h>
#include <artec/sdk/project/IProject.h>
#include <artec/sdk/project/ProjectSettings.h>
#include <artec/sdk/project/ProjectSaverSettings.h>
#include <artec/sdk/project/EntryInfo.h>
#include <artec/sdk/project/EntryType.h>
#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#include <atomic>
#include "flyhook.h"
using namespace artec::sdk::base;
using namespace artec::sdk::capturing;
using namespace artec::sdk::scanning;
using clk = std::chrono::steady_clock;
static int hook(int lvl, const wchar_t* m) { if (lvl <= 1) { wprintf(L"[sdk %d] %s", lvl, m); fflush(stdout); } return 0; }
struct Obs : public ScanningProcedureObserverBase {
  std::atomic<long> captured{0}, scanned{0}, regFail{0}, capFail{0}, recFail{0}, texFail{0}, addFail{0}, other{0};
  void onFrameCaptured(const RegistrationInfo*) override { captured++; }
  void onFrameScanned(const RegistrationInfo* i) override {
    scanned++;
    switch (i->frameState) { case FrameState_Ok: break; case FrameState_RegistrationFailed: regFail++; break; case FrameState_CaptureFailed: case FrameState_TriggerCaptureFailed: capFail++; break;
      case FrameState_ReconstructionFailed: recFail++; break; case FrameState_TextureMappingFailed: texFail++; break; case FrameState_AddToScanFailed: addFail++; break; default: other++; } }
  void onScanningFinished(int) override { printf("scanning finished\n"); fflush(stdout); }
};
static long wsMB() { PROCESS_MEMORY_COUNTERS c; GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof c); return (long)(c.WorkingSetSize >> 20); }
int main(int argc, char** argv) {
  setReportHook(RPTHOOK_INSTALL, hook); setOutputLevel(VerboseLevel_Warning);
  int secs = argc > 1 ? atoi(argv[1]) : 120; bool tex = argc > 2 ? atoi(argv[2]) != 0 : true;
  RegistrationAlgorithmType reg = RegistrationAlgorithmType_Hybrid;
  if (argc > 3) { if (!strcmp(argv[3], "icp")) reg = RegistrationAlgorithmType_ICP; else if (!strcmp(argv[3], "texture")) reg = RegistrationAlgorithmType_Texture; }
  flyhook::install();
  IArrayScannerId* list = nullptr; ErrorCode ec = enumerateScanners(&list);
  if (ec != ErrorCode_OK || !list || list->getSize() < 1) { printf("enumerateScanners 0x%08X\n", (unsigned)ec); return 2; }
  IScanner* sc = nullptr; ec = createScanner(&sc, list->getPointer());
  if (ec != ErrorCode_OK || !sc) { printf("createScanner 0x%08X\n", (unsigned)ec); return 3; }
  ScanningProcedureSettings st; ec = initializeScanningProcedureSettings(&st); printf("initializeScanningProcedureSettings 0x%08X (maxFrames=%d pipeline=0x%x reg=%d)\n", (unsigned)ec, st.maxFrameCount, st.pipelineConfiguration, (int)st.registrationType);
  Obs* obs = new Obs();
  st.maxFrameCount = 1000000; st.registrationType = reg; st.initialState = ScanningState_Record; st.scanningCallback = obs; st.ignoreRegistrationErrors = true;
  st.pipelineConfiguration = ScanningPipeline_CalculateNormals | ScanningPipeline_RegisterFrame | ScanningPipeline_FindGeometryKeyFrame | (tex ? (ScanningPipeline_ConvertTextures | ScanningPipeline_MapTexture) : 0);
  st.captureTexture = tex ? CaptureTextureMethod_EveryNFrame : CaptureTextureMethod_NoTextures; st.captureTextureFrequency = 1; st.saveEmptySurfaces = false;
  IScanningProcedure* proc = nullptr; ec = createScanningProcedure(&proc, sc, &st);
  printf("createScanningProcedure 0x%08X\n", (unsigned)ec); fflush(stdout); if (ec != ErrorCode_OK || !proc) return 4;
  IModel* in = nullptr; IModel* out = nullptr; createModel(&in); createModel(&out);
  AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = in; ws.out = out; ws.threadsCount = std::thread::hardware_concurrency();
  printf("recording for %d s: texture=%d registration=%d threads=%u\n--- t(s)  fps10s  scanned  regFail  capFail  recFail  texFail  addFail  ws_MB\n", secs, (int)tex, (int)reg, ws.threadsCount); fflush(stdout);
  std::atomic<ErrorCode> jobEc{ErrorCode_OK};
  std::thread job([&] { jobEc = executeJob(proc, &ws); });
  auto t0 = clk::now(); long last = 0;
  while (std::chrono::duration<double>(clk::now() - t0).count() < secs) {
    Sleep(10000); long s = obs->scanned.load(); double t = std::chrono::duration<double>(clk::now() - t0).count();
    printf("%6.0f %7.2f %8ld %8ld %8ld %8ld %8ld %8ld %6ld\n", t, (s - last) / 10.0, s, obs->regFail.load(), obs->capFail.load(), obs->recFail.load(), obs->texFail.load(), obs->addFail.load(), wsMB()); fflush(stdout); last = s;
    if (!job.joinable()) break;
  }
  proc->setState(ScanningState_Stop); job.join();
  int frames = 0; for (int i = 0; i < out->getSize(); i++) frames += out->getElement(i)->getSize();
  printf("job result 0x%08X; scans=%d frames stored=%d; captured=%ld scanned=%ld\n", (unsigned)jobEc.load(), out->getSize(), frames, obs->captured.load(), obs->scanned.load()); fflush(stdout);
  if (argc > 4) {  // save as an Artec project: argv[4] = path to <name>.sproj
    wchar_t wpath[1024]; mbstowcs(wpath, argv[4], 1023);
    for (int i = 0; i < out->getSize(); i++) { wchar_t nm[32]; swprintf(nm, 32, L"Scan %d", i + 1); out->getElement(i)->setName(nm); }
    artec::sdk::project::ProjectSettings ps; ps.path = wpath; artec::sdk::project::IProject* prj = nullptr;
    ec = artec::sdk::project::createNewProject(&prj, &ps); printf("createNewProject 0x%08X\n", (unsigned)ec);
    if (ec == ErrorCode_OK && prj) {
      artec::sdk::project::ProjectSaverSettings ss; ss.path = wpath; generateUuid(&ss.projectId); ss.compressionLevel = 0;
      IJob* saver = nullptr; ec = prj->createSaver(&saver, &ss); printf("createSaver 0x%08X\n", (unsigned)ec);
      if (ec == ErrorCode_OK && saver) {
        AlgorithmWorkset sws; memset(&sws, 0, sizeof sws); sws.in = out; sws.out = nullptr; sws.threadsCount = ws.threadsCount;
        auto t1 = clk::now(); ec = executeJob(saver, &sws);
        printf("save 0x%08X in %.1f s -> %s (entries=%d)\n", (unsigned)ec, std::chrono::duration<double>(clk::now() - t1).count(), argv[4], prj->getEntryCount());
        saver->release();
      }
      prj->release();
    }
    fflush(stdout);
  }
  proc->release(); out->release(); in->release(); sc->release(); list->release(); return 0;
}
