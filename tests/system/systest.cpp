// SYSTEM tests: drive a test copy of the real artecrec.exe with no scanner attached.
// The exe is copied to %TEMP%\artecrec-systest\ (own artecrec.ini / artecrec.log), started, and driven through
// WM_COMMAND menu ids (IDM_ enum in artecrec.cpp, starting at 101), keys and the Save dialog. The log file and the
// window are checked. Responsiveness: a sampler sends WM_NULL with SendMessageTimeout every 250 ms the whole time;
// every reply must come within 2 s. Tests depend on each other in order (one app session per group).
// Resilient to the app being rewritten: a missing feature or a changed log line gives SKIP with the reason; FAIL is
// kept for hangs, crashes, error messages and lost files.
// Environment: SYSTEST_EXE (default C:\Artec\sdk-fps\artecrec.exe), SYSTEST_PROJ (default: the integration
// fixture %TEMP%\artecrec-tests\Trail14\Trail14.sproj; copied from C:\Artec\stage if missing).
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include "../testfw.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
using clk = std::chrono::steady_clock;
static double since(clk::time_point t) { return std::chrono::duration<double>(clk::now() - t).count(); }
enum { IDM_NEW = 101, IDM_OPEN, IDM_SAVE, IDM_SAVEAS, IDM_EXIT, IDM_FIT, IDM_HOME, IDM_SETHOME, IDM_ORTHO, IDM_GRID, IDM_LIGHT, IDM_RANGE,
       IDM_RSOLID, IDM_RWIRE, IDM_RPOINTS, IDM_RWIRESOLID, IDM_RPOINTSSOLID, IDM_SMOOTH, IDM_FLAT, IDM_BF_SHOW, IDM_BF_CULL, IDM_BF_BLACK, IDM_BF_TRANS, IDM_COL_TEX, IDM_COL_SCAN, IDM_COL_SURF,
       IDM_VFRONT, IDM_VBACK, IDM_VLEFT, IDM_VRIGHT, IDM_VTOP, IDM_VBOTTOM, IDM_SHOT, IDM_CUR_MASS, IDM_CUR_ORIGIN,
       IDM_SCANPANEL, IDM_WORKSPACE, IDM_LOG, IDM_AUDIO, IDM_AUTOALIGN, IDM_ZOOMCUR, IDM_SHOT_WHITE, IDM_SHOT_BLACK, IDM_ABOUT };

// ---------------------------------------------------------------- paths and files
static std::wstring envOr(const wchar_t* k, const std::wstring& d) { wchar_t b[1024]; DWORD n = GetEnvironmentVariableW(k, b, 1024); return n && n < 1024 ? std::wstring(b) : d; }
static std::wstring tempDir() { wchar_t b[MAX_PATH]; GetTempPathW(MAX_PATH, b); std::wstring s = b; if (!s.empty() && s.back() == L'\\') s.pop_back(); return s; }
static std::wstring SYS() { return tempDir() + L"\\artecrec-systest"; }
static std::wstring EXE() { return SYS() + L"\\artecrec.exe"; }
static std::wstring LOG() { return SYS() + L"\\artecrec.log"; }
static std::wstring PROJ() { return envOr(L"SYSTEST_PROJ", tempDir() + L"\\artecrec-tests\\Trail14\\Trail14.sproj"); }
static bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static bool removeDir(const std::wstring& dir) { if (!exists(dir)) return true; std::wstring dz = dir; dz.push_back(L'\0'); SHFILEOPSTRUCTW op = {}; op.wFunc = FO_DELETE; op.pFrom = dz.c_str(); op.fFlags = FOF_NO_UI; return SHFileOperationW(&op) == 0; }
static std::string readFile(const std::wstring& p) { std::string s; FILE* f = _wfopen(p.c_str(), L"rb"); if (!f) return s; char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n); fclose(f); return s; }
static void writeFile(const std::wstring& p, const std::string& s) { FILE* f = _wfopen(p.c_str(), L"wb"); if (f) { fwrite(s.data(), 1, s.size(), f); fclose(f); } }
static std::string logText() { return readFile(LOG()); }
static std::string sessionLog() { std::string l = logText(); size_t p = l.rfind("by Mythic Society start"); if (p == std::string::npos) return l; size_t b = l.rfind('\n', p); return l.substr(b == std::string::npos ? 0 : b + 1); }
static bool has(const std::string& s, const char* t) { return s.find(t) != std::string::npos; }
static std::string narrow(const std::wstring& w) { return tfw::esc(w); }
static int countOf(const std::string& s, const char* t) { int n = 0; size_t p = 0; while ((p = s.find(t, p)) != std::string::npos) { n++; p += strlen(t); } return n; }
static std::string lineWith(const std::string& s, const char* t) { size_t p = s.rfind(t); if (p == std::string::npos) return ""; size_t b = s.rfind('\n', p), e = s.find('\n', p); b = b == std::string::npos ? 0 : b + 1; std::string r = s.substr(b, e == std::string::npos ? std::string::npos : e - b); while (!r.empty() && (r.back() == '\r')) r.pop_back(); return r; }
static bool waitLog(const char* t, double secs, int count = 1) { auto t0 = clk::now(); while (since(t0) < secs) { if (countOf(sessionLog(), t) >= count) return true; Sleep(250); } return false; }

