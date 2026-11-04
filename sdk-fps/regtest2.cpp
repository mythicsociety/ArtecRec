// regtest2 — offline registration experiments on a saved project: Studio's manual sequence Rough serial -> Fine
// serial -> Global, geometry only (frames are not all textured), with timings. Which steps run is chosen by the
// mode string: letters r (rough), f (fine), g (global); e.g. "rfg" or "fg" or "r".
// usage: regtest2 <in.sproj> <out.sproj> <mode>
#include <artec/sdk/base/Log.h>
#include <artec/sdk/base/IModel.h>
#include <artec/sdk/base/IScan.h>
#include <artec/sdk/base/IFrameMesh.h>
#include <artec/sdk/base/AlgorithmWorkset.h>
#include <artec/sdk/base/IJob.h>
#include <artec/sdk/base/IArray.h>
#include <artec/sdk/base/Uuid.h>
#include <artec/sdk/base/TRef.h>
#include <artec/sdk/project/IProject.h>
#include <artec/sdk/project/ProjectSettings.h>
#include <artec/sdk/project/EntryInfo.h>
#include <artec/sdk/project/ProjectLoaderSettings.h>
#include <artec/sdk/project/ProjectSaverSettings.h>
#include <artec/sdk/algorithms/Algorithms.h>
#include <artec/sdk/algorithms/IAlgorithm.h>
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
using namespace artec::sdk::base; namespace prj = artec::sdk::project; namespace alg = artec::sdk::algorithms; using clk = std::chrono::steady_clock;
static void ensureNormals(IScan* s) { for (int k = 0; k < s->getSize(); k++) { IFrameMesh* f = s->getElement(k); if (f && !f->hasPointsNormals()) f->calculate(CM_PointsNormals_Default); } }
static IModel* run(IModel* in, alg::IAlgorithm* a, const wchar_t* what) { IModel* out = nullptr; createModel(&out); AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = in; ws.out = out; ws.threadsCount = std::thread::hardware_concurrency(); auto t0 = clk::now(); ErrorCode ec = executeJob(a, &ws); a->release();
  wprintf(L"%ls: 0x%08X in %.1f s, out scans %d\n", what, (unsigned)ec, std::chrono::duration<double>(clk::now() - t0).count(), out->getSize()); if (ec != ErrorCode_OK || out->getSize() == 0) { out->release(); return nullptr; } return out; }
int wmain(int argc, wchar_t** argv) {
  if (argc < 4) { wprintf(L"usage: regtest2 in.sproj out.sproj mode(r f g letters)\n"); return 2; } std::wstring mode = argv[3]; setOutputLevel(VerboseLevel_Error);
  prj::IProject* p = nullptr; ErrorCode ec = prj::openProject(&p, argv[1]); if (ec != ErrorCode_OK || !p) { wprintf(L"openProject 0x%08X\n", (unsigned)ec); return 1; }
  std::vector<Uuid> v; for (int i = 0; i < p->getEntryCount(); i++) { prj::EntryInfo e; if (p->getEntry(i, &e, nullptr) == ErrorCode_OK && e.type == prj::EntryType_Scan) v.push_back(e.uuid); }
  IArrayUuid* ids = nullptr; createArrayUuid(&ids, (int)v.size()); for (int i = 0; i < (int)v.size(); i++) ids->setElement(i, v[i]);
  prj::ProjectLoaderSettings ls; ls.entryList = ids; IJob* loader = nullptr; ec = p->createLoader(&loader, &ls); IModel* cur = nullptr; createModel(&cur);
  { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.out = cur; ws.threadsCount = std::thread::hardware_concurrency(); ec = executeJob(loader, &ws); loader->release(); } ids->release(); p->release(); if (ec != ErrorCode_OK) { wprintf(L"load 0x%08X\n", (unsigned)ec); return 1; }
  for (int i = 0; i < cur->getSize(); i++) ensureNormals(cur->getElement(i)); ScannerType st = cur->getElement(0)->getScannerType(); wprintf(L"loaded %d scans, mode %ls\n", cur->getSize(), mode.c_str());
  for (wchar_t c : mode) { alg::IAlgorithm* a = nullptr; IModel* out = nullptr;
    if (c == L'r') { alg::SerialRegistrationSettings s; s.scannerType = st; s.registrationType = alg::SerialRegistrationType_Rough; if (alg::createSerialRegistrationAlgorithm(&a, &s) == ErrorCode_OK) out = run(cur, a, L"rough serial"); }
    else if (c == L'f') { alg::SerialRegistrationSettings s; s.scannerType = st; s.registrationType = alg::SerialRegistrationType_Fine; if (alg::createSerialRegistrationAlgorithm(&a, &s) == ErrorCode_OK) out = run(cur, a, L"fine serial"); }
    else if (c == L'g') { alg::GlobalRegistrationSettings s; s.scannerType = st; s.registrationType = alg::GlobalRegistrationType_Geometry; if (alg::createGlobalRegistrationAlgorithm(&a, &s) == ErrorCode_OK) out = run(cur, a, L"global geometry"); }
    if (!out) { wprintf(L"step %lc failed\n", c); return 1; } for (int i = 0; i < out->getSize(); i++) ensureNormals(out->getElement(i)); cur->release(); cur = out; }
  std::wstring outp = argv[2]; std::wstring dir = outp.substr(0, outp.find_last_of(L"\\/")); SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
  prj::ProjectSettings ps; ps.path = outp.c_str(); prj::IProject* po = nullptr; ec = prj::createNewProject(&po, &ps); if (ec != ErrorCode_OK) { wprintf(L"createNewProject 0x%08X\n", (unsigned)ec); return 1; }
  prj::ProjectSaverSettings ss; ss.path = outp.c_str(); generateUuid(&ss.projectId); ss.compressionLevel = 0; IJob* saver = nullptr; ec = po->createSaver(&saver, &ss);
  { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = cur; ws.threadsCount = std::thread::hardware_concurrency(); ec = executeJob(saver, &ws); saver->release(); po->release(); wprintf(L"saved 0x%08X -> %ls\n", (unsigned)ec, outp.c_str()); }
  cur->release(); return ec == ErrorCode_OK ? 0 : 1;
}
