// passalign — pass-to-pass refinement (offline experiment, 4 Nov): the live hybrid placement puts a new pass 4–7 mm off
// in depth and the SDK global registration does not correct it. Here each scan i>0 is refined as one rigid body by
// point-to-plane ICP against the merged points of the scans before it (which are already placed), 15 rounds, nearest
// within 6 mm then 2 mm. The scan transformation is updated; the result is saved as a new project.
// usage: passalign <in.sproj> <out.sproj>
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
#include <algorithm>
#include <thread>
#include <chrono>
#include "rec_feat.h"
using namespace artec::sdk::base; namespace prj = artec::sdk::project; using clk = std::chrono::steady_clock;
static void mul(const double A[4][4], const double B[4][4], double o[4][4]) { for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { double s = 0; for (int k = 0; k < 4; k++) s += A[i][k] * B[k][j]; o[i][j] = s; } }
struct Model { std::vector<float> pts, nrm; feat::Grid g; void build() { std::vector<float> c = pts; g.build(std::move(c), 3.0f); }   // 3 mm cells: a 6 mm search touches 125 cells, not 2197 (the 1059 s run)
  bool nearest(const float* q, float maxd, const float*& p, const float*& n) const { int ix = (int)floorf(q[0] / g.cell), iy = (int)floorf(q[1] / g.cell), iz = (int)floorf(q[2] / g.cell); int r = std::min(5, (int)ceilf(maxd / g.cell)); float best = maxd * maxd; int bi = -1;
    for (int dx = -r; dx <= r; dx++) for (int dy = -r; dy <= r; dy++) for (int dz = -r; dz <= r; dz++) { long long k = feat::Grid::key(ix + dx, iy + dy, iz + dz); auto lo = std::lower_bound(g.idx.begin(), g.idx.end(), std::make_pair(k, -1)); for (; lo != g.idx.end() && lo->first == k; ++lo) { const float* w = &g.pts[lo->second * 3]; float d = (w[0] - q[0]) * (w[0] - q[0]) + (w[1] - q[1]) * (w[1] - q[1]) + (w[2] - q[2]) * (w[2] - q[2]); if (d < best) { best = d; bi = lo->second; } } }
    if (bi < 0) return false; p = &g.pts[bi * 3]; n = &nrm[bi * 3]; return true; } };
