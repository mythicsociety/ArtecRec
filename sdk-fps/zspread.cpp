// zspread — "points at different levels": for every 1 mm cell of the top view, the spread of Z (height) over all points
// of all frames that land in the cell. On a well-registered scan of a rough stone the spread is the relief within the
// cell (sub-mm); frames sitting at different depths show as a spread of several mm. Writes a BMP: grey = spread
// <0.5 mm, yellow 0.5–1, orange 1–2, red >2 mm; empty = dark grey. Prints the share of cells in each band and, per
// frame, how far its points sit from the per-cell median height (frames at the wrong level stand out).
// usage: zspread <project.sproj> <scanIndex(1-based)> <out.bmp>
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
#include <algorithm>
#include <thread>
using namespace artec::sdk::base; namespace prj = artec::sdk::project;
static void mul(const double A[4][4], const double B[4][4], double o[4][4]) { for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { double s = 0; for (int k = 0; k < 4; k++) s += A[i][k] * B[k][j]; o[i][j] = s; } }
int wmain(int argc, wchar_t** argv) {
  if (argc < 4) { wprintf(L"usage: zspread project.sproj scanIndex(0 = all scans together) out.bmp\n"); return 2; } int want = _wtoi(argv[2]) - 1; setOutputLevel(VerboseLevel_Error);
  prj::IProject* p = nullptr; if (prj::openProject(&p, argv[1]) != ErrorCode_OK || !p) { wprintf(L"openProject failed\n"); return 1; }
  std::vector<Uuid> v; for (int i = 0; i < p->getEntryCount(); i++) { prj::EntryInfo e; if (p->getEntry(i, &e, nullptr) == ErrorCode_OK && e.type == prj::EntryType_Scan) v.push_back(e.uuid); }
  bool all = want < 0; if (!all && want >= (int)v.size()) { wprintf(L"scan index out of range\n"); return 1; }
  IArrayUuid* ids = nullptr; createArrayUuid(&ids, all ? (int)v.size() : 1); if (all) for (int i = 0; i < (int)v.size(); i++) ids->setElement(i, v[i]); else ids->setElement(0, v[want]); prj::ProjectLoaderSettings ls; ls.entryList = ids; IJob* loader = nullptr; p->createLoader(&loader, &ls); IModel* m = nullptr; createModel(&m);
  { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.out = m; ws.threadsCount = std::thread::hardware_concurrency(); if (executeJob(loader, &ws) != ErrorCode_OK) { wprintf(L"load failed\n"); return 1; } loader->release(); } ids->release(); p->release();
  // frames of one scan, or of all scans in sequence (frame numbers then run on across scans; a per-scan table is printed)
  IScan* s = m->getElement(0); int nf = 0; std::vector<std::pair<IScan*, int>> fl; for (int si = 0; si < m->getSize(); si++) { IScan* sc = m->getElement(si); for (int k = 0; k < sc->getSize(); k++) fl.push_back({sc, k}); } nf = (int)fl.size();
  if (all) { wprintf(L"%d scans together:", m->getSize()); int base = 1; for (int si = 0; si < m->getSize(); si++) { wprintf(L" scan %d = frames %d-%d;", si + 1, base, base + m->getElement(si)->getSize() - 1); base += m->getElement(si)->getSize(); } wprintf(L"\n"); }
  else { const auto& ST = s->getScanTransformation().m; (void)ST; wprintf(L"frame translations (scan-local): "); for (int k = 0; k < nf && k < 40; k += 13) { const auto& T = s->getTransformation(k).m; wprintf(L"f%d(%.2f %.2f %.2f) ", k + 1, T[0][3], T[1][3], T[2][3]); } wprintf(L"\n"); }
  std::vector<float> xs, ys, zs; std::vector<int> fr; float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f;
  for (int k = 0; k < nf; k++) { IScan* sc = fl[k].first; int kk = fl[k].second; IFrameMesh* f = sc->getElement(kk); if (!f) continue; IArrayPoint3F* pts = f->getPoints(); if (!pts) continue; double M[4][4]; mul(sc->getScanTransformation().m, sc->getTransformation(kk).m, M); int n = pts->getSize(); const Point3F* q = pts->getPointer();
    for (int i = 0; i < n; i += 2) { double x = q[i].x, y = q[i].y, z = q[i].z; float X = (float)(M[0][0] * x + M[0][1] * y + M[0][2] * z + M[0][3]), Y = (float)(M[1][0] * x + M[1][1] * y + M[1][2] * z + M[1][3]), Z = (float)(M[2][0] * x + M[2][1] * y + M[2][2] * z + M[2][3]); xs.push_back(X); ys.push_back(Y); zs.push_back(Z); fr.push_back(k); mnx = std::min(mnx, X); mxx = std::max(mxx, X); mny = std::min(mny, Y); mxy = std::max(mxy, Y); } }
  const float cell = 1.0f; int W = (int)((mxx - mnx) / cell) + 3, H = (int)((mxy - mny) / cell) + 3; if (W > 4000 || H > 4000) { wprintf(L"too big\n"); return 1; }
  std::vector<std::vector<float>> cz((size_t)W * H); std::vector<std::vector<int>> cf((size_t)W * H);
  for (size_t i = 0; i < xs.size(); i++) { int cx = (int)((xs[i] - mnx) / cell) + 1, cy = (int)((ys[i] - mny) / cell) + 1; size_t c = (size_t)cy * W + cx; cz[c].push_back(zs[i]); cf[c].push_back(fr[i]); }
  // per cell: spread between the 5th and 95th percentile of Z (robust to single stray points); per frame: |z - cell median|
  std::vector<double> frameOff(nf, 0), frameSigned(nf, 0), frameDist(nf, 0); std::vector<int> frameN(nf, 0); int bands[4] = {0, 0, 0, 0}; int nonEmpty = 0;
  for (int k = 0; k < nf; k++) { IFrameMesh* f = fl[k].first->getElement(fl[k].second); if (!f) continue; IArrayPoint3F* pts = f->getPoints(); if (!pts || !pts->getSize()) continue; const Point3F* q = pts->getPointer(); double sz = 0; for (int i = 0; i < pts->getSize(); i++) sz += q[i].z; frameDist[k] = -sz / pts->getSize(); }   // scanning distance: frame coords, camera at origin looking down -z
  std::vector<unsigned char> img((size_t)W * H * 3, 70);
  int wbands[4] = {0, 0, 0, 0}; int wcells = 0;   // WITHIN-frame spread: the same statistic using only the points of the single frame that has most points in the cell
  for (size_t c = 0; c < cz.size(); c++) { auto& z = cz[c]; if (z.size() < 4) continue; nonEmpty++; std::vector<float> sorted = z; std::sort(sorted.begin(), sorted.end()); float med = sorted[sorted.size() / 2], lo = sorted[sorted.size() * 5 / 100], hi = sorted[sorted.size() * 95 / 100]; float sp = hi - lo;
    { std::vector<int> fs = cf[c]; std::sort(fs.begin(), fs.end()); int bestF = fs[0], bestN = 0, run = 0; for (size_t i = 0; i < fs.size(); i++) { run = (i && fs[i] == fs[i - 1]) ? run + 1 : 1; if (run > bestN) { bestN = run; bestF = fs[i]; } }
      if (bestN >= 4) { std::vector<float> zz; for (size_t i = 0; i < z.size(); i++) if (cf[c][i] == bestF) zz.push_back(z[i]); std::sort(zz.begin(), zz.end()); float wsp = zz[zz.size() * 95 / 100] - zz[zz.size() * 5 / 100]; wbands[wsp < 0.5f ? 0 : wsp < 1.0f ? 1 : wsp < 2.0f ? 2 : 3]++; wcells++; } }
    int b = sp < 0.5f ? 0 : sp < 1.0f ? 1 : sp < 2.0f ? 2 : 3; bands[b]++; unsigned char* px = &img[c * 3]; if (b == 0) { px[0] = px[1] = px[2] = 200; } else if (b == 1) { px[0] = 60; px[1] = 220; px[2] = 230; } else if (b == 2) { px[0] = 30; px[1] = 140; px[2] = 240; } else { px[0] = 30; px[1] = 30; px[2] = 230; }   // BGR
    for (size_t i = 0; i < z.size(); i++) { frameOff[cf[c][i]] += fabs(z[i] - med); frameSigned[cf[c][i]] += z[i] - med; frameN[cf[c][i]]++; } }
  int rowB = W * 3, pad = (4 - rowB % 4) % 4; FILE* bf = _wfopen(argv[3], L"wb"); if (!bf) { wprintf(L"cannot write\n"); return 1; }
  BITMAPFILEHEADER fh = {0x4D42, (DWORD)(54 + (rowB + pad) * H), 0, 0, 54}; BITMAPINFOHEADER ih = {40, W, H, 1, 24, 0, (DWORD)((rowB + pad) * H)}; fwrite(&fh, 14, 1, bf); fwrite(&ih, 40, 1, bf); unsigned char zpad[3] = {0, 0, 0};
  for (int y = 0; y < H; y++) { fwrite(&img[(size_t)y * W * 3], 3, W, bf); fwrite(zpad, 1, pad, bf); } fclose(bf);
  wprintf(L"scan %d: %d frames, %d cells with points. height spread per cell: <0.5 mm %.1f%%, 0.5-1 mm %.1f%%, 1-2 mm %.1f%%, >2 mm %.1f%%\n", want + 1, nf, nonEmpty, 100.0 * bands[0] / nonEmpty, 100.0 * bands[1] / nonEmpty, 100.0 * bands[2] / nonEmpty, 100.0 * bands[3] / nonEmpty);
  if (wcells) wprintf(L"WITHIN a single frame, same cells: <0.5 mm %.1f%%, 0.5-1 mm %.1f%%, 1-2 mm %.1f%%, >2 mm %.1f%%\n", 100.0 * wbands[0] / wcells, 100.0 * wbands[1] / wcells, 100.0 * wbands[2] / wcells, 100.0 * wbands[3] / wcells);
  wprintf(L"per frame: abs offset from the cell median height | signed offset (+ = towards the scanner side) | scanning distance\n"); int bad = 0;
  for (int k = 0; k < nf; k++) { if (!frameN[k]) continue; double o = frameOff[k] / frameN[k], sg = frameSigned[k] / frameN[k]; bool flag = o > 0.5; if (flag) bad++; wprintf(L"%s frame %3d: %.2f mm | %+.2f mm | %.0f mm%s\n", flag ? L"!!" : L"  ", k + 1, o, sg, frameDist[k], fl[k].first->getElement(fl[k].second) && fl[k].first->getElement(fl[k].second)->isTextured() ? L" (photo)" : L""); }
  wprintf(L"%d of %d frames flagged\n", bad, nf);
  m->release(); return 0;
}
