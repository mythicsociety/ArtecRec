// ArtecRec — image-feature position search ("humans use texture to identify places", owner, 4 Nov 2026).
// The SDK's auto-align is geometry-only and fails on flat carved stone. Every textured frame carries a colour image and
// the UV mapping from image pixels to its 3D points, so the place a frame shows can be recognised by its image:
//   1. ORB-style features on a 320-px-wide grey copy of the texture (FAST-9 corners, intensity-centroid orientation,
//      256-bit rotated BRIEF descriptors);
//   2. each keypoint is given the 3D point of the nearest textured vertex (UV lookup grid);
//   3. the latest probe frame is matched (Hamming + ratio test) against every reference frame of every earlier scan;
//   4. a 3-point RANSAC rigid fit on the 3D correspondences gives the probe frame's pose in world; the SDK's textured
//      global registration then tightens it (rec_sdk.h).
#pragma once
#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <intrin.h>

namespace feat {
struct Image { int w = 0, h = 0; std::vector<uint8_t> g, b;   // g: grey (corners); b: 5x5 box-blurred grey (descriptors, as ORB does)
  uint8_t at(int x, int y) const { return g[(size_t)y * w + x]; } uint8_t bl(int x, int y) const { return b[(size_t)y * w + x]; } };
static void blur5(Image& im) { im.b.assign(im.g.size(), 0); std::vector<int> tmp((size_t)im.w * im.h);
  for (int y = 0; y < im.h; y++) for (int x = 0; x < im.w; x++) { int s = 0, n = 0; for (int d = -2; d <= 2; d++) { int xx = x + d; if (xx >= 0 && xx < im.w) { s += im.g[(size_t)y * im.w + xx]; n++; } } tmp[(size_t)y * im.w + x] = s * 16 / n; }
  for (int y = 0; y < im.h; y++) for (int x = 0; x < im.w; x++) { int s = 0, n = 0; for (int d = -2; d <= 2; d++) { int yy = y + d; if (yy >= 0 && yy < im.h) { s += tmp[(size_t)yy * im.w + x]; n++; } } im.b[(size_t)y * im.w + x] = (uint8_t)(s / n / 16); } }
struct Frame { std::vector<float> pts;   // 3 per keypoint, frame coordinates (mm)
               std::vector<float> kp;    // 2 per keypoint, pixel coords in the grey image
               std::vector<uint8_t> desc; // 32 bytes per keypoint
               int n() const { return (int)kp.size() / 2; }
               // append another frame's keypoints with its points mapped through M (4x4 row-major) — builds a window
               void append(const Frame& o, const double M[4][4]) { for (int i = 0; i < o.n(); i++) { const float* p = &o.pts[i * 3];
                   pts.push_back((float)(M[0][0] * p[0] + M[0][1] * p[1] + M[0][2] * p[2] + M[0][3])); pts.push_back((float)(M[1][0] * p[0] + M[1][1] * p[1] + M[1][2] * p[2] + M[1][3])); pts.push_back((float)(M[2][0] * p[0] + M[2][1] * p[1] + M[2][2] * p[2] + M[2][3])); }
                 kp.insert(kp.end(), o.kp.begin(), o.kp.end()); desc.insert(desc.end(), o.desc.begin(), o.desc.end()); } };
// point grid for the geometric check of a find: nearest-neighbour distance within maxd
struct Grid { float cell = 2.f; std::vector<float> pts; std::vector<std::pair<long long, int>> idx;   // sorted (cellkey, point) pairs
  static long long key(int ix, int iy, int iz) { return ((long long)ix * 73856093LL) ^ ((long long)iy * 19349663LL) ^ ((long long)iz * 83492791LL); }
  void build(std::vector<float>&& p, float c) { cell = c; pts = std::move(p); int n = (int)pts.size() / 3; idx.resize(n); for (int i = 0; i < n; i++) idx[i] = {key((int)floorf(pts[i * 3] / c), (int)floorf(pts[i * 3 + 1] / c), (int)floorf(pts[i * 3 + 2] / c)), i}; std::sort(idx.begin(), idx.end()); }
  float nearest(float x, float y, float z, float maxd) const { int ix = (int)floorf(x / cell), iy = (int)floorf(y / cell), iz = (int)floorf(z / cell); int r = (int)ceilf(maxd / cell); float best = maxd * maxd;
    for (int dx = -r; dx <= r; dx++) for (int dy = -r; dy <= r; dy++) for (int dz = -r; dz <= r; dz++) { long long k = key(ix + dx, iy + dy, iz + dz); auto it = std::lower_bound(idx.begin(), idx.end(), std::make_pair(k, -1)); for (; it != idx.end() && it->first == k; ++it) { const float* q = &pts[it->second * 3]; float a = q[0] - x, b = q[1] - y, c2 = q[2] - z; float d = a * a + b * b + c2 * c2; if (d < best) best = d; } }
    return sqrtf(best); }
  // share of the given points (after R,t) within maxd of the grid, and their median distance
  void check(const std::vector<float>& p, const double R[3][3], const double t[3], float maxd, float& share, float& median) const { std::vector<float> d; int n = (int)p.size() / 3;
    for (int i = 0; i < n; i++) { const float* q = &p[i * 3]; float x = (float)(R[0][0] * q[0] + R[0][1] * q[1] + R[0][2] * q[2] + t[0]), y = (float)(R[1][0] * q[0] + R[1][1] * q[1] + R[1][2] * q[2] + t[1]), z = (float)(R[2][0] * q[0] + R[2][1] * q[1] + R[2][2] * q[2] + t[2]); float dd = nearest(x, y, z, maxd); if (dd < maxd) d.push_back(dd); }
    share = n ? (float)d.size() / n : 0; if (d.empty()) { median = maxd; return; } std::sort(d.begin(), d.end()); median = d[d.size() / 2]; } };

// grey image downsampled to ~targetW px wide from a BGR/RGB/mono buffer
static Image toGrey(const uint8_t* p, int w, int h, int pitch, int ch, int targetW = 320) {
  Image im; int f = w >= 2 * targetW ? w / targetW : 1; if (f < 1) f = 1; im.w = w / f; im.h = h / f; im.g.resize((size_t)im.w * im.h);
  for (int y = 0; y < im.h; y++) for (int x = 0; x < im.w; x++) { int s = 0; for (int dy = 0; dy < f; dy++) { const uint8_t* row = p + (size_t)(y * f + dy) * pitch; for (int dx = 0; dx < f; dx++) { const uint8_t* q = row + (size_t)(x * f + dx) * ch; s += ch == 1 ? q[0] : (q[0] + 2 * q[1] + q[2]) / 4; } } im.g[(size_t)y * im.w + x] = (uint8_t)(s / (f * f)); }
  blur5(im); return im;
}
// FAST-9 corner test with score = sum of abs differences over the arc
static const int FAST_DX[16] = {0, 1, 2, 3, 3, 3, 2, 1, 0, -1, -2, -3, -3, -3, -2, -1};
static const int FAST_DY[16] = {-3, -3, -2, -1, 0, 1, 2, 3, 3, 3, 2, 1, 0, -1, -2, -3};
static int fastScore(const Image& im, int x, int y, int t) {
  int c = im.at(x, y); int v[16]; for (int i = 0; i < 16; i++) v[i] = im.at(x + FAST_DX[i], y + FAST_DY[i]) - c;
  for (int sign = -1; sign <= 1; sign += 2) { int run = 0, best = 0, sum = 0; for (int i = 0; i < 32; i++) { int d = v[i & 15] * sign; if (d > t) { run++; if (run > best) best = run; } else run = 0; }
    if (best >= 9) { for (int i = 0; i < 16; i++) { int d = v[i] * sign; if (d > t) sum += d; } return sum; } }
  return 0;
}
struct KP { int x, y, score; float angle; };
static std::vector<KP> fastCorners(const Image& im, int thresh, int maxN) {
  std::vector<int> sc((size_t)im.w * im.h, 0); std::vector<KP> out;
  for (int y = 16; y < im.h - 16; y++) for (int x = 16; x < im.w - 16; x++) sc[(size_t)y * im.w + x] = fastScore(im, x, y, thresh);
  for (int y = 17; y < im.h - 17; y++) for (int x = 17; x < im.w - 17; x++) { int s = sc[(size_t)y * im.w + x]; if (!s) continue; bool mx = true;
    for (int dy = -1; dy <= 1 && mx; dy++) for (int dx = -1; dx <= 1; dx++) if ((dx || dy) && sc[(size_t)(y + dy) * im.w + x + dx] > s) { mx = false; break; }
    if (mx) out.push_back({x, y, s, 0.f}); }
  std::sort(out.begin(), out.end(), [](const KP& a, const KP& b) { return a.score > b.score; }); if ((int)out.size() > maxN) out.resize(maxN);
  // orientation: intensity centroid in a radius-7 disc
  for (auto& k : out) { double m01 = 0, m10 = 0; for (int dy = -7; dy <= 7; dy++) for (int dx = -7; dx <= 7; dx++) { if (dx * dx + dy * dy > 49) continue; int v = im.at(k.x + dx, k.y + dy); m10 += dx * v; m01 += dy * v; } k.angle = (float)atan2(m01, m10); }
  return out;
}
// 256 BRIEF test pairs, fixed pseudo-random pattern inside a 31x31 patch (generated once, same for every frame)
struct Pattern { int8_t p[256][4]; Pattern() { uint32_t s = 0x9E3779B9u; auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (int)((s >> 8) % 25) - 12; }; for (int i = 0; i < 256; i++) { p[i][0] = (int8_t)rnd(); p[i][1] = (int8_t)rnd(); p[i][2] = (int8_t)rnd(); p[i][3] = (int8_t)rnd(); } } };
static const Pattern& pattern() { static Pattern P; return P; }
static void describe(const Image& im, const std::vector<KP>& kps, std::vector<uint8_t>& desc) {
  const Pattern& P = pattern(); desc.assign(kps.size() * 32, 0);
  for (size_t i = 0; i < kps.size(); i++) { float c = cosf(kps[i].angle), s = sinf(kps[i].angle); uint8_t* d = &desc[i * 32];
    for (int b = 0; b < 256; b++) { int x1 = (int)lroundf(P.p[b][0] * c - P.p[b][1] * s), y1 = (int)lroundf(P.p[b][0] * s + P.p[b][1] * c), x2 = (int)lroundf(P.p[b][2] * c - P.p[b][3] * s), y2 = (int)lroundf(P.p[b][2] * s + P.p[b][3] * c);
      int ax = kps[i].x + x1, ay = kps[i].y + y1, bx = kps[i].x + x2, by = kps[i].y + y2; if (ax < 0 || ay < 0 || bx < 0 || by < 0 || ax >= im.w || ay >= im.h || bx >= im.w || by >= im.h) continue;
      if (im.bl(ax, ay) < im.bl(bx, by)) d[b >> 3] |= (uint8_t)(1 << (b & 7)); } }
}
static int hamming(const uint8_t* a, const uint8_t* b) { uint64_t x[4], y[4]; memcpy(x, a, 32); memcpy(y, b, 32); return (int)(__popcnt64(x[0] ^ y[0]) + __popcnt64(x[1] ^ y[1]) + __popcnt64(x[2] ^ y[2]) + __popcnt64(x[3] ^ y[3])); }
// Build a frame's feature set. uv: per-vertex texture coordinates (u right, v down? the SDK stores v as in the image
// rows — both conventions are tried by the caller through flipV). pts: vertices in frame coordinates.
static void build(const Image& im, const float* uv, const float* pts, int nv, bool flipV, Frame& out) {
  std::vector<KP> kps = fastCorners(im, 18, 500); std::vector<uint8_t> desc; describe(im, kps, desc);
  // UV grid: cell lists of vertex indices (64 x 48 cells over the image)
  const int GX = 64, GY = 48; std::vector<std::vector<int>> grid((size_t)GX * GY);
  for (int i = 0; i < nv; i++) { float u = uv[i * 2], v = uv[i * 2 + 1]; if (flipV) v = 1.f - v; if (u < 0 || u >= 1 || v < 0 || v >= 1) continue; grid[(size_t)(int)(v * GY) * GX + (int)(u * GX)].push_back(i); }
  out.pts.clear(); out.kp.clear(); out.desc.clear();
  for (size_t k = 0; k < kps.size(); k++) { float u = (kps[k].x + 0.5f) / im.w, v = (kps[k].y + 0.5f) / im.h; int gx = (int)(u * GX), gy = (int)(v * GY); int best = -1; float bd = 1e9f;
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) { int cx = gx + dx, cy = gy + dy; if (cx < 0 || cy < 0 || cx >= GX || cy >= GY) continue; for (int i : grid[(size_t)cy * GX + cx]) { float du = uv[i * 2] - u, dv = (flipV ? 1.f - uv[i * 2 + 1] : uv[i * 2 + 1]) - v; float d = du * du + dv * dv; if (d < bd) { bd = d; best = i; } } }
    if (best < 0 || bd > (2.0f / im.w) * (2.0f / im.w)) continue;   // no vertex within ~2 px: the corner lies on a hole
    out.pts.push_back(pts[best * 3]); out.pts.push_back(pts[best * 3 + 1]); out.pts.push_back(pts[best * 3 + 2]); out.kp.push_back((float)kps[k].x); out.kp.push_back((float)kps[k].y); out.desc.insert(out.desc.end(), desc.begin() + k * 32, desc.begin() + k * 32 + 32); }
}
// matches: indices (a in A, b in B) with Hamming < 48 and ratio test 0.8
static std::vector<std::pair<int, int>> match(const Frame& A, const Frame& B) {
  std::vector<std::pair<int, int>> m; int nb = B.n(); if (nb < 2) return m;
  for (int i = 0; i < A.n(); i++) { int b1 = 256, b2 = 256, j1 = -1; const uint8_t* da = &A.desc[(size_t)i * 32]; for (int j = 0; j < nb; j++) { int d = hamming(da, &B.desc[(size_t)j * 32]); if (d < b1) { b2 = b1; b1 = d; j1 = j; } else if (d < b2) b2 = d; }
    if (j1 >= 0 && b1 < 64 && b1 < 0.8 * b2) m.push_back({i, j1}); }
  return m;
}
// rigid transform from 3 point pairs (orthonormal frames), then Kabsch-style refinement on inliers via polar iteration
static bool rigidFrom3(const float* a0, const float* a1, const float* a2, const float* b0, const float* b1, const float* b2, double R[3][3], double t[3]) {
  auto basis = [](const float* p0, const float* p1, const float* p2, double B[3][3], double c[3]) { double u[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]}, v[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
    double lu = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]); if (lu < 1e-6) return false; for (int i = 0; i < 3; i++) u[i] /= lu; double d = u[0] * v[0] + u[1] * v[1] + u[2] * v[2]; for (int i = 0; i < 3; i++) v[i] -= d * u[i]; double lv = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (lv < 1e-6) return false; for (int i = 0; i < 3; i++) v[i] /= lv;
    double w[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]}; for (int i = 0; i < 3; i++) { B[i][0] = u[i]; B[i][1] = v[i]; B[i][2] = w[i]; c[i] = (p0[i] + p1[i] + p2[i]) / 3.0; } return true; };
  double A[3][3], B[3][3], ca[3], cb[3]; if (!basis(a0, a1, a2, A, ca) || !basis(b0, b1, b2, B, cb)) return false;
  for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { double s = 0; for (int k = 0; k < 3; k++) s += B[i][k] * A[j][k]; R[i][j] = s; }   // R = B * A^T
  for (int i = 0; i < 3; i++) t[i] = cb[i] - (R[i][0] * ca[0] + R[i][1] * ca[1] + R[i][2] * ca[2]); return true;
}
static void refineRigid(const std::vector<float>& a, const std::vector<float>& b, const std::vector<int>& idx, double R[3][3], double t[3]) {
  int n = (int)idx.size(); if (n < 3) return; double ca[3] = {0, 0, 0}, cb[3] = {0, 0, 0};
  for (int i : idx) for (int k = 0; k < 3; k++) { ca[k] += a[i * 3 + k]; cb[k] += b[i * 3 + k]; } for (int k = 0; k < 3; k++) { ca[k] /= n; cb[k] /= n; }
  double H[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}; for (int i : idx) for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) H[r][c] += (b[i * 3 + r] - cb[r]) * (a[i * 3 + c] - ca[c]);
  // polar decomposition of H by Newton iteration: X <- (X + X^-T)/2
  // polar factor of H; normalise first (the Newton iteration below starts from H itself, and with thousands of
  // points H ~ 1e6, which needs ~20 halvings before it even starts converging — found 4 Nov by pairfit)
  double X[3][3]; memcpy(X, H, sizeof X); { double fn = 0; for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) fn += X[r][c] * X[r][c]; fn = sqrt(fn); if (fn < 1e-12) return; for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) X[r][c] /= fn; }
  for (int it = 0; it < 30; it++) { double d = X[0][0] * (X[1][1] * X[2][2] - X[1][2] * X[2][1]) - X[0][1] * (X[1][0] * X[2][2] - X[1][2] * X[2][0]) + X[0][2] * (X[1][0] * X[2][1] - X[1][1] * X[2][0]); if (fabs(d) < 1e-12) return;
    double inv[3][3] = {{(X[1][1] * X[2][2] - X[1][2] * X[2][1]) / d, (X[0][2] * X[2][1] - X[0][1] * X[2][2]) / d, (X[0][1] * X[1][2] - X[0][2] * X[1][1]) / d}, {(X[1][2] * X[2][0] - X[1][0] * X[2][2]) / d, (X[0][0] * X[2][2] - X[0][2] * X[2][0]) / d, (X[0][2] * X[1][0] - X[0][0] * X[1][2]) / d}, {(X[1][0] * X[2][1] - X[1][1] * X[2][0]) / d, (X[0][1] * X[2][0] - X[0][0] * X[2][1]) / d, (X[0][0] * X[1][1] - X[0][1] * X[1][0]) / d}};
    double N[3][3]; for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) N[r][c] = 0.5 * (X[r][c] + inv[c][r]); memcpy(X, N, sizeof X); }
  double det = X[0][0] * (X[1][1] * X[2][2] - X[1][2] * X[2][1]) - X[0][1] * (X[1][0] * X[2][2] - X[1][2] * X[2][0]) + X[0][2] * (X[1][0] * X[2][1] - X[1][1] * X[2][0]); if (det < 0) return;   // reflection: keep the RANSAC estimate
  memcpy(R, X, sizeof X); for (int i = 0; i < 3; i++) t[i] = cb[i] - (R[i][0] * ca[0] + R[i][1] * ca[1] + R[i][2] * ca[2]);
}
// RANSAC rigid fit of A points (probe frame coords) onto B points (reference world coords). Returns inlier count.
static int ransacRigid(const std::vector<float>& a, const std::vector<float>& b, double R[3][3], double t[3], float thresh, int iters, uint32_t seed = 12345) {
  int n = (int)a.size() / 3; if (n < 6) return 0; uint32_t s = seed; auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (int)((s >> 8) % n); };
  int bestN = 0; double bR[3][3], bt[3]; std::vector<int> bestIdx;
  for (int it = 0; it < iters; it++) { int i0 = rnd(), i1 = rnd(), i2 = rnd(); if (i0 == i1 || i1 == i2 || i0 == i2) continue; double Rr[3][3], tr[3]; if (!rigidFrom3(&a[i0 * 3], &a[i1 * 3], &a[i2 * 3], &b[i0 * 3], &b[i1 * 3], &b[i2 * 3], Rr, tr)) continue;
    // the three pairs must agree on scale (rigid): side lengths within 10 %
    auto d2 = [](const float* p, const float* q) { return (p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]); };
    double la = sqrt(d2(&a[i0 * 3], &a[i1 * 3])), lb = sqrt(d2(&b[i0 * 3], &b[i1 * 3])); if (la < 5 || fabs(la - lb) > 0.1 * la) continue;
    std::vector<int> idx; for (int i = 0; i < n; i++) { double x = Rr[0][0] * a[i * 3] + Rr[0][1] * a[i * 3 + 1] + Rr[0][2] * a[i * 3 + 2] + tr[0] - b[i * 3], y = Rr[1][0] * a[i * 3] + Rr[1][1] * a[i * 3 + 1] + Rr[1][2] * a[i * 3 + 2] + tr[1] - b[i * 3 + 1], z = Rr[2][0] * a[i * 3] + Rr[2][1] * a[i * 3 + 1] + Rr[2][2] * a[i * 3 + 2] + tr[2] - b[i * 3 + 2]; if (x * x + y * y + z * z < thresh * thresh) idx.push_back(i); }
    if ((int)idx.size() > bestN) { bestN = (int)idx.size(); memcpy(bR, Rr, sizeof bR); memcpy(bt, tr, sizeof bt); bestIdx = idx; } }
  if (bestN < 6) return 0; refineRigid(a, b, bestIdx, bR, bt); memcpy(R, bR, sizeof bR); memcpy(t, bt, sizeof bt); return bestN;
}
} // namespace feat
