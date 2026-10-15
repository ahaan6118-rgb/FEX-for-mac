#include <windows.h>

typedef long HRESULT;
#define S_OK 0
#define SUCCEEDED(hr) ((hr) >= 0)
#define FAILED(hr)    ((hr) < 0)

typedef struct IDXGIFactory IDXGIFactory;
typedef struct IDXGIAdapter IDXGIAdapter;
typedef struct ID3D11Device ID3D11Device;
typedef struct ID3D11DeviceContext ID3D11DeviceContext;

typedef HRESULT (WINAPI *PFN_CreateDXGIFactory)(const void*, void**);
typedef HRESULT (WINAPI *PFN_D3D11CreateDevice)(void*, int, void*, unsigned int,
    const int*, unsigned int, unsigned int, void**, int*, void**);

int main(void) {
    char buf[512];
    int n;
    HMODULE dxgi, d3d11;
    PFN_CreateDXGIFactory pCreateDXGIFactory;
    PFN_D3D11CreateDevice pD3D11CreateDevice;
    HRESULT hr;

    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "=== D3D11 Support Test ===\n", 27, NULL, NULL);

    dxgi = LoadLibraryA("dxgi.dll");
    n = wsprintfA(buf, "LoadLibrary(dxgi.dll): %s\n", dxgi ? "OK" : "FAILED");
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

    d3d11 = LoadLibraryA("d3d11.dll");
    n = wsprintfA(buf, "LoadLibrary(d3d11.dll): %s\n", d3d11 ? "OK" : "FAILED");
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

    if (!dxgi || !d3d11) {
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "FAIL: cannot load D3D DLLs\n", 27, NULL, NULL);
        return 1;
    }

    pCreateDXGIFactory = (PFN_CreateDXGIFactory)GetProcAddress(dxgi, "CreateDXGIFactory");
    n = wsprintfA(buf, "CreateDXGIFactory: %s\n", pCreateDXGIFactory ? "found" : "NOT FOUND");
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

    pD3D11CreateDevice = (PFN_D3D11CreateDevice)GetProcAddress(d3d11, "D3D11CreateDevice");
    n = wsprintfA(buf, "D3D11CreateDevice: %s\n", pD3D11CreateDevice ? "found" : "NOT FOUND");
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

    if (pD3D11CreateDevice) {
        void *device = NULL, *context = NULL;
        int feature_level = 0;
        hr = pD3D11CreateDevice(
            NULL,       /* adapter */
            1,          /* D3D_DRIVER_TYPE_HARDWARE */
            NULL,       /* software */
            0,          /* flags */
            NULL,       /* feature levels */
            0,          /* num feature levels */
            7,          /* SDK version */
            &device,
            &feature_level,
            &context
        );
        n = wsprintfA(buf, "D3D11CreateDevice: hr=0x%08lx\n", (unsigned long)hr);
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

        if (SUCCEEDED(hr)) {
            n = wsprintfA(buf, "Feature level: 0x%x\n", feature_level);
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "D3D11 device created OK!\n", 25, NULL, NULL);
        } else {
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "D3D11 device creation failed\n", 29, NULL, NULL);
        }
    }

    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "DONE\n", 5, NULL, NULL);
    return 0;
}
