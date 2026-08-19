| rastercopytest.s -- X68000 self-booting raster-copy demonstrator for MPX68K.
|
| Shows whether the CRTC raster copy runs continuously while the operation
| port's bit 3 is set, or only at the moment a register is written.
|
| A bar is animated left-to-right inside a text raster block near the bottom
| of the screen (the source). Another block below it is set as the copy
| destination once, at startup, and R22 is never written again. The blocks sit
| well below anything the console prints, so console text cannot end up
| underneath them. The operation port's copy bit is then toggled every few
| seconds without touching R22:
|
|   COPY ON   the lower bar must track the upper one, because the copy is
|             re-run every horizontal front porch from the same registers
|   COPY OFF  the lower bar must freeze at whatever it last held, while the
|             upper one keeps moving
|
| If the lower bar never moves at all, the copy is only happening when a
| register is written. If it keeps moving after COPY OFF, the bit is not
| being honoured as a level.
|
| R22 addresses one four-raster block per unit: byte $2C is the source block,
| byte $2D the destination, and R21's low four bits pick the planes. Only
| plane 0 is copied here, so the bars come out in text colour 1.
|
| Runs from the floppy boot sector using only IPLROM IOCS services
| (_CRTMOD $10, _G_CLR_ON $90, _B_PRINT $21) -- no Human68k, no copyrighted
| content on the disk. Position independent; must fit in the single 1024-byte
| boot sector the ROM IPL loads.
|
| Assemble with GNU binutils for m68k (see build.sh).

        .equ    SRCBLK, 100             | source block: rasters 400..403
        .equ    DSTBLK, 110             | destination block: rasters 440..443
        .equ    INDBLK, 105             | copy-bit indicator: rasters 420..423
        .equ    TVRAM0, 0xe00000        | text plane 0

        .text
        .global entry
entry:
        bra.s   start
        .ascii  "MPX68K RCTEST "        | 14-byte id string
        .even
start:
        move.l  #0x10,%d0               | _CRTMOD 16: 768x512 31kHz
        moveq   #16,%d1
        trap    #15
        move.l  #0x90,%d0               | _G_CLR_ON: black graphics behind
        trap    #15
        lea     banner(%pc),%a1
        move.l  #0x21,%d0               | _B_PRINT
        trap    #15

        | Point the copy at block SRCBLK -> DSTBLK once. Nothing below writes
        | R21 or R22 again, so any further copying can only come from the
        | front porch re-running with these same values.
        move.b  #0x0f,0xe8002b          | R21: copy all four planes
        move.b  #SRCBLK,0xe8002c        | R22 high: source block
        move.b  #DSTBLK,0xe8002d        | R22 low:  destination block

        bsr     clearblk                | wipe both blocks on all planes
        moveq   #0,%d5                  | bar position, in bytes
mainloop:
        move.b  #0x08,0xe80481          | operation port: copy bit on
        moveq   #1,%d7                  | indicator: solid = copy on
        bsr     indicate
        bsr     animate

        move.b  #0x00,0xe80481          | operation port: copy bit off
        moveq   #0,%d7                  | indicator: blank = copy off
        bsr     indicate
        bsr     animate
        bra.s   mainloop

| Walk the bar across the source block a few times, so the difference
| between tracking and frozen has time to be obvious.
animate:
        move.w  #24,%d6                 | steps per phase
anl:
        bsr     drawsrc
        bsr     delay
        addq.w  #4,%d5                  | 768 dots = 96 bytes, bar is 4 wide,
        cmpi.w  #92,%d5                 | so wrap at 92 to keep it on screen
        ble.s   onscreen
        moveq   #0,%d5
onscreen:
        subq.w  #1,%d6
        bne.s   anl
        rts

| Repaint the source block: clear its four rasters, then set four bytes at
| the current bar position in each. Only the source is ever written here.
drawsrc:
        movea.l #TVRAM0+SRCBLK*512,%a0
        moveq   #3,%d3                  | four rasters in a block
dsrow:
        movea.l %a0,%a1
        move.w  #127,%d4
dsclr:
        move.b  #0,(%a1)+
        subq.w  #1,%d4
        bpl.s   dsclr
        movea.l %a0,%a1
        adda.w  %d5,%a1
        move.b  #0xff,(%a1)
        move.b  #0xff,1(%a1)
        move.b  #0xff,2(%a1)
        move.b  #0xff,3(%a1)
        adda.w  #128,%a0                | next raster
        subq.w  #1,%d3
        bpl.s   dsrow
        rts

| Wipe the source, destination and indicator blocks on all four planes.
| The console banner is printed before this, so anything it left inside the
| blocks would otherwise survive on the planes we do not draw into and mix
| with the copied ones -- which shows up as odd colours rather than a clean
| bar.
clearblk:
        moveq   #3,%d2                  | four planes
        move.l  #TVRAM0,%d1             | plane base
cbplane:
        movea.l %d1,%a0
        adda.l  #SRCBLK*512,%a0
        bsr.s   cbwipe
        movea.l %d1,%a0
        adda.l  #DSTBLK*512,%a0
        bsr.s   cbwipe
        movea.l %d1,%a0
        adda.l  #INDBLK*512,%a0
        bsr.s   cbwipe
        add.l   #0x20000,%d1            | next plane
        subq.w  #1,%d2
        bpl.s   cbplane
        rts
| clear one 4-raster block (512 bytes) at a0
cbwipe:
        move.w  #511,%d4
cbw:
        move.b  #0,(%a0)+
        subq.w  #1,%d4
        bpl.s   cbw
        rts

| Phase indicator, drawn on plane 0 of its own block: a full-width line while
| the copy bit is set, blank while it is clear. Drawn rather than printed so
| the console cursor never walks down into the blocks under test.
indicate:
        movea.l #TVRAM0+INDBLK*512,%a0
        moveq   #3,%d3                  | four rasters
inrow:
        movea.l %a0,%a1
        move.w  #95,%d4                 | 96 bytes = 768 dots
inb:
        tst.w   %d7
        beq.s   inclr
        move.b  #0xff,(%a1)+
        bra.s   innext
inclr:
        move.b  #0,(%a1)+
innext:
        subq.w  #1,%d4
        bpl.s   inb
        adda.w  #128,%a0                | next raster
        subq.w  #1,%d3
        bpl.s   inrow
        rts

| ~0.12s at 10MHz; scales with the configured CPU clock
delay:
        move.l  #70000,%d2
dl:
        subq.l  #1,%d2
        bne.s   dl
        rts

banner: .asciz  "MPX68K RASTER COPY TEST\r\nTOP BAR    = SOURCE, MOVES ALWAYS\r\nMIDDLE LINE= COPY BIT IS ON\r\nBOTTOM BAR = COPY DESTINATION\r\nR22 WRITTEN ONCE, NEVER AGAIN\r\nDEST MUST TRACK WHILE MIDDLE ON,\r\nAND FREEZE WHILE MIDDLE IS OFF\r\n"
        .even
