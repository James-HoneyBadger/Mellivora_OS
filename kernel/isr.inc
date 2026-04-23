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
        ; Restore text mode if in GUI so errors are visible
        cmp byte [gui_desktop_active], 0
        je .exc_noerr_text_ok
        mov byte [gui_desktop_active], 0
        call vbe_restore_text
.exc_noerr_text_ok:
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
        ; Print register dump from pushad frame
        call exc_dump_regs
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
        ; Restore text mode if in GUI so errors are visible
        cmp byte [gui_desktop_active], 0
        je .exc_err_text_ok
        mov byte [gui_desktop_active], 0
        call vbe_restore_text
.exc_err_text_ok:
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
        ; Print register dump from pushad frame
        call exc_dump_regs
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ; Recover to shell
        mov esp, KERNEL_STACK
        sti
        jmp shell_main

;---------------------------------------
; exc_dump_regs - Print register contents from pushad frame.
; Must be called from within a pushad block.
; Pushad frame layout (at ESP on entry to this routine, after CALL):
;   [ESP+4]  = saved EDI    [ESP+8]  = saved ESI
;   [ESP+12] = saved EBP    [ESP+16] = orig ESP (pre-pushad)
;   [ESP+20] = saved EBX    [ESP+24] = saved EDX
;   [ESP+28] = saved ECX    [ESP+32] = saved EAX
;---------------------------------------
exc_dump_regs:
        push esi
        push eax

        mov esi, msg_exc_regs
        call vga_print

        ; EAX
        mov esi, msg_exc_eax
        call vga_print
        mov eax, [esp + 32 + 4]   ; +4 for push esi, +4 for push eax above, but
        ; recalculate: after push esi + push eax, esp moved by 8
        ; pushad frame base was at esp+8 relative to entry of exc_dump_regs
        ; [entry_esp+4..+32] = EDI..EAX  → adjust for two pushes = +8 more
        ; So saved EAX = [entry_esp + 4 + 8 + 28] ... let's recompute cleanly.
        ; On entry to exc_dump_regs, ESP = ret_addr, then +4 = pushad[EDI]
        ; After "push esi; push eax" ESP moved -8, so pushad[EDI] = [esp+12]
        ; pushad: EDI=+12, ESI=+16, EBP=+20, origESP=+24, EBX=+28, EDX=+32, ECX=+36, EAX=+40
        mov eax, [esp + 40]   ; saved EAX
        call vga_print_hex
        mov al, ' '
        call vga_putchar
        ; EBX
        mov esi, msg_exc_ebx
        call vga_print
        mov eax, [esp + 28]   ; saved EBX
        call vga_print_hex
        mov al, ' '
        call vga_putchar
        ; ECX
        mov esi, msg_exc_ecx
        call vga_print
        mov eax, [esp + 36]   ; saved ECX
        call vga_print_hex
        mov al, 0x0A
        call vga_putchar
        ; EDX
        mov esi, msg_exc_edx
        call vga_print
        mov eax, [esp + 32]   ; saved EDX
        call vga_print_hex
        mov al, ' '
        call vga_putchar
        ; ESI
        mov esi, msg_exc_esi
        call vga_print
        mov eax, [esp + 16]   ; saved ESI
        call vga_print_hex
        mov al, ' '
        call vga_putchar
        ; EDI
        mov esi, msg_exc_edi
        call vga_print
        mov eax, [esp + 12]   ; saved EDI
        call vga_print_hex
        mov al, 0x0A
        call vga_putchar
        ; EBP
        mov esi, msg_exc_ebp
        call vga_print
        mov eax, [esp + 20]   ; saved EBP
        call vga_print_hex
        mov al, ' '
        call vga_putchar
        ; ESP (pre-pushad, pre-exception stack pointer)
        mov esi, msg_exc_esp
        call vga_print
        mov eax, [esp + 24]   ; orig ESP from pushad
        call vga_print_hex
        mov al, 0x0A
        call vga_putchar

        ; Also echo to COM1 for remote debugging
        call serial_exc_dump

        pop eax
        pop esi
        ret

