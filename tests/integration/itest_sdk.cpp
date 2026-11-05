// INTEGRATION tests: Artec 3D Scanning SDK 2.0, no scanner. They work on a %TEMP% copy of the real ArtecRec project
// C:\Artec\stage\Trail14.sproj (never on the original). The SDK calls mirror rec_sdk.h (openProject, saveProject,
// runAutoAlign, postProcessScan / execLimited) as of 5 Nov 2026; rec_sdk.h itself is not included because it carries the
// whole app state. Run one test per process (run-tests.bat does): a hung SDK job is then killed with its process.
//   itest_sdk.exe <test name substring>...   (no argument: all tests except the baseline_* timings)
// Environment: ITEST_WORK (default %TEMP%\artecrec-tests), ITEST_SRC (default C:\Artec\stage).
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include "../testfw.h"
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
#include <artec/sdk/base/Uuid.h>
#include <artec/sdk/algorithms/Algorithms.h>
#include <artec/sdk/algorithms/IAlgorithm.h>
#include <artec/sdk/project/IProject.h>
#include <artec/sdk/project/ProjectSettings.h>
#include <artec/sdk/project/ProjectSaverSettings.h>
#include <artec/sdk/project/ProjectLoaderSettings.h>
#include <artec/sdk/project/EntryInfo.h>
#include <artec/sdk/project/EntryType.h>
#include <thread>
#include <atomic>
#include <algorithm>
using namespace artec::sdk::base; namespace alg = artec::sdk::algorithms; namespace prj = artec::sdk::project;
using clk = std::chrono::steady_clock;
static double since(clk::time_point t) { return std::chrono::duration<double>(clk::now() - t).count(); }

