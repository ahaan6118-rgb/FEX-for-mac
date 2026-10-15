#include <windows.h>

int main(void) {
    char buf[256];
    int n;
    
    volatile unsigned int *tick_low = (volatile unsigned int *)0x7FFE0324;
    volatile unsigned int *tick_hi1 = (volatile unsigned int *)0x7FFE0320;
    volatile unsigned int *tick_hi2 = (volatile unsigned int *)0x7FFE0328;
    volatile unsigned int *ntmajor  = (volatile unsigned int *)0x7FFE0260;
    volatile unsigned int *tickmult = (volatile unsigned int *)0x7FFE0004;
    
    n = wsprintfA(buf, "Direct read 0x7FFE0324 (TickCount.LowPart): %u\n", *tick_low);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);
    
    n = wsprintfA(buf, "Direct read 0x7FFE0320 (TickCount.High1): %u\n", *tick_hi1);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);
    
    n = wsprintfA(buf, "Direct read 0x7FFE0260 (NtMajorVersion): %u\n", *ntmajor);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);
    
    n = wsprintfA(buf, "Direct read 0x7FFE0004 (TickCountMultiplier): 0x%08x\n", *tickmult);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);
    
    Sleep(1000);
    
    n = wsprintfA(buf, "After Sleep(1000) 0x7FFE0324: %u\n", *tick_low);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);
    
    n = wsprintfA(buf, "GetTickCount: %lu\n", GetTickCount());
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, n, NULL, NULL);
    
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "PASS\n", 5, NULL, NULL);
    return 0;
}
