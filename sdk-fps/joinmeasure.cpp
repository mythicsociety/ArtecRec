// joinmeasure — how well do the scans of a project sit on each other? For every scan B after the first, sample its
// points, find the nearest point of the earlier scans within 8 mm (grid hash, cell 2 mm), and report the median and
// 90th-percentile distance of the overlap, plus the share of B that overlaps at all. Numbers, not pictures.
// usage: joinmeasure <project.sproj>
#include <artec/sdk/base/Log.h>
#include <artec/sdk/base/IModel.h>
#include <artec/sdk/base/IScan.h>
#include <artec/sdk/base/IFrameMesh.h>
#include <artec/sdk/base/AlgorithmWorkset.h>
#include <artec/sdk/base/IJob.h>
#include <artec/sdk/base/IArray.h>
#include <artec/sdk/base/IString.h>
#include <artec/sdk/base/Uuid.h>
#include <artec/sdk/project/IProject.h>
#include <artec/sdk/project/ProjectSettings.h>
#include <artec/sdk/project/EntryInfo.h>
#include <artec/sdk/project/ProjectLoaderSettings.h>
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
static std::vector<P3> worldPoints(IScan* s, int step) {
  std::vector<P3> out; const auto& ST = s->getScanTransformation().m;
  for (int k = 0; k < s->getSize(); k++) { IFrameMesh* f = s->getElement(k); if (!f) continue; IArrayPoint3F* pts = f->getPoints(); if (!pts) continue; double M[4][4]; mul(ST, s->getTransformation(k).m, M);
    int n = pts->getSize(); const Point3F* p = pts->getPointer(); for (int i = 0; i < n; i += step) { double x = p[i].x, y = p[i].y, z = p[i].z; out.push_back({(float)(M[0][0]*x + M[0][1]*y + M[0][2]*z + M[0][3]), (float)(M[1][0]*x + M[1][1]*y + M[1][2]*z + M[1][3]), (float)(M[2][0]*x + M[2][1]*y + M[2][2]*z + M[2][3])}); } }
  return out;
}
struct Grid { float cell; std::unordered_map<long long, std::vector<int>> cells; std::vector<P3> pts;
  long long key(int ix, int iy, int iz) const { return ((long long)ix * 73856093LL) ^ ((long long)iy * 19349663LL) ^ ((long long)iz * 83492791LL); }
  void build(const std::vector<P3>& p, float c) { cell = c; pts = p; for (int i = 0; i < (int)p.size(); i++) cells[key((int)floorf(p[i].x / c), (int)floorf(p[i].y / c), (int)floorf(p[i].z / c))].push_back(i); }
  float nearest(const P3& q, float maxd) const { int ix = (int)floorf(q.x / cell), iy = (int)floorf(q.y / cell), iz = (int)floorf(q.z / cell); int r = (int)ceilf(maxd / cell); float best = maxd * maxd;
    for (int dx = -r; dx <= r; dx++) for (int dy = -r; dy <= r; dy++) for (int dz = -r; dz <= r; dz++) { auto it = cells.find(key(ix + dx, iy + dy, iz + dz)); if (it == cells.end()) continue; for (int i : it->second) { float a = pts[i].x - q.x, b = pts[i].y - q.y, c2 = pts[i].z - q.z; float d = a * a + b * b + c2 * c2; if (d < best) best = d; } }
    return sqrtf(best); } };
int wmain(int argc, wchar_t** argv) {
  if (argc < 2) { wprintf(L"usage: joinmeasure project.sproj\n"); return 2; } setOutputLevel(VerboseLevel_Error);
  prj::IProject* p = nullptr; ErrorCode ec = prj::openProject(&p, argv[1]); if (ec != ErrorCode_OK || !p) { wprintf(L"openProject 0x%08X\n", (unsigned)ec); return 1; }
  int n = p->getEntryCount(); std::vector<Uuid> v; std::vector<std::wstring> names; for (int i = 0; i < n; i++) { prj::EntryInfo e; IString* nm = nullptr; if (p->getEntry(i, &e, &nm) == ErrorCode_OK && e.type == prj::EntryType_Scan) { v.push_back(e.uuid); names.push_back(nm ? nm->getPointer() : L"?"); } if (nm) nm->release(); }
  IArrayUuid* ids = nullptr; createArrayUuid(&ids, (int)v.size()); for (int i = 0; i < (int)v.size(); i++) ids->setElement(i, v[i]);
  prj::ProjectLoaderSettings ls; ls.entryList = ids; IJob* loader = nullptr; ec = p->createLoader(&loader, &ls); IModel* m = nullptr; createModel(&m);
  { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.out = m; ws.threadsCount = std::thread::hardware_concurrency(); ec = executeJob(loader, &ws); loader->release(); } ids->release(); p->release();
  if (ec != ErrorCode_OK) { wprintf(L"load 0x%08X\n", (unsigned)ec); return 1; }
  wprintf(L"%d scans\n", m->getSize()); std::vector<P3> earlier;
  for (int i = 0; i < m->getSize(); i++) { IScan* s = m->getElement(i); std::vector<P3> pts = worldPoints(s, 7); double cx = 0, cy = 0, cz = 0; for (auto& q : pts) { cx += q.x; cy += q.y; cz += q.z; } if (!pts.empty()) { cx /= pts.size(); cy /= pts.size(); cz /= pts.size(); }
    if (i == 0) { wprintf(L"scan 1 \"%ls\": %d frames, %zu sample points, centre %.0f %.0f %.0f mm (reference)\n", i < (int)names.size() ? names[i].c_str() : L"?", s->getSize(), pts.size(), cx, cy, cz); earlier = pts; continue; }
    Grid g; g.build(earlier, 2.0f); std::vector<float> d; for (auto& q : pts) { float dd = g.nearest(q, 8.0f); if (dd < 8.0f) d.push_back(dd); }
    std::sort(d.begin(), d.end()); float med = d.empty() ? -1 : d[d.size() / 2], p90 = d.empty() ? -1 : d[(size_t)(d.size() * 0.9)];
    wprintf(L"scan %d \"%ls\": %d frames, %zu sample points, centre %.0f %.0f %.0f mm; overlap with earlier scans %.0f%% of its points; join distance median %.2f mm, 90th pct %.2f mm\n", i + 1, i < (int)names.size() ? names[i].c_str() : L"?", s->getSize(), pts.size(), cx, cy, cz, pts.empty() ? 0.0 : 100.0 * d.size() / pts.size(), med, p90);
    earlier.insert(earlier.end(), pts.begin(), pts.end()); }
  m->release(); return 0;
}
