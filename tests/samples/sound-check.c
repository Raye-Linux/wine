/*
 * Sound test: lists the playback devices Windows programs see, names the
 * default one, then plays a one second 440 Hz tone on it through
 * waveOut. With PipeWire the default device is the system's default.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <math.h>
#include <stdio.h>

#define RATE 48000

static void list_devices(void)
{
    IMMDeviceEnumerator *en;
    IMMDeviceCollection *all;
    IMMDevice *dev;
    UINT count, i;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                                &IID_IMMDeviceEnumerator, (void **)&en)))
    {
        printf("no sound devices\n");
        return;
    }
    IMMDeviceEnumerator_EnumAudioEndpoints(en, eRender, DEVICE_STATE_ACTIVE, &all);
    IMMDeviceCollection_GetCount(all, &count);
    for (i = 0; i < count; i++)
    {
        IPropertyStore *props;
        PROPVARIANT name;

        IMMDeviceCollection_Item(all, i, &dev);
        IMMDevice_OpenPropertyStore(dev, STGM_READ, &props);
        PropVariantInit(&name);
        IPropertyStore_GetValue(props, &PKEY_Device_FriendlyName, &name);
        printf("playback: %ls\n", name.pwszVal);
        PropVariantClear(&name);
        IPropertyStore_Release(props);
        IMMDevice_Release(dev);
    }
    if (SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &dev)))
    {
        IPropertyStore *props;
        PROPVARIANT name;

        IMMDevice_OpenPropertyStore(dev, STGM_READ, &props);
        PropVariantInit(&name);
        IPropertyStore_GetValue(props, &PKEY_Device_FriendlyName, &name);
        printf("default: %ls\n", name.pwszVal);
    }
}

static int play_tone(void)
{
    WAVEFORMATEX fmt = {WAVE_FORMAT_PCM, 1, RATE, RATE * 2, 2, 16, 0};
    static short samples[RATE];
    WAVEHDR hdr = {0};
    HWAVEOUT out;
    MMRESULT r;
    int i;

    for (i = 0; i < RATE; i++)
        samples[i] = (short)(sin(2 * 3.14159265358979 * 440 * i / RATE) * 8000);
    if ((r = waveOutOpen(&out, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL)))
    {
        printf("waveOutOpen failed: %u\n", r);
        return 1;
    }
    hdr.lpData = (char *)samples;
    hdr.dwBufferLength = sizeof(samples);
    waveOutPrepareHeader(out, &hdr, sizeof(hdr));
    waveOutWrite(out, &hdr, sizeof(hdr));
    while (!(hdr.dwFlags & WHDR_DONE)) Sleep(50);
    waveOutUnprepareHeader(out, &hdr, sizeof(hdr));
    waveOutClose(out);
    printf("tone played\n");
    return 0;
}

int main(void)
{
    WAVEOUTCAPSW caps;

    list_devices();
    if (!waveOutGetDevCapsW(WAVE_MAPPER, &caps, sizeof(caps)))
        printf("mapper: %ls\n", caps.szPname);
    return play_tone();
}
