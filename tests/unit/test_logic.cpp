// UNIT tests for C:\Artec\sdk-fps\rec_logic.h (pure decision rules; no SDK, no window).
// Each test names the review finding it guards (C:\Artec\docs\artecrec-review-2026-11-05.md + the 48-item page).
// Tests marked "FINDING" fail on purpose when the rule is wrong; they are not to be "fixed" by changing the test.
#include "../testfw.h"
#include "rec_logic.h"
#include <random>
#define NOMINMAX
#include <windows.h>   // only for WideCharToMultiByte as an independent UTF-8 oracle
using namespace logic;

// ---------------------------------------------------------------- helpers
static bool validUtf8(const std::string& s) {   // strict: no overlong forms, no surrogates, <= U+10FFFF
  size_t i = 0, n = s.size(); const unsigned char* p = (const unsigned char*)s.data();
  while (i < n) { unsigned c = p[i];
    if (c < 0x80) { i++; continue; }
    int len = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0; if (!len || i + len > n) return false;
    unsigned cp = c & (len == 2 ? 0x1F : len == 3 ? 0x0F : 0x07);
    for (int k = 1; k < len; k++) { if ((p[i + k] & 0xC0) != 0x80) return false; cp = (cp << 6) | (p[i + k] & 0x3F); }
    if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    i += len; }
  return true;
}
static std::string oracleUtf8(const std::wstring& w) { if (w.empty()) return ""; int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr); std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr); return s; }
static std::wstring fromUtf8(const std::string& s) { if (s.empty()) return L""; int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0); if (n <= 0) return L"<invalid>"; std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n); return w; }
// unescape the 4 entities xmlAttr writes
static std::string xmlUnescape(const std::string& v) { std::string o; for (size_t i = 0; i < v.size(); i++) { if (v[i] == '&') { const char* e[] = {"&amp;", "&quot;", "&lt;", "&gt;", "&apos;"}; const char r[] = {'&', '"', '<', '>', '\''}; bool hit = false; for (int k = 0; k < 5; k++) if (v.compare(i, strlen(e[k]), e[k]) == 0) { o += r[k]; i += strlen(e[k]) - 1; hit = true; break; } if (!hit) return "<bad entity>"; } else o += v[i]; } return o; }
// minimal well-formedness check of an XML attribute value body: no raw '<', '"', every '&' starts a known entity, no control chars
static bool attrBodyOk(const std::string& v) { for (size_t i = 0; i < v.size(); i++) { unsigned char c = v[i]; if (c == '<' || c == '"' || (c < 0x20)) return false;
    if (c == '&') { bool ok = false; for (const char* e : {"&amp;", "&quot;", "&lt;", "&gt;", "&apos;"}) if (v.compare(i, strlen(e), e) == 0) ok = true; if (!ok) return false; } } return validUtf8(v); }
// Name="..." values of <Scan elements, in order
static std::vector<std::string> scanNames(const std::string& xml) { std::vector<std::string> out; size_t p = 0; while ((p = xml.find("<Scan ", p)) != std::string::npos) { size_t e = xml.find("/>", p); size_t nm = xml.find(" Name=\"", p); if (nm == std::string::npos || nm > e) out.push_back("<none>"); else { size_t q = xml.find('"', nm + 7); out.push_back(xml.substr(nm + 7, q - nm - 7)); } p = e; } return out; }

static const wchar_t* KANNADA_BELUR = L"\x0CAC\x0CC7\x0CB2\x0CC2\x0CB0\x0CC1";           // ಬೇಲೂರು
static const wchar_t* KANNADA_SHASANA = L"\x0CB6\x0CBE\x0CB8\x0CA8 \x0C95\x0CB2\x0CCD\x0CB2\x0CC1 3";  // ಶಾಸನ ಕಲ್ಲು 3

