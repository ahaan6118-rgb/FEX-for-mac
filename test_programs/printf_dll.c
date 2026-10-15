#include <stdio.h>

__declspec(dllexport) void test_dll_printf(void) {
    printf("DLL printf: %d + %d = %d\n", 10, 20, 30);
    printf("DLL string: [%s]\n", "from dll");
    printf("DLL float: %.3f\n", 2.718);
    printf("DLL hex: 0x%X\n", 0xDEAD);
}
