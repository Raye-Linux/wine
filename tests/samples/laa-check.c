/*
 * How much address space a 32-bit program gets. Built without the
 * large-address flag, so it shows 2 GB unless WINE_LARGE_ADDRESS_AWARE=1
 * hands it 4 GB.
 */
#include <windows.h>
#include <stdio.h>

int main(void)
{
    SYSTEM_INFO si;
    MEMORYSTATUSEX ms = { sizeof(ms) };
    void *high;

    GetSystemInfo(&si);
    GlobalMemoryStatusEx(&ms);
    high = VirtualAlloc((void *)0x90000000, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    printf("highest address %p, virtual %llu MB, above 2 GB: %s\n",
           si.lpMaximumApplicationAddress, ms.ullTotalVirtual >> 20, high ? "yes" : "no");
    return 0;
}
