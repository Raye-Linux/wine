/* Asks for a file or a folder the ways Windows programs do and prints the
 * answer, to check which dialog shows up and what the program gets back.
 *
 *   dialog-probe open-a | open-multi | save-w | save-small | folder
 */
#define COBJMACROS
#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>
#include <stdio.h>
#include <string.h>

static int open_a(void)
{
    char file[MAX_PATH] = "", title[64] = "";
    OPENFILENAMEA ofn = { sizeof(ofn) };
    ofn.lpstrFilter = "Text files\0*.txt;*.log\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof(file);
    ofn.lpstrFileTitle = title;
    ofn.nMaxFileTitle = sizeof(title);
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameA(&ofn))
    {
        printf("cancel %lu\n", CommDlgExtendedError());
        return 1;
    }
    printf("file=%s title=%s offset=%u ext=%u\n", file, title, ofn.nFileOffset, ofn.nFileExtension);
    return 0;
}

static int open_multi(void)
{
    WCHAR file[4096] = L"", *p;
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.lpstrFile = file;
    ofn.nMaxFile = ARRAYSIZE(file);
    ofn.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn))
    {
        printf("cancel %lu\n", CommDlgExtendedError());
        return 1;
    }
    printf("offset=%u\n", ofn.nFileOffset);
    for (p = file; *p; p += lstrlenW(p) + 1) printf("item=%ls\n", p);
    return 0;
}

static int save_w(int size)
{
    WCHAR file[MAX_PATH] = L"report";
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.lpstrFilter = L"Text\0*.txt\0";
    ofn.lpstrDefExt = L"txt";
    ofn.lpstrFile = file;
    ofn.nMaxFile = size;
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn))
    {
        printf("cancel %lu need=%u\n", CommDlgExtendedError(), (unsigned)*(WORD *)file);
        return 1;
    }
    printf("file=%ls offset=%u ext=%u\n", file, ofn.nFileOffset, ofn.nFileExtension);
    return 0;
}

static int folder(void)
{
    IFileOpenDialog *dlg;
    IShellItem *item;
    WCHAR *path;
    HRESULT hr;

    CoInitialize(NULL);
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void **)&dlg)))
        return 2;
    IFileOpenDialog_SetOptions(dlg, FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    hr = IFileOpenDialog_Show(dlg, NULL);
    if (FAILED(hr))
    {
        printf("cancel %#lx\n", hr);
        return 1;
    }
    IFileOpenDialog_GetResult(dlg, &item);
    IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path);
    printf("folder=%ls\n", path);
    return 0;
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "open-a";
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!strcmp(mode, "open-a")) return open_a();
    if (!strcmp(mode, "open-multi")) return open_multi();
    if (!strcmp(mode, "save-w")) return save_w(MAX_PATH);
    if (!strcmp(mode, "save-small")) return save_w(8);
    if (!strcmp(mode, "folder")) return folder();
    return 2;
}
