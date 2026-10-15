cmake_minimum_required(VERSION 3.14)
project(RosettaKiller CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Path to FEX build
set(FEX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../FEX")
set(FEX_BUILD "${FEX_ROOT}/build_macos")

add_executable(rosetta-killer
  src/main.cpp
  src/macos_host_features.cpp
)

target_include_directories(rosetta-killer PRIVATE
  ${FEX_ROOT}/FEXCore/include
  ${FEX_ROOT}/FEXCore/Source
  ${FEX_ROOT}/FEXHeaderUtils
  ${FEX_ROOT}/CodeEmitter
  ${FEX_ROOT}/External/fmt/include
  ${FEX_ROOT}/External/xxhash
  ${FEX_ROOT}/External/unordered_dense/include
  ${FEX_ROOT}/External/SoftFloat-3e/include
  ${FEX_ROOT}/External/SoftFloat-3e/include/SoftFloat-3e
  ${FEX_ROOT}/External/cephes/include
  ${FEX_BUILD}/include
  ${FEX_BUILD}/generated
  ${FEX_BUILD}/FEXCore/Source
)

target_compile_definitions(rosetta-killer PRIVATE
  ARCHITECTURE_arm64=1
  JIT_ARM64
  FEX_HAS_PRESERVE_ALL_ATTR=1
  FEXCORE_HAS_PRESERVE_ALL_ATTR=1
  FEX_DISABLE_TELEMETRY=1
  _LARGEFILE64_SOURCE
)

target_compile_options(rosetta-killer PRIVATE
  -DFEX_PRESERVE_ALL_ATTR=__attribute__\(\(preserve_all\)\)
  -DFEXCORE_PRESERVE_ALL_ATTR=__attribute__\(\(preserve_all\)\)
)

target_compile_options(rosetta-killer PRIVATE
  -fno-strict-aliasing
  -fno-exceptions
  -mcpu=apple-m1
)

# Link FEXCore and its dependencies
set(FEX_LIBS
  ${FEX_BUILD}/FEXCore/Source/libFEXCore.a
  ${FEX_BUILD}/FEXCore/Source/libFEXCore_Base.a
  ${FEX_BUILD}/FEXCore/Source/libJemallocLibs.a
  ${FEX_BUILD}/External/fmt/libfmt.a
  ${FEX_BUILD}/External/xxhash/cmake_unofficial/libxxhash.a
  ${FEX_BUILD}/External/cephes/libcephes_128bit.a
  ${FEX_BUILD}/External/SoftFloat-3e/libsoftfloat_3e.a
)

target_link_libraries(rosetta-killer PRIVATE
  ${FEX_LIBS}
  pthread
  objc
)

# FEXCore-Wine bridge shared library
# Wine's ntdll.so loads this via dlopen to get x86_64 JIT translation
add_library(fexbridge SHARED
  src/fexcore_wine_bridge.cpp
  src/macos_host_features.cpp
)

target_include_directories(fexbridge PRIVATE
  ${FEX_ROOT}/FEXCore/include
  ${FEX_ROOT}/FEXCore/Source
  ${FEX_ROOT}/FEXHeaderUtils
  ${FEX_ROOT}/CodeEmitter
  ${FEX_ROOT}/External/fmt/include
  ${FEX_ROOT}/External/xxhash
  ${FEX_ROOT}/External/unordered_dense/include
  ${FEX_ROOT}/External/SoftFloat-3e/include
  ${FEX_ROOT}/External/SoftFloat-3e/include/SoftFloat-3e
  ${FEX_ROOT}/External/cephes/include
  ${FEX_BUILD}/include
  ${FEX_BUILD}/generated
  ${FEX_BUILD}/FEXCore/Source
)

target_compile_definitions(fexbridge PRIVATE
  ARCHITECTURE_arm64=1
  JIT_ARM64
  FEX_HAS_PRESERVE_ALL_ATTR=1
  FEXCORE_HAS_PRESERVE_ALL_ATTR=1
  FEX_DISABLE_TELEMETRY=1
  _LARGEFILE64_SOURCE
)

target_compile_options(fexbridge PRIVATE
  -DFEX_PRESERVE_ALL_ATTR=__attribute__\(\(preserve_all\)\)
  -DFEXCORE_PRESERVE_ALL_ATTR=__attribute__\(\(preserve_all\)\)
  -fno-strict-aliasing
  -fno-exceptions
  -mcpu=apple-m1
)

target_link_libraries(fexbridge PRIVATE
  ${FEX_LIBS}
  pthread
  objc
)

set_target_properties(fexbridge PROPERTIES
  OUTPUT_NAME "fexbridge"
  SUFFIX ".dylib"
)

# Bridge test binary — validates FEXCore JIT works through the bridge API
add_executable(test_bridge
  src/test_bridge.cpp
  src/fexcore_wine_bridge.cpp
  src/macos_host_features.cpp
)

target_include_directories(test_bridge PRIVATE
  ${FEX_ROOT}/FEXCore/include
  ${FEX_ROOT}/FEXCore/Source
  ${FEX_ROOT}/FEXHeaderUtils
  ${FEX_ROOT}/CodeEmitter
  ${FEX_ROOT}/External/fmt/include
  ${FEX_ROOT}/External/xxhash
  ${FEX_ROOT}/External/unordered_dense/include
  ${FEX_ROOT}/External/SoftFloat-3e/include
  ${FEX_ROOT}/External/SoftFloat-3e/include/SoftFloat-3e
  ${FEX_ROOT}/External/cephes/include
  ${FEX_BUILD}/include
  ${FEX_BUILD}/generated
  ${FEX_BUILD}/FEXCore/Source
)

target_compile_definitions(test_bridge PRIVATE
  ARCHITECTURE_arm64=1
  JIT_ARM64
  FEX_HAS_PRESERVE_ALL_ATTR=1
  FEXCORE_HAS_PRESERVE_ALL_ATTR=1
  FEX_DISABLE_TELEMETRY=1
  _LARGEFILE64_SOURCE
)

target_compile_options(test_bridge PRIVATE
  -DFEX_PRESERVE_ALL_ATTR=__attribute__\(\(preserve_all\)\)
  -DFEXCORE_PRESERVE_ALL_ATTR=__attribute__\(\(preserve_all\)\)
  -fno-strict-aliasing
  -fno-exceptions
  -mcpu=apple-m1
)

target_link_libraries(test_bridge PRIVATE
  ${FEX_LIBS}
  pthread
  objc
)
