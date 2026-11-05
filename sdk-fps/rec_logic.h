// ArtecRec — pure decision rules, no Win32/SDK/GL state. Everything here is unit-tested (C:\Artec\tests).
// Review 5 Nov 2026 (https://claude.ai/artifact/DSWy46XH37LgxR6Zanmdas): the rules that decide where a project is
// saved, whether an existing folder may be replaced, how names are written into the .sproj, whether a join or a
// refinement is accepted, and which user actions are allowed in which mode.
#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <cwctype>
#include <algorithm>

namespace logic {

// ---------------------------------------------------------------- save target (review A1, F9)
// The Save dialog returns a full path to a .sproj the user typed or picked. The project always lives in its own
// folder <dir>\<stem>\<stem>.sproj, unless the user picked a .sproj that is already inside a folder named <stem>
// (re-saving an existing project). The stem never loses characters when ".sproj" was not typed.
inline bool iEndsWith(const std::wstring& s, const std::wstring& suf) {
  if (suf.size() > s.size()) return false;
  for (size_t i = 0; i < suf.size(); i++) if (towlower(s[s.size() - suf.size() + i]) != towlower(suf[i])) return false;
  return true;
}
inline std::wstring stemOf(const std::wstring& file) {   // file name without folder; ".sproj" removed only if present
  size_t sl = file.find_last_of(L"\\/"); std::wstring n = sl == std::wstring::npos ? file : file.substr(sl + 1);
  if (iEndsWith(n, L".sproj")) n = n.substr(0, n.size() - 6);
  while (!n.empty() && (n.back() == L' ' || n.back() == L'.')) n.pop_back();   // Windows strips these from folder names
  return n;
}
inline std::wstring folderOf(const std::wstring& path) { size_t sl = path.find_last_of(L"\\/"); return sl == std::wstring::npos ? L"" : path.substr(0, sl); }
inline std::wstring leafOf(const std::wstring& path) { size_t sl = path.find_last_of(L"\\/"); return sl == std::wstring::npos ? path : path.substr(sl + 1); }
inline bool iEquals(const std::wstring& a, const std::wstring& b) { if (a.size() != b.size()) return false; for (size_t i = 0; i < a.size(); i++) if (towlower(a[i]) != towlower(b[i])) return false; return true; }
// Returns "" when the stem is empty or invalid.
inline std::wstring saveTarget(const std::wstring& picked) {
  std::wstring stem = stemOf(picked), dir = folderOf(picked);
  if (stem.empty() || dir.empty()) return L"";
  for (wchar_t c : stem) if (c == L'<' || c == L'>' || c == L':' || c == L'"' || c == L'|' || c == L'?' || c == L'*') return L"";
  if (iEquals(leafOf(dir), stem)) return dir + L"\\" + stem + L".sproj";   // whole leaf equals the stem, not just its end
  return dir + L"\\" + stem + L"\\" + stem + L".sproj";
}

// ---------------------------------------------------------------- replacing an existing project folder (A1, A2)
// The SDK cannot write into an existing project folder, so a re-save writes a new folder and swaps it in. That swap
// is allowed only for a folder ArtecRec itself made for this project (marker file present) or the folder of the
// project currently open in ArtecRec that was saved by ArtecRec. Any file the swap would lose that is not a project
// file blocks it. Names are leaf names inside the folder; dirs end with '\'.
struct FolderEntry { std::wstring name; bool dir; };
enum SwapVerdict { Swap_New, Swap_Ok, Swap_Foreign, Swap_NotOurs };
inline bool isProjectFile(const std::wstring& name, const std::wstring& stem) {
  return iEquals(name, stem + L".sproj") || iEquals(name, L"artecrec-groups.txt") || iEquals(name, L"artecrec-project.txt") || iEquals(name, L"Scans") || iEquals(name, L"Models") || iEquals(name, L"Data") || iEquals(name, L"Thumbnails");
}
inline SwapVerdict canReplaceFolder(bool exists, const std::vector<FolderEntry>& entries, const std::wstring& stem, bool hasMarker, bool openedFromStudioWithOtherEntries) {
  if (!exists) return Swap_New;
  if (entries.empty()) return Swap_Ok;   // empty folder: nothing to lose
  if (openedFromStudioWithOtherEntries) return Swap_Foreign;
  for (auto& e : entries) if (!isProjectFile(e.name, stem)) return Swap_Foreign;
  return hasMarker ? Swap_Ok : Swap_NotOurs;
}

// ---------------------------------------------------------------- names in the .sproj (A5)
inline std::string utf8(const std::wstring& w) {
  std::string s; for (size_t i = 0; i < w.size(); i++) { unsigned cp = (unsigned)w[i];
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size()) { unsigned lo = (unsigned)w[i + 1]; if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i++; } }
    if (cp < 0x80) s += (char)cp; else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); } }
  return s;
}
inline std::string xmlAttr(const std::wstring& w) {
  std::string s; for (char c : utf8(w)) { if (c == '&') s += "&amp;"; else if (c == '"') s += "&quot;"; else if (c == '<') s += "&lt;"; else if (c == '>') s += "&gt;"; else if ((unsigned char)c < 0x20) s += ' '; else s += c; }
  return s;
}
// Rewrites the Name="…" of each <Scan …> element in order. names[i] empty = leave as is. Returns count rewritten.
inline int rewriteScanNames(std::string& xml, const std::vector<std::wstring>& names) {
  size_t pos = 0; int i = 0, n = 0;
  while (true) { size_t s = xml.find("<Scan ", pos); if (s == std::string::npos) break; size_t end = xml.find('>', s); if (end == std::string::npos) break;
    size_t nm = xml.find("Name=\"", s); if (nm == std::string::npos || nm > end) { pos = end; i++; continue; }
    size_t q = xml.find('"', nm + 6); if (q == std::string::npos || q > end) break;
    if (i < (int)names.size() && !names[i].empty()) { std::string v = xmlAttr(names[i]); xml.replace(nm + 6, q - nm - 6, v); pos = nm + 6 + v.size(); n++; } else pos = q + 1;
    i++; }
  return n;
}

