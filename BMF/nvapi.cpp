/**
* This file is part of Batman "Fix".
*
* Batman Tweak is free software : you can redistribute it and / or modify
* it under the terms of the GNU General Public License as published by
* The Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* Batman Tweak is distributed in the hope that it will be useful,
* But WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Batman Tweak.If not, see <http://www.gnu.org/licenses/>.
**/

#define _CRT_NON_CONFORMING_SWPRINTFS
#define _CRT_SECURE_NO_WARNINGS

#include "nvapi.h"
#include "nvapi/NvApiDriverSettings.h"

#include <Windows.h>
#include <dxgi.h>
#include <string>

#pragma comment (lib, "nvapi/amd64/nvapi64.lib")

using namespace bmf;
using namespace bmf::NVAPI;

static bool nvapi_silent = false;

#define NVAPI_SILENT()  { nvapi_silent = true;  }
#define NVAPI_VERBOSE() { nvapi_silent = false; }

#if 0
#define NVAPI_CALL(x) { NvAPI_Status ret = NvAPI_##x;        \
                        if (nvapi_silent != true &&          \
                                     ret != NVAPI_OK)        \
                          MessageBox (                       \
                            NVAPI_ErrorMessage (             \
                              ret, #x, __LINE__,             \
                              __FUNCTION__, __FILE__),       \
                            L"Error Calling NVAPI Function", \
                            MB_OK | MB_ICONASTERISK );       \
                      }
#else
#define NVAPI_CALL(x) { NvAPI_Status ret = NvAPI_##x; if (nvapi_silent != true && ret != NVAPI_OK) MessageBox (NULL, ErrorMessage (ret, #x, __LINE__, __FUNCTION__, __FILE__).c_str (), L"Error Calling NVAPI Function", MB_OK | MB_ICONASTERISK ); }
#define NVAPI_CALL2(x,y) { ##y = NvAPI_##x; if (nvapi_silent != true && ##y != NVAPI_OK) MessageBox (NULL, ErrorMessage (##y, #x, __LINE__, __FUNCTION__, __FILE__).c_str (), L"Error Calling NVAPI Function", MB_OK | MB_ICONASTERISK); }
#endif

#define NVAPI_SET_DWORD(x,y,z) (x).version = NVDRS_SETTING_VER; (x).settingId = (y); (x).settingType = NVDRS_DWORD_TYPE; (x).u32CurrentValue = (z);


std::wstring
NVAPI::ErrorMessage (_NvAPI_Status err,
                     const char*   args,
                     UINT          line_no,
                     const char*   function_name,
                     const char*   file_name)
{
  char szError [64];

  NvAPI_GetErrorMessage (err, szError);

  wchar_t wszError          [64];
  wchar_t wszFile           [256];
  wchar_t wszFunction       [256];
  wchar_t wszArgs           [256];
  wchar_t wszFormattedError [1024];

  MultiByteToWideChar (CP_OEMCP, 0, szError,       -1, wszError,     64);
  MultiByteToWideChar (CP_OEMCP, 0, file_name,     -1, wszFile,     256);
  MultiByteToWideChar (CP_OEMCP, 0, function_name, -1, wszFunction, 256);
  MultiByteToWideChar (CP_OEMCP, 0, args,          -1, wszArgs,     256);
  *wszFormattedError = L'\0';

  swprintf ( wszFormattedError, 1024,
              L"Line %u of %s (in %s (...)):\n"
              L"------------------------\n\n"
              L"NvAPI_%s\n\n\t>> %s <<",
               line_no,
                wszFile,
                 wszFunction,
                  wszArgs,
                   wszError );

  return wszFormattedError;
}

int
bmf::NVAPI::CountPhysicalGPUs (void)
{
  static int nv_gpu_count = -1;

  if (nv_gpu_count == -1) {
    if (nv_hardware) {
      NvPhysicalGpuHandle gpus [64];
      NvU32               gpu_count = 0;

      NVAPI_CALL (EnumPhysicalGPUs (gpus, &gpu_count));

      nv_gpu_count = gpu_count;
    }
    else {
      nv_gpu_count = 0;
    }
  }

  return nv_gpu_count;
}

int
NVAPI::CountSLIGPUs (void)
{
  static int nv_sli_count = -1;

  DXGI_ADAPTER_DESC* adapters = NVAPI::EnumGPUs_DXGI ();

  if (nv_sli_count == -1) {
    if (nv_hardware) {
      nv_sli_count = 0;

      while (adapters != nullptr) {
        if (adapters->AdapterLuid.LowPart > 1)
          nv_sli_count++;

        ++adapters;

        if (*adapters->Description == '\0')
          break;
      }
    }
  }

  return nv_sli_count;
}

static DXGI_ADAPTER_DESC   _nv_sli_adapters [64];

DXGI_ADAPTER_DESC*
NVAPI::EnumSLIGPUs (void)
{
  static int nv_sli_count = -1;

  *_nv_sli_adapters [0].Description = L'\0';

  DXGI_ADAPTER_DESC* adapters = NVAPI::EnumGPUs_DXGI ();

  if (nv_sli_count == -1) {
    if (nv_hardware) {
      nv_sli_count = 0;

      while (adapters != nullptr) {
        if (adapters->AdapterLuid.LowPart > 1)
          memcpy (&_nv_sli_adapters [nv_sli_count++], adapters, sizeof (DXGI_ADAPTER_DESC));

        ++adapters;

        if (*adapters->Description == '\0')
          break;
      }

      *_nv_sli_adapters [nv_sli_count].Description = L'\0';
    }
  }

  return _nv_sli_adapters;
}

