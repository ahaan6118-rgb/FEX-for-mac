#include <stdio.h>

__declspec(dllimport) int add_numbers(int a, int b);
__declspec(dllimport) void say_hello(void);

int main(void) {
    say_hello();
    int result = add_numbers(40, 2);
    if (result == 42)
        puts("add_numbers(40, 2) = 42 CORRECT");
    else
        puts("add_numbers WRONG");
    return 0;
}