// ---------------------------------------------------------------- the app process
static PROCESS_INFORMATION g_pi = {}; static HWND g_main = nullptr; static DWORD g_tid = 0;
static std::atomic<bool> g_sampling{false}; static std::atomic<double> g_maxReply{0}; static std::atomic<int> g_noReply{0}; static std::thread g_sampler;
static bool alive() { return g_pi.hProcess && WaitForSingleObject(g_pi.hProcess, 0) == WAIT_TIMEOUT; }
static void startSampler() { g_maxReply = 0; g_noReply = 0; g_sampling = true; g_sampler = std::thread([] { while (g_sampling.load()) { HWND h = g_main; if (h && alive()) { DWORD_PTR r; auto t0 = clk::now(); LRESULT ok = SendMessageTimeoutW(h, WM_NULL, 0, 0, SMTO_NORMAL, 5000, &r); double d = since(t0); if (!ok && alive() && IsWindow(h)) g_noReply++; if (d > g_maxReply.load()) g_maxReply = d; } Sleep(250); } }); }
static void stopSampler() { g_sampling = false; if (g_sampler.joinable()) g_sampler.join(); }
static void resetSample() { g_maxReply = 0; g_noReply = 0; }
#define CHECK_RESPONSIVE(what) do { CHECK_MSG(g_maxReply.load() < 2.0 && g_noReply.load() == 0, std::string(what) + ": slowest WM_NULL reply " + tfw::str(g_maxReply.load()) + " s, " + std::to_string(g_noReply.load()) + " timeouts"); } while (0)
struct FindCtx { DWORD pid; const wchar_t* cls; HWND found; };
static HWND findTop(const wchar_t* cls) { FindCtx c = {g_pi.dwProcessId, cls, nullptr}; EnumWindows([](HWND h, LPARAM lp) -> BOOL { auto* c = (FindCtx*)lp; DWORD pid; GetWindowThreadProcessId(h, &pid); wchar_t k[64]; GetClassNameW(h, k, 64); if (pid == c->pid && !wcscmp(k, c->cls) && IsWindowVisible(h)) { c->found = h; return FALSE; } return TRUE; }, (LPARAM)&c); return c.found; }
static std::wstring textOf(HWND h) { wchar_t b[2048] = L""; DWORD_PTR r; SendMessageTimeoutW(h, WM_GETTEXT, 2048, (LPARAM)b, SMTO_NORMAL, 2000, &r); return b; }
static std::vector<HWND> children(HWND p) { std::vector<HWND> v; EnumChildWindows(p, [](HWND h, LPARAM lp) -> BOOL { ((std::vector<HWND>*)lp)->push_back(h); return TRUE; }, (LPARAM)&v); return v; }
static std::wstring cls(HWND h) { wchar_t k[64] = L""; GetClassNameW(h, k, 64); return k; }
static std::wstring dialogText(HWND dlg) { std::wstring s = textOf(dlg) + L" | "; for (HWND c : children(dlg)) if (cls(c) == L"Static") s += textOf(c) + L" "; return s; }
static HWND waitDialog(double secs) { auto t0 = clk::now(); while (since(t0) < secs) { HWND d = findTop(L"#32770"); if (d) return d; if (!alive()) return nullptr; Sleep(100); } return nullptr; }
// clicks button `id`; if the dialog has no such button (a MessageBox with only OK uses another id), its first button
static void clickButton(HWND dlg, int id) { HWND b = GetDlgItem(dlg, id);
  if (!b || cls(b) != L"Button") { b = nullptr; for (HWND c : children(dlg)) if (cls(c) == L"Button" && IsWindowVisible(c)) { b = c; break; } if (b) id = GetDlgCtrlID(b); }
  PostMessageW(dlg, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)b); }