/**
 * These were hoisted out of EnumGPUs_DXGI (...) to reduce stack size.
 **/
static DXGI_ADAPTER_DESC   _nv_dxgi_adapters [64];
static NvPhysicalGpuHandle _nv_dxgi_gpus     [64];
static NvPhysicalGpuHandle phys [64];

// This function does much more than it's supposed to -- consider fixing that!
DXGI_ADAPTER_DESC*
bmf::NVAPI::EnumGPUs_DXGI (void)
{
  // Only do this once...
  static bool enumerated = false;

  // Early-out if this was already called once before.
  if (enumerated)
    return _nv_dxgi_adapters;

  if (! nv_hardware) {
    enumerated = true;
    *_nv_dxgi_adapters [0].Description = L'\0';
    return _nv_dxgi_adapters;
  }

  NvU32 gpu_count     = 0;

  NVAPI_CALL (EnumPhysicalGPUs (_nv_dxgi_gpus, &gpu_count));

  for (int i = 0; i < CountPhysicalGPUs (); i++) {
    DXGI_ADAPTER_DESC adapterDesc;

    NvAPI_ShortString name;

    int   sli_group = 0;
    int   sli_size  = 0;

    NVAPI_CALL (EnumPhysicalGPUs (_nv_dxgi_gpus,     &gpu_count));

    NvU32              phys_count;
    NvLogicalGpuHandle logical;

    NVAPI_CALL (GetLogicalGPUFromPhysicalGPU  (_nv_dxgi_gpus [i], &logical));
    NVAPI_CALL (GetPhysicalGPUsFromLogicalGPU (logical, phys, &phys_count));

    sli_group = (size_t)logical & 0xffff;
    sli_size  = phys_count;

    NVAPI_CALL (GPU_GetFullName (_nv_dxgi_gpus [i], name));

    NV_DISPLAY_DRIVER_MEMORY_INFO meminfo;
    meminfo.version = NV_DISPLAY_DRIVER_MEMORY_INFO_VER;

    NVAPI_CALL (GPU_GetMemoryInfo (_nv_dxgi_gpus [i], &meminfo));

    MultiByteToWideChar (CP_OEMCP, 0, name, -1, adapterDesc.Description, 64);

    adapterDesc.VendorId = 0x10de;

    adapterDesc.AdapterLuid.HighPart = sli_group;
    adapterDesc.AdapterLuid.LowPart  = sli_size;

    // NVIDIA's driver measures these numbers in KiB (to store as a 32-bit int)
    //  * We want the numbers in bytes (64-bit)
    adapterDesc.DedicatedVideoMemory  = (size_t)meminfo.dedicatedVideoMemory * 1024;
    adapterDesc.DedicatedSystemMemory = (size_t)meminfo.systemVideoMemory    * 1024;
    adapterDesc.SharedSystemMemory    = (size_t)meminfo.sharedSystemMemory   * 1024;

    _nv_dxgi_adapters [i] = adapterDesc;
  }

  *_nv_dxgi_adapters [gpu_count].Description = L'\0';

#if 0
  NvDRSSessionHandle hSession;
  NVAPI_CALL (DRS_CreateSession (&hSession));

  NvDRSProfileHandle hProfile;

  NVDRS_APPLICATION app;
  app.version = NVDRS_APPLICATION_VER;

  NVAPI_CALL (DRS_LoadSettings (hSession));

  //extern std::wstring executable;

  NvAPI_Status ret;

  NVAPI_CALL2 (DRS_FindApplicationByName (hSession, (NvU16 *)L"batmanak.exe", &hProfile, &app), ret);

#if 0
  NvU32 max_count = 2048;
  NvU32 setting_ids [2048];
  NvAPI_DRS_EnumAvailableSettingIds (setting_ids, &max_count);

  for (int i = 0; i < max_count; i++) {
    wchar_t setting [2048];
    NvAPI_DRS_GetSettingNameFromId (setting_ids [i], (NvAPI_UnicodeString *)setting);
    MessageBox (NULL, setting, L"Driver Setting", MB_OK);
  }
#endif

  if (ret == NVAPI_OK) {
    NVAPI_SILENT ();

  //NVAPI_CALL2 (DRS_GetSetting (hSession, hProfile, MAXWELL_B_SAMPLE_INTERLEAVE_ID, &misc->mfaa), ret);

  //if (ret == NVAPI_OK)
    //support_mfaa = true;

    NVAPI_CALL (DRS_GetSetting (hSession, hProfile, PRERENDERLIMIT_ID,   &misc->prerender_limit));
    NVAPI_CALL (DRS_GetSetting (hSession, hProfile, PREFERRED_PSTATE_ID, &misc->power_policy));

    // Don't load this stuff if there's only 1 GPU...
    if (CountPhysicalGPUs () > 1) {
      NVAPI_CALL (DRS_GetSetting (hSession, hProfile, SLI_RENDERING_MODE_ID, &sli->mode));
      NVAPI_CALL (DRS_GetSetting (hSession, hProfile, VSYNCSMOOTHAFR_ID,     &vsync->smooth));
    }

    NVAPI_CALL (DRS_GetSetting (hSession, hProfile, VSYNCMODE_ID,        &vsync->mode));
    NVAPI_CALL (DRS_GetSetting (hSession, hProfile, VSYNCTEARCONTROL_ID, &vsync->adaptive));

    NVAPI_CALL (DRS_GetSetting (hSession, hProfile, LODBIASADJUST_ID,               &lodbias->adjust));
    NVAPI_CALL (DRS_GetSetting (hSession, hProfile, PS_TEXFILTER_NO_NEG_LODBIAS_ID, &lodbias->allow_negative));

    NVAPI_VERBOSE ();
  }

  NVAPI_CALL (DRS_DestroySession (hSession));
#endif

  enumerated = true;

  return _nv_dxgi_adapters;
}

