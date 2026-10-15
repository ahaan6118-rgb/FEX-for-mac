#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int fibonacci(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int t = a + b;
        a = b;
        b = t;
    }
    return b;
}

void test_strings(void) {
    char buf[256];
    strcpy(buf, "Hello");
    strcat(buf, " ");
    strcat(buf, "World");
    printf("String concat: [%s] len=%d\n", buf, (int)strlen(buf));

    char *dup = _strdup(buf);
    printf("Strdup: [%s]\n", dup);
    free(dup);

    int cmp = strcmp("abc", "abd");
    printf("strcmp(abc,abd) = %d (expect <0)\n", cmp);

    snprintf(buf, sizeof(buf), "formatted: %d %.1f %s", 42, 3.14, "ok");
    printf("snprintf: [%s]\n", buf);
}

void test_memory(void) {
    int *arr = (int*)malloc(10 * sizeof(int));
    for (int i = 0; i < 10; i++) arr[i] = i * i;
    printf("malloc squares: ");
    for (int i = 0; i < 10; i++) printf("%d ", arr[i]);
    printf("\n");

    arr = (int*)realloc(arr, 20 * sizeof(int));
    for (int i = 10; i < 20; i++) arr[i] = i * i;
    printf("realloc squares[15] = %d\n", arr[15]);

    int *zeros = (int*)calloc(5, sizeof(int));
    int all_zero = 1;
    for (int i = 0; i < 5; i++) if (zeros[i] != 0) all_zero = 0;
    printf("calloc zeroed: %s\n", all_zero ? "yes" : "no");

    free(arr);
    free(zeros);

    void *big = malloc(1024 * 1024);
    printf("1MB alloc: %s\n", big ? "ok" : "FAIL");
    if (big) {
        memset(big, 0xAB, 1024);
        unsigned char *p = (unsigned char*)big;
        printf("memset check: 0x%02x\n", p[500]);
        free(big);
    }
}

void test_math(void) {
    printf("abs(-42) = %d\n", abs(-42));
    printf("sqrt(144) = %.0f\n", sqrt(144.0));
    printf("pow(2,10) = %.0f\n", pow(2.0, 10.0));
    printf("sin(0) = %.1f\n", sin(0.0));
    printf("cos(0) = %.1f\n", cos(0.0));
    printf("floor(3.7) = %.0f\n", floor(3.7));
    printf("ceil(3.2) = %.0f\n", ceil(3.2));
}

int compare_ints(const void *a, const void *b) {
    return (*(int*)a - *(int*)b);
}

void test_qsort(void) {
    int arr[] = {5, 2, 8, 1, 9, 3, 7, 4, 6, 0};
    qsort(arr, 10, sizeof(int), compare_ints);
    printf("qsort: ");
    for (int i = 0; i < 10; i++) printf("%d ", arr[i]);
    printf("\n");
}

int main(void) {
    printf("=== Complex Test ===\n");

    printf("\n--- Fibonacci ---\n");
    for (int i = 0; i <= 10; i++)
        printf("fib(%d)=%d ", i, fibonacci(i));
    printf("\n");

    printf("\n--- Strings ---\n");
    test_strings();

    printf("\n--- Memory ---\n");
    test_memory();

    printf("\n--- Math ---\n");
    test_math();

    printf("\n--- QSort ---\n");
    test_qsort();

    printf("\n=== All tests passed ===\n");
    return 0;
}
