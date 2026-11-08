// fusetest — 8 Nov: do Field-mode passes make holes in the fused model, and does fine registration remove them?
// Loads a project, optionally runs fine (serial) registration on every scan and global registration on the set, fuses
// (Poisson, sharp, no hole filling) and counts the boundary loops (= holes + outer rims) of the fused mesh.
// usage: fusetest <in.sproj> <outdir> [--fine] [--global] [--res 0.5] [--save <out.sproj>]
#include <artec/sdk/base/Log.h>
#include <artec/sdk/base/IModel.h>
#include <artec/sdk/base/IScan.h>
#include <artec/sdk/base/IFrameMesh.h>
#include <artec/sdk/base/ICompositeContainer.h>
#include <artec/sdk/base/ICompositeMesh.h>
#include <artec/sdk/base/AlgorithmWorkset.h>
#include <artec/sdk/base/IJob.h>
#include <artec/sdk/base/IArray.h>
#include <artec/sdk/base/Uuid.h>
#include <artec/sdk/base/io/PlyIO.h>
#include <artec/sdk/algorithms/IAlgorithm.h>
#include <artec/sdk/algorithms/Algorithms.h>
#include <artec/sdk/project/IProject.h>
#include <artec/sdk/project/ProjectSettings.h>
#include <artec/sdk/project/EntryInfo.h>
#include <artec/sdk/project/ProjectLoaderSettings.h>
#include <artec/sdk/project/ProjectSaverSettings.h>
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <map>
#include <string>
#include <thread>
#include <chrono>
#include <algorithm>
using namespace artec::sdk::base; namespace prj = artec::sdk::project; namespace alg = artec::sdk::algorithms; using clk = std::chrono::steady_clock;
static double secs(clk::time_point t0) { return std::chrono::duration<double>(clk::now() - t0).count(); }
static int threads() { return (int)std::thread::hardware_concurrency(); }

