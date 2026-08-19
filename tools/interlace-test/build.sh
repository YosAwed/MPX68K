#!/bin/sh -e
# Cross-assemble interlacetest.s and pack it into a bootable XDF image.
# Override PREFIX for another m68k GNU binutils installation.
cd "$(dirname "$0")"
PREFIX=${PREFIX:-m68k-linux-gnu-}

"${PREFIX}as" -m68000 -o interlacetest.o interlacetest.s
"${PREFIX}ld" -e 0 -Ttext=0 -o interlacetest.elf interlacetest.o
"${PREFIX}objcopy" -O binary interlacetest.elf interlacetest.bin

size=$(wc -c < interlacetest.bin)
if [ "$size" -gt 1024 ]; then
    echo "error: boot code is $size bytes; the ROM IPL only loads 1024" >&2
    exit 1
fi

# XDF = raw 2HD dump: 77 cylinders x 2 heads x 8 sectors x 1024 bytes.
rm -f interlacetest.xdf
dd if=/dev/zero of=interlacetest.xdf bs=1024 count=1232 status=none
dd if=interlacetest.bin of=interlacetest.xdf conv=notrunc status=none
echo "interlacetest.xdf ready ($size bytes of boot code)"