// ================================================================= saveTarget / stemOf  (A1, F9)
TEST(save_nested_by_default, "A1") {
  CHECK_EQ(saveTarget(L"D:\\Hoysala\\Belur.sproj"), std::wstring(L"D:\\Hoysala\\Belur\\Belur.sproj"));
  CHECK_EQ(saveTarget(L"C:\\Users\\team\\Desktop\\stone.sproj"), std::wstring(L"C:\\Users\\team\\Desktop\\stone\\stone.sproj"));
  CHECK_EQ(saveTarget(L"D:\\Belur.sproj"), std::wstring(L"D:\\Belur\\Belur.sproj"));   // drive root
}
TEST(save_folder_suffix_is_not_the_project_folder, "A1") {
  // old bug: a folder whose name merely ENDS with the project name became the project folder and was swapped out
  CHECK_EQ(saveTarget(L"D:\\Hoysala\\OldBelur\\Belur.sproj"), std::wstring(L"D:\\Hoysala\\OldBelur\\Belur\\Belur.sproj"));
  CHECK_EQ(saveTarget(L"D:\\Hoysala\\Belur 2\\Belur.sproj"), std::wstring(L"D:\\Hoysala\\Belur 2\\Belur\\Belur.sproj"));
  CHECK_EQ(saveTarget(L"D:\\Hoysala\\NotBelur\\belur.sproj"), std::wstring(L"D:\\Hoysala\\NotBelur\\belur\\belur.sproj"));
}
TEST(save_resave_inside_own_folder, "A1") {
  CHECK_EQ(saveTarget(L"D:\\Hoysala\\Belur\\Belur.sproj"), std::wstring(L"D:\\Hoysala\\Belur\\Belur.sproj"));
  CHECK_EQ(saveTarget(L"D:\\Hoysala\\BELUR\\belur.SPROJ"), std::wstring(L"D:\\Hoysala\\BELUR\\belur.sproj"));   // case-insensitive leaf match
  CHECK_EQ(saveTarget(L"D:\\Hoysala\\Belur\\Belur\\Belur.sproj"), std::wstring(L"D:\\Hoysala\\Belur\\Belur\\Belur.sproj"));
}
TEST(save_belur_into_belur_only_when_own_folder, "A1 A2") {
  // "Belur" saved into D:\Hoysala\Belur: the target is that folder itself; replacing it must then be allowed only when
  // the folder is this project's own (marker / project files only). A folder holding other projects and photos is refused.
  std::wstring t = saveTarget(L"D:\\Hoysala\\Belur\\Belur.sproj"); std::wstring stem = stemOf(t);
  CHECK_EQ(t, std::wstring(L"D:\\Hoysala\\Belur\\Belur.sproj"));
  std::vector<FolderEntry> own = {{L"Belur.sproj", false}, {L"Scans", true}, {L"artecrec-groups.txt", false}, {L"artecrec-project.txt", false}};
  std::vector<FolderEntry> shared = {{L"Belur.sproj", false}, {L"Scans", true}, {L"Halebidu", true}, {L"IMG_0412.JPG", false}};
  CHECK_EQ(canReplaceFolder(true, own, stem, true, false), Swap_Ok);
  CHECK_EQ(canReplaceFolder(true, shared, stem, true, false), Swap_Foreign);
  CHECK_EQ(canReplaceFolder(true, shared, stem, false, false), Swap_Foreign);
  NOTE("design: a non-own D:\\Hoysala\\Belur is REFUSED (user must pick another name), not nested to Belur\\Belur\\Belur.sproj");
}
TEST(save_name_with_dots_keeps_full_name, "F9") {
  // the dialog does not append .sproj to "Hoysala 10.11" (Windows sees ".11" as an extension); the old code cut 6 chars
  CHECK_EQ(stemOf(L"D:\\Hoysala 10.11"), std::wstring(L"Hoysala 10.11"));
  CHECK_EQ(saveTarget(L"D:\\Trip\\Hoysala 10.11"), std::wstring(L"D:\\Trip\\Hoysala 10.11\\Hoysala 10.11.sproj"));
  CHECK_EQ(saveTarget(L"D:\\Trip\\Hoysala 10.11.sproj"), std::wstring(L"D:\\Trip\\Hoysala 10.11\\Hoysala 10.11.sproj"));
  CHECK_EQ(saveTarget(L"D:\\Trip\\v1.2.3"), std::wstring(L"D:\\Trip\\v1.2.3\\v1.2.3.sproj"));
  CHECK_EQ(saveTarget(L"D:\\Trip\\stone"), std::wstring(L"D:\\Trip\\stone\\stone.sproj"));   // no extension at all
  CHECK_EQ(stemOf(L"a.sproj.sproj"), std::wstring(L"a.sproj"));
  CHECK_EQ(stemOf(L"D:\\x\\Belur.SProj"), std::wstring(L"Belur"));
  CHECK_EQ(stemOf(L"D:/x/Belur.sproj"), std::wstring(L"Belur"));   // forward slashes
}
TEST(save_trailing_dots_and_spaces_removed, "F9") {
  CHECK_EQ(stemOf(L"D:\\x\\stone .sproj"), std::wstring(L"stone"));
  CHECK_EQ(stemOf(L"D:\\x\\stone..sproj"), std::wstring(L"stone"));
  CHECK_EQ(stemOf(L"D:\\x\\stone. . "), std::wstring(L"stone"));
  CHECK_EQ(saveTarget(L"D:\\x\\Belur\\Belur. .sproj"), std::wstring(L"D:\\x\\Belur\\Belur.sproj"));
  CHECK_EQ(stemOf(L" lead"), std::wstring(L" lead"));   // leading space is legal in Windows names: kept
}
TEST(save_invalid_names_refused, "F9") {
  const wchar_t* bad[] = {L"D:\\x\\.sproj", L"D:\\x\\   .sproj", L"D:\\x\\...", L"D:\\x\\", L"Belur.sproj", L"", L"D:\\x\\a<b.sproj", L"D:\\x\\a>b", L"D:\\x\\a:b.sproj",
                          L"D:\\x\\a\"b.sproj", L"D:\\x\\a|b.sproj", L"D:\\x\\a?b.sproj", L"D:\\x\\a*b.sproj"};
  for (auto b : bad) CHECK_MSG(saveTarget(b).empty(), tfw::str(b));
}
TEST(save_kannada_names, "A5 F9") {
  std::wstring k = KANNADA_BELUR; std::wstring t = saveTarget(L"D:\\Hoysala\\" + k + L".sproj");
  CHECK_EQ(t, L"D:\\Hoysala\\" + k + L"\\" + k + L".sproj");
  CHECK_EQ(saveTarget(L"D:\\Hoysala\\" + k + L"\\" + k + L".sproj"), L"D:\\Hoysala\\" + k + L"\\" + k + L".sproj");
}
TEST(save_target_is_stable, "A1") {   // saving the result again must give the same path (re-save never nests deeper)
  const wchar_t* in[] = {L"D:\\Hoysala\\Belur.sproj", L"D:\\a\\Hoysala 10.11", L"C:\\x\\OldBelur\\Belur.sproj", L"D:\\x\\stone ..sproj"};
  for (auto p : in) { std::wstring t = saveTarget(p); CHECK_MSG(!t.empty(), tfw::str(p)); CHECK_EQ(saveTarget(t), t); CHECK(iEndsWith(t, L".sproj")); CHECK(iEquals(leafOf(folderOf(t)), stemOf(t))); }
}
TEST(save_reserved_device_names, "F9") {
  // Windows cannot create a folder called CON, NUL, AUX, PRN, COM1..9, LPT1..9 (also with an extension).
  int accepted = 0; const wchar_t* dev[] = {L"D:\\x\\CON.sproj", L"D:\\x\\nul", L"D:\\x\\Aux.sproj", L"D:\\x\\COM1.sproj", L"D:\\x\\lpt1"};
  for (auto d : dev) if (!saveTarget(d).empty()) accepted++;
  if (accepted) NOTE(std::to_string(accepted) + "/5 reserved device names accepted by saveTarget (the Save dialog normally rejects them first; low risk)");
}

