/*
 * Asks for the screen the way Windows programs do, and measures what
 * comes back.
 *
 *   capture-test monitors
 *   capture-test dxgi <output> <seconds> [out.bmp]
 *   capture-test gdi <seconds> [out.bmp]
 *
 * Copyright 2026 Raye Linux
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <stdlib.h>

static double now_ms( void )
{
    LARGE_INTEGER c, f;
    QueryPerformanceCounter( &c );
    QueryPerformanceFrequency( &f );
    return c.QuadPart * 1000.0 / f.QuadPart;
}

static double qpc_ms( LONGLONG t )
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency( &f );
    return t * 1000.0 / f.QuadPart;
}

static void save_bmp( const char *path, const BYTE *bits, int width, int height, int pitch )
{
    BITMAPFILEHEADER fh = { 0 };
    BITMAPINFOHEADER ih = { sizeof(ih) };
    FILE *f = fopen( path, "wb" );
    int y;

    if (!f) return;
    ih.biWidth = width;
    ih.biHeight = -height;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    fh.bfType = 0x4d42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + width * height * 4;
    fwrite( &fh, sizeof(fh), 1, f );
    fwrite( &ih, sizeof(ih), 1, f );
    for (y = 0; y < height; y++) fwrite( bits + y * pitch, width * 4, 1, f );
    fclose( f );
    printf( "saved %s\n", path );
}

static BOOL CALLBACK print_monitor( HMONITOR monitor, HDC hdc, RECT *rect, LPARAM index )
{
    MONITORINFOEXA info = { .cbSize = sizeof(info) };

    GetMonitorInfoA( monitor, (MONITORINFO *)&info );
    printf( "monitor %d: %s %ld,%ld %ldx%ld%s\n", (int)(*(int *)index)++, info.szDevice,
            info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right - info.rcMonitor.left,
            info.rcMonitor.bottom - info.rcMonitor.top, info.dwFlags & MONITORINFOF_PRIMARY ? " primary" : "" );
    return TRUE;
}

static int monitors( void )
{
    IDXGIFactory1 *factory;
    IDXGIAdapter1 *adapter;
    IDXGIOutput *output;
    int index = 0, a, o;

    EnumDisplayMonitors( NULL, NULL, print_monitor, (LPARAM)&index );
    if (FAILED(CreateDXGIFactory1( &IID_IDXGIFactory1, (void **)&factory ))) return 1;
    for (a = 0; SUCCEEDED(IDXGIFactory1_EnumAdapters1( factory, a, &adapter )); a++)
    {
        DXGI_ADAPTER_DESC1 ad;
        IDXGIAdapter1_GetDesc1( adapter, &ad );
        printf( "adapter %d: %ls\n", a, ad.Description );
        for (o = 0; SUCCEEDED(IDXGIAdapter1_EnumOutputs( adapter, o, &output )); o++)
        {
            DXGI_OUTPUT_DESC d;
            IDXGIOutput_GetDesc( output, &d );
            printf( "  output %d: %ls %ld,%ld %ldx%ld\n", o, d.DeviceName, d.DesktopCoordinates.left,
                    d.DesktopCoordinates.top, d.DesktopCoordinates.right - d.DesktopCoordinates.left,
                    d.DesktopCoordinates.bottom - d.DesktopCoordinates.top );
            IDXGIOutput_Release( output );
        }
        IDXGIAdapter1_Release( adapter );
    }
    IDXGIFactory1_Release( factory );
    return 0;
}

static int dxgi( int number, int seconds, const char *path )
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    IDXGIDevice *dxgi_device;
    IDXGIAdapter *adapter;
    IDXGIOutput *output;
    IDXGIOutput1 *output1;
    IDXGIOutputDuplication *dup;
    DXGI_OUTDUPL_DESC desc;
    ID3D11Texture2D *last = NULL;
    double start, first = 0, end, lat_sum = 0, lat_max = 0;
    int frames = 0, timeouts = 0, pointer_moves = 0, shapes = 0;
    UINT64 accumulated = 0;
    HRESULT hr;

    hr = D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &device, NULL, &context );
    if (FAILED(hr)) { printf( "D3D11CreateDevice failed %#lx\n", hr ); return 1; }
    ID3D11Device_QueryInterface( device, &IID_IDXGIDevice, (void **)&dxgi_device );
    IDXGIDevice_GetAdapter( dxgi_device, &adapter );
    if (FAILED(hr = IDXGIAdapter_EnumOutputs( adapter, number, &output ))) { printf( "no output %d\n", number ); return 1; }
    IDXGIOutput_QueryInterface( output, &IID_IDXGIOutput1, (void **)&output1 );

    start = now_ms();
    hr = IDXGIOutput1_DuplicateOutput( output1, (IUnknown *)device, &dup );
    printf( "DuplicateOutput: %#lx in %.1f ms\n", hr, now_ms() - start );
    if (FAILED(hr)) return 1;
    IDXGIOutputDuplication_GetDesc( dup, &desc );
    printf( "desc: %ux%u format %u\n", desc.ModeDesc.Width, desc.ModeDesc.Height, desc.ModeDesc.Format );

    end = now_ms() + 120000; /* the picker may be open */
    while (now_ms() < end)
    {
        DXGI_OUTDUPL_FRAME_INFO info;
        IDXGIResource *resource;

        hr = IDXGIOutputDuplication_AcquireNextFrame( dup, 100, &info, &resource );
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) { timeouts++; continue; }
        if (FAILED(hr)) { printf( "AcquireNextFrame %#lx\n", hr ); break; }

        if (info.LastPresentTime.QuadPart)
        {
            double t = now_ms(), lat = t - qpc_ms( info.LastPresentTime.QuadPart );
            if (!frames)
            {
                first = t;
                end = t + seconds * 1000.0;
                printf( "first image after %.0f ms\n", t - start );
            }
            frames++;
            accumulated += info.AccumulatedFrames;
            lat_sum += lat;
            if (lat > lat_max) lat_max = lat;
            if (last) ID3D11Texture2D_Release( last );
            IDXGIResource_QueryInterface( resource, &IID_ID3D11Texture2D, (void **)&last );
        }
        if (info.LastMouseUpdateTime.QuadPart) pointer_moves++;
        if (info.PointerShapeBufferSize)
        {
            DXGI_OUTDUPL_POINTER_SHAPE_INFO shape;
            BYTE *buf = malloc( info.PointerShapeBufferSize );
            UINT required;
            if (SUCCEEDED(IDXGIOutputDuplication_GetFramePointerShape( dup, info.PointerShapeBufferSize, buf, &required, &shape )))
            {
                if (!shapes) printf( "pointer shape %ux%u type %u hot %ld,%ld\n", shape.Width, shape.Height,
                                     shape.Type, shape.HotSpot.x, shape.HotSpot.y );
                shapes++;
            }
            free( buf );
        }
        IDXGIResource_Release( resource );
        IDXGIOutputDuplication_ReleaseFrame( dup );
    }

    printf( "dxgi output %d: %d images in %.1f s = %.1f fps (%llu accumulated), %d timeouts, "
            "age at the program %.2f ms avg %.2f max, %d pointer updates, %d shapes\n",
            number, frames, frames ? (now_ms() - first) / 1000 : 0.0,
            frames > 1 ? (frames - 1) * 1000.0 / (now_ms() - first) : 0.0, (unsigned long long)accumulated, timeouts,
            frames ? lat_sum / frames : 0.0, lat_max, pointer_moves, shapes );

    if (last && path)
    {
        D3D11_TEXTURE2D_DESC td;
        ID3D11Texture2D *staging;
        D3D11_MAPPED_SUBRESOURCE map;

        ID3D11Texture2D_GetDesc( last, &td );
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        td.MiscFlags = 0;
        ID3D11Device_CreateTexture2D( device, &td, NULL, &staging );
        ID3D11DeviceContext_CopyResource( context, (ID3D11Resource *)staging, (ID3D11Resource *)last );
        if (SUCCEEDED(ID3D11DeviceContext_Map( context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map )))
        {
            save_bmp( path, map.pData, td.Width, td.Height, map.RowPitch );
            ID3D11DeviceContext_Unmap( context, (ID3D11Resource *)staging, 0 );
        }
        ID3D11Texture2D_Release( staging );
    }
    if (last) ID3D11Texture2D_Release( last );
    IDXGIOutputDuplication_Release( dup );
    return frames ? 0 : 1;
}

