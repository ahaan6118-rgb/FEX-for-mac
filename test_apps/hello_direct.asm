; Minimal x86_64 PE that writes "Hello World" via direct NT syscalls
; Bypasses CRT and loader — entry point calls NtWriteFile then NtTerminateProcess
;
; NtWriteFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine,
;             PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock,
;             PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset,
;             PULONG Key)
; Syscall 0x08
;
; NtTerminateProcess(HANDLE ProcessHandle, NTSTATUS ExitStatus)
; Syscall 0x2c

bits 64
default rel

section .text

global _start
_start:
    ; Get stdout handle: on Windows, stdout = GetStdHandle(-11)
    ; But we're bypassing CRT. The console stdout handle is typically
    ; passed as ProcessParameters->StandardOutput.
    ; For Wine, STDOUT_FILENO handle is 0x3 (index) or we can use
    ; NtCurrentPeb()->ProcessParameters->StandardOutput
    ;
    ; Simpler approach: use handle value 0x7 which Wine uses for stdout
    ; Or better: just use a fixed handle and see what happens

    ; First, set up IO_STATUS_BLOCK on stack
    sub rsp, 0x60           ; Reserve stack space (aligned)

    ; IO_STATUS_BLOCK at [rsp+0x40]
    xor rax, rax
    mov [rsp+0x40], rax     ; Status
    mov [rsp+0x48], rax     ; Information

    ; NtWriteFile arguments:
    ; RCX (R10) = FileHandle = stdout handle
    ; RDX = Event = NULL
    ; R8  = ApcRoutine = NULL
    ; R9  = ApcContext = NULL
    ; Stack[5] = IoStatusBlock
    ; Stack[6] = Buffer
    ; Stack[7] = Length
    ; Stack[8] = ByteOffset = NULL
    ; Stack[9] = Key = NULL

    mov r10, 7              ; FileHandle (Wine stdout = 7, or try -11)
    xor edx, edx            ; Event = NULL
    xor r8d, r8d            ; ApcRoutine = NULL
    xor r9d, r9d            ; ApcContext = NULL

    ; Shadow space + stack args
    lea rax, [rsp+0x40]     ; IoStatusBlock ptr
    mov [rsp+0x28], rax     ; 5th arg (stack[5])
    lea rax, [hello_msg]    ; Buffer
    mov [rsp+0x30], rax     ; 6th arg
    mov qword [rsp+0x38], hello_len  ; 7th arg = Length
    mov qword [rsp+0x40], 0 ; We need IoStatusBlock elsewhere...

    ; Actually, rethink the stack layout:
    ; Stack layout for syscall (after shadow space of 32 bytes):
    ; [rsp+0x28] = 5th arg = IoStatusBlock
    ; [rsp+0x30] = 6th arg = Buffer
    ; [rsp+0x38] = 7th arg = Length
    ; [rsp+0x40] = 8th arg = ByteOffset (NULL)
    ; [rsp+0x48] = 9th arg = Key (NULL)

    ; IO_STATUS_BLOCK at [rsp+0x50]
    mov qword [rsp+0x50], 0
    mov qword [rsp+0x58], 0

    lea rax, [rsp+0x50]
    mov [rsp+0x28], rax     ; IoStatusBlock
    lea rax, [hello_msg]
    mov [rsp+0x30], rax     ; Buffer
    mov dword [rsp+0x38], hello_len ; Length
    mov qword [rsp+0x40], 0 ; ByteOffset = NULL
    mov qword [rsp+0x48], 0 ; Key = NULL

    mov eax, 0x08           ; NtWriteFile syscall number
    syscall

    ; NtTerminateProcess(NULL, 0)
    xor r10d, r10d          ; ProcessHandle = NULL (current process)
    xor edx, edx            ; ExitStatus = 0
    mov eax, 0x2c           ; NtTerminateProcess syscall number
    syscall

    ; Should not reach here
    hlt

section .rdata
hello_msg: db "Hello from Rosetta Killer!", 10
hello_len equ $ - hello_msg
