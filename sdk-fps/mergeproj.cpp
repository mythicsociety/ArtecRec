// mergeproj — 8 Nov: put the scans of several Artec projects into one new project (Trail31 + Trail31_part2 -> Trail31_full).
// Scans are copied as they are (frames, poses, names); nothing is registered or changed.
// usage: mergeproj <out.sproj> <in1.sproj> <in2.sproj> [...]
#include <artec/sdk/base/Log.h>
#include <artec/sdk/base/IModel.h>
#include <artec/sdk/base/IScan.h>
#include <artec/sdk/base/AlgorithmWorkset.h>
#include <artec/sdk/base/IJob.h>
#include <artec/sdk/base/IArray.h>
#include <artec/sdk/base/Uuid.h>
#include <artec/sdk/project/IProject.h>
#include <artec/sdk/project/ProjectSettings.h>
#include <artec/sdk/project/EntryInfo.h>
#include <artec/sdk/project/ProjectLoaderSettings.h>
#include <artec/sdk/project/ProjectSaverSettings.h>
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <thread>
using namespace artec::sdk::base; namespace prj = artec::sdk::project;
int wmain(int argc, wchar_t** argv) {
  if (argc < 3) { wprintf(L"usage: mergeproj out.sproj in1.sproj [in2.sproj ...]\n"); return 2; } setOutputLevel(VerboseLevel_Error);
  IModel* all = nullptr; createModel(&all); int total = 0;
  for (int a = 2; a < argc; a++) {
    prj::IProject* p = nullptr; if (prj::openProject(&p, argv[a]) != ErrorCode_OK || !p) { wprintf(L"openProject failed: %s\n", argv[a]); return 1; }
    std::vector<Uuid> v; for (int i = 0; i < p->getEntryCount(); i++) { prj::EntryInfo e; if (p->getEntry(i, &e, nullptr) == ErrorCode_OK && e.type == prj::EntryType_Scan) v.push_back(e.uuid); }
    IArrayUuid* ids = nullptr; createArrayUuid(&ids, (int)v.size()); for (int i = 0; i < (int)v.size(); i++) ids->setElement(i, v[i]); prj::ProjectLoaderSettings ls; ls.entryList = ids; IJob* loader = nullptr; p->createLoader(&loader, &ls); IModel* m = nullptr; createModel(&m);
    { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.out = m; ws.threadsCount = (int)std::thread::hardware_concurrency(); if (executeJob(loader, &ws) != ErrorCode_OK) { wprintf(L"load failed: %s\n", argv[a]); return 1; } loader->release(); } ids->release(); p->release();
    for (int i = 0; i < m->getSize(); i++) { IScan* s = m->getElement(i); if (!s) continue; long nf = s->getSize(); all->add(s); total++; wprintf(L"%s: scan %d \"%s\", %ld frames\n", argv[a], i + 1, s->getName() ? s->getName() : L"", nf); }
    m->release(); }
  std::wstring out = argv[1]; std::wstring dir = out.substr(0, out.find_last_of(L"\\/")); SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
  prj::ProjectSettings ps; ps.path = out.c_str(); prj::IProject* q = nullptr; ErrorCode ec = prj::createNewProject(&q, &ps); if (ec != ErrorCode_OK || !q) { wprintf(L"createNewProject 0x%08X\n", (unsigned)ec); return 1; }
  prj::ProjectSaverSettings ss; ss.path = out.c_str(); generateUuid(&ss.projectId); ss.compressionLevel = 0; IJob* saver = nullptr; ec = q->createSaver(&saver, &ss);
  if (ec == ErrorCode_OK && saver) { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = all; ws.threadsCount = (int)std::thread::hardware_concurrency(); ec = executeJob(saver, &ws); saver->release(); } q->release();
  wprintf(L"saved %d scan(s) 0x%08X -> %s\n", total, (unsigned)ec, out.c_str()); all->release(); return ec == ErrorCode_OK ? 0 : 1;
}
