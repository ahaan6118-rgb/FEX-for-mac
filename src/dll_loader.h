// DLL Loader — loads Windows PE DLLs into memory and resolves cross-DLL imports
// DLLs are loaded as x86_64 code that FEXCore JIT-translates at runtime.
// No native Wine build needed — DLLs are just data (x86_64 code blobs).
#pragma once

#include "pe_loader.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <sys/mman.h>

namespace DLL {

struct LoadedDLL {
  PE::LoadedPE pe;
  std::string name;
  std::map<std::string, uint64_t> exports;
  bool initialized;
};

using StubResolver = uint64_t (*)(const char* dll, const char* func, void* ctx);

class DLLLoader {
public:
  ~DLLLoader() {
    for (auto& dll : dlls_) {
      if (dll.pe.base)
        munmap(dll.pe.base, dll.pe.total_size);
    }
  }

  void SetSearchPath(const char* path) {
    search_paths_.push_back(path);
  }

  void SetStubResolver(StubResolver resolver, void* ctx) {
    stub_resolver_ = resolver;
    stub_ctx_ = ctx;
  }

  LoadedDLL* Load(const char* dll_name) {
    std::string lower = ToLower(dll_name);

    auto it = dll_map_.find(lower);
    if (it != dll_map_.end())
      return &dlls_[it->second];

    if (dlls_.size() >= kMaxDLLs) {
      printf("  [DLL] Too many DLLs loaded (max %zu)\n", kMaxDLLs);
      return nullptr;
    }

    std::string path = FindDLL(dll_name);
    if (path.empty()) {
      if (!IsSystemDLL(lower))
        printf("  [DLL] Cannot find: %s\n", dll_name);
      return nullptr;
    }

    printf("  [DLL] Loading: %s from %s\n", dll_name, path.c_str());

    LoadedDLL dll{};
    dll.name = lower;
    dll.initialized = false;

    if (!PE::LoadFromFile(path.c_str(), dll.pe)) {
      printf("  [DLL] Failed to parse: %s\n", dll_name);
      return nullptr;
    }

    PE::ApplyRelocations(dll.pe);
    ParseExports(dll);

    size_t idx = dlls_.size();
    dlls_.push_back(std::move(dll));
    dll_map_[lower] = idx;

    // Resolve the DLL's own imports (may trigger loading more DLLs)
    auto& loaded = dlls_[idx];
    if (loaded.pe.import_dir_rva != 0) {
      PE::ResolveImports(loaded.pe,
        [this](const char* imp_dll, const char* func) -> uint64_t {
          return ResolveSymbol(imp_dll, func);
        });
    }

    return &dlls_[idx];
  }

  bool ResolveAllImports() {
    int total_unresolved = 0;

    for (auto& dll : dlls_) {
      if (dll.pe.import_dir_rva == 0)
        continue;

      int unresolved = PE::ResolveImports(dll.pe,
        [this](const char* imp_dll, const char* func) -> uint64_t {
          return ResolveSymbol(imp_dll, func);
        });

      total_unresolved += unresolved;
    }

    if (total_unresolved > 0)
      printf("  [DLL] Total unresolved: %d\n", total_unresolved);

    return total_unresolved == 0;
  }

  uint64_t ResolveSymbol(const char* dll_name, const char* func_name) {
    std::string lower = ToLower(dll_name);

    // Check loaded DLLs first
    auto it = dll_map_.find(lower);
    if (it == dll_map_.end()) {
      // Try to load the DLL on demand
      Load(dll_name);
      it = dll_map_.find(lower);
    }
    if (it != dll_map_.end()) {
      auto& dll = dlls_[it->second];
      auto exp = dll.exports.find(func_name);
      if (exp != dll.exports.end())
        return exp->second;
    }

    // Fall back to stub resolver
    if (stub_resolver_) {
      uint64_t addr = stub_resolver_(dll_name, func_name, stub_ctx_);
      if (addr != 0)
        return addr;
    }

    return 0;
  }

  uint64_t GetExport(const char* dll_name, const char* func_name) const {
    std::string lower = ToLower(dll_name);
    auto it = dll_map_.find(lower);
    if (it == dll_map_.end())
      return 0;

    auto& dll = dlls_[it->second];
    auto exp = dll.exports.find(func_name);
    if (exp != dll.exports.end())
      return exp->second;
    return 0;
  }

