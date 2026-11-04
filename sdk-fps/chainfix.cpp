// chainfix — frame-to-model re-registration of one scan (offline experiment, 4 Nov): the SDK's serial and global
// registration leave a slow depth drift (~0.25 mm/frame, zspread 20 % of cells >2 mm); Studio's tracker does not.
// Here every frame k is re-placed by ICP against the merged points of the previously placed frames k-W..k-1 (W=12),
// which averages frame noise and stops the random walk. Point-to-plane ICP (normals from the model's local planes)
// with a Kabsch linearised solve; 12 rounds, nearest within 2 mm. The result is saved as a new project.
// usage: chainfix <in.sproj> <scanIndex(1-based)> <out.sproj>
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
static void invRigid(const double M[4][4], double o[4][4]) { for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) o[i][j] = M[j][i]; for (int i = 0; i < 3; i++) o[i][3] = -(o[i][0] * M[0][3] + o[i][1] * M[1][3] + o[i][2] * M[2][3]); o[3][0] = o[3][1] = o[3][2] = 0; o[3][3] = 1; }
// model: points + normals in world coords, grid for nearest lookup
struct Model { std::vector<float> pts, nrm; feat::Grid g; void build() { std::vector<float> c = pts; g.build(std::move(c), 1.0f); }
  bool nearest(const float* q, float maxd, const float*& p, const float*& n) const { int ix = (int)floorf(q[0] / g.cell), iy = (int)floorf(q[1] / g.cell), iz = (int)floorf(q[2] / g.cell); int r = (int)ceilf(maxd / g.cell); float best = maxd * maxd; int bi = -1;
    for (int dx = -r; dx <= r; dx++) for (int dy = -r; dy <= r; dy++) for (int dz = -r; dz <= r; dz++) { long long k = feat::Grid::key(ix + dx, iy + dy, iz + dz); auto lo = std::lower_bound(g.idx.begin(), g.idx.end(), std::make_pair(k, -1)); for (; lo != g.idx.end() && lo->first == k; ++lo) { const float* w = &g.pts[lo->second * 3]; float d = (w[0] - q[0]) * (w[0] - q[0]) + (w[1] - q[1]) * (w[1] - q[1]) + (w[2] - q[2]) * (w[2] - q[2]); if (d < best) { best = d; bi = lo->second; } } }
    if (bi < 0) return false; p = &g.pts[bi * 3]; n = &nrm[bi * 3]; return true; } };