// ================================================================= canReplaceFolder (A1, A2)
TEST(replace_new_and_empty_folder, "A1") {
  CHECK_EQ(canReplaceFolder(false, {}, L"Belur", false, false), Swap_New);
  CHECK_EQ(canReplaceFolder(false, {{L"x.jpg", false}}, L"Belur", false, false), Swap_New);
  CHECK_EQ(canReplaceFolder(true, {}, L"Belur", false, false), Swap_Ok);
  CHECK_EQ(canReplaceFolder(true, {}, L"Belur", false, true), Swap_Ok);
}
TEST(replace_own_project_folder, "A1") {
  std::vector<FolderEntry> e = {{L"Belur.sproj", false}, {L"Scans", true}, {L"artecrec-groups.txt", false}, {L"artecrec-project.txt", false}};
  CHECK_EQ(canReplaceFolder(true, e, L"Belur", true, false), Swap_Ok);
  CHECK_EQ(canReplaceFolder(true, e, L"belur", true, false), Swap_Ok);   // case-insensitive
  CHECK_EQ(canReplaceFolder(true, e, L"Belur", false, false), Swap_NotOurs);   // same files but no marker
}
TEST(replace_refuses_foreign_files, "A1") {
  std::vector<std::vector<FolderEntry>> bad = {
    {{L"Belur.sproj", false}, {L"Scans", true}, {L"IMG_0412.JPG", false}},
    {{L"Belur.sproj", false}, {L"Scans", true}, {L"Halebidu", true}},
    {{L"Halebidu.sproj", false}, {L"Scans", true}},            // another project's .sproj
    {{L"notes.docx", false}},
    {{L"Belur.sproj.bak", false}, {L"Belur.sproj", false}},
    {{L"Belur.sproj", false}, {L"Thumbs.db", false}},           // Explorer leftovers also block (safe side)
  };
  for (auto& e : bad) { CHECK_EQ(canReplaceFolder(true, e, L"Belur", true, false), Swap_Foreign); CHECK_EQ(canReplaceFolder(true, e, L"Belur", false, false), Swap_Foreign); }
}
TEST(replace_tmp_leftovers_of_atomic_write, "A1 A5") {
  // an interrupted atomic write leaves "<file>.tmp"; it belongs to the project and must not block a re-save
  CHECK(isProjectFile(L"Trail14.sproj.tmp", L"Trail14")); CHECK(isProjectFile(L"artecrec-groups.txt.tmp", L"Trail14")); CHECK(isProjectFile(L"artecrec-project.txt.TMP", L"Trail14"));
  CHECK(!isProjectFile(L"IMG_0412.JPG.tmp", L"Trail14")); CHECK(!isProjectFile(L"Other.sproj.tmp", L"Trail14")); CHECK(!isProjectFile(L".tmp", L"Trail14")); CHECK(!isProjectFile(L"Trail14.tmp", L"Trail14"));
  std::vector<FolderEntry> e = {{L"Trail14.sproj", false}, {L"Trail14.sproj.tmp", false}, {L"Scans", true}, {L"artecrec-groups.txt.tmp", false}, {L"artecrec-project.txt", false}};
  CHECK_EQ(canReplaceFolder(true, e, L"Trail14", true, false), Swap_Ok);
  e.push_back({L"notes.docx.tmp", false}); CHECK_EQ(canReplaceFolder(true, e, L"Trail14", true, false), Swap_Foreign);   // Word's temp file of a foreign document
}
TEST(replace_unlisted_folder_refused, "A1") {
  // listedOk=false: the folder could not be listed (access denied, network drive): nothing is known, so never swap it
  std::vector<FolderEntry> own = {{L"Belur.sproj", false}, {L"Scans", true}, {L"artecrec-project.txt", false}};
  CHECK_EQ(canReplaceFolder(true, own, L"Belur", true, false, false), Swap_Foreign);
  CHECK_EQ(canReplaceFolder(true, {}, L"Belur", true, false, false), Swap_Foreign);   // an "empty" listing that failed is not an empty folder
  CHECK_EQ(canReplaceFolder(true, {}, L"Belur", false, true, false), Swap_Foreign);
  CHECK_EQ(canReplaceFolder(false, {}, L"Belur", false, false, false), Swap_New);
  CHECK_EQ(canReplaceFolder(true, own, L"Belur", true, false, true), Swap_Ok);   // default true keeps the old behaviour
  CHECK_EQ(canReplaceFolder(true, own, L"Belur", true, false), Swap_Ok);
}
TEST(replace_refuses_studio_project_with_other_entries, "A2") {
  std::vector<FolderEntry> e = {{L"Belur.sproj", false}, {L"Scans", true}};
  CHECK_EQ(canReplaceFolder(true, e, L"Belur", true, true), Swap_Foreign);
  CHECK_EQ(canReplaceFolder(true, e, L"Belur", false, true), Swap_Foreign);
  std::vector<FolderEntry> studio = {{L"Belur.sproj", false}, {L"Scans", true}, {L"Models", true}, {L"Data", true}, {L"Thumbnails", true}};
  CHECK(canReplaceFolder(true, studio, L"Belur", false, false) != Swap_Ok);   // a Studio folder without our marker is never swapped
}
TEST(replace_refuses_folder_with_studio_models, "A2 FINDING") {
  // ArtecRec writes only <stem>.sproj, Scans\ and its two text files (see integration test save_writes_only_project_files).
  // A Models\ (fusions, textured meshes) or Data\ folder can only come from Studio; ArtecRec does not carry it over, so a
  // swap loses it. Case: project saved by ArtecRec (marker present), fused in Studio 17, then Save As onto it from a
  // NEW session (the flag "opened from Studio" is false because it was not opened in this session).
  std::vector<FolderEntry> e = {{L"Belur.sproj", false}, {L"Scans", true}, {L"Models", true}, {L"artecrec-project.txt", false}};
  CHECK_MSG(canReplaceFolder(true, e, L"Belur", true, false) != Swap_Ok, "isProjectFile() counts Models/Data/Thumbnails as project files: the swap would delete Studio fusions");
}