// boundary statistics of a triangle mesh: boundary edges, loops, perimeters
struct Holes { long long boundaryEdges = 0; int loops = 0; double totalLen = 0; int nSmall = 0, nMedium = 0, nLarge = 0; double largest = 0; };
static Holes holeStats(const IMesh* mesh) {
  Holes h; const IArrayPoint3F* P = mesh->getPoints(); const IArrayIndexTriplet* T = mesh->getTriangles(); if (!P || !T) return h;
  const Point3F* p = P->getPointer(); const IndexTriplet* t = T->getPointer(); int nt = T->getSize();
  std::map<std::pair<int, int>, int> cnt; for (int i = 0; i < nt; i++) for (int e = 0; e < 3; e++) { int a = t[i].data[e], b = t[i].data[(e + 1) % 3]; if (a > b) std::swap(a, b); cnt[{a, b}]++; }
  std::multimap<int, int> adj; std::vector<std::pair<int, int>> be; for (auto& kv : cnt) if (kv.second == 1) { be.push_back(kv.first); adj.insert({kv.first.first, (int)be.size() - 1}); adj.insert({kv.first.second, (int)be.size() - 1}); }
  h.boundaryEdges = (long long)be.size(); std::vector<char> seen(be.size(), 0);
  for (size_t s = 0; s < be.size(); s++) { if (seen[s]) continue; double len = 0; std::vector<int> stack{(int)s}; seen[s] = 1;
    while (!stack.empty()) { int ei = stack.back(); stack.pop_back(); auto& e = be[ei]; double dx = p[e.first].x - p[e.second].x, dy = p[e.first].y - p[e.second].y, dz = p[e.first].z - p[e.second].z; len += sqrt(dx * dx + dy * dy + dz * dz);
      for (int v : {e.first, e.second}) { auto r = adj.equal_range(v); for (auto it = r.first; it != r.second; ++it) if (!seen[it->second]) { seen[it->second] = 1; stack.push_back(it->second); } } }
    h.loops++; h.totalLen += len; if (len > h.largest) h.largest = len; if (len < 6) h.nSmall++; else if (len < 25) h.nMedium++; else h.nLarge++; }
  return h;
}
int wmain(int argc, wchar_t** argv) {
  if (argc < 3) { wprintf(L"usage: fusetest in.sproj outdir [--fine] [--global] [--res 0.5] [--save out.sproj]\n       fusetest --simplify in.ply out.ply [triangles]\n"); return 2; }
  if (std::wstring(argv[1]) == L"--dumpframes") { if (argc < 4) return 2; setOutputLevel(VerboseLevel_Error); int every = argc > 4 ? _wtoi(argv[4]) : 150; SHCreateDirectoryExW(nullptr, argv[3], nullptr);
    prj::IProject* p = nullptr; if (prj::openProject(&p, argv[2]) != ErrorCode_OK || !p) { wprintf(L"openProject failed\n"); return 1; }
    std::vector<Uuid> v; for (int i = 0; i < p->getEntryCount(); i++) { prj::EntryInfo e; if (p->getEntry(i, &e, nullptr) == ErrorCode_OK && e.type == prj::EntryType_Scan) v.push_back(e.uuid); }
    IArrayUuid* ids = nullptr; createArrayUuid(&ids, (int)v.size()); for (int i = 0; i < (int)v.size(); i++) ids->setElement(i, v[i]); prj::ProjectLoaderSettings ls; ls.entryList = ids; IJob* loader = nullptr; p->createLoader(&loader, &ls); IModel* m = nullptr; createModel(&m);
    { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.out = m; ws.threadsCount = threads(); if (executeJob(loader, &ws) != ErrorCode_OK) { wprintf(L"load failed\n"); return 1; } loader->release(); } ids->release(); p->release();
    for (int si = 0; si < m->getSize(); si++) { IScan* sc = m->getElement(si); for (int k = 0; k < sc->getSize(); k += every) { IFrameMesh* f = sc->getElement(k); if (!f) continue; int np = f->getPoints() ? f->getPoints()->getSize() : 0, nt = f->getTriangles() ? f->getTriangles()->getSize() : 0;
        // bounding box of the frame's points and the share of a 2 mm grid filled: a frame with rectangular gaps fills less
        const Point3F* q = f->getPoints()->getPointer(); float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f}; for (int i = 0; i < np; i++) { float c[3] = {q[i].x, q[i].y, q[i].z}; for (int a = 0; a < 3; a++) { lo[a] = std::min(lo[a], c[a]); hi[a] = std::max(hi[a], c[a]); } }
        std::vector<char> cells; int gx = (int)((hi[0] - lo[0]) / 2) + 1, gy = (int)((hi[1] - lo[1]) / 2) + 1; cells.assign((size_t)gx * gy, 0); for (int i = 0; i < np; i++) cells[(size_t)((q[i].y - lo[1]) / 2) * gx + (size_t)((q[i].x - lo[0]) / 2)] = 1; long filled = 0; for (char c : cells) filled += c;
        wchar_t name[64]; swprintf(name, 64, L"\\scan%d-frame%04d.ply", si + 1, k); std::wstring out = std::wstring(argv[3]) + name; ErrorCode ec = io::Ply::save(out.c_str(), f);
        wprintf(L"scan %d frame %d: %d points, %d triangles, extent %.0f x %.0f mm, 2 mm cells filled %ld of %d (%.0f%%), textured %d, ply 0x%08X\n", si + 1, k, np, nt, hi[0] - lo[0], hi[1] - lo[1], filled, gx * gy, 100.0 * filled / (gx * gy), (int)f->isTextured(), (unsigned)ec); } }
    return 0; }
  if (std::wstring(argv[1]) == L"--simplify") { if (argc < 4) return 2; setOutputLevel(VerboseLevel_Error); ICompositeMesh* cm = nullptr; ErrorCode ec = io::Ply::load(&cm, nullptr, argv[2]); if (ec != ErrorCode_OK || !cm) { wprintf(L"load 0x%08X\n", (unsigned)ec); return 1; }
    int target = argc > 4 ? _wtoi(argv[4]) : 1500000; IModel* in = nullptr; createModel(&in); ICompositeContainer* cc0 = nullptr; createCompositeContainer(&cc0); cc0->add(cm); in->setCompositeContainer(cc0); IModel* out = nullptr; createModel(&out);
    wprintf(L"loaded %d points, %d triangles\n", cm->getPoints() ? cm->getPoints()->getSize() : 0, cm->getTriangles() ? cm->getTriangles()->getSize() : 0);
    alg::FastMeshSimplificationSettings ms; alg::initializeFastMeshSimplificationSettings(&ms, ScannerType_Spider); ms.triangleNumber = target; ms.keepBoundary = true; alg::IAlgorithm* sa = nullptr; ec = alg::createFastMeshSimplificationAlgorithm(&sa, &ms);
    if (ec == ErrorCode_OK && sa) { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = in; ws.out = out; ws.threadsCount = threads(); ec = executeJob(sa, &ws); sa->release(); }
    ICompositeContainer* sc = out->getCompositeContainer(); if (ec != ErrorCode_OK || !sc || sc->getSize() < 1) { wprintf(L"simplify 0x%08X\n", (unsigned)ec); return 1; }
    ec = io::Ply::save(argv[3], sc->getElement(0)); wprintf(L"simplified to %d triangles, ply 0x%08X -> %s\n", sc->getElement(0)->getTriangles() ? sc->getElement(0)->getTriangles()->getSize() : 0, (unsigned)ec, argv[3]); return ec == ErrorCode_OK ? 0 : 1; }
  bool fine = false, global = false, fast = false, fillAll = false, align = false; float res = 0.5f, holeR = 0; std::wstring saveTo; std::wstring outdir = argv[2];
  for (int i = 3; i < argc; i++) { std::wstring a = argv[i]; if (a == L"--fine") fine = true; else if (a == L"--align") align = true; else if (a == L"--fast") fast = true; else if (a == L"--holer" && i + 1 < argc) holeR = (float)_wtof(argv[++i]); else if (a == L"--fillall") fillAll = true; else if (a == L"--global") global = true; else if (a == L"--res" && i + 1 < argc) res = (float)_wtof(argv[++i]); else if (a == L"--save" && i + 1 < argc) saveTo = argv[++i]; }
  setOutputLevel(VerboseLevel_Error); SHCreateDirectoryExW(nullptr, outdir.c_str(), nullptr);
  if (!alg::checkAlgorithmsPermission()) { wprintf(L"the SDK algorithms are not permitted on this machine\n"); return 1; }
  auto T0 = clk::now();
  prj::IProject* p = nullptr; if (prj::openProject(&p, argv[1]) != ErrorCode_OK || !p) { wprintf(L"openProject failed\n"); return 1; }
  std::vector<Uuid> v; for (int i = 0; i < p->getEntryCount(); i++) { prj::EntryInfo e; if (p->getEntry(i, &e, nullptr) == ErrorCode_OK && e.type == prj::EntryType_Scan) v.push_back(e.uuid); }
  IArrayUuid* ids = nullptr; createArrayUuid(&ids, (int)v.size()); for (int i = 0; i < (int)v.size(); i++) ids->setElement(i, v[i]); prj::ProjectLoaderSettings ls; ls.entryList = ids; IJob* loader = nullptr; p->createLoader(&loader, &ls); IModel* m = nullptr; createModel(&m);
  { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.out = m; ws.threadsCount = threads(); if (executeJob(loader, &ws) != ErrorCode_OK) { wprintf(L"load failed\n"); return 1; } loader->release(); } ids->release(); p->release();
  int ns = m->getSize(); long frames = 0; for (int i = 0; i < ns; i++) frames += m->getElement(i)->getSize(); ScannerType st = ns ? m->getElement(0)->getScannerType() : ScannerType_Spider;
  wprintf(L"loaded %d scan(s), %ld frames in %.0f s\n", ns, frames, secs(T0));
  // normals for every frame (auto-align and fusion need them; a loaded project has none)
  for (int i = 0; i < ns; i++) { IScan* s = m->getElement(i); for (int k = 0; k < s->getSize(); k++) { IFrameMesh* f = s->getElement(k); if (f && !f->hasPointsNormals()) f->calculate(CM_PointsNormals_Default); } }
  // fine registration of every scan (what Studio does at Stop and ArtecRec's Field mode skips)
  if (fine) { for (int i = 0; i < ns; i++) { IScan* s = m->getElement(i); int nf = s->getSize(); bool allTex = true; for (int k = 0; k < nf; k++) { IFrameMesh* f = s->getElement(k); if (!f || !f->isTextured()) { allTex = false; break; } }
      IModel* in = nullptr; createModel(&in); IModel* out = nullptr; createModel(&out); in->add(s);
      alg::SerialRegistrationSettings sd; sd.scannerType = s->getScannerType(); sd.registrationType = allTex ? alg::SerialRegistrationType_FineTextured : alg::SerialRegistrationType_Fine; alg::IAlgorithm* a = nullptr; ErrorCode ec = alg::createSerialRegistrationAlgorithm(&a, &sd);
      auto t0 = clk::now(); if (ec == ErrorCode_OK && a) { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = in; ws.out = out; ws.threadsCount = threads(); ec = executeJob(a, &ws); a->release(); }
      if (ec == ErrorCode_OK && out->getSize() == 1 && out->getElement(0)->getSize() == nf) { IScan* r = out->getElement(0); r->setScanTransformation(s->getScanTransformation()); if (s->getName()) r->setName(s->getName()); m->setElement(i, r); wprintf(L"scan %d: fine registration (%s) of %d frames in %.0f s\n", i + 1, allTex ? L"geometry+texture" : L"geometry", nf, secs(t0)); }
      else wprintf(L"scan %d: fine registration failed 0x%08X in %.0f s; kept as it was\n", i + 1, (unsigned)ec, secs(t0));
      in->release(); out->release(); } }
  // auto-align: place the passes against each other (Studio's Align; ArtecRec's Edit > Auto-align). Field mode leaves
  // passes where the scanner started them, so this is the step that closes mm-to-cm offsets between passes.
  if (align && ns >= 2) { IModel* out = nullptr; createModel(&out); alg::AutoAlignSettings as; as.scannerType = st; alg::IAlgorithm* a = nullptr; ErrorCode ec = alg::createAutoalignAlgorithm(&a, &as);
    auto t0 = clk::now(); if (ec == ErrorCode_OK && a) { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = m; ws.out = out; ws.threadsCount = threads(); ec = executeJob(a, &ws); a->release(); }
    if (ec == ErrorCode_OK && out->getSize() == ns) { for (int i = 0; i < ns; i++) { const auto& A = m->getElement(i)->getScanTransformation().m; const auto& B = out->getElement(i)->getScanTransformation().m; double d = sqrt((A[0][3] - B[0][3]) * (A[0][3] - B[0][3]) + (A[1][3] - B[1][3]) * (A[1][3] - B[1][3]) + (A[2][3] - B[2][3]) * (A[2][3] - B[2][3])); wprintf(L"auto-align: scan %d moved %.1f mm\n", i + 1, d); IScan* r = out->getElement(i); if (m->getElement(i)->getName()) r->setName(m->getElement(i)->getName()); m->setElement(i, r); } wprintf(L"auto-align of %d scans in %.0f s\n", ns, secs(t0)); }
    else wprintf(L"auto-align failed 0x%08X in %.0f s; scans kept as they were\n", (unsigned)ec, secs(t0)); out->release(); }
  // global registration of the set
  if (global && ns >= 2) { IModel* out = nullptr; createModel(&out); alg::GlobalRegistrationSettings gs; gs.scannerType = st; gs.registrationType = alg::GlobalRegistrationType_Geometry; alg::IAlgorithm* a = nullptr; ErrorCode ec = alg::createGlobalRegistrationAlgorithm(&a, &gs);
    auto t0 = clk::now(); if (ec == ErrorCode_OK && a) { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = m; ws.out = out; ws.threadsCount = threads(); ec = executeJob(a, &ws); a->release(); }
    if (ec == ErrorCode_OK && out->getSize() == ns) { for (int i = 0; i < ns; i++) { IScan* r = out->getElement(i); if (m->getElement(i)->getName()) r->setName(m->getElement(i)->getName()); m->setElement(i, r); } wprintf(L"global registration of %d scans in %.0f s\n", ns, secs(t0)); }
    else wprintf(L"global registration failed 0x%08X in %.0f s; scans kept as they were\n", (unsigned)ec, secs(t0)); out->release(); }
  // save the processed project under the new name
  if (!saveTo.empty()) { std::wstring dir = saveTo.substr(0, saveTo.find_last_of(L"\\/")); SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    prj::ProjectSettings ps; ps.path = saveTo.c_str(); prj::IProject* q = nullptr; ErrorCode ec = prj::createNewProject(&q, &ps);
    if (ec == ErrorCode_OK && q) { prj::ProjectSaverSettings ss; ss.path = saveTo.c_str(); generateUuid(&ss.projectId); ss.compressionLevel = 0; IJob* saver = nullptr; ec = q->createSaver(&saver, &ss);
      auto t0 = clk::now(); if (ec == ErrorCode_OK && saver) { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = m; ws.threadsCount = threads(); ec = executeJob(saver, &ws); saver->release(); } q->release(); wprintf(L"saved project 0x%08X in %.0f s -> %s\n", (unsigned)ec, secs(t0), saveTo.c_str()); }
    else wprintf(L"createNewProject 0x%08X\n", (unsigned)ec); }
  // fusion: Poisson, sharp, holes not filled (ByRadius with radius 0), same settings for every run
  IModel* fused = nullptr; createModel(&fused); alg::PoissonFusionSettings fs; alg::initializePoissonFusionSettings(&fs, st); fs.fusionType = alg::PoissonFusionType_Sharp; fs.fillType = alg::FillHolesType_ByRadius; fs.maxHoleRadius = holeR; fs.resolution = res; fs.removeTargets = false;
  if (fillAll) fs.fillType = alg::FillHolesType_All;
  alg::IAlgorithm* fa = nullptr; ErrorCode ec; if (fast) { alg::FastFusionSettings ff; alg::initializeFastFusionSettings(&ff, st); ff.resolution = res; ff.generateNormals = true; ec = alg::createFastFusionAlgorithm(&fa, &ff); } else ec = alg::createPoissonFusionAlgorithm(&fa, &fs); auto t0 = clk::now();
  if (ec == ErrorCode_OK && fa) { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = m; ws.out = fused; ws.threadsCount = threads(); ec = executeJob(fa, &ws); fa->release(); }
  wprintf(L"fusion job 0x%08X: out model scans %d, composite container %s with %d element(s); in model composite %d\n", (unsigned)ec, fused->getSize(), fused->getCompositeContainer() ? L"present" : L"absent", fused->getCompositeContainer() ? fused->getCompositeContainer()->getSize() : -1, m->getCompositeContainer() ? m->getCompositeContainer()->getSize() : -1);
  ICompositeContainer* cc = fused->getCompositeContainer(); if ((!cc || cc->getSize() < 1) && m->getCompositeContainer() && m->getCompositeContainer()->getSize() > 0) cc = m->getCompositeContainer();
  if (ec != ErrorCode_OK || !cc || cc->getSize() < 1) { wprintf(L"fusion failed 0x%08X in %.0f s\n", (unsigned)ec, secs(t0)); return 1; }
  ICompositeMesh* mesh = cc->getElement(0); int np = mesh->getPoints() ? mesh->getPoints()->getSize() : 0, nt = mesh->getTriangles() ? mesh->getTriangles()->getSize() : 0;
  wprintf(L"fusion (%s, %.2f mm, hole filling up to %.1f mm): %d points, %d triangles in %.0f s\n", fast ? L"fast" : fillAll ? L"sharp, all holes filled" : L"sharp", res, fast ? 0.f : fillAll ? 9999.f : holeR, np, nt, secs(t0));
  Holes h = holeStats(mesh);
  wprintf(L"RESULT %s%s%s: boundary loops %d (holes ~%d), boundary edges %lld, total rim length %.0f mm, largest loop %.0f mm; loops under 6 mm %d, 6-25 mm %d, over 25 mm %d\n", fine ? L"fine " : L"raw ", align ? L"+align" : L"", global ? L"+global" : L"", h.loops, std::max(0, h.loops - 1), h.boundaryEdges, h.totalLen, h.largest, h.nSmall, h.nMedium, h.nLarge);
  std::wstring ply = outdir + L"\\fused-" + (fast ? L"fast-" : L"") + (fine ? L"fine" : L"raw") + (align ? L"-align" : L"") + (global ? L"-global" : L"") + L".ply"; ec = io::Ply::save(ply.c_str(), mesh); wprintf(L"ply 0x%08X -> %s\n", (unsigned)ec, ply.c_str());
  // a lighter copy for viewing (about 1.5 M triangles)
  if (nt > 2000000) { IModel* small2 = nullptr; createModel(&small2); alg::FastMeshSimplificationSettings ms; alg::initializeFastMeshSimplificationSettings(&ms, st); ms.triangleNumber = 1500000; ms.keepBoundary = true; alg::IAlgorithm* sa = nullptr; ErrorCode e2 = alg::createFastMeshSimplificationAlgorithm(&sa, &ms); auto t1 = clk::now();
    if (e2 == ErrorCode_OK && sa) { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = fused; ws.out = small2; ws.threadsCount = threads(); e2 = executeJob(sa, &ws); sa->release(); }
    ICompositeContainer* sc = small2->getCompositeContainer(); if (e2 == ErrorCode_OK && sc && sc->getSize() > 0) { std::wstring sp = ply.substr(0, ply.size() - 4) + L"-view.ply"; e2 = io::Ply::save(sp.c_str(), sc->getElement(0)); wprintf(L"view copy %d triangles, ply 0x%08X in %.0f s -> %s\n", sc->getElement(0)->getTriangles() ? sc->getElement(0)->getTriangles()->getSize() : 0, (unsigned)e2, secs(t1), sp.c_str()); }
    else wprintf(L"simplification failed 0x%08X\n", (unsigned)e2); small2->release(); }
  fused->release(); m->release(); wprintf(L"total %.0f s\n", secs(T0)); return 0;
}
