// noisepts — what are the stray points inside ArtecRec frames? Per frame, in FRAME coordinates (camera at the
// origin looking down -z), 1 mm cells in xy; a point is "stray" when it sits more than 1 mm from its cell's median
// depth while the cell holds >= 4 points. Reports the stray share and how strays differ from the rest: scanning
// distance, grazing angle (cos between the normal and the view ray), and whether they sit at the frame border
// (cells with < 5 of 8 neighbours occupied). Optionally writes a filtered copy of the project where points failing
// a chosen rule are removed from every frame (triangles remapped), so zspread can measure the effect.
// usage: noisepts <in.sproj> <scanIndex> [out.sproj minCos farMax]   e.g. minCos 0.3 farMax 330
#include <artec/sdk/base/Log.h>
#include <artec/sdk/base/IModel.h>
#include <artec/sdk/base/IScan.h>
#include <artec/sdk/base/IFrameMesh.h>
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
#include <cmath>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <thread>
using namespace artec::sdk::base; namespace prj = artec::sdk::project;
int wmain(int argc, wchar_t** argv) {
  if (argc < 3) { wprintf(L"usage: noisepts in.sproj scanIndex [out.sproj minCos farMax]\n"); return 2; } int want = _wtoi(argv[2]) - 1; bool filt = argc >= 6; double minCos = filt ? _wtof(argv[4]) : 0, farMax = filt ? _wtof(argv[5]) : 1e9; setOutputLevel(VerboseLevel_Error);
  prj::IProject* p = nullptr; if (prj::openProject(&p, argv[1]) != ErrorCode_OK || !p) { wprintf(L"openProject failed\n"); return 1; }
  std::vector<Uuid> v; for (int i = 0; i < p->getEntryCount(); i++) { prj::EntryInfo e; if (p->getEntry(i, &e, nullptr) == ErrorCode_OK && e.type == prj::EntryType_Scan) v.push_back(e.uuid); } if (want < 0 || want >= (int)v.size()) { wprintf(L"scan index out of range\n"); return 1; }
  IArrayUuid* ids = nullptr; createArrayUuid(&ids, (int)v.size()); for (int i = 0; i < (int)v.size(); i++) ids->setElement(i, v[i]); prj::ProjectLoaderSettings ls; ls.entryList = ids; IJob* loader = nullptr; p->createLoader(&loader, &ls); IModel* m = nullptr; createModel(&m);
  { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.out = m; ws.threadsCount = std::thread::hardware_concurrency(); if (executeJob(loader, &ws) != ErrorCode_OK) { wprintf(L"load failed\n"); return 1; } loader->release(); } ids->release(); p->release();
  for (int si = 0; si < m->getSize(); si++) { if (!filt && si != want) continue; IScan* s = m->getElement(si); int nf = s->getSize();
    long long tot = 0, stray = 0, strayBorder = 0, allBorder = 0; double dStray = 0, dAll = 0, cStray = 0, cAll = 0; long long cN = 0, cNs = 0; int grazeStray = 0, grazeAll = 0, farStray = 0, farAll = 0; long long removed = 0;
    for (int k = 0; k < nf; k++) { IFrameMesh* f = s->getElement(k); if (!f || !f->getPoints()) continue; if (!f->hasPointsNormals()) f->calculate(CM_PointsNormals_Default); IArrayPoint3F* pts = f->getPoints(); IArrayPoint3F* nr = f->getPointsNormals(); int n = pts->getSize(); const Point3F* q = pts->getPointer(); const Point3F* qn = nr && nr->getSize() == n ? nr->getPointer() : nullptr;
      std::unordered_map<long long, std::vector<int>> cells; auto key = [](int x, int y) { return ((long long)x << 32) ^ (unsigned)y; }; std::vector<int> cx(n), cy(n);
      for (int i = 0; i < n; i++) { cx[i] = (int)floorf(q[i].x); cy[i] = (int)floorf(q[i].y); cells[key(cx[i], cy[i])].push_back(i); }
      std::vector<char> isStray(n, 0), isBorder(n, 0);
      for (auto& kv : cells) { auto& ix = kv.second; int cxx = (int)(kv.first >> 32), cyy = (int)(unsigned)(kv.first & 0xffffffff); int nb = 0; for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) { if (!dx && !dy) continue; if (cells.count(key(cxx + dx, cyy + dy))) nb++; } bool border = nb < 5;
        std::vector<float> z; for (int i : ix) z.push_back(q[i].z); std::sort(z.begin(), z.end()); float med = z[z.size() / 2];
        for (int i : ix) { if (border) isBorder[i] = 1; if (ix.size() >= 4 && fabs(q[i].z - med) > 1.0f) isStray[i] = 1; } }
      // Studio-like frame filters (env NOISE_EDGE mm, NOISE_MINCOMP triangles): triangles with a long edge are dropped,
      // then connected components smaller than NOISE_MINCOMP triangles, then points no triangle uses.
      static double edgeMax = _wgetenv(L"NOISE_EDGE") ? _wtof(_wgetenv(L"NOISE_EDGE")) : 0; static int minComp = _wgetenv(L"NOISE_MINCOMP") ? _wtoi(_wgetenv(L"NOISE_MINCOMP")) : 0; std::vector<char> usedPt(n, 0); std::vector<char> triKeep;
      IArrayIndexTriplet* tri0 = f->getTriangles(); if (filt && (edgeMax > 0 || minComp > 0) && tri0) { const IndexTriplet* t = tri0->getPointer(); int nt = tri0->getSize(); triKeep.assign(nt, 1);
        if (edgeMax > 0) { double e2 = edgeMax * edgeMax; for (int i = 0; i < nt; i++) { auto d2 = [&](unsigned a, unsigned b) { double dx = q[a].x - q[b].x, dy = q[a].y - q[b].y, dz = q[a].z - q[b].z; return dx * dx + dy * dy + dz * dz; }; if (t[i].x >= (unsigned)n || t[i].y >= (unsigned)n || t[i].z >= (unsigned)n || d2(t[i].x, t[i].y) > e2 || d2(t[i].y, t[i].z) > e2 || d2(t[i].z, t[i].x) > e2) triKeep[i] = 0; } }
        if (minComp > 0) { std::vector<int> parent(n); for (int i = 0; i < n; i++) parent[i] = i; auto find = [&](int a) { while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; } return a; };
          for (int i = 0; i < nt; i++) if (triKeep[i]) { int a = find(t[i].x), b = find(t[i].y), c = find(t[i].z); parent[a] = b; parent[find(b)] = find(c); }
          std::unordered_map<int, int> compTris; for (int i = 0; i < nt; i++) if (triKeep[i]) compTris[find(t[i].x)]++; for (int i = 0; i < nt; i++) if (triKeep[i] && compTris[find(t[i].x)] < minComp) triKeep[i] = 0; }
        for (int i = 0; i < nt; i++) if (triKeep[i]) { usedPt[t[i].x] = usedPt[t[i].y] = usedPt[t[i].z] = 1; } }
      std::vector<int> keep(n, -1); int nk = 0;
      for (int i = 0; i < n; i++) { double d = -q[i].z; double c = 1; if (qn) { double L = sqrt((double)q[i].x * q[i].x + (double)q[i].y * q[i].y + (double)q[i].z * q[i].z); if (L > 0) c = fabs((qn[i].x * q[i].x + qn[i].y * q[i].y + qn[i].z * q[i].z) / L); }
        tot++; dAll += d; cAll += c; cN++; if (isBorder[i]) allBorder++; if (c < 0.3) grazeAll++; if (d > 330) farAll++;
        if (isStray[i]) { stray++; dStray += d; cStray += c; cNs++; if (isBorder[i]) strayBorder++; if (c < 0.3) grazeStray++; if (d > 330) farStray++; }
        bool drop = filt && (c < minCos || d > farMax || (!triKeep.empty() && !usedPt[i])); if (!drop) keep[i] = nk++; else removed++; }
      if (filt && nk < n) {   // rebuild the frame's points/normals/uv/triangles with the kept subset
        IArrayPoint3F* np = nullptr; createArrayPoint3F(&np, nk); IArrayPoint3F* nn = nullptr; if (qn) createArrayPoint3F(&nn, nk); IArrayUVCoordinates* uv = f->isTextured() ? f->getUVCoordinates() : nullptr; IArrayUVCoordinates* nuv = nullptr; if (uv && uv->getSize() == n) createArrayUVCoordinates(&nuv, nk);
        Point3F* wp = np->getPointer(); Point3F* wn = nn ? nn->getPointer() : nullptr; UVCoordinates* wuv = nuv ? nuv->getPointer() : nullptr; const UVCoordinates* ouv = nuv ? uv->getPointer() : nullptr;
        for (int i = 0; i < n; i++) if (keep[i] >= 0) { wp[keep[i]] = q[i]; if (wn) wn[keep[i]] = qn[i]; if (wuv) wuv[keep[i]] = ouv[i]; }
        IArrayIndexTriplet* tri = f->getTriangles(); std::vector<IndexTriplet> nt; if (tri) { const IndexTriplet* t = tri->getPointer(); for (int i = 0; i < tri->getSize(); i++) { if (!triKeep.empty() && !triKeep[i]) continue; if (t[i].x < (unsigned)n && t[i].y < (unsigned)n && t[i].z < (unsigned)n && keep[t[i].x] >= 0 && keep[t[i].y] >= 0 && keep[t[i].z] >= 0) { IndexTriplet o; o.x = keep[t[i].x]; o.y = keep[t[i].y]; o.z = keep[t[i].z]; nt.push_back(o); } } }
        IArrayIndexTriplet* ntri = nullptr; createArrayIndexTriplet(&ntri, (int)nt.size()); { IndexTriplet* wt = ntri->getPointer(); for (size_t i = 0; i < nt.size(); i++) wt[i] = nt[i]; }
        f->setPoints(np); if (nn) f->setPointsNormals(nn); if (nuv) f->setUVCoordinates(nuv); f->setTriangles(ntri); np->release(); if (nn) nn->release(); if (nuv) nuv->release(); ntri->release(); } }
    wprintf(L"scan %d: %d frames, %lld points, stray (second level inside the frame) %.2f%%\n", si + 1, nf, tot, 100.0 * stray / tot);
    wprintf(L"  mean scanning distance: strays %.0f mm, all %.0f mm; beyond 330 mm: strays %.1f%%, all %.1f%%\n", cNs ? dStray / cNs : 0, cN ? dAll / cN : 0, stray ? 100.0 * farStray / stray : 0, 100.0 * farAll / tot);
    wprintf(L"  grazing (cos < 0.3): strays %.1f%%, all %.1f%%; mean cos strays %.2f, all %.2f\n", stray ? 100.0 * grazeStray / stray : 0, 100.0 * grazeAll / tot, cNs ? cStray / cNs : 0, cN ? cAll / cN : 0);
    wprintf(L"  at the frame border: strays %.1f%%, all %.1f%%\n", stray ? 100.0 * strayBorder / stray : 0, 100.0 * allBorder / tot);
    if (filt) wprintf(L"  filter minCos %.2f farMax %.0f removed %.1f%% of points\n", minCos, farMax, 100.0 * removed / tot); }
  if (filt) { std::wstring outp = argv[3]; std::wstring dir = outp.substr(0, outp.find_last_of(L"\\/")); SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    prj::ProjectSettings ps; ps.path = outp.c_str(); prj::IProject* po = nullptr; ErrorCode ec = prj::createNewProject(&po, &ps); if (ec != ErrorCode_OK) { wprintf(L"createNewProject 0x%08X\n", (unsigned)ec); return 1; }
    prj::ProjectSaverSettings ss; ss.path = outp.c_str(); generateUuid(&ss.projectId); ss.compressionLevel = 0; IJob* saver = nullptr; ec = po->createSaver(&saver, &ss);
    AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = m; ws.threadsCount = std::thread::hardware_concurrency(); ec = executeJob(saver, &ws); saver->release(); po->release(); wprintf(L"saved 0x%08X -> %ls\n", (unsigned)ec, outp.c_str()); }
  m->release(); return 0;
}
