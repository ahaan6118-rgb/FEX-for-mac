#include <windows.h>

int main(void) {
    char buf[128];
    int n;
    DWORD t1, t2;

    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "Before Sleep(3000)\n", 19, NULL, NULL);
    t1 = GetTickCount();
    Sleep(3000);
    t2 = GetTickCount();
    n = wsprintfA(buf, "After Sleep(3000) — elapsed ticks: %lu\n", t2 - t1);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);

    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "Before Sleep(1000)\n", 19, NULL, NULL);
    Sleep(1000);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "After Sleep(1000)\n", 18, NULL, NULL);

    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "PASS\n", 5, NULL, NULL);
    return 0;
}
