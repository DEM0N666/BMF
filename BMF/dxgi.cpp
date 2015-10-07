/**
* This file is part of Batman "Fix".
*
* Batman "Fix" is free software : you can redistribute it and / or modify
* it under the terms of the GNU General Public License as published by
* The Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* Batman "Fix" is distributed in the hope that it will be useful,
* But WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Batman "Fix". If not, see <http://www.gnu.org/licenses/>.
**/
#define _CRT_SECURE_NO_WARNINGS

//#include "dxgi_interfaces.h"

#include "dxgi_backend.h"

#include "stdafx.h"
#include "nvapi.h"
#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <string>

//#define HOOK_D3D11_DEVICE_CREATION
#ifdef HOOK_D3D11_DEVICE_CREATION
#include "minhook/MinHook.h"
#endif

#include "log.h"

#include "core.h"

static CRITICAL_SECTION d3dhook_mutex = { 0 };

extern int                      gpu_prio;

// Breaks Metal Gear Solid V
//#define ALLOW_DEVICE_TRANSITION

//static ID3D11Device* g_pD3D11Dev;
//static IDXGIDevice*  g_pDXGIDev;

extern "C" {
#undef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE __cdecl

  typedef HRESULT (STDMETHODCALLTYPE *CreateDXGIFactory2_t) \
    (UINT Flags, REFIID riid,  void** ppFactory);
  typedef HRESULT (STDMETHODCALLTYPE *CreateDXGIFactory1_t) \
    (REFIID riid,  void** ppFactory);
  typedef HRESULT (STDMETHODCALLTYPE *CreateDXGIFactory_t)  \
    (REFIID riid,  void** ppFactory);

  typedef HRESULT (WINAPI *D3D11CreateDeviceAndSwapChain_t)(
    _In_opt_                             IDXGIAdapter*, 
                                         D3D_DRIVER_TYPE,
                                         HMODULE,
                                         UINT, 
    _In_reads_opt_ (FeatureLevels) CONST D3D_FEATURE_LEVEL*, 
                                         UINT FeatureLevels,
                                         UINT,
    _In_opt_                       CONST DXGI_SWAP_CHAIN_DESC*,
    _Out_opt_                            IDXGISwapChain**,
    _Out_opt_                            ID3D11Device**, 
    _Out_opt_                            D3D_FEATURE_LEVEL*,
    _Out_opt_                            ID3D11DeviceContext**);


  typedef HRESULT (STDMETHODCALLTYPE *PresentSwapChain_t)(
                                         IDXGISwapChain *This,
                                         UINT            SyncInterval,
                                         UINT            Flags);

  typedef HRESULT (STDMETHODCALLTYPE *CreateSwapChain_t)(
                                         IDXGIFactory          *This,
                                   _In_  IUnknown              *pDevice,
                                   _In_  DXGI_SWAP_CHAIN_DESC  *pDesc,
                                  _Out_  IDXGISwapChain       **ppSwapChain);


  typedef HRESULT (STDMETHODCALLTYPE *GetDesc1_t)(IDXGIAdapter1      *This,
                                           _Out_  DXGI_ADAPTER_DESC1 *pDesc);
  typedef HRESULT (STDMETHODCALLTYPE *GetDesc2_t)(IDXGIAdapter2      *This,
                                           _Out_  DXGI_ADAPTER_DESC2 *pDesc);
  typedef HRESULT (STDMETHODCALLTYPE *GetDesc_t) (IDXGIAdapter       *This,
                                           _Out_  DXGI_ADAPTER_DESC  *pDesc);

  typedef HRESULT (STDMETHODCALLTYPE *EnumAdapters_t)(
                                          IDXGIFactory  *This,
                                          UINT           Adapter,
                                    _Out_ IDXGIAdapter **ppAdapter);

  typedef HRESULT (STDMETHODCALLTYPE *EnumAdapters1_t)(
                                          IDXGIFactory1  *This,
                                          UINT            Adapter,
                                    _Out_ IDXGIAdapter1 **ppAdapter);

  volatile
    D3D11CreateDeviceAndSwapChain_t
    D3D11CreateDeviceAndSwapChain_Import = nullptr;

  PresentSwapChain_t   Present_Original          = nullptr;
  CreateSwapChain_t    CreateSwapChain_Original  = nullptr;

  GetDesc_t            GetDesc_Original          = nullptr;
  GetDesc1_t           GetDesc1_Original         = nullptr;
  GetDesc2_t           GetDesc2_Original         = nullptr;

  EnumAdapters_t       EnumAdapters_Original     = nullptr;
  EnumAdapters1_t      EnumAdapters1_Original    = nullptr;

  CreateDXGIFactory_t  CreateDXGIFactory_Import  = nullptr;
  CreateDXGIFactory1_t CreateDXGIFactory1_Import = nullptr;
  CreateDXGIFactory2_t CreateDXGIFactory2_Import = nullptr;

  const wchar_t*
  BMF_DescribeVirtualProtectFlags (DWORD dwProtect)
  {
    switch (dwProtect)
    {
    case 0x10:
      return L"Execute";
    case 0x20:
      return L"Execute + Read-Only";
    case 0x40:
      return L"Execute + Read/Write";
    case 0x80:
      return L"Execute + Read-Only or Copy-on-Write)";
    case 0x01:
      return L"No Access";
    case 0x02:
      return L"Read-Only";
    case 0x04:
      return L"Read/Write";
    case 0x08:
      return L" Read-Only or Copy-on-Write";
    default:
      return L"UNKNOWN";
    }
  }

  void dxgi_init_callback (void)
  {
    HMODULE hBackend = backend_dll;

    dll_log.Log (L"Importing CreateDXGIFactory{1|2}");
    dll_log.Log (L"================================");

    dll_log.Log (L"  CreateDXGIFactory:  %08Xh", 
      (CreateDXGIFactory_Import =  \
        (CreateDXGIFactory_t)GetProcAddress (hBackend, "CreateDXGIFactory")));
    dll_log.Log (L"  CreateDXGIFactory1: %08Xh",
      (CreateDXGIFactory1_Import = \
        (CreateDXGIFactory1_t)GetProcAddress (hBackend, "CreateDXGIFactory1")));
    dll_log.Log (L"  CreateDXGIFactory2: %08Xh",
      (CreateDXGIFactory2_Import = \
        (CreateDXGIFactory2_t)GetProcAddress (hBackend, "CreateDXGIFactory2")));
  }

#define DXGI_CALL(_Ret, _Call) {                                      \
  dll_log.LogEx (true, L"  Calling original function: ");            \
  (_Ret) = (_Call);                                                   \
  dll_log.LogEx (false, L"(ret=%s)\n\n", BMF_DescribeHRESULT (_Ret));\
}

  // Interface-based DXGI call
