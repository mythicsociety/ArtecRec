// Page-fault / fresh-memory benchmark: allocate, touch, free, repeat. Mimics a per-frame buffer churn.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <cstdlib>
using clk = std::chrono::steady_clock;
static double ms(clk::time_point a) { return std::chrono::duration<double, std::milli>(clk::now() - a).count(); }
int main(int argc, char** argv) {
  size_t mb = argc > 1 ? atoi(argv[1]) : 16; int iters = argc > 2 ? atoi(argv[2]) : 100; size_t sz = mb << 20;
  SYSTEM_INFO si; GetSystemInfo(&si);
  printf("block %zu MB, %d iterations, page %lu\n", mb, iters, si.dwPageSize);
  // 1. VirtualAlloc commit + touch + free (pure demand-zero faults)
  double t1 = 0, t1max = 0; for (int i = 0; i < iters; i++) { auto a = clk::now(); void* p = VirtualAlloc(nullptr, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); memset(p, 1, sz); VirtualFree(p, 0, MEM_RELEASE); double d = ms(a); t1 += d; if (d > t1max) t1max = d; }
  printf("VirtualAlloc+touch+free : avg %.2f ms  max %.2f ms  -> %.0f faults/s equiv\n", t1 / iters, t1max, (sz / si.dwPageSize) / (t1 / iters / 1000.0));
  // 2. malloc + touch + free (CRT heap; large blocks go to the OS)
  double t2 = 0; for (int i = 0; i < iters; i++) { auto a = clk::now(); char* p = (char*)malloc(sz); memset(p, 1, sz); free(p); t2 += ms(a); }
  printf("malloc+touch+free       : avg %.2f ms\n", t2 / iters);
  // 3. touch already-committed memory (no faults) for comparison = pure memset bandwidth
  char* q = (char*)malloc(sz); memset(q, 1, sz); double t3 = 0; for (int i = 0; i < iters; i++) { auto a = clk::now(); memset(q, i & 0xff, sz); t3 += ms(a); }
  printf("memset only (no faults) : avg %.2f ms\n", t3 / iters); free(q);
  // 4. many small allocs (64 KB x 256 = 16 MB) + free
  double t4 = 0; void* blk[256]; for (int i = 0; i < iters; i++) { auto a = clk::now(); for (int k = 0; k < 256; k++) { blk[k] = malloc(65536); memset(blk[k], 1, 65536); } for (int k = 0; k < 256; k++) free(blk[k]); t4 += ms(a); }
  printf("256 x 64KB alloc/free   : avg %.2f ms\n", t4 / iters);
  return 0;
}
