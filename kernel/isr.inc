;=======================================================================
; ISR / IRQ HANDLERS
;=======================================================================

; Default handler for unregistered interrupts
isr_default:
        iretq

; Exception handler for exceptions WITHOUT error code
isr_exception_noerr:
        ; 64-bit stack: [RIP(8)] [CS(8)] [RFLAGS(8)] [RSP(8)] [SS(8)]
        ; Save the faulting RIP for display
        push rax
        mov rax, [rsp + 8]      ; RIP is at rsp+8 (after our push rax)
        mov [exc_rip], rax
        pop rax

        PUSHALL
        ; Restore text mode if in GUI so errors are visible
        cmp byte [gui_desktop_active], 0
        je .exc_noerr_text_ok
        mov byte [gui_desktop_active], 0
        call vbe_restore_text
.exc_noerr_text_ok:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_exception
        call vga_print
        ; Print faulting RIP
        mov esi, msg_exc_eip
        call vga_print
        mov eax, [exc_rip]
        call vga_print_hex
        mov al, 0x0A
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ; Recover to shell
        mov rsp, KERNEL_STACK
        sti
        jmp shell_main

; Exception handler for exceptions WITH error code
isr_exception_err:
        ; 64-bit stack: [ErrCode(8)] [RIP(8)] [CS(8)] [RFLAGS(8)] [RSP(8)] [SS(8)]
        push rax
        mov rax, [rsp + 16]     ; RIP is at rsp+16 (errcode + push rax)
        mov [exc_rip], rax
        mov rax, [rsp + 8]      ; Error code at rsp+8
        mov [exc_errcode], rax
        pop rax

        PUSHALL
        ; Restore text mode if in GUI so errors are visible
        cmp byte [gui_desktop_active], 0
        je .exc_err_text_ok
        mov byte [gui_desktop_active], 0
        call vbe_restore_text
.exc_err_text_ok:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_exception
        call vga_print
        ; Print faulting RIP
        mov esi, msg_exc_eip
        call vga_print
        mov eax, [exc_rip]
        call vga_print_hex
        ; Print error code
        mov esi, msg_exc_err
        call vga_print
        mov eax, [exc_errcode]
        call vga_print_hex
        mov al, 0x0A
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ; Remove 8-byte error code and recover to shell
        add rsp, 8
        mov rsp, KERNEL_STACK
        sti
        jmp shell_main

;---------------------------------------
; isr_fpu_nm - Exception 7: Device Not Available (#NM)
; Triggered when a task uses FPU instructions while CR0.TS is set.
; Implements lazy FPU context switching:
;   1. Save previous owner's FPU state (if any)
;   2. Restore current task's FPU state
;   3. Clear TS bit so FPU instructions proceed
;---------------------------------------
isr_fpu_nm:
        PUSHALL

        ; Clear the TS bit so FPU instructions work after we return
        clts

        ; Find current running task index
        mov esi, [task_current]

        ; Check if this task already owns the FPU
        cmp [fpu_owner], esi
        je .fpu_nm_done         ; Already owns it — just needed TS cleared

        ; Save the previous owner's FPU state (if there was one)
        mov eax, [fpu_owner]
        cmp eax, -1
        je .fpu_nm_no_save      ; No previous owner
        ; Calculate address: fpu_state_table + owner * FPU_STATE_SIZE
        imul rax, FPU_STATE_SIZE
        add rax, fpu_state_table
        fxsave [rax]            ; Save FPU/SSE state (512 bytes)

.fpu_nm_no_save:
        ; Restore current task's FPU state
        mov eax, esi
        imul rax, FPU_STATE_SIZE
        add rax, fpu_state_table
        ; Check if this slot has been initialized (first dword non-zero)
        cmp dword [rax], 0
        jne .fpu_nm_restore
        ; First FPU use by this task — just FNINIT
        fninit
        jmp .fpu_nm_set_owner
.fpu_nm_restore:
        fxrstor [rax]           ; Restore 512-byte FPU/SSE state

.fpu_nm_set_owner:
        mov [fpu_owner], esi

.fpu_nm_done:
        POPALL
        iretq

irq_stub:
        PUSHALL
        ; Send EOI to PIC1 only (for IRQs 2-7)
        mov al, 0x20
        out PIC1_CMD, al
        POPALL
        iretq

; IRQ stub for PIC2 IRQs (8-15) - send EOI to both PICs
irq_stub_pic2:
        PUSHALL
        mov al, 0x20
        out PIC2_CMD, al
        out PIC1_CMD, al
        POPALL
        iretq

; IRQ0: PIT Timer interrupt (100 Hz)
; Also handles preemptive multitasking: when a user-mode task is
; running, counts ticks and switches on quantum expiry.
irq_timer:
        PUSHALL
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
        ; Stack layout after PUSHALL (15×8=120 bytes): RSP+120=RIP, RSP+128=CS
        mov rax, [rsp + FRAME_CS]  ; CS from interrupt frame
        and rax, 3              ; RPL bits
        cmp rax, 3
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
        ; Save kernel RSP (points to PUSHALL frame + iretq frame)
        mov [edi + TCB_RSP], rsp
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
        mov rax, rsi
        imul rax, TCB_SIZE
        lea rbx, [task_table + rax]
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

        ; Update TSS RSP0 for future ring-3 → ring-0 transitions
        mov rax, [rbx + TCB_KSTACK]
        mov [tss_struct + 4], rax

        ; Load the new task's saved kernel RSP
        mov rsp, [rbx + TCB_RSP]

        ; Deliver any pending signals to the new task
        ; (may modify the IRETQ frame to redirect to signal handler)
        call sched_deliver_signals

        ; Set CR0.TS so the next FPU instruction triggers #NM for lazy FPU switching
        mov rax, cr0
        or eax, (1 << 3)       ; Set TS bit
        mov cr0, rax

        ; Restore GP regs and IRETQ to new task's ring-3 context
        ; (IRETQ restores RFLAGS which re-enables IF for user mode)
        POPALL
        iretq

.timer_done:
        POPALL
        iretq

; IRQ1: Keyboard interrupt
irq_keyboard:
        PUSHALL

        ; Read scancode from keyboard controller
        in al, KB_DATA

        ; Check for E0 extended key prefix
        cmp al, 0xE0
        je .e0_prefix

        ; Check if it's a key release (bit 7 set)
        test al, 0x80
        jnz .key_up

        ; Key press: check if previous was E0 prefix
        movzx ebx, al
        cmp byte [kb_e0_flag], 0
        je .normal_key

        ; Extended key press — clear flag and translate
        mov byte [kb_e0_flag], 0
        cmp bl, 0x47
        je .e0_home
        cmp bl, 0x4F
        je .e0_end
        cmp bl, 0x53
        je .e0_delete
        ; Other E0 keys (arrows, Right Ctrl/Alt): fall through

.normal_key:
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
        mov rsp, KERNEL_STACK
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
        mov byte [kb_e0_flag], 0    ; Clear E0 flag on any release
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
        POPALL
        iretq

.e0_prefix:
        mov byte [kb_e0_flag], 1
        jmp .eoi

.e0_home:
        mov al, KEY_HOME
        jmp .store_key

.e0_end:
        mov al, KEY_END
        jmp .store_key

.e0_delete:
        mov al, KEY_DELETE
        jmp .store_key
