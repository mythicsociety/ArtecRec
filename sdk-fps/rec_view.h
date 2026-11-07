// ArtecRec — 3D view, following Artec Studio 17's "Viewing Scans and Models" chapter: LMB rotate, MMB or LMB+RMB
// move, RMB drag / wheel zoom, double-click sets the rotation centre (purple marker), F fit, H home, G grid,
// L lighting, 5 projection, standard viewpoints, render/shading/backface/colour modes, range meter, screenshots.
#pragma once
#include <windowsx.h>
#include <gl/GL.h>
#include <algorithm>
#include <objidl.h>
#include <gdiplus.h>
#ifndef GL_BGR_EXT
#define GL_BGR_EXT 0x80E0
#endif

namespace view {
static HWND hwnd; static HDC dc; static HGLRC rc; static GLuint fontBase, fontMid, fontBig;
static float target[3] = {0, 0, 0}, yaw = 0.0f, pitch = 0.0f, dist = 600; static bool ortho = false, rotSet = false;
static float homeT[3] = {0, 0, 0}, homeYaw = 0, homePitch = 0, homeDist = 600;
enum { R_SOLID, R_WIRE, R_POINTS, R_WIRESOLID, R_POINTSSOLID };   // Studio's render modes; "points and solid" = scans as points, default for Spider
static int renderMode = R_SOLID; static bool smooth = true; static int backface = 3 /*0 show 1 cull 2 black 3 transparent*/; static int colorMode = 0 /*0 texture 1 scan colour 2 surface colour*/;
// Vertex buffer objects (GL 1.5 entry points fetched at run time): stored frames are uploaded to the GPU once instead of
// being copied from client memory on every redraw — the client-array path pulled the capture rate down to 1.5–3.5 fps
// once a project held several thousand frames (2 Nov, 13:00–14:00).
#define GL_ARRAY_BUFFER_ 0x8892
#define GL_ELEMENT_ARRAY_BUFFER_ 0x8893
#define GL_STATIC_DRAW_ 0x88E4
typedef void (APIENTRY* PFN_GENBUF)(GLsizei, GLuint*); typedef void (APIENTRY* PFN_BINDBUF)(GLenum, GLuint); typedef void (APIENTRY* PFN_BUFDATA)(GLenum, ptrdiff_t, const void*, GLenum); typedef void (APIENTRY* PFN_BUFSUB)(GLenum, ptrdiff_t, ptrdiff_t, const void*); typedef void (APIENTRY* PFN_DELBUF)(GLsizei, const GLuint*);
static PFN_GENBUF pGenBuffers; static PFN_BINDBUF pBindBuffer; static PFN_BUFDATA pBufferData; static PFN_BUFSUB pBufferSubData; static PFN_DELBUF pDeleteBuffers; static bool g_vboOk = false; static bool g_vbBound = false; static size_t g_vbUvOff = 0;
static void deleteTrashTextures() { std::lock_guard<std::mutex> l(g_texTrashMx); for (unsigned t : g_texTrash) glDeleteTextures(1, &t); g_texTrash.clear(); if (g_vboOk) for (unsigned b : g_bufTrash) pDeleteBuffers(1, &b); g_bufTrash.clear(); }
static bool useVbo(FrameMesh& m) {
  if (!g_vboOk || m.noVbo || m.v.empty()) return false;
  if (!m.vbo) { size_t bv = m.v.size() * 4, bn = m.n.size() * 4, bu = m.uv.size() * 4, bc = m.col.size() * 4; m.offN = bv; m.offUv = bv + bn; m.offCol = bv + bn + bu;
    pGenBuffers(1, &m.vbo); pBindBuffer(GL_ARRAY_BUFFER_, m.vbo); pBufferData(GL_ARRAY_BUFFER_, (ptrdiff_t)(bv + bn + bu + bc), nullptr, GL_STATIC_DRAW_);
    pBufferSubData(GL_ARRAY_BUFFER_, 0, (ptrdiff_t)bv, m.v.data()); if (bn) pBufferSubData(GL_ARRAY_BUFFER_, (ptrdiff_t)m.offN, (ptrdiff_t)bn, m.n.data()); if (bu) pBufferSubData(GL_ARRAY_BUFFER_, (ptrdiff_t)m.offUv, (ptrdiff_t)bu, m.uv.data()); if (bc) pBufferSubData(GL_ARRAY_BUFFER_, (ptrdiff_t)m.offCol, (ptrdiff_t)bc, m.col.data());
    if (!m.idx.empty()) { pGenBuffers(1, &m.ibo); pBindBuffer(GL_ELEMENT_ARRAY_BUFFER_, m.ibo); pBufferData(GL_ELEMENT_ARRAY_BUFFER_, (ptrdiff_t)(m.idx.size() * 4), m.idx.data(), GL_STATIC_DRAW_); } }
  else { pBindBuffer(GL_ARRAY_BUFFER_, m.vbo); if (m.ibo) pBindBuffer(GL_ELEMENT_ARRAY_BUFFER_, m.ibo); }
  g_vbBound = true; g_vbUvOff = m.offUv; return true;
}
static void endVbo() { if (g_vbBound) { pBindBuffer(GL_ARRAY_BUFFER_, 0); pBindBuffer(GL_ELEMENT_ARRAY_BUFFER_, 0); g_vbBound = false; } }
static bool bindTexture(FrameMesh& m) {
  if (m.texp && !m.uv.empty()) { GlTex& t = *m.texp; if (!t.id) { if (t.data.empty()) return false; glGenTextures(1, &t.id); glBindTexture(GL_TEXTURE_2D, t.id); glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, t.w, t.h, 0, GL_RGB, GL_UNSIGNED_BYTE, t.data.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP); std::vector<unsigned char>().swap(t.data); }
    else glBindTexture(GL_TEXTURE_2D, t.id); }
  else { if ((m.tex.empty() && !m.texId) || m.uv.empty()) return false;
    if (!m.texId) { glGenTextures(1, &m.texId); glBindTexture(GL_TEXTURE_2D, m.texId); glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m.texW, m.texH, 0, GL_RGB, GL_UNSIGNED_BYTE, m.tex.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP); std::vector<unsigned char>().swap(m.tex); }
    else glBindTexture(GL_TEXTURE_2D, m.texId); }
  glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); glEnableClientState(GL_TEXTURE_COORD_ARRAY); glTexCoordPointer(2, GL_FLOAT, 0, g_vbBound ? (const void*)g_vbUvOff : (const void*)m.uv.data()); return true;
}
static bool lighting = true, grid = false, follow = true, wheelToCursor = false, rangeMeter = true;
static bool dragL = false, dragM = false, dragR = false; static POINT last;
static int selectedScan = -1;
static double mv[16], pr[16]; static int vp[4];

