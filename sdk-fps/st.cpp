// Single-thread compute benchmark (integer, float, memory-latency), 3 passes.
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <vector>
#include <cmath>
using clk = std::chrono::steady_clock;
int main() {
  for (int pass = 0; pass < 3; pass++) {
    auto a = clk::now(); unsigned long long x = 1; for (long i = 0; i < 400000000L; i++) x = x * 6364136223846793005ull + 1442695040888963407ull ^ (x >> 13);
    double ti = std::chrono::duration<double, std::milli>(clk::now() - a).count();
    a = clk::now(); double s = 0; for (long i = 1; i < 100000000L; i++) s += std::sqrt((double)i) * 1.0000001;
    double tf = std::chrono::duration<double, std::milli>(clk::now() - a).count();
    std::vector<unsigned> v(32 << 20); for (size_t i = 0; i < v.size(); i++) v[i] = (unsigned)((i * 2654435761u) % v.size());
    a = clk::now(); unsigned p = 0; for (int i = 0; i < 20000000; i++) p = v[p];
    double tm = std::chrono::duration<double, std::milli>(clk::now() - a).count();
    printf("pass %d: int %6.0f ms   float %6.0f ms   mem-chase %6.0f ms   (%llu %f %u)\n", pass, ti, tf, tm, x & 1, s > 0 ? 1.0 : 0.0, p & 1);
  }
  return 0;
}
