# Host-side unit tests for the px68k emulation core.
# These compile selected core translation units with libc stubs, so they
# run on any platform with a C compiler (no Xcode required).

CC ?= cc
PX68K = ../../X68000 Shared/px68k
# Sanitizers catch renderer buffer overruns (e.g. 1024-dot line rendering
# into Text_TrFlag/ScrBuf). Override with SAN= on toolchains without them.
SAN ?= -fsanitize=address,undefined
CFLAGS += -Wall -g $(SAN) \
	-I "$(PX68K)/x68k" -I "$(PX68K)/x11" \
	-I "$(PX68K)/win32api" -I "$(PX68K)/m68000"

# Test binaries are phony so edits to the (space-containing) core source
# paths always trigger a rebuild; the builds are cheap.
.PHONY: all run clean test_disk_d88 test_crtc_timing test_mfp_hsync test_scrbuf test_fdd_mount test_sram_scsi test_sysport test_scc_rs232c test_c68k_buserr

all: run

test_disk_d88:
	$(CC) $(CFLAGS) -o $@ test_disk_d88.c stubs.c "$(PX68K)/x68k/disk_d88.c"

test_crtc_timing:
	$(CC) $(CFLAGS) -o $@ test_crtc_timing.c \
		"$(PX68K)/x68k/crtc_timing.c" "$(PX68K)/x68k/crtc.c" \
		"$(PX68K)/x68k/sysport.c" -lm

test_mfp_hsync:
	$(CC) $(CFLAGS) -o $@ test_mfp_hsync.c "$(PX68K)/x68k/mfp.c"

test_scrbuf:
	$(CC) $(CFLAGS) -o $@ test_scrbuf.c \
		"$(PX68K)/x11/scrbuf.c" "$(PX68K)/x11/windraw.c" \
		"$(PX68K)/x68k/crtc_timing.c" "$(PX68K)/x68k/crtc.c" -lm

test_fdd_mount:
	$(CC) $(CFLAGS) -o $@ test_fdd_mount.c "$(PX68K)/x68k/fdd.c"

test_sram_scsi:
	$(CC) $(CFLAGS) -o $@ test_sram_scsi.c "$(PX68K)/x68k/sram.c"

test_sysport:
	$(CC) $(CFLAGS) -o $@ test_sysport.c "$(PX68K)/x68k/sysport.c"

# _GNU_SOURCE: scc.c's host serial backends use pty and usleep declarations
# that glibc only exposes with it.
test_scc_rs232c:
	$(CC) $(CFLAGS) -D_GNU_SOURCE -o $@ test_scc_rs232c.c "$(PX68K)/x68k/scc.c" -lpthread

# The 68000 core, built the way the app builds it.
test_c68k_buserr:
	$(CC) $(CFLAGS) -w -DC68K_NO_JUMP_TABLE -DHAVE_C68K -o $@ test_c68k_buserr.c \
		"$(PX68K)/m68000/c68k/c68k.c" "$(PX68K)/m68000/c68k/c68kexec.c"

run: test_disk_d88 test_crtc_timing test_mfp_hsync test_scrbuf test_fdd_mount test_sram_scsi test_sysport test_scc_rs232c test_c68k_buserr
	./test_disk_d88
	./test_crtc_timing
	./test_mfp_hsync
	./test_scrbuf
	./test_fdd_mount
	./test_sram_scsi
	./test_sysport
	./test_scc_rs232c
	./test_c68k_buserr

clean:
	rm -f test_disk_d88 test_crtc_timing test_mfp_hsync test_scrbuf test_fdd_mount test_sram_scsi test_sysport test_scc_rs232c test_c68k_buserr _test_image.d88
