# Small Windows programs used to check raye-wine by hand or in the test VM.
#   make            build the .exe files (needs the mingw-w64 compilers)
CC = x86_64-w64-mingw32-gcc
CC32 = i686-w64-mingw32-gcc
CFLAGS = -O2 -Wall
PROGRAMS = d3d11-triangle.exe d3d12-clear.exe sound-check.exe crash.exe d2d1-effect.exe \
           runtimes-check.exe runtimes-check32.exe midi-in.exe child-focus.exe \
           fontprobe.exe laa-check32.exe

all: $(PROGRAMS)

d3d11-triangle.exe: d3d11-triangle.c
	$(CC) $(CFLAGS) -o $@ $< -ld3d11 -ldxgi -ld3dcompiler -ldxguid -luuid

d3d12-clear.exe: d3d12-clear.c
	$(CC) $(CFLAGS) -o $@ $< -ld3d12 -ldxgi -ldxguid -luuid

sound-check.exe: sound-check.c
	$(CC) $(CFLAGS) -o $@ $< -lole32 -lwinmm -luuid

d2d1-effect.exe: d2d1-effect.c
	$(CC) $(CFLAGS) -o $@ $< -ld2d1 -ld3d11 -ldxguid -luuid

runtimes-check.exe: runtimes-check.c
	$(CC) $(CFLAGS) -o $@ $<

runtimes-check32.exe: runtimes-check.c
	$(CC32) $(CFLAGS) -o $@ $<

midi-in.exe: midi-in.c
	$(CC) $(CFLAGS) -o $@ $< -lwinmm

child-focus.exe: child-focus.c
	$(CC) $(CFLAGS) -o $@ $< -lgdi32

fontprobe.exe: fontprobe.c
	$(CC) $(CFLAGS) -o $@ $< -lgdi32 -luser32

laa-check32.exe: laa-check.c
	$(CC32) $(CFLAGS) -Wl,--disable-large-address-aware -o $@ $<

crash.exe: crash.c
	$(CC) $(CFLAGS) -mwindows -o $@ $<

clean:
	rm -f $(PROGRAMS)

.PHONY: all clean