;---------------------------------------
; serial_exc_dump - Send exception info over COM1 serial port.
; Called from within exc_dump_regs (push esi + push eax active there).
; Stack layout when we execute here (after our own push esi + push eax):
;   [esp+0]  saved eax (ours)   [esp+4]  saved esi (ours)
;   [esp+8]  ret addr (call serial_exc_dump)
;   [esp+12] saved eax (exc_dump_regs)  [esp+16] saved esi (exc_dump_regs)
;   [esp+20] ret addr (call exc_dump_regs)
;   pushad frame: EDI=+24, ESI=+28, EBP=+32, origESP=+36, EBX=+40, EDX=+44, ECX=+48, EAX=+52
;---------------------------------------
serial_exc_dump:
        push esi
        push eax
        mov esi, msg_exception
        call serial_print
        mov esi, msg_exc_eip
        call serial_print
        mov eax, [exc_eip]
        call serial_print_hex
        mov esi, msg_exc_regs
        call serial_print
        mov esi, msg_exc_eax
        call serial_print
        mov eax, [esp + 52]     ; saved EAX from pushad
        call serial_print_hex
        mov esi, msg_exc_ebx
        call serial_print
        mov eax, [esp + 40]     ; saved EBX from pushad
        call serial_print_hex
        mov esi, msg_exc_ecx
        call serial_print
        mov eax, [esp + 48]     ; saved ECX from pushad
        call serial_print_hex
        mov esi, msg_exc_edx
        call serial_print
        mov eax, [esp + 44]     ; saved EDX from pushad
        call serial_print_hex
        pop eax
        pop esi
        ret

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
; Also handles preemptive multitasking: when a user-mode task is
; running, counts ticks and switches on quantum expiry.
irq_timer:
        pushad
        inc dword [tick_count]

        ; Send EOI first (so PIC isn't held up during context switch)
        mov al, 0x20
        out PIC1_CMD, al

        ; v3.0: Wake any sleeping/blocked tasks whose timer expired
        call sched_wake_sleepers

        ; --- Preemptive scheduling ---
        ; Only preempt if we have active tasks
        cmp dword [task_count], 0
        je .timer_done

        ; Check if we interrupted a ring-3 task (CS on stack has RPL=3)
        ; Stack layout after pushad: ESP+32 = EIP, ESP+36 = CS
        mov eax, [esp + 36]     ; CS from interrupt frame
        and eax, 3              ; RPL bits
        cmp eax, 3
        jne .timer_done         ; Not ring 3 — don't preempt kernel code

        ; Count down the current task's time slice
        inc dword [sched_tick]
        mov eax, [sched_tick]
        cmp eax, SCHED_QUANTUM
        jb .timer_done

        ; Quantum expired — context switch
        mov dword [sched_tick], 0

        ; Find the current RUNNING task
        mov edi, task_table
        xor edx, edx
.timer_find_cur:
        cmp edx, MAX_TASKS
        jge .timer_done        ; No running task
        cmp dword [edi + TCB_STATE], TASK_RUNNING
        je .timer_save
        add edi, TCB_SIZE
        inc edx
        jmp .timer_find_cur

.timer_save:
        ; Save kernel ESP (points to pushad frame + iretd frame)
        mov [edi + TCB_ESP], esp
        mov dword [edi + TCB_STATE], TASK_READY

        ; Round-robin scan for next READY task
        mov ecx, MAX_TASKS
        mov esi, edx
.timer_scan:
        inc esi
        cmp esi, MAX_TASKS
        jb .timer_check
        xor esi, esi
.timer_check:
        mov eax, esi
        imul eax, TCB_SIZE
        lea ebx, [task_table + eax]
        cmp dword [ebx + TCB_STATE], TASK_READY
        je .timer_switch
        dec ecx
        jnz .timer_scan

        ; No other ready task — re-mark self as RUNNING
        mov dword [edi + TCB_STATE], TASK_RUNNING
        jmp .timer_done

.timer_switch:
        ; v3.0: Disable interrupts during critical context switch.
        ; EOI was already sent above, so the PIC could deliver another
        ; IRQ between now and IRETD if interrupts were enabled.
        ; Interrupt gates clear IF on entry, but CLI is defensive in
        ; case the gate type ever changes or an NMI arrives.
        cli

        ; EBX = new task's TCB
        mov dword [ebx + TCB_STATE], TASK_RUNNING
        mov [task_current], esi

        ; Update TSS ESP0 for future ring-3 → ring-0 transitions
        mov eax, [ebx + TCB_KSTACK]
        mov [tss_struct + 4], eax

        ; Load the new task's saved kernel ESP
        mov esp, [ebx + TCB_ESP]

        ; Restore GP regs and IRETD to new task's ring-3 context
        ; (IRETD restores EFLAGS which re-enables IF for user mode)
        popad
        iretd

.timer_done:
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

        ; Check for alt press
        cmp bl, 0x38            ; Left alt press
        je .alt_on

        ; Check for shift press BEFORE ASCII lookup
        cmp bl, 0x2A            ; Left shift press
        je .shift_on
        cmp bl, 0x36            ; Right shift press
        je .shift_on

        ; Check for Alt+Tab (Alt held + Tab scancode)
        cmp bl, 0x0F            ; Tab scancode
        jne .not_alt_tab
        cmp byte [kb_alt], 1
        jne .not_alt_tab
        ; Inject Alt+Tab special code (0x8E) into keyboard buffer
        mov al, 0x8E
        jmp .store_key
.not_alt_tab:

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
        ; Hard-abort to shell — clean up redirection state
        mov esp, KERNEL_STACK
        call shell_redir_reset
        mov byte [vga_color], COLOR_DEFAULT
        sti
        jmp shell_main
.no_ctrl:
.store_key:
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

.alt_on:
        or byte [kb_alt], 1
        jmp .eoi

.key_up:
        and al, 0x7F            ; Remove release bit
        cmp al, 0x1D
        je .ctrl_off
        cmp al, 0x38
        je .alt_off
        cmp al, 0x2A
        je .shift_off
        cmp al, 0x36
        je .shift_off
        jmp .eoi

.ctrl_off:
        and byte [kb_ctrl], ~1
        jmp .eoi

.alt_off:
        and byte [kb_alt], ~1
        jmp .eoi

.shift_off:
        and byte [kb_shift], ~1

.eoi:
        mov al, 0x20
        out PIC1_CMD, al
        popad
        iretd
