// Minimal D3D11 device creation test
// Links against d3d11.dll and dxgi.dll stubs
#include <stdio.h>
#include <stdint.h>

// Minimal D3D11 type definitions for calling CreateDevice
typedef uint32_t HRESULT;
typedef void* IUnknown;

typedef enum {
    D3D_DRIVER_TYPE_HARDWARE = 1
} D3D_DRIVER_TYPE;

typedef enum {
    D3D_FEATURE_LEVEL_11_0 = 0xb000,
    D3D_FEATURE_LEVEL_11_1 = 0xb100
} D3D_FEATURE_LEVEL;

#define D3D11_SDK_VERSION 7

// Imported from d3d11.dll
__declspec(dllimport) HRESULT __stdcall D3D11CreateDevice(
    void* pAdapter,
    D3D_DRIVER_TYPE DriverType,
    void* Software,
    uint32_t Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    uint32_t FeatureLevels,
    uint32_t SDKVersion,
    void** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel,
    void** ppImmediateContext
);

int main(void) {
    printf("=== D3D11 Device Creation Test ===\n");

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL got_level = 0;
    void* device = NULL;
    void* context = NULL;

    HRESULT hr = D3D11CreateDevice(
        NULL,                       // adapter
        D3D_DRIVER_TYPE_HARDWARE,   // driver type
        NULL,                       // software
        0,                          // flags
        levels,                     // feature levels
        2,                          // num feature levels
        D3D11_SDK_VERSION,          // SDK version
        &device,                    // out device
        &got_level,                 // out feature level
        &context                    // out context
    );

    printf("D3D11CreateDevice returned: 0x%08x\n", hr);
    printf("Device:        %p\n", device);
    printf("Context:       %p\n", context);
    printf("Feature Level: 0x%x\n", got_level);

    if (hr == 0 && device != NULL) {
        printf("SUCCESS: D3D11 device created on Metal!\n");
    } else {
        printf("FAILED: D3D11 device creation failed\n");
    }

    printf("=== D3D11 Test Done ===\n");
    return 0;
}