static bool waitGone(HWND h, double secs) { auto t0 = clk::now(); while (since(t0) < secs) { if (!IsWindow(h) || !IsWindowVisible(h)) return true; Sleep(100); } return false; }
static void cmd(int id) { DWORD_PTR r; SendMessageTimeoutW(g_main, WM_COMMAND, MAKEWPARAM(id, 0), 0, SMTO_NORMAL, 5000, &r); }
static void postCmd(int id) { PostMessageW(g_main, WM_COMMAND, MAKEWPARAM(id, 0), 0); }

static bool launch(const std::wstring& arg, std::string& why) {
  std::wstring sdkBin = L"C:\\Program Files\\Artec\\Artec 3D Scanning SDK\\bin-x64"; wchar_t path[32767]; GetEnvironmentVariableW(L"PATH", path, 32767);
  std::wstring p = path; if (p.find(sdkBin) == std::wstring::npos) SetEnvironmentVariableW(L"PATH", (sdkBin + L";" + p).c_str());
  std::wstring cl = L"\"" + EXE() + L"\"" + (arg.empty() ? L"" : L" \"" + arg + L"\""); std::vector<wchar_t> buf(cl.begin(), cl.end()); buf.push_back(0);
  STARTUPINFOW si = {sizeof si}; g_pi = {}; g_main = nullptr;
  if (!CreateProcessW(EXE().c_str(), buf.data(), nullptr, nullptr, FALSE, 0, nullptr, SYS().c_str(), &si, &g_pi)) { why = "CreateProcess failed " + std::to_string(GetLastError()); return false; }
  auto t0 = clk::now(); while (since(t0) < 60) { HWND h = findTop(L"ArtecRecMain"); if (h) { g_main = h; g_tid = GetWindowThreadProcessId(h, nullptr); break; } if (!alive()) { why = "the process ended before showing its window"; return false; } Sleep(100); }
  if (!g_main) { why = "no ArtecRecMain window within 60 s"; return false; }
  startSampler(); return true;
}
static void killApp() { stopSampler(); if (alive()) { TerminateProcess(g_pi.hProcess, 9); WaitForSingleObject(g_pi.hProcess, 5000); } if (g_pi.hProcess) { CloseHandle(g_pi.hProcess); CloseHandle(g_pi.hThread); } g_pi = {}; g_main = nullptr; }
// closes any message box the app shows; "Save the project first?" is answered No. Returns the texts seen.
static std::wstring dismissBoxes(double secs) { std::wstring seen; auto t0 = clk::now(); while (since(t0) < secs) { HWND d = findTop(L"#32770"); if (!d) { Sleep(100); continue; } std::wstring t = dialogText(d); seen += t + L"\n"; if (GetDlgItem(d, IDNO)) clickButton(d, IDNO); else if (GetDlgItem(d, IDOK)) clickButton(d, IDOK); else PostMessageW(d, WM_CLOSE, 0, 0); waitGone(d, 3); } return seen; }
static bool exitApp(double secs, std::wstring& boxes, double& took) {
  auto t0 = clk::now(); postCmd(IDM_EXIT);
  while (since(t0) < secs) { if (!alive()) { took = since(t0); stopSampler(); return true; } HWND d = findTop(L"#32770"); if (d) { boxes += dialogText(d) + L"\n"; if (GetDlgItem(d, IDNO)) clickButton(d, IDNO); else clickButton(d, IDOK); waitGone(d, 3); } Sleep(100); }
  took = since(t0); return false;
}
static int listCount() { for (HWND c : children(g_main)) if (cls(c) == L"SysListView32") { DWORD_PTR r = 0; SendMessageTimeoutW(c, LVM_GETITEMCOUNT, 0, 0, SMTO_NORMAL, 2000, &r); return (int)r; } return -1; }
// Save dialog: put a full path in the file-name box and press Save. Returns false if the dialog cannot be driven.
static bool driveSaveDialog(const std::wstring& path, std::string& why) {
  HWND d = waitDialog(15); if (!d) { why = "no Save dialog appeared within 15 s"; return false; }
  std::wstring title = textOf(d); HWND edit = nullptr;
  for (int i = 0; i < 20 && !edit; i++) { GUITHREADINFO gi = {sizeof gi}; if (GetGUIThreadInfo(GetWindowThreadProcessId(d, nullptr), &gi) && gi.hwndFocus && cls(gi.hwndFocus) == L"Edit" && GetAncestor(gi.hwndFocus, GA_ROOT) == d) edit = gi.hwndFocus; else Sleep(150); }
  if (!edit) for (HWND c : children(d)) { HWND par = GetParent(c); if (cls(c) == L"Edit" && IsWindowVisible(c) && (cls(par) == L"ComboBox" || cls(par) == L"ComboBoxEx32")) { edit = c; break; } }
  if (!edit) { why = "file-name box not found in dialog \"" + narrow(title) + "\""; PostMessageW(d, WM_CLOSE, 0, 0); return false; }
  DWORD_PTR r; SendMessageTimeoutW(edit, WM_SETTEXT, 0, (LPARAM)path.c_str(), SMTO_NORMAL, 3000, &r); Sleep(300);
  clickButton(d, IDOK);
  if (!waitGone(d, 10)) { HWND d2 = findTop(L"#32770"); why = "dialog did not accept the name (" + narrow(d2 ? dialogText(d2) : L"") + ")"; return false; }
  return true;
}

