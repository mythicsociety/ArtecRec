// ArtecRec v3 — Scan panel (left): Studio 17's scan controls on a scrollable child window.
#pragma once
#include <commctrl.h>

namespace panel {
static HWND hwnd;                 // scrollable container
static int contentH = 0, scrollY = 0;
static HWND lTitle, lStatus, bPreview, bRecord, bStop, lTrack, rGeoTex, rGeo, rTargets, cDist, cAlign, cBase, cFusion, cRtp, lSpeed, lSpeedL, lSpeedR, sSpeed;
static HWND lAdv, cHide, lDof, lNear, sNear, lTicks, lFar, sFar, cMotion, lName, eName, lSens, lSensL, lSensR, sSens, lBright, lBrightL, lBrightR, sBright, cAuto, lUse, lUseDev, cNoTex, cNoFlash, lFreq, rFreqAuto, rFreqN, eFreqN, lFreqN, lDelay, eDelay, lDelayS, cLimit, eLimit;
static bool advOpen = true; static HWND bAdv;
static HFONT font, fontB, fontSmall;

static HWND mk(const wchar_t* cls, const wchar_t* text, DWORD style, HFONT f = nullptr) { HWND h = CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, hwnd, nullptr, GetModuleHandle(nullptr), nullptr); SendMessageW(h, WM_SETFONT, (WPARAM)(f ? f : font), TRUE); return h; }
static HWND slider(int lo, int hi, int pos) { HWND s = mk(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP); SendMessageW(s, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi)); SendMessageW(s, TBM_SETPOS, TRUE, pos); return s; }
static void place(HWND h, int x, int y, int w, int hh) { MoveWindow(h, uik(x), uik(y) - scrollY, uik(w), uik(hh), TRUE); }   /* design px -> screen px */

static void layout() {
  RECT r; GetClientRect(hwnd, &r); int W = (int)(r.right / UI_K), x = 16, cw = W - 32, y = 10;   /* design px */
  place(lTitle, x, y, cw, 22); y += 30;
  place(lStatus, x, y, cw, 150); y += 158;
  int bw = (cw - 12) / 3; place(bPreview, x, y, bw, 34); place(bRecord, x + bw + 6, y, bw, 34); place(bStop, x + 2 * (bw + 6), y, bw, 34); y += 48;
  place(lTrack, x, y, cw, 20); y += 22; place(rGeoTex, x, y, cw, 22); y += 24; place(rGeo, x, y, cw, 22); y += 24; place(rTargets, x, y, cw, 22); y += 32;
  place(cDist, x, y, cw, 22); y += 24; place(cAlign, x, y, cw, 22); y += 24; place(cBase, x, y, cw, 22); y += 24; place(cFusion, x, y, cw, 22); y += 24; place(cRtp, x, y, cw, 22); y += 32;
  place(lSpeed, x, y, cw, 20); y += 20; place(lSpeedL, x, y, 60, 18); place(lSpeedR, x + cw - 60, y, 60, 18); y += 18; place(sSpeed, x, y, cw, 26); y += 40;
  place(bAdv, x, y, cw, 26); y += 32;
  HWND advs[] = {cHide, lDof, lNear, sNear, lTicks, lFar, sFar, cMotion, lName, eName, lSens, lSensL, lSensR, sSens, lBright, lBrightL, lBrightR, sBright, cAuto, lUse, lUseDev, cNoTex, cNoFlash, lFreq, rFreqAuto, rFreqN, eFreqN, lFreqN, lDelay, eDelay, lDelayS, cLimit, eLimit};
  for (HWND h : advs) ShowWindow(h, advOpen ? SW_SHOW : SW_HIDE);
  if (advOpen) {
    place(cHide, x, y, cw, 22); y += 30;
    place(lDof, x, y, cw, 20); y += 22; place(lNear, x, y, 100, 20); place(sNear, x + 102, y - 3, cw - 102, 26); y += 26; place(lTicks, x + 102, y, cw - 102, 16); y += 18; place(lFar, x, y, 100, 20); place(sFar, x + 102, y - 3, cw - 102, 26); y += 34;
    place(cMotion, x, y, cw, 22); y += 30;
    place(lName, x, y, cw, 20); y += 22; place(eName, x, y, cw, 24); y += 32;
    place(lSens, x, y, cw, 20); y += 20; place(lSensL, x, y, 60, 18); place(lSensR, x + cw - 60, y, 60, 18); y += 18; place(sSens, x, y, cw, 26); y += 34;
    place(lBright, x, y, cw, 20); y += 20; place(lBrightL, x, y, 60, 18); place(lBrightR, x + cw - 60, y, 60, 18); y += 18; place(sBright, x, y, cw, 26); y += 30;
    place(cAuto, x, y, cw, 22); y += 30;
    place(lUse, x, y, cw, 20); y += 22; place(lUseDev, x, y, cw, 24); y += 30;
    place(cNoTex, x, y, cw, 22); y += 24; place(cNoFlash, x, y, cw, 22); y += 30;
    place(lFreq, x, y, cw, 20); y += 22; place(rFreqAuto, x, y, cw, 22); y += 24; place(rFreqN, x, y, 80, 22); place(eFreqN, x + 82, y, 44, 22); place(lFreqN, x + 130, y + 2, 60, 20); y += 32;
    place(lDelay, x, y, 150, 22); place(eDelay, x + 152, y, 44, 22); place(lDelayS, x + 200, y + 2, 60, 20); y += 32;
    place(cLimit, x, y, 190, 22); place(eLimit, x + 192, y, 70, 22); y += 32;
  }
  contentH = uik(y + 10);
  SCROLLINFO si = {sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, contentH, (UINT)r.bottom, scrollY}; SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}
