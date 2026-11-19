/*
 * MIDI output check: lists the MIDI outputs a Windows program sees, then
 * plays a few seconds through the MIDI Mapper, the way old games and MIDI
 * players do: a GS reset (SysEx), a piano, a scale and a chord.
 *
 *   midi-out.exe [seconds]
 *
 * Exit code 0 when the mapper opened and every message was taken.
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>

static const char *technology(WORD t)
{
    switch (t)
    {
    case MOD_MIDIPORT: return "port";
    case MOD_SYNTH: return "synth";
    case MOD_SQSYNTH: return "square synth";
    case MOD_FMSYNTH: return "FM synth";
    case MOD_MAPPER: return "mapper";
    case MOD_WAVETABLE: return "wavetable";
    case MOD_SWSYNTH: return "software synth";
    }
    return "?";
}

int main(int argc, char **argv)
{
    static BYTE gs_reset[] = { 0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41, 0xf7 };
    static const BYTE scale[] = { 60, 62, 64, 65, 67, 69, 71, 72 };
    int seconds = argc > 1 ? atoi(argv[1]) : 4, fails = 0;
    UINT count = midiOutGetNumDevs(), i;
    MIDIOUTCAPSA caps;
    MIDIHDR hdr = {0};
    HMIDIOUT out;
    DWORD start;
    MMRESULT r;

    printf("%u MIDI output(s)\n", count);
    for (i = 0; i < count; i++)
        if (!midiOutGetDevCapsA(i, &caps, sizeof(caps)))
            printf("midiOut %u: %s (%s, %u voices)\n", i, caps.szPname, technology(caps.wTechnology), caps.wVoices);
    if (!midiOutGetDevCapsA(MIDI_MAPPER, &caps, sizeof(caps)))
        printf("mapper: %s\n", caps.szPname);

    start = GetTickCount();
    if ((r = midiOutOpen(&out, MIDI_MAPPER, 0, 0, CALLBACK_NULL)))
    {
        printf("FAIL: midiOutOpen(MIDI_MAPPER): %u\n", r);
        return 1;
    }
    printf("opened in %lu ms\n", GetTickCount() - start);

    hdr.lpData = (LPSTR)gs_reset;
    hdr.dwBufferLength = sizeof(gs_reset);
    if (midiOutPrepareHeader(out, &hdr, sizeof(hdr)) || midiOutLongMsg(out, &hdr, sizeof(hdr))) fails++;
    Sleep(50);
    midiOutUnprepareHeader(out, &hdr, sizeof(hdr));

    if (midiOutShortMsg(out, 0x00c0)) fails++;          /* piano */
    if (midiOutShortMsg(out, 0x6407b0)) fails++;        /* volume 100 */
    start = GetTickCount();
    while (GetTickCount() - start < (DWORD)seconds * 1000)
    {
        for (i = 0; i < sizeof(scale); i++)
        {
            if (midiOutShortMsg(out, 0x700090 | (scale[i] << 8))) fails++;
            Sleep(120);
            if (midiOutShortMsg(out, 0x000080 | (scale[i] << 8))) fails++;
        }
        if (midiOutShortMsg(out, 0x703c90) || midiOutShortMsg(out, 0x704090) || midiOutShortMsg(out, 0x704390)) fails++;
        Sleep(600);
        if (midiOutShortMsg(out, 0x3c80) || midiOutShortMsg(out, 0x4080) || midiOutShortMsg(out, 0x4380)) fails++;
    }
    midiOutReset(out);
    midiOutClose(out);
    printf("%s\n", fails ? "FAIL: some messages were refused" : "played");
    return fails ? 1 : 0;
}
