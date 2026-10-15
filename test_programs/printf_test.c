#include <stdio.h>

int main(void) {
    printf("=== Printf Test Suite ===\n");
    printf("Integer: %d\n", 42);
    printf("Negative: %d\n", -7);
    printf("Two ints: %d + %d = %d\n", 10, 20, 30);
    printf("String: [%s]\n", "hello world");
    printf("Hex: 0x%x\n", 255);
    printf("Char: %c\n", 'A');
    printf("Float: %.2f\n", 3.14);
    printf("Percent: 100%%\n");
    printf("Mixed: name=%s age=%d score=%.1f\n", "test", 25, 98.6);
    printf("=== All printf tests done ===\n");
    return 0;
}
