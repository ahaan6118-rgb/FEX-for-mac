#include <windows.h>

int main(void) {
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    const char *msg = "Hello from x86_64 Windows binary running on ARM64 macOS via FEXCore JIT!\r\n";
    DWORD written;
    WriteFile(hStdOut, msg, (DWORD)lstrlenA(msg), &written, NULL);
    return 0;
}
