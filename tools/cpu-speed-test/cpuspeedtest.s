| cpuspeedtest.s -- X68000 self-booting CPU speed meter for MPX68K.
|
| Counts how many iterations of a 30-cycle loop fit in one video field
| (rising edge to rising edge of V-DISP, MFP GPIP bit 4 at $E88001) and
| shows the cycles per field and the clock that implies. Interrupts are
| masked while counting so only the CPU loop runs.
|
|   loop: addq.l #1,d7      8 cycles
|         btst   #4,(a0)   12 cycles
|         bne.s  loop      10 cycles (taken)
|
| The screen runs CRTMOD 16 (768x512, 31 kHz, about 55.46 Hz), where 1 MHz
| is about 18,031 cycles per field. The emulator's turbo/no-wait settings
| run more fields per second but must not change the cycles per field.
|
| Runs from the floppy boot sector using only IPLROM IOCS services
| (_CRTMOD $10, _B_PRINT $21, _B_LOCATE $23, _OS_CUROF $AF) -- no Human68k,
| no copyrighted content on the disk. Position independent; must fit in the
| 1024-byte boot sector.
|
| Assemble with GNU binutils for m68k (see build.sh).

        .text
        .global entry
entry:
        bra.s   start
        .ascii  "MPX68K CPUTEST"        | 14-byte id string
        .even
start:
        moveq   #0x10,%d0               | _CRTMOD 16
        moveq   #16,%d1
        trap    #15
        move.l  #0xaf,%d0               | _OS_CUROF
        trap    #15
        moveq   #0,%d1
        moveq   #0,%d2
        lea     s_title(%pc),%a1
        bsr     printat
        moveq   #0,%d1
        moveq   #6,%d2
        lea     s_help(%pc),%a1
        bsr     printat

        lea     0xe88001,%a0            | MFP GPIP
mainloop:
        move.w  %sr,-(%sp)
        ori.w   #0x0700,%sr             | mask interrupts while counting
        moveq   #0,%d7
| sync to the start of the display period
1:      btst    #4,(%a0)
        bne.s   1b
2:      btst    #4,(%a0)
        beq.s   2b
| one full field: display period, then vertical blank
3:      addq.l  #1,%d7
        btst    #4,(%a0)
        bne.s   3b
4:      addq.l  #1,%d7
        btst    #4,(%a0)
        beq.s   4b
        move.w  (%sp)+,%sr

        move.l  %d7,%d6                 | cycles = iterations * 30
        lsl.l   #5,%d6
        add.l   %d7,%d7
        sub.l   %d7,%d6

        moveq   #0,%d1
        moveq   #2,%d2
        lea     s_cyc(%pc),%a1
        bsr     printat
        move.l  %d6,%d0
        bsr     printdec
        lea     s_mhz(%pc),%a1
        bsr     print
        move.l  %d6,%d0                 | MHz x 10 = cycles / 1803
        divu    #1803,%d0
        andi.l  #0xffff,%d0
        divu    #10,%d0
        move.l  %d0,%d5
        andi.l  #0xffff,%d0
        bsr     printdec
        lea     s_dot(%pc),%a1
        bsr     print
        move.l  %d5,%d0
        swap    %d0
        andi.l  #0xffff,%d0
        bsr     printdec
        lea     s_tail(%pc),%a1
        bsr     print
        bra     mainloop

| ---- helpers -----------------------------------------------------------
printat:                                | d1=x, d2=y, a1=string
        moveq   #0x23,%d0               | _B_LOCATE
        trap    #15
print:                                  | a1=string
        moveq   #0x21,%d0               | _B_PRINT
        trap    #15
        rts

printdec:                               | d0.l unsigned, no leading zeros
        lea     decbuf(%pc),%a1
        lea     pow10(%pc),%a2
        moveq   #0,%d3                  | digits emitted
5:      move.l  (%a2)+,%d2
        beq.s   7f
        moveq   #'0',%d1
6:      cmp.l   %d2,%d0
        bcs.s   8f
        sub.l   %d2,%d0
        addq.b  #1,%d1
        bra.s   6b
8:      cmpi.b  #'0',%d1
        bne.s   9f
        tst.w   %d3
        bne.s   9f
        cmpi.l  #1,%d2                  | always print the units digit
        bne.s   5b
9:      move.b  %d1,(%a1)+
        addq.w  #1,%d3
        bra.s   5b
7:      clr.b   (%a1)
        lea     decbuf(%pc),%a1
        bra.s   print

        .even
pow10:  .long   10000000,1000000,100000,10000,1000,100,10,1,0
s_title: .asciz "MPX68K CPU SPEED TEST"
s_cyc:   .asciz "cycles/field: "
s_mhz:   .asciz "   = "
s_dot:   .asciz "."
s_tail:  .asciz " MHz        "
s_help:  .ascii "Expected (31kHz, 55.46 Hz field):\r\n"
         .ascii "   1 MHz ->    18,031      10 MHz ->   180,310\r\n"
         .ascii "  16 MHz ->   300,517 (16.67 MHz)\r\n"
         .ascii "  24 MHz ->   432,744      50 MHz ->   901,550\r\n\r\n"
         .ascii "Expect within about 1%. Turbo / No-Wait must\r\n"
         .asciz "not change cycles/field, only how fast fields go by."
        .even
decbuf: .space  12