// ---------------------------------------------------------------- join acceptance (D1)
// The surface gate: share of probe points within 6 mm of the reference window and their median distance.
// The geometry-only placement is additionally refused when the reference window is too flat to fix a position by
// shape: a pass can slide along a flat surface and still lie on it. flatness = RMS distance (mm) of the window
// points from their best-fit plane. 0.6 mm is deliberately loose (only near-featureless surfaces are refused) until
// field data calibrate it: every join now logs its flatness.
struct GateResult { bool ok; const char* why; };
inline GateResult acceptJoin(float share, float medianMm, bool geometryOnly, float flatnessMm) {
  if (share < 0.30f) return {false, "too little of the new surface lies on the previous pass"};
  if (medianMm > 2.5f) return {false, "the new surface lies too far from the previous pass"};
  if (geometryOnly && flatnessMm < 0.6f) return {false, "the surface is too flat to place the pass by shape alone"};
  return {true, ""};
}
// RMS distance from the best-fit plane through points (x,y,z triples), by the smallest eigenvalue of the covariance.
inline float planeRms(const std::vector<float>& p) {
  size_t n = p.size() / 3; if (n < 3) return 0;
  double m[3] = {0, 0, 0}; for (size_t i = 0; i < n; i++) for (int k = 0; k < 3; k++) m[k] += p[i * 3 + k]; for (int k = 0; k < 3; k++) m[k] /= n;
  double C[3][3] = {}; for (size_t i = 0; i < n; i++) { double d[3] = {p[i * 3] - m[0], p[i * 3 + 1] - m[1], p[i * 3 + 2] - m[2]}; for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) C[a][b] += d[a] * d[b]; }
  for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) C[a][b] /= n;
  // smallest eigenvalue of a symmetric 3x3 (closed form)
  double p1 = C[0][1] * C[0][1] + C[0][2] * C[0][2] + C[1][2] * C[1][2];
  if (p1 < 1e-18) return (float)std::sqrt(std::max(0.0, std::min(C[0][0], std::min(C[1][1], C[2][2]))));
  double q = (C[0][0] + C[1][1] + C[2][2]) / 3, p2 = (C[0][0] - q) * (C[0][0] - q) + (C[1][1] - q) * (C[1][1] - q) + (C[2][2] - q) * (C[2][2] - q) + 2 * p1, pp = std::sqrt(p2 / 6);
  double B[3][3]; for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) B[a][b] = (C[a][b] - (a == b ? q : 0)) / pp;
  double r = (B[0][0] * (B[1][1] * B[2][2] - B[1][2] * B[2][1]) - B[0][1] * (B[1][0] * B[2][2] - B[1][2] * B[2][0]) + B[0][2] * (B[1][0] * B[2][1] - B[1][1] * B[2][0])) / 2;
  double phi = r <= -1 ? 3.14159265358979 / 3 : r >= 1 ? 0 : std::acos(r) / 3;
  double e3 = q + 2 * pp * std::cos(phi + 2 * 3.14159265358979 / 3);   // smallest
  return (float)std::sqrt(std::max(0.0, e3));
}

