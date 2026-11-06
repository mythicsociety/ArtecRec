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
  for (wchar_t c : stem) if (c == L'<' || c == L'>' || c == L':' || c == L'"' || c == L'|' || c == L'?' || c == L'*' || c < 32) return L"";
  { static const wchar_t* reserved[] = {L"CON", L"PRN", L"AUX", L"NUL", L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9", L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9"};
    for (auto r : reserved) if (iEquals(stem, r)) return L""; }
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
  std::wstring n = iEndsWith(name, L".tmp") ? name.substr(0, name.size() - 4) : name;   // a leftover of an interrupted atomic write
  return iEquals(n, stem + L".sproj") || iEquals(n, L"artecrec-groups.txt") || iEquals(n, L"artecrec-project.txt") || iEquals(n, L"Scans");   // the SDK saver writes only these; Models etc. are Studio's and block a swap
}
// listedOk=false (the folder could not be read) is treated as foreign: never swap what could not be checked.
inline SwapVerdict canReplaceFolder(bool exists, const std::vector<FolderEntry>& entries, const std::wstring& stem, bool hasMarker, bool openedFromStudioWithOtherEntries, bool listedOk = true) {
  if (!exists) return Swap_New;
  if (!listedOk) return Swap_Foreign;
  if (entries.empty()) return Swap_Ok;   // empty folder: nothing to lose
  if (openedFromStudioWithOtherEntries) return Swap_Foreign;
  for (auto& e : entries) if (!isProjectFile(e.name, stem)) return Swap_Foreign;
  return hasMarker ? Swap_Ok : Swap_NotOurs;
}

