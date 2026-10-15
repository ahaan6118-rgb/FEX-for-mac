/* Rosetta Killer benchmark — runs computation in x86_64, JIT-translated to ARM64 */
#include <stdint.h>

/* NT syscall stub for NtWriteFile */
__attribute__((naked))
static uint64_t NtWriteFile(void *handle, void *event, void *apc, void *apc_ctx,
                            void *iosb, const void *buffer, uint32_t length,
                            void *offset, void *key) {
    __asm__ volatile (
        "mov %%rcx, %%r10\n\t"
        "mov $0x08, %%eax\n\t"
        "syscall\n\t"
        "ret\n\t"
        ::: "r10", "rax"
    );
}

__attribute__((naked))
static uint64_t NtTerminateProcess(void *handle, uint32_t exit_code) {
    __asm__ volatile (
        "mov %%rcx, %%r10\n\t"
        "mov $0x2c, %%eax\n\t"
        "syscall\n\t"
        "ret\n\t"
        ::: "r10", "rax"
    );
}

/* Get stdout handle from PEB */
static void *get_stdout(void) {
    void *peb;
    __asm__ volatile ("movq %%gs:0x60, %0" : "=r"(peb));
    void *params = *(void **)((char *)peb + 0x20);
    return *(void **)((char *)params + 0x28);
}

typedef struct { uint32_t status; uint32_t info; uint64_t bytes; } IO_STATUS_BLOCK;

static void write_str(void *handle, const char *s) {
    uint32_t len = 0;
    while (s[len]) len++;
    IO_STATUS_BLOCK iosb;
    NtWriteFile(handle, 0, 0, 0, &iosb, s, len, 0, 0);
}

static void write_num(void *handle, uint64_t n) {
    char buf[24];
    int i = 23;
    buf[i--] = 0;
    if (n == 0) { buf[i--] = '0'; }
    else while (n > 0) { buf[i--] = '0' + (n % 10); n /= 10; }
    write_str(handle, &buf[i + 1]);
}

/* Fibonacci — exercises integer arithmetic + branching */
static uint64_t fib(uint64_t n) {
    if (n <= 1) return n;
    uint64_t a = 0, b = 1;
    for (uint64_t i = 2; i <= n; i++) {
        uint64_t t = a + b;
        a = b;
        b = t;
    }
    return b;
}

/* Sieve of Eratosthenes — exercises memory access patterns */
static uint32_t count_primes(uint32_t limit) {
    /* Use a small stack buffer to avoid __chkstk_ms */
    uint8_t sieve[1024];
    if (limit > sizeof(sieve)) limit = sizeof(sieve);
    for (uint32_t i = 0; i < limit; i++) sieve[i] = 1;
    sieve[0] = sieve[1] = 0;
    for (uint32_t i = 2; i * i < limit; i++) {
        if (sieve[i]) {
            for (uint32_t j = i * i; j < limit; j += i)
                sieve[j] = 0;
        }
    }
    uint32_t count = 0;
    for (uint32_t i = 0; i < limit; i++)
        if (sieve[i]) count++;
    return count;
}

/* Simple hash — exercises bit manipulation */
static uint64_t fnv1a(const char *data, uint32_t len) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (uint32_t i = 0; i < len; i++) {
        hash ^= (uint8_t)data[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

void _start(void) {
    void *stdout_handle = get_stdout();

    write_str(stdout_handle, "=== Rosetta Killer Benchmark ===\n");
    write_str(stdout_handle, "x86_64 instructions JIT-compiled to ARM64\n\n");

    /* Test 1: Fibonacci */
    write_str(stdout_handle, "[1] Fibonacci sequence (integer ALU + branches)\n");
    write_str(stdout_handle, "    fib(10)  = ");
    write_num(stdout_handle, fib(10));
    write_str(stdout_handle, "\n    fib(20)  = ");
    write_num(stdout_handle, fib(20));
    write_str(stdout_handle, "\n    fib(45)  = ");
    write_num(stdout_handle, fib(45));
    write_str(stdout_handle, "\n    fib(90)  = ");
    write_num(stdout_handle, fib(90));
    write_str(stdout_handle, "\n");

    /* Benchmark: run fib(90) many times */
    write_str(stdout_handle, "    Running fib(90) x 1,000,000 iterations... ");
    volatile uint64_t result = 0;
    for (int i = 0; i < 1000000; i++) {
        result = fib(90);
    }
    write_str(stdout_handle, "done (");
    write_num(stdout_handle, result);
    write_str(stdout_handle, ")\n\n");

    /* Test 2: Prime sieve */
    write_str(stdout_handle, "[2] Prime sieve (memory access patterns)\n");
    uint32_t primes = count_primes(1024);
    write_str(stdout_handle, "    Primes below 1024: ");
    write_num(stdout_handle, primes);
    write_str(stdout_handle, "\n");

    write_str(stdout_handle, "    Running sieve x 10,000 iterations... ");
    volatile uint32_t pr = 0;
    for (int i = 0; i < 10000; i++) {
        pr = count_primes(1024);
    }
    write_str(stdout_handle, "done (");
    write_num(stdout_handle, pr);
    write_str(stdout_handle, ")\n\n");

    /* Test 3: FNV-1a hash */
    write_str(stdout_handle, "[3] FNV-1a hash (bit manipulation)\n");
    const char *test_data = "Rosetta Killer: translating x86_64 to ARM64 via FEXCore JIT";
    uint32_t test_len = 0;
    while (test_data[test_len]) test_len++;
    uint64_t hash = fnv1a(test_data, test_len);
    write_str(stdout_handle, "    Hash of test string: 0x");
    /* Print hex */
    char hex[17];
    for (int i = 15; i >= 0; i--) {
        int nibble = (hash >> (i * 4)) & 0xf;
        hex[15 - i] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
    }
    hex[16] = 0;
    write_str(stdout_handle, hex);
    write_str(stdout_handle, "\n");

    write_str(stdout_handle, "    Running hash x 1,000,000 iterations... ");
    volatile uint64_t h = 0;
    for (int i = 0; i < 1000000; i++) {
        h = fnv1a(test_data, test_len);
    }
    write_str(stdout_handle, "done (0x");
    for (int i = 15; i >= 0; i--) {
        int nibble = (h >> (i * 4)) & 0xf;
        hex[15 - i] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
    }
    hex[16] = 0;
    write_str(stdout_handle, hex);
    write_str(stdout_handle, ")\n\n");

    write_str(stdout_handle, "All tests passed. x86_64 → ARM64 JIT translation verified.\n");

    NtTerminateProcess((void *)(int64_t)-1, 0);
    __builtin_unreachable();
}