#define dll_log_CALL_I(_Interface,_Name,_Format)                            \
  dll_log.LogEx (true, L"[!] %s::%s (", _Interface, _Name);                 \
  dll_log.LogEx (false, _Format
  // Global DXGI call
#define dll_log_CALL(_Name,_Format)                                         \
  dll_log.LogEx (true, L"[!] %s (", _Name);                                 \
  dll_log.LogEx (false, _Format
#define dll_log_CALL_END                                                    \
  dll_log.LogEx (false, L") -- [Calling Thread: 0x%04x]\n",                 \
    GetCurrentThreadId ());

#define dll_log_CALL_I0(_Interface,_Name) {                                 \
  dll_log_CALL_I   (_Interface,_Name, L"void"));                            \
  dll_log_CALL_END                                                          \
}

#define dll_log_CALL_I1(_Interface,_Name,_Format,_Args) {                   \
  dll_log_CALL_I   (_Interface,_Name, _Format), _Args);                     \
  dll_log_CALL_END                                                          \
}

#define dll_log_CALL_I2(_Interface,_Name,_Format,_Args0,_Args1) {           \
  dll_log_CALL_I   (_Interface,_Name, _Format), _Args0, _Args1);            \
  dll_log_CALL_END                                                          \
}

#define dll_log_CALL_I3(_Interface,_Name,_Format,_Args0,_Args1,_Args2) {    \
  dll_log_CALL_I   (_Interface,_Name, _Format), _Args0, _Args1, _Args2);    \
  dll_log_CALL_END                                                          \
}

#define dll_log_CALL_0(_Name) {                               \
  dll_log_CALL   (_Name, L"void"));                           \
  dll_log_CALL_END                                            \
}

#define dll_log_CALL_1(_Name,_Format,_Args0) {                \
  dll_log_CALL   (_Name, _Format), _Args0);                   \
  dll_log_CALL_END                                            \
}

#define dll_log_CALL_2(_Name,_Format,_Args0,_Args1) {         \
  dll_log_CALL     (_Name, _Format), _Args0, _Args1);         \
  dll_log_CALL_END                                            \
}

#define dll_log_CALL_3(_Name,_Format,_Args0,_Args1,_Args2) {  \
  dll_log_CALL     (_Name, _Format), _Args0, _Args1, _Args2); \
  dll_log_CALL_END                                            \
}

