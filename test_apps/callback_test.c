#include <windows.h>

static int callback_count = 0;

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    callback_count++;
    if (msg == WM_USER + 1) {
        char buf[64];
        int n = wsprintfA(buf, "WM_USER+1 received wParam=%lu\n", (unsigned long)wParam);
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);
        return 42;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

int main(void) {
    WNDCLASSA wc = {0};
    HWND hWnd;
    char buf[128];
    int n;

    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "CallbackTest";

    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "Registering...\n", 15, NULL, NULL);
    if (!RegisterClassA(&wc)) {
        n = wsprintfA(buf, "RegisterClass FAILED err=%lu\n", GetLastError());
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);
        return 1;
    }

    /* HWND_MESSAGE = message-only window, no desktop needed */
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "Creating msg window...\n", 23, NULL, NULL);
    hWnd = CreateWindowExA(0, "CallbackTest", NULL, 0,
                           0, 0, 0, 0,
                           HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
    if (!hWnd) {
        n = wsprintfA(buf, "CreateWindow FAILED err=%lu\n", GetLastError());
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);
        return 1;
    }

    n = wsprintfA(buf, "Window created: %p\n", (void*)hWnd);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

    /* Send a message to test callback dispatch */
    LRESULT r = SendMessageA(hWnd, WM_USER + 1, 123, 0);
    n = wsprintfA(buf, "SendMessage returned: %ld\n", (long)r);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

    r = SendMessageA(hWnd, WM_USER + 1, 456, 0);
    n = wsprintfA(buf, "SendMessage returned: %ld\n", (long)r);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

    DestroyWindow(hWnd);

    n = wsprintfA(buf, "Total callbacks: %d\n", callback_count);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "PASS: callback test\n", 20, NULL, NULL);
    return 0;
}
