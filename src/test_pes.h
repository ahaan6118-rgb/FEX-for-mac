// Hand-crafted minimal PE64 x86_64 test binaries
#pragma once

#include <cstdint>
#include <cstring>

namespace TestPEs {

static constexpr uint32_t kFileAlignment = 0x200;
static constexpr uint32_t kSectionAlignment = 0x1000;
static constexpr uint64_t kImageBase = 0x140000000ULL;
static constexpr uint32_t kEntryRVA = 0x1000;

struct TestPEBuffer {
  uint8_t data[0x200 + 0x200];
  size_t  size;
};

// Test 1: Pure computation (no imports). 7 * 13 = 91, then HLT.
inline TestPEBuffer BuildMinimalPE() {
  TestPEBuffer buf = {};
  buf.size = sizeof(buf.data);
  uint8_t* d = buf.data;

  d[0] = 'M'; d[1] = 'Z';
  uint32_t pe_offset = 0x40;
  memcpy(d + 0x3C, &pe_offset, 4);
  d[0x40] = 'P'; d[0x41] = 'E'; d[0x42] = 0; d[0x43] = 0;

  uint16_t machine = 0x8664;
  memcpy(d + 0x44, &machine, 2);
  uint16_t num_sections = 1;
  memcpy(d + 0x46, &num_sections, 2);
  uint16_t opt_hdr_size = 112;
  memcpy(d + 0x54, &opt_hdr_size, 2);
  uint16_t characteristics = 0x0022;
  memcpy(d + 0x56, &characteristics, 2);

  size_t opt = 0x58;
  uint16_t magic = 0x20b;
  memcpy(d + opt, &magic, 2);
  uint32_t entry_rva = kEntryRVA;
  memcpy(d + opt + 16, &entry_rva, 4);
  uint64_t image_base = kImageBase;
  memcpy(d + opt + 24, &image_base, 8);
  uint32_t section_align = kSectionAlignment;
  memcpy(d + opt + 32, &section_align, 4);
  uint32_t file_align = kFileAlignment;
  memcpy(d + opt + 36, &file_align, 4);
  uint32_t size_of_image = 0x2000;
  memcpy(d + opt + 56, &size_of_image, 4);
  uint32_t size_of_headers = kFileAlignment;
  memcpy(d + opt + 60, &size_of_headers, 4);
  uint32_t num_rva = 0;
  memcpy(d + opt + 108, &num_rva, 4);

  size_t sec = 0xC8;
  memcpy(d + sec, ".text\0\0\0", 8);
  uint32_t virtual_size = 0x20;
  memcpy(d + sec + 8, &virtual_size, 4);
  uint32_t virtual_addr = kSectionAlignment;
  memcpy(d + sec + 12, &virtual_addr, 4);
  uint32_t raw_size = kFileAlignment;
  memcpy(d + sec + 16, &raw_size, 4);
  uint32_t raw_ptr = kFileAlignment;
  memcpy(d + sec + 20, &raw_ptr, 4);
  uint32_t sec_flags = 0x60000020;
  memcpy(d + sec + 36, &sec_flags, 4);

  size_t code = 0x200;
  d[code++] = 0xB8; d[code++] = 0x07; d[code++] = 0x00; d[code++] = 0x00; d[code++] = 0x00;
  d[code++] = 0xB9; d[code++] = 0x0D; d[code++] = 0x00; d[code++] = 0x00; d[code++] = 0x00;
  d[code++] = 0x0F; d[code++] = 0xAF; d[code++] = 0xC1;
  d[code++] = 0xF4;

  return buf;
}

// Test 2: PE with imports — calls GetStdHandle + WriteFile + ExitProcess
// This is a larger PE with import directory, IAT, and import name table.
//
// Layout:
//   0x000-0x1FF: Headers (DOS + PE + COFF + OptHdr + 2 section headers)
//   0x200-0x3FF: .text  (code at RVA 0x1000)
//   0x400-0x5FF: .idata (import tables at RVA 0x2000)
//
// The code uses Windows x64 ABI: args in RCX, RDX, R8, R9
struct ImportPEBuffer {
  uint8_t data[0x600];
  size_t  size;
};

inline ImportPEBuffer BuildImportPE() {
  ImportPEBuffer buf = {};
  buf.size = sizeof(buf.data);
  uint8_t* d = buf.data;

  // --- DOS Header ---
  d[0] = 'M'; d[1] = 'Z';
  uint32_t pe_offset = 0x40;
  memcpy(d + 0x3C, &pe_offset, 4);

  // --- PE Signature ---
  d[0x40] = 'P'; d[0x41] = 'E';

  // --- COFF Header ---
  uint16_t machine = 0x8664;
  memcpy(d + 0x44, &machine, 2);
  uint16_t num_sections = 2;       // .text + .idata
  memcpy(d + 0x46, &num_sections, 2);
  // OptionalHeader size = 112 + 16*2 = 144 (with 2 data directories: export + import)
  uint16_t opt_hdr_size = 112 + 16 * 2;
  memcpy(d + 0x54, &opt_hdr_size, 2);
  uint16_t characteristics = 0x0022;
  memcpy(d + 0x56, &characteristics, 2);

  // --- Optional Header ---
  size_t opt = 0x58;
  uint16_t magic = 0x20b;
  memcpy(d + opt, &magic, 2);
  uint32_t entry_rva = 0x1000;
  memcpy(d + opt + 16, &entry_rva, 4);
  uint64_t image_base = 0x140000000ULL;
  memcpy(d + opt + 24, &image_base, 8);
  uint32_t section_align = 0x1000;
  memcpy(d + opt + 32, &section_align, 4);
  uint32_t file_align = 0x200;
  memcpy(d + opt + 36, &file_align, 4);
  uint32_t size_of_image = 0x3000; // headers + .text + .idata
  memcpy(d + opt + 56, &size_of_image, 4);
  uint32_t size_of_headers = 0x200;
  memcpy(d + opt + 60, &size_of_headers, 4);

  // NumberOfRvaAndSizes = 2 (export + import)
  uint32_t num_rva = 2;
  memcpy(d + opt + 108, &num_rva, 4);

  // Data directories start at opt + 112
  size_t dd = opt + 112;
  // [0] Export — empty
  // [1] Import — RVA 0x2000
  uint32_t import_rva = 0x2000;
  uint32_t import_size = 0x100;
  memcpy(d + dd + 8, &import_rva, 4);
  memcpy(d + dd + 12, &import_size, 4);

  // --- Section Headers (after optional header) ---
  // OptHdr ends at 0x58 + 144 = 0xE8
  size_t sec0 = 0xE8;

  // .text
  memcpy(d + sec0, ".text\0\0\0", 8);
  uint32_t vs = 0x200; memcpy(d + sec0 + 8, &vs, 4);
  uint32_t va = 0x1000; memcpy(d + sec0 + 12, &va, 4);
  uint32_t rs = 0x200; memcpy(d + sec0 + 16, &rs, 4);
  uint32_t rp = 0x200; memcpy(d + sec0 + 20, &rp, 4);
  uint32_t sf = 0x60000020; memcpy(d + sec0 + 36, &sf, 4);

  // .idata
  size_t sec1 = sec0 + 40;
  memcpy(d + sec1, ".idata\0\0", 8);
  vs = 0x200; memcpy(d + sec1 + 8, &vs, 4);
  va = 0x2000; memcpy(d + sec1 + 12, &va, 4);
  rs = 0x200; memcpy(d + sec1 + 16, &rs, 4);
  rp = 0x400; memcpy(d + sec1 + 20, &rp, 4);
  sf = 0xC0000040; memcpy(d + sec1 + 36, &sf, 4); // INITIALIZED_DATA | MEM_READ | MEM_WRITE

  // === .idata section (file offset 0x400, RVA 0x2000) ===
  // Layout within .idata:
  //   0x00: Import Descriptor for kernel32.dll (20 bytes)
  //   0x14: Null Import Descriptor (20 bytes)
  //   0x28: INT entries (3 * 8 = 24 bytes) — GetStdHandle, WriteFile, ExitProcess
  //   0x40: IAT entries (3 * 8 = 24 bytes) — patched by loader
  //   0x58: DLL name "kernel32.dll\0"
  //   0x68: ImportByName for GetStdHandle
  //   0x80: ImportByName for WriteFile
  //   0x94: ImportByName for ExitProcess
  //   0xA8: Message "Hello PE!\n"

  size_t idata = 0x400;

  // Import Descriptor for kernel32.dll
  uint32_t int_rva = 0x2028;  // OriginalFirstThunk -> INT
  memcpy(d + idata + 0, &int_rva, 4);
  uint32_t name_rva = 0x2058; // Name -> "kernel32.dll"
  memcpy(d + idata + 12, &name_rva, 4);
  uint32_t iat_rva = 0x2040;  // FirstThunk -> IAT
  memcpy(d + idata + 16, &iat_rva, 4);

  // Null terminator descriptor (all zeros) at offset 0x14

  // INT entries (RVAs to ImportByName structs)
  uint64_t int0 = 0x2068; memcpy(d + idata + 0x28, &int0, 8); // GetStdHandle
  uint64_t int1 = 0x2080; memcpy(d + idata + 0x30, &int1, 8); // WriteFile
  uint64_t int2 = 0x2094; memcpy(d + idata + 0x38, &int2, 8); // ExitProcess
  // null terminator at 0x40 (overlaps with IAT start — we need to separate them)

  // Actually, INT and IAT can't overlap. Let me fix the layout:
  // INT at 0x28: 4 entries * 8 = 32 bytes -> ends at 0x48
  // IAT at 0x48: 4 entries * 8 = 32 bytes -> ends at 0x68
  // But I already wrote INT entries at 0x28,0x30,0x38 with null at 0x40

  // Recalculate: IAT starts at 0x48
  uint32_t iat_rva_fix = 0x2048;
  memcpy(d + idata + 16, &iat_rva_fix, 4); // fix IAT RVA in descriptor

  // IAT entries (initially same as INT — loader patches these with thunk addresses)
  memcpy(d + idata + 0x48, &int0, 8); // GetStdHandle
  memcpy(d + idata + 0x50, &int1, 8); // WriteFile
  memcpy(d + idata + 0x58, &int2, 8); // ExitProcess
  // null at 0x60

  // DLL name — move to 0x68
  uint32_t name_rva_fix = 0x2068;
  memcpy(d + idata + 12, &name_rva_fix, 4);
  memcpy(d + idata + 0x68, "kernel32.dll", 13);

  // ImportByName structs — move to 0x78, 0x90, 0xA4
  uint64_t ibn0_rva = 0x2078; memcpy(d + idata + 0x28, &ibn0_rva, 8);
  uint64_t ibn1_rva = 0x2090; memcpy(d + idata + 0x30, &ibn1_rva, 8);
  uint64_t ibn2_rva = 0x20A4; memcpy(d + idata + 0x38, &ibn2_rva, 8);
  // null terminator
  uint64_t zero = 0;
  memcpy(d + idata + 0x40, &zero, 8);

  // IAT initial values (same RVAs)
  memcpy(d + idata + 0x48, &ibn0_rva, 8);
  memcpy(d + idata + 0x50, &ibn1_rva, 8);
  memcpy(d + idata + 0x58, &ibn2_rva, 8);
  memcpy(d + idata + 0x60, &zero, 8);

  // ImportByName: hint(2) + name
  // GetStdHandle at 0x78
  uint16_t hint = 0;
  memcpy(d + idata + 0x78, &hint, 2);
  memcpy(d + idata + 0x7A, "GetStdHandle\0", 13);

  // WriteFile at 0x90
  memcpy(d + idata + 0x90, &hint, 2);
  memcpy(d + idata + 0x92, "WriteFile\0", 10);

  // ExitProcess at 0xA4
  memcpy(d + idata + 0xA4, &hint, 2);
  memcpy(d + idata + 0xA6, "ExitProcess\0", 12);

  // Message string at 0xB8 (RVA 0x20B8)
  memcpy(d + idata + 0xB8, "Hello PE!\n", 10);

  // === .text code (file offset 0x200, RVA 0x1000) ===
  // Windows x64 ABI: RCX, RDX, R8, R9
  // IAT entries at RVA 0x2048, 0x2050, 0x2058 (8 bytes each)
  //
  // Code:
  //   sub rsp, 0x38          ; shadow space + alignment
  //   ; stdout = GetStdHandle(STD_OUTPUT_HANDLE = -11)
  //   mov ecx, -11           ; nStdHandle = STD_OUTPUT_HANDLE
  //   call [rip + GetStdHandle_IAT]
  //   ; WriteFile(stdout, "Hello PE!\n", 10, NULL, NULL)
  //   mov rcx, rax           ; hFile = stdout handle
  //   lea rdx, [rip + msg]   ; lpBuffer
  //   mov r8d, 10            ; nBytesToWrite
  //   xor r9d, r9d           ; lpBytesWritten = NULL
  //   push 0                 ; lpOverlapped = NULL (5th arg on stack)
  //   call [rip + WriteFile_IAT]
  //   ; ExitProcess(0)
  //   xor ecx, ecx           ; uExitCode = 0
  //   call [rip + ExitProcess_IAT]
  //   hlt

  size_t c = 0x200;

  // sub rsp, 0x38
  d[c++] = 0x48; d[c++] = 0x83; d[c++] = 0xEC; d[c++] = 0x38;

  // mov ecx, -11 (0xFFFFFFF5)
  d[c++] = 0xB9; d[c++] = 0xF5; d[c++] = 0xFF; d[c++] = 0xFF; d[c++] = 0xFF;

  // call [rip + disp32] — GetStdHandle IAT at RVA 0x2048
  // RIP after this insn = RVA 0x1000 + (c - 0x200) + 6
  // disp = 0x2048 - RIP_after
  size_t call1_off = c;
  d[c++] = 0xFF; d[c++] = 0x15; // call [rip+disp32]
  // We'll patch the displacement after we know the final offset
  c += 4; // placeholder

  // mov rcx, rax
  d[c++] = 0x48; d[c++] = 0x89; d[c++] = 0xC1;

  // lea rdx, [rip + msg_disp]  — msg at RVA 0x20B8
  size_t lea_off = c;
  d[c++] = 0x48; d[c++] = 0x8D; d[c++] = 0x15;
  c += 4; // placeholder

  // mov r8d, 10
  d[c++] = 0x41; d[c++] = 0xB8; d[c++] = 0x0A; d[c++] = 0x00; d[c++] = 0x00; d[c++] = 0x00;

  // xor r9d, r9d
  d[c++] = 0x45; d[c++] = 0x31; d[c++] = 0xC9;

  // push 0 (5th arg)
  d[c++] = 0x6A; d[c++] = 0x00;

  // call [rip + disp32] — WriteFile IAT at RVA 0x2050
  size_t call2_off = c;
  d[c++] = 0xFF; d[c++] = 0x15;
  c += 4;

  // add rsp, 8 (pop the push 0)
  d[c++] = 0x48; d[c++] = 0x83; d[c++] = 0xC4; d[c++] = 0x08;

  // xor ecx, ecx
  d[c++] = 0x31; d[c++] = 0xC9;

  // call [rip + disp32] — ExitProcess IAT at RVA 0x2058
  size_t call3_off = c;
  d[c++] = 0xFF; d[c++] = 0x15;
  c += 4;

  // hlt
  d[c++] = 0xF4;

  // Patch displacements
  // call [rip+disp]: disp = target_rva - (insn_rva + 6)
  auto patch_call = [&](size_t insn_file_off, uint32_t target_rva) {
    uint32_t insn_rva = 0x1000 + static_cast<uint32_t>(insn_file_off - 0x200);
    int32_t disp = static_cast<int32_t>(target_rva) - static_cast<int32_t>(insn_rva + 6);
    memcpy(d + insn_file_off + 2, &disp, 4);
  };

  patch_call(call1_off, 0x2048); // GetStdHandle
  patch_call(call2_off, 0x2050); // WriteFile
  patch_call(call3_off, 0x2058); // ExitProcess

  // Patch lea rdx, [rip + msg] — msg at RVA 0x20B8
  {
    uint32_t lea_rva = 0x1000 + static_cast<uint32_t>(lea_off - 0x200);
    int32_t disp = static_cast<int32_t>(0x20B8) - static_cast<int32_t>(lea_rva + 7);
    memcpy(d + lea_off + 3, &disp, 4);
  }

  return buf;
}

// Test 3: PE with msvcrt.dll import — calls puts("Hello MSVCRT!") then ExitProcess(0)
//
// Layout:
//   0x000-0x1FF: Headers (DOS + PE + COFF + OptHdr + 2 section headers)
//   0x200-0x3FF: .text  (code at RVA 0x1000)
//   0x400-0x7FF: .idata (import tables at RVA 0x2000)
struct MsvcrtPEBuffer {
  uint8_t data[0x800];
  size_t  size;
};

inline MsvcrtPEBuffer BuildMsvcrtPE() {
  MsvcrtPEBuffer buf = {};
  buf.size = sizeof(buf.data);
  uint8_t* d = buf.data;

  // DOS Header
  d[0] = 'M'; d[1] = 'Z';
  uint32_t pe_offset = 0x40;
  memcpy(d + 0x3C, &pe_offset, 4);

  // PE Signature
  d[0x40] = 'P'; d[0x41] = 'E';

  // COFF Header
  uint16_t machine = 0x8664;
  memcpy(d + 0x44, &machine, 2);
  uint16_t num_sections = 2;
  memcpy(d + 0x46, &num_sections, 2);
  uint16_t opt_hdr_size = 112 + 16 * 2;
  memcpy(d + 0x54, &opt_hdr_size, 2);
  uint16_t characteristics = 0x0022;
  memcpy(d + 0x56, &characteristics, 2);

  // Optional Header
  size_t opt = 0x58;
  uint16_t magic = 0x20b;
  memcpy(d + opt, &magic, 2);
  uint32_t entry_rva = 0x1000;
  memcpy(d + opt + 16, &entry_rva, 4);
  uint64_t image_base = 0x140000000ULL;
  memcpy(d + opt + 24, &image_base, 8);
  uint32_t section_align = 0x1000;
  memcpy(d + opt + 32, &section_align, 4);
  uint32_t file_align = 0x200;
  memcpy(d + opt + 36, &file_align, 4);
  uint32_t size_of_image = 0x3000;
  memcpy(d + opt + 56, &size_of_image, 4);
  uint32_t size_of_headers = 0x200;
  memcpy(d + opt + 60, &size_of_headers, 4);

  uint32_t num_rva = 2;
  memcpy(d + opt + 108, &num_rva, 4);

  size_t dd = opt + 112;
  uint32_t import_rva = 0x2000;
  uint32_t import_size = 0x200;
  memcpy(d + dd + 8, &import_rva, 4);
  memcpy(d + dd + 12, &import_size, 4);

  // Section Headers
  size_t sec0 = 0xE8;
  memcpy(d + sec0, ".text\0\0\0", 8);
  uint32_t vs = 0x200; memcpy(d + sec0 + 8, &vs, 4);
  uint32_t va = 0x1000; memcpy(d + sec0 + 12, &va, 4);
  uint32_t rs = 0x200; memcpy(d + sec0 + 16, &rs, 4);
  uint32_t rp = 0x200; memcpy(d + sec0 + 20, &rp, 4);
  uint32_t sf = 0x60000020; memcpy(d + sec0 + 36, &sf, 4);

  size_t sec1 = sec0 + 40;
  memcpy(d + sec1, ".idata\0\0", 8);
  vs = 0x400; memcpy(d + sec1 + 8, &vs, 4);
  va = 0x2000; memcpy(d + sec1 + 12, &va, 4);
  rs = 0x400; memcpy(d + sec1 + 16, &rs, 4);
  rp = 0x400; memcpy(d + sec1 + 20, &rp, 4);
  sf = 0xC0000040; memcpy(d + sec1 + 36, &sf, 4);

  // === .idata section (file offset 0x400, RVA 0x2000) ===
  // Two DLLs: msvcrt.dll (puts) and kernel32.dll (ExitProcess)
  //
  // Layout:
  //   0x00-0x13: Import Descriptor #1 (msvcrt.dll)
  //   0x14-0x27: Import Descriptor #2 (kernel32.dll)
  //   0x28-0x3B: Null Import Descriptor
  //   0x40-0x4F: INT for msvcrt (1 entry + null)
  //   0x50-0x5F: INT for kernel32 (1 entry + null)
  //   0x60-0x6F: IAT for msvcrt (1 entry + null)
  //   0x70-0x7F: IAT for kernel32 (1 entry + null)
  //   0x80-0x8B: "msvcrt.dll\0"
  //   0x90-0x9D: "kernel32.dll\0"
  //   0xA0-0xAF: ImportByName "puts\0"
  //   0xB0-0xBF: ImportByName "ExitProcess\0"
  //   0xD0-0xDF: "Hello MSVCRT!\0"

  size_t id = 0x400;
  uint64_t zero = 0;

  // Descriptor #1: msvcrt.dll
  uint32_t msvcrt_int_rva = 0x2040;
  memcpy(d + id + 0, &msvcrt_int_rva, 4);
  uint32_t msvcrt_name_rva = 0x2080;
  memcpy(d + id + 12, &msvcrt_name_rva, 4);
  uint32_t msvcrt_iat_rva = 0x2060;
  memcpy(d + id + 16, &msvcrt_iat_rva, 4);

  // Descriptor #2: kernel32.dll
  uint32_t k32_int_rva = 0x2050;
  memcpy(d + id + 0x14, &k32_int_rva, 4);
  uint32_t k32_name_rva = 0x2090;
  memcpy(d + id + 0x14 + 12, &k32_name_rva, 4);
  uint32_t k32_iat_rva = 0x2070;
  memcpy(d + id + 0x14 + 16, &k32_iat_rva, 4);

  // Null descriptor at 0x28 (already zeroed)

  // INT for msvcrt: puts
  uint64_t puts_ibn_rva = 0x20A0;
  memcpy(d + id + 0x40, &puts_ibn_rva, 8);
  memcpy(d + id + 0x48, &zero, 8); // null terminator

  // INT for kernel32: ExitProcess
  uint64_t exit_ibn_rva = 0x20B0;
  memcpy(d + id + 0x50, &exit_ibn_rva, 8);
  memcpy(d + id + 0x58, &zero, 8);

  // IAT for msvcrt (same initial values as INT)
  memcpy(d + id + 0x60, &puts_ibn_rva, 8);
  memcpy(d + id + 0x68, &zero, 8);

  // IAT for kernel32
  memcpy(d + id + 0x70, &exit_ibn_rva, 8);
  memcpy(d + id + 0x78, &zero, 8);

  // DLL names
  memcpy(d + id + 0x80, "msvcrt.dll", 11);
  memcpy(d + id + 0x90, "kernel32.dll", 13);

  // ImportByName: puts
  uint16_t hint = 0;
  memcpy(d + id + 0xA0, &hint, 2);
  memcpy(d + id + 0xA2, "puts", 5);

  // ImportByName: ExitProcess
  memcpy(d + id + 0xB0, &hint, 2);
  memcpy(d + id + 0xB2, "ExitProcess", 12);

  // Message string at offset 0xD0 (RVA 0x20D0)
  memcpy(d + id + 0xD0, "Hello MSVCRT!", 14);

  // === .text code (file offset 0x200, RVA 0x1000) ===
  // Code:
  //   sub rsp, 0x28         ; shadow space + alignment
  //   lea rcx, [rip + msg]  ; "Hello MSVCRT!"
  //   call [rip + puts_IAT] ; puts()
  //   xor ecx, ecx          ; exit code 0
  //   call [rip + exit_IAT] ; ExitProcess(0)
  //   hlt

  size_t c = 0x200;

  // sub rsp, 0x28
  d[c++] = 0x48; d[c++] = 0x83; d[c++] = 0xEC; d[c++] = 0x28;

  // lea rcx, [rip + msg]  — msg at RVA 0x20D0
  size_t lea_off = c;
  d[c++] = 0x48; d[c++] = 0x8D; d[c++] = 0x0D;
  c += 4; // displacement placeholder

  // call [rip + puts_IAT]  — IAT at RVA 0x2060
  size_t call1_off = c;
  d[c++] = 0xFF; d[c++] = 0x15;
  c += 4;

  // xor ecx, ecx
  d[c++] = 0x31; d[c++] = 0xC9;

  // call [rip + exit_IAT]  — IAT at RVA 0x2070
  size_t call2_off = c;
  d[c++] = 0xFF; d[c++] = 0x15;
  c += 4;

  // hlt
  d[c++] = 0xF4;

  // Patch displacements
  auto patch_disp = [&](size_t insn_file_off, int prefix_len, uint32_t target_rva) {
    uint32_t insn_rva = 0x1000 + static_cast<uint32_t>(insn_file_off - 0x200);
    uint32_t insn_len = prefix_len + 4;
    int32_t disp = static_cast<int32_t>(target_rva) - static_cast<int32_t>(insn_rva + insn_len);
    memcpy(d + insn_file_off + prefix_len, &disp, 4);
  };

  patch_disp(lea_off, 3, 0x20D0);   // lea rcx, [rip + msg]
  patch_disp(call1_off, 2, 0x2060);  // call [rip + puts_IAT]
  patch_disp(call2_off, 2, 0x2070);  // call [rip + exit_IAT]

  return buf;
}

// Test 4: CRT startup sequence — mimics MinGW mainCRTStartup
// Calls __set_app_type, __getmainargs, _initterm, then user main (puts + exit).
// This validates the full CRT initialization path that real MinGW .exe files use.
//
// Layout:
//   0x000-0x1FF: Headers
//   0x200-0x5FF: .text (code at RVA 0x1000, 1024 bytes)
//   0x600-0xBFF: .idata (import tables at RVA 0x2000)
struct CrtStartupPEBuffer {
  uint8_t data[0xC00];
  size_t  size;
};

inline CrtStartupPEBuffer BuildCrtStartupPE() {
  CrtStartupPEBuffer buf = {};
  buf.size = sizeof(buf.data);
  uint8_t* d = buf.data;

  // DOS Header
  d[0] = 'M'; d[1] = 'Z';
  uint32_t pe_offset = 0x40;
  memcpy(d + 0x3C, &pe_offset, 4);
  d[0x40] = 'P'; d[0x41] = 'E';

  // COFF
  uint16_t machine = 0x8664;
  memcpy(d + 0x44, &machine, 2);
  uint16_t num_sections = 2;
  memcpy(d + 0x46, &num_sections, 2);
  uint16_t opt_hdr_size = 112 + 16 * 2;
  memcpy(d + 0x54, &opt_hdr_size, 2);
  uint16_t characteristics = 0x0022;
  memcpy(d + 0x56, &characteristics, 2);

  // Optional Header
  size_t opt = 0x58;
  uint16_t magic = 0x20b;
  memcpy(d + opt, &magic, 2);
  uint32_t entry_rva = 0x1000; // mainCRTStartup
  memcpy(d + opt + 16, &entry_rva, 4);
  uint64_t image_base = 0x140000000ULL;
  memcpy(d + opt + 24, &image_base, 8);
  uint32_t section_align = 0x1000;
  memcpy(d + opt + 32, &section_align, 4);
  uint32_t file_align = 0x200;
  memcpy(d + opt + 36, &file_align, 4);
  uint32_t size_of_image = 0x3000;
  memcpy(d + opt + 56, &size_of_image, 4);
  uint32_t size_of_headers = 0x200;
  memcpy(d + opt + 60, &size_of_headers, 4);
  uint16_t subsystem = 3; // CONSOLE
  memcpy(d + opt + 68, &subsystem, 2);
  uint32_t num_rva = 2;
  memcpy(d + opt + 108, &num_rva, 4);

  size_t dd = opt + 112;
  uint32_t import_rva = 0x2000;
  uint32_t import_size = 0x300;
  memcpy(d + dd + 8, &import_rva, 4);
  memcpy(d + dd + 12, &import_size, 4);

  // Section headers
  size_t sec0 = 0xE8;
  memcpy(d + sec0, ".text\0\0\0", 8);
  uint32_t vs = 0x400; memcpy(d + sec0 + 8, &vs, 4);
  uint32_t va = 0x1000; memcpy(d + sec0 + 12, &va, 4);
  uint32_t rs = 0x400; memcpy(d + sec0 + 16, &rs, 4);
  uint32_t rp = 0x200; memcpy(d + sec0 + 20, &rp, 4);
  uint32_t sf = 0x60000020; memcpy(d + sec0 + 36, &sf, 4);

  size_t sec1 = sec0 + 40;
  memcpy(d + sec1, ".idata\0\0", 8);
  vs = 0x600; memcpy(d + sec1 + 8, &vs, 4);
  va = 0x2000; memcpy(d + sec1 + 12, &va, 4);
  rs = 0x600; memcpy(d + sec1 + 16, &rs, 4);
  rp = 0x600; memcpy(d + sec1 + 20, &rp, 4);
  sf = 0xC0000040; memcpy(d + sec1 + 36, &sf, 4);

  // === .idata (file offset 0x600, RVA 0x2000) ===
  // Two DLLs: msvcrt.dll and kernel32.dll
  //
  // msvcrt imports: __set_app_type, __getmainargs, _initterm, puts, exit
  // kernel32 imports: ExitProcess
  //
  // Layout:
  //   0x00: Descriptor msvcrt.dll
  //   0x14: Descriptor kernel32.dll
  //   0x28: Null descriptor
  //   0x40: INT msvcrt (5 entries + null = 48 bytes)
  //   0x70: INT kernel32 (1 entry + null = 16 bytes)
  //   0x80: IAT msvcrt (5 entries + null = 48 bytes)  -> IAT RVA 0x2080
  //   0xB0: IAT kernel32 (1 entry + null = 16 bytes)  -> IAT RVA 0x20B0
  //   0xC0: "msvcrt.dll\0"
  //   0xD0: "kernel32.dll\0"
  //   0xE0: ImportByName "__set_app_type\0"
  //   0xF8: ImportByName "__getmainargs\0"
  //   0x110: ImportByName "_initterm\0"
  //   0x124: ImportByName "puts\0"
  //   0x130: ImportByName "exit\0"
  //   0x13C: ImportByName "ExitProcess\0"
  // Data area:
  //   0x180: static int argc
  //   0x184: static char** argv  (8 bytes)
  //   0x18C: static char** envp  (8 bytes)
  //   0x194: static _startupinfo (8 bytes, zeroed)
  //   0x1A0: static func* __xi_a (null, for _initterm range)
  //   0x1A8: static func* __xi_z (null)
  //   0x1B0: "Hello from CRT startup!\0"

  size_t id = 0x600;
  uint64_t zero = 0;

  // Descriptor msvcrt.dll
  uint32_t msvcrt_int_rva = 0x2040;
  memcpy(d + id + 0, &msvcrt_int_rva, 4);
  uint32_t msvcrt_name_rva = 0x20C0;
  memcpy(d + id + 12, &msvcrt_name_rva, 4);
  uint32_t msvcrt_iat_rva = 0x2080;
  memcpy(d + id + 16, &msvcrt_iat_rva, 4);

  // Descriptor kernel32.dll
  uint32_t k32_int_rva = 0x2070;
  memcpy(d + id + 0x14, &k32_int_rva, 4);
  uint32_t k32_name_rva = 0x20D0;
  memcpy(d + id + 0x14 + 12, &k32_name_rva, 4);
  uint32_t k32_iat_rva = 0x20B0;
  memcpy(d + id + 0x14 + 16, &k32_iat_rva, 4);

  // Null descriptor at 0x28

  // IBN RVAs
  uint64_t ibn_set_app_type  = 0x20E0;
  uint64_t ibn_getmainargs   = 0x20F8;
  uint64_t ibn_initterm      = 0x2110;
  uint64_t ibn_puts          = 0x2124;
  uint64_t ibn_exit          = 0x2130;
  uint64_t ibn_exitprocess   = 0x213C;

  // INT msvcrt (5 entries + null)
  memcpy(d + id + 0x40, &ibn_set_app_type, 8);
  memcpy(d + id + 0x48, &ibn_getmainargs, 8);
  memcpy(d + id + 0x50, &ibn_initterm, 8);
  memcpy(d + id + 0x58, &ibn_puts, 8);
  memcpy(d + id + 0x60, &ibn_exit, 8);
  memcpy(d + id + 0x68, &zero, 8);

  // INT kernel32 (1 entry + null)
  memcpy(d + id + 0x70, &ibn_exitprocess, 8);
  memcpy(d + id + 0x78, &zero, 8);

  // IAT msvcrt (same initial values)
  memcpy(d + id + 0x80, &ibn_set_app_type, 8);
  memcpy(d + id + 0x88, &ibn_getmainargs, 8);
  memcpy(d + id + 0x90, &ibn_initterm, 8);
  memcpy(d + id + 0x98, &ibn_puts, 8);
  memcpy(d + id + 0xA0, &ibn_exit, 8);
  memcpy(d + id + 0xA8, &zero, 8);

  // IAT kernel32
  memcpy(d + id + 0xB0, &ibn_exitprocess, 8);
  memcpy(d + id + 0xB8, &zero, 8);

  // DLL names
  memcpy(d + id + 0xC0, "msvcrt.dll", 11);
  memcpy(d + id + 0xD0, "kernel32.dll", 13);

  // ImportByName entries
  uint16_t hint = 0;
  memcpy(d + id + 0xE0, &hint, 2); memcpy(d + id + 0xE2, "__set_app_type", 15);
  memcpy(d + id + 0xF8, &hint, 2); memcpy(d + id + 0xFA, "__getmainargs", 14);
  memcpy(d + id + 0x110, &hint, 2); memcpy(d + id + 0x112, "_initterm", 10);
  memcpy(d + id + 0x124, &hint, 2); memcpy(d + id + 0x126, "puts", 5);
  memcpy(d + id + 0x130, &hint, 2); memcpy(d + id + 0x132, "exit", 5);
  memcpy(d + id + 0x13C, &hint, 2); memcpy(d + id + 0x13E, "ExitProcess", 12);

  // Data: __xi_a and __xi_z (null function pointers = empty init table)
  // at 0x1A0 and 0x1A8 (RVA 0x21A0, 0x21A8)
  memcpy(d + id + 0x1A0, &zero, 8);
  memcpy(d + id + 0x1A8, &zero, 8);

  // Message string at 0x1B0 (RVA 0x21B0)
  memcpy(d + id + 0x1B0, "Hello from CRT startup!", 24);

  // === .text code (file offset 0x200, RVA 0x1000) ===
  // Simulates MinGW mainCRTStartup:
  //
  //   mainCRTStartup:
  //     sub rsp, 0x38
  //     ; __set_app_type(1)  -- console app
  //     mov ecx, 1
  //     call [rip + __set_app_type_IAT]   ; IAT[0] at RVA 0x2080
  //     ; __getmainargs(&argc, &argv, &envp, 0, &startupinfo)
  //     lea rcx, [rip + argc_var]         ; RVA 0x2180
  //     lea rdx, [rip + argv_var]         ; RVA 0x2184
  //     lea r8, [rip + envp_var]          ; RVA 0x218C
  //     xor r9d, r9d                      ; doWild = 0
  //     lea rax, [rip + startupinfo]      ; RVA 0x2194
  //     push rax                          ; 5th arg on stack
  //     call [rip + __getmainargs_IAT]    ; IAT[1] at RVA 0x2088
  //     add rsp, 8
  //     ; _initterm(__xi_a, __xi_z)
  //     lea rcx, [rip + __xi_a]           ; RVA 0x21A0
  //     lea rdx, [rip + __xi_z]           ; RVA 0x21A8
  //     call [rip + _initterm_IAT]        ; IAT[2] at RVA 0x2090
  //     ; puts("Hello from CRT startup!")
  //     lea rcx, [rip + msg]              ; RVA 0x21B0
  //     call [rip + puts_IAT]             ; IAT[3] at RVA 0x2098
  //     ; exit(0)
  //     xor ecx, ecx
  //     call [rip + exit_IAT]             ; IAT[4] at RVA 0x20A0
  //     hlt

  size_t c = 0x200;
  struct PatchEntry { size_t off; int prefix_len; uint32_t target_rva; };
  PatchEntry patches[16];
  int npatch = 0;

  auto emit_call_iat = [&](uint32_t iat_rva) {
    patches[npatch++] = {c, 2, iat_rva};
    d[c++] = 0xFF; d[c++] = 0x15; c += 4;
  };
  auto emit_lea_rcx_rip = [&](uint32_t target_rva) {
    patches[npatch++] = {c, 3, target_rva};
    d[c++] = 0x48; d[c++] = 0x8D; d[c++] = 0x0D; c += 4;
  };
  auto emit_lea_rdx_rip = [&](uint32_t target_rva) {
    patches[npatch++] = {c, 3, target_rva};
    d[c++] = 0x48; d[c++] = 0x8D; d[c++] = 0x15; c += 4;
  };
  auto emit_lea_r8_rip = [&](uint32_t target_rva) {
    patches[npatch++] = {c, 3, target_rva};
    d[c++] = 0x4C; d[c++] = 0x8D; d[c++] = 0x05; c += 4;
  };
  auto emit_lea_rax_rip = [&](uint32_t target_rva) {
    patches[npatch++] = {c, 3, target_rva};
    d[c++] = 0x48; d[c++] = 0x8D; d[c++] = 0x05; c += 4;
  };

  // sub rsp, 0x38
  d[c++] = 0x48; d[c++] = 0x83; d[c++] = 0xEC; d[c++] = 0x38;

  // mov ecx, 1
  d[c++] = 0xB9; d[c++] = 0x01; d[c++] = 0x00; d[c++] = 0x00; d[c++] = 0x00;

  // call [rip + __set_app_type_IAT]
  emit_call_iat(0x2080);

  // lea rcx, [rip + argc_var] at RVA 0x2180
  emit_lea_rcx_rip(0x2180);

  // lea rdx, [rip + argv_var] at RVA 0x2184
  emit_lea_rdx_rip(0x2184);

  // lea r8, [rip + envp_var] at RVA 0x218C
  emit_lea_r8_rip(0x218C);

  // xor r9d, r9d
  d[c++] = 0x45; d[c++] = 0x31; d[c++] = 0xC9;

  // lea rax, [rip + startupinfo]
  emit_lea_rax_rip(0x2194);

  // push rax (5th arg)
  d[c++] = 0x50;

  // call [rip + __getmainargs_IAT]
  emit_call_iat(0x2088);

  // add rsp, 8
  d[c++] = 0x48; d[c++] = 0x83; d[c++] = 0xC4; d[c++] = 0x08;

  // lea rcx, [rip + __xi_a]
  emit_lea_rcx_rip(0x21A0);

  // lea rdx, [rip + __xi_z]
  emit_lea_rdx_rip(0x21A8);

  // call [rip + _initterm_IAT]
  emit_call_iat(0x2090);

  // lea rcx, [rip + msg]
  emit_lea_rcx_rip(0x21B0);

  // call [rip + puts_IAT]
  emit_call_iat(0x2098);

  // xor ecx, ecx
  d[c++] = 0x31; d[c++] = 0xC9;

  // call [rip + exit_IAT]
  emit_call_iat(0x20A0);

  // hlt
  d[c++] = 0xF4;

  // Patch all RIP-relative displacements
  for (int i = 0; i < npatch; i++) {
    auto& p = patches[i];
    uint32_t insn_rva = 0x1000 + static_cast<uint32_t>(p.off - 0x200);
    uint32_t insn_len = p.prefix_len + 4;
    int32_t disp = static_cast<int32_t>(p.target_rva) - static_cast<int32_t>(insn_rva + insn_len);
    memcpy(d + p.off + p.prefix_len, &disp, 4);
  }

  return buf;
}

} // namespace TestPEs
