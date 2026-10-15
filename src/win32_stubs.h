// Win32 API stub implementations for PE import resolution
// Each stub is a small x86_64 thunk that triggers a host callback via syscall
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <map>
#include <string>

namespace Win32 {

// Stub function IDs — used in the syscall handler to dispatch
enum StubID : uint32_t {
  STUB_ExitProcess = 1,
  STUB_GetStdHandle,
  STUB_WriteFile,
  STUB_WriteConsoleA,
  STUB_GetLastError,
  STUB_SetLastError,
  STUB_GetCommandLineA,
  STUB_GetModuleHandleA,
  STUB_GetProcessHeap,
  STUB_HeapAlloc,
  STUB_HeapFree,
  STUB_VirtualAlloc,
  STUB_VirtualFree,
  STUB_GetCurrentProcessId,
  STUB_GetCurrentThreadId,
  STUB_QueryPerformanceCounter,
  // New stubs for real .exe support
  STUB_CloseHandle,
  STUB_CreateFileA,
  STUB_CreateFileW,
  STUB_ReadFile,
  STUB_FlushFileBuffers,
  STUB_GetFileType,
  STUB_SetHandleCount,
  STUB_GetStartupInfoA,
  STUB_GetStartupInfoW,
  STUB_GetModuleFileNameA,
  STUB_GetModuleFileNameW,
  STUB_GetEnvironmentStringsW,
  STUB_FreeEnvironmentStringsW,
  STUB_GetEnvironmentVariableA,
  STUB_GetEnvironmentVariableW,
  STUB_WideCharToMultiByte,
  STUB_MultiByteToWideChar,
  STUB_GetCurrentProcess,
  STUB_TerminateProcess,
  STUB_SetUnhandledExceptionFilter,
  STUB_UnhandledExceptionFilter,
  STUB_IsDebuggerPresent,
  STUB_IsProcessorFeaturePresent,
  STUB_GetSystemTimeAsFileTime,
  STUB_GetTickCount,
  STUB_QueryPerformanceFrequency,
  STUB_GetConsoleMode,
  STUB_SetConsoleMode,
  STUB_GetConsoleCP,
  STUB_GetConsoleOutputCP,
  STUB_SetConsoleCtrlHandler,
  STUB_GetACP,
  STUB_GetOEMCP,
  STUB_GetCPInfo,
  STUB_GetStringTypeW,
  STUB_LCMapStringW,
  STUB_HeapSize,
  STUB_HeapReAlloc,
  STUB_GetProcAddress,
  STUB_LoadLibraryA,
  STUB_GetModuleHandleW,
  STUB_RtlUnwind,
  STUB_InitializeCriticalSectionAndSpinCount,
  STUB_EnterCriticalSection,
  STUB_LeaveCriticalSection,
  STUB_DeleteCriticalSection,
  STUB_TlsAlloc,
  STUB_TlsGetValue,
  STUB_TlsSetValue,
  STUB_TlsFree,
  STUB_Sleep,
  STUB_GetSystemInfo,
  // msvcrt.dll CRT startup
  STUB_msvcrt_getmainargs,
  STUB_msvcrt_set_app_type,
  STUB_msvcrt_initterm,
  STUB_msvcrt_initterm_e,
  STUB_msvcrt_exit,
  STUB_msvcrt_cexit,
  STUB_msvcrt_c_exit,
  STUB_msvcrt_amsg_exit,
  STUB_msvcrt_setusermatherr,
  STUB_msvcrt_configthreadlocale,
  STUB_msvcrt_set_new_mode,
  // msvcrt.dll I/O
  STUB_msvcrt_printf,
  STUB_msvcrt_fprintf,
  STUB_msvcrt_puts,
  STUB_msvcrt_putchar,
  STUB_msvcrt_fwrite,
  STUB_msvcrt_fflush,
  STUB_msvcrt_iob_func,
  STUB_msvcrt_fileno,
  STUB_msvcrt_setmode,
  // msvcrt.dll memory
  STUB_msvcrt_malloc,
  STUB_msvcrt_free,
  STUB_msvcrt_calloc,
  STUB_msvcrt_realloc,
  // msvcrt.dll string
  STUB_msvcrt_strlen,
  STUB_msvcrt_strcpy,
  STUB_msvcrt_strncpy,
  STUB_msvcrt_strcmp,
  STUB_msvcrt_memset,
  STUB_msvcrt_memcpy,
  STUB_msvcrt_memmove,
  // msvcrt.dll misc
  STUB_msvcrt_fmode,
  STUB_msvcrt_commode,
  STUB_msvcrt_p_argc,
  STUB_msvcrt_p_argv,
  STUB_msvcrt_errno,
  STUB_msvcrt_abort,
  STUB_msvcrt_signal,
  // UCRT additions (api-ms-win-crt-* DLLs used by modern MinGW)
  STUB_ucrt_configure_narrow_argv,
  STUB_ucrt_initialize_narrow_environment,
  STUB_ucrt_crt_atexit,
  STUB_ucrt_set_invalid_parameter_handler,
  STUB_ucrt_p_environ,
  STUB_ucrt_exit,        // _exit (not exit)
  STUB_ucrt_acrt_iob_func,
  STUB_ucrt_p_commode,
  STUB_ucrt_p_fmode,
  STUB_ucrt_stdio_common_vfprintf,
  STUB_ucrt_setvbuf,
  STUB_ucrt_strncmp,
  STUB_ucrt_C_specific_handler,
  STUB_VirtualProtect,
  STUB_VirtualQuery,
  STUB_InitializeCriticalSection,
  STUB_ucrt_fpreset,
  STUB_ucrt_execute_onexit_table,
  STUB_ucrt_initialize_onexit_table,
  STUB_ucrt_register_onexit_function,
  STUB_ucrt_realloc,
  STUB_ucrt_vsprintf,
  STUB_ucrt_putchar,
  STUB_ucrt_strdup,
  STUB_ucrt_memset,
  STUB_ucrt_qsort,
  STUB_ucrt_strcat,
  STUB_ucrt_strcpy,
  STUB_ucrt_strcmp,
  STUB_ucrt_snprintf,
  STUB_ucrt_sprintf,
  STUB_ucrt_abs,
  STUB_ucrt_strtol,
  STUB_ucrt_strtoul,
  STUB_ucrt_atoi,
  STUB_ucrt_memmove,
  STUB_ucrt_memcmp,
  STUB_ucrt_strchr,
  STUB_ucrt_strrchr,
  STUB_ucrt_strstr,
  STUB_ucrt_toupper,
  STUB_ucrt_tolower,
  STUB_ucrt_isdigit,
  STUB_ucrt_isalpha,
  STUB_ucrt_isspace,
  STUB_ucrt_fprintf,
  STUB_ucrt_vfprintf,
  // D3D11 / DXGI
  STUB_D3D11CreateDevice,
  STUB_D3D11CreateDeviceAndSwapChain,
  STUB_CreateDXGIFactory,
  STUB_CreateDXGIFactory1,
  STUB_CreateDXGIFactory2,
  STUB_UNKNOWN,
};

// Each stub thunk is a small x86_64 code sequence:
//   mov r10, rcx                  (3 bytes)  — save first arg (syscall clobbers RCX)
//   mov eax, <SYSCALL_NR_WIN32>   (5 bytes)  — our custom syscall number (0x1000 + stub_id)
//   syscall                        (2 bytes)
//   ret                            (1 byte)
// Total: 11 bytes, padded to 16 for alignment
static constexpr size_t kThunkSize = 16;
static constexpr uint32_t kWin32SyscallBase = 0x1000;

struct StubEntry {
  const char* dll_name;
  const char* func_name;
  StubID id;
};

static const StubEntry kStubs[] = {
  // kernel32.dll
  {"kernel32.dll", "ExitProcess",                         STUB_ExitProcess},
  {"kernel32.dll", "GetStdHandle",                        STUB_GetStdHandle},
  {"kernel32.dll", "WriteFile",                           STUB_WriteFile},
  {"kernel32.dll", "WriteConsoleA",                       STUB_WriteConsoleA},
  {"kernel32.dll", "GetLastError",                        STUB_GetLastError},
  {"kernel32.dll", "SetLastError",                        STUB_SetLastError},
  {"kernel32.dll", "GetCommandLineA",                     STUB_GetCommandLineA},
  {"kernel32.dll", "GetModuleHandleA",                    STUB_GetModuleHandleA},
  {"kernel32.dll", "GetProcessHeap",                      STUB_GetProcessHeap},
  {"kernel32.dll", "HeapAlloc",                           STUB_HeapAlloc},
  {"kernel32.dll", "HeapFree",                            STUB_HeapFree},
  {"kernel32.dll", "VirtualAlloc",                        STUB_VirtualAlloc},
  {"kernel32.dll", "VirtualFree",                         STUB_VirtualFree},
  {"kernel32.dll", "GetCurrentProcessId",                 STUB_GetCurrentProcessId},
  {"kernel32.dll", "GetCurrentThreadId",                  STUB_GetCurrentThreadId},
  {"kernel32.dll", "QueryPerformanceCounter",             STUB_QueryPerformanceCounter},
  {"kernel32.dll", "CloseHandle",                         STUB_CloseHandle},
  {"kernel32.dll", "CreateFileA",                         STUB_CreateFileA},
  {"kernel32.dll", "CreateFileW",                         STUB_CreateFileW},
  {"kernel32.dll", "ReadFile",                            STUB_ReadFile},
  {"kernel32.dll", "FlushFileBuffers",                    STUB_FlushFileBuffers},
  {"kernel32.dll", "GetFileType",                         STUB_GetFileType},
  {"kernel32.dll", "SetHandleCount",                      STUB_SetHandleCount},
  {"kernel32.dll", "GetStartupInfoA",                     STUB_GetStartupInfoA},
  {"kernel32.dll", "GetStartupInfoW",                     STUB_GetStartupInfoW},
  {"kernel32.dll", "GetModuleFileNameA",                  STUB_GetModuleFileNameA},
  {"kernel32.dll", "GetModuleFileNameW",                  STUB_GetModuleFileNameW},
  {"kernel32.dll", "GetEnvironmentStringsW",              STUB_GetEnvironmentStringsW},
  {"kernel32.dll", "FreeEnvironmentStringsW",             STUB_FreeEnvironmentStringsW},
  {"kernel32.dll", "GetEnvironmentVariableA",             STUB_GetEnvironmentVariableA},
  {"kernel32.dll", "GetEnvironmentVariableW",             STUB_GetEnvironmentVariableW},
  {"kernel32.dll", "WideCharToMultiByte",                 STUB_WideCharToMultiByte},
  {"kernel32.dll", "MultiByteToWideChar",                 STUB_MultiByteToWideChar},
  {"kernel32.dll", "GetCurrentProcess",                   STUB_GetCurrentProcess},
  {"kernel32.dll", "TerminateProcess",                    STUB_TerminateProcess},
  {"kernel32.dll", "SetUnhandledExceptionFilter",         STUB_SetUnhandledExceptionFilter},
  {"kernel32.dll", "UnhandledExceptionFilter",            STUB_UnhandledExceptionFilter},
  {"kernel32.dll", "IsDebuggerPresent",                   STUB_IsDebuggerPresent},
  {"kernel32.dll", "IsProcessorFeaturePresent",           STUB_IsProcessorFeaturePresent},
  {"kernel32.dll", "GetSystemTimeAsFileTime",             STUB_GetSystemTimeAsFileTime},
  {"kernel32.dll", "GetTickCount",                        STUB_GetTickCount},
  {"kernel32.dll", "QueryPerformanceFrequency",           STUB_QueryPerformanceFrequency},
  {"kernel32.dll", "GetConsoleMode",                      STUB_GetConsoleMode},
  {"kernel32.dll", "SetConsoleMode",                      STUB_SetConsoleMode},
  {"kernel32.dll", "GetConsoleCP",                        STUB_GetConsoleCP},
  {"kernel32.dll", "GetConsoleOutputCP",                  STUB_GetConsoleOutputCP},
  {"kernel32.dll", "SetConsoleCtrlHandler",               STUB_SetConsoleCtrlHandler},
  {"kernel32.dll", "GetACP",                              STUB_GetACP},
  {"kernel32.dll", "GetOEMCP",                            STUB_GetOEMCP},
  {"kernel32.dll", "GetCPInfo",                           STUB_GetCPInfo},
  {"kernel32.dll", "GetStringTypeW",                      STUB_GetStringTypeW},
  {"kernel32.dll", "LCMapStringW",                        STUB_LCMapStringW},
  {"kernel32.dll", "HeapSize",                            STUB_HeapSize},
  {"kernel32.dll", "HeapReAlloc",                         STUB_HeapReAlloc},
  {"kernel32.dll", "GetProcAddress",                      STUB_GetProcAddress},
  {"kernel32.dll", "LoadLibraryA",                        STUB_LoadLibraryA},
  {"kernel32.dll", "GetModuleHandleW",                    STUB_GetModuleHandleW},
  {"kernel32.dll", "RtlUnwind",                           STUB_RtlUnwind},
  {"kernel32.dll", "InitializeCriticalSectionAndSpinCount", STUB_InitializeCriticalSectionAndSpinCount},
  {"kernel32.dll", "EnterCriticalSection",                STUB_EnterCriticalSection},
  {"kernel32.dll", "LeaveCriticalSection",                STUB_LeaveCriticalSection},
  {"kernel32.dll", "DeleteCriticalSection",               STUB_DeleteCriticalSection},
  {"kernel32.dll", "TlsAlloc",                            STUB_TlsAlloc},
  {"kernel32.dll", "TlsGetValue",                         STUB_TlsGetValue},
  {"kernel32.dll", "TlsSetValue",                         STUB_TlsSetValue},
  {"kernel32.dll", "TlsFree",                             STUB_TlsFree},
  {"kernel32.dll", "Sleep",                               STUB_Sleep},
  {"kernel32.dll", "GetSystemInfo",                       STUB_GetSystemInfo},
  // msvcrt.dll — CRT startup
  {"msvcrt.dll",   "__getmainargs",                       STUB_msvcrt_getmainargs},
  {"msvcrt.dll",   "__set_app_type",                      STUB_msvcrt_set_app_type},
  {"msvcrt.dll",   "_initterm",                           STUB_msvcrt_initterm},
  {"msvcrt.dll",   "_initterm_e",                         STUB_msvcrt_initterm_e},
  {"msvcrt.dll",   "exit",                                STUB_msvcrt_exit},
  {"msvcrt.dll",   "_cexit",                              STUB_msvcrt_cexit},
  {"msvcrt.dll",   "_c_exit",                             STUB_msvcrt_c_exit},
  {"msvcrt.dll",   "_amsg_exit",                          STUB_msvcrt_amsg_exit},
  {"msvcrt.dll",   "__setusermatherr",                    STUB_msvcrt_setusermatherr},
  {"msvcrt.dll",   "_configthreadlocale",                 STUB_msvcrt_configthreadlocale},
  {"msvcrt.dll",   "_set_new_mode",                       STUB_msvcrt_set_new_mode},
  // msvcrt.dll — I/O
  {"msvcrt.dll",   "printf",                              STUB_msvcrt_printf},
  {"msvcrt.dll",   "fprintf",                             STUB_msvcrt_fprintf},
  {"msvcrt.dll",   "puts",                                STUB_msvcrt_puts},
  {"msvcrt.dll",   "putchar",                             STUB_msvcrt_putchar},
  {"msvcrt.dll",   "fwrite",                              STUB_msvcrt_fwrite},
  {"msvcrt.dll",   "fflush",                              STUB_msvcrt_fflush},
  {"msvcrt.dll",   "__iob_func",                          STUB_msvcrt_iob_func},
  {"msvcrt.dll",   "_fileno",                             STUB_msvcrt_fileno},
  {"msvcrt.dll",   "_setmode",                            STUB_msvcrt_setmode},
  // msvcrt.dll — memory
  {"msvcrt.dll",   "malloc",                              STUB_msvcrt_malloc},
  {"msvcrt.dll",   "free",                                STUB_msvcrt_free},
  {"msvcrt.dll",   "calloc",                              STUB_msvcrt_calloc},
  {"msvcrt.dll",   "realloc",                             STUB_msvcrt_realloc},
  // msvcrt.dll — string
  {"msvcrt.dll",   "strlen",                              STUB_msvcrt_strlen},
  {"msvcrt.dll",   "strcpy",                              STUB_msvcrt_strcpy},
  {"msvcrt.dll",   "strncpy",                             STUB_msvcrt_strncpy},
  {"msvcrt.dll",   "strcmp",                               STUB_msvcrt_strcmp},
  {"msvcrt.dll",   "memset",                              STUB_msvcrt_memset},
  {"msvcrt.dll",   "memcpy",                              STUB_msvcrt_memcpy},
  {"msvcrt.dll",   "memmove",                             STUB_msvcrt_memmove},
  // msvcrt.dll — misc
  {"msvcrt.dll",   "_fmode",                              STUB_msvcrt_fmode},
  {"msvcrt.dll",   "_commode",                            STUB_msvcrt_commode},
  {"msvcrt.dll",   "__p___argc",                          STUB_msvcrt_p_argc},
  {"msvcrt.dll",   "__p___argv",                          STUB_msvcrt_p_argv},
  {"msvcrt.dll",   "_errno",                              STUB_msvcrt_errno},
  {"msvcrt.dll",   "abort",                               STUB_msvcrt_abort},
  {"msvcrt.dll",   "signal",                              STUB_msvcrt_signal},
  // kernel32.dll — additional entries needed by real binaries
  {"kernel32.dll", "VirtualProtect",                      STUB_VirtualProtect},
  {"kernel32.dll", "VirtualQuery",                        STUB_VirtualQuery},
  {"kernel32.dll", "InitializeCriticalSection",           STUB_InitializeCriticalSection},
  // UCRT: api-ms-win-crt-runtime-l1-1-0.dll
  {"api-ms-win-crt-runtime-l1-1-0.dll", "_set_app_type",                   STUB_msvcrt_set_app_type},
  {"api-ms-win-crt-runtime-l1-1-0.dll", "_initterm",                       STUB_msvcrt_initterm},
  {"api-ms-win-crt-runtime-l1-1-0.dll", "_initterm_e",                     STUB_msvcrt_initterm_e},
  {"api-ms-win-crt-runtime-l1-1-0.dll", "exit",                            STUB_msvcrt_exit},
  {"api-ms-win-crt-runtime-l1-1-0.dll", "_exit",                           STUB_ucrt_exit},
  {"api-ms-win-crt-runtime-l1-1-0.dll", "_cexit",                          STUB_msvcrt_cexit},
  {"api-ms-win-crt-runtime-l1-1-0.dll", "abort",                           STUB_msvcrt_abort},
  {"api-ms-win-crt-runtime-l1-1-0.dll", "signal",                          STUB_msvcrt_signal},
  {"api-ms-win-crt-runtime-l1-1-0.dll", "__p___argc",                      STUB_msvcrt_p_argc},
  {"api-ms-win-crt-runtime-l1-1-0.dll", "__p___argv",                      STUB_msvcrt_p_argv},
  {"api-ms-win-crt-runtime-l1-1-0.dll", "_configure_narrow_argv",          STUB_ucrt_configure_narrow_argv},
  {"api-ms-win-crt-runtime-l1-1-0.dll", "_initialize_narrow_environment",  STUB_ucrt_initialize_narrow_environment},
  {"api-ms-win-crt-runtime-l1-1-0.dll", "_crt_atexit",                     STUB_ucrt_crt_atexit},
  {"api-ms-win-crt-runtime-l1-1-0.dll", "_set_invalid_parameter_handler",  STUB_ucrt_set_invalid_parameter_handler},
  // UCRT: api-ms-win-crt-stdio-l1-1-0.dll
  {"api-ms-win-crt-stdio-l1-1-0.dll",   "puts",                           STUB_msvcrt_puts},
  {"api-ms-win-crt-stdio-l1-1-0.dll",   "fflush",                         STUB_msvcrt_fflush},
  {"api-ms-win-crt-stdio-l1-1-0.dll",   "__acrt_iob_func",                STUB_ucrt_acrt_iob_func},
  {"api-ms-win-crt-stdio-l1-1-0.dll",   "__p__commode",                   STUB_ucrt_p_commode},
  {"api-ms-win-crt-stdio-l1-1-0.dll",   "__p__fmode",                     STUB_ucrt_p_fmode},
  {"api-ms-win-crt-stdio-l1-1-0.dll",   "__stdio_common_vfprintf",        STUB_ucrt_stdio_common_vfprintf},
  {"api-ms-win-crt-stdio-l1-1-0.dll",   "setvbuf",                        STUB_ucrt_setvbuf},
  // UCRT: api-ms-win-crt-heap-l1-1-0.dll
  {"api-ms-win-crt-heap-l1-1-0.dll",    "malloc",                         STUB_msvcrt_malloc},
  {"api-ms-win-crt-heap-l1-1-0.dll",    "free",                           STUB_msvcrt_free},
  {"api-ms-win-crt-heap-l1-1-0.dll",    "calloc",                         STUB_msvcrt_calloc},
  {"api-ms-win-crt-heap-l1-1-0.dll",    "_set_new_mode",                  STUB_msvcrt_set_new_mode},
  // UCRT: api-ms-win-crt-string-l1-1-0.dll
  {"api-ms-win-crt-string-l1-1-0.dll",  "strlen",                         STUB_msvcrt_strlen},
  {"api-ms-win-crt-string-l1-1-0.dll",  "strncmp",                        STUB_ucrt_strncmp},
  // UCRT: api-ms-win-crt-math-l1-1-0.dll
  {"api-ms-win-crt-math-l1-1-0.dll",    "__setusermatherr",               STUB_msvcrt_setusermatherr},
  // UCRT: api-ms-win-crt-locale-l1-1-0.dll
  {"api-ms-win-crt-locale-l1-1-0.dll",  "_configthreadlocale",            STUB_msvcrt_configthreadlocale},
  // UCRT: api-ms-win-crt-environment-l1-1-0.dll
  {"api-ms-win-crt-environment-l1-1-0.dll", "__p__environ",               STUB_ucrt_p_environ},
  // UCRT: api-ms-win-crt-private-l1-1-0.dll
  {"api-ms-win-crt-private-l1-1-0.dll", "__C_specific_handler",           STUB_ucrt_C_specific_handler},
  {"api-ms-win-crt-private-l1-1-0.dll", "memcpy",                         STUB_msvcrt_memcpy},
  {"api-ms-win-crt-runtime-l1-1-0.dll", "_fpreset",                       STUB_ucrt_fpreset},
  {"api-ms-win-crt-runtime-l1-1-0.dll", "_execute_onexit_table",          STUB_ucrt_execute_onexit_table},
  {"api-ms-win-crt-runtime-l1-1-0.dll", "_initialize_onexit_table",       STUB_ucrt_initialize_onexit_table},
  {"api-ms-win-crt-runtime-l1-1-0.dll", "_register_onexit_function",      STUB_ucrt_register_onexit_function},
  {"api-ms-win-crt-runtime-l1-1-0.dll", "abort",                          STUB_msvcrt_abort},
  {"api-ms-win-crt-runtime-l1-1-0.dll", "abs",                            STUB_ucrt_abs},
  // UCRT: api-ms-win-crt-heap (additional)
  {"api-ms-win-crt-heap-l1-1-0.dll",    "realloc",                        STUB_ucrt_realloc},
  // UCRT: api-ms-win-crt-stdio (additional)
  {"api-ms-win-crt-stdio-l1-1-0.dll",   "putchar",                        STUB_ucrt_putchar},
  {"api-ms-win-crt-stdio-l1-1-0.dll",   "__stdio_common_vsprintf",        STUB_ucrt_vsprintf},
  {"api-ms-win-crt-stdio-l1-1-0.dll",   "fprintf",                        STUB_ucrt_fprintf},
  {"api-ms-win-crt-stdio-l1-1-0.dll",   "printf",                         STUB_msvcrt_printf},
  // UCRT: api-ms-win-crt-string (additional)
  {"api-ms-win-crt-string-l1-1-0.dll",  "_strdup",                        STUB_ucrt_strdup},
  {"api-ms-win-crt-string-l1-1-0.dll",  "memset",                         STUB_ucrt_memset},
  {"api-ms-win-crt-string-l1-1-0.dll",  "strcat",                         STUB_ucrt_strcat},
  {"api-ms-win-crt-string-l1-1-0.dll",  "strcpy",                         STUB_ucrt_strcpy},
  {"api-ms-win-crt-string-l1-1-0.dll",  "strcmp",                          STUB_ucrt_strcmp},
  {"api-ms-win-crt-string-l1-1-0.dll",  "memmove",                        STUB_ucrt_memmove},
  {"api-ms-win-crt-string-l1-1-0.dll",  "memcmp",                         STUB_ucrt_memcmp},
  {"api-ms-win-crt-string-l1-1-0.dll",  "strchr",                         STUB_ucrt_strchr},
  {"api-ms-win-crt-string-l1-1-0.dll",  "strrchr",                        STUB_ucrt_strrchr},
  {"api-ms-win-crt-string-l1-1-0.dll",  "strstr",                         STUB_ucrt_strstr},
  {"api-ms-win-crt-string-l1-1-0.dll",  "toupper",                        STUB_ucrt_toupper},
  {"api-ms-win-crt-string-l1-1-0.dll",  "tolower",                        STUB_ucrt_tolower},
  {"api-ms-win-crt-string-l1-1-0.dll",  "isdigit",                        STUB_ucrt_isdigit},
  {"api-ms-win-crt-string-l1-1-0.dll",  "isalpha",                        STUB_ucrt_isalpha},
  {"api-ms-win-crt-string-l1-1-0.dll",  "isspace",                        STUB_ucrt_isspace},
  {"api-ms-win-crt-string-l1-1-0.dll",  "strtol",                         STUB_ucrt_strtol},
  {"api-ms-win-crt-string-l1-1-0.dll",  "strtoul",                        STUB_ucrt_strtoul},
  // UCRT: api-ms-win-crt-convert-l1-1-0.dll
  {"api-ms-win-crt-convert-l1-1-0.dll",  "atoi",                          STUB_ucrt_atoi},
  // UCRT: api-ms-win-crt-utility-l1-1-0.dll
  {"api-ms-win-crt-utility-l1-1-0.dll", "qsort",                          STUB_ucrt_qsort},
  // D3D11
  {"d3d11.dll",    "D3D11CreateDevice",              STUB_D3D11CreateDevice},
  {"d3d11.dll",    "D3D11CreateDeviceAndSwapChain",   STUB_D3D11CreateDeviceAndSwapChain},
  // DXGI
  {"dxgi.dll",     "CreateDXGIFactory",               STUB_CreateDXGIFactory},
  {"dxgi.dll",     "CreateDXGIFactory1",              STUB_CreateDXGIFactory1},
  {"dxgi.dll",     "CreateDXGIFactory2",              STUB_CreateDXGIFactory2},
};
static constexpr size_t kNumStubs = sizeof(kStubs) / sizeof(kStubs[0]);

class StubTable {
public:
  bool Init() {
    size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    size_t needed = kNumStubs * kThunkSize;
    thunk_size_ = (needed + page_size - 1) & ~(page_size - 1);

    thunk_mem_ = static_cast<uint8_t*>(
      mmap(nullptr, thunk_size_, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (thunk_mem_ == MAP_FAILED) { thunk_mem_ = nullptr; return false; }

    for (size_t i = 0; i < kNumStubs; i++) {
      uint8_t* t = thunk_mem_ + i * kThunkSize;
      uint32_t nr = kWin32SyscallBase + kStubs[i].id;

      // mov r10, rcx — save first arg (syscall clobbers RCX with return address)
      t[0] = 0x49; t[1] = 0x89; t[2] = 0xCA;
      // mov eax, nr
      t[3] = 0xB8;
      memcpy(t + 4, &nr, 4);
      // syscall
      t[8] = 0x0F;
      t[9] = 0x05;
      // ret
      t[10] = 0xC3;

      // Build lookup key: "kernel32.dll!WriteFile"
      std::string key = std::string(kStubs[i].dll_name) + "!" + kStubs[i].func_name;
      for (size_t j = 0; j < strlen(kStubs[i].dll_name); j++)
        key[j] = tolower(key[j]);

      thunk_map_[key] = reinterpret_cast<uint64_t>(t);
    }

    return true;
  }

  uint64_t Resolve(const char* dll, const char* func) const {
    std::string key = std::string(dll) + "!" + func;
    for (size_t i = 0; key[i] && i < strlen(dll); i++)
      key[i] = tolower(key[i]);

    auto it = thunk_map_.find(key);
    if (it != thunk_map_.end())
      return it->second;

    return 0;
  }

  uint8_t* ThunkBase() const { return thunk_mem_; }
  size_t ThunkSize() const { return thunk_size_; }

private:
  uint8_t* thunk_mem_ = nullptr;
  size_t thunk_size_ = 0;
  std::map<std::string, uint64_t> thunk_map_;
};

} // namespace Win32
