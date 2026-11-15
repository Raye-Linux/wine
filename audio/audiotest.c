/*
 * Sound checks for the Windows program layer.
 *
 *   audiotest devices                       endpoints, formats and periods
 *   audiotest wasapi [ms] [loop|-] [s]      round trip through the default output and
 *                                           input (loop: loopback capture of the output)
 *   audiotest exclusive                     what exclusive mode answers
 *   audiotest asio [frames] [s]             round trip, callback timing and xruns
 *
 * AT_RATE asks the ASIO driver for that sample rate before making its
 * buffers, as a host does when its project runs at another rate.
 *
 * AT_RENDER / AT_CAPTURE pick a device by part of its name, AT_VERBOSE
 * prints each click heard, AT_DEBUG each packet, FAKE_LOAD_MS keeps the
 * ASIO callback busy that long.
 *
 * A round trip needs the output to come back on the input: measure.sh
 * feeds the default input from the default output's monitor.
 *
 * Copyright 2026 Raye Linux
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define SPIKE_EVERY 0.5   /* seconds between test clicks */
#define THRESHOLD   0.25f

static double qpc_freq;

static double now(void)
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart / qpc_freq;
}

static double cpu_seconds(void)
{
    FILETIME c, e, k, u;
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    return (((ULONGLONG)k.dwHighDateTime << 32 | k.dwLowDateTime) +
            ((ULONGLONG)u.dwHighDateTime << 32 | u.dwLowDateTime)) / 1e7;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static void report(const char *what, double *v, int n, const char *unit)
{
    if (!n) { printf("%s: no data\n", what); return; }
    qsort(v, n, sizeof(*v), cmp_double);
    printf("%s: n=%d min=%.2f median=%.2f max=%.2f %s\n", what, n, v[0], v[n / 2], v[n - 1], unit);
}

/* devices */

static void list_flow(IMMDeviceEnumerator *en, EDataFlow flow)
{
    IMMDeviceCollection *col;
    IMMDevice *def = NULL;
    WCHAR *def_id = NULL;
    UINT i, count;

    if (SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, flow, eConsole, &def)))
        IMMDevice_GetId(def, &def_id);
    IMMDeviceEnumerator_EnumAudioEndpoints(en, flow, DEVICE_STATE_ACTIVE, &col);
    IMMDeviceCollection_GetCount(col, &count);
    printf("%s endpoints: %u\n", flow == eRender ? "render" : "capture", count);
    for (i = 0; i < count; i++)
    {
        IMMDevice *dev;
        IPropertyStore *ps;
        PROPVARIANT pv;
        IAudioClient *ac;
        WAVEFORMATEX *fmt;
        REFERENCE_TIME def_p = 0, min_p = 0;
        WCHAR *id;

        IMMDeviceCollection_Item(col, i, &dev);
        IMMDevice_GetId(dev, &id);
        IMMDevice_OpenPropertyStore(dev, STGM_READ, &ps);
        PropVariantInit(&pv);
        IPropertyStore_GetValue(ps, &PKEY_Device_FriendlyName, &pv);
        printf("  %s\"%ls\"", def_id && !wcscmp(id, def_id) ? "* " : "  ", pv.pwszVal);
        if (SUCCEEDED(IMMDevice_Activate(dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&ac)))
        {
            IAudioClient_GetDevicePeriod(ac, &def_p, &min_p);
            if (SUCCEEDED(IAudioClient_GetMixFormat(ac, &fmt)))
            {
                printf(" %luHz %uch %ubit", fmt->nSamplesPerSec, fmt->nChannels, fmt->wBitsPerSample);
                CoTaskMemFree(fmt);
            }
            printf(" period def=%.2fms min=%.2fms", def_p / 1e4, min_p / 1e4);
            IAudioClient_Release(ac);
        }
        printf("\n");
        PropVariantClear(&pv);
        IPropertyStore_Release(ps);
        CoTaskMemFree(id);
        IMMDevice_Release(dev);
    }
    IMMDeviceCollection_Release(col);
    if (def) IMMDevice_Release(def);
    CoTaskMemFree(def_id);
}

static int cmd_devices(void)
{
    IMMDeviceEnumerator *en;

    CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&en);
    list_flow(en, eRender);
    list_flow(en, eCapture);
    IMMDeviceEnumerator_Release(en);
    return 0;
}

