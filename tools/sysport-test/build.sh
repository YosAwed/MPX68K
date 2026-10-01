#!/bin/sh -e
# Cross-assemble sysporttest.s and pack it into a bootable XDF image.
# Needs GNU binutils for m68k, e.g. on Debian/Ubuntu:
#   apt-get install binutils-m68k-linux-gnu
# Override the tool prefix with PREFIX=... for other toolchains.
cd "$(dirname "$0")"
PREFIX=${PREFIX:-m68k-linux-gnu-}

"${PREFIX}as" -m68000 -o sysporttest.o sysporttest.s
"${PREFIX}ld" -e 0 -Ttext=0 -o sysporttest.elf sysporttest.o
"${PREFIX}objcopy" -O binary sysporttest.elf sysporttest.bin

size=$(wc -c < sysporttest.bin)
if [ "$size" -gt 1024 ]; then
    echo "error: boot code is $size bytes; the ROM IPL only loads 1024" >&2
    exit 1
fi

# XDF = raw 2HD dump: 77 cylinders x 2 heads x 8 sectors x 1024 bytes
rm -f sysporttest.xdf
dd if=/dev/zero of=sysporttest.xdf bs=1024 count=1232 status=none
dd if=sysporttest.bin of=sysporttest.xdf conv=notrunc status=none
echo "sysporttest.xdf ready ($size bytes of boot code)"
