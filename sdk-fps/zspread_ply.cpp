// zspread_ply — the height-spread check (see zspread.cpp) on a folder of per-frame PLY files as Studio exports them
// (one file per frame, "<name>_<n>.ply", vertices in the scan's registered coordinates). No SDK needed.
// usage: zspread_ply <folder> <out.bmp>
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
struct Ply { std::vector<float> xyz; int n = 0; };
static bool readPly(const wchar_t* path, Ply& out) {
  FILE* f = _wfopen(path, L"rb"); if (!f) return false; char line[512]; bool binary = false, bigEndian = false; int nv = 0; std::vector<std::pair<std::string, int>> props;   // (name, byte size)
  bool inVertex = false; std::vector<std::pair<std::string, int>> faceProps; bool ok = false;
  while (fgets(line, sizeof line, f)) { std::string s = line; while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    if (s.rfind("format", 0) == 0) { binary = s.find("binary") != std::string::npos; bigEndian = s.find("big_endian") != std::string::npos; }
    else if (s.rfind("element vertex", 0) == 0) { nv = atoi(s.c_str() + 15); inVertex = true; }
    else if (s.rfind("element", 0) == 0) inVertex = false;
    else if (s.rfind("property", 0) == 0 && inVertex) { char t[32], nm[64]; if (sscanf(s.c_str(), "property %31s %63s", t, nm) == 2) { int sz = !strcmp(t, "float") || !strcmp(t, "float32") || !strcmp(t, "int") || !strcmp(t, "uint") || !strcmp(t, "int32") || !strcmp(t, "uint32") ? 4 : !strcmp(t, "double") || !strcmp(t, "float64") ? 8 : !strcmp(t, "uchar") || !strcmp(t, "uint8") || !strcmp(t, "char") || !strcmp(t, "int8") ? 1 : !strcmp(t, "short") || !strcmp(t, "ushort") || !strcmp(t, "int16") || !strcmp(t, "uint16") ? 2 : -1; props.push_back({nm, sz}); } }
    else if (s == "end_header") { ok = true; break; } }
  if (!ok || nv <= 0 || bigEndian) { fclose(f); return false; }
  int ix = -1, iy = -1, iz = -1, stride = 0; std::vector<int> off; for (auto& p : props) { if (p.second < 0) { fclose(f); return false; } off.push_back(stride); if (p.first == "x") ix = (int)off.size() - 1; if (p.first == "y") iy = (int)off.size() - 1; if (p.first == "z") iz = (int)off.size() - 1; stride += p.second; }
  if (ix < 0 || iy < 0 || iz < 0) { fclose(f); return false; } out.xyz.reserve((size_t)nv * 3);
  if (binary) { std::vector<unsigned char> buf((size_t)nv * stride); size_t got = fread(buf.data(), 1, buf.size(), f); fclose(f); if (got < buf.size()) return false;
    auto rd = [&](const unsigned char* p, int i) -> float { if (props[i].second == 4) { float v; memcpy(&v, p + off[i], 4); return v; } double d; memcpy(&d, p + off[i], 8); return (float)d; };
    for (int k = 0; k < nv; k++) { const unsigned char* p = &buf[(size_t)k * stride]; out.xyz.push_back(rd(p, ix)); out.xyz.push_back(rd(p, iy)); out.xyz.push_back(rd(p, iz)); } }
  else { for (int k = 0; k < nv; k++) { if (!fgets(line, sizeof line, f)) break; std::vector<float> vals; char* q = line; char* e; while (true) { double v = strtod(q, &e); if (e == q) break; vals.push_back((float)v); q = e; } if ((int)vals.size() < (int)props.size()) break; out.xyz.push_back(vals[ix]); out.xyz.push_back(vals[iy]); out.xyz.push_back(vals[iz]); } fclose(f); }
  out.n = (int)out.xyz.size() / 3; return out.n > 0;
}
int wmain(int argc, wchar_t** argv) {
  if (argc < 3) { wprintf(L"usage: zspread_ply folder out.bmp\n"); return 2; }
  std::wstring dir = argv[1]; std::vector<std::pair<int, std::wstring>> files; WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*.ply").c_str(), &fd);
  if (h != INVALID_HANDLE_VALUE) { do { std::wstring nm = fd.cFileName; size_t u = nm.find_last_of(L'_'); int idx = u == std::wstring::npos ? 0 : _wtoi(nm.c_str() + u + 1); files.push_back({idx, dir + L"\\" + nm}); } while (FindNextFileW(h, &fd)); FindClose(h); }
  std::sort(files.begin(), files.end()); int nf = (int)files.size(); if (!nf) { wprintf(L"no ply files\n"); return 1; }
  std::vector<float> xs, ys, zs; std::vector<int> fr; std::vector<double> cx(nf), cy(nf), cz(nf); float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f;
  for (int k = 0; k < nf; k++) { Ply p; if (!readPly(files[k].second.c_str(), p)) { wprintf(L"cannot read %ls\n", files[k].second.c_str()); continue; } double sx = 0, sy = 0, sz = 0;
    for (int i = 0; i < p.n; i++) { float X = p.xyz[i * 3], Y = p.xyz[i * 3 + 1], Z = p.xyz[i * 3 + 2]; sx += X; sy += Y; sz += Z; if (i % 2) continue; xs.push_back(X); ys.push_back(Y); zs.push_back(Z); fr.push_back(k); mnx = std::min(mnx, X); mxx = std::max(mxx, X); mny = std::min(mny, Y); mxy = std::max(mxy, Y); }
    cx[k] = sx / p.n; cy[k] = sy / p.n; cz[k] = sz / p.n; }
  wprintf(L"%d frames; centroids of frames 1, 2, %d: (%.0f %.0f %.0f) (%.0f %.0f %.0f) (%.0f %.0f %.0f)\n", nf, nf, cx[0], cy[0], cz[0], cx[1], cy[1], cz[1], cx[nf - 1], cy[nf - 1], cz[nf - 1]);
  const float cell = 1.0f; int W = (int)((mxx - mnx) / cell) + 3, H = (int)((mxy - mny) / cell) + 3; if (W > 4000 || H > 4000 || W < 1 || H < 1) { wprintf(L"map size %dx%d unusable: frames are probably not in a common coordinate system\n", W, H); return 1; }
  std::vector<std::vector<float>> czv((size_t)W * H); std::vector<std::vector<int>> cf((size_t)W * H);
  for (size_t i = 0; i < xs.size(); i++) { int gx = (int)((xs[i] - mnx) / cell) + 1, gy = (int)((ys[i] - mny) / cell) + 1; size_t c = (size_t)gy * W + gx; czv[c].push_back(zs[i]); cf[c].push_back(fr[i]); }
  std::vector<double> frameOff(nf, 0), frameSigned(nf, 0); std::vector<int> frameN(nf, 0); int bands[4] = {0, 0, 0, 0}; int nonEmpty = 0; std::vector<unsigned char> img((size_t)W * H * 3, 70);
  int wbands[4] = {0, 0, 0, 0}; int wcells = 0;
  for (size_t c = 0; c < czv.size(); c++) { auto& z = czv[c]; if (z.size() < 4) continue; nonEmpty++; std::vector<float> sorted = z; std::sort(sorted.begin(), sorted.end()); float med = sorted[sorted.size() / 2], lo = sorted[sorted.size() * 5 / 100], hi = sorted[sorted.size() * 95 / 100]; float sp = hi - lo;
    { std::vector<int> fs = cf[c]; std::sort(fs.begin(), fs.end()); int bestF = fs[0], bestN = 0, run = 0; for (size_t i = 0; i < fs.size(); i++) { run = (i && fs[i] == fs[i - 1]) ? run + 1 : 1; if (run > bestN) { bestN = run; bestF = fs[i]; } }
      if (bestN >= 4) { std::vector<float> zz; for (size_t i = 0; i < z.size(); i++) if (cf[c][i] == bestF) zz.push_back(z[i]); std::sort(zz.begin(), zz.end()); float wsp = zz[zz.size() * 95 / 100] - zz[zz.size() * 5 / 100]; wbands[wsp < 0.5f ? 0 : wsp < 1.0f ? 1 : wsp < 2.0f ? 2 : 3]++; wcells++; } }
    int b = sp < 0.5f ? 0 : sp < 1.0f ? 1 : sp < 2.0f ? 2 : 3; bands[b]++; unsigned char* px = &img[c * 3]; if (b == 0) { px[0] = px[1] = px[2] = 200; } else if (b == 1) { px[0] = 60; px[1] = 220; px[2] = 230; } else if (b == 2) { px[0] = 30; px[1] = 140; px[2] = 240; } else { px[0] = 30; px[1] = 30; px[2] = 230; }
    for (size_t i = 0; i < z.size(); i++) { frameOff[cf[c][i]] += fabs(z[i] - med); frameSigned[cf[c][i]] += z[i] - med; frameN[cf[c][i]]++; } }
  int rowB = W * 3, pad = (4 - rowB % 4) % 4; FILE* bf = _wfopen(argv[2], L"wb"); if (bf) { BITMAPFILEHEADER fh = {0x4D42, (DWORD)(54 + (rowB + pad) * H), 0, 0, 54}; BITMAPINFOHEADER ih = {40, W, H, 1, 24, 0, (DWORD)((rowB + pad) * H)}; fwrite(&fh, 14, 1, bf); fwrite(&ih, 40, 1, bf); unsigned char zp[3] = {0, 0, 0}; for (int y = 0; y < H; y++) { fwrite(&img[(size_t)y * W * 3], 3, W, bf); fwrite(zp, 1, pad, bf); } fclose(bf); }
  wprintf(L"%d cells with points. height spread per cell: <0.5 mm %.1f%%, 0.5-1 mm %.1f%%, 1-2 mm %.1f%%, >2 mm %.1f%%\n", nonEmpty, 100.0 * bands[0] / nonEmpty, 100.0 * bands[1] / nonEmpty, 100.0 * bands[2] / nonEmpty, 100.0 * bands[3] / nonEmpty);
  if (wcells) wprintf(L"WITHIN a single frame, same cells: <0.5 mm %.1f%%, 0.5-1 mm %.1f%%, 1-2 mm %.1f%%, >2 mm %.1f%%\n", 100.0 * wbands[0] / wcells, 100.0 * wbands[1] / wcells, 100.0 * wbands[2] / wcells, 100.0 * wbands[3] / wcells);
  int bad = 0; for (int k = 0; k < nf; k++) { if (!frameN[k]) continue; double o = frameOff[k] / frameN[k], sg = frameSigned[k] / frameN[k]; bool flag = o > 0.5; if (flag) bad++; wprintf(L"%s frame %3d: %.2f mm | %+.2f mm\n", flag ? L"!!" : L"  ", files[k].first, o, sg); }
  wprintf(L"%d of %d frames flagged (mean abs offset > 0.5 mm)\n", bad, nf); return 0;
}