/* WASAPI round trip */

/* endpoint whose name contains want, or NULL */
static IMMDevice *find_device(IMMDeviceEnumerator *en, EDataFlow flow, const char *want)
{
    IMMDeviceCollection *col;
    IMMDevice *ret = NULL;
    UINT i, count;
    WCHAR wwant[64];

    MultiByteToWideChar(CP_UTF8, 0, want, -1, wwant, 64);
    IMMDeviceEnumerator_EnumAudioEndpoints(en, flow, DEVICE_STATE_ACTIVE, &col);
    IMMDeviceCollection_GetCount(col, &count);
    for (i = 0; i < count && !ret; i++)
    {
        IMMDevice *dev;
        IPropertyStore *ps;
        PROPVARIANT pv;

        IMMDeviceCollection_Item(col, i, &dev);
        IMMDevice_OpenPropertyStore(dev, STGM_READ, &ps);
        PropVariantInit(&pv);
        IPropertyStore_GetValue(ps, &PKEY_Device_FriendlyName, &pv);
        if (wcsstr(pv.pwszVal, wwant))
        {
            ret = dev;
            printf("%s \"%ls\"\n", flow == eCapture ? "capturing from" : "playing to", pv.pwszVal);
        }
        else IMMDevice_Release(dev);
        PropVariantClear(&pv);
        IPropertyStore_Release(ps);
    }
    IMMDeviceCollection_Release(col);
    return ret;
}

