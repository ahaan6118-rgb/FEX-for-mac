// Direct3D 11 / DXGI stub layer — intercepts D3D calls from x86_64 guest code
// Maps D3D11 device creation and rendering to Metal via host bridge
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include <new>

#include "metal_backend.h"

namespace D3D {

// --- GUID / IID definitions ---
struct GUID {
  uint32_t Data1;
  uint16_t Data2;
  uint16_t Data3;
  uint8_t  Data4[8];
};
using IID = GUID;
using REFIID = const IID&;

static bool GuidEqual(const GUID& a, const GUID& b) {
  return memcmp(&a, &b, sizeof(GUID)) == 0;
}

// Key IIDs
static constexpr GUID IID_IUnknown =
  {0x00000000, 0x0000, 0x0000, {0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static constexpr GUID IID_IDXGIFactory =
  {0x7b7166ec, 0x21c7, 0x44ae, {0xb2,0x1a,0xc9,0xae,0x32,0x1a,0xe3,0x69}};
static constexpr GUID IID_IDXGIAdapter =
  {0x2411e7e1, 0x12ac, 0x4ccf, {0xbd,0x14,0x97,0x98,0xe8,0x53,0x4d,0xc0}};
static constexpr GUID IID_IDXGIDevice =
  {0x54ec77fa, 0x1377, 0x44e6, {0x8c,0x32,0x88,0xfd,0x5f,0x44,0xc8,0x4c}};
static constexpr GUID IID_ID3D11Device =
  {0xdb6f6ddb, 0xac77, 0x4e88, {0x82,0x53,0x81,0x9d,0xf9,0xbb,0xf1,0x40}};
static constexpr GUID IID_ID3D11DeviceContext =
  {0xc0bfa96c, 0xe089, 0x44fb, {0x8e,0xaf,0x26,0xf8,0x79,0x61,0x90,0xda}};

// --- HRESULT codes ---
static constexpr int32_t S_OK = 0;
static constexpr int32_t S_FALSE = 1;
static constexpr int32_t E_NOINTERFACE = static_cast<int32_t>(0x80004002);
static constexpr int32_t E_FAIL = static_cast<int32_t>(0x80004005);
static constexpr int32_t E_NOTIMPL = static_cast<int32_t>(0x80004001);
static constexpr int32_t DXGI_ERROR_NOT_FOUND = static_cast<int32_t>(0x887A0002);

// --- D3D_FEATURE_LEVEL ---
enum D3D_FEATURE_LEVEL : uint32_t {
  D3D_FEATURE_LEVEL_11_0 = 0xb000,
  D3D_FEATURE_LEVEL_11_1 = 0xb100,
  D3D_FEATURE_LEVEL_12_0 = 0xc000,
  D3D_FEATURE_LEVEL_12_1 = 0xc100,
};

// --- D3D_DRIVER_TYPE ---
enum D3D_DRIVER_TYPE : uint32_t {
  D3D_DRIVER_TYPE_UNKNOWN   = 0,
  D3D_DRIVER_TYPE_HARDWARE  = 1,
  D3D_DRIVER_TYPE_REFERENCE = 2,
  D3D_DRIVER_TYPE_NULL      = 3,
  D3D_DRIVER_TYPE_SOFTWARE  = 4,
  D3D_DRIVER_TYPE_WARP      = 5,
};

// --- DXGI_FORMAT (subset) ---
enum DXGI_FORMAT : uint32_t {
  DXGI_FORMAT_UNKNOWN                  = 0,
  DXGI_FORMAT_R8G8B8A8_UNORM          = 28,
  DXGI_FORMAT_R8G8B8A8_UNORM_SRGB     = 29,
  DXGI_FORMAT_B8G8R8A8_UNORM          = 87,
  DXGI_FORMAT_B8G8R8A8_UNORM_SRGB     = 91,
  DXGI_FORMAT_D24_UNORM_S8_UINT       = 45,
  DXGI_FORMAT_D32_FLOAT               = 40,
  DXGI_FORMAT_R32G32B32A32_FLOAT      = 2,
  DXGI_FORMAT_R32G32B32_FLOAT         = 6,
  DXGI_FORMAT_R32G32_FLOAT            = 16,
  DXGI_FORMAT_R32_FLOAT               = 41,
};

// --- D3D11 bind flags ---
static constexpr uint32_t D3D11_BIND_VERTEX_BUFFER    = 0x1;
static constexpr uint32_t D3D11_BIND_INDEX_BUFFER     = 0x2;
static constexpr uint32_t D3D11_BIND_CONSTANT_BUFFER  = 0x4;
static constexpr uint32_t D3D11_BIND_SHADER_RESOURCE  = 0x8;
static constexpr uint32_t D3D11_BIND_RENDER_TARGET    = 0x20;
static constexpr uint32_t D3D11_BIND_DEPTH_STENCIL    = 0x40;

// Map DXGI format to Metal pixel format
static Metal::MTLPixelFormat DXGIToMetal(DXGI_FORMAT fmt) {
  switch (fmt) {
  case DXGI_FORMAT_R8G8B8A8_UNORM:      return Metal::MTLPixelFormatRGBA8Unorm;
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return Metal::MTLPixelFormatRGBA8Unorm_sRGB;
  case DXGI_FORMAT_B8G8R8A8_UNORM:      return Metal::MTLPixelFormatBGRA8Unorm;
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return Metal::MTLPixelFormatBGRA8Unorm_sRGB;
  case DXGI_FORMAT_D32_FLOAT:           return Metal::MTLPixelFormatDepth32Float;
  case DXGI_FORMAT_R32_FLOAT:           return Metal::MTLPixelFormatR32Float;
  case DXGI_FORMAT_R32G32_FLOAT:        return Metal::MTLPixelFormatRG32Float;
  case DXGI_FORMAT_R32G32B32A32_FLOAT:  return Metal::MTLPixelFormatRGBA32Float;
  default:                               return Metal::MTLPixelFormatBGRA8Unorm;
  }
}

// Map D3D11 primitive topology to Metal
static Metal::MTLPrimitiveType D3DTopologyToMetal(uint32_t topology) {
  switch (topology) {
  case 1: return Metal::MTLPrimitiveTypePoint;         // POINTLIST
  case 2: return Metal::MTLPrimitiveTypeLine;           // LINELIST
  case 3: return Metal::MTLPrimitiveTypeLineStrip;      // LINESTRIP
  case 4: return Metal::MTLPrimitiveTypeTriangle;       // TRIANGLELIST
  case 5: return Metal::MTLPrimitiveTypeTriangleStrip;  // TRIANGLESTRIP
  default: return Metal::MTLPrimitiveTypeTriangle;
  }
}

// --- DXGI structures ---
struct DXGI_MODE_DESC {
  uint32_t Width;
  uint32_t Height;
  struct { uint32_t Numerator; uint32_t Denominator; } RefreshRate;
  DXGI_FORMAT Format;
  uint32_t ScanlineOrdering;
  uint32_t Scaling;
};

struct DXGI_SWAP_CHAIN_DESC {
  DXGI_MODE_DESC BufferDesc;
  struct { uint32_t Count; uint32_t Quality; } SampleDesc;
  uint32_t BufferUsage;
  uint32_t BufferCount;
  uint64_t OutputWindow;
  int32_t  Windowed;
  uint32_t SwapEffect;
  uint32_t Flags;
};

struct DXGI_ADAPTER_DESC {
  uint16_t Description[128];
  uint32_t VendorId;
  uint32_t DeviceId;
  uint32_t SubSysId;
  uint32_t Revision;
  uint64_t DedicatedVideoMemory;
  uint64_t DedicatedSystemMemory;
  uint64_t SharedSystemMemory;
  struct { uint32_t LowPart; int32_t HighPart; } AdapterLuid;
};

// --- Fake COM vtable infrastructure ---
struct FakeComObject {
  void** vtable;
  uint32_t ref_count;
  uint32_t type_id;
  void* backend_data;
};

enum ObjectType : uint32_t {
  OBJ_DXGI_FACTORY = 1,
  OBJ_DXGI_ADAPTER,
  OBJ_DXGI_DEVICE,
  OBJ_DXGI_SWAP_CHAIN,
  OBJ_D3D11_DEVICE,
  OBJ_D3D11_DEVICE_CONTEXT,
  OBJ_D3D11_RENDER_TARGET_VIEW,
  OBJ_D3D11_TEXTURE2D,
  OBJ_D3D11_BUFFER,
  OBJ_D3D11_VERTEX_SHADER,
  OBJ_D3D11_PIXEL_SHADER,
  OBJ_D3D11_INPUT_LAYOUT,
  OBJ_D3D11_DEPTH_STENCIL_VIEW,
  OBJ_D3D11_RASTERIZER_STATE,
  OBJ_D3D11_PIPELINE_STATE,
};

// --- D3D11 Stub Manager ---
// Manages fake COM objects backed by real Metal resources.

class D3D11StubManager {
public:
  bool Init() {
    if (!metal_.Init()) {
      printf("[DXMT] Metal init failed — falling back to stubs\n");
      metal_available_ = false;
    } else {
      metal_available_ = true;
    }
    initialized_ = true;
    printf("[DXMT] D3D11 stub manager initialized (Metal: %s)\n",
           metal_available_ ? "YES" : "NO");
    return true;
  }

  int32_t CreateDevice(
    uint64_t adapter, D3D_DRIVER_TYPE driver_type, uint64_t software,
    uint32_t flags, const D3D_FEATURE_LEVEL* feature_levels,
    uint32_t num_feature_levels, uint32_t sdk_version,
    uint64_t* out_device, D3D_FEATURE_LEVEL* out_feature_level,
    uint64_t* out_context
  ) {
    printf("[DXMT] D3D11CreateDevice (driver=%u, flags=0x%x, Metal=%s)\n",
           driver_type, flags, metal_available_ ? "YES" : "stub");

    if (out_feature_level)
      *out_feature_level = D3D_FEATURE_LEVEL_11_1;

    if (out_device) {
      auto* dev = CreateFakeObject(OBJ_D3D11_DEVICE);
      if (!dev) return E_FAIL;
      *out_device = reinterpret_cast<uint64_t>(dev);
      printf("[DXMT]   Device: %p\n", dev);
    }

    if (out_context) {
      auto* ctx = CreateFakeObject(OBJ_D3D11_DEVICE_CONTEXT);
      if (!ctx) return E_FAIL;
      *out_context = reinterpret_cast<uint64_t>(ctx);
      printf("[DXMT]   Context: %p\n", ctx);
    }

    return S_OK;
  }

  int32_t CreateDXGIFactory(const GUID& riid, uint64_t* out) {
    printf("[DXMT] CreateDXGIFactory\n");
    if (!out) return E_FAIL;
    auto* factory = CreateFakeObject(OBJ_DXGI_FACTORY);
    if (!factory) return E_FAIL;
    *out = reinterpret_cast<uint64_t>(factory);
    return S_OK;
  }

  int64_t HandleMethodCall(uint64_t this_ptr, uint32_t method_index) {
    auto* obj = reinterpret_cast<FakeComObject*>(this_ptr);
    if (!obj) return E_FAIL;

    switch (obj->type_id) {
    case OBJ_D3D11_DEVICE:       return HandleDeviceMethod(obj, method_index);
    case OBJ_D3D11_DEVICE_CONTEXT: return HandleContextMethod(obj, method_index);
    case OBJ_DXGI_FACTORY:       return HandleFactoryMethod(obj, method_index);
    case OBJ_DXGI_ADAPTER:       return HandleAdapterMethod(obj, method_index);
    case OBJ_DXGI_SWAP_CHAIN:    return HandleSwapChainMethod(obj, method_index);
    default:
      return E_NOTIMPL;
    }
  }

  bool IsInitialized() const { return initialized_; }
  bool HasMetal() const { return metal_available_; }
  Metal::MetalBackend& GetMetal() { return metal_; }

private:
  FakeComObject* CreateFakeObject(ObjectType type) {
    if (objects_.size() >= 1024) return nullptr;
    auto* obj = new (std::nothrow) FakeComObject;
    if (!obj) return nullptr;
    obj->vtable = nullptr;
    obj->ref_count = 1;
    obj->type_id = type;
    obj->backend_data = nullptr;
    objects_.push_back(obj);
    return obj;
  }

  int64_t HandleIUnknown(FakeComObject* obj, uint32_t method) {
    switch (method) {
    case 0: return E_NOINTERFACE;
    case 1: return ++obj->ref_count;
    case 2: {
      if (obj->ref_count == 0) return 0;
      uint32_t rc = --obj->ref_count;
      if (rc == 0) {
        for (auto it = objects_.begin(); it != objects_.end(); ++it) {
          if (*it == obj) { objects_.erase(it); break; }
        }
        delete obj;
      }
      return rc;
    }
    default: return E_NOTIMPL;
    }
  }

  // ID3D11Device methods — now backed by Metal
  int64_t HandleDeviceMethod(FakeComObject* obj, uint32_t method) {
    if (method <= 2) return HandleIUnknown(obj, method);
    switch (method) {
    case 3: { // CreateBuffer(pDesc, pInitialData, ppBuffer)
      printf("[DXMT] CreateBuffer");
      if (metal_available_) {
        auto* buf = metal_.CreateBuffer(4096, D3D11_BIND_VERTEX_BUFFER, nullptr);
        if (buf) {
          auto* com = CreateFakeObject(OBJ_D3D11_BUFFER);
          if (com) com->backend_data = buf;
          printf(" → Metal buffer %p\n", buf);
          return S_OK;
        }
      }
      printf(" (stub)\n");
      return S_OK;
    }
    case 5: { // CreateTexture2D(pDesc, pInitialData, ppTexture)
      printf("[DXMT] CreateTexture2D");
      if (metal_available_) {
        auto* tex = metal_.CreateTexture2D(256, 256,
          Metal::MTLPixelFormatBGRA8Unorm, D3D11_BIND_SHADER_RESOURCE);
        if (tex) {
          auto* com = CreateFakeObject(OBJ_D3D11_TEXTURE2D);
          if (com) com->backend_data = tex;
          printf(" → Metal texture %p\n", tex);
          return S_OK;
        }
      }
      printf(" (stub)\n");
      return S_OK;
    }
    case 9: { // CreateRenderTargetView
      printf("[DXMT] CreateRenderTargetView");
      auto* com = CreateFakeObject(OBJ_D3D11_RENDER_TARGET_VIEW);
      if (com) printf(" → %p\n", com); else printf(" (stub)\n");
      return S_OK;
    }
    case 12: { // CreateDepthStencilView
      printf("[DXMT] CreateDepthStencilView");
      if (metal_available_) {
        auto* depth = metal_.CreateDepthTexture(256, 256);
        if (depth) {
          auto* com = CreateFakeObject(OBJ_D3D11_DEPTH_STENCIL_VIEW);
          if (com) com->backend_data = depth;
          printf(" → Metal depth %p\n", depth);
          return S_OK;
        }
      }
      printf(" (stub)\n");
      return S_OK;
    }
    case 15: { // CreateVertexShader
      printf("[DXMT] CreateVertexShader → stub (DXBC→MSL not yet implemented)\n");
      auto* com = CreateFakeObject(OBJ_D3D11_VERTEX_SHADER);
      return com ? S_OK : E_FAIL;
    }
    case 17: { // CreatePixelShader
      printf("[DXMT] CreatePixelShader → stub (DXBC→MSL not yet implemented)\n");
      auto* com = CreateFakeObject(OBJ_D3D11_PIXEL_SHADER);
      return com ? S_OK : E_FAIL;
    }
    case 20: { // CreateInputLayout
      printf("[DXMT] CreateInputLayout → stub\n");
      auto* com = CreateFakeObject(OBJ_D3D11_INPUT_LAYOUT);
      return com ? S_OK : E_FAIL;
    }
    case 24: { // CreateRasterizerState
      printf("[DXMT] CreateRasterizerState → stub\n");
      auto* com = CreateFakeObject(OBJ_D3D11_RASTERIZER_STATE);
      return com ? S_OK : E_FAIL;
    }
    case 38: return D3D_FEATURE_LEVEL_11_1; // GetFeatureLevel
    case 40: { // GetImmediateContext
      printf("[DXMT] GetImmediateContext\n");
      return S_OK;
    }
    default:
      printf("[DXMT] Device method %u (unimpl)\n", method);
      return E_NOTIMPL;
    }
  }

  // ID3D11DeviceContext methods — render commands backed by Metal
  int64_t HandleContextMethod(FakeComObject* obj, uint32_t method) {
    if (method <= 2) return HandleIUnknown(obj, method);
    switch (method) {
    case 7:  // IASetInputLayout
    case 8:  // IASetVertexBuffers
    case 9:  // IASetIndexBuffer
      return 0;
    case 12: // VSSetShader
    case 20: // PSSetShader
      return 0;
    case 13: // DrawIndexed
      if (metal_available_)
        printf("[DXMT] DrawIndexed (Metal ready, shader pipeline pending)\n");
      return 0;
    case 14: // Draw
      if (metal_available_)
        printf("[DXMT] Draw (Metal ready, shader pipeline pending)\n");
      return 0;
    case 33: // OMSetRenderTargets
      return 0;
    case 44: // RSSetViewports
      return 0;
    case 45: // RSSetScissorRects
      return 0;
    case 46: // IASetPrimitiveTopology
      current_topology_ = Metal::MTLPrimitiveTypeTriangle;
      return 0;
    case 47: { // ClearRenderTargetView(pRTV, ColorRGBA)
      printf("[DXMT] ClearRenderTargetView");
      if (metal_available_) printf(" (Metal)\n"); else printf(" (stub)\n");
      return 0;
    }
    case 48: // ClearDepthStencilView
      return 0;
    default:
      return E_NOTIMPL;
    }
  }

  int64_t HandleFactoryMethod(FakeComObject* obj, uint32_t method) {
    if (method <= 2) return HandleIUnknown(obj, method);
    switch (method) {
    case 7: { // EnumAdapters
      auto* adapter = CreateFakeObject(OBJ_DXGI_ADAPTER);
      if (adapter) {
        printf("[DXMT] EnumAdapters → Apple Silicon adapter\n");
        return S_OK;
      }
      return DXGI_ERROR_NOT_FOUND;
    }
    case 10: { // CreateSwapChain
      auto* sc = CreateFakeObject(OBJ_DXGI_SWAP_CHAIN);
      printf("[DXMT] CreateSwapChain → %p\n", sc);
      return S_OK;
    }
    default:
      return E_NOTIMPL;
    }
  }

  int64_t HandleAdapterMethod(FakeComObject* obj, uint32_t method) {
    if (method <= 2) return HandleIUnknown(obj, method);
    switch (method) {
    case 8: { // GetDesc(pDesc)
      printf("[DXMT] GetDesc → Apple Silicon\n");
      return S_OK;
    }
    default: return E_NOTIMPL;
    }
  }

  int64_t HandleSwapChainMethod(FakeComObject* obj, uint32_t method) {
    if (method <= 2) return HandleIUnknown(obj, method);
    switch (method) {
    case 8:  return S_OK; // Present
    case 9:  return S_OK; // GetBuffer
    case 10: return S_OK; // SetFullscreenState
    case 13: return S_OK; // ResizeBuffers
    default: return E_NOTIMPL;
    }
  }

  Metal::MetalBackend metal_;
  bool initialized_ = false;
  bool metal_available_ = false;
  std::vector<FakeComObject*> objects_;
  Metal::MTLPrimitiveType current_topology_ = Metal::MTLPrimitiveTypeTriangle;
};

} // namespace D3D
