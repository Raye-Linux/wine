/*
 * Hardware check: prints what a Windows program sees of this computer,
 * one line per thing found, grouped the way Device Manager groups them:
 * processor, memory, display, sound, MIDI, game controllers, cameras,
 * printers, COM ports, drives and network. A line starting with "none"
 * means the class is empty. With an argument, only the classes named:
 *
 *   hw-check.exe [cpu,display,sound,midi,input,camera,printer,com,drive,net]
 *
 * "com" also opens each port and sets 9600 8N1 on it, the way a terminal
 * program does, and "net" pings 127.0.0.1 with ICMP.
 */
#define COBJMACROS
#define DIRECTINPUT_VERSION 0x0800
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <dxgi.h>
#include <d3d9.h>
#include <dinput.h>
#include <xinput.h>
#include <strmif.h>
#include <uuids.h>
#include <mfapi.h>
#include <mfidl.h>
#include <setupapi.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <winspool.h>
#include <stdio.h>
#include <string.h>

static int only(const char *want, const char *cls)
{
    size_t n = strlen(cls);
    const char *p;

    if (!want) return 1;
    for (p = want; (p = strstr(p, cls)); p += n)
        if ((p == want || p[-1] == ',') && (!p[n] || p[n] == ',')) return 1;
    return 0;
}

static void cpu(void)
{
    SYSTEM_INFO si;
    MEMORYSTATUSEX ms = { sizeof(ms) };
    char name[128] = "";
    DWORD size = sizeof(name);
    OSVERSIONINFOEXW ver = { sizeof(ver) };
    LONG (WINAPI *rtl_version)(OSVERSIONINFOEXW *);

    GetNativeSystemInfo(&si);
    RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                 "ProcessorNameString", RRF_RT_REG_SZ, NULL, name, &size);
    printf("cpu: %s, %lu logical, arch %u\n", name, si.dwNumberOfProcessors,
           si.wProcessorArchitecture);
    GlobalMemoryStatusEx(&ms);
    printf("memory: %llu MB total, %llu MB free\n", ms.ullTotalPhys >> 20, ms.ullAvailPhys >> 20);
    rtl_version = (void *)GetProcAddress(GetModuleHandleA("ntdll"), "RtlGetVersion");
    if (rtl_version && !rtl_version(&ver))
        printf("windows: %lu.%lu build %lu\n", ver.dwMajorVersion, ver.dwMinorVersion,
               ver.dwBuildNumber);
}

static void display(void)
{
    DISPLAY_DEVICEA dd = { sizeof(dd) };
    IDXGIFactory1 *factory;
    IDXGIAdapter1 *adapter;
    IDirect3D9 *d3d;
    D3DADAPTER_IDENTIFIER9 id;
    DWORD i;
    int n = 0;

    for (i = 0; EnumDisplayDevicesA(NULL, i, &dd, 0); i++)
        if (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)
        {
            DEVMODEA dm = { .dmSize = sizeof(dm) };
            EnumDisplaySettingsA(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm);
            printf("screen: %s %s %lux%lu %lu Hz\n", dd.DeviceName, dd.DeviceString,
                   dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency);
            n++;
        }
    if (SUCCEEDED(CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory)))
    {
        for (i = 0; IDXGIFactory1_EnumAdapters1(factory, i, &adapter) == S_OK; i++)
        {
            DXGI_ADAPTER_DESC1 d;
            IDXGIAdapter1_GetDesc1(adapter, &d);
            printf("dxgi: %ls, vendor %04x device %04x, %llu MB video memory%s\n", d.Description,
                   d.VendorId, d.DeviceId, (unsigned long long)d.DedicatedVideoMemory >> 20,
                   d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE ? ", software" : "");
            IDXGIAdapter1_Release(adapter);
            n++;
        }
        IDXGIFactory1_Release(factory);
    }
    if ((d3d = Direct3DCreate9(D3D_SDK_VERSION)))
    {
        if (SUCCEEDED(IDirect3D9_GetAdapterIdentifier(d3d, 0, 0, &id)))
            printf("d3d9: %s, driver %s, vendor %04lx device %04lx\n", id.Description, id.Driver,
                   id.VendorId, id.DeviceId);
        IDirect3D9_Release(d3d);
    }
    if (!n) printf("none: display\n");
}