static int gdi( int seconds, const char *path )
{
    int x = GetSystemMetrics( SM_XVIRTUALSCREEN ), y = GetSystemMetrics( SM_YVIRTUALSCREEN );
    int w = GetSystemMetrics( SM_CXVIRTUALSCREEN ), h = GetSystemMetrics( SM_CYVIRTUALSCREEN );
    BITMAPINFO bi = {{ sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB }};
    HDC screen = GetDC( NULL ), mem = CreateCompatibleDC( screen );
    void *bits;
    HBITMAP bmp = CreateDIBSection( mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0 );
    double start, end, t, worst = 0;
    int count = 0;

    SelectObject( mem, bmp );
    printf( "virtual screen %d,%d %dx%d\n", x, y, w, h );
    start = now_ms();
    BitBlt( mem, 0, 0, w, h, screen, x, y, SRCCOPY );
    printf( "first BitBlt took %.0f ms (includes the picker)\n", now_ms() - start );

    start = now_ms();
    end = start + seconds * 1000.0;
    while ((t = now_ms()) < end)
    {
        BitBlt( mem, 0, 0, w, h, screen, x, y, SRCCOPY );
        t = now_ms() - t;
        if (t > worst) worst = t;
        count++;
    }
    printf( "gdi: %d full-desktop BitBlt in %d s = %.1f per second, worst %.1f ms\n",
            count, seconds, count * 1000.0 / (now_ms() - start), worst );
    if (path) save_bmp( path, bits, w, h, w * 4 );
    ReleaseDC( NULL, screen );
    return 0;
}

int main( int argc, char **argv )
{
    if (argc >= 2 && !strcmp( argv[1], "monitors" )) return monitors();
    if (argc >= 4 && !strcmp( argv[1], "dxgi" )) return dxgi( atoi( argv[2] ), atoi( argv[3] ), argc > 4 ? argv[4] : NULL );
    if (argc >= 3 && !strcmp( argv[1], "gdi" )) return gdi( atoi( argv[2] ), argc > 3 ? argv[3] : NULL );
    fprintf( stderr, "usage: capture-test monitors | dxgi <output> <seconds> [out.bmp] | gdi <seconds> [out.bmp]\n" );
    return 2;
}