static int cmd_wasapi(int period_ms, int loop, int seconds)
{
    IMMDeviceEnumerator *en;
    IMMDevice *out_dev, *in_dev;
    IAudioClient *out, *in;
    IAudioRenderClient *render;
    IAudioCaptureClient *capture;
    WAVEFORMATEX *fmt;
    HANDLE ev_out, ev_in;
    REFERENCE_TIME period = period_ms * 10000, def_p, min_p;
    UINT32 bufsize, rate, ch;
    UINT64 written = 0, read = 0, next_spike;
    double spike_time[256], spike_queued[256], lat[256], after[256], cpu0, t0;
    int n_spikes = 0, n_lat = 0, waiting = 0, glitches = 0;
    HRESULT hr;

    CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&en);
    if (!getenv("AT_RENDER") || !(out_dev = find_device(en, eRender, getenv("AT_RENDER"))))
        IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &out_dev);
    IMMDevice_Activate(out_dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&out);
    if (loop)
        IMMDevice_Activate(out_dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&in);
    else
    {
        if (!getenv("AT_CAPTURE") || !(in_dev = find_device(en, eCapture, getenv("AT_CAPTURE"))))
            IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eCapture, eConsole, &in_dev);
        IMMDevice_Activate(in_dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&in);
    }
    IAudioClient_GetMixFormat(out, &fmt);
    rate = fmt->nSamplesPerSec;
    ch = fmt->nChannels;
    IAudioClient_GetDevicePeriod(out, &def_p, &min_p);
    if (!period) period = def_p;
    printf("format %uHz %uch, device period def=%.2fms min=%.2fms, asking %.2fms\n",
           rate, ch, def_p / 1e4, min_p / 1e4, period / 1e4);

    ev_out = CreateEventW(NULL, FALSE, FALSE, NULL);
    ev_in = CreateEventW(NULL, FALSE, FALSE, NULL);
    hr = IAudioClient_Initialize(out, AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, 0, fmt, NULL);
    if (FAILED(hr)) { printf("render Initialize failed %#lx\n", hr); return 1; }
    hr = IAudioClient_Initialize(in, AUDCLNT_SHAREMODE_SHARED,
                                 loop ? AUDCLNT_STREAMFLAGS_LOOPBACK : AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                 period, 0, fmt, NULL);
    if (FAILED(hr)) { printf("capture Initialize failed %#lx\n", hr); return 1; }
    IAudioClient_SetEventHandle(out, ev_out);
    if (!loop) IAudioClient_SetEventHandle(in, ev_in);
    IAudioClient_GetBufferSize(out, &bufsize);
    IAudioClient_GetService(out, &IID_IAudioRenderClient, (void **)&render);
    IAudioClient_GetService(in, &IID_IAudioCaptureClient, (void **)&capture);

    next_spike = rate / 2;
    IAudioClient_Start(in);
    IAudioClient_Start(out);
    t0 = now();
    cpu0 = cpu_seconds();
    while (now() - t0 < seconds)
    {
        UINT32 pad, avail, i, j, frames;
        BYTE *data;
        DWORD flags;
        UINT64 dpos, qpos;

        WaitForSingleObject(ev_out, loop ? 2 : 20);
        IAudioClient_GetCurrentPadding(out, &pad);
        avail = bufsize - pad;
        if (avail && SUCCEEDED(IAudioRenderClient_GetBuffer(render, avail, &data)))
        {
            float *f = (float *)data;
            double t = now();
            memset(data, 0, avail * fmt->nBlockAlign);
            for (i = 0; i < avail; i++, written++)
                if (written == next_spike)
                {
                    for (j = 0; j < ch; j++) f[i * ch + j] = 0.9f;
                    /* when the click leaves the program: written now, behind pad + i frames */
                    /* handed over now, but i frames into this chunk; pad frames are queued before it */
                    if (n_spikes < 256)
                    {
                        spike_queued[n_spikes] = (double)pad / rate * 1000;
                        spike_time[n_spikes++] = t + (double)i / rate;
                    }
                    next_spike += (UINT64)(SPIKE_EVERY * rate);
                    waiting++;
                }
            IAudioRenderClient_ReleaseBuffer(render, avail, 0);
        }
        for (;;)
        {
            hr = IAudioCaptureClient_GetBuffer(capture, &data, &frames, &flags, &dpos, &qpos);
            if (FAILED(hr) && getenv("AT_DEBUG")) printf("%.3f s: capture GetBuffer %#lx\n", now() - t0, hr);
            if (FAILED(hr) || !frames) break;
            float *f = (float *)data;
            double t = now();
            if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) glitches++;
            if (getenv("AT_DEBUG"))
            {
                float peak = 0;
                for (i = 0; i < frames * ch; i++) if (fabsf(f[i]) > peak) peak = fabsf(f[i]);
                if (peak > 0.01f || (flags & AUDCLNT_BUFFERFLAGS_SILENT))
                    printf("%.3f s: packet %u frames flags %#lx peak %.2f read %llu written %llu\n",
                           t - t0, frames, flags, peak, read, written);
            }
            for (i = 0; i < frames; i++, read++)
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && f[i * ch] > THRESHOLD && n_lat < 256)
                {
                    /* the program sees it now; frames after it in this packet came later */
                    double heard = t - (double)(frames - 1 - i) / rate;
                    int k = n_spikes - 1;

                    while (k > 0 && spike_time[k] > heard) k--;
                    lat[n_lat] = (heard - spike_time[k]) * 1000;
                    after[n_lat] = lat[n_lat] - spike_queued[k];
                    if (getenv("AT_VERBOSE")) printf("heard click %d at %.2f s, %.1f ms\n", k, t - t0, lat[n_lat]);
                    n_lat++;
                    i += 64; read += 64;   /* skip the rest of the click */
                }
            IAudioCaptureClient_ReleaseBuffer(capture, frames);
        }
    }
    printf("cpu %.1f%% of one core\n", (cpu_seconds() - cpu0) * 100 / (now() - t0));
    printf("clicks sent %d, heard %d, discontinuities %d\n", n_spikes, n_lat, glitches);
    report("round trip", lat, n_lat, "ms");
    report("of which after the program's queue", after, n_lat, "ms");
    IAudioClient_Stop(out);
    IAudioClient_Stop(in);
    return 0;
}

