// testfw.h - tiny self-contained test framework for the ArtecRec test suite (no downloads, header only).
//   TEST(name, "A1 F9") { CHECK(x); CHECK_EQ(a, b); CHECK_NEAR(a, b, tol); if (...) SKIP("why"); NOTE("..."); }
//   int main(int argc, char** argv) { return tfw::run(argc, argv, "unit"); }
// Each test prints one line: PASS / FAIL / SKIP, its name, the review findings it guards and its time.
// Arguments: substrings; only tests whose name contains one of them run ("--list" lists the tests).
// If the environment variable TEST_RESULTS names a file, one line "<level> <pass> <fail> <skip> <secs>" is appended
// (run-tests.bat adds these up for the final summary). Exit code: 0 when nothing failed, 1 otherwise.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <chrono>
#include <sstream>
#include <exception>

namespace tfw {
struct Skip { std::string why; };
struct Case { const char* name; const char* guards; void (*fn)(); };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
struct Reg { Reg(const char* n, const char* g, void (*f)()) { registry().push_back({n, g, f}); } };
inline int& failures() { static int f = 0; return f; }
inline std::vector<std::string>& notes() { static std::vector<std::string> n; return n; }

inline std::string esc(const std::wstring& w) {   // printable form of a wide string (non-ASCII as \uXXXX)
  std::string s; char b[16]; for (wchar_t c : w) { if (c >= 0x20 && c < 0x7F) s += (char)c; else { snprintf(b, sizeof b, "\\u%04X", (unsigned)c); s += b; } } return s; }
inline std::string esc(const std::string& v) { std::string s; char b[8]; for (unsigned char c : v) { if (c >= 0x20 && c < 0x7F) s += (char)c; else { snprintf(b, sizeof b, "\\x%02X", c); s += b; } } return s; }
template <class T> std::string str(const T& v) { std::ostringstream o; o.precision(10); o << v; return o.str(); }
inline std::string str(const std::wstring& v) { return "L\"" + esc(v) + "\""; }
inline std::string str(const wchar_t* v) { return str(std::wstring(v ? v : L"(null)")); }
inline std::string str(const std::string& v) { return "\"" + esc(v) + "\""; }
inline std::string str(const char* v) { return str(std::string(v ? v : "(null)")); }
inline std::string str(bool v) { return v ? "true" : "false"; }

inline void fail(const char* file, int line, const std::string& msg) {
  failures()++; if (failures() <= 8) { const char* f = strrchr(file, '\\'); printf("        %s:%d: %s\n", f ? f + 1 : file, line, msg.c_str()); }
  else if (failures() == 9) printf("        (more failures in this test not shown)\n"); fflush(stdout); }
inline void note(const std::string& s) { notes().push_back(s); }

inline int run(int argc, char** argv, const char* level) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  bool list = false; std::vector<std::string> filt; for (int i = 1; i < argc; i++) { if (!strcmp(argv[i], "--list")) list = true; else filt.push_back(argv[i]); }
  int pass = 0, fail = 0, skip = 0; auto T0 = std::chrono::steady_clock::now();
  printf("=== %s tests ===\n", level);
  for (auto& c : registry()) {
    if (!filt.empty()) { bool m = false; for (auto& f : filt) if (strstr(c.name, f.c_str())) m = true; if (!m) continue; }
    if (list) { printf("  %s  [%s]\n", c.name, c.guards); continue; }
    failures() = 0; notes().clear(); std::string skipWhy, crash; auto t0 = std::chrono::steady_clock::now();
    try { c.fn(); } catch (Skip& s) { skipWhy = s.why; } catch (std::exception& e) { crash = e.what(); } catch (...) { crash = "unknown exception"; }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!crash.empty()) { failures()++; printf("        exception: %s\n", crash.c_str()); }
    const char* verdict = failures() ? "FAIL" : !skipWhy.empty() ? "SKIP" : "PASS";
    if (failures()) fail++; else if (!skipWhy.empty()) skip++; else pass++;
    printf("%s  %-46s [%s]  (%.0f ms)%s%s\n", verdict, c.name, c.guards, ms, skipWhy.empty() ? "" : "  - ", skipWhy.c_str());
    for (auto& n : notes()) printf("        note: %s\n", n.c_str());
  }
  double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - T0).count();
  if (!list) printf("--- %s: %d passed, %d failed, %d skipped in %.1f s ---\n", level, pass, fail, skip, secs);
  if (const char* rf = getenv("TEST_RESULTS")) { if (FILE* f = fopen(rf, "a")) { fprintf(f, "%s %d %d %d %.1f\n", level, pass, fail, skip, secs); fclose(f); } }
  return fail ? 1 : 0;
}
}  // namespace tfw

#define TFW_CAT2(a, b) a##b
#define TEST(name, guards) static void name(); static tfw::Reg TFW_CAT2(name, _reg)(#name, guards, name); static void name()
#define CHECK(c) do { if (!(c)) tfw::fail(__FILE__, __LINE__, "CHECK(" #c ")"); } while (0)
#define CHECK_MSG(c, msg) do { if (!(c)) tfw::fail(__FILE__, __LINE__, std::string("CHECK(" #c "): ") + (msg)); } while (0)
#define CHECK_EQ(a, b) do { auto _va = (a); auto _vb = (b); if (!(_va == _vb)) tfw::fail(__FILE__, __LINE__, std::string(#a " == " #b "  got ") + tfw::str(_va) + " vs " + tfw::str(_vb)); } while (0)
#define CHECK_NEAR(a, b, tol) do { double _va = (double)(a), _vb = (double)(b); if (!(std::fabs(_va - _vb) <= (tol))) tfw::fail(__FILE__, __LINE__, std::string(#a " ~ " #b "  got ") + tfw::str(_va) + " vs " + tfw::str(_vb) + " (tol " + tfw::str((double)(tol)) + ")"); } while (0)
#define SKIP(why) throw tfw::Skip{why}
#define NOTE(s) tfw::note(s)
