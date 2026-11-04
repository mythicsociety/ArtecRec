// regtest — offline experiment: load an ArtecRec/Studio .sproj, run the SDK global registration
// (GeometryAndTexture) across all scans, report timing, save to <out>.sproj. No UI, no scanner.
// usage: regtest <in.sproj> <out.sproj> [geometry|texture]
#include <artec/sdk/base/Log.h>
#include <artec/sdk/base/IModel.h>
#include <artec/sdk/base/IScan.h>
#include <artec/sdk/base/IFrameMesh.h>
#include <artec/sdk/base/AlgorithmWorkset.h>
#include <artec/sdk/base/IJob.h>
#include <artec/sdk/base/IArray.h>
#include <artec/sdk/base/IString.h>
#include <artec/sdk/base/Uuid.h>
#include <artec/sdk/algorithms/Algorithms.h>
#include <artec/sdk/algorithms/IAlgorithm.h>
#include <artec/sdk/project/IProject.h>
#include <artec/sdk/project/ProjectSettings.h>
#include <artec/sdk/project/EntryInfo.h>
#include <artec/sdk/project/ProjectLoaderSettings.h>
#include <artec/sdk/project/ProjectSaverSettings.h>
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <chrono>
#include <string>
#include <vector>
#include <thread>
using namespace artec::sdk::base; namespace alg = artec::sdk::algorithms; namespace prj = artec::sdk::project;
typedef std::chrono::steady_clock clk;
static void ensureNormals(IScan* s) { for (int k = 0; k < s->getSize(); k++) { IFrameMesh* f = s->getElement(k); if (f && !f->hasPointsNormals()) f->calculate(CM_PointsNormals_Default); } }
int wmain(int argc, wchar_t** argv) {
  if (argc < 3) { wprintf(L"usage: regtest in.sproj out.sproj [geometry|texture]\n"); return 2; }
  bool tex = argc < 4 || _wcsicmp(argv[3], L"texture") == 0; setOutputLevel(VerboseLevel_Warning);
  prj::IProject* p = nullptr; ErrorCode ec = prj::openProject(&p, argv[1]); if (ec != ErrorCode_OK || !p) { wprintf(L"openProject 0x%08X\n", (unsigned)ec); return 1; }
  int n = p->getEntryCount(); IArrayUuid* ids = nullptr; createArrayUuid(&ids, 0); std::vector<Uuid> v;
  for (int i = 0; i < n; i++) { prj::EntryInfo e; if (p->getEntry(i, &e, nullptr) == ErrorCode_OK && e.type == prj::EntryType_Scan) v.push_back(e.uuid); }
  ids->release(); createArrayUuid(&ids, (int)v.size()); for (int i = 0; i < (int)v.size(); i++) ids->setElement(i, v[i]);
  prj::ProjectLoaderSettings ls; ls.entryList = ids; IJob* loader = nullptr; ec = p->createLoader(&loader, &ls); IModel* in = nullptr; createModel(&in);
  { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.out = in; ws.threadsCount = std::thread::hardware_concurrency(); auto t0 = clk::now(); ec = executeJob(loader, &ws); loader->release();
    wprintf(L"loaded 0x%08X: %d scans in %.1f s\n", (unsigned)ec, in->getSize(), std::chrono::duration<double>(clk::now() - t0).count()); }
  ids->release(); p->release(); if (ec != ErrorCode_OK) return 1;
  int frames = 0; bool allTex = true; for (int i = 0; i < in->getSize(); i++) { IScan* s = in->getElement(i); ensureNormals(s); frames += s->getSize(); for (int k = 0; k < s->getSize(); k++) { IFrameMesh* f = s->getElement(k); if (!f || !f->isTextured()) allTex = false; } }
  wprintf(L"frames %d, all textured %d, mode %s\n", frames, (int)allTex, tex ? L"GeometryAndTexture" : L"Geometry");
  if (tex && !allTex) { wprintf(L"not all frames textured: GeometryAndTexture would be refused by the SDK, using Geometry\n"); tex = false; }
  IModel* out = nullptr; createModel(&out);
  alg::GlobalRegistrationSettings gs; gs.scannerType = in->getElement(0)->getScannerType(); gs.registrationType = tex ? alg::GlobalRegistrationType_GeometryAndTexture : alg::GlobalRegistrationType_Geometry;
  alg::IAlgorithm* a = nullptr; ec = alg::createGlobalRegistrationAlgorithm(&a, &gs); if (ec != ErrorCode_OK || !a) { wprintf(L"createGlobalRegistrationAlgorithm 0x%08X\n", (unsigned)ec); return 1; }
  { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = in; ws.out = out; ws.threadsCount = std::thread::hardware_concurrency(); auto t0 = clk::now(); ec = executeJob(a, &ws); a->release();
    wprintf(L"global registration 0x%08X in %.1f s, out scans %d\n", (unsigned)ec, std::chrono::duration<double>(clk::now() - t0).count(), out->getSize()); }
  if (ec != ErrorCode_OK) return 1;
  for (int i = 0; i < out->getSize(); i++) { const auto& T = out->getElement(i)->getScanTransformation().m; wprintf(L"scan %d: scan transform t = %.1f %.1f %.1f\n", i, T[0][3], T[1][3], T[2][3]); }
  std::wstring outp = argv[2]; std::wstring dir = outp.substr(0, outp.find_last_of(L"\\/")); SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
  prj::ProjectSettings ps; ps.path = outp.c_str(); prj::IProject* po = nullptr; ec = prj::createNewProject(&po, &ps); if (ec != ErrorCode_OK) { wprintf(L"createNewProject 0x%08X\n", (unsigned)ec); return 1; }
  prj::ProjectSaverSettings ss; ss.path = outp.c_str(); generateUuid(&ss.projectId); ss.compressionLevel = 0; IJob* saver = nullptr; ec = po->createSaver(&saver, &ss);
  { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = out; ws.threadsCount = std::thread::hardware_concurrency(); auto t0 = clk::now(); ec = executeJob(saver, &ws); saver->release(); po->release();
    wprintf(L"saved 0x%08X in %.1f s -> %ls\n", (unsigned)ec, std::chrono::duration<double>(clk::now() - t0).count(), outp.c_str()); }
  out->release(); in->release(); return ec == ErrorCode_OK ? 0 : 1;
}
