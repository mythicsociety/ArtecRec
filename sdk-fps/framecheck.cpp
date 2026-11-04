// framecheck — per-frame consistency inside one scan: for every frame, the median distance of its sampled points to
// the nearest point of the OTHER frames of the same scan (grid hash, 6 mm cap), plus its share within 1 mm, its point
// count and its longest-edge statistics. Outlier frames (bad registration, noise slivers) stand out.
// usage: framecheck <project.sproj> <scanIndex(1-based)>
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
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <thread>
using namespace artec::sdk::base; namespace prj = artec::sdk::project;
struct P3 { float x, y, z; };
static void mul(const double A[4][4], const double B[4][4], double o[4][4]) { for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { double s = 0; for (int k = 0; k < 4; k++) s += A[i][k] * B[k][j]; o[i][j] = s; } }
struct Grid { float cell; std::unordered_map<long long, std::vector<int>> cells; std::vector<P3> pts; std::vector<int> frame;
  long long key(int ix, int iy, int iz) const { return ((long long)ix * 73856093LL) ^ ((long long)iy * 19349663LL) ^ ((long long)iz * 83492791LL); }
  void add(const P3& p, int f) { pts.push_back(p); frame.push_back(f); cells[key((int)floorf(p.x / cell), (int)floorf(p.y / cell), (int)floorf(p.z / cell))].push_back((int)pts.size() - 1); }
  float nearestOther(const P3& q, int f, float maxd) const { int ix = (int)floorf(q.x / cell), iy = (int)floorf(q.y / cell), iz = (int)floorf(q.z / cell); int r = (int)ceilf(maxd / cell); float best = maxd * maxd;
    for (int dx = -r; dx <= r; dx++) for (int dy = -r; dy <= r; dy++) for (int dz = -r; dz <= r; dz++) { auto it = cells.find(key(ix + dx, iy + dy, iz + dz)); if (it == cells.end()) continue; for (int i : it->second) { if (frame[i] == f) continue; float a = pts[i].x - q.x, b = pts[i].y - q.y, c2 = pts[i].z - q.z; float d = a * a + b * b + c2 * c2; if (d < best) best = d; } }
    return sqrtf(best); } };
int wmain(int argc, wchar_t** argv) {
  if (argc < 3) { wprintf(L"usage: framecheck project.sproj scanIndex\n"); return 2; } int want = _wtoi(argv[2]) - 1; setOutputLevel(VerboseLevel_Error);
  prj::IProject* p = nullptr; if (prj::openProject(&p, argv[1]) != ErrorCode_OK || !p) { wprintf(L"openProject failed\n"); return 1; }
  std::vector<Uuid> v; for (int i = 0; i < p->getEntryCount(); i++) { prj::EntryInfo e; if (p->getEntry(i, &e, nullptr) == ErrorCode_OK && e.type == prj::EntryType_Scan) v.push_back(e.uuid); }
  if (want < 0 || want >= (int)v.size()) { wprintf(L"scan index out of range (%d scans)\n", (int)v.size()); return 1; }
  IArrayUuid* ids = nullptr; createArrayUuid(&ids, 1); ids->setElement(0, v[want]);
  prj::ProjectLoaderSettings ls; ls.entryList = ids; IJob* loader = nullptr; p->createLoader(&loader, &ls); IModel* m = nullptr; createModel(&m);
  { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.out = m; ws.threadsCount = std::thread::hardware_concurrency(); if (executeJob(loader, &ws) != ErrorCode_OK) { wprintf(L"load failed\n"); return 1; } loader->release(); } ids->release(); p->release();
  IScan* s = m->getElement(0); int nf = s->getSize(); const auto& ST = s->getScanTransformation().m; Grid g; g.cell = 2.0f;
  struct FS { int pts = 0, tri = 0, longTri = 0; float med = 0, within1 = 0; bool tex = false; }; std::vector<FS> fs(nf);
  for (int k = 0; k < nf; k++) { IFrameMesh* f = s->getElement(k); if (!f) continue; IArrayPoint3F* pts = f->getPoints(); if (!pts) continue; double M[4][4]; mul(ST, s->getTransformation(k).m, M); int n = pts->getSize(); const Point3F* q = pts->getPointer(); fs[k].pts = n; fs[k].tex = f->isTextured();
    for (int i = 0; i < n; i += 5) { double x = q[i].x, y = q[i].y, z = q[i].z; g.add({(float)(M[0][0] * x + M[0][1] * y + M[0][2] * z + M[0][3]), (float)(M[1][0] * x + M[1][1] * y + M[1][2] * z + M[1][3]), (float)(M[2][0] * x + M[2][1] * y + M[2][2] * z + M[2][3])}, k); }
    IArrayIndexTriplet* tri = f->getTriangles(); if (tri) { int nt = tri->getSize(); const IndexTriplet* t = tri->getPointer(); fs[k].tri = nt; for (int i = 0; i < nt; i++) { auto d2 = [&](unsigned a, unsigned b) { float dx = q[a].x - q[b].x, dy = q[a].y - q[b].y, dz = q[a].z - q[b].z; return dx * dx + dy * dy + dz * dz; }; if (d2(t[i].x, t[i].y) > 16 || d2(t[i].y, t[i].z) > 16 || d2(t[i].z, t[i].x) > 16) fs[k].longTri++; } } }
  for (int k = 0; k < nf; k++) { std::vector<float> d; int c1 = 0; for (size_t i = 0; i < g.pts.size(); i++) { if (g.frame[i] != k) continue; float dd = g.nearestOther(g.pts[i], k, 6.0f); d.push_back(dd); if (dd < 1.0f) c1++; } if (d.empty()) continue; std::sort(d.begin(), d.end()); fs[k].med = d[d.size() / 2]; fs[k].within1 = (float)c1 / d.size(); }
  wprintf(L"scan %d: %d frames. frame  points  tris  longTris(>4mm)  tex  median-dist-to-other-frames  within1mm\n", want + 1, nf); int bad = 0;
  for (int k = 0; k < nf; k++) { bool flag = fs[k].med > 1.0f || fs[k].within1 < 0.5f || fs[k].longTri * 10 > fs[k].tri; if (flag) bad++; wprintf(L"%s%4d %7d %6d %6d %5s %6.2f mm %5.0f%%\n", flag ? L"!! " : L"   ", k + 1, fs[k].pts, fs[k].tri, fs[k].longTri, fs[k].tex ? L"yes" : L"no", fs[k].med, fs[k].within1 * 100); }
  wprintf(L"flagged frames: %d of %d\n", bad, nf); m->release(); return 0;
}