/* exclusive mode: what a program that asks for it gets */
static int cmd_exclusive(void)
{
    IMMDeviceEnumerator *en;
    IMMDevice *dev;
    IAudioClient *ac;
    WAVEFORMATEX *mix;
    WAVEFORMATEXTENSIBLE fmt;
    REFERENCE_TIME def_p, min_p;
    HRESULT hr;
    int event;

    CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&en);
    IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &dev);
    for (event = 0; event < 2; event++)
    {
        IMMDevice_Activate(dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&ac);
        IAudioClient_GetMixFormat(ac, &mix);
        memcpy(&fmt, mix, sizeof(fmt));
        IAudioClient_GetDevicePeriod(ac, &def_p, &min_p);
        hr = IAudioClient_IsFormatSupported(ac, AUDCLNT_SHAREMODE_EXCLUSIVE, &fmt.Format, NULL);
        printf("exclusive IsFormatSupported: %#lx\n", hr);
        hr = IAudioClient_Initialize(ac, AUDCLNT_SHAREMODE_EXCLUSIVE, event ? AUDCLNT_STREAMFLAGS_EVENTCALLBACK : 0,
                                     event ? min_p : 8 * min_p, event ? min_p : 0, &fmt.Format, NULL);
        printf("exclusive Initialize (%s, period %.2f ms): %#lx%s\n", event ? "event" : "polling", min_p / 1e4, hr,
               hr == AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED ? " (exclusive mode not allowed: use shared)" :
               hr == AUDCLNT_E_DEVICE_IN_USE ? " (device in use)" : "");
        CoTaskMemFree(mix);
        IAudioClient_Release(ac);
    }
    return 0;
}

/* ASIO host */

struct asio_samples { ULONG hi, lo; };
struct asio_time
{
    LONG reserved[4];
    struct { double speed; struct asio_samples system_time, sample_position; double sample_rate; ULONG flags; char reserved[12]; } info;
    struct { double speed; struct asio_samples samples; ULONG flags; char future[64]; } code;
};
struct asio_callbacks
{
    void (__cdecl *buffer_switch)(LONG, LONG);
    void (__cdecl *rate_changed)(double);
    LONG (__cdecl *message)(LONG, LONG, void *, double *);
    struct asio_time *(__cdecl *buffer_switch_time_info)(struct asio_time *, LONG, LONG);
};
struct asio_buffer_info { LONG is_input, channel; void *buffers[2]; };
struct asio_channel_info { LONG channel, is_input, is_active, group, type; char name[32]; };

#ifdef __i386__
#define ASIOCALL __thiscall
#else
#define ASIOCALL
#endif

typedef struct iasio iasio;
struct iasio_vtbl
{
    HRESULT (WINAPI *QueryInterface)(iasio *, REFIID, void **);
    ULONG (WINAPI *AddRef)(iasio *);
    ULONG (WINAPI *Release)(iasio *);
    LONG (ASIOCALL *init)(iasio *, void *);
    void (ASIOCALL *get_driver_name)(iasio *, char *);
    LONG (ASIOCALL *get_driver_version)(iasio *);
    void (ASIOCALL *get_error_message)(iasio *, char *);
    LONG (ASIOCALL *start)(iasio *);
    LONG (ASIOCALL *stop)(iasio *);
    LONG (ASIOCALL *get_channels)(iasio *, LONG *, LONG *);
    LONG (ASIOCALL *get_latencies)(iasio *, LONG *, LONG *);
    LONG (ASIOCALL *get_buffer_size)(iasio *, LONG *, LONG *, LONG *, LONG *);
    LONG (ASIOCALL *can_sample_rate)(iasio *, double);
    LONG (ASIOCALL *get_sample_rate)(iasio *, double *);
    LONG (ASIOCALL *set_sample_rate)(iasio *, double);
    LONG (ASIOCALL *get_clock_sources)(iasio *, void *, LONG *);
    LONG (ASIOCALL *set_clock_source)(iasio *, LONG);
    LONG (ASIOCALL *get_sample_position)(iasio *, struct asio_samples *, struct asio_samples *);
    LONG (ASIOCALL *get_channel_info)(iasio *, struct asio_channel_info *);
    LONG (ASIOCALL *create_buffers)(iasio *, struct asio_buffer_info *, LONG, LONG, struct asio_callbacks *);
    LONG (ASIOCALL *dispose_buffers)(iasio *);
    LONG (ASIOCALL *control_panel)(iasio *);
    LONG (ASIOCALL *future)(iasio *, LONG, void *);
    LONG (ASIOCALL *output_ready)(iasio *);
};
struct iasio { const struct iasio_vtbl *vtbl; };

