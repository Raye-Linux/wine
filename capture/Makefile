# libraye-capture: screen capture for Windows programs, portal + PipeWire.

PREFIX ?= /usr
LIBDIR ?= $(PREFIX)/lib/$(shell $(CC) -print-multiarch)
CFLAGS ?= -O2 -g
CFLAGS += -Wall -Wextra -Wno-unused-parameter -fPIC -Iinclude -Isrc
DEPS = gio-unix-2.0 libpipewire-0.3
DEP_CFLAGS := $(shell pkg-config --cflags $(DEPS))
DEP_LIBS := $(shell pkg-config --libs $(DEPS))

SONAME = libraye-capture.so.1
LOGIC = src/match.c src/pixels.c src/tokens.c src/asking.c
SRCS = $(LOGIC) src/capture.c src/portal.c src/pipewire.c

all: $(SONAME) raye-capture-probe

$(SONAME): $(SRCS) include/raye-capture.h src/private.h src/session.h
	$(CC) $(CFLAGS) $(DEP_CFLAGS) -shared -Wl,-soname,$(SONAME) -Wl,--version-script,src/exports.map \
		-o $@ $(SRCS) $(DEP_LIBS) -lpthread

raye-capture-probe: tools/raye-capture-probe.c $(SONAME)
	$(CC) $(CFLAGS) -o $@ $< -L. -l:$(SONAME) -Wl,-rpath,'$$ORIGIN'

test-logic: tests/test-logic.c $(LOGIC) src/private.h
	$(CC) $(CFLAGS) -o $@ tests/test-logic.c $(LOGIC)

check: test-logic
	./test-logic

# a Windows program that asks for the screen like OBS or a screenshot tool
capture-test.exe: tests/windows/capture-test.c
	x86_64-w64-mingw32-gcc -O2 -Wall -o $@ $< -ld3d11 -ldxgi -luser32 -lgdi32 -luuid -ldxguid

install: all
	install -D -m 644 $(SONAME) $(DESTDIR)$(LIBDIR)/$(SONAME)
	install -D -m 755 raye-capture-probe $(DESTDIR)$(PREFIX)/libexec/raye/raye-capture-probe
	install -D -m 644 include/raye-capture.h $(DESTDIR)$(PREFIX)/include/raye-capture.h

clean:
	rm -f $(SONAME) raye-capture-probe test-logic capture-test.exe

.PHONY: all check install clean
