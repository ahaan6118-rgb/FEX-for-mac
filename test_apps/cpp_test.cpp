#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct Result {
    const char* name;
    int pass;
    char detail[128];
};

static Result results[20];
static int num_results = 0;

static void add_result(const char* name, int pass, const char* fmt, ...) {
    Result* r = &results[num_results++];
    r->name = name;
    r->pass = pass;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->detail, sizeof(r->detail), fmt, ap);
    va_end(ap);
}

static void test_heap_operations(void) {
    HANDLE heap = GetProcessHeap();
    void* blocks[100];
    int ok = 1;
    for (int i = 0; i < 100; i++) {
        blocks[i] = HeapAlloc(heap, HEAP_ZERO_MEMORY, (i + 1) * 64);
        if (!blocks[i]) { ok = 0; break; }
        memset(blocks[i], 0xAA, (i + 1) * 64);
    }
    for (int i = 99; i >= 0; i--) {
        if (blocks[i]) HeapFree(heap, 0, blocks[i]);
    }
    add_result("HeapStress", ok, "100 allocs/frees");
}

static void test_string_ops(void) {
    char buf[256];
    strcpy(buf, "Hello");
    strcat(buf, " ");
    strcat(buf, "World");
    int ok = (strcmp(buf, "Hello World") == 0);
    sprintf(buf + strlen(buf), " %d", 42);
    ok = ok && (strcmp(buf, "Hello World 42") == 0);
    add_result("StringOps", ok, "'%s'", buf);
}

static void test_math(void) {
    volatile double a = 3.14159265358979;
    volatile double b = 2.71828182845905;
    double sum = a + b;
    double prod = a * b;
    int ok = (sum > 5.85) && (sum < 5.86) && (prod > 8.53) && (prod < 8.54);
    add_result("FloatMath", ok, "pi+e=%.6f pi*e=%.6f", sum, prod);
}

static void test_environment(void) {
    char buf[256] = {};
    DWORD len = GetEnvironmentVariableA("WINEDLLPATH", buf, sizeof(buf));
    int ok = (len > 0 && strlen(buf) > 0);
    add_result("EnvVar", ok, "WINEDLLPATH=%s", ok ? buf : "(empty)");
}

static void test_file_io(void) {
    const char* path = "Z:\\tmp\\rk_cpp_test.txt";
    const char* data = "Rosetta Killer test data 12345";
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    int ok = (f != INVALID_HANDLE_VALUE);
    if (ok) {
        DWORD written;
        WriteFile(f, data, strlen(data), &written, NULL);
        CloseHandle(f);
        f = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        char rbuf[128] = {};
        DWORD bytesRead;
        ReadFile(f, rbuf, sizeof(rbuf) - 1, &bytesRead, NULL);
        CloseHandle(f);
        ok = (strcmp(rbuf, data) == 0);
        DeleteFileA(path);
    }
    add_result("FileIO", ok, ok ? "write+read+delete OK" : "failed");
}

volatile LONG counter = 0;
DWORD WINAPI inc_thread(LPVOID p) {
    for (int i = 0; i < 10000; i++)
        InterlockedIncrement(&counter);
    return 0;
}

static void test_threading(void) {
    HANDLE t[4];
    for (int i = 0; i < 4; i++)
        t[i] = CreateThread(NULL, 0, inc_thread, NULL, 0, NULL);
    WaitForMultipleObjects(4, t, TRUE, 5000);
    for (int i = 0; i < 4; i++) CloseHandle(t[i]);
    add_result("Threading", counter == 40000, "counter=%ld/40000", counter);
}

int main(void) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD w;
    char line[256];
    int len;

    len = sprintf(line, "=== Rosetta Killer Comprehensive Test ===\r\n");
    WriteFile(out, line, len, &w, NULL);

    test_heap_operations();
    test_string_ops();
    test_math();
    test_environment();
    test_file_io();
    test_threading();

    int pass = 0, fail = 0;
    for (int i = 0; i < num_results; i++) {
        len = sprintf(line, "  [%s] %s: %s\r\n",
                      results[i].pass ? "PASS" : "FAIL",
                      results[i].name, results[i].detail);
        WriteFile(out, line, len, &w, NULL);
        if (results[i].pass) pass++; else fail++;
    }

    len = sprintf(line, "\r\n%d/%d tests passed%s\r\n",
                  pass, pass + fail,
                  fail == 0 ? " - ALL PASS" : "");
    WriteFile(out, line, len, &w, NULL);
    return fail;
}
