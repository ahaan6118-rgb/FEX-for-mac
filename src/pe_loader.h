// Minimal PE64 (Portable Executable) loader for x86_64 Windows binaries
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

namespace PE {

// DOS Header (only need e_magic and e_lfanew)
struct DOSHeader {
  uint16_t e_magic;    // "MZ"
  uint8_t  pad[58];
  uint32_t e_lfanew;   // offset to PE signature
};

struct PESignature {
  uint32_t Signature;  // "PE\0\0"
};

struct COFFHeader {
  uint16_t Machine;
  uint16_t NumberOfSections;
  uint32_t TimeDateStamp;
  uint32_t PointerToSymbolTable;
  uint32_t NumberOfSymbols;
  uint16_t SizeOfOptionalHeader;
  uint16_t Characteristics;
};

struct OptionalHeader64 {
  uint16_t Magic;                  // 0x20b = PE32+
  uint8_t  MajorLinkerVersion;
  uint8_t  MinorLinkerVersion;
  uint32_t SizeOfCode;
  uint32_t SizeOfInitializedData;
  uint32_t SizeOfUninitializedData;
  uint32_t AddressOfEntryPoint;
  uint32_t BaseOfCode;
  uint64_t ImageBase;
  uint32_t SectionAlignment;
  uint32_t FileAlignment;
  uint16_t MajorOSVersion;
  uint16_t MinorOSVersion;
  uint16_t MajorImageVersion;
  uint16_t MinorImageVersion;
  uint16_t MajorSubsystemVersion;
  uint16_t MinorSubsystemVersion;
  uint32_t Win32VersionValue;
  uint32_t SizeOfImage;
  uint32_t SizeOfHeaders;
  uint32_t CheckSum;
  uint16_t Subsystem;
  uint16_t DllCharacteristics;
  uint64_t SizeOfStackReserve;
  uint64_t SizeOfStackCommit;
  uint64_t SizeOfHeapReserve;
  uint64_t SizeOfHeapCommit;
  uint32_t LoaderFlags;
  uint32_t NumberOfRvaAndSizes;
};

struct SectionHeader {
  char     Name[8];
  uint32_t VirtualSize;
  uint32_t VirtualAddress;
  uint32_t SizeOfRawData;
  uint32_t PointerToRawData;
  uint32_t PointerToRelocations;
  uint32_t PointerToLinenumbers;
  uint16_t NumberOfRelocations;
  uint16_t NumberOfLinenumbers;
  uint32_t Characteristics;
};

static constexpr uint16_t IMAGE_FILE_MACHINE_AMD64 = 0x8664;
static constexpr uint16_t PE32_PLUS_MAGIC = 0x20b;

// Section characteristic flags
static constexpr uint32_t IMAGE_SCN_MEM_EXECUTE = 0x20000000;
static constexpr uint32_t IMAGE_SCN_MEM_READ    = 0x40000000;
static constexpr uint32_t IMAGE_SCN_MEM_WRITE   = 0x80000000;

struct DataDirectory {
  uint32_t VirtualAddress;
  uint32_t Size;
};

static constexpr int IMAGE_DIRECTORY_ENTRY_IMPORT    = 1;
static constexpr int IMAGE_DIRECTORY_ENTRY_EXCEPTION  = 3;
static constexpr int IMAGE_DIRECTORY_ENTRY_BASERELOC  = 5;
static constexpr int IMAGE_DIRECTORY_ENTRY_TLS        = 9;

// TLS Directory (PE64)
struct TLSDirectory64 {
  uint64_t StartAddressOfRawData;
  uint64_t EndAddressOfRawData;
  uint64_t AddressOfIndex;
  uint64_t AddressOfCallBacks;
  uint32_t SizeOfZeroFill;
  uint32_t Characteristics;
};

// Exception directory entry (x86_64 RUNTIME_FUNCTION)
struct RuntimeFunction {
  uint32_t BeginAddress;
  uint32_t EndAddress;
  uint32_t UnwindInfoAddress;
};

// Base relocation block header
struct BaseRelocationBlock {
  uint32_t VirtualAddress; // page RVA
  uint32_t SizeOfBlock;    // total size including header + entries
};

// Relocation entry types (top 4 bits of each 16-bit entry)
static constexpr uint16_t IMAGE_REL_BASED_ABSOLUTE = 0; // padding, skip
static constexpr uint16_t IMAGE_REL_BASED_DIR64    = 10; // 64-bit fixup (PE64)

struct ImportDescriptor {
  uint32_t OriginalFirstThunk; // RVA to Import Name Table (INT)
  uint32_t TimeDateStamp;
  uint32_t ForwarderChain;
  uint32_t Name;               // RVA to DLL name string
  uint32_t FirstThunk;         // RVA to Import Address Table (IAT)
};

struct ImportByName {
  uint16_t Hint;
  char     Name[1]; // variable length
};

struct LoadedPE {
  uint64_t entry;
  void*    base;
  size_t   total_size;
  uint64_t image_base;
  uint64_t load_bias;
  uint32_t import_dir_rva;
  uint32_t import_dir_size;
  uint32_t reloc_dir_rva;
  uint32_t reloc_dir_size;
  uint32_t exception_dir_rva;
  uint32_t exception_dir_size;
  uint32_t tls_dir_rva;
  uint32_t tls_dir_size;
  uint16_t subsystem;
  uint64_t stack_reserve;
  uint64_t stack_commit;
};

inline bool LoadFromBuffer(const uint8_t* data, size_t size, LoadedPE& out) {
  if (size < sizeof(DOSHeader)) {
    fprintf(stderr, "PE: too small for DOS header\n");
    return false;
  }

  auto* dos = reinterpret_cast<const DOSHeader*>(data);
  if (dos->e_magic != 0x5A4D) { // "MZ"
    fprintf(stderr, "PE: bad DOS magic (0x%04X)\n", dos->e_magic);
    return false;
  }

  uint32_t pe_off = dos->e_lfanew;
  if (pe_off + sizeof(PESignature) + sizeof(COFFHeader) > size) {
    fprintf(stderr, "PE: PE header out of bounds\n");
    return false;
  }

  auto* pe_sig = reinterpret_cast<const PESignature*>(data + pe_off);
  if (pe_sig->Signature != 0x00004550) { // "PE\0\0"
    fprintf(stderr, "PE: bad PE signature\n");
    return false;
  }

  auto* coff = reinterpret_cast<const COFFHeader*>(data + pe_off + 4);
  if (coff->Machine != IMAGE_FILE_MACHINE_AMD64) {
    fprintf(stderr, "PE: not x86_64 (machine=0x%04X)\n", coff->Machine);
    return false;
  }

  size_t opt_off = pe_off + 4 + sizeof(COFFHeader);
  if (opt_off + sizeof(OptionalHeader64) > size) {
    fprintf(stderr, "PE: optional header out of bounds\n");
    return false;
  }

  auto* opt = reinterpret_cast<const OptionalHeader64*>(data + opt_off);
  if (opt->Magic != PE32_PLUS_MAGIC) {
    fprintf(stderr, "PE: not PE32+ (magic=0x%04X)\n", opt->Magic);
    return false;
  }

  printf("  PE image base: 0x%llx\n", (unsigned long long)opt->ImageBase);
  printf("  Entry RVA:     0x%x\n", opt->AddressOfEntryPoint);
  printf("  Sections:      %u\n", coff->NumberOfSections);
  printf("  SizeOfImage:   0x%x\n", opt->SizeOfImage);

  // Allocate memory for the entire image
  size_t image_size = opt->SizeOfImage;
  if (image_size > 512 * 1024 * 1024) {
    fprintf(stderr, "PE: SizeOfImage too large (0x%zx, max 512MB)\n", image_size);
    return false;
  }
  void* mem = mmap(nullptr, image_size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mem == MAP_FAILED) {
    fprintf(stderr, "PE: mmap failed for image\n");
    return false;
  }

  uint64_t load_bias = reinterpret_cast<uint64_t>(mem) - opt->ImageBase;

  // Copy headers
  size_t headers_size = opt->SizeOfHeaders;
  if (headers_size > size) headers_size = size;
  memcpy(mem, data, headers_size);

  // Load sections
  size_t sec_off = opt_off + coff->SizeOfOptionalHeader;
  for (int i = 0; i < coff->NumberOfSections; i++) {
    if (sec_off + sizeof(SectionHeader) > size) {
      fprintf(stderr, "PE: section header %d out of bounds\n", i);
      munmap(mem, image_size);
      return false;
    }

    auto* sec = reinterpret_cast<const SectionHeader*>(data + sec_off);

    char name[9] = {};
    memcpy(name, sec->Name, 8);

    printf("  Section %-8s: RVA=0x%06x VSize=0x%06x RawOff=0x%06x RawSz=0x%06x [%c%c%c]\n",
           name, sec->VirtualAddress, sec->VirtualSize,
           sec->PointerToRawData, sec->SizeOfRawData,
           (sec->Characteristics & IMAGE_SCN_MEM_READ)    ? 'R' : '-',
           (sec->Characteristics & IMAGE_SCN_MEM_WRITE)   ? 'W' : '-',
           (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) ? 'X' : '-');

    if (sec->VirtualAddress + sec->VirtualSize > image_size) {
      fprintf(stderr, "PE: section %s exceeds SizeOfImage\n", name);
      munmap(mem, image_size);
      return false;
    }

    if (sec->SizeOfRawData > 0 && sec->PointerToRawData > 0) {
      if (sec->PointerToRawData + sec->SizeOfRawData > size) {
        fprintf(stderr, "PE: section %s raw data out of bounds\n", name);
        munmap(mem, image_size);
        return false;
      }
      size_t copy_size = sec->SizeOfRawData;
      if (sec->VirtualSize > 0 && copy_size > sec->VirtualSize)
        copy_size = sec->VirtualSize;

      memcpy(static_cast<uint8_t*>(mem) + sec->VirtualAddress,
             data + sec->PointerToRawData, copy_size);
    }

    if (sec->VirtualSize > sec->SizeOfRawData) {
      memset(static_cast<uint8_t*>(mem) + sec->VirtualAddress + sec->SizeOfRawData,
             0, sec->VirtualSize - sec->SizeOfRawData);
    }

    sec_off += sizeof(SectionHeader);
  }

  // Read data directories
  out.import_dir_rva = 0;    out.import_dir_size = 0;
  out.reloc_dir_rva = 0;     out.reloc_dir_size = 0;
  out.exception_dir_rva = 0; out.exception_dir_size = 0;
  out.tls_dir_rva = 0;       out.tls_dir_size = 0;
  out.subsystem = opt->Subsystem;
  out.stack_reserve = opt->SizeOfStackReserve;
  out.stack_commit = opt->SizeOfStackCommit;

  if (opt->NumberOfRvaAndSizes > 0) {
    auto* data_dirs = reinterpret_cast<const DataDirectory*>(
      data + opt_off + offsetof(OptionalHeader64, NumberOfRvaAndSizes) + 4);
    auto read_dir = [&](int idx, uint32_t& rva, uint32_t& sz) {
      if (opt->NumberOfRvaAndSizes > static_cast<uint32_t>(idx)) {
        rva = data_dirs[idx].VirtualAddress;
        sz  = data_dirs[idx].Size;
      }
    };
    read_dir(IMAGE_DIRECTORY_ENTRY_IMPORT, out.import_dir_rva, out.import_dir_size);
    read_dir(IMAGE_DIRECTORY_ENTRY_EXCEPTION, out.exception_dir_rva, out.exception_dir_size);
    read_dir(IMAGE_DIRECTORY_ENTRY_BASERELOC, out.reloc_dir_rva, out.reloc_dir_size);
    read_dir(IMAGE_DIRECTORY_ENTRY_TLS, out.tls_dir_rva, out.tls_dir_size);
  }

  out.entry = opt->AddressOfEntryPoint + reinterpret_cast<uint64_t>(mem);
  out.base = mem;
  out.total_size = image_size;
  out.image_base = opt->ImageBase;
  out.load_bias = load_bias;

  printf("  PE loaded: base=0x%llx bias=0x%llx entry=0x%llx\n",
         (unsigned long long)reinterpret_cast<uint64_t>(mem),
         (unsigned long long)load_bias,
         (unsigned long long)out.entry);
  return true;
}

// Apply base relocations when the PE is loaded at a different address than ImageBase.
inline int ApplyRelocations(LoadedPE& pe) {
  if (pe.reloc_dir_rva == 0 || pe.reloc_dir_size == 0)
    return 0;
  if (pe.load_bias == 0)
    return 0; // loaded at preferred base, no fixups needed

  auto* base = static_cast<uint8_t*>(pe.base);
  int64_t bias = static_cast<int64_t>(pe.load_bias);
  uint32_t offset = 0;
  int applied = 0;

  while (offset < pe.reloc_dir_size) {
    if (pe.reloc_dir_rva + offset + sizeof(BaseRelocationBlock) > pe.total_size)
      break;

    auto* block = reinterpret_cast<BaseRelocationBlock*>(
      base + pe.reloc_dir_rva + offset);

    if (block->SizeOfBlock < sizeof(BaseRelocationBlock))
      break;

    uint32_t num_entries = (block->SizeOfBlock - sizeof(BaseRelocationBlock)) / 2;
    auto* entries = reinterpret_cast<uint16_t*>(
      base + pe.reloc_dir_rva + offset + sizeof(BaseRelocationBlock));

    for (uint32_t i = 0; i < num_entries; i++) {
      uint16_t type = entries[i] >> 12;
      uint16_t off  = entries[i] & 0xFFF;
      uint32_t fixup_rva = block->VirtualAddress + off;

      if (type == IMAGE_REL_BASED_ABSOLUTE)
        continue; // padding

      if (type == IMAGE_REL_BASED_DIR64) {
        if (fixup_rva + 8 > pe.total_size)
          continue;
        auto* target = reinterpret_cast<uint64_t*>(base + fixup_rva);
        *target += bias;
        applied++;
      } else {
        fprintf(stderr, "PE: unsupported reloc type %u at RVA 0x%x\n", type, fixup_rva);
      }
    }

    offset += block->SizeOfBlock;
  }

  if (applied > 0)
    printf("  Relocations: %d applied (bias=0x%llx)\n", applied, (unsigned long long)bias);
  return applied;
}

// Resolve PE imports using a callback that maps "dll!function" to a thunk address.
// resolver(dll_name, func_name) should return the address or 0 if unknown.
template<typename ResolverFn>
inline int ResolveImports(LoadedPE& pe, ResolverFn resolver) {
  if (pe.import_dir_rva == 0)
    return 0;

  auto* base = static_cast<uint8_t*>(pe.base);
  auto* imp = reinterpret_cast<ImportDescriptor*>(base + pe.import_dir_rva);
  int resolved = 0;
  int unresolved = 0;

  // Walk import descriptors until a null entry
  while (imp->Name != 0) {
    if (imp->Name >= pe.total_size) break;
    const char* dll_name = reinterpret_cast<const char*>(base + imp->Name);

    printf("  Import DLL: %s\n", dll_name);

    // Walk the Import Name Table (INT) and patch the Import Address Table (IAT)
    uint32_t int_rva = imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk;
    uint32_t iat_rva = imp->FirstThunk;

    if (int_rva >= pe.total_size || iat_rva >= pe.total_size) {
      imp++;
      continue;
    }

    auto* int_entry = reinterpret_cast<uint64_t*>(base + int_rva);
    auto* iat_entry = reinterpret_cast<uint64_t*>(base + iat_rva);

    while (*int_entry != 0) {
      const char* func_name = nullptr;

      // Bit 63 set = import by ordinal
      if (*int_entry & (1ULL << 63)) {
        uint16_t ordinal = static_cast<uint16_t>(*int_entry & 0xFFFF);
        printf("    [ordinal %u] — not supported\n", ordinal);
        unresolved++;
      } else {
        uint32_t name_rva = static_cast<uint32_t>(*int_entry & 0x7FFFFFFF);
        if (name_rva + sizeof(ImportByName) <= pe.total_size) {
          auto* by_name = reinterpret_cast<ImportByName*>(base + name_rva);
          func_name = by_name->Name;
        }
      }

      if (func_name) {
        uint64_t addr = resolver(dll_name, func_name);
        if (addr != 0) {
          *iat_entry = addr;
          printf("    %s -> 0x%llx\n", func_name, (unsigned long long)addr);
          resolved++;
        } else {
          printf("    %s -> UNRESOLVED\n", func_name);
          unresolved++;
        }
      }

      int_entry++;
      iat_entry++;
    }

    imp++;
  }

  printf("  Imports: %d resolved, %d unresolved\n", resolved, unresolved);
  return unresolved;
}

// Initialize TLS: copy raw data template and set TLS index to 0.
inline bool InitializeTLS(LoadedPE& pe) {
  if (pe.tls_dir_rva == 0 || pe.tls_dir_size == 0)
    return true;

  auto* base = static_cast<uint8_t*>(pe.base);
  if (pe.tls_dir_rva + sizeof(TLSDirectory64) > pe.total_size) {
    fprintf(stderr, "PE: TLS directory out of bounds\n");
    return false;
  }

  auto* tls = reinterpret_cast<TLSDirectory64*>(base + pe.tls_dir_rva);

  // The TLS template addresses are VAs (not RVAs) — adjust for load bias
  uint64_t start_va = tls->StartAddressOfRawData;
  uint64_t end_va = tls->EndAddressOfRawData;
  uint64_t index_va = tls->AddressOfIndex;

  if (start_va && end_va && end_va > start_va) {
    size_t tls_data_size = end_va - start_va;
    printf("  TLS: %zu bytes of template data, %u zero-fill\n",
           tls_data_size, tls->SizeOfZeroFill);
  }

  // Set TLS index to 0 (single-threaded PoC)
  if (index_va) {
    uint64_t index_offset = index_va - pe.image_base;
    if (index_offset + 4 <= pe.total_size) {
      *reinterpret_cast<uint32_t*>(base + index_offset) = 0;
      printf("  TLS: index set to 0 at RVA 0x%llx\n", (unsigned long long)index_offset);
    }
  }

  return true;
}

// Dump exception directory (RUNTIME_FUNCTION table) for diagnostics.
inline void DumpExceptionDirectory(const LoadedPE& pe) {
  if (pe.exception_dir_rva == 0 || pe.exception_dir_size == 0)
    return;

  auto* base = static_cast<const uint8_t*>(pe.base);
  uint32_t count = pe.exception_dir_size / sizeof(RuntimeFunction);
  printf("  Exception directory: %u entries\n", count);

  auto* funcs = reinterpret_cast<const RuntimeFunction*>(base + pe.exception_dir_rva);
  uint32_t show = (count > 5) ? 5 : count;
  for (uint32_t i = 0; i < show; i++) {
    printf("    [%u] 0x%06x - 0x%06x (unwind at 0x%06x)\n",
           i, funcs[i].BeginAddress, funcs[i].EndAddress, funcs[i].UnwindInfoAddress);
  }
  if (count > 5)
    printf("    ... and %u more\n", count - 5);
}

inline bool LoadFromFile(const char* path, LoadedPE& out) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "PE: cannot open '%s'\n", path);
    return false;
  }

  struct stat st;
  if (fstat(fd, &st) != 0 || st.st_size == 0) {
    fprintf(stderr, "PE: cannot stat '%s'\n", path);
    close(fd);
    return false;
  }
  if (st.st_size > 256 * 1024 * 1024) {
    fprintf(stderr, "PE: file too large (%lld bytes, max 256MB)\n", (long long)st.st_size);
    close(fd);
    return false;
  }

  void* mapped = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (mapped == MAP_FAILED) {
    fprintf(stderr, "PE: mmap file failed\n");
    return false;
  }

  bool ok = LoadFromBuffer(static_cast<const uint8_t*>(mapped), st.st_size, out);
  munmap(mapped, st.st_size);
  return ok;
}

} // namespace PE