static void init(HWND h) {
  hwnd = h; dc = GetDC(h); PIXELFORMATDESCRIPTOR pfd = {sizeof pfd, 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 24};
  pfd.cDepthBits = 24; int pf = ChoosePixelFormat(dc, &pfd); SetPixelFormat(dc, pf, &pfd); rc = wglCreateContext(dc); wglMakeCurrent(dc, rc);
  glEnable(GL_DEPTH_TEST); glEnableClientState(GL_VERTEX_ARRAY); glPointSize(2.0f); glEnable(GL_NORMALIZE);
  float amb[] = {0.25f, 0.25f, 0.25f, 1}, dif[] = {0.85f, 0.85f, 0.85f, 1}; glLightfv(GL_LIGHT0, GL_AMBIENT, amb); glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
  glEnable(GL_COLOR_MATERIAL); glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE); glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 1);
  pGenBuffers = (PFN_GENBUF)wglGetProcAddress("glGenBuffers"); pBindBuffer = (PFN_BINDBUF)wglGetProcAddress("glBindBuffer"); pBufferData = (PFN_BUFDATA)wglGetProcAddress("glBufferData"); pBufferSubData = (PFN_BUFSUB)wglGetProcAddress("glBufferSubData"); pDeleteBuffers = (PFN_DELBUF)wglGetProcAddress("glDeleteBuffers");
  g_vboOk = pGenBuffers && pBindBuffer && pBufferData && pBufferSubData && pDeleteBuffers; logf("OpenGL: %s, vertex buffers %s", (const char*)glGetString(GL_RENDERER), g_vboOk ? "on" : "unavailable (client arrays)");
  HFONT f = CreateFontW(uik(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI"); SelectObject(dc, f); fontBase = glGenLists(256); wglUseFontBitmapsW(dc, 0, 255, fontBase);
  // Big fonts for the full-screen scanning bar: the owner reads it from 4–5 feet while holding the scanner (2 Nov).
  HFONT fm = CreateFontW(uik(30), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI"); SelectObject(dc, fm); fontMid = glGenLists(256); wglUseFontBitmapsW(dc, 0, 255, fontMid);
  HFONT fh = CreateFontW(uik(54), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI"); SelectObject(dc, fh); fontBig = glGenLists(256); wglUseFontBitmapsW(dc, 0, 255, fontBig); SelectObject(dc, f);
}
static void text(float x, float y, const wchar_t* s) { glRasterPos2f(x, y); glListBase(fontBase); glCallLists((GLsizei)wcslen(s), GL_UNSIGNED_SHORT, s); }
static void textMid(float x, float y, const wchar_t* s) { glRasterPos2f(x, y); glListBase(fontMid); glCallLists((GLsizei)wcslen(s), GL_UNSIGNED_SHORT, s); }
static void textBig(float x, float y, const wchar_t* s) { glRasterPos2f(x, y); glListBase(fontBig); glCallLists((GLsizei)wcslen(s), GL_UNSIGNED_SHORT, s); }
static bool scanShown(const ScanRec& s) { return s.visible && (s.group < 0 || s.group >= (int)g_groups.size() || g_groups[s.group].visible); }   // Gemini 2: range-checked (a stale group index shows the scan instead of reading past the end)

static bool bounds(float lo[3], float hi[3]) {
  bool any = false; lo[0] = lo[1] = lo[2] = 1e9f; hi[0] = hi[1] = hi[2] = -1e9f;
  auto add = [&](const FrameMesh& m) { for (size_t i = 0; i + 2 < m.v.size(); i += 3 * 7) { for (int a = 0; a < 3; a++) { lo[a] = std::min(lo[a], m.v[i + a]); hi[a] = std::max(hi[a], m.v[i + a]); } any = true; } };
  int mode = g_mode.load();
  if (mode != M_Preview) { std::lock_guard<std::mutex> l(g_scanMx); for (auto& r : g_scans) if (scanShown(r)) for (auto& m : r.disp) add(m); }
  { std::lock_guard<std::mutex> l(g_mx); add(g_live); for (auto& m : g_liveAcc) add(m); }
  return any;
}
static void fit() { float lo[3], hi[3]; if (!bounds(lo, hi)) return; for (int a = 0; a < 3; a++) target[a] = (lo[a] + hi[a]) * 0.5f; float r = 0; for (int a = 0; a < 3; a++) r = std::max(r, hi[a] - lo[a]); dist = std::max(80.f, r * 1.3f); follow = false; }
static void home() { memcpy(target, homeT, sizeof target); yaw = homeYaw; pitch = homePitch; dist = homeDist; follow = true; rotSet = false; }
static void setHome() { memcpy(homeT, target, sizeof target); homeYaw = yaw; homePitch = pitch; homeDist = dist; }
static void massCentre() { float lo[3], hi[3]; if (!bounds(lo, hi)) return; for (int a = 0; a < 3; a++) target[a] = (lo[a] + hi[a]) * 0.5f; rotSet = true; follow = false; }
static void originCentre() { target[0] = target[1] = target[2] = 0; rotSet = false; follow = false; }
static void stdView(int which) {   // 0 front 1 back 2 left 3 right 4 top 5 bottom
  const float P = 3.14159265f; switch (which) { case 0: yaw = 0; pitch = 0; break; case 1: yaw = P; pitch = 0; break; case 2: yaw = -P / 2; pitch = 0; break; case 3: yaw = P / 2; pitch = 0; break; case 4: yaw = 0; pitch = -P / 2 + 0.001f; break; case 5: yaw = 0; pitch = P / 2 - 0.001f; break; } }

static void setupCamera(int w, int h) {
  glViewport(0, 0, w, h); glMatrixMode(GL_PROJECTION); glLoadIdentity(); double a = (double)w / h, n = 5, f = 30000;
  if (ortho) { double hh = dist * 0.5; glOrtho(-hh * a, hh * a, -hh, hh, -f, f); } else { double t = n * tan(27.5 * 3.14159265 / 180); glFrustum(-t * a, t * a, -t, t, n, f); }
  glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glTranslatef(0, 0, ortho ? -dist * 0.5f - 1000 : -dist); glRotatef(pitch * 57.2958f, 1, 0, 0); glRotatef(yaw * 57.2958f, 0, 1, 0);
  // Studio: while scanning the 3D view is attached to the scanner — the live surface stays centred and the captured
  // surfaces move past as the scanner moves. Rotation follows the scanner pose; mouse rotation offsets on top of it.
  { std::lock_guard<std::mutex> l(g_mx); if (follow && g_liveRotSet && g_mode.load() != M_Idle) glMultMatrixd(g_liveRot); }
  glTranslatef(-target[0], -target[1], -target[2]);
  glGetDoublev(GL_MODELVIEW_MATRIX, mv); glGetDoublev(GL_PROJECTION_MATRIX, pr); glGetIntegerv(GL_VIEWPORT, vp);
}
static void surfaceColour(int k, float c[3]) { float h = fmodf(k * 0.61803f, 1.f) * 6; int i = (int)h; float f = h - i, q = 1 - f; float r, g, b; switch (i) { case 0: r = 1; g = f; b = 0; break; case 1: r = q; g = 1; b = 0; break; case 2: r = 0; g = 1; b = f; break; case 3: r = 0; g = q; b = 1; break; case 4: r = f; g = 0; b = 1; break; default: r = 1; g = 0; b = q; } c[0] = 0.35f + 0.5f * r; c[1] = 0.35f + 0.5f * g; c[2] = 0.35f + 0.5f * b; }
// draw one frame mesh in the current render mode; `asPoints` forces point display (Studio's "points and solid" for scans)
static void drawMesh(FrameMesh& m, const float* col, bool asPoints, bool allowTexture = true, bool texFill = false) {
  if (m.v.empty()) return; bool vb = useVbo(m); const unsigned* ix = vb ? nullptr : m.idx.data();
  glVertexPointer(3, GL_FLOAT, 0, vb ? (const void*)0 : (const void*)m.v.data());
  bool hasCol = !m.col.empty(); if (hasCol) { glEnableClientState(GL_COLOR_ARRAY); glColorPointer(3, GL_FLOAT, 0, vb ? (const void*)m.offCol : (const void*)m.col.data()); } else glColor3fv(col);
  bool solid = !asPoints && (renderMode == R_SOLID || renderMode == R_WIRESOLID || renderMode == R_POINTSSOLID), wire = !asPoints && (renderMode == R_WIRE || renderMode == R_WIRESOLID), points = asPoints || renderMode == R_POINTS;
  if (m.idx.empty()) points = true;
  bool textured = solid && allowTexture && colorMode == 0 && !hasCol && bindTexture(m); if (textured) glColor3f(1, 1, 1);
  { bool tl = textured || texFill; float a = tl ? 0.7f : 0.25f, d = tl ? 0.55f : 0.85f; float amb[] = {a, a, a, 1}, dif[] = {d, d, d, 1}; glLightfv(GL_LIGHT0, GL_AMBIENT, amb); glLightfv(GL_LIGHT0, GL_DIFFUSE, dif); }   // Studio-like brightness on textured surfaces
  if (backface == 1) { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); } else glDisable(GL_CULL_FACE);
  if (solid) {
    // Studio 17 (reference screenshot of wall_sdk_4, 06:24) shows textured frames unlit at full brightness, so neighbouring
    // frames at different angles keep the same tone. Lighting stays on for untextured (scan-colour / surface) modes.
    bool lit = lighting && !m.n.empty();   // 4 Nov: textured surfaces are lit too — Studio shades the carved relief under the photo; flat drawing smeared the letters
    if (lit) { glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); glEnableClientState(GL_NORMAL_ARRAY); glNormalPointer(GL_FLOAT, 0, vb ? (const void*)m.offN : (const void*)m.n.data()); } glShadeModel(smooth ? GL_SMOOTH : GL_FLAT);
    if (backface == 2 || backface == 3) {   // back faces black / semi-transparent: draw them first with their own colour
      glEnable(GL_CULL_FACE); glCullFace(GL_FRONT); glDisable(GL_LIGHTING); glDisableClientState(GL_COLOR_ARRAY); if (textured) glDisable(GL_TEXTURE_2D);
      if (backface == 2) glColor3f(0.05f, 0.05f, 0.05f); else { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glColor4f(col[0] * 0.6f, col[1] * 0.6f, col[2] * 0.7f, 0.45f); }
      glDrawElements(GL_TRIANGLES, (GLsizei)m.idx.size(), GL_UNSIGNED_INT, ix); glDisable(GL_BLEND); glCullFace(GL_BACK);
      if (lit) glEnable(GL_LIGHTING); if (hasCol) glEnableClientState(GL_COLOR_ARRAY); else if (textured) { glEnable(GL_TEXTURE_2D); glColor3f(1, 1, 1); } else glColor3fv(col);
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glDrawElements(GL_TRIANGLES, (GLsizei)m.idx.size(), GL_UNSIGNED_INT, ix);
    glDisable(GL_LIGHTING); glDisableClientState(GL_NORMAL_ARRAY); glDisable(GL_CULL_FACE);
  }
  if (textured) { glDisable(GL_TEXTURE_2D); glDisableClientState(GL_TEXTURE_COORD_ARRAY); glColor3fv(col); }
  if (wire) { glDisable(GL_LIGHTING); if (solid) { glEnable(GL_POLYGON_OFFSET_LINE); glPolygonOffset(-1, -1); glDisableClientState(GL_COLOR_ARRAY); glColor3f(0.1f, 0.1f, 0.15f); } glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); glDrawElements(GL_TRIANGLES, (GLsizei)m.idx.size(), GL_UNSIGNED_INT, ix); glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glDisable(GL_POLYGON_OFFSET_LINE); if (solid && hasCol) glEnableClientState(GL_COLOR_ARRAY); }
  if (points) { glDisable(GL_LIGHTING); glDrawArrays(GL_POINTS, 0, (GLsizei)(m.v.size() / 3)); }
  if (hasCol) glDisableClientState(GL_COLOR_ARRAY); glDisable(GL_CULL_FACE); endVbo();
}
static void drawGrid() {
  glDisable(GL_LIGHTING); glColor3f(0.70f, 0.70f, 0.72f); glBegin(GL_LINES); float s = 500, st = 25;
  for (float x = -s; x <= s; x += st) { glVertex3f(x, -s, 0); glVertex3f(x, s, 0); glVertex3f(-s, x, 0); glVertex3f(s, x, 0); } glEnd();
  glLineWidth(2); glBegin(GL_LINES); glColor3f(0.85f, 0.25f, 0.25f); glVertex3f(0, 0, 0); glVertex3f(60, 0, 0); glColor3f(0.25f, 0.8f, 0.25f); glVertex3f(0, 0, 0); glVertex3f(0, 60, 0); glColor3f(0.3f, 0.4f, 1.0f); glVertex3f(0, 0, 0); glVertex3f(0, 0, 60); glEnd(); glLineWidth(1);
}
static void drawRotCentre() {   // Studio: small purple sphere with three small axes at a custom rotation centre
  if (!rotSet) return; glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST); glPointSize(9); glColor3f(0.65f, 0.25f, 0.8f); glBegin(GL_POINTS); glVertex3fv(target); glEnd(); glPointSize(2);
  float L = dist * 0.03f; glBegin(GL_LINES); glColor3f(0.9f, 0.3f, 0.3f); glVertex3fv(target); glVertex3f(target[0] + L, target[1], target[2]); glColor3f(0.3f, 0.9f, 0.3f); glVertex3fv(target); glVertex3f(target[0], target[1] + L, target[2]); glColor3f(0.4f, 0.5f, 1); glVertex3fv(target); glVertex3f(target[0], target[1], target[2] + L); glEnd(); glEnable(GL_DEPTH_TEST);
}
static void overlayBegin(int w, int h) { glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, w, h, 0, -1, 1); glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity(); glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
static void overlayEnd() { glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopMatrix(); }
static void rect(float x, float y, float w, float h) { glBegin(GL_QUADS); glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h); glEnd(); }