static struct asio_buffer_info bufs[2];
static LONG frames;
static double rate;
static volatile LONG callbacks, overloads, pos_jumps;
static UINT64 last_pos = ~0ull, out_frame, next_click, click_at[256];
static int n_clicks, n_heard;
static double asio_lat[256], intervals[200000], last_call, work_ms[200000];
static int n_int, n_work;
static double fake_load_ms;

static void process(LONG index, UINT64 pos)
{
    float *in = bufs[0].buffers[index], *out = bufs[1].buffers[index];
    double t = now();
    LONG i;

    if (last_call && n_int < 200000) intervals[n_int++] = (t - last_call) * 1000;
    last_call = t;
    if (last_pos != ~0ull && pos != last_pos + frames) pos_jumps++;
    last_pos = pos;
    callbacks++;

    for (i = 0; i < frames; i++)
        if (in[i] > THRESHOLD && n_clicks && n_heard < 256)
        {
            /* the last click sent before this frame */
            int k = n_clicks - 1;
            while (k > 0 && click_at[k] > out_frame + i) k--;
            asio_lat[n_heard++] = (double)(out_frame + i - click_at[k]);
            break;
        }
    memset(out, 0, frames * sizeof(float));
    for (i = 0; i < frames; i++)
        if (out_frame + i == next_click)
        {
            out[i] = 0.9f;
            if (n_clicks < 256) click_at[n_clicks++] = next_click;
            next_click += (UINT64)(SPIKE_EVERY * rate);
        }
    out_frame += frames;
    /* pretend to be a busy plug-in chain */
    if (fake_load_ms) while ((now() - t) * 1000 < fake_load_ms) ;
    if (n_work < 200000) work_ms[n_work++] = (now() - t) * 1000;
}

static void __cdecl cb_switch(LONG index, LONG direct)
{
    process(index, out_frame);
}

static struct asio_time *__cdecl cb_switch_time(struct asio_time *t, LONG index, LONG direct)
{
    process(index, (UINT64)t->info.sample_position.hi << 32 | t->info.sample_position.lo);
    return NULL;
}

static void __cdecl cb_rate(double r) { printf("rate changed to %f\n", r); }

static LONG __cdecl cb_message(LONG sel, LONG value, void *msg, double *opt)
{
    switch (sel)
    {
    case 1: return value == 7 || value == 15 || value == 3 || value == 6;
    case 7: return 1;
    case 15: overloads++; return 1;
    case 3: printf("driver asks for a reset\n"); return 1;
    default: return 0;
    }
}

static int find_driver(CLSID *clsid, char *name, size_t len)
{
    HKEY key, sub;
    char sub_name[64], value[64];
    DWORD i, size;
    WCHAR wvalue[64];

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\ASIO", 0, KEY_READ, &key)) return 0;
    for (i = 0; !RegEnumKeyA(key, i, sub_name, sizeof(sub_name)); i++)
    {
        size = sizeof(value);
        if (!RegOpenKeyExA(key, sub_name, 0, KEY_READ, &sub) &&
            !RegQueryValueExA(sub, "CLSID", NULL, NULL, (BYTE *)value, &size))
        {
            printf("ASIO driver found: \"%s\" %s\n", sub_name, value);
            MultiByteToWideChar(CP_ACP, 0, value, -1, wvalue, 64);
            CLSIDFromString(wvalue, clsid);
            snprintf(name, len, "%s", sub_name);
            RegCloseKey(sub);
            RegCloseKey(key);
            return 1;
        }
    }
    RegCloseKey(key);
    return 0;
}