// ================================================================= utf8 / xmlAttr / rewriteScanNames (A5)
TEST(utf8_matches_windows_encoder, "A5") {
  std::vector<std::wstring> v = {L"", L"Spider Scan 1", L"caf\x00E9", KANNADA_BELUR, KANNADA_SHASANA, L"\x20AC 5", L"\xD83D\xDE00", L"a\xD800\xDC00z", L"\xDBFF\xDFFF", L"\x07FF\x0800\xFFFD"};
  for (auto& w : v) { std::string u = utf8(w); CHECK_EQ(u, oracleUtf8(w)); CHECK(validUtf8(u)); CHECK_EQ(fromUtf8(u), w); }
}
TEST(utf8_kannada_round_trip, "A5") {
  std::wstring k = KANNADA_SHASANA; std::string u = utf8(k); CHECK(validUtf8(u)); CHECK_EQ(fromUtf8(u), k);
  CHECK_EQ(u.substr(0, 3), std::string("\xE0\xB2\xB6"));   // U+0CB6
}
TEST(utf8_lone_surrogates_stay_valid, "A5 FINDING") {
  // A name with a broken surrogate (pasted text) must still give valid UTF-8, otherwise the .sproj is not well-formed XML.
  std::vector<std::wstring> v = {L"a\xD800", L"\xDC00z", L"\xD83Dx", std::wstring(1, (wchar_t)0xDE00)};
  for (auto& w : v) CHECK_MSG(validUtf8(utf8(w)), "utf8(" + tfw::esc(w) + ") = " + tfw::esc(utf8(w)) + " encodes a surrogate (invalid UTF-8)");
}
TEST(xml_attr_escapes, "A5") {
  CHECK_EQ(xmlAttr(L"a&b"), std::string("a&amp;b")); CHECK_EQ(xmlAttr(L"\"q\""), std::string("&quot;q&quot;")); CHECK_EQ(xmlAttr(L"<x>"), std::string("&lt;x&gt;"));
  CHECK_EQ(xmlAttr(L"tab\there\nnl"), std::string("tab here nl")); CHECK_EQ(xmlAttr(L"O'Neil"), std::string("O'Neil"));
  std::wstring all = L"G1 - <&\"> \x0CAC\x0CC7\x0CB2\x0CC2\x0CB0\x0CC1 \x01\x1F\x7F";
  std::string a = xmlAttr(all); CHECK(attrBodyOk(a));
  std::wstring back = fromUtf8(xmlUnescape(a)); CHECK_EQ(back, std::wstring(L"G1 - <&\"> \x0CAC\x0CC7\x0CB2\x0CC2\x0CB0\x0CC1   \x7F"));
}
static const char* SPROJ =
  "<?xml version=\"1.0\"?>\n<ArtecProject Version=\"2\" Name=\"Trail14\" Uuid=\"{B38B}\" Rev=\"1\">\n\t<Scans Count=\"3\">\n"
  "\t\t<Scan Uuid=\"{62C0}\" Name=\"Scan 0\" Items=\"87\" LoadMode=\"scan\" Selected=\"false\" Size_kb=\"0\" DataType=\"0\" />\n"
  "\t\t<Scan Uuid=\"{B336}\" Name=\"Scan 1\" Items=\"131\" LoadMode=\"scan\" Selected=\"false\" Size_kb=\"0\" DataType=\"0\" />\n"
  "\t\t<Scan Uuid=\"{139A}\" Name=\"Scan 2\" Items=\"161\" LoadMode=\"scan\" Selected=\"false\" Size_kb=\"0\" DataType=\"0\" />\n\t</Scans>\n</ArtecProject>\n";
TEST(rewrite_names_in_order, "A5 D5") {
  std::string x = SPROJ; int n = rewriteScanNames(x, {L"Group 1 - Spider Scan 1", L"Group 1 - Spider Scan 2", L"Spider Scan 3"});
  CHECK_EQ(n, 3); auto nm = scanNames(x); CHECK_EQ(nm.size(), (size_t)3);
  if (nm.size() == 3) { CHECK_EQ(nm[0], std::string("Group 1 - Spider Scan 1")); CHECK_EQ(nm[2], std::string("Spider Scan 3")); }
  CHECK(x.find("<Scans Count=\"3\">") != std::string::npos); CHECK(x.find("Name=\"Trail14\"") != std::string::npos);   // project name untouched
  CHECK(x.find("Items=\"161\"") != std::string::npos);
}
TEST(rewrite_names_kannada_well_formed, "A5") {
  std::string x = SPROJ; std::wstring k1 = std::wstring(KANNADA_BELUR) + L" - " + KANNADA_SHASANA, k2 = L"\"<&>\"";
  CHECK_EQ(rewriteScanNames(x, {k1, k2, std::wstring(KANNADA_BELUR) + L" 3"}), 3);
  CHECK(validUtf8(x)); auto nm = scanNames(x);
  if (nm.size() == 3) { for (auto& v : nm) CHECK(attrBodyOk(v)); CHECK_EQ(fromUtf8(xmlUnescape(nm[0])), k1); CHECK_EQ(fromUtf8(xmlUnescape(nm[1])), k2); }
  // a second rewrite (re-save) over the escaped values must work too
  CHECK_EQ(rewriteScanNames(x, {L"A", L"B", L"C"}), 3); nm = scanNames(x); if (nm.size() == 3) CHECK_EQ(nm[1], std::string("B"));
}
TEST(rewrite_names_partial_and_extra, "A5 D5") {
  std::string x = SPROJ; CHECK_EQ(rewriteScanNames(x, {L"", L"Two"}), 1); auto nm = scanNames(x);
  if (nm.size() == 3) { CHECK_EQ(nm[0], std::string("Scan 0")); CHECK_EQ(nm[1], std::string("Two")); CHECK_EQ(nm[2], std::string("Scan 2")); }
  x = SPROJ; CHECK_EQ(rewriteScanNames(x, {L"a", L"b", L"c", L"d", L"e"}), 3);
  x = SPROJ; CHECK_EQ(rewriteScanNames(x, {}), 0); CHECK_EQ(x, std::string(SPROJ));
  std::string none = "<ArtecProject><Scans Count=\"0\"></Scans></ArtecProject>"; CHECK_EQ(rewriteScanNames(none, {L"a"}), 0);
}
TEST(rewrite_names_scan_without_name_keeps_order, "D5") {
  std::string x = "<Scans><Scan Uuid=\"1\" Items=\"3\" /><Scan Uuid=\"2\" Name=\"old\" /></Scans>";
  CHECK_EQ(rewriteScanNames(x, {L"first", L"second"}), 1); auto nm = scanNames(x); if (nm.size() == 2) CHECK_EQ(nm[1], std::string("second"));
}
TEST(rewrite_names_other_attr_ending_in_Name, "A5 FINDING") {
  // find("Name=\"") also matches the tail of another attribute such as DisplayName="..." placed before Name
  std::string x = "<Scans><Scan Uuid=\"1\" DisplayName=\"keep\" Name=\"Scan 0\" /></Scans>";
  rewriteScanNames(x, {L"new"});
  CHECK_MSG(x.find("DisplayName=\"keep\"") != std::string::npos && x.find(" Name=\"new\"") != std::string::npos, "got: " + x);
}
TEST(rewrite_names_value_with_raw_gt, "A5 FINDING") {
  // '>' is legal unescaped inside an XML attribute; another writer (Studio) may leave it raw. The element end is then
  // taken at that '>', and the rewrite stops for this and every later scan.
  std::string x = "<Scans><Scan Uuid=\"1\" Name=\"a>b\" /><Scan Uuid=\"2\" Name=\"Scan 1\" /></Scans>";
  int n = rewriteScanNames(x, {L"one", L"two"});
  CHECK_MSG(n == 2, "rewrote " + std::to_string(n) + " of 2: " + x);
}

