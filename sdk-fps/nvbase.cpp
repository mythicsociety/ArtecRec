#include <windows.h>
#include <cstdio>
typedef int NvAPI_Status; typedef unsigned int NvU32; typedef unsigned short NvU16; typedef void* H;
#pragma pack(push,8)
struct NVDRS_BINARY { NvU32 valueLength; unsigned char valueData[4096]; };
struct NVDRS_SETTING { NvU32 version; NvU16 settingName[2048]; NvU32 settingId; NvU32 settingType; NvU32 settingLocation; NvU32 isCurrentPredefined; NvU32 isPredefinedValid; union { NvU32 u32PredefinedValue; NVDRS_BINARY b1; NvU16 w1[2048]; }; union { NvU32 u32CurrentValue; NVDRS_BINARY b2; NvU16 w2[2048]; }; };
#pragma pack(pop)
typedef void* (*QI_t)(NvU32);
int main(){ HMODULE h=LoadLibraryA("nvapi64.dll"); QI_t QI=(QI_t)GetProcAddress(h,"nvapi_QueryInterface");
 auto Init=(NvAPI_Status(*)())QI(0x0150E828); auto CS=(NvAPI_Status(*)(H*))QI(0x0694D52E); auto LS=(NvAPI_Status(*)(H))QI(0x375DBD6B); auto GB=(NvAPI_Status(*)(H,H*))QI(0xDA8466A0); auto GS=(NvAPI_Status(*)(H,H,NvU32,NVDRS_SETTING*))QI(0x73BF8338);
 Init(); H s; CS(&s); LS(s); H base; printf("GetBaseProfile=%d\n", GB(s,&base));
 NvU32 ids[3]={0x00A879CF,0x20C1221E,0x1057EB71}; const char* nm[3]={"VSYNCMODE","OGL_THREAD_CONTROL","PREFERRED_PSTATE"};
 for(int k=0;k<3;k++){ NVDRS_SETTING st={0}; st.version=(NvU32)sizeof(st)|(1u<<16); NvAPI_Status r=GS(s,base,ids[k],&st); if(r==0) printf("base %-20s = 0x%08X (%s)\n",nm[k],st.u32CurrentValue,st.isCurrentPredefined?"default":"user-set"); else printf("base %-20s : status %d\n",nm[k],r); }
 return 0; }