// Studio's range meter: histograms of surface points by distance — grey = registered key frames, dark green = last
// registered frame, red = registration error. Far at the top, near at the bottom.
static void drawRangeMeter(int w, int h) {
  std::vector<float> hist, hkey; bool lost; { std::lock_guard<std::mutex> l(g_mx); hist = g_hist; hkey = g_histKey; lost = g_lostNow.load(); } if (hist.empty()) return;
  float x0 = 40, top = 90, bot = h - 60, bw = 70; float lo = S.nearMm.load() - 40, hi = S.farMm.load() + 40; int n = (int)hist.size();
  auto yOf = [&](float d) { return bot - (d - lo) / (hi - lo) * (bot - top); };
  float yN = yOf(S.nearMm.load()), yF = yOf(S.farMm.load());
  glColor4f(0.4f, 0.4f, 0.4f, 0.3f); rect(x0, top, bw, bot - top); glColor4f(0.55f, 0.75f, 0.55f, 0.3f); rect(x0, yF, bw, yN - yF);
  auto bars = [&](const std::vector<float>& hh, float r, float g, float b, float a) { for (int i = 0; i < (int)hh.size(); i++) { float d0 = lo + (hi - lo) * i / n, d1 = lo + (hi - lo) * (i + 1) / n; float y = yOf(d1), ht = yOf(d0) - y; glColor4f(r, g, b, a); rect(x0, y, bw * hh[i], ht - 1); } };
  if (!hkey.empty()) bars(hkey, 0.75f, 0.75f, 0.75f, 0.55f);
  if (lost) bars(hist, 0.9f, 0.15f, 0.1f, 0.85f); else bars(hist, 0.1f, 0.45f, 0.15f, 0.85f);
  glColor4f(0.4f, 0.9f, 0.4f, 1); glBegin(GL_LINES); glVertex2f(x0 - 6, yN); glVertex2f(x0 + bw + 6, yN); glVertex2f(x0 - 6, yF); glVertex2f(x0 + bw + 6, yF); glEnd();
  wchar_t t[32]; glColor3f(0.1f, 0.1f, 0.1f); for (int k = 0; k < 5; k++) { float d = S.nearMm.load() + (S.farMm.load() - S.nearMm.load()) * k / 4; swprintf(t, 32, L"%.0f", d); text(x0 - 34, yOf(d) + 5, t); }
}
static void drawGizmo(int w, int h) {
  float cx = w - 70.f, cy = 100.f, L = 40;   /* below the cube strip */ float cy_ = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
  auto proj = [&](float x, float y, float z, float& ox, float& oy) { float x1 = cy_ * x + sy * z, z1 = -sy * x + cy_ * z; float y2 = cp * y - sp * z1; ox = cx + x1 * L; oy = cy - y2 * L; };
  float ax, ay; glLineWidth(2); glBegin(GL_LINES);
  glColor3f(0.9f, 0.2f, 0.2f); proj(1, 0, 0, ax, ay); glVertex2f(cx, cy); glVertex2f(ax, ay); glColor3f(0.2f, 0.75f, 0.2f); proj(0, 1, 0, ax, ay); glVertex2f(cx, cy); glVertex2f(ax, ay); glColor3f(0.25f, 0.35f, 1.0f); proj(0, 0, 1, ax, ay); glVertex2f(cx, cy); glVertex2f(ax, ay); glEnd(); glLineWidth(1);
  glColor3f(0.9f, 0.2f, 0.2f); proj(1.25f, 0, 0, ax, ay); text(ax - 4, ay + 5, L"X"); glColor3f(0.2f, 0.75f, 0.2f); proj(0, 1.25f, 0, ax, ay); text(ax - 4, ay + 5, L"Y"); glColor3f(0.25f, 0.35f, 1.0f); proj(0, 0, 1.25f, ax, ay); text(ax - 4, ay + 5, L"Z");
}
static const wchar_t* cubeLabels[6] = {L"Top", L"Front", L"Left", L"Right", L"Back", L"Bottom"}; static const int cubeViews[6] = {4, 0, 2, 3, 1, 5};
#define WM_VIEWBTN (WM_APP + 7)   // wParam 1 = Record/Pause, 2 = Stop (full-screen scanning overlay)
static bool fullView = false;      // panels hidden while scanning: show status + Pause/Stop in the viewport
static float cubeX(int w) { return (float)std::max(w - 392, 84); }   /* the cube strip keeps right of the Fit button on a narrow view */
static void drawButtons(int w, int h) {   // top-left: fit to view; top-right strip: navigation-cube faces
  glColor4f(0.88f, 0.88f, 0.88f, 1); rect(10, 10, 60, 24); glColor3f(0, 0, 0); text(18, 27, L"Fit  (F)");
  for (int i = 0; i < 6; i++) { glColor4f(0.88f, 0.88f, 0.88f, 1); rect(cubeX(w) + i * 62, 10, 58, 24); glColor3f(0, 0, 0); text(cubeX(w) + 6 + i * 62, 27, cubeLabels[i]); }
  if (fullView) { int m = g_mode.load(); double secs = g_recSecs + (m == M_Record ? std::chrono::duration<double>(clk::now() - g_recStart).count() : 0); wchar_t t[160], f[32];
    // Readable from 4–5 feet: 54 px fps, 30 px details, 30 px buttons.
    swprintf(f, 32, L"%.1f fps", fpsNow()); swprintf(t, 160, L"Surfaces: %ld     %d:%02d     Failures: %ld", g_frames.load(), (int)secs / 60, (int)secs % 60, g_regFail.load());
    glColor4f(0, 0, 0, 1); rect(130, h - 78.f, 820, 66); glColor3f(1, 1, 1); textBig(144, h - 28.f, f); textMid(370, h - 34.f, t);   // starts right of the range meter
    // A 5000-frame scan takes ~30 s to close and then a while to optimise: say so, as Studio's progress text does.
    const wchar_t* st = m == M_Stopping ? L"Stopping - finishing surfaces..." : (m == M_Idle && g_optimising.load()) ? L"Optimising surfaces..." : nullptr;   // bitmap font covers code points 0-255 only
    if (st) { glColor4f(0.0f, 0.30f, 0.0f, 1); rect(960, h - 78.f, 330, 66); glColor3f(1, 1, 1); textMid(972, h - 34.f, st); }
    glColor4f(0, 0, 0, 1); rect(w - 300.f, h - 80.f, 140, 68); rect(w - 150.f, h - 80.f, 140, 68); glColor3f(1, 1, 1); textMid(w - 290.f, h - 34.f, m == M_Record ? L"Pause" : L"Record"); textMid(w - 112.f, h - 34.f, L"Stop"); }
}
static int hitButton(int x, int y, int w, int h) { if (x >= 10 && x <= 70 && y >= 10 && y <= 34) return 100; for (int i = 0; i < 6; i++) if (x >= cubeX(w) + i * 62 && x <= cubeX(w) + 58 + i * 62 && y >= 10 && y <= 34) return cubeViews[i];
  if (fullView && y >= h - 80 && y <= h - 12) { if (x >= w - 300 && x <= w - 160) return 201; if (x >= w - 150 && x <= w - 10) return 202; } return -1; }