  void GetAllExecutableRegions(std::vector<std::pair<uint64_t, uint64_t>>& regions) const {
    for (auto& dll : dlls_) {
      if (dll.pe.base)
        regions.push_back({reinterpret_cast<uint64_t>(dll.pe.base), dll.pe.total_size});
    }
  }

  size_t Count() const { return dlls_.size(); }
  const LoadedDLL& Get(size_t i) const { return dlls_[i]; }

private:
  static constexpr size_t kMaxDLLs = 64;
  std::vector<LoadedDLL> dlls_;
  std::map<std::string, size_t> dll_map_;
  std::vector<std::string> search_paths_;
  StubResolver stub_resolver_ = nullptr;
  void* stub_ctx_ = nullptr;

  static bool IsSystemDLL(const std::string& lower_name) {
    if (lower_name == "kernel32.dll" || lower_name == "ntdll.dll" ||
        lower_name == "msvcrt.dll" || lower_name == "user32.dll" ||
        lower_name == "advapi32.dll" || lower_name == "gdi32.dll")
      return true;
    if (lower_name.find("api-ms-win-") == 0)
      return true;
    return false;
  }

  static std::string ToLower(const char* s) {
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return r;
  }

  std::string FindDLL(const char* name) const {
    for (auto& dir : search_paths_) {
      std::string path = dir + "/" + name;
      if (access(path.c_str(), R_OK) == 0)
        return path;

      // Try lowercase
      std::string lower_name = ToLower(name);
      path = dir + "/" + lower_name;
      if (access(path.c_str(), R_OK) == 0)
        return path;
    }
    return "";
  }

  void ParseExports(LoadedDLL& dll) {
    // PE export directory is data directory index 0
    auto* base = static_cast<uint8_t*>(dll.pe.base);
    auto* dos = reinterpret_cast<const PE::DOSHeader*>(base);
    uint32_t pe_off = dos->e_lfanew;
    auto* coff = reinterpret_cast<const PE::COFFHeader*>(base + pe_off + 4);
    size_t opt_off = pe_off + 4 + sizeof(PE::COFFHeader);
    auto* opt = reinterpret_cast<const PE::OptionalHeader64*>(base + opt_off);

    if (opt->NumberOfRvaAndSizes == 0)
      return;

    auto* data_dirs = reinterpret_cast<const PE::DataDirectory*>(
      base + opt_off + offsetof(PE::OptionalHeader64, NumberOfRvaAndSizes) + 4);

    uint32_t export_rva = data_dirs[0].VirtualAddress;
    uint32_t export_size = data_dirs[0].Size;

    if (export_rva == 0 || export_size == 0)
      return;

    if (export_rva + 40 > dll.pe.total_size)
      return;

    // Export directory table
    uint32_t num_functions = *reinterpret_cast<uint32_t*>(base + export_rva + 20);
    uint32_t num_names = *reinterpret_cast<uint32_t*>(base + export_rva + 24);
    uint32_t addr_table_rva = *reinterpret_cast<uint32_t*>(base + export_rva + 28);
    uint32_t name_table_rva = *reinterpret_cast<uint32_t*>(base + export_rva + 32);
    uint32_t ordinal_table_rva = *reinterpret_cast<uint32_t*>(base + export_rva + 36);

    if (addr_table_rva == 0 || name_table_rva == 0)
      return;

    auto* addrs = reinterpret_cast<uint32_t*>(base + addr_table_rva);
    auto* names = reinterpret_cast<uint32_t*>(base + name_table_rva);
    auto* ordinals = reinterpret_cast<uint16_t*>(base + ordinal_table_rva);

    uint32_t limit = (num_names < num_functions) ? num_names : num_functions;

    for (uint32_t i = 0; i < limit; i++) {
      if (names[i] == 0 || names[i] >= dll.pe.total_size)
        continue;

      const char* name = reinterpret_cast<const char*>(base + names[i]);
      uint16_t ordinal = ordinals[i];

      if (ordinal >= num_functions)
        continue;

      uint32_t func_rva = addrs[ordinal];

      // Check for forwarder (RVA points inside export directory)
      if (func_rva >= export_rva && func_rva < export_rva + export_size) {
        // Forwarder string like "NTDLL.RtlInitializeSListHead"
        // We skip forwarders for now
        continue;
      }

      uint64_t func_addr = reinterpret_cast<uint64_t>(base) + func_rva;
      dll.exports[name] = func_addr;
    }

    printf("  [DLL] %s: %zu exports parsed\n", dll.name.c_str(), dll.exports.size());
  }
};

} // namespace DLL
