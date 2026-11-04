// Minimal OpenGL present benchmark: per frame upload a 1280x960 RGB texture, draw it, SwapBuffers.
// Reports ms per frame for swap interval 1 (vsync) and 0, and the swap call's own time. gltest.exe <outfile>
#include <windows.h>
#include <GL/gl.h>
#include <cstdio>
#include <vector>
#include <chrono>
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
using clk = std::chrono::steady_clock;
typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXTPROC)(int);
static LRESULT CALLBACK wp(HWND h, UINT m, WPARAM w, LPARAM l) { if (m == WM_CLOSE) PostQuitMessage(0); return DefWindowProc(h, m, w, l); }
int main(int argc, char** argv) {
  FILE* out = argc > 1 ? fopen(argv[1], "w") : stdout;
  WNDCLASSA wc = {}; wc.lpfnWndProc = wp; wc.hInstance = GetModuleHandle(nullptr); wc.lpszClassName = "gltest"; wc.style = CS_OWNDC; RegisterClassA(&wc);
  HWND hw = CreateWindowA("gltest", "gltest", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 1024, 768, nullptr, nullptr, wc.hInstance, nullptr);
  HDC dc = GetDC(hw); PIXELFORMATDESCRIPTOR pfd = { sizeof pfd, 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32 }; pfd.cDepthBits = 24;
  SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd); HGLRC rc = wglCreateContext(dc); wglMakeCurrent(dc, rc);
  fprintf(out, "renderer: %s | version: %s\n", (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));
  auto swapInterval = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
  const int W = 1280, H = 960; std::vector<unsigned char> img(W * H * 3);
  GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, W, H, 0, GL_RGB, GL_UNSIGNED_BYTE, img.data()); glEnable(GL_TEXTURE_2D);
  for (int mode = 0; mode < 4; mode++) {
    int interval = (mode == 0 || mode == 2) ? 1 : 0; bool upload = mode < 2;
    if (swapInterval) swapInterval(interval);
    double tSwap = 0, tUp = 0, tAll = 0; const int N = 120; MSG msg;
    for (int i = 0; i < N; i++) {
      while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
      auto a = clk::now();
      if (upload) { for (int k = 0; k < W * H * 3; k += 977) img[k] = (unsigned char)(i * 7 + k); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, img.data()); }
      auto b = clk::now();
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); glBegin(GL_QUADS); glTexCoord2f(0, 0); glVertex2f(-1, -1); glTexCoord2f(1, 0); glVertex2f(1, -1); glTexCoord2f(1, 1); glVertex2f(1, 1); glTexCoord2f(0, 1); glVertex2f(-1, 1); glEnd();
      auto c = clk::now(); SwapBuffers(dc); auto d = clk::now();
      if (i >= 10) { tUp += std::chrono::duration<double, std::milli>(b - a).count(); tSwap += std::chrono::duration<double, std::milli>(d - c).count(); tAll += std::chrono::duration<double, std::milli>(d - a).count(); }
    }
    int n = N - 10; fprintf(out, "vsync=%d upload=%d : frame %6.2f ms  (upload %5.2f, swap %6.2f)  -> %5.1f fps\n", interval, (int)upload, tAll / n, tUp / n, tSwap / n, 1000.0 * n / tAll); fflush(out);
  }
  if (out != stdout) fclose(out); wglMakeCurrent(nullptr, nullptr); wglDeleteContext(rc); DestroyWindow(hw); return 0;
}
