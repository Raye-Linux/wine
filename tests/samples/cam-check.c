/* Lists the cameras a Windows program sees and reads frames from one.
 *
 *   cam-check.exe              both lists, then 30 frames from the first camera
 *   cam-check.exe list         the lists only
 *   cam-check.exe read N TYPE  N frames, TYPE is a format (NV12, YUY2, MJPG...) or a type index
 *   cam-check.exe link         open the first camera again by its symbolic link
 *   cam-check.exe rgb          ask the reader for RGB32 and let it convert, the way OpenCV does
 *   cam-check.exe again        the same camera started three times, in three formats
 *
 * CAM_CHECK_DUMP=file writes the 11th frame there, as the camera gave it.
 *
 * One line per fact, so the output can be compared between runs. Exit code
 * 0 when everything asked for worked. */

#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <dshow.h>

static void fourcc(const GUID *subtype, char out[5])
{
    DWORD f = subtype->Data1;
    int i;

    for (i = 0; i < 4; i++)
    {
        char c = (char)(f >> (8 * i));
        out[i] = (c >= 32 && c < 127) ? c : '?';
    }
    out[4] = 0;
    if (f == 20) strcpy(out, "RGB24");
    else if (f == 22) strcpy(out, "RGB32");
}

static int list_directshow(void)
{
    ICreateDevEnum *devenum;
    IEnumMoniker *monikers = NULL;
    IMoniker *moniker;
    int count = 0;

    if (FAILED(CoCreateInstance(&CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
            &IID_ICreateDevEnum, (void **)&devenum)))
    {
        printf("directshow: no device enumerator\n");
        return 1;
    }
    if (ICreateDevEnum_CreateClassEnumerator(devenum, &CLSID_VideoInputDeviceCategory, &monikers, 0) == S_OK)
    {
        while (IEnumMoniker_Next(monikers, 1, &moniker, NULL) == S_OK)
        {
            IPropertyBag *bag;
            VARIANT name, path;

            VariantInit(&name);
            VariantInit(&path);
            if (SUCCEEDED(IMoniker_BindToStorage(moniker, NULL, NULL, &IID_IPropertyBag, (void **)&bag)))
            {
                IPropertyBag_Read(bag, L"FriendlyName", &name, NULL);
                IPropertyBag_Read(bag, L"DevicePath", &path, NULL);
                IPropertyBag_Release(bag);
            }
            printf("directshow camera: %ls | %ls\n", name.vt == VT_BSTR ? name.bstrVal : L"?",
                    path.vt == VT_BSTR ? path.bstrVal : L"-");
            VariantClear(&name);
            VariantClear(&path);
            IMoniker_Release(moniker);
            count++;
        }
        IEnumMoniker_Release(monikers);
    }
    ICreateDevEnum_Release(devenum);
    printf("directshow cameras: %d\n", count);
    return 0;
}

