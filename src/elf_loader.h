// Minimal ELF64 loader for statically-linked x86_64 binaries
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

namespace ELF {

struct Elf64_Ehdr {
  uint8_t  e_ident[16];
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint64_t e_entry;
  uint64_t e_phoff;
  uint64_t e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
};

struct Elf64_Phdr {
  uint32_t p_type;
  uint32_t p_flags;
  uint64_t p_offset;
  uint64_t p_vaddr;
  uint64_t p_paddr;
  uint64_t p_filesz;
  uint64_t p_memsz;
  uint64_t p_align;
};

static constexpr uint32_t PT_LOAD = 1;
static constexpr uint32_t PT_DYNAMIC = 2;
static constexpr uint32_t PF_X = 1;
static constexpr uint32_t PF_W = 2;
static constexpr uint32_t PF_R = 4;

static constexpr uint16_t ET_EXEC = 2;
static constexpr uint16_t ET_DYN  = 3;
static constexpr uint16_t EM_X86_64 = 62;

struct Elf64_Dyn {
  int64_t  d_tag;
  uint64_t d_val;
};

struct Elf64_Rela {
  uint64_t r_offset;
  uint64_t r_info;
  int64_t  r_addend;
};

static constexpr int64_t DT_NULL    = 0;
static constexpr int64_t DT_RELA    = 7;
static constexpr int64_t DT_RELASZ  = 8;
static constexpr int64_t DT_RELAENT = 9;

static constexpr uint32_t R_X86_64_RELATIVE = 8;
static constexpr uint32_t R_X86_64_64       = 1;

static inline uint32_t ELF64_R_TYPE(uint64_t info) { return static_cast<uint32_t>(info); }
static inline uint32_t ELF64_R_SYM(uint64_t info) { return static_cast<uint32_t>(info >> 32); }

struct LoadedELF {
  uint64_t entry;
  void*    base;
  size_t   total_size;
  uint64_t load_bias;
};

inline bool ValidateHeader(const Elf64_Ehdr* ehdr) {
  if (memcmp(ehdr->e_ident, "\x7f" "ELF", 4) != 0) {
    fprintf(stderr, "ELF: bad magic\n");
    return false;
  }
  if (ehdr->e_ident[4] != 2) {
    fprintf(stderr, "ELF: not 64-bit\n");
    return false;
  }
  if (ehdr->e_ident[5] != 1) {
    fprintf(stderr, "ELF: not little-endian\n");
    return false;
  }
  if (ehdr->e_machine != EM_X86_64) {
    fprintf(stderr, "ELF: not x86_64 (machine=%u)\n", ehdr->e_machine);
    return false;
  }
  if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
    fprintf(stderr, "ELF: unsupported type %u (need ET_EXEC or ET_DYN/PIE)\n", ehdr->e_type);
    return false;
  }
  return true;
}

