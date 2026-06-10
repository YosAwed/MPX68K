# Host-side unit tests for the px68k emulation core.
# These compile selected core translation units with libc stubs, so they
# run on any platform with a C compiler (no Xcode required).

CC ?= cc
PX68K = ../../X68000 Shared/px68k
CFLAGS += -Wall -g \
	-I "$(PX68K)/x68k" -I "$(PX68K)/x11" \
	-I "$(PX68K)/win32api" -I "$(PX68K)/m68000"

.PHONY: all run clean

all: run

test_disk_d88:
	$(CC) $(CFLAGS) -o $@ test_disk_d88.c stubs.c "$(PX68K)/x68k/disk_d88.c"

run: test_disk_d88
	./test_disk_d88

clean:
	rm -f test_disk_d88 _test_image.d88
