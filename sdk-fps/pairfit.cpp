// pairfit — frame SHAPE quality, independent of any registration chain: every consecutive frame pair (k, k+1) is
// aligned on its own by rigid ICP (nearest point within 3 mm, Kabsch, 10 rounds) and the residual over the overlap is
// reported: RMS and the share of points within 0.3 mm. Good frames of a rigid stone align to ~0.1-0.2 mm; warped
// frames cannot, whatever the registration does. Works on an SDK project scan or a folder of per-frame PLY files.
// usage: pairfit <project.sproj> <scanIndex>   |   pairfit <folder-with-ply>
#include <artec/sdk/base/Log.h>
#include <artec/sdk/base/IModel.h>
#include <artec/sdk/base/IScan.h>
#include <artec/sdk/base/IFrameMesh.h>
#include <artec/sdk/base/AlgorithmWorkset.h>
#include <artec/sdk/base/IJob.h>
#include <artec/sdk/base/IArray.h>
#include <artec/sdk/base/Uuid.h>
#include <artec/sdk/project/IProject.h>
#include <artec/sdk/project/ProjectSettings.h>
#include <artec/sdk/project/EntryInfo.h>
#include <artec/sdk/project/ProjectLoaderSettings.h>
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <thread>
#include "rec_feat.h"
using namespace artec::sdk::base; namespace prj = artec::sdk::project;
struct Ply { std::vector<float> xyz; int n = 0; };
static bool readPly(const wchar_t* path, Ply& out) {   // binary little-endian or ascii, x y z float/double
  FILE* f = _wfopen(path, L"rb"); if (!f) return false; char line[512]; bool binary = false; int nv = 0; std::vector<std::pair<std::string, int>> props; bool inV = false, ok = false;
  while (fgets(line, sizeof line, f)) { std::string s = line; while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    if (s.rfind("format", 0) == 0) binary = s.find("binary") != std::string::npos; else if (s.rfind("element vertex", 0) == 0) { nv = atoi(s.c_str() + 15); inV = true; } else if (s.rfind("element", 0) == 0) inV = false;
    else if (s.rfind("property", 0) == 0 && inV) { char t[32], nm[64]; if (sscanf(s.c_str(), "property %31s %63s", t, nm) == 2) { std::string ts = t; int sz = ts == "float" || ts == "float32" || ts == "int" || ts == "uint" || ts == "int32" || ts == "uint32" ? 4 : ts == "double" || ts == "float64" ? 8 : ts == "uchar" || ts == "uint8" || ts == "char" || ts == "int8" ? 1 : ts == "short" || ts == "ushort" ? 2 : -1; props.push_back({nm, sz}); } }
    else if (s == "end_header") { ok = true; break; } }
  if (!ok || nv <= 0) { fclose(f); return false; } int ix = -1, iy = -1, iz = -1, stride = 0; std::vector<int> off; for (auto& p : props) { if (p.second < 0) { fclose(f); return false; } off.push_back(stride); if (p.first == "x") ix = (int)off.size() - 1; if (p.first == "y") iy = (int)off.size() - 1; if (p.first == "z") iz = (int)off.size() - 1; stride += p.second; }
  if (ix < 0 || iy < 0 || iz < 0) { fclose(f); return false; }
  if (binary) { std::vector<unsigned char> buf((size_t)nv * stride); size_t got = fread(buf.data(), 1, buf.size(), f); fclose(f); if (got < buf.size()) return false; auto rd = [&](const unsigned char* p, int i) -> float { if (props[i].second == 4) { float v; memcpy(&v, p + off[i], 4); return v; } double d; memcpy(&d, p + off[i], 8); return (float)d; };
    for (int k = 0; k < nv; k++) { const unsigned char* p = &buf[(size_t)k * stride]; out.xyz.push_back(rd(p, ix)); out.xyz.push_back(rd(p, iy)); out.xyz.push_back(rd(p, iz)); } }
  else { for (int k = 0; k < nv; k++) { if (!fgets(line, sizeof line, f)) break; std::vector<float> vals; char* q = line; char* e; while (true) { double v = strtod(q, &e); if (e == q) break; vals.push_back((float)v); q = e; } if ((int)vals.size() < (int)props.size()) break; out.xyz.push_back(vals[ix]); out.xyz.push_back(vals[iy]); out.xyz.push_back(vals[iz]); } fclose(f); }
  out.n = (int)out.xyz.size() / 3; return out.n > 0;
}
static double g_lastScale = 1;
static void mul(const double A[4][4], const double B[4][4], double o[4][4]) { for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { double s = 0; for (int k = 0; k < 4; k++) s += A[i][k] * B[k][j]; o[i][j] = s; } }
// ICP of points a onto grid of b (both world coords); returns RMS and share within 0.3 mm over the matched points
static void icp(const std::vector<float>& aIn, const feat::Grid& g, double& rms, double& share03, int& matched) {
  std::vector<float> a = aIn; double R[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, t[3] = {0, 0, 0}; int n = (int)a.size() / 3;
  for (int it = 0; it < 10; it++) { std::vector<float> src, dst; for (int i = 0; i < n; i++) { const float* p = &a[i * 3]; float x = (float)(R[0][0] * p[0] + R[0][1] * p[1] + R[0][2] * p[2] + t[0]), y = (float)(R[1][0] * p[0] + R[1][1] * p[1] + R[1][2] * p[2] + t[1]), z = (float)(R[2][0] * p[0] + R[2][1] * p[1] + R[2][2] * p[2] + t[2]);
      // nearest point of b within 3 mm: scan the grid cells (feat::Grid gives only the distance, so look up the point by brute force in the cell neighbourhood)
      int ix = (int)floorf(x / g.cell), iy = (int)floorf(y / g.cell), iz = (int)floorf(z / g.cell); float best = 9.f; const float* bp = nullptr;
      for (int dx = -2; dx <= 2; dx++) for (int dy = -2; dy <= 2; dy++) for (int dz = -2; dz <= 2; dz++) { long long k = feat::Grid::key(ix + dx, iy + dy, iz + dz); auto lo = std::lower_bound(g.idx.begin(), g.idx.end(), std::make_pair(k, -1)); for (; lo != g.idx.end() && lo->first == k; ++lo) { const float* q = &g.pts[lo->second * 3]; float d = (q[0] - x) * (q[0] - x) + (q[1] - y) * (q[1] - y) + (q[2] - z) * (q[2] - z); if (d < best) { best = d; bp = q; } } }
      if (!bp) continue; src.push_back(p[0]); src.push_back(p[1]); src.push_back(p[2]); dst.push_back(bp[0]); dst.push_back(bp[1]); dst.push_back(bp[2]); }
    matched = (int)src.size() / 3; if (matched < 50) { rms = -1; share03 = 0; return; }
    std::vector<int> idx(matched); for (int i = 0; i < matched; i++) idx[i] = i; double Rn[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, tn[3] = {0, 0, 0}; feat::refineRigid(src, dst, idx, Rn, tn); memcpy(R, Rn, sizeof R); memcpy(t, tn, sizeof t);
    if (it == 9) { double se = 0; int c03 = 0; for (int i = 0; i < matched; i++) { const float* p = &src[i * 3]; double x = R[0][0] * p[0] + R[0][1] * p[1] + R[0][2] * p[2] + t[0] - dst[i * 3], y = R[1][0] * p[0] + R[1][1] * p[1] + R[1][2] * p[2] + t[1] - dst[i * 3 + 1], z = R[2][0] * p[0] + R[2][1] * p[1] + R[2][2] * p[2] + t[2] - dst[i * 3 + 2]; double d2 = x * x + y * y + z * z; se += d2; if (d2 < 0.09) c03++; } rms = sqrt(se / matched); share03 = (double)c03 / matched;
      // best similarity scale on top of the rigid fit (Umeyama): s = sum(dst' . R src') / sum |src'|^2 — a frame that is
      // larger or smaller than its neighbour (distance-dependent depth scale) shows as s != 1
      double ca[3] = {0, 0, 0}, cb[3] = {0, 0, 0}; for (int i = 0; i < matched; i++) for (int k = 0; k < 3; k++) { ca[k] += src[i * 3 + k]; cb[k] += dst[i * 3 + k]; } for (int k = 0; k < 3; k++) { ca[k] /= matched; cb[k] /= matched; }
      double num = 0, den = 0; for (int i = 0; i < matched; i++) { double a[3], b[3]; for (int k = 0; k < 3; k++) { a[k] = src[i * 3 + k] - ca[k]; b[k] = dst[i * 3 + k] - cb[k]; } double ra[3]; for (int r = 0; r < 3; r++) ra[r] = R[r][0] * a[0] + R[r][1] * a[1] + R[r][2] * a[2]; num += b[0] * ra[0] + b[1] * ra[1] + b[2] * ra[2]; den += a[0] * a[0] + a[1] * a[1] + a[2] * a[2]; } g_lastScale = den > 0 ? num / den : 1; } }
}
int wmain(int argc, wchar_t** argv) {
  if (argc < 2) { wprintf(L"usage: pairfit project.sproj scanIndex | pairfit plyFolder\n"); return 2; } setOutputLevel(VerboseLevel_Error);
  std::vector<std::vector<float>> frames;   // world-coordinate points per frame, every 3rd point
  if (argc >= 3) { int want = _wtoi(argv[2]) - 1; prj::IProject* p = nullptr; if (prj::openProject(&p, argv[1]) != ErrorCode_OK || !p) { wprintf(L"openProject failed\n"); return 1; }
    std::vector<Uuid> v; for (int i = 0; i < p->getEntryCount(); i++) { prj::EntryInfo e; if (p->getEntry(i, &e, nullptr) == ErrorCode_OK && e.type == prj::EntryType_Scan) v.push_back(e.uuid); } if (want < 0 || want >= (int)v.size()) { wprintf(L"scan index out of range\n"); return 1; }
    IArrayUuid* ids = nullptr; createArrayUuid(&ids, 1); ids->setElement(0, v[want]); prj::ProjectLoaderSettings ls; ls.entryList = ids; IJob* loader = nullptr; p->createLoader(&loader, &ls); IModel* m = nullptr; createModel(&m);
    { AlgorithmWorkset ws; memset(&ws, 0, sizeof ws); ws.out = m; ws.threadsCount = std::thread::hardware_concurrency(); if (executeJob(loader, &ws) != ErrorCode_OK) { wprintf(L"load failed\n"); return 1; } loader->release(); } ids->release(); p->release();
    IScan* s = m->getElement(0); const auto& ST = s->getScanTransformation().m; for (int k = 0; k < s->getSize(); k++) { IFrameMesh* f = s->getElement(k); std::vector<float> w; if (f && f->getPoints()) { double M[4][4]; mul(ST, s->getTransformation(k).m, M); const Point3F* q = f->getPoints()->getPointer(); int n = f->getPoints()->getSize(); for (int i = 0; i < n; i += 3) { double x = q[i].x, y = q[i].y, z = q[i].z; w.push_back((float)(M[0][0] * x + M[0][1] * y + M[0][2] * z + M[0][3])); w.push_back((float)(M[1][0] * x + M[1][1] * y + M[1][2] * z + M[1][3])); w.push_back((float)(M[2][0] * x + M[2][1] * y + M[2][2] * z + M[2][3])); } } frames.push_back(std::move(w)); } }
  else { std::wstring dir = argv[1]; std::vector<std::pair<int, std::wstring>> files; WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*.ply").c_str(), &fd); if (h != INVALID_HANDLE_VALUE) { do { std::wstring nm = fd.cFileName; size_t u = nm.find_last_of(L'_'); files.push_back({u == std::wstring::npos ? 0 : _wtoi(nm.c_str() + u + 1), dir + L"\\" + nm}); } while (FindNextFileW(h, &fd)); FindClose(h); } std::sort(files.begin(), files.end());
    for (auto& fl : files) { Ply pl; std::vector<float> w; if (readPly(fl.second.c_str(), pl)) for (int i = 0; i < pl.n; i += 3) { w.push_back(pl.xyz[i * 3]); w.push_back(pl.xyz[i * 3 + 1]); w.push_back(pl.xyz[i * 3 + 2]); } frames.push_back(std::move(w)); } }
  int nf = (int)frames.size(); int stride = _wtoi(argc >= 3 && argc >= 4 ? argv[3] : (argc == 3 && argv[2][0] != L'\0' && !iswdigit(argv[2][0]) ? L"1" : L"1")); const wchar_t* env = _wgetenv(L"PAIRSTRIDE"); if (env) stride = _wtoi(env); if (stride < 1) stride = 1;
  wprintf(L"%d frames, pairs %d apart\n", nf, stride); std::vector<double> rmsAll, scAll; int shown = 0;
  for (int k = 0; k + stride < nf; k++) { if (frames[k].size() < 300 || frames[k + stride].size() < 300) continue; feat::Grid g; std::vector<float> b = frames[k]; g.build(std::move(b), 1.0f); double rms, sh; int matched; icp(frames[k + stride], g, rms, sh, matched); if (rms < 0 || matched < 2000) continue; rmsAll.push_back(rms); scAll.push_back(g_lastScale);
    if (shown++ < 40) wprintf(L"pair %3d-%3d: %6d matched, RMS %.2f mm, within 0.3 mm %3.0f%%, scale %.4f\n", k + 1, k + 1 + stride, matched, rms, sh * 100, g_lastScale); }
  std::sort(rmsAll.begin(), rmsAll.end()); std::sort(scAll.begin(), scAll.end()); if (!rmsAll.empty()) wprintf(L"pairs %d: RMS median %.2f mm, 90th pct %.2f mm, worst %.2f mm; scale 5th pct %.4f, median %.4f, 95th pct %.4f\n", (int)rmsAll.size(), rmsAll[rmsAll.size() / 2], rmsAll[rmsAll.size() * 9 / 10], rmsAll.back(), scAll[scAll.size() * 5 / 100], scAll[scAll.size() / 2], scAll[scAll.size() * 95 / 100]); return 0;
}