// ---------------------------------------------------------------- pass refinement acceptance (D2)
// ICP result: translation (mm), rotation angle (degrees), share of sampled points matched within 2 mm at the end.
struct RefineVerdict { bool ok; const char* why; };
inline RefineVerdict acceptRefine(double movedMm, double rotDeg, double matchedShare) {
  if (movedMm > 8.0) return {false, "it would move the pass more than 8 mm"};
  if (rotDeg > 2.0) return {false, "it would turn the pass more than 2 degrees"};
  if (matchedShare < 0.30) return {false, "too few points overlap the other passes"};
  return {true, ""};
}
inline double rotationDeg(const double R[3][3]) { double c = (R[0][0] + R[1][1] + R[2][2] - 1) / 2; c = std::max(-1.0, std::min(1.0, c)); return std::acos(c) * 180.0 / 3.14159265358979; }

// ---------------------------------------------------------------- what the user may do in each mode (C1, F1, F7)
enum Mode { M_Idle, M_Preview, M_Record, M_Paused, M_Stopping, M_Aligning, M_Busy };
enum Action { A_EditScans, A_Save, A_Open, A_New, A_AutoAlign, A_Countdown, A_Preview, A_Record, A_Close };
// busyPost: scans still being tidied after recording. stuck: the scanner job was abandoned (no new recording).
inline bool allowed(Action a, int mode, bool busyPost, bool stuck) {
  switch (a) {
  case A_EditScans: return mode == M_Idle && !busyPost;
  case A_Save: return mode == M_Idle;                  // queued until tidy-up finishes when busyPost
  case A_Open: case A_New: return mode == M_Idle && !busyPost;
  case A_AutoAlign: return mode == M_Idle && !busyPost;
  case A_Countdown: return (mode == M_Idle || mode == M_Preview) && !stuck;
  case A_Preview: return (mode == M_Idle || mode == M_Paused) && !stuck;
  case A_Record: return (mode == M_Idle || mode == M_Preview || mode == M_Record || mode == M_Paused) && !stuck;
  case A_Close: return mode == M_Idle || mode == M_Busy || stuck;
  }
  return false;
}

// ---------------------------------------------------------------- restart rules after tracking loss (B5)
// Fresh recordings that track nothing are restarted at most this many times in a row; then recording stops and the
// user is told to point the scanner at the stone.
inline bool mayRestartFresh(int consecutiveFreshRestarts, bool userStop, bool reconnecting) { return !userStop && !reconnecting && consecutiveFreshRestarts < 5; }

// ---------------------------------------------------------------- depth range (F4)
// Space Spider working range per Artec: about 150-350 mm; Studio's default 170-330 mm.
inline void clampRange(int& nearMm, int& farMm) {
  nearMm = std::max(150, std::min(nearMm, 330)); farMm = std::max(170, std::min(farMm, 400)); if (nearMm > farMm - 20) nearMm = farMm - 20;
}

// ---------------------------------------------------------------- memory (E1)
enum MemLevel { Mem_Ok, Mem_Warn, Mem_Refuse };
inline MemLevel memLevel(double freeGB) { return freeGB < 6 ? Mem_Refuse : freeGB < 16 ? Mem_Warn : Mem_Ok; }

// ---------------------------------------------------------------- pause/resume segments (C6)
// One entry per recorded segment, in the order the SDK returns the scans.
struct SegmentPose { bool found; double T[4][4]; };

}  // namespace logic
