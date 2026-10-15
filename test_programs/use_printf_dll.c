#include <stdio.h>

__declspec(dllimport) void test_dll_printf(void);

int main(void) {
    printf("EXE printf: %d items at $%.2f each\n", 5, 9.99);
    test_dll_printf();
    printf("EXE done!\n");
    return 0;
}
