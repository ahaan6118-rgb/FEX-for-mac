#include <windows.h>

static void print(const char *s) {
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), NULL, NULL);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            print("WM_CREATE\n");
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            print("WM_PAINT begin\n");
            RECT rc;
            GetClientRect(hWnd, &rc);
            SetBkMode(hdc, TRANSPARENT);
            DrawTextA(hdc, "Hello from Rosetta Killer!", -1, &rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            EndPaint(hWnd, &ps);
            print("WM_PAINT end\n");
            return 0;
        }
        case WM_DESTROY:
            print("WM_DESTROY\n");
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

    print("Registering class...\n");
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "TestWin";
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassA(&wc)) { print("RegisterClass FAILED\n"); return 1; }

    hWnd = CreateWindowExA(0, "TestWin", "Rosetta Killer Test",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           100, 100, 400, 300,
                           NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!hWnd) {
        char buf[128];
        int n = wsprintfA(buf, "CreateWindow FAILED err=%lu\n", GetLastError());
        print(buf);
        return 1;
    }
    print("Window created and visible!\n");

    /* Brief message loop */
    {
        int count = 0;
        while (count < 30) {
            if (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) break;
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            count++;
            Sleep(100);
        }
    }

    DestroyWindow(hWnd);
    print("PASS: GUI test complete\n");
    return 0;
}