// ================================================================= acceptJoin / planeRms (D1)
TEST(join_share_and_median_limits, "D1") {
  CHECK(!acceptJoin(0.29f, 0.5f, false, 5).ok); CHECK(acceptJoin(0.30f, 0.5f, false, 5).ok); CHECK(acceptJoin(1.0f, 0.0f, false, 5).ok);
  CHECK(!acceptJoin(0.9f, 2.51f, false, 5).ok); CHECK(acceptJoin(0.9f, 2.5f, false, 5).ok); CHECK(!acceptJoin(0.0f, 9.f, false, 5).ok);
  CHECK(strlen(acceptJoin(0.1f, 0.5f, false, 5).why) > 0); CHECK(strlen(acceptJoin(0.9f, 0.5f, false, 5).why) == 0);
}
TEST(join_geometry_only_refuses_flat_window, "D1") {
  // threshold 0.6 mm (lead, 5 Nov): geometry-only placement needs relief; texture joins are not affected
  CHECK(!acceptJoin(0.95f, 0.3f, true, 0.0f).ok); CHECK(!acceptJoin(0.95f, 0.3f, true, 0.59f).ok);
  CHECK(acceptJoin(0.95f, 0.3f, true, 0.6f).ok); CHECK(acceptJoin(0.95f, 0.3f, true, 3.0f).ok);
  CHECK(acceptJoin(0.95f, 0.3f, false, 0.0f).ok);   // texture join on a flat stone: allowed
  CHECK(strstr(acceptJoin(0.95f, 0.3f, true, 0.1f).why, "flat") != nullptr);
  CHECK(!acceptJoin(0.1f, 0.3f, true, 3.0f).ok);    // share still applies to geometry joins
}
TEST(join_gate_nan_fails_closed, "D1 FINDING") {
  float nan = std::nanf("");
  CHECK_MSG(!acceptJoin(nan, 0.5f, false, 5).ok, "NaN share accepted");
  CHECK_MSG(!acceptJoin(0.9f, nan, false, 5).ok, "NaN median accepted");
  CHECK_MSG(!acceptJoin(0.9f, 0.5f, true, nan).ok, "NaN flatness accepted for a geometry-only join");
}
// Jacobi eigenvalues as an independent oracle for planeRms
static double minEigJacobi(std::vector<float> p) { size_t n = p.size() / 3; double m[3] = {0, 0, 0}; for (size_t i = 0; i < n; i++) for (int k = 0; k < 3; k++) m[k] += p[i * 3 + k]; for (double& v : m) v /= n;
  double A[3][3] = {}; for (size_t i = 0; i < n; i++) for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) A[a][b] += (p[i * 3 + a] - m[a]) * (p[i * 3 + b] - m[b]); for (auto& r : A) for (double& v : r) v /= n;
  for (int sweep = 0; sweep < 60; sweep++) for (int a = 0; a < 3; a++) for (int b = a + 1; b < 3; b++) { if (std::fabs(A[a][b]) < 1e-300) continue; double th = 0.5 * std::atan2(2 * A[a][b], A[b][b] - A[a][a]), c = std::cos(th), s = std::sin(th);
      for (int k = 0; k < 3; k++) { double x = A[k][a], y = A[k][b]; A[k][a] = c * x - s * y; A[k][b] = s * x + c * y; } for (int k = 0; k < 3; k++) { double x = A[a][k], y = A[b][k]; A[a][k] = c * x - s * y; A[b][k] = s * x + c * y; } }
  return std::sqrt(std::max(0.0, std::min(A[0][0], std::min(A[1][1], A[2][2])))); }
static void rot(double ax, double ay, double az, float& x, float& y, float& z) { double cx = cos(ax), sx = sin(ax), cy = cos(ay), sy = sin(ay), cz = cos(az), sz = sin(az);
  double X = x, Y = y * cx - z * sx, Z = y * sx + z * cx; double X2 = X * cy + Z * sy, Z2 = -X * sy + Z * cy; x = (float)(X2 * cz - Y * sz); y = (float)(X2 * sz + Y * cz); z = (float)Z2; }
TEST(plane_rms_flat_tilted_relief, "D1") {
  std::mt19937 rng(7); std::uniform_real_distribution<float> U(-20, 20);
  std::vector<float> flat, tilted, relief, noisy;
  for (int i = 0; i < 4000; i++) { float x = U(rng), y = U(rng);
    flat.insert(flat.end(), {x, y, 0.f});
    float a = x, b = y, c = 0; rot(0.4, -0.7, 1.1, a, b, c); tilted.insert(tilted.end(), {a + 5000.f, b - 3000.f, c + 250.f});   // far from the origin
    relief.insert(relief.end(), {x, y, 1.0f * sinf(x * 0.6283f)});   // 1 mm carving amplitude, 10 mm wavelength -> rms 0.707
    noisy.insert(noisy.end(), {x, y, 0.3f * (U(rng) / 20.f)}); }       // uniform +-0.3 mm -> rms 0.173
  CHECK_NEAR(planeRms(flat), 0.0, 1e-3); CHECK_NEAR(planeRms(tilted), 0.0, 0.01); CHECK_NEAR(planeRms(relief), 0.7071, 0.02); CHECK_NEAR(planeRms(noisy), 0.1732, 0.01);
  CHECK(!acceptJoin(0.95f, 0.2f, true, planeRms(tilted)).ok); CHECK(acceptJoin(0.95f, 0.2f, true, planeRms(relief)).ok);
}
TEST(plane_rms_degenerate_inputs, "D1") {
  CHECK_EQ(planeRms({}), 0.0f); CHECK_EQ(planeRms({1, 2, 3, 4, 5, 6}), 0.0f); CHECK_EQ(planeRms({1, 2, 3, 4, 5, 6, 7}), 0.0f);
  std::vector<float> same; for (int i = 0; i < 100; i++) same.insert(same.end(), {3.f, 4.f, 5.f}); CHECK_NEAR(planeRms(same), 0, 1e-6);
  std::vector<float> line; for (int i = 0; i < 100; i++) line.insert(line.end(), {(float)i, 2.f * i, -1.f * i}); CHECK_NEAR(planeRms(line), 0, 1e-3);
  std::vector<float> cube; std::mt19937 rng(3); std::uniform_real_distribution<float> U(-1, 1); for (int i = 0; i < 20000; i++) cube.insert(cube.end(), {U(rng), U(rng), U(rng)});
  CHECK_NEAR(planeRms(cube), minEigJacobi(cube), 2e-3);   // isotropic: nearly equal eigenvalues (closed form's hard case)
  CHECK(std::isfinite(planeRms(cube)));
}
TEST(plane_rms_matches_jacobi_random, "D1") {
  std::mt19937 rng(11); std::uniform_real_distribution<float> U(-1, 1); double worst = 0;
  for (int t = 0; t < 300; t++) { std::vector<float> p; float sx = 1 + 30 * (U(rng) + 1), sy = 1 + 30 * (U(rng) + 1), sz = 3 * (U(rng) + 1); double ax = U(rng) * 3, ay = U(rng) * 3, az = U(rng) * 3;
    for (int i = 0; i < 500; i++) { float x = U(rng) * sx, y = U(rng) * sy, z = U(rng) * sz; rot(ax, ay, az, x, y, z); p.insert(p.end(), {x + 100.f, y, z - 300.f}); }
    double a = planeRms(p), b = minEigJacobi(p); worst = std::max(worst, std::fabs(a - b)); CHECK_MSG(std::fabs(a - b) < 1e-3 + 1e-3 * b, "case " + std::to_string(t) + ": " + tfw::str(a) + " vs " + tfw::str(b)); }
  NOTE("worst |planeRms - Jacobi| over 300 random clouds: " + tfw::str(worst) + " mm");
}

