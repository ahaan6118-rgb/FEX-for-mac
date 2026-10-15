#include <stdio.h>
#include <windows.h>

int main(int argc, char *argv[]) {
    printf("=== Win32 API Test ===\n\n");

    /* GetTickCount */
    DWORD tick = GetTickCount();
    printf("[1] GetTickCount: %lu ms\n", (unsigned long)tick);

    /* GetCurrentProcessId / GetCurrentThreadId */
    DWORD pid = GetCurrentProcessId();
    DWORD tid = GetCurrentThreadId();
    printf("[2] PID=%lu TID=%lu\n", (unsigned long)pid, (unsigned long)tid);

    /* GetSystemInfo */
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    printf("[3] SystemInfo: arch=%u, page_size=%lu, num_cpus=%lu\n",
           si.wProcessorArchitecture,
           (unsigned long)si.dwPageSize,
           (unsigned long)si.dwNumberOfProcessors);

    /* GetCommandLineA */
    char *cmdline = GetCommandLineA();
    printf("[4] CommandLine: %s\n", cmdline ? cmdline : "(null)");

    /* GetModuleHandleA */
    HMODULE hmod = GetModuleHandleA(NULL);
    printf("[5] Module base: %p\n", (void*)hmod);

    /* HeapAlloc / HeapFree */
    HANDLE heap = GetProcessHeap();
    void *mem = HeapAlloc(heap, 0, 256);
    printf("[6] HeapAlloc: %p (heap=%p)\n", mem, (void*)heap);
    if (mem) {
        memset(mem, 0xAA, 256);
        HeapFree(heap, 0, mem);
        printf("    HeapFree: OK\n");
    }

    /* GetStdHandle + WriteConsoleA */
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    const char *msg = "[7] WriteConsoleA: works!\n";
    DWORD written = 0;
    WriteConsoleA(hout, msg, strlen(msg), &written, NULL);

    /* File I/O: CreateFile + WriteFile + ReadFile */
    HANDLE hfile = CreateFileA("_rk_test.tmp",
                               GENERIC_WRITE | GENERIC_READ,
                               0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hfile != INVALID_HANDLE_VALUE) {
        const char *data = "Rosetta Killer file I/O test";
        DWORD bw = 0;
        WriteFile(hfile, data, strlen(data), &bw, NULL);
        printf("[8] WriteFile: %lu bytes written\n", (unsigned long)bw);

        SetFilePointer(hfile, 0, NULL, FILE_BEGIN);
        char buf[64] = {0};
        DWORD br = 0;
        ReadFile(hfile, buf, sizeof(buf)-1, &br, NULL);
        printf("    ReadFile: '%s' (%lu bytes)\n", buf, (unsigned long)br);
        CloseHandle(hfile);
        DeleteFileA("_rk_test.tmp");
        printf("    File cleanup: OK\n");
    } else {
        printf("[8] CreateFile failed: %lu\n", (unsigned long)GetLastError());
    }

    /* GetEnvironmentVariableA */
    char envbuf[256] = {0};
    DWORD envlen = GetEnvironmentVariableA("PATH", envbuf, sizeof(envbuf));
    printf("[9] PATH length: %lu chars\n", (unsigned long)envlen);

    printf("\nAll Win32 API tests completed.\n");
    return 0;
}
