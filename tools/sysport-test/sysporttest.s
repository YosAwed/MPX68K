| sysporttest.s -- X68000 self-booting system-port / CPU exception checker
| for MPX68K.
|
|  1. ILLEGAL ($4AFC) stacked PC: installs a handler on vector 4, executes
|     ILLEGAL and reports whether the stacked PC points at the instruction
|     itself (68000 behaviour) or 2 bytes past it.
|  2. $E8E00B machine class: re-read continuously, so changing the clock
|     from the emulator menu shows up live.
|  3. Guest power-off: P writes $00,$0F,$0F to $E8E00F (the power-off
|     sequence); N writes $00,$0F,$05,$0F, a broken sequence that must not
|     switch the machine off.
|
| Runs from the floppy boot sector using only IPLROM IOCS services
| (_B_KEYINP $00, _B_KEYSNS $01, _CRTMOD $10, _B_PRINT $21, _B_LOCATE $23,
| _OS_CUROF $AF) -- no Human68k, no copyrighted content on the disk.
| Position independent; must fit in the 1024-byte boot sector.
|
| Assemble with GNU binutils for m68k (see build.sh).

        .text
        .global entry
entry:
        bra.s   start
        .ascii  "MPX68K SYSTEST"        | 14-byte id string
        .even
start:
        moveq   #0x10,%d0               | _CRTMOD 16: 768x512, clears screen
        moveq   #16,%d1
        trap    #15
        move.l  #0xaf,%d0               | _OS_CUROF
        trap    #15

        moveq   #0,%d1
        moveq   #0,%d2
        lea     s_title(%pc),%a1
        bsr     printat

| ---- 1. ILLEGAL stacked PC --------------------------------------------
        move.l  0x10.w,%a5              | save vector 4
        lea     ill_handler(%pc),%a0
        move.l  %a0,0x10.w
        lea     ill_site(%pc),%a0
        move.l  %a0,%d5                 | address of the ILLEGAL instruction
        moveq   #-1,%d4                 | stacked PC (set by the handler)
ill_site:
        illegal
        move.l  %a5,0x10.w              | restore vector 4

        moveq   #0,%d1
        moveq   #2,%d2
        lea     s_ill(%pc),%a1
        bsr     printat
        move.l  %d5,%d0
        bsr     printhex8
        lea     s_stk(%pc),%a1
        bsr     print
        move.l  %d4,%d0
        bsr     printhex8
        lea     s_pass(%pc),%a1
        cmp.l   %d5,%d4
        beq.s   1f
        lea     s_fail(%pc),%a1
1:      bsr     print

        moveq   #0,%d1
        moveq   #6,%d2
        lea     s_help(%pc),%a1
        bsr     printat

| ---- main loop: 2. $E8E00B live, 3. power-off keys ---------------------
mainloop:
        moveq   #0,%d1
        moveq   #4,%d2
        lea     s_cls(%pc),%a1
        bsr     printat
        moveq   #0,%d0
        move.b  0xe8e00b,%d0
        move.l  %d0,%d6
        bsr     printhex2
        lea     s_c10(%pc),%a1
        cmpi.b  #0xff,%d6
        beq.s   2f
        lea     s_c16(%pc),%a1
        cmpi.b  #0xfe,%d6
        beq.s   2f
        lea     s_c30(%pc),%a1
        cmpi.b  #0xdc,%d6
        beq.s   2f
        lea     s_cunk(%pc),%a1
2:      bsr     print

        moveq   #0x01,%d0               | _B_KEYSNS
        trap    #15
        tst.l   %d0
        beq.s   mainloop
        moveq   #0x00,%d0               | _B_KEYINP
        trap    #15
        andi.b  #0xdf,%d0               | upper-case
        cmpi.b  #'P',%d0
        beq.s   poweroff
        cmpi.b  #'N',%d0
        beq.s   broken
        bra.s   mainloop

poweroff:
        moveq   #0,%d1
        moveq   #12,%d2
        lea     s_poff(%pc),%a1
        bsr     printat
        move.b  #0x00,0xe8e00f
        move.b  #0x0f,0xe8e00f
        move.b  #0x0f,0xe8e00f
        bra     mainloop

broken:
        moveq   #0,%d1
        moveq   #12,%d2
        lea     s_brk(%pc),%a1
        bsr     printat
        move.b  #0x00,0xe8e00f
        move.b  #0x0f,0xe8e00f
        move.b  #0x05,0xe8e00f
        move.b  #0x0f,0xe8e00f
        bra     mainloop

| Vector 4 handler: record the stacked PC; step over the ILLEGAL when it
| points at the instruction, otherwise it already points past it.
ill_handler:
        move.l  2(%sp),%d4
        cmp.l   %d5,%d4
        bne.s   3f
        addq.l  #2,2(%sp)
3:      rte

| ---- helpers -----------------------------------------------------------
printat:                                | d1=x, d2=y, a1=string
        moveq   #0x23,%d0               | _B_LOCATE
        trap    #15
print:                                  | a1=string
        moveq   #0x21,%d0               | _B_PRINT
        trap    #15
        rts

printhex8:                              | d0.l
        moveq   #7,%d3
        bra.s   printhex
printhex2:                              | d0.b
        ror.l   #8,%d0
        moveq   #1,%d3
printhex:
        lea     hexbuf(%pc),%a1
4:      rol.l   #4,%d0
        move.b  %d0,%d1
        andi.b  #15,%d1
        addi.b  #'0',%d1
        cmpi.b  #'9',%d1
        bls.s   5f
        addq.b  #7,%d1
5:      move.b  %d1,(%a1)+
        dbra    %d3,4b
        clr.b   (%a1)
        lea     hexbuf(%pc),%a1
        bra.s   print

s_title: .asciz "MPX68K SYSPORT TEST"
s_ill:   .asciz "ILLEGAL at $"
s_stk:   .asciz "  stacked PC $"
s_pass:  .asciz "  PASS"
s_fail:  .asciz "  FAIL (expected = ILLEGAL address)"
s_cls:   .asciz "$E8E00B = $"
s_c10:   .asciz "  10MHz class        "
s_c16:   .asciz "  16MHz class (XVI)  "
s_c30:   .asciz "  030 class          "
s_cunk:  .asciz "  unknown            "
s_help:  .ascii "Expected $E8E00B: 16/24MHz -> $FE, other clocks -> $FF\r\n"
         .ascii "Change the clock from the Clock menu; the value updates live.\r\n\r\n"
         .ascii "P: power-off sequence ($00,$0F,$0F to $E8E00F) -> must power off\r\n"
         .asciz "N: broken sequence ($00,$0F,$05,$0F)          -> must keep running"
s_poff:  .asciz "P: power-off sequence sent                       "
s_brk:   .asciz "N: broken sequence sent - still running = PASS   "
        .even
hexbuf: .space  10
