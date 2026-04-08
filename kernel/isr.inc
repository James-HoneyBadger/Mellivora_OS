;=======================================================================
; ISR / IRQ HANDLERS
;=======================================================================

; Default handler for unregistered interrupts
isr_default:
        iretd

; Exception handler for exceptions WITHOUT error code
isr_exception_noerr:
        ; Stack: [EIP] [CS] [EFLAGS]
        ; Save the faulting EIP for display
        push eax
        mov eax, [esp + 4]      ; EIP is at esp+4 (after our push eax)
        mov [exc_eip], eax
        pop eax

        pushad
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_exception
        call vga_print
        ; Print faulting EIP
        mov esi, msg_exc_eip
        call vga_print
        mov eax, [exc_eip]
        call vga_print_hex
        mov al, 0x0A
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ; Recover to shell
        mov esp, KERNEL_STACK
        sti
        jmp shell_main

; Exception handler for exceptions WITH error code
isr_exception_err:
        ; Stack: [ErrorCode] [EIP] [CS] [EFLAGS]
        push eax
        mov eax, [esp + 8]      ; EIP is at esp+8 (errcode + push eax)
        mov [exc_eip], eax
        mov eax, [esp + 4]      ; Error code at esp+4
        mov [exc_errcode], eax
        pop eax

        pushad
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_exception
        call vga_print
        ; Print faulting EIP
        mov esi, msg_exc_eip
        call vga_print
        mov eax, [exc_eip]
        call vga_print_hex
        ; Print error code
        mov esi, msg_exc_err
        call vga_print
        mov eax, [exc_errcode]
        call vga_print_hex
        mov al, 0x0A
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ; Recover to shell
        mov esp, KERNEL_STACK
        sti
        jmp shell_main

irq_stub:
        pushad
        ; Send EOI to PIC1 only (for IRQs 2-7)
        mov al, 0x20
        out PIC1_CMD, al
        popad
        iretd

; IRQ stub for PIC2 IRQs (8-15) - send EOI to both PICs
irq_stub_pic2:
        pushad
        mov al, 0x20
        out PIC2_CMD, al
        out PIC1_CMD, al
        popad
        iretd

; IRQ0: PIT Timer interrupt (100 Hz)
irq_timer:
        pushad
        inc dword [tick_count]

        ; Send EOI
        mov al, 0x20
        out PIC1_CMD, al
        popad
        iretd

; IRQ1: Keyboard interrupt
irq_keyboard:
        pushad

        ; Read scancode from keyboard controller
        in al, KB_DATA

        ; Check if it's a key release (bit 7 set)
        test al, 0x80
        jnz .key_up

        ; Key press: check for modifier keys first
        movzx ebx, al
        cmp ebx, 128
        jge .eoi

        ; Check for ctrl press
        cmp bl, 0x1D            ; Left ctrl press
        je .ctrl_on

        ; Check for shift press BEFORE ASCII lookup
        cmp bl, 0x2A            ; Left shift press
        je .shift_on
        cmp bl, 0x36            ; Right shift press
        je .shift_on

        ; Translate scancode to ASCII
        mov al, [scancode_table + ebx]
        or al, al
        jz .eoi                 ; Non-printable key

        ; Store in keyboard buffer (ring buffer)
        test byte [kb_shift], 1
        jz .no_shift
        cmp al, 0x20            ; Don't shift-translate control chars
        jl .no_shift
        movzx ebx, al
        mov al, [shift_table + ebx - 0x20]
        cmp al, 0
        je .no_shift_store
.no_shift:
        ; If Ctrl is held, generate control code for letters
        test byte [kb_ctrl], 1
        jz .no_ctrl
        cmp al, 'A'
        jl .no_ctrl
        cmp al, 'z'
        jg .no_ctrl
        and al, 0x1F            ; Ctrl+A=1, Ctrl+C=3, etc.
        ; Check for Ctrl+C program abort
        cmp al, CTRL_C_CODE
        jne .no_ctrl
        cmp byte [program_running], 1
        jne .no_ctrl
        ; Abort the running program
        mov byte [program_running], 0
        mov dword [program_exit_code], 0xFFFFFFFF
        mov byte [ctrl_c_flag], 1
        ; Send EOI to PIC
        mov al, 0x20
        out PIC1_CMD, al
        ; Hard-abort to shell
        mov esp, KERNEL_STACK
        mov byte [vga_color], COLOR_DEFAULT
        sti
        jmp shell_main
.no_ctrl:
        ; Check if buffer is full before writing
        mov ebx, [kb_write_idx]
        lea ecx, [ebx + 1]
        and ecx, KB_BUFFER_SIZE - 1
        cmp ecx, [kb_read_idx]
        je .eoi                 ; Buffer full, drop keystroke
        mov [kb_buffer + ebx], al
        mov [kb_write_idx], ecx
        jmp .eoi

.no_shift_store:
        jmp .eoi

.shift_on:
        or byte [kb_shift], 1
        jmp .eoi

.ctrl_on:
        or byte [kb_ctrl], 1
        jmp .eoi

.key_up:
        and al, 0x7F            ; Remove release bit
        cmp al, 0x1D
        je .ctrl_off
        cmp al, 0x2A
        je .shift_off
        cmp al, 0x36
        je .shift_off
        jmp .eoi

.ctrl_off:
        and byte [kb_ctrl], ~1
        jmp .eoi

.shift_off:
        and byte [kb_shift], ~1

.eoi:
        mov al, 0x20
        out PIC1_CMD, al
        popad
        iretd
