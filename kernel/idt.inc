;=======================================================================
; IDT (Interrupt Descriptor Table)
;=======================================================================

idt_init:
        ; First, fill ALL 256 entries with default handler
        mov ecx, 0
        mov eax, isr_default
.fill_all:
        cmp ecx, 256
        jge .set_exceptions
        call idt_set_gate
        inc ecx
        jmp .fill_all

.set_exceptions:
        ; Set exception handlers (INT 0-31) — no error code
        ; Exceptions 0-7, 9, 15, 16-20, 22-31: no error code
        mov ecx, 0
.exc_noerr:
        cmp ecx, 32
        jge .irq_setup
        ; Skip those that push error codes
        cmp ecx, 8
        je .exc_skip
        cmp ecx, 10
        je .exc_skip
        cmp ecx, 11
        je .exc_skip
        cmp ecx, 12
        je .exc_skip
        cmp ecx, 13
        je .exc_skip
        cmp ecx, 14
        je .exc_skip
        cmp ecx, 17
        je .exc_skip
        cmp ecx, 21
        je .exc_skip
        mov eax, isr_exception_noerr
        call idt_set_gate
.exc_skip:
        inc ecx
        jmp .exc_noerr

.irq_setup:
        ; Set exception handlers that push error codes
        mov ecx, 8
        mov eax, isr_exception_err
        call idt_set_gate
        mov ecx, 10
        call idt_set_gate
        mov ecx, 11
        call idt_set_gate
        mov ecx, 12
        call idt_set_gate
        mov ecx, 13
        call idt_set_gate
        mov ecx, 14
        call idt_set_gate
        mov ecx, 17
        call idt_set_gate
        mov ecx, 21
        call idt_set_gate

        ; IRQ0 = INT 0x20 = Timer
        mov ecx, 0x20
        mov eax, irq_timer
        call idt_set_gate

        ; IRQ1 = INT 0x21 = Keyboard
        mov ecx, 0x21
        mov eax, irq_keyboard
        call idt_set_gate

        ; Fill PIC1 IRQs (INT 0x22-0x27) with PIC1-only EOI stub
        mov ecx, 0x22
.irq_loop_pic1:
        cmp ecx, 0x28
        jge .irq_pic2
        mov eax, irq_stub
        call idt_set_gate
        inc ecx
        jmp .irq_loop_pic1

.irq_pic2:
        ; Fill PIC2 IRQs (INT 0x28-0x2F) with dual-EOI stub
.irq_loop_pic2:
        cmp ecx, 0x30
        jge .syscall
        mov eax, irq_stub_pic2
        call idt_set_gate
        inc ecx
        jmp .irq_loop_pic2

.syscall:
        ; INT 0x80 = Syscall
        mov ecx, 0x80
        mov eax, syscall_handler
        ; Use trap gate with DPL=3 for syscalls
        mov edi, idt_table
        shl ecx, 3             ; ECX * 8 = offset in IDT
        add edi, ecx

        mov word [edi], ax      ; Offset low
        mov word [edi+2], 0x08  ; Code selector
        mov byte [edi+4], 0x00  ; Reserved
        mov byte [edi+5], 0xEF  ; Trap gate, DPL=3, Present
        shr eax, 16
        mov word [edi+6], ax    ; Offset high
        jmp .load

.load:
        lidt [idt_descriptor]
        ret

; Set IDT gate: ECX=vector, EAX=handler address
idt_set_gate:
        push edi
        mov edi, idt_table
        push ecx
        shl ecx, 3             ; ECX * 8 = offset in IDT
        add edi, ecx
        pop ecx

        mov word [edi], ax      ; Offset low
        mov word [edi+2], 0x08  ; Code segment selector
        mov byte [edi+4], 0x00  ; Reserved
        mov byte [edi+5], 0x8E  ; Interrupt gate, DPL=0, Present
        shr eax, 16
        mov word [edi+6], ax    ; Offset high

        pop edi
        ret
