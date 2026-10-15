// Hand-crafted minimal ELF64 x86_64 test binaries (no assembler needed)
#pragma once

#include <cstdint>

namespace TestELFs {

// Minimal ELF64 binary: write(1, "Hello ELF!\n", 11) then exit(42)
// Single PT_LOAD segment at vaddr 0x400000
// Returns 42 via exit syscall so the test harness can verify
//
// Equivalent x86_64 assembly:
//   _start:
//     mov eax, 1          ; SYS_write
//     mov edi, 1          ; stdout
//     lea rsi, [rip+msg]  ; pointer to message
//     mov edx, 11         ; length
//     syscall
//     mov eax, 60         ; SYS_exit
//     mov edi, 42         ; exit code
//     syscall
//   msg:
//     db "Hello ELF!", 10

static const uint8_t kHelloELF[] = {
  // === ELF Header (64 bytes) ===
  0x7f, 'E', 'L', 'F',       // e_ident[0..3]: magic
  0x02,                        // e_ident[4]: ELFCLASS64
  0x01,                        // e_ident[5]: ELFDATA2LSB
  0x01,                        // e_ident[6]: EV_CURRENT
  0x00,                        // e_ident[7]: ELFOSABI_NONE
  0x00, 0x00, 0x00, 0x00,     // e_ident[8..11]: padding
  0x00, 0x00, 0x00, 0x00,     // e_ident[12..15]: padding
  0x02, 0x00,                  // e_type: ET_EXEC
  0x3e, 0x00,                  // e_machine: EM_X86_64
  0x01, 0x00, 0x00, 0x00,     // e_version: EV_CURRENT
  0x78, 0x00, 0x40, 0x00,     // e_entry: 0x400078 (offset 0x78 = after headers)
  0x00, 0x00, 0x00, 0x00,
  0x40, 0x00, 0x00, 0x00,     // e_phoff: 0x40 (right after ELF header)
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,     // e_shoff: 0 (no section headers)
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,     // e_flags: 0
  0x40, 0x00,                  // e_ehsize: 64
  0x38, 0x00,                  // e_phentsize: 56
  0x01, 0x00,                  // e_phnum: 1
  0x00, 0x00,                  // e_shentsize: 0
  0x00, 0x00,                  // e_shnum: 0
  0x00, 0x00,                  // e_shstrndx: 0

  // === Program Header (56 bytes, at offset 0x40) ===
  0x01, 0x00, 0x00, 0x00,     // p_type: PT_LOAD
  0x05, 0x00, 0x00, 0x00,     // p_flags: PF_R | PF_X
  0x00, 0x00, 0x00, 0x00,     // p_offset: 0
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x40, 0x00,     // p_vaddr: 0x400000
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x40, 0x00,     // p_paddr: 0x400000
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,     // p_filesz: filled below
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,     // p_memsz: filled below
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x10, 0x00, 0x00,     // p_align: 0x1000
  0x00, 0x00, 0x00, 0x00,

  // === Code (at offset 0x78, vaddr 0x400078) ===
  // mov eax, 1 (SYS_write)
  0xB8, 0x01, 0x00, 0x00, 0x00,
  // mov edi, 1 (stdout)
  0xBF, 0x01, 0x00, 0x00, 0x00,
  // lea rsi, [rip + 0x13]  (points to msg, 19 bytes ahead of next insn)
  0x48, 0x8D, 0x35, 0x13, 0x00, 0x00, 0x00,
  // mov edx, 11 (length)
  0xBA, 0x0B, 0x00, 0x00, 0x00,
  // syscall
  0x0F, 0x05,
  // mov eax, 60 (SYS_exit)
  0xB8, 0x3C, 0x00, 0x00, 0x00,
  // mov edi, 42 (exit code)
  0xBF, 0x2A, 0x00, 0x00, 0x00,
  // syscall
  0x0F, 0x05,

  // === Data: "Hello ELF!\n" (at offset 0x9C, vaddr 0x40009C) ===
  'H', 'e', 'l', 'l', 'o', ' ', 'E', 'L', 'F', '!', '\n',
};

// Total size of the ELF binary
static constexpr size_t kHelloELF_Size = sizeof(kHelloELF);

// Patch the p_filesz and p_memsz fields in a copy of the ELF
// (We need the final size which depends on the code+data above)
inline void PatchSizes(uint8_t* elf, size_t total) {
  uint32_t sz = static_cast<uint32_t>(total);
  // p_filesz at offset 0x60, p_memsz at offset 0x68
  memcpy(elf + 0x60, &sz, 4);
  memcpy(elf + 0x68, &sz, 4);
}

} // namespace TestELFs
