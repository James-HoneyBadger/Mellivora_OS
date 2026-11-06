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

        ; Start with everything masked and let irq_enable decide, so the 8259
        ; and the I/O APIC agree on which lines are live: an SMP machine moves
        ; to the APIC later and replays the same requests.
        mov al, 0xFF
        out PIC1_DATA, al
        out PIC2_DATA, al
        xor al, al              ; IRQ0 - the timer tick
        call irq_enable
        mov al, 1               ; IRQ1 - the keyboard
        call irq_enable

        ret

;---------------------------------------
; irq_set_level - make one IRQ input level-triggered
;   In : AL = IRQ number (0-15)
;
; The 8259 pair comes out of reset with edge-triggered inputs, but a PCI
; interrupt line is not an edge: the card holds it asserted while its status
; register is not clear, and low otherwise. Configured as an edge, an assertion
; that lands while the handler is still running is lost - the line never went
; low, so there is no new edge to see - and a card whose receive ring is full
; has nothing to deliver, so it asserts no more. The result is a stack that
; stops dead with nothing on the console: a fuzz storm of malformed frames took
; the network down this way, ~12 000 frames in, every time.
;---------------------------------------
irq_set_level:
        push eax
        push ecx
        push edx
        mov ecx, eax
        and ecx, 7              ; bit within the pair's register
        cmp al, 8
        jb .lvl_master
        mov dx, ELCR2
        jmp .lvl_read
.lvl_master:
        mov dx, ELCR1
.lvl_read:
        in al, dx
        mov ah, al              ; the other seven lines keep their mode
        mov al, 1
        shl al, cl
        or al, ah
        out dx, al
        pop edx
        pop ecx
        pop eax
        ret
