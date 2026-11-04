// covmap — top-view coverage map of one scan: every point of every frame projected on the XY plane (Studio's Top
// view: X right, Y up), 1 mm cells, value = number of distinct frames covering the cell. Written as a BMP where
// white = 1 frame, brighter cells for more frames (capped), red = cells covered by exactly one frame, grey = empty.
// usage: covmap <project.sproj> <scanIndex(1-based)> <out.bmp>
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
#include <thread>
using namespace artec::sdk::base; namespace prj = artec::sdk::project;
static void mul(const double A[4][4], const double B[4][4], double o[4][4]) { for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { double s = 0; for (int k = 0; k < 4; k++) s += A[i][k] * B[k][j]; o[i][j] = s; } }
int wmain(int argc, wchar_t** argv) {
  if (argc < 4) { wprintf(L"usage: covmap project.sproj scanIndex out.bmp\n"); return 2; } int want = _wtoi(argv[2]) - 1; setOutputLevel(VerboseLevel_Error);
  prj::IProject* p = nullptr; if (prj::openProject(&p, argv[1]) != ErrorCode_OK || !p) { wprintf(L"openProject failed\n"); return 1; }
  std::vector<Uuid> v; for (int i = 0; i < p->getEntryCount(); i++) { prj::EntryInfo e; if (p->getEntry(i, &e, nullptr) == ErrorCode_OK && e.type == prj::EntryType_Scan) v.push_back(e.uuid); }
  if (want < 0 || want >= (int)v.size()) { wprintf(L"scan index out of range\n"); return 1; }
  IArrayUuid* ids = nullptr; createArrayUuid(&ids, 1); ids->setElement(0, v[want]); prj::ProjectLoaderSettings ls; ls.entryList = ids; IJob* loader = nullptr; p->createLoader(&loader, &ls); IModel* m = nullptr; createModel(&m);
  { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.out = m; ws.threadsCount = std::thread::hardware_concurrency(); if (executeJob(loader, &ws) != ErrorCode_OK) { wprintf(L"load failed\n"); return 1; } loader->release(); } ids->release(); p->release();
  IScan* s = m->getElement(0); int nf = s->getSize(); const auto& ST = s->getScanTransformation().m;
  std::vector<float> xs, ys; std::vector<int> fr; float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f;
  for (int k = 0; k < nf; k++) { IFrameMesh* f = s->getElement(k); if (!f) continue; IArrayPoint3F* pts = f->getPoints(); if (!pts) continue; double M[4][4]; mul(ST, s->getTransformation(k).m, M); int n = pts->getSize(); const Point3F* q = pts->getPointer();
    for (int i = 0; i < n; i++) { double x = q[i].x, y = q[i].y, z = q[i].z; float X = (float)(M[0][0] * x + M[0][1] * y + M[0][2] * z + M[0][3]), Y = (float)(M[1][0] * x + M[1][1] * y + M[1][2] * z + M[1][3]); xs.push_back(X); ys.push_back(Y); fr.push_back(k); mnx = std::min(mnx, X); mxx = std::max(mxx, X); mny = std::min(mny, Y); mxy = std::max(mxy, Y); } }
  const float cell = 1.0f; int W = (int)((mxx - mnx) / cell) + 3, H = (int)((mxy - mny) / cell) + 3; if (W > 4000 || H > 4000) { wprintf(L"too big %dx%d\n", W, H); return 1; }
  std::vector<int> cnt((size_t)W * H, 0), last((size_t)W * H, -1);
  for (size_t i = 0; i < xs.size(); i++) { int cx = (int)((xs[i] - mnx) / cell) + 1, cy = (int)((ys[i] - mny) / cell) + 1; size_t c = (size_t)cy * W + cx; if (last[c] != fr[i]) { last[c] = fr[i]; cnt[c]++; } }
  // BMP, bottom-up rows = Y up like Studio's Top view
  int rowB = W * 3, pad = (4 - rowB % 4) % 4; FILE* bf = _wfopen(argv[3], L"wb"); if (!bf) { wprintf(L"cannot write\n"); return 1; }
  BITMAPFILEHEADER fh = {0x4D42, (DWORD)(54 + (rowB + pad) * H), 0, 0, 54}; BITMAPINFOHEADER ih = {40, W, H, 1, 24, 0, (DWORD)((rowB + pad) * H)}; fwrite(&fh, 14, 1, bf); fwrite(&ih, 40, 1, bf); unsigned char z[3] = {0, 0, 0};
  int one = 0, covered = 0; for (int y = 0; y < H; y++) { for (int x = 0; x < W; x++) { int c = cnt[(size_t)y * W + x]; unsigned char px[3]; if (c == 0) { px[0] = px[1] = px[2] = 110; } else if (c == 1) { px[0] = 40; px[1] = 40; px[2] = 230; one++; covered++; } else { int g = std::min(255, 120 + c * 12); px[0] = px[1] = px[2] = (unsigned char)g; covered++; } fwrite(px, 3, 1, bf); } fwrite(z, 1, pad, bf); }
  fclose(bf); wprintf(L"scan %d: %d frames, map %dx%d mm, covered cells %d, of which single-frame %d (%.1f%%)\n", want + 1, nf, W, H, covered, one, covered ? 100.0 * one / covered : 0); m->release(); return 0;
}