static HRESULT enum_mf(IMFActivate ***devices, UINT32 *count)
{
    IMFAttributes *attrs;
    HRESULT hr;

    MFCreateAttributes(&attrs, 1);
    IMFAttributes_SetGUID(attrs, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    hr = MFEnumDeviceSources(attrs, devices, count);
    IMFAttributes_Release(attrs);
    return hr;
}

static int list_mf(void)
{
    IMFActivate **devices = NULL;
    UINT32 count = 0, i;
    HRESULT hr;

    if (FAILED(hr = enum_mf(&devices, &count)))
    {
        printf("media foundation: MFEnumDeviceSources failed %#lx\n", hr);
        return 1;
    }
    for (i = 0; i < count; i++)
    {
        WCHAR *name = NULL, *link = NULL;
        UINT32 len;

        IMFActivate_GetAllocatedString(devices[i], &MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &len);
        IMFActivate_GetAllocatedString(devices[i], &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &len);
        printf("mf camera: %ls | %ls\n", name ? name : L"?", link ? link : L"-");
        CoTaskMemFree(name);
        CoTaskMemFree(link);
        IMFActivate_Release(devices[i]);
    }
    CoTaskMemFree(devices);
    printf("mf cameras: %u\n", count);
    return 0;
}

static IMFMediaSource *first_source(BOOL by_link)
{
    IMFActivate **devices = NULL;
    IMFMediaSource *source = NULL;
    UINT32 count = 0, i;
    HRESULT hr;

    if (FAILED(enum_mf(&devices, &count)) || !count)
    {
        printf("read: no camera\n");
        return NULL;
    }
    if (by_link)
    {
        IMFAttributes *attrs;
        WCHAR *link = NULL;
        UINT32 len;

        IMFActivate_GetAllocatedString(devices[0], &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &len);
        MFCreateAttributes(&attrs, 2);
        IMFAttributes_SetGUID(attrs, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        IMFAttributes_SetString(attrs, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, link ? link : L"");
        hr = MFCreateDeviceSource(attrs, &source);
        printf("link: MFCreateDeviceSource %#lx\n", hr);
        CoTaskMemFree(link);
        IMFAttributes_Release(attrs);
    }
    else
    {
        hr = IMFActivate_ActivateObject(devices[0], &IID_IMFMediaSource, (void **)&source);
        printf("read: ActivateObject %#lx\n", hr);
    }
    for (i = 0; i < count; i++) IMFActivate_Release(devices[i]);
    CoTaskMemFree(devices);
    return SUCCEEDED(hr) ? source : NULL;
}

/* One reader on the source: picks a type, reads, then lets the source go
 * (stopped, not shut down) so the next round can start it again. */
static int read_round(IMFMediaSource *source, int frames, const char *want, BOOL rgb)
{
    IMFSourceReader *reader;
    IMFMediaType *type, *chosen = NULL;
    IMFAttributes *attrs;
    char name[8], chosen_name[8] = "";
    UINT64 last_time = 0, size = 0, rate = 0;
    int i, good = 0, index = 0;
    HRESULT hr;

    MFCreateAttributes(&attrs, 2);
    IMFAttributes_SetUINT32(attrs, &MF_SOURCE_READER_DISCONNECT_MEDIASOURCE_ON_SHUTDOWN, TRUE);
    if (rgb)
        IMFAttributes_SetUINT32(attrs, &MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    hr = MFCreateSourceReaderFromMediaSource(source, attrs, &reader);
    IMFAttributes_Release(attrs);
    if (FAILED(hr))
    {
        printf("read: MFCreateSourceReaderFromMediaSource failed %#lx\n", hr);
        return 0;
    }

    while (IMFSourceReader_GetNativeMediaType(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &type) == S_OK)
    {
        UINT64 s = 0, r = 0;
        GUID subtype;

        IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &subtype);
        IMFMediaType_GetUINT64(type, &MF_MT_FRAME_SIZE, &s);
        IMFMediaType_GetUINT64(type, &MF_MT_FRAME_RATE, &r);
        fourcc(&subtype, name);
        printf("type %d: %s %ux%u %.2f fps\n", index, name, (UINT32)(s >> 32), (UINT32)s,
                (double)(r >> 32) / ((UINT32)r ? (UINT32)r : 1));
        if (!chosen && (!want || !strcmp(want, name) || (want[0] >= '0' && want[0] <= '9' && atoi(want) == index)))
        {
            chosen = type;
            IMFMediaType_AddRef(chosen);
            strcpy(chosen_name, name);
            size = s;
            rate = r;
        }
        IMFMediaType_Release(type);
        index++;
    }
    printf("types: %d\n", index);
    if (!chosen)
    {
        printf("read: no type %s\n", want ? want : "");
        IMFSourceReader_Release(reader);
        return 0;
    }
    hr = IMFSourceReader_SetCurrentMediaType(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, chosen);
    IMFMediaType_Release(chosen);
    if (FAILED(hr))
    {
        printf("read: SetCurrentMediaType failed %#lx\n", hr);
        IMFSourceReader_Release(reader);
        return 0;
    }
    if (rgb)
    {
        IMFMediaType *out;

        /* size and rate as the camera gives them, the format converted */
        MFCreateMediaType(&out);
        IMFMediaType_SetGUID(out, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
        IMFMediaType_SetGUID(out, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);
        IMFMediaType_SetUINT64(out, &MF_MT_FRAME_SIZE, size);
        IMFMediaType_SetUINT64(out, &MF_MT_FRAME_RATE, rate);
        hr = IMFSourceReader_SetCurrentMediaType(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, out);
        IMFMediaType_Release(out);
        printf("read: RGB32 output %#lx\n", hr);
        if (FAILED(hr))
        {
            IMFSourceReader_Release(reader);
            return 0;
        }
        strcpy(chosen_name, "RGB32");
    }
    printf("read: %s %ux%u\n", chosen_name, (UINT32)(size >> 32), (UINT32)size);

    for (i = 0; i < frames; i++)
    {
        DWORD flags = 0, stream, length = 0;
        LONGLONG time = 0;
        IMFMediaBuffer *buffer;
        IMFSample *sample = NULL;
        BYTE *data;

        hr = IMFSourceReader_ReadSample(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &stream, &flags, &time, &sample);
        if (FAILED(hr) || !sample)
        {
            printf("frame %d: hr %#lx flags %#lx\n", i, hr, flags);
            if (FAILED(hr) || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM))) break;
            continue;
        }
        if (SUCCEEDED(IMFSample_ConvertToContiguousBuffer(sample, &buffer)))
        {
            DWORD sum = 0, j, distinct = 0;
            BYTE first;

            IMFMediaBuffer_Lock(buffer, &data, NULL, &length);
            if (i == 10 && getenv("CAM_CHECK_DUMP"))
            {
                FILE *f = fopen(getenv("CAM_CHECK_DUMP"), "wb");
                if (f)
                {
                    fwrite(data, 1, length, f);
                    fclose(f);
                }
            }
            first = length ? data[0] : 0;
            for (j = 0; j < length; j++)
            {
                sum = sum * 31 + data[j];
                if (data[j] != first) distinct++;
            }
            IMFMediaBuffer_Unlock(buffer);
            IMFMediaBuffer_Release(buffer);
            if (i < 3 || i == frames - 1)
                printf("frame %d: %lu bytes, time %.3f s, checksum %08lx\n", i, length, time / 1e7, sum);
            if (length && distinct && (i == 0 || (UINT64)time >= last_time)) good++;
            last_time = time;
        }
        IMFSample_Release(sample);
    }
    IMFSourceReader_Release(reader);
    return good;
}

static int read_frames(int frames, const char *want, BOOL by_link, BOOL rgb)
{
    IMFMediaSource *source;
    DWORD characteristics;
    int good;

    if (!(source = first_source(by_link)))
        return 1;
    IMFMediaSource_GetCharacteristics(source, &characteristics);
    printf("read: live %s\n", characteristics & MFMEDIASOURCE_IS_LIVE ? "yes" : "no");
    good = read_round(source, frames, want, rgb);
    printf("frames: %d of %d\n", good, frames);
    IMFMediaSource_Shutdown(source);
    IMFMediaSource_Release(source);
    return good == frames ? 0 : 1;
}

/* the same source started three times, each time in another format */
static int read_again(void)
{
    static const char *formats[] = {"YUY2", "MJPG", "NV12"};
    IMFMediaSource *source;
    int round, good = 0;

    if (!(source = first_source(FALSE)))
        return 1;
    for (round = 0; round < 3; round++)
    {
        int n = read_round(source, 15, formats[round], FALSE);
        printf("round %d: %s %d of 15\n", round, formats[round], n);
        good += n;
    }
    printf("frames: %d of 45\n", good);
    IMFMediaSource_Shutdown(source);
    IMFMediaSource_Release(source);
    return good == 45 ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *what = argc > 1 ? argv[1] : "all";
    int ret = 0;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_FULL);

    if (!strcmp(what, "all") || !strcmp(what, "list"))
    {
        ret |= list_directshow();
        ret |= list_mf();
    }
    if (!strcmp(what, "all"))
        ret |= read_frames(30, NULL, FALSE, FALSE);
    else if (!strcmp(what, "read"))
        ret |= read_frames(argc > 2 ? atoi(argv[2]) : 30, argc > 3 ? argv[3] : NULL, FALSE, FALSE);
    else if (!strcmp(what, "link"))
        ret |= read_frames(5, NULL, TRUE, FALSE);
    else if (!strcmp(what, "rgb"))
        ret |= read_frames(30, argc > 2 ? argv[2] : NULL, FALSE, TRUE);
    else if (!strcmp(what, "again"))
        ret |= read_again();

    MFShutdown();
    CoUninitialize();
    printf("result: %s\n", ret ? "failed" : "ok");
    return ret;
}
