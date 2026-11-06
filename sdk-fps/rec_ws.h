// ArtecRec — Workspace panel following Studio 17's "Workspace and More": columns (eye, Name, Color, Type, Error,
// Frames, Scanner type, Texture frames), groups, multi-selection, Space/Ctrl+A/Ctrl+D/Ctrl+Alt+A, F2 rename,
// Del delete, Ctrl+G group, Duplicate, Show/Hide, Change color, column sorting; Properties panel below.
#pragma once
#include <commctrl.h>
#include <commdlg.h>

namespace ws {
static HWND hwnd, list, props, title, ptitle; static HFONT font, fontB; static bool refreshing = false; static int sortCol = -1, sortDir = 0; static LRESULT nres = 0;
enum { CM_SELALL = 3001, CM_DESELALL, CM_INVERT, CM_GROUP, CM_UNGROUP, CM_RENAME, CM_DUP, CM_SHOW, CM_HIDE, CM_COLOR, CM_DELETE };
static std::function<void()> onChanged;
// Review C1: editing the scan list (delete, duplicate, group, rename, sort) only when nothing records, aligns, saves or
// tidies scans: background work holds scan numbers and objects. Visibility ticks stay allowed, as in Studio.
static bool editOk() { if (logic::allowed(logic::A_EditScans, g_mode.load(), !postIdle(), g_stuck)) return true; notice(L"Wait until recording and processing have finished, then try again."); return false; }
#define WM_WSSYNC (WM_APP + 4)

static LPARAM paramAt(int row) { LVITEMW it = {LVIF_PARAM}; it.iItem = row; ListView_GetItem(list, &it); return it.lParam; }
static int scanAt(int row) { LPARAM p = paramAt(row); return p > 0 ? (int)p - 1 : -1; }
static int groupAt(int row) { LPARAM p = paramAt(row); return p < 0 ? (int)(-p) - 1 : -1; }
static int rowOfScan(int si) { int n = ListView_GetItemCount(list); for (int r = 0; r < n; r++) if (scanAt(r) == si) return r; return -1; }
static std::vector<int> highlightedScans() { std::vector<int> v; int r = -1; while ((r = ListView_GetNextItem(list, r, LVNI_SELECTED)) >= 0) { int si = scanAt(r); if (si >= 0) v.push_back(si); else { int gi = groupAt(r); if (gi >= 0) for (int i = 0; i < (int)g_scans.size(); i++) if (g_scans[i].group == gi) v.push_back(i); } } return v; }

static void addRow(int& row, const std::wstring& name, LPARAM lp, bool checked, const wchar_t* type, const wchar_t* err, const wchar_t* frames, const wchar_t* scanner, const wchar_t* tex) {
  LVITEMW it = {LVIF_TEXT | LVIF_PARAM}; it.iItem = row; it.pszText = (LPWSTR)name.c_str(); it.lParam = lp; ListView_InsertItem(list, &it);
  ListView_SetCheckState(list, row, checked); ListView_SetItemText(list, row, 1, (LPWSTR)L"■"); ListView_SetItemText(list, row, 2, (LPWSTR)type); ListView_SetItemText(list, row, 3, (LPWSTR)err); ListView_SetItemText(list, row, 4, (LPWSTR)frames); ListView_SetItemText(list, row, 5, (LPWSTR)scanner); ListView_SetItemText(list, row, 6, (LPWSTR)tex); row++;
}
static void refresh() {
  refreshing = true; int sel = view::selectedScan; ListView_DeleteAllItems(list); int row = 0; wchar_t e[32], f[32], t[32];
  auto scanRow = [&](int i, bool indent) { auto& s = g_scans[i]; swprintf(e, 32, L"%.2f", s.maxErr); swprintf(f, 32, L"%d", s.frames); swprintf(t, 32, L"%d", s.texFrames); addRow(row, (indent ? L"      " : L"") + s.name, i + 1, s.visible, L"Scan", e, f, L"Spider", t); };
  for (int g = 0; g < (int)g_groups.size(); g++) { int n = 0; for (auto& s : g_scans) if (s.group == g) n++; swprintf(f, 32, L"%d", n); addRow(row, g_groups[g].name, -(g + 1), g_groups[g].visible, L"Group", L"", f, L"", L""); for (int i = 0; i < (int)g_scans.size(); i++) if (g_scans[i].group == g) scanRow(i, true); }
  for (int i = 0; i < (int)g_scans.size(); i++) if (g_scans[i].group < 0) scanRow(i, false);
  int r = rowOfScan(sel); if (r >= 0) ListView_SetItemState(list, r, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); refreshing = false;
}
static void showProps() {
  int i = view::selectedScan; wchar_t b[1024];
  if (i < 0 || i >= (int)g_scans.size()) swprintf(b, 1024, L"Project\r\n\r\nScans: %d\r\nGroups: %d\r\nFile: %s", (int)g_scans.size(), (int)g_groups.size(), g_projPath.empty() ? L"(unsaved)" : g_projPath.substr(g_projPath.find_last_of(L"\\/") + 1).c_str());
  else { auto& s = g_scans[i]; swprintf(b, 1024, L"%s\r\n\r\nType: Scan   Scanner: Artec Space Spider\r\nFrames: %d\r\nTexture frames: %d\r\nKey frames: %d\r\nMax registration error: %.3f mm\r\nGroup: %s\r\nShown: %s", s.name.c_str(), s.frames, s.texFrames, s.keyFrames, s.maxErr, s.group >= 0 ? g_groups[s.group].name.c_str() : L"—", s.visible ? L"yes" : L"no"); }
  SetWindowTextW(props, b);
}
static void layout() { RECT r; GetClientRect(hwnd, &r); MoveWindow(title, 10, 8, r.right - 20, 22, TRUE); int ph = 190; MoveWindow(list, 6, 34, r.right - 12, r.bottom - 34 - ph - 10, TRUE); MoveWindow(ptitle, 10, r.bottom - ph, r.right - 20, 22, TRUE); MoveWindow(props, 10, r.bottom - ph + 26, r.right - 20, ph - 30, TRUE); }
static void changed() { rebuildMaster(); g_dirty = true; refresh(); showProps(); if (onChanged) onChanged(); }

static void syncVisibleFromList() {   // read every eye tick into the model
  int n = ListView_GetItemCount(list); { std::lock_guard<std::mutex> l(g_scanMx); for (int r = 0; r < n; r++) { bool on = ListView_GetCheckState(list, r) != 0; int si = scanAt(r), gi = groupAt(r); if (si >= 0 && si < (int)g_scans.size()) g_scans[si].visible = on; else if (gi >= 0 && gi < (int)g_groups.size()) g_groups[gi].visible = on; } }
  g_dirty = true; showProps(); if (onChanged) onChanged();
}
// Round 4 (R4-02/R4-03): visibility is saved (groups sidecar) -> project modified; written under the scan lock, which
// background work holds when it reads it. Colour is display-only and not saved -> not "modified".
static void setAllVisible(bool v) { { std::lock_guard<std::mutex> l(g_scanMx); for (auto& s : g_scans) s.visible = v; for (auto& g : g_groups) g.visible = v; } g_dirty = true; refresh(); showProps(); if (onChanged) onChanged(); }
static void invert() { { std::lock_guard<std::mutex> l(g_scanMx); for (auto& s : g_scans) s.visible = !s.visible; } g_dirty = true; refresh(); showProps(); if (onChanged) onChanged(); }
static void toggleHighlighted() { std::vector<int> hs = highlightedScans(); { std::lock_guard<std::mutex> l(g_scanMx); for (int si : hs) if (si < (int)g_scans.size()) g_scans[si].visible = !g_scans[si].visible; } g_dirty = true; refresh(); showProps(); if (onChanged) onChanged(); }
static void showHighlighted(bool v) { std::vector<int> hs = highlightedScans(); { std::lock_guard<std::mutex> l(g_scanMx); for (int si : hs) if (si < (int)g_scans.size()) g_scans[si].visible = v; } g_dirty = true; refresh(); showProps(); if (onChanged) onChanged(); }
static void onlyThis(int si) { { std::lock_guard<std::mutex> l(g_scanMx); for (auto& s : g_scans) s.visible = false; if (si >= 0 && si < (int)g_scans.size()) g_scans[si].visible = true; } g_dirty = true; refresh(); showProps(); if (onChanged) onChanged(); }
static void deleteSelected(HWND owner) {
  if (!editOk()) return;
  std::vector<int> v = highlightedScans(); int r = ListView_GetNextItem(list, -1, LVNI_SELECTED); int gi = r >= 0 ? groupAt(r) : -1; if (v.empty() && gi < 0) return;
  wchar_t m[256]; if (v.size() == 1 && gi < 0) swprintf(m, 256, L"Delete \"%s\"?", g_scans[v[0]].name.c_str()); else swprintf(m, 256, L"Delete %d scan(s)?", (int)v.size()); if (MessageBoxW(owner, m, L"ArtecRec", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
  std::sort(v.begin(), v.end()); { std::lock_guard<std::mutex> l(g_scanMx); for (int k = (int)v.size() - 1; k >= 0; k--) { if (g_scans[v[k]].scan) g_scans[v[k]].scan->release(); if (g_scans[v[k]].ref) g_scans[v[k]].ref->release(); g_scans.erase(g_scans.begin() + v[k]); }
    if (gi >= 0) { g_groups.erase(g_groups.begin() + gi); for (auto& s : g_scans) { if (s.group == gi) s.group = -1; else if (s.group > gi) s.group--; } } }
  view::selectedScan = -1; changed();
}
static void groupSelected(HWND owner) {   // Studio: select required objects → Group
  if (!editOk()) return;
  std::vector<int> v = highlightedScans(); if (v.empty()) { MessageBoxW(owner, L"Highlight the scans to group first (click names, Ctrl+click for several).", L"ArtecRec", MB_ICONINFORMATION); return; }
  GroupRec g; wchar_t nm[64]; swprintf(nm, 64, L"Group %d", (int)g_groups.size() + 1); g.name = nm; { std::lock_guard<std::mutex> l(g_scanMx); g_groups.push_back(g); int gi = (int)g_groups.size() - 1; for (int si : v) g_scans[si].group = gi; } g_dirty = true; refresh(); showProps();   // review C5: under the lock
}
static void ungroup() { if (!editOk()) return; int r = ListView_GetNextItem(list, -1, LVNI_SELECTED); if (r < 0) return; int gi = groupAt(r); int si = scanAt(r); if (si >= 0) gi = g_scans[si].group; if (gi < 0) return;
  { std::lock_guard<std::mutex> l(g_scanMx); for (auto& s : g_scans) if (s.group == gi) s.group = -1; g_groups.erase(g_groups.begin() + gi); for (auto& s : g_scans) if (s.group > gi) s.group--; } g_dirty = true; refresh(); showProps(); }
static void duplicateSelected() { if (!editOk()) return; std::vector<int> v = highlightedScans(); std::sort(v.begin(), v.end()); for (int k = (int)v.size() - 1; k >= 0; k--) duplicateScan(v[k]); changed(); }
static void changeColor(HWND owner) {
  std::vector<int> v = highlightedScans(); int r = ListView_GetNextItem(list, -1, LVNI_SELECTED); int gi = r >= 0 ? groupAt(r) : -1; if (v.empty()) return;
  static COLORREF cust[16]; CHOOSECOLORW cc = {sizeof cc}; cc.hwndOwner = owner; cc.lpCustColors = cust; auto& c0 = g_scans[v[0]].color; cc.rgbResult = RGB((int)(c0[0] * 255), (int)(c0[1] * 255), (int)(c0[2] * 255)); cc.Flags = CC_RGBINIT | CC_FULLOPEN;
  if (!ChooseColorW(&cc)) return; for (int si : v) { auto& c = g_scans[si].color; c[0] = GetRValue(cc.rgbResult) / 255.f; c[1] = GetGValue(cc.rgbResult) / 255.f; c[2] = GetBValue(cc.rgbResult) / 255.f; } (void)gi; InvalidateRect(list, nullptr, TRUE);
}
static void rename() { if (!editOk()) return; int r = ListView_GetNextItem(list, -1, LVNI_SELECTED); if (r >= 0) { SetFocus(list); ListView_EditLabel(list, r); } }
static void sortBy(int col) {   // Studio: click a header → ascending → descending → initial
  if (!editOk()) return;
  static std::vector<IScan*> initialOrder;   // the order before the first sort, by scan object (R4-04)
  if (sortCol != col) { if (sortDir == 0) { initialOrder.clear(); for (auto& r : g_scans) initialOrder.push_back(r.scan); } sortCol = col; sortDir = 1; } else sortDir = sortDir == 1 ? 2 : sortDir == 2 ? 0 : 1;
  if (sortDir == 1 && initialOrder.empty()) for (auto& r : g_scans) initialOrder.push_back(r.scan);
  std::vector<int> order;
  if (sortDir) { std::vector<logic::SortKey> keys; for (int i = 0; i < (int)g_scans.size(); i++) keys.push_back({g_scans[i].name, (double)g_scans[i].maxErr, g_scans[i].frames, g_scans[i].texFrames, i});
    order.resize(g_scans.size()); for (int i = 0; i < (int)order.size(); i++) order[i] = i;
    bool desc = sortDir == 2; std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return logic::sortLess(col, desc, keys[a], keys[b]); }); }   // R4-01: strict weak ordering
  else { std::vector<IScan*> cur; for (auto& r : g_scans) cur.push_back(r.scan); order = logic::restoreOrder(cur, initialOrder); initialOrder.clear(); sortCol = -1; }   // R4-04: back to the order before sorting
  { std::lock_guard<std::mutex> l(g_scanMx); std::vector<ScanRec> ns; for (int i : order) ns.push_back(std::move(g_scans[i])); g_scans = std::move(ns); } /* review C2: moved under the lock */ g_dirty = true; /* the scan order is saved */ view::selectedScan = -1; rebuildMaster(); refresh(); showProps();
}
static void contextMenu(HWND owner, int x, int y) {
  HMENU m = CreatePopupMenu(); std::vector<int> v = highlightedScans(); bool any = !v.empty(); int r = ListView_GetNextItem(list, -1, LVNI_SELECTED); bool grp = r >= 0 && groupAt(r) >= 0;
  AppendMenuW(m, MF_STRING, CM_SELALL, L"Select all\tCtrl+A"); AppendMenuW(m, MF_STRING, CM_DESELALL, L"Deselect all\tCtrl+D"); AppendMenuW(m, MF_STRING, CM_INVERT, L"Invert selection\tCtrl+Alt+A"); AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING | (any ? 0 : MF_GRAYED), CM_GROUP, L"Group\tCtrl+G"); AppendMenuW(m, MF_STRING | (any || grp ? 0 : MF_GRAYED), CM_UNGROUP, L"Ungroup"); AppendMenuW(m, MF_STRING | (any || grp ? 0 : MF_GRAYED), CM_RENAME, L"Rename\tF2"); AppendMenuW(m, MF_STRING | (any ? 0 : MF_GRAYED), CM_DUP, L"Duplicate"); AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING | (any ? 0 : MF_GRAYED), CM_SHOW, L"Show"); AppendMenuW(m, MF_STRING | (any ? 0 : MF_GRAYED), CM_HIDE, L"Hide"); AppendMenuW(m, MF_STRING | (any ? 0 : MF_GRAYED), CM_COLOR, L"Change color…"); AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING | (any || grp ? 0 : MF_GRAYED), CM_DELETE, L"Delete\tDel");
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, 0, owner, nullptr); DestroyMenu(m);
  switch (cmd) { case CM_SELALL: setAllVisible(true); break; case CM_DESELALL: setAllVisible(false); break; case CM_INVERT: invert(); break; case CM_GROUP: groupSelected(owner); break; case CM_UNGROUP: ungroup(); break; case CM_RENAME: rename(); break; case CM_DUP: duplicateSelected(); break;
    case CM_SHOW: showHighlighted(true); break; case CM_HIDE: showHighlighted(false); break; case CM_COLOR: changeColor(owner); break; case CM_DELETE: deleteSelected(owner); break; }
}
static bool notify(HWND owner, NMHDR* n) {
  if (n->hwndFrom != list) return false; nres = 0;
  switch (n->code) {
  case LVN_ITEMCHANGED: { if (refreshing) return true; auto* p = (NMLISTVIEW*)n;
    if ((p->uNewState ^ p->uOldState) & LVIS_STATEIMAGEMASK) { bool on = ((p->uNewState & LVIS_STATEIMAGEMASK) >> 12) == 2; int si = scanAt(p->iItem), gi = groupAt(p->iItem); { std::lock_guard<std::mutex> l(g_scanMx); if (si >= 0 && si < (int)g_scans.size()) g_scans[si].visible = on; else if (gi >= 0 && gi < (int)g_groups.size()) g_groups[gi].visible = on; } g_dirty = true; showProps(); if (onChanged) onChanged(); }
    if ((p->uNewState & LVIS_SELECTED) && !(p->uOldState & LVIS_SELECTED)) { view::selectedScan = scanAt(p->iItem); showProps(); } return true; }
  case NM_CLICK: { auto* p = (NMITEMACTIVATE*)n; bool ctrl = GetKeyState(VK_CONTROL) < 0, alt = GetKeyState(VK_MENU) < 0; if (p->iItem >= 0 && ((ctrl && alt) || (ctrl && p->iSubItem == 0 && p->ptAction.x < 20))) { int si = scanAt(p->iItem); if (si >= 0) onlyThis(si); } return true; }
  case LVN_COLUMNCLICK: { auto* p = (NMLISTVIEW*)n; sortBy(p->iSubItem); return true; }
  case LVN_ENDLABELEDITW: { auto* p = (NMLVDISPINFOW*)n; if (!p->item.pszText || !*p->item.pszText) return true; std::wstring t = p->item.pszText; while (!t.empty() && t.front() == L' ') t.erase(t.begin()); if (t.size() > 200) { t.resize(200); notice(L"Names are limited to 200 characters; the name was shortened."); }   /* DeepSeek 6 */ int si = scanAt(p->item.iItem), gi = groupAt(p->item.iItem);
    if (!editOk()) { refresh(); return true; } { std::lock_guard<std::mutex> l(g_scanMx); if (si >= 0) setScanName(g_scans[si], t); else if (gi >= 0) g_groups[gi].name = t; } g_dirty = true; refresh(); showProps(); return true; }
  case LVN_KEYDOWN: { auto* p = (NMLVKEYDOWN*)n; bool ctrl = GetKeyState(VK_CONTROL) < 0, alt = GetKeyState(VK_MENU) < 0;
    if (p->wVKey == VK_DELETE) { deleteSelected(owner); return true; } if (p->wVKey == VK_F2) { rename(); return true; } if (p->wVKey == VK_SPACE) { toggleHighlighted(); return true; }
    if (p->wVKey == 'A' && ctrl && alt) { invert(); return true; } if (p->wVKey == 'A' && ctrl) { setAllVisible(true); return true; } if (p->wVKey == 'D' && ctrl) { setAllVisible(false); return true; } if (p->wVKey == 'G' && ctrl) { groupSelected(owner); return true; } return false; }
  case NM_RCLICK: { POINT pt; GetCursorPos(&pt); contextMenu(owner, pt.x, pt.y); return true; }
  case NM_CUSTOMDRAW: { auto* cd = (NMLVCUSTOMDRAW*)n; nres = CDRF_DODEFAULT; switch (cd->nmcd.dwDrawStage) { case CDDS_PREPAINT: nres = CDRF_NOTIFYITEMDRAW; break; case CDDS_ITEMPREPAINT: nres = CDRF_NOTIFYSUBITEMDRAW; break;
      case CDDS_ITEMPREPAINT | CDDS_SUBITEM: { int si = scanAt((int)cd->nmcd.dwItemSpec); if (cd->iSubItem == 1 && si >= 0) { auto& c = g_scans[si].color; cd->clrText = RGB((int)(c[0] * 255), (int)(c[1] * 255), (int)(c[2] * 255)); } else cd->clrText = RGB(0, 0, 0); nres = CDRF_NEWFONT; break; } }
    return true; }
  }
  return false;
}
static void create(HWND parent, HFONT f, HFONT fb) {
  font = f; fontB = fb; WNDCLASSW wc = {}; wc.lpfnWndProc = [](HWND h, UINT u, WPARAM wp, LPARAM lp) -> LRESULT { if (u == WM_SIZE) { layout(); return 0; } if (u == WM_NOTIFY || u == WM_COMMAND) return SendMessageW(GetParent(h), u, wp, lp); if (u == WM_CTLCOLORSTATIC) { SetBkColor((HDC)wp, RGB(255, 255, 255)); return (LRESULT)GetStockObject(WHITE_BRUSH); } return DefWindowProcW(h, u, wp, lp); };
  wc.hInstance = GetModuleHandle(nullptr); wc.lpszClassName = L"ArtecRecWs"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH); RegisterClassW(&wc);
  hwnd = CreateWindowW(L"ArtecRecWs", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 10, 10, parent, nullptr, wc.hInstance, nullptr);
  title = CreateWindowW(L"STATIC", L"Workspace", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hwnd, nullptr, wc.hInstance, nullptr); SendMessageW(title, WM_SETFONT, (WPARAM)fb, TRUE);
  list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_EDITLABELS, 0, 0, 10, 10, hwnd, nullptr, wc.hInstance, nullptr);
  ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP); SendMessageW(list, WM_SETFONT, (WPARAM)f, TRUE);
  const wchar_t* cols[] = {L"Name", L"Color", L"Type", L"Error", L"Frames", L"Scanner", L"Texture frames"}; int widths[] = {150, 44, 50, 52, 56, 60, 60};
  for (int i = 0; i < 7; i++) { LVCOLUMNW c = {LVCF_TEXT | LVCF_WIDTH}; c.pszText = (LPWSTR)cols[i]; c.cx = widths[i]; ListView_InsertColumn(list, i, &c); }
  ptitle = CreateWindowW(L"STATIC", L"Properties", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hwnd, nullptr, wc.hInstance, nullptr); SendMessageW(ptitle, WM_SETFONT, (WPARAM)fb, TRUE);
  props = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10, hwnd, nullptr, wc.hInstance, nullptr); SendMessageW(props, WM_SETFONT, (WPARAM)f, TRUE);
  showProps();
}
}  // namespace ws
