| crtmodetest.s -- X68000 self-booting CRT mode exerciser for MPX68K.
|
| Cycles forever through the standard video modes
|   CRTMOD 16 (768x512 31kHz), 4 (512x512 31kHz), 8 (512x512 256color),
|   6 (256x256 31kHz raster double-read), 7 (256x256 15.98kHz),
|   5 (512x512 15.98kHz interlace)
| and finally a direct-CRTC 1024-dot wide mode (128 columns, a register
| combination the CrtcTiming model accepts as valid), drawing color bands
| and printing a label in each mode. Each mode holds until a key is pressed,
| or for about eight seconds unattended, so the disk serves both eyeball
| comparison and unattended sanitizer soaks. Exercises mode-change paths,
| the 1024-dot rendering, and leaves the text plane accumulating output so
| stale-frame artifacts are visible.
|
| Runs from the floppy boot sector using only IPLROM IOCS services
| (_CRTMOD $10, _G_CLR_ON $90, _B_PRINT $21, _B_KEYINP $00, _B_KEYSNS $01)
| -- no Human68k, no copyrighted content on the disk. Position independent;
| must fit in the single 1024-byte boot sector the ROM IPL loads.
|
| Assemble with GNU binutils for m68k (see build.sh).

        .text
        .global entry
entry:
        bra.s   start
        .ascii  "MPX68K CRTTEST"        | 14-byte id string
        .even
start:
mainloop:
        lea     modetab(%pc),%a6
nextmode:
        move.w  (%a6)+,%d7              | mode number; negative = wide phase
        move.w  (%a6)+,%d6              | label offset from modetab
        tst.w   %d7
        bmi.s   wide1024
        move.l  #0x10,%d0               | _CRTMOD
        move.l  %d7,%d1
        trap    #15
        move.l  #0x90,%d0               | _G_CLR_ON
        trap    #15
        bsr     announce
        bsr     pattern
        bsr     delay
        bra.s   nextmode

wide1024:
        move.l  #0x10,%d0               | _CRTMOD 16: 768x512 base
        moveq   #16,%d1
        trap    #15
        move.l  #0x90,%d0               | _G_CLR_ON
        trap    #15
        bsr     announce
        bsr     pattern
        | widen to 128 columns = 1024 dots; R00 grows with it so the
        | combination stays scannable (R03 <= R00, 128-column limit)
        move.w  #0x00a9,0xe80000        | R00: h_total-1 = 169
        move.w  #0x009c,0xe80006        | R03: HEND $9c -> ($9c-$1c)*8 = 1024
        bsr     delay
        bsr     delay
        bra.s   mainloop

| print the NUL-terminated label at modetab + d6
announce:
        lea     modetab(%pc),%a1
        adda.w  %d6,%a1
        move.l  #0x21,%d0               | _B_PRINT
        trap    #15
        rts

| fill the 512KB graphics window with horizontal color bands; the word
| pattern lands on sensible colors in every 16/256-color packing
pattern:
        lea     0xc00000,%a0
        move.l  #0x40000,%d2            | words to write
        moveq   #0,%d3                  | current color
        moveq   #0,%d4                  | words in current band
patl:
        move.w  %d3,(%a0)+
        addq.w  #1,%d4
        cmpi.w  #0x1000,%d4
        bne.s   patn
        moveq   #0,%d4
        addq.w  #1,%d3
        andi.w  #15,%d3
patn:
        subq.l  #1,%d2
        bne.s   patl
        rts

| Hold the current mode until a key is pressed, or for roughly eight seconds
| if nobody is watching. Two seconds was too short to compare modes by eye,
| but a key-only wait would stall an unattended sanitizer soak, so do both:
| poll _B_KEYSNS about five times a second inside a busy loop that times out
| on its own. If key sensing ever misbehaves the timeout still advances.
delay:
        move.l  #40,%d3                 | outer: poll slots (~0.2s each)
dout:
        move.l  #120000,%d2             | inner: busy work between polls
din:
        subq.l  #1,%d2
        bne.s   din
        moveq   #1,%d0                  | _B_KEYSNS: key waiting?
        trap    #15
        tst.l   %d0
        bne.s   dkey
        subq.l  #1,%d3
        bne.s   dout
        rts
dkey:
        moveq   #0,%d0                  | _B_KEYINP: consume it
        trap    #15
        rts

modetab:
        .word   16, m16-modetab
        .word   4,  m4-modetab
        .word   8,  m8-modetab
        .word   6,  m6-modetab
        .word   7,  m7-modetab
        .word   5,  m5-modetab
        .word   -1, mw-modetab

m16:    .asciz  "CRT MODE 16  768x512  16c 31K\r\n"
m4:     .asciz  "CRT MODE  4  512x512  16c 31K\r\n"
m8:     .asciz  "CRT MODE  8  512x512 256c 31K\r\n"
m6:     .asciz  "CRT MODE  6  256x256  16c 31K DBL\r\n"
m7:     .asciz  "CRT MODE  7  256x256  16c 15K\r\n"
m5:     .asciz  "CRT MODE  5  512x512  16c 15K INT\r\n"
mw:     .asciz  "CRT 1024-DOT WIDE (CRTC R00/R03)\r\n"
        .even