// ================================================================= acceptRefine / rotationDeg (D2)
static void Raxis(double ax, double ay, double az, double deg, double R[3][3]) { double n = sqrt(ax * ax + ay * ay + az * az); ax /= n; ay /= n; az /= n; double t = deg * 3.14159265358979 / 180, c = cos(t), s = sin(t), C = 1 - c;
  double M[3][3] = {{c + ax * ax * C, ax * ay * C - az * s, ax * az * C + ay * s}, {ay * ax * C + az * s, c + ay * ay * C, ay * az * C - ax * s}, {az * ax * C - ay * s, az * ay * C + ax * s, c + az * az * C}}; memcpy(R, M, sizeof M); }
TEST(refine_limits, "D2") {
  CHECK(acceptRefine(0, 0, 1).ok); CHECK(acceptRefine(6.6, 0.5, 0.8).ok);   // pass 7 on 5 Nov moved 6.6 mm
  // matched-share limit 0.10 (lead, 5 Nov: share of the whole pass; strips overlap 20-40 %)
  CHECK(acceptRefine(8.0, 2.0, 0.10).ok); CHECK(!acceptRefine(8.01, 0, 1).ok); CHECK(!acceptRefine(0, 2.01, 1).ok); CHECK(!acceptRefine(0, 0, 0.09).ok); CHECK(acceptRefine(0, 0, 0.25).ok); CHECK(!acceptRefine(0, 0, 0).ok);
  CHECK(!acceptRefine(21, 0, 1).ok);   // 10-21 mm misplacements seen on Trail12 must not be "refined" silently
  CHECK(strstr(acceptRefine(9, 0, 1).why, "8 mm") != nullptr); CHECK(strstr(acceptRefine(0, 3, 1).why, "2 degrees") != nullptr);
}
TEST(refine_nan_fails_closed, "D2 FINDING") {
  double nan = std::nan("");
  CHECK_MSG(!acceptRefine(nan, 0, 1).ok, "NaN move accepted"); CHECK_MSG(!acceptRefine(0, nan, 1).ok, "NaN rotation accepted"); CHECK_MSG(!acceptRefine(0, 0, nan).ok, "NaN share accepted");
}
TEST(rotation_deg, "D2") {
  double I[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; CHECK_NEAR(rotationDeg(I), 0, 1e-9);
  double angles[] = {0.01, 0.5, 1.0, 1.99, 2.0, 2.01, 10, 90, 179.9, 180}; double R[3][3];
  for (double a : angles) { Raxis(0.3, -0.5, 0.8, a, R); CHECK_NEAR(rotationDeg(R), a, a < 1 ? 1e-4 : 1e-6); }
  double J[3][3] = {{1 + 1e-9, 0, 0}, {0, 1 + 1e-9, 0}, {0, 0, 1 + 1e-9}}; CHECK_NEAR(rotationDeg(J), 0, 1e-6);   // trace > 3 by rounding: clamped
  Raxis(1, 1, 1, 2.5, R); CHECK(!acceptRefine(1, rotationDeg(R), 1).ok); Raxis(1, 0, 0, 1.5, R); CHECK(acceptRefine(1, rotationDeg(R), 1).ok);
}

// ================================================================= allowed() (C1, F1, F7, B3)
static const char* MODE_N[] = {"Idle", "Preview", "Record", "Paused", "Stopping", "Aligning", "Busy"};
static const char* ACT_N[] = {"EditScans", "Save", "Open", "New", "AutoAlign", "Countdown", "Preview", "Record", "Close"};
// Expected table, written from the review's intent. Rows: action; columns: mode Idle..Busy. Each cell 4 chars for
// (busyPost,stuck) = (0,0) (0,1) (1,0) (1,1). '1' allowed, '0' refused, '?' not asserted (open design question).
static const char* EXPECT[9][7] = {
  /*EditScans*/ {"1100", "0000", "0000", "0000", "0000", "0000", "0000"},
  /*Save     */ {"1111", "0000", "0000", "0000", "0000", "0000", "0000"},
  /*Open     */ {"1100", "0000", "0000", "0000", "0000", "0000", "0000"},
  /*New      */ {"1100", "0000", "0000", "0000", "0000", "0000", "0000"},
  /*AutoAlign*/ {"1100", "0000", "0000", "0000", "0000", "0000", "0000"},
  /*Countdown*/ {"1010", "1010", "0000", "0000", "0000", "0000", "0000"},
  /*Preview  */ {"1010", "0000", "0000", "1010", "0000", "0000", "0000"},
  /*Record   */ {"1010", "1010", "1010", "1010", "0000", "0000", "0000"},
  /*Close    */ {"1111", "0101", "0101", "0101", "0101", "0101", "????"},
};
TEST(allowed_full_table, "C1 F1 F7 B3") {
  int checked = 0;
  for (int a = 0; a < 9; a++) for (int m = 0; m < 7; m++) for (int bp = 0; bp < 2; bp++) for (int st = 0; st < 2; st++) {
    char e = EXPECT[a][m][st + 2 * bp]; if (e == '?') continue; bool got = allowed((Action)a, m, bp != 0, st != 0); checked++;
    CHECK_MSG(got == (e == '1'), std::string(ACT_N[a]) + " in " + MODE_N[m] + " busyPost=" + std::to_string(bp) + " stuck=" + std::to_string(st) + ": got " + (got ? "allowed" : "refused")); }
  NOTE(std::to_string(checked) + " cells checked; Close is refused while Busy (decided 5 Nov)");
}
TEST(allowed_invariants, "C1 B3") {
  for (int m = 0; m < 7; m++) for (int bp = 0; bp < 2; bp++) {
    CHECK_MSG(allowed(A_Close, m, bp, true), std::string("no way out when stuck in ") + MODE_N[m]);                        // B3
    for (Action a : {A_Preview, A_Record, A_Countdown}) CHECK(!allowed(a, m, bp, true));                                   // no new recording on an abandoned job
    if (m != M_Idle) for (Action a : {A_EditScans, A_Save, A_Open, A_New, A_AutoAlign}) CHECK_MSG(!allowed(a, m, bp, false), std::string(ACT_N[a]) + " in " + MODE_N[m]);   // C1
    if (bp) for (Action a : {A_EditScans, A_Open, A_New, A_AutoAlign}) CHECK(!allowed(a, m, true, false));                // C1/C2/C3
  }
  CHECK(allowed(A_Save, M_Idle, true, false));    // Save while tidying: queued (A3)
  CHECK(allowed(A_Record, M_Idle, true, false));  // the next pass may start while the last one is tidied
  CHECK(!allowed((Action)99, M_Idle, false, false)); CHECK(!allowed(A_Save, 42, false, false)); CHECK(allowed(A_Close, 42, false, true));
}

// ================================================================= mayRestartFresh (B5), clampRange (F4), memLevel (E1)
TEST(restart_fresh_cap, "B5") {
  for (int n = 0; n < 5; n++) CHECK(mayRestartFresh(n, false, false));
  CHECK(!mayRestartFresh(5, false, false)); CHECK(!mayRestartFresh(100, false, false));
  for (int n = 0; n < 6; n++) { CHECK(!mayRestartFresh(n, true, false)); CHECK(!mayRestartFresh(n, false, true)); CHECK(!mayRestartFresh(n, true, true)); }
}
TEST(clamp_range_invariants, "F4") {
  int a = 170, b = 330; clampRange(a, b); CHECK_EQ(a, 170); CHECK_EQ(b, 330);
  a = 100; b = 500; clampRange(a, b); CHECK_EQ(a, 150); CHECK_EQ(b, 350);
  a = 330; b = 330; clampRange(a, b); CHECK_EQ(a, 310); CHECK_EQ(b, 330);
  a = 330; b = 170; clampRange(a, b); CHECK_EQ(a, 150); CHECK_EQ(b, 170);
  int bad = 0;
  for (int n = -500; n <= 1000; n += 7) for (int f = -500; f <= 1000; f += 7) { int x = n, y = f; clampRange(x, y);
    bool ok = x >= 150 && x <= 330 && y >= 170 && y <= 350 && x <= y - 20; int x2 = x, y2 = y; clampRange(x2, y2); ok = ok && x2 == x && y2 == y;
    if (!ok && bad++ < 3) CHECK_MSG(false, "clampRange(" + std::to_string(n) + "," + std::to_string(f) + ") -> " + std::to_string(x) + "," + std::to_string(y)); }
  CHECK_EQ(bad, 0);
  NOTE("far limit is 350 mm (decided 5 Nov)");
}
TEST(mem_levels, "E1") {
  CHECK_EQ(memLevel(0), Mem_Refuse); CHECK_EQ(memLevel(5.99), Mem_Refuse); CHECK_EQ(memLevel(6), Mem_Warn); CHECK_EQ(memLevel(15.99), Mem_Warn); CHECK_EQ(memLevel(16), Mem_Ok); CHECK_EQ(memLevel(64), Mem_Ok); CHECK_EQ(memLevel(-1), Mem_Refuse);
}

// ================================================================= live pose composition (outside review, 6 Nov)
static void rigid(double ax, double ay, double az, double ang, double tx, double ty, double tz, double M[4][4]) {
  double n = sqrt(ax * ax + ay * ay + az * az); ax /= n; ay /= n; az /= n; double c = cos(ang), s = sin(ang), t = 1 - c;
  double R[3][3] = {{t * ax * ax + c, t * ax * ay - s * az, t * ax * az + s * ay}, {t * ax * ay + s * az, t * ay * ay + c, t * ay * az - s * ax}, {t * ax * az - s * ay, t * ay * az + s * ax, t * az * az + c}};
  for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) M[i][j] = R[i][j]; } M[0][3] = tx; M[1][3] = ty; M[2][3] = tz; M[3][0] = M[3][1] = M[3][2] = 0; M[3][3] = 1; }
