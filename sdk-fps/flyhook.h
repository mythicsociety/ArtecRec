// In-process hook for FlyCapture2_v100.dll (the SDK's camera library): clamps the USB packet size that the SDK's
// flyCamera layer programs into each Point Grey camera, the way Studio's ForcePacketSizeSpider does.
// Env: FLYPKT=<bytes>  clamp absolute packet size;  FLYPKTPCT=<percent>  clamp the percent overload.  Unset = log only.
#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdlib>
namespace flyhook {
using FnAbs = void* (*)(void*, void*, const void*, unsigned int);
using FnPct = void* (*)(void*, void*, const void*, float);
using FnVal = void* (*)(void*, void*, const void*, bool*, unsigned int*);
static FnAbs origAbs; static FnPct origPct; static FnVal origVal;
static unsigned int limitAbs = 0; static float limitPct = 0; static int calls = 0;
static void* hookAbs(void* self, void* ret, const void* s, unsigned int pkt) {
  unsigned int use = (limitAbs && pkt > limitAbs) ? limitAbs : pkt;
  printf("[flyhook] SetFormat7Configuration(packet=%u) -> %u  (call %d)\n", pkt, use, ++calls); fflush(stdout);
  return origAbs(self, ret, s, use);
}
static void* hookPct(void* self, void* ret, const void* s, float pct) {
  float use = (limitPct > 0 && pct > limitPct) ? limitPct : pct;
  printf("[flyhook] SetFormat7Configuration(percent=%.1f) -> %.1f  (call %d)\n", pct, use, ++calls); fflush(stdout);
  return origPct(self, ret, s, use);
}
static void* hookVal(void* self, void* ret, const void* s, bool* valid, unsigned int* info) {
  void* r = origVal(self, ret, s, valid, info);
  if (info) printf("[flyhook] ValidateFormat7Settings: valid=%d unit=%u recommended=%u max=%u\n", valid ? (int)*valid : -1, info[0], info[1], info[2]);
  fflush(stdout); return r;
}
// Finds the Camera vtable by constructing a Camera with the exported constructor, then swaps the three slots.
static bool install() {
  const char* e = getenv("FLYPKT"); if (e) limitAbs = (unsigned)atoi(e);
  const char* p = getenv("FLYPKTPCT"); if (p) limitPct = (float)atof(p);
  HMODULE h = LoadLibraryA("FlyCapture2_v100.dll"); if (!h) { printf("[flyhook] FlyCapture2_v100.dll not found\n"); return false; }
  auto ctor = (void (*)(void*))GetProcAddress(h, "??0Camera@FlyCapture2@@QEAA@XZ");
  auto fAbs = GetProcAddress(h, "?SetFormat7Configuration@Camera@FlyCapture2@@UEAA?AVError@2@PEBUFormat7ImageSettings@2@I@Z");
  auto fPct = GetProcAddress(h, "?SetFormat7Configuration@Camera@FlyCapture2@@UEAA?AVError@2@PEBUFormat7ImageSettings@2@M@Z");
  auto fVal = GetProcAddress(h, "?ValidateFormat7Settings@Camera@FlyCapture2@@UEAA?AVError@2@PEBUFormat7ImageSettings@2@PEA_NPEAUFormat7PacketInfo@2@@Z");
  if (!ctor || !fAbs || !fPct || !fVal) { printf("[flyhook] exports missing\n"); return false; }
  alignas(16) unsigned char obj[512] = {0}; ctor(obj);
  void** vt = *(void***)obj; int done = 0;
  for (int i = 0; i < 200 && done < 3; i++) {
    void** slot = &vt[i]; void* tgt = nullptr; void* f = *slot;
    if (f == (void*)fAbs) { origAbs = (FnAbs)f; tgt = (void*)&hookAbs; }
    else if (f == (void*)fPct) { origPct = (FnPct)f; tgt = (void*)&hookPct; }
    else if (f == (void*)fVal) { origVal = (FnVal)f; tgt = (void*)&hookVal; }
    if (!tgt) continue;
    DWORD old; if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) { printf("[flyhook] VirtualProtect failed slot %d\n", i); return false; }
    *slot = tgt; VirtualProtect(slot, sizeof(void*), old, &old); done++;
    printf("[flyhook] patched vtable slot %d\n", i);
  }
  // The SDK's flyCamera DLL calls these through its import table (devirtualised), so patch its IAT as well.
  HMODULE fc = LoadLibraryA("artec-sdk-flyCamera.dll"); int iat = 0;
  if (fc) {
    auto base = (unsigned char*)fc; auto dos = (IMAGE_DOS_HEADER*)base; auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto d = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); d->Name; d++) {
      if (_stricmp((const char*)(base + d->Name), "FlyCapture2_v100.dll")) continue;
      for (auto t = (IMAGE_THUNK_DATA*)(base + d->FirstThunk); t->u1.Function; t++) {
        void* f = (void*)t->u1.Function; void* tgt = nullptr;
        if (f == (void*)fAbs) { origAbs = (FnAbs)f; tgt = (void*)&hookAbs; }
        else if (f == (void*)fPct) { origPct = (FnPct)f; tgt = (void*)&hookPct; }
        else if (f == (void*)fVal) { origVal = (FnVal)f; tgt = (void*)&hookVal; }
        if (!tgt) continue;
        DWORD old; VirtualProtect(&t->u1.Function, sizeof(void*), PAGE_READWRITE, &old); t->u1.Function = (ULONGLONG)tgt; VirtualProtect(&t->u1.Function, sizeof(void*), old, &old); iat++;
      }
    }
  }
  printf("[flyhook] installed %d/3 vtable + %d/3 IAT hooks; FLYPKT=%u FLYPKTPCT=%.1f\n", done, iat, limitAbs, limitPct); fflush(stdout);
  return iat == 3;
}
}