static void scrollTo(int ny) { RECT r; GetClientRect(hwnd, &r); int maxY = contentH > r.bottom ? contentH - r.bottom : 0; if (ny < 0) ny = 0; if (ny > maxY) ny = maxY; if (ny == scrollY) return; scrollY = ny; layout(); InvalidateRect(hwnd, nullptr, TRUE); }

static void readSettings() {
  S.tracking = SendMessageW(rGeo, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0;
  S.distColor = SendMessageW(cDist, BM_GETCHECK, 0, 0) == BST_CHECKED; S.autoAlign = SendMessageW(cAlign, BM_GETCHECK, 0, 0) == BST_CHECKED; S.hidePanel = SendMessageW(cHide, BM_GETCHECK, 0, 0) == BST_CHECKED;
  S.speed = (int)SendMessageW(sSpeed, TBM_GETPOS, 0, 0); int nearV = (int)SendMessageW(sNear, TBM_GETPOS, 0, 0), farV = (int)SendMessageW(sFar, TBM_GETPOS, 0, 0);
  if (nearV > farV - 20) { if (GetFocus() == sFar) { nearV = farV - 20; SendMessageW(sNear, TBM_SETPOS, TRUE, nearV); } else { farV = nearV + 20; SendMessageW(sFar, TBM_SETPOS, TRUE, farV); } }   // Studio keeps near < far
  { int n0 = nearV, f0 = farV; logic::clampRange(nearV, farV); if (n0 != nearV) SendMessageW(sNear, TBM_SETPOS, TRUE, nearV); if (f0 != farV) SendMessageW(sFar, TBM_SETPOS, TRUE, farV); }   // review F4: an old ini (100..500) is brought into range
  S.nearMm = (float)nearV; S.farMm = (float)farV;
  { wchar_t tk[96]; int d = farV - nearV; swprintf(tk, 96, L"%d            %d            %d            %d", nearV, nearV + d / 3, nearV + 2 * d / 3, farV); SetWindowTextW(lTicks, tk); }   // ticks follow the setting
  S.sens = SendMessageW(sSens, TBM_GETPOS, 0, 0) / 100.f; S.texBright = SendMessageW(sBright, TBM_GETPOS, 0, 0) / 100.f; S.autoBright = SendMessageW(cAuto, BM_GETCHECK, 0, 0) == BST_CHECKED;
  S.recordTexture = SendMessageW(cNoTex, BM_GETCHECK, 0, 0) != BST_CHECKED; S.flash = SendMessageW(cNoFlash, BM_GETCHECK, 0, 0) != BST_CHECKED;
  S.texFreq = SendMessageW(rFreqN, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0; wchar_t b[64]; GetWindowTextW(eFreqN, b, 64); S.texEvery = _wtoi(b); if (S.texEvery < 1) S.texEvery = 1;
  GetWindowTextW(eDelay, b, 64); S.delaySec = _wtoi(b); if (S.delaySec < 0) S.delaySec = 0; S.limitFrames = SendMessageW(cLimit, BM_GETCHECK, 0, 0) == BST_CHECKED; GetWindowTextW(eLimit, b, 64); S.frameLimit = _wtoi(b); if (S.frameLimit < 10) S.frameLimit = 10;
  wchar_t nm[128]; GetWindowTextW(eName, nm, 128); S.scanName = nm;
  // No texture → hybrid tracking is unavailable (Studio greys it): switch to Geometry, and switch back when texture is re-enabled.
  static bool forcedGeo = false;
  if (!S.recordTexture && S.tracking == 0) { S.tracking = 1; forcedGeo = true; SendMessageW(rGeo, BM_SETCHECK, BST_CHECKED, 0); SendMessageW(rGeoTex, BM_SETCHECK, BST_UNCHECKED, 0); }
  else if (S.recordTexture && forcedGeo) { forcedGeo = false; S.tracking = 0; SendMessageW(rGeoTex, BM_SETCHECK, BST_CHECKED, 0); SendMessageW(rGeo, BM_SETCHECK, BST_UNCHECKED, 0); }
  EnableWindow(rGeoTex, S.recordTexture);
  wchar_t t[64]; swprintf(t, 64, L"Near (mm): %.0f", S.nearMm.load()); SetWindowTextW(lNear, t); swprintf(t, 64, L"Far (mm): %.0f", S.farMm.load()); SetWindowTextW(lFar, t);
  swprintf(t, 64, L"Scanning speed   %d fps", S.speed); SetWindowTextW(lSpeed, t);
}
static void enableForMode(int m) {
  bool idle = m == M_Idle, rec = m == M_Record, pv = m == M_Preview, pa = m == M_Paused;
  EnableWindow(bPreview, idle || pa); EnableWindow(bRecord, idle || pv || rec || pa); EnableWindow(bStop, rec || pv || pa);
  SetWindowTextW(bRecord, rec ? L"Pause" : pa ? L"Record" : L"Record");
  for (HWND h : {rGeoTex, rGeo, cNoTex, rFreqAuto, rFreqN, eFreqN, cLimit, eLimit, eName}) EnableWindow(h, idle);
}
static void create(HWND parent, HFONT f, HFONT fb, HFONT fs, const wchar_t* scannerLabel) {
  font = f; fontB = fb; fontSmall = fs;
  WNDCLASSW wc = {}; wc.lpfnWndProc = [](HWND h, UINT u, WPARAM wp, LPARAM lp) -> LRESULT {
    switch (u) {
    case WM_SIZE: layout(); return 0;
    case WM_VSCROLL: { SCROLLINFO si = {sizeof si, SIF_ALL}; GetScrollInfo(h, SB_VERT, &si); int ny = scrollY; switch (LOWORD(wp)) { case SB_LINEUP: ny -= 30; break; case SB_LINEDOWN: ny += 30; break; case SB_PAGEUP: ny -= si.nPage; break; case SB_PAGEDOWN: ny += si.nPage; break; case SB_THUMBTRACK: case SB_THUMBPOSITION: ny = si.nTrackPos; break; } scrollTo(ny); return 0; }
    case WM_MOUSEWHEEL: scrollTo(scrollY - GET_WHEEL_DELTA_WPARAM(wp) / 120 * 60); return 0;
    case WM_COMMAND: case WM_HSCROLL: return SendMessageW(GetParent(h), u, wp, lp);
    case WM_CTLCOLORSTATIC: { HDC d = (HDC)wp; SetBkColor(d, RGB(250, 250, 250)); return (LRESULT)GetStockObject(WHITE_BRUSH); }
    }
    return DefWindowProcW(h, u, wp, lp); };
  wc.hInstance = GetModuleHandle(nullptr); wc.lpszClassName = L"ArtecRecPanel"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH); RegisterClassW(&wc);
  hwnd = CreateWindowW(L"ArtecRecPanel", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0, 10, 10, parent, nullptr, wc.hInstance, nullptr);
  lTitle = mk(L"STATIC", L"SCAN", SS_LEFT, fb); lStatus = mk(L"STATIC", L"", SS_LEFT);
  bPreview = mk(L"BUTTON", L"Preview", BS_PUSHBUTTON | WS_TABSTOP); bRecord = mk(L"BUTTON", L"Record", BS_PUSHBUTTON | WS_TABSTOP); bStop = mk(L"BUTTON", L"Stop", BS_PUSHBUTTON | WS_TABSTOP);
  lTrack = mk(L"STATIC", L"Features to track", SS_LEFT); rGeoTex = mk(L"BUTTON", L"Geometry + Texture", BS_AUTORADIOBUTTON | WS_GROUP); rGeo = mk(L"BUTTON", L"Geometry", BS_AUTORADIOBUTTON); rTargets = mk(L"BUTTON", L"Targets   (not available in the SDK)", BS_AUTORADIOBUTTON);
  SendMessageW(rGeoTex, BM_SETCHECK, BST_CHECKED, 0); EnableWindow(rTargets, FALSE);
  cDist = mk(L"BUTTON", L"Show distance color", BS_AUTOCHECKBOX | WS_GROUP); SendMessageW(cDist, BM_SETCHECK, BST_CHECKED, 0);
  cAlign = mk(L"BUTTON", L"Auto-align new scans with those marked in Workspace", BS_AUTOCHECKBOX); SendMessageW(cAlign, BM_SETCHECK, BST_CHECKED, 0);   // review F2: on by default
  cBase = mk(L"BUTTON", L"Enable automatic base removal   (not in the SDK)", BS_AUTOCHECKBOX); EnableWindow(cBase, FALSE);
  cFusion = mk(L"BUTTON", L"Real-time fusion   (not in the SDK)", BS_AUTOCHECKBOX); EnableWindow(cFusion, FALSE);
  cRtp = mk(L"BUTTON", L"Enable real-time processing   (always on)", BS_AUTOCHECKBOX); SendMessageW(cRtp, BM_SETCHECK, BST_CHECKED, 0); EnableWindow(cRtp, FALSE);
  lSpeed = mk(L"STATIC", L"Scanning speed", SS_LEFT); lSpeedL = mk(L"STATIC", L"1 fps", SS_LEFT, fs); lSpeedR = mk(L"STATIC", L"8 fps", SS_RIGHT, fs); sSpeed = slider(1, 8, 8);
  bAdv = mk(L"BUTTON", L"Advanced  —", BS_PUSHBUTTON | BS_LEFT);
  cHide = mk(L"BUTTON", L"Hide panels during scanning (full-screen view)", BS_AUTOCHECKBOX); SendMessageW(cHide, BM_SETCHECK, BST_CHECKED, 0);
  lDof = mk(L"STATIC", L"Depth of field", SS_LEFT); lNear = mk(L"STATIC", L"Near (mm): 170", SS_LEFT, fs); sNear = slider(150, 330, 170); lTicks = mk(L"STATIC", L"170            223            276            330", SS_CENTER, fs); lFar = mk(L"STATIC", L"Far (mm): 330", SS_LEFT, fs); sFar = slider(170, 350, 330);   // review F4: within the Space Spider's range
  cMotion = mk(L"BUTTON", L"Motion tolerance   (not in the SDK)", BS_AUTOCHECKBOX); EnableWindow(cMotion, FALSE);
  lName = mk(L"STATIC", L"Scan name", SS_LEFT); eName = mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP);
  lSens = mk(L"STATIC", L"Sensitivity", SS_LEFT); lSensL = mk(L"STATIC", L"Normal", SS_LEFT, fs); lSensR = mk(L"STATIC", L"Extreme", SS_RIGHT, fs); sSens = slider(0, 100, 0);
  lBright = mk(L"STATIC", L"Texture brightness", SS_LEFT); lBrightL = mk(L"STATIC", L"Normal", SS_LEFT, fs); lBrightR = mk(L"STATIC", L"High", SS_RIGHT, fs); sBright = slider(0, 100, 0);
  cAuto = mk(L"BUTTON", L"Auto-adjust brightness", BS_AUTOCHECKBOX); SendMessageW(cAuto, BM_SETCHECK, BST_CHECKED, 0);
  lUse = mk(L"STATIC", L"Use", SS_LEFT); lUseDev = mk(L"STATIC", scannerLabel, SS_LEFT | SS_SUNKEN);
  cNoTex = mk(L"BUTTON", L"Don't record texture", BS_AUTOCHECKBOX); cNoFlash = mk(L"BUTTON", L"Disable flash bulb", BS_AUTOCHECKBOX);
  lFreq = mk(L"STATIC", L"Frequency of texture frames", SS_LEFT); rFreqAuto = mk(L"BUTTON", L"Automatic", BS_AUTORADIOBUTTON | WS_GROUP); rFreqN = mk(L"BUTTON", L"One per", BS_AUTORADIOBUTTON); eFreqN = mk(L"EDIT", L"1", WS_BORDER | ES_NUMBER | ES_CENTER); lFreqN = mk(L"STATIC", L"frames", SS_LEFT);
  SendMessageW(rFreqAuto, BM_SETCHECK, BST_CHECKED, 0);
  lDelay = mk(L"STATIC", L"Delay before recording:", SS_LEFT); eDelay = mk(L"EDIT", L"0", WS_BORDER | ES_NUMBER | ES_CENTER | WS_GROUP); lDelayS = mk(L"STATIC", L"seconds", SS_LEFT);
  cLimit = mk(L"BUTTON", L"Limit number of frames in scan to", BS_AUTOCHECKBOX); eLimit = mk(L"EDIT", L"2000", WS_BORDER | ES_NUMBER | ES_CENTER);
  readSettings(); layout();
}
static void setStatus(const wchar_t* s) { SetWindowTextW(lStatus, s); }
// Studio remembers the Scan-panel settings between sessions; ArtecRec keeps them in artecrec.ini next to the exe.
static void toggleAdvanced();
static std::wstring iniPath() { wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH); std::wstring s = p; return s.substr(0, s.find_last_of(L"\\/") + 1) + L"artecrec.ini"; }
static bool iniLoaded = false;   // a save fired by a control notification during start-up overwrote the file with defaults before it was read (2 Nov)
static void saveIni() {
  if (!iniLoaded) return; std::wstring f = iniPath(); auto wi = [&](const wchar_t* k, int v) { wchar_t b[32]; swprintf(b, 32, L"%d", v); WritePrivateProfileStringW(L"scan", k, b, f.c_str()); };
  wi(L"iniVersion", 2); wi(L"tracking", S.tracking); wi(L"distColor", S.distColor); wi(L"autoAlign", S.autoAlign); wi(L"hidePanel", S.hidePanel); wi(L"speed", S.speed); wi(L"near", (int)S.nearMm.load()); wi(L"far", (int)S.farMm.load());
  wi(L"sens", (int)(S.sens * 100 + 0.5f)); wi(L"texBright", (int)(S.texBright * 100 + 0.5f)); wi(L"autoBright", S.autoBright); wi(L"recordTexture", S.recordTexture); wi(L"flash", S.flash); wi(L"texFreq", S.texFreq); wi(L"texEvery", S.texEvery);
  wi(L"delay", S.delaySec); wi(L"limitFrames", S.limitFrames); wi(L"frameLimit", S.frameLimit); wi(L"audio", S.audio); wi(L"scanUsingAutoAlign", S.scanUsingAutoAlign); wi(L"fieldMode", S.fieldMode); wi(L"daylight", S.daylight); wi(L"shotBg", S.shotBg); wi(L"advOpen", advOpen);
  if (!g_projPath.empty()) WritePrivateProfileStringW(L"scan", L"lastProject", g_projPath.c_str(), f.c_str());   // DeepSeek 4: checked at the next start
  BOOL ok = WritePrivateProfileStringW(L"scan", L"scanName", S.scanName.c_str(), f.c_str());
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, f.c_str());   // flush Windows' ini cache now; otherwise a killed process loses the settings (found 2 Nov)
  if (!ok) logf("settings could not be saved to %ls", f.c_str());
}
static void loadIni() {
  std::wstring f = iniPath(); if (GetFileAttributesW(f.c_str()) == INVALID_FILE_ATTRIBUTES) { iniLoaded = true; return; } auto ri = [&](const wchar_t* k, int d) { return (int)GetPrivateProfileIntW(L"scan", k, d, f.c_str()); };
  auto chk = [](HWND h, bool v) { SendMessageW(h, BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0); }; auto pos = [](HWND h, int v) { SendMessageW(h, TBM_SETPOS, TRUE, v); };
  int tr = ri(L"tracking", 0); chk(rGeoTex, tr == 0); chk(rGeo, tr == 1); chk(cDist, ri(L"distColor", 1)); int iniVer = ri(L"iniVersion", 1); int aa = iniVer >= 2 ? ri(L"autoAlign", 1) : 1;   /* cloud review: builds before 5 Nov stored autoAlign=0 as their default; switch it on once */ /* review F2: joining new passes is on unless the user switched it off */ chk(cAlign, aa != 0); chk(cHide, ri(L"hidePanel", 1));
  pos(sSpeed, ri(L"speed", 8)); pos(sNear, ri(L"near", 170)); pos(sFar, ri(L"far", 330)); pos(sSens, ri(L"sens", 0)); pos(sBright, ri(L"texBright", 0)); chk(cAuto, ri(L"autoBright", 1));
  chk(cNoTex, !ri(L"recordTexture", 1)); chk(cNoFlash, !ri(L"flash", 1)); int tf = ri(L"texFreq", 0); chk(rFreqAuto, tf == 0); chk(rFreqN, tf == 1); wchar_t b[32];
  swprintf(b, 32, L"%d", ri(L"texEvery", 1)); SetWindowTextW(eFreqN, b); swprintf(b, 32, L"%d", ri(L"delay", 0)); SetWindowTextW(eDelay, b); chk(cLimit, ri(L"limitFrames", 0)); swprintf(b, 32, L"%d", ri(L"frameLimit", 2000)); SetWindowTextW(eLimit, b);
  wchar_t nm[128] = L""; GetPrivateProfileStringW(L"scan", L"scanName", L"", nm, 128, f.c_str()); SetWindowTextW(eName, nm);
  S.audio = ri(L"audio", 1); S.scanUsingAutoAlign = ri(L"scanUsingAutoAlign", 1); S.fieldMode = ri(L"fieldMode", 1) != 0; S.daylight = ri(L"daylight", 0) != 0; S.shotBg = ri(L"shotBg", 0); bool adv = ri(L"advOpen", 1); if (adv != advOpen) toggleAdvanced();
  readSettings(); iniLoaded = true; logf("settings loaded from %ls (autoAlign %d, tracking %d, speed %d)", f.c_str(), (int)S.autoAlign, S.tracking, S.speed);
}
static void toggleAdvanced() { advOpen = !advOpen; SetWindowTextW(bAdv, advOpen ? L"Advanced  —" : L"Advanced  +"); layout(); InvalidateRect(hwnd, nullptr, TRUE); }
}  // namespace panel