// ---------------------------------------------------------------- fixture
static std::wstring envOr(const wchar_t* k, const std::wstring& d) { wchar_t b[1024]; DWORD n = GetEnvironmentVariableW(k, b, 1024); return n && n < 1024 ? std::wstring(b) : d; }
static std::wstring tempDir() { wchar_t b[MAX_PATH]; GetTempPathW(MAX_PATH, b); std::wstring s = b; if (!s.empty() && s.back() == L'\\') s.pop_back(); return s; }
static std::wstring WORK() { return envOr(L"ITEST_WORK", tempDir() + L"\\artecrec-tests"); }
static std::wstring SRC() { return envOr(L"ITEST_SRC", L"C:\\Artec\\stage"); }
static std::wstring FIX() { return WORK() + L"\\Trail14"; }
static std::wstring FIXPROJ() { return FIX() + L"\\Trail14.sproj"; }
static bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static long long fileSize(const std::wstring& p) { WIN32_FILE_ATTRIBUTE_DATA a; if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return -1; return ((long long)a.nFileSizeHigh << 32) | a.nFileSizeLow; }
static std::vector<logic::FolderEntry> listDir(const std::wstring& d) { std::vector<logic::FolderEntry> v; WIN32_FIND_DATAW f; HANDLE h = FindFirstFileW((d + L"\\*").c_str(), &f); if (h == INVALID_HANDLE_VALUE) return v;
  do { if (!wcscmp(f.cFileName, L".") || !wcscmp(f.cFileName, L"..")) continue; v.push_back({f.cFileName, (f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0}); } while (FindNextFileW(h, &f)); FindClose(h); return v; }
static bool removeDir(const std::wstring& dir) { if (!exists(dir)) return true; std::wstring dz = dir; dz.push_back(L'\0'); SHFILEOPSTRUCTW op = {}; op.wFunc = FO_DELETE; op.pFrom = dz.c_str(); op.fFlags = FOF_NO_UI; return SHFileOperationW(&op) == 0 && !exists(dir); }
static std::string narrow(const std::wstring& w) { return tfw::esc(w); }
// copies the project (sproj + groups + Scans\*) unless a complete copy is already there; the source is only read
static bool ensureFixture(std::string& why) {
  std::wstring s = SRC(), d = FIX(); if (!exists(s + L"\\Trail14.sproj")) { why = "source project missing: " + narrow(s) + "\\Trail14.sproj"; return false; }
  auto src = listDir(s + L"\\Scans"); bool complete = exists(d + L"\\Trail14.sproj");
  for (auto& e : src) if (fileSize(d + L"\\Scans\\" + e.name) != fileSize(s + L"\\Scans\\" + e.name)) complete = false;
  if (complete) return true;
  removeDir(d); SHCreateDirectoryExW(nullptr, (d + L"\\Scans").c_str(), nullptr); auto t0 = clk::now();
  for (const wchar_t* f : {L"Trail14.sproj", L"artecrec-groups.txt"}) if (exists(s + L"\\" + f) && !CopyFileW((s + L"\\" + f).c_str(), (d + L"\\" + f).c_str(), FALSE)) { why = "copy failed"; return false; }
  for (auto& e : src) if (!CopyFileW((s + L"\\Scans\\" + e.name).c_str(), (d + L"\\Scans\\" + e.name).c_str(), FALSE)) { why = "copy failed: " + narrow(e.name); return false; }
  printf("        (fixture copied to %s in %.1f s)\n", narrow(d).c_str(), since(t0)); return true;
}
#define NEED_FIXTURE() do { std::string _w; if (!ensureFixture(_w)) SKIP(_w); } while (0)

// ---------------------------------------------------------------- SDK helpers, mirroring rec_sdk.h
struct Loaded { IModel* m = nullptr; std::vector<std::wstring> entryNames; int scanEntries = 0, allEntries = 0; ErrorCode ec = ErrorCode_OK; double secs = 0; ~Loaded() { if (m) m->release(); } };
static void openProj(const std::wstring& sproj, Loaded& L) {   // = rec_sdk.h openProject (scan entries only)
  prj::IProject* p = nullptr; L.ec = prj::openProject(&p, sproj.c_str()); if (L.ec != ErrorCode_OK || !p) return;
  int n = p->getEntryCount(); L.allEntries = n; std::vector<Uuid> ids;
  for (int i = 0; i < n; i++) { prj::EntryInfo e; IString* nm = nullptr; if (p->getEntry(i, &e, &nm) == ErrorCode_OK && e.type == prj::EntryType_Scan) { ids.push_back(e.uuid); L.entryNames.push_back(nm && nm->getPointer() ? nm->getPointer() : L""); } if (nm) nm->release(); }
  L.scanEntries = (int)ids.size(); IArrayUuid* use = nullptr; createArrayUuid(&use, (int)ids.size()); for (int i = 0; i < (int)ids.size(); i++) use->setElement(i, ids[i]);
  prj::ProjectLoaderSettings ls; ls.entryList = use; IJob* loader = nullptr; L.ec = p->createLoader(&loader, &ls); createModel(&L.m);
  if (L.ec == ErrorCode_OK && loader) { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.out = L.m; ws.threadsCount = std::thread::hardware_concurrency(); auto t0 = clk::now(); L.ec = executeJob(loader, &ws); L.secs = since(t0); loader->release(); }
  use->release(); p->release();
}
static ErrorCode saveProj(IModel* m, const std::wstring& sproj, double& secs) {   // = rec_sdk.h saveProject without the folder swap
  std::wstring dir = sproj.substr(0, sproj.find_last_of(L"\\/")); SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
  prj::ProjectSettings ps; ps.path = sproj.c_str(); prj::IProject* p = nullptr; ErrorCode ec = prj::createNewProject(&p, &ps); if (ec != ErrorCode_OK || !p) return ec;
  prj::ProjectSaverSettings ss; ss.path = sproj.c_str(); generateUuid(&ss.projectId); ss.compressionLevel = 0; IJob* saver = nullptr; ec = p->createSaver(&saver, &ss);
  if (ec != ErrorCode_OK || !saver) { p->release(); return ec; }
  AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = m; ws.threadsCount = std::thread::hardware_concurrency(); auto t0 = clk::now(); ec = executeJob(saver, &ws); secs = since(t0); saver->release(); p->release(); return ec;
}
static int totalFrames(IModel* m) { int n = 0; for (int i = 0; i < m->getSize(); i++) n += m->getElement(i)->getSize(); return n; }
static void ensureNormals(IScan* s) { for (int k = 0; k < s->getSize(); k++) { IFrameMesh* f = s->getElement(k); if (f && !f->hasPointsNormals()) f->calculate(CM_PointsNormals_Default); } }
static double maxDiff(const Matrix4x4D& a, const Matrix4x4D& b) { double d = 0; for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) d = std::max(d, std::fabs(a.m[i][j] - b.m[i][j])); return d; }
static std::string readFile(const std::wstring& p) { std::string s; FILE* f = _wfopen(p.c_str(), L"rb"); if (!f) return s; char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n); fclose(f); return s; }
static bool writeFile(const std::wstring& p, const std::string& s) { FILE* f = _wfopen(p.c_str(), L"wb"); if (!f) return false; bool ok = fwrite(s.data(), 1, s.size(), f) == s.size(); fclose(f); return ok; }
static const char* ecName(ErrorCode ec) { static char b[32]; snprintf(b, sizeof b, "0x%08X", (unsigned)ec); return b; }
static void writeResultAndDie(const char* level, const char* name, const char* why) {   // a hung SDK job cannot be stopped: report, then end the process
  printf("        %s\nFAIL  %-46s [B1 B3]  (killed)\n--- %s: 0 passed, 1 failed, 0 skipped ---\n", why, name, level);
  if (const char* rf = getenv("TEST_RESULTS")) if (FILE* f = fopen(rf, "a")) { fprintf(f, "%s 0 1 0 0\n", level); fclose(f); }
  fflush(stdout); TerminateProcess(GetCurrentProcess(), 3);
}
static const int EXPECT_SCANS = 7, EXPECT_FRAMES = 87 + 131 + 161 + 41 + 63 + 215 + 174;   // from Trail14.sproj Items=

