// ArtecRec by Mythic Society — Artec Studio's scanning screen rebuilt on the Artec 3D Scanning SDK (scanning only).
// Behaviour follows the Artec Studio 17 manual (Scanning, Viewing, Workspace, Settings, Hot Keys chapters).
// Space Spider on the HP ZBook: USB packet clamped to 960 B via flyhook.h (env FLYPKT overrides).
#include "rec_sdk.h"
#include "rec_view.h"
#include "rec_panel.h"
#include "rec_ws.h"
#include <commdlg.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

static const wchar_t* APP = L"ArtecRec by Mythic Society";
enum { IDM_NEW = 101, IDM_OPEN, IDM_SAVE, IDM_SAVEAS, IDM_EXIT, IDM_FIT, IDM_HOME, IDM_SETHOME, IDM_ORTHO, IDM_GRID, IDM_LIGHT, IDM_RANGE,
       IDM_RSOLID, IDM_RWIRE, IDM_RPOINTS, IDM_RWIRESOLID, IDM_RPOINTSSOLID, IDM_SMOOTH, IDM_FLAT, IDM_BF_SHOW, IDM_BF_CULL, IDM_BF_BLACK, IDM_BF_TRANS, IDM_COL_TEX, IDM_COL_SCAN, IDM_COL_SURF,
       IDM_VFRONT, IDM_VBACK, IDM_VLEFT, IDM_VRIGHT, IDM_VTOP, IDM_VBOTTOM, IDM_SHOT, IDM_CUR_MASS, IDM_CUR_ORIGIN,
       IDM_SCANPANEL, IDM_WORKSPACE, IDM_LOG, IDM_AUDIO, IDM_AUTOALIGN, IDM_ZOOMCUR, IDM_SHOT_WHITE, IDM_SHOT_BLACK, IDM_ABOUT, IDM_ALIGNNOW };
static HWND g_view, g_statusBar, g_logBox; static bool g_showPanel = true, g_showWs = true, g_showLog = true; static HMENU g_menu;
static bool g_fitOnPost = false, g_capRestart = false, g_capWedged = false; static int g_capRestarts = 0; static bool g_reconnect = false, g_reconnectAdvised = false; static clk::time_point g_reconnectStart, g_reconnectLast; static bool g_autoRestart = false, g_fullView = false, g_savedPanel = true, g_savedWs = true, g_savedLog = true; static clk::time_point g_lastBeep;
static void layout(); static clk::time_point g_cdStart; static bool g_firstScan = true;
// review B5/B4/F1: user Stop during an automatic restart; consecutive fresh restarts; the scanner stopped sending;
// a save waiting for the tidy-up to finish (and closing after it)
static bool g_userStop = false, g_stallStop = false, g_limitRestart = false; static int g_freshRestarts = 0; static bool g_pendingSave = false, g_pendingClose = false; static std::wstring g_pendingSavePath;
static HFONT g_font, g_fontB, g_fontSmall; static ULONG_PTR g_gdip;

// ---------------------------------------------------------------- scanning control (Studio: Preview / Record / Pause / Stop, Space toggles)
static void startReconnect();
static void beginRecordNow() {
  int m = g_mode.load(); panel::readSettings();
  if (g_stuck) { notice(L"The scanner stopped answering. Save the project, then close and reopen ArtecRec."); return; }
  // review E1: refuse to record when memory is nearly gone (a crash would lose everything); warn earlier
  logic::MemLevel ml = logic::memLevel(freeGB()); if (ml == logic::Mem_Refuse) { notice(L"Memory is almost full. Save the project and start a new one before recording."); logf("record refused: free memory %.1f GB", freeGB()); return; }
  if (ml == logic::Mem_Warn) notice(L"Memory is getting low. Save soon and start a new project for the next stone.");
  // review B7/B9: a failed start says so in the panel (no message box) and starts looking for the scanner again
  if (m == M_Idle) { if (!startProcedure(ScanningState_Record)) { notice(L"Could not start the scanner. ArtecRec is looking for it again. If this stays, unplug the scanner's power and USB, wait 10 s, plug power then USB."); startReconnect(); return; } g_mode = M_Record; g_recSecs = 0; g_recStart = clk::now(); }
  else if (m == M_Preview && g_proc) { prepareRecording(true); g_proc->setState(ScanningState_Record); g_mode = M_Record; g_recSecs = 0; g_recStart = clk::now(); }
  else if (m == M_Paused && g_proc) { prepareRecording(true); g_proc->setState(ScanningState_Record); g_mode = M_Record; g_recStart = clk::now(); }   // SDK: ContinueRecord unsupported; Resume starts a new scan, as Studio does
  else return;
  g_dirty = true; view::follow = true; if (g_scans.empty() && view::dist > 400) view::dist = 320;   // start close enough to see the live surface
  // Keyboard focus goes to the 3D view: hiding a focused panel button leaves no focus window, and hotkeys (Ctrl+S…) were dead after a scan (found 2 Nov T6).
  if (S.hidePanel && !g_fullView) { g_savedPanel = g_showPanel; g_savedWs = g_showWs; g_savedLog = g_showLog; g_showPanel = g_showWs = g_showLog = false; g_fullView = true; layout(); } SetFocus(view::hwnd);
}
static void restorePanels() { if (!g_fullView) return; g_fullView = false; g_showPanel = g_savedPanel; g_showWs = g_savedWs; g_showLog = g_savedLog; layout(); SetFocus(view::hwnd); }
static void doRecord() {
  int m = g_mode.load();
  if (m == M_Record && g_proc) { g_recSecs += std::chrono::duration<double>(clk::now() - g_recStart).count(); pushSegmentPose(); g_segPushedAtPause = true; g_proc->setState(ScanningState_Preview); g_mode = M_Paused; logf("paused"); return; }   // review C6
  if (g_countdown.load() > 0) return;
  if (!logic::allowed(logic::A_Record, m, !postIdle(), g_stuck)) return;
  if (S.delaySec > 0 && m != M_Paused) { if (!logic::allowed(logic::A_Countdown, m, !postIdle(), g_stuck)) return; /* review F7 */ g_countdown = S.delaySec; g_cdStart = clk::now(); logf("recording in %d s", S.delaySec); return; }
  beginRecordNow();
}
static void doPreview() { panel::readSettings(); int m = g_mode.load(); if (g_stuck) { notice(L"The scanner stopped answering. Save the project, then close and reopen ArtecRec."); return; }
  if (m == M_Idle) { if (startProcedure(ScanningState_Preview)) { g_mode = M_Preview; view::follow = true; g_notice.clear(); } else { notice(L"Could not start the scanner. ArtecRec is looking for it again."); startReconnect(); } } else if (m == M_Paused) g_mode = M_Preview; }   // review B7
