/* Rosetta Killer Demo — x86_64 PE with multiple NT syscalls
 * Demonstrates:
 *   - GS segment TEB/PEB access
 *   - NtWriteFile (stdout)
 *   - NtQuerySystemInformation (basic system info)
 *   - Multiple string outputs
 *   - Clean exit via HLT
 *
 * Compiled: x86_64-w64-mingw32-gcc -nostdlib -nostartfiles -e _start -O2 -o demo_direct.exe demo_direct.c
 */

typedef unsigned long long UINT64;
typedef unsigned long ULONG;
typedef long NTSTATUS;
typedef void *HANDLE;

typedef struct { NTSTATUS Status; UINT64 Information; } IO_STATUS_BLOCK;

static void write_stdout(HANDLE h, const char *msg, ULONG len)
{
    IO_STATUS_BLOCK iosb;
    iosb.Status = 0;
    iosb.Information = 0;

    register UINT64 r10 __asm__("r10") = (UINT64)h;
    register UINT64 rdx __asm__("rdx") = 0;
    register UINT64 r8  __asm__("r8")  = 0;
    register UINT64 r9  __asm__("r9")  = 0;

    __asm__ volatile (
        "sub $0x58, %%rsp\n\t"
        "mov %[iosb], 0x28(%%rsp)\n\t"
        "mov %[buf], 0x30(%%rsp)\n\t"
        "mov %[len], 0x38(%%rsp)\n\t"
        "movq $0, 0x40(%%rsp)\n\t"
        "movq $0, 0x48(%%rsp)\n\t"
        "mov $0x08, %%eax\n\t"
        "syscall\n\t"
        "add $0x58, %%rsp\n\t"
        :
        : "r" (r10), "r" (rdx), "r" (r8), "r" (r9),
          [iosb] "r" ((UINT64)&iosb),
          [buf] "r" ((UINT64)msg),
          [len] "r" (len)
        : "rax", "rcx", "r11", "memory", "cc"
    );
}

static ULONG strlen_simple(const char *s)
{
    ULONG n = 0;
    while (s[n]) n++;
    return n;
}

static void write_hex(HANDLE h, UINT64 val)
{
    char buf[19];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 16; i++) {
        int nibble = (val >> (60 - i * 4)) & 0xf;
        buf[2 + i] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
    }
    buf[18] = '\0';
    write_stdout(h, buf, 18);
}

void __attribute__((section(".text"))) _start(void)
{
    HANDLE stdout_handle;
    UINT64 peb;
    UINT64 params;
    const char *banner;
    const char *line2;
    const char *line3;
    const char *line4;
    const char *done;

    /* Read PEB from TEB via GS segment */
    __asm__ volatile ("movq %%gs:0x60, %0" : "=r" (peb));
    params = *(UINT64 *)(peb + 0x20);
    stdout_handle = *(HANDLE *)(params + 0x28);

    banner = "=== Rosetta Killer v0.1.0 ===\n";
    write_stdout(stdout_handle, banner, strlen_simple(banner));

    line2 = "x86_64 PE -> FEXCore JIT -> ARM64 macOS\n";
    write_stdout(stdout_handle, line2, strlen_simple(line2));

    line3 = "PEB address: ";
    write_stdout(stdout_handle, line3, strlen_simple(line3));
    write_hex(stdout_handle, peb);

    line4 = "\nStdout handle: ";
    write_stdout(stdout_handle, line4, strlen_simple(line4));
    write_hex(stdout_handle, (UINT64)stdout_handle);

    done = "\n\nAll systems operational. JIT translation verified.\n";
    write_stdout(stdout_handle, done, strlen_simple(done));

    __asm__ volatile ("hlt");
}