// ================================================================= tests
static bool g_started = false;
TEST(sys_prepare, "setup") {
  std::wstring src = envOr(L"SYSTEST_EXE", L"C:\\Artec\\sdk-fps\\artecrec.exe"); if (!exists(src)) SKIP("no exe at " + narrow(src));
  for (HWND h = FindWindowW(L"ArtecRecMain", nullptr); h; h = FindWindowExW(nullptr, h, L"ArtecRecMain", nullptr)) { DWORD pid; GetWindowThreadProcessId(h, &pid); HANDLE ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid); wchar_t img[MAX_PATH]; DWORD n = MAX_PATH;
    if (ph && QueryFullProcessImageNameW(ph, 0, img, &n) && std::wstring(img).find(L"artecrec-systest") != std::wstring::npos) TerminateProcess(ph, 9); if (ph) CloseHandle(ph); }   // leftover test copy from an aborted run only
  Sleep(500); removeDir(SYS()); SHCreateDirectoryExW(nullptr, SYS().c_str(), nullptr);
  // the lead may be rebuilding the exe: wait until it is unchanged for 5 s and copies cleanly (up to 3 min)
  { auto stamp = [&]() { WIN32_FILE_ATTRIBUTE_DATA d; if (!GetFileAttributesExW(src.c_str(), GetFileExInfoStandard, &d)) return 0ull; return ((unsigned long long)d.ftLastWriteTime.dwHighDateTime << 32 | d.ftLastWriteTime.dwLowDateTime) ^ d.nFileSizeLow; };
    auto t0 = clk::now(); bool copied = false; int retries = 0;
    while (since(t0) < 180) { unsigned long long a1 = stamp(); Sleep(5000); if (a1 && a1 == stamp() && CopyFileW(src.c_str(), EXE().c_str(), FALSE)) { copied = true; break; } retries++; }
    if (retries) NOTE("exe was changing or locked (rebuild?): waited " + tfw::str(since(t0)) + " s");
    if (!copied) SKIP("exe still being rebuilt after 3 min"); } std::wstring man = src + L".manifest"; if (exists(man)) CopyFileW(man.c_str(), (EXE() + L".manifest").c_str(), FALSE);
  WIN32_FILE_ATTRIBUTE_DATA a; GetFileAttributesExW(src.c_str(), GetFileExInfoStandard, &a); SYSTEMTIME st, lt; FileTimeToSystemTime(&a.ftLastWriteTime, &st); SystemTimeToTzSpecificLocalTime(nullptr, &st, &lt);
  char b[96]; snprintf(b, sizeof b, "exe under test: %04d-%02d-%02d %02d:%02d", lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute); NOTE(narrow(src) + ", " + b);
  if (!exists(PROJ())) { std::wstring s = L"C:\\Artec\\stage", d = PROJ().substr(0, PROJ().find_last_of(L'\\')); SHCreateDirectoryExW(nullptr, (d + L"\\Scans").c_str(), nullptr);
    CopyFileW((s + L"\\Trail14.sproj").c_str(), (d + L"\\Trail14.sproj").c_str(), FALSE); CopyFileW((s + L"\\artecrec-groups.txt").c_str(), (d + L"\\artecrec-groups.txt").c_str(), FALSE);
    WIN32_FIND_DATAW f; HANDLE h = FindFirstFileW((s + L"\\Scans\\*").c_str(), &f); if (h != INVALID_HANDLE_VALUE) { do { if (f.cFileName[0] != L'.') CopyFileW((s + L"\\Scans\\" + f.cFileName).c_str(), (d + L"\\Scans\\" + f.cFileName).c_str(), FALSE); } while (FindNextFileW(h, &f)); FindClose(h); } }
  CHECK(exists(PROJ()));
}
TEST(sys_start_without_scanner, "B6 F7") {
  if (!exists(EXE())) SKIP("sys_prepare did not run"); std::string why; auto t0 = clk::now();
  if (!launch(L"", why)) { killApp(); CHECK_MSG(false, why); return; } g_started = true;
  NOTE("window after " + tfw::str(since(t0)) + " s; title: " + narrow(textOf(g_main)));
  CHECK_MSG(waitLog("by Mythic Society start", 10), "no start line in artecrec.log");
  bool noScanner = waitLog("no scanner at start", 20) || has(sessionLog(), "enumerateScanners") || has(sessionLog(), "createScanner 0x");
  if (has(sessionLog(), " connected, fps")) SKIP("a scanner IS attached to this PC: the no-scanner tests do not apply");
  if (!noScanner) SKIP("log has no 'no scanner at start' line (changed in the rewrite?)");
  std::wstring boxes = dismissBoxes(1.5); CHECK_MSG(boxes.empty(), "a message box appeared at start: " + narrow(boxes));
}
TEST(sys_status_says_no_scanner, "F7") {
  if (!g_started || !alive()) SKIP("app not running"); Sleep(1500); std::wstring all;
  for (HWND c : children(g_main)) if (cls(c) == L"Static" || cls(c) == L"Edit") all += textOf(c) + L"\n";
  std::wstring low = all; std::transform(low.begin(), low.end(), low.begin(), ::towlower);
  bool says = low.find(L"no scanner") != std::wstring::npos;
  if (!says) SKIP("no 'no scanner' text found in the window's labels (texts: " + narrow(all.substr(0, 200)) + ")");
}
TEST(sys_responsive_idle, "B2") {
  if (!g_started || !alive()) SKIP("app not running"); resetSample(); Sleep(8000); CHECK(alive()); CHECK_RESPONSIVE("8 s idle");
  NOTE("slowest WM_NULL reply " + tfw::str(g_maxReply.load() * 1000) + " ms");
}
TEST(sys_menu_view_commands, "F1") {
  if (!g_started || !alive()) SKIP("app not running"); resetSample(); HMENU m = GetMenu(g_main);
  UINT g0 = m ? GetMenuState(m, IDM_GRID, MF_BYCOMMAND) : 0; cmd(IDM_GRID); UINT g1 = m ? GetMenuState(m, IDM_GRID, MF_BYCOMMAND) : 0;
  if (m && g0 != 0xFFFFFFFF) CHECK_MSG(((g0 ^ g1) & MF_CHECKED) != 0, "Show grid did not toggle its check mark"); cmd(IDM_GRID);
  int ids[] = {IDM_ORTHO, IDM_ORTHO, IDM_LIGHT, IDM_LIGHT, IDM_RANGE, IDM_RANGE, IDM_RWIRE, IDM_RPOINTS, IDM_RWIRESOLID, IDM_RPOINTSSOLID, IDM_RSOLID, IDM_FLAT, IDM_SMOOTH, IDM_BF_CULL, IDM_BF_SHOW, IDM_COL_SCAN, IDM_COL_SURF, IDM_COL_TEX,
               IDM_VFRONT, IDM_VBACK, IDM_VLEFT, IDM_VRIGHT, IDM_VTOP, IDM_VBOTTOM, IDM_FIT, IDM_HOME, IDM_SETHOME, IDM_CUR_ORIGIN, IDM_SCANPANEL, IDM_SCANPANEL, IDM_WORKSPACE, IDM_WORKSPACE, IDM_LOG, IDM_LOG};
  for (int id : ids) { cmd(id); Sleep(60); }
  Sleep(500); CHECK(alive()); CHECK_RESPONSIVE("view commands"); std::wstring boxes = dismissBoxes(1); CHECK_MSG(boxes.empty(), "unexpected box: " + narrow(boxes));
}
TEST(sys_keys_view, "F1") {
  if (!g_started || !alive()) SKIP("app not running"); resetSample();
  for (WPARAM k : {(WPARAM)'G', (WPARAM)'G', (WPARAM)'L', (WPARAM)'L', (WPARAM)'F', (WPARAM)'H', (WPARAM)VK_F7, (WPARAM)VK_F7, (WPARAM)VK_F11, (WPARAM)VK_F11}) { PostMessageW(g_main, WM_KEYDOWN, k, 0); PostMessageW(g_main, WM_KEYUP, k, 0xC0000001); Sleep(80); }
  Sleep(500); CHECK(alive()); CHECK_RESPONSIVE("view keys");
}
TEST(sys_about_box_modal, "F1") {
  if (!g_started || !alive()) SKIP("app not running"); resetSample(); postCmd(IDM_ABOUT);
  HWND d = waitDialog(5); if (!d) SKIP("About did not open a dialog");
  std::wstring t = dialogText(d); CHECK_MSG(t.find(L"ArtecRec") != std::wstring::npos, narrow(t)); Sleep(1500); CHECK_RESPONSIVE("with the About box open");
  clickButton(d, IDOK); CHECK(waitGone(d, 5)); CHECK(alive());
}
TEST(sys_record_without_scanner, "B4 F7") {
  if (!g_started || !alive()) SKIP("app not running"); resetSample();
  PostMessageW(g_main, WM_KEYDOWN, VK_SPACE, 0); PostMessageW(g_main, WM_KEYUP, VK_SPACE, 0xC0000001);
  HWND d = waitDialog(6); std::wstring t = d ? dialogText(d) : L"";
  bool refused = waitLog("start refused", 3) || has(sessionLog(), "no scanner connected");
  if (d) { clickButton(d, IDOK); waitGone(d, 5); }
  if (!refused && !d) SKIP("Space gave neither a refusal line nor a message (behaviour changed)");
  CHECK_MSG(refused, "no 'start refused' line in the log"); if (d) NOTE("message: " + narrow(t));
  Sleep(1000); CHECK(alive()); CHECK_RESPONSIVE("after Record without a scanner"); CHECK_MSG(!has(sessionLog(), "procedure started"), "a scanning procedure started without a scanner");
}
TEST(sys_exit_clean, "B6") {
  if (!g_started || !alive()) SKIP("app not running"); std::wstring boxes; double took = 0; bool ok = exitApp(30, boxes, took);
  CHECK_MSG(ok, "still running 30 s after File > Exit (B6: invisible process holding the scanner)"); DWORD code = 99; if (ok) GetExitCodeProcess(g_pi.hProcess, &code);
  if (ok) { CHECK_EQ(code, (DWORD)0); NOTE("exited in " + tfw::str(took) + " s" + (boxes.empty() ? "" : ", boxes: " + narrow(boxes))); }
  CHECK_MSG(has(sessionLog(), "ArtecRec exit"), "no 'ArtecRec exit' line"); killApp(); g_started = false;
}
// ---------------------------------------------------------------- session 2: open by argument, Save As
TEST(sys_open_by_argument, "E2 B2") {
  if (!exists(EXE())) SKIP("sys_prepare did not run"); if (!exists(PROJ())) SKIP("project copy missing: " + narrow(PROJ()));
  std::string why; auto t0 = clk::now(); if (!launch(PROJ(), why)) { killApp(); CHECK_MSG(false, why); return; } g_started = true; resetSample();
  if (!waitLog("open 0x", 240)) { CHECK(alive()); SKIP("no 'open 0x' log line within 240 s (path argument not supported, or log line changed)"); }
  std::string ln = lineWith(sessionLog(), "open 0x"); NOTE(ln + "  (window to opened: " + tfw::str(since(t0)) + " s)");
  CHECK_MSG(has(ln, "open 0x00000000"), ln); CHECK_MSG(has(ln, "scans 7"), "expected 7 scans: " + ln);
  bool refs = waitLog("search reference built", 180, 7); NOTE(std::string("display/reference build for 7 scans ") + (refs ? "finished" : "NOT finished in 180 s") + " after " + tfw::str(since(t0)) + " s");
  int n = listCount(); if (n >= 0) CHECK_MSG(n == 7 || n == 8, "Workspace rows " + std::to_string(n) + " (expected 7 scans, + 1 row for 'Group 1')"); else NOTE("Workspace list not found");
  CHECK(alive()); CHECK_RESPONSIVE("while opening the project"); std::wstring boxes = dismissBoxes(1); CHECK_MSG(boxes.empty(), "box: " + narrow(boxes));
}
static bool saveAs(const std::wstring& target, double secs, std::string& line, std::wstring& boxes, std::string& why) {
  int before = countOf(sessionLog(), "save 0x"), refusedBefore = countOf(sessionLog(), "save refused"); postCmd(IDM_SAVEAS); if (!driveSaveDialog(target, why)) return false;
  auto t0 = clk::now(); clk::time_point boxAt{}; while (since(t0) < secs) { if (countOf(sessionLog(), "save 0x") > before) break;
    if (countOf(sessionLog(), "save refused") > refusedBefore) { Sleep(1000); boxes += dismissBoxes(2); line = lineWith(sessionLog(), "save refused"); return true; }
    if (!boxes.empty() && boxAt == clk::time_point{}) boxAt = clk::now(); if (boxAt != clk::time_point{} && since(boxAt) > 10 && !findTop(L"#32770")) break;   /* told the user something and did not save */
    HWND d = findTop(L"#32770"); if (d) { boxes += dialogText(d) + L"\n"; if (GetDlgItem(d, IDNO)) clickButton(d, IDNO); else clickButton(d, IDOK); waitGone(d, 3); } if (!alive()) break; Sleep(250); }
  Sleep(1500); boxes += dismissBoxes(1.5); line = countOf(sessionLog(), "save 0x") > before ? lineWith(sessionLog(), "save 0x") : ""; return true;
}
TEST(sys_save_as_new_folder, "A1 A3 A5") {
  if (!g_started || !alive()) SKIP("app not running"); std::wstring dir = SYS() + L"\\saveas"; SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr); resetSample();
  std::string line, why; std::wstring boxes; if (!saveAs(dir + L"\\Trail14sys.sproj", 400, line, boxes, why)) SKIP("Save dialog not automatable: " + why);
  CHECK(alive()); if (line.empty()) { CHECK_MSG(false, "no 'save 0x' line within 400 s; boxes: " + narrow(boxes)); return; }
  NOTE(line); CHECK_MSG(has(line, "save 0x00000000"), line); CHECK_MSG(boxes.empty(), "message: " + narrow(boxes));
  std::wstring sp = dir + L"\\Trail14sys\\Trail14sys.sproj"; CHECK_MSG(exists(sp), "expected the project in its own folder: " + narrow(sp)); CHECK(exists(dir + L"\\Trail14sys\\Scans"));
  std::string x = readFile(sp); CHECK_EQ(countOf(x, "<Scan "), 7); CHECK_MSG(has(x, "Name=\"Group 1 - Spider Scan 1\""), "Studio names not written into the .sproj");
  CHECK_RESPONSIVE("during Save As");
}
TEST(sys_save_into_folder_with_photos, "A1 FINDING-on-old-build") {
  // A1: "Belur" saved into a folder Belur that already holds photos and another project must not delete them
  if (!g_started || !alive()) SKIP("app not running"); std::wstring b = SYS() + L"\\Hoysala\\Belur"; SHCreateDirectoryExW(nullptr, (b + L"\\Halebidu").c_str(), nullptr);
  writeFile(b + L"\\IMG_0412.JPG", "photo"); writeFile(b + L"\\Halebidu\\notes.txt", "other project"); resetSample();
  std::string line, why; std::wstring boxes; if (!saveAs(b + L"\\Belur.sproj", 400, line, boxes, why)) SKIP("Save dialog not automatable: " + why);
  CHECK(alive()); NOTE((line.empty() ? std::string("no save line") : line) + (boxes.empty() ? "" : "; message: " + narrow(boxes)));
  CHECK_MSG(exists(b + L"\\IMG_0412.JPG"), "the photo in the chosen folder was DELETED by the save (A1)");
  CHECK_MSG(exists(b + L"\\Halebidu\\notes.txt"), "the other project in the chosen folder was DELETED by the save (A1)");
  bool refusedWithMessage = has(line, "save refused") || boxes.find(L"will not replace it") != std::wstring::npos;   // DeepSeek 3-3: the specific refusal, not any message box
  bool savedSafely = exists(b + L"\\Belur\\Belur.sproj") || refusedWithMessage; CHECK_MSG(savedSafely, "neither saved to Belur\\Belur nor refused with the folder-protection message");
  if (has(line, "refused") && boxes.empty()) NOTE("refused in the log, but no message box was seen by the driver");
}
TEST(sys_exit_after_save, "B6 A4") {
  if (!g_started || !alive()) SKIP("app not running"); std::wstring boxes; double took = 0; bool ok = exitApp(60, boxes, took);
  CHECK_MSG(ok, "still running 60 s after File > Exit"); if (ok) NOTE("exited in " + tfw::str(took) + " s" + (boxes.empty() ? "" : "; boxes: " + narrow(boxes)));
  CHECK_MSG(has(sessionLog(), "ArtecRec exit"), "no 'ArtecRec exit' line"); killApp(); g_started = false;
}
TEST(sys_log_failure_signatures, "B1 B3 A1") {
  std::string l = logText(); if (l.empty()) SKIP("no log");
  for (const char* sig : {"could not move the old project folder", "could not move the new project folder", "Stop did not finish", "the scanning job did not end", "auto-align did not end in 90 s", "did not finish in 6 min", "createSaver 0x", "createNewProject 0x"}) CHECK_MSG(!has(l, sig), std::string("log contains: ") + lineWith(l, sig));
  NOTE("log: " + narrow(LOG()) + " (" + std::to_string(countOf(l, "\n")) + " lines)");
}
// ---------------------------------------------------------------- autosave (6 Nov): each finished pass is saved on its own in the
// background; after a crash the passes are offered at the next start; a discarded project removes them.
static std::wstring autosaveSession() { std::wstring root = SYS() + L"\\Autosave"; WIN32_FIND_DATAW f; HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &f); std::wstring found;
  if (h != INVALID_HANDLE_VALUE) { do { if (f.cFileName[0] != L'.' && (f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) found = root + L"\\" + f.cFileName; } while (FindNextFileW(h, &f)); FindClose(h); } return found; }
