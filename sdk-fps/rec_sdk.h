// ArtecRec v3 — SDK layer: scanner, scanning procedure, model/scan bookkeeping, auto-align, project load/save.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <algorithm>
#include <memory>
#include "rec_feat.h"
#include "rec_logic.h"
#include <artec/sdk/base/Log.h>
#include <artec/sdk/base/IModel.h>
#include <artec/sdk/base/IScan.h>
#include <artec/sdk/base/IFrameMesh.h>
#include <artec/sdk/base/IArray.h>
#include <artec/sdk/base/IString.h>
#include <artec/sdk/base/AlgorithmWorkset.h>
#include <artec/sdk/base/ICancellationTokenSource.h>
#include <artec/sdk/base/ICancellationToken.h>
#include <artec/sdk/base/IJob.h>
#include <artec/sdk/base/IProgressInfo.h>
#include <artec/sdk/base/RefBase.h>
#include <artec/sdk/base/Uuid.h>
#include <artec/sdk/capturing/IScanner.h>
#include <artec/sdk/capturing/IArrayScannerId.h>
#include <artec/sdk/capturing/ScannerInfo.h>
#include <artec/sdk/capturing/IScannerObserver.h>
#include <artec/sdk/scanning/IScanningProcedure.h>
#include <artec/sdk/scanning/IScanningProcedureObserver.h>
#include <artec/sdk/algorithms/Algorithms.h>
#include <artec/sdk/algorithms/IAlgorithm.h>
#include <artec/sdk/project/IProject.h>
#include <artec/sdk/project/ProjectSettings.h>
#include <artec/sdk/project/ProjectSaverSettings.h>
#include <artec/sdk/project/ProjectLoaderSettings.h>
#include <artec/sdk/project/EntryInfo.h>
#include <artec/sdk/project/EntryType.h>
#include "flyhook.h"
using namespace artec::sdk::base;
using namespace artec::sdk::capturing;
using namespace artec::sdk::scanning;
namespace prj = artec::sdk::project;
namespace alg = artec::sdk::algorithms;
using clk = std::chrono::steady_clock;

// ---------------------------------------------------------------- log
static FILE* g_log; static std::deque<std::wstring> g_logLines; static std::mutex g_logMx;
static void logf(const char* fmt, ...) {
  char b[2048]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
  SYSTEMTIME t; GetLocalTime(&t); char line[2100]; snprintf(line, sizeof line, "%02d:%02d:%02d  %s", t.wHour, t.wMinute, t.wSecond, b);
  if (g_log) { fprintf(g_log, "%s\n", line); fflush(g_log); }
  wchar_t w[2100]; if (!MultiByteToWideChar(CP_UTF8, 0, line, -1, w, 2100)) mbstowcs(w, line, 2099);   // DeepSeek 3: UTF-8 (Kannada names) std::lock_guard<std::mutex> l(g_logMx); g_logLines.push_back(w); if (g_logLines.size() > 200) g_logLines.pop_front();
}
static int sdkHook(int lvl, const wchar_t* m) { if (lvl <= 1) { char b[1024]; if (!WideCharToMultiByte(CP_UTF8, 0, m, -1, b, 1024, nullptr, nullptr)) { wcstombs(b, m, 1023); b[1023] = 0; } logf("[sdk] %s", b); } return 0; }   // DeepSeek 3

static void mul44(const double A[4][4], const double B[4][4], double out[4][4]) { for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { double s = 0; for (int k = 0; k < 4; k++) s += A[i][k] * B[k][j]; out[i][j] = s; } }
static void invRigid(const double M[4][4], double out[4][4]) {   // inverse of a rotation+translation matrix
  for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) out[i][j] = M[j][i];
  for (int i = 0; i < 3; i++) out[i][3] = -(out[i][0] * M[0][3] + out[i][1] * M[1][3] + out[i][2] * M[2][3]);
  out[3][0] = out[3][1] = out[3][2] = 0; out[3][3] = 1; }
static bool isIdentity(const double M[4][4]) { for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) if (fabs(M[i][j] - (i == j ? 1.0 : 0.0)) > 1e-6) return false; return true; }
// The SDK's project-sample computes point normals on every frame before auto-alignment; without them RANSAC "cannot take sample".
static void ensureNormals(IScan* s) { if (!s) return; for (int k = 0; k < s->getSize(); k++) { IFrameMesh* f = s->getElement(k); if (f && !f->hasPointsNormals()) f->calculate(CM_PointsNormals_Default); } }
// ---------------------------------------------------------------- display geometry
static std::vector<unsigned> g_texTrash, g_bufTrash; static std::mutex g_texTrashMx;   // GL textures / buffers to delete on the UI thread
// A photo shared by several display frames (4 Nov): the scanner takes a photo on every ~8th frame only; the frames in
// between get the nearest photo projected onto them, as Studio does, instead of a flat fill colour.
struct GlTex { std::vector<unsigned char> data; int w = 0, h = 0; unsigned id = 0; ~GlTex() { std::lock_guard<std::mutex> l(g_texTrashMx); if (id) g_texTrash.push_back(id); } };
struct FrameMesh {
  std::vector<float> v, n, col, uv; std::vector<unsigned> idx; float centre[3] = {0, 0, 0}; float texAvg[3] = {0, 0, 0};
  std::vector<unsigned char> tex; int texW = 0, texH = 0; unsigned texId = 0;   // RGB texture (downsampled 2x) of a textured frame
  std::shared_ptr<GlTex> texp;   // shared photo (stored scans); when set it is used instead of tex/texId
  unsigned vbo = 0, ibo = 0; size_t offN = 0, offUv = 0, offCol = 0; bool noVbo = false;   // GPU copies (built on first draw); the live frame changes every frame and stays in client memory
  FrameMesh() {} FrameMesh(FrameMesh&& o) noexcept { *this = std::move(o); }
  // Round 3 (R3-01): the destination's own GL texture/buffers go to the trash before they are overwritten (they leaked)
  FrameMesh& operator=(FrameMesh&& o) noexcept { if (this == &o) return *this; if (texId || vbo || ibo) { std::lock_guard<std::mutex> l(g_texTrashMx); if (texId) g_texTrash.push_back(texId); if (vbo) g_bufTrash.push_back(vbo); if (ibo) g_bufTrash.push_back(ibo); texId = vbo = ibo = 0; } v = std::move(o.v); n = std::move(o.n); col = std::move(o.col); uv = std::move(o.uv); idx = std::move(o.idx); memcpy(centre, o.centre, sizeof centre); memcpy(texAvg, o.texAvg, sizeof texAvg); tex = std::move(o.tex); texW = o.texW; texH = o.texH; texId = o.texId; o.texId = 0; texp = std::move(o.texp);
    vbo = o.vbo; ibo = o.ibo; offN = o.offN; offUv = o.offUv; offCol = o.offCol; noVbo = o.noVbo; o.vbo = o.ibo = 0; return *this; }
  FrameMesh(const FrameMesh& o) { v = o.v; n = o.n; col = o.col; uv = o.uv; idx = o.idx; memcpy(centre, o.centre, sizeof centre); memcpy(texAvg, o.texAvg, sizeof texAvg); tex = o.tex; texW = o.texW; texH = o.texH; texId = 0; noVbo = o.noVbo; texp = o.texp; }
  ~FrameMesh() { std::lock_guard<std::mutex> l(g_texTrashMx); if (texId) g_texTrash.push_back(texId); if (vbo) g_bufTrash.push_back(vbo); if (ibo) g_bufTrash.push_back(ibo); }
};
// Image features of a textured frame for the feature position search (rec_feat.h). Keypoints carry the 3D point of
// the nearest textured vertex in FRAME coordinates; world placement is applied at search time.
// A bank entry is a WINDOW of 5 consecutive textured frames (keypoints + 3D points + a point grid for the geometric
// check), all in SCAN-LOCAL coordinates (per-frame transform only), so a later move of the scan does not stale it.
struct FrameFeat { int frame = -1; feat::Frame f; feat::Grid grid; };
static bool frameFeatures(const IFrameMesh* fm, int targetW, feat::Frame& out) {
  IImage* img = fm->getImage(); IArrayUVCoordinates* uvs = fm->getUVCoordinates(); IArrayPoint3F* pts = fm->getPoints(); if (!img || !uvs || !pts) return false;
  int w = img->getWidth(), h = img->getHeight(), pitch = img->getPitch(); PixelFormat pf = img->getPixelFormat(); int ch = pf == PixelFormat_BGR || pf == PixelFormat_RGB ? 3 : pf == PixelFormat_BGRA || pf == PixelFormat_RGBA ? 4 : pf == PixelFormat_Mono ? 1 : 0; if (!ch || w < 64 || h < 64) return false;
  int nv = pts->getSize(); if (uvs->getSize() != nv || nv < 100) return false;
  feat::Image im = feat::toGrey((const uint8_t*)img->getPointer(), w, h, pitch, ch, targetW);
  feat::build(im, (const float*)uvs->getPointer(), (const float*)pts->getPointer(), nv, false, out); return out.n() >= 20;
}
static void framePointsT(const IFrameMesh* fm, const double M[4][4], int step, std::vector<float>& out) { IArrayPoint3F* pts = fm->getPoints(); if (!pts) return; const Point3F* p = pts->getPointer(); for (int i = 0; i < pts->getSize(); i += step) { double x = p[i].x, y = p[i].y, z = p[i].z; out.push_back((float)(M[0][0] * x + M[0][1] * y + M[0][2] * z + M[0][3])); out.push_back((float)(M[1][0] * x + M[1][1] * y + M[1][2] * z + M[1][3])); out.push_back((float)(M[2][0] * x + M[2][1] * y + M[2][2] * z + M[2][3])); } }
// copy a frame's texture image as RGB, halved in each direction
static std::atomic<long long> g_cbWaitMaxUs{0}, g_drawMaxUs{0};   // Round 3 (R3-03): longest frame-lock wait in the callback / longest draw, per minute
static void grabTexture(const IFrameMesh* f, FrameMesh& out) {
  IImage* img = f->getImage(); IArrayUVCoordinates* uvs = f->getUVCoordinates(); if (!img || !uvs) return; int w = img->getWidth(), h = img->getHeight(), pitch = img->getPitch(); PixelFormat pf = img->getPixelFormat();
  int ch = pf == PixelFormat_BGR || pf == PixelFormat_RGB ? 3 : pf == PixelFormat_BGRA || pf == PixelFormat_RGBA ? 4 : pf == PixelFormat_Mono ? 1 : 0;
  if (!ch) { static bool once = false; if (!once) { once = true; logf("texture image format %d not handled (%dx%d)", (int)pf, w, h); } return; } if (w < 4 || h < 4) return; bool bgr = pf == PixelFormat_BGR || pf == PixelFormat_BGRA;
  const unsigned char* p = (const unsigned char*)img->getPointer(); int ow = w / 2, oh = h / 2; out.tex.resize((size_t)ow * oh * 3); out.texW = ow; out.texH = oh;
  // display brightening like Studio's texture view. Measured 2 Nov: raw frames already average ~199/255, so the earlier
  // gamma 1/1.7 washed them to cream. Mild gamma only. Built once, thread-safe (review C11: callback, tidy-up and align threads).
  struct Lut { unsigned char v[1024]; Lut() { for (int i = 0; i < 1024; i++) { double x = pow(i / 1020.0, 1.0 / 1.3) * 255.0 + 0.5; v[i] = (unsigned char)(x > 255 ? 255 : x); } } }; static const Lut lutTab; const unsigned char* lut = lutTab.v;
  // Exposure normalisation: the raw camera frame (0.2 ms shutter, flash) is dark and its brightness falls with the
  // flash distance. Studio shows every frame at an even brightness, so scale each frame so that its mean luminance
  // lands on a fixed target before the display gamma. Mean over every 4th pixel of the full-res image.
  double sum = 0; int cnt = 0;
  for (int y = 0; y < h; y += 4) { const unsigned char* row = p + (size_t)y * pitch; for (int x = 0; x < w; x += 4, cnt++) { const unsigned char* q = row + (size_t)x * ch; sum += ch == 1 ? q[0] : (q[0] + 2 * q[1] + q[2]) * 0.25; } }
  // Per-frame normalisation (tried 07:00) made the scan patchy: each frame got its own scale. The camera exposure is fixed,
  // so one fixed scale keeps neighbouring frames the same tone, as in Studio. The mean is only logged.
  double mean = cnt ? sum / cnt : 128.0; double k = 1.0;   // Studio's textured scans render bright, near saturation on a light wall
  auto L = [&](int s) -> unsigned char { int i = (int)(s * k + 0.5); return lut[i > 1020 ? 1020 : i]; };
  for (int y = 0; y < oh; y++) for (int x = 0; x < ow; x++) { const unsigned char* a = p + (size_t)(2 * y) * pitch + (size_t)(2 * x) * ch; const unsigned char* b = a + ch; const unsigned char* c = a + pitch; const unsigned char* d = c + ch; unsigned char* o = &out.tex[((size_t)y * ow + x) * 3];
    if (ch == 1) { o[0] = o[1] = o[2] = L(a[0] + b[0] + c[0] + d[0]); } else { int r = bgr ? 2 : 0, bl = bgr ? 0 : 2; o[0] = L(a[r] + b[r] + c[r] + d[r]); o[1] = L(a[1] + b[1] + c[1] + d[1]); o[2] = L(a[bl] + b[bl] + c[bl] + d[bl]); } }
  { double s0 = 0, s1 = 0, s2 = 0; size_t np = (size_t)ow * oh; for (size_t q = 0; q < np; q += 7) { s0 += out.tex[q * 3]; s1 += out.tex[q * 3 + 1]; s2 += out.tex[q * 3 + 2]; } double nn = (double)((np + 6) / 7) * 255.0; out.texAvg[0] = (float)(s0 / nn); out.texAvg[1] = (float)(s1 / nn); out.texAvg[2] = (float)(s2 / nn); }
  static int logK = 0; if (logK++ < 3) logf("texture frame mean %.1f/255, exposure scale %.2f", mean, k);
  int n = uvs->getSize(); const UVCoordinates* u = uvs->getPointer(); out.uv.resize((size_t)n * 2); for (int k = 0; k < n; k++) { out.uv[k * 2] = u[k].u; out.uv[k * 2 + 1] = u[k].v; }
  static int logged = 0; if (logged++ < 2) { logf("texture frame: image %dx%d pitch %d format %d channels %d, uv %d (first %.3f,%.3f)", w, h, pitch, (int)pf, ch, n, n ? u[0].u : 0.f, n ? u[0].v : 0.f);
    if (logged == 1 && getenv("ARTECREC_DEBUG")) { FILE* bf = _wfopen(L"tex-debug.bmp", L"wb"); if (bf) { int rowB = ow * 3, pad = (4 - rowB % 4) % 4; BITMAPFILEHEADER fh = {0x4D42, (DWORD)(54 + (rowB + pad) * oh), 0, 0, 54}; BITMAPINFOHEADER ih = {40, ow, oh, 1, 24, 0, (DWORD)((rowB + pad) * oh)}; fwrite(&fh, 14, 1, bf); fwrite(&ih, 40, 1, bf); unsigned char z[3] = {0, 0, 0};
      for (int y = oh - 1; y >= 0; y--) { for (int x = 0; x < ow; x++) { const unsigned char* o = &out.tex[((size_t)y * ow + x) * 3]; unsigned char b3[3] = {o[2], o[1], o[0]}; fwrite(b3, 3, 1, bf); } if (pad) fwrite(z, pad, 1, bf); } fclose(bf); } } }
}
// UI scale (owner, 7 Nov: "the font sizes are too small"). Body text 21 px (about 16 pt), buttons at least 42 px tall,
// panel rows 28 px: readable at arm's length in daylight. Every layout number in the panels and the 3D-view overlay is a
// design value that uik() scales, so one constant sets the whole UI.
static const float UI_K = 1.25f; static int uik(int v) { return (int)(v * UI_K + 0.5f); }
struct ScanRec {
  IScan* scan = nullptr; std::wstring name; int group = -1; bool visible = true;
  int frames = 0, texFrames = 0, keyFrames = 0; double maxErr = 0; std::vector<FrameMesh> disp; float color[3] = {0.72f, 0.72f, 0.70f}; float texAvg[3] = {0.7f, 0.7f, 0.68f}; bool hasTexAvg = false;
  IScan* ref = nullptr;   // thinned copy (≤40 frames, normals ready) used as the position-search reference; built once in the background
  bool placed = false;    // placed by the live position search (hybrid resume / auto-align) — do not re-align it after Stop
  std::vector<FrameFeat> feats;   // image-feature windows (feature position search), scan-local, built in the background
};
struct GroupRec { std::wstring name; bool visible = true; };

// ---------------------------------------------------------------- settings (UI thread writes, SDK threads read)
struct Settings {
  int tracking = 0;              // 0 geometry+texture, 1 geometry
  std::atomic<bool> distColor{true};   // read on the scanning and tidy-up threads (DeepSeek 2)
  bool autoAlign = false, hidePanel = false, recordTexture = true, flash = true, autoBright = true, audio = true;
  bool scanUsingAutoAlign = true;   // Settings > Scan > "Scan using auto-alignment" (default on)
  bool fieldMode = true;            // 7 Nov (owner, museum day): ON by default. Record only; no clean-up, optimisation or auto-align after Stop - the passes are placed in the lab (Edit menu / Studio). Battery time is precious.
  int shotBg = 0;                   // screenshot background: 0 white, 1 black
  std::atomic<float> nearMm{170.f}, farMm{330.f};
  float sens = 0.0f, texBright = 0.0f; int speed = 8; int texFreq = 0 /*0 auto*/, texEvery = 1; int delaySec = 0; bool limitFrames = false; int frameLimit = 2000;
  std::wstring scanName;
};
static Settings S;

// ---------------------------------------------------------------- recorder state
enum Mode { M_Idle, M_Preview, M_Record, M_Paused, M_Stopping, M_Aligning, M_Busy };
static std::atomic<int> g_mode{M_Idle};
static IScanner* g_scanner; static ScannerId g_scannerId; static IScanningProcedure* g_proc; static IModel* g_master; static IModel* g_jobOut;
static std::vector<ScanRec> g_scans; static std::vector<GroupRec> g_groups; static std::mutex g_scanMx;
static ICancellationTokenSource* g_cts = nullptr; static std::mutex g_ctsMx; static std::atomic<bool> g_stopAsked{false}, g_cancelSent{false}; static clk::time_point g_stopAt;
static std::thread g_job; static std::atomic<bool> g_jobDone{false}; static std::atomic<ErrorCode> g_jobEc{ErrorCode_OK};
static std::atomic<long> g_frames{0}, g_regFail{0}, g_lost{0}, g_keyFrames{0}, g_texFrames{0}; static std::atomic<double> g_maxErr{0};
static std::atomic<long> g_cbFrames{0}; static clk::time_point g_procStart;   // frames delivered by the SDK since the procedure started (any mode) — a stalled capture shows as 0
static std::deque<clk::time_point> g_stamps; static std::mutex g_stmx;
static clk::time_point g_recStart; static double g_recSecs = 0;
// Review C5: the status line text is written by background threads and read by the UI every 40 ms: guarded string.
struct LockedText { std::mutex m; std::wstring s; void operator=(const std::wstring& v) { std::lock_guard<std::mutex> l(m); s = v; } void operator=(const wchar_t* v) { std::lock_guard<std::mutex> l(m); s = v; } std::wstring get() { std::lock_guard<std::mutex> l(m); return s; } void clear() { std::lock_guard<std::mutex> l(m); s.clear(); } };
static bool g_dirty = false; static std::wstring g_projPath; static LockedText g_alignMsg;
// Non-modal notice for the team (review B9: no message boxes while scanning); shown in the scan panel and status bar.
static LockedText g_notice; static void notice(const std::wstring& s) { g_notice = s; }
static bool g_projForeign = false;   // the open project holds Studio entries ArtecRec does not load (review A2)
static bool g_stuck = false;         // the scanner job was abandoned after a failed Stop/cancel (review B3): save, then restart
static std::wstring g_saveErr;       // why the last save failed, in plain words (review F6)
static std::wstring g_replaceOk;     // a target the user confirmed may replace an existing ArtecRec project (cloud review)
static std::vector<std::wstring> g_zombieSaveDirs;   // __saving folders of saves given up on this session (still being written) - DeepSeek 2-2
static std::atomic<bool> g_lostNow{false}; static std::atomic<int> g_countdown{0};
static HWND g_hwnd;
#define WM_JOBDONE   (WM_APP + 1)
#define WM_ALIGNDONE (WM_APP + 2)
#define WM_LOSTBEEP  (WM_APP + 3)
#define WM_SCANBTN   (WM_APP + 5)   // wParam = ScannerButton
#define WM_SCANDISC  (WM_APP + 6)
static std::atomic<bool> g_overheated{false};
static float g_gainDefault = 0, g_sensDefault = 0.5f;   // scanner defaults = Studio's "Normal"
static int g_resumeGroupWith = -1;   // scan index to group the next (resumed) scan with, Studio-style
struct ScanObs : public ScannerObserverBase {
  void buttonPressed(ScannerButton b) override { PostMessageW(g_hwnd, WM_SCANBTN, (WPARAM)b, 0); }
  void deviceOverheated() override { g_overheated = true; logf("scanner overheated"); }
  void deviceTemperatureBackToNormal() override { g_overheated = false; logf("scanner temperature back to normal"); }
  void deviceDisconnected() override { logf("scanner disconnected"); PostMessageW(g_hwnd, WM_SCANDISC, 0, 0); }
};
static ScanObs* g_scanObs;

