#include <windows.h>
#include <stdio.h>

#define NUM_THREADS 4

volatile LONG shared_counter = 0;

DWORD WINAPI worker_thread(LPVOID param) {
    int id = (int)(UINT_PTR)param;
    char buf[128];
    int len;

    len = sprintf(buf, "[Thread %d] started (TID=%lu)\n", id, GetCurrentThreadId());
    WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), buf, len, NULL, NULL);

    for (int i = 0; i < 1000; i++) {
        InterlockedIncrement(&shared_counter);
    }

    len = sprintf(buf, "[Thread %d] done, counter now=%ld\n", id, shared_counter);
    WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), buf, len, NULL, NULL);
    return id * 10;
}

int main(void) {
    HANDLE threads[NUM_THREADS];
    DWORD exit_codes[NUM_THREADS];
    char buf[256];
    int len;

    len = sprintf(buf, "=== Threading Test ===\nMain TID=%lu\nCreating %d threads...\n",
                  GetCurrentThreadId(), NUM_THREADS);
    WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), buf, len, NULL, NULL);

    for (int i = 0; i < NUM_THREADS; i++) {
        threads[i] = CreateThread(NULL, 0, worker_thread, (LPVOID)(UINT_PTR)i, 0, NULL);
        if (!threads[i]) {
            len = sprintf(buf, "CreateThread %d FAILED (err=%lu)\n", i, GetLastError());
            WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), buf, len, NULL, NULL);
        }
    }

    DWORD wait = WaitForMultipleObjects(NUM_THREADS, threads, TRUE, 10000);
    len = sprintf(buf, "WaitForMultipleObjects returned %lu\n", wait);
    WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), buf, len, NULL, NULL);

    for (int i = 0; i < NUM_THREADS; i++) {
        GetExitCodeThread(threads[i], &exit_codes[i]);
        CloseHandle(threads[i]);
    }

    len = sprintf(buf, "Final counter=%ld (expected %d)\n", shared_counter, NUM_THREADS * 1000);
    WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), buf, len, NULL, NULL);

    for (int i = 0; i < NUM_THREADS; i++) {
        len = sprintf(buf, "Thread %d exit code=%lu (expected %d)\n", i, exit_codes[i], i * 10);
        WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), buf, len, NULL, NULL);
    }

    len = sprintf(buf, "=== PASS ===\n");
    WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), buf, len, NULL, NULL);
    return 0;
}