DXGI_ADAPTER_DESC*
NVAPI::FindGPUByDXGIName (const wchar_t* wszName)
{
  DXGI_ADAPTER_DESC* adapters = EnumGPUs_DXGI ();

  //"NVIDIA "
  // 01234567

  while (*adapters->Description != L'\0') {
    if (wcsstr (adapters->Description, wszName + 7) != NULL) {//strstrw (lstrcmpiW () == 0) {
      return adapters;
    }

    ++adapters;
  }

  return NULL;
}

#include "nvapi/nvapi_lite_common.h"



void SaveDriverTweaksNV (HWND hDlg)
{
#if 0
  NvDRSSessionHandle hSession;
  NVAPI_CALL (DRS_CreateSession (&hSession));

  NvDRSProfileHandle hProfile;

  NVDRS_APPLICATION app;
  app.version = NVDRS_APPLICATION_VER;

  NVAPI_CALL (DRS_LoadSettings (hSession));

  extern std::wstring executable;

  // We expect to fail here, just go with it if it happens... we can fix it.
  NVAPI_SILENT ();

  NvAPI_Status ret;
  NVAPI_CALL2 (DRS_FindApplicationByName (hSession, (NvU16 *)L"batmanak.exe", &hProfile, &app), ret);

  if (ret == NVAPI_EXECUTABLE_NOT_FOUND) {
    NVAPI_CALL2 (DRS_FindApplicationByName (hSession, (NvU16 *)L"bmgame.exe", &hProfile, &app), ret);

    NVAPI_VERBOSE ();

    if (ret == NVAPI_OK) {
      lstrcpyW ((wchar_t *)app.appName, L"batmanak.exe");
      lstrcpyW ((wchar_t *)app.userFriendlyName, L"Batman: Arkham Knight");
      app.isPredefined = false;

      NvAPI_DRS_CreateApplication (hSession, hProfile, &app);
    }
    else {
      return;
    }
  }

  NVAPI_VERBOSE ();

  NvU32 setting_id = 0;

  NVAPI_CALL (DRS_GetSettingIdFromName ((NvU16 *)PRERENDERLIMIT_STRING, &setting_id));

  if (ret == NVAPI_OK) {
    NVAPI_SET_DWORD (misc->prerender_limit, setting_id, misc->poll_prerender_limit ());
    NVAPI_CALL      (DRS_SetSetting (hSession, hProfile, &misc->prerender_limit));

    NVAPI_SET_DWORD (misc->power_policy, PREFERRED_PSTATE_ID, misc->poll_power_policy ());
    NVAPI_CALL      (DRS_SetSetting (hSession, hProfile, &misc->power_policy));

    // Don't save this stuff if there's only 1 GPU...
    if (CountSLIGPUs () > 1) {
      NVAPI_SET_DWORD (sli->mode, SLI_RENDERING_MODE_ID, sli->poll_mode ());
      NVAPI_CALL      (DRS_SetSetting (hSession, hProfile, &sli->mode));

      NVAPI_SET_DWORD (vsync->smooth, VSYNCSMOOTHAFR_ID, vsync->poll_smooth ());
      NVAPI_CALL      (DRS_SetSetting (hSession, hProfile, &vsync->smooth));
    }

    NVAPI_SET_DWORD (vsync->mode, VSYNCMODE_ID, vsync->poll_mode ());
    NVAPI_CALL      (DRS_SetSetting (hSession, hProfile, &vsync->mode));

    NVAPI_SET_DWORD (vsync->adaptive, VSYNCTEARCONTROL_ID, vsync->poll_adaptive ());
    NVAPI_CALL      (DRS_SetSetting (hSession, hProfile, &vsync->adaptive));

    NVDRS_SETTING lodbias_auto_adjust;
    NVAPI_SET_DWORD (lodbias_auto_adjust, AUTO_LODBIASADJUST_ID, 0);
    NVAPI_CALL      (DRS_SetSetting (hSession, hProfile, &lodbias_auto_adjust));

    NVAPI_SET_DWORD (lodbias->adjust, LODBIASADJUST_ID, lodbias->poll_adjust ());
    NVAPI_CALL      (DRS_SetSetting (hSession, hProfile, &lodbias->adjust));

    NVAPI_SET_DWORD (lodbias->allow_negative, PS_TEXFILTER_NO_NEG_LODBIAS_ID, lodbias->poll_allow_negative ())
    NVAPI_CALL      (DRS_SetSetting (hSession, hProfile, &lodbias->allow_negative));

//    if (support_mfaa) {
//      NVAPI_SET_DWORD (misc->mfaa, MAXWELL_B_SAMPLE_INTERLEAVE_ID, misc->poll_mfaa ());
//      NVAPI_CALL      (DRS_SetSetting (hSession, hProfile, &misc->mfaa));
//    }
  }

  NVAPI_CALL (DRS_SaveSettings   (hSession));
  NVAPI_CALL (DRS_DestroySession (hSession));
#endif
}