// ---------------------------------------------------------------- names in the .sproj (A5)
inline std::string utf8(const std::wstring& w) {
  std::string s; for (size_t i = 0; i < w.size(); i++) { unsigned cp = (unsigned)w[i];
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size() && (unsigned)w[i + 1] >= 0xDC00 && (unsigned)w[i + 1] <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + ((unsigned)w[i + 1] - 0xDC00); i++; }
    else if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;   // a lone surrogate is not valid UTF-8: replacement character
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
inline size_t tagEnd(const std::string& x, size_t from) {   // the '>' that closes the tag starting at `from`, skipping quoted values
  char q = 0; for (size_t i = from; i < x.size(); i++) { char c = x[i]; if (q) { if (c == q) q = 0; } else if (c == '"' || c == '\'') q = c; else if (c == '>') return i; } return std::string::npos; }
inline int rewriteScanNames(std::string& xml, const std::vector<std::wstring>& names) {
  size_t pos = 0; int i = 0, n = 0;
  while (true) { size_t s = xml.find("<Scan ", pos); if (s == std::string::npos) break; size_t end = tagEnd(xml, s); if (end == std::string::npos) break;
    // the Name attribute itself: preceded by whitespace, not "DisplayName=" or "FileName=" (test finding, 5 Nov)
    size_t nm = std::string::npos; { char q = 0; for (size_t k = s + 5; k + 6 <= end; k++) { char c = xml[k]; if (q) { if (c == q) q = 0; continue; } if (c == '"' || c == '\'') { q = c; continue; } if ((c == ' ' || c == '\t' || c == '\n' || c == '\r') && xml.compare(k + 1, 6, "Name=\"") == 0) { nm = k + 1; break; } } }
    if (nm == std::string::npos) { pos = end; i++; continue; }
    size_t q = xml.find('"', nm + 6); if (q == std::string::npos || q > end) break;
    if (i < (int)names.size() && !names[i].empty()) { std::string v = xmlAttr(names[i]); xml.replace(nm + 6, q - nm - 6, v); end = end + v.size() - (q - nm - 6); n++; }
    pos = end; i++; }
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
  if (!(share == share) || !(medianMm == medianMm) || !(flatnessMm == flatnessMm)) return {false, "the join could not be measured"};   // NaN
  if (share < 0.30f) return {false, "too little of the new surface lies on the previous pass"};
  if (medianMm > 2.5f) return {false, "the new surface lies too far from the previous pass"};
  if (geometryOnly && flatnessMm < 0.6f) return {false, "the surface is too flat to place the pass by shape alone"};
  // test finding 5 Nov: on 2 mm carving a half-letter slide still lies within 1.2-1.5 mm of the surface; the real
  // shape-only joins of 5 Nov all had medians of 0.3-0.8 mm. Shape-only joins must sit within 1.0 mm.
  if (geometryOnly && medianMm > 1.0f) return {false, "the new surface does not sit closely enough on the previous pass"};
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
  if (!(movedMm == movedMm) || !(rotDeg == rotDeg) || !(matchedShare == matchedShare)) return {false, "the result could not be measured"};   // NaN
  if (movedMm > 8.0) return {false, "it would move the pass more than 8 mm"};
  if (rotDeg > 2.0) return {false, "it would turn the pass more than 2 degrees"};
  if (matchedShare < 0.10) return {false, "too few points overlap the other passes"};   // share of the whole pass; strips overlap 20-40 %
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
  case A_Close: return mode == M_Idle || stuck;   // not while saving or opening
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
  nearMm = std::max(150, std::min(nearMm, 330)); farMm = std::max(170, std::min(farMm, 350)); if (nearMm > farMm - 20) nearMm = farMm - 20;
}

// ---------------------------------------------------------------- memory (E1)
enum MemLevel { Mem_Ok, Mem_Warn, Mem_Refuse };
inline MemLevel memLevel(double freeGB) { return freeGB < 6 ? Mem_Refuse : freeGB < 16 ? Mem_Warn : Mem_Ok; }

// ---------------------------------------------------------------- live pose from an auto-align result
// The search aligns [reference scan with pose Rin, probe] and the SDK returns new poses Rout (reference) and Pout (probe).
// The reference must stay where it is, so the correction C = Rin * inv(Rout) (C * Rout = Rin) is applied to the probe:
// T = Rin * inv(Rout) * Pout. (Outside review 6 Nov: the code had inv(Rout) * Rin * Pout, which is the same only when
// the SDK leaves the reference unmoved.) Rigid 4x4, row-major, column vectors.
inline void mul4(const double A[4][4], const double B[4][4], double O[4][4]) { for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { double t = 0; for (int k = 0; k < 4; k++) t += A[i][k] * B[k][j]; O[i][j] = t; } }
inline void invRigid4(const double M[4][4], double O[4][4]) { for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) O[i][j] = M[j][i]; for (int i = 0; i < 3; i++) O[i][3] = -(O[i][0] * M[0][3] + O[i][1] * M[1][3] + O[i][2] * M[2][3]); O[3][0] = O[3][1] = O[3][2] = 0; O[3][3] = 1; }
inline void livePose(const double Rin[4][4], const double Rout[4][4], const double Pout[4][4], double T[4][4]) { double inv[4][4], C[4][4]; invRigid4(Rout, inv); mul4(Rin, inv, C); mul4(C, Pout, T); }

// ---------------------------------------------------------------- which earlier pass the live search compares with
// Trail21 (5 Nov evening): a tracking loss left a 48-frame scrap as the newest pass, and every search compared the new
// pass with that scrap only, so a pass started over the earlier, larger passes was never found. The search now rotates
// over the most recent visible passes (newest first). candidates: indexes of visible passes with a search reference,
// newest first; try: the search attempt number since the pass started (0 = first).
inline int searchTarget(const std::vector<int>& candidates, int tryNo) { if (candidates.empty()) return -1; int n = (int)std::min<size_t>(candidates.size(), 3); return candidates[(tryNo < 0 ? 0 : tryNo) % n]; }

// ---------------------------------------------------------------- pause/resume segments (C6)
// One entry per recorded segment, in the order the SDK returns the scans.
struct SegmentPose { bool found; double T[4][4]; };

}  // namespace logic