// review B5: a Stop pressed while an automatic restart is stopping cancels that restart
static void doStop() { g_countdown = 0; int m = g_mode.load(); if (m == M_Record) g_recSecs += std::chrono::duration<double>(clk::now() - g_recStart).count(); if (m == M_Record || m == M_Preview || m == M_Paused) { g_userStop = true; stopProcedure(); } else if (m == M_Stopping) g_userStop = true; }
// Scanner body buttons (manual, "Hardware Buttons"): Record button → Preview, then Record, then toggles Preview/Record; Stop → Stop.
static void scannerButton(int b) {
  int m = g_mode.load();
  if (b == ScannerButton_Stop) { doStop(); return; }
  if (m == M_Idle) doPreview(); else if (m == M_Preview) doRecord(); else if (m == M_Record) doRecord(); else if (m == M_Paused) doRecord();
}

// ---------------------------------------------------------------- project
static std::wstring askPath(bool save) {
  wchar_t file[MAX_PATH] = L""; if (save && !g_projPath.empty()) wcscpy(file, g_projPath.substr(g_projPath.find_last_of(L"\\/") + 1).c_str()); else if (save) wcscpy(file, L"stone.sproj");
  OPENFILENAMEW o = {sizeof o}; o.hwndOwner = g_hwnd; o.lpstrFilter = L"Artec project (*.sproj)\0*.sproj\0"; o.lpstrFile = file; o.nMaxFile = MAX_PATH; o.lpstrDefExt = L"sproj"; o.Flags = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
  o.lpstrTitle = save ? L"Save project (a folder of the same name is created)" : L"Open Artec project"; if (!(save ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o))) return L""; return file;
}
// Review A1/A2/F1/F6/F9: the target folder comes from logic::saveTarget; a project opened from Studio with other entries
// is never overwritten (Save As with a new name instead); a save asked for while scans are still being tidied, or while
// stopping/aligning, waits and runs by itself afterwards; a failure says what happened and where the data is.
static bool doSaveNow(const std::wstring& target) {
  g_mode = M_Busy; SetCursor(LoadCursor(nullptr, IDC_WAIT)); bool ok = saveProject(target); g_mode = M_Idle; SetCursor(LoadCursor(nullptr, IDC_ARROW));
  if (ok) { g_notice.clear(); notice(L"Saved: " + target); } else { std::wstring m = L"The project was NOT saved.\n\n" + (g_saveErr.empty() ? std::wstring(L"See artecrec.log.") : g_saveErr); MessageBoxW(g_hwnd, m.c_str(), APP, MB_ICONERROR); }
  ws::showProps(); return ok;
}
static bool doSave(bool as, bool closing = false) {
  if (g_mode.load() == M_Busy) { notice(L"Saving or opening is already running. Wait for it to finish."); return false; }
  std::wstring target = g_projPath;
  if (g_projForeign && !as && !target.empty()) { MessageBoxW(g_hwnd, L"This project was opened from Artec Studio and holds models ArtecRec cannot keep.\n\nSave it under a new name, so the Studio project stays as it is.", APP, MB_ICONINFORMATION); as = true; }
  if (as || target.empty()) { std::wstring p = askPath(true); if (p.empty()) return false; target = logic::saveTarget(p);
    if (target.empty()) { MessageBoxW(g_hwnd, L"That name cannot be used for a project. Use letters, numbers, spaces and dashes.", APP, MB_ICONWARNING); return false; } }
  int m = g_mode.load();
  if (m == M_Busy) { notice(L"Saving or opening is already running. Wait for it to finish."); return false; }   // second review: never queue from Busy (wrong project)
  if (m == M_Preview || m == M_Record || m == M_Paused || m == M_Stopping) { notice(L"Stop the scanner first, then save."); return false; }
  if (m == M_Aligning || !postIdle()) { if (g_pendingSave) { notice(L"A save is already waiting."); return false; } g_pendingSave = true; g_pendingSavePath = target; notice(L"Will save as soon as the scans are finished processing..."); logf("save queued until the scans are processed"); return false; }
  return doSaveNow(target);
}
static bool confirmDiscard(bool closing = false) { if (!g_dirty) return true; int r = MessageBoxW(g_hwnd, L"Save the project first?", APP, MB_YESNOCANCEL | MB_ICONWARNING); if (r == IDCANCEL) return false; if (r == IDYES) { bool had = g_pendingSave; bool ok = doSave(false, closing); if (!ok && closing && g_pendingSave && !had) { g_pendingClose = true; notice(L"ArtecRec will save and then close once the scans are processed."); return false; } return ok; } return true; }
static void doNew() { if (!logic::allowed(logic::A_New, g_mode.load(), !postIdle(), g_stuck)) { notice(L"Wait until the scans are finished processing."); return; } if (!confirmDiscard()) return; clearProject(); view::selectedScan = -1; ws::refresh(); ws::showProps(); g_firstScan = true; }
static void openPath(const std::wstring& p) { g_mode = M_Busy; SetCursor(LoadCursor(nullptr, IDC_WAIT)); bool ok = openProject(p); g_mode = M_Idle; SetCursor(LoadCursor(nullptr, IDC_ARROW));
  if (!ok) { std::wstring m = L"Could not open the project.\n\n" + (g_saveErr.empty() ? std::wstring(L"See artecrec.log.") : g_saveErr); MessageBoxW(g_hwnd, m.c_str(), APP, MB_ICONERROR); return; }
  view::selectedScan = -1; ws::refresh(); ws::showProps(); view::fit(); view::setHome(); g_firstScan = false; }
static void doOpen() { if (!logic::allowed(logic::A_Open, g_mode.load(), !postIdle(), g_stuck)) { notice(L"Wait until the scans are finished processing."); return; } if (!confirmDiscard()) return; std::wstring p = askPath(false); if (p.empty()) return; openPath(p); }

