#include <windows.h>

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "WM_CREATE\n", 10, NULL, NULL);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            DrawTextA(hdc, "Hello from Rosetta Killer!", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            EndPaint(hWnd, &ps);
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "WM_PAINT\n", 9, NULL, NULL);
            return 0;
        }
        case WM_DESTROY:
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "WM_DESTROY\n", 11, NULL, NULL);
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

    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "Registering class...\n", 21, NULL, NULL);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "TestWin";
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassA(&wc)) {
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "RegisterClass FAILED\n", 21, NULL, NULL);
        return 1;
    }

    hWnd = CreateWindowExA(0, "TestWin", "Rosetta Killer Test",
                           WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
                           NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!hWnd) {
        char buf[128];
        int n = wsprintfA(buf, "CreateWindow FAILED err=%lu\n", GetLastError());
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);
        return 1;
    }
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "Window created OK!\n", 19, NULL, NULL);

    ShowWindow(hWnd, SW_SHOW);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "ShowWindow done\n", 16, NULL, NULL);

    UpdateWindow(hWnd);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "UpdateWindow done\n", 18, NULL, NULL);

    /* Brief message loop */
    {
        DWORD start = GetTickCount();
        while (GetTickCount() - start < 2000) {
            if (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) break;
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            Sleep(100);
        }
    }

    DestroyWindow(hWnd);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "PASS: GUI test complete\n", 24, NULL, NULL);
    return 0;
}