static double maxDiff(const double A[4][4], const double B[4][4]) { double d = 0; for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) d = std::max(d, fabs(A[i][j] - B[i][j])); return d; }
TEST(live_pose_keeps_reference_fixed_rotated_reference, "D1 outside-review") {
  // The SDK may move the reference too. Whatever rigid motion G it applies to both (Rout = G*Rin, Pout = G*P), the probe's
  // placement relative to the unmoved reference must be P: T = Rin*inv(Rout)*Pout = P. The old order inv(Rout)*Rin*Pout
  // gives P only when G commutes with Rin.
  std::mt19937 rng(7); std::uniform_real_distribution<double> U(-1, 1); int bad = 0; double worstOld = 0;
  for (int it = 0; it < 200; it++) {
    double Rin[4][4], P[4][4], G[4][4], Rout[4][4], Pout[4][4], T[4][4];
    rigid(U(rng), U(rng), U(rng) + 2, U(rng) * 2, U(rng) * 400, U(rng) * 400, U(rng) * 400, Rin);   // a rotated, shifted reference
    rigid(U(rng), U(rng) + 2, U(rng), U(rng), U(rng) * 300, U(rng) * 300, U(rng) * 300, P);
    rigid(U(rng) + 2, U(rng), U(rng), U(rng), U(rng) * 200, U(rng) * 200, U(rng) * 200, G);
    mul4(G, Rin, Rout); mul4(G, P, Pout);
    livePose(Rin, Rout, Pout, T); if (maxDiff(T, P) > 1e-6) bad++;
    double inv[4][4], tmp[4][4], Old[4][4]; invRigid4(Rout, inv); mul4(inv, Rin, tmp); mul4(tmp, Pout, Old); worstOld = std::max(worstOld, maxDiff(Old, P)); }
  CHECK_EQ(bad, 0);
  NOTE("old order inv(Rout)*Rin*Pout: worst error over 200 random cases " + std::to_string(worstOld) + " (matrix units, mm in the translation)");
  CHECK(worstOld > 1.0);   // the regression this guards: the old order is really wrong when the reference moves
}
TEST(live_pose_reference_unmoved_equals_probe_pose, "D1") {
  double Rin[4][4], P[4][4], T[4][4]; rigid(0.3, 0.2, 1, 0.7, 120, -40, 300, Rin); rigid(1, 0, 0.2, -0.4, 10, 20, 30, P);
  livePose(Rin, Rin, P, T); CHECK(maxDiff(T, P) < 1e-9);
}
// ================================================================= search target rotation (Trail21, 5 Nov evening)
TEST(search_target_rotates_over_three_newest, "D1 Trail21") {
  std::vector<int> c = {6, 5, 3, 1};   // newest first
  CHECK_EQ(searchTarget(c, 0), 6); CHECK_EQ(searchTarget(c, 1), 5); CHECK_EQ(searchTarget(c, 2), 3); CHECK_EQ(searchTarget(c, 3), 6); CHECK_EQ(searchTarget(c, 4), 5);
  CHECK_EQ(searchTarget({}, 0), -1); CHECK_EQ(searchTarget({2}, 5), 2); CHECK_EQ(searchTarget({4, 2}, 3), 2); CHECK_EQ(searchTarget(c, -3), 6);
}

