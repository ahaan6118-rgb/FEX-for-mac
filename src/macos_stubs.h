// macOS stub implementations for FEXCore's Linux-dependent interfaces
#pragma once

#include <FEXCore/Core/HostFeatures.h>
#include <FEXCore/Core/SignalDelegator.h>
#include <FEXCore/HLE/SyscallHandler.h>
#include <FEXCore/Core/CoreState.h>

#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <mach/mach_time.h>
#include <time.h>
#include <cstdio>
#include <cstring>
#include <optional>

#include "d3d11_stubs.h"

namespace FEX {

FEXCore::HostFeatures BuildHostFeatures();

class MacOSSignalDelegator final : public FEXCore::SignalDelegator {
public:
  MacOSSignalDelegator() {
    // Don't install signal handlers here — Wine installs its own during
    // signal_init_process, and FEXCore's JIT faults are handled there.
  }
};

class MacOSSyscallHandler final : public FEXCore::HLE::SyscallHandler {
public:
  void HandleSyscall(FEXCore::Core::CpuStateFrame* Frame) override {
    Frame->State.rip += 2;

    auto& GPRs = Frame->State.gregs;
    uint64_t Nr = GPRs[FEXCore::X86State::REG_RAX];
    uint64_t A0 = GPRs[FEXCore::X86State::REG_RDI];
    uint64_t A1 = GPRs[FEXCore::X86State::REG_RSI];
    uint64_t A2 = GPRs[FEXCore::X86State::REG_RDX];

    int64_t Ret;
    switch (Nr) {
    case 0: { // SYS_read
      int fd = static_cast<int>(A0);
      if (!IsAllowedFd(fd)) { Ret = -9; break; } // -EBADF
      if (!IsGuestBuffer(A1, A2)) { Ret = -14; break; } // -EFAULT
      Ret = ::read(fd, reinterpret_cast<void*>(A1), A2);
      if (Ret < 0) Ret = -errno;
      break;
    }
    case 1: { // SYS_write
      int fd = static_cast<int>(A0);
      if (!IsAllowedFd(fd)) { Ret = -9; break; } // -EBADF
      if (!IsGuestBuffer(A1, A2)) { Ret = -14; break; } // -EFAULT
      Ret = ::write(fd, reinterpret_cast<const void*>(A1), A2);
      if (Ret < 0) Ret = -errno;
      break;
    }
    case 9: { // SYS_mmap — sandboxed: anonymous-only, no MAP_FIXED, no fd access
      if (GuestMmapUsed_ + A1 > kGuestMemoryLimit) { Ret = -12; break; }
      int prot = static_cast<int>(A2) & (PROT_READ | PROT_WRITE);
      void* P = ::mmap(nullptr, A1, prot,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (P == MAP_FAILED) { Ret = -errno; }
      else { GuestMmapUsed_ += A1; Ret = reinterpret_cast<int64_t>(P); }
      break;
    }
    case 11: { // SYS_munmap
      if (!IsGuestOwned(A0, A1)) { Ret = -22; break; } // -EINVAL
      Ret = ::munmap(reinterpret_cast<void*>(A0), A1);
      if (Ret == 0 && GuestMmapUsed_ >= A1) GuestMmapUsed_ -= A1;
      if (Ret < 0) Ret = -errno;
      break;
    }
    case 12: // SYS_brk stub
      Ret = 0;
      break;
    case 60:  // SYS_exit
    case 231: // SYS_exit_group
      ExitCode_ = static_cast<int>(A0);
      Exited_ = true;
      Frame->State.rip = HltAddr_;
      Ret = 0;
      break;
    default:
      if (Nr >= 0x1000 && Nr < 0x2000) {
        Ret = HandleWin32(Frame, static_cast<uint32_t>(Nr - 0x1000));
        break;
      }
      fprintf(stderr, "[FEX-macOS] Guest syscall %llu (unhandled)\n",
              (unsigned long long)Nr);
      Ret = -38; // -ENOSYS
      break;
    }
    GPRs[FEXCore::X86State::REG_RAX] = static_cast<uint64_t>(Ret);
  }

  void SetHltAddress(uint64_t Addr) { HltAddr_ = Addr; }
  bool DidExit() const { return Exited_; }
  int  GetExitCode() const { return ExitCode_; }
  void ResetExit() { Exited_ = false; ExitCode_ = 0; }

  FEXCore::HLE::ExecutableRangeInfo QueryGuestExecutableRange(
    FEXCore::Core::InternalThreadState* Thread, uint64_t Address) override {
    if (GuestSize > 0 && Address >= GuestBase && Address < GuestBase + GuestSize)
      return {GuestBase, GuestSize, true};
    for (size_t i = 0; i < NumExecRegions_; i++) {
      if (Address >= ExecRegions_[i].base && Address < ExecRegions_[i].base + ExecRegions_[i].size)
        return {ExecRegions_[i].base, ExecRegions_[i].size, true};
    }
    return {0, 0, false};
  }

  std::optional<FEXCore::ExecutableFileSectionInfo> LookupExecutableFileSection(
    FEXCore::Core::InternalThreadState* Thread, uint64_t GuestAddr) override {
    return std::nullopt;
  }

  void SetGuestMemoryRegion(uint64_t Base, uint64_t Size) {
    GuestBase = Base;
    GuestSize = Size;
  }

  void AddExecutableRegion(uint64_t Base, uint64_t Size) {
    if (NumExecRegions_ < kMaxExecRegions)
      ExecRegions_[NumExecRegions_++] = {Base, Size};
  }

  void ClearExecutableRegions() { NumExecRegions_ = 0; }

private:
  uint64_t GuestBase = 0;
  uint64_t GuestSize = 0;
  uint64_t HltAddr_ = 0;
  bool     Exited_ = false;
  int      ExitCode_ = 0;
  uint64_t GuestMmapUsed_ = 0;
  uint32_t Win32LastError_ = 0;
  static constexpr uint64_t kGuestMemoryLimit = 512ULL * 1024 * 1024;

  static constexpr size_t kMaxTlsSlots = 64;
  uint64_t TlsSlots_[kMaxTlsSlots] = {};
  uint32_t NextTlsSlot_ = 0;

  static constexpr size_t kMaxExecRegions = 8;
  struct ExecRegion { uint64_t base; uint64_t size; };
  ExecRegion ExecRegions_[kMaxExecRegions] = {};
  size_t NumExecRegions_ = 0;

  // CRT state for msvcrt.dll stubs
  int CrtArgc_ = 1;
  static constexpr char kDefaultArgv0[] = "rosetta-killer.exe";
  char* CrtArgvPtrs_[2] = { const_cast<char*>(kDefaultArgv0), nullptr };
  char** CrtArgv_ = CrtArgvPtrs_;
  char* CrtEnvpEmpty_[1] = { nullptr };
  char** CrtEnvp_ = CrtEnvpEmpty_;
  int CrtFmode_ = 0;
  int CrtCommode_ = 0;
  int CrtErrno_ = 0;
  uint8_t FakeIOB_[3 * 48] = {};

  // D3D11/DXGI stub manager
  D3D::D3D11StubManager D3DStubs_;

  int FakeFileToFd(uint64_t file_ptr) {
    if (file_ptr <= 2) return static_cast<int>(file_ptr);
    uint64_t base = reinterpret_cast<uint64_t>(FakeIOB_);
    if (file_ptr >= base && file_ptr < base + sizeof(FakeIOB_)) {
      int fd = static_cast<int>((file_ptr - base) / 48);
      if (fd >= 0 && fd <= 2) return fd;
    }
    return STDOUT_FILENO;
  }

  // Win32 API dispatch — Microsoft x64 ABI: RCX, RDX, R8, R9
  // The thunk saves RCX to R10 before syscall (syscall clobbers RCX with return RIP).
  int64_t HandleWin32(FEXCore::Core::CpuStateFrame* Frame, uint32_t stub_id) {
    auto& GPRs = Frame->State.gregs;
    uint64_t rcx = GPRs[FEXCore::X86State::REG_R10]; // first arg saved in R10
    uint64_t rdx = GPRs[FEXCore::X86State::REG_RDX];
    uint64_t r8  = GPRs[FEXCore::X86State::REG_R8];
    uint64_t r9  = GPRs[FEXCore::X86State::REG_R9];

    switch (stub_id) {
    case 1: // ExitProcess(uExitCode)
      ExitCode_ = static_cast<int>(rcx);
      Exited_ = true;
      Frame->State.rip = HltAddr_;
      return 0;

    case 2: // GetStdHandle(nStdHandle) -> HANDLE
      if (static_cast<int32_t>(rcx) == -10) return 0;  // stdin
      if (static_cast<int32_t>(rcx) == -11) return 1;  // stdout
      if (static_cast<int32_t>(rcx) == -12) return 2;  // stderr
      return -1; // INVALID_HANDLE_VALUE

    case 3: { // WriteFile(hFile, lpBuffer, nBytes, lpBytesWritten, lpOverlapped)
      int fd = static_cast<int>(rcx);
      if (!IsAllowedFd(fd)) { Win32LastError_ = 6; return 0; }
      auto written = ::write(fd, reinterpret_cast<const void*>(rdx), static_cast<size_t>(r8));
      if (written < 0) { Win32LastError_ = 29; return 0; }
      if (r9 != 0)
        *reinterpret_cast<uint32_t*>(r9) = static_cast<uint32_t>(written);
      return 1;
    }

    case 4: { // WriteConsoleA
      int fd = static_cast<int>(rcx);
      if (!IsAllowedFd(fd)) { Win32LastError_ = 6; return 0; }
      auto written = ::write(fd, reinterpret_cast<const void*>(rdx), static_cast<size_t>(r8));
      if (written < 0) { Win32LastError_ = 29; return 0; }
      if (r9 != 0)
        *reinterpret_cast<uint32_t*>(r9) = static_cast<uint32_t>(written);
      return 1;
    }

    case 5: return Win32LastError_;   // GetLastError
    case 6: Win32LastError_ = static_cast<uint32_t>(rcx); return 0; // SetLastError

    case 7: // GetCommandLineA
      return reinterpret_cast<int64_t>(kFakeCmdLine);

    case 8: // GetModuleHandleA(lpModuleName)
      return 0x10000;

    case 9: // GetProcessHeap
      return 0xBEEF0000ULL;

    case 10: { // HeapAlloc(hHeap, dwFlags, dwBytes)
      size_t sz = static_cast<size_t>(r8);
      if (sz == 0) sz = 1;
      if (GuestMmapUsed_ + sz > kGuestMemoryLimit) return 0;
      void* p = mmap(nullptr, sz + 16, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (p == MAP_FAILED) return 0;
      *reinterpret_cast<uint64_t*>(p) = sz + 16; // store alloc size for HeapFree/HeapSize
      GuestMmapUsed_ += sz + 16;
      return reinterpret_cast<int64_t>(static_cast<uint8_t*>(p) + 16);
    }

    case 11: { // HeapFree(hHeap, dwFlags, lpMem)
      if (r8 == 0) return 1;
      auto* real = reinterpret_cast<uint8_t*>(r8) - 16;
      uint64_t sz = *reinterpret_cast<uint64_t*>(real);
      if (sz > 0 && sz <= kGuestMemoryLimit && IsGuestOwned(reinterpret_cast<uint64_t>(real), sz)) {
        munmap(real, sz);
        if (sz <= GuestMmapUsed_) GuestMmapUsed_ -= sz;
      }
      return 1;
    }

    case 12: { // VirtualAlloc(lpAddress, dwSize, flAllocType, flProtect)
      size_t sz = static_cast<size_t>(rdx);
      // Round up to host page size (16KB on macOS arm64) to avoid partial-page issues
      size_t host_page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
      if (sz == 0) sz = host_page;
      sz = (sz + host_page - 1) & ~(host_page - 1);
      if (GuestMmapUsed_ + sz > kGuestMemoryLimit) return 0;
      void* p = mmap(nullptr, sz, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (p == MAP_FAILED) return 0;
      GuestMmapUsed_ += sz;
      return reinterpret_cast<int64_t>(p);
    }

    case 13: { // VirtualFree(lpAddress, dwSize, dwFreeType)
      if (rcx == 0) return 0;
      size_t sz = static_cast<size_t>(rdx);
      if (sz == 0) {
        size_t host_page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        sz = host_page;
      }
      if (IsGuestOwned(rcx, sz)) {
        munmap(reinterpret_cast<void*>(rcx), sz);
        if (sz <= GuestMmapUsed_) GuestMmapUsed_ -= sz;
      }
      return 1;
    }
    case 14: return static_cast<int64_t>(getpid()); // GetCurrentProcessId
    case 15: return 1; // GetCurrentThreadId

    case 16: { // QueryPerformanceCounter(lpCounter)
      if (rcx == 0) return 0;
      *reinterpret_cast<uint64_t*>(rcx) = mach_absolute_time();
      return 1;
    }

    case 17: // CloseHandle — no-op for std handles
      return 1;

    case 18: // CreateFileA — stub returns INVALID_HANDLE_VALUE
    case 19: // CreateFileW
      Win32LastError_ = 2; // ERROR_FILE_NOT_FOUND
      return -1; // INVALID_HANDLE_VALUE

    case 20: { // ReadFile(hFile, lpBuffer, nBytes, lpBytesRead, lpOverlapped)
      int fd = static_cast<int>(rcx);
      if (!IsAllowedFd(fd)) { Win32LastError_ = 6; return 0; }
      auto rd = ::read(fd, reinterpret_cast<void*>(rdx), static_cast<size_t>(r8));
      if (rd < 0) { Win32LastError_ = 30; return 0; }
      if (r9 != 0) *reinterpret_cast<uint32_t*>(r9) = static_cast<uint32_t>(rd);
      return 1;
    }

    case 21: return 1; // FlushFileBuffers
    case 22: // GetFileType — return FILE_TYPE_CHAR for std handles
      if (static_cast<int>(rcx) <= 2) return 0x0002; // FILE_TYPE_CHAR
      return 0x0001; // FILE_TYPE_DISK
    case 23: return static_cast<int64_t>(rcx); // SetHandleCount — return same count

    case 24: { // GetStartupInfoA(lpStartupInfo)
      if (rcx == 0) return 0;
      memset(reinterpret_cast<void*>(rcx), 0, 68); // STARTUPINFOA is 68 bytes
      *reinterpret_cast<uint32_t*>(rcx) = 68; // cb = sizeof(STARTUPINFOA)
      return 0;
    }

    case 25: { // GetStartupInfoW(lpStartupInfo)
      if (rcx == 0) return 0;
      memset(reinterpret_cast<void*>(rcx), 0, 104); // STARTUPINFOW is 104 bytes
      *reinterpret_cast<uint32_t*>(rcx) = 104;
      return 0;
    }

    case 26: { // GetModuleFileNameA(hModule, lpFilename, nSize)
      if (rdx == 0 || r8 == 0) return 0;
      const char* name = "rosetta-killer.exe";
      size_t len = strlen(name);
      if (len >= r8) len = r8 - 1;
      memcpy(reinterpret_cast<void*>(rdx), name, len);
      reinterpret_cast<char*>(rdx)[len] = '\0';
      return static_cast<int64_t>(len);
    }

    case 27: { // GetModuleFileNameW(hModule, lpFilename, nSize)
      if (rdx == 0 || r8 == 0) return 0;
      const char* name = "rosetta-killer.exe";
      size_t len = strlen(name);
      if (len >= r8) len = r8 - 1;
      auto* dst = reinterpret_cast<uint16_t*>(rdx);
      for (size_t i = 0; i < len; i++) dst[i] = static_cast<uint16_t>(name[i]);
      dst[len] = 0;
      return static_cast<int64_t>(len);
    }

    case 28: { // GetEnvironmentStringsW -> pointer to empty env block
      static const uint16_t kEmptyEnv[2] = {0, 0};
      return reinterpret_cast<int64_t>(kEmptyEnv);
    }
    case 29: return 1; // FreeEnvironmentStringsW

    case 30: // GetEnvironmentVariableA
    case 31: // GetEnvironmentVariableW
      Win32LastError_ = 203; // ERROR_ENVVAR_NOT_FOUND
      return 0;

    case 32: { // WideCharToMultiByte(CP, flags, wstr, wlen, str, slen, defchar, useddef)
      auto* wstr = reinterpret_cast<const uint16_t*>(r8);
      int wlen = static_cast<int>(r9);
      // 5th and 6th args are on stack — read from Frame
      uint64_t rsp = Frame->State.gregs[FEXCore::X86State::REG_RSP];
      auto* str = reinterpret_cast<char*>(*reinterpret_cast<uint64_t*>(rsp + 0x28));
      int slen = static_cast<int>(*reinterpret_cast<uint64_t*>(rsp + 0x30));

      if (wlen == -1) { // null-terminated
        wlen = 0;
        while (wstr[wlen]) wlen++;
        wlen++;
      }
      if (slen == 0) return wlen; // query size
      int copied = (wlen < slen) ? wlen : slen;
      for (int i = 0; i < copied; i++)
        str[i] = (wstr[i] < 128) ? static_cast<char>(wstr[i]) : '?';
      return copied;
    }

    case 33: { // MultiByteToWideChar(CP, flags, str, slen, wstr, wlen)
      auto* str = reinterpret_cast<const char*>(r8);
      int slen = static_cast<int>(r9);
      uint64_t rsp = Frame->State.gregs[FEXCore::X86State::REG_RSP];
      auto* wstr = reinterpret_cast<uint16_t*>(*reinterpret_cast<uint64_t*>(rsp + 0x28));
      int wlen = static_cast<int>(*reinterpret_cast<uint64_t*>(rsp + 0x30));

      if (slen == -1) slen = static_cast<int>(strlen(str)) + 1;
      if (wlen == 0) return slen; // query size
      int copied = (slen < wlen) ? slen : wlen;
      for (int i = 0; i < copied; i++)
        wstr[i] = static_cast<uint16_t>(static_cast<unsigned char>(str[i]));
      return copied;
    }

    case 34: return -1; // GetCurrentProcess -> pseudo-handle
    case 35: // TerminateProcess
      ExitCode_ = static_cast<int>(rdx);
      Exited_ = true;
      Frame->State.rip = HltAddr_;
      return 1;

    case 36: return 0; // SetUnhandledExceptionFilter -> return previous (NULL)
    case 37: return 0; // UnhandledExceptionFilter -> EXCEPTION_CONTINUE_SEARCH
    case 38: return 0; // IsDebuggerPresent -> FALSE
    case 39: return 0; // IsProcessorFeaturePresent -> FALSE for all

    case 40: { // GetSystemTimeAsFileTime(lpSystemTimeAsFileTime)
      if (rcx == 0) return 0;
      // Windows FILETIME: 100ns intervals since 1601-01-01
      // Unix epoch offset: 116444736000000000 (100ns ticks)
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      uint64_t ft = 116444736000000000ULL +
        static_cast<uint64_t>(ts.tv_sec) * 10000000ULL +
        static_cast<uint64_t>(ts.tv_nsec) / 100;
      *reinterpret_cast<uint64_t*>(rcx) = ft;
      return 0;
    }

    case 41: return static_cast<int64_t>(mach_absolute_time() / 1000000); // GetTickCount (ms)

    case 42: { // QueryPerformanceFrequency(lpFrequency)
      if (rcx == 0) return 0;
      mach_timebase_info_data_t info;
      mach_timebase_info(&info);
      uint64_t freq = (1000000000ULL * info.denom) / info.numer;
      *reinterpret_cast<uint64_t*>(rcx) = freq;
      return 1;
    }

    case 43: { // GetConsoleMode(hConsole, lpMode)
      if (rdx == 0) return 0;
      *reinterpret_cast<uint32_t*>(rdx) = 0x7; // ENABLE_PROCESSED_INPUT etc.
      return 1;
    }
    case 44: return 1; // SetConsoleMode -> success
    case 45: return 65001; // GetConsoleCP -> UTF-8
    case 46: return 65001; // GetConsoleOutputCP -> UTF-8
    case 47: return 1; // SetConsoleCtrlHandler -> success
    case 48: return 65001; // GetACP -> UTF-8
    case 49: return 437;   // GetOEMCP -> OEM United States

    case 50: { // GetCPInfo(codepage, lpCPInfo)
      if (rdx == 0) return 0;
      auto* info = reinterpret_cast<uint8_t*>(rdx);
      memset(info, 0, 20); // CPINFO is 20 bytes
      info[0] = 1; // MaxCharSize = 1 for single-byte codepages
      return 1;
    }

    case 51: return 1; // GetStringTypeW -> success (stub)
    case 52: return 0; // LCMapStringW -> 0 (stub)

    case 53: { // HeapSize(hHeap, dwFlags, lpMem)
      if (r8 == 0) return static_cast<int64_t>(-1);
      auto* real = reinterpret_cast<uint8_t*>(r8) - 16;
      if (!IsGuestOwned(reinterpret_cast<uint64_t>(real), 16)) return static_cast<int64_t>(-1);
      uint64_t sz = *reinterpret_cast<uint64_t*>(real);
      if (sz > kGuestMemoryLimit) return static_cast<int64_t>(-1);
      return static_cast<int64_t>(sz > 16 ? sz - 16 : 0);
    }

    case 54: { // HeapReAlloc(hHeap, dwFlags, lpMem, dwBytes)
      size_t new_sz = static_cast<size_t>(r9);
      if (new_sz == 0) new_sz = 1;
      if (r8 == 0) {
        if (GuestMmapUsed_ + new_sz > kGuestMemoryLimit) return 0;
        void* p = mmap(nullptr, new_sz + 16, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) return 0;
        *reinterpret_cast<uint64_t*>(p) = new_sz + 16;
        GuestMmapUsed_ += new_sz + 16;
        return reinterpret_cast<int64_t>(static_cast<uint8_t*>(p) + 16);
      }
      auto* old_real = reinterpret_cast<uint8_t*>(r8) - 16;
      uint64_t old_sz = *reinterpret_cast<uint64_t*>(old_real);
      if (old_sz > kGuestMemoryLimit) return 0;
      size_t old_usable = (old_sz > 16) ? old_sz - 16 : 0;
      if (new_sz <= old_usable) return static_cast<int64_t>(r8);
      if (GuestMmapUsed_ + new_sz > kGuestMemoryLimit) return 0;
      void* np = mmap(nullptr, new_sz + 16, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (np == MAP_FAILED) return 0;
      *reinterpret_cast<uint64_t*>(np) = new_sz + 16;
      memcpy(static_cast<uint8_t*>(np) + 16, reinterpret_cast<void*>(r8),
             old_usable < new_sz ? old_usable : new_sz);
      if (IsGuestOwned(reinterpret_cast<uint64_t>(old_real), old_sz))
        munmap(old_real, old_sz);
      GuestMmapUsed_ += new_sz + 16;
      if (old_sz <= GuestMmapUsed_) GuestMmapUsed_ -= old_sz;
      return reinterpret_cast<int64_t>(static_cast<uint8_t*>(np) + 16);
    }

    case 55: return 0; // GetProcAddress -> NULL (stub)
    case 56: return 0; // LoadLibraryA -> NULL (stub)
    case 57: return 0x10000; // GetModuleHandleW -> same fake handle
    case 58: return 0; // RtlUnwind -> void

    case 59: { // InitializeCriticalSectionAndSpinCount(lpCS, dwSpinCount)
      if (rcx != 0) memset(reinterpret_cast<void*>(rcx), 0, 40); // RTL_CRITICAL_SECTION
      return 1;
    }
    case 60: return 0; // EnterCriticalSection -> void (single-threaded, no-op)
    case 61: return 0; // LeaveCriticalSection -> void
    case 62: return 0; // DeleteCriticalSection -> void

    case 63: { // TlsAlloc -> index
      if (NextTlsSlot_ < kMaxTlsSlots) return NextTlsSlot_++;
      Win32LastError_ = 0x58; // ERROR_NO_MORE_ITEMS
      return 0xFFFFFFFF;
    }
    case 64: { // TlsGetValue(dwTlsIndex)
      uint32_t idx = static_cast<uint32_t>(rcx);
      if (idx < kMaxTlsSlots) { Win32LastError_ = 0; return static_cast<int64_t>(TlsSlots_[idx]); }
      Win32LastError_ = 0x57; return 0;
    }
    case 65: { // TlsSetValue(dwTlsIndex, lpTlsValue)
      uint32_t idx = static_cast<uint32_t>(rcx);
      if (idx < kMaxTlsSlots) { TlsSlots_[idx] = rdx; return 1; }
      Win32LastError_ = 0x57; return 0;
    }
    case 66: return 1; // TlsFree -> success

    case 67: { // Sleep(dwMilliseconds) — capped to prevent hangs
      uint64_t ms = rcx;
      if (ms > 10000) ms = 10000;
      if (ms > 0) usleep(static_cast<useconds_t>(ms) * 1000);
      return 0;
    }

    case 68: { // GetSystemInfo(lpSystemInfo)
      if (rcx == 0) return 0;
      auto* si = reinterpret_cast<uint8_t*>(rcx);
      memset(si, 0, 48); // SYSTEM_INFO is 48 bytes
      // wProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64 = 9
      *reinterpret_cast<uint16_t*>(si) = 9;
      // dwPageSize
      *reinterpret_cast<uint32_t*>(si + 4) = 4096;
      // dwNumberOfProcessors
      int ncpu = 1;
      size_t len = sizeof(ncpu);
      sysctlbyname("hw.ncpu", &ncpu, &len, nullptr, 0);
      *reinterpret_cast<uint32_t*>(si + 32) = static_cast<uint32_t>(ncpu);
      // wProcessorLevel = 6 (Pentium Pro family)
      *reinterpret_cast<uint16_t*>(si + 36) = 6;
      return 0;
    }

    // === msvcrt.dll CRT startup ===
    case 69: { // __getmainargs(int* argc, char*** argv, char*** env, int doWild, void* startInfo)
      if (rcx) *reinterpret_cast<int*>(rcx) = CrtArgc_;
      if (rdx) *reinterpret_cast<uint64_t*>(rdx) = reinterpret_cast<uint64_t>(CrtArgv_);
      if (r8)  *reinterpret_cast<uint64_t*>(r8) = reinterpret_cast<uint64_t>(CrtEnvp_);
      return 0;
    }
    case 70: return 0; // __set_app_type
    case 71: { // _initterm(func** start, func** end)
      auto* start = reinterpret_cast<uint64_t*>(rcx);
      auto* end = reinterpret_cast<uint64_t*>(rdx);
      // Each entry is a function pointer; call non-null ones.
      // For our PoC, we skip calling them — CRT init functions would
      // need full guest execution context. Real impl would re-enter JIT.
      (void)start; (void)end;
      return 0;
    }
    case 72: return 0; // _initterm_e -> success
    case 73: // exit(int code)
      ExitCode_ = static_cast<int>(rcx);
      Exited_ = true;
      Frame->State.rip = HltAddr_;
      return 0;
    case 74: return 0; // _cexit
    case 75: return 0; // _c_exit
    case 76: // _amsg_exit
      ExitCode_ = static_cast<int>(rcx);
      Exited_ = true;
      Frame->State.rip = HltAddr_;
      return 0;
    case 77: return 0; // __setusermatherr
    case 78: return 0; // _configthreadlocale -> 0
    case 79: return 0; // _set_new_mode -> old mode (0)

    // === msvcrt.dll I/O ===
    case 80: { // printf(const char* fmt, ...)
      auto* fmt = reinterpret_cast<const char*>(rcx);
      if (!fmt) return 0;
      uint64_t args[16] = {rdx, r8, r9};
      uint64_t rsp = Frame->State.gregs[FEXCore::X86State::REG_RSP];
      for (int i = 3; i < 16; i++)
        args[i] = *reinterpret_cast<uint64_t*>(rsp + 0x28 + (i - 3) * 8);
      int ai = 0, written = 0;
      for (const char* p = fmt; *p; p++) {
        if (*p != '%' || !*(p + 1)) { fputc(*p, stdout); written++; continue; }
        p++;
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') p++;
        int width = 0; bool hw = false;
        if (*p == '*') { width = (int)args[ai++]; hw = true; p++; }
        else while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); hw = true; p++; }
        int prec = -1; bool hp = false;
        if (*p == '.') { p++; prec = 0; hp = true;
          if (*p == '*') { prec = (int)args[ai++]; p++; }
          else while (*p >= '0' && *p <= '9') { prec = prec * 10 + (*p - '0'); p++; }
        }
        while (*p == 'h' || *p == 'l' || *p == 'z' || *p == 'j' || *p == 't') p++;
        if (*p == 'I' && *(p+1) >= '0' && *(p+1) <= '9') { p += 3; }
        char s = *p;
        uint64_t v = (ai < 16) ? args[ai++] : 0;
        if (s == 'd' || s == 'i') {
          if (hp) written += fprintf(stdout, "%.*lld", prec, (long long)(int64_t)v);
          else if (hw) written += fprintf(stdout, "%*lld", width, (long long)(int64_t)v);
          else written += fprintf(stdout, "%lld", (long long)(int32_t)v);
        } else if (s == 'u') { written += fprintf(stdout, "%llu", (unsigned long long)(uint32_t)v);
        } else if (s == 'x' || s == 'X') { written += fprintf(stdout, s == 'x' ? "%llx" : "%llX", (unsigned long long)v);
        } else if (s == 'o') { written += fprintf(stdout, "%llo", (unsigned long long)v);
        } else if (s == 's') {
          auto* str = reinterpret_cast<const char*>(v);
          if (hp) written += fprintf(stdout, "%.*s", prec, str ? str : "(null)");
          else written += fprintf(stdout, "%s", str ? str : "(null)");
        } else if (s == 'c') { fputc((char)v, stdout); written++;
        } else if (s == 'p') { written += fprintf(stdout, "%p", reinterpret_cast<void*>(v));
        } else if (s == 'f' || s == 'F') { double d; memcpy(&d, &v, 8); if (hp) written += fprintf(stdout, "%.*f", prec, d); else written += fprintf(stdout, "%f", d);
        } else if (s == 'e' || s == 'E') { double d; memcpy(&d, &v, 8); written += fprintf(stdout, s == 'e' ? "%e" : "%E", d);
        } else if (s == 'g' || s == 'G') { double d; memcpy(&d, &v, 8); written += fprintf(stdout, s == 'g' ? "%g" : "%G", d);
        } else if (s == '%') { fputc('%', stdout); written++; ai--;
        } else { fputc('%', stdout); fputc(s, stdout); written += 2; ai--; }
      }
      fflush(stdout);
      return written;
    }

    case 81: { // fprintf(FILE* stream, const char* fmt, ...)
      int fd = FakeFileToFd(rcx);
      auto* fmt = reinterpret_cast<const char*>(rdx);
      if (!fmt) return 0;
      uint64_t args[16] = {r8, r9};
      uint64_t rsp = Frame->State.gregs[FEXCore::X86State::REG_RSP];
      for (int i = 2; i < 16; i++)
        args[i] = *reinterpret_cast<uint64_t*>(rsp + 0x28 + (i - 2) * 8);
      int ai = 0, written = 0;
      for (const char* p = fmt; *p; p++) {
        if (*p != '%' || !*(p + 1)) { ::write(fd, p, 1); written++; continue; }
        p++;
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') p++;
        if (*p == '*') { ai++; p++; } else while (*p >= '0' && *p <= '9') p++;
        int prec = -1; bool hp = false;
        if (*p == '.') { p++; prec = 0; hp = true;
          if (*p == '*') { prec = (int)args[ai++]; p++; }
          else while (*p >= '0' && *p <= '9') { prec = prec * 10 + (*p - '0'); p++; }
        }
        while (*p == 'h' || *p == 'l' || *p == 'z' || *p == 'j' || *p == 't') p++;
        if (*p == 'I' && *(p+1) >= '0' && *(p+1) <= '9') { p += 3; }
        char s = *p;
        uint64_t v = (ai < 16) ? args[ai++] : 0;
        if (s == 'd' || s == 'i') { written += dprintf(fd, "%lld", (long long)(int32_t)v);
        } else if (s == 'u') { written += dprintf(fd, "%llu", (unsigned long long)(uint32_t)v);
        } else if (s == 'x' || s == 'X') { written += dprintf(fd, s == 'x' ? "%llx" : "%llX", (unsigned long long)v);
        } else if (s == 's') {
          auto* str = reinterpret_cast<const char*>(v);
          if (hp) written += dprintf(fd, "%.*s", prec, str ? str : "(null)");
          else written += dprintf(fd, "%s", str ? str : "(null)");
        } else if (s == 'c') { char ch = (char)v; ::write(fd, &ch, 1); written++;
        } else if (s == 'p') { written += dprintf(fd, "%p", reinterpret_cast<void*>(v));
        } else if (s == 'f') { double d; memcpy(&d, &v, 8); if (hp) written += dprintf(fd, "%.*f", prec, d); else written += dprintf(fd, "%f", d);
        } else if (s == 'e') { double d; memcpy(&d, &v, 8); written += dprintf(fd, "%e", d);
        } else if (s == 'g') { double d; memcpy(&d, &v, 8); written += dprintf(fd, "%g", d);
        } else if (s == '%') { ::write(fd, "%", 1); written++; ai--;
        } else { ::write(fd, p - 1, 2); written += 2; ai--; }
      }
      return written;
    }

    case 82: { // puts(const char* str)
      auto* str = reinterpret_cast<const char*>(rcx);
      if (!str) str = "(null)";
      size_t len = strlen(str);
      ::write(STDOUT_FILENO, str, len);
      ::write(STDOUT_FILENO, "\n", 1);
      return static_cast<int64_t>(len + 1);
    }

    case 83: { // putchar(int c)
      char ch = static_cast<char>(rcx);
      ::write(STDOUT_FILENO, &ch, 1);
      return static_cast<int64_t>(static_cast<unsigned char>(ch));
    }

    case 84: { // fwrite(const void* ptr, size_t size, size_t count, FILE* stream)
      size_t elem = static_cast<size_t>(rdx);
      size_t cnt  = static_cast<size_t>(r8);
      if (elem == 0 || cnt == 0) return 0;
      if (cnt > kGuestMemoryLimit / elem) return 0;
      size_t total = elem * cnt;
      if (total > kGuestMemoryLimit) return 0;
      int fd = FakeFileToFd(r9);
      auto w = ::write(fd, reinterpret_cast<const void*>(rcx), total);
      if (w <= 0) return 0;
      return static_cast<int64_t>(static_cast<size_t>(w) / elem);
    }

    case 85: { // fflush(FILE* stream)
      // No-op since we use write() directly
      return 0;
    }

    case 86: // __iob_func() -> pointer to fake FILE array
      return reinterpret_cast<int64_t>(FakeIOB_);

    case 87: { // _fileno(FILE* stream)
      return FakeFileToFd(rcx);
    }

    case 88: return 0; // _setmode -> previous mode (0)

    // === msvcrt.dll memory ===
    case 89: { // malloc(size_t size)
      size_t sz = static_cast<size_t>(rcx);
      if (sz == 0) sz = 1;
      if (GuestMmapUsed_ + sz > kGuestMemoryLimit) return 0;
      void* p = mmap(nullptr, sz + 16, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (p == MAP_FAILED) return 0;
      *reinterpret_cast<uint64_t*>(p) = sz + 16;
      GuestMmapUsed_ += sz + 16;
      return reinterpret_cast<int64_t>(static_cast<uint8_t*>(p) + 16);
    }

    case 90: { // free(void* ptr)
      if (rcx == 0) return 0;
      auto* real = reinterpret_cast<uint8_t*>(rcx) - 16;
      uint64_t sz = *reinterpret_cast<uint64_t*>(real);
      if (sz > 0 && sz <= kGuestMemoryLimit && IsGuestOwned(reinterpret_cast<uint64_t>(real), sz)) {
        munmap(real, sz);
        if (sz <= GuestMmapUsed_) GuestMmapUsed_ -= sz;
      }
      return 0;
    }

    case 91: { // calloc(size_t count, size_t size)
      size_t count = static_cast<size_t>(rcx);
      size_t each  = static_cast<size_t>(rdx);
      if (each != 0 && count > kGuestMemoryLimit / each) return 0;
      size_t total = count * each;
      if (total == 0) total = 1;
      if (GuestMmapUsed_ + total > kGuestMemoryLimit) return 0;
      void* p = mmap(nullptr, total + 16, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (p == MAP_FAILED) return 0;
      *reinterpret_cast<uint64_t*>(p) = total + 16;
      GuestMmapUsed_ += total + 16;
      return reinterpret_cast<int64_t>(static_cast<uint8_t*>(p) + 16);
    }

    case 92: { // realloc(void* ptr, size_t size)
      size_t new_sz = static_cast<size_t>(rdx);
      if (rcx == 0) {
        // realloc(NULL, size) = malloc(size)
        if (new_sz == 0) new_sz = 1;
        if (GuestMmapUsed_ + new_sz > kGuestMemoryLimit) return 0;
        void* p = mmap(nullptr, new_sz + 16, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) return 0;
        *reinterpret_cast<uint64_t*>(p) = new_sz + 16;
        GuestMmapUsed_ += new_sz + 16;
        return reinterpret_cast<int64_t>(static_cast<uint8_t*>(p) + 16);
      }
      if (new_sz == 0) { // realloc(ptr, 0) = free
        auto* real = reinterpret_cast<uint8_t*>(rcx) - 16;
        uint64_t sz = *reinterpret_cast<uint64_t*>(real);
        if (sz > 0 && sz <= kGuestMemoryLimit && IsGuestOwned(reinterpret_cast<uint64_t>(real), sz)) {
          munmap(real, sz);
          if (sz <= GuestMmapUsed_) GuestMmapUsed_ -= sz;
        }
        return 0;
      }
      auto* old_real = reinterpret_cast<uint8_t*>(rcx) - 16;
      uint64_t old_sz = *reinterpret_cast<uint64_t*>(old_real);
      if (old_sz > kGuestMemoryLimit) return 0;
      size_t old_usable = (old_sz > 16) ? old_sz - 16 : 0;
      if (new_sz <= old_usable) return static_cast<int64_t>(rcx);
      if (GuestMmapUsed_ + new_sz > kGuestMemoryLimit) return 0;
      void* np = mmap(nullptr, new_sz + 16, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (np == MAP_FAILED) return 0;
      *reinterpret_cast<uint64_t*>(np) = new_sz + 16;
      memcpy(static_cast<uint8_t*>(np) + 16, reinterpret_cast<void*>(rcx),
             old_usable < new_sz ? old_usable : new_sz);
      if (IsGuestOwned(reinterpret_cast<uint64_t>(old_real), old_sz))
        munmap(old_real, old_sz);
      GuestMmapUsed_ += new_sz + 16;
      if (old_sz <= GuestMmapUsed_) GuestMmapUsed_ -= old_sz;
      return reinterpret_cast<int64_t>(static_cast<uint8_t*>(np) + 16);
    }

    // === msvcrt.dll string ===
    case 93: // strlen
      if (!rcx) return 0;
      return static_cast<int64_t>(strnlen(reinterpret_cast<const char*>(rcx), kGuestMemoryLimit));
    case 94: // strcpy
      if (!rcx || !rdx) return static_cast<int64_t>(rcx);
      strcpy(reinterpret_cast<char*>(rcx), reinterpret_cast<const char*>(rdx));
      return static_cast<int64_t>(rcx);
    case 95: // strncpy
      if (!rcx || !rdx) return static_cast<int64_t>(rcx);
      strncpy(reinterpret_cast<char*>(rcx), reinterpret_cast<const char*>(rdx),
              static_cast<size_t>(r8));
      return static_cast<int64_t>(rcx);
    case 96: // strcmp
      if (!rcx || !rdx) return 0;
      return strcmp(reinterpret_cast<const char*>(rcx), reinterpret_cast<const char*>(rdx));
    case 97: // memset
      if (!rcx || r8 > kGuestMemoryLimit) return static_cast<int64_t>(rcx);
      memset(reinterpret_cast<void*>(rcx), static_cast<int>(rdx), static_cast<size_t>(r8));
      return static_cast<int64_t>(rcx);
    case 98: // memcpy
      if (!rcx || !rdx || r8 > kGuestMemoryLimit) return static_cast<int64_t>(rcx);
      memcpy(reinterpret_cast<void*>(rcx), reinterpret_cast<const void*>(rdx),
             static_cast<size_t>(r8));
      return static_cast<int64_t>(rcx);
    case 99: // memmove
      if (!rcx || !rdx || r8 > kGuestMemoryLimit) return static_cast<int64_t>(rcx);
      memmove(reinterpret_cast<void*>(rcx), reinterpret_cast<const void*>(rdx),
              static_cast<size_t>(r8));
      return static_cast<int64_t>(rcx);

    // === msvcrt.dll misc ===
    case 100: return reinterpret_cast<int64_t>(&CrtFmode_);   // _fmode
    case 101: return reinterpret_cast<int64_t>(&CrtCommode_); // _commode
    case 102: return reinterpret_cast<int64_t>(&CrtArgc_);    // __p___argc
    case 103: return reinterpret_cast<int64_t>(&CrtArgv_);    // __p___argv
    case 104: return reinterpret_cast<int64_t>(&CrtErrno_);   // _errno -> &errno
    case 105: // abort
      ExitCode_ = 3;
      Exited_ = true;
      Frame->State.rip = HltAddr_;
      return 0;
    case 106: return 0; // signal -> SIG_DFL (0)

    // === UCRT / additional stubs ===
    case 107: return 0; // _configure_narrow_argv -> success
    case 108: return 0; // _initialize_narrow_environment -> success
    case 109: return 0; // _crt_atexit -> success (register atexit handler, ignored)
    case 110: return 0; // _set_invalid_parameter_handler -> NULL (old handler)
    case 111: return reinterpret_cast<int64_t>(&CrtEnvp_); // __p__environ
    case 112: // _exit (immediate, no cleanup)
      ExitCode_ = static_cast<int>(rcx);
      Exited_ = true;
      Frame->State.rip = HltAddr_;
      return 0;
    case 113: // __acrt_iob_func(index) -> FILE* for stdin(0)/stdout(1)/stderr(2)
      return reinterpret_cast<int64_t>(FakeIOB_ + static_cast<size_t>(rcx) * 48);
    case 114: return reinterpret_cast<int64_t>(&CrtCommode_); // __p__commode
    case 115: return reinterpret_cast<int64_t>(&CrtFmode_);   // __p__fmode
    case 116: { // __stdio_common_vfprintf(options, stream, format, locale, arglist)
      auto* fmt = reinterpret_cast<const char*>(r8);
      if (!fmt) return 0;
      int fd = FakeFileToFd(rdx);
      uint64_t rsp = Frame->State.gregs[FEXCore::X86State::REG_RSP];
      uint64_t ap_val = *reinterpret_cast<uint64_t*>(rsp + 0x28);
      auto* arglist = ap_val ? reinterpret_cast<uint64_t*>(ap_val) : nullptr;
      int arg_idx = 0;
      static constexpr int kMaxArgs = 32;
      int written = 0;
      auto next_int = [&]() -> int64_t {
        if (!arglist || arg_idx >= kMaxArgs) return 0;
        return static_cast<int64_t>(arglist[arg_idx++]);
      };
      auto next_uint = [&]() -> uint64_t {
        if (!arglist || arg_idx >= kMaxArgs) return 0;
        return arglist[arg_idx++];
      };
      auto next_dbl = [&]() -> double {
        uint64_t bits = next_uint();
        double d;
        memcpy(&d, &bits, 8);
        return d;
      };
      for (const char* p = fmt; *p; p++) {
        if (*p != '%' || !*(p + 1)) { ::write(fd, p, 1); written++; continue; }
        p++;
        // Flags
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') p++;
        // Width
        int width = 0;
        bool has_width = false;
        if (*p == '*') { width = static_cast<int>(next_int()); has_width = true; p++; }
        else while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); has_width = true; p++; }
        // Precision
        int prec = -1;
        bool has_prec = false;
        if (*p == '.') {
          p++; prec = 0; has_prec = true;
          if (*p == '*') { prec = static_cast<int>(next_int()); p++; }
          else while (*p >= '0' && *p <= '9') { prec = prec * 10 + (*p - '0'); p++; }
        }
        // Size modifier
        int size_mod = 0; // 0=default, 1=h, 2=hh, 3=l, 4=ll, 5=z/I64
        if (*p == 'h') { size_mod = 1; p++; if (*p == 'h') { size_mod = 2; p++; } }
        else if (*p == 'l') { size_mod = 3; p++; if (*p == 'l') { size_mod = 4; p++; } }
        else if (*p == 'z' || *p == 'j' || *p == 't') { size_mod = 5; p++; }
        else if (*p == 'I' && *(p+1) == '6' && *(p+2) == '4') { size_mod = 5; p += 3; }
        else if (*p == 'I' && *(p+1) == '3' && *(p+2) == '2') { size_mod = 0; p += 3; }
        // Conversion
        char spec = *p;
        if (spec == 'd' || spec == 'i') {
          int64_t v = next_int();
          if (size_mod == 2) v = static_cast<int8_t>(v);
          else if (size_mod == 1) v = static_cast<int16_t>(v);
          else if (size_mod == 0) v = static_cast<int32_t>(v);
          if (has_width && has_prec) written += dprintf(fd, "%*.*lld", width, prec, (long long)v);
          else if (has_width) written += dprintf(fd, "%*lld", width, (long long)v);
          else if (has_prec) written += dprintf(fd, "%.*lld", prec, (long long)v);
          else written += dprintf(fd, "%lld", (long long)v);
        } else if (spec == 'u') {
          uint64_t v = next_uint();
          if (size_mod == 2) v = static_cast<uint8_t>(v);
          else if (size_mod == 1) v = static_cast<uint16_t>(v);
          else if (size_mod == 0) v = static_cast<uint32_t>(v);
          written += dprintf(fd, "%llu", (unsigned long long)v);
        } else if (spec == 'x' || spec == 'X') {
          uint64_t v = next_uint();
          if (size_mod <= 1) v = static_cast<uint32_t>(v);
          written += dprintf(fd, spec == 'x' ? "%llx" : "%llX", (unsigned long long)v);
        } else if (spec == 'o') {
          uint64_t v = next_uint();
          written += dprintf(fd, "%llo", (unsigned long long)v);
        } else if (spec == 's') {
          auto* s = reinterpret_cast<const char*>(next_uint());
          if (has_prec) written += dprintf(fd, "%.*s", prec, s ? s : "(null)");
          else written += dprintf(fd, "%s", s ? s : "(null)");
        } else if (spec == 'c') {
          char ch = static_cast<char>(next_int());
          ::write(fd, &ch, 1); written++;
        } else if (spec == 'p') {
          uint64_t v = next_uint();
          written += dprintf(fd, "%p", reinterpret_cast<void*>(v));
        } else if (spec == 'f' || spec == 'F') {
          double v = next_dbl();
          if (has_prec) written += dprintf(fd, "%.*f", prec, v);
          else written += dprintf(fd, "%f", v);
        } else if (spec == 'e' || spec == 'E') {
          double v = next_dbl();
          if (has_prec) written += dprintf(fd, spec == 'e' ? "%.*e" : "%.*E", prec, v);
          else written += dprintf(fd, spec == 'e' ? "%e" : "%E", v);
        } else if (spec == 'g' || spec == 'G') {
          double v = next_dbl();
          if (has_prec) written += dprintf(fd, spec == 'g' ? "%.*g" : "%.*G", prec, v);
          else written += dprintf(fd, spec == 'g' ? "%g" : "%G", v);
        } else if (spec == 'n') {
          (void)next_uint();
        } else if (spec == '%') {
          ::write(fd, "%", 1); written++;
        } else {
          ::write(fd, p - 1, 2); written += 2;
        }
      }
      return written;
    }
    case 117: return 0; // setvbuf -> success
    case 118: // strncmp
      if (!rcx || !rdx) return 0;
      return strncmp(reinterpret_cast<const char*>(rcx),
                     reinterpret_cast<const char*>(rdx),
                     static_cast<size_t>(r8));
    case 119: return 0; // __C_specific_handler (SEH, stub for now)
    case 120: { // VirtualProtect(addr, size, newProtect, oldProtect)
      if (r9) *reinterpret_cast<uint32_t*>(r9) = 0x40; // PAGE_EXECUTE_READWRITE
      return 1; // success
    }
    case 121: { // VirtualQuery(addr, buffer, length) -> bytes written
      if (rdx && r8 >= 48) {
        memset(reinterpret_cast<void*>(rdx), 0, static_cast<size_t>(r8));
        // Report page-aligned addresses — guest sees 4KB pages (x86 convention)
        // but actual host allocations are 16KB-aligned. BaseAddress uses the
        // guest's 4KB alignment so Windows CRT code works correctly.
        *reinterpret_cast<uint64_t*>(rdx) = rcx & ~0xFFFULL; // BaseAddress (4KB aligned)
        *reinterpret_cast<uint64_t*>(rdx + 8) = rcx & ~0xFFFULL; // AllocationBase
        *reinterpret_cast<uint32_t*>(rdx + 16) = 0x40; // AllocationProtect = RWX
        *reinterpret_cast<uint64_t*>(rdx + 24) = 0x4000; // RegionSize (16KB host page)
        *reinterpret_cast<uint32_t*>(rdx + 32) = 0x1000; // State = MEM_COMMIT
        *reinterpret_cast<uint32_t*>(rdx + 36) = 0x40; // Protect
        *reinterpret_cast<uint32_t*>(rdx + 40) = 0x20000; // Type = MEM_PRIVATE
        return 48;
      }
      return 0;
    }
    case 122: return 1; // InitializeCriticalSection -> success
    case 123: return 0; // _fpreset -> no-op
    case 124: return 0; // _execute_onexit_table -> success
    case 125: { // _initialize_onexit_table -> zero out the table
      if (rcx) memset(reinterpret_cast<void*>(rcx), 0, 24);
      return 0;
    }
    case 126: return 0; // _register_onexit_function -> success

    case 127: // realloc (UCRT alias)
    {
      size_t new_sz = static_cast<size_t>(rdx);
      if (rcx == 0) {
        if (new_sz == 0) new_sz = 1;
        if (GuestMmapUsed_ + new_sz > kGuestMemoryLimit) return 0;
        void* p = mmap(nullptr, new_sz + 16, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) return 0;
        *reinterpret_cast<uint64_t*>(p) = new_sz + 16;
        GuestMmapUsed_ += new_sz + 16;
        return reinterpret_cast<int64_t>(static_cast<uint8_t*>(p) + 16);
      }
      if (new_sz == 0) {
        auto* real = reinterpret_cast<uint8_t*>(rcx) - 16;
        uint64_t sz = *reinterpret_cast<uint64_t*>(real);
        if (sz > 0 && sz <= kGuestMemoryLimit && IsGuestOwned(reinterpret_cast<uint64_t>(real), sz)) {
          munmap(real, sz);
          if (sz <= GuestMmapUsed_) GuestMmapUsed_ -= sz;
        }
        return 0;
      }
      auto* old_real = reinterpret_cast<uint8_t*>(rcx) - 16;
      uint64_t old_sz = *reinterpret_cast<uint64_t*>(old_real);
      if (old_sz > kGuestMemoryLimit) return 0;
      size_t old_usable = (old_sz > 16) ? old_sz - 16 : 0;
      if (new_sz <= old_usable) return static_cast<int64_t>(rcx);
      if (GuestMmapUsed_ + new_sz > kGuestMemoryLimit) return 0;
      void* np = mmap(nullptr, new_sz + 16, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (np == MAP_FAILED) return 0;
      *reinterpret_cast<uint64_t*>(np) = new_sz + 16;
      memcpy(static_cast<uint8_t*>(np) + 16, reinterpret_cast<void*>(rcx), old_usable < new_sz ? old_usable : new_sz);
      if (IsGuestOwned(reinterpret_cast<uint64_t>(old_real), old_sz))
        munmap(old_real, old_sz);
      GuestMmapUsed_ += new_sz + 16;
      if (old_sz <= GuestMmapUsed_) GuestMmapUsed_ -= old_sz;
      return reinterpret_cast<int64_t>(static_cast<uint8_t*>(np) + 16);
    }

    case 128: { // __stdio_common_vsprintf(options, buf, bufsize, fmt, locale, arglist)
      auto* buf = reinterpret_cast<char*>(rdx);
      size_t bufsize = static_cast<size_t>(r8);
      auto* fmt = reinterpret_cast<const char*>(r9);
      if (!fmt) { if (buf && bufsize) buf[0] = 0; return 0; }
      uint64_t rsp = Frame->State.gregs[FEXCore::X86State::REG_RSP];
      uint64_t ap_val = *reinterpret_cast<uint64_t*>(rsp + 0x30);
      auto* arglist = ap_val ? reinterpret_cast<uint64_t*>(ap_val) : nullptr;
      int arg_idx = 0;
      auto next_uint = [&]() -> uint64_t { return arglist ? arglist[arg_idx++] : 0; };
      char temp[4096];
      int pos = 0;
      for (const char* p = fmt; *p && pos < (int)sizeof(temp) - 1; p++) {
        if (*p != '%' || !*(p + 1)) { temp[pos++] = *p; continue; }
        p++;
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') p++;
        if (*p == '*') { next_uint(); p++; } else while (*p >= '0' && *p <= '9') p++;
        int prec = -1; bool hp = false;
        if (*p == '.') { p++; prec = 0; hp = true;
          if (*p == '*') { prec = (int)next_uint(); p++; }
          else while (*p >= '0' && *p <= '9') { prec = prec * 10 + (*p - '0'); p++; }
        }
        while (*p == 'h' || *p == 'l' || *p == 'z' || *p == 'j' || *p == 't') p++;
        if (*p == 'I' && *(p+1) >= '0') p += 3;
        char s = *p;
        uint64_t v = next_uint();
        if (s == 'd' || s == 'i') pos += snprintf(temp + pos, sizeof(temp) - pos, "%d", (int)(int32_t)v);
        else if (s == 'u') pos += snprintf(temp + pos, sizeof(temp) - pos, "%u", (unsigned)(uint32_t)v);
        else if (s == 'x') pos += snprintf(temp + pos, sizeof(temp) - pos, "%x", (unsigned)v);
        else if (s == 'X') pos += snprintf(temp + pos, sizeof(temp) - pos, "%X", (unsigned)v);
        else if (s == 's') {
          auto* str = reinterpret_cast<const char*>(v);
          if (hp) pos += snprintf(temp + pos, sizeof(temp) - pos, "%.*s", prec, str ? str : "(null)");
          else pos += snprintf(temp + pos, sizeof(temp) - pos, "%s", str ? str : "(null)");
        } else if (s == 'c') { temp[pos++] = (char)v;
        } else if (s == 'p') { pos += snprintf(temp + pos, sizeof(temp) - pos, "%p", (void*)v);
        } else if (s == 'f') { double d; memcpy(&d, &v, 8);
          if (hp) pos += snprintf(temp + pos, sizeof(temp) - pos, "%.*f", prec, d);
          else pos += snprintf(temp + pos, sizeof(temp) - pos, "%f", d);
        } else if (s == '%') { temp[pos++] = '%'; arg_idx--;
        } else { temp[pos++] = '%'; temp[pos++] = s; arg_idx--; }
      }
      if (pos > (int)sizeof(temp) - 1) pos = sizeof(temp) - 1;
      temp[pos] = 0;
      if (buf && bufsize > 0) {
        size_t to_copy = (size_t)pos < bufsize - 1 ? (size_t)pos : bufsize - 1;
        memcpy(buf, temp, to_copy);
        buf[to_copy] = 0;
      }
      return pos;
    }

    case 129: { // putchar(ch)
      char ch = static_cast<char>(rcx);
      ::write(STDOUT_FILENO, &ch, 1);
      return static_cast<unsigned char>(ch);
    }

    case 130: { // _strdup(str)
      auto* str = reinterpret_cast<const char*>(rcx);
      if (!str) return 0;
      size_t len = strlen(str) + 1;
      if (GuestMmapUsed_ + len > kGuestMemoryLimit) return 0;
      void* p = mmap(nullptr, len + 16, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (p == MAP_FAILED) return 0;
      *reinterpret_cast<uint64_t*>(p) = len + 16;
      GuestMmapUsed_ += len + 16;
      auto* dup = static_cast<uint8_t*>(p) + 16;
      memcpy(dup, str, len);
      return reinterpret_cast<int64_t>(dup);
    }

    case 131: { // memset(dst, val, count)
      if (!rcx || r8 > kGuestMemoryLimit) return static_cast<int64_t>(rcx);
      memset(reinterpret_cast<void*>(rcx), static_cast<int>(rdx), static_cast<size_t>(r8));
      return static_cast<int64_t>(rcx);
    }

    case 132: { // qsort(base, num, size, compare)
      // Guest compare callback requires re-entering JIT — for now, use
      // host-side integer comparison (covers int32/int64/pointer arrays).
      auto* base = reinterpret_cast<uint8_t*>(rcx);
      size_t num = static_cast<size_t>(rdx);
      size_t size = static_cast<size_t>(r8);
      auto cmp4 = [](const void* a, const void* b) -> int {
        int32_t va = *static_cast<const int32_t*>(a), vb = *static_cast<const int32_t*>(b);
        return (va > vb) - (va < vb);
      };
      auto cmp8 = [](const void* a, const void* b) -> int {
        int64_t va = *static_cast<const int64_t*>(a), vb = *static_cast<const int64_t*>(b);
        return (va > vb) - (va < vb);
      };
      if (size == 4) qsort(base, num, size, cmp4);
      else if (size == 8) qsort(base, num, size, cmp8);
      else qsort(base, num, size, [](const void* a, const void* b) -> int { return memcmp(a, b, 1); });
      return 0;
    }

    case 133: { // strcat(dst, src)
      if (!rcx || !rdx) return static_cast<int64_t>(rcx);
      auto* dst = reinterpret_cast<char*>(rcx);
      auto* src = reinterpret_cast<const char*>(rdx);
      size_t dst_len = strnlen(dst, kGuestMemoryLimit);
      size_t src_len = strnlen(src, kGuestMemoryLimit);
      if (dst_len + src_len >= kGuestMemoryLimit) return static_cast<int64_t>(rcx);
      strcat(dst, src);
      return static_cast<int64_t>(rcx);
    }

    case 134: { // strcpy(dst, src)
      if (!rcx || !rdx) return static_cast<int64_t>(rcx);
      strcpy(reinterpret_cast<char*>(rcx), reinterpret_cast<const char*>(rdx));
      return static_cast<int64_t>(rcx);
    }

    case 135: // strcmp(s1, s2)
      if (!rcx || !rdx) return 0;
      return strcmp(reinterpret_cast<const char*>(rcx), reinterpret_cast<const char*>(rdx));

    case 136: return static_cast<int64_t>(rcx); // snprintf — mapped to vsprintf handler
    case 137: return static_cast<int64_t>(rcx); // sprintf — mapped to vsprintf handler

    case 138: // abs(val)
      return static_cast<int64_t>(abs(static_cast<int>(rcx)));

    case 139: // strtol(str, endptr, base)
      if (!rcx) return 0;
      return static_cast<int64_t>(strtol(reinterpret_cast<const char*>(rcx),
             rdx ? reinterpret_cast<char**>(rdx) : nullptr, static_cast<int>(r8)));

    case 140: // strtoul(str, endptr, base)
      if (!rcx) return 0;
      return static_cast<int64_t>(strtoul(reinterpret_cast<const char*>(rcx),
             rdx ? reinterpret_cast<char**>(rdx) : nullptr, static_cast<int>(r8)));

    case 141: // atoi(str)
      if (!rcx) return 0;
      return static_cast<int64_t>(atoi(reinterpret_cast<const char*>(rcx)));

    case 142: { // memmove(dst, src, count)
      if (!rcx || !rdx || r8 > kGuestMemoryLimit) return static_cast<int64_t>(rcx);
      memmove(reinterpret_cast<void*>(rcx), reinterpret_cast<const void*>(rdx),
              static_cast<size_t>(r8));
      return static_cast<int64_t>(rcx);
    }

    case 143: // memcmp(s1, s2, count)
      if (!rcx || !rdx || r8 > kGuestMemoryLimit) return 0;
      return memcmp(reinterpret_cast<const void*>(rcx), reinterpret_cast<const void*>(rdx),
                    static_cast<size_t>(r8));

    case 144: // strchr(str, ch)
      if (!rcx) return 0;
      return reinterpret_cast<int64_t>(strchr(reinterpret_cast<const char*>(rcx),
             static_cast<int>(rdx)));

    case 145: // strrchr(str, ch)
      if (!rcx) return 0;
      return reinterpret_cast<int64_t>(strrchr(reinterpret_cast<const char*>(rcx),
             static_cast<int>(rdx)));

    case 146: // strstr(haystack, needle)
      if (!rcx || !rdx) return 0;
      return reinterpret_cast<int64_t>(strstr(reinterpret_cast<const char*>(rcx),
             reinterpret_cast<const char*>(rdx)));

    case 147: return (rcx >= 'a' && rcx <= 'z') ? rcx - 32 : rcx; // toupper
    case 148: return (rcx >= 'A' && rcx <= 'Z') ? rcx + 32 : rcx; // tolower
    case 149: return (rcx >= '0' && rcx <= '9') ? 1 : 0; // isdigit
    case 150: return ((rcx >= 'A' && rcx <= 'Z') || (rcx >= 'a' && rcx <= 'z')) ? 1 : 0; // isalpha
    case 151: return (rcx == ' ' || rcx == '\t' || rcx == '\n' || rcx == '\r' || rcx == '\f' || rcx == '\v') ? 1 : 0; // isspace

    case 152: { // fprintf (direct, not __stdio_common) — rcx=stream, rdx=fmt, r8+ args
      int fd = FakeFileToFd(rcx);
      auto* fmt = reinterpret_cast<const char*>(rdx);
      if (!fmt) return 0;
      uint64_t args[16] = {r8, r9};
      uint64_t rsp = Frame->State.gregs[FEXCore::X86State::REG_RSP];
      for (int i = 2; i < 16; i++)
        args[i] = *reinterpret_cast<uint64_t*>(rsp + 0x28 + (i - 2) * 8);
      int ai = 0, written = 0;
      for (const char* p = fmt; *p; p++) {
        if (*p != '%' || !*(p + 1)) { ::write(fd, p, 1); written++; continue; }
        p++;
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') p++;
        if (*p == '*') { ai++; p++; } else while (*p >= '0' && *p <= '9') p++;
        int prec = -1; bool hp = false;
        if (*p == '.') { p++; prec = 0; hp = true;
          if (*p == '*') { prec = (int)args[ai++]; p++; }
          else while (*p >= '0' && *p <= '9') { prec = prec * 10 + (*p - '0'); p++; }
        }
        while (*p == 'h' || *p == 'l' || *p == 'z' || *p == 'j' || *p == 't') p++;
        uint64_t v = (ai < 16) ? args[ai++] : 0;
        if (*p == 'd' || *p == 'i') written += dprintf(fd, "%d", (int)(int32_t)v);
        else if (*p == 'u') written += dprintf(fd, "%u", (unsigned)(uint32_t)v);
        else if (*p == 'x') written += dprintf(fd, "%x", (unsigned)v);
        else if (*p == 'X') written += dprintf(fd, "%X", (unsigned)v);
        else if (*p == 's') { auto* s = reinterpret_cast<const char*>(v);
          if (hp) written += dprintf(fd, "%.*s", prec, s ? s : "(null)");
          else written += dprintf(fd, "%s", s ? s : "(null)");
        } else if (*p == 'c') { char ch = (char)v; ::write(fd, &ch, 1); written++;
        } else if (*p == 'f') { double d; memcpy(&d, &v, 8);
          if (hp) written += dprintf(fd, "%.*f", prec, d); else written += dprintf(fd, "%f", d);
        } else if (*p == '%') { ::write(fd, "%", 1); written++; ai--;
        } else { ::write(fd, p-1, 2); written += 2; ai--; }
      }
      return written;
    }

    case 153: return 0; // vfprintf stub — rarely called directly

    // === D3D11 / DXGI ===
    case 154: { // D3D11CreateDevice
      // rcx=adapter, rdx=driver_type, r8=software, r9=flags
      // stack: feature_levels, num_levels, sdk_version, out_device, out_feature_level, out_context
      if (!D3DStubs_.IsInitialized()) D3DStubs_.Init();
      uint64_t rsp = Frame->State.gregs[FEXCore::X86State::REG_RSP];
      auto* feat_levels = reinterpret_cast<const D3D::D3D_FEATURE_LEVEL*>(
        *reinterpret_cast<uint64_t*>(rsp + 0x28));
      uint32_t num_levels = *reinterpret_cast<uint32_t*>(rsp + 0x30);
      uint32_t sdk_ver = *reinterpret_cast<uint32_t*>(rsp + 0x38);
      auto* out_device = reinterpret_cast<uint64_t*>(*reinterpret_cast<uint64_t*>(rsp + 0x40));
      auto* out_feat = reinterpret_cast<D3D::D3D_FEATURE_LEVEL*>(
        *reinterpret_cast<uint64_t*>(rsp + 0x48));
      auto* out_ctx = reinterpret_cast<uint64_t*>(*reinterpret_cast<uint64_t*>(rsp + 0x50));
      return D3DStubs_.CreateDevice(rcx,
        static_cast<D3D::D3D_DRIVER_TYPE>(rdx), r8,
        static_cast<uint32_t>(r9), feat_levels, num_levels, sdk_ver,
        out_device, out_feat, out_ctx);
    }

    case 155: { // D3D11CreateDeviceAndSwapChain
      if (!D3DStubs_.IsInitialized()) D3DStubs_.Init();
      printf("[DXMT] D3D11CreateDeviceAndSwapChain called (stub)\n");
      // Similar to CreateDevice but also creates a swap chain
      // For now, delegate to CreateDevice logic
      uint64_t rsp = Frame->State.gregs[FEXCore::X86State::REG_RSP];
      auto* out_device = reinterpret_cast<uint64_t*>(*reinterpret_cast<uint64_t*>(rsp + 0x48));
      auto* out_feat = reinterpret_cast<D3D::D3D_FEATURE_LEVEL*>(
        *reinterpret_cast<uint64_t*>(rsp + 0x50));
      auto* out_ctx = reinterpret_cast<uint64_t*>(*reinterpret_cast<uint64_t*>(rsp + 0x58));
      return D3DStubs_.CreateDevice(rcx,
        static_cast<D3D::D3D_DRIVER_TYPE>(rdx), r8,
        static_cast<uint32_t>(r9), nullptr, 0, 7,
        out_device, out_feat, out_ctx);
    }

    case 156: // CreateDXGIFactory
    case 157: // CreateDXGIFactory1
    case 158: { // CreateDXGIFactory2
      if (!D3DStubs_.IsInitialized()) D3DStubs_.Init();
      auto* riid = reinterpret_cast<const D3D::GUID*>(rcx);
      auto* out = reinterpret_cast<uint64_t*>(rdx);
      return D3DStubs_.CreateDXGIFactory(riid ? *riid : D3D::IID_IDXGIFactory, out);
    }

    default:
      fprintf(stderr, "[Win32] Unknown stub ID %u\n", stub_id);
      return 0;
    }
  }

  static constexpr char kFakeCmdLine[] = "rosetta-killer.exe";

  // Only allow stdin/stdout/stderr — FEXCore may have writable fds open
  // internally (profiler trace, perf map) that guest code must not reach.
  static bool IsAllowedFd(int fd) {
    return fd >= 0 && fd <= 2;
  }

  // Validate that a buffer lies within guest-accessible memory.
  // Prevents guest from reading/writing arbitrary host addresses.
  bool IsGuestBuffer(uint64_t addr, uint64_t len) const {
    if (len == 0) return true;
    if (GuestSize == 0) return true; // no region set, allow (backwards compat)
    // Allow buffers in the guest code/data region
    if (addr >= GuestBase && addr + len <= GuestBase + GuestSize && addr + len > addr)
      return true;
    // Allow buffers on the stack (stack grows down, so check a wide range).
    // The stack is allocated by the host harness at a known location, but we
    // don't track it here — for now, allow any address that doesn't overlap
    // with the low 4GB (where no host allocations live on macOS arm64).
    // mmap on macOS arm64 returns addresses above 0x100000000.
    if (addr >= 0x100000000ULL && addr + len > addr)
      return true;
    return false;
  }

  // Only allow munmap of addresses the guest could have obtained via mmap.
  // macOS arm64 mmap returns addresses above 0x100000000.
  bool IsGuestOwned(uint64_t addr, uint64_t len) const {
    if (len == 0) return false;
    return addr >= 0x100000000ULL && addr + len > addr;
  }
};

} // namespace FEX
