        /* Replacement paravirt Sony driver stubs
         *
         * This is the Basilisk "sony_driver" from the binary code in rom_patches.cpp,
         * except instead of using custom opcodes this driver stores magic values
         * to a magic address.
         *
         * The original code:   Basilisk II (C) Christian Bauer
         * Extensions:          Copyright 2024 Matt Evans
         *
         * GPLv2
         */

#define SONY_REF_NUM            -5
#define SONY_STATUS_SIZE        30

        .org 0
        .globl _start
_start:
        .globl header
header:
        .word 0x6f00            // flags
        .word 0                 // ticks between actions
        .word 0                 // desk accessory event mask
        .word 0                 // menu ID
        .word open
        .word prime
        .word control
        .word status
        .word close
name:
        .byte 5
        .ascii ".Sony"

open:
        // Open()
        // pico-mac: one drive per index (d1 = 0, 1, ...). Each gets a new drive status block,
        // and the PV call returns the drive number to AddDrive, or 0 when there are no more.
        movem.l %d3/%a0-%a3, -(%sp)
        moveq   #0, %d3                         // drive index
1:      moveq   #SONY_STATUS_SIZE, %d0
        .word   0xa71e                          // NewPtrSysClear
        move.l  %a0, %d1
        beq     3f                              // lolfail
        move.l  %a0, %a2
        movem.l 4(%sp), %a0-%a1                 // the pb and DCE again
        move.l  %d3, %d1
        move.l  (faulting_address), %a3
        move.b  #0, (%a3)                       // Fault! Open: op 0 -> d0 = drive number
        tst.l   %d0
        ble     2f
        // Now add the drive:
        swap    %d0                             // drive number in the high word
        move.w  #SONY_REF_NUM, %d0
        lea     6(%a2), %a0
        .word   0xa04e                          // AddDrive
        addq.l  #1, %d3
        bra     1b
2:      movem.l (%sp)+, %d3/%a0-%a3
        moveq   #0, %d0
        rts
3:      movem.l (%sp)+, %d3/%a0-%a3
        moveq   #-23, %d0
        rts

prime:
        // Prime()
        move.l  (faulting_address), %a2
        move.b  #1, (%a2)                       // Prime: op 1
        bra.b   IOReturn

control:
        // Control()
        move.l  (faulting_address), %a2
        move.b  #2, (%a2)                       // Control: op 2
        cmp.w   #65, 0x1a(%a0)                  // pico-mac: accRun returns a drive to mount
        bne.b   1f
        tst.l   %d0
        ble.b   IOReturn
        move.l  %a0, -(%sp)
        move.w  #7, %a0                         // diskEvt
        .word   0xa02f                          // PostEvent(diskEvt, drive number in d0)
        move.l  (%sp)+, %a0
        moveq   #0, %d0
        bra.b   IOReturn
1:      cmp.w   #1, 0x1a(%a0)
        bne.b   IOReturn
        rts

status:
        // Status()
        move.l  (faulting_address), %a2
        move.b  #3, (%a2)                       // Status: op 3
        // Fall through

IOReturn:
        // IOReturn
        move.w  6(%a0),%d1                      // IO trap?
        btst    #9,%d1                          // Test queue bit
        beq.b   1f
        tst.w   %d0                             // Not queued; test async result
        ble.b   2f
        clr.w   %d0                             // In progress
2:      move.w  %d0, 0x10(%a0)                  // ImmedRTS
        rts
1:      tst.w   %d0                             // Queued
        ble.b   3f
        clr.w   %d0
        rts
3:      move.l  0x8fc,-(%sp)                    // IODone vector
        rts

close:
        // Close()
        moveq   #-24, %d0                       // closErr
        rts

        .balign 4
faulting_address:
        .long   0
