#include <windows.h>

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "WM_CREATE\n", 10, NULL, NULL);
            return 0;
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
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "Class registered OK\n", 20, NULL, NULL);

    hWnd = CreateWindowExA(0, "TestWin", "Rosetta Killer Test",
                           WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
                           NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!hWnd) {
        char buf[128];
        DWORD err = GetLastError();
        int n = wsprintfA(buf, "CreateWindow FAILED err=%lu\n", err);
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);
        return 1;
    }
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "Window created OK!\n", 19, NULL, NULL);

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "Window shown\n", 13, NULL, NULL);

    /* Run message loop briefly, then exit */
    {
        int count = 0;
        while (count < 10) {
            if (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) break;
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            count++;
            Sleep(50);
        }
    }

    DestroyWindow(hWnd);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "PASS: GUI test complete\n", 24, NULL, NULL);
    return 0;
}