// live auto-align ("Searching for position"): the first frames of a new scan are matched against the reference scans
static std::atomic<bool> g_alignLive{false}, g_liveFound{false}, g_searchBusy{false}, g_searchStop{false}; static bool g_segPushedAtPause = false; /* the last Pause/Resume segment was already recorded at Pause (C6) */ static std::atomic<int> g_searchGen{0}; static std::atomic<bool> g_searchZombie{false}; /* cloud review: a detached search must not publish; cleaning waits for it too */ static double g_liveT[4][4]; static std::mutex g_liveTmx; static bool g_forceAlign = false;
static std::mutex g_joinMx; static std::vector<int> g_joinedTo;   /* scans (indices) the live search joined this recording to; they become anchors at Stop */
static bool g_afterLoss = false;   /* the running procedure was started by a tracking-loss restart (artecrec.cpp) */
static void noteJoin(int idx) { std::lock_guard<std::mutex> l(g_joinMx); g_joinedTo.push_back(idx); }
static std::atomic<long> g_foundAt{0}, g_storedFrames{0};   // frames captured before the position was found are discarded, as Studio does not record them
static std::vector<bool> g_frameBad; static std::mutex g_frameBadMx;   // per stored frame: registration failed (Studio records no such frame)
static IScan* g_tempScan; static IScan* g_tempTex; static std::atomic<int> g_tempCount{0}; static std::mutex g_tempMx; struct AtomicTime { std::atomic<long long> t{0}; void operator=(clk::time_point v) { t = v.time_since_epoch().count(); } operator clk::time_point() const { return clk::time_point(clk::duration(t.load())); } };
static AtomicTime g_lastSearch; static std::thread g_searchThread; static std::atomic<int> g_searchTries{0};   // g_tempTex: latest textured probe frames for the hybrid resume registration
static void setIdentity(double M[4][4]) { for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) M[i][j] = i == j ? 1.0 : 0.0; }
// live view data (SDK thread writes under g_mx)
static std::mutex g_mx;
static FrameMesh g_live; static std::vector<FrameMesh> g_liveAcc; static float g_liveCentre[3] = {0, 0, 0};
static float g_liveTexAvg[3] = {0.7f, 0.7f, 0.68f}; static bool g_liveTexSet = false;
static double g_liveRot[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; static bool g_liveRotSet = false;   // inverse scanner rotation (GL column-major): the view follows the scanner like Studio
static std::vector<float> g_hist; static int g_histN = 32; static std::atomic<bool> g_newData{false};   /* set by the scanning thread, cleared by the UI (DeepSeek 5-1) */
static std::deque<std::vector<float>> g_histKeys; static std::vector<float> g_histKey;   // Studio's grey histogram: registered key frames (last 20)
static const size_t LIVE_ACC_MAX = 150;

// Studio's distance colour bands: red = too close, orange = near end of the range, green = optimal (most of the range),
// blue = too far. Flat bands, so a surface at a good distance is a smooth green.
static void distColor(float d, float& r, float& g, float& b) {
  float n = S.nearMm.load(), f = S.farMm.load(); float span = f - n;
  if (d < n) { r = 0.95f; g = 0.30f; b = 0.25f; return; }
  if (d > f) { r = 0.35f; g = 0.50f; b = 1.0f; return; }
  if (d < n + span * 0.15f) { r = 0.95f; g = 0.60f; b = 0.20f; return; }   // orange band near the close limit
  if (d > f - span * 0.10f) { r = 0.45f; g = 0.75f; b = 0.95f; return; }   // light blue band near the far limit
  r = 0.30f; g = 0.80f; b = 0.32f; }

// Build a display mesh from an SDK frame: positions transformed by M (row-major 4x4), normals rotated, triangles copied.
static void buildFrameMesh(const IFrameMesh* f, const double M[4][4], FrameMesh& out, bool colour, int stride) {
  IArrayPoint3F* pts = f->getPoints(); if (!pts) return; int n = pts->getSize(); const Point3F* p = pts->getPointer();
  IArrayPoint3F* nrm = f->getPointsNormals(); const Point3F* pn = (nrm && nrm->getSize() == n) ? nrm->getPointer() : nullptr;
  IArrayIndexTriplet* tri = f->getTriangles(); int nt = tri ? tri->getSize() : 0; const IndexTriplet* t = nt ? tri->getPointer() : nullptr;
  out.v.resize((size_t)n * 3); out.n.assign((size_t)n * 3, 0.f); if (colour) out.col.resize((size_t)n * 3); else out.col.clear();
  double cx = 0, cy = 0, cz = 0;
  for (int k = 0; k < n; k++) {
    double x = p[k].x, y = p[k].y, z = p[k].z;
    out.v[k * 3] = (float)(M[0][0] * x + M[0][1] * y + M[0][2] * z + M[0][3]); out.v[k * 3 + 1] = (float)(M[1][0] * x + M[1][1] * y + M[1][2] * z + M[1][3]); out.v[k * 3 + 2] = (float)(M[2][0] * x + M[2][1] * y + M[2][2] * z + M[2][3]);
    cx += out.v[k * 3]; cy += out.v[k * 3 + 1]; cz += out.v[k * 3 + 2];
    if (pn) { double a = pn[k].x, b = pn[k].y, c = pn[k].z; out.n[k * 3] = (float)(M[0][0] * a + M[0][1] * b + M[0][2] * c); out.n[k * 3 + 1] = (float)(M[1][0] * a + M[1][1] * b + M[1][2] * c); out.n[k * 3 + 2] = (float)(M[2][0] * a + M[2][1] * b + M[2][2] * c); }
    if (colour) distColor((float)-z, out.col[k * 3], out.col[k * 3 + 1], out.col[k * 3 + 2]);
  }
  if (n) { out.centre[0] = (float)(cx / n); out.centre[1] = (float)(cy / n); out.centre[2] = (float)(cz / n); }
  // Keep triangles whose vertices are in range and whose edges are short (Spider point spacing ~0.3 mm; long edges are
  // bridges across gaps that Studio does not draw either).
  // Also drop triangles seen at a grazing angle from the camera (Studio's "max angle" frame filter): in frame
  // coordinates the camera is at the origin looking down -z.
  // 2 Nov: the grazing-angle filter (minCos 0.3) and the 1.5 mm edge limit were cutting holes into frames that the
  // SDK had reconstructed whole; tilted re-scans then could not close them. Studio draws the frame as reconstructed.
  // 4 Nov 12:30, owner: "I don't want any part of the scan to be dropped" — Studio draws every triangle, so does ArtecRec now.
  out.idx.clear(); out.idx.reserve((size_t)nt * 3); const float maxEdge2 = 1e12f, minCos = 0.0f;
  for (int k = 0; k < nt; k++) { unsigned a = t[k].x, b = t[k].y, c = t[k].z; if (a >= (unsigned)n || b >= (unsigned)n || c >= (unsigned)n) continue;
    const float* A = &out.v[a * 3]; const float* B = &out.v[b * 3]; const float* C = &out.v[c * 3];
    auto d2 = [](const float* P, const float* Q) { float dx = P[0] - Q[0], dy = P[1] - Q[1], dz = P[2] - Q[2]; return dx * dx + dy * dy + dz * dz; };
    if (d2(A, B) > maxEdge2 || d2(B, C) > maxEdge2 || d2(C, A) > maxEdge2) continue;
    float ux = p[b].x - p[a].x, uy = p[b].y - p[a].y, uz = p[b].z - p[a].z, wx = p[c].x - p[a].x, wy = p[c].y - p[a].y, wz = p[c].z - p[a].z;
    float nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx; float nl = sqrtf(nx * nx + ny * ny + nz * nz);
    float vx = -(p[a].x + p[b].x + p[c].x) / 3, vy = -(p[a].y + p[b].y + p[c].y) / 3, vz = -(p[a].z + p[b].z + p[c].z) / 3; float vl = sqrtf(vx * vx + vy * vy + vz * vz);
    if (nl > 1e-9f && vl > 1e-6f && fabsf(nx * vx + ny * vy + nz * vz) / (nl * vl) < minCos) continue;
    out.idx.push_back(a); out.idx.push_back(b); out.idx.push_back(c); }
  nt = (int)(out.idx.size() / 3);
  if (!pn && nt) {   // no normals from the SDK: accumulate face normals
    for (int k = 0; k < nt; k++) { unsigned a = out.idx[k * 3], b = out.idx[k * 3 + 1], c = out.idx[k * 3 + 2]; if (a >= (unsigned)n || b >= (unsigned)n || c >= (unsigned)n) continue;
      float* A = &out.v[a * 3]; float* B = &out.v[b * 3]; float* C = &out.v[c * 3]; float u[3] = {B[0] - A[0], B[1] - A[1], B[2] - A[2]}, w[3] = {C[0] - A[0], C[1] - A[1], C[2] - A[2]};
      float nx = u[1] * w[2] - u[2] * w[1], ny = u[2] * w[0] - u[0] * w[2], nz = u[0] * w[1] - u[1] * w[0];
      for (unsigned i : {a, b, c}) { out.n[i * 3] += nx; out.n[i * 3 + 1] += ny; out.n[i * 3 + 2] += nz; } }
    for (int k = 0; k < n; k++) { float* N = &out.n[k * 3]; float l = sqrtf(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]); if (l > 1e-6f) { N[0] /= l; N[1] /= l; N[2] /= l; } }
  }
  (void)stride;
}

// Deep copy of a frame mesh (points, triangles, normals, texture image + UVs) — used for the position-search probe.
static IFrameMesh* cloneFrame(const IFrameMesh* f) {
  IArrayPoint3F* pts = f->getPoints(); IArrayIndexTriplet* tri = f->getTriangles(); if (!pts || !tri) return nullptr; int n = pts->getSize(), nt = tri->getSize();
  IFrameMesh* m = nullptr; if (createFrameMesh(&m, n, nt) != ErrorCode_OK || !m) return nullptr;
  IArrayPoint3F* p2 = nullptr; createArrayPoint3F(&p2, n); memcpy(p2->getPointer(), pts->getPointer(), (size_t)n * sizeof(Point3F)); m->setPoints(p2); p2->release();
  IArrayIndexTriplet* t2 = nullptr; createArrayIndexTriplet(&t2, nt); memcpy(t2->getPointer(), tri->getPointer(), (size_t)nt * sizeof(IndexTriplet)); m->setTriangles(t2); t2->release();
  IArrayPoint3F* nr = f->getPointsNormals(); if (nr && nr->getSize() == n) { IArrayPoint3F* n2 = nullptr; createArrayPoint3F(&n2, n); memcpy(n2->getPointer(), nr->getPointer(), (size_t)n * sizeof(Point3F)); m->setPointsNormals(n2); n2->release(); }
  if (f->isTextured()) { IImage* img = f->getImage(); IArrayUVCoordinates* uv = f->getUVCoordinates(); if (img && uv) { IImage* i2 = nullptr; if (img->clone(&i2) == ErrorCode_OK && i2) { m->setImage(i2); i2->release(); }
      IArrayUVCoordinates* u2 = nullptr; createArrayUVCoordinates(&u2, uv->getSize()); memcpy(u2->getPointer(), uv->getPointer(), (size_t)uv->getSize() * sizeof(UVCoordinates)); m->setUVCoordinates(u2); u2->release(); } }
  return m;
}
struct Obs : public ScanningProcedureObserverBase {
  void onFrameCaptured(const RegistrationInfo*) override {}
  void onFrameScanned(const RegistrationInfo* i) override {
    if (!i) return; bool rec = g_mode.load() == M_Record; g_cbFrames++;
    { std::lock_guard<std::mutex> l(g_stmx); auto now = clk::now(); g_stamps.push_back(now); while (!g_stamps.empty() && std::chrono::duration<double>(now - g_stamps.front()).count() > 3.0) g_stamps.pop_front(); }
    bool stored = i->frameState == FrameState_Ok || i->frameState == FrameState_RegistrationFailed || i->frameState == FrameState_TextureMappingFailed;   // states that generate a frame in the scan
    size_t storedIdx; { std::lock_guard<std::mutex> l(g_frameBadMx); storedIdx = g_frameBad.size(); }
    // A negative error on a state-Ok frame means "not registered" (normal for the first frames); such frames are dropped
    // from the scan but do not end it. Tracking is "lost" on a RegistrationFailed state or three unregistered frames in a row.
    bool unreg = rec && i->registrationError < 0 && storedIdx > 0; static int unregRun = 0; unregRun = unreg ? unregRun + 1 : 0;
    bool lost = i->frameState == FrameState_RegistrationFailed || (unregRun >= 3 && g_storedFrames.load() > 0);   // warm-up frames before the first registered one are not a loss
    if (rec && stored) { std::lock_guard<std::mutex> l(g_frameBadMx); g_frameBad.push_back(unreg || i->frameState == FrameState_RegistrationFailed); }
    if (rec && i->frameState == FrameState_Ok && !unreg) g_storedFrames++;   // frames the SDK keeps (ignoreRegistrationErrors = false)
    if (rec) { g_frames++; if (i->frameState == FrameState_RegistrationFailed) g_regFail++; if (lost) g_lost++; if (i->geometryKeyFrame) g_keyFrames++; if (i->textureKeyFrame || i->rawTexture) g_texFrames++;
      if (i->registrationError > g_maxErr.load()) g_maxErr = i->registrationError; }
    bool wasLost = g_lostNow.exchange(lost); if (rec && lost && !wasLost) PostMessageW(g_hwnd, WM_LOSTBEEP, 0, 0);
    const IFrameMesh* f = i->frame; if (!f) return;
    if (rec && g_alignLive.load() && !g_liveFound.load() && !lost && i->frameState == FrameState_Ok) {   // collect frames for the position search (sliding window of the latest 24)
      // The callback frame is reference-counted; IScan::add holds its own reference. A deep copy (cloneFrame) was tried on
      // 2 Nov: the copies lack the SDK's internal feature data and the auto-align then fails ("cannot take sample for RANSAC").
      // The probe is a sliding window of the latest 12 registered frames: after lost tracking the first frames point
      // away from the object (3 Nov 04:13, "not found" once, then nothing), the match must use where the scanner is now.
      std::lock_guard<std::mutex> l(g_tempMx); if (g_tempScan) { IFrameMesh* nf = const_cast<IFrameMesh*>(f); while (g_tempScan->getSize() >= 24) g_tempScan->remove(0); if (g_tempScan->add(nf, i->transformation) == ErrorCode_OK) g_tempCount++;
        if (g_tempTex && f->isTextured()) { while (g_tempTex->getSize() >= 12) g_tempTex->remove(0); g_tempTex->add(nf, i->transformation); } } }
    // live frames are drawn under g_liveT: the found position, or before that the starting guess (end of the previous
    // pass, owner 4 Nov 17:40: "the scan window should show where we are trying to align, at the last place before Stop")
    double M[4][4]; { std::lock_guard<std::mutex> l(g_liveTmx); if (g_liveFound.load() || g_alignLive.load()) mul44(g_liveT, i->transformation.m, M); else memcpy(M, i->transformation.m, sizeof M); }
    FrameMesh m; buildFrameMesh(f, M, m, S.distColor, 1);
    // depth histogram of this frame
    std::vector<float> h(g_histN, 0.f); IArrayPoint3F* pts = f->getPoints(); if (pts) { int n = pts->getSize(); const Point3F* p = pts->getPointer(); float lo = S.nearMm.load() - 40, hi = S.farMm.load() + 40;
      for (int k = 0; k < n; k += 4) { float d = -p[k].z; int b = (int)((d - lo) / (hi - lo) * g_histN); if (b >= 0 && b < g_histN) h[b] += 1; } float mx = 1; for (float x : h) mx = x > mx ? x : mx; for (float& x : h) x /= mx; }
    // Round 3: build the accumulated copy (and grab its texture) outside the lock; measure how long the lock is waited for
    bool accum = rec && !lost && i->frameState == FrameState_Ok && (i->geometryKeyFrame || (g_frames.load() % 2 == 0)); FrameMesh k; if (accum) { k = FrameMesh(m); k.col.clear(); if (f->isTextured()) grabTexture(f, k); }
    auto tw = clk::now(); std::lock_guard<std::mutex> l(g_mx); { long long us = std::chrono::duration_cast<std::chrono::microseconds>(clk::now() - tw).count(); long long prev = g_cbWaitMaxUs.load(); while (us > prev && !g_cbWaitMaxUs.compare_exchange_weak(prev, us)) {} }
    memcpy(g_liveCentre, m.centre, sizeof g_liveCentre);
    for (int c = 0; c < 3; c++) for (int r = 0; r < 3; r++) g_liveRot[c * 4 + r] = M[c][r]; g_liveRotSet = true;   // R^T of the frame pose
    if (rec && i->geometryKeyFrame && !lost) { g_histKeys.push_back(h); if (g_histKeys.size() > 20) g_histKeys.pop_front(); g_histKey.assign(g_histN, 0.f); for (auto& hk : g_histKeys) for (int b = 0; b < g_histN; b++) g_histKey[b] += hk[b]; float mx = 1; for (float x : g_histKey) mx = x > mx ? x : mx; for (float& x : g_histKey) x /= mx; }
    g_hist = std::move(h);
    // Studio accumulates every registered frame in the live view, which is how re-scanning from another angle fills
    // holes on screen. Key frames only (earlier) left the holes visible until Stop.
    // 5 Nov, owner: Studio shows the area covered in this pass as a dense green; keep the distance colours on the
    // accumulated frames when "Show distance color" is on (they are drawn flat, unlit — rec_view.h).
    if (accum) { if (f->isTextured()) { if (!k.uv.empty()) { for (int a = 0; a < 3; a++) g_liveTexAvg[a] = g_liveTexSet ? g_liveTexAvg[a] * 0.8f + k.texAvg[a] * 0.2f : k.texAvg[a]; g_liveTexSet = true; } }   // Studio shows captured surfaces textured while scanning
      if (g_liveAcc.size() >= LIVE_ACC_MAX) { std::vector<FrameMesh> half; for (size_t j = 0; j < g_liveAcc.size(); j += 2) half.push_back(std::move(g_liveAcc[j])); g_liveAcc = std::move(half); } g_liveAcc.push_back(std::move(k)); }
    g_live = std::move(m); g_live.noVbo = true; g_newData = true;
  }
  // review C4: the search holds the procedure's own live frames; the SDK post-processes their textures after this
  // call, so the search is stopped and given up to 5 s to let go of them first.
  void onScanningFinished(int) override { g_searchStop = true; for (int i = 0; i < 100 && g_searchBusy.load(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(50)); if (g_searchBusy.load()) logf("the position search did not stop within 5 s of the end of the scan");
    { std::lock_guard<std::mutex> l(g_tempMx); if (g_tempScan) { g_tempScan->release(); g_tempScan = nullptr; } if (g_tempTex) { g_tempTex->release(); g_tempTex = nullptr; } } }   // the SDK asks for its textures to be released here
};
static Obs* g_obs;

// replay source (rec_replay.h): frames from a saved pass in place of the scanner; see --replay-bench in artecrec.cpp
namespace replay { inline bool on = false; static bool start(ScanningState initial); static void setState(ScanningState s); static void stopNow(); }
// ---------------------------------------------------------------- scanner / procedure
static void disconnectScanner() { if (g_scanner) { g_scanner->setObserver(nullptr); g_scanner->release(); g_scanner = nullptr; } if (g_scanObs) { g_scanObs->release(); g_scanObs = nullptr; } }
static bool connectScanner() {
  IArrayScannerId* list = nullptr; ErrorCode ec = enumerateScanners(&list);
  if (ec != ErrorCode_OK || !list || list->getSize() < 1) { static clk::time_point lastLog; if (std::chrono::duration<double>(clk::now() - lastLog).count() > 60) { lastLog = clk::now(); logf("enumerateScanners 0x%08X (logged at most once a minute while looking)", (unsigned)ec); } return false; }
  g_scannerId = *list->getPointer(); ec = createScanner(&g_scanner, list->getPointer()); list->release();
  if (ec != ErrorCode_OK || !g_scanner) { logf("createScanner 0x%08X", (unsigned)ec); g_scanner = nullptr; return false; }
  char ser[64]; wcstombs(ser, g_scannerId.serial, 63); logf("scanner %s connected, fps %.1f max %.1f, texture gain %.2f..%.2f", ser, g_scanner->getFPS(), g_scanner->getMaximumFPS(), g_scanner->getTextureGainMin(), g_scanner->getTextureGainMax());
  g_scanObs = new ScanObs(); g_scanner->setObserver(g_scanObs); g_gainDefault = g_scanner->getTextureGain();
  logf("default texture gain %.2f, shutter %.2f ms, illumination %.4f, auto exposure %d, auto white balance %d", g_gainDefault, g_scanner->getTextureShutterSpeed(), g_scanner->getTextureIllumination(), (int)g_scanner->isAutoExposureEnabled(), (int)g_scanner->isAutoWhiteBalanceEnabled());
  return true;
}
static void applyLiveSettings() {
  if (!g_scanner) return;
  g_scanner->setFPS((float)S.speed); g_scanner->enableTextureFlash(S.flash); g_scanner->enableAutoExposure(S.autoBright); g_scanner->enableAutoWhiteBalance(true);
  float hi = g_scanner->getTextureGainMax(); if (hi > g_gainDefault) g_scanner->setTextureGain(g_gainDefault + (hi - g_gainDefault) * S.texBright);   // Normal = default gain, High = max
  if (g_proc) { g_proc->setScanningRange(S.nearMm.load(), S.farMm.load()); g_proc->setSensitivity(g_sensDefault + (1.f - g_sensDefault) * S.sens); }   // Normal = default, Extreme = 1
}
static void joinSearchBounded();
// Reset per-scan state; arm the live position search when recording with references (called at Record, also from Preview).
static std::atomic<int> g_hybridNext{0};   // rotation index over the hybrid-resume windows
static void prepareRecording(bool recording) {
  g_segPushedAtPause = false;   // a new segment starts
  // review C8: a search still running from before Pause/Resume or a restart is stopped and joined before the state is
  // reset, so its late result cannot place the new pass (its jobs are cancelled through g_searchStop, 20 s at most)
  joinSearchBounded(); g_searchStop = false;
  { std::lock_guard<std::mutex> l(g_tempMx); if (g_tempScan) { g_tempScan->release(); g_tempScan = nullptr; } if (g_tempTex) { g_tempTex->release(); g_tempTex = nullptr; } g_tempCount = 0; }
  int refs = 0; { std::lock_guard<std::mutex> l(g_scanMx); for (auto& s : g_scans) if (s.visible && s.scan && (s.group < 0 || (s.group < (int)g_groups.size() && g_groups[s.group].visible))) refs++; }
  g_liveFound = false; g_foundAt = 0; g_searchTries = 0; g_hybridNext = 0; g_lastSearch = clk::now(); { std::lock_guard<std::mutex> l(g_frameBadMx); g_frameBad.clear(); }
  { // starting guess for the new pass: the pose of the last frame of the most recent visible pass (the SDK starts every
    // procedure at the origin, which put the live view at the START of pass 1 — owner, 4 Nov 17:3x)
    double G[4][4]; setIdentity(G); { std::lock_guard<std::mutex> l(g_scanMx); for (int i = (int)g_scans.size() - 1; i >= 0; i--) { auto& s = g_scans[i]; if (!(s.visible && s.scan && s.scan->getSize() > 0 && (s.group < 0 || (s.group < (int)g_groups.size() && g_groups[s.group].visible)))) continue; mul44(s.scan->getScanTransformation().m, s.scan->getTransformation(s.scan->getSize() - 1).m, G); break; } }
    std::lock_guard<std::mutex> l(g_liveTmx); memcpy(g_liveT, G, sizeof G); }
  g_frames = 0; g_regFail = 0; g_lost = 0; g_keyFrames = 0; g_texFrames = 0; g_maxErr = 0; g_storedFrames = 0;
  g_alignLive = (S.autoAlign || g_forceAlign) && refs > 0 && recording;   // g_forceAlign: resuming after lost tracking (Studio's "Scan using auto-alignment" works regardless of the Scan-panel checkbox)
  if (g_alignLive) { std::lock_guard<std::mutex> l(g_tempMx); createScan(&g_tempScan, g_scannerId.type); createScan(&g_tempTex, g_scannerId.type); }
}
// Review B1 (second pass): the UI never joins the search without a bound. Stop it, wait up to 10 s, then let it go.
static void joinSearchBounded() { g_searchStop = true; for (int i = 0; i < 500 && g_searchBusy.load(); i++) { MSG pm; while (PeekMessageW(&pm, nullptr, WM_PAINT, WM_PAINT, PM_REMOVE)) DispatchMessageW(&pm); Sleep(20); }   /* DeepSeek 7-2: repaint while waiting (10 s max); paint only, so no handler re-enters onJobDone */
  if (g_searchThread.joinable()) { if (g_searchBusy.load()) { logf("the position search did not stop in 10 s: left to finish on its own (its result is ignored)"); ++g_searchGen; g_searchZombie = true; g_searchThread.detach(); notice(L"The position search is taking too long to stop. Live joining is paused until ArtecRec is restarted; recording, auto-align after Stop and saving still work."); } else g_searchThread.join(); } g_searchBusy = false; }   // DeepSeek 3-4
// Review C6: one live-search result per recorded segment (Pause/Resume makes one scan per segment).
static std::vector<logic::SegmentPose> g_segPoses;
static void pushSegmentPose() { logic::SegmentPose p; p.keyFrames = (int)g_keyFrames.load(); p.maxErr = g_maxErr.load(); p.found = g_alignLive.load() && g_liveFound.load(); /* DeepSeek 6-3: per-segment stats (prepareRecording resets the counters at Resume) */ { std::lock_guard<std::mutex> l(g_liveTmx); memcpy(p.T, g_liveT, sizeof p.T); } g_segPoses.push_back(p); }
// Review B3: a scanning job whose Stop and cancel both failed is abandoned: its thread is detached, its late WM_JOBDONE
// (if it ever comes) is ignored by generation, and the app goes to Idle with g_stuck set so the scans can be saved.
static std::atomic<int> g_jobGen{0};
static bool startProcedure(ScanningState initial) {
  { std::lock_guard<std::mutex> l(g_joinMx); g_joinedTo.clear(); }
  if (replay::on) return replay::start(initial);
  if (g_stuck) { logf("start refused: the scanner job is stuck; restart ArtecRec"); return false; } g_segPushedAtPause = false;
  if (!g_scanner) { logf("start refused: no scanner connected"); return false; }
  ScanningProcedureSettings st; initializeScanningProcedureSettings(&st);
  bool tex = S.recordTexture; bool hybrid = S.tracking == 0 && tex;
  st.maxFrameCount = 0;   // the frame limit is enforced by ArtecRec's timer (new scan, recording continues); the SDK's own cap ends the procedure for good (seen 2 Nov T35)
  st.registrationType = hybrid ? RegistrationAlgorithmType_Hybrid : RegistrationAlgorithmType_ICP; st.initialState = initial;
  st.scanningCallback = g_obs; st.ignoreRegistrationErrors = false; st.saveEmptySurfaces = false;   // SDK: only successfully registered frames go into the scan (Studio records nothing while tracking is lost)
  st.pipelineConfiguration = ScanningPipeline_CalculateNormals | ScanningPipeline_RegisterFrame | ScanningPipeline_FindGeometryKeyFrame | (tex ? (ScanningPipeline_ConvertTextures | ScanningPipeline_MapTexture) : 0);
  // "Automatic" = every 8th frame. The SDK's texture-key-frame mode was tried (2 Nov) and gave ~1 texture per scan on a
  // slowly moving scanner, too few for texturing and for the position search; Studio's "Automatic" is denser than that.
  st.captureTexture = !tex ? CaptureTextureMethod_NoTextures : CaptureTextureMethod_EveryNFrame; st.captureTextureFrequency = S.texFreq == 0 ? 8 : (S.texEvery < 1 ? 1 : S.texEvery);
  ErrorCode ec = createScanningProcedure(&g_proc, g_scanner, &st);
  if (ec != ErrorCode_OK || !g_proc) { logf("createScanningProcedure 0x%08X", (unsigned)ec); g_proc = nullptr; return false; }
  g_sensDefault = g_proc->getSensitivity(); applyLiveSettings();
  createModel(&g_jobOut); g_jobDone = false; g_cbFrames = 0; g_procStart = clk::now(); g_frames = 0; g_regFail = 0; g_lost = 0; g_keyFrames = 0; g_texFrames = 0; g_maxErr = 0; g_lostNow = false;
  prepareRecording(initial == ScanningState_Record);
  { std::lock_guard<std::mutex> l(g_mx); g_liveAcc.clear(); g_live = FrameMesh(); }
  ICancellationTokenSource* cts = nullptr; createCancellationTokenSource(&cts); { std::lock_guard<std::mutex> l(g_ctsMx); g_cts = cts; } g_stopAsked = false; g_cancelSent = false; g_segPoses.clear();
  int gen = ++g_jobGen; IScanningProcedure* proc = g_proc; IModel* jobOut = g_jobOut; proc->addRef(); jobOut->addRef();   // the thread owns its own references (it may be abandoned)
  g_job = std::thread([cts, gen, proc, jobOut] {
    IModel* in = nullptr; createModel(&in); AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = in; ws.out = jobOut; ws.threadsCount = std::thread::hardware_concurrency();
    if (cts) ws.cancellation = cts->getToken();
    ErrorCode ec = executeJob(proc, &ws); in->release();
    { std::lock_guard<std::mutex> l(g_ctsMx); if (g_cts == cts) g_cts = nullptr; } if (cts) cts->release();
    proc->release(); jobOut->release();
    if (gen != g_jobGen.load()) return;   // abandoned: the app has moved on
    g_jobEc = ec; g_jobDone = true; PostMessageW(g_hwnd, WM_JOBDONE, (WPARAM)gen, 0);
  });
  logf("procedure started (%s, texture=%d, tracking=%s, range %.0f..%.0f, sens %.2f, speed %d, texFreq %s)", initial == ScanningState_Record ? "record" : "preview", (int)tex, hybrid ? "geometry+texture" : "geometry",
       S.nearMm.load(), S.farMm.load(), S.sens, S.speed, S.texFreq == 0 ? "auto" : "every N"); return true;
}
// Review B1/B8: Stop also stops the position search, and the SDK's setState runs on a helper thread so a scanner that
// stopped answering cannot freeze the window (the 8 s cancel below runs from the same UI timer).
static void stopProcedure() { if (replay::on) { if (!g_jobDone) { g_mode = M_Stopping; g_searchStop = true; g_stopAsked = true; g_stopAt = clk::now(); replay::stopNow(); } return; }
  if (g_proc && !g_jobDone) { g_mode = M_Stopping; g_searchStop = true; if (!g_stopAsked) { g_stopAsked = true; g_stopAt = clk::now(); IScanningProcedure* p = g_proc; p->addRef(); std::thread([p] { p->setState(ScanningState_Stop); p->release(); }).detach(); } } }
// 5 Nov 12:01: the scanner dropped off USB mid-recording; the SDK procedure never returned from Stop (waitForTriggerReady
// errors, ~1,500 in 6 min) and ArtecRec sat in "Stopping" until killed in Task Manager — three unsaved scans lost.
// If Stop has not finished in 8 s, cancel the procedure through its cancellation token.
// Returns true when the job was abandoned just now (review B3): 8 s after Stop a cancel is sent; 12 s after the cancel,
// if the job still has not returned, it is given up so the scans already in the Workspace can be saved.
static bool cancelStuckProcedure() {
  if (g_jobDone || !g_stopAsked || !g_proc) return false; double since = std::chrono::duration<double>(clk::now() - g_stopAt).count();
  // second review: a long textured pass can legitimately take a while to finish its textures after Stop: 15 s, then 45 s
  if (!g_cancelSent && since >= 15.0) { g_cancelSent = true; std::lock_guard<std::mutex> l(g_ctsMx); if (g_cts) g_cts->cancel(); logf("Stop did not finish in 15 s (scanner not answering): procedure cancelled"); return false; }
  if (g_cancelSent && since >= 60.0) { logf("the scanning job did not end 45 s after the cancel: abandoned; the scans already recorded can be saved");
    ++g_jobGen; g_job.detach(); g_proc->release(); g_proc = nullptr; if (g_jobOut) { g_jobOut->release(); g_jobOut = nullptr; }
    g_searchStop = true; if (g_searchThread.joinable()) { if (g_searchBusy.load()) { ++g_searchGen; g_searchZombie = true; } g_searchThread.detach(); } g_searchBusy = false;
    { std::lock_guard<std::mutex> l(g_tempMx); if (g_tempScan) { g_tempScan->release(); g_tempScan = nullptr; } if (g_tempTex) { g_tempTex->release(); g_tempTex = nullptr; } } g_alignLive = false;
    { std::lock_guard<std::mutex> l(g_mx); g_liveAcc.clear(); g_live = FrameMesh(); }
    g_stuck = true; g_mode = M_Idle; return true; }
  return false; }
static IScan* buildRef(IScan* s, int cap);
static bool g_featSearch = false;   // image-feature position search (the "FS" build: exe name contains "artecrec-fs")
// Image-feature position search: match the latest textured probe frame against the feature banks of all visible
// scans, RANSAC a rigid pose from the 3D correspondences, then tighten with the SDK's textured global registration
// over [the matched frame's textured neighbours + the probe frames]. T = live transform for the new scan.
// `stop` (optional): an outside request to give up early (the position search is stopped this way at Stop, review B1).
template <class J> static ErrorCode execLimited(J* job, AlgorithmWorkset* ws, int maxSecs, const char* what, const std::atomic<bool>* stop = nullptr) {
  ICancellationTokenSource* cts = nullptr; createCancellationTokenSource(&cts); if (cts) ws->cancellation = cts->getToken();
  std::atomic<bool> fin{false}, gaveUp{false};
  std::thread wd([&] { for (int i = 0; i < maxSecs * 10 && !fin.load() && !(stop && stop->load()); i++) std::this_thread::sleep_for(std::chrono::milliseconds(100)); if (!fin.load() && cts) { gaveUp = true; cts->cancel(); if (stop && stop->load()) logf("%s: stopped", what); else logf("%s: no result in %d s, cancelled; scan kept as it was", what, maxSecs); } });
  ErrorCode ec = executeJob(job, ws); fin = true; wd.join(); ws->cancellation = nullptr; if (cts) cts->release();
  return gaveUp.load() && ec == ErrorCode_OK ? ErrorCode_OperationAborted : ec;
}
// No time limit (6 Nov): runs the job until it ends. Cancelled when *stop becomes true, or when the job reports progress
// and its progress has not moved for stallSecs (logic::watchVerdict). pct (optional) receives the progress in percent.
struct WatchProg : public ProgressInfoBase { std::atomic<long long> lastMs{0}; std::atomic<bool> any{false}; std::atomic<int>* pct = nullptr;
  static long long nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(clk::now().time_since_epoch()).count(); }
  void tick() { lastMs = nowMs(); any = true; }
  void report(int current, int total) override { tick(); if (pct) *pct = total > 0 ? (int)((long long)current * 100 / total) : -1; } void pulse() override { tick(); } void notify(DetailsInfo) override { tick(); } };
