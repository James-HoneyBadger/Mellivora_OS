;=======================================================================
; PIC (Programmable Interrupt Controller) INITIALIZATION
;=======================================================================

pic_init:
        ; ICW1: begin initialization sequence
        mov al, 0x11
        out PIC1_CMD, al
        out PIC2_CMD, al

        ; ICW2: remap IRQ vectors
        ; PIC1: IRQ 0-7  -> INT 0x20-0x27
        ; PIC2: IRQ 8-15 -> INT 0x28-0x2F
        mov al, 0x20
        out PIC1_DATA, al
        mov al, 0x28
        out PIC2_DATA, al

        ; ICW3: cascade configuration
        mov al, 0x04            ; PIC1: IRQ2 has slave
        out PIC1_DATA, al
        mov al, 0x02            ; PIC2: cascade identity 2
        out PIC2_DATA, al

        ; ICW4: 8086 mode
        mov al, 0x01
        out PIC1_DATA, al
        out PIC2_DATA, al

        ; Mask all IRQs except IRQ0 (timer) and IRQ1 (keyboard)
        mov al, 0xFC            ; Enable IRQ0, IRQ1
        out PIC1_DATA, al
        mov al, 0xFF            ; Mask all on PIC2
        out PIC2_DATA, al

        ret