static bool p2pStep(const std::vector<float>& P, const Model& M, double R[3][3], double t[3], float maxd, double& rms, int& matched) {
  double A[6][6] = {}, b[6] = {}; matched = 0; double se = 0; int n = (int)P.size() / 3;
  for (int i = 0; i < n; i++) { const float* p0 = &P[i * 3]; float p[3]; for (int r = 0; r < 3; r++) p[r] = (float)(R[r][0] * p0[0] + R[r][1] * p0[1] + R[r][2] * p0[2] + t[r]); const float* q; const float* nn; if (!M.nearest(p, maxd, q, nn)) continue;
    double d = nn[0] * (p[0] - q[0]) + nn[1] * (p[1] - q[1]) + nn[2] * (p[2] - q[2]); double c[3] = {p[1] * nn[2] - p[2] * nn[1], p[2] * nn[0] - p[0] * nn[2], p[0] * nn[1] - p[1] * nn[0]}; double J[6] = {c[0], c[1], c[2], nn[0], nn[1], nn[2]};
    for (int r = 0; r < 6; r++) { b[r] -= J[r] * d; for (int cc = 0; cc < 6; cc++) A[r][cc] += J[r] * J[cc]; } matched++; se += d * d; }
  if (matched < 500) return false; rms = sqrt(se / matched);
  double Mx[6][7]; for (int r = 0; r < 6; r++) { for (int cc = 0; cc < 6; cc++) Mx[r][cc] = A[r][cc]; Mx[r][6] = b[r]; }
  for (int c = 0; c < 6; c++) { int piv = c; for (int r = c + 1; r < 6; r++) if (fabs(Mx[r][c]) > fabs(Mx[piv][c])) piv = r; if (fabs(Mx[piv][c]) < 1e-12) return false; if (piv != c) for (int j = 0; j < 7; j++) std::swap(Mx[c][j], Mx[piv][j]); for (int r = 0; r < 6; r++) { if (r == c) continue; double f = Mx[r][c] / Mx[c][c]; for (int j = c; j < 7; j++) Mx[r][j] -= f * Mx[c][j]; } }
  double x[6]; for (int r = 0; r < 6; r++) x[r] = Mx[r][6] / Mx[r][r];
  double w[3] = {x[0], x[1], x[2]}, th = sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]); double K[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  if (th > 1e-12) { double k[3] = {w[0] / th, w[1] / th, w[2] / th}, c = cos(th), s = sin(th); double kx[3][3] = {{0, -k[2], k[1]}, {k[2], 0, -k[0]}, {-k[1], k[0], 0}}; for (int r = 0; r < 3; r++) for (int cc = 0; cc < 3; cc++) K[r][cc] = (r == cc ? 1 : 0) * c + s * kx[r][cc] + (1 - c) * k[r] * k[cc]; }
  double R2[3][3], t2[3]; for (int r = 0; r < 3; r++) { for (int cc = 0; cc < 3; cc++) R2[r][cc] = K[r][0] * R[0][cc] + K[r][1] * R[1][cc] + K[r][2] * R[2][cc]; t2[r] = K[r][0] * t[0] + K[r][1] * t[1] + K[r][2] * t[2] + x[3 + r]; }
  memcpy(R, R2, sizeof R2); memcpy(t, t2, sizeof t2); return true;
}
int wmain(int argc, wchar_t** argv) {
  if (argc < 3) { wprintf(L"usage: passalign in.sproj out.sproj\n"); return 2; } setOutputLevel(VerboseLevel_Error);
  prj::IProject* p = nullptr; if (prj::openProject(&p, argv[1]) != ErrorCode_OK || !p) { wprintf(L"openProject failed\n"); return 1; }
  std::vector<Uuid> v; for (int i = 0; i < p->getEntryCount(); i++) { prj::EntryInfo e; if (p->getEntry(i, &e, nullptr) == ErrorCode_OK && e.type == prj::EntryType_Scan) v.push_back(e.uuid); }
  IArrayUuid* ids = nullptr; createArrayUuid(&ids, (int)v.size()); for (int i = 0; i < (int)v.size(); i++) ids->setElement(i, v[i]); prj::ProjectLoaderSettings ls; ls.entryList = ids; IJob* loader = nullptr; p->createLoader(&loader, &ls); IModel* m = nullptr; createModel(&m);
  { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.out = m; ws.threadsCount = std::thread::hardware_concurrency(); if (executeJob(loader, &ws) != ErrorCode_OK) { wprintf(L"load failed\n"); return 1; } loader->release(); } ids->release(); p->release();
  int ns = m->getSize(); wprintf(L"%d scans\n", ns); for (int si = 0; si < ns; si++) { IScan* s = m->getElement(si); for (int k = 0; k < s->getSize(); k++) { IFrameMesh* f = s->getElement(k); if (f && !f->hasPointsNormals()) f->calculate(CM_PointsNormals_Default); } }
  auto worldPts = [&](IScan* s, int step, bool withN, std::vector<float>& P, std::vector<float>* Nn) { const auto& ST = s->getScanTransformation().m; for (int k = 0; k < s->getSize(); k++) { IFrameMesh* f = s->getElement(k); if (!f || !f->getPoints()) continue; double M[4][4]; mul(ST, s->getTransformation(k).m, M); const Point3F* q = f->getPoints()->getPointer(); const Point3F* qn = withN && f->getPointsNormals() ? f->getPointsNormals()->getPointer() : nullptr; int n = f->getPoints()->getSize();
      for (int i = 0; i < n; i += step) { double x = q[i].x, y = q[i].y, z = q[i].z; P.push_back((float)(M[0][0] * x + M[0][1] * y + M[0][2] * z + M[0][3])); P.push_back((float)(M[1][0] * x + M[1][1] * y + M[1][2] * z + M[1][3])); P.push_back((float)(M[2][0] * x + M[2][1] * y + M[2][2] * z + M[2][3]));
        if (Nn && qn) { double a = qn[i].x, b = qn[i].y, c = qn[i].z; Nn->push_back((float)(M[0][0] * a + M[0][1] * b + M[0][2] * c)); Nn->push_back((float)(M[1][0] * a + M[1][1] * b + M[1][2] * c)); Nn->push_back((float)(M[2][0] * a + M[2][1] * b + M[2][2] * c)); } } } };
  for (int si = 1; si < ns; si++) { auto t0 = clk::now(); Model M; for (int j = 0; j < si; j++) worldPts(m->getElement(j), 8, true, M.pts, &M.nrm); M.build();
    IScan* s = m->getElement(si); std::vector<float> P; worldPts(s, 30, false, P, nullptr);   // world coords under the CURRENT scan transform; the ICP finds the correction C: world -> world
    double R[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, t[3] = {0, 0, 0}; double rms = 0; int matched = 0; bool ok = true; for (int it = 0; it < 18 && ok; it++) ok = p2pStep(P, M, R, t, it < 4 ? 15.0f : it < 9 ? 6.0f : 2.0f, rms, matched);   // scan 3 of Trail12 sat 7 mm off: a 6 mm first cap found only 108 points
    if (!ok) { wprintf(L"scan %d: refinement failed (matched %d)\n", si + 1, matched); continue; }
    // scale check (Umeyama on the matched pairs after the rigid fit): a pass larger/smaller than the earlier ones shows as s != 1
    { std::vector<float> a, b; int n = (int)P.size() / 3; for (int i = 0; i < n; i++) { const float* p0 = &P[i * 3]; float p[3]; for (int r = 0; r < 3; r++) p[r] = (float)(R[r][0] * p0[0] + R[r][1] * p0[1] + R[r][2] * p0[2] + t[r]); const float* q; const float* nn; if (!M.nearest(p, 2.0f, q, nn)) continue; a.insert(a.end(), p, p + 3); b.insert(b.end(), q, q + 3); }
      int k = (int)a.size() / 3; double ca[3] = {0, 0, 0}, cb[3] = {0, 0, 0}; for (int i = 0; i < k; i++) for (int r = 0; r < 3; r++) { ca[r] += a[i * 3 + r]; cb[r] += b[i * 3 + r]; } for (int r = 0; r < 3; r++) { ca[r] /= k; cb[r] /= k; }
      double num = 0, den = 0; for (int i = 0; i < k; i++) { double da[3], db[3]; for (int r = 0; r < 3; r++) { da[r] = a[i * 3 + r] - ca[r]; db[r] = b[i * 3 + r] - cb[r]; } num += da[0] * db[0] + da[1] * db[1] + da[2] * db[2]; den += da[0] * da[0] + da[1] * da[1] + da[2] * da[2]; }
      wprintf(L"scan %d: best scale against the earlier passes %.4f (%d matched points, overlap extent %.0f mm)\n", si + 1, den > 0 ? num / den : 1, k, k ? sqrt(den / k) * 2 : 0); }
    // new scan transform = C * ST
    const auto& ST = s->getScanTransformation().m; double C[4][4]; for (int r = 0; r < 3; r++) { for (int c = 0; c < 3; c++) C[r][c] = R[r][c]; C[r][3] = t[r]; } C[3][0] = C[3][1] = C[3][2] = 0; C[3][3] = 1; double N[4][4]; mul(C, ST, N); Matrix4x4D Mx; memcpy(Mx.m, N, sizeof N); s->setScanTransformation(Mx);
    wprintf(L"scan %d: moved by (%.2f %.2f %.2f) mm, residual RMS %.2f mm over %d points, %.1f s\n", si + 1, t[0], t[1], t[2], rms, matched, std::chrono::duration<double>(clk::now() - t0).count()); }
  std::wstring outp = argv[2]; std::wstring dir = outp.substr(0, outp.find_last_of(L"\\/")); SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
  prj::ProjectSettings ps; ps.path = outp.c_str(); prj::IProject* po = nullptr; ErrorCode ec = prj::createNewProject(&po, &ps); if (ec != ErrorCode_OK) { wprintf(L"createNewProject 0x%08X\n", (unsigned)ec); return 1; }
  prj::ProjectSaverSettings ss; ss.path = outp.c_str(); generateUuid(&ss.projectId); ss.compressionLevel = 0; IJob* saver = nullptr; ec = po->createSaver(&saver, &ss);
  AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = m; ws.threadsCount = std::thread::hardware_concurrency(); ec = executeJob(saver, &ws); saver->release(); po->release(); wprintf(L"saved 0x%08X -> %ls\n", (unsigned)ec, outp.c_str()); m->release(); return 0;
}