// ================================================================= Workspace sort (Round 4)
TEST(sort_is_strict_weak_ordering_with_equal_keys, "R4-01") {
  // equal keys, different names: the old descending rule returned true both ways
  std::vector<SortKey> k = {{L"Scan 1", 0.5, 100, 10, 0}, {L"Scan 2", 0.5, 100, 10, 1}, {L"scan 1", 0.7, 90, 12, 2}, {L"B", 0.5, 100, 8, 3}};
  int bad = 0;
  for (int col : {0, 3, 4, 6, 9}) for (bool d : {false, true}) for (auto& a : k) { if (sortLess(col, d, a, a)) bad++; for (auto& b : k) { if (sortLess(col, d, a, b) && sortLess(col, d, b, a)) bad++;
        for (auto& c : k) if (sortLess(col, d, a, b) && sortLess(col, d, b, c) && !sortLess(col, d, a, c)) bad++; } }   // irreflexive, asymmetric, transitive
  CHECK_EQ(bad, 0);
}
TEST(sort_descending_reverses_and_keeps_ties_stable, "R4-01") {
  std::vector<SortKey> k = {{L"a", 1, 5, 0, 0}, {L"b", 2, 7, 0, 1}, {L"c", 2, 7, 0, 2}, {L"d", 3, 1, 0, 3}};
  std::vector<int> o = {0, 1, 2, 3}; std::stable_sort(o.begin(), o.end(), [&](int a, int b) { return sortLess(4, true, k[a], k[b]); });
  CHECK(o == (std::vector<int>{1, 2, 0, 3}));   // frames 7,7,5,1; the two 7s keep their order
  o = {0, 1, 2, 3}; std::stable_sort(o.begin(), o.end(), [&](int a, int b) { return sortLess(0, false, k[a], k[b]); }); CHECK(o == (std::vector<int>{0, 1, 2, 3}));
  CHECK(sortLess(0, false, {L"alpha", 0, 0, 0, 0}, {L"Beta", 0, 0, 0, 1}));   // case-insensitive names
}
TEST(sort_third_click_restores_initial_order, "R4-04") {
  std::vector<int> initial = {10, 20, 30, 40}; std::vector<int> current = {40, 10, 50, 30, 20};   // sorted, plus scan 50 recorded since
  std::vector<int> pos = restoreOrder(current, initial); std::vector<int> got; for (int p : pos) got.push_back(current[p]);
  CHECK(got == (std::vector<int>{10, 20, 30, 40, 50}));
  std::vector<int> deleted = {40, 10}; pos = restoreOrder(deleted, initial); got.clear(); for (int p : pos) got.push_back(deleted[p]); CHECK(got == (std::vector<int>{10, 40}));
}

// ================================================================= autosave manifest (6 Nov)
TEST(autosave_recoverable_after_last_save, "autosave") {
  std::vector<std::wstring> L = {L"P\t1\tC:\\a\\pass-1-1\tSpider Scan 1\n", L"P\t2\tC:\\a\\pass-2-1\tSpider Scan 2\n", L"S\t20261106-101500\n", L"P\t3\tC:\\a\\pass-3-1\tSpider Scan 3\r\n", L"P\t3\tC:\\a\\pass-3-2\tSpider Scan 3\n", L"P\t4\tC:\\a\\pass-4-1\t\x0CAC\x0CC7\n"};
  auto r = recoverablePasses(L); CHECK_EQ((int)r.size(), 2);
  if (r.size() == 2) { CHECK_EQ(r[0].id, 3); CHECK(r[0].folder == L"C:\\a\\pass-3-2"); CHECK(r[1].name == L"\x0CAC\x0CC7"); }
}
TEST(autosave_nothing_after_save_or_discard, "autosave") {
  CHECK(recoverablePasses({L"P\t1\tC:\\y\tA\n", L"S\t1\n"}).empty()); CHECK(recoverablePasses({L"P\t1\tC:\\y\tA\n", L"X\t1\n"}).empty());
  CHECK(recoverablePasses({}).empty()); CHECK(recoverablePasses({L"\n", L"garbage\n", L"P\t1\n"}).empty());   // malformed lines are ignored
  CHECK(autosaveDiskOk(8.0)); CHECK(!autosaveDiskOk(7.9));
}

int main(int argc, char** argv) { return tfw::run(argc, argv, "unit-logic"); }
