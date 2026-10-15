/* Minimal PE that writes "Hello World" via direct NT syscalls.
 * Reads stdout handle from PEB->ProcessParameters. */

typedef unsigned long long UINT64;
typedef unsigned long ULONG;
typedef long NTSTATUS;
typedef void *HANDLE;

typedef struct { NTSTATUS Status; UINT64 Information; } IO_STATUS_BLOCK;

static const char hello[] = "Hello from Rosetta Killer!\n";

void __attribute__((section(".text"))) _start(void)
{
    IO_STATUS_BLOCK iosb;
    HANDLE stdout_handle;
    NTSTATUS result;
    UINT64 peb;
    UINT64 params;

    iosb.Status = 0;
    iosb.Information = 0;

    /* Read PEB from TEB via GS segment: TEB->ProcessEnvironmentBlock at offset 0x60 */
    __asm__ volatile ("movq %%gs:0x60, %0" : "=r" (peb));

    /* PEB->ProcessParameters at PEB+0x20 */
    params = *(UINT64 *)(peb + 0x20);

    /* RTL_USER_PROCESS_PARAMETERS->StandardOutput at offset 0x28 */
    stdout_handle = *(HANDLE *)(params + 0x28);

    /* NtWriteFile syscall */
    {
        register UINT64 r10 __asm__("r10") = (UINT64)stdout_handle;
        register UINT64 rdx __asm__("rdx") = 0;
        register UINT64 r8  __asm__("r8")  = 0;
        register UINT64 r9  __asm__("r9")  = 0;

        __asm__ volatile (
            "sub $0x58, %%rsp\n\t"
            "mov %[iosb], 0x28(%%rsp)\n\t"
            "mov %[buf], 0x30(%%rsp)\n\t"
            "movl %[len], 0x38(%%rsp)\n\t"
            "movq $0, 0x40(%%rsp)\n\t"
            "movq $0, 0x48(%%rsp)\n\t"
            "mov $0x08, %%eax\n\t"
            "syscall\n\t"
            "add $0x58, %%rsp\n\t"
            : "=a" (result)
            : "r" (r10), "r" (rdx), "r" (r8), "r" (r9),
              [iosb] "r" ((UINT64)&iosb),
              [buf] "r" ((UINT64)hello),
              [len] "r" ((ULONG)(sizeof(hello) - 1))
            : "rcx", "r11", "memory", "cc"
        );
    }

    /* Exit by HLT — FEXCore has EnableExitOnHLT */
    __asm__ volatile ("hlt");
}
