/*
 * What a program built with Visual Studio 2005 to 2022 or the DirectX SDK
 * finds in a Windows space: each runtime DLL is loaded the way such a
 * program loads it (the 2005 and 2008 ones through their side-by-side
 * manifest), called once, and checked to be Microsoft's file rather than
 * the layer's own copy. The DirectX pieces only have to answer: the layer
 * brings its own.
 *
 *   runtimes-check.exe          exit code 0 when every runtime is Microsoft's and answers
 *   runtimes-check.exe --any    accept the layer's own copies too (a space without the runtimes)
 *
 * Build as 64-bit and as 32-bit: the two halves of a space are separate.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int any, fails;

/* Wine marks its own DLLs in the DOS stub; the version resource will
 * not tell, the built-in ones claim Microsoft there too */
static int builtin(HMODULE mod, char *path, DWORD size)
{
    char head[0x80] = {0};
    HANDLE f;
    DWORD got = 0;

    if (!GetModuleFileNameA(mod, path, size)) return -1;
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return -1;
    ReadFile(f, head, sizeof(head), &got, NULL);
    CloseHandle(f);
    return got == sizeof(head) && (!memcmp(head + 0x40, "Wine builtin DLL", 16) ||
                                   !memcmp(head + 0x40, "Wine placeholder DLL", 20));
}

static void report(const char *what, HMODULE mod, int answered, int microsoft)
{
    char path[MAX_PATH];
    int layer;

    if (!mod)
    {
        printf("FAIL: %s does not load (%lu)\n", what, GetLastError());
        fails++;
        return;
    }
    layer = builtin(mod, path, sizeof(path));
    if (!answered) { printf("FAIL: %s loads but does not answer\n", what); fails++; }
    else if (microsoft && !any && layer)
    {
        printf("FAIL: %s is the layer's own, not Microsoft's (%s)\n", what, path);
        fails++;
    }
    else printf("ok: %s, %s\n", what, layer ? "the layer's own" : "Microsoft's");
}

static void plain(const char *dll, const char *func)
{
    HMODULE mod = LoadLibraryA(dll);
    report(dll, mod, mod && GetProcAddress(mod, func) != NULL, 1);
}

/* the 2005 and 2008 runtimes live in WinSxS and load through a manifest */
static void side_by_side(const char *name, const char *version, const char *dll)
{
    char manifest[1024], path[MAX_PATH], what[96];
    ACTCTXA ctx = {sizeof(ctx)};
    ULONG_PTR cookie;
    HANDLE act;
    HMODULE mod;
    FILE *f;

    GetTempPathA(sizeof(path), path);
    strcat(path, "runtimes-check.manifest");
    snprintf(manifest, sizeof(manifest),
             "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
             "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
             "<dependency><dependentAssembly><assemblyIdentity type=\"win32\" name=\"%s\" "
             "version=\"%s\" processorArchitecture=\"%s\" publicKeyToken=\"1fc8b3b9a1e18e3b\"/>"
             "</dependentAssembly></dependency></assembly>",
             name, version, sizeof(void *) == 8 ? "amd64" : "x86");
    f = fopen(path, "w");
    fputs(manifest, f);
    fclose(f);
    snprintf(what, sizeof(what), "%s (%s %s)", dll, name, version);
    ctx.lpSource = path;
    act = CreateActCtxA(&ctx);
    if (act == INVALID_HANDLE_VALUE)
    {
        printf("FAIL: %s: no such assembly (%lu)\n", what, GetLastError());
        fails++;
        return;
    }
    ActivateActCtx(act, &cookie);
    mod = LoadLibraryA(dll);
    report(what, mod, mod && GetProcAddress(mod, "printf") != NULL, 1);
    DeactivateActCtx(0, cookie);
    ReleaseActCtx(act);
    DeleteFileA(path);
}

/* D3DCompile of a tiny pixel shader: the d3dcompiler a game ships against */
static void compiler(const char *dll)
{
    typedef HRESULT (WINAPI *compile_fn)(const void *, SIZE_T, const char *, const void *, void *,
                                         const char *, const char *, UINT, UINT, void **, void **);
    static const char src[] = "float4 main() : SV_Target { return float4(1, 0, 0, 1); }";
    HMODULE mod = LoadLibraryA(dll);
    compile_fn compile = mod ? (compile_fn)GetProcAddress(mod, "D3DCompile") : NULL;
    void *code = NULL, *errors = NULL;
    int ok = compile && compile(src, sizeof(src) - 1, "check", NULL, NULL, "main", "ps_4_0",
                                0, 0, &code, &errors) == S_OK && code;
    report(dll, mod, ok, 0);
    if (code) ((IUnknown *)code)->lpVtbl->Release(code);
    if (errors) ((IUnknown *)errors)->lpVtbl->Release(errors);
}

int main(int argc, char **argv)
{
    any = argc > 1 && !strcmp(argv[1], "--any");
    printf("%d-bit\n", (int)sizeof(void *) * 8);
    /* what programs built with VS 2005 SP1 and VS 2008 SP1 ask for, then the last updates */
    side_by_side("Microsoft.VC80.CRT", "8.0.50727.762", "msvcr80.dll");
    side_by_side("Microsoft.VC80.CRT", "8.0.50727.6195", "msvcr80.dll");
    side_by_side("Microsoft.VC90.CRT", "9.0.30729.1", "msvcr90.dll");
    side_by_side("Microsoft.VC90.CRT", "9.0.30729.6161", "msvcr90.dll");
    plain("msvcr100.dll", "printf");
    plain("msvcp100.dll", "_Getctype");
    plain("msvcr110.dll", "printf");
    plain("msvcr120.dll", "printf");
    plain("msvcp120.dll", "_Thrd_yield");
    plain("vcruntime140.dll", "memcpy");
    plain("msvcp140.dll", "_Thrd_yield");
    plain("mfc140u.dll", MAKEINTRESOURCEA(256));
    plain("vcomp140.dll", "omp_get_max_threads");
    {
        HMODULE mod = LoadLibraryA("d3dx9_43.dll");
        report("d3dx9_43.dll", mod, mod && GetProcAddress(mod, "D3DXCreateBuffer") != NULL, 0);
    }
    compiler("d3dcompiler_43.dll");
    compiler("d3dcompiler_47.dll");
    printf("%s\n", fails ? "FAILED" : "all runtimes answered");
    return fails ? 1 : 0;
}