#define DXGI_STUB(_Return, _Name, _Proto, _Args)                          \
  __declspec (dllexport) _Return STDMETHODCALLTYPE                        \
  _Name _Proto {                                                          \
    WaitForInit ();                                                       \
                                                                          \
    typedef _Return (STDMETHODCALLTYPE *passthrough_t) _Proto;            \
    static passthrough_t _default_impl = nullptr;                         \
                                                                          \
    if (_default_impl == nullptr) {                                       \
      static const char* szName = #_Name;                                 \
      _default_impl = (passthrough_t)GetProcAddress (backend_dll, szName);\
                                                                          \
      if (_default_impl == nullptr) {                                     \
        dll_log.Log (                                                     \
          L"Unable to locate symbol  %s in dxgi.dll",                     \
          L#_Name);                                                       \
        return E_NOTIMPL;                                                 \
      }                                                                   \
    }                                                                     \
                                                                          \
    dll_log.Log (L"[!] %s %s - "                                          \
             L"[Calling Thread: 0x%04x]",                                 \
      L#_Name, L#_Proto, GetCurrentThreadId ());                          \
                                                                          \
    return _default_impl _Args;                                           \
}

  extern "C++" {
    int
    BMF_GetDXGIFactoryInterfaceVer (const IID& riid)
    {
      if (riid == __uuidof (IDXGIFactory))
        return 0;
      if (riid == __uuidof (IDXGIFactory1))
        return 1;
      if (riid == __uuidof (IDXGIFactory2))
        return 2;
      if (riid == __uuidof (IDXGIFactory3))
        return 3;
      if (riid == __uuidof (IDXGIFactory4))
        return 4;

      //assert (false);
      return -1;
    }

    std::wstring
    BMF_GetDXGIFactoryInterfaceEx (const IID& riid)
    {
      std::wstring interface_name;

      if (riid == __uuidof (IDXGIFactory))
        interface_name = L"IDXGIFactory";
      else if (riid == __uuidof (IDXGIFactory1))
        interface_name = L"IDXGIFactory1";
      else if (riid == __uuidof (IDXGIFactory2))
        interface_name = L"IDXGIFactory2";
      else if (riid == __uuidof (IDXGIFactory3))
        interface_name = L"IDXGIFactory3";
      else if (riid == __uuidof (IDXGIFactory4))
        interface_name = L"IDXGIFactory4";
      else {
        wchar_t *pwszIID;

        if (SUCCEEDED (StringFromIID (riid, (LPOLESTR *)&pwszIID)))
        {
          interface_name = pwszIID;
          CoTaskMemFree (pwszIID);
        }
      }

      return interface_name;
    }

    int
    BMF_GetDXGIFactoryInterfaceVer (IUnknown *pFactory)
    {
      IUnknown *pTemp = nullptr;

      if (SUCCEEDED (
        pFactory->QueryInterface (__uuidof (IDXGIFactory4), (void **)&pTemp)))
      {
        pTemp->Release ();
        return 4;
      }
      if (SUCCEEDED (
        pFactory->QueryInterface (__uuidof (IDXGIFactory3), (void **)&pTemp)))
      {
        pTemp->Release ();
        return 3;
      }

      if (SUCCEEDED (
        pFactory->QueryInterface (__uuidof (IDXGIFactory2), (void **)&pTemp)))
      {
        pTemp->Release ();
        return 2;
      }

      if (SUCCEEDED (
        pFactory->QueryInterface (__uuidof (IDXGIFactory1), (void **)&pTemp)))
      {
        pTemp->Release ();
        return 1;
      }

      if (SUCCEEDED (
        pFactory->QueryInterface (__uuidof (IDXGIFactory), (void **)&pTemp)))
      {
        pTemp->Release ();
        return 0;
      }

      //assert (false);
      return -1;
    }

    std::wstring
    BMF_GetDXGIFactoryInterface (IUnknown *pFactory)
    {
      int iver = BMF_GetDXGIFactoryInterfaceVer (pFactory);

      if (iver == 4)
        return BMF_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory4));

      if (iver == 3)
        return BMF_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory3));

      if (iver == 2)
        return BMF_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory2));

      if (iver == 1)
        return BMF_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory1));

      if (iver == 0)
        return BMF_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory));

      return L"{Invalid-Factory-UUID}";
    }

    int
    BMF_GetDXGIAdapterInterfaceVer (const IID& riid)
    {
      if (riid == __uuidof (IDXGIAdapter))
        return 0;
      if (riid == __uuidof (IDXGIAdapter1))
        return 1;
      if (riid == __uuidof (IDXGIAdapter2))
        return 2;
      if (riid == __uuidof (IDXGIAdapter3))
        return 3;

      //assert (false);
      return -1;
    }

    std::wstring
    BMF_GetDXGIAdapterInterfaceEx (const IID& riid)
    {
      std::wstring interface_name;

      if (riid == __uuidof (IDXGIAdapter))
        interface_name = L"IDXGIAdapter";
      else if (riid == __uuidof (IDXGIAdapter1))
        interface_name = L"IDXGIAdapter1";
      else if (riid == __uuidof (IDXGIAdapter2))
        interface_name = L"IDXGIAdapter2";
      else if (riid == __uuidof (IDXGIAdapter3))
        interface_name = L"IDXGIAdapter3";
      else {
        wchar_t* pwszIID;

        if (SUCCEEDED (StringFromIID (riid, (LPOLESTR *)&pwszIID)))
        {
          interface_name = pwszIID;
          CoTaskMemFree (pwszIID);
        }
      }

      return interface_name;
    }

    int
    BMF_GetDXGIAdapterInterfaceVer (IUnknown *pAdapter)
    {
      IUnknown *pTemp = nullptr;

      if (SUCCEEDED(
        pAdapter->QueryInterface (__uuidof (IDXGIAdapter3), (void **)&pTemp)))
      {
        pTemp->Release ();
        return 3;
      }

      if (SUCCEEDED(
        pAdapter->QueryInterface (__uuidof (IDXGIAdapter2), (void **)&pTemp)))
      {
        pTemp->Release ();
        return 2;
      }

      if (SUCCEEDED(
        pAdapter->QueryInterface (__uuidof (IDXGIAdapter1), (void **)&pTemp)))
      {
        pTemp->Release ();
        return 1;
      }

      if (SUCCEEDED(
        pAdapter->QueryInterface (__uuidof (IDXGIAdapter), (void **)&pTemp)))
      {
        pTemp->Release ();
        return 0;
      }

      //assert (false);
      return -1;
    }

    std::wstring
    BMF_GetDXGIAdapterInterface (IUnknown *pAdapter)
    {
      int iver = BMF_GetDXGIAdapterInterfaceVer (pAdapter);

      if (iver == 3)
        return BMF_GetDXGIAdapterInterfaceEx (__uuidof (IDXGIAdapter3));

      if (iver == 2)
        return BMF_GetDXGIAdapterInterfaceEx (__uuidof (IDXGIAdapter2));

      if (iver == 1)
        return BMF_GetDXGIAdapterInterfaceEx (__uuidof (IDXGIAdapter1));

      if (iver == 0)
        return BMF_GetDXGIAdapterInterfaceEx (__uuidof (IDXGIAdapter));

      return L"{Invalid-Adapter-UUID}";
    }
  }

