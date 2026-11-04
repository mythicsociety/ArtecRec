// feattest — offline check of the image-feature position search (rec_feat.h) on a saved project.
// Bank: for every scan, windows of WIN consecutive textured frames (keypoints + 3D points in world coordinates) plus
// a point grid of the window for the geometric check. Probe: for scans after the first, PROBE consecutive textured
// frames (at two image scales) mapped into the LAST frame's coordinates, matched against the banks of the EARLIER
// scans, RANSAC rigid pose, geometric verification, then compared with the pose the project stores for that frame.
// usage: feattest <project.sproj> [flipV 0|1]
#include <artec/sdk/base/Log.h>
#include <artec/sdk/base/IModel.h>
#include <artec/sdk/base/IScan.h>
#include <artec/sdk/base/IFrameMesh.h>
#include <artec/sdk/base/IImage.h>
#include <artec/sdk/base/AlgorithmWorkset.h>
#include <artec/sdk/base/IJob.h>
#include <artec/sdk/base/IArray.h>
#include <artec/sdk/base/IString.h>
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
#include <vector>
#include <chrono>
#include <thread>
#include "rec_feat.h"
using namespace artec::sdk::base; namespace prj = artec::sdk::project; using clk = std::chrono::steady_clock;
static const int WIN = 5, WINSTEP = 3, PROBE = 3;
static void mul44(const double A[4][4], const double B[4][4], double o[4][4]) { for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { double s = 0; for (int k = 0; k < 4; k++) s += A[i][k] * B[k][j]; o[i][j] = s; } }
static void invRigid(const double M[4][4], double o[4][4]) { for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) o[i][j] = M[j][i]; for (int i = 0; i < 3; i++) o[i][3] = -(o[i][0] * M[0][3] + o[i][1] * M[1][3] + o[i][2] * M[2][3]); o[3][0] = o[3][1] = o[3][2] = 0; o[3][3] = 1; }
static bool frameFeatures(const IFrameMesh* fm, bool flipV, int targetW, feat::Frame& out) {
  IImage* img = fm->getImage(); IArrayUVCoordinates* uvs = fm->getUVCoordinates(); IArrayPoint3F* pts = fm->getPoints(); if (!img || !uvs || !pts) return false;
  int w = img->getWidth(), h = img->getHeight(), pitch = img->getPitch(); PixelFormat pf = img->getPixelFormat(); int ch = pf == PixelFormat_BGR || pf == PixelFormat_RGB ? 3 : pf == PixelFormat_BGRA || pf == PixelFormat_RGBA ? 4 : pf == PixelFormat_Mono ? 1 : 0; if (!ch || w < 64 || h < 64) return false;
  int nv = pts->getSize(); if (uvs->getSize() != nv || nv < 100) return false;
  feat::Image im = feat::toGrey((const uint8_t*)img->getPointer(), w, h, pitch, ch, targetW); feat::build(im, (const float*)uvs->getPointer(), (const float*)pts->getPointer(), nv, flipV, out); return out.n() >= 20;
}
static void framePoints(const IFrameMesh* fm, const double M[4][4], int step, std::vector<float>& out) { IArrayPoint3F* pts = fm->getPoints(); const Point3F* p = pts->getPointer(); for (int i = 0; i < pts->getSize(); i += step) { double x = p[i].x, y = p[i].y, z = p[i].z; out.push_back((float)(M[0][0] * x + M[0][1] * y + M[0][2] * z + M[0][3])); out.push_back((float)(M[1][0] * x + M[1][1] * y + M[1][2] * z + M[1][3])); out.push_back((float)(M[2][0] * x + M[2][1] * y + M[2][2] * z + M[2][3])); } }
struct Bank { int scan, frame0; feat::Frame f; feat::Grid grid; };
int wmain(int argc, wchar_t** argv) {
  if (argc < 2) { wprintf(L"usage: feattest project.sproj [flipV]\n"); return 2; } bool flipV = argc > 2 && argv[2][0] == L'1'; setOutputLevel(VerboseLevel_Error);
  prj::IProject* p = nullptr; if (prj::openProject(&p, argv[1]) != ErrorCode_OK || !p) { wprintf(L"openProject failed\n"); return 1; }
  std::vector<Uuid> v; for (int i = 0; i < p->getEntryCount(); i++) { prj::EntryInfo e; if (p->getEntry(i, &e, nullptr) == ErrorCode_OK && e.type == prj::EntryType_Scan) v.push_back(e.uuid); }
  IArrayUuid* ids = nullptr; createArrayUuid(&ids, (int)v.size()); for (int i = 0; i < (int)v.size(); i++) ids->setElement(i, v[i]);
  prj::ProjectLoaderSettings ls; ls.entryList = ids; IJob* loader = nullptr; p->createLoader(&loader, &ls); IModel* m = nullptr; createModel(&m);
  { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.out = m; ws.threadsCount = std::thread::hardware_concurrency(); if (executeJob(loader, &ws) != ErrorCode_OK) { wprintf(L"load failed\n"); return 1; } loader->release(); } ids->release(); p->release();
  wprintf(L"%d scans, flipV=%d, window %d textured frames, probe %d frames x 2 scales\n", m->getSize(), (int)flipV, WIN, PROBE);
  std::vector<Bank> banks; int totalFound = 0, totalProbes = 0, totalGood = 0;
  std::wstring outp = argc > 3 ? argv[3] : L"";
  for (int i = 0; i < m->getSize(); i++) { IScan* s = m->getElement(i); double ST[4][4]; memcpy(ST, s->getScanTransformation().m, sizeof ST); int bestScanInl = 0; double corr[4][4]; std::vector<int> ti; for (int k = 0; k < s->getSize(); k++) { IFrameMesh* f = s->getElement(k); if (f && f->isTextured()) ti.push_back(k); }
    if (i > 0 && (int)ti.size() >= PROBE) { // probes: 6 positions spread over this scan, each = PROBE consecutive textured frames ending there
      for (int q = 0; q < 6; q++) { int last = (int)(ti.size() * (q + 0.5) / 6); if (last < PROBE - 1) last = PROBE - 1; int kLast = ti[last]; double Mlast[4][4]; mul44(ST, s->getTransformation(kLast).m, Mlast); double MlastInv[4][4]; invRigid(Mlast, MlastInv);
        auto t0 = clk::now(); feat::Frame probe; std::vector<float> probePts;
        for (int j = last - PROBE + 1; j <= last; j++) { int k = ti[j]; double Mk[4][4], rel[4][4]; mul44(ST, s->getTransformation(k).m, Mk); mul44(MlastInv, Mk, rel);   // frame j -> last-frame coords
          for (int sc = 0; sc < 2; sc++) { feat::Frame f; if (frameFeatures(s->getElement(k), flipV, sc ? 240 : 320, f)) probe.append(f, rel); } framePoints(s->getElement(k), rel, 25, probePts); }
        int bestN = 0, matches = 0, bScan = -1, bFrame = -1, storedInl = 0; double bR[3][3], bt[3]; float bShare = 0, bMed = 0, sShare = 0, sMed = 0;
        double sR[3][3], st[3]; for (int r = 0; r < 3; r++) { for (int c = 0; c < 3; c++) sR[r][c] = Mlast[r][c]; st[r] = Mlast[r][3]; }
        for (auto& b : banks) { auto mm = feat::match(probe, b.f); if ((int)mm.size() < 12) continue; matches += (int)mm.size(); std::vector<float> a, bb; for (auto& pr : mm) { a.insert(a.end(), &probe.pts[pr.first * 3], &probe.pts[pr.first * 3] + 3); bb.insert(bb.end(), &b.f.pts[pr.second * 3], &b.f.pts[pr.second * 3] + 3); }
          // how many of these matches agree with the STORED pose (within 4 mm)? and how does the stored pose sit on this window's geometry?
          { int n = (int)a.size() / 3, c = 0; for (int j = 0; j < n; j++) { const float* q = &a[j * 3]; double e[3]; for (int r = 0; r < 3; r++) e[r] = sR[r][0] * q[0] + sR[r][1] * q[1] + sR[r][2] * q[2] + st[r] - bb[j * 3 + r]; if (e[0] * e[0] + e[1] * e[1] + e[2] * e[2] < 16) c++; } storedInl += c;
            float sh, md; b.grid.check(probePts, sR, st, 6.0f, sh, md); if (sh > sShare) { sShare = sh; sMed = md; } }
          double R[3][3], t[3]; int n = feat::ransacRigid(a, bb, R, t, 4.0f, 600); if (n < 20 || n <= bestN) continue; float share, med; b.grid.check(probePts, R, t, 6.0f, share, med); if (share < 0.25f || med > 2.5f) continue;
          bestN = n; memcpy(bR, R, sizeof bR); memcpy(bt, t, sizeof bt); bScan = b.scan; bFrame = b.frame0; bShare = share; bMed = med; }
        double secs = std::chrono::duration<double>(clk::now() - t0).count(); totalProbes++;
        if (bestN >= 20) { double c[3] = {0, 0, 0}; int np = (int)probePts.size() / 3; for (int j = 0; j < np; j++) for (int r = 0; r < 3; r++) c[r] += probePts[j * 3 + r]; for (int r = 0; r < 3; r++) c[r] /= np;
          double e[3]; for (int r = 0; r < 3; r++) e[r] = (bR[r][0] * c[0] + bR[r][1] * c[1] + bR[r][2] * c[2] + bt[r]) - (Mlast[r][0] * c[0] + Mlast[r][1] * c[1] + Mlast[r][2] * c[2] + Mlast[r][3]); double err = sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]); totalFound++; if (err < 15) totalGood++;
          if (bestN > bestScanInl) { bestScanInl = bestN; double F[4][4]; for (int r = 0; r < 3; r++) { for (int cc = 0; cc < 3; cc++) F[r][cc] = bR[r][cc]; F[r][3] = bt[r]; } F[3][0] = F[3][1] = F[3][2] = 0; F[3][3] = 1; mul44(F, MlastInv, corr); }   // world correction = found * inv(stored)
          wprintf(L"scan %d frame %3d (%4d kp): FOUND via scan %d window@%3d, %3d inliers / %4d matches, geom %.0f%% within 6 mm median %.1f mm, %.2f s; vs stored pose: offset (%.0f %.0f %.0f) = %.1f mm %ls; stored pose has %d inliers, geom %.0f%% median %.1f mm\n", i + 1, kLast + 1, probe.n(), bScan + 1, bFrame + 1, bestN, matches, bShare * 100, bMed, secs, e[0], e[1], e[2], err, err < 15 ? L"OK" : L"DIFFERENT", storedInl, sShare * 100, sMed); }
        else wprintf(L"scan %d frame %3d (%4d kp): not found (%d matches, %.2f s); stored pose has %d inliers, geom %.0f%% median %.1f mm\n", i + 1, kLast + 1, probe.n(), matches, secs, storedInl, sShare * 100, sMed); } }
    if (bestScanInl > 0) { double n[4][4]; mul44(corr, ST, n); memcpy(ST, n, sizeof ST); Matrix4x4D M; memcpy(M.m, ST, sizeof ST); s->setScanTransformation(M); wprintf(L"scan %d: moved to the feature position (best find %d inliers)\n", i + 1, bestScanInl); }
    // bank windows
    auto tb = clk::now(); int nb = 0, kps = 0; for (int j = 0; j + WIN <= (int)ti.size(); j += WINSTEP) { Bank b; b.scan = i; b.frame0 = ti[j]; std::vector<float> wpts;
      for (int q = j; q < j + WIN; q++) { int k = ti[q]; double Mk[4][4]; mul44(ST, s->getTransformation(k).m, Mk); feat::Frame f; if (frameFeatures(s->getElement(k), flipV, 320, f)) b.f.append(f, Mk); framePoints(s->getElement(k), Mk, 6, wpts); }
      if (b.f.n() < 50) continue; b.grid.build(std::move(wpts), 2.0f); kps += b.f.n(); banks.push_back(std::move(b)); nb++; }
    wprintf(L"scan %d bank: %d windows, %d keypoints, %.1f s\n", i + 1, nb, kps, std::chrono::duration<double>(clk::now() - tb).count()); }
  wprintf(L"found %d of %d probes, %d within 15 mm of the stored pose\n", totalFound, totalProbes, totalGood);
  if (!outp.empty()) { std::wstring dir = outp.substr(0, outp.find_last_of(L"\\/")); SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    prj::ProjectSettings ps; ps.path = outp.c_str(); prj::IProject* po = nullptr; ErrorCode ec = prj::createNewProject(&po, &ps); if (ec != ErrorCode_OK) { wprintf(L"createNewProject 0x%08X\n", (unsigned)ec); return 1; }
    prj::ProjectSaverSettings ss; ss.path = outp.c_str(); generateUuid(&ss.projectId); ss.compressionLevel = 0; IJob* saver = nullptr; ec = po->createSaver(&saver, &ss);
    AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = m; ws.threadsCount = std::thread::hardware_concurrency(); ec = executeJob(saver, &ws); saver->release(); po->release(); wprintf(L"saved 0x%08X -> %ls\n", (unsigned)ec, outp.c_str()); }
  m->release(); return 0;
}
