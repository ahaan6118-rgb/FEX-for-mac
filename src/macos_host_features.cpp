// macOS Apple Silicon host feature detection
// Replaces FEX's Linux-specific HostFeatures detection with sysctl-based queries

#include <FEXCore/Core/HostFeatures.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstring>

namespace FEX {

static int GetNCPUs() {
  int ncpu = 1;
  size_t len = sizeof(ncpu);
  sysctlbyname("hw.ncpu", &ncpu, &len, nullptr, 0);
  return ncpu;
}

static bool HasSysctlFeature(const char* name) {
  int val = 0;
  size_t len = sizeof(val);
  if (sysctlbyname(name, &val, &len, nullptr, 0) == 0) {
    return val != 0;
  }
  return false;
}

static int GetCacheLineSize() {
  int cls = 64;
  size_t len = sizeof(cls);
  sysctlbyname("hw.cachelinesize", &cls, &len, nullptr, 0);
  return cls;
}

FEXCore::HostFeatures BuildHostFeatures() {
  FEXCore::HostFeatures Features {};

  // FEX stores DminLine from CTR_EL0: cache line size == 4 << DCacheLineLog2.
  // CTR_EL0 traps at EL0 on macOS, so use sysctl instead.
  {
    int cacheLineSize = GetCacheLineSize();
    if (cacheLineSize < 4) {
      cacheLineSize = 64;
    }
    int encoded = 0;
    while ((4 << encoded) < cacheLineSize && encoded < 15) {
      encoded++;
    }
    Features.DCacheLineLog2 = encoded;
  }

  Features.SupportsCacheMaintenanceOps = true;

  // Apple Silicon feature detection via sysctl
  Features.SupportsAES = HasSysctlFeature("hw.optional.arm.FEAT_AES");
  Features.SupportsCRC = HasSysctlFeature("hw.optional.arm.FEAT_CRC32");
  Features.SupportsAtomics = HasSysctlFeature("hw.optional.arm.FEAT_LSE");
  Features.SupportsRCPC = HasSysctlFeature("hw.optional.arm.FEAT_LRCPC");
  Features.SupportsSHA = HasSysctlFeature("hw.optional.arm.FEAT_SHA256");
  Features.SupportsPMULL_128Bit = HasSysctlFeature("hw.optional.arm.FEAT_PMULL");
  Features.SupportsFCMA = HasSysctlFeature("hw.optional.arm.FEAT_FCMA");
  Features.SupportsFlagM = HasSysctlFeature("hw.optional.arm.FEAT_FlagM");
  Features.SupportsFlagM2 = HasSysctlFeature("hw.optional.arm.FEAT_FlagM2");
  Features.SupportsDotProd = HasSysctlFeature("hw.optional.arm.FEAT_DotProd");
  Features.SupportsFRINTTS = HasSysctlFeature("hw.optional.arm.FEAT_FRINTTS");
  Features.SupportsI8MM = HasSysctlFeature("hw.optional.arm.FEAT_I8MM");

  // Apple Silicon doesn't expose SVE
  Features.SupportsSVE128 = false;
  Features.SupportsSVE256 = false;
  Features.SupportsAVX = false;

  // RAND not available on Apple Silicon
  Features.SupportsRAND = false;
  Features.SupportsCLZERO = false;

  // preserve_all ABI support (Apple Clang supports this)
#if FEX_HAS_PRESERVE_ALL_ATTR
  Features.SupportsPreserveAllABI = true;
#else
  Features.SupportsPreserveAllABI = false;
#endif

  Features.SupportsFloatExceptions = true;

  // Guest personality, not host OS. OpcodeDispatcher uses this to decide Linux
  // (INT 0x80) vs Windows (INT 0x2E) syscall entry. These PoC blobs are SysV
  // x86_64 userspace, so Linux is the correct FEX personality on macOS too.
  Features.HostType = FEXCore::HostFeatures::HostTypeEnum::Linux;

  // Populate MIDR info (Apple doesn't expose MIDR, use placeholder)
  int ncpus = GetNCPUs();
  Features.CPUMIDRs.resize(ncpus);
  for (int i = 0; i < ncpus; i++) {
    Features.CPUMIDRs[i] = 0x610F0000;
  }

  Features.ProcessPID = getpid();

  return Features;
}

} // namespace FEX
