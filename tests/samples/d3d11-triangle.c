/*
 * Direct3D 11 test: a coloured triangle on Raye violet, redrawn every
 * frame. The title says which adapter draws it, so a screenshot shows
 * whether Direct3D went through Vulkan (the GPU's name) or not.
 * Closes by itself after a few seconds when started with a number of
 * seconds, e.g. d3d11-triangle.exe 10.
 */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdlib.h>

static const char shader[] =
    "struct v2p { float4 pos : SV_POSITION; float4 col : COLOR; };\n"
    "v2p vs(uint id : SV_VertexID) {\n"
    "    float2 p[3] = { float2(0, 0.7), float2(0.6, -0.5), float2(-0.6, -0.5) };\n"
    "    float4 c[3] = { float4(0.95,0.92,1,1), float4(0.75,0.25,1,1), float4(0.48,0.24,1,1) };\n"
    "    v2p o; o.pos = float4(p[id], 0, 1); o.col = c[id]; return o;\n"
    "}\n"
    "float4 ps(v2p i) : SV_TARGET { return i.col; }\n";

static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static ID3DBlob *compile(const char *entry, const char *target)
{
    ID3DBlob *code = NULL, *errors = NULL;
    if (FAILED(D3DCompile(shader, sizeof(shader) - 1, "triangle", NULL, NULL, entry, target, 0, 0, &code, &errors)))
    {
        printf("shader %s: %s\n", entry, errors ? (char *)ID3D10Blob_GetBufferPointer(errors) : "failed");
        exit(1);
    }
    return code;
}

int main(int argc, char **argv)
{
    WNDCLASSW wc = {0};
    DXGI_SWAP_CHAIN_DESC sd = {0};
    ID3D11Device *dev;
    ID3D11DeviceContext *ctx;
    IDXGISwapChain *swap;
    ID3D11Texture2D *back;
    ID3D11RenderTargetView *rtv;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3DBlob *vsb, *psb;
    IDXGIDevice *dxgi_dev;
    IDXGIAdapter *adapter;
    DXGI_ADAPTER_DESC ad;
    D3D11_VIEWPORT vp = {0, 0, 800, 500, 0, 1};
    DWORD end = argc > 1 ? GetTickCount() + atoi(argv[1]) * 1000 : 0;
    WCHAR title[256];
    HWND hwnd;
    HRESULT hr;
    MSG msg;

    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"d3d11test";
    RegisterClassW(&wc);
    hwnd = CreateWindowW(L"d3d11test", L"Direct3D 11", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                         100, 100, 816, 539, NULL, NULL, wc.hInstance, NULL);

    sd.BufferCount = 2;
    sd.BufferDesc.Width = 800;
    sd.BufferDesc.Height = 500;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
                                       D3D11_SDK_VERSION, &sd, &swap, &dev, NULL, &ctx);
    if (FAILED(hr)) { printf("D3D11CreateDeviceAndSwapChain failed: %#lx\n", hr); return 1; }

    ID3D11Device_QueryInterface(dev, &IID_IDXGIDevice, (void **)&dxgi_dev);
    IDXGIDevice_GetAdapter(dxgi_dev, &adapter);
    IDXGIAdapter_GetDesc(adapter, &ad);
    swprintf(title, 256, L"Direct3D 11 - %ls", ad.Description);
    SetWindowTextW(hwnd, title);
    printf("Direct3D 11 on: %ls (feature level %#x)\n", ad.Description,
           ID3D11Device_GetFeatureLevel(dev));
    fflush(stdout);

    IDXGISwapChain_GetBuffer(swap, 0, &IID_ID3D11Texture2D, (void **)&back);
    ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)back, NULL, &rtv);
    vsb = compile("vs", "vs_4_0");
    psb = compile("ps", "ps_4_0");
    ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(vsb), ID3D10Blob_GetBufferSize(vsb), NULL, &vs);
    ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(psb), ID3D10Blob_GetBufferSize(psb), NULL, &ps);

    for (;;)
    {
        static const float night[4] = {0x0e / 255.0f, 0x0a / 255.0f, 0x17 / 255.0f, 1.0f};
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) return 0;
            DispatchMessageW(&msg);
        }
        if (end && GetTickCount() > end) break;
        ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, night);
        ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
        ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
        ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
        ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
        ID3D11DeviceContext_Draw(ctx, 3, 0);
        IDXGISwapChain_Present(swap, 1, 0);
    }
    printf("ok\n");
    return 0;
}