// Load an ELF64 x86_64 binary from a memory buffer.
// Allocates guest memory, copies PT_LOAD segments, returns entry point.
inline bool LoadFromBuffer(const uint8_t* data, size_t size, LoadedELF& out) {
  if (size < sizeof(Elf64_Ehdr)) {
    fprintf(stderr, "ELF: too small\n");
    return false;
  }

  auto* ehdr = reinterpret_cast<const Elf64_Ehdr*>(data);
  if (!ValidateHeader(ehdr))
    return false;

  if (ehdr->e_phoff + ehdr->e_phnum * sizeof(Elf64_Phdr) > size) {
    fprintf(stderr, "ELF: program headers out of bounds\n");
    return false;
  }

  auto* phdrs = reinterpret_cast<const Elf64_Phdr*>(data + ehdr->e_phoff);

  // Find the virtual address range needed
  uint64_t lo = UINT64_MAX, hi = 0;
  int load_count = 0;
  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (phdrs[i].p_type != PT_LOAD) continue;
    load_count++;
    uint64_t seg_lo = phdrs[i].p_vaddr;
    uint64_t seg_hi = phdrs[i].p_vaddr + phdrs[i].p_memsz;
    if (seg_lo < lo) lo = seg_lo;
    if (seg_hi > hi) hi = seg_hi;
  }

  if (load_count == 0) {
    fprintf(stderr, "ELF: no PT_LOAD segments\n");
    return false;
  }

  size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  uint64_t base_aligned = lo & ~(page - 1);
  uint64_t total = ((hi - base_aligned) + page - 1) & ~(page - 1);

  if (total > 512ULL * 1024 * 1024) {
    fprintf(stderr, "ELF: memory footprint too large (0x%llx, max 512MB)\n",
            (unsigned long long)total);
    return false;
  }

  // Allocate at a system-chosen address and compute the load bias
  void* mem = mmap(nullptr, total, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mem == MAP_FAILED) {
    fprintf(stderr, "ELF: mmap failed\n");
    return false;
  }

  uint64_t load_bias = reinterpret_cast<uint64_t>(mem) - base_aligned;

  // Copy each PT_LOAD segment
  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (phdrs[i].p_type != PT_LOAD) continue;

    if (phdrs[i].p_offset + phdrs[i].p_filesz > size) {
      fprintf(stderr, "ELF: segment %d data out of bounds\n", i);
      munmap(mem, total);
      return false;
    }

    uint64_t dest = phdrs[i].p_vaddr + load_bias;
    memcpy(reinterpret_cast<void*>(dest), data + phdrs[i].p_offset, phdrs[i].p_filesz);

    // Zero the BSS portion (memsz > filesz)
    if (phdrs[i].p_memsz > phdrs[i].p_filesz) {
      memset(reinterpret_cast<void*>(dest + phdrs[i].p_filesz), 0,
             phdrs[i].p_memsz - phdrs[i].p_filesz);
    }

    printf("  ELF segment %d: vaddr=0x%llx memsz=0x%llx filesz=0x%llx flags=%c%c%c\n",
           i, phdrs[i].p_vaddr, phdrs[i].p_memsz, phdrs[i].p_filesz,
           (phdrs[i].p_flags & PF_R) ? 'R' : '-',
           (phdrs[i].p_flags & PF_W) ? 'W' : '-',
           (phdrs[i].p_flags & PF_X) ? 'X' : '-');
  }

  // Process relocations for PIE/ET_DYN binaries
  if (ehdr->e_type == ET_DYN && load_bias != 0) {
    uint64_t rela_addr = 0, rela_size = 0, rela_ent = 0;
    for (int i = 0; i < ehdr->e_phnum; i++) {
      if (phdrs[i].p_type != PT_DYNAMIC) continue;
      auto* dyn = reinterpret_cast<const Elf64_Dyn*>(
        reinterpret_cast<uint8_t*>(mem) + (phdrs[i].p_vaddr - base_aligned));
      for (; dyn->d_tag != DT_NULL; dyn++) {
        if (dyn->d_tag == DT_RELA)    rela_addr = dyn->d_val;
        if (dyn->d_tag == DT_RELASZ)  rela_size = dyn->d_val;
        if (dyn->d_tag == DT_RELAENT) rela_ent  = dyn->d_val;
      }
      break;
    }
    if (rela_addr && rela_size && rela_ent) {
      size_t count = rela_size / rela_ent;
      auto* relas = reinterpret_cast<Elf64_Rela*>(rela_addr + load_bias);
      int applied = 0;
      for (size_t i = 0; i < count; i++) {
        uint32_t type = ELF64_R_TYPE(relas[i].r_info);
        auto* target = reinterpret_cast<uint64_t*>(relas[i].r_offset + load_bias);
        if (type == R_X86_64_RELATIVE) {
          *target = static_cast<uint64_t>(relas[i].r_addend) + load_bias;
          applied++;
        } else if (type == R_X86_64_64) {
          *target += load_bias;
          applied++;
        }
      }
      printf("  ELF relocations: %d/%zu applied (bias=0x%llx)\n",
             applied, count, (unsigned long long)load_bias);
    }
  }

  out.entry = ehdr->e_entry + load_bias;
  out.base = mem;
  out.total_size = total;
  out.load_bias = load_bias;

  printf("  ELF loaded: base=0x%llx bias=0x%llx entry=0x%llx type=%s\n",
         (unsigned long long)reinterpret_cast<uint64_t>(mem),
         (unsigned long long)load_bias,
         (unsigned long long)out.entry,
         ehdr->e_type == ET_DYN ? "PIE" : "EXEC");
  return true;
}

// Load from a file on disk
inline bool LoadFromFile(const char* path, LoadedELF& out) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "ELF: cannot open '%s'\n", path);
    return false;
  }

  struct stat st;
  if (fstat(fd, &st) != 0 || st.st_size == 0) {
    fprintf(stderr, "ELF: cannot stat '%s'\n", path);
    close(fd);
    return false;
  }
  if (st.st_size > 256 * 1024 * 1024) {
    fprintf(stderr, "ELF: file too large (%lld bytes, max 256MB)\n", (long long)st.st_size);
    close(fd);
    return false;
  }

  void* mapped = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (mapped == MAP_FAILED) {
    fprintf(stderr, "ELF: mmap file failed\n");
    return false;
  }

  bool ok = LoadFromBuffer(static_cast<const uint8_t*>(mapped), st.st_size, out);
  munmap(mapped, st.st_size);
  return ok;
}

} // namespace ELF
