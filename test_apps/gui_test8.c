#include <windows.h>

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "WM_CREATE\n", 10, NULL, NULL);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT r;
            GetClientRect(hWnd, &r);
            FillRect(hdc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, 0x00FF0000);
            TextOutA(hdc, 50, 50, "Rosetta Killer Works!", 21);
            EndPaint(hWnd, &ps);
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "WM_PAINT done\n", 14, NULL, NULL);
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

    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "TestWin";
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassA(&wc)) return 1;

    hWnd = CreateWindowExA(0, "TestWin", "Rosetta Killer Test",
                           WS_OVERLAPPEDWINDOW,
                           100, 100, 500, 400,
                           NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!hWnd) return 1;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "Window OK\n", 10, NULL, NULL);

    ShowWindow(hWnd, SW_SHOW);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "ShowWindow done\n", 16, NULL, NULL);

    /* Sleep long enough to see the window, skip UpdateWindow */
    Sleep(8000);

    DestroyWindow(hWnd);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "PASS\n", 5, NULL, NULL);
    return 0;
}
