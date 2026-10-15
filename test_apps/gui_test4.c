#include <windows.h>

static void print(const char *s) {
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), NULL, NULL);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    char buf[128];
    int n;
    switch (msg) {
        case WM_CREATE: print("WM_CREATE\n"); return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            print("WM_PAINT: BeginPaint...\n");
            HDC hdc = BeginPaint(hWnd, &ps);
            print("WM_PAINT: EndPaint...\n");
            EndPaint(hWnd, &ps);
            print("WM_PAINT: done\n");
            return 0;
        }
        case WM_DESTROY: print("WM_DESTROY\n"); PostQuitMessage(0); return 0;
        default:
            n = wsprintfA(buf, "WndProc msg=0x%x\n", msg);
            print(buf);
            return DefWindowProcA(hWnd, msg, wParam, lParam);
    }
}

int main(void) {
    WNDCLASSA wc = {0};
    HWND hWnd;
    MSG msg;

    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "TestWin";
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&wc);

    hWnd = CreateWindowExA(0, "TestWin", "RK Test",
                           WS_OVERLAPPEDWINDOW,
                           100, 100, 400, 300,
                           NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!hWnd) { print("CreateWindow FAILED\n"); return 1; }
    print("Window created\n");

    print("Calling ShowWindow...\n");
    ShowWindow(hWnd, SW_SHOW);
    print("ShowWindow returned\n");

    print("Entering message loop...\n");
    {
        int i;
        for (i = 0; i < 20; i++) {
            char b[64];
            int n = wsprintfA(b, "Loop %d: PeekMessage...\n", i);
            print(b);
            while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) goto done;
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            n = wsprintfA(b, "Loop %d: Sleep...\n", i);
            print(b);
            Sleep(100);
        }
    }
done:
    print("Destroying window...\n");
    DestroyWindow(hWnd);
    print("PASS\n");
    return 0;
}
