#include <windows.h>

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    char buf[128];
    int n;
    switch (msg) {
        case WM_CREATE:
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "WM_CREATE\n", 10, NULL, NULL);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            n = wsprintfA(buf, "WndProc msg=0x%x\n", msg);
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);
            return DefWindowProcA(hWnd, msg, wParam, lParam);
    }
}

int main(void) {
    WNDCLASSA wc = {0};
    HWND hWnd;

    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "TestWin";
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassA(&wc)) return 1;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "Class OK\n", 9, NULL, NULL);

    hWnd = CreateWindowExA(0, "TestWin", "Rosetta Killer Test",
                           WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
                           NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!hWnd) return 1;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "Window OK\n", 10, NULL, NULL);

    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "Calling ShowWindow...\n", 21, NULL, NULL);
    ShowWindow(hWnd, SW_SHOW);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "ShowWindow done\n", 16, NULL, NULL);

    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "Calling UpdateWindow...\n", 23, NULL, NULL);
    UpdateWindow(hWnd);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "UpdateWindow done\n", 18, NULL, NULL);

    Sleep(2000);

    DestroyWindow(hWnd);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "PASS\n", 5, NULL, NULL);
    return 0;
}
