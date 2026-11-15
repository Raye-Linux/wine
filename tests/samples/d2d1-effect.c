/*
 * Direct2D custom effect check. The effect's factory hands back an IUnknown
 * that is not its ID2D1EffectImpl (as an implementation with separate
 * interface pointers does, Paint.NET's for one); Initialize then asks its
 * context for ID2D1EffectContext2 and a colour context, and the device
 * context for an sRGB one. Prints each step; exit code 0 when all pass.
 */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <d2d1_1.h>
#include <d2d1_3.h>
#include <stdio.h>

DEFINE_GUID(CLSID_check_effect, 0x1d2a5e10, 0x7b31, 0x4c4e, 0x9a, 0x51, 0x52, 0x61, 0x79, 0x65, 0x00, 0x01);
DEFINE_GUID(IID_effect_context2, 0x577ad2a0, 0x9fc7, 0x4dda, 0x8b, 0x18, 0xda, 0xb8, 0x10, 0x14, 0x00, 0x52);

DEFINE_GUID(IID_effect_impl, 0xa248fd3f, 0x3e6c, 0x4e63, 0x9f, 0x03, 0x7f, 0x68, 0xec, 0xc9, 0x1d, 0xb9);

/* The effect authoring interfaces only come as C++ in these headers: the
 * calls below go through the vtable slots. ID2D1EffectContext has 24
 * methods; ID2D1EffectContext2 adds CreateLookupTable3D (24) and
 * CreateColorContextFromDxgiColorSpace (25). */
typedef void *com;
#define SLOT(obj, n, type) ((type)((*(void ***)(obj))[n]))
typedef HRESULT (STDMETHODCALLTYPE *qi_fn)(com, REFIID, void **);
typedef ULONG (STDMETHODCALLTYPE *release_fn)(com);
typedef HRESULT (STDMETHODCALLTYPE *from_dxgi_fn)(com, DXGI_COLOR_SPACE_TYPE, com *);
typedef D2D1_COLOR_SPACE (STDMETHODCALLTYPE *space_fn)(com);

typedef struct
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(com, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(com);
    ULONG (STDMETHODCALLTYPE *Release)(com);
    HRESULT (STDMETHODCALLTYPE *Initialize)(com, com context, com graph);
    HRESULT (STDMETHODCALLTYPE *PrepareForRender)(com, UINT32);
    HRESULT (STDMETHODCALLTYPE *SetGraph)(com, com graph);
} impl_vtbl_t;

static int failures;
static BOOL initialized, impl_asked;

static void check(BOOL ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

struct effect
{
    IUnknown IUnknown_iface;            /* what the factory returns */
    struct { const impl_vtbl_t *lpVtbl; } impl;
    LONG ref;
};

static struct effect *from_unknown(IUnknown *iface) { return CONTAINING_RECORD(iface, struct effect, IUnknown_iface); }
static struct effect *from_impl(com iface) { return CONTAINING_RECORD(iface, struct effect, impl); }

static HRESULT query(struct effect *e, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown)) *out = &e->IUnknown_iface;
    else if (IsEqualGUID(iid, &IID_effect_impl)) { *out = &e->impl; impl_asked = TRUE; }
    else { *out = NULL; return E_NOINTERFACE; }
    InterlockedIncrement(&e->ref);
    return S_OK;
}

static ULONG release(struct effect *e)
{
    ULONG ref = InterlockedDecrement(&e->ref);
    if (!ref) HeapFree(GetProcessHeap(), 0, e);
    return ref;
}

static HRESULT STDMETHODCALLTYPE unk_QueryInterface(IUnknown *iface, REFIID iid, void **out) { return query(from_unknown(iface), iid, out); }
static ULONG STDMETHODCALLTYPE unk_AddRef(IUnknown *iface) { return InterlockedIncrement(&from_unknown(iface)->ref); }
static ULONG STDMETHODCALLTYPE unk_Release(IUnknown *iface) { return release(from_unknown(iface)); }
static IUnknownVtbl unk_vtbl = { unk_QueryInterface, unk_AddRef, unk_Release };

static HRESULT STDMETHODCALLTYPE impl_QueryInterface(com iface, REFIID iid, void **out) { return query(from_impl(iface), iid, out); }
static ULONG STDMETHODCALLTYPE impl_AddRef(com iface) { return InterlockedIncrement(&from_impl(iface)->ref); }
static ULONG STDMETHODCALLTYPE impl_Release(com iface) { return release(from_impl(iface)); }

