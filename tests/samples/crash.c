/*
 * Crashes on purpose, to look at the crash dialog.
 */
#include <windows.h>

int main(void)
{
    volatile int *nowhere = NULL;
    MessageBoxW(NULL, L"Press OK to crash.", L"Crash test", MB_OK);
    return *nowhere;
}