// point-to-plane ICP step: solve the 6-dof small motion (rotation vector w, translation t) minimising sum (n.(R p + t - q))^2
static bool p2pStep(const std::vector<float>& P, const Model& M, double R[3][3], double t[3], float maxd, double& rms, int& matched) {
  double A[6][6] = {}, b[6] = {}; matched = 0; double se = 0; int n = (int)P.size() / 3;
  for (int i = 0; i < n; i++) { const float* p0 = &P[i * 3]; float p[3]; for (int r = 0; r < 3; r++) p[r] = (float)(R[r][0] * p0[0] + R[r][1] * p0[1] + R[r][2] * p0[2] + t[r]); const float* q; const float* nn; if (!M.nearest(p, maxd, q, nn)) continue;
    double d = nn[0] * (p[0] - q[0]) + nn[1] * (p[1] - q[1]) + nn[2] * (p[2] - q[2]); double c[3] = {p[1] * nn[2] - p[2] * nn[1], p[2] * nn[0] - p[0] * nn[2], p[0] * nn[1] - p[1] * nn[0]}; double J[6] = {c[0], c[1], c[2], nn[0], nn[1], nn[2]};
    for (int r = 0; r < 6; r++) { b[r] -= J[r] * d; for (int cc = 0; cc < 6; cc++) A[r][cc] += J[r] * J[cc]; } matched++; se += d * d; }
  if (matched < 200) return false; rms = sqrt(se / matched);
  // solve A x = b (Gaussian elimination)
  double Mx[6][7]; for (int r = 0; r < 6; r++) { for (int cc = 0; cc < 6; cc++) Mx[r][cc] = A[r][cc]; Mx[r][6] = b[r]; }
  for (int c = 0; c < 6; c++) { int piv = c; for (int r = c + 1; r < 6; r++) if (fabs(Mx[r][c]) > fabs(Mx[piv][c])) piv = r; if (fabs(Mx[piv][c]) < 1e-12) return false; if (piv != c) for (int j = 0; j < 7; j++) std::swap(Mx[c][j], Mx[piv][j]); for (int r = 0; r < 6; r++) { if (r == c) continue; double f = Mx[r][c] / Mx[c][c]; for (int j = c; j < 7; j++) Mx[r][j] -= f * Mx[c][j]; } }
  double x[6]; for (int r = 0; r < 6; r++) x[r] = Mx[r][6] / Mx[r][r];
  // apply: R' = Rot(w) R, t' = Rot(w) t + dt   (small-angle rotation about the origin of world coords)
  double w[3] = {x[0], x[1], x[2]}, th = sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]); double K[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  if (th > 1e-12) { double k[3] = {w[0] / th, w[1] / th, w[2] / th}, c = cos(th), s = sin(th); double kx[3][3] = {{0, -k[2], k[1]}, {k[2], 0, -k[0]}, {-k[1], k[0], 0}}; for (int r = 0; r < 3; r++) for (int cc = 0; cc < 3; cc++) K[r][cc] = (r == cc ? 1 : 0) * c + s * kx[r][cc] + (1 - c) * k[r] * k[cc]; }
  double R2[3][3], t2[3]; for (int r = 0; r < 3; r++) { for (int cc = 0; cc < 3; cc++) R2[r][cc] = K[r][0] * R[0][cc] + K[r][1] * R[1][cc] + K[r][2] * R[2][cc]; t2[r] = K[r][0] * t[0] + K[r][1] * t[1] + K[r][2] * t[2] + x[3 + r]; }
  memcpy(R, R2, sizeof R2); memcpy(t, t2, sizeof t2); return true;
}
int wmain(int argc, wchar_t** argv) {
  if (argc < 4) { wprintf(L"usage: chainfix in.sproj scanIndex out.sproj\n"); return 2; } int want = _wtoi(argv[2]) - 1; setOutputLevel(VerboseLevel_Error);
  prj::IProject* p = nullptr; if (prj::openProject(&p, argv[1]) != ErrorCode_OK || !p) { wprintf(L"openProject failed\n"); return 1; }
  std::vector<Uuid> v; for (int i = 0; i < p->getEntryCount(); i++) { prj::EntryInfo e; if (p->getEntry(i, &e, nullptr) == ErrorCode_OK && e.type == prj::EntryType_Scan) v.push_back(e.uuid); } if (want < 0 || want >= (int)v.size()) { wprintf(L"scan index out of range\n"); return 1; }
  IArrayUuid* ids = nullptr; createArrayUuid(&ids, (int)v.size()); for (int i = 0; i < (int)v.size(); i++) ids->setElement(i, v[i]); prj::ProjectLoaderSettings ls; ls.entryList = ids; IJob* loader = nullptr; p->createLoader(&loader, &ls); IModel* m = nullptr; createModel(&m);
  { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.out = m; ws.threadsCount = std::thread::hardware_concurrency(); if (executeJob(loader, &ws) != ErrorCode_OK) { wprintf(L"load failed\n"); return 1; } loader->release(); } ids->release(); p->release();
  IScan* s = m->getElement(want); int nf = s->getSize(); for (int k = 0; k < nf; k++) { IFrameMesh* f = s->getElement(k); if (f && !f->hasPointsNormals()) f->calculate(CM_PointsNormals_Default); }
  const int W = 12; auto t0 = clk::now(); std::vector<Matrix4x4D> newT(nf); for (int k = 0; k < nf; k++) newT[k] = s->getTransformation(k); double moved = 0; int fixed = 0;
  for (int k = 1; k < nf; k++) { IFrameMesh* f = s->getElement(k); if (!f || !f->getPoints()) continue;
    // model from the previous W placed frames (every 2nd point), with their normals, in world (scan-local) coords
    Model M; for (int j = std::max(0, k - W); j < k; j++) { IFrameMesh* g = s->getElement(j); if (!g || !g->getPoints() || !g->getPointsNormals()) continue; const Point3F* q = g->getPoints()->getPointer(); const Point3F* qn = g->getPointsNormals()->getPointer(); int n = g->getPoints()->getSize(); const auto& T = newT[j].m;
      for (int i = 0; i < n; i += 2) { double x = q[i].x, y = q[i].y, z = q[i].z; M.pts.push_back((float)(T[0][0] * x + T[0][1] * y + T[0][2] * z + T[0][3])); M.pts.push_back((float)(T[1][0] * x + T[1][1] * y + T[1][2] * z + T[1][3])); M.pts.push_back((float)(T[2][0] * x + T[2][1] * y + T[2][2] * z + T[2][3]));
        double a = qn[i].x, bb = qn[i].y, c = qn[i].z; M.nrm.push_back((float)(T[0][0] * a + T[0][1] * bb + T[0][2] * c)); M.nrm.push_back((float)(T[1][0] * a + T[1][1] * bb + T[1][2] * c)); M.nrm.push_back((float)(T[2][0] * a + T[2][1] * bb + T[2][2] * c)); } }
    if (M.pts.size() < 3000) continue; M.build();
    // frame k points in frame coords (every 3rd); initial pose = its current transform
    std::vector<float> P; { const Point3F* q = f->getPoints()->getPointer(); int n = f->getPoints()->getSize(); for (int i = 0; i < n; i += 3) { P.push_back(q[i].x); P.push_back(q[i].y); P.push_back(q[i].z); } }
    double R[3][3], t[3]; const auto& T = newT[k].m; for (int r = 0; r < 3; r++) { for (int c = 0; c < 3; c++) R[r][c] = T[r][c]; t[r] = T[r][3]; }
    double rms = 0; int matched = 0; bool ok = true; for (int it = 0; it < 12 && ok; it++) ok = p2pStep(P, M, R, t, it < 4 ? 2.0f : 1.0f, rms, matched); if (!ok) continue;
    Matrix4x4D N; for (int r = 0; r < 3; r++) { for (int c = 0; c < 3; c++) N.m[r][c] = R[r][c]; N.m[r][3] = t[r]; } N.m[3][0] = N.m[3][1] = N.m[3][2] = 0; N.m[3][3] = 1;
    double dz = N.m[2][3] - T[2][3], dx = N.m[0][3] - T[0][3], dy = N.m[1][3] - T[1][3]; moved += sqrt(dx * dx + dy * dy + dz * dz); fixed++; newT[k] = N; }
  for (int k = 0; k < nf; k++) s->setTransformation(k, newT[k]);
  wprintf(L"re-placed %d of %d frames in %.1f s, mean move %.2f mm\n", fixed, nf, std::chrono::duration<double>(clk::now() - t0).count(), fixed ? moved / fixed : 0);
  std::wstring outp = argv[3]; std::wstring dir = outp.substr(0, outp.find_last_of(L"\\/")); SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
  prj::ProjectSettings ps; ps.path = outp.c_str(); prj::IProject* po = nullptr; ErrorCode ec = prj::createNewProject(&po, &ps); if (ec != ErrorCode_OK) { wprintf(L"createNewProject 0x%08X\n", (unsigned)ec); return 1; }
  prj::ProjectSaverSettings ss; ss.path = outp.c_str(); generateUuid(&ss.projectId); ss.compressionLevel = 0; IJob* saver = nullptr; ec = po->createSaver(&saver, &ss);
  AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = m; ws.threadsCount = std::thread::hardware_concurrency(); ec = executeJob(saver, &ws); saver->release(); po->release(); wprintf(L"saved 0x%08X -> %ls\n", (unsigned)ec, outp.c_str()); m->release(); return 0;
}
