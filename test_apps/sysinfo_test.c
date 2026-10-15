#include <windows.h>

int main(void) {
    char buf[512];
    int n;
    SYSTEM_INFO si;

    GetSystemInfo(&si);

    n = wsprintfA(buf, "wProcessorArchitecture: %u", si.wProcessorArchitecture);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);
    switch (si.wProcessorArchitecture) {
        case 0:  WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), " (x86)\n", 7, NULL, NULL); break;
        case 5:  WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), " (ARM)\n", 7, NULL, NULL); break;
        case 9:  WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), " (AMD64)\n", 9, NULL, NULL); break;
        case 12: WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), " (ARM64)\n", 9, NULL, NULL); break;
        default: WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), " (unknown)\n", 11, NULL, NULL); break;
    }

    n = wsprintfA(buf, "dwNumberOfProcessors: %lu\n", si.dwNumberOfProcessors);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

    n = wsprintfA(buf, "dwProcessorType: %lu\n", si.dwProcessorType);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

    n = wsprintfA(buf, "wProcessorLevel: %u\n", si.wProcessorLevel);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

    n = wsprintfA(buf, "dwPageSize: %lu\n", si.dwPageSize);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

    n = wsprintfA(buf, "lpMinAppAddr: %p\n", si.lpMinimumApplicationAddress);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

    n = wsprintfA(buf, "lpMaxAppAddr: %p\n", si.lpMaximumApplicationAddress);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

    if (si.wProcessorArchitecture == 9)
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "PASS\n", 5, NULL, NULL);
    else
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "FAIL: expected AMD64 (9)\n", 24, NULL, NULL);

    return 0;
}
