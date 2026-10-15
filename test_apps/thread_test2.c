#include <windows.h>
#include <stdio.h>

#define NUM_THREADS 4

volatile LONG shared_counter = 0;
volatile LONG threads_done = 0;

DWORD WINAPI worker_thread(LPVOID param) {
    int id = (int)(UINT_PTR)param;
    
    for (int i = 0; i < 1000; i++) {
        InterlockedIncrement(&shared_counter);
    }
    
    InterlockedIncrement(&threads_done);
    return id * 10;
}

int main(void) {
    HANDLE threads[NUM_THREADS];
    DWORD exit_codes[NUM_THREADS];
    char buf[512];
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    int len;

    len = sprintf(buf, "=== Threading Test ===\r\nMain TID=%lu\r\n", GetCurrentThreadId());
    WriteFile(out, buf, len, &written, NULL);

    for (int i = 0; i < NUM_THREADS; i++) {
        threads[i] = CreateThread(NULL, 0, worker_thread, (LPVOID)(UINT_PTR)i, 0, NULL);
        if (!threads[i]) {
            len = sprintf(buf, "CreateThread %d FAILED err=%lu\r\n", i, GetLastError());
            WriteFile(out, buf, len, &written, NULL);
        } else {
            len = sprintf(buf, "Created thread %d handle=%p\r\n", i, threads[i]);
            WriteFile(out, buf, len, &written, NULL);
        }
    }

    DWORD wait = WaitForMultipleObjects(NUM_THREADS, threads, TRUE, 10000);
    len = sprintf(buf, "Wait=%lu done=%ld counter=%ld\r\n", wait, threads_done, shared_counter);
    WriteFile(out, buf, len, &written, NULL);

    for (int i = 0; i < NUM_THREADS; i++) {
        GetExitCodeThread(threads[i], &exit_codes[i]);
        CloseHandle(threads[i]);
        len = sprintf(buf, "Thread %d exit=%lu\r\n", i, exit_codes[i]);
        WriteFile(out, buf, len, &written, NULL);
    }

    int pass = (shared_counter == NUM_THREADS * 1000) && (threads_done == NUM_THREADS);
    len = sprintf(buf, "Counter=%ld/%d Done=%ld/%d %s\r\n",
                  shared_counter, NUM_THREADS*1000, threads_done, NUM_THREADS,
                  pass ? "=== PASS ===" : "=== FAIL ===");
    WriteFile(out, buf, len, &written, NULL);
    return pass ? 0 : 1;
}