#ifdef _WIN64
#define DXGI_VIRTUAL_OVERRIDE(_Base,_Index,_Name,_Override,_Original,_Type) { \
  void** vftable = *(void***)*_Base;                                          \
                                                                              \
  if (vftable [_Index] != _Override) {                                        \
    DWORD dwProtect;                                                          \
                                                                              \
    VirtualProtect (&vftable [_Index], 8, PAGE_EXECUTE_READWRITE, &dwProtect);\
                                                                              \
  dll_log.Log (L" Original VFTable entry for %s: %08Xh  (Memory Policy: %s)",\
             L##_Name, vftable [_Index],                                      \
             BMF_DescribeVirtualProtectFlags (dwProtect));                    \
                                                                              \
    if (_Original == NULL)                                                    \
      _Original = (##_Type)vftable [_Index];                                  \
                                                                              \
    dll_log.Log (L"  + %s: %08Xh", L#_Original, _Original);                  \
                                                                              \
    vftable [_Index] = _Override;                                             \
                                                                              \
    VirtualProtect (&vftable [_Index], 8, dwProtect, &dwProtect);             \
                                                                              \
    dll_log.Log (L" New VFTable entry for %s: %08Xh  (Memory Policy: %s)\n", \
                  L##_Name, vftable [_Index],                                 \
                  BMF_DescribeVirtualProtectFlags (dwProtect));               \
  }                                                                           \
}
#else
#define DXGI_VIRTUAL_OVERRIDE(_Base,_Index,_Name,_Override,_Original,_Type) { \
  void** vftable = *(void***)*_Base;                                          \
                                                                              \
  if (vftable [_Index] != _Override) {                                        \
    DWORD dwProtect;                                                          \
                                                                              \
    VirtualProtect (&vftable [_Index], 4, PAGE_EXECUTE_READWRITE, &dwProtect);\
                                                                              \
  dll_log.Log (L" Original VFTable entry for %s: %08Xh  (Memory Policy: %s)",\
             L##_Name, vftable [_Index],                                      \
             BMF_DescribeVirtualProtectFlags (dwProtect));                    \
                                                                              \
    if (_Original == NULL)                                                    \
      _Original = (##_Type)vftable [_Index];                                  \
                                                                              \
    dll_log.Log (L"  + %s: %08Xh", L#_Original, _Original);                  \
                                                                              \
    vftable [_Index] = _Override;                                             \
                                                                              \
    VirtualProtect (&vftable [_Index], 4, dwProtect, &dwProtect);             \
                                                                              \
    dll_log.Log (L" New VFTable entry for %s: %08Xh  (Memory Policy: %s)\n", \
                  L##_Name, vftable [_Index],                                 \
                  BMF_DescribeVirtualProtectFlags (dwProtect));               \
  }                                                                           \
}
#endif

  HRESULT
  __cdecl PresentCallback (IDXGISwapChain *This,
                           UINT            SyncInterval,
                           UINT            Flags)
  {
    BMF_BeginBufferSwap ();

    return BMF_EndBufferSwap (Present_Original (This, SyncInterval, Flags));
  }

  HRESULT
  STDMETHODCALLTYPE CreateSwapChain_Override (IDXGIFactory          *This,
                                        _In_  IUnknown              *pDevice,
                                        _In_  DXGI_SWAP_CHAIN_DESC  *pDesc,
                                       _Out_  IDXGISwapChain       **ppSwapChain)
  {
    std::wstring iname = BMF_GetDXGIFactoryInterface (This);

    dll_log_CALL_I3 (iname.c_str (), L"CreateSwapChain", L"%08Xh, %08Xh, %08Xh",
                      pDevice, pDesc, ppSwapChain);

    HRESULT ret;
    DXGI_CALL(ret, CreateSwapChain_Original (This, pDevice, pDesc, ppSwapChain));

    if (ppSwapChain != NULL && (*ppSwapChain) != NULL &&
      Present_Original == nullptr)
    {
      ID3D11Device *pDev;

      if (SUCCEEDED (pDevice->QueryInterface (__uuidof (ID3D11Device),
        (void **)&pDev)))
      {
        budget_log.silent = false;

        budget_log.LogEx (true, L"Hooking IDXGISwapChain::Present... ");

#if 0
        void** vftable = *(void***)*ppSwapChain;

        MH_STATUS stat =
          MH_CreateHook (vftable [8], PresentCallback, (void **)&Present_Original);

        if (stat != MH_ERROR_ALREADY_CREATED &&
          stat != MH_OK) {
          budget_log.LogEx (false, L" failed\n");
        }
        else {
          budget_log.LogEx (false, L" %p\n", Present_Original);
        }

        MH_ApplyQueued ();
        MH_EnableHook  (MH_ALL_HOOKS);
#else
        DXGI_VIRTUAL_OVERRIDE (ppSwapChain, 8, "IDXGISwapChain::Present",
                               PresentCallback, Present_Original,
                               PresentSwapChain_t);

        budget_log.LogEx (false, L"Done\n");
#endif

        budget_log.silent = true;

        pDev->Release ();
      }
    }

    return ret;
  }

  HRESULT
  WINAPI D3D11CreateDeviceAndSwapChain (
   _In_opt_                             IDXGIAdapter          *pAdapter,
                                        D3D_DRIVER_TYPE        DriverType,
                                        HMODULE                Software,
                                        UINT                   Flags,
   _In_reads_opt_ (FeatureLevels) CONST D3D_FEATURE_LEVEL     *pFeatureLevels,
                                        UINT                   FeatureLevels,
                                        UINT                   SDKVersion,
   _In_opt_                       CONST DXGI_SWAP_CHAIN_DESC  *pSwapChainDesc,
   _Out_opt_                            IDXGISwapChain       **ppSwapChain,
   _Out_opt_                            ID3D11Device         **ppDevice,
   _Out_opt_                            D3D_FEATURE_LEVEL     *pFeatureLevel,
   _Out_opt_                            ID3D11DeviceContext  **ppImmediateContext)
  {
    dll_log_CALL_0 (L"D3D11CreateDeviceAndSwapChain");

    dll_log.LogEx (true, L" Preferred Feature Level(s): <%u> - ", FeatureLevels);

    for (UINT i = 0; i < FeatureLevels; i++) {
      switch (pFeatureLevels [i])
      {
      case D3D_FEATURE_LEVEL_9_1:
        dll_log.LogEx (false, L" 9_1");
        break;
      case D3D_FEATURE_LEVEL_9_2:
        dll_log.LogEx (false, L" 9_2");
        break;
      case D3D_FEATURE_LEVEL_9_3:
        dll_log.LogEx (false, L" 9_3");
        break;
      case D3D_FEATURE_LEVEL_10_0:
        dll_log.LogEx (false, L" 10_0");
        break;
      case D3D_FEATURE_LEVEL_10_1:
        dll_log.LogEx (false, L" 10_1");
        break;
      case D3D_FEATURE_LEVEL_11_0:
        dll_log.LogEx (false, L" 11_0");
        break;
      case D3D_FEATURE_LEVEL_11_1:
        dll_log.LogEx (false, L" 11_1");
        break;
        //case D3D_FEATURE_LEVEL_12_0:
        //dll_log.LogEx (false, L" 12_0");
        //break;
        //case D3D_FEATURE_LEVEL_12_1:
        //dll_log.LogEx (false, L" 12_1");
        //break;
      }
    }

    dll_log.LogEx (false, L"\n");

    HRESULT res =
      D3D11CreateDeviceAndSwapChain_Import (pAdapter,
                                            DriverType,
                                            Software,
                                            Flags,
                                            pFeatureLevels,
                                            FeatureLevels,
                                            SDKVersion,
                                            pSwapChainDesc,
                                            ppSwapChain,
                                            ppDevice,
                                            pFeatureLevel,
                                            ppImmediateContext);

    if (res == S_OK && (ppDevice != NULL))
    {
      dll_log.Log (L" >> Device = 0x%08Xh", *ppDevice);

#if 0
      g_pD3D11Dev = (*ppDevice);

      if (g_pDXGIDev == nullptr && g_pD3D11Dev != nullptr)
      {
        // First device created, this is the sane case...
        g_pD3D11Dev->QueryInterface (__uuidof (IDXGIDevice),
          (void **)&g_pDXGIDev);
      }

      ///
      // This does not happen often, but sometimes a game creates multiple
      //   devices, let us just transition the DXGI device to the new D3D
      //     device.
      //
      //   Is this the proper behavior? Who can say? Why do these games need
      //     multiple devices capable of presenting swapchains in the first
      //       place?
      //
      else if (g_pD3D11Dev != nullptr)
      {
        //if (config.allow_dev_trans) {
#ifdef ALLOW_DEVICE_TRANSITION
        g_pDXGIDev->Release ();
        g_pD3D11Dev->QueryInterface (__uuidof (IDXGIDevice),
          (void **)&g_pDXGIDev);
#endif
        //}
      }
#endif
    }

    return res;
  }

  HRESULT
  STDMETHODCALLTYPE GetDesc2_Override (IDXGIAdapter2      *This,
                                _Out_  DXGI_ADAPTER_DESC2 *pDesc)
  {
    std::wstring iname = BMF_GetDXGIAdapterInterface (This);

    dll_log_CALL_I2 (iname.c_str (), L"GetDesc2", L"%08Xh, %08Xh", This, pDesc);

    HRESULT ret;
    DXGI_CALL (ret, GetDesc2_Original (This, pDesc));

    //// OVERRIDE VRAM NUMBER
    if (nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0) {
      dll_log.LogEx ( true,
        L" <> GetDesc2_Override: Looking for matching NVAPI GPU for %s...: ",
        pDesc->Description );

      DXGI_ADAPTER_DESC* match =
        bmf::NVAPI::FindGPUByDXGIName (pDesc->Description);

      if (match != NULL) {
        dll_log.LogEx (false, L"Success! (%s)\n", match->Description);
        pDesc->DedicatedVideoMemory = match->DedicatedVideoMemory;
      }
      else
        dll_log.LogEx (false, L"Failure! (No Match Found)\n");
    }

    dll_log.LogEx (false, L"\n");

    return ret;
  }

  HRESULT
  STDMETHODCALLTYPE GetDesc1_Override (IDXGIAdapter1      *This,
                                _Out_  DXGI_ADAPTER_DESC1 *pDesc)
  {
    std::wstring iname = BMF_GetDXGIAdapterInterface (This);

    dll_log_CALL_I2 (iname.c_str (), L"GetDesc1", L"%08Xh, %08Xh", This, pDesc);

    HRESULT ret;
    DXGI_CALL (ret, GetDesc1_Original (This, pDesc));

    //// OVERRIDE VRAM NUMBER
    if (nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0) {
      dll_log.LogEx ( true,
        L" <> GetDesc1_Override: Looking for matching NVAPI GPU for %s...: ",
        pDesc->Description );

      DXGI_ADAPTER_DESC* match =
        bmf::NVAPI::FindGPUByDXGIName (pDesc->Description);

      if (match != NULL) {
        dll_log.LogEx (false, L"Success! (%s)\n", match->Description);
        pDesc->DedicatedVideoMemory = match->DedicatedVideoMemory;
      }
      else
        dll_log.LogEx (false, L"Failure! (No Match Found)\n");
    }

    dll_log.LogEx (false, L"\n");

    return ret;
  }

  HRESULT
  STDMETHODCALLTYPE GetDesc_Override (IDXGIAdapter      *This,
                               _Out_  DXGI_ADAPTER_DESC *pDesc)
  {
    std::wstring iname = BMF_GetDXGIAdapterInterface (This);

    dll_log_CALL_I2 (iname.c_str (), L"GetDesc",L"%08Xh, %08Xh", This, pDesc);

    HRESULT ret;
    DXGI_CALL (ret, GetDesc_Original (This, pDesc));

    //// OVERRIDE VRAM NUMBER
    if (nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0) {
      dll_log.LogEx ( true,
        L" <> GetDesc_Override: Looking for matching NVAPI GPU for %s...: ",
        pDesc->Description );

      DXGI_ADAPTER_DESC* match =
        bmf::NVAPI::FindGPUByDXGIName (pDesc->Description);

      if (match != NULL) {
        dll_log.LogEx (false, L"Success! (%s)\n", match->Description);
        pDesc->DedicatedVideoMemory = match->DedicatedVideoMemory;
      }
      else
        dll_log.LogEx (false, L"Failure! (No Match Found)\n");
    }

    dll_log.LogEx (false, L"\n");

    return ret;
  }

  typedef enum bmfUndesirableVendors {
    Microsoft = 0x1414,
    Intel     = 0x8086
  } Vendors;

#ifdef HOOK_D3D11_DEVICE_CREATION
  volatile bool init = false;

  DWORD
    WINAPI HookThread (LPVOID user)
  {
    if (init) {
      //LeaveCriticalSection (&d3dhook_mutex);
      return 1;
    }

    init = true;

    LoadLibrary (L"d3d11.dll");

    dll_log.LogEx (true, L"Hooking D3D11CreateDeviceAndSwapChain... ");

    MH_STATUS stat =
      MH_CreateHookApi (L"d3d11.dll",
        "D3D11CreateDeviceAndSwapChain",
        D3D11CreateDeviceAndSwapChain,
        (void **)&D3D11CreateDeviceAndSwapChain_Import);

    if (stat != MH_ERROR_ALREADY_CREATED &&
      stat != MH_OK) {
      dll_log.LogEx (false, L" failed\n");
    }
    else {
      dll_log.LogEx (false, L" %p\n", D3D11CreateDeviceAndSwapChain_Import);
    }

    dll_log.LogEx (false, L"\n");

    MH_ApplyQueued ();
    MH_EnableHook  (MH_ALL_HOOKS);

    return 0;
  }

  void
    BMF_InstallD3D11DeviceHooks (void)
  {
    EnterCriticalSection (&d3dhook_mutex);
    {
      WaitForInit ();

      if (init) {
        LeaveCriticalSection (&d3dhook_mutex);
        return;
      }

      HANDLE hThread =
        CreateThread (NULL, 0, HookThread, NULL, 0, NULL);

      if (hThread != 0)
        WaitForSingleObject (hThread, INFINITE);
      else
        Sleep (20);

      init = true;
    }
    LeaveCriticalSection (&d3dhook_mutex);
  }
#endif

  HRESULT
  STDMETHODCALLTYPE EnumAdapters_Common (IDXGIFactory       *This,
                                         UINT                Adapter,
                                  _Out_  IDXGIAdapter      **ppAdapter,
                                         DXGI_ADAPTER_DESC  *pDesc,
                                         EnumAdapters_t      pFunc)
  {
    // Logic to skip Intel and Microsoft adapters and return only AMD / NV
    //if (lstrlenW (pDesc->Description)) {
    if (true) {
      if (! lstrlenW (pDesc->Description))
        dll_log.LogEx (false, L" >> Assertion filed: Zero-length adapter name!\n");

      if (pDesc->VendorId == Microsoft || pDesc->VendorId == Intel) {
        // We need to release the reference we were just handed before
        //   skipping it.
        (*ppAdapter)->Release ();

        dll_log.LogEx (false,
          L" >> (Host Application Tried To Enum Intel or Microsoft Adapter)"
          L" -- Skipping Adapter %lu <<\n\n", Adapter);

        return (pFunc (This, Adapter + 1, ppAdapter));
      }
      else {
        if (nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0) {
          DXGI_ADAPTER_DESC* match =
            bmf::NVAPI::FindGPUByDXGIName (pDesc->Description);

          if (match != NULL &&
            pDesc->DedicatedVideoMemory > match->DedicatedVideoMemory) {
// This creates problems in 32-bit environments...
#ifndef _WIN32
            dll_log.Log (
              L"   # SLI Detected (Corrected Memory Total: %llu MiB -- "
              L"Original: %llu MiB)",
              match->DedicatedVideoMemory >> 20ULL,
              pDesc->DedicatedVideoMemory >> 20ULL);
#endif
          }
        }

        BMF_StartDXGI_1_4_BudgetThread (ppAdapter);

        dll_log.LogEx (false, L"\n");
      }

      dll_log.Log(L"   @ Returning Adapter w/ Name: %s\n",pDesc->Description);
    }

    return S_OK;
  }

  HRESULT
  STDMETHODCALLTYPE EnumAdapters1_Override (IDXGIFactory1  *This,
                                            UINT            Adapter,
                                     _Out_  IDXGIAdapter1 **ppAdapter)
  {
    std::wstring iname = BMF_GetDXGIFactoryInterface (This);

    dll_log_CALL_I3 (iname.c_str (), L"EnumAdapters1", L"%08Xh, %u, %08Xh",
      This, Adapter, ppAdapter);

    HRESULT ret;
    DXGI_CALL (ret, EnumAdapters1_Original (This,Adapter,ppAdapter));

    if (SUCCEEDED (ret)) {
      if (! GetDesc1_Original) {
        // Only do this for NVIDIA GPUs
        if (nvapi_init) {
          DXGI_VIRTUAL_OVERRIDE (ppAdapter, 10, "(*ppAdapter)->GetDesc1",
                                 GetDesc1_Override, GetDesc1_Original, GetDesc1_t);
        } else {
          GetDesc1_Original = (GetDesc1_t)(*(void ***)*ppAdapter) [10];
        }
      }

      DXGI_ADAPTER_DESC1 desc;
      GetDesc1_Original ((*ppAdapter), &desc);

      return EnumAdapters_Common (This, Adapter, (IDXGIAdapter **)ppAdapter,
                                  (DXGI_ADAPTER_DESC *)&desc,
                                  (EnumAdapters_t)EnumAdapters1_Override);
    }

    return ret;
  }

  HRESULT
  STDMETHODCALLTYPE EnumAdapters_Override (IDXGIFactory  *This,
                                           UINT           Adapter,
                                    _Out_  IDXGIAdapter **ppAdapter)
  {
    std::wstring iname = BMF_GetDXGIFactoryInterface (This);

    dll_log_CALL_I3 (iname.c_str (), L"EnumAdapters", L"%08Xh, %u, %08Xh",
      This, Adapter, ppAdapter);

    HRESULT ret;
    DXGI_CALL (ret, EnumAdapters_Original (This, Adapter, ppAdapter));

    if (SUCCEEDED (ret)) {
      if (! GetDesc_Original) {
        // Only do this for NVIDIA GPUs
        if (nvapi_init) {
          DXGI_VIRTUAL_OVERRIDE (ppAdapter, 8, "(*ppAdapter)->GetDesc",
                                 GetDesc_Override, GetDesc_Original, GetDesc_t);
        } else {
          GetDesc_Original = (GetDesc_t)(*(void ***)*ppAdapter) [8];
        }
      }

      DXGI_ADAPTER_DESC desc;
      GetDesc_Original ((*ppAdapter), &desc);

      return EnumAdapters_Common (This, Adapter, ppAdapter,
                                  &desc,
                                  (EnumAdapters_t)EnumAdapters_Override);
    }

    return ret;
  }

  __declspec (dllexport)
    HRESULT
    STDMETHODCALLTYPE CreateDXGIFactory (REFIID   riid,
                                   _Out_ void   **ppFactory)
  {
#ifdef HOOK_D3D11_DEVICE_CREATION
    //BMF_InstallD3D11DeviceHooks ();
#else
    WaitForInit ();
#endif

    std::wstring iname = BMF_GetDXGIFactoryInterfaceEx  (riid);
    int          iver  = BMF_GetDXGIFactoryInterfaceVer (riid);

    dll_log_CALL_2 (L"CreateDXGIFactory", L"%s, %08Xh",
      iname.c_str (), ppFactory);

    HRESULT ret;
    DXGI_CALL (ret, CreateDXGIFactory_Import (riid, ppFactory));

    DXGI_VIRTUAL_OVERRIDE (ppFactory, 7, "IDXGIFactory::EnumAdapters",
                           EnumAdapters_Override, EnumAdapters_Original,
                           EnumAdapters_t);

    DXGI_VIRTUAL_OVERRIDE (ppFactory, 10, "IDXGIFactory::CreateSwapChain",
                           CreateSwapChain_Override, CreateSwapChain_Original,
                           CreateSwapChain_t);

#if 1
    // DXGI 1.1+
    if (iver > 0) {
      DXGI_VIRTUAL_OVERRIDE (ppFactory, 12, "IDXGIFactory::EnumAdapters1",
                             EnumAdapters1_Override, EnumAdapters1_Original,
                             EnumAdapters1_t);
    }
#endif

    return ret;
  }

  __declspec (dllexport)
    HRESULT
    STDMETHODCALLTYPE CreateDXGIFactory1 (REFIID   riid,
                                    _Out_ void   **ppFactory)
  {
#ifdef HOOK_D3D11_DEVICE_CREATION
    BMF_InstallD3D11DeviceHooks ();
#else
    WaitForInit ();
#endif

    std::wstring iname = BMF_GetDXGIFactoryInterfaceEx  (riid);
    int          iver  = BMF_GetDXGIFactoryInterfaceVer (riid);

    dll_log_CALL_2 (L"CreateDXGIFactory1", L"%s, %08Xh",
      iname.c_str (), ppFactory);

    // Windows Vista does not have this function -- wrap it with CreateDXGIFactory
    if (CreateDXGIFactory1_Import == nullptr) {
      dll_log.Log (L"  >> Falling back to CreateDXGIFactory on Vista...\n");
      return CreateDXGIFactory (riid, ppFactory);
    }

    HRESULT ret;
    DXGI_CALL (ret, CreateDXGIFactory1_Import (riid, ppFactory));

    DXGI_VIRTUAL_OVERRIDE (ppFactory, 7,  "IDXGIFactory1::EnumAdapters",
                           EnumAdapters_Override,  EnumAdapters_Original,
                           EnumAdapters_t);
    DXGI_VIRTUAL_OVERRIDE (ppFactory, 10, "IDXGIFactory1::CreateSwapChain",
                           CreateSwapChain_Override, CreateSwapChain_Original,
                           CreateSwapChain_t);

#if 1
    // DXGI 1.1+
    if (iver > 0) {
      DXGI_VIRTUAL_OVERRIDE (ppFactory, 12, "IDXGIFactory1::EnumAdapters1",
                             EnumAdapters1_Override, EnumAdapters1_Original,
                             EnumAdapters1_t);
    }
#endif

    return ret;
  }

  __declspec (dllexport)
    HRESULT
    STDMETHODCALLTYPE CreateDXGIFactory2 (UINT     Flags,
                                          REFIID   riid,
                                    _Out_ void   **ppFactory)
  {
#ifdef HOOK_D3D11_DEVICE_CREATION
    BMF_InstallD3D11DeviceHooks ();
#else
    WaitForInit ();
#endif

    std::wstring iname = BMF_GetDXGIFactoryInterfaceEx  (riid);
    int          iver  = BMF_GetDXGIFactoryInterfaceVer (riid);

    dll_log_CALL_3 (L"CreateDXGIFactory2", L"0x%04X, %s, %08Xh",
      Flags, iname.c_str (), ppFactory);

    // Windows 7 does not have this function -- wrap it with CreateDXGIFactory1
    if (CreateDXGIFactory2_Import == nullptr) {
      dll_log.Log (L"  >> Falling back to CreateDXGIFactory1 on Vista/7...\n");
      return CreateDXGIFactory1 (riid, ppFactory);
    }

    HRESULT ret;
    DXGI_CALL (ret, CreateDXGIFactory2_Import (Flags, riid, ppFactory));

    DXGI_VIRTUAL_OVERRIDE (ppFactory, 7, "IDXGIFactory2::EnumAdapters",
                           EnumAdapters_Override, EnumAdapters_Original,
                           EnumAdapters_t);
    DXGI_VIRTUAL_OVERRIDE (ppFactory, 10, "IDXGIFactory2::CreateSwapChain",
                           CreateSwapChain_Override, CreateSwapChain_Original,
                           CreateSwapChain_t);

#if 1
    // DXGI 1.1+
    if (iver > 0) {
      DXGI_VIRTUAL_OVERRIDE (ppFactory, 12, "IDXGIFactory2::EnumAdapters1",
                             EnumAdapters1_Override, EnumAdapters1_Original,
                             EnumAdapters1_t);
    }
#endif

    return ret;
  }

  DXGI_STUB (HRESULT, DXGID3D10CreateDevice,
    (HMODULE hModule, IDXGIFactory *pFactory, IDXGIAdapter *pAdapter,
      UINT    Flags,   void         *unknown,  void         *ppDevice),
    (hModule, pFactory, pAdapter, Flags, unknown, ppDevice));

  struct UNKNOWN5 {
    DWORD unknown [5];
  };

  DXGI_STUB (HRESULT, DXGID3D10CreateLayeredDevice,
    (UNKNOWN5 Unknown),
    (Unknown))

  DXGI_STUB (SIZE_T, DXGID3D10GetLayeredDeviceSize,
    (const void *pLayers, UINT NumLayers),
    (pLayers, NumLayers))

  DXGI_STUB (HRESULT, DXGID3D10RegisterLayers,
    (const void *pLayers, UINT NumLayers),
    (pLayers, NumLayers))

  __declspec (dllexport)
  HRESULT
  STDMETHODCALLTYPE DXGIDumpJournal (void)
  {
    dll_log_CALL_0 (L"DXGIDumpJournal");

    return E_NOTIMPL;
  }

  __declspec (dllexport)
  HRESULT
  STDMETHODCALLTYPE DXGIReportAdapterConfiguration (void)
  {
    dll_log_CALL_0 (L"DXGIReportAdapterConfiguration");

    return E_NOTIMPL;
  }
}

bool
BMF::DXGI::Startup (void)
{
  return BMF_StartupCore (L"dxgi", dxgi_init_callback);
}

bool
BMF::DXGI::Shutdown (void)
{
  return BMF_ShutdownCore (L"dxgi");
}