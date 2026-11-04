// meshcheck — thorough check of a fused mesh in PLY (binary little-endian or ascii; vertices with x y z, optional
// nx ny nz, optional u v / s t; faces as vertex lists). Reports: counts, bounding box, duplicate vertices, unused
// vertices, degenerate faces, edge length statistics, non-manifold edges, boundary edges grouped into loops (holes:
// count, size distribution, the largest ones with perimeter and location), connected components, face orientation
// consistency across shared edges, vertex-normal vs face-normal agreement, dihedral angle (curvature) statistics,
// spikes (edges with dihedral > 60 deg), and uv coverage.
// usage: meshcheck <mesh.ply>
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <algorithm>
struct V { float x, y, z; };
int wmain(int argc, wchar_t** argv) {
  if (argc < 2) { wprintf(L"usage: meshcheck mesh.ply\n"); return 2; } FILE* f = _wfopen(argv[1], L"rb"); if (!f) { wprintf(L"cannot open\n"); return 1; }
  char line[1024]; bool binary = false; int nv = 0, nf = 0; std::vector<std::pair<std::string, int>> vprops; std::string curElem; int faceIdxSize = 4, faceCntSize = 1; bool ok = false; int extraFaceProps = 0;
  while (fgets(line, sizeof line, f)) { std::string s = line; while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    if (s.rfind("format", 0) == 0) binary = s.find("binary") != std::string::npos;
    else if (s.rfind("element", 0) == 0) { char nm[64]; int cnt; if (sscanf(s.c_str(), "element %63s %d", nm, &cnt) == 2) { curElem = nm; if (curElem == "vertex") nv = cnt; else if (curElem == "face") nf = cnt; } }
    else if (s.rfind("property", 0) == 0) { auto sz = [](const std::string& t) { return t == "float" || t == "float32" || t == "int" || t == "uint" || t == "int32" || t == "uint32" ? 4 : t == "double" || t == "float64" ? 8 : t == "uchar" || t == "uint8" || t == "char" || t == "int8" ? 1 : t == "short" || t == "ushort" || t == "int16" || t == "uint16" ? 2 : -1; };
      if (curElem == "vertex") { char t[32], nm[64]; if (sscanf(s.c_str(), "property %31s %63s", t, nm) == 2) vprops.push_back({nm, sz(t)}); }
      else if (curElem == "face") { char a[32], b[32], c[32], nm[64]; if (sscanf(s.c_str(), "property %31s %31s %31s %63s", a, b, c, nm) == 4 && !strcmp(a, "list")) { faceCntSize = sz(b); faceIdxSize = sz(c); } else extraFaceProps++; } }
    else if (s == "end_header") { ok = true; break; } }
  if (!ok || !binary) { wprintf(L"unsupported header (ascii or malformed)\n"); fclose(f); return 1; }
  int stride = 0; std::vector<int> off; int ix = -1, iy = -1, iz = -1, inx = -1, iny = -1, inz = -1, iu = -1, iv = -1; for (auto& p : vprops) { if (p.second < 0) { wprintf(L"unknown vertex property type\n"); return 1; } off.push_back(stride); int i = (int)off.size() - 1; if (p.first == "x") ix = i; else if (p.first == "y") iy = i; else if (p.first == "z") iz = i; else if (p.first == "nx") inx = i; else if (p.first == "ny") iny = i; else if (p.first == "nz") inz = i; else if (p.first == "u" || p.first == "s" || p.first == "texture_u") iu = i; else if (p.first == "v" || p.first == "t" || p.first == "texture_v") iv = i; stride += p.second; }
  std::vector<unsigned char> vb((size_t)nv * stride); if (fread(vb.data(), 1, vb.size(), f) < vb.size()) { wprintf(L"short vertex data\n"); return 1; }
  auto rd = [&](const unsigned char* p, int i) -> float { if (vprops[i].second == 4) { float v; memcpy(&v, p + off[i], 4); return v; } double d; memcpy(&d, p + off[i], 8); return (float)d; };
  std::vector<V> P(nv), N; bool hasN = inx >= 0 && iny >= 0 && inz >= 0, hasUV = iu >= 0 && iv >= 0; if (hasN) N.resize(nv); std::vector<float> U, Vv; if (hasUV) { U.resize(nv); Vv.resize(nv); }
  for (int i = 0; i < nv; i++) { const unsigned char* p = &vb[(size_t)i * stride]; P[i] = {rd(p, ix), rd(p, iy), rd(p, iz)}; if (hasN) N[i] = {rd(p, inx), rd(p, iny), rd(p, inz)}; if (hasUV) { U[i] = rd(p, iu); Vv[i] = rd(p, iv); } }
  std::vector<unsigned> F; F.reserve((size_t)nf * 3); int nonTri = 0; for (int k = 0; k < nf; k++) { unsigned cnt = 0; unsigned char cb[8]; if (fread(cb, 1, faceCntSize, f) < (size_t)faceCntSize) break; cnt = faceCntSize == 1 ? cb[0] : faceCntSize == 2 ? *(unsigned short*)cb : *(unsigned*)cb; std::vector<unsigned> idx(cnt); for (unsigned j = 0; j < cnt; j++) { unsigned char ib[8]; fread(ib, 1, faceIdxSize, f); idx[j] = faceIdxSize == 4 ? *(unsigned*)ib : faceIdxSize == 2 ? *(unsigned short*)ib : ib[0]; } if (extraFaceProps) fseek(f, 4 * extraFaceProps, SEEK_CUR); if (cnt == 3) { F.push_back(idx[0]); F.push_back(idx[1]); F.push_back(idx[2]); } else { nonTri++; for (unsigned j = 1; j + 1 < cnt; j++) { F.push_back(idx[0]); F.push_back(idx[j]); F.push_back(idx[j + 1]); } } }
  fclose(f); int nt = (int)F.size() / 3;
  V mn = P[0], mx = P[0]; for (auto& p : P) { mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z); mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z); }
  wprintf(L"vertices %d, faces %d (%d non-triangles split), normals %ls, uv %ls\nbounding box %.1f x %.1f x %.1f mm\n", nv, nt, nonTri, hasN ? L"yes" : L"no", hasUV ? L"yes" : L"no", mx.x - mn.x, mx.y - mn.y, mx.z - mn.z);
  // duplicates, unused, degenerate
  { std::unordered_map<long long, int> seen; int dup = 0; for (auto& p : P) { long long k = ((long long)(int)lroundf(p.x * 1000) * 73856093LL) ^ ((long long)(int)lroundf(p.y * 1000) * 19349663LL) ^ ((long long)(int)lroundf(p.z * 1000) * 83492791LL); if (seen[k]++) dup++; } std::vector<char> used(nv, 0); for (unsigned i : F) if (i < (unsigned)nv) used[i] = 1; int unused = 0; for (char c : used) if (!c) unused++; int degen = 0, badIdx = 0; for (int k = 0; k < nt; k++) { unsigned a = F[k * 3], b = F[k * 3 + 1], c = F[k * 3 + 2]; if (a >= (unsigned)nv || b >= (unsigned)nv || c >= (unsigned)nv) { badIdx++; continue; } if (a == b || b == c || a == c) { degen++; continue; } V u = {P[b].x - P[a].x, P[b].y - P[a].y, P[b].z - P[a].z}, w = {P[c].x - P[a].x, P[c].y - P[a].y, P[c].z - P[a].z}; V cr = {u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x}; if (cr.x * cr.x + cr.y * cr.y + cr.z * cr.z < 1e-12f) degen++; }
    wprintf(L"duplicate vertices (same position to 1 um) %d, unused vertices %d, degenerate faces %d, bad indices %d\n", dup, unused, degen, badIdx); }
  // edges
  struct E { int cnt; int f1, f2; }; std::unordered_map<long long, E> edges; edges.reserve((size_t)nt * 2); auto ek = [](unsigned a, unsigned b) { return a < b ? ((long long)a << 32) | b : ((long long)b << 32) | a; };
  std::vector<double> elen; elen.reserve((size_t)nt * 3); for (int k = 0; k < nt; k++) for (int j = 0; j < 3; j++) { unsigned a = F[k * 3 + j], b = F[k * 3 + (j + 1) % 3]; if (a >= (unsigned)nv || b >= (unsigned)nv) continue; auto& e = edges[ek(a, b)]; if (e.cnt == 0) { e.f1 = k; double dx = P[a].x - P[b].x, dy = P[a].y - P[b].y, dz = P[a].z - P[b].z; elen.push_back(sqrt(dx * dx + dy * dy + dz * dz)); } else if (e.cnt == 1) e.f2 = k; e.cnt++; }
  int boundary = 0, nonManifold = 0; for (auto& kv : edges) { if (kv.second.cnt == 1) boundary++; else if (kv.second.cnt > 2) nonManifold++; }
  std::sort(elen.begin(), elen.end()); wprintf(L"edges %zu: length median %.3f mm, 90th pct %.3f mm, max %.2f mm; boundary edges %d, non-manifold edges %d\n", elen.size(), elen[elen.size() / 2], elen[elen.size() * 9 / 10], elen.back(), boundary, nonManifold);
  // boundary loops (holes + outer border)
  { std::unordered_map<unsigned, std::vector<unsigned>> adj; for (auto& kv : edges) if (kv.second.cnt == 1) { unsigned a = (unsigned)(kv.first >> 32), b = (unsigned)(kv.first & 0xffffffff); adj[a].push_back(b); adj[b].push_back(a); }
    std::unordered_map<unsigned, char> vis; struct Loop { int n; double per; V c; }; std::vector<Loop> loops;
    for (auto& kv : adj) { if (vis[kv.first]) continue; std::vector<unsigned> stack = {kv.first}; vis[kv.first] = 1; int n = 0; double per = 0; double cx = 0, cy = 0, cz = 0; while (!stack.empty()) { unsigned v = stack.back(); stack.pop_back(); n++; cx += P[v].x; cy += P[v].y; cz += P[v].z; for (unsigned w : adj[v]) { double dx = P[v].x - P[w].x, dy = P[v].y - P[w].y, dz = P[v].z - P[w].z; per += sqrt(dx * dx + dy * dy + dz * dz) * 0.5; if (!vis[w]) { vis[w] = 1; stack.push_back(w); } } } loops.push_back({n, per, {(float)(cx / n), (float)(cy / n), (float)(cz / n)}}); }
    std::sort(loops.begin(), loops.end(), [](const Loop& a, const Loop& b) { return a.per > b.per; }); int hSmall = 0, hMedium = 0, hLarge = 0; for (size_t i = 1; i < loops.size(); i++) { if (loops[i].per < 3) hSmall++; else if (loops[i].per < 15) hMedium++; else hLarge++; }
    wprintf(L"boundary loops %zu: outer border perimeter %.0f mm; holes %zu (perimeter <3 mm: %d, 3-15 mm: %d, >15 mm: %d)\n", loops.size(), loops.empty() ? 0.0 : loops[0].per, loops.size() ? loops.size() - 1 : 0, hSmall, hMedium, hLarge);
    for (size_t i = 1; i < loops.size() && i <= 8; i++) wprintf(L"  hole %zu: perimeter %.1f mm, %d vertices, at (%.0f %.0f %.0f)\n", i, loops[i].per, loops[i].n, loops[i].c.x, loops[i].c.y, loops[i].c.z); }
  // components
  { std::vector<int> parent(nv); for (int i = 0; i < nv; i++) parent[i] = i; auto find = [&](int a) { while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; } return a; }; for (int k = 0; k < nt; k++) { unsigned a = F[k * 3], b = F[k * 3 + 1], c = F[k * 3 + 2]; if (a >= (unsigned)nv || b >= (unsigned)nv || c >= (unsigned)nv) continue; parent[find(a)] = find(b); parent[find(b)] = find(c); } std::map<int, int> comp; for (int k = 0; k < nt; k++) { unsigned a = F[k * 3]; if (a < (unsigned)nv) comp[find(a)]++; } std::vector<int> sizes; for (auto& kv : comp) sizes.push_back(kv.second); std::sort(sizes.rbegin(), sizes.rend()); wprintf(L"connected components %zu: largest %d faces", sizes.size(), sizes.empty() ? 0 : sizes[0]); if (sizes.size() > 1) wprintf(L", second %d, smallest %d", sizes[1], sizes.back()); wprintf(L"\n"); }
  // orientation consistency, normals, dihedral
  { std::vector<V> fn(nt); for (int k = 0; k < nt; k++) { unsigned a = F[k * 3], b = F[k * 3 + 1], c = F[k * 3 + 2]; if (a >= (unsigned)nv || b >= (unsigned)nv || c >= (unsigned)nv) { fn[k] = {0, 0, 0}; continue; } V u = {P[b].x - P[a].x, P[b].y - P[a].y, P[b].z - P[a].z}, w = {P[c].x - P[a].x, P[c].y - P[a].y, P[c].z - P[a].z}; V cr = {u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x}; float L = sqrtf(cr.x * cr.x + cr.y * cr.y + cr.z * cr.z); fn[k] = L > 0 ? V{cr.x / L, cr.y / L, cr.z / L} : V{0, 0, 0}; }
    long long shared = 0, flipped = 0; std::vector<double> dih; dih.reserve(edges.size()); int spikes = 0;
    for (auto& kv : edges) { if (kv.second.cnt != 2) continue; shared++; int f1 = kv.second.f1, f2 = kv.second.f2; unsigned a = (unsigned)(kv.first >> 32), b = (unsigned)(kv.first & 0xffffffff);
      auto dir = [&](int fk) { for (int j = 0; j < 3; j++) { unsigned p = F[fk * 3 + j], q = F[fk * 3 + (j + 1) % 3]; if (p == a && q == b) return 1; if (p == b && q == a) return -1; } return 0; }; if (dir(f1) == dir(f2) && dir(f1) != 0) flipped++;   // consistent orientation traverses a shared edge in opposite directions
      double d = fn[f1].x * fn[f2].x + fn[f1].y * fn[f2].y + fn[f1].z * fn[f2].z; d = std::max(-1.0, std::min(1.0, d)); double ang = acos(d) * 180 / 3.14159265; dih.push_back(ang); if (ang > 60) spikes++; }
    std::sort(dih.begin(), dih.end()); wprintf(L"face orientation: %lld shared edges, %lld with inconsistent winding (%.2f%%)\n", shared, flipped, shared ? 100.0 * flipped / shared : 0);
    if (!dih.empty()) wprintf(L"dihedral angle between neighbouring faces: median %.1f deg, 90th pct %.1f, 99th pct %.1f; edges over 60 deg (spikes/folds) %d (%.3f%%)\n", dih[dih.size() / 2], dih[dih.size() * 9 / 10], dih[dih.size() * 99 / 100], spikes, 100.0 * spikes / dih.size());
    if (hasN) { long long agree = 0, disagree = 0, zero = 0; for (int k = 0; k < nt; k++) for (int j = 0; j < 3; j++) { unsigned vi = F[k * 3 + j]; if (vi >= (unsigned)nv) continue; float L = sqrtf(N[vi].x * N[vi].x + N[vi].y * N[vi].y + N[vi].z * N[vi].z); if (L < 1e-6f) { zero++; continue; } float d = (N[vi].x * fn[k].x + N[vi].y * fn[k].y + N[vi].z * fn[k].z) / L; if (d < 0) disagree++; else agree++; }
      wprintf(L"vertex normals vs face normals: agree %lld, opposed %lld (%.2f%%), zero-length %lld\n", agree, disagree, agree + disagree ? 100.0 * disagree / (agree + disagree) : 0, zero); } }
  if (hasUV) { int inside = 0; for (int i = 0; i < nv; i++) if (U[i] >= 0 && U[i] <= 1 && Vv[i] >= 0 && Vv[i] <= 1) inside++; wprintf(L"uv: %.2f%% of vertices inside [0,1]\n", 100.0 * inside / nv); }
  return 0;
}