// ---------------------------------------------------------------- UI
static void updateStatus() {
  int m = g_mode.load(); const wchar_t* ms = m == M_Idle ? L"Click \"Preview\" to begin" : m == M_Preview ? L"Preview" : m == M_Record ? L"Recording in progress" : m == M_Paused ? L"Recording paused" : m == M_Aligning ? L"Auto-aligning…" : m == M_Busy ? L"Working…" : L"Stopping…"; if (g_optimising.load() && (m == M_Idle || m == M_Stopping)) ms = L"Optimising surfaces…";
  std::wstring nt = g_notice.get();
  if (g_reconnect && m == M_Idle) ms = g_scanner ? L"Scanner disconnected - reconnecting..." : L"No scanner - looking for it... (if this stays: unplug the scanner's power and USB, wait 10 s, plug power then USB)";
  static wchar_t busy[96]; if (m == M_Busy && !g_busyText.empty()) { if (g_busyPct.load() >= 0) swprintf(busy, 96, L"%s %d%%", g_busyText.c_str(), g_busyPct.load()); else swprintf(busy, 96, L"%s", g_busyText.c_str()); ms = busy; }
  double secs = g_recSecs + (m == M_Record ? std::chrono::duration<double>(clk::now() - g_recStart).count() : 0); wchar_t b[512]; double fps = fpsNow();
  long shown = g_alignLive.load() ? (g_liveFound.load() ? g_frames.load() - g_foundAt.load() : 0) : g_frames.load();
  if (m == M_Record && g_alignLive.load() && !g_liveFound.load()) ms = L"Searching for position…"; if (m == M_Record && g_lostNow.load()) ms = S.scanUsingAutoAlign ? L"Searching for position…" : L"Tracking lost";
  if (g_pendingSave && m == M_Idle) ms = L"Will save when the scans are processed...";
  swprintf(b, 512, L"%.1f fps\r\n\r\nSurfaces recorded: %ld\r\nRecording time: %d:%02d\r\nRegistration failures: %ld   %s\r\n\r\n%s\r\n%s", (m == M_Idle) ? 0.0 : fps, shown, (int)secs / 60, (int)secs % 60, g_regFail.load(), g_alignMsg.get().c_str(), ms, nt.c_str());
  panel::setStatus(b); panel::enableForMode(m);
  wchar_t sb[640]; swprintf(sb, 640, L"  Build %hs %hs   |   Free RAM: %.0f MB   |   Total memory in use: %.0f MB   |   %s%s%s%s", __DATE__, __TIME__, freeGB() * 1024, wsGB() * 1024, m == M_Idle ? L"Ready" : ms, g_dirty ? L"   |   NOT SAVED" : L"", nt.empty() ? L"" : L"   |   ", nt.c_str()); SetWindowTextW(g_statusBar, sb);
  if (g_showLog) { std::wstring t; { std::lock_guard<std::mutex> l(g_logMx); size_t n = g_logLines.size(); for (size_t i = n > 5 ? n - 5 : 0; i < n; i++) { t += g_logLines[i]; t += L"\r\n"; } } static std::wstring lastT; if (t != lastT) { SetWindowTextW(g_logBox, t.c_str()); lastT = t; } }
}
static void layout() {
  RECT r; GetClientRect(g_hwnd, &r); int W = r.right, H = r.bottom, sbH = 24, logH = g_showLog ? 86 : 0, pw = g_showPanel ? 380 : 0, ww = g_showWs ? 500 : 0;
  MoveWindow(g_statusBar, 0, H - sbH, W, sbH, TRUE); ShowWindow(g_logBox, g_showLog ? SW_SHOW : SW_HIDE); if (g_showLog) MoveWindow(g_logBox, pw, H - sbH - logH, W - pw - ww, logH, TRUE);
  ShowWindow(panel::hwnd, g_showPanel ? SW_SHOW : SW_HIDE); if (g_showPanel) MoveWindow(panel::hwnd, 0, 0, pw, H - sbH, TRUE);
  ShowWindow(ws::hwnd, g_showWs ? SW_SHOW : SW_HIDE); if (g_showWs) MoveWindow(ws::hwnd, W - ww, 0, ww, H - sbH, TRUE);
  view::fullView = g_fullView; if (g_fullView) { sbH = 0; ShowWindow(g_statusBar, SW_HIDE); } else ShowWindow(g_statusBar, SW_SHOW);
  MoveWindow(g_view, pw, 0, W - pw - ww, H - sbH - logH, TRUE);
}
static void buildMenu() {
  g_menu = CreateMenu(); HMENU f = CreatePopupMenu(), e = CreatePopupMenu(), cur = CreatePopupMenu(), v = CreatePopupMenu(), sv = CreatePopupMenu(), rm = CreatePopupMenu(), bf = CreatePopupMenu(), cm = CreatePopupMenu(), w = CreatePopupMenu(), st = CreatePopupMenu(), sb = CreatePopupMenu(), h = CreatePopupMenu();
  AppendMenuW(f, MF_STRING, IDM_NEW, L"New project\tCtrl+N"); AppendMenuW(f, MF_STRING, IDM_OPEN, L"Open project…\tCtrl+O"); AppendMenuW(f, MF_STRING, IDM_SAVE, L"Save project\tCtrl+S"); AppendMenuW(f, MF_STRING, IDM_SAVEAS, L"Save project as…"); AppendMenuW(f, MF_SEPARATOR, 0, nullptr); AppendMenuW(f, MF_STRING, IDM_EXIT, L"Exit");
  AppendMenuW(e, MF_STRING, IDM_ALIGNNOW, L"Auto-align visible scans now"); AppendMenuW(e, MF_SEPARATOR, 0, nullptr); AppendMenuW(cur, MF_STRING, IDM_CUR_MASS, L"Set to mass center"); AppendMenuW(cur, MF_STRING, IDM_CUR_ORIGIN, L"Set to origin of axis grid"); AppendMenuW(e, MF_POPUP, (UINT_PTR)cur, L"Cursor (rotation center)");
  AppendMenuW(sv, MF_STRING, IDM_VFRONT, L"To front\tCtrl+Shift+1"); AppendMenuW(sv, MF_STRING, IDM_VBACK, L"To back\tCtrl+1"); AppendMenuW(sv, MF_STRING, IDM_VRIGHT, L"To right\tCtrl+Shift+3"); AppendMenuW(sv, MF_STRING, IDM_VLEFT, L"To left\tCtrl+3"); AppendMenuW(sv, MF_STRING, IDM_VTOP, L"To top\tCtrl+Shift+7"); AppendMenuW(sv, MF_STRING, IDM_VBOTTOM, L"To bottom\tCtrl+7");
  AppendMenuW(rm, MF_STRING, IDM_RSOLID, L"Render solid"); AppendMenuW(rm, MF_STRING, IDM_RWIRE, L"Render wireframe"); AppendMenuW(rm, MF_STRING, IDM_RPOINTS, L"Render points"); AppendMenuW(rm, MF_STRING, IDM_RWIRESOLID, L"Render wireframe over solid"); AppendMenuW(rm, MF_STRING, IDM_RPOINTSSOLID, L"Render points and solid"); AppendMenuW(rm, MF_SEPARATOR, 0, nullptr); AppendMenuW(rm, MF_STRING, IDM_SMOOTH, L"Smooth shading"); AppendMenuW(rm, MF_STRING, IDM_FLAT, L"Flat shading");
  AppendMenuW(bf, MF_STRING, IDM_BF_SHOW, L"Show"); AppendMenuW(bf, MF_STRING, IDM_BF_CULL, L"Cull"); AppendMenuW(bf, MF_STRING, IDM_BF_BLACK, L"Black"); AppendMenuW(bf, MF_STRING, IDM_BF_TRANS, L"Transparent");
  AppendMenuW(cm, MF_STRING, IDM_COL_TEX, L"Texture\tCtrl+Alt+1"); AppendMenuW(cm, MF_STRING, IDM_COL_SCAN, L"Scan color\tCtrl+Alt+2"); AppendMenuW(cm, MF_STRING, IDM_COL_SURF, L"Surface color\tCtrl+Alt+3");
  AppendMenuW(v, MF_STRING, IDM_FIT, L"Fit to view\tF"); AppendMenuW(v, MF_STRING, IDM_HOME, L"Home\tH"); AppendMenuW(v, MF_STRING, IDM_SETHOME, L"Set current view as Home"); AppendMenuW(v, MF_POPUP, (UINT_PTR)sv, L"Viewpoints"); AppendMenuW(v, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(v, MF_STRING, IDM_ORTHO, L"Orthogonal projection\tCtrl+5"); AppendMenuW(v, MF_STRING, IDM_GRID, L"Show grid\tG"); AppendMenuW(v, MF_STRING, IDM_LIGHT, L"Lighting\tL"); AppendMenuW(v, MF_STRING, IDM_RANGE, L"Range meter"); AppendMenuW(v, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(v, MF_POPUP, (UINT_PTR)rm, L"Rendering"); AppendMenuW(v, MF_POPUP, (UINT_PTR)bf, L"Backface rendering"); AppendMenuW(v, MF_POPUP, (UINT_PTR)cm, L"Color"); AppendMenuW(v, MF_SEPARATOR, 0, nullptr); AppendMenuW(v, MF_STRING, IDM_SHOT, L"Save screenshot…\tCtrl+Shift+S");
  AppendMenuW(w, MF_STRING, IDM_SCANPANEL, L"Scan panel\tF7"); AppendMenuW(w, MF_STRING, IDM_WORKSPACE, L"Workspace\tF11"); AppendMenuW(w, MF_STRING, IDM_LOG, L"Log window\tCtrl+Alt+L");
  AppendMenuW(sb, MF_STRING, IDM_SHOT_WHITE, L"White"); AppendMenuW(sb, MF_STRING, IDM_SHOT_BLACK, L"Black");
  AppendMenuW(st, MF_STRING, IDM_AUTOALIGN, L"After tracking loss: search for position (scan using auto-alignment)"); AppendMenuW(st, MF_STRING, IDM_AUDIO, L"Enable audio warnings in scan mode"); AppendMenuW(st, MF_STRING, IDM_ZOOMCUR, L"Mouse-wheel zoom to cursor"); AppendMenuW(st, MF_POPUP, (UINT_PTR)sb, L"Background for screenshots");
  AppendMenuW(h, MF_STRING, IDM_ABOUT, L"About and hot keys");
  AppendMenuW(g_menu, MF_POPUP, (UINT_PTR)f, L"File"); AppendMenuW(g_menu, MF_POPUP, (UINT_PTR)e, L"Edit"); AppendMenuW(g_menu, MF_POPUP, (UINT_PTR)v, L"View"); AppendMenuW(g_menu, MF_POPUP, (UINT_PTR)w, L"Window"); AppendMenuW(g_menu, MF_POPUP, (UINT_PTR)st, L"Settings"); AppendMenuW(g_menu, MF_POPUP, (UINT_PTR)h, L"Help"); SetMenu(g_hwnd, g_menu);
}
static void syncMenu() {
  CheckMenuItem(g_menu, IDM_ORTHO, view::ortho ? MF_CHECKED : 0); CheckMenuItem(g_menu, IDM_GRID, view::grid ? MF_CHECKED : 0); CheckMenuItem(g_menu, IDM_LIGHT, view::lighting ? MF_CHECKED : 0); CheckMenuItem(g_menu, IDM_RANGE, view::rangeMeter ? MF_CHECKED : 0);
  CheckMenuRadioItem(g_menu, IDM_RSOLID, IDM_RPOINTSSOLID, IDM_RSOLID + view::renderMode, MF_BYCOMMAND); CheckMenuRadioItem(g_menu, IDM_SMOOTH, IDM_FLAT, view::smooth ? IDM_SMOOTH : IDM_FLAT, MF_BYCOMMAND);
  CheckMenuRadioItem(g_menu, IDM_BF_SHOW, IDM_BF_TRANS, IDM_BF_SHOW + view::backface, MF_BYCOMMAND); CheckMenuRadioItem(g_menu, IDM_COL_TEX, IDM_COL_SURF, IDM_COL_TEX + view::colorMode, MF_BYCOMMAND);
  CheckMenuItem(g_menu, IDM_SCANPANEL, g_showPanel ? MF_CHECKED : 0); CheckMenuItem(g_menu, IDM_WORKSPACE, g_showWs ? MF_CHECKED : 0); CheckMenuItem(g_menu, IDM_LOG, g_showLog ? MF_CHECKED : 0);
  CheckMenuItem(g_menu, IDM_AUDIO, S.audio ? MF_CHECKED : 0); CheckMenuItem(g_menu, IDM_AUTOALIGN, S.scanUsingAutoAlign ? MF_CHECKED : 0); CheckMenuItem(g_menu, IDM_ZOOMCUR, view::wheelToCursor ? MF_CHECKED : 0); CheckMenuRadioItem(g_menu, IDM_SHOT_WHITE, IDM_SHOT_BLACK, IDM_SHOT_WHITE + S.shotBg, MF_BYCOMMAND);
}
static void command(int id) {
  switch (id) {
  case IDM_NEW: doNew(); break; case IDM_OPEN: doOpen(); break; case IDM_SAVE: doSave(false); break; case IDM_SAVEAS: doSave(true); break; case IDM_EXIT: PostMessageW(g_hwnd, WM_CLOSE, 0, 0); break;
  case IDM_ALIGNNOW: if (!logic::allowed(logic::A_AutoAlign, g_mode.load(), !postIdle(), g_stuck)) { notice(L"Wait until recording and processing have finished, then try again."); break; } { int vis = 0; for (auto& sc : g_scans) if (sc.visible) vis++; if (vis < 2) { notice(L"Show at least two scans in the Workspace to align them."); break; } } runAutoAlign(true); break;   // review D3
  case IDM_FIT: view::fit(); break; case IDM_HOME: view::home(); break; case IDM_SETHOME: view::setHome(); break; case IDM_ORTHO: view::ortho = !view::ortho; break; case IDM_GRID: view::grid = !view::grid; break; case IDM_LIGHT: view::lighting = !view::lighting; break; case IDM_RANGE: view::rangeMeter = !view::rangeMeter; break;
  case IDM_RSOLID: case IDM_RWIRE: case IDM_RPOINTS: case IDM_RWIRESOLID: case IDM_RPOINTSSOLID: view::renderMode = id - IDM_RSOLID; break; case IDM_SMOOTH: view::smooth = true; break; case IDM_FLAT: view::smooth = false; break;
  case IDM_BF_SHOW: case IDM_BF_CULL: case IDM_BF_BLACK: case IDM_BF_TRANS: view::backface = id - IDM_BF_SHOW; break; case IDM_COL_TEX: view::colorMode = 0; break; case IDM_COL_SCAN: view::colorMode = 1; break; case IDM_COL_SURF: view::colorMode = 2; break;
  case IDM_VFRONT: view::stdView(0); break; case IDM_VBACK: view::stdView(1); break; case IDM_VLEFT: view::stdView(2); break; case IDM_VRIGHT: view::stdView(3); break; case IDM_VTOP: view::stdView(4); break; case IDM_VBOTTOM: view::stdView(5); break;
  case IDM_CUR_MASS: view::massCentre(); break; case IDM_CUR_ORIGIN: view::originCentre(); break;
  case IDM_SHOT: { wchar_t file[MAX_PATH] = L"screenshot.png"; OPENFILENAMEW o = {sizeof o}; o.hwndOwner = g_hwnd; o.lpstrFilter = L"PNG image (*.png)\0*.png\0"; o.lpstrFile = file; o.nMaxFile = MAX_PATH; o.lpstrDefExt = L"png"; o.Flags = OFN_NOCHANGEDIR; if (GetSaveFileNameW(&o)) { if (!view::screenshot(file)) MessageBoxW(g_hwnd, L"Could not save the screenshot.", APP, MB_ICONERROR); } break; }
  case IDM_SCANPANEL: g_showPanel = !g_showPanel; layout(); break; case IDM_WORKSPACE: g_showWs = !g_showWs; layout(); break; case IDM_LOG: g_showLog = !g_showLog; layout(); break;
  case IDM_AUDIO: S.audio = !S.audio; break; case IDM_AUTOALIGN: S.scanUsingAutoAlign = !S.scanUsingAutoAlign; break; case IDM_ZOOMCUR: view::wheelToCursor = !view::wheelToCursor; break; case IDM_SHOT_WHITE: S.shotBg = 0; break; case IDM_SHOT_BLACK: S.shotBg = 1; break;
  case IDM_ABOUT: MessageBoxW(g_hwnd, L"ArtecRec by Mythic Society\nSpace Spider scanning on the Artec 3D Scanning SDK 2.0. Behaviour follows the Artec Studio 17 manual.\n\nScanning: F7 scan panel, Space toggle Preview/Record, scanner buttons as in Studio.\nView: F fit, H home, G grid, L lighting, Ctrl+5 projection; Ctrl+Shift+1/3/7 front/right/top, Ctrl+1/3/7 back/left/bottom; Ctrl+Shift+S screenshot.\nWorkspace: Ctrl+A / Ctrl+D select / deselect all, Space toggle highlighted, Ctrl+Alt+A invert, Ctrl+Alt+click one only, F2 rename, Ctrl+G group, Del delete.\nProject: Ctrl+N / Ctrl+O / Ctrl+S.\n\nMouse: LMB rotate, MMB or LMB+RMB move, RMB zoom, wheel zoom, double-click rotation center.\n\nNot available in the SDK: targets, base removal, real-time fusion, motion tolerance, scanner temperature.", APP, MB_ICONINFORMATION); break;
  }
  syncMenu();
}
static bool hotkey(WPARAM k) {
  bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0, alt = GetKeyState(VK_MENU) < 0; HWND focus = GetFocus(); wchar_t cls[32] = L""; GetClassNameW(focus, cls, 32); bool typing = !_wcsicmp(cls, L"Edit"); bool inList = focus == ws::list;
  if (typing && !(ctrl || k == VK_F7 || k == VK_F11)) return false;
  if (inList && (k == VK_SPACE || k == VK_DELETE || k == VK_F2 || (ctrl && (k == 'A' || k == 'D' || k == 'G')))) return false;   // the Workspace list handles its own keys
  switch (k) {
  case VK_SPACE: doRecord(); return true;
  case VK_F7: command(IDM_SCANPANEL); return true; case VK_F11: command(IDM_WORKSPACE); return true;
  case 'F': if (!ctrl) { view::fit(); return true; } break; case 'H': if (!ctrl) { view::home(); return true; } break; case 'G': if (ctrl) { ws::groupSelected(g_hwnd); return true; } command(IDM_GRID); return true; case 'L': if (ctrl && alt) { command(IDM_LOG); return true; } command(IDM_LIGHT); return true;
  case '5': if (ctrl) { command(IDM_ORTHO); return true; } break; case VK_NUMPAD5: command(IDM_ORTHO); return true;
  case VK_NUMPAD1: view::stdView(ctrl ? 1 : 0); return true;
  case '3': if (ctrl) { view::stdView(shift ? 3 : 2); return true; } break; case VK_NUMPAD3: view::stdView(ctrl ? 2 : 3); return true;
  case '7': if (ctrl) { view::stdView(shift ? 4 : 5); return true; } break; case VK_NUMPAD7: view::stdView(ctrl ? 5 : 4); return true;
  case '2': if (ctrl && alt) { command(IDM_COL_SCAN); return true; } break;
  case '1': if (ctrl && alt) { command(IDM_COL_TEX); return true; } if (ctrl) { view::stdView(shift ? 0 : 1); return true; } break;
  case 'S': if (ctrl && shift) { command(IDM_SHOT); return true; } if (ctrl) { command(IDM_SAVE); return true; } break;
  case 'N': if (ctrl) { command(IDM_NEW); return true; } break; case 'O': if (ctrl) { command(IDM_OPEN); return true; } break;
  case 'A': if (ctrl && alt) { ws::invert(); return true; } if (ctrl) { ws::setAllVisible(true); return true; } break; case 'D': if (ctrl) { ws::setAllVisible(false); return true; } break;
  case VK_ESCAPE: if (g_countdown.load() > 0) { g_countdown = 0; return true; } break;
  }
  if (k == '3' && ctrl && alt) { command(IDM_COL_SURF); return true; }
  return false;
}
static void startReconnect() { if (!g_reconnect) { g_reconnect = true; g_reconnectAdvised = false; g_reconnectStart = clk::now(); g_reconnectLast = clk::time_point(); } int m = g_mode.load(); if (m == M_Preview || m == M_Record || m == M_Paused) stopProcedure(); }
static void onTimer() {
  int m = g_mode.load();
  if (g_countdown.load() > 0) { int left = S.delaySec - (int)std::chrono::duration<double>(clk::now() - g_cdStart).count(); if (left <= 0) { g_countdown = 0; beginRecordNow(); } else g_countdown = left; }
  if (m == M_Record && S.limitFrames && g_frames.load() >= S.frameLimit && !g_autoRestart) { g_autoRestart = true; g_limitRestart = true; g_resumeGroupWith = (int)g_scans.size(); logf("frame limit %d reached: new scan, placed and grouped with this one", S.frameLimit); /* review A8 */ g_recSecs += std::chrono::duration<double>(clk::now() - g_recStart).count(); stopProcedure(); }
  // Studio's "Searching for position" at the start of a scan recorded with "Auto-align new scans with those marked"
  if (m == M_Record && g_alignLive.load() && !g_liveFound.load() && !g_searchBusy.load() && g_tempCount.load() >= 8 && std::chrono::duration<double>(clk::now() - (clk::time_point)g_lastSearch).count() >= 2.0) runPositionSearch();
  // Studio's "Scan using auto-alignment": lost tracking switches almost at once to "Searching for position"; recording resumes in a new, aligned, grouped scan
  // Studio stops recording the moment tracking is lost; with "Scan using auto-alignment" it resumes in a new, aligned, grouped scan.
  // 5 Nov 11:00 (and 4 Nov 16:54): a fresh recording started right after a loss can lock onto whatever the scanner saw
  // at that instant (floor, background) and then every frame fails, even back over the stone — 200 of 201 frames. The
  // rule below only restarts scans with > 10 good frames, so it stayed stuck. If a fresh recording has tracked nothing
  // for 3 s, start it again: the new start locks onto what the scanner sees by then.
  { static clk::time_point lostAt; static bool wasLostR = false; bool lostR = m == M_Record && g_lostNow.load();
    if (lostR && !wasLostR) lostAt = clk::now(); wasLostR = lostR;
    if (lostR && !g_autoRestart && g_storedFrames.load() <= 10 && std::chrono::duration<double>(clk::now() - lostAt).count() > 3.0) { wasLostR = false;
      if (logic::mayRestartFresh(g_freshRestarts, g_userStop, g_reconnect)) { g_freshRestarts++; g_autoRestart = true; g_resumeGroupWith = (int)g_scans.size(); logf("no tracking for 3 s on a fresh recording: restarting it (%d)", g_freshRestarts); }
      else { logf("no tracking after %d fresh restarts: recording stopped", g_freshRestarts); notice(L"The scanner cannot find the surface. Point it at the stone you already scanned, about 25 cm away, and press Record."); g_userStop = true; }   // review B5
      g_recSecs += std::chrono::duration<double>(clk::now() - g_recStart).count(); stopProcedure(); } }
  static int lostRun = 0; if (m == M_Record) { lostRun = g_lostNow.load() ? lostRun + 1 : 0; if (lostRun >= 2 && !g_autoRestart && g_storedFrames.load() > 10) { lostRun = 0; g_autoRestart = true; g_resumeGroupWith = (int)g_scans.size(); logf("tracking lost: ending this scan%s", S.scanUsingAutoAlign ? ", searching for position in a new one" : ""); g_recSecs += std::chrono::duration<double>(clk::now() - g_recStart).count(); stopProcedure(); } } else lostRun = 0;
  // Capture watchdog (09:05 case: Preview ran 17 s with no frame from the scanner). Restart the capture once or twice,
  // then tell the user to reconnect the scanner.
  // review B10: a capture that delivers frames resets the restart budget
  if ((m == M_Preview || m == M_Record) && g_cbFrames.load() > 20) g_capRestarts = 0;
  // review B4: frames stopped arriving after the first one (the old watchdog only covered "never started")
  { static long lastCb = -1; static clk::time_point lastChange = clk::now(); long cb = g_cbFrames.load(); if (cb != lastCb || !(m == M_Preview || m == M_Record)) { lastCb = cb; lastChange = clk::now(); }
    else if (cb > 0 && !g_autoRestart && !g_capRestart && std::chrono::duration<double>(clk::now() - lastChange).count() > 6.0) { logf("the scanner stopped sending frames for 6 s during %s: stopping", m == M_Record ? "recording" : "preview"); notice(L"The scanner stopped sending pictures. The recording was stopped and kept. If it does not come back, unplug the scanner's power and USB, wait 10 s, plug power then USB."); g_stallStop = true; lastChange = clk::now(); stopProcedure(); } }
  if ((m == M_Preview || m == M_Record) && !g_reconnect && g_cbFrames.load() == 0 && !g_autoRestart && !g_capRestart && std::chrono::duration<double>(clk::now() - g_procStart).count() > 6.0) {
    if (g_capRestarts < 2) { g_capRestarts++; g_capRestart = true; logf("no frames from the scanner in 6 s: restarting the capture (%d)", g_capRestarts); stopProcedure(); }
    else { g_capRestarts = 0; g_capWedged = true; logf("no frames from the scanner after two restarts: the cameras need a power cycle"); stopProcedure(); } }
  // Scanner reconnection (USB drop 2 Nov 08:24 and 3 Nov 04:16: the SDK reports a disconnect ~5 s after a scan ends and
  // the scanner object is dead afterwards). Re-enumerate every 2 s while idle, for up to 90 s, without a modal dialog.
  if (g_reconnect && !g_stuck && m == M_Idle && !g_proc && std::chrono::duration<double>(clk::now() - g_reconnectLast).count() >= 2.0) { /* second review: not while an abandoned job holds the scanner */ g_reconnectLast = clk::now();
    disconnectScanner(); if (connectScanner()) { g_reconnect = false; applyLiveSettings(); logf("scanner reconnected"); }
    // 4 Nov: never give up — after 90 s advise a power cycle once (status text, no modal), then keep looking every 5 s
    // so a scanner plugged in hours later (next morning) is picked up without a relaunch.
    else if (!g_reconnectAdvised && std::chrono::duration<double>(clk::now() - g_reconnectStart).count() > 90) { g_reconnectAdvised = true; logf("no scanner for 90 s: still looking every 2 s (status text advises a power cycle)"); } }
  // Reconnected but still no frames (3 Nov 04:26: after a USB drop the cameras stay wedged until power is removed): say so once.
  if (g_capRestarts == 0 && g_capWedged) { g_capWedged = false; notice(L"The scanner is connected but sends no pictures. Unplug its power and USB cables, wait 10 s, plug the power in, then the USB, and press Preview again."); }   // review B9: no message box
  // Studio beeps continuously while tracking is lost and while it searches for the position; stops once tracking resumes.
  if (S.audio && m == M_Record && (g_lostNow.load() || (g_alignLive.load() && !g_liveFound.load())) && std::chrono::duration<double>(clk::now() - g_lastBeep).count() > 0.7) { g_lastBeep = clk::now(); MessageBeep(MB_ICONEXCLAMATION); }
  static clk::time_point lastLog = clk::now(); if (m == M_Record && std::chrono::duration<double>(clk::now() - lastLog).count() >= 60) { lastLog = clk::now(); logf("rec fps %.2f frames %ld regFail %ld lost %ld memGB %.1f", fpsNow(), g_frames.load(), g_regFail.load(), g_lost.load(), wsGB()); }
  if (cancelStuckProcedure()) { restorePanels(); ws::refresh(); notice(L"The scanner stopped answering. Your scans are kept: SAVE THE PROJECT NOW, then close and reopen ArtecRec."); }   // review B3
  if (abandonStuckPost()) notice(L"A scan took too long to finish processing; it is kept as recorded. You can save now.");
  if (abandonStuckAlign()) { ws::refresh(); notice(L"Auto-align did not finish. Check the scans and save the project."); }
  // queued work runs once nothing is recording, aligning or being processed (review B2, F1)
  if (g_mode.load() == M_Idle && postIdle()) {
    if (g_pendingAlign) runAutoAlign();
    else if (g_pendingSave) { g_pendingSave = false; std::wstring t = g_pendingSavePath; bool ok = doSaveNow(t); if (g_pendingClose) { g_pendingClose = false; if (ok) { panel::readSettings(); panel::saveIni(); DestroyWindow(g_hwnd); return; } } } }
  { static clk::time_point lastMem = clk::now(); if (std::chrono::duration<double>(clk::now() - lastMem).count() > 30) { lastMem = clk::now(); if (logic::memLevel(freeGB()) != logic::Mem_Ok && (m == M_Idle || m == M_Record)) { std::wstring cur = g_notice.get(); if (cur.empty() || cur.rfind(L"Memory", 0) == 0) notice(L"Memory is getting low. Save the project and start a new one for the next stone."); logf("low memory: %.1f GB free", freeGB()); } } }   // review E1
  updateStatus(); view::draw();
}
static void jobDone(int gen) {
  if (gen != g_jobGen.load()) return;   // a job abandoned earlier (review B3)
  bool userStop = g_userStop, stall = g_stallStop; g_userStop = false; g_stallStop = false; if (userStop || stall) g_limitRestart = false;
  // capture watchdog: start the capture again, unless the user stopped or the scanner is being reconnected (review B10)
  if (g_capRestart) { g_capRestart = false; onJobDone(); if (!userStop && !g_reconnect) doPreview(); else restorePanels(); return; }
  bool resumeSearch = g_resumeGroupWith >= 0;   // read BEFORE onJobDone, which clears it (bug found 2 Nov T19: no position search after lost tracking)
  int added = onJobDone(); ws::refresh(); ws::showProps(); if (added) g_capRestarts = 0;
  if (added && g_firstScan) { g_fitOnPost = true; g_firstScan = false; }   // fit once the scan's display meshes arrive (WM_POSTDONE)
  if (g_autoRestart) { g_autoRestart = false;
    // review B5: no automatic restart after the user's Stop, a scanner drop, a stalled scanner, or a stuck job
    if (userStop || stall || g_reconnect || g_stuck) { g_resumeGroupWith = -1; g_freshRestarts = 0; restorePanels(); if (g_reconnect) notice(L"The scanner disconnected. ArtecRec is reconnecting; press Record when the scanner is back."); return; }
    bool limit = g_limitRestart; g_limitRestart = false;
    if (resumeSearch && !S.scanUsingAutoAlign && !limit) { g_resumeGroupWith = -1; restorePanels(); return; }   // option off: Studio shows "Tracking lost" and waits for the user
    if (added) g_freshRestarts = 0;
    g_forceAlign = resumeSearch; beginRecordNow(); g_forceAlign = false;   // (S.autoAlign is re-read from the panel inside beginRecordNow, so a flag is used instead)
    if (g_mode.load() != M_Record) { restorePanels(); return; }
    if (resumeSearch && added) g_resumeGroupWith = (int)g_scans.size() - 1;   // the next scan is grouped with the one that just ended (Studio groups resumed scans)
    return; }
  restorePanels(); g_freshRestarts = 0;
  // Post-Stop auto-align only when the newest scan was NOT placed by the live search (3 Nov 16:37). It is queued and runs
  // from the timer once the new scan's tidy-up has finished (review B2: it used to block the window meanwhile).
  bool newestPlaced = !g_scans.empty() && g_scans.back().placed;
  if (added && S.autoAlign && !newestPlaced) { int vis = 0; for (auto& sc : g_scans) if (sc.visible) vis++; if (vis >= 2) g_pendingAlign = true; }
}
static LRESULT CALLBACK wndProc(HWND h, UINT u, WPARAM wp, LPARAM lp) {
  switch (u) {
  case WM_SIZE: layout(); return 0;
  case WM_TIMER: onTimer(); return 0;
  case WM_JOBDONE: jobDone((int)wp); return 0;
  case WM_POSTDONE: onPostDone((PostResult*)lp); ws::refresh(); ws::showProps(); if (g_fitOnPost) { g_fitOnPost = false; view::fit(); view::setHome(); } view::wantRedraw = true; return 0;
  case WM_ALIGNDONE: onAlignDone((int)wp); ws::refresh(); return 0;
  case WM_WSSYNC: ws::syncVisibleFromList(); return 0;
  case WM_SCANBTN: scannerButton((int)wp); updateStatus(); return 0;
  case WM_VIEWBTN: if (wp == 1) doRecord(); else if (wp == 2) doStop(); updateStatus(); return 0;
  case WM_SCANDISC: startReconnect(); updateStatus(); return 0;
  case WM_LOSTBEEP: if (S.audio && std::chrono::duration<double>(clk::now() - g_lastBeep).count() > 1.0) { g_lastBeep = clk::now(); MessageBeep(MB_ICONEXCLAMATION); } return 0;
  case WM_KEYDOWN: if (hotkey(wp)) return 0; break;
  case WM_HSCROLL: panel::readSettings(); applyLiveSettings(); return 0;
  case WM_CONTEXTMENU: { if ((HWND)wp == g_view) { HMENU m = CreatePopupMenu(); AppendMenuW(m, MF_STRING, IDM_FIT, L"Fit to view\tF"); AppendMenuW(m, MF_STRING, IDM_HOME, L"Home\tH"); AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(m, MF_STRING, IDM_VFRONT, L"Front"); AppendMenuW(m, MF_STRING, IDM_VBACK, L"Back"); AppendMenuW(m, MF_STRING, IDM_VLEFT, L"Left"); AppendMenuW(m, MF_STRING, IDM_VRIGHT, L"Right"); AppendMenuW(m, MF_STRING, IDM_VTOP, L"Top"); AppendMenuW(m, MF_STRING, IDM_VBOTTOM, L"Bottom"); AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(m, MF_STRING | (view::ortho ? MF_CHECKED : 0), IDM_ORTHO, L"Orthogonal"); AppendMenuW(m, MF_STRING | (view::grid ? MF_CHECKED : 0), IDM_GRID, L"Grid"); AppendMenuW(m, MF_STRING | (view::lighting ? MF_CHECKED : 0), IDM_LIGHT, L"Lighting"); AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
      const wchar_t* rn[] = {L"Render solid", L"Render wireframe", L"Render points", L"Render wireframe over solid", L"Render points and solid"}; for (int i = 0; i < 5; i++) AppendMenuW(m, MF_STRING | (view::renderMode == i ? MF_CHECKED : 0), IDM_RSOLID + i, rn[i]);
      POINT p; GetCursorPos(&p); int id = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, h, nullptr); DestroyMenu(m); if (id) command(id); } return 0; }
  case WM_NOTIFY: if (ws::notify(h, (NMHDR*)lp)) return ws::nres; break;
  case WM_COMMAND: {
    HWND c = (HWND)lp; int id = LOWORD(wp);
    if (!c) { command(id); return 0; }
    if (c == panel::bPreview) doPreview(); else if (c == panel::bRecord) doRecord(); else if (c == panel::bStop) doStop(); else if (c == panel::bAdv) panel::toggleAdvanced();
    else if (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == EN_CHANGE) { panel::readSettings(); applyLiveSettings(); panel::saveIni(); }
    updateStatus(); return 0; }
  case WM_CLOSE:   // review B3: allowed when the scanner job is stuck, so the scans can be saved and the app closed
    if (!logic::allowed(logic::A_Close, g_mode.load(), !postIdle(), g_stuck)) { MessageBoxW(h, g_mode.load() == M_Aligning ? L"Auto-align is still running. Wait for it to finish." : L"Stop the scan first.", APP, MB_ICONINFORMATION); return 0; }
    if (g_mode.load() == M_Busy) return 0;
    if (!confirmDiscard(true)) return 0; panel::readSettings(); panel::saveIni(); DestroyWindow(h); return 0;
  case WM_DESTROY: PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(h, u, wp, lp);
}
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR cmd, int) {
  wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH); std::wstring dir = exe; dir = dir.substr(0, dir.find_last_of(L"\\/")); SetCurrentDirectoryW(dir.c_str());
  { std::wstring ex = exe; std::transform(ex.begin(), ex.end(), ex.begin(), ::towlower); g_featSearch = ex.find(L"artecrec-fs") != std::wstring::npos; }
  g_log = _wfopen((dir + (g_featSearch ? L"\\artecrec-fs.log" : L"\\artecrec.log")).c_str(), L"a"); logf(g_featSearch ? "ArtecRec FS (image-feature search) by Mythic Society start" : "ArtecRec by Mythic Society start");
  INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&icc); Gdiplus::GdiplusStartupInput gsi; Gdiplus::GdiplusStartup(&g_gdip, &gsi, nullptr);
  setReportHook(RPTHOOK_INSTALL, sdkHook); setOutputLevel(VerboseLevel_Warning);
  if (!getenv("FLYPKT")) _putenv("FLYPKT=960"); if (!flyhook::install()) logf("flyhook install failed");
  createModel(&g_master); g_obs = new Obs();
  // No scanner at start (3 Nov 17:10: cameras wedged after a USB drop, createScanner 0xC0050000): open the window
  // anyway and keep trying to connect; the status bar says what to do. No modal box, no exit.
  bool scannerOk = connectScanner(); if (!scannerOk) logf("no scanner at start: will keep trying");
  WNDCLASSW wc = {}; wc.lpfnWndProc = wndProc; wc.hInstance = hi; wc.lpszClassName = L"ArtecRecMain"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassW(&wc);
  WNDCLASSW gc = {}; gc.lpfnWndProc = view::proc; gc.hInstance = hi; gc.lpszClassName = L"ArtecRecGL"; gc.style = CS_OWNDC | CS_DBLCLKS; gc.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassW(&gc);
  // build date in the title bar (owner, 5 Nov: "how do I know this is the latest version")
  static wchar_t title[128]; swprintf(title, 128, L"%s  -  build %hs %hs", APP, __DATE__, __TIME__); logf("build %s %s", __DATE__, __TIME__);
  g_hwnd = CreateWindowW(L"ArtecRecMain", title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1500, 900, nullptr, nullptr, hi, nullptr);
  g_font = CreateFontW(17, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI"); g_fontB = CreateFontW(18, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI"); g_fontSmall = CreateFontW(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
  wchar_t dev[160]; if (scannerOk) swprintf(dev, 160, L"Artec Scanner [%s]", g_scannerId.serial); else wcscpy(dev, L"(no scanner connected)");
  panel::create(g_hwnd, g_font, g_fontB, g_fontSmall, dev); ws::create(g_hwnd, g_font, g_fontB); ws::onChanged = [] { };
  panel::loadIni(); syncMenu();   // Scan-panel settings as the user left them last time (Studio does the same)
  g_view = CreateWindowW(L"ArtecRecGL", L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, g_hwnd, nullptr, hi, nullptr); view::init(g_view);
  g_logBox = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 10, 10, g_hwnd, nullptr, hi, nullptr); SendMessageW(g_logBox, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
  g_statusBar = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 10, 10, g_hwnd, nullptr, hi, nullptr); SendMessageW(g_statusBar, WM_SETFONT, (WPARAM)g_font, TRUE);
  buildMenu(); syncMenu(); layout(); updateStatus(); ShowWindow(g_hwnd, SW_SHOWMAXIMIZED); SetTimer(g_hwnd, 1, 40, nullptr);
  if (!scannerOk) startReconnect();
  if (cmd && *cmd) { std::wstring p = cmd; if (p.size() > 2 && p.front() == L'"') p = p.substr(1, p.size() - 2); openPath(p); }   // review A9: Busy while loading (no recording meanwhile)
  MSG msg; while (GetMessageW(&msg, nullptr, 0, 0)) {
    if (msg.message == WM_MOUSEWHEEL) { POINT p; GetCursorPos(&p); HWND w = WindowFromPoint(p); if (w && w != panel::hwnd && GetParent(w) == panel::hwnd) msg.hwnd = panel::hwnd; }   // wheel over a slider/checkbox scrolls the Scan panel (Studio), it does not move the control (found 2 Nov T29)
    if (msg.message == WM_KEYDOWN) { wchar_t cls[32] = L""; GetClassNameW(msg.hwnd, cls, 32); bool edit = !_wcsicmp(cls, L"Edit"); if ((!edit || GetKeyState(VK_CONTROL) < 0 || msg.wParam == VK_F7 || msg.wParam == VK_F11) && hotkey(msg.wParam)) continue; }
    TranslateMessage(&msg); DispatchMessageW(&msg); }
  // review B6: never wait for background work at exit (a stuck job kept an invisible process holding the scanner).
  // Release the scanner first, log, then end the process at once; the project was saved (or declined) before this.
  g_searchStop = true; if (!g_stuck) disconnectScanner(); logf("ArtecRec exit"); /* second review: an abandoned job may hold the scanner's lock */ if (g_log) fflush(g_log);
  TerminateProcess(GetCurrentProcess(), 0); return 0;
}