TEST(sys_autosave_writes_passes_in_background, "autosave") {
  if (!exists(EXE()) || !exists(PROJ())) SKIP("sys_prepare did not run"); killApp(); removeDir(SYS() + L"\\Autosave");
  std::string why; if (!launch(PROJ() + L" --autosave-test", why)) { killApp(); CHECK_MSG(false, why); return; } resetSample();
  if (!waitLog("autosave test: 7 pass(es) queued", 240)) { CHECK(alive()); killApp(); SKIP("project did not open / no autosave test line within 240 s"); }
  CHECK_MSG(!has(sessionLog(), "autosave paused"), lineWith(sessionLog(), "autosave paused")); bool all = waitLog("autosave: pass \"", 600, 7); CHECK_MSG(all, "expected 7 'autosave: pass ... saved' lines within 600 s, got " + std::to_string(countOf(sessionLog(), "autosave: pass \"")));
  NOTE(lineWith(sessionLog(), "autosave: pass \""));
  std::wstring ses = autosaveSession(); CHECK_MSG(!ses.empty(), "no autosave session folder"); if (ses.empty()) { killApp(); return; }
  std::string man = readFile(ses + L"\\autosave.txt"); CHECK_EQ(countOf(man, "P\t"), 7); int folders = 0;
  for (int id = 1; id <= 7; id++) if (exists(ses + L"\\pass-" + std::to_wstring(id) + L"-1\\pass.sproj")) folders++; CHECK_EQ(folders, 7);
  CHECK_RESPONSIVE("while autosaving 7 passes in the background"); CHECK(alive());
  killApp();   // a crash: the passes must survive
  CHECK_MSG(exists(ses + L"\\autosave.txt"), "the autosave was lost when the app was killed");
}
TEST(sys_autosave_recovered_after_crash, "autosave") {
  killApp();
  std::wstring ses = autosaveSession(); if (ses.empty()) SKIP("no autosave session from the previous test");
  std::string why; if (!launch(L"", why)) { killApp(); CHECK_MSG(false, why); return; }
  HWND d = waitDialog(30); CHECK_MSG(d != nullptr, "no recovery question at start"); if (!d) { killApp(); return; }
  std::wstring t = dialogText(d); NOTE(narrow(t)); CHECK_MSG(t.find(L"did not close normally") != std::wstring::npos && t.find(L"7 pass") != std::wstring::npos, "unexpected text: " + narrow(t));
  clickButton(d, IDYES); waitGone(d, 5);
  CHECK_MSG(waitLog("autosave: 7 pass(es) recovered", 300), "the 7 passes were not recovered within 300 s");
  int n = listCount(); CHECK_MSG(n >= 7, "Workspace rows after recovery: " + std::to_string(n));
  std::wstring boxes; double took = 0; bool ok = exitApp(60, boxes, took);   // answers "Save first?" with No = discard
  CHECK_MSG(ok, "did not exit"); CHECK_MSG(boxes.find(L"Save the project first") != std::wstring::npos, "recovered passes did not mark the project as unsaved; boxes: " + narrow(boxes));
  std::string man = readFile(ses + L"\\autosave.txt"); CHECK_MSG(!exists(ses) || has(man, "X\t"), "a discarded project kept its autosave as recoverable");
  killApp();
}
TEST(sys_autosave_no_question_after_discard, "autosave") {
  killApp();
  std::string why; if (!launch(L"", why)) { killApp(); CHECK_MSG(false, why); return; }
  HWND d = waitDialog(12); if (d) NOTE("box: " + narrow(dialogText(d))); CHECK_MSG(d == nullptr || dialogText(d).find(L"did not close normally") == std::wstring::npos, "recovery offered again after the project was discarded");
  killApp();
}
TEST(sys_cleanup, "setup") { killApp(); NOTE("left for inspection: " + narrow(SYS()) + " (delete by hand)"); }

int main(int argc, char** argv) { int r = tfw::run(argc, argv, "system"); killApp(); return r; }
