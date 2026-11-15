/*
 * A window of another process inside ours, the way a web view is hosted
 * (Roblox Studio's sign-in page): the host makes a top-level window, a
 * second process puts a child window in it. A click on the child must give
 * it the keyboard, and the keys typed next must reach it.
 *
 *   child-focus.exe            host side, prints each step, exit code 0 when all pass
 *   child-focus.exe <hwnd>     the other process, started by the host
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static HWND child;
static LONG got_focus, got_keys, got_click;
static char typed[64];

static LRESULT CALLBACK child_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_MOUSEACTIVATE:
        return MA_ACTIVATE;
    case WM_LBUTTONDOWN:
        got_click = 1;
        SetFocus(hwnd);
        return 0;
    case WM_SETFOCUS:
        got_focus = 1;
        return 0;
    case WM_CHAR:
        if (strlen(typed) < sizeof(typed) - 1) typed[strlen(typed)] = (char)wp;
        got_keys = 1;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* the other process: a child window in the host's, then report back */
static int guest(HWND host)
{
    WNDCLASSA wc = {0};
    MSG msg;
    DWORD end;
    RECT rc;

    wc.lpfnWndProc = child_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "raye_guest";
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    RegisterClassA(&wc);
    GetClientRect(host, &rc);
    child = CreateWindowExA(0, "raye_guest", "", WS_CHILD | WS_VISIBLE,
                            20, 20, rc.right - 40, rc.bottom - 40, host, NULL, wc.hInstance, NULL);
    if (!child) return 3;
    PostMessageA(host, WM_APP, 0, (LPARAM)child);
    end = GetTickCount() + 15000;
    while (GetTickCount() < end)
    {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        MsgWaitForMultipleObjects(0, NULL, FALSE, 50, QS_ALLINPUT);
    }
done:
    PostMessageA(host, WM_APP + 1, got_click | (got_focus << 1) | (got_keys << 2), 0);
    printf("child: click %ld, focus %ld, keys \"%s\"\n", got_click, got_focus, typed);
    fflush(stdout);
    return 0;
}

static HWND guest_hwnd;
static LONG guest_report = -1;

static LRESULT CALLBACK host_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_APP) { guest_hwnd = (HWND)lp; return 0; }
    if (msg == WM_APP + 1) { guest_report = (LONG)wp; return 0; }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    while (GetTickCount() < end)
    {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        MsgWaitForMultipleObjects(0, NULL, FALSE, 20, QS_ALLINPUT);
    }
}

static void click_at(int x, int y)
{
    INPUT in[3] = {0};
    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);

    in[0].type = in[1].type = in[2].type = INPUT_MOUSE;
    in[0].mi.dx = x * 65535 / (sx - 1);
    in[0].mi.dy = y * 65535 / (sy - 1);
    in[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    in[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(3, in, sizeof(INPUT));
}

static void type_text(const char *s)
{
    for (; *s; s++)
    {
        INPUT in[2] = {0};
        SHORT vk = VkKeyScanA(*s);
        in[0].type = in[1].type = INPUT_KEYBOARD;
        in[0].ki.wVk = in[1].ki.wVk = LOBYTE(vk);
        in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(INPUT));
        pump(30);
    }
}

int main(int argc, char **argv)
{
    WNDCLASSA wc = {0};
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    char cmd[MAX_PATH + 64], self[MAX_PATH];
    GUITHREADINFO gti = {sizeof(gti)};
    HWND host;
    RECT rc;
    int fails = 0;

    if (argc == 2) return guest((HWND)(ULONG_PTR)strtoull(argv[1], NULL, 0));

    wc.lpfnWndProc = host_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "raye_host";
    wc.hbrBackground = GetStockObject(GRAY_BRUSH);
    RegisterClassA(&wc);
    host = CreateWindowExA(0, "raye_host", "Child focus check", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           100, 100, 500, 360, NULL, NULL, wc.hInstance, NULL);
    SetForegroundWindow(host);
    SetFocus(host);
    pump(500);

    GetModuleFileNameA(NULL, self, sizeof(self));
    snprintf(cmd, sizeof(cmd), "\"%s\" %#llx", self, (unsigned long long)(ULONG_PTR)host);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        printf("FAIL: could not start the other process\n");
        return 1;
    }
    for (int i = 0; i < 100 && !guest_hwnd; i++) pump(50);
    if (!guest_hwnd) { printf("FAIL: no child window from the other process\n"); return 1; }
    printf("ok: child %p of another process in host %p\n", guest_hwnd, host);
    pump(500);

    GetWindowRect(guest_hwnd, &rc);
    click_at((rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2);
    pump(700);
    GetGUIThreadInfo(0, &gti);
    if (gti.hwndFocus == guest_hwnd) printf("ok: the click gave the keyboard to the child\n");
    else { printf("FAIL: focus is on %p after the click, not the child\n", gti.hwndFocus); fails++; }

    type_text("abc");
    pump(700);
    PostThreadMessageA(pi.dwThreadId, WM_QUIT, 0, 0);
    for (int i = 0; i < 100 && guest_report < 0; i++) pump(50);
    WaitForSingleObject(pi.hProcess, 5000);

    if (guest_report < 0) { printf("FAIL: no answer from the other process\n"); return 1; }
    printf("%s: the child saw the click\n", guest_report & 1 ? "ok" : "FAIL");
    printf("%s: the child got WM_SETFOCUS\n", guest_report & 2 ? "ok" : "FAIL");
    printf("%s: the keys reached the child\n", guest_report & 4 ? "ok" : "FAIL");
    fails += !(guest_report & 1) + !(guest_report & 2) + !(guest_report & 4);
    DestroyWindow(host);
    return fails ? 1 : 0;
}
