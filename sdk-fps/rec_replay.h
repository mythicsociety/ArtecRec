#pragma once
// ---------------------------------------------------------------- replay source (6 Nov, owner: "is there a way to simulate
// scanning without a human?"). The frames of a saved pass are fed into the same observer the scanner calls, at the
// scanner's rate, so everything after the camera runs unchanged: the live position search, Stop, the tidy-up, anchors,
// auto-align, autosave and Save. The SDK's own frame-to-frame tracking is not replayed; the recorded frame poses stand
// in for it. A tracking loss can be injected (frames reported as RegistrationFailed), which drives ArtecRec's real
// loss-restart path. Enabled by --replay-bench (artecrec.cpp); never active with a scanner.
namespace replay {
static std::atomic<int> state{0};            // 1 preview, 2 record, 3 stop (ScanningState mapped by setState)
static std::atomic<int> fed{0};              // frames delivered in the current procedure
static std::atomic<int> cursor{0};           // next source frame; survives a loss restart (the new procedure continues)
static int endFrame = 0; static IScan* src = nullptr; static double fps = 5.7;
static std::atomic<int> loseFrames{0};       // > 0: that many frames are reported as "registration failed"
static void setSource(IScan* s, int from, int to) { src = s; cursor = from; endFrame = to; if (s) g_scannerId.type = s->getScannerType(); }
static void setState(ScanningState s) { state = s == ScanningState_Record ? 2 : s == ScanningState_Preview ? 1 : 3; }
static void stopNow() { state = 3; }
static void loseTracking(int frames) { loseFrames = frames; }
// = startProcedure for the replay: the same per-procedure resets, then the feeder thread in place of the SDK job.
static bool start(ScanningState initial) {
  if (!src) { logf("replay: no source scan"); return false; }
  createModel(&g_jobOut); g_jobDone = false; g_cbFrames = 0; g_procStart = clk::now(); g_frames = 0; g_regFail = 0; g_lost = 0; g_keyFrames = 0; g_texFrames = 0; g_maxErr = 0; g_lostNow = false;
  prepareRecording(initial == ScanningState_Record);
  { std::lock_guard<std::mutex> l(g_mx); g_liveAcc.clear(); g_live = FrameMesh(); }
  g_stopAsked = false; g_cancelSent = false; g_segPoses.clear(); setState(initial); fed = 0;
  int gen = ++g_jobGen; IModel* jobOut = g_jobOut; jobOut->addRef(); IScan* s = src; s->addRef(); int from = cursor.load(), to = std::min(endFrame, s->getSize());
  if (g_job.joinable()) g_job.join();
  g_job = std::thread([gen, jobOut, s, from, to] {
    IScan* cur = nullptr; int k = from; int lostLeft = 0; auto next = clk::now();
    while (state.load() != 3) {
      if (k >= to) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }   // source used up: wait for Stop
      next += std::chrono::microseconds((long long)(1e6 / fps)); std::this_thread::sleep_until(next);
      IFrameMesh* f = s->getElement(k); Matrix4x4D F = s->getTransformation(k); bool rec = state.load() == 2;
      RegistrationInfo i; memset(&i, 0, sizeof i); i.frame = f; i.transformation = F; i.errorCode = ErrorCode_OK;
      int lf = loseFrames.exchange(0); if (lf > 0) lostLeft = lf;
      if (lostLeft > 0) { lostLeft--; i.frameState = FrameState_RegistrationFailed; i.registrationError = -1; }
      else { i.frameState = FrameState_Ok; i.registrationError = 0.3; i.geometryKeyFrame = (k - from) % 8 == 0; i.textureKeyFrame = f && f->isTextured(); }
      if (rec && i.frameState == FrameState_Ok && f) { if (!cur) createSimilarScan(&cur, s); if (cur) cur->add(f, F); }
      if (!rec && cur) { jobOut->add(cur); cur->release(); cur = nullptr; }   // Pause: the SDK makes one scan per Record segment
      if (g_obs) g_obs->onFrameScanned(&i);
      k++; cursor = k; fed++;
    }
    if (cur) { jobOut->add(cur); cur->release(); }
    if (g_obs) g_obs->onScanningFinished(0);
    s->release(); jobOut->release();
    if (gen != g_jobGen.load()) return;
    g_jobEc = ErrorCode_OK; g_jobDone = true; PostMessageW(g_hwnd, WM_JOBDONE, (WPARAM)gen, 0);
  });
  logf("procedure started (REPLAY of %d frames %d..%d at %.1f fps, %s)", to - from, from, to, fps, initial == ScanningState_Record ? "record" : "preview"); return true;
}
}  // namespace replay
// the SDK procedure or the replay, whichever is running
static void procSetState(ScanningState s) { if (replay::on) replay::setState(s); else if (g_proc) g_proc->setState(s); }
static bool procRunning() { return replay::on ? !g_jobDone && g_job.joinable() : g_proc != nullptr; }
