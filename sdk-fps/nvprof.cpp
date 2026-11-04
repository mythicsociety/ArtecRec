// Set NVIDIA driver profile settings for an application via NVAPI DRS (no Control Panel needed).
// usage: nvprof.exe <exe-name> [threaded 0|1|2] [vsync off|on|app]      (read-only when no settings given)
//   threaded: 1=on 2=off 0=default.   Shows the current values after applying.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
typedef int NvAPI_Status; typedef unsigned int NvU32; typedef unsigned short NvU16;
typedef void* NvDRSSessionHandle; typedef void* NvDRSProfileHandle;
#pragma pack(push, 8)
struct NVDRS_PROFILE { NvU32 version; NvU16 profileName[2048]; NvU32 gpuSupport; NvU32 isPredefined; NvU32 numOfApps; NvU32 numOfSettings; };
struct NVDRS_APPLICATION_V1 { NvU32 version; NvU32 isPredefined; NvU16 appName[2048]; NvU16 userFriendlyName[2048]; NvU16 launcher[2048]; };
struct NVDRS_BINARY { NvU32 valueLength; unsigned char valueData[4096]; };
struct NVDRS_SETTING { NvU32 version; NvU16 settingName[2048]; NvU32 settingId; NvU32 settingType; NvU32 settingLocation; NvU32 isCurrentPredefined; NvU32 isPredefinedValid;
  union { NvU32 u32PredefinedValue; NVDRS_BINARY binaryPredefinedValue; NvU16 wszPredefinedValue[2048]; };
  union { NvU32 u32CurrentValue; NVDRS_BINARY binaryCurrentValue; NvU16 wszCurrentValue[2048]; }; };
