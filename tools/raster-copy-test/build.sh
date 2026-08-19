#!/bin/sh -e
# Cross-assemble rastercopytest.s and pack it into a bootable XDF image.
# Needs GNU binutils for m68k, e.g. on Debian/Ubuntu:
#   apt-get install binutils-m68k-linux-gnu
# Override the tool prefix with PREFIX=... for other toolchains.
cd "$(dirname "$0")"
PREFIX=${PREFIX:-m68k-linux-gnu-}

"${PREFIX}as" -m68000 -o rastercopytest.o rastercopytest.s
"${PREFIX}ld" -e 0 -Ttext=0 -o rastercopytest.elf rastercopytest.o
"${PREFIX}objcopy" -O binary rastercopytest.elf rastercopytest.bin

size=$(wc -c < rastercopytest.bin)
if [ "$size" -gt 1024 ]; then
    echo "error: boot code is $size bytes; the ROM IPL only loads 1024" >&2
    exit 1
fi

# XDF = raw 2HD dump: 77 cylinders x 2 heads x 8 sectors x 1024 bytes
rm -f rastercopytest.xdf
dd if=/dev/zero of=rastercopytest.xdf bs=1024 count=1232 status=none
dd if=rastercopytest.bin of=rastercopytest.xdf conv=notrunc status=none
echo "rastercopytest.xdf ready ($size bytes of boot code)"