std::wstring
NVAPI::GetDriverVersion (NvU32* pVer)
{
  NvU32             ver;
  NvAPI_ShortString ver_str;       // ANSI
  wchar_t           ver_wstr [64]; // Unicode

  NvAPI_SYS_GetDriverAndBranchVersion (&ver, ver_str);

  // The driver-branch string's not particularly user frieldy,
  //   let's do this the right way and report a number the end-user
  //     is actually going to recognize...
  swprintf (ver_wstr, 64, L"%u.%u", ver / 100, ver % 100);
  //MultiByteToWideChar (CP_OEMCP, 0, ver_str, -1, ver_wstr, 64);

  if (pVer != NULL)
    *pVer = ver;

  return ver_wstr;
}


BOOL bLibShutdown = FALSE;
BOOL bLibInit     = FALSE;

BOOL
NVAPI::UnloadLibrary (void)
{
  if (bLibInit == TRUE && bLibShutdown == FALSE) {
    // Whine very loudly if this fails, because that's not
    //   supposed to happen!
    NVAPI_VERBOSE ()

    NvAPI_Status ret;

    NVAPI_CALL2 (Unload (), ret);

    if (ret == NVAPI_OK) {
      bLibShutdown = TRUE;
      bLibInit     = FALSE;
    }
  }

  return bLibShutdown;
}

BOOL
NVAPI::InitializeLibrary (void)
{
  // It's silly to call this more than once, but not necessarily
  //  an error... just ignore repeated calls.
  if (bLibInit == TRUE)
    return TRUE;

  // If init is not false and not true, it's because we failed to
  //   initialize the API once before. Just return the failure status
  //     again.
  if (bLibInit != FALSE)
    return FALSE;

  NvAPI_Status ret;

  // We want this error to be silent, because this tool works on AMD GPUs too!
  NVAPI_SILENT ()
  {
    NVAPI_CALL2 (Initialize (), ret);
  }
  NVAPI_VERBOSE ()

    if (ret != NVAPI_OK) {
      nv_hardware = false;
      bLibInit    = TRUE + 1; // Clearly this isn't a boolean, it just looks like one
      return FALSE;
    }

  if (! CheckDriverVersion ()) {
    MessageBox (NULL,
                L"WARNING:  Your display drivers are too old to play this game!\n",
                L"Please update your display drivers (Minimum Version = 353.30)",
                MB_OK | MB_ICONEXCLAMATION);
  }

  return (bLibInit = TRUE);
}

bool
TEST_SLI (void)
{
  return false;
  //NvAPI_D3D_GetCurrentSLIState ()
}

#if 0
std::wstring
EDID_Audio_CodecName (uint8_t SAD1)
{
  switch ((SAD1 & 0x78) >> 3)
  {
  case 1:
    return L"LPCM";
  case 2:
    return L"AC-3";
  case 3:
    return L"MPEG1";
  case 4:
    return L"MP3";
  case 5:
    return L"MPEG2";
  case 6:
    return L"AAC";
  case 7:
    return L"DTS";
  case 8:
    return L"ATRAC";
  case 9:
    return L"SACD";
  case 10:
    return L"Dolby Digital+";
  case 11:
    return L"DTS-HD";
  case 12:
    return L"Dolby TrueHD";
  case 13:
    return L"DST Audio";
  case 14:
    return L"Microsoft WMA Pro";
  default:
    return L"INVALID";
  }
}

std::wstring
EDID_Audio_SampleRates (uint8_t SAD2)
{
  std::wstring rates = L" ";

  if (SAD2 & 0x1)
    rates += L"32 kHz ";
  if (SAD2 & 0x2)
    rates += L"44 kHz ";
  if (SAD2 & 0x4)
    rates += L"48 kHz ";
  if (SAD2 & 0x8)
    rates += L"88 kHz ";
  if (SAD2 & 0x10)
    rates += L"96 kHz ";
  if (SAD2 & 0x20)
    rates += L"176 kHz ";
  if (SAD2 & 0x40)
    rates += L"192 kHz ";

  return rates;
}

std::wstring
EDID_Audio_BitRates (uint8_t SAD3)
{
  std::wstring rates = L" ";

  if (SAD3 & 0x1)
    rates += L"16-bit ";
  if (SAD3 & 0x2)
    rates += L"20-bit ";
  if (SAD3 & 0x4)
    rates += L"24-bit ";

  return rates;
}

wchar_t wszFormat [16384];
#endif

