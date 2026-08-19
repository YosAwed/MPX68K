| interlacetest.s -- DOS-free R20=$19 field-weave visual test for MPX68K.
|
| The ROM IPL loads this single-sector program. It uses IOCS only to select
| the standard 768x512 base mode and clear graphics, then writes a diagnostic
| pattern directly to all 1024 TVRAM rows before changing R20 to $19.
|
| Each 256-row quarter uses a different pair of colours. Even source rows
| occupy only the left half; odd source rows occupy only the right half.
| A correct alternating-field weave therefore shows all four horizontal
| colour bands and both halves. A stuck field loses one half, while the old
| R20=$19 double-read path exposes only the first quarter.

        .equ    TVRAM0, 0xe00000
        .equ    PLANE,  0x20000

        .text
        .global entry
entry:
        bra.s   start
        .ascii  "MPX68K ILTEST "        | 14-byte boot identifier
        .even

start:
        move.l  #0x10,%d0               | _CRTMOD 16: 768x512 16c 31kHz
        moveq   #16,%d1
        trap    #15
        move.l  #0x90,%d0               | _G_CLR_ON
        trap    #15

        | Clear all four 128KB text planes, including rows 512..1023.
        lea     TVRAM0,%a0
        move.l  #0x20000,%d0             | 0x20000 longwords = 512KB
clear:
        clr.l   (%a0)+
        subq.l  #1,%d0
        bne.s   clear

        | Draw 1024 rows. Each row occupies 128 bytes; only the visible
        | 768 dots (96 bytes) are used. Even rows fill the left 384 dots,
        | odd rows fill the right 384 dots.
        lea     TVRAM0,%a2
        movea.l #TVRAM0+PLANE,%a3
        movea.l #TVRAM0+PLANE*2,%a4
        movea.l #TVRAM0+PLANE*3,%a5
        moveq   #0,%d7                   | source row 0..1023
row:
        move.w  %d7,%d0
        lsr.w   #8,%d0                   | quarter 0..3
        btst    #0,%d7
        bne.s   odd
        lea     even_masks(%pc),%a1
        move.b  0(%a1,%d0.w),%d1
        bra.s   side
odd:
        lea     odd_masks(%pc),%a1
        move.b  0(%a1,%d0.w),%d1
        adda.w  #48,%a2                  | right half of visible row
        adda.w  #48,%a3
        adda.w  #48,%a4
        adda.w  #48,%a5
side:
        moveq   #47,%d6                  | 48 bytes = 384 dots
byte:
        btst    #0,%d1
        beq.s   p1
        move.b  #0xff,(%a2)
p1:
        btst    #1,%d1
        beq.s   p2
        move.b  #0xff,(%a3)
p2:
        btst    #2,%d1
        beq.s   p3
        move.b  #0xff,(%a4)
p3:
        btst    #3,%d1
        beq.s   nextbyte
        move.b  #0xff,(%a5)
nextbyte:
        addq.l  #1,%a2
        addq.l  #1,%a3
        addq.l  #1,%a4
        addq.l  #1,%a5
        dbra    %d6,byte

        | Advance from the current half's end to the next 128-byte row.
        btst    #0,%d7
        bne.s   oddnext
        adda.w  #80,%a2                  | 48 -> 128
        adda.w  #80,%a3
        adda.w  #80,%a4
        adda.w  #80,%a5
        bra.s   rownext
oddnext:
        adda.w  #32,%a2                  | 96 -> 128
        adda.w  #32,%a3
        adda.w  #32,%a4
        adda.w  #32,%a5
rownext:
        addq.w  #1,%d7
        cmpi.w  #1024,%d7
        bne     row

        | Preserve the video-controller bits selected by IOCS and change
        | only R20's scan/dot-clock byte: HF=1, VRES=2, HRES=1.
        move.b  #0x19,0xe80029

forever:
        bra.s   forever

        | Plane masks by vertical quarter. Opposite parities deliberately
        | use different colours so both fields remain independently visible.
even_masks:
        .byte   0x01,0x04,0x03,0x05
odd_masks:
        .byte   0x02,0x08,0x0c,0x0a
        .even
