// Pipelined Artec probe: thread A captures frames, thread B decodes them into meshes (like Studio should).
// Prints per second: captured, decoded, dropped (queue full), capture ms, decode ms, points.
// usage: fpspipe.exe <seconds> <texture 0|1> <queue depth>
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
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
using namespace artec::sdk::capturing;
using clk = std::chrono::steady_clock;
static int hook(int lvl, const wchar_t* m) { if (lvl <= 1) { wprintf(L"[sdk %d] %s", lvl, m); fflush(stdout); } return 0; }
struct Stats { std::atomic<int> cap{0}, dec{0}, drop{0}, cerr{0}, derr{0}; std::atomic<long long> capUs{0}, decUs{0}, pts{0}; };
int main(int argc, char** argv) {
  artec::sdk::base::setReportHook(artec::sdk::base::RPTHOOK_INSTALL, hook); artec::sdk::base::setOutputLevel(artec::sdk::base::VerboseLevel_Warning);
  int secs = argc > 1 ? atoi(argv[1]) : 60; bool tex = argc > 2 && atoi(argv[2]) != 0; size_t depth = argc > 3 ? (size_t)atoi(argv[3]) : 4;
  IArrayScannerId* list = nullptr; ErrorCode ec = enumerateScanners(&list);
  if (ec != ErrorCode_OK || !list || list->getSize() < 1) { printf("enumerateScanners 0x%08X\n", (unsigned)ec); return 2; }
  IScanner* sc = nullptr; ec = createScanner(&sc, list->getPointer());
  if (ec != ErrorCode_OK || !sc) { printf("createScanner 0x%08X\n", (unsigned)ec); return 3; }
  IFrameProcessor* fp = nullptr; ec = sc->createFrameProcessor(&fp);
  if (ec != ErrorCode_OK || !fp) { printf("createFrameProcessor 0x%08X\n", (unsigned)ec); return 4; }
  printf("pipelined: texture=%d queue=%zu scannerFPS=%.1f\n--- sec  cap  dec  drop  cap_ms  dec_ms  pts  cerr derr\n", (int)tex, depth, sc->getFPS()); fflush(stdout);
  std::deque<IFrame*> q; std::mutex mu; std::condition_variable cv; std::atomic<bool> stop{false}; Stats st;
  std::thread capT([&] {
    while (!stop) { IFrame* f = nullptr; auto a = clk::now(); ErrorCode e = sc->capture(&f, tex);
      st.capUs += (long long)std::chrono::duration<double, std::micro>(clk::now() - a).count();
      if (e == ErrorCode_OK && f) { st.cap++; std::lock_guard<std::mutex> lk(mu); if (q.size() >= depth) { q.front()->release(); q.pop_front(); st.drop++; } q.push_back(f); cv.notify_one(); }
      else { st.cerr++; Sleep(5); } } });
  std::thread decT([&] {
    while (true) { IFrame* f = nullptr; { std::unique_lock<std::mutex> lk(mu); cv.wait(lk, [&] { return stop || !q.empty(); }); if (q.empty()) break; f = q.front(); q.pop_front(); }
      artec::sdk::base::IFrameMesh* m = nullptr; auto a = clk::now();
      ErrorCode e = tex ? fp->reconstructAndTexturizeMesh(&m, f) : fp->reconstructMesh(&m, f);
      st.decUs += (long long)std::chrono::duration<double, std::micro>(clk::now() - a).count();
      if (e == ErrorCode_OK && m) { st.dec++; if (m->getPoints()) st.pts += m->getPoints()->getSize(); m->release(); } else st.derr++;
      f->release(); } });
  auto t0 = clk::now(); int sec = 0;
  while (std::chrono::duration<double>(clk::now() - t0).count() < secs) {
    Sleep(1000); int c = st.cap.exchange(0), d = st.dec.exchange(0), dr = st.drop.exchange(0), ce = st.cerr.exchange(0), de = st.derr.exchange(0);
    long long cu = st.capUs.exchange(0), du = st.decUs.exchange(0), p = st.pts.exchange(0);
    printf("%5d %4d %4d %5d %7.1f %7.1f %6lld %4d %4d\n", ++sec, c, d, dr, c ? cu / 1000.0 / c : 0, d ? du / 1000.0 / d : 0, d ? p / d : 0, ce, de); fflush(stdout);
  }
  stop = true; cv.notify_all(); capT.join(); decT.join();
  { std::lock_guard<std::mutex> lk(mu); for (auto f : q) f->release(); q.clear(); }
  fp->release(); sc->release(); list->release(); return 0;
}