bool
NVAPI::CheckDriverVersion (void)
{
  NvU32 ver;
  GetDriverVersion (&ver);

#if 0
  NV_MONITOR_CAPABILITIES mon_caps;
  mon_caps.version = NV_MONITOR_CAPABILITIES_VER;
  mon_caps.size = sizeof (NV_MONITOR_CAPABILITIES);

  NvPhysicalGpuHandle gpus [16];
  NvU32               gpu_count = 16;

  NV_GPU_DISPLAYIDS disp_ids [4];
  disp_ids [0].version = NV_GPU_DISPLAYIDS_VER;
  disp_ids [1].version = NV_GPU_DISPLAYIDS_VER;
  disp_ids [2].version = NV_GPU_DISPLAYIDS_VER;
  disp_ids [3].version = NV_GPU_DISPLAYIDS_VER;

  NvU32             disp_count = 4;

  NVAPI_CALL (EnumPhysicalGPUs (gpus, &gpu_count));
  NVAPI_CALL (GPU_GetConnectedDisplayIds (gpus [0], disp_ids, &disp_count, NV_GPU_CONNECTED_IDS_FLAG_SLI));

  for (int i = 0; i < disp_count; i++) {
    //NVAPI_CALL (DISP_GetMonitorCapabilities (disp_ids [i].displayId, &mon_caps));

    NV_EDID edid;
    edid.version    = NV_EDID_VER;
    edid.sizeofEDID = NV_EDID_DATA_SIZE;

    NVAPI_CALL (GPU_GetEDID (gpus [0], disp_ids [i].displayId, &edid));

    if (((uint64_t *)edid.EDID_Data) [0] == 0x00ffffffffffff00)
      MessageBox (NULL, L"We did it!", L"Neat", MB_OK);
    else
      return true;

    uint8_t* data = &edid.EDID_Data [128 + 4];
    while (true) {
#if 0
      uint8_t type = (*data & 0x7);// (*data & 0xE0) >> 5;
      uint8_t adv = (*data & 0xF8) >> 5;// (*data & 0x1F);
#else
      uint8_t type = (*data & 0xE0) >> 5;
      uint8_t adv  = (*data & 0x1F);
#endif

      data++;

      wchar_t* pwszFormat = wszFormat;

      if (type == 1) {
        //MessageBox (NULL, L"Audio Block", L"EDID Debug", MB_OK);
        uint8_t* end = data + adv;
        while (data < end) {
          pwszFormat += swprintf (pwszFormat, 16384, L"%s (%d Channels - [%s] @ (%s)))\n",
            EDID_Audio_CodecName (*data).c_str (),
              (*data & 0x7) + 1,
            EDID_Audio_SampleRates (*(data + 1)).c_str (),
            EDID_Audio_BitRates    (*(data + 2)).c_str ());

          data += 3;
        }

        MessageBox (NULL, wszFormat, L"Audio Channels", MB_OK);
      }

      if (type == 2) {
        //MessageBox (NULL, L"Video Block", L"EDID Debug", MB_OK);
        data += adv;
      }

      if (type == 3) {
        MessageBox (NULL, L"Vendor Block", L"EDID Debug", MB_OK);

        if (data [8]) {
          swprintf (wszFormat, 512, L"%d MHz Max TMDS", data [8] * 5);
          MessageBox (NULL, wszFormat, L"Pixel Clock", MB_OK);
        }

        bool latency = (data [9] & 0x80);

        if (latency) {
          swprintf (wszFormat, 512, L"(%d : %d) ms <video : audio> latency", data [10] * 2 - 1, data [11] * 2 - 1);
          MessageBox (NULL, wszFormat, L"Video Latency", MB_OK);
        }
          
        //uint8_t* end = 
        data += adv;
      }

      if (type == 4) {
        //MessageBox (NULL, L"Speaker Block", L"EDID Debug", MB_OK);
        data += adv;
      }

      if (data > &edid.EDID_Data [127] + edid.EDID_Data [128 + 2])
        break;
    }

    //NvAPI_GetAssociatedNvidiaDisplayName (disp_ids [i]., disp_name);
    //MessageBoxA (NULL, disp_name, "Test", MB_OK);

    //wchar_t wszSummary [1024];
    //disp_ids [i].connectorType

    //swprintf (wszSummary, 1024, L"%d ms Latency\n", mon_caps.data.vcdb.quantizationRangeYcc);// mon_caps.data.vsdb.supportDeepColor36bits);// mon_caps.data.vsdb.videoLatency);
    //MessageBox (NULL, wszSummary, L"Monitor Info", MB_OK);
    //mon_caps.data.vsdb.videoLatency;
  }
#endif

  return ver >= 35330;
  if (ver < 35330) {
    return false;
  }

  return true;
}

bool NVAPI::nv_hardware    = true;


#if 0
#ifdef NVAPI
typedef uint32_t NvU32;
typedef uint16_t NvU16;

wchar_t wszOrdinal [4096];