template <class J> static ErrorCode execWatched(J* job, AlgorithmWorkset* ws, const char* what, const std::atomic<bool>* stop, int stallSecs = 300, std::atomic<int>* pct = nullptr) {
  ICancellationTokenSource* cts = nullptr; createCancellationTokenSource(&cts); if (cts) ws->cancellation = cts->getToken();
  WatchProg* wp = new WatchProg(); wp->pct = pct; wp->lastMs = WatchProg::nowMs(); ws->progress = wp;
  std::atomic<bool> fin{false}, cancelled{false};
  std::thread wd([&] { while (!fin.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); if (fin.load() || cancelled.load()) continue;
      logic::WatchVerdict v = logic::watchVerdict(wp->any.load(), (WatchProg::nowMs() - wp->lastMs.load()) / 1000.0, stallSecs, stop && stop->load());
      if (v != logic::W_Run && cts) { cancelled = true; cts->cancel(); if (v == logic::W_CancelAsked) logf("%s: cancelled", what); else logf("%s: no progress for %d s (stuck), cancelled", what, stallSecs); } } });
  ErrorCode ec = executeJob(job, ws); fin = true; wd.join(); ws->cancellation = nullptr; ws->progress = nullptr; wp->release(); if (cts) cts->release();
  return cancelled.load() && ec == ErrorCode_OK ? ErrorCode_OperationAborted : ec;
}
struct ScanHold { IScan* p = nullptr; ~ScanHold() { if (p) p->release(); } };   // see the 11:55 note at hybridResume
static bool featureSearch(double T[4][4], int& inliers, int& matches, int& refScan, int& refFrame, double& secs) {
  inliers = 0; matches = -1; refScan = refFrame = -1; auto t0 = clk::now();
  // probe: the latest 3 textured frames at two image scales, keypoints and sample points in the LAST frame's coordinates
  feat::Frame probe; std::vector<float> probePts; Matrix4x4D Fin; int nProbe = 0;
  { std::lock_guard<std::mutex> l(g_tempMx); if (!g_tempTex || g_tempTex->getSize() < 1) return false; nProbe = g_tempTex->getSize(); Fin = g_tempTex->getTransformation(nProbe - 1); double FinInv[4][4]; invRigid(Fin.m, FinInv);
    for (int j = std::max(0, nProbe - 3); j < nProbe; j++) { IFrameMesh* pf = g_tempTex->getElement(j); if (!pf) continue; double rel[4][4]; mul44(FinInv, g_tempTex->getTransformation(j).m, rel);
      for (int sc = 0; sc < 2; sc++) { feat::Frame f; if (frameFeatures(pf, sc ? 240 : 320, f)) probe.append(f, rel); } framePointsT(pf, rel, 25, probePts); } }
  if (probe.n() < 40 || probePts.size() < 60) return false;
  matches = 0; double bestR[3][3], bestT[3]; int bestN = 0; int bestScan = -1, bestFrame = -1; float bestShare = 0, bestMed = 0;
  { std::lock_guard<std::mutex> l(g_scanMx);
    for (int i = (int)g_scans.size() - 1; i >= 0; i--) { auto& s = g_scans[i]; if (!(s.visible && s.scan && (s.group < 0 || g_groups[s.group].visible)) || s.feats.empty()) continue;
      for (auto& ff : s.feats) { auto m = feat::match(probe, ff.f); if ((int)m.size() < 12) continue; matches += (int)m.size();
        std::vector<float> a, b; a.reserve(m.size() * 3); b.reserve(m.size() * 3); for (auto& pr : m) { a.insert(a.end(), &probe.pts[pr.first * 3], &probe.pts[pr.first * 3] + 3); b.insert(b.end(), &ff.f.pts[pr.second * 3], &ff.f.pts[pr.second * 3] + 3); }
        double R[3][3], t[3]; int n = feat::ransacRigid(a, b, R, t, 4.0f, 600); if (n < 20 || n <= bestN) continue;   // pose: probe (last-frame coords) -> scan-local
        float share, med; ff.grid.check(probePts, R, t, 6.0f, share, med); if (share < 0.25f || med > 2.5f) continue;   // the probe's surface must sit on the window's surface
        bestN = n; memcpy(bestR, R, sizeof bestR); memcpy(bestT, t, sizeof bestT); bestScan = i; bestFrame = ff.frame; bestShare = share; bestMed = med; } } }
  inliers = bestN; refScan = bestScan; refFrame = bestFrame; secs = std::chrono::duration<double>(clk::now() - t0).count();
  if (bestN < 20) return false;
  logf("feature search: window scan %d frame %d, %d inliers, surface %.0f%% within 6 mm, median %.1f mm", bestScan + 1, bestFrame + 1, bestN, bestShare * 100, bestMed);
  // world pose of the probe frame = ST * F; live transform of the new scan: liveT = Tw * inv(Fin)
  double F[4][4]; for (int r = 0; r < 3; r++) { for (int c = 0; c < 3; c++) F[r][c] = bestR[r][c]; F[r][3] = bestT[r]; } F[3][0] = F[3][1] = F[3][2] = 0; F[3][3] = 1;
  double Tw[4][4]; { std::lock_guard<std::mutex> l(g_scanMx); if (bestScan >= (int)g_scans.size() || !g_scans[bestScan].scan) return false; mul44(g_scans[bestScan].scan->getScanTransformation().m, F, Tw); }
  double FinInv[4][4]; invRigid(Fin.m, FinInv); double live[4][4]; mul44(Tw, FinInv, live);
  // refine with the SDK: textured global registration over [textured neighbours of the matched frame + probe frames]
  IScan* src = nullptr; ScanHold hold; std::vector<int> win; double ST[4][4]; { std::lock_guard<std::mutex> l(g_scanMx); if (bestScan >= 0 && bestScan < (int)g_scans.size() && g_scans[bestScan].scan) { src = g_scans[bestScan].scan; src->addRef(); hold.p = src; memcpy(ST, src->getScanTransformation().m, sizeof ST);
      std::vector<int> ti; for (int k = 0; k < src->getSize(); k++) { IFrameMesh* f = src->getElement(k); if (f && f->isTextured()) ti.push_back(k); } int pos = 0; for (int k = 0; k < (int)ti.size(); k++) if (ti[k] == bestFrame) pos = k;
      for (int k = std::max(0, pos - 2); k < (int)ti.size() && k <= pos + 8; k++) win.push_back(ti[k]); } }   // the matched window (5 textured frames from bestFrame) plus margins
  if (src && win.size() >= 3) { IScan* R = nullptr; createSimilarScan(&R, src); if (R) { int nRef = 0;
      { std::lock_guard<std::mutex> l(g_scanMx); for (int k : win) { double W[4][4]; mul44(ST, src->getTransformation(k).m, W); Matrix4x4D Mw; memcpy(Mw.m, W, sizeof W); R->add(src->getElement(k), Mw); nRef++; } }
      std::vector<Matrix4x4D> pin; { std::lock_guard<std::mutex> l(g_tempMx); if (g_tempTex) for (int j = 0; j < g_tempTex->getSize(); j++) { Matrix4x4D fj = g_tempTex->getTransformation(j); double W[4][4]; mul44(live, fj.m, W); Matrix4x4D Mw; memcpy(Mw.m, W, sizeof W); R->add(g_tempTex->getElement(j), Mw); pin.push_back(fj); } }
      int np = (int)pin.size(); ensureNormals(R); IModel* in = nullptr; createModel(&in); in->add(R); IModel* out = nullptr; createModel(&out);
      alg::GlobalRegistrationSettings gd; gd.scannerType = src->getScannerType(); gd.registrationType = alg::GlobalRegistrationType_GeometryAndTexture; alg::IAlgorithm* ga = nullptr; ErrorCode ec = alg::createGlobalRegistrationAlgorithm(&ga, &gd);
      auto tg = clk::now(); if (ec == ErrorCode_OK && ga) { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = in; ws.out = out; ws.threadsCount = std::thread::hardware_concurrency(); ec = execLimited(ga, &ws, 20, "position search", &g_searchStop); ga->release(); }
      if (ec == ErrorCode_OK && out->getSize() == 1 && out->getElement(0)->getSize() == nRef + np && np > 0) { IScan* Ro = out->getElement(0);
        // anchor on the last reference frame; live = refIn * inv(refOut) * probeOut * inv(Fin_probe)
        double refIn[4][4], refOut[4][4], inv[4][4], A[4][4]; memcpy(refIn, R->getTransformation(nRef - 1).m, sizeof refIn); memcpy(refOut, Ro->getTransformation(nRef - 1).m, sizeof refOut); invRigid(refOut, inv); mul44(refIn, inv, A);
        double pOut[4][4], pInv[4][4], tmp[4][4]; memcpy(pOut, Ro->getTransformation(nRef + np - 1).m, sizeof pOut); invRigid(pin[np - 1].m, pInv); mul44(A, pOut, tmp); mul44(tmp, pInv, live);
        logf("feature search: textured global registration over %d + %d frames in %.1f s", nRef, np, std::chrono::duration<double>(clk::now() - tg).count()); }
      else logf("feature search: refinement 0x%08X (kept the feature pose)", (unsigned)ec);
      out->release(); in->release(); R->release(); } }
  memcpy(T, live, sizeof live); secs = std::chrono::duration<double>(clk::now() - t0).count(); return true;
}
// Hybrid (texture + geometry) resume: the SDK's auto-align is geometry-only and fails on flat, low-relief stones
// (Malurpatna herostone, 3 Nov: 17 failures in two trials). Studio's "point the scanner at a recently captured region"
// is reproduced with the SDK's rough serial registration: a scan made of the last textured frames of the most recent
// scan followed by the latest textured probe frames; RoughTextured registers each frame to the previous one using
// texture as well, so the first probe frame chains onto the tail of the previous scan. Returns the live transform T.
// Resume from ANY part of any earlier pass (Gowtham, 3 Nov 17:30: "it must continue from any part of scan 1, this is
// exactly how Artec works"): the textured frames of every visible scan are cut into windows of 10 (step 8), tail of the
// most recent scan first; each search try tests the next two windows in rotation, so the whole project is covered in
// a few seconds and the tail (the usual resume spot) is always tried first.
struct HybridWindow { int scan; std::vector<int> frames; };
// 5 Nov 10:55 crash (0xC0000409 in ucrtbase): after a tracking loss the position search read the frames of the scan
// just closed while cleanScan was rewriting them (points/triangles replaced in place). The scan being cleaned is
// published here; the search waits (returns, retries in 2 s) while it is set. Cleaning takes 0.2–1.2 s.
// Scans being cleaned (review C7: one slot was not enough with Pause/Resume). The search skips these scans.
static std::mutex g_cleanMx; static std::vector<IScan*> g_cleaning;
static bool isCleaning(IScan* s) { std::lock_guard<std::mutex> l(g_cleanMx); return std::find(g_cleaning.begin(), g_cleaning.end(), s) != g_cleaning.end(); }
// Review B1/C4/C8: the search is stopped (its SDK jobs cancelled through their tokens) at Stop, at the start of a new
// recording and when the scanning procedure finishes; every search job also has a 20 s limit.
static std::vector<HybridWindow> hybridWindows() {
  std::vector<HybridWindow> w; std::lock_guard<std::mutex> l(g_scanMx);
  for (int i = (int)g_scans.size() - 1; i >= 0; i--) { auto& s = g_scans[i]; if (!(s.visible && s.scan && s.scan->getSize() > 0 && (s.group < 0 || (s.group < (int)g_groups.size() && g_groups[s.group].visible)))) continue; if (isCleaning(s.scan)) continue;
    std::vector<int> ti; for (int k = 0; k < s.scan->getSize(); k++) { IFrameMesh* f = s.scan->getElement(k); if (f && f->isTextured()) ti.push_back(k); }
    if (ti.size() < 3) continue;
    // 16-frame windows, step 10 (3 Nov evening: passes 3 and 4 of Trail7 sat visibly low in Studio with 10-frame windows)
    const int W = 16, STEP = 10; if ((int)ti.size() <= W) { w.push_back({i, ti}); continue; }
    for (int pos = (int)ti.size() - W; pos >= 0; pos -= STEP) { HybridWindow hw; hw.scan = i; hw.frames.assign(ti.begin() + pos, ti.begin() + pos + W); w.push_back(hw); if (pos == 0) break; if (pos - STEP < 0 && pos > 0) { HybridWindow h0; h0.scan = i; h0.frames.assign(ti.begin(), ti.begin() + W); w.push_back(h0); break; } } }
  return w;
}
// 5 Nov 11:55 crash (0xC0000005, call through a null pointer): the search read a scan's pointer under the lock, then
// used it after unlocking (createSimilarScan, getScannerType) while WM_POSTDONE swapped in the registered scan and
// released the old one. The search now holds its own reference for as long as it uses the scan.
static bool hybridResume(const HybridWindow& win, double T[4][4], int& nRef, int& nProbe, double& secs) {
  nRef = nProbe = 0; secs = 0; IScan* R = nullptr; IScan* src = nullptr; ScanHold hold; double scanT[4][4]; const std::vector<int>& refIdx = win.frames;
  { std::lock_guard<std::mutex> l(g_scanMx); if (win.scan < 0 || win.scan >= (int)g_scans.size() || !g_scans[win.scan].scan) return false; src = g_scans[win.scan].scan; src->addRef(); hold.p = src;
    if (refIdx.size() < 3) return false; createSimilarScan(&R, src); if (!R) return false;
    for (int k : refIdx) { if (k < src->getSize()) R->add(src->getElement(k), src->getTransformation(k)); } memcpy(scanT, src->getScanTransformation().m, sizeof scanT); if (R->getSize() < 3) { R->release(); return false; } }
  nRef = R->getSize(); std::vector<Matrix4x4D> probeIn;
  { std::lock_guard<std::mutex> l(g_tempMx); if (!g_tempTex || g_tempTex->getSize() < 3) { R->release(); return false; } for (int j = 0; j < g_tempTex->getSize(); j++) { R->add(g_tempTex->getElement(j), g_tempTex->getTransformation(j)); probeIn.push_back(g_tempTex->getTransformation(j)); } }
  nProbe = (int)probeIn.size(); ensureNormals(R);
  // 7 Nov diagnostic: "can't register surfaces with different texture image scales" (113 of 201 searches on 6 Nov) -
  // log the texture image sizes of the reference and the probe frames, once a minute
  // 7 Nov 09:40 (museum day, Trail29): the stored passes carry full-size photos (960x1280), the live frames half-size
  // (480x640); the SDK refuses to register them, so the three texture tries (~6 s each) only used up the pass before
  // the geometry search got its turn. Different sizes -> skip the texture join at once (logic::textureJoinPossible).
  { auto dims = [](IFrameMesh* f, int& w, int& h) { w = h = 0; if (f && f->isTextured()) { IImage* im = f->getImage(); if (im) { w = im->getWidth(); h = im->getHeight(); } } };
    int rw, rh, pw, ph; dims(R->getElement(0), rw, rh); dims(R->getElement(nRef), pw, ph);
    static clk::time_point lastTexLog; bool say = std::chrono::duration<double>(clk::now() - lastTexLog).count() > 60; if (say) lastTexLog = clk::now();
    if (!logic::textureJoinPossible(rw, rh, pw, ph)) { if (say) logf("texture join skipped: reference photos %dx%d, live photos %dx%d (the SDK cannot register different sizes); using the geometry search", rw, rh, pw, ph); R->release(); return false; }
    if (say) logf("hybrid resume textures: reference frame %dx%d, probe frame %dx%d", rw, rh, pw, ph); }
  IModel* in = nullptr; createModel(&in); in->add(R); IModel* out = nullptr; createModel(&out);
  alg::SerialRegistrationSettings sd; sd.scannerType = src->getScannerType(); sd.registrationType = alg::SerialRegistrationType_RoughTextured; alg::IAlgorithm* a = nullptr; ErrorCode ec = alg::createSerialRegistrationAlgorithm(&a, &sd);
  auto t0 = clk::now(); if (ec == ErrorCode_OK && a) { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = in; ws.out = out; ws.threadsCount = std::thread::hardware_concurrency(); ec = execLimited(a, &ws, 20, "position search", &g_searchStop); a->release(); }
  // Refine: the rough pass gives a coarse join (a visible step between passes, Gowtham's screenshot 3 Nov 17:04);
  // a fine textured pass on the rough result tightens it. Falls back to the rough result if the fine pass fails.
  IModel* out2 = nullptr;
  if (ec == ErrorCode_OK && out->getSize() == 1) { createModel(&out2); alg::SerialRegistrationSettings fd; fd.scannerType = src->getScannerType(); fd.registrationType = alg::SerialRegistrationType_FineTextured; alg::IAlgorithm* fa = nullptr;
    if (alg::createSerialRegistrationAlgorithm(&fa, &fd) == ErrorCode_OK && fa) { AlgorithmWorkset ws2; memset(&ws2, 0, sizeof ws2); ws2.in = out; ws2.out = out2; ws2.threadsCount = std::thread::hardware_concurrency(); ErrorCode ec2 = execLimited(fa, &ws2, 20, "position search", &g_searchStop); fa->release();
      if (ec2 == ErrorCode_OK && out2->getSize() == 1 && out2->getElement(0)->getSize() == nRef + nProbe) { IModel* t = out; out = out2; out2 = t; } else logf("hybrid resume: fine textured pass failed 0x%08X, using the rough join", (unsigned)ec2); }
    // Third step: global registration (texture + geometry) over the window + probe. The serial passes only register
    // consecutive pairs; measured joins on the herostone were 3.7–6.5 mm (joinmeasure, 4 Nov) against 0.1–0.5 mm where
    // the geometry auto-align works. Global registration optimises every overlap in the set; ~30 frames is fast.
    { IModel* out3 = nullptr; createModel(&out3); alg::GlobalRegistrationSettings gd; gd.scannerType = src->getScannerType(); gd.registrationType = alg::GlobalRegistrationType_GeometryAndTexture; alg::IAlgorithm* ga = nullptr;
      if (alg::createGlobalRegistrationAlgorithm(&ga, &gd) == ErrorCode_OK && ga) { AlgorithmWorkset ws3; memset(&ws3, 0, sizeof ws3); ws3.in = out; ws3.out = out3; ws3.threadsCount = std::thread::hardware_concurrency(); auto tg = clk::now(); ErrorCode ec3 = execLimited(ga, &ws3, 20, "position search", &g_searchStop); ga->release();
        if (ec3 == ErrorCode_OK && out3->getSize() == 1 && out3->getElement(0)->getSize() == nRef + nProbe) { IModel* t = out; out = out3; out3 = t; logf("hybrid resume: global registration in %.1f s", std::chrono::duration<double>(clk::now() - tg).count()); } else logf("hybrid resume: global registration failed 0x%08X", (unsigned)ec3); }
      if (out3) out3->release(); } }
  if (out2) out2->release();
  secs = std::chrono::duration<double>(clk::now() - t0).count(); bool ok = false;
  if (ec == ErrorCode_OK && out->getSize() == 1 && out->getElement(0)->getSize() == nRef + nProbe) {
    IScan* Ro = out->getElement(0);
    // Anchor: the last reference frame's output pose maps back onto its original pose. A = refIn * inv(refOut).
    double refIn[4][4], refOut[4][4], inv[4][4], A[4][4]; memcpy(refIn, R->getTransformation(nRef - 1).m, sizeof refIn); memcpy(refOut, Ro->getTransformation(nRef - 1).m, sizeof refOut); invRigid(refOut, inv); mul44(refIn, inv, A);
    // Live transform from the last two probe frames; accept when they agree (same rigid motion within 3 mm).
    double cand[2][4][4]; for (int c = 0; c < 2; c++) { int j = nProbe - 1 - c; double pOut[4][4], pInInv[4][4], tmp[4][4], tmp2[4][4]; memcpy(pOut, Ro->getTransformation(nRef + j).m, sizeof pOut); invRigid(probeIn[j].m, pInInv); mul44(scanT, A, tmp); mul44(tmp, pOut, tmp2); mul44(tmp2, pInInv, cand[c]); }
    double d = 0; for (int r = 0; r < 3; r++) d += (cand[0][r][3] - cand[1][r][3]) * (cand[0][r][3] - cand[1][r][3]); bool moved = false; for (int j = 0; j < nProbe; j++) if (!isIdentity(Ro->getTransformation(nRef + j).m)) moved = true;
    if (moved && d < 9.0) { memcpy(T, cand[0], sizeof cand[0]); ok = true; } else logf("hybrid resume: registration returned but the probe poses disagree (%.1f mm) or did not move", sqrt(d)); }
  out->release(); in->release(); R->release(); if (!ok) logf("hybrid resume: 0x%08X in %.1f s, ref frames %d, probe frames %d -> not found", (unsigned)ec, secs, nRef, nProbe);
  return ok;
}
// Background position search: auto-align [visible reference scans + the first frames of the new scan].
// Option 1 (owner, 4 Nov 16:40, for the Hoysala trip): a new pass starts over the END of the previous pass, as in
// Studio. The search is then LOCAL: only the tail windows of the most recent visible pass are tried, and a find is
// accepted only if it lands within 150 mm of that tail. No image-feature search, no RANSAC tiers — on a repeating
// inscription those landed one letter row off (Trail12: 10–21 mm; feattest: 113–193 mm).
static bool g_localResume = true;
// the search thread ends: a current search clears "busy"; a detached (abandoned) one only clears the zombie flag
static void searchDone(int gen) { if (gen == g_searchGen.load()) g_searchBusy = false; else g_searchZombie = false; }
static void runPositionSearch() {
  if (g_searchZombie.load()) return;   // an abandoned search still runs: no second one beside it
  if (g_searchBusy.exchange(true)) return; if (g_searchThread.joinable()) g_searchThread.join();
  int gen = ++g_searchGen; g_searchThread = std::thread([gen] {
    if (g_localResume) { std::vector<HybridWindow> wins = hybridWindows(); bool texReady = false; Matrix4x4D Fin; { std::lock_guard<std::mutex> l(g_tempMx); texReady = g_tempTex && g_tempTex->getSize() >= 3; if (texReady) Fin = g_tempTex->getTransformation(g_tempTex->getSize() - 1); }
      // outside review 6 Nov / Trail21: rotate over the 3 most recent visible passes, newest first (logic::searchTarget)
      std::vector<int> cand; { std::lock_guard<std::mutex> l(g_scanMx); for (int i = (int)g_scans.size() - 1; i >= 0; i--) { auto& s = g_scans[i]; if (s.visible && s.scan && s.ref && s.scan->getSize() >= 10 && (s.group < 0 || (s.group < (int)g_groups.size() && g_groups[s.group].visible)) && !isCleaning(s.scan)) cand.push_back(i); } }
      int target = logic::searchTarget(cand, g_hybridNext.load()); g_hybridNext++;
      if (texReady && !wins.empty() && target >= 0) { int latest = target; int tried = 0;
        for (size_t a = 0; a < wins.size() && tried < 3; a++) { const HybridWindow& win = wins[a]; if (win.scan != latest) continue; tried++;
          double T[4][4]; int nr = 0, np = 0; double secs = 0; if (!hybridResume(win, T, nr, np, secs)) continue;
          // gate: where does this put the latest probe frame, and where is the tail of the previous pass?
          double pw[3] = {T[0][0] * Fin.m[0][3] + T[0][1] * Fin.m[1][3] + T[0][2] * Fin.m[2][3] + T[0][3], T[1][0] * Fin.m[0][3] + T[1][1] * Fin.m[1][3] + T[1][2] * Fin.m[2][3] + T[1][3], T[2][0] * Fin.m[0][3] + T[2][1] * Fin.m[1][3] + T[2][2] * Fin.m[2][3] + T[2][3]};
          double rw[3] = {0, 0, 0}; { std::lock_guard<std::mutex> l(g_scanMx); if (win.scan < (int)g_scans.size() && g_scans[win.scan].scan) { double M[4][4]; if (win.frames.back() < g_scans[win.scan].scan->getSize()) mul44(g_scans[win.scan].scan->getScanTransformation().m, g_scans[win.scan].scan->getTransformation(win.frames.back()).m, M); else setIdentity(M); rw[0] = M[0][3]; rw[1] = M[1][3]; rw[2] = M[2][3]; } }
          double dist = sqrt((pw[0] - rw[0]) * (pw[0] - rw[0]) + (pw[1] - rw[1]) * (pw[1] - rw[1]) + (pw[2] - rw[2]) * (pw[2] - rw[2]));   // camera-to-camera, for the log only (16:21: ~360 mm every time — a tilted scanner moves its camera far while looking at the same spot)
          // the real gate: the probe SURFACE (latest 3 textured frames under T) must lie on the window's surface
          float share = 0, med = 99; { std::vector<float> wpts, ppts; { std::lock_guard<std::mutex> l(g_scanMx); if (win.scan < (int)g_scans.size() && g_scans[win.scan].scan) { IScan* ws = g_scans[win.scan].scan; const auto& ST = ws->getScanTransformation().m; for (int k : win.frames) { if (k >= ws->getSize()) continue; double M[4][4]; mul44(ST, ws->getTransformation(k).m, M); framePointsT(ws->getElement(k), M, 10, wpts); } } }
            { std::lock_guard<std::mutex> l(g_tempMx); if (g_tempTex) for (int j = std::max(0, g_tempTex->getSize() - 3); j < g_tempTex->getSize(); j++) { double M[4][4]; mul44(T, g_tempTex->getTransformation(j).m, M); framePointsT(g_tempTex->getElement(j), M, 25, ppts); } }
            if (wpts.size() >= 300 && ppts.size() >= 60) { feat::Grid g; g.build(std::move(wpts), 2.0f); double I[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, z[3] = {0, 0, 0}; g.check(ppts, I, z, 6.0f, share, med); } }
          // 16:28: the surface check reported 0 % for finds that the post-Stop auto-align then confirmed (both moved the
          // pass 37–38 cm, from the project origin to the end of pass 1). The check is wrong somewhere; it is now
          // log-only, and the restriction to the tail windows of the latest pass is what keeps wrong repeats out.
          // 16:49: the check was RIGHT — the SDK's textured registration refuses the probe frames ("different texture
          // image scales", in the log since 10:46) and leaves them at the origin, so "found" was a no-op. Hard reject.
          if (share < 0.3f || med > 2.5f) { logf("position search #%d (local resume, window %zu): texture join rejected - probe surface %.0f%% on the window, median %.1f mm", g_searchTries + 1, a + 1, share * 100, med); continue; }
          logf("position search #%d (local resume, scan %d window %zu): FOUND in %.1f s, ref frames %d, probe frames %d, surface %.0f%% within 6 mm median %.1f mm (cameras %.0f mm apart)", ++g_searchTries, win.scan + 1, a + 1, secs, nr, np, share * 100, med, dist);
          if (gen == g_searchGen.load()) { { std::lock_guard<std::mutex> l(g_liveTmx); memcpy(g_liveT, T, sizeof T); } g_foundAt = g_storedFrames.load(); g_liveFound = true; noteJoin(win.scan); std::lock_guard<std::mutex> l(g_mx); g_liveAcc.clear(); }   /* DeepSeek 1: inside the generation check */
          g_hybridNext = 0; g_lastSearch = clk::now(); searchDone(gen); return; }
        if (tried) logf("position search #%d (local resume, pass %d): texture join not found; trying the geometry auto-align against that pass", g_searchTries + 1, latest + 1); }
      // geometry auto-align (the method that joined Trail13 after Stop) against the latest visible pass only, then the same surface gate on its tail windows
      IScan* probe = nullptr; { std::lock_guard<std::mutex> l(g_tempMx); if (g_tempScan) { createSimilarScan(&probe, g_tempScan); if (probe) cloneScan(probe, g_tempScan); } }
      if (!probe || probe->getSize() < 6) { if (probe) probe->release(); g_lastSearch = clk::now(); searchDone(gen); return; } ensureNormals(probe);
      IModel* in = nullptr; createModel(&in); { std::lock_guard<std::mutex> l(g_scanMx); if (target >= 0 && target < (int)g_scans.size() && g_scans[target].scan && g_scans[target].ref) { auto& s = g_scans[target]; s.ref->setScanTransformation(s.scan->getScanTransformation()); in->add(s.ref); } }
      int nref = in->getSize(); if (nref == 0) { in->release(); probe->release(); g_lastSearch = clk::now(); searchDone(gen); return; } in->add(probe);
      IModel* out = nullptr; createModel(&out); alg::AutoAlignSettings as; as.scannerType = probe->getScannerType(); alg::IAlgorithm* a = nullptr; ErrorCode ec = alg::createAutoalignAlgorithm(&a, &as);
      auto t0 = clk::now(); if (ec == ErrorCode_OK && a) { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = in; ws.out = out; ws.threadsCount = std::thread::hardware_concurrency(); ec = execLimited(a, &ws, 20, "position search", &g_searchStop); a->release(); }
      bool found = false; double T[4][4];
      if (ec == ErrorCode_OK && out->getSize() == 2) { double Rin[4][4], Rout[4][4], Pout[4][4], Rinv[4][4], tmp[4][4]; memcpy(Rin, in->getElement(0)->getScanTransformation().m, sizeof Rin); memcpy(Rout, out->getElement(0)->getScanTransformation().m, sizeof Rout); memcpy(Pout, out->getElement(1)->getScanTransformation().m, sizeof Pout);
        (void)Rinv; (void)tmp; logic::livePose(Rin, Rout, Pout, T); found = !isIdentity(out->getElement(0)->getScanTransformation().m) || !isIdentity(out->getElement(1)->getScanTransformation().m); }   /* outside review 6 Nov: Rin * inv(Rout) * Pout */
      double secs = std::chrono::duration<double>(clk::now() - t0).count(); float share = 0, med = 99, flat = 0;
      if (found && target >= 0) { std::vector<float> wpts, ppts; { std::lock_guard<std::mutex> l(g_scanMx); if (target < (int)g_scans.size() && g_scans[target].scan && g_scans[target].ref) { IScan* rs = g_scans[target].ref; Matrix4x4D STm = g_scans[target].scan->getScanTransformation(); for (int k = 0; k < rs->getSize(); k++) { double M[4][4]; mul44(STm.m, rs->getTransformation(k).m, M); framePointsT(rs->getElement(k), M, 10, wpts); } } }   // the whole pass (its 40-frame reference): the geometry search places a pass anywhere on it; the 1.0 mm median and flatness rules guard against slides
        for (int j = std::max(0, probe->getSize() - 3); j < probe->getSize(); j++) { double M[4][4]; mul44(T, probe->getTransformation(j).m, M); framePointsT(probe->getElement(j), M, 25, ppts); }
        if (wpts.size() >= 300 && ppts.size() >= 60) { flat = logic::planeRms(wpts); feat::Grid g; g.build(std::move(wpts), 2.0f); double I[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, z[3] = {0, 0, 0}; g.check(ppts, I, z, 6.0f, share, med); } }
      logic::GateResult gate = logic::acceptJoin(share, med, true, flat); bool ok = found && gate.ok;   // review D1: geometry-only placement refused on a near-flat window
      logf("position search #%d (local geometry auto-align vs pass %d): 0x%08X in %.1f s, probe frames %d -> %s (surface %.0f%% within 6 mm, median %.1f mm, window flatness %.1f mm)", ++g_searchTries, target + 1, (unsigned)ec, secs, probe->getSize(), ok ? "FOUND" : found ? gate.why : "not found", share * 100, med, flat);
      if (ok) { if (gen == g_searchGen.load()) { { std::lock_guard<std::mutex> l(g_liveTmx); memcpy(g_liveT, T, sizeof T); } g_foundAt = g_storedFrames.load(); g_liveFound = true; noteJoin(target); std::lock_guard<std::mutex> l(g_mx); g_liveAcc.clear(); } }   /* DeepSeek 1 */
      out->release(); in->release(); probe->release(); g_lastSearch = clk::now(); searchDone(gen); return; }
    // 0) image-feature search (FS build): recognise the place the latest textured probe frame shows in any earlier pass
    if (g_featSearch) { double T[4][4]; int inl = 0, matches = 0, refScan = -1, refFrame = -1; double secs = 0;
      if (featureSearch(T, inl, matches, refScan, refFrame, secs)) {
        logf("position search #%d (image features): FOUND in %.1f s, %d inliers of %d matches, scan %d frame %d", ++g_searchTries, secs, inl, matches, refScan + 1, refFrame + 1);
        if (gen == g_searchGen.load()) { { std::lock_guard<std::mutex> l(g_liveTmx); memcpy(g_liveT, T, sizeof T); } g_foundAt = g_storedFrames.load(); g_liveFound = true; noteJoin(refScan); std::lock_guard<std::mutex> l(g_mx); g_liveAcc.clear(); }   /* DeepSeek 1: inside the generation check */
        g_hybridNext = 0; g_lastSearch = clk::now(); searchDone(gen); return; }
      else if (matches >= 0) logf("position search #%d (image features): not found in %.1f s (best %d inliers of %d matches)", g_searchTries + 1, secs, inl, matches); }
    // 1) hybrid resume (texture + geometry): two windows of an earlier pass per try, tail of the latest pass first
    { std::vector<HybridWindow> wins = hybridWindows(); bool texReady = false; { std::lock_guard<std::mutex> l(g_tempMx); texReady = g_tempTex && g_tempTex->getSize() >= 3; }
      if (texReady && !wins.empty()) { for (int a = 0; a < 2 && a < (int)wins.size(); a++) { const HybridWindow& win = wins[(g_hybridNext + a) % wins.size()];
          double T[4][4]; int nr = 0, np = 0; double secs = 0; if (hybridResume(win, T, nr, np, secs)) {
            logf("position search #%d (hybrid resume, scan %d window %d of %d): FOUND in %.1f s, ref frames %d, probe frames %d", ++g_searchTries, win.scan + 1, (g_hybridNext + a) % (int)wins.size() + 1, (int)wins.size(), secs, nr, np);
            if (gen == g_searchGen.load()) { { std::lock_guard<std::mutex> l(g_liveTmx); memcpy(g_liveT, T, sizeof T); } g_foundAt = g_storedFrames.load(); g_liveFound = true; noteJoin(win.scan); std::lock_guard<std::mutex> l(g_mx); g_liveAcc.clear(); }   /* DeepSeek 1: inside the generation check */
            g_hybridNext = 0; g_lastSearch = clk::now(); searchDone(gen); return; } }
        g_hybridNext = (g_hybridNext + 2) % (int)wins.size(); } }
    // 2) geometry-only auto-align (RANSAC) of the probe against the thinned references
    IScan* probe = nullptr; { std::lock_guard<std::mutex> l(g_tempMx); if (g_tempScan) { createSimilarScan(&probe, g_tempScan); if (probe) cloneScan(probe, g_tempScan); } }
    if (!probe || probe->getSize() < 6) { if (probe) probe->release(); g_lastSearch = clk::now(); searchDone(gen); return; }
    ensureNormals(probe);
    // References = the prebuilt thinned copies (ScanRec::ref). Tiered for big stones with many scans: the first two
    // tries use the three most recent visible scans (the operator usually resumes near where the last scan ended);
    // later tries use every visible scan. The search time grows with the frames passed (~2 s per 100 frames).
    // Tier 3 (from the 5th try, flat subjects such as the Malurpatna herostone, 3 Nov: 11 failures with a 40-frame
    // reference) rebuilds the references with 120 frames each; the clone is instant, the match takes a few seconds.
    IModel* in = nullptr; createModel(&in); int tier = g_searchTries < 2 ? 1 : g_searchTries < 4 ? 2 : 3; std::vector<IScan*> big; std::vector<int> refIdx;
    { std::lock_guard<std::mutex> l(g_scanMx); int taken = 0;
      for (int i = (int)g_scans.size() - 1; i >= 0; i--) { auto& s = g_scans[i]; if (!(s.visible && s.scan && s.ref && (s.group < 0 || g_groups[s.group].visible))) continue;
        if (tier == 1 && taken >= 3) break;
        if (tier == 3) { IScan* t = buildRef(s.scan, 120); if (t) { t->setScanTransformation(s.scan->getScanTransformation()); in->add(t); big.push_back(t); refIdx.push_back(i); taken++; } continue; }
        s.ref->setScanTransformation(s.scan->getScanTransformation()); in->add(s.ref); refIdx.push_back(i); taken++; } }
    int nref = in->getSize(); in->add(probe);
    IModel* out = nullptr; createModel(&out); alg::AutoAlignSettings as; as.scannerType = probe->getScannerType(); alg::IAlgorithm* a = nullptr; ErrorCode ec = alg::createAutoalignAlgorithm(&a, &as);
    auto t0 = clk::now(); if (ec == ErrorCode_OK && a) { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = in; ws.out = out; ws.threadsCount = std::thread::hardware_concurrency(); ec = execLimited(a, &ws, 20, "position search", &g_searchStop); a->release(); }
    bool found = false; double T[4][4];
    if (ec == ErrorCode_OK && out->getSize() == nref + 1 && nref > 0) {
      // The SDK may anchor on any scan: express the probe's pose relative to the first reference's ORIGINAL pose,
      // T = inv(ref0_out) * ref0_in * probe_out (3 Nov: it had moved the reference and the probe looked "identity").
      double Rin[4][4], Rout[4][4], Pout[4][4], Rinv[4][4], tmp[4][4]; memcpy(Rin, in->getElement(0)->getScanTransformation().m, sizeof Rin); memcpy(Rout, out->getElement(0)->getScanTransformation().m, sizeof Rout); memcpy(Pout, out->getElement(nref)->getScanTransformation().m, sizeof Pout);
      (void)Rinv; (void)tmp; logic::livePose(Rin, Rout, Pout, T); bool anyMoved = false; for (int k = 0; k <= nref; k++) if (!isIdentity(out->getElement(k)->getScanTransformation().m)) anyMoved = true; found = anyMoved; }
    logf("position search #%d (%s): 0x%08X in %.1f s, probe frames %d, refs %d -> %s", ++g_searchTries, tier == 1 ? "recent scans" : tier == 2 ? "all scans" : "all scans, 120-frame references", (unsigned)ec, std::chrono::duration<double>(clk::now() - t0).count(), probe->getSize(), nref, found ? "FOUND" : "not found");
    for (IScan* t : big) t->release();
    if (found) { if (gen == g_searchGen.load()) { { std::lock_guard<std::mutex> l(g_liveTmx); memcpy(g_liveT, T, sizeof T); } g_foundAt = g_storedFrames.load(); g_liveFound = true; for (int rk : refIdx) noteJoin(rk); std::lock_guard<std::mutex> l(g_mx); g_liveAcc.clear(); } }   /* DeepSeek 1 */
    out->release(); in->release(); probe->release(); g_lastSearch = clk::now(); searchDone(gen);
  });
}

// Build the display meshes of a stored scan (at most `maxFrames` frames, evenly spaced).
// world = scanTransformation * frameTransformation (auto-align and registration write the scan-level transform)
// Display frames: every textured frame (up to 60, so the texture shows as in Studio) plus evenly spaced geometry frames.
// Texture camera of the scanner: uv = P X / (P3 X) for X in frame coordinates. The camera is rigid to the scanner, so
// one fit (11-parameter DLT on a textured frame's points + uv, normal equations) serves every frame of every scan.
static double g_camP[3][4]; static std::atomic<bool> g_camOk{false}; static std::mutex g_camMx;   // g_camOk read by tidy-up threads without the lock (DeepSeek 4-3)
static bool fitCamera(const IFrameMesh* f) {
  IArrayPoint3F* pts = f->getPoints(); IArrayUVCoordinates* uvs = f->getUVCoordinates(); if (!pts || !uvs || uvs->getSize() != pts->getSize() || pts->getSize() < 500) return false;
  int n = pts->getSize(); const Point3F* p = pts->getPointer(); const UVCoordinates* u = uvs->getPointer(); int step = n > 4000 ? n / 4000 : 1;
  double A[11][11] = {}, b[11] = {}; auto addRow = [&](const double* row, double rhs) { for (int i = 0; i < 11; i++) { b[i] += row[i] * rhs; for (int j = 0; j < 11; j++) A[i][j] += row[i] * row[j]; } };
  for (int k = 0; k < n; k += step) { double X = p[k].x, Y = p[k].y, Z = p[k].z, uu = u[k].u, vv = u[k].v; if (!(uu >= 0 && uu <= 1 && vv >= 0 && vv <= 1)) continue;
    double r1[11] = {X, Y, Z, 1, 0, 0, 0, 0, -uu * X, -uu * Y, -uu * Z}; addRow(r1, uu); double r2[11] = {0, 0, 0, 0, X, Y, Z, 1, -vv * X, -vv * Y, -vv * Z}; addRow(r2, vv); }
  // Gaussian elimination with partial pivoting
  double M[11][12]; for (int i = 0; i < 11; i++) { for (int j = 0; j < 11; j++) M[i][j] = A[i][j]; M[i][11] = b[i]; }
  for (int c = 0; c < 11; c++) { int piv = c; for (int r = c + 1; r < 11; r++) if (fabs(M[r][c]) > fabs(M[piv][c])) piv = r; if (fabs(M[piv][c]) < 1e-12) return false; if (piv != c) for (int j = 0; j < 12; j++) std::swap(M[c][j], M[piv][j]);
    for (int r = 0; r < 11; r++) { if (r == c) continue; double f2 = M[r][c] / M[c][c]; for (int j = c; j < 12; j++) M[r][j] -= f2 * M[c][j]; } }
  double x[11]; for (int i = 0; i < 11; i++) x[i] = M[i][11] / M[i][i];
  double P[3][4] = {{x[0], x[1], x[2], x[3]}, {x[4], x[5], x[6], x[7]}, {x[8], x[9], x[10], 1.0}};
  double err = 0; int cnt = 0; for (int k = 0; k < n; k += step * 4) { double X = p[k].x, Y = p[k].y, Z = p[k].z; double w = P[2][0] * X + P[2][1] * Y + P[2][2] * Z + P[2][3]; if (fabs(w) < 1e-9) continue; double pu = (P[0][0] * X + P[0][1] * Y + P[0][2] * Z + P[0][3]) / w, pv = (P[1][0] * X + P[1][1] * Y + P[1][2] * Z + P[1][3]) / w; err += fabs(pu - u[k].u) + fabs(pv - u[k].v); cnt++; }
  double mean = cnt ? err / cnt / 2 : 1; if (mean > 0.01) { logf("texture camera fit rejected: mean uv error %.4f", mean); return false; }
  std::lock_guard<std::mutex> l(g_camMx); if (!g_camOk) { memcpy(g_camP, P, sizeof P); g_camOk = true; logf("texture camera fitted: mean uv error %.4f (%.2f px) from %d points", mean, mean * 1280, cnt); } return true;
}
// uv of frame k's points seen by textured frame t's camera: X_t = inv(T_t) T_k x_k, then the camera projection.
static void projectUv(const IFrameMesh* fk, const double Tk[4][4], const double Tt[4][4], std::vector<float>& uv) {
  IArrayPoint3F* pts = fk->getPoints(); if (!pts) return; int n = pts->getSize(); const Point3F* p = pts->getPointer(); double inv[4][4], rel[4][4]; invRigid(Tt, inv); mul44(inv, Tk, rel); double P[3][4]; { std::lock_guard<std::mutex> l(g_camMx); memcpy(P, g_camP, sizeof P); }
  uv.resize((size_t)n * 2); for (int k = 0; k < n; k++) { double x = p[k].x, y = p[k].y, z = p[k].z; double X = rel[0][0] * x + rel[0][1] * y + rel[0][2] * z + rel[0][3], Y = rel[1][0] * x + rel[1][1] * y + rel[1][2] * z + rel[1][3], Z = rel[2][0] * x + rel[2][1] * y + rel[2][2] * z + rel[2][3];
    double w = P[2][0] * X + P[2][1] * Y + P[2][2] * Z + P[2][3]; if (fabs(w) < 1e-9) w = 1e-9; double pu = (P[0][0] * X + P[0][1] * Y + P[0][2] * Z + P[0][3]) / w, pv = (P[1][0] * X + P[1][1] * Y + P[1][2] * Z + P[1][3]) / w;
    uv[k * 2] = (float)pu; uv[k * 2 + 1] = (float)pv; } }   // unclamped: the caller measures coverage and drops triangles outside the photo
static int uvInside(const std::vector<float>& uv) { int n = 0; for (size_t i = 0; i + 1 < uv.size(); i += 2) if (uv[i] >= 0.01f && uv[i] <= 0.99f && uv[i + 1] >= 0.01f && uv[i + 1] <= 0.99f) n++; return n; }
static void buildScanDisplay(ScanRec& r, int maxFrames = 120) {   // 240 (08:00 test) stalled the laptop's GPU for minutes; 120 still covers holes from overlapping passes
  r.disp.clear(); if (!r.scan) return; int nf = r.scan->getSize(); if (nf <= 0) return; const auto& ST = r.scan->getScanTransformation().m;
  std::vector<int> pick; std::vector<int> texd; for (int k = 0; k < nf; k++) { IFrameMesh* f = r.scan->getElement(k); if (f && f->isTextured()) texd.push_back(k); }
  int tstep = texd.size() > (size_t)maxFrames ? (int)((texd.size() + maxFrames - 1) / maxFrames) : 1; for (size_t i = 0; i < texd.size(); i += tstep) pick.push_back(texd[i]);
  int geomWanted = maxFrames - (int)pick.size(); if (geomWanted > 0) { int step = nf > geomWanted ? (nf + geomWanted - 1) / geomWanted : 1; for (int k = 0; k < nf; k += step) { bool dup = false; for (int p : pick) if (p == k) dup = true; if (!dup) pick.push_back(k); } }
  std::sort(pick.begin(), pick.end());
  if (!g_camOk && !texd.empty()) fitCamera(r.scan->getElement(texd[0]));
  double ta[3] = {0, 0, 0}; int tn = 0; std::vector<std::pair<int, std::shared_ptr<GlTex>>> photos;   // (frame index, photo) of the textured frames drawn
  std::vector<int> dispPick;   // DeepSeek 6-2: the frame index of each r.disp entry (no vertex-count matching)
  for (int k : pick) { IFrameMesh* f = r.scan->getElement(k); if (!f) continue; double M[4][4]; mul44(ST, r.scan->getTransformation(k).m, M); FrameMesh m; buildFrameMesh(f, M, m, false, 1); if (m.v.empty()) continue;
    if (f->isTextured()) { grabTexture(f, m); if (!m.uv.empty()) { for (int a = 0; a < 3; a++) ta[a] += m.texAvg[a]; tn++; auto gt = std::make_shared<GlTex>(); gt->data = std::move(m.tex); gt->w = m.texW; gt->h = m.texH; m.tex.clear(); m.texp = gt; photos.push_back({k, gt}); } }
    r.disp.push_back(std::move(m)); dispPick.push_back(k); }
  // frames without a photo: project the nearest photo (by frame index) onto them, as Studio does
  if (g_camOk && !photos.empty()) {   // r.disp is in `pick` order (frames that produced an empty mesh were skipped)
    std::vector<FrameMesh> extra; for (size_t di = 0; di < dispPick.size(); di++) { int k = dispPick[di]; IFrameMesh* f = r.scan->getElement(k); if (!f || !f->getPoints()) continue; FrameMesh& m = r.disp[di]; if (f->isTextured()) continue;   // DeepSeek 6-1/6-2
      // candidate photos: the 6 nearest by frame index; take the one that covers most of this frame (the scanner moves
      // between photos, so the nearest photo may miss part of the frame — that showed as dark bands, 4 Nov 12:05)
      std::vector<int> cand; for (size_t i = 0; i < photos.size(); i++) cand.push_back((int)i); std::sort(cand.begin(), cand.end(), [&](int a, int b) { return abs(photos[a].first - k) < abs(photos[b].first - k); }); if (cand.size() > 6) cand.resize(6);
      int best = -1, bestIn = -1; std::vector<float> bestUv; for (int c : cand) { std::vector<float> uv; projectUv(f, r.scan->getTransformation(k).m, r.scan->getTransformation(photos[c].first).m, uv); int in = uvInside(uv); if (in > bestIn) { bestIn = in; best = c; bestUv = std::move(uv); } if (bestIn >= (int)(m.v.size() / 3) * 98 / 100) break; }
      if (best < 0 || bestIn < (int)(m.v.size() / 3) / 4) continue;   // no photo covers a quarter of it: keep the fill colour
      m.uv = std::move(bestUv); m.texp = photos[best].second;
      // triangles that reach outside the photo would take its dark border: they are NOT dropped (owner, 12:30) but
      // moved to a second display mesh of the same frame drawn in the scan's fill colour.
      std::vector<unsigned> keep, rest; keep.reserve(m.idx.size()); for (size_t i = 0; i + 2 < m.idx.size(); i += 3) { bool ok = true; for (int q = 0; q < 3 && ok; q++) { unsigned vi = m.idx[i + q]; float u = m.uv[vi * 2], v = m.uv[vi * 2 + 1]; if (!(u >= 0.005f && u <= 0.995f && v >= 0.005f && v <= 0.995f)) ok = false; } auto& dst = ok ? keep : rest; dst.push_back(m.idx[i]); dst.push_back(m.idx[i + 1]); dst.push_back(m.idx[i + 2]); }
      m.idx = std::move(keep); for (size_t i = 0; i < m.uv.size(); i++) m.uv[i] = m.uv[i] < 0 ? 0 : m.uv[i] > 1 ? 1 : m.uv[i];
      if (!rest.empty()) { FrameMesh o; o.v = m.v; o.n = m.n; o.idx = std::move(rest); memcpy(o.centre, m.centre, sizeof o.centre); extra.push_back(std::move(o)); } }
    for (auto& o : extra) r.disp.push_back(std::move(o)); }
  if (tn) { for (int a = 0; a < 3; a++) r.texAvg[a] = (float)(ta[a] / tn); r.hasTexAvg = true; }   // frames still without a photo (no camera fit) show the scan's average texture colour
}
static void countScanStats(ScanRec& r) {
  if (!r.scan) return; r.frames = r.scan->getSize(); int tex = 0; for (int k = 0; k < r.frames; k++) { IFrameMesh* f = r.scan->getElement(k); if (f && f->isTextured()) tex++; } r.texFrames = tex;
}
static void setScanName(ScanRec& r, const std::wstring& nm) { r.name = nm; if (r.scan) r.scan->setName(nm.c_str()); }
static std::wstring nextScanName() {
  std::wstring base = S.scanName.empty() ? L"Spider Scan" : S.scanName; int n = 1;
  std::vector<std::wstring> names; { std::lock_guard<std::mutex> l(g_scanMx); for (auto& s : g_scans) names.push_back(s.name); }   // DeepSeek 5: snapshot under the lock
  for (;;) { wchar_t b[128]; swprintf(b, 128, L"%s %d", base.c_str(), n); bool used = false; for (auto& nm : names) if (nm == b) used = true; if (!used) return b; n++; }
}
static std::wstring uniqueName(const std::wstring& base) { std::vector<std::wstring> names; { std::lock_guard<std::mutex> l(g_scanMx); for (auto& s : g_scans) names.push_back(s.name); } std::wstring n = base; int k = 2; for (;;) { bool used = false; for (auto& nm : names) if (nm == n) used = true; if (!used) return n; wchar_t b[160]; swprintf(b, 160, L"%s (%d)", base.c_str(), k++); n = b; } }
static void startPostProcess(IScan* s, bool optimise = true, bool fresh = false); static bool postIdle();
static void duplicateScan(int i) {   // Studio: Duplicate → "Copy of <name>"
  if (i < 0 || i >= (int)g_scans.size() || !g_scans[i].scan) return; /* the Workspace allows this only when idle (editOk) */ IScan* c = nullptr; createSimilarScan(&c, g_scans[i].scan); if (!c) return; cloneScan(c, g_scans[i].scan);
  ScanRec r; r.scan = c; r.group = g_scans[i].group; r.keyFrames = g_scans[i].keyFrames; r.maxErr = g_scans[i].maxErr; memcpy(r.color, g_scans[i].color, sizeof r.color); setScanName(r, uniqueName(L"Copy of " + g_scans[i].name)); countScanStats(r); buildScanDisplay(r);
  { std::lock_guard<std::mutex> l(g_scanMx); g_scans.insert(g_scans.begin() + i + 1, std::move(r)); } g_dirty = true; startPostProcess(c, false);   // search reference for the copy
}
// Studio runs fine registration on every scan as part of real-time processing ("surface optimization"); the SDK
// scanning pipeline has no such stage, so run the SDK's serial fine registration on each new scan after Stop.
// It runs in a background thread after the scan is already in the Workspace (Studio does it "simultaneously" too), so
// a lost-tracking restart is not delayed by it (08:12 test: 4.6 s freeze + display build on the UI thread).
// The thread also builds the scan's display meshes; the UI thread only swaps the result in (WM_POSTDONE).
#define WM_POSTDONE (WM_APP + 9)
struct PostResult { bool fresh = false; /* a newly recorded pass (autosave it) */ IScan* src; IScan* res; IScan* ref; std::vector<FrameMesh> disp; float texAvg[3]; bool hasTexAvg; std::vector<FrameFeat> feats; };
static std::vector<FrameFeat> buildFeats(IScan* s, int cap) {   // windows of 5 textured frames, step 3, at most `cap` windows, scan-local
  const int WIN = 5; std::vector<FrameFeat> out; std::vector<int> ti; for (int k = 0; k < s->getSize(); k++) { IFrameMesh* f = s->getElement(k); if (f && f->isTextured()) ti.push_back(k); }
  int nwin = (int)ti.size() >= WIN ? ((int)ti.size() - WIN) / 3 + 1 : 0; int stride = nwin > cap ? (nwin + cap - 1) / cap * 3 : 3; auto t0 = clk::now(); int kps = 0;
  for (int j = 0; j + WIN <= (int)ti.size(); j += stride) { FrameFeat ff; ff.frame = ti[j]; std::vector<float> wpts;
    for (int q = j; q < j + WIN; q++) { int k = ti[q]; const auto& Mk = s->getTransformation(k).m; feat::Frame f; if (frameFeatures(s->getElement(k), 320, f)) ff.f.append(f, Mk); framePointsT(s->getElement(k), Mk, 6, wpts); }
    if (ff.f.n() < 50) continue; ff.grid.build(std::move(wpts), 2.0f); kps += ff.f.n(); out.push_back(std::move(ff)); }
  logf("image features: %d windows, %d keypoints in %.1f s", (int)out.size(), kps, std::chrono::duration<double>(clk::now() - t0).count()); return out;
}
static std::atomic<bool> g_optimising{false}; static std::atomic<int> g_postBusy{0}; static std::vector<std::thread> g_postThreads; static std::mutex g_postMx;
// Position-search reference: a clone of the scan (cloneScan keeps the per-frame attributes RANSAC needs; add() does
// not) thinned to `cap` evenly spaced frames, normals computed. Built once per scan so a search never clones a
// 5000-frame scan (big stones: an hour of frames per project).
static IScan* buildRef(IScan* s, int cap) {
  auto t0 = clk::now(); IScan* t = nullptr; createSimilarScan(&t, s); if (!t) return nullptr; cloneScan(t, s); int n = t->getSize();
  if (n > cap) { int step = (n + cap - 1) / cap; for (int k = n - 1; k >= 0; k--) if (k % step != 0) t->remove(k); }
  ensureNormals(t); logf("search reference built: %d of %d frames in %.1f s", t->getSize(), n, std::chrono::duration<double>(clk::now() - t0).count()); return t;
}
// Frame cleaning as Studio does at capture (4 Nov, measured on Trail10 pass 2: cells with points >2 mm apart in height
// 20 % -> 1.2 %, Studio's own scan 3 %): the SDK's scanning procedure reconstructs frames with its defaults (edge
// threshold 10 mm, no small-patch filter, no angle filter), which leaves long bridging triangles and small floating
// patches at a second level that no registration can fix. Rule: drop triangles with an edge over 2 mm, then connected
// patches under 300 triangles, then the points no triangle uses. Points, normals, uv and triangles are rebuilt in place.
static long long cleanFrame(IFrameMesh* f, double edgeMax = 2.0, int minComp = 300) {
  IArrayPoint3F* pts = f->getPoints(); IArrayIndexTriplet* tri = f->getTriangles(); if (!pts || !tri) return 0; int n = pts->getSize(), nt = tri->getSize(); if (n < 3 || nt < 1) return 0; const Point3F* q = pts->getPointer(); const IndexTriplet* t = tri->getPointer();
  std::vector<char> triKeep(nt, 1); double e2 = edgeMax * edgeMax;
  for (int i = 0; i < nt; i++) { if (t[i].x >= (unsigned)n || t[i].y >= (unsigned)n || t[i].z >= (unsigned)n) { triKeep[i] = 0; continue; } auto d2 = [&](unsigned a, unsigned b) { double dx = q[a].x - q[b].x, dy = q[a].y - q[b].y, dz = q[a].z - q[b].z; return dx * dx + dy * dy + dz * dz; }; if (d2(t[i].x, t[i].y) > e2 || d2(t[i].y, t[i].z) > e2 || d2(t[i].z, t[i].x) > e2) triKeep[i] = 0; }
  if (minComp > 0) { std::vector<int> parent(n); for (int i = 0; i < n; i++) parent[i] = i; auto find = [&](int a) { while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; } return a; };
    for (int i = 0; i < nt; i++) if (triKeep[i]) { int a = find(t[i].x), b = find(t[i].y); parent[a] = b; int c = find(t[i].z); parent[find(b)] = c; }
    std::vector<int> compTris(n, 0); for (int i = 0; i < nt; i++) if (triKeep[i]) compTris[find(t[i].x)]++; for (int i = 0; i < nt; i++) if (triKeep[i] && compTris[find(t[i].x)] < minComp) triKeep[i] = 0; }
  std::vector<int> keep(n, -1); for (int i = 0; i < nt; i++) if (triKeep[i]) keep[t[i].x] = keep[t[i].y] = keep[t[i].z] = 0; int nk = 0; for (int i = 0; i < n; i++) if (keep[i] == 0) keep[i] = nk++; long long removed = n - nk; if (removed == 0) return 0;
  IArrayPoint3F* nr = f->getPointsNormals(); const Point3F* qn = nr && nr->getSize() == n ? nr->getPointer() : nullptr; IArrayUVCoordinates* uv = f->isTextured() ? f->getUVCoordinates() : nullptr; const UVCoordinates* ouv = uv && uv->getSize() == n ? uv->getPointer() : nullptr;
  IArrayPoint3F* np = nullptr; createArrayPoint3F(&np, nk); IArrayPoint3F* nn = nullptr; if (qn) createArrayPoint3F(&nn, nk); IArrayUVCoordinates* nuv = nullptr; if (ouv) createArrayUVCoordinates(&nuv, nk);
  Point3F* wp = np->getPointer(); Point3F* wn = nn ? nn->getPointer() : nullptr; UVCoordinates* wuv = nuv ? nuv->getPointer() : nullptr; for (int i = 0; i < n; i++) if (keep[i] >= 0) { wp[keep[i]] = q[i]; if (wn) wn[keep[i]] = qn[i]; if (wuv) wuv[keep[i]] = ouv[i]; }
  int ntk = 0; for (int i = 0; i < nt; i++) if (triKeep[i]) ntk++; IArrayIndexTriplet* ntri = nullptr; createArrayIndexTriplet(&ntri, ntk); { IndexTriplet* wt = ntri->getPointer(); int j = 0; for (int i = 0; i < nt; i++) if (triKeep[i]) { IndexTriplet o; o.x = keep[t[i].x]; o.y = keep[t[i].y]; o.z = keep[t[i].z]; wt[j++] = o; } }
  unsigned derived = f->getCalculated() & (CM_PolyNormals | CM_PolyAreas | CM_Centers | CM_Angles | CM_EdgeLengths);   // cached per-triangle data of the old mesh (review D6)
  f->setPoints(np); if (nn) f->setPointsNormals(nn); if (nuv) f->setUVCoordinates(nuv); f->setTriangles(ntri); np->release(); if (nn) nn->release(); if (nuv) nuv->release(); ntri->release();
  if (derived) { f->clear((int)derived); f->calculate(derived, derived); } return removed;
}
static void cleanScan(IScan* s) { auto t0 = clk::now(); long long removed = 0, total = 0; for (int k = 0; k < s->getSize(); k++) { IFrameMesh* f = s->getElement(k); if (!f || !f->getPoints()) continue; total += f->getPoints()->getSize(); removed += cleanFrame(f); }
  // Frames with almost no surface left (scanner pulled away before Stop, or pointed at nothing): Trail11's last 15
  // frames had 300–2000 points against 50 000 normally and sat 4–9 mm off. Drop frames under 10 % of the scan's median.
  std::vector<int> cnt; for (int k = 0; k < s->getSize(); k++) { IFrameMesh* f = s->getElement(k); cnt.push_back(f && f->getPoints() ? f->getPoints()->getSize() : 0); } std::vector<int> sorted = cnt; std::sort(sorted.begin(), sorted.end()); int med = sorted.empty() ? 0 : sorted[sorted.size() / 2]; int dropped = 0;
  if (med > 0 && s->getSize() > 3) { std::lock_guard<std::mutex> l(g_scanMx); for (int k = s->getSize() - 1; k >= 0 && s->getSize() > 3; k--) if (cnt[k] < med / 10) { s->remove(k); dropped++; } }   // under the lock: prepareRecording reads the last frame (review C7)
  logf("frame cleaning: removed %.1f%% of %lld points (bridging triangles, floating patches)%s in %.1f s", total ? 100.0 * removed / total : 0, total, dropped ? (", dropped " + std::to_string(dropped) + " near-empty frames").c_str() : "", std::chrono::duration<double>(clk::now() - t0).count()); }
// Pass refinement (4 Nov, Trail12): the live placement of a new pass was 10–21 mm off (one letter row) while Studio
// joins the same passes perfectly. After a placed pass is registered, it is refined as one rigid body by point-to-
// plane ICP against the search references (≤40 frames each, normals ready) of the other visible scans: 18 rounds,
// nearest within 15 → 6 → 2 mm. Runs on the post-processing thread; the model points are copied under g_scanMx.
namespace pa {
struct Model { std::vector<float> pts, nrm; feat::Grid g; void build() { std::vector<float> c = pts; g.build(std::move(c), 3.0f); }
  bool nearest(const float* q, float maxd, const float*& p, const float*& n) const { int ix = (int)floorf(q[0] / g.cell), iy = (int)floorf(q[1] / g.cell), iz = (int)floorf(q[2] / g.cell); int r = std::min(5, (int)ceilf(maxd / g.cell)); float best = maxd * maxd; int bi = -1;
    for (int dx = -r; dx <= r; dx++) for (int dy = -r; dy <= r; dy++) for (int dz = -r; dz <= r; dz++) { long long k = feat::Grid::key(ix + dx, iy + dy, iz + dz); auto lo = std::lower_bound(g.idx.begin(), g.idx.end(), std::make_pair(k, -1)); for (; lo != g.idx.end() && lo->first == k; ++lo) { const float* w = &g.pts[lo->second * 3]; float d = (w[0] - q[0]) * (w[0] - q[0]) + (w[1] - q[1]) * (w[1] - q[1]) + (w[2] - q[2]) * (w[2] - q[2]); if (d < best) { best = d; bi = lo->second; } } }
    if (bi < 0) return false; p = &g.pts[bi * 3]; n = &nrm[bi * 3]; return true; } };
static bool step(const std::vector<float>& P, const Model& M, double R[3][3], double t[3], float maxd, double& rms, int& matched) {
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
static void worldPts(IScan* s, int frameStep, int ptStep, bool withN, std::vector<float>& P, std::vector<float>* Nn, const double (*STo)[4] = nullptr) { Matrix4x4D STm = s->getScanTransformation(); const double (*ST)[4] = STo ? STo : STm.m; for (int k = 0; k < s->getSize(); k += frameStep) { IFrameMesh* f = s->getElement(k); if (!f || !f->getPoints()) continue; if (withN && !f->hasPointsNormals()) continue; double M[4][4]; mul44(ST, s->getTransformation(k).m, M); const Point3F* q = f->getPoints()->getPointer(); const Point3F* qn = withN ? f->getPointsNormals()->getPointer() : nullptr; int n = f->getPoints()->getSize();
    for (int i = 0; i < n; i += ptStep) { double x = q[i].x, y = q[i].y, z = q[i].z; P.push_back((float)(M[0][0] * x + M[0][1] * y + M[0][2] * z + M[0][3])); P.push_back((float)(M[1][0] * x + M[1][1] * y + M[1][2] * z + M[1][3])); P.push_back((float)(M[2][0] * x + M[2][1] * y + M[2][2] * z + M[2][3]));
      if (Nn && qn) { double a = qn[i].x, b = qn[i].y, c = qn[i].z; Nn->push_back((float)(M[0][0] * a + M[0][1] * b + M[0][2] * c)); Nn->push_back((float)(M[1][0] * a + M[1][1] * b + M[1][2] * c)); Nn->push_back((float)(M[2][0] * a + M[2][1] * b + M[2][2] * c)); } } } }
// Refine scan `s` (world pose = its scan transformation) against the references of the other visible scans.
// Returns true and the corrected scan transformation in `out`.
// Review D2/C10: the reference scans are read with their owner's pose passed in (the shared reference object is no
// longer moved, the search uses it too); the result reports rotation and the share of points matched within 2 mm, and
// the caller accepts it only within logic::acceptRefine limits.
static bool refine(IScan* s, Matrix4x4D& out, double& moved, double& rms, double& rotDeg, double& share) {
  Model M; { std::lock_guard<std::mutex> l(g_scanMx); for (auto& rec : g_scans) { if (rec.scan == s || !rec.visible || !rec.ref || !rec.scan || (rec.group >= 0 && rec.group < (int)g_groups.size() && !g_groups[rec.group].visible)) continue; Matrix4x4D st = rec.scan->getScanTransformation(); worldPts(rec.ref, 1, 10, true, M.pts, &M.nrm, st.m); } }   // every 10th point (58 s at every 4th, 16:36)
  if (M.pts.size() < 3000 * 3) return false; M.build(); std::vector<float> P; worldPts(s, std::max(1, s->getSize() / 25), 30, false, P, nullptr); if (P.size() < 1500 * 3) return false;
  double R[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, t[3] = {0, 0, 0}; int matched = 0; bool ok = true; rms = 0; for (int it = 0; it < 18 && ok; it++) ok = step(P, M, R, t, it < 4 ? 15.0f : it < 9 ? 6.0f : 2.0f, rms, matched); if (!ok) return false;
  Matrix4x4D STm = s->getScanTransformation(); const auto& ST = STm.m; double C[4][4]; for (int r = 0; r < 3; r++) { for (int c = 0; c < 3; c++) C[r][c] = R[r][c]; C[r][3] = t[r]; } C[3][0] = C[3][1] = C[3][2] = 0; C[3][3] = 1; double N[4][4]; mul44(C, ST, N); memcpy(out.m, N, sizeof N);
  // moved = how far the pass's centre moves (the ICP translation alone is about the world origin, metres away)
  double cx = 0, cy = 0, cz = 0; size_t np = P.size() / 3; for (size_t i = 0; i < np; i++) { cx += P[i * 3]; cy += P[i * 3 + 1]; cz += P[i * 3 + 2]; } if (np) { cx /= np; cy /= np; cz /= np; }
  double nx = R[0][0] * cx + R[0][1] * cy + R[0][2] * cz + t[0] - cx, ny = R[1][0] * cx + R[1][1] * cy + R[1][2] * cz + t[1] - cy, nz = R[2][0] * cx + R[2][1] * cy + R[2][2] * cz + t[2] - cz;
  moved = sqrt(nx * nx + ny * ny + nz * nz); rotDeg = logic::rotationDeg(R); share = np ? (double)matched / np : 0; return true;
}
} // namespace pa
// 5 Nov 14:21: global registration of a 328-frame pass never returned (normally ~10 s); the auto-align after the next
// pass waited for it and the window locked ("Not Responding") until the team killed ArtecRec. Each tidy-up step after
// a scan now has a time limit: past it the job is cancelled through its token and the scan is kept as it was.
// At most 3 tidy-up threads work at once (review E2: opening a 40-scan project started 40 full clones together).
struct PostGate { std::mutex m; std::condition_variable cv; int n = 0; void enter() { std::unique_lock<std::mutex> l(m); cv.wait(l, [&] { return n < 3; }); n++; } void leave() { { std::lock_guard<std::mutex> l(m); n--; } cv.notify_one(); } };
static PostGate g_postGate;
// 6 Nov: no time limit. Each item is tracked; Cancel processing sets `cancel` (the SDK step stops within seconds and the
// scan stays as recorded). An item asked to cancel that still runs after logic::giveUpAfterCancel is given up (count
// lowered, gate slot freed) and a late result is thrown away. Save does not wait for tidy-ups (only for frame cleaning).
struct PostItem { IScan* s; clk::time_point t0, cancelAt; std::mutex m; bool done = false, abandoned = false, gated = false, optimise = false; std::atomic<bool> cancel{false}; };
static std::atomic<int> g_cleaningNow{0};   // passes whose frames are being cleaned right now (the scan is changed): Save waits for this only
// 6 Nov (system test): frame cleaning removes frames from a scan; a save or global registration reads every scan. This lock
// is held while one pass is cleaned and for the whole of a save / global registration, so the two never overlap.
static std::mutex g_frameEditMx;
static std::mutex g_itemsMx; static std::vector<std::shared_ptr<PostItem>> g_items; static std::atomic<int> g_lostPosts{0};   // results that could not be delivered
static void postProcessScan(IScan* s, bool optimise, bool fresh, std::shared_ptr<PostItem> item) {   // s carries one reference for this thread; g_postBusy was raised by startPostProcess; fresh = a pass just recorded (autosaved when done)
  g_postGate.enter(); { std::lock_guard<std::mutex> l(item->m); if (item->abandoned) { g_postGate.leave(); s->release(); return; } item->gated = true; item->t0 = clk::now(); } auto t0 = clk::now(); int nf = s->getSize(); IScan* r = nullptr;
  if (optimise && nf >= 1) {
    // publish first, then let a search already in flight finish (it may be reading this scan); if it does not finish in
    // 20 s, skip cleaning rather than rewrite frames under it (review C7)
    { std::lock_guard<std::mutex> l(g_cleanMx); g_cleaning.push_back(s); } g_cleaningNow++;
    int w = 0; for (; w < 400 && (g_searchBusy.load() || g_searchZombie.load()); w++) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (!g_searchBusy.load() && !g_searchZombie.load()) { std::lock_guard<std::mutex> fe(g_frameEditMx); cleanScan(s); } else logf("frame cleaning skipped: the position search is still running");   // cleanScan takes the scan lock only while removing frames (C7)
    { std::lock_guard<std::mutex> l(g_cleanMx); g_cleaning.erase(std::remove(g_cleaning.begin(), g_cleaning.end(), s), g_cleaning.end()); } g_cleaningNow--; nf = s->getSize();
    // search reference right away (instant clone): after a loss mid-pass the search must compare against THIS scan,
    // not the one before it; the reference used to arrive only after registration + refinement, 10–20 s later
    // (5 Nov 11:03: second recovery failed, searched against scan 1 while the scanner was over the end of scan 2).
    if (nf >= 3) { IScan* early = buildRef(s, 40); if (early) { std::lock_guard<std::mutex> l(g_scanMx); bool used = false; for (auto& rec : g_scans) if (rec.scan == s) { if (rec.ref) rec.ref->release(); rec.ref = early; used = true; break; } if (!used) early->release(); } }
    // image memory for the position search right away (~1 s), so a resume just after a tracking loss can find this
    // scan while its registration (5–15 s) still runs; the registered result replaces it when WM_POSTDONE lands
    if (g_featSearch && nf >= 3) { std::vector<FrameFeat> early = buildFeats(s, 40); std::lock_guard<std::mutex> l(g_scanMx); for (auto& rec : g_scans) if (rec.scan == s) { rec.feats = std::move(early); break; } } }
  if (optimise && nf >= 3 && item->cancel.load()) logf("surface optimisation cancelled before it started; scan kept as recorded");
  if (optimise && nf >= 3 && !item->cancel.load()) { bool allTex = true; for (int k = 0; k < nf; k++) { IFrameMesh* f = s->getElement(k); if (!f || !f->isTextured()) { allTex = false; break; } }
    IModel* in = nullptr; createModel(&in); IModel* res = nullptr; createModel(&res); in->add(s);
    alg::SerialRegistrationSettings sd; sd.scannerType = s->getScannerType(); sd.registrationType = allTex ? alg::SerialRegistrationType_FineTextured : alg::SerialRegistrationType_Fine;
    alg::IAlgorithm* a = nullptr; ErrorCode ec = alg::createSerialRegistrationAlgorithm(&a, &sd);
    if (ec == ErrorCode_OK && a) { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = in; ws.out = res; ws.threadsCount = std::thread::hardware_concurrency(); ec = execWatched(a, &ws, "surface optimisation", &item->cancel); a->release(); }
    double dt = std::chrono::duration<double>(clk::now() - t0).count();
    if (ec == ErrorCode_OK && res->getSize() > 0 && res->getElement(0) && res->getElement(0)->getSize() == nf) { r = res->getElement(0); r->addRef(); r->setScanTransformation(s->getScanTransformation()); if (s->getName()) r->setName(s->getName());
      logf("surface optimisation (fine registration, %s) of %d frames in %.1f s", allTex ? "geometry+texture" : "geometry", nf, dt); }
    else logf("surface optimisation %s 0x%08X in %.1f s; scan kept as recorded", ec == ErrorCode_OperationAborted ? "cancelled" : "failed", (unsigned)ec, dt);
    in->release(); res->release(); }
    // 6 Nov (owner, option A): global registration is no longer run on every pass. On the field laptop it never finished
    // within minutes on passes over ~250 frames. It is run like Studio's Tools > Global registration, from the Edit menu.
  // pass refinement for a scan the live search placed (its pose is a guess from a few frames): rigid ICP against the other
  // scans, accepted only within limits (review D2: a 6.6 mm move was applied unchecked on 5 Nov)
  if (optimise && r && nf >= 3 && !item->cancel.load()) { bool placed = false; { std::lock_guard<std::mutex> l(g_scanMx); for (auto& rec : g_scans) if (rec.scan == s) placed = rec.placed; }
    if (placed) { auto tr = clk::now(); Matrix4x4D nT; double moved = 0, rms = 0, rot = 0, share = 0; double secs = 0;
      if (pa::refine(r, nT, moved, rms, rot, share)) { secs = std::chrono::duration<double>(clk::now() - tr).count(); logic::RefineVerdict v = logic::acceptRefine(moved, rot, share);
        if (v.ok) { r->setScanTransformation(nT); logf("pass refinement: moved %.1f mm, turned %.2f deg, %.0f%% of points within 2 mm (residual %.2f mm), %.1f s", moved, rot, share * 100, rms, secs); }
        else { logf("pass refinement rejected: %s (moved %.1f mm, turned %.2f deg, %.0f%% matched); the pass keeps its live position", v.why, moved, rot, share * 100); g_alignMsg = std::wstring(L"a join could not be tightened - check it on screen"); } }
      else logf("pass refinement: no overlap found with the other scans (%.1f s)", std::chrono::duration<double>(clk::now() - tr).count()); } }
  if (fresh && !optimise) logf("field mode: pass of %d frames kept as recorded (no clean-up, optimisation or auto-align); place the passes in the lab (Edit menu) or in Studio", nf);
  PostResult* pr = new PostResult(); pr->fresh = fresh; pr->src = s; pr->res = r ? r : s; ScanRec tmp; tmp.scan = pr->res; buildScanDisplay(tmp); pr->disp = std::move(tmp.disp); memcpy(pr->texAvg, tmp.texAvg, sizeof pr->texAvg); pr->hasTexAvg = tmp.hasTexAvg; tmp.scan = nullptr;
  pr->ref = nf >= 3 ? buildRef(pr->res, 40) : nullptr;
  if (g_featSearch && nf >= 3) pr->feats = buildFeats(pr->res, 40);
  { std::lock_guard<std::mutex> l(item->m); if (item->abandoned) { if (pr->ref) pr->ref->release(); if (pr->res && pr->res != pr->src) pr->res->release(); pr->src->release(); delete pr; return; } item->done = true; g_postGate.leave(); }
  { bool posted = false; for (int t = 0; t < 50 && !(posted = PostMessageW(g_hwnd, WM_POSTDONE, 0, (LPARAM)pr) != 0); t++) std::this_thread::sleep_for(std::chrono::milliseconds(100));   // second outside review (F03)
    if (!posted) { logf("tidy-up result could not be delivered (error %lu): dropped, scan kept as recorded", GetLastError()); if (pr->ref) pr->ref->release(); if (pr->res && pr->res != pr->src) pr->res->release(); pr->src->release(); delete pr; g_lostPosts++; } }
}
// Review C3/A3/E3: the busy count is raised here, on the UI thread, before the thread exists, and lowered by
// onPostDone on the UI thread after the result is swapped in. postIdle() is therefore exact: no queued result exists.
// Threads are detached: each holds its own references, and nothing waits for them at exit (review B6).
static void startPostProcess(IScan* s, bool optimise, bool fresh) { s->addRef(); g_postBusy++; g_optimising = true; auto it = std::make_shared<PostItem>(); it->s = s; it->t0 = clk::now(); it->optimise = optimise; { std::lock_guard<std::mutex> l(g_itemsMx); g_items.push_back(it); } std::thread(postProcessScan, s, optimise, fresh, it).detach(); }
// Cancel processing (Edit menu, Close, Open, New): every running tidy-up stops; its scan stays as recorded.
static int cancelProcessing() { int n = 0; std::lock_guard<std::mutex> l(g_itemsMx); for (auto& p : g_items) { std::lock_guard<std::mutex> lk(p->m); if (!p->done && !p->abandoned && !p->cancel.load()) { p->cancel = true; p->cancelAt = clk::now(); n++; } } if (n) logf("processing cancelled by the user (%d pass(es)); kept as recorded", n); return n; }
// UI timer: a tidy-up asked to cancel that has not returned after logic::giveUpAfterCancel is given up. Returns true if one was.
static bool abandonStuckPost() { bool any = false; for (int lp = g_lostPosts.exchange(0); lp > 0; lp--) { if (g_postBusy.load() > 0) g_postBusy--; any = true; } std::lock_guard<std::mutex> l(g_itemsMx);
  for (auto it = g_items.begin(); it != g_items.end();) { auto& p = *it; bool drop = false; { std::lock_guard<std::mutex> lk(p->m); if (p->done) drop = true; else if (p->cancel.load() && logic::giveUpAfterCancel(std::chrono::duration<double>(clk::now() - p->cancelAt).count())) { p->abandoned = true; drop = true; any = true; g_postGate.leave(); if (g_postBusy.load() > 0) g_postBusy--; } }
    if (drop) it = g_items.erase(it); else ++it; }
  if (any) { logf("a pass's processing did not stop within 60 s of Cancel: given up, the pass is kept as recorded"); g_optimising = g_postBusy.load() > 0; } return any; }
static bool postIdle() { return g_postBusy.load() == 0; }
static bool cleaningNow() { return g_cleaningNow.load() > 0; }
// passes whose surface optimisation is queued or running (display rebuilds after Open do not count)
static int optimisingNow() { int n = 0; std::lock_guard<std::mutex> l(g_itemsMx); for (auto& p : g_items) { std::lock_guard<std::mutex> lk(p->m); if (p->optimise && !p->done && !p->abandoned) n++; } return n; }
static void rebuildMaster();
static void autosaveScan(IScan* s, const std::wstring& name); static void autosaveRename(IScan* from, IScan* to); static void autosaveCovered(bool saved);
// 6 Nov: a pass whose optimisation ends while a save runs (the window keeps working during the save) was saved as
// recorded: after the save the project stays NOT SAVED and the new version is autosaved again (the save clears autosave).
static bool g_saving = false; static std::vector<std::pair<IScan*, std::wstring>> g_changedWhileSaving;
static void onPostDone(PostResult* pr) {   // UI thread
  IScan* doneScan = nullptr; std::wstring doneName; bool fresh = pr->fresh;
  { std::lock_guard<std::mutex> l(g_scanMx); for (auto& rec : g_scans) if (rec.scan == pr->src) { if (pr->res != pr->src) { autosaveRename(rec.scan, pr->res); rec.scan->release(); rec.scan = pr->res; pr->res = nullptr; } doneScan = rec.scan; doneName = rec.name; rec.disp = std::move(pr->disp); memcpy(rec.texAvg, pr->texAvg, sizeof rec.texAvg); rec.hasTexAvg = pr->hasTexAvg; if (rec.ref) rec.ref->release(); rec.ref = pr->ref; pr->ref = nullptr; rec.feats = std::move(pr->feats); countScanStats(rec); break; } }
  bool changed = pr->res == nullptr;   // a replaced (fine-registered) scan modifies the project; a display/reference build alone does not
  if (pr->ref) pr->ref->release(); if (pr->res && pr->res != pr->src) pr->res->release(); pr->src->release(); delete pr; rebuildMaster(); if (changed) g_dirty = true;
  if (g_postBusy.load() > 0) g_postBusy--; g_optimising = g_postBusy.load() > 0;
  if (fresh && doneScan) autosaveScan(doneScan, doneName);   // autosave: the finished pass, in the background
  if (changed && g_saving && doneScan) { doneScan->addRef(); g_changedWhileSaving.push_back({doneScan, doneName}); }
}
// Called on the UI thread when the scanning job has ended: move recorded scans into the master model.
static int onJobDone() {
  if (g_job.joinable()) g_job.join(); int added = 0;
  if (g_jobOut) {
    joinSearchBounded();   // review B1: stopped, at most 10 s
    // review C6: one pose per segment (Pause/Resume); the last segment's state is still live
    if (!g_segPushedAtPause) pushSegmentPose(); g_segPushedAtPause = false; std::vector<logic::SegmentPose> poses = g_segPoses; g_segPoses.clear();
    bool perSegment = (int)poses.size() == g_jobOut->getSize(); if (!perSegment) logf("%d segment result(s) for %d scan(s): no live placement this time; auto-align will place them", (int)poses.size(), g_jobOut->getSize());
    for (int i = 0; i < g_jobOut->getSize(); i++) { IScan* s = g_jobOut->getElement(i); if (!s || s->getSize() <= 0) continue;
      bool found = perSegment && poses[i].found; const double (*PT)[4] = perSegment ? poses[i].T : nullptr;
      if (logic::discardStub(s->getSize(), found, g_afterLoss)) { logf("scan with %d frame(s) discarded%s", s->getSize(), s->getSize() >= 10 ? " (the stub between a tracking loss and Stop; it was not placed)" : ""); continue; }   /* 3 Nov: 3- and 9-frame stubs; 6 Nov: 13- and 16-frame stubs after a loss */
      if (found) { Matrix4x4D T; memcpy(T.m, PT, sizeof T.m); s->setScanTransformation(T);
        // 5 Nov: the frames recorded while searching belong to the same continuous tracking as the rest of this scan, so
        // the found pose places them too — keep them (they were discarded before, losing a few seconds per join).
        logf("scan placed with the position found live; %d search frames kept", (int)g_foundAt.load()); }
      if (g_alignLive.load() && !found) logf("position was not found during this scan; it keeps its own coordinates");
      ScanRec r; r.scan = s; r.placed = found; setScanName(r, nextScanName()); r.keyFrames = perSegment ? poses[i].keyFrames : (int)g_keyFrames.load(); r.maxErr = perSegment ? (float)poses[i].maxErr : g_maxErr.load(); countScanStats(r);
      // Studio groups a scan that was placed by the live search with the scan it was placed against (the most recent
      // visible one). The old "group with the scan that ended at the loss" rule left 1-scan groups when the resumed
      // scan was a discarded stub (3 Nov 16:52 screenshot).
      if (r.placed) { int ref = -1; for (int k = (int)g_scans.size() - 1; k >= 0; k--) if (g_scans[k].visible && g_scans[k].scan) { ref = k; break; }
        if (ref >= 0) { std::lock_guard<std::mutex> l(g_scanMx); auto& prev = g_scans[ref]; if (prev.group < 0) { GroupRec g; wchar_t nm[64]; swprintf(nm, 64, L"Group %d", (int)g_groups.size() + 1); g.name = nm; g_groups.push_back(g); prev.group = (int)g_groups.size() - 1; } r.group = prev.group; } }   // under the lock: tidy-up threads read g_groups (review C5)
      s->addRef(); g_master->add(s); { std::lock_guard<std::mutex> l(g_scanMx); g_scans.push_back(std::move(r)); } added++; g_dirty = true; }
    // 6 Nov: the passes the new pass was joined to are anchors from now on (logic::anchorJoinTargets)
    { std::vector<int> jt; { std::lock_guard<std::mutex> l(g_joinMx); jt.swap(g_joinedTo); } bool anyPlaced = false; for (int i = (int)g_scans.size() - added; i < (int)g_scans.size(); i++) if (i >= 0 && g_scans[i].placed) anyPlaced = true;
      if (anyPlaced && !jt.empty()) { std::lock_guard<std::mutex> l(g_scanMx); std::vector<bool> pl; for (auto& r : g_scans) pl.push_back(r.placed); logic::anchorJoinTargets(pl, jt);
        for (size_t k = 0; k < g_scans.size(); k++) if (pl[k] && !g_scans[k].placed) { g_scans[k].placed = true; logf("scan %d \"%ls\" is an anchor now: the new pass was joined to it", (int)k + 1, g_scans[k].name.c_str()); } } }
    g_resumeGroupWith = -1;
    if (added >= 2) {   // Pause/Resume inside one recording yields several scans; Studio groups them automatically
      std::lock_guard<std::mutex> l(g_scanMx); int g = -1; for (int i = (int)g_scans.size() - added; i < (int)g_scans.size(); i++) if (g_scans[i].group >= 0) g = g_scans[i].group;
      if (g < 0) { GroupRec gr; wchar_t nm[64]; swprintf(nm, 64, L"Group %d", (int)g_groups.size() + 1); gr.name = nm; g_groups.push_back(gr); g = (int)g_groups.size() - 1; }
      for (int i = (int)g_scans.size() - added; i < (int)g_scans.size(); i++) g_scans[i].group = g; }
    g_jobOut->release(); g_jobOut = nullptr;
  }
  if (g_proc) { g_proc->release(); g_proc = nullptr; }
  { std::lock_guard<std::mutex> l(g_tempMx); if (g_tempScan) { g_tempScan->release(); g_tempScan = nullptr; } if (g_tempTex) { g_tempTex->release(); g_tempTex = nullptr; } } g_alignLive = false;
  logf("procedure finished 0x%08X, scans added %d, frames %ld, scans in project %d", (unsigned)g_jobEc.load(), added, g_frames.load(), (int)g_scans.size());
  // review A6: say so when a recording ended in an error or left nothing (only the log knew before)
  if (g_jobEc.load() != ErrorCode_OK && !added && g_frames.load() > 0) notice(L"The last recording ended with a scanner error and nothing could be kept. Check the scanner and record again.");
  { std::lock_guard<std::mutex> l(g_mx); g_liveAcc.clear(); g_live = FrameMesh(); g_hist.clear(); g_histKey.clear(); g_histKeys.clear(); }
  for (int i = (int)g_scans.size() - added; i < (int)g_scans.size(); i++) startPostProcess(g_scans[i].scan, logic::processAfterStop(S.fieldMode), true);   // fine registration + display build off the UI thread (field mode: display + search reference only)
  g_mode = M_Idle; return added;
}
// Rebuild g_master from g_scans (after delete / reorder / align).
static void rebuildMaster() { g_master->clear(); for (auto& r : g_scans) if (r.scan) g_master->add(r.scan); }

static std::thread g_alignThread; static std::atomic<int> g_alignGen{0}; static clk::time_point g_alignStart, g_alignCancelAt; static bool g_pendingAlign = false; static std::atomic<bool> g_alignCancel{false};
// Review B2/C1/C9: auto-align never waits on the UI thread. It starts only when no scan is being tidied (otherwise it is
// queued: g_pendingAlign, run from the timer); it works on its own references to the scans and applies the results to
// the records holding the same scan objects (not by index); display meshes are rebuilt outside the scan lock.
// manual (Edit > Auto-align visible scans now): only the first visible scan stays fixed, every other visible scan may move.
static void runAutoAlign(bool manual = false) {   // aligns the visible scans to each other (Studio: "Auto-align new scans with those marked in Workspace")
  if (!postIdle() || g_mode.load() != M_Idle) { if (!manual) g_pendingAlign = true; return; }
  g_pendingAlign = false; g_mode = M_Aligning; g_alignMsg = L"aligning..."; g_alignStart = clk::now(); g_alignCancel = false; int gen = ++g_alignGen;
  if (g_alignThread.joinable()) g_alignThread.detach();
  struct Item { IScan* s; bool placed; std::wstring name; };
  std::vector<Item> items; { std::lock_guard<std::mutex> l(g_scanMx); for (auto& r : g_scans) if (r.visible && r.scan) { r.scan->addRef(); items.push_back({r.scan, manual ? items.empty() : r.placed, r.name}); } }
  g_alignThread = std::thread([items, gen] {
    IModel* in = nullptr; createModel(&in); for (auto& it : items) { ensureNormals(it.s); in->add(it.s); }
    ErrorCode ec = ErrorCode_OK; double secs = 0; IModel* out = nullptr; createModel(&out); int moved = 0;
    struct Moved { IScan* s; Matrix4x4D T; }; std::vector<Moved> moves;
    if (in->getSize() >= 2) {
      alg::AutoAlignSettings as; as.scannerType = in->getElement(0)->getScannerType(); alg::IAlgorithm* a = nullptr; ec = alg::createAutoalignAlgorithm(&a, &as);
      // 5 Nov 14:26: auto-align of a 297-frame pass ran 6+ minutes. 6 Nov: no time limit; it reports progress, so a hang
      // (no progress for 5 min) is cancelled, and Edit > Cancel processing stops it at once.
      auto t0 = clk::now(); if (ec == ErrorCode_OK && a) { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = in; ws.out = out; ws.threadsCount = std::thread::hardware_concurrency(); ec = execWatched(a, &ws, "auto-align", &g_alignCancel); a->release(); }
      secs = std::chrono::duration<double>(clk::now() - t0).count();
      // 4 Nov 16:57: scans already placed are anchors: their returned transforms are not applied; an unplaced scan's
      // transform is expressed relative to the first placed scan's returned pose, so the anchors stay put.
      if (ec == ErrorCode_OK && out->getSize() == in->getSize()) {
        int anchor = -1; for (int k = 0; k < (int)items.size(); k++) if (items[k].placed) { anchor = k; break; }
        double Ain[4][4], Aout[4][4], Ainv[4][4], Afix[4][4]; if (anchor >= 0) { memcpy(Ain, in->getElement(anchor)->getScanTransformation().m, sizeof Ain); memcpy(Aout, out->getElement(anchor)->getScanTransformation().m, sizeof Aout); invRigid(Aout, Ainv); mul44(Ain, Ainv, Afix); }
        for (int k = 0; k < (int)items.size(); k++) { if (items[k].placed) { logf("auto-align: scan %d \"%ls\" kept (placed earlier)", k + 1, items[k].name.c_str()); continue; }
          Matrix4x4D T = out->getElement(k)->getScanTransformation(); if (anchor >= 0) { double N[4][4]; mul44(Afix, T.m, N); memcpy(T.m, N, sizeof N); }
          const auto& Tin = in->getElement(k)->getScanTransformation().m; double d = sqrt((T.m[0][3] - Tin[0][3]) * (T.m[0][3] - Tin[0][3]) + (T.m[1][3] - Tin[1][3]) * (T.m[1][3] - Tin[1][3]) + (T.m[2][3] - Tin[2][3]) * (T.m[2][3] - Tin[2][3])); bool mv = d > 0.01;
          if (mv) { moved++; moves.push_back({items[k].s, T}); } logf("auto-align: scan %d \"%ls\" %s (moved %.1f mm)", k + 1, items[k].name.c_str(), mv ? "moved" : "kept", d); } }
      logf("auto-align 0x%08X in %.1f s (in %d, out %d, scans with a transform %d)", (unsigned)ec, secs, in->getSize(), out->getSize(), moved);
      // The SDK returns OK even when RANSAC found nothing: then no scan has a transform.
      if (ec == ErrorCode_OK && items.size() >= 2 && moved == 0) ec = ErrorCode_OperationFailed;
    }
    out->release(); in->release();
    if (gen == g_alignGen.load()) {   // not abandoned: apply, then rebuild the moved scans' display outside the lock
      for (auto& mv : moves) { ScanRec tmp; { std::lock_guard<std::mutex> l(g_scanMx); if (gen != g_alignGen.load()) break; /* R2-02: abandoned meanwhile - apply nothing more */ bool still = false; for (auto& r : g_scans) if (r.scan == mv.s) { r.scan->setScanTransformation(mv.T); r.placed = true; still = true; } /* cloud review: aligned = anchor, so a later auto-align does not move it again */ if (!still) continue; }
        { std::wstring nm; { std::lock_guard<std::mutex> l(g_scanMx); for (auto& r : g_scans) if (r.scan == mv.s) nm = r.name; } if (!nm.empty()) autosaveScan(mv.s, nm); }   // autosave the new position
        tmp.scan = mv.s; buildScanDisplay(tmp); { std::lock_guard<std::mutex> l(g_scanMx); if (gen == g_alignGen.load()) for (auto& r : g_scans) if (r.scan == mv.s) { r.disp = std::move(tmp.disp); memcpy(r.texAvg, tmp.texAvg, sizeof r.texAvg); r.hasTexAvg = tmp.hasTexAvg; } } tmp.scan = nullptr; }
      wchar_t m[96]; if (ec == ErrorCode_OK) swprintf(m, 96, L"aligned in %.0f s", secs); else if (ec == ErrorCode_OperationFailed) wcscpy(m, L"could not align the new scan (too little overlap)"); else if (ec == ErrorCode_OperationAborted) wcscpy(m, L"auto-align was cancelled; the scans keep their positions"); else swprintf(m, 96, L"align failed (0x%08X)", (unsigned)ec);
      if (gen == g_alignGen.load()) { g_alignMsg = std::wstring(m); PostMessageW(g_hwnd, WM_ALIGNDONE, (WPARAM)gen, 0); } }
    for (auto& it : items) it.s->release();
  });
}
static bool cancelAlign() { if (g_mode.load() != M_Aligning || g_alignCancel.load()) return false; g_alignCancel = true; g_alignCancelAt = clk::now(); return true; }
// Review B3: an auto-align that ignores its cancel is abandoned 60 s after Cancel so the app is usable and the scans can be saved.
static bool abandonStuckAlign() { if (g_mode.load() != M_Aligning || !g_alignCancel.load() || !logic::giveUpAfterCancel(std::chrono::duration<double>(clk::now() - g_alignCancelAt).count())) return false; { std::lock_guard<std::mutex> l(g_scanMx); ++g_alignGen; }   /* R2-02: never between a generation check and an apply */ if (g_alignThread.joinable()) g_alignThread.detach(); g_mode = M_Idle; logf("auto-align did not stop within 60 s of Cancel: abandoned"); g_alignMsg = L"auto-align did not finish - check the scans; save the project"; return true; }
static void onAlignDone(int gen) { if (gen != g_alignGen.load()) return; if (g_alignThread.joinable()) g_alignThread.join(); rebuildMaster(); g_mode = M_Idle; }

// 6 Nov (owner, option A): global registration as Studio's Tools > Global registration: on the visible scans, when the
// user asks, with progress and Esc to cancel. No time limit (it reports progress; the window keeps drawing). The registered
// scans replace the originals; their display and search references are rebuilt in the background.
static ErrorCode runJobPumped(IJob* job, AlgorithmWorkset* ws, const wchar_t* what, int askAfterSecs, std::vector<IScan*>* keepAlive, bool cancellable);
static int globalRegisterVisible(double& secs) {
  secs = 0; std::vector<IScan*> items; { std::lock_guard<std::mutex> l(g_scanMx); for (auto& r : g_scans) if (r.visible && r.scan && (r.group < 0 || g_groups[r.group].visible)) { r.scan->addRef(); items.push_back(r.scan); } }
  std::unique_lock<std::mutex> fe(g_frameEditMx, std::try_to_lock); if (!fe.owns_lock()) { for (IScan* it : items) it->release(); return -3; }
  if (items.empty()) return 0; int frames = 0; IModel* in = nullptr; createModel(&in); for (IScan* it : items) { in->add(it); frames += it->getSize(); } IModel* out = nullptr; createModel(&out);
  alg::GlobalRegistrationSettings gd; gd.scannerType = items[0]->getScannerType(); gd.registrationType = alg::GlobalRegistrationType_Geometry; alg::IAlgorithm* ga = nullptr; ErrorCode ec = alg::createGlobalRegistrationAlgorithm(&ga, &gd);
  auto t0 = clk::now(); int replaced = 0;
  if (ec == ErrorCode_OK && ga) { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = in; ws.out = out; ws.threadsCount = std::thread::hardware_concurrency(); std::vector<IScan*> keep;
    ec = runJobPumped(ga, &ws, L"Global registration", 300, &keep, true); ga->release(); }
  secs = std::chrono::duration<double>(clk::now() - t0).count();
  if (ec == ErrorCode_OK && out->getSize() == (int)items.size()) {
    for (int k = 0; k < (int)items.size(); k++) { IScan* g = out->getElement(k); if (!g || g->getSize() != items[k]->getSize()) continue;
      bool swapped = false; std::wstring nm; { std::lock_guard<std::mutex> l(g_scanMx); for (auto& r : g_scans) if (r.scan == items[k]) { g->addRef(); if (items[k]->getName()) g->setName(items[k]->getName()); autosaveRename(r.scan, g); r.scan->release(); r.scan = g; r.placed = true; nm = r.name; swapped = true; break; } }
      if (swapped) { replaced++; startPostProcess(g, false); autosaveScan(g, nm); } }
    if (replaced) { rebuildMaster(); g_dirty = true; } }
  logf("global registration %s 0x%08X of %d scan(s), %d frames, in %.1f s; %d scan(s) replaced", ec == ErrorCode_OperationAborted ? "cancelled" : ec == ErrorCode_OK ? "done" : "failed", (unsigned)ec, (int)items.size(), frames, secs, replaced);
  out->release(); in->release(); for (IScan* it : items) it->release();
  return ec == ErrorCode_OK ? replaced : (ec == ErrorCode_OperationAborted ? -1 : -2);
}

// ---------------------------------------------------------------- project
static std::wstring projDir(const std::wstring& sproj) { size_t sl = sproj.find_last_of(L"\\/"); return sproj.substr(0, sl); }
// Writes a whole file through a temporary name and an atomic replace, so a crash mid-write never leaves a broken file
// (review A5: the .sproj names rewrite and the sidecar used to truncate in place).
static bool writeFileAtomic(const std::wstring& path, const std::string& bytes) {
  std::wstring tmp = path + L".tmp"; FILE* f = _wfopen(tmp.c_str(), L"wb"); if (!f) return false; bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size(); ok = fflush(f) == 0 && ok; fclose(f);
  if (!ok) { DeleteFileW(tmp.c_str()); return false; } return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0; }
// Sidecar: groups are not part of the .sproj format. Review D4: "placed" (already joined) is saved; D5: each scan line
// carries its frame count so a reopened project only takes a line that matches its scan.
static bool saveGroups(const std::wstring& sproj) {
  std::wstring w; wchar_t b[4200];   // DeepSeek 6: names are limited to 200 characters at rename; room to spare
  for (auto& g : g_groups) { swprintf(b, 4200, L"G\t%s\t%d\n", g.name.c_str(), g.visible ? 1 : 0); w += b; }
  int i = 0; for (auto& sc : g_scans) { swprintf(b, 4200, L"S\t%d\t%s\t%d\t%d\t%d\t%.4f\t%d\t%d\n", i++, sc.name.c_str(), sc.group, sc.visible ? 1 : 0, sc.keyFrames, sc.maxErr, sc.placed ? 1 : 0, sc.scan ? sc.scan->getSize() : 0); w += b; }
  if (!writeFileAtomic(projDir(sproj) + L"\\artecrec-groups.txt", "\xEF\xBB\xBF" + logic::utf8(w))) { logf("groups file could not be written"); return false; } return true;
}
static void loadGroups(const std::wstring& sproj) {
  FILE* f = _wfopen((projDir(sproj) + L"\\artecrec-groups.txt").c_str(), L"r, ccs=UTF-8"); if (!f) return; wchar_t line[4300]; g_groups.clear(); int skipped = 0;
  while (fgetws(line, 4300, f)) { std::wstring s = line; while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r')) s.pop_back(); std::vector<std::wstring> c; size_t p = 0;
    for (;;) { size_t q = s.find(L'\t', p); c.push_back(s.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p)); if (q == std::wstring::npos) break; p = q + 1; }
    if (c.size() >= 3 && c[0] == L"G") { GroupRec g; g.name = c[1]; g.visible = c[2] == L"1"; g_groups.push_back(g); }
    else if (c.size() >= 5 && c[0] == L"S") { int i = _wtoi(c[1].c_str()); if (i >= 0 && i < (int)g_scans.size()) { auto& r = g_scans[i];
        if (c.size() > 8 && r.scan && _wtoi(c[8].c_str()) != r.scan->getSize()) { skipped++; continue; }   // not the same scan: leave its Studio name
        setScanName(r, c[2]); r.group = _wtoi(c[3].c_str()); r.visible = c[4] == L"1"; if (c.size() > 5) r.keyFrames = _wtoi(c[5].c_str()); if (c.size() > 6) r.maxErr = _wtof(c[6].c_str()); /* loaded scans stay anchors (cloud review); the saved flag is informational */ } } }
  fclose(f); for (auto& r : g_scans) if (r.group >= (int)g_groups.size()) r.group = -1; if (skipped) logf("groups file: %d scan line(s) did not match their scan and were ignored", skipped);
}
// The SDK's project writer names scans "Scan 0, 1, ...". Rewrite the Name="..." attributes in the .sproj XML (in scan
// order) to "<group> - <name>" so Studio shows the ArtecRec names and groups. UTF-8, atomic (review A5).
static bool writeStudioNames(const std::wstring& sproj) {
  FILE* f = _wfopen(sproj.c_str(), L"rb"); if (!f) { logf("studio names: project file not readable"); return false; } std::string x; char buf[4096]; size_t n; while ((n = fread(buf, 1, sizeof buf, f)) > 0) x.append(buf, n); fclose(f);
  std::vector<std::wstring> names; for (auto& sc : g_scans) names.push_back(sc.group >= 0 && sc.group < (int)g_groups.size() ? g_groups[sc.group].name + L" - " + sc.name : sc.name);
  int k = logic::rewriteScanNames(x, names); if (!writeFileAtomic(sproj, x)) { logf("studio names could not be written"); return false; } logf("studio names written for %d of %d scans", k, (int)names.size()); return k == (int)names.size();
}
// Long SDK jobs (save: 287 s for a 4.9 GB project on 2 Nov; open) run in a worker thread while the UI thread pumps
// messages, so the window keeps drawing and shows progress, as Studio does. Input is guarded by g_mode == M_Busy.
static std::atomic<int> g_busyPct{-1}; static std::wstring g_busyText;
struct Prog : public ProgressInfoBase { std::atomic<bool> abandoned{false};   // DeepSeek 3-1: a job given up on stops reporting
  std::atomic<long long> lastMs{0}; std::atomic<bool> any{false}; void tick() { lastMs = WatchProg::nowMs(); any = true; }
  void report(int current, int total) override { if (abandoned.load()) return; tick(); g_busyPct = total > 0 ? (int)((long long)current * 100 / total) : -1; } void pulse() override { tick(); } void notify(DetailsInfo) override { tick(); } };
// Round 2 (R2-01): a save or open whose SDK job does not return is no longer a dead end. After `askAfterSecs` the user is
// asked whether to keep waiting; giving up detaches the job (it holds its own references to the job and models, and
// writes only into its own folder) and returns ErrorCode_OperationAborted. The scans stay in memory.
struct PumpState { std::atomic<bool> done{false}; ErrorCode ec = ErrorCode_OK; AlgorithmWorkset ws; std::vector<IScan*> keep; };   // keep: scans the job reads, released when it ends (DeepSeek 2-4)
// 6 Nov: no time limit. cancellable: Esc asks "Cancel <what>?" and cancels the job (it stops within seconds).
// The "keep waiting?" question comes only when the job's progress has stopped moving (logic::askKeepWaiting).
static ErrorCode runJobPumped(IJob* job, AlgorithmWorkset* ws, const wchar_t* what, int askAfterSecs = 300, std::vector<IScan*>* keepAlive = nullptr, bool cancellable = false) {
  g_busyText = cancellable ? std::wstring(what) + L" (Esc to cancel)" : std::wstring(what); g_busyPct = -1; Prog* prog = new Prog(); prog->lastMs = WatchProg::nowMs(); auto st = std::make_shared<PumpState>(); st->ws = *ws; st->ws.progress = prog;
  ICancellationTokenSource* cts = nullptr; if (cancellable) { createCancellationTokenSource(&cts); if (cts) st->ws.cancellation = cts->getToken(); } bool userCancelled = false; if (keepAlive) { st->keep = std::move(*keepAlive); keepAlive->clear(); }
  job->addRef(); if (st->ws.in) st->ws.in->addRef(); if (st->ws.out) st->ws.out->addRef(); prog->addRef();
  std::thread t([st, job, prog] { st->ec = executeJob(job, &st->ws); if (st->ws.in) st->ws.in->release(); if (st->ws.out) st->ws.out->release(); for (IScan* k : st->keep) k->release(); st->keep.clear(); job->release(); prog->release(); st->done = true; });
  auto t0 = clk::now(); bool abandoned = false; bool sawQuit = false; int quitCode = 0;   // Gemini 1: remember a quit, re-post it once after the job (no re-peek every 20 ms)
  auto tStart = clk::now();
  while (!st->done.load()) { MSG m; while (!sawQuit && PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { if (m.message == WM_QUIT) { sawQuit = true; quitCode = (int)m.wParam; break; }
        if (cts && !userCancelled && m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) { std::wstring q = std::wstring(L"Cancel ") + what + L"?\n\nThe scans stay as they are now."; if (MessageBoxW(g_hwnd, q.c_str(), L"ArtecRec", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES && !st->done.load()) { userCancelled = true; cts->cancel(); logf("%ls: cancelled by the user", what); } continue; }
        TranslateMessage(&m); DispatchMessageW(&m); } Sleep(20);
    double silent = (WatchProg::nowMs() - prog->lastMs.load()) / 1000.0, elapsed = std::chrono::duration<double>(clk::now() - t0).count();
    if (logic::askKeepWaiting(prog->any.load(), silent, elapsed, askAfterSecs)) {
      wchar_t q[400]; swprintf(q, 400, L"%s has made no progress for %d minutes.\n\nKeep waiting?\n\nYes = keep waiting.\nNo = give up. Your scans stay in ArtecRec; save them again, if possible to another folder or drive.", what, (int)((prog->any.load() ? silent : std::chrono::duration<double>(clk::now() - tStart).count()) / 60));
      if (MessageBoxW(g_hwnd, q, L"ArtecRec", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON1) == IDYES) { t0 = clk::now(); prog->lastMs = WatchProg::nowMs(); } else if (!st->done.load()) { abandoned = true; break; } } }
  if (abandoned) { prog->abandoned = true; logf("%ls: no progress; given up by the user (job left to finish on its own)", what); t.detach(); } else t.join();
  if (cts && !abandoned) cts->release();   /* an abandoned job may still read its token: left alive */
  ws->progress = nullptr; prog->release(); g_busyText.clear(); g_busyPct = -1; if (sawQuit) PostQuitMessage(quitCode); return abandoned || userCancelled ? ErrorCode_OperationAborted : st->ec;
}
static bool removeDir(const std::wstring& dir) { std::wstring dz = dir; dz.push_back(L'\0'); SHFILEOPSTRUCTW op = {}; op.wFunc = FO_DELETE; op.pFrom = dz.c_str(); op.fFlags = FOF_NO_UI; return SHFileOperationW(&op) == 0; }
static bool dirExists(const std::wstring& d) { DWORD a = GetFileAttributesW(d.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
static std::vector<logic::FolderEntry> listFolder(const std::wstring& dir, bool* ok = nullptr) {
  std::vector<logic::FolderEntry> v; if (ok) *ok = false; WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd); if (h == INVALID_HANDLE_VALUE) return v; if (ok) *ok = true;
  do { std::wstring n = fd.cFileName; if (n == L"." || n == L"..") continue; v.push_back({n, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0}); } while (FindNextFileW(h, &fd));
  DWORD e = GetLastError(); FindClose(h); if (e != ERROR_NO_MORE_FILES) { if (ok) *ok = false; logf("folder listing stopped part-way (error %lu): treated as not checked", e); }   // DeepSeek 7-1: partial = not listed (fail closed)
  return v; }
// Review A1/A2/A3/F6. Save:
//  - 6 Nov: also while passes are being processed (a pass still processing is saved as recorded; when its processing ends
//    the project shows NOT SAVED again and autosave keeps the new version). Only frame cleaning, which changes a scan for
//    a moment, is waited for (the caller queues). From a snapshot model the UI cannot change;
//  - into a new folder, or by swapping out a folder that is this project's own (logic::canReplaceFolder): never a folder
//    with other files, never a Studio project with entries ArtecRec did not load;
//  - the previous copy is kept as "<name>__previous" (one generation), never deleted outright;
//  - g_saveErr says in plain words what went wrong and where the data is.
static bool saveProjectInner(const std::wstring& sproj);
static bool saveProject(const std::wstring& sproj) {
  std::unique_lock<std::mutex> fe(g_frameEditMx, std::try_to_lock); if (!fe.owns_lock()) { g_saveErr = L"A pass is being cleaned (a few seconds). Save again in a moment."; logf("save refused for a moment: a pass is being cleaned"); return false; }
  g_saving = true; bool ok = saveProjectInner(sproj); g_saving = false; fe.unlock();
  std::vector<std::pair<IScan*, std::wstring>> ch; ch.swap(g_changedWhileSaving);
  for (auto& c : ch) { bool still = false; { std::lock_guard<std::mutex> l(g_scanMx); for (auto& r : g_scans) if (r.scan == c.first) still = true; } if (still) autosaveScan(c.first, c.second); c.first->release(); }
  if (ok && !ch.empty()) { g_dirty = true; logf("%d pass(es) finished optimising during the save: the project needs saving again", (int)ch.size()); }
  return ok; }
static bool saveProjectInner(const std::wstring& sproj) {
  g_saveErr.clear(); if (cleaningNow()) { g_saveErr = L"A pass is being cleaned (a few seconds). Try again in a moment."; return false; }
  std::wstring dir = projDir(sproj), name = sproj.substr(dir.size() + 1), stem = logic::stemOf(name);
  bool exists = dirExists(dir); bool listed = true; std::vector<logic::FolderEntry> entries = exists ? listFolder(dir, &listed) : std::vector<logic::FolderEntry>();
  // second review: re-read the project on disk now: Studio may have added models since ArtecRec opened or saved it
  bool foreignNow = g_projForeign && logic::iEquals(sproj, g_projPath); std::wstring onDisk = dir + L"\\" + name;
  if (exists && GetFileAttributesW(onDisk.c_str()) != INVALID_FILE_ATTRIBUTES) { prj::IProject* q = nullptr; if (prj::openProject(&q, onDisk.c_str()) == ErrorCode_OK && q) { int n = q->getEntryCount(); for (int i = 0; i < n; i++) { prj::EntryInfo e; IString* nm = nullptr; if (q->getEntry(i, &e, &nm) == ErrorCode_OK && e.type != prj::EntryType_Scan) foreignNow = true; if (nm) nm->release(); } q->release(); } }
  bool marker = GetFileAttributesW((dir + L"\\artecrec-project.txt").c_str()) != INVALID_FILE_ATTRIBUTES, sidecar = GetFileAttributesW((dir + L"\\artecrec-groups.txt").c_str()) != INVALID_FILE_ATTRIBUTES;
  bool ours = (logic::iEquals(sproj, g_projPath) && (marker || sidecar)) || (marker && logic::iEquals(sproj, g_replaceOk));   // another ArtecRec project of the same name is replaced only after the user said yes
  logic::SwapVerdict v = logic::canReplaceFolder(exists, entries, stem, ours, foreignNow, listed);
  bool studioDirs = false; for (auto& e : entries) if (e.dir && (logic::iEquals(e.name, L"Models") || logic::iEquals(e.name, L"Data") || logic::iEquals(e.name, L"Thumbnails"))) studioDirs = true;   // Gemini 5
  if (v == logic::Swap_Foreign || v == logic::Swap_NotOurs) { g_saveErr = (foreignNow || studioDirs) ? L"The project in\n" + dir + L"\nnow holds models made in Artec Studio. ArtecRec will not replace it.\n\nUse Save project as... with a new name." : L"The folder\n" + dir + L"\nalready holds other files (or a project ArtecRec did not make). ArtecRec will not replace it.\n\nUse Save project as... with a new name."; logf("save refused: %s is not this project's own folder", logic::utf8(dir).c_str()); return false; }
  bool swap = exists; /* the SDK saver cannot write into an existing folder, even an empty one */ std::wstring outDir = swap ? dir + L"__saving" : dir, outProj = outDir + L"\\" + name;
  for (auto& z : g_zombieSaveDirs) if (logic::iEquals(z, outDir)) { g_saveErr = L"An earlier save of this project was given up and may still be writing to\n" + outDir + L"\n\nSave under a new name, or close and reopen ArtecRec first."; logf("save refused: an abandoned save may still be writing %s", logic::utf8(outDir).c_str()); return false; }   // DeepSeek 2-2
  if (swap && dirExists(outDir)) removeDir(outDir);   // our own leftover from an interrupted save
  bool created = !dirExists(outDir);   // a first save that fails removes the folder it made (second review: a partial folder blocked later saves)
  if (created && SHCreateDirectoryExW(nullptr, outDir.c_str(), nullptr) != ERROR_SUCCESS && !dirExists(outDir)) { g_saveErr = L"Could not create the folder\n" + outDir + L"\n(is the drive full or read-only?)"; return false; }
  prj::ProjectSettings ps; ps.path = outProj.c_str(); prj::IProject* p = nullptr; ErrorCode ec = prj::createNewProject(&p, &ps); if (ec != ErrorCode_OK || !p) { logf("createNewProject 0x%08X", (unsigned)ec); g_saveErr = L"The scanner software could not create the project file."; if (swap || created) removeDir(outDir); return false; }
  prj::ProjectSaverSettings ss; ss.path = outProj.c_str(); generateUuid(&ss.projectId); ss.compressionLevel = 0; IJob* saver = nullptr; ec = p->createSaver(&saver, &ss);
  if (ec != ErrorCode_OK || !saver) { logf("createSaver 0x%08X", (unsigned)ec); p->release(); g_saveErr = L"The scanner software could not start saving."; if (swap || created) removeDir(outDir); return false; }
  IModel* snap = nullptr; createModel(&snap); std::vector<IScan*> keep; { std::lock_guard<std::mutex> l(g_scanMx); for (auto& r : g_scans) if (r.scan) { r.scan->addRef(); keep.push_back(r.scan); snap->add(r.scan); } }   // DeepSeek 2-4: the save holds every scan it writes   // snapshot: the saver never sees g_master change (review A3)
  AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = snap; ws.threadsCount = std::thread::hardware_concurrency();
  auto t0 = clk::now(); ec = runJobPumped(saver, &ws, L"Saving project...", 300, &keep); saver->release(); p->release(); snap->release();
  logf("save 0x%08X %.1f s -> %s%s", (unsigned)ec, std::chrono::duration<double>(clk::now() - t0).count(), logic::utf8(sproj).c_str(), swap ? " (replacing the existing copy; the old one is kept as __previous)" : "");
  if (ec == ErrorCode_OperationAborted) { g_zombieSaveDirs.push_back(outDir); g_saveErr = L"Saving was given up. The unfinished copy is in\n" + outDir + L"\n(do not use it). " + (swap ? L"The previous copy of the project is unchanged. " : L"") + L"Save again, if possible to another folder or drive."; return false; }   // R2-01
  if (ec != ErrorCode_OK) { if (swap || created) removeDir(outDir); g_saveErr = L"The scanner software reported an error while saving (code " + std::to_wstring((unsigned)ec) + L")." + (swap ? L" The previous copy of the project is unchanged." : L" Nothing was kept; save again, if possible to another drive."); return false; }
  // second outside review (F01): the older backup is set aside, not deleted, until the new project is in place
  if (swap) { std::wstring prev = dir + L"__previous", older = dir + L"__previous_old"; bool hadPrev = dirExists(prev);
    if (hadPrev) { if (dirExists(older)) removeDir(older); if (!MoveFileExW(prev.c_str(), older.c_str(), 0)) { DWORD e = GetLastError(); g_saveErr = L"The new copy is saved in\n" + outDir + L"\nbut the older backup could not be set aside (error " + std::to_wstring(e) + L"). Nothing else was changed."; logf("could not set the older backup aside (error %lu)", e); return false; } }
    auto restorePrev = [&] { if (hadPrev && !MoveFileExW(older.c_str(), prev.c_str(), 0)) logf("could not restore %s (error %lu)", logic::utf8(prev).c_str(), GetLastError()); };
    if (!MoveFileExW(dir.c_str(), prev.c_str(), 0)) { DWORD e = GetLastError(); restorePrev(); g_saveErr = L"The new copy is saved in\n" + outDir + L"\nbut the old folder could not be moved aside (error " + std::to_wstring(e) + L"; is a window open on it?). Close Explorer windows on it and save again."; logf("could not move the old project folder aside (error %lu)", e); return false; }
    if (!MoveFileExW(outDir.c_str(), dir.c_str(), 0)) { DWORD e = GetLastError(); bool back = MoveFileExW(prev.c_str(), dir.c_str(), 0) != 0; if (back) restorePrev();
      g_saveErr = back ? L"The new copy is saved in\n" + outDir + L"\nand the old project is unchanged (error " + std::to_wstring(e) + L")." : L"Saving was interrupted. Your data is safe in two folders:\n" + outDir + L"  (the new copy)\n" + prev + L"  (the previous copy)\nDo not delete them; tell the support channel.";
      logf("could not move the new project folder in (error %lu); old folder %s", e, back ? "restored" : "NOT restored"); return false; }
    if (hadPrev) removeDir(older); }
  bool markerOk = writeFileAtomic(dir + L"\\artecrec-project.txt", "Saved by ArtecRec. This folder holds one project; ArtecRec may replace it when the project is saved again.\r\n");   // DeepSeek 2-3
  autosaveCovered(true);   // the passes are in the saved project now
  if (!markerOk) { logf("project marker file could not be written"); notice(L"Saved. A small ArtecRec marker file could not be written; the next save of this project may ask for a new name (the scans are fine)."); }
  if (!saveGroups(sproj)) notice(L"Saved. The ArtecRec names and groups could not be written (the scans are fine; names and groups will be lost on reopening)."); else if (!writeStudioNames(sproj)) notice(L"Saved. The scan names could not all be written for Artec Studio; Studio will show its own names (the scans are fine)."); g_dirty = false; g_projPath = sproj; g_projForeign = false; g_replaceOk.clear(); return true;
}
static void clearProject() { std::lock_guard<std::mutex> l(g_scanMx); for (auto& r : g_scans) { if (r.scan) r.scan->release(); if (r.ref) r.ref->release(); } g_scans.clear(); g_groups.clear(); g_master->clear(); g_projPath.clear(); g_dirty = false; g_alignMsg.clear(); g_projForeign = false; g_replaceOk.clear(); }   // DeepSeek 4-1: a replace confirmation never outlives its project
// Review F5/A2/D4: the current project is cleared only after the new one loaded with at least one scan; a Studio project
// with entries ArtecRec does not load is marked (never overwritten); loaded scans are anchors for later auto-aligns.
// Loads the scans of a project into `out` (a new model the caller releases). Shared by Open and autosave recovery.
static bool loadProjectScans(const std::wstring& sproj, IModel*& out, std::vector<std::wstring>& names, int& other, bool pumped) {
  out = nullptr; names.clear(); other = 0; prj::IProject* p = nullptr; ErrorCode ec = prj::openProject(&p, sproj.c_str()); if (ec != ErrorCode_OK || !p) { logf("openProject 0x%08X", (unsigned)ec); g_saveErr = L"The file could not be read as an Artec project."; return false; }
  int n = p->getEntryCount(); std::vector<Uuid> ids;
  for (int i = 0; i < n; i++) { prj::EntryInfo e; IString* nm = nullptr; if (p->getEntry(i, &e, &nm) == ErrorCode_OK) { if (e.type == prj::EntryType_Scan) { ids.push_back(e.uuid); names.push_back(nm ? nm->getPointer() : L"Scan"); } else other++; } if (nm) nm->release(); }
  if (ids.empty()) { p->release(); g_saveErr = L"This project has no scans that ArtecRec can open (it may hold only models). Your current project is unchanged."; return false; }
  IArrayUuid* use = nullptr; createArrayUuid(&use, (int)ids.size()); for (int i = 0; i < (int)ids.size(); i++) use->setElement(i, ids[i]);
  prj::ProjectLoaderSettings ls; ls.entryList = use; IJob* loader = nullptr; ec = p->createLoader(&loader, &ls); createModel(&out);
  if (ec == ErrorCode_OK && loader) { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.out = out; ws.threadsCount = std::thread::hardware_concurrency(); ec = pumped ? runJobPumped(loader, &ws, L"Opening project...") : executeJob(loader, &ws); loader->release(); }
  use->release(); p->release();
  if (ec != ErrorCode_OK || out->getSize() == 0) { out->release(); out = nullptr; g_saveErr = L"The scans could not be loaded. Your current project is unchanged."; return false; }
  return true;
}
static bool openProject(const std::wstring& sproj) {
  g_saveErr.clear(); prj::IProject* p = nullptr; ErrorCode ec = prj::openProject(&p, sproj.c_str()); if (ec != ErrorCode_OK || !p) { logf("openProject 0x%08X", (unsigned)ec); g_saveErr = L"The file could not be read as an Artec project."; return false; }
  int n = p->getEntryCount(); std::vector<Uuid> ids; std::vector<std::wstring> names; int other = 0;
  for (int i = 0; i < n; i++) { prj::EntryInfo e; IString* nm = nullptr; if (p->getEntry(i, &e, &nm) == ErrorCode_OK) { if (e.type == prj::EntryType_Scan) { ids.push_back(e.uuid); names.push_back(nm ? nm->getPointer() : L"Scan"); } else other++; } if (nm) nm->release(); }
  if (ids.empty()) { p->release(); g_saveErr = L"This project has no scans that ArtecRec can open (it may hold only models). Your current project is unchanged."; logf("open: no scans in %s", logic::utf8(sproj).c_str()); return false; }
  IArrayUuid* use = nullptr; createArrayUuid(&use, (int)ids.size()); for (int i = 0; i < (int)ids.size(); i++) use->setElement(i, ids[i]);
  prj::ProjectLoaderSettings ls; ls.entryList = use; IJob* loader = nullptr; ec = p->createLoader(&loader, &ls);
  IModel* out = nullptr; createModel(&out);
  if (ec == ErrorCode_OK && loader) { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.out = out; ws.threadsCount = std::thread::hardware_concurrency(); auto t0 = clk::now(); ec = runJobPumped(loader, &ws, L"Opening project..."); loader->release();
    logf("open 0x%08X %.1f s, scans %d (other entries not loaded: %d)", (unsigned)ec, std::chrono::duration<double>(clk::now() - t0).count(), out->getSize(), other); }
  use->release(); p->release();
  if (ec != ErrorCode_OK || out->getSize() == 0) { out->release(); g_saveErr = L"The scans could not be loaded. Your current project is unchanged."; return false; }
  clearProject();
  { std::lock_guard<std::mutex> l(g_scanMx); for (int i = 0; i < out->getSize(); i++) { IScan* s = out->getElement(i); if (!s) continue; ScanRec r; r.scan = s; s->addRef(); r.placed = true; r.name = i < (int)names.size() && !names[i].empty() ? names[i] : (s->getName() ? s->getName() : L"Scan"); r.scan->setName(r.name.c_str()); countScanStats(r); g_scans.push_back(std::move(r)); } }
  for (auto& r : g_scans) startPostProcess(r.scan, false);   // display meshes + search reference in the background (no re-registration of saved scans); at most 3 at once
  out->release(); rebuildMaster(); loadGroups(sproj); g_projPath = sproj; g_dirty = false; g_projForeign = other > 0; return true;
}

// ---------------------------------------------------------------- autosave (6 Nov, owner: "Studio has auto save and it
// does not get in the way"). The SDK can only write whole projects, so each finished pass is saved ON ITS OWN, in the
// background, as a small project of one scan: <exe dir>\Autosave\<session>\pass-<id>-<k>\pass.sproj. One low-priority
// worker; only passes whose tidy-up has finished (never the pass being recorded); never the team's project folder;
// paused while a Save or Open runs; skipped when the drive has under 8 GB free. A manifest records every saved pass. A
// successful project save deletes the autosaved passes it now contains; a discarded project is marked. At the next start,
// passes newer than the last save are offered for recovery.
struct AutoItem { IScan* s; int id; std::wstring name; };
static std::mutex g_autoMx; static std::condition_variable g_autoCv; static std::deque<AutoItem> g_autoQ; static bool g_autoRun = false;
static std::wstring g_autoSession; static std::map<IScan*, int> g_autoId; static std::map<int, std::wstring> g_autoFolder; static int g_autoNextId = 1;
static std::atomic<bool> g_autoBusy{false}; static std::atomic<int> g_autoSaved{0};
static std::wstring exeDir() { wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH); std::wstring s = p; return s.substr(0, s.find_last_of(L"\\/")); }
static std::wstring autosaveRoot() { return exeDir() + L"\\Autosave"; }
static void appendManifest(const std::wstring& session, const std::wstring& line) {
  std::string u = logic::utf8(line + L"\n"); FILE* f = _wfopen((session + L"\\autosave.txt").c_str(), L"ab"); if (!f) { logf("autosave manifest could not be written"); return; } fwrite(u.data(), 1, u.size(), f); fflush(f); fclose(f); }
static std::wstring nowStamp() { SYSTEMTIME t; GetLocalTime(&t); wchar_t b[32]; swprintf(b, 32, L"%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond); return b; }
static double driveFreeGB(const std::wstring& path) { ULARGE_INTEGER fb; if (GetDiskFreeSpaceExW(path.c_str(), &fb, nullptr, nullptr)) return fb.QuadPart / 1073741824.0; logf("autosave: free space could not be read (%lu)", GetLastError()); return 1e9; }   /* unknown = do not block autosave; the save itself fails if the drive is full */
static void autosaveWorker() {
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL); bool diskWarned = false;
  for (;;) { AutoItem it; std::wstring session;
    { std::unique_lock<std::mutex> l(g_autoMx); g_autoCv.wait(l, [] { return !g_autoQ.empty(); }); it = g_autoQ.front(); g_autoQ.pop_front(); session = g_autoSession; }
    while (g_mode.load() == M_Busy) std::this_thread::sleep_for(std::chrono::milliseconds(500));   // a Save/Open runs: wait
    g_autoBusy = true; auto t0 = clk::now(); bool ok = false; std::wstring folder;
    if (!logic::autosaveDiskOk(driveFreeGB(exeDir() + L"\\"))) { if (!diskWarned) { diskWarned = true; logf("autosave paused: under 8 GB free on the drive"); notice(L"Autosave is paused: the drive has under 8 GB free. Save the project and free some space."); } }
    else { SHCreateDirectoryExW(nullptr, session.c_str(), nullptr); int k = 0; do { folder = session + L"\\pass-" + std::to_wstring(it.id) + L"-" + std::to_wstring(++k); } while (GetFileAttributesW(folder.c_str()) != INVALID_FILE_ATTRIBUTES);
      SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr); std::wstring sp = folder + L"\\pass.sproj";
      prj::ProjectSettings ps; ps.path = sp.c_str(); prj::IProject* p = nullptr; ErrorCode ec = prj::createNewProject(&p, &ps);
      if (ec == ErrorCode_OK && p) { prj::ProjectSaverSettings ss; ss.path = sp.c_str(); generateUuid(&ss.projectId); ss.compressionLevel = 0; IJob* saver = nullptr; ec = p->createSaver(&saver, &ss);
        if (ec == ErrorCode_OK && saver) { IModel* m = nullptr; createModel(&m); m->add(it.s); AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = m; ws.threadsCount = std::max(1u, std::thread::hardware_concurrency() / 4); ec = execWatched(saver, &ws, "autosave", nullptr, 300); saver->release(); m->release(); ok = ec == ErrorCode_OK; }
        p->release(); }
      if (ok) { appendManifest(session, L"P\t" + std::to_wstring(it.id) + L"\t" + folder + L"\t" + it.name); std::wstring old;
        { std::lock_guard<std::mutex> l(g_autoMx); if (session == g_autoSession) { auto f = g_autoFolder.find(it.id); if (f != g_autoFolder.end()) old = f->second; g_autoFolder[it.id] = folder; } }
        if (!old.empty() && old != folder) removeDir(old); g_autoSaved++;
        logf("autosave: pass \"%s\" saved in %.1f s", logic::utf8(it.name).c_str(), std::chrono::duration<double>(clk::now() - t0).count()); }
      else { removeDir(folder); logf("autosave: pass \"%s\" could not be saved (0x%08X)", logic::utf8(it.name).c_str(), (unsigned)ec); } }
    it.s->release(); g_autoBusy = false; }
}
// Queue a pass for autosave (UI or align thread). Holds its own reference until it is written.
static void autosaveScan(IScan* s, const std::wstring& name) {
  if (!s || replay::on) return;   /* a replay never autosaves */ std::lock_guard<std::mutex> l(g_autoMx);
  if (g_autoSession.empty()) { g_autoSession = autosaveRoot() + L"\\" + nowStamp(); }
  if (!g_autoRun) { g_autoRun = true; std::thread(autosaveWorker).detach(); }
  int id; auto f = g_autoId.find(s); if (f != g_autoId.end()) id = f->second; else { id = g_autoNextId++; g_autoId[s] = id; }
  for (auto it = g_autoQ.begin(); it != g_autoQ.end(); ++it) if (it->id == id) { it->s->release(); g_autoQ.erase(it); break; }   // only the latest state of a pass
  s->addRef(); g_autoQ.push_back({s, id, name}); g_autoCv.notify_one(); }
// The project was saved: its passes are in the project now; their autosaves go. Or the project was discarded.
static void autosaveCovered(bool saved) {
  std::wstring session; std::vector<std::wstring> folders;
  { std::lock_guard<std::mutex> l(g_autoMx); session = g_autoSession; for (auto& f : g_autoFolder) folders.push_back(f.second); g_autoFolder.clear(); g_autoId.clear(); g_autoSession.clear();
    for (auto& q : g_autoQ) q.s->release(); g_autoQ.clear(); }
  if (session.empty()) return; appendManifest(session, std::wstring(saved ? L"S\t" : L"X\t") + nowStamp());
  for (auto& f : folders) removeDir(f);
  if (!g_autoBusy.load()) removeDir(session);   // a pass still being written is left to the start-up clean-up
}
// The scan swapped in by the tidy-up replaces the recorded one: keep the same autosave id.
static void autosaveRename(IScan* from, IScan* to) { std::lock_guard<std::mutex> l(g_autoMx); auto f = g_autoId.find(from); if (f != g_autoId.end()) { int id = f->second; g_autoId.erase(f); g_autoId[to] = id; } }
// At start: sessions with passes newer than their last save. Sessions fully covered are deleted.
static std::vector<std::pair<std::wstring, std::vector<logic::AutoPass>>> autosaveRecoverable() {
  std::vector<std::pair<std::wstring, std::vector<logic::AutoPass>>> res; std::wstring root = autosaveRoot();
  for (auto& e : listFolder(root)) { if (!e.dir) continue; std::wstring session = root + L"\\" + e.name;
    FILE* f = _wfopen((session + L"\\autosave.txt").c_str(), L"r, ccs=UTF-8"); std::vector<std::wstring> lines; if (f) { wchar_t line[2048]; while (fgetws(line, 2048, f)) lines.push_back(line); fclose(f); }
    std::vector<logic::AutoPass> rec = logic::recoverablePasses(lines); std::vector<logic::AutoPass> keep;
    for (auto& a : rec) if (GetFileAttributesW((a.folder + L"\\pass.sproj").c_str()) != INVALID_FILE_ATTRIBUTES) keep.push_back(a);
    if (keep.empty()) { removeDir(session); logf("autosave: old session %s cleaned up", logic::utf8(e.name).c_str()); } else res.push_back({session, keep}); }
  return res; }
// Load recovered passes into the current (empty) project. They stay in the autosave until the project is saved.
static int autosaveRecover(const std::wstring& session, const std::vector<logic::AutoPass>& passes) {
  int added = 0;
  for (auto& a : passes) { IModel* out = nullptr; std::vector<std::wstring> names; int other = 0; if (!loadProjectScans(a.folder + L"\\pass.sproj", out, names, other, true)) { logf("autosave: %s could not be loaded", logic::utf8(a.folder).c_str()); continue; }
    for (int i = 0; i < out->getSize(); i++) { IScan* s = out->getElement(i); if (!s) continue; ScanRec r; r.scan = s; s->addRef(); r.placed = true; r.name = a.name.empty() ? L"Recovered scan" : a.name; s->setName(r.name.c_str()); countScanStats(r);
      { std::lock_guard<std::mutex> l(g_scanMx); g_scans.push_back(std::move(r)); } startPostProcess(s, false); added++;
      { std::lock_guard<std::mutex> l(g_autoMx); g_autoSession = session; g_autoId[s] = a.id; g_autoFolder[a.id] = a.folder; if (a.id >= g_autoNextId) g_autoNextId = a.id + 1; } }
    out->release(); }
  if (added) { rebuildMaster(); g_dirty = true; logf("autosave: %d pass(es) recovered from %s", added, logic::utf8(session).c_str()); }
  return added; }

static double fpsNow() { std::lock_guard<std::mutex> l(g_stmx); if (g_stamps.size() < 2) return 0; double s = std::chrono::duration<double>(g_stamps.back() - g_stamps.front()).count(); return s > 0.2 ? (g_stamps.size() - 1) / s : 0; }
static double wsGB() { PROCESS_MEMORY_COUNTERS c; GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof c); return c.WorkingSetSize / 1073741824.0; }
static double freeGB() { MEMORYSTATUSEX m = {sizeof m}; GlobalMemoryStatusEx(&m); return m.ullAvailPhys / 1073741824.0; }