static void render(int w, int h, bool forShot) {
  int mode = g_mode.load(); bool lost = mode == M_Record && g_lostNow.load();
  { std::lock_guard<std::mutex> l(g_mx); if (follow && !forShot && !g_live.v.empty() && mode != M_Idle) for (int a = 0; a < 3; a++) target[a] += (g_liveCentre[a] - target[a]) * 0.25f; }
  setupCamera(w, h);
  if (forShot) { if (S.shotBg == 1) glClearColor(0, 0, 0, 1); else glClearColor(1, 1, 1, 1); } else if (lost) glClearColor(1.0f, 0.86f, 0.66f, 1); else glClearColor(0.96f, 0.96f, 0.96f, 1);   /* 7 Nov (owner, daylight): near-white background, opaque high-contrast overlays */   // Studio: "dropped" background on misalignment
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  if (grid) drawGrid();
  bool scansAsPoints = renderMode == R_POINTSSOLID;
  // One fill tone for the whole project: per-scan tones drew a visible line between two aligned passes that Studio
  // shows as one surface (Gowtham, 3 Nov 17:23, Trail6).
  float gAvg[3] = {0.7f, 0.7f, 0.68f}; { std::lock_guard<std::mutex> l(g_scanMx); double a[3] = {0, 0, 0}; int n = 0; for (auto& s : g_scans) if (s.hasTexAvg && scanShown(s)) { for (int k = 0; k < 3; k++) a[k] += s.texAvg[k]; n++; } if (n) for (int k = 0; k < 3; k++) gAvg[k] = (float)(a[k] / n); }
  if (mode != M_Preview) { std::lock_guard<std::mutex> l(g_scanMx); for (int i = 0; i < (int)g_scans.size(); i++) { auto& s = g_scans[i]; if (!scanShown(s)) continue;   // Studio hides uploaded scans in Preview
      float c[3] = {s.color[0], s.color[1], s.color[2]}; if (i == selectedScan) { c[0] = std::min(1.f, c[0] + 0.2f); c[1] = std::min(1.f, c[1] + 0.2f); c[2] = std::min(1.f, c[2] + 0.1f); }
      bool anyTex = false; for (auto& m : s.disp) if (!m.uv.empty()) { anyTex = true; break; }
      int k = 0, drawn = 0; glEnable(GL_POLYGON_OFFSET_FILL);
      bool fillPass = colorMode == 0 && anyTex && s.hasTexAvg;
      // While recording, draw at most ~30 frames of each stored scan: with a 5000-frame project loaded the capture rate
      // fell to 3.5–4.5 fps (13:00, 2 Nov) because every redraw pushed ~10 M client-memory vertices through the driver.
      // 4 Nov, owner: while scanning the next pass the earlier passes must show WHOLE, as in Studio, so uncovered
      // areas can be seen. With vertex buffers the full 120-frame display of each scan is drawn; the 30-frame thinning
      // (pre-VBO, 5000-frame projects) is kept only as a cap of 600 frames over all stored scans.
      int totalDisp = 0; for (auto& q : g_scans) if (q.visible) totalDisp += (int)q.disp.size();
      int stride = (mode == M_Record || mode == M_Paused) && totalDisp > 600 ? std::max(1, totalDisp / 600) : 1; int j = 0;
      if (fillPass) for (auto& m : s.disp) { if (j++ % stride || !m.uv.empty()) continue; glPolygonOffset(-0.6f * drawn, -0.6f * drawn); drawn++; drawMesh(m, gAvg, scansAsPoints, false, true); }   // untextured frames underneath in the project's tone: they fill the gaps between texture frames
      j = 0;
      for (auto& m : s.disp) { if (colorMode == 2) surfaceColour(i * 131 + k, c); k++;
        if (j++ % stride) continue;
        if (fillPass && m.uv.empty()) continue;
        glPolygonOffset(-0.6f * drawn, -0.6f * drawn); drawn++;   // Studio-like: each later frame is drawn on top of earlier ones instead of interleaving
        drawMesh(m, c, scansAsPoints); }
      glDisable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(0, 0); } }
  { std::lock_guard<std::mutex> l(g_mx); float grey[3] = {0.66f, 0.66f, 0.64f}; glEnable(GL_POLYGON_OFFSET_FILL); int drawn = 0;
    // Studio look while scanning (owner, 5 Nov): the whole area covered in this pass is a dense, flat distance-colour
    // surface (green at a good distance). Accumulated frames keep their colours when "Show distance color" is on and
    // are drawn unlit; the current frame on top, also flat.
    // only while scanning: after Stop the accumulated frames stay until the next start, and green there left a stale
    // patch over the finished scan (owner, 5 Nov 10:36) — idle draws them the normal way
    bool scanningNow = mode == M_Record || mode == M_Paused || mode == M_Preview;
    // 5 Nov 10:45 (Madhusudhana's photo): Studio's dense green is the scanner's CURRENT view only, moving with the
    // scanner; the area already covered shows normally behind it. Accumulating green left a patch where the scanner had
    // been. So: accumulated frames are drawn the normal way (below), and only the current frame is drawn flat, unlit.
    if (false) {
    }
    else {
    bool liveFill = colorMode == 0 && g_liveTexSet;
    if (liveFill) for (auto& m : g_liveAcc) { if (!m.uv.empty()) continue; glPolygonOffset(-0.6f * drawn, -0.6f * drawn); drawn++; drawMesh(m, g_liveTexAvg, false, false, true); }
    for (auto& m : g_liveAcc) { if (liveFill && m.uv.empty()) continue; glPolygonOffset(-0.6f * drawn, -0.6f * drawn); drawn++; std::vector<float> keep; keep.swap(m.col); drawMesh(m, grey, false, colorMode == 0); keep.swap(m.col); }   // captured surfaces: textured where available, later ones on top (distance colours, kept for the live green, hidden here)
    float cur[3] = {0.30f, 0.80f, 0.32f}; if (lost) { cur[0] = 0.9f; cur[1] = 0.2f; cur[2] = 0.15f; }
    glPolygonOffset(-0.6f * drawn - 2, -0.6f * drawn - 2);
    // the current frame only while scanning: after Stop it stayed on screen as a green patch (owner, 5 Nov 10:42)
    // flat, unlit, and drawn OVER the captured frames: during Record those lie at the same depth and the two fought for
    // the pixels (depth precision at 5 mm–30 m), breaking the green into light speckles (owner, 5 Nov 10:51 — "will
    // cause visibility issues in daylight"). Depth test off for the current frame only, as Studio overlays it.
    if (scanningNow && !g_live.v.empty()) { bool lit = lighting; lighting = false; glDisable(GL_DEPTH_TEST);
      FrameMesh& m = g_live; if (lost && !m.col.empty()) { std::vector<float> keep; keep.swap(m.col); drawMesh(m, cur, false, false); keep.swap(m.col); } else drawMesh(m, cur, false, false); lighting = lit; glEnable(GL_DEPTH_TEST); }
    glDisable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(0, 0); } }
  deleteTrashTextures(); if (forShot) return;   // DeepSeek 5-2: no rotation-centre marker in screenshots
  drawRotCentre();
  overlayBegin(w, h); glScalef(UI_K, UI_K, 1); w = (int)(w / UI_K); h = (int)(h / UI_K);   /* the overlay is drawn in design px (fonts are made at the scaled size) */
  if (rangeMeter && mode != M_Idle) drawRangeMeter(w, h); drawGizmo(w, h); drawButtons(w, h);
  // 4 Nov 16:54: after a second loss every frame failed for a minute (314 of 315) — the scanner saw nothing it could
  // track. After 6 s of that, say what to do, in plain words.
  { static clk::time_point lostSince; static bool wasLost = false; if (lost && !wasLost) lostSince = clk::now(); wasLost = lost;
    if (lost && std::chrono::duration<double>(clk::now() - lostSince).count() > 6.0) { glColor4f(0.85f, 0.0f, 0.0f, 1); rect(0, h / 2.f + 34, (float)w, 34); glColor3f(1, 1, 1); text(w / 2.f - 260, h / 2.f + 56, L"No usable surface: hold the scanner about 25 cm from carved stone, over an area already scanned"); } }
  // 7 Nov: a loss keeps the scan open (artecrec.cpp, logic::lossEndsScan); plain words on what to do
  if (lost) { glColor4f(1.0f, 0.80f, 0.0f, 1); rect(0, h / 2.f - 30, (float)w, 60); glColor3f(0, 0, 0); text(w / 2.f - 250, h / 2.f + 6, L"Tracking lost: go back to the last place you scanned, about 25 cm away. The scan continues."); }
  else if (mode == M_Record && g_alignLive.load() && !g_liveFound.load()) { glColor4f(0.0f, 0.35f, 0.0f, 1); rect(0, h / 2.f - 30, (float)w, 60); glColor3f(1, 1, 1); text(w / 2.f - 200, h / 2.f + 6, L"Searching for position: Point 3D scanner at the object to continue"); }
  else if (mode == M_Record && g_liveFound.load() && std::chrono::duration<double>(clk::now() - (clk::time_point)g_lastSearch).count() < 3.0) { glColor4f(0.0f, 0.35f, 0.0f, 1); rect(w / 2.f - 120, h - 60.f, 240, 30); glColor3f(1, 1, 1); text(w / 2.f - 55, h - 40.f, L"Position found"); }
  if (g_overheated.load()) { glColor4f(0.85f, 0.0f, 0.0f, 1); rect(0, 44, (float)w, 30); glColor3f(1, 1, 1); text(w / 2.f - 160, 64, L"Scanner overheated — let it cool before continuing"); }
  int cd = g_countdown.load(); if (cd > 0) { wchar_t t[32]; swprintf(t, 32, L"Recording in %d…", cd); glColor4f(0, 0, 0, 1); rect(w / 2.f - 100, h / 2.f - 30, 200, 60); glColor3f(1, 1, 1); text(w / 2.f - 60, h / 2.f + 6, t); }
  // 7 Nov (owner: "he is not tech savvy"): every wait says so in plain words, in the middle of the screen, with the way out
  if (mode == M_Aligning) { glColor4f(0, 0, 0, 1); rect(0, h / 2.f - 30, (float)w, 60); glColor3f(1, 1, 1); text(w / 2.f - 250, h / 2.f + 6, L"Please wait: placing the passes. Record is back in a moment. (Edit > Cancel processing to skip)"); }
  if (mode == M_Busy) { glColor4f(0, 0, 0, 1); rect(0, h / 2.f - 30, (float)w, 60); glColor3f(1, 1, 1); text(w / 2.f - 200, h / 2.f + 6, L"Please wait: working on the scans. Press Esc to stop and keep them as they are."); }
  overlayEnd();
}
// Redraw only when something changed (new frame, input, project change) or every 400 ms. Drawing a large scan on every
// 40 ms timer tick starved the message loop on 2 Nov (window frozen for minutes, Stop clicks lost).
static bool wantRedraw = true; static clk::time_point lastDraw;
static void draw() { deleteTrashTextures();   /* DeepSeek 2-1: also while minimised (render does not run then) */ RECT r; GetClientRect(hwnd, &r); if (r.right < 2 || r.bottom < 2) return;
  bool due = std::chrono::duration<double>(clk::now() - lastDraw).count() >= 0.4;
  if (!wantRedraw && !g_newData && !due) return; wantRedraw = false; g_newData = false; lastDraw = clk::now();
  render(r.right, r.bottom, false); SwapBuffers(dc); }
