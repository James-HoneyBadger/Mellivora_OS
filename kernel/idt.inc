;=======================================================================
; IDT (Interrupt Descriptor Table) — 64-bit Long Mode
; Each entry is 16 bytes (vs 8 in 32-bit)
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
        ; Override exception 7 (#NM) with FPU lazy-switch handler
        mov ecx, 7
        mov eax, isr_fpu_nm
        call idt_set_gate

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
        mov rdi, idt_table
        push rcx
        shl ecx, 4             ; ECX * 16 = offset in IDT
        add rdi, rcx
        pop rcx

        mov word [rdi], ax      ; Offset bits 0-15
        mov word [rdi+2], 0x08  ; Code selector
        mov byte [rdi+4], 0x00  ; IST=0
        mov byte [rdi+5], 0xEF  ; Trap gate, DPL=3, Present
        mov rdx, rax
        shr rdx, 16
        mov word [rdi+6], dx    ; Offset bits 16-31
        shr rdx, 16
        mov word [rdi+8], dx    ; Offset bits 32-47
        shr rdx, 16
        mov word [rdi+10], dx   ; Offset bits 48-63
        mov dword [rdi+12], 0   ; Reserved
        jmp .load

.load:
        lidt [idt_descriptor]
        ret

; Set IDT gate: ECX=vector, EAX=handler address (RAX preserved)
; Writes a 16-byte 64-bit interrupt gate descriptor
idt_set_gate:
        push rdi
        push rdx
        mov rdi, idt_table
        push rcx
        shl ecx, 4             ; ECX * 16 = offset in IDT
        add rdi, rcx
        pop rcx

        mov word [rdi], ax      ; Offset bits 0-15
        mov word [rdi+2], 0x08  ; Code segment selector
        mov byte [rdi+4], 0x00  ; IST=0, reserved
        mov byte [rdi+5], 0x8E  ; Interrupt gate, DPL=0, Present
        mov rdx, rax
        shr rdx, 16
        mov word [rdi+6], dx    ; Offset bits 16-31
        shr rdx, 16
        mov word [rdi+8], dx    ; Offset bits 32-47
        shr rdx, 16
        mov word [rdi+10], dx   ; Offset bits 48-63
        mov dword [rdi+12], 0   ; Reserved (must be zero)

        pop rdx
        pop rdi
        ret
