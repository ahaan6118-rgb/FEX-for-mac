#include <windows.h>

int main(void) {
    char buf[256];
    int n;
    DWORD t1, t2, t3;

    t1 = GetTickCount();
    n = wsprintfA(buf, "GetTickCount #1: %lu\n", t1);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

    Sleep(1000);

    t2 = GetTickCount();
    n = wsprintfA(buf, "GetTickCount #2: %lu (delta=%lu)\n", t2, t2 - t1);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

    Sleep(500);

    t3 = GetTickCount();
    n = wsprintfA(buf, "GetTickCount #3: %lu (delta=%lu)\n", t3, t3 - t2);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

    n = wsprintfA(buf, "Total elapsed: %lu ms\n", t3 - t1);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

    /* Also test LARGE_INTEGER counter */
    {
        LARGE_INTEGER freq, c1, c2;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&c1);
        Sleep(200);
        QueryPerformanceCounter(&c2);
        n = wsprintfA(buf, "QPC freq=%lu, elapsed=%lu\n",
                      (DWORD)freq.LowPart, (DWORD)(c2.QuadPart - c1.QuadPart));
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);
    }

    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "PASS\n", 5, NULL, NULL);
    return 0;
}
