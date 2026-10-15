#include <windows.h>

static void print(const char *s) {
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), NULL, NULL);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) { print("WM_CREATE\n"); return 0; }
    if (msg == WM_DESTROY) { print("WM_DESTROY\n"); PostQuitMessage(0); return 0; }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

int main(void) {
    WNDCLASSA wc = {0};
    HWND hWnd;

    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "TestWin";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&wc);

    hWnd = CreateWindowExA(0, "TestWin", "RK Test",
                           WS_OVERLAPPEDWINDOW,
                           100, 100, 400, 300,
                           NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!hWnd) { print("FAIL\n"); return 1; }
    print("Window OK\n");

    ShowWindow(hWnd, SW_SHOW);
    print("Shown\n");

    Sleep(2000);
    print("Slept\n");

    DestroyWindow(hWnd);
    print("PASS\n");
    return 0;
}
