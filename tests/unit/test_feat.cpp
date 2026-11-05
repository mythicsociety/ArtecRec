// UNIT tests for C:\Artec\sdk-fps\rec_feat.h: feat::Grid (nearest / check, the join gate's surface test), the
// RANSAC rigid fit, and a synthetic slide test that shows why the geometry-only join needs the flatness rule (D1).
#include "../testfw.h"
#include "rec_feat.h"
#include "rec_logic.h"
#include <random>

static float brute(const std::vector<float>& p, float x, float y, float z, float maxd) { float best = maxd * maxd; for (size_t i = 0; i < p.size(); i += 3) { float a = p[i] - x, b = p[i + 1] - y, c = p[i + 2] - z; float d = a * a + b * b + c * c; if (d < best) best = d; } return sqrtf(best); }
static const double I3[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; static const double Z3[3] = {0, 0, 0};

TEST(grid_nearest_matches_brute_force, "D1") {
  std::mt19937 rng(5); std::uniform_real_distribution<float> U(-60, 60); std::vector<float> p; for (int i = 0; i < 3000; i++) p.insert(p.end(), {U(rng), U(rng), U(rng) * 0.2f});
  for (float cell : {0.7f, 2.0f, 3.0f}) { feat::Grid g; std::vector<float> c = p; g.build(std::move(c), cell); int bad = 0;
    for (int q = 0; q < 400; q++) { float x = U(rng), y = U(rng), z = U(rng) * 0.3f; for (float maxd : {1.0f, 6.0f, 15.0f}) { float a = g.nearest(x, y, z, maxd), b = brute(p, x, y, z, maxd); if (std::fabs(a - b) > 1e-4f && bad++ < 3) CHECK_MSG(false, "cell " + tfw::str(cell) + " maxd " + tfw::str(maxd) + ": " + tfw::str(a) + " vs " + tfw::str(b)); } }
    CHECK_EQ(bad, 0); }
}
TEST(grid_nearest_edges, "D1") {
  feat::Grid empty; empty.build({}, 2.f); CHECK_NEAR(empty.nearest(0, 0, 0, 6), 6.0, 1e-6);
  feat::Grid g; g.build({-0.5f, -0.5f, -0.5f, 100, 100, 100}, 2.f);   // negative coordinates straddle the cell boundary at 0
  CHECK_NEAR(g.nearest(0.5f, 0.5f, 0.5f, 6), sqrt(3.0), 1e-5); CHECK_NEAR(g.nearest(-0.5f, -0.5f, -0.5f, 6), 0, 1e-6);
  CHECK_NEAR(g.nearest(50, 50, 50, 6), 6.0, 1e-6);   // nothing within maxd: maxd is returned
  CHECK_NEAR(g.nearest(-0.5f, -0.5f, 5.49f, 6), 5.99, 1e-4); CHECK_NEAR(g.nearest(-0.5f, -0.5f, 5.6f, 6), 6.0, 1e-6);
}
TEST(grid_check_share_and_median, "D1") {
  std::vector<float> ref; for (int i = 0; i <= 40; i++) for (int j = 0; j <= 40; j++) ref.insert(ref.end(), {(float)i, (float)j, 0.f});   // 40x40 mm plane, 1 mm grid
  feat::Grid g; std::vector<float> c = ref; g.build(std::move(c), 2.f);
  std::vector<float> probe; for (int i = 0; i < 20; i++) for (int j = 0; j < 20; j++) probe.insert(probe.end(), {10.f + i, 10.f + j, 1.0f});   // 1 mm above, all inside
  float share, med; g.check(probe, I3, Z3, 6.f, share, med); CHECK_NEAR(share, 1.0, 1e-6); CHECK_NEAR(med, 1.0, 1e-5);
  double t[3] = {0, 0, 10}; g.check(probe, I3, t, 6.f, share, med); CHECK_NEAR(share, 0.0, 1e-6); CHECK_NEAR(med, 6.0, 1e-6);   // 11 mm off: none
  double t2[3] = {30, 0, 0}; g.check(probe, I3, t2, 6.f, share, med); CHECK_NEAR(share, 0.3, 1e-6);   // probe x 40..59 past the edge at 40, 1 mm above: x 40..45 within 6 mm = 6 of 20 columns
  double R[3][3] = {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}; double t3[3] = {50, 0, 0}; g.check(probe, R, t3, 6.f, share, med); CHECK_NEAR(share, 1.0, 1e-6);   // rotated 90 deg about z: x' = 50 - y in 21..40
  std::vector<float> none; g.check(none, I3, Z3, 6.f, share, med); CHECK_EQ(share, 0.0f); CHECK_EQ(med, 6.0f);
}
// D1: a pass slid sideways along a flat stone still lies on the stone - the surface gate cannot see it, the flatness
// rule must refuse a geometry-only placement there. On carved relief the same slide is caught by the surface test.
TEST(slide_on_flat_stone_needs_flatness_rule, "D1") {
  auto surface = [](float x, float y, float amp) { return amp * sinf(x * 0.6283f) * cosf(y * 0.4f); };
  for (float amp : {0.0f, 0.15f, 2.0f}) {
    std::vector<float> win; for (float x = 0; x <= 60; x += 0.5f) for (float y = 0; y <= 60; y += 0.5f) win.insert(win.end(), {x, y, surface(x, y, amp)});
    float flat = logic::planeRms(win); feat::Grid g; std::vector<float> c = win; g.build(std::move(c), 2.f);
    std::vector<float> probe; for (float x = 20; x <= 40; x += 1) for (float y = 20; y <= 40; y += 1) probe.insert(probe.end(), {x, y, surface(x + 5.f, y, amp)});   // true place is 5 mm further along x
    float share, med; g.check(probe, I3, Z3, 6.f, share, med);   // probe placed WRONGLY (no shift applied)
    auto geo = logic::acceptJoin(share, med, true, flat);
    if (amp == 0.0f) { CHECK(share > 0.99f); CHECK(med < 0.01f); CHECK_MSG(!geo.ok, "flat stone: a 5 mm slide was accepted for geometry-only"); }
    if (amp == 0.15f) CHECK_MSG(!geo.ok, "0.15 mm relief (rms " + tfw::str(flat) + "): slid geometry-only join accepted");
    if (amp == 2.0f) { CHECK(flat > 0.6f); NOTE("2 mm relief: wrong place gives share " + tfw::str(share) + ", median " + tfw::str(med) + " mm -> " + (geo.ok ? "accepted" : "refused")); }
  }
}
TEST(slide_on_relief_refused, "D1 FINDING") {
  // 2 mm deep carving with a 10 mm period: a 5 mm slide (half a period) puts the probe 2-4 mm off the surface
  auto s = [](float x) { return 2.0f * sinf(x * 0.6283f); };
  std::vector<float> win; for (float x = 0; x <= 60; x += 0.25f) for (float y = 0; y <= 30; y += 0.5f) win.insert(win.end(), {x, y, s(x)});
  feat::Grid g; std::vector<float> c = win; g.build(std::move(c), 2.f);
  std::vector<float> probe; for (float x = 20; x <= 40; x += 0.5f) for (float y = 5; y <= 25; y += 1) probe.insert(probe.end(), {x, y, s(x + 5.f)});
  float share, med; g.check(probe, I3, Z3, 6.f, share, med);
  NOTE("half-period slide on 2 mm relief: share " + tfw::str(share) + ", median " + tfw::str(med) + " mm");
  // nearest-point distance is small on steep flanks: the 2.5 mm median limit does not catch a half-letter slide on
  // 2 mm deep carving. A texture or normal-direction check is needed (review D1).
  CHECK_MSG(!logic::acceptJoin(share, med, true, logic::planeRms(win)).ok, "geometry-only join ACCEPTED 5 mm off on 2 mm relief (share " + tfw::str(share) + ", median " + tfw::str(med) + " mm)");
}
TEST(ransac_rigid_recovers_pose, "D1 D2") {
  std::mt19937 rng(9); std::uniform_real_distribution<float> U(-40, 40); double ang = 0.35; double R0[3][3] = {{cos(ang), -sin(ang), 0}, {sin(ang), cos(ang), 0}, {0, 0, 1}}; double t0[3] = {12.5, -7.0, 3.0};
  std::vector<float> a, b; int n = 300;
  for (int i = 0; i < n; i++) { float p[3] = {U(rng), U(rng), U(rng) * 0.3f}; a.insert(a.end(), p, p + 3);
    if (i % 10 < 3) { b.insert(b.end(), {U(rng), U(rng), U(rng)}); continue; }   // 30 % outliers
    for (int r = 0; r < 3; r++) b.push_back((float)(R0[r][0] * p[0] + R0[r][1] * p[1] + R0[r][2] * p[2] + t0[r] + (U(rng) / 400.f))); }   // +-0.1 mm noise
  double R[3][3], t[3]; int inl = feat::ransacRigid(a, b, R, t, 1.0f, 600);
  CHECK(inl >= 200 && inl <= 215);
  double D[3][3]; for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { D[i][j] = 0; for (int k = 0; k < 3; k++) D[i][j] += R[i][k] * R0[j][k]; }   // R * R0^T
  CHECK(logic::rotationDeg(D) < 0.1); for (int k = 0; k < 3; k++) CHECK_NEAR(t[k], t0[k], 0.1);
}
TEST(ransac_rejects_degenerate, "D1") {
  float a0[3] = {0, 0, 0}, a1[3] = {10, 0, 0}, a2[3] = {20, 0, 0}; double R[3][3], t[3];
  CHECK(!feat::rigidFrom3(a0, a1, a2, a0, a1, a2, R, t));   // collinear
  CHECK(!feat::rigidFrom3(a0, a0, a2, a0, a0, a2, R, t));   // repeated point
  std::vector<float> few = {0, 0, 0, 1, 0, 0, 0, 1, 0}; CHECK_EQ(feat::ransacRigid(few, few, R, t, 1.f, 100), 0);
  std::mt19937 rng(1); std::uniform_real_distribution<float> U(-40, 40); std::vector<float> a, b; for (int i = 0; i < 200; i++) { a.insert(a.end(), {U(rng), U(rng), U(rng)}); b.insert(b.end(), {U(rng), U(rng), U(rng)}); }
  CHECK(feat::ransacRigid(a, b, R, t, 1.0f, 600) < 20);   // pure noise: no pose with the 20 inliers the app needs
}

int main(int argc, char** argv) { return tfw::run(argc, argv, "unit-feat"); }