static bool pick(int x, int y, float out[3]) {
  RECT r; GetClientRect(hwnd, &r); float z; glReadPixels(x, r.bottom - 1 - y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &z); if (z >= 0.9999f) return false;
  double m[16]; for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { double s = 0; for (int k = 0; k < 4; k++) s += pr[k * 4 + j] * mv[i * 4 + k]; m[i * 4 + j] = s; }
  double inv[16], det; const double* a = m;
  inv[0] = a[5]*a[10]*a[15]-a[5]*a[11]*a[14]-a[9]*a[6]*a[15]+a[9]*a[7]*a[14]+a[13]*a[6]*a[11]-a[13]*a[7]*a[10]; inv[4] = -a[4]*a[10]*a[15]+a[4]*a[11]*a[14]+a[8]*a[6]*a[15]-a[8]*a[7]*a[14]-a[12]*a[6]*a[11]+a[12]*a[7]*a[10];
  inv[8] = a[4]*a[9]*a[15]-a[4]*a[11]*a[13]-a[8]*a[5]*a[15]+a[8]*a[7]*a[13]+a[12]*a[5]*a[11]-a[12]*a[7]*a[9]; inv[12] = -a[4]*a[9]*a[14]+a[4]*a[10]*a[13]+a[8]*a[5]*a[14]-a[8]*a[6]*a[13]-a[12]*a[5]*a[10]+a[12]*a[6]*a[9];
  inv[1] = -a[1]*a[10]*a[15]+a[1]*a[11]*a[14]+a[9]*a[2]*a[15]-a[9]*a[3]*a[14]-a[13]*a[2]*a[11]+a[13]*a[3]*a[10]; inv[5] = a[0]*a[10]*a[15]-a[0]*a[11]*a[14]-a[8]*a[2]*a[15]+a[8]*a[3]*a[14]+a[12]*a[2]*a[11]-a[12]*a[3]*a[10];
  inv[9] = -a[0]*a[9]*a[15]+a[0]*a[11]*a[13]+a[8]*a[1]*a[15]-a[8]*a[3]*a[13]-a[12]*a[1]*a[11]+a[12]*a[3]*a[9]; inv[13] = a[0]*a[9]*a[14]-a[0]*a[10]*a[13]-a[8]*a[1]*a[14]+a[8]*a[2]*a[13]+a[12]*a[1]*a[10]-a[12]*a[2]*a[9];
  inv[2] = a[1]*a[6]*a[15]-a[1]*a[7]*a[14]-a[5]*a[2]*a[15]+a[5]*a[3]*a[14]+a[13]*a[2]*a[7]-a[13]*a[3]*a[6]; inv[6] = -a[0]*a[6]*a[15]+a[0]*a[7]*a[14]+a[4]*a[2]*a[15]-a[4]*a[3]*a[14]-a[12]*a[2]*a[7]+a[12]*a[3]*a[6];
  inv[10] = a[0]*a[5]*a[15]-a[0]*a[7]*a[13]-a[4]*a[1]*a[15]+a[4]*a[3]*a[13]+a[12]*a[1]*a[7]-a[12]*a[3]*a[5]; inv[14] = -a[0]*a[5]*a[14]+a[0]*a[6]*a[13]+a[4]*a[1]*a[14]-a[4]*a[2]*a[13]-a[12]*a[1]*a[6]+a[12]*a[2]*a[5];
  inv[3] = -a[1]*a[6]*a[11]+a[1]*a[7]*a[10]+a[5]*a[2]*a[11]-a[5]*a[3]*a[10]-a[9]*a[2]*a[7]+a[9]*a[3]*a[6]; inv[7] = a[0]*a[6]*a[11]-a[0]*a[7]*a[10]-a[4]*a[2]*a[11]+a[4]*a[3]*a[10]+a[8]*a[2]*a[7]-a[8]*a[3]*a[6];
  inv[11] = -a[0]*a[5]*a[11]+a[0]*a[7]*a[9]+a[4]*a[1]*a[11]-a[4]*a[3]*a[9]-a[8]*a[1]*a[7]+a[8]*a[3]*a[5]; inv[15] = a[0]*a[5]*a[10]-a[0]*a[6]*a[9]-a[4]*a[1]*a[10]+a[4]*a[2]*a[9]+a[8]*a[1]*a[6]-a[8]*a[2]*a[5];
  det = a[0]*inv[0]+a[1]*inv[4]+a[2]*inv[8]+a[3]*inv[12]; if (fabs(det) < 1e-12) return false; for (int i = 0; i < 16; i++) inv[i] /= det;
  double nx = (x - vp[0]) * 2.0 / vp[2] - 1, ny = ((r.bottom - 1 - y) - vp[1]) * 2.0 / vp[3] - 1, nz = z * 2.0 - 1; double in[4] = {nx, ny, nz, 1}, o[4];
  for (int i = 0; i < 4; i++) o[i] = inv[i] * in[0] + inv[4 + i] * in[1] + inv[8 + i] * in[2] + inv[12 + i] * in[3]; if (fabs(o[3]) < 1e-12) return false;
  out[0] = (float)(o[0] / o[3]); out[1] = (float)(o[1] / o[3]); out[2] = (float)(o[2] / o[3]); return true;
}
static void zoomAt(float factor, int x, int y) {
  float p[3]; bool hit = wheelToCursor && pick(x, y, p); dist *= factor; if (dist < 20) dist = 20; if (dist > 20000) dist = 20000;
  if (hit) for (int a = 0; a < 3; a++) target[a] += (p[a] - target[a]) * (1 - factor);
}
static bool pngEncoder(CLSID* out) { UINT n = 0, sz = 0; Gdiplus::GetImageEncodersSize(&n, &sz); if (!sz) return false; std::vector<unsigned char> buf(sz); auto* enc = (Gdiplus::ImageCodecInfo*)buf.data(); Gdiplus::GetImageEncoders(n, sz, enc); for (UINT i = 0; i < n; i++) if (!wcscmp(enc[i].MimeType, L"image/png")) { *out = enc[i].Clsid; return true; } return false; }
static bool screenshot(const std::wstring& path) {   // Studio: PNG of the 3D View only, with the screenshot background colour
  RECT r; GetClientRect(hwnd, &r); int w = r.right, h = r.bottom; render(w, h, true); std::vector<unsigned char> px((size_t)w * h * 3); glReadPixels(0, 0, w, h, GL_BGR_EXT, GL_UNSIGNED_BYTE, px.data());
  Gdiplus::Bitmap bmp(w, h, PixelFormat24bppRGB); Gdiplus::BitmapData bd; Gdiplus::Rect rc(0, 0, w, h); if (bmp.LockBits(&rc, Gdiplus::ImageLockModeWrite, PixelFormat24bppRGB, &bd) != Gdiplus::Ok) return false;
  for (int y = 0; y < h; y++) memcpy((unsigned char*)bd.Scan0 + (size_t)y * bd.Stride, &px[(size_t)(h - 1 - y) * w * 3], (size_t)w * 3); bmp.UnlockBits(&bd);
  CLSID clsid; if (!pngEncoder(&clsid)) return false; return bmp.Save(path.c_str(), &clsid, nullptr) == Gdiplus::Ok;
}
static LRESULT CALLBACK proc(HWND h, UINT u, WPARAM wp, LPARAM lp) {
  int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp); RECT r; GetClientRect(h, &r); wantRedraw = true;
  switch (u) {
  case WM_LBUTTONDOWN: { int b = hitButton((int)(x / UI_K), (int)(y / UI_K), (int)(r.right / UI_K), (int)(r.bottom / UI_K)); if (b == 100) { fit(); return 0; } if (b == 201 || b == 202) { PostMessageW(GetParent(h), WM_VIEWBTN, b - 200, 0); return 0; } if (b >= 0) { stdView(b); return 0; } dragL = true; last = {x, y}; SetCapture(h); SetFocus(h); return 0; }
  case WM_LBUTTONDBLCLK: { float p[3]; if (pick(x, y, p)) { memcpy(target, p, sizeof target); rotSet = true; follow = false; } return 0; }
  case WM_MBUTTONDOWN: dragM = true; last = {x, y}; SetCapture(h); return 0;
  case WM_RBUTTONDOWN: dragR = true; last = {x, y}; SetCapture(h); return 0;
  case WM_LBUTTONUP: dragL = false; if (!dragM && !dragR) ReleaseCapture(); return 0;
  case WM_MBUTTONUP: dragM = false; if (!dragL && !dragR) ReleaseCapture(); return 0;
  case WM_RBUTTONUP: { bool wasDrag = dragR; dragR = false; if (!dragL && !dragM) ReleaseCapture(); if (wasDrag && abs(x - last.x) < 3 && abs(y - last.y) < 3) PostMessageW(GetParent(h), WM_CONTEXTMENU, (WPARAM)h, lp); return 0; }
  case WM_MOUSEMOVE: {
    int dx = x - last.x, dy = y - last.y; if (!dragL && !dragM && !dragR) return 0;
    if (dragM || (dragL && dragR)) { float s = (ortho ? dist * 0.5f : dist) / r.bottom * 2.0f * 0.52f; float cy_ = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
      float rx[3] = {cy_, 0, -sy}, up[3] = {sy * sp, cp, cy_ * sp}; for (int a = 0; a < 3; a++) target[a] += (-dx * rx[a] + dy * up[a]) * s; follow = false; }
    else if (dragL) { yaw += dx * 0.008f; pitch += dy * 0.008f; if (pitch > 1.56f) pitch = 1.56f; if (pitch < -1.56f) pitch = -1.56f; }
    else if (dragR) { if (abs(dx) + abs(dy) > 0) zoomAt(powf(1.01f, (float)(dx + dy)), x, y); }
    last = {x, y}; return 0; }
  case WM_MOUSEWHEEL: { POINT p = {x, y}; ScreenToClient(h, &p); zoomAt(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 0.85f : 1.18f, p.x, p.y); return 0; }
  case WM_ERASEBKGND: return 1;
  case WM_KEYDOWN: PostMessageW(GetParent(h), WM_KEYDOWN, wp, lp); return 0;
  }
  return DefWindowProcW(h, u, wp, lp);
}
}  // namespace view