static int cmd_asio(LONG want, int seconds)
{
    static struct asio_callbacks cbs = { cb_switch, cb_rate, cb_message, cb_switch_time };
    struct asio_channel_info info;
    LONG nin, nout, minb, maxb, pref, gran, lin, lout, r;
    char name[64], drvname[33] = "", err[128] = "";
    CLSID clsid;
    iasio *drv;
    double t0, cpu0, cpu;

    if (!find_driver(&clsid, name, sizeof(name))) { printf("no ASIO driver registered\n"); return 1; }
    if (FAILED(CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER, &clsid, (void **)&drv)))
    { printf("CoCreateInstance failed\n"); return 1; }
    if (!drv->vtbl->init(drv, GetDesktopWindow()))
    {
        drv->vtbl->get_error_message(drv, err);
        printf("init failed: %s\n", err);
        return 1;
    }
    drv->vtbl->get_driver_name(drv, drvname);
    drv->vtbl->get_channels(drv, &nin, &nout);
    drv->vtbl->get_buffer_size(drv, &minb, &maxb, &pref, &gran);
    drv->vtbl->get_sample_rate(drv, &rate);
    info.channel = 0; info.is_input = 1;
    drv->vtbl->get_channel_info(drv, &info);
    printf("driver \"%s\" v%ld: %ld in / %ld out, %.0f Hz, buffer min %ld max %ld preferred %ld gran %ld, in 1 is \"%s\"\n",
           drvname, drv->vtbl->get_driver_version(drv), nin, nout, rate, minb, maxb, pref, gran, info.name);
    printf("can do 44100: %ld, 48000: %ld\n", drv->vtbl->can_sample_rate(drv, 44100.0),
           drv->vtbl->can_sample_rate(drv, 48000.0));
    if (getenv("AT_RATE"))
    {
        double want_rate = atof(getenv("AT_RATE"));
        r = drv->vtbl->set_sample_rate(drv, want_rate);
        drv->vtbl->get_sample_rate(drv, &rate);
        printf("setSampleRate(%.0f) %ld, now %.0f Hz\n", want_rate, r, rate);
    }

    frames = want ? want : pref;
    bufs[0].is_input = 1; bufs[0].channel = 0;
    bufs[1].is_input = 0; bufs[1].channel = 0;
    if ((r = drv->vtbl->create_buffers(drv, bufs, 2, frames, &cbs)))
    { printf("createBuffers(%ld) failed %ld\n", frames, r); return 1; }
    drv->vtbl->get_latencies(drv, &lin, &lout);
    printf("buffers of %ld frames (%.2f ms), reported latency in %ld out %ld frames\n",
           frames, frames * 1000.0 / rate, lin, lout);

    next_click = (UINT64)rate / 2;
    t0 = now();
    cpu0 = cpu_seconds();
    drv->vtbl->start(drv);
    Sleep(seconds * 1000);
    drv->vtbl->stop(drv);
    cpu = cpu_seconds() - cpu0;
    t0 = now() - t0;
    printf("callbacks %ld in %.1f s (expected %.0f), position jumps %ld, overload messages %ld\n",
           callbacks, t0, t0 * rate / frames, pos_jumps, overloads);
    printf("cpu %.1f%% of one core (fake load %.2f ms per callback)\n", cpu * 100 / t0, fake_load_ms);
    printf("clicks sent %d, heard %d\n", n_clicks, n_heard);
    if (n_heard)
    {
        int i;
        for (i = 0; i < n_heard; i++) asio_lat[i] = asio_lat[i] * 1000 / rate;
        report("round trip", asio_lat, n_heard, "ms");
    }
    report("callback interval", intervals, n_int, "ms");
    report("time in callback", work_ms, n_work, "ms");
    drv->vtbl->dispose_buffers(drv);
    drv->vtbl->Release(drv);
    return 0;
}

int main(int argc, char **argv)
{
    LARGE_INTEGER f;
    int ret = 1;

    QueryPerformanceFrequency(&f);
    qpc_freq = f.QuadPart;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (getenv("FAKE_LOAD_MS")) fake_load_ms = atof(getenv("FAKE_LOAD_MS"));
    if (argc > 1 && !strcmp(argv[1], "devices")) ret = cmd_devices();
    else if (argc > 1 && !strcmp(argv[1], "wasapi"))
        ret = cmd_wasapi(argc > 2 ? atoi(argv[2]) : 0, argc > 3 && !strcmp(argv[3], "loop"),
                         argc > 4 ? atoi(argv[4]) : 10);
    else if (argc > 1 && !strcmp(argv[1], "exclusive")) ret = cmd_exclusive();
    else if (argc > 1 && !strcmp(argv[1], "asio"))
        ret = cmd_asio(argc > 2 ? atoi(argv[2]) : 0, argc > 3 ? atoi(argv[3]) : 10);
    else
        printf("usage: audiotest devices | wasapi [period_ms] [loop|-] [seconds] | asio [frames] [seconds]\n");
    CoUninitialize();
    return ret;
}
