#include <windows.h>

typedef long HRESULT;
#define S_OK 0
#define SUCCEEDED(hr) ((hr) >= 0)

typedef HRESULT (WINAPI *PFN_D3D11CreateDevice)(void*, int, void*, unsigned int,
    const int*, unsigned int, unsigned int, void**, int*, void**);

int main(void) {
    HMODULE d3d11;
    PFN_D3D11CreateDevice pD3D11CreateDevice;
    HRESULT hr;
    void *device = NULL, *context = NULL;
    int feature_level = 0;
    int n;
    char buf[256];

    /* Feature levels to try: 11_1, 11_0, 10_1, 10_0, 9_3 */
    int levels[] = { 0xb100, 0xb000, 0xa100, 0xa000, 0x9300 };
    const char *names[] = { "11_1", "11_0", "10_1", "10_0", "9_3" };
    int i;

    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "=== D3D11 Feature Level Test ===\n", 33, NULL, NULL);

    d3d11 = LoadLibraryA("d3d11.dll");
    if (!d3d11) { WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "FAIL: no d3d11\n", 15, NULL, NULL); return 1; }

    pD3D11CreateDevice = (PFN_D3D11CreateDevice)GetProcAddress(d3d11, "D3D11CreateDevice");
    if (!pD3D11CreateDevice) { WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "FAIL: no func\n", 14, NULL, NULL); return 1; }

    /* Try each level individually */
    for (i = 0; i < 5; i++) {
        device = NULL; context = NULL; feature_level = 0;
        hr = pD3D11CreateDevice(NULL, 1, NULL, 0, &levels[i], 1, 7, &device, &feature_level, &context);
        n = wsprintfA(buf, "FL %s: hr=0x%08lx actual=0x%x\n", names[i], (unsigned long)hr, feature_level);
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);
    }

    /* Auto (NULL levels) */
    device = NULL; context = NULL; feature_level = 0;
    hr = pD3D11CreateDevice(NULL, 1, NULL, 0, NULL, 0, 7, &device, &feature_level, &context);
    n = wsprintfA(buf, "FL auto: hr=0x%08lx actual=0x%x\n", (unsigned long)hr, feature_level);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "DONE\n", 5, NULL, NULL);
    return 0;
}
