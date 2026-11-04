// Kernel-transition micro-benchmark: cost per call of common syscalls and a context switch.
#include <windows.h>
#include <cstdio>
#include <chrono>
#include <thread>
using clk = std::chrono::steady_clock;
static double ns_per(clk::time_point a, int n) { return std::chrono::duration<double, std::nano>(clk::now() - a).count() / n; }
int main() {
  const int N = 200000;
  HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  auto a = clk::now(); for (int i = 0; i < N; i++) SetEvent(ev); printf("SetEvent                 : %7.0f ns\n", ns_per(a, N));
  ResetEvent(ev);
  a = clk::now(); for (int i = 0; i < N; i++) WaitForSingleObject(ev, 0); printf("WaitForSingleObject(0)   : %7.0f ns\n", ns_per(a, N));
  MEMORY_BASIC_INFORMATION mbi; a = clk::now(); for (int i = 0; i < N; i++) VirtualQuery(&mbi, &mbi, sizeof mbi); printf("VirtualQuery             : %7.0f ns\n", ns_per(a, N));
  a = clk::now(); for (int i = 0; i < N; i++) Sleep(0); printf("Sleep(0)                 : %7.0f ns\n", ns_per(a, N));
  a = clk::now(); for (int i = 0; i < N; i++) SwitchToThread(); printf("SwitchToThread           : %7.0f ns\n", ns_per(a, N));
  HANDLE f = CreateFileW(L"NUL", GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr); DWORD w;
  a = clk::now(); for (int i = 0; i < N; i++) WriteFile(f, "x", 1, &w, nullptr); printf("WriteFile(NUL,1 byte)    : %7.0f ns\n", ns_per(a, N));
  a = clk::now(); for (int i = 0; i < N; i++) GetProcessId(GetCurrentProcess()); printf("GetProcessId             : %7.0f ns\n", ns_per(a, N));
  // 20 x 1 MB VirtualAlloc/touch/free per iteration = allocation syscalls + faults
  a = clk::now(); for (int i = 0; i < 2000; i++) { void* p = VirtualAlloc(nullptr, 1 << 20, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); memset(p, 1, 1 << 20); VirtualFree(p, 0, MEM_RELEASE); } printf("VirtualAlloc 1MB+touch+free: %7.0f ns\n", ns_per(a, 2000));
  // context-switch ping-pong between two threads via auto-reset events
  HANDLE e1 = CreateEventW(nullptr, FALSE, FALSE, nullptr), e2 = CreateEventW(nullptr, FALSE, FALSE, nullptr); const int M = 50000;
  std::thread t([&] { for (int i = 0; i < M; i++) { WaitForSingleObject(e1, INFINITE); SetEvent(e2); } });
  a = clk::now(); for (int i = 0; i < M; i++) { SetEvent(e1); WaitForSingleObject(e2, INFINITE); } printf("thread ping-pong (2 switches): %7.0f ns\n", ns_per(a, M)); t.join();
  // timer resolution actually in effect
  a = clk::now(); for (int i = 0; i < 20; i++) Sleep(1); printf("Sleep(1) actual          : %7.2f ms\n", ns_per(a, 20) / 1e6);
  return 0;
}
