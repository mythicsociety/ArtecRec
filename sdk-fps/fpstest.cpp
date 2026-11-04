// Artec SDK capture-rate probe. Captures frames for N seconds and prints one line per second:
// frames received, frames the scanner numbered but we never got (gaps), capture() time, errors.
#include <artec/sdk/base/Log.h>
#include <artec/sdk/capturing/IScanner.h>
#include <artec/sdk/capturing/IFrame.h>
#include <artec/sdk/capturing/IArrayScannerId.h>
#include <artec/sdk/capturing/IFrameProcessor.h>
#include <artec/sdk/base/IFrameMesh.h>
#include <artec/sdk/base/IArray.h>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <chrono>
static int hook(int lvl, const wchar_t* m) { wprintf(L"[sdk %d] %s", lvl, m); fflush(stdout); return 0; }
using namespace artec::sdk::capturing;
using clk = std::chrono::steady_clock;
int main(int argc, char** argv) {
  artec::sdk::base::setReportHook(artec::sdk::base::RPTHOOK_INSTALL, hook); artec::sdk::base::setOutputLevel(artec::sdk::base::VerboseLevel_Trace);
  int secs = argc > 1 ? atoi(argv[1]) : 60;
  float setFps = argc > 2 ? (float)atof(argv[2]) : 0.f; bool tex = argc > 3 && atoi(argv[3]) != 0; int ctrl = argc > 4 ? atoi(argv[4]) : 0; bool recon = argc > 5 && atoi(argv[5]) != 0;
  printf("texture capture: %d  control writes per frame: %d  reconstruct mesh per frame: %d\n", (int)tex, ctrl, (int)recon);
  IArrayScannerId* list = nullptr;
  ErrorCode ec = enumerateScanners(&list);
  printf("enumerateScanners: 0x%08X, count=%d\n", (unsigned)ec, list ? list->getSize() : -1); fflush(stdout);
  if (ec != ErrorCode_OK || !list || list->getSize() < 1) return 2;
  ScannerId* id = list->getPointer();
  wprintf(L"scanner: %s serial=%s license=%s type=%d\n", id[0].name, id[0].serial, id[0].license, (int)id[0].type); fflush(stdout);
  IScanner* sc = nullptr;
  ec = createScanner(&sc, &id[0]);
  printf("createScanner: 0x%08X\n", (unsigned)ec); fflush(stdout);
  if (ec != ErrorCode_OK || !sc) return 3;
  printf("depth %dx%d  maxFPS=%.1f  FPS=%.1f", sc->getInfo()->depthMapSizeX, sc->getInfo()->depthMapSizeY, sc->getMaximumFPS(), sc->getFPS());
  if (setFps > 0) { sc->setFPS(setFps); printf("  -> set %.1f, now %.1f", setFps, sc->getFPS()); }
  float g0 = sc->getTextureGain(), gmin = sc->getTextureGainMin(), gmax = sc->getTextureGainMax();
  printf("\ntexture gain %.2f [%.2f..%.2f] flash=%d texflash=%d\n", g0, gmin, gmax, (int)sc->isFlashEnabled(), (int)sc->isTextureFlashEnabled());
  IFrameProcessor* fp = nullptr;
  if (recon) { ec = sc->createFrameProcessor(&fp); printf("createFrameProcessor: 0x%08X\n", (unsigned)ec); if (ec != ErrorCode_OK || !fp) return 4; }
  printf("--- sec  got  gap  avg_ms  max_ms  errs  lastErr  | ctrl_avg_ms ctrl_max_ms | recon_avg_ms recon_max_ms pts recon_err\n"); fflush(stdout);
  auto t0 = clk::now(); auto sec0 = t0; int got = 0, gap = 0, errs = 0, last = -1, sec = 0; unsigned lastErr = 0;
  double sumMs = 0, maxMs = 0, cSum = 0, cMax = 0, rSum = 0, rMax = 0; int n = 0, cn = 0, rn = 0, rErr = 0; long long pts = 0;
  while (std::chrono::duration<double>(clk::now() - t0).count() < secs) {
    IFrame* f = nullptr; auto a = clk::now();
    ec = sc->capture(&f, tex);
    double ms = std::chrono::duration<double, std::milli>(clk::now() - a).count();
    if (ec == ErrorCode_OK && f) {
      got++; int fn = f->getFrameNumber(); if (last >= 0 && fn > last + 1) gap += fn - last - 1; last = fn;
      sumMs += ms; n++; if (ms > maxMs) maxMs = ms;
      if (fp) {                                   // what Studio does next: decode the structured light into a mesh (CPU)
        artec::sdk::base::IFrameMesh* mesh = nullptr; auto r0 = clk::now();
        ErrorCode e2 = tex ? fp->reconstructAndTexturizeMesh(&mesh, f) : fp->reconstructMesh(&mesh, f);
        double rms = std::chrono::duration<double, std::milli>(clk::now() - r0).count();
        rSum += rms; rn++; if (rms > rMax) rMax = rms;
        if (e2 == ErrorCode_OK && mesh) { if (mesh->getPoints()) pts += mesh->getPoints()->getSize(); mesh->release(); } else rErr++;
      }
      f->release();
    } else { errs++; lastErr = (unsigned)ec; Sleep(5); }
    for (int k = 0; k < ctrl; k++) {           // mimic Studio's per-frame camera control traffic (auto-brightness writes gain)
      auto c = clk::now();
      float g = g0 + ((got + k) % 2 ? 0.5f : 0.f); if (g > gmax) g = gmax;
      sc->setTextureGain(g);
      double cms = std::chrono::duration<double, std::milli>(clk::now() - c).count();
      cSum += cms; cn++; if (cms > cMax) cMax = cms;
    }
    if (std::chrono::duration<double>(clk::now() - sec0).count() >= 1.0) {
      printf("%5d %4d %4d %7.1f %7.1f %5d  0x%08X  | %7.1f %7.1f | %7.1f %7.1f %7lld %d\n", ++sec, got, gap, n ? sumMs / n : 0, maxMs, errs, lastErr, cn ? cSum / cn : 0, cMax, rn ? rSum / rn : 0, rMax, rn ? pts / rn : 0, rErr); fflush(stdout);
      got = gap = errs = n = cn = rn = rErr = 0; pts = 0; sumMs = maxMs = cSum = cMax = rSum = rMax = 0; sec0 = clk::now();
    }
  }
  sc->setTextureGain(g0);
  if (fp) fp->release();
  sc->release(); list->release();
  return 0;
}
