/*
 * The Windows pieces programs and games expect to find without installing
 * them: DirectX extras (D3DX, the shader compiler, XAudio2, X3DAudio, XACT,
 * XInput), MSXML, the Visual Basic 6 runtime, OpenAL, PhysX, XNA, .NET
 * and GDI+. Each line says whether the piece loads, answers a first call,
 * and whether it is the layer's own copy or a real installed one.
 *
 *   deps-check.exe      exit code = number of pieces missing
 *
 * Build as 64-bit and as 32-bit: XNA, PhysX's old loader and VB6 only
 * exist for 32-bit programs.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>

static int missing;

static const char *origin(HMODULE mod)
{
    char path[MAX_PATH], head[0x80] = {0};
    HANDLE f;
    DWORD got = 0;

    if (!GetModuleFileNameA(mod, path, sizeof(path))) return "?";
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return "?";
    ReadFile(f, head, sizeof(head), &got, NULL);
    CloseHandle(f);
    return !memcmp(head + 0x40, "Wine builtin DLL", 16) ? "layer" : "installed";
}

static HMODULE need(const char *dll, const char *func)
{
    HMODULE mod = LoadLibraryA(dll);
    if (!mod) { printf("missing: %s (%lu)\n", dll, GetLastError()); missing++; return NULL; }
    if (func && !GetProcAddress(mod, func))
    {
        printf("missing: %s has no %s\n", dll, func);
        missing++;
        return NULL;
    }
    printf("ok: %s (%s)\n", dll, origin(mod));
    return mod;
}

static void com_class(const char *what, const char *progid)
{
    WCHAR w[128];
    CLSID clsid;
    IUnknown *unk;
    HRESULT hr;

    MultiByteToWideChar(CP_ACP, 0, progid, -1, w, ARRAYSIZE(w));
    hr = CLSIDFromProgID(w, &clsid);
    if (SUCCEEDED(hr)) hr = CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&unk);
    if (FAILED(hr)) { printf("missing: %s (%s, %#lx)\n", what, progid, hr); missing++; return; }
    printf("ok: %s (%s)\n", what, progid);
    IUnknown_Release(unk);
}

static void key(const char *what, const char *path)
{
    HKEY k;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &k))
    {
        printf("missing: %s (HKLM\\%s)\n", what, path);
        missing++;
        return;
    }
    printf("ok: %s\n", what);
    RegCloseKey(k);
}

static void xaudio2(void)
{
    typedef HRESULT (WINAPI *create_fn)(IUnknown **, UINT32, UINT32);
    HMODULE mod = need("xaudio2_9.dll", "XAudio2Create");
    IUnknown *xa = NULL;
    HRESULT hr;

    if (!mod) return;
    hr = ((create_fn)GetProcAddress(mod, "XAudio2Create"))(&xa, 0, 1);
    printf("%s: XAudio2Create %#lx\n", SUCCEEDED(hr) ? "ok" : "missing", hr);
    if (FAILED(hr)) missing++;
    if (xa) IUnknown_Release(xa);
}

static void dotnet(void)
{
    typedef HRESULT (WINAPI *version_fn)(WCHAR *, DWORD, DWORD *);
    HMODULE mod = need("mscoree.dll", "GetCORVersion");
    WCHAR ver[64];
    DWORD len = ARRAYSIZE(ver);

    if (mod && SUCCEEDED(((version_fn)GetProcAddress(mod, "GetCORVersion"))(ver, len, &len)))
        printf("ok: .NET runtime %ls\n", ver);
    key(".NET Framework 4.8 (registered)", "SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v4\\Full");
}

int main(void)
{
    int bits = (int)sizeof(void *) * 8;

    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    printf("%d-bit\n", bits);
    need("d3dx9_43.dll", "D3DXCreateSprite");
    need("d3dx9_24.dll", "D3DXCreateSprite");
    need("d3dx10_43.dll", "D3DX10CreateEffectFromMemory");
    need("d3dx11_43.dll", "D3DX11CreateShaderResourceViewFromMemory");
    need("d3dcompiler_47.dll", "D3DCompile");
    need("d3dcompiler_43.dll", "D3DCompile");
    xaudio2();
    need("xaudio2_7.dll", "DllGetClassObject");
    need("x3daudio1_7.dll", "X3DAudioInitialize");
    need("xapofx1_5.dll", "CreateFX");
    need("xactengine3_7.dll", "DllGetClassObject");
    need("xinput1_3.dll", "XInputGetState");
    need("xinput1_4.dll", "XInputGetState");
    need("xinput9_1_0.dll", "XInputGetState");
    need("dinput8.dll", "DirectInput8Create");
    need("dsound.dll", "DirectSoundCreate8");
    need("dplayx.dll", "DirectPlayCreate");
    need("d3d12.dll", "D3D12CreateDevice");
    need("vulkan-1.dll", "vkCreateInstance");
    need("opencl.dll", "clGetPlatformIDs");
    need("openal32.dll", "alcOpenDevice");
    need("gdiplus.dll", "GdiplusStartup");
    need("mfc42.dll", NULL);
    need("msvcr71.dll", "printf");
    need("msvcp71.dll", NULL);
    need("msvbvm60.dll", "ThunRTMain");
    need("msxml4.dll", NULL);
    com_class("MSXML 3", "Msxml2.DOMDocument.3.0");
    com_class("MSXML 6", "Msxml2.DOMDocument.6.0");
    com_class("Windows Script (VBScript)", "MSScriptControl.ScriptControl");
    com_class("DirectShow graph", "FilterGraph");
    com_class("Web browser control", "Shell.Explorer.2");
    dotnet();
    need("physxloader.dll", NULL);
    key("PhysX system software", "SOFTWARE\\AGEIA Technologies");
    key("XNA Framework 4.0", "SOFTWARE\\Microsoft\\XNA\\Framework\\v4.0");
    printf("%d missing\n", missing);
    return missing;
}