extern "C" {
  static char* dump_ints (void* data, const unsigned int& len)
  {
    static char szOut [4096];
    char* pszOut = szOut;
    pszOut += sprintf (pszOut, "Size:  %d\n", len);

    for (int i = 1; i < len / 4; i++) {
      pszOut += sprintf (pszOut, "NvU32 [%d] : %u\n", i - 1, ((NvU32 *)data) [i]);
    }

    for (int i = 2; i < len / 2; i++) {
      pszOut += sprintf (pszOut, "NvU16 [%d] : %u\n", i - 2, ((NvU16 *)data) [i]);
    }

    return szOut;
  }

  char szOut [4096];

  // Generic dump, for unknown data where lexical analysis is needed.
  static char* dump (void* data, const unsigned int& len)
  {
    char* pszOut = szOut;
    pszOut += sprintf (pszOut, "Size:  %d\n", len);

    if (len > 0) {
      unsigned width = 16;
      unsigned char *str = (unsigned char *)data;
      unsigned int j, i = 0;

      while (i < len) {
        pszOut += sprintf (pszOut, " ");

        for (j = 0; j < width; j++) {
          if (i + j < len)
            pszOut += sprintf (pszOut, "%02x ", (unsigned char)str [j]);
          else
            pszOut += sprintf (pszOut, "   ");

          if ((j + 1) % (width / 2) == 0)
            pszOut += sprintf (pszOut, " -  ");
        }

        for (j = 0; j < width; j++) {
          if (i + j < len)
            pszOut += sprintf (pszOut, "%c", isprint (str [j]) ? str [j] : '.');
          else
            pszOut += sprintf (pszOut, " ");
        }

        str += width;
        i += j;

        pszOut += sprintf (pszOut, "\n");
      }
    }

    return szOut;
  }

  //d9930b07
  //aad604d1

  enum NvAPI_Ordinals {
    _NvAPI_Initialize = 0x150e828,
    _NvAPI_EnumPhysicalGPUs = 0xe5ac921f,
    _NvAPI_EnumLogicalGPUs = 0x48b3ea59,
    _NvAPI_EnumNvidiaDisplayHandle = 0x9abdd40d,
    _NvAPI_GetPhysicalGPUsFromDisplay = 0x34ef9506,

    _NvAPI_GetDisplayDriverVersion = 0xf951a4d1,
    _NvAPI_GetPhysicalGPUsFromLogicalGPU = 0xaea3fa32,
    _NvAPI_GPU_GetThermalSettings = 0xe3640a56,
    _NvAPI_GPU_GetPerfClocks = 0x1ea54a3b, // NDA
    _NvAPI_GPU_SetPerfClocks = 0x7bcf4ac,  // NDA
    _NvAPI_GPU_GetAllClocks = 0x1bd69f49, // NDA
    _NvAPI_GPU_GetVPECount = 0xd8cbf37b, // NDA
    _NvAPI_GPU_GetShaderPipeCount = 0x63e2f56f, // NDA
    _NvAPI_GPU_GetShaderSubPipeCount = 0xbe17923,  // NDA
    _NvAPI_GPU_GetPartitionCount = 0x86f05d7a,
    _NvAPI_GPU_GetPhysicalFrameBufferSize = 0x46fbeb03,
    _NvAPI_GPU_GetVirtualFrameBufferSize = 0x5a04b644,
    _NvAPI_GetAssociatedNvidiaDisplayName = 0x22a78b05,

    _NvAPI_I2CReadEx = 0x4d7b0709, // NDA
    _NvAPI_I2CWriteEx = 0x283ac65a, // NDA

    _NvAPI_GPU_GetFullName = 0xceee8e9f,
    _NvAPI_GPU_GetShortName = 0xd988f0f3, // NDA
    _NvAPI_GPU_GetSerialNumber = 0x14b83a5f, // NDA

    _NvAPI_GPU_GetPCIIdentifiers = 0x2ddfb66e,
    _NvAPI_GPU_GetCoolerSettings = 0xda141340,
    _NvAPI_GPU_SetCoolerLevels = 0x8f6ed0fb, // NDA

    _NvAPI_GetGPUIDfromPhysicalGPU = 0x6533ea3e, // NDA
    _NvAPI_GetPhysicalGPUFromGPUID = 0x5380ad1a, // NDA

    _NvAPI_GPU_GetAllOutputs = 0x7d554f8e,
    _NvAPI_GPU_GetConnectedOutputs = 0x1730bfc9,
    _NvAPI_GPU_GetActiveOutputs = 0xe3e89b6f,
    _NvAPI_GPU_GetEDID = 0x37d32e69,
    _NvAPI_GPU_GetBusId = 0x1be0b8e5,
    _NvAPI_GPU_GetBusSlotId = 0x2a0a350f,

    _NvAPI_GPU_GetRamType = 0x57f7caac, // NDA
    _NvAPI_GPU_GetFBWidthAndLocation = 0x11104158, // NDA
    _NvAPI_GetDisplayDriverMemoryInfo = 0x774aa982, // NDA

    _NvAPI_GPU_GetDynamicPstatesInfoEx = 0x60ded2ed,

    _NvAPI_GPU_GetTotalSMCount = 0xae5fbcfe, // NDA
    _NvAPI_GPU_GetTotalSPCount = 0xb6d62591, // NDA
    _NvAPI_GPU_GetTotalTPCCount = 0x4e2f76a8, // NDA

    _NvAPI_GPU_GetGpuCoreCount = 0xc7026a87,

    _NvAPI_GPU_GetVbiosImage = 0xfc13ee11, // NDA
    _NvAPI_GPU_GetVbiosVersionString = 0xa561fd7d,

    _NvAPI_GPU_GetVoltages = 0x7d656244, // NDA
    _NvAPI_GPU_GetPstatesInfo = 0xba94c56e, // NDA
    _NvAPI_GPU_SetPstatesInfo = 0xcdf27911, // NDA
    _NvAPI_GPU_GetCurrentPstate = 0x927da4f6, // NDA

    _NvAPI_GPU_GetTachReading = 0x5f608315,
    _NvAPI_GPU_GetPstates20 = 0x6ff81213,
    _NvAPI_GPU_SetPstates20 = 0xf4dae6b,  // NDA
    _NvAPI_GPU_GetAllClockFrequencies = 0xdcb616c3, // NDA

    _NvAPI_GPU_ClientPowerTopologyGetInfo = 0xa4dfd3f2, // NDA
    _NvAPI_GPU_ClientPowerTopologyGetStatus = 0xedcf624e, // NDA
    _NvAPI_GPU_ClientPowerPoliciesGetInfo = 0x34206d86, // NDA
    _NvAPI_GPU_ClientPowerPoliciesGetStatus = 0x70916171, // NDA
    _NvAPI_GPU_ClientPowerPoliciesSetStatus = 0xad95f5ed, // NDA
    _NvAPI_GPU_GetVoltageDomainsStatus = 0xc16c7e2c, // NDA

                                                     //d258bb5
                                                     //e9c425a1
                                                     //34c0b13d

    _NvAPI_GPU_GetConnectedDisplayIds = 0x78dba2, // NDA

                                                  //175167e9
                                                  //1f7db630
                                                  //49882876

    _NvAPI_DISP_GetDisplayConfig = 0x11abccf8,
    _NvAPI_DISP_SetDisplayConfig = 0x5d8cf8de,

    //409d9841
    //3d358a0c
    //42aea16a
    //d9930b07

    _NvAPI_GetLogicalGPUFromPhysicalGPU = 0xadd604d1,

    _NvAPI_GPU_RestoreCoolerSettings = 0x8f6ed0fb,

    _NvAPI_GetAssociatedNvidiaDisplayHandle = 0x35c29134,


    _NvAPI_UNKNOWN_d258bbf = 0xd258bbf,
    _NvAPI_UNKNOWN_d258bb5 = 0xd258bb5,
    _NvAPI_UNKNOWN_e9c425a1 = 0xe9c425a1,
    _NvAPI_UNKNOWN_34c0b13d = 0x34c0b13d,
    _NvAPI_UNKNOWN_175167e9 = 0x175167e9,
    _NvAPI_UNKNOWN_1f7db630 = 0x1f7db630,
    _NvAPI_UNKNOWN_49882876 = 0x49882876,
    _NvAPI_UNKNOWN_409d9841 = 0x409d9841,
    _NvAPI_UNKNOWN_3d358a0c = 0x3d358a0c,
    _NvAPI_UNKNOWN_42aea16a = 0x42aea16a,
    _NvAPI_UNKNOWN_d9930b07 = 0xd9930b07,
    _NvAPI_UNKNONW_891f0ae = 0x891f0ae,

    // Supposedly has something to do with OpenCL
    _NvAPI_UNKNONW_33c7358c = 0x33c7358c,
    _NvAPI_UNKNOWN_593e8644 = 0x593e8644,
    _NvAPI_UNKNOWN_1629a173 = 0x1629a173,
    _NvAPI_UNKNOWN_f1d2777b = 0xf1d2777b,
    _NvAPI_UNKNOWN_8efc0978 = 0x8efc0978,



    //
    //
    //
    //
    //
    //
    //
    //
    //
    //
    //
    //
    //
    //
    //
    //
    //
    //
    //
    //
    //
    //

  };

  enum NvAPI_Status {
    NVAPI_OK = 0
  };

  typedef void* (*NvAPI_QueryInterface_t)(unsigned int offset);
  NvAPI_QueryInterface_t QueryInterface = NULL;

  NvAPI_Status
    __cdecl
    NvAPI_GetPhysicalGPUsFromDisplay (NvU32* param1, NvU32* param2, NvU32* param3)
  {
    return NVAPI_OK;
  }

  struct foundry_t {
    NvU32 version;
    NvU32 unknown;
    NvU32 data [128];// [32];
  } foundry;

  NvAPI_Status
    __cdecl
    NvAPI_GPU_GetAllClocks (NvU32 hGPU, void* args1, void* args2)
  {
    //NvU32 model = 0x0100; // WDDM 1.0

    MessageBoxA (NULL, dump (args2, 64), "Blah", MB_OK);
    //memcpy (&foundry, args1, ((foundry_t *)args1)->version & 0xff);

    //MessageBoxA (NULL, dump_ints (&foundry, foundry.version & 0xffff/* (foundry_t)*/), "Blah", MB_OK);

    if (QueryInterface == NULL)
      HMODULE hMod = LoadLibrary (L"nvapi-.dll");

    typedef NvAPI_Status (__cdecl *NvAPI_GPU_GetAllClocks_t)(NvU32 handle, void* args1, void* args2);
    NvAPI_GPU_GetAllClocks_t NvAPI_GPU_GetAllClocksEX = (NvAPI_GPU_GetAllClocks_t)QueryInterface (_NvAPI_GPU_GetPerfClocks);

    NvAPI_Status status = NvAPI_GPU_GetAllClocksEX (hGPU, args1, args2);

    MessageBoxA (NULL, dump (args2, 64), "Blah", MB_OK);
    //MessageBoxA (NULL, dump_ints (&foundry, foundry.version & 0xffff/* (foundry_t)*/), "Blah", MB_OK);

    return status;
  }

  __declspec(dllexport)
    void*
    __cdecl
    nvapi_DumpParameters (NvU32 handle, NvU32 addr, NvU32 addr2, NvU32 addr3)
  {
    return 0;
  }

  __declspec(dllexport)
    void*
    __cdecl
    nvapi_QueryInterface (NvU32 ordinal)
  {
    if (QueryInterface == NULL) {
      HMODULE hMod = LoadLibrary (L"nvapi-.dll");
      QueryInterface = (NvAPI_QueryInterface_t)GetProcAddress (hMod, "nvapi_QueryInterface");
    }

    if (ordinal == _NvAPI_Initialize)
      return QueryInterface (ordinal);

    if (ordinal == _NvAPI_EnumLogicalGPUs)
      return QueryInterface (_NvAPI_EnumLogicalGPUs);

    if (ordinal == _NvAPI_EnumPhysicalGPUs)
      return QueryInterface (ordinal);

    if (ordinal == _NvAPI_EnumNvidiaDisplayHandle)
      return QueryInterface (ordinal);

    if (ordinal == _NvAPI_GetPhysicalGPUsFromDisplay)
      return QueryInterface (ordinal);

    switch (ordinal) {
    case _NvAPI_GPU_GetPerfClocks:
      return &NvAPI_GPU_GetAllClocks;
    case _NvAPI_GetDisplayDriverVersion:
    case _NvAPI_GetPhysicalGPUsFromLogicalGPU:
    case _NvAPI_GPU_GetThermalSettings:
      //case _NvAPI_GPU_GetPerfClocks:
    case _NvAPI_GPU_SetPerfClocks:
    case _NvAPI_GPU_GetAllClocks:
    case _NvAPI_GPU_GetVPECount:
    case _NvAPI_GPU_GetShaderPipeCount:
    case _NvAPI_GPU_GetShaderSubPipeCount:
    case _NvAPI_GPU_GetPartitionCount:
    case _NvAPI_GPU_GetPhysicalFrameBufferSize:
    case _NvAPI_GPU_GetVirtualFrameBufferSize:
    case _NvAPI_GetAssociatedNvidiaDisplayName:

    case _NvAPI_I2CReadEx: // NDA
    case _NvAPI_I2CWriteEx: // NDA

    case _NvAPI_GPU_GetFullName:
    case _NvAPI_GPU_GetShortName: // NDA
    case _NvAPI_GPU_GetSerialNumber: // NDA

    case _NvAPI_GPU_GetPCIIdentifiers:
    case _NvAPI_GPU_GetCoolerSettings:
    case _NvAPI_GPU_SetCoolerLevels:

    case _NvAPI_GetGPUIDfromPhysicalGPU: // NDA
    case _NvAPI_GetPhysicalGPUFromGPUID: // NDA

    case _NvAPI_GPU_GetAllOutputs:
    case _NvAPI_GPU_GetConnectedOutputs:
    case _NvAPI_GPU_GetActiveOutputs:
    case _NvAPI_GPU_GetEDID:
    case _NvAPI_GPU_GetBusId:
    case _NvAPI_GPU_GetBusSlotId:

    case _NvAPI_GPU_GetRamType:             // NDA
    case _NvAPI_GPU_GetFBWidthAndLocation:  // NDA
    case _NvAPI_GetDisplayDriverMemoryInfo: // NDA

    case _NvAPI_GPU_GetDynamicPstatesInfoEx:

    case _NvAPI_GPU_GetTotalSMCount: // NDA
    case _NvAPI_GPU_GetTotalSPCount: // NDA
    case _NvAPI_GPU_GetTotalTPCCount: // NDA

    case _NvAPI_GPU_GetGpuCoreCount:

    case _NvAPI_GPU_GetVbiosImage: // NDA
    case _NvAPI_GPU_GetVbiosVersionString:

    case _NvAPI_GPU_GetVoltages: // NDA
    case _NvAPI_GPU_GetPstatesInfo: // NDA
    case _NvAPI_GPU_SetPstatesInfo: // NDA
    case _NvAPI_GPU_GetCurrentPstate: // NDA

    case _NvAPI_GPU_GetTachReading:
    case _NvAPI_GPU_GetPstates20:
    case _NvAPI_GPU_SetPstates20: // NDA
    case _NvAPI_GPU_GetAllClockFrequencies: // NDA

    case _NvAPI_GPU_ClientPowerTopologyGetInfo: // NDA
    case _NvAPI_GPU_ClientPowerTopologyGetStatus: // NDA
    case _NvAPI_GPU_ClientPowerPoliciesGetInfo: // NDA
    case _NvAPI_GPU_ClientPowerPoliciesGetStatus: // NDA
    case _NvAPI_GPU_ClientPowerPoliciesSetStatus: // NDA
    case _NvAPI_GPU_GetVoltageDomainsStatus: // NDA

    case _NvAPI_GPU_GetConnectedDisplayIds: // NDA

    case _NvAPI_DISP_GetDisplayConfig: // NDA
    case _NvAPI_DISP_SetDisplayConfig: // NDA

    case _NvAPI_GetPhysicalGPUsFromDisplay:
    case _NvAPI_GetLogicalGPUFromPhysicalGPU:
    case _NvAPI_GetAssociatedNvidiaDisplayHandle:
      //case _NvAPI_GPU_RestoreCoolerSettings:
      return QueryInterface (ordinal);

    case _NvAPI_UNKNOWN_d258bbf:
    case _NvAPI_UNKNOWN_d258bb5:
    case _NvAPI_UNKNOWN_e9c425a1:
    case _NvAPI_UNKNOWN_34c0b13d:
    case _NvAPI_UNKNOWN_175167e9:
    case _NvAPI_UNKNOWN_1f7db630:
    case _NvAPI_UNKNOWN_49882876:
    case _NvAPI_UNKNOWN_409d9841:
    case _NvAPI_UNKNOWN_3d358a0c:
    case _NvAPI_UNKNOWN_42aea16a:
    case _NvAPI_UNKNOWN_d9930b07:
    case _NvAPI_UNKNONW_891f0ae:
    case _NvAPI_UNKNONW_33c7358c:
    case _NvAPI_UNKNOWN_593e8644:
    case _NvAPI_UNKNOWN_1629a173:
    case _NvAPI_UNKNOWN_f1d2777b:
    case _NvAPI_UNKNOWN_8efc0978:

    default:
      return &nvapi_DumpParameters;
    }

    wsprintf (wszOrdinal, L"Application requested ordinal: nvapi_QueryInterface (%x)", ordinal);

    MessageBox (NULL, wszOrdinal, L"Test", MB_OK);

    return &nvapi_DumpParameters;
    //return &nvapi_QueryInterface;
  }

  __declspec(dllexport)
    void*
    __cdecl
    nvapi_pepQueryInterface (NvU32 ordinal)
  {
    wsprintf (wszOrdinal, L"Application requested ordinal: nvapi_pepQueryInterface (%x)", ordinal);
    MessageBox (NULL, wszOrdinal, L"Test", MB_OK);
    return &nvapi_pepQueryInterface;
  }
}
#endif
#endif