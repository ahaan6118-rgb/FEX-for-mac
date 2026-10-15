#include <stdio.h>

__declspec(dllexport) int add_numbers(int a, int b) {
    return a + b;
}

__declspec(dllexport) void say_hello(void) {
    puts("Hello from a REAL DLL!");
}
