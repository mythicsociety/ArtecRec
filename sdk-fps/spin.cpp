// Keeps every core awake at IDLE priority (never competes with real work). spin.exe <seconds>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include <atomic>
int main(int argc, char** argv) {
  int secs = argc > 1 ? atoi(argv[1]) : 300;
  SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
  unsigned n = std::thread::hardware_concurrency(); std::atomic<bool> stop{false};
  std::vector<std::thread> t; volatile unsigned long long sink = 0;
  for (unsigned i = 0; i < n; i++) t.emplace_back([&, i] { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE); SetThreadAffinityMask(GetCurrentThread(), 1ull << i); unsigned long long x = i; while (!stop) { for (int k = 0; k < 100000; k++) x = x * 6364136223846793005ull + 1442695040888963407ull; sink = x; } });
  printf("spinning %u threads at idle priority for %d s\n", n, secs); fflush(stdout);
  Sleep(secs * 1000); stop = true; for (auto& th : t) th.join(); return 0;
}