static void endpoints(IMMDeviceEnumerator *en, EDataFlow flow, const char *what)
{
    IMMDeviceCollection *all;
    IMMDevice *dev;
    UINT count = 0, i;

    if (SUCCEEDED(IMMDeviceEnumerator_EnumAudioEndpoints(en, flow, DEVICE_STATE_ACTIVE, &all)))
    {
        IMMDeviceCollection_GetCount(all, &count);
        for (i = 0; i < count; i++)
        {
            IPropertyStore *props;
            PROPVARIANT name;

            IMMDeviceCollection_Item(all, i, &dev);
            IMMDevice_OpenPropertyStore(dev, STGM_READ, &props);
            PropVariantInit(&name);
            IPropertyStore_GetValue(props, &PKEY_Device_FriendlyName, &name);
            printf("wasapi %s: %ls\n", what, name.vt == VT_LPWSTR ? name.pwszVal : L"?");
            PropVariantClear(&name);
            IPropertyStore_Release(props);
            IMMDevice_Release(dev);
        }
        IMMDeviceCollection_Release(all);
    }
    if (!count) printf("none: wasapi %s\n", what);
    if (SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, flow, eConsole, &dev)))
    {
        printf("wasapi %s default: yes\n", what);
        IMMDevice_Release(dev);
    }
}

static void sound(void)
{
    IMMDeviceEnumerator *en;
    UINT i, n;

    if (SUCCEEDED(CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                                   &IID_IMMDeviceEnumerator, (void **)&en)))
    {
        endpoints(en, eRender, "playback");
        endpoints(en, eCapture, "recording");
        IMMDeviceEnumerator_Release(en);
    }
    n = waveOutGetNumDevs();
    for (i = 0; i < n; i++)
    {
        WAVEOUTCAPSA c;
        if (!waveOutGetDevCapsA(i, &c, sizeof(c))) printf("waveOut %u: %s\n", i, c.szPname);
    }
    if (!n) printf("none: waveOut\n");
    n = waveInGetNumDevs();
    for (i = 0; i < n; i++)
    {
        WAVEINCAPSA c;
        if (!waveInGetDevCapsA(i, &c, sizeof(c))) printf("waveIn %u: %s\n", i, c.szPname);
    }
    if (!n) printf("none: waveIn\n");
}

static void midi(void)
{
    UINT i, n;

    n = midiInGetNumDevs();
    for (i = 0; i < n; i++)
    {
        MIDIINCAPSA c;
        if (!midiInGetDevCapsA(i, &c, sizeof(c))) printf("midiIn %u: %s\n", i, c.szPname);
    }
    if (!n) printf("none: midiIn\n");
    n = midiOutGetNumDevs();
    for (i = 0; i < n; i++)
    {
        MIDIOUTCAPSA c;
        if (!midiOutGetDevCapsA(i, &c, sizeof(c))) printf("midiOut %u: %s\n", i, c.szPname);
    }
    if (!n) printf("none: midiOut\n");
}

static BOOL CALLBACK di_device(const DIDEVICEINSTANCEA *d, void *ctx)
{
    printf("dinput: %s (%s), type %#lx\n", d->tszInstanceName, d->tszProductName, d->dwDevType);
    ++*(int *)ctx;
    return DIENUM_CONTINUE;
}