static HRESULT STDMETHODCALLTYPE impl_Initialize(com iface, com context, com graph)
{
    com context2, color = NULL;

    initialized = TRUE;
    if (FAILED(SLOT(context, 0, qi_fn)(context, &IID_effect_context2, &context2)))
    {
        check(FALSE, "the effect context answers ID2D1EffectContext2");
        return S_OK;
    }
    check(TRUE, "the effect context answers ID2D1EffectContext2");
    check(SUCCEEDED(SLOT(context2, 25, from_dxgi_fn)(context2, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, &color)),
          "a colour context from a DXGI colour space");
    if (color) SLOT(color, 2, release_fn)(color);
    SLOT(context2, 2, release_fn)(context2);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE impl_PrepareForRender(com iface, UINT32 type) { return S_OK; }
static HRESULT STDMETHODCALLTYPE impl_SetGraph(com iface, com graph) { return E_NOTIMPL; }
static const impl_vtbl_t impl_vtbl =
{
    impl_QueryInterface, impl_AddRef, impl_Release, impl_Initialize, impl_PrepareForRender, impl_SetGraph
};

static HRESULT CALLBACK effect_factory(IUnknown **out)
{
    struct effect *e = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*e));
    e->IUnknown_iface.lpVtbl = &unk_vtbl;
    e->impl.lpVtbl = &impl_vtbl;
    e->ref = 1;
    *out = &e->IUnknown_iface;
    return S_OK;
}

static const WCHAR effect_xml[] =
    L"<?xml version='1.0'?><Effect>"
    L"<Property name='DisplayName' type='string' value='Check'/>"
    L"<Property name='Author' type='string' value='Raye'/>"
    L"<Property name='Category' type='string' value='Test'/>"
    L"<Property name='Description' type='string' value='Check'/>"
    L"<Inputs><Input name='Source'/></Inputs></Effect>";

int main(void)
{
    D3D_FEATURE_LEVEL level;
    ID3D11Device *d3d;
    IDXGIDevice *dxgi;
    ID2D1Factory1 *factory;
    ID2D1Device *device;
    ID2D1DeviceContext *dc;
    ID2D1ColorContext *color = NULL;  /* used through its slots */
    ID2D1Effect *effect;
    HRESULT hr;

    if (FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                 NULL, 0, D3D11_SDK_VERSION, &d3d, &level, NULL))
        && FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                    NULL, 0, D3D11_SDK_VERSION, &d3d, &level, NULL)))
    {
        printf("FAIL: no Direct3D 11 device\n");
        return 2;
    }
    ID3D11Device_QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dxgi);
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory1, NULL, (void **)&factory);
    ID2D1Factory1_CreateDevice(factory, dxgi, &device);
    ID2D1Device_CreateDeviceContext(device, D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc);

    check(SUCCEEDED(ID2D1DeviceContext_CreateColorContext(dc, D2D1_COLOR_SPACE_SRGB, NULL, 0, &color)),
          "an sRGB colour context");
    if (color)
    {
        check(SLOT(color, 4, space_fn)(color) == D2D1_COLOR_SPACE_SRGB, "it says sRGB");
        SLOT(color, 2, release_fn)(color);
    }

    hr = ID2D1Factory1_RegisterEffectFromString(factory, &CLSID_check_effect, effect_xml, NULL, 0, effect_factory);
    check(SUCCEEDED(hr), "the effect registers");
    hr = ID2D1DeviceContext_CreateEffect(dc, &CLSID_check_effect, &effect);
    check(SUCCEEDED(hr), "the effect is created");
    check(impl_asked, "Direct2D asked the factory's object for ID2D1EffectImpl");
    check(initialized, "Initialize was called on ID2D1EffectImpl");
    if (SUCCEEDED(hr)) SLOT(effect, 2, release_fn)(effect);

    SLOT(dc, 2, release_fn)(dc);
    SLOT(device, 2, release_fn)(device);
    SLOT(factory, 2, release_fn)(factory);
    IDXGIDevice_Release(dxgi);
    ID3D11Device_Release(d3d);
    printf("%s\n", failures ? "some checks failed" : "all checks passed");
    return failures ? 1 : 0;
}
