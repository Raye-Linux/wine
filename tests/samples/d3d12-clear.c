/*
 * Direct3D 12 test: a swap chain cleared to a colour that slowly moves
 * between Raye night and Raye violet. The title names the adapter.
 * d3d12-clear.exe 10 closes by itself after 10 seconds.
 */
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define FRAMES 2

static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void barrier(ID3D12GraphicsCommandList *list, ID3D12Resource *res,
                    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER b = {0};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &b);
}

int main(int argc, char **argv)
{
    WNDCLASSW wc = {0};
    IDXGIFactory4 *factory;
    IDXGIAdapter1 *adapter;
    DXGI_ADAPTER_DESC1 ad;
    ID3D12Device *dev;
    ID3D12CommandQueue *queue;
    D3D12_COMMAND_QUEUE_DESC qd = {0};
    DXGI_SWAP_CHAIN_DESC1 sd = {0};
    IDXGISwapChain1 *swap1;
    IDXGISwapChain3 *swap;
    D3D12_DESCRIPTOR_HEAP_DESC hd = {0};
    ID3D12DescriptorHeap *heap;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv[FRAMES];
    ID3D12Resource *buffers[FRAMES];
    ID3D12CommandAllocator *alloc;
    ID3D12GraphicsCommandList *list;
    ID3D12Fence *fence;
    HANDLE event = CreateEventW(NULL, FALSE, FALSE, NULL);
    UINT64 value = 0;
    UINT step, i;
    DWORD start = GetTickCount(), end = argc > 1 ? start + atoi(argv[1]) * 1000 : 0;
    WCHAR title[256];
    HWND hwnd;
    HRESULT hr;
    MSG msg;

    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"d3d12test";
    RegisterClassW(&wc);
    hwnd = CreateWindowW(L"d3d12test", L"Direct3D 12", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                         140, 140, 816, 539, NULL, NULL, wc.hInstance, NULL);

    if (FAILED(hr = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&factory)))
    { printf("CreateDXGIFactory1 failed: %#lx\n", hr); return 1; }
    if (FAILED(hr = IDXGIFactory4_EnumAdapters1(factory, 0, &adapter)))
    { printf("no adapter: %#lx\n", hr); return 1; }
    IDXGIAdapter1_GetDesc1(adapter, &ad);
    if (FAILED(hr = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&dev)))
    { printf("D3D12CreateDevice failed on %ls: %#lx\n", ad.Description, hr); return 1; }
    swprintf(title, 256, L"Direct3D 12 - %ls", ad.Description);
    SetWindowTextW(hwnd, title);
    printf("Direct3D 12 on: %ls\n", ad.Description);
    fflush(stdout);

    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void **)&queue);
    sd.Width = 800;
    sd.Height = 500;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = FRAMES;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(hr = IDXGIFactory4_CreateSwapChainForHwnd(factory, (IUnknown *)queue, hwnd, &sd, NULL, NULL, &swap1)))
    { printf("CreateSwapChainForHwnd failed: %#lx\n", hr); return 1; }
    IDXGISwapChain1_QueryInterface(swap1, &IID_IDXGISwapChain3, (void **)&swap);

    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = FRAMES;
    ID3D12Device_CreateDescriptorHeap(dev, &hd, &IID_ID3D12DescriptorHeap, (void **)&heap);
    step = ID3D12Device_GetDescriptorHandleIncrementSize(dev, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (i = 0; i < FRAMES; i++)
    {
        rtv[i] = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
        rtv[i].ptr += i * step;
        IDXGISwapChain3_GetBuffer(swap, i, &IID_ID3D12Resource, (void **)&buffers[i]);
        ID3D12Device_CreateRenderTargetView(dev, buffers[i], NULL, rtv[i]);
    }
    ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&alloc);
    ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, NULL,
                                   &IID_ID3D12GraphicsCommandList, (void **)&list);
    ID3D12GraphicsCommandList_Close(list);
    ID3D12Device_CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&fence);

    for (;;)
    {
        float t = (sinf((GetTickCount() - start) / 1000.0f) + 1) / 2, colour[4];
        UINT frame;

        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) return 0;
            DispatchMessageW(&msg);
        }
        if (end && GetTickCount() > end) break;
        /* #0e0a17 to #7b3cff */
        colour[0] = (0x0e + t * (0x7b - 0x0e)) / 255.0f;
        colour[1] = (0x0a + t * (0x3c - 0x0a)) / 255.0f;
        colour[2] = (0x17 + t * (0xff - 0x17)) / 255.0f;
        colour[3] = 1.0f;
        frame = IDXGISwapChain3_GetCurrentBackBufferIndex(swap);
        ID3D12CommandAllocator_Reset(alloc);
        ID3D12GraphicsCommandList_Reset(list, alloc, NULL);
        barrier(list, buffers[frame], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv[frame], colour, 0, NULL);
        barrier(list, buffers[frame], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        ID3D12GraphicsCommandList_Close(list);
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, (ID3D12CommandList **)&list);
        IDXGISwapChain3_Present(swap, 1, 0);
        ID3D12CommandQueue_Signal(queue, fence, ++value);
        if (ID3D12Fence_GetCompletedValue(fence) < value)
        {
            ID3D12Fence_SetEventOnCompletion(fence, value, event);
            WaitForSingleObject(event, INFINITE);
        }
    }
    printf("ok\n");
    return 0;
}