static void input(void)
{
    IDirectInput8A *di;
    XINPUT_STATE xs;
    UINT i, raw = 0, hid = 0;
    int n = 0;
    HDEVINFO set;
    SP_DEVICE_INTERFACE_DATA iface = { sizeof(iface) };
    GUID hid_guid = { 0x4d1e55b2, 0xf16f, 0x11cf, { 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };

    if (SUCCEEDED(DirectInput8Create(GetModuleHandleA(NULL), DIRECTINPUT_VERSION, &IID_IDirectInput8A,
                                     (void **)&di, NULL)))
    {
        IDirectInput8_EnumDevices(di, DI8DEVCLASS_GAMECTRL, di_device, &n, DIEDFL_ATTACHEDONLY);
        IDirectInput8_Release(di);
    }
    if (!n) printf("none: dinput game controllers\n");
    for (i = 0; i < XUSER_MAX_COUNT; i++)
        if (XInputGetState(i, &xs) == ERROR_SUCCESS) printf("xinput: pad %u connected\n", i);
    for (i = 0; i < 16; i++)
    {
        JOYINFOEX ji = { sizeof(ji), JOY_RETURNALL };
        if (joyGetPosEx(i, &ji) == JOYERR_NOERROR) printf("winmm joystick %u\n", i);
    }
    GetRawInputDeviceList(NULL, &raw, sizeof(RAWINPUTDEVICELIST));
    set = SetupDiGetClassDevsA(&hid_guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set != INVALID_HANDLE_VALUE)
    {
        while (SetupDiEnumDeviceInterfaces(set, NULL, &hid_guid, hid, &iface)) hid++;
        SetupDiDestroyDeviceInfoList(set);
    }
    printf("raw input devices: %u, hid interfaces: %u\n", raw, hid);
}

static void camera(void)
{
    ICreateDevEnum *de;
    IEnumMoniker *em = NULL;
    IMoniker *m;
    IMFAttributes *attr;
    IMFActivate **src = NULL;
    UINT32 count = 0, i;
    int n = 0;

    if (SUCCEEDED(CoCreateInstance(&CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_ICreateDevEnum, (void **)&de)))
    {
        if (ICreateDevEnum_CreateClassEnumerator(de, &CLSID_VideoInputDeviceCategory, &em, 0) == S_OK)
        {
            while (IEnumMoniker_Next(em, 1, &m, NULL) == S_OK)
            {
                IPropertyBag *bag;
                VARIANT v;

                VariantInit(&v);
                if (SUCCEEDED(IMoniker_BindToStorage(m, NULL, NULL, &IID_IPropertyBag, (void **)&bag)))
                {
                    if (SUCCEEDED(IPropertyBag_Read(bag, L"FriendlyName", &v, NULL)))
                        printf("directshow camera: %ls\n", v.bstrVal);
                    VariantClear(&v);
                    IPropertyBag_Release(bag);
                }
                IMoniker_Release(m);
                n++;
            }
            IEnumMoniker_Release(em);
        }
        ICreateDevEnum_Release(de);
    }
    if (!n) printf("none: directshow camera\n");
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (SUCCEEDED(MFCreateAttributes(&attr, 1)))
    {
        IMFAttributes_SetGUID(attr, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                              &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        if (SUCCEEDED(MFEnumDeviceSources(attr, &src, &count)))
        {
            for (i = 0; i < count; i++)
            {
                WCHAR *name;
                UINT32 len;
                if (SUCCEEDED(IMFActivate_GetAllocatedString(src[i], &MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                                                             &name, &len)))
                {
                    printf("media foundation camera: %ls\n", name);
                    CoTaskMemFree(name);
                }
                IMFActivate_Release(src[i]);
            }
            CoTaskMemFree(src);
        }
        IMFAttributes_Release(attr);
    }
    if (!count) printf("none: media foundation camera\n");
}

static void printer(void)
{
    DWORD need = 0, got = 0, i;
    PRINTER_INFO_2A *p;
    char def[256];
    DWORD size = sizeof(def);

    EnumPrintersA(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL, 2, NULL, 0, &need, &got);
    p = need ? HeapAlloc(GetProcessHeap(), 0, need) : NULL;
    if (p && EnumPrintersA(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL, 2, (BYTE *)p, need,
                           &need, &got))
        for (i = 0; i < got; i++)
            printf("printer: %s, driver %s, port %s\n", p[i].pPrinterName, p[i].pDriverName,
                   p[i].pPortName);
    if (!got) printf("none: printer\n");
    if (GetDefaultPrinterA(def, &size)) printf("printer default: %s\n", def);
    HeapFree(GetProcessHeap(), 0, p);
}

static void com(void)
{
    static char names[65536];
    char *s, path[64], target[512];
    HKEY key;
    DWORD i = 0, nlen, vlen, type;
    char vname[256], value[64];
    int n = 0;

    if (QueryDosDeviceA(NULL, names, sizeof(names)))
        for (s = names; *s; s += strlen(s) + 1)
            if (!strncmp(s, "COM", 3))
            {
                HANDLE h;
                DCB dcb = { sizeof(dcb) };
                const char *state;

                QueryDosDeviceA(s, target, sizeof(target));
                snprintf(path, sizeof(path), "\\\\.\\%.48s", s);
                h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
                if (h == INVALID_HANDLE_VALUE)
                    state = GetLastError() == ERROR_ACCESS_DENIED ? "access denied" : "cannot open";
                else
                {
                    GetCommState(h, &dcb);
                    dcb.BaudRate = CBR_9600; dcb.ByteSize = 8; dcb.Parity = NOPARITY; dcb.StopBits = ONESTOPBIT;
                    state = SetCommState(h, &dcb) ? "opens, 9600 8N1 set" : "opens, settings refused";
                    CloseHandle(h);
                }
                printf("com: %s -> %s, %s\n", s, target, state);
                n++;
            }
    if (!n) printf("none: com\n");
    if (!RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &key))
    {
        for (n = 0;; i++, n++)
        {
            nlen = sizeof(vname), vlen = sizeof(value);
            if (RegEnumValueA(key, i, vname, &nlen, NULL, &type, (BYTE *)value, &vlen)) break;
        }
        printf("SERIALCOMM entries: %d\n", n);
        RegCloseKey(key);
    }
    else printf("none: SERIALCOMM key\n");
}

static void drive(void)
{
    char all[512], *d, label[MAX_PATH], fs[32];
    static const char *types[] = { "unknown", "no root", "removable", "fixed", "network", "cd", "ram" };
    ULARGE_INTEGER total;

    GetLogicalDriveStringsA(sizeof(all), all);
    for (d = all; *d; d += strlen(d) + 1)
    {
        UINT t = GetDriveTypeA(d);
        label[0] = fs[0] = 0;
        total.QuadPart = 0;
        GetVolumeInformationA(d, label, sizeof(label), NULL, NULL, NULL, fs, sizeof(fs));
        GetDiskFreeSpaceExA(d, NULL, &total, NULL);
        printf("drive: %s %s, label \"%s\", %s, %llu GB\n", d, t < 7 ? types[t] : "?", label, fs,
               total.QuadPart >> 30);
    }
}

static void net(void)
{
    ULONG size = 0;
    IP_ADAPTER_ADDRESSES *a, *all;
    HANDLE icmp;
    char reply[sizeof(ICMP_ECHO_REPLY) + 32];
    struct addrinfo *ai = NULL;
    WSADATA wsa;

    GetAdaptersAddresses(AF_UNSPEC, 0, NULL, NULL, &size);
    all = HeapAlloc(GetProcessHeap(), 0, size);
    if (all && !GetAdaptersAddresses(AF_UNSPEC, 0, NULL, all, &size))
        for (a = all; a; a = a->Next)
            printf("adapter: %ls, type %lu, %s, %llu Mbit/s, mac %02x:%02x:%02x:%02x:%02x:%02x\n",
                   a->FriendlyName, a->IfType, a->OperStatus == IfOperStatusUp ? "up" : "down",
                   (unsigned long long)a->TransmitLinkSpeed / 1000000, a->PhysicalAddress[0],
                   a->PhysicalAddress[1], a->PhysicalAddress[2], a->PhysicalAddress[3],
                   a->PhysicalAddress[4], a->PhysicalAddress[5]);
    HeapFree(GetProcessHeap(), 0, all);
    if ((icmp = IcmpCreateFile()) != INVALID_HANDLE_VALUE)
    {
        DWORD n = IcmpSendEcho(icmp, htonl(INADDR_LOOPBACK), "raye", 4, NULL, reply, sizeof(reply), 1000);
        printf("ping 127.0.0.1: %s\n", n ? "reply" : "no reply");
        IcmpCloseHandle(icmp);
    }
    WSAStartup(MAKEWORD(2, 2), &wsa);
    printf("dns localhost: %s\n", getaddrinfo("localhost", NULL, NULL, &ai) ? "failed" : "ok");
    if (ai) freeaddrinfo(ai);
}

int main(int argc, char **argv)
{
    const char *want = argc > 1 ? argv[1] : NULL;

    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (only(want, "cpu")) cpu();
    if (only(want, "display")) display();
    if (only(want, "sound")) sound();
    if (only(want, "midi")) midi();
    if (only(want, "input")) input();
    if (only(want, "camera")) camera();
    if (only(want, "printer")) printer();
    if (only(want, "com")) com();
    if (only(want, "drive")) drive();
    if (only(want, "net")) net();
    return 0;
}