// ================================================================= tests
TEST(fixture_copy, "setup") {
  std::string why; long long before = fileSize(SRC() + L"\\Trail14.sproj"); FILETIME wt0 = {}; { WIN32_FILE_ATTRIBUTE_DATA a; if (GetFileAttributesExW((SRC() + L"\\Trail14.sproj").c_str(), GetFileExInfoStandard, &a)) wt0 = a.ftLastWriteTime; }
  if (!ensureFixture(why)) SKIP(why);
  CHECK(exists(FIXPROJ())); CHECK_EQ(listDir(FIX() + L"\\Scans").size(), listDir(SRC() + L"\\Scans").size());
  WIN32_FILE_ATTRIBUTE_DATA a; GetFileAttributesExW((SRC() + L"\\Trail14.sproj").c_str(), GetFileExInfoStandard, &a); CHECK(CompareFileTime(&a.ftLastWriteTime, &wt0) == 0); CHECK_EQ(fileSize(SRC() + L"\\Trail14.sproj"), before);
}
TEST(open_project_counts, "A2 E2") {
  NEED_FIXTURE(); Loaded L; openProj(FIXPROJ(), L); CHECK_EQ(L.ec, ErrorCode_OK); if (!L.m) return;
  CHECK_EQ(L.scanEntries, EXPECT_SCANS); CHECK_EQ(L.m->getSize(), EXPECT_SCANS); CHECK_EQ(totalFrames(L.m), EXPECT_FRAMES);
  CHECK_EQ(L.allEntries, L.scanEntries);   // ArtecRec-made: scans only (a Studio project would show other entries here: A2)
  int tex = 0; for (int i = 0; i < L.m->getSize(); i++) for (int k = 0; k < L.m->getElement(i)->getSize(); k++) if (L.m->getElement(i)->getElement(k)->isTextured()) tex++;
  NOTE("opened " + std::to_string(L.m->getSize()) + " scans, " + std::to_string(totalFrames(L.m)) + " frames (" + std::to_string(tex) + " textured) in " + tfw::str(L.secs) + " s; first name " + narrow(L.entryNames.empty() ? L"" : L.entryNames[0]));
  if (!L.entryNames.empty()) CHECK_EQ(L.entryNames[0], std::wstring(L"Group 1 - Spider Scan 1"));
}
TEST(save_and_reopen_round_trip, "A1 A3 A5") {
  NEED_FIXTURE(); Loaded A; openProj(FIXPROJ(), A); CHECK_EQ(A.ec, ErrorCode_OK); if (!A.m || A.ec != ErrorCode_OK) return;
  std::wstring dir = WORK() + L"\\saved\\Trail14rt", sp = dir + L"\\Trail14rt.sproj"; removeDir(WORK() + L"\\saved"); double ss = 0;
  for (int i = 0; i < A.m->getSize(); i++) A.m->getElement(i)->setName((L"Named " + std::to_wstring(i + 1)).c_str());   // does the saver write IScan names?
  ErrorCode ec = saveProj(A.m, sp, ss); CHECK_EQ(ec, ErrorCode_OK); if (ec != ErrorCode_OK) return;
  Loaded B; openProj(sp, B); CHECK_EQ(B.ec, ErrorCode_OK); if (!B.m) return;
  CHECK_EQ(B.m->getSize(), A.m->getSize()); CHECK_EQ(totalFrames(B.m), totalFrames(A.m)); double worstScan = 0, worstFrame = 0;
  for (int i = 0; i < std::min(A.m->getSize(), B.m->getSize()); i++) { IScan* a = A.m->getElement(i); IScan* b = B.m->getElement(i); CHECK_EQ(b->getSize(), a->getSize());
    worstScan = std::max(worstScan, maxDiff(a->getScanTransformation(), b->getScanTransformation()));
    for (int k = 0; k < std::min(a->getSize(), b->getSize()); k++) worstFrame = std::max(worstFrame, maxDiff(a->getTransformation(k), b->getTransformation(k))); }
  CHECK_MSG(worstScan <= 1e-6, "scan transformation differs by " + tfw::str(worstScan)); CHECK_MSG(worstFrame <= 1e-6, "frame transformation differs by " + tfw::str(worstFrame));
  std::string x = readFile(sp); bool namesSaved = x.find("Name=\"Named 1\"") != std::string::npos;
  NOTE("saved " + tfw::str(fileSize(sp)) + "-byte .sproj + Scans in " + tfw::str(ss) + " s; reopened in " + tfw::str(B.secs) + " s; worst diff scan " + tfw::str(worstScan) + " frame " + tfw::str(worstFrame) + "; IScan names written by the SDK saver: " + (namesSaved ? "yes" : "no (writeStudioNames is needed)"));
}
TEST(save_writes_only_project_files, "A1 A2") {
  // every entry the SDK saver writes must count as a project file, otherwise canReplaceFolder refuses every re-save
  std::wstring dir = WORK() + L"\\saved\\Trail14rt"; if (!exists(dir + L"\\Trail14rt.sproj")) SKIP("run save_and_reopen_round_trip first");
  auto e = listDir(dir); std::string names; for (auto& f : e) { names += narrow(f.name) + (f.dir ? "\\ " : " "); CHECK_MSG(logic::isProjectFile(f.name, L"Trail14rt"), "not a project file: " + narrow(f.name)); }
  NOTE("SDK saver wrote: " + names);
  e.push_back({L"artecrec-groups.txt", false}); e.push_back({L"artecrec-project.txt", false});
  CHECK_EQ(logic::canReplaceFolder(true, e, L"Trail14rt", true, false), logic::Swap_Ok);
  CHECK_EQ(logic::canReplaceFolder(true, e, L"Trail14rt", false, false), logic::Swap_NotOurs);
  auto stage = listDir(FIX()); CHECK_EQ(logic::canReplaceFolder(true, stage, L"Trail14", false, false), logic::Swap_NotOurs);   // a pre-marker ArtecRec project (Trail14 has no marker)
}
TEST(save_into_existing_folder, "A1") {
  // SDK fact behind the folder swap (2 Nov: 0x80010104): the saver cannot write into an existing project folder
  std::wstring sp = WORK() + L"\\saved\\Trail14rt\\Trail14rt.sproj"; if (!exists(sp)) SKIP("run save_and_reopen_round_trip first");
  Loaded A; openProj(FIXPROJ(), A); if (!A.m || A.ec != ErrorCode_OK) SKIP("could not open the fixture");
  double ss = 0; ErrorCode ec = saveProj(A.m, sp, ss);
  NOTE(std::string("second save into the same folder: ") + ecName(ec) + " in " + tfw::str(ss) + " s" + (ec == ErrorCode_OK ? " (the SDK CAN overwrite now)" : " (as expected: the swap is needed)"));
  Loaded B; openProj(sp, B); CHECK_EQ(B.ec, ErrorCode_OK); if (B.m) { CHECK_EQ(B.m->getSize(), EXPECT_SCANS); CHECK_EQ(totalFrames(B.m), EXPECT_FRAMES); }   // the earlier save must still open
}
TEST(kannada_names_in_sproj, "A5 D5") {
  std::wstring sp = WORK() + L"\\saved\\Trail14rt\\Trail14rt.sproj"; if (!exists(sp)) SKIP("run save_and_reopen_round_trip first");
  std::string x = readFile(sp); std::vector<std::wstring> names;
  for (int i = 0; i < EXPECT_SCANS; i++) names.push_back(std::wstring(L"\x0CAC\x0CC7\x0CB2\x0CC2\x0CB0\x0CC1 - \x0CB6\x0CBE\x0CB8\x0CA8 ") + std::to_wstring(i + 1) + (i == 2 ? L" <&\">" : L""));
  CHECK_EQ(logic::rewriteScanNames(x, names), EXPECT_SCANS); std::wstring sp2 = sp + L".tmp"; CHECK(writeFile(sp2, x)); CHECK(MoveFileExW(sp2.c_str(), sp.c_str(), MOVEFILE_REPLACE_EXISTING));
  Loaded B; openProj(sp, B); CHECK_EQ(B.ec, ErrorCode_OK); CHECK_EQ(B.scanEntries, EXPECT_SCANS);
  for (int i = 0; i < std::min((int)B.entryNames.size(), EXPECT_SCANS); i++) CHECK_EQ(B.entryNames[i], names[i]);
}
TEST(loaded_scan_names_match_sproj, "D5") {
  // The SDK gives every loaded scan its internal name ("Scan 5"); the .sproj entry names are the real ones. rec_sdk.h
  // openProject (fixed 5 Nov) names each scan from its entry first, the SDK name only as a fallback. Same rule here.
  std::wstring sp = WORK() + L"\saved\Trail14rt\Trail14rt.sproj"; if (!exists(sp)) SKIP("run save_and_reopen_round_trip first");
  Loaded B; openProj(sp, B); if (!B.m || B.ec != ErrorCode_OK) SKIP("could not open the saved copy"); int bad = 0, sdkDiffers = 0; std::string ex;
  for (int i = 0; i < std::min(B.m->getSize(), (int)B.entryNames.size()); i++) { const wchar_t* n = B.m->getElement(i)->getName(); std::wstring sdk = n ? n : L"";
    if (sdk != B.entryNames[i]) sdkDiffers++;
    std::wstring app = !B.entryNames[i].empty() ? B.entryNames[i] : (n ? n : L"Scan");   // the app's rule
    if (app != B.entryNames[i]) { if (!bad) ex = narrow(app) + " vs " + narrow(B.entryNames[i]); bad++; } }
  NOTE(std::to_string(sdkDiffers) + " of " + std::to_string(B.m->getSize()) + " SDK scan names differ from the .sproj names (expected; the app uses the .sproj names)");
  CHECK_MSG(bad == 0, std::to_string(bad) + " scans would be shown with a name other than their .sproj entry, e.g. " + ex);
}
TEST(auto_align_two_scans_returns, "B2 D3") {
  NEED_FIXTURE(); Loaded L; openProj(FIXPROJ(), L); if (!L.m || L.ec != ErrorCode_OK || L.m->getSize() < 5) SKIP("could not open the fixture");
  IModel* in = nullptr; createModel(&in); IScan* s3 = L.m->getElement(3); IScan* s4 = L.m->getElement(4); ensureNormals(s3); ensureNormals(s4); in->add(s3); in->add(s4);   // 41 + 63 frames
  Matrix4x4D before = s4->getScanTransformation();
  alg::AutoAlignSettings as; as.scannerType = s3->getScannerType(); alg::IAlgorithm* a = nullptr; ErrorCode ec = alg::createAutoalignAlgorithm(&a, &as); CHECK_EQ(ec, ErrorCode_OK); if (!a) { in->release(); return; }
  IModel* out = nullptr; createModel(&out); AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = in; ws.out = out; ws.threadsCount = std::thread::hardware_concurrency();
  ICancellationTokenSource* cts = nullptr; createCancellationTokenSource(&cts); ws.cancellation = cts->getToken();
  std::atomic<bool> fin{false}; std::atomic<bool> gaveUp{false}; auto t0 = clk::now();
  std::thread wd([&] { for (int i = 0; i < 600 && !fin.load(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(100)); if (!fin.load()) { gaveUp = true; cts->cancel(); }   // = runAutoAlign's 60 s limit
    for (int i = 0; i < 1200 && !fin.load(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(100)); if (!fin.load()) writeResultAndDie("integration", "auto_align_two_scans_returns", "auto-align did not return 120 s after cancel(): the 60 s limit does not work"); });
  ec = executeJob(a, &ws); double secs = since(t0); fin = true; wd.join(); a->release();
  double moved = 0; if (ec == ErrorCode_OK && out->getSize() == 2) { auto& T = out->getElement(1)->getScanTransformation(); for (int r = 0; r < 3; r++) moved += (T.m[r][3] - before.m[r][3]) * (T.m[r][3] - before.m[r][3]); moved = sqrt(moved); }
  NOTE(std::string("auto-align (scans 4+5) ") + ecName(ec) + " in " + tfw::str(secs) + " s, out " + std::to_string(out->getSize()) + ", scan 5 moved " + tfw::str(moved) + " mm" + (gaveUp ? ", CANCELLED at 60 s" : ""));
  CHECK(secs < 120); CHECK(out->getSize() == 0 || out->getSize() == 2);
  out->release(); in->release(); cts->release();
}
// cancellation: start the job, cancel after `cancelAt` s, measure how long executeJob takes to return after cancel()
static void cancelRun(const char* name, bool global, double cancelAt, bool preCancel) {
  NEED_FIXTURE(); Loaded L; openProj(FIXPROJ(), L); if (!L.m || L.ec != ErrorCode_OK) SKIP("could not open the fixture");
  int big = 0; for (int i = 1; i < L.m->getSize(); i++) if (L.m->getElement(i)->getSize() > L.m->getElement(big)->getSize()) big = i;
  IScan* s = L.m->getElement(big); int nf = s->getSize(); bool allTex = true; for (int k = 0; k < nf; k++) if (!s->getElement(k)->isTextured()) { allTex = false; break; }
  IModel* in = nullptr; createModel(&in); in->add(s); IModel* out = nullptr; createModel(&out); alg::IAlgorithm* a = nullptr; ErrorCode ec;
  if (global) { alg::GlobalRegistrationSettings gd; gd.scannerType = s->getScannerType(); gd.registrationType = alg::GlobalRegistrationType_Geometry; ec = alg::createGlobalRegistrationAlgorithm(&a, &gd); }
  else { alg::SerialRegistrationSettings sd; sd.scannerType = s->getScannerType(); sd.registrationType = allTex ? alg::SerialRegistrationType_FineTextured : alg::SerialRegistrationType_Fine; ec = alg::createSerialRegistrationAlgorithm(&a, &sd); }
  CHECK_EQ(ec, ErrorCode_OK); if (!a) return;
  AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = in; ws.out = out; ws.threadsCount = global ? std::max(1u, std::thread::hardware_concurrency() / 2) : std::thread::hardware_concurrency();
  ICancellationTokenSource* cts = nullptr; createCancellationTokenSource(&cts); ws.cancellation = cts->getToken(); if (preCancel) cts->cancel();
  std::atomic<bool> fin{false}; std::atomic<double> cancelT{-1}; auto t0 = clk::now();
  std::thread wd([&] { if (!preCancel) { while (!fin.load() && since(t0) < cancelAt) std::this_thread::sleep_for(std::chrono::milliseconds(20)); if (fin.load()) return; cts->cancel(); cancelT = since(t0); } else cancelT = 0;
    while (!fin.load()) { if (since(t0) - cancelT.load() > 120) { char b[200]; snprintf(b, sizeof b, "%s did not return 120 s after cancel() - the time limit cannot stop it", global ? "global registration" : "serial registration"); writeResultAndDie("integration", name, b); } std::this_thread::sleep_for(std::chrono::milliseconds(50)); } });
  ec = executeJob(a, &ws); double total = since(t0); fin = true; wd.join(); a->release();
  double after = cancelT.load() < 0 ? -1 : total - cancelT.load();
  NOTE(std::string(global ? "global registration (geometry, half cores)" : (allTex ? "serial FineTextured" : "serial Fine")) + " on scan " + std::to_string(big + 1) + " (" + std::to_string(nf) + " frames): " + ecName(ec) + ", total " + tfw::str(total) + " s, " +
       (after < 0 ? std::string("finished before cancel at ") + tfw::str(cancelAt) + " s (increase work)" : (preCancel ? "token cancelled before start" : "cancel() at " + tfw::str(cancelT.load()) + " s") + ", returned " + tfw::str(after) + " s after cancel"));
  if (after < 0) { out->release(); in->release(); cts->release(); SKIP("job finished before the cancel"); }
  CHECK_MSG(after <= 15.0, "executeJob returned only " + tfw::str(after) + " s after cancel() (bound 15 s)");
  CHECK_MSG(ec != ErrorCode_OK, "cancelled job returned OK (execLimited maps this to OperationAborted; result must not be used)");
  out->release(); in->release(); cts->release();
}
TEST(cancel_serial_registration, "B1 B3 D2") { cancelRun("cancel_serial_registration", false, 1.0, false); }   // uncancelled: ~4 s here
TEST(cancel_global_registration, "B1 B3") { cancelRun("cancel_global_registration", true, 3.0, false); }
TEST(cancel_global_before_start, "B1") { cancelRun("cancel_global_before_start", true, 0, true); }
// timings without cancel (run with: itest_sdk.exe baseline_)
static void baseline(bool global) {
  NEED_FIXTURE(); Loaded L; openProj(FIXPROJ(), L); if (!L.m || L.ec != ErrorCode_OK) SKIP("could not open the fixture");
  int big = 0; for (int i = 1; i < L.m->getSize(); i++) if (L.m->getElement(i)->getSize() > L.m->getElement(big)->getSize()) big = i; IScan* s = L.m->getElement(big); bool allTex = true; for (int k = 0; k < s->getSize(); k++) if (!s->getElement(k)->isTextured()) allTex = false;
  IModel* in = nullptr; createModel(&in); in->add(s); IModel* out = nullptr; createModel(&out); alg::IAlgorithm* a = nullptr;
  if (global) { alg::GlobalRegistrationSettings gd; gd.scannerType = s->getScannerType(); gd.registrationType = alg::GlobalRegistrationType_Geometry; alg::createGlobalRegistrationAlgorithm(&a, &gd); }
  else { alg::SerialRegistrationSettings sd; sd.scannerType = s->getScannerType(); sd.registrationType = allTex ? alg::SerialRegistrationType_FineTextured : alg::SerialRegistrationType_Fine; alg::createSerialRegistrationAlgorithm(&a, &sd); }
  if (!a) { CHECK(false); return; } AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.in = in; ws.out = out; ws.threadsCount = global ? std::max(1u, std::thread::hardware_concurrency() / 2) : std::thread::hardware_concurrency();
  auto t0 = clk::now(); ErrorCode ec = executeJob(a, &ws); double t = since(t0); a->release();
  NOTE(std::string(global ? "global" : "serial") + " registration uncancelled on scan " + std::to_string(big + 1) + " (" + std::to_string(s->getSize()) + " frames): " + ecName(ec) + " in " + tfw::str(t) + " s");
  CHECK_EQ(ec, ErrorCode_OK); CHECK(out->getSize() == 1 && out->getElement(0)->getSize() == s->getSize()); out->release(); in->release();
}
TEST(baseline_serial_registration, "timing") { baseline(false); }
TEST(baseline_global_registration, "timing") { baseline(true); }
TEST(flatness_of_real_windows, "D1") {
  // how often would the geometry-only join be refused on a real carved stone? planeRms of 5-frame windows of Trail14
  NEED_FIXTURE(); Loaded L; openProj(FIXPROJ(), L); if (!L.m || L.ec != ErrorCode_OK) SKIP("could not open the fixture");
  std::vector<float> all; int below = 0, total = 0;
  for (int i = 0; i < L.m->getSize(); i++) { IScan* s = L.m->getElement(i); for (int j = 0; j + 5 <= s->getSize(); j += 10) { std::vector<float> p;
      for (int k = j; k < j + 5; k++) { IFrameMesh* f = s->getElement(k); if (!f || !f->getPoints()) continue; const auto& M = s->getTransformation(k).m; const Point3F* q = f->getPoints()->getPointer(); int n = f->getPoints()->getSize();
        for (int t = 0; t < n; t += 6) { double x = q[t].x, y = q[t].y, z = q[t].z; for (int r = 0; r < 3; r++) p.push_back((float)(M[r][0] * x + M[r][1] * y + M[r][2] * z + M[r][3])); } }
      if (p.size() < 300) continue; float r = logic::planeRms(p); all.push_back(r); total++; if (!logic::acceptJoin(0.9f, 0.5f, true, r).ok) below++; } }
  if (all.empty()) SKIP("no windows"); std::sort(all.begin(), all.end());
  NOTE(std::to_string(total) + " windows: flatness min " + tfw::str(all.front()) + " / median " + tfw::str(all[all.size() / 2]) + " / max " + tfw::str(all.back()) + " mm; " + std::to_string(below) + " (" + std::to_string(100 * below / total) + "%) would be refused for a geometry-only join at 0.6 mm");
  CHECK(total > 0);
}
TEST(cleanup_saved_copies, "setup") { CHECK(removeDir(WORK() + L"\\saved")); NOTE("the Trail14 copy in " + narrow(FIX()) + " is kept for the next run (delete it by hand: ~1.1 GB)"); }

int main(int argc, char** argv) {
  setOutputLevel(VerboseLevel_Warning);
  if (argc == 1) {   // everything except the baseline timings, in order
    static char* def[] = {argv[0], (char*)"fixture_copy", (char*)"open_project", (char*)"save_", (char*)"kannada", (char*)"loaded_scan_names", (char*)"auto_align", (char*)"cancel_", (char*)"flatness", (char*)"cleanup"};
    return tfw::run(10, def, "integration"); }
  return tfw::run(argc, argv, "integration");
}
