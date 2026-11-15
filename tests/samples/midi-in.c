/*
 * MIDI input check: lists the MIDI inputs a Windows program sees, opens
 * each one at the same time (as FL Studio does) and counts the short
 * messages that arrive. Send notes from outside meanwhile, for instance
 * with snd-virmidi loaded:  amidi -p hw:<card> -S "90 3C 64 80 3C 00"
 *
 *   midi-in.exe [seconds] [part of a device name that must play]
 *
 * Exit code 0 when every input opened and, with a name given, that
 * input received notes.
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

#define MAX_IN 32

static volatile LONG notes[MAX_IN];

static void CALLBACK on_midi(HMIDIIN h, UINT msg, DWORD_PTR user, DWORD_PTR p1, DWORD_PTR p2)
{
    if (msg == MIM_DATA && (p1 & 0xf0) == 0x90) InterlockedIncrement(&notes[user]);
}

int main(int argc, char **argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 10;
    const char *want = argc > 2 ? argv[2] : NULL;
    HMIDIIN in[MAX_IN] = {0};
    MIDIINCAPSA caps[MAX_IN];
    UINT count = midiInGetNumDevs(), i;
    int fails = 0, played = 0;

    if (count > MAX_IN) count = MAX_IN;
    printf("%u MIDI input(s)\n", count);
    for (i = 0; i < count; i++)
    {
        MMRESULT r = midiInGetDevCapsA(i, &caps[i], sizeof(caps[i]));
        if (r) { printf("FAIL: caps of input %u: %u\n", i, r); fails++; continue; }
        r = midiInOpen(&in[i], i, (DWORD_PTR)on_midi, i, CALLBACK_FUNCTION);
        if (r) { printf("FAIL: open \"%s\": %u\n", caps[i].szPname, r); fails++; in[i] = NULL; continue; }
        r = midiInStart(in[i]);
        if (r) { printf("FAIL: start \"%s\": %u\n", caps[i].szPname, r); fails++; continue; }
        printf("ok: \"%s\" open\n", caps[i].szPname);
    }
    fflush(stdout);
    Sleep(seconds * 1000);
    for (i = 0; i < count; i++)
    {
        if (!in[i]) continue;
        midiInStop(in[i]);
        midiInClose(in[i]);
        printf("%s: %ld note(s)\n", caps[i].szPname, notes[i]);
        if (want && strstr(caps[i].szPname, want) && notes[i] > 0) played = 1;
    }
    /* a second open after closing must work again */
    for (i = 0; i < count; i++)
    {
        HMIDIIN h;
        if (midiInOpen(&h, i, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) midiInClose(h);
        else { printf("FAIL: \"%s\" cannot be opened again\n", caps[i].szPname); fails++; }
    }
    if (want && !played) { printf("FAIL: no note from \"%s\"\n", want); fails++; }
    else if (want) printf("ok: notes arrived from \"%s\"\n", want);
    return fails ? 1 : 0;
}
