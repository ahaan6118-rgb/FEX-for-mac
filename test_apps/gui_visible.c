#include <windows.h>

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT r;
            GetClientRect(hWnd, &r);
            FillRect(hdc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, 0x000000FF);
            TextOutA(hdc, 30, 30, "Rosetta Killer Works!", 21);
            SetTextColor(hdc, 0x00000000);
            TextOutA(hdc, 30, 60, "x86_64 PE -> ARM64 JIT on macOS", 31);
            TextOutA(hdc, 30, 90, "FEXCore + Wine + Apple Silicon", 30);
            EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcA(hWnd, msg, wParam, lParam);
    }
}

int main(void) {
    WNDCLASSA wc = {0};
    HWND hWnd;
    MSG msg;

    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "RKTest";
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassA(&wc);

    hWnd = CreateWindowExA(0, "RKTest", "Rosetta Killer - GUI Test",
                           WS_OVERLAPPEDWINDOW,
                           100, 100, 500, 250,
                           NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!hWnd) return 1;

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    /* Brief message loop then auto-exit */
    {
        int i;
        for (i = 0; i < 100; i++) {
            if (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) break;
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            Sleep(100);
        }
    }

    DestroyWindow(hWnd);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "PASS\n", 5, NULL, NULL);
    return 0;
}