#pragma pack(pop)
#define VER(s, n) ((NvU32)sizeof(s) | ((n) << 16))
typedef void* (*QI_t)(NvU32);
static QI_t QI;
template <class T> T fn(NvU32 id) { return (T)QI(id); }
typedef NvAPI_Status (*Initialize_t)(); typedef NvAPI_Status (*Unload_t)();
typedef NvAPI_Status (*CreateSession_t)(NvDRSSessionHandle*); typedef NvAPI_Status (*DestroySession_t)(NvDRSSessionHandle);
typedef NvAPI_Status (*LoadSettings_t)(NvDRSSessionHandle); typedef NvAPI_Status (*SaveSettings_t)(NvDRSSessionHandle);
typedef NvAPI_Status (*FindApplicationByName_t)(NvDRSSessionHandle, NvU16*, NvDRSProfileHandle*, NVDRS_APPLICATION_V1*);
typedef NvAPI_Status (*FindProfileByName_t)(NvDRSSessionHandle, NvU16*, NvDRSProfileHandle*);
typedef NvAPI_Status (*CreateProfile_t)(NvDRSSessionHandle, NVDRS_PROFILE*, NvDRSProfileHandle*);
typedef NvAPI_Status (*CreateApplication_t)(NvDRSSessionHandle, NvDRSProfileHandle, NVDRS_APPLICATION_V1*);
typedef NvAPI_Status (*SetSetting_t)(NvDRSSessionHandle, NvDRSProfileHandle, NVDRS_SETTING*);
typedef NvAPI_Status (*GetSetting_t)(NvDRSSessionHandle, NvDRSProfileHandle, NvU32, NVDRS_SETTING*);
typedef NvAPI_Status (*GetProfileInfo_t)(NvDRSSessionHandle, NvDRSProfileHandle, NVDRS_PROFILE*);
typedef NvAPI_Status (*GetErrorMessage_t)(NvAPI_Status, char*);
static const NvU32 OGL_THREAD_CONTROL_ID = 0x20C1221E, VSYNCMODE_ID = 0x00A879CF;
static const NvU32 VSYNC_OFF = 0x08416747, VSYNC_ON = 0x47814940, VSYNC_APP = 0x60925292;
static void wcpy(NvU16* d, const wchar_t* s, size_t n) { for (size_t i = 0; i < n && s[i]; i++) d[i] = (NvU16)s[i]; }
int wmain(int argc, wchar_t** argv) {
  if (argc < 2) { printf("usage: nvprof <exe-name> [threaded 0|1|2] [vsync off|on|app]\n"); return 1; }
  HMODULE h = LoadLibraryA("nvapi64.dll"); if (!h) { printf("nvapi64.dll not found\n"); return 2; }
  QI = (QI_t)GetProcAddress(h, "nvapi_QueryInterface"); if (!QI) { printf("no QueryInterface\n"); return 2; }
  auto Initialize = fn<Initialize_t>(0x0150E828); auto Unload = fn<Unload_t>(0xD22BDD7E);
  auto CreateSession = fn<CreateSession_t>(0x0694D52E); auto DestroySession = fn<DestroySession_t>(0xDAD9CFF8);
  auto LoadSettings = fn<LoadSettings_t>(0x375DBD6B); auto SaveSettings = fn<SaveSettings_t>(0xFCBC7E14);
  auto FindApplicationByName = fn<FindApplicationByName_t>(0xEEE566B2); auto FindProfileByName = fn<FindProfileByName_t>(0x7E4A9A0B);
  auto CreateProfile = fn<CreateProfile_t>(0xCC176068); auto CreateApplication = fn<CreateApplication_t>(0x4347A9DE);
  auto SetSetting = fn<SetSetting_t>(0x577DD202); auto GetSetting = fn<GetSetting_t>(0x73BF8338); auto GetProfileInfo = fn<GetProfileInfo_t>(0x61CD6FD6);
  auto GetErrorMessage = fn<GetErrorMessage_t>(0x6C2D048C);
  if (!Initialize || !CreateSession || !LoadSettings || !FindApplicationByName || !SetSetting || !GetSetting) { printf("missing NVAPI entry points\n"); return 2; }
  auto chk = [&](NvAPI_Status s, const char* what) { if (s != 0) { char m[64] = {0}; if (GetErrorMessage) GetErrorMessage(s, m); printf("%s: status %d %s\n", what, s, m); } return s == 0; };
  if (!chk(Initialize(), "Initialize")) return 3;
  NvDRSSessionHandle ses = nullptr; if (!chk(CreateSession(&ses), "CreateSession")) return 3;
  if (!chk(LoadSettings(ses), "LoadSettings")) return 3;
  NvU16 app[2048] = {0}; wcpy(app, argv[1], 2047);
  NvDRSProfileHandle prof = nullptr; NVDRS_APPLICATION_V1 a; memset(&a, 0, sizeof a); a.version = VER(NVDRS_APPLICATION_V1, 1);
  NvAPI_Status s = FindApplicationByName(ses, app, &prof, &a);
  if (s == 0) { NVDRS_PROFILE p; memset(&p, 0, sizeof p); p.version = VER(NVDRS_PROFILE, 1); GetProfileInfo(ses, prof, &p); wprintf(L"application %s found in profile \"%s\" (predefined=%u, settings=%u)\n", argv[1], (wchar_t*)p.profileName, p.isPredefined, p.numOfSettings); }
  else {
    printf("no profile holds %ls (status %d); creating profile \"Artec Studio\"\n", argv[1], s);
    NvU16 pname[2048] = {0}; wcpy(pname, L"Artec Studio", 2047);
    if (FindProfileByName(ses, pname, &prof) != 0) { NVDRS_PROFILE p; memset(&p, 0, sizeof p); p.version = VER(NVDRS_PROFILE, 1); wcpy(p.profileName, L"Artec Studio", 2047); if (!chk(CreateProfile(ses, &p, &prof), "CreateProfile")) return 4; }
    memset(&a, 0, sizeof a); a.version = VER(NVDRS_APPLICATION_V1, 1); wcpy(a.appName, argv[1], 2047); wcpy(a.userFriendlyName, L"Artec Studio", 2047);
    if (!chk(CreateApplication(ses, prof, &a), "CreateApplication")) return 4;
  }
  bool changed = false;
  for (int i = 2; i + 1 < argc; i += 2) {
    NVDRS_SETTING st; memset(&st, 0, sizeof st); st.version = VER(NVDRS_SETTING, 1); st.settingType = 0; st.settingLocation = 0;
    if (!wcscmp(argv[i], L"threaded")) { st.settingId = OGL_THREAD_CONTROL_ID; st.u32CurrentValue = (NvU32)_wtoi(argv[i + 1]); }
    else if (!wcscmp(argv[i], L"vsync")) { st.settingId = VSYNCMODE_ID; st.u32CurrentValue = !wcscmp(argv[i + 1], L"off") ? VSYNC_OFF : !wcscmp(argv[i + 1], L"on") ? VSYNC_ON : VSYNC_APP; }
    else { printf("unknown setting %ls\n", argv[i]); continue; }
    if (chk(SetSetting(ses, prof, &st), "SetSetting")) { printf("set 0x%08X = 0x%08X\n", st.settingId, st.u32CurrentValue); changed = true; }
  }
  if (changed && !chk(SaveSettings(ses), "SaveSettings")) return 5;
  if (changed) printf("saved.\n");
  NvU32 ids[2] = { OGL_THREAD_CONTROL_ID, VSYNCMODE_ID }; const char* names[2] = { "Threaded optimization (1=on 2=off 0=default)", "Vertical sync" };
  for (int k = 0; k < 2; k++) { NVDRS_SETTING st; memset(&st, 0, sizeof st); st.version = VER(NVDRS_SETTING, 1);
    s = GetSetting(ses, prof, ids[k], &st); if (s == 0) printf("  %-46s = 0x%08X (%s)\n", names[k], st.u32CurrentValue, st.isCurrentPredefined ? "inherited/default" : "set in profile"); else printf("  %-46s : not set (status %d)\n", names[k], s); }
  DestroySession(ses); Unload(); return 0;
}
