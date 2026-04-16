;=======================================================================
; sched.inc - Preemptive task scheduler
;
; Provides TCB management, round-robin scheduling, SYS_YIELD, and
; preemptive time-slicing via the PIT timer (100 Hz).
;
; The PIT IRQ handler (irq_timer in isr.inc) counts ticks per task.
; When SCHED_QUANTUM ticks elapse, the current ring-3 task is preempted
; and the next READY task runs.  Programs can also call SYS_YIELD for
; cooperative yielding.
;=======================================================================

; Task states
TASK_FREE    equ 0
TASK_READY   equ 1
TASK_RUNNING equ 2
TASK_BLOCKED equ 3          ; v3.0: Blocked (sleeping / waiting on I/O)

; Preemptive scheduling quantum (in PIT ticks, 100 Hz)
; 10 ticks = 100 ms per time slice
SCHED_QUANTUM equ 10

; TCB field offsets (64 bytes per entry — 64-bit long mode)
TCB_RSP      equ 0          ; 8 bytes: Saved kernel-mode RSP (PUSHALL+iretq frame)
TCB_STATE    equ 8          ; 4 bytes: Task state (FREE / READY / RUNNING / BLOCKED)
TCB_KSTACK   equ 16         ; 8 bytes: Kernel stack top (for TSS RSP0)
TCB_USTACK   equ 24         ; 8 bytes: User-mode stack top
TCB_ENTRY    equ 32         ; 8 bytes: Program entry point (RIP)
TCB_PID      equ 40         ; 4 bytes: Task ID
TCB_PRIORITY equ 44         ; 4 bytes: Task priority (0=highest, 3=lowest)
TCB_WAKEUP   equ 48         ; 4 bytes: Wakeup tick count (for sys_sleep)
TCB_SIZE     equ 64         ; Padded to 64-byte alignment

MAX_TASKS    equ 32          ; v3.0: Doubled from 16

; FPU/SSE state size per task (FXSAVE/FXRSTOR = 512 bytes, 16-byte aligned)
FPU_STATE_SIZE equ 512

;---------------------------------------
; sched_init - Initialize the scheduler
; Clears the task table. No tasks are
; created — the shell is not a task.
;---------------------------------------
sched_init:
        PUSHALL
        ; Zero out the task table
        mov edi, task_table
        xor eax, eax
        mov ecx, (MAX_TASKS * TCB_SIZE) / 8
        rep stosq
        mov dword [task_count], 0
        mov dword [task_current], 0
        mov dword [sched_tick], 0
        mov dword [fpu_owner], -1   ; No task owns the FPU yet
        ; Clear signal handler and pending tables
        mov edi, sig_handlers
        xor eax, eax
        mov ecx, (MAX_TASKS * SIG_ENTRY_SIZE) / 8
        rep stosq
        mov edi, sig_pending
        mov ecx, MAX_TASKS / 4
        rep stosd
        POPALL
        ret

;---------------------------------------
; sched_create_task - Register a new task
;  EBX = program entry point (ring 3 RIP)
;  ECX = user-mode stack top (ring 3 RSP)
; Returns:
;  EAX = task ID (0..MAX_TASKS-1), or -1 on failure
;---------------------------------------
sched_create_task:
        PUSHALL
        ; Find a free TCB slot
        xor edx, edx               ; slot index
        mov edi, task_table
.find_slot:
        cmp edx, MAX_TASKS
        jge .create_fail
        cmp dword [edi + TCB_STATE], TASK_FREE
        je .found_slot
        add edi, TCB_SIZE
        inc edx
        jmp .find_slot

.found_slot:
        ; Allocate a 4KB page for this task's kernel stack
        push rdx
        push rdi
        push rbx
        push rcx
        call pmm_alloc_page         ; EAX = physical page address (or 0)
        test eax, eax
        jz .create_fail_pop

        ; Kernel stack top = page + 4096
        mov rsi, rax
        add rsi, 4096               ; RSI = kstack_top

        pop rcx                     ; user stack
        pop rbx                     ; entry point
        pop rdi                     ; TCB pointer
        pop rdx                     ; slot index

        ; Store kernel stack page top
        mov [edi + TCB_KSTACK], rsi

        ; Build an initial switch frame on the kernel stack (64-bit)
        ; Layout (160 bytes: IRETQ frame 40 bytes + PUSHALL 120 bytes):
        ;   [kstack_top - 8]   = SS3       (USER_DS, qword)
        ;   [kstack_top - 16]  = RSP3      (user stack, qword)
        ;   [kstack_top - 24]  = RFLAGS    (0x200 = IF set, qword)
        ;   [kstack_top - 32]  = CS3       (USER_CS, qword)
        ;   [kstack_top - 40]  = RIP       (entry point, qword)
        ;   [kstack_top - 48]  = RAX       (0)   -- PUSHALL frame --
        ;   [kstack_top - 56]  = RBX       (0)
        ;   [kstack_top - 64]  = RCX       (0)
        ;   [kstack_top - 72]  = RDX       (0)
        ;   [kstack_top - 80]  = RSI       (0)
        ;   [kstack_top - 88]  = RDI       (0)
        ;   [kstack_top - 96]  = RBP       (0)
        ;   [kstack_top - 104] = R8        (0)
        ;   [kstack_top - 112] = R9        (0)
        ;   [kstack_top - 120] = R10       (0)
        ;   [kstack_top - 128] = R11       (0)
        ;   [kstack_top - 136] = R12       (0)
        ;   [kstack_top - 144] = R13       (0)
        ;   [kstack_top - 152] = R14       (0)
        ;   [kstack_top - 160] = R15       (0)

        ; IRETQ frame (qword slots, required by long mode)
        mov qword [rsi - 8],  USER_DS        ; SS3
        mov qword [rsi - 16], rcx            ; RSP3 (user stack)
        mov qword [rsi - 24], 0x200          ; RFLAGS (IF=1)
        mov qword [rsi - 32], USER_CS        ; CS3
        mov qword [rsi - 40], rbx            ; RIP = entry point

        ; PUSHALL frame (15 general-purpose registers, all zero)
        xor eax, eax
        mov qword [rsi - 48],  rax           ; RAX
        mov qword [rsi - 56],  rax           ; RBX
        mov qword [rsi - 64],  rax           ; RCX
        mov qword [rsi - 72],  rax           ; RDX
        mov qword [rsi - 80],  rax           ; RSI
        mov qword [rsi - 88],  rax           ; RDI
        mov qword [rsi - 96],  rax           ; RBP
        mov qword [rsi - 104], rax           ; R8
        mov qword [rsi - 112], rax           ; R9
        mov qword [rsi - 120], rax           ; R10
        mov qword [rsi - 128], rax           ; R11
        mov qword [rsi - 136], rax           ; R12
        mov qword [rsi - 144], rax           ; R13
        mov qword [rsi - 152], rax           ; R14
        mov qword [rsi - 160], rax           ; R15

        ; Saved RSP = kstack_top - 160
        sub esi, 160
        mov [edi + TCB_RSP], rsi

        ; Fill remaining TCB fields
        mov dword [edi + TCB_STATE], TASK_READY
        mov [edi + TCB_USTACK], rcx
        mov [edi + TCB_ENTRY], rbx
        mov [edi + TCB_PID], edx

        inc dword [task_count]

        ; Return task ID in EAX (via PUSHALL slot)
        mov qword [rsp + FRAME_RAX], rdx    ; Patch RAX in PUSHALL frame
        POPALL
        ret

.create_fail_pop:
        pop rcx
        pop rbx
        pop rdi
        pop rdx
.create_fail:
        mov qword [rsp + FRAME_RAX], -1     ; Return -1
        POPALL
        ret

;---------------------------------------
; sched_exit_task - Terminate current task
; Called when a task's program exits.
; Frees the kernel stack page and TCB.
;---------------------------------------
sched_exit_task:
        PUSHALL
        cmp dword [task_count], 0
        je .exit_done

        ; Find the RUNNING task
        mov edi, task_table
        xor edx, edx
.exit_find:
        cmp edx, MAX_TASKS
        jge .exit_done
        cmp dword [edi + TCB_STATE], TASK_RUNNING
        je .exit_found
        add edi, TCB_SIZE
        inc edx
        jmp .exit_find

.exit_found:
        ; If this task owns the FPU, release ownership
        cmp [fpu_owner], edx
        jne .exit_no_fpu
        mov dword [fpu_owner], -1
.exit_no_fpu:

        ; Clear signal state for this task (save TCB pointer in r8)
        mov r8, rdi
        mov byte [sig_pending + edx], 0
        mov eax, edx
        imul rax, SIG_ENTRY_SIZE
        lea rdi, [sig_handlers + rax]
        xor eax, eax
        mov ecx, SIG_MAX
        rep stosq
        mov rdi, r8             ; restore TCB pointer

        ; Free the kernel stack page (kstack_top - 4096 = page base)
        mov rax, [rdi + TCB_KSTACK]
        sub rax, 4096
        call pmm_free_page

        ; Clear the TCB
        mov dword [edi + TCB_STATE], TASK_FREE
        mov qword [edi + TCB_RSP], 0
        mov qword [edi + TCB_KSTACK], 0

        dec dword [task_count]

.exit_done:
        POPALL
        ret

;---------------------------------------
; sys_yield - SYS_YIELD syscall handler
;
; Cooperatively yields the CPU to the
; next ready task. If no other tasks are
; ready, returns immediately.
;
; Called from the INT 0x80 dispatcher.
; Stack has the IRETD frame from the CPU.
;---------------------------------------
sys_yield:
        ; Fast path: if 0 or 1 tasks, nothing to switch to
        cmp dword [task_count], 2
        jb .yield_nop

        ; Save all GP registers onto the kernel stack
        PUSHALL

        ; Find the current RUNNING task
        mov edi, task_table
        xor edx, edx
.yld_find_cur:
        cmp edx, MAX_TASKS
        jge .yld_abort              ; No running task? Shouldn't happen
        cmp dword [edi + TCB_STATE], TASK_RUNNING
        je .yld_save
        add edi, TCB_SIZE
        inc edx
        jmp .yld_find_cur

.yld_save:
        ; Save current kernel RSP
        mov [edi + TCB_RSP], rsp
        ; Mark as READY
        mov dword [edi + TCB_STATE], TASK_READY

        ; Round-robin: scan forward from current+1
        mov ecx, MAX_TASKS          ; iteration limit
        mov esi, edx                ; start index
.yld_scan:
        inc esi
        cmp esi, MAX_TASKS
        jb .yld_check
        xor esi, esi                ; wrap to 0
.yld_check:
        ; Calculate TCB offset for slot ESI
        mov rax, rsi
        imul rax, TCB_SIZE
        lea rbx, [task_table + rax]
        cmp dword [ebx + TCB_STATE], TASK_READY
        je .yld_switch
        dec ecx
        jnz .yld_scan

        ; No other ready task found — re-mark self as RUNNING
        mov dword [edi + TCB_STATE], TASK_RUNNING
        jmp .yld_abort

.yld_switch:
        ; EBX = pointer to new task's TCB, ESI = new task index
        mov dword [ebx + TCB_STATE], TASK_RUNNING
        mov [task_current], esi

        ; Update TSS RSP0 so future ring-3→0 transitions use the new kernel stack
        mov rax, [rbx + TCB_KSTACK]
        mov [tss_struct + 4], rax

        ; Load the new task's kernel RSP (with its PUSHALL+iretq frame)
        mov rsp, [rbx + TCB_RSP]

        ; Set CR0.TS for lazy FPU switching
        mov rax, cr0
        or eax, (1 << 3)
        mov cr0, rax

        ; Restore GP regs and return to the new task's ring-3 context
        POPALL
        iretq

.yld_abort:
        ; Couldn't switch — restore current task state and return
        POPALL
.yield_nop:
        iretq

;---------------------------------------
; sys_getpid - Return current task PID
; Returns EAX = task PID (or -1 if shell)
;---------------------------------------
sys_getpid:
        ; Find RUNNING task
        mov edi, task_table
        xor ecx, ecx
.gp_find:
        cmp ecx, MAX_TASKS
        jge .gp_shell
        cmp dword [edi + TCB_STATE], TASK_RUNNING
        je .gp_found
        add edi, TCB_SIZE
        inc ecx
        jmp .gp_find
.gp_found:
        mov eax, [edi + TCB_PID]
        iretq
.gp_shell:
        mov eax, -1
        iretq

;---------------------------------------
; sys_kill - Terminate task by PID
;   EBX = PID to kill
; Returns EAX = 0 on success, -1 on fail
;---------------------------------------
sys_kill:
        PUSHALL
        ; Find task with matching PID
        mov edi, task_table
        xor ecx, ecx
.sk_find:
        cmp ecx, MAX_TASKS
        jge .sk_fail
        cmp dword [edi + TCB_STATE], TASK_FREE
        je .sk_next
        cmp [edi + TCB_PID], ebx
        je .sk_found
.sk_next:
        add edi, TCB_SIZE
        inc ecx
        jmp .sk_find

.sk_found:
        ; Release FPU ownership if this task owns it
        cmp [fpu_owner], ecx
        jne .sk_no_fpu
        mov dword [fpu_owner], -1
.sk_no_fpu:
        ; Clear signal state
        mov byte [sig_pending + ecx], 0
        ; Free the kernel stack page
        mov rax, [rdi + TCB_KSTACK]
        test rax, rax
        jz .sk_clear
        sub rax, 4096
        call pmm_free_page

.sk_clear:
        ; Clear the TCB
        mov dword [edi + TCB_STATE], TASK_FREE
        mov qword [edi + TCB_RSP], 0
        mov qword [edi + TCB_KSTACK], 0
        dec dword [task_count]

        mov qword [rsp + FRAME_RAX], 0    ; Return EAX=0
        POPALL
        iretq

.sk_fail:
        mov qword [rsp + FRAME_RAX], -1   ; Return EAX=-1
        POPALL
        iretq

;---------------------------------------
; sys_proclist - Get task info for a scheduler slot
; EBX = slot index (0..MAX_TASKS-1)
; ECX = pointer to 16-byte output buffer
; Output buffer: [state(4), pid(4), entry(8), rsp(8)]
; Returns: EAX=0 on success, -1 if invalid slot
;---------------------------------------
sys_proclist:
        PUSHALL
        cmp ebx, MAX_TASKS
        jge .spl_fail
        ; Calculate TCB address
        mov rdi, rbx
        imul rdi, TCB_SIZE
        add rdi, task_table
        ; Fill output buffer (24 bytes)
        mov eax, [rdi + TCB_STATE]
        mov [rcx], eax
        mov eax, [rdi + TCB_PID]
        mov [rcx + 4], eax
        mov rax, [rdi + TCB_ENTRY]
        mov [rcx + 8], rax
        mov rax, [rdi + TCB_RSP]
        mov [rcx + 16], rax
        mov qword [rsp + FRAME_RAX], 0
        POPALL
        iretq
.spl_fail:
        mov qword [rsp + FRAME_RAX], -1
        POPALL
        iretq

;---------------------------------------
; sys_sleep - Block current task for N
;             PIT ticks (each = 10 ms)
;   EBX = number of ticks to sleep
; Returns: EAX = 0 on wakeup
;
; v3.0: Sets TASK_BLOCKED and stores the
;       target wakeup tick in TCB_WAKEUP.
;       irq_timer checks blocked tasks each
;       tick and wakes them when time expires.
;
; When task_count == 0 (shell / single-program context), falls back
; to a HLT-based busy-wait so that timing programs and poll loops
; still work correctly even without a scheduler task.
;---------------------------------------
sys_sleep:
        cmp dword [task_count], 0
        je .sleep_hlt_fallback

        PUSHALL

        ; Find the current RUNNING task
        mov edi, task_table
        xor edx, edx
.sleep_find:
        cmp edx, MAX_TASKS
        jge .sleep_abort
        cmp dword [edi + TCB_STATE], TASK_RUNNING
        je .sleep_block
        add edi, TCB_SIZE
        inc edx
        jmp .sleep_find

.sleep_block:
        ; Calculate wakeup tick = tick_count + EBX
        mov eax, [tick_count]
        add eax, ebx
        mov [edi + TCB_WAKEUP], eax

        ; Mark as BLOCKED and save RSP
        mov dword [edi + TCB_STATE], TASK_BLOCKED
        mov [edi + TCB_RSP], rsp

        ; Try to find another READY task to run
        mov ecx, MAX_TASKS
        mov esi, edx
.sleep_scan:
        inc esi
        cmp esi, MAX_TASKS
        jb .sleep_check
        xor esi, esi
.sleep_check:
        mov rax, rsi
        imul rax, TCB_SIZE
        lea rbx, [task_table + rax]
        cmp dword [ebx + TCB_STATE], TASK_READY
        je .sleep_switch
        dec ecx
        jnz .sleep_scan

        ; No other task — unblock self immediately (can't sleep)
        mov dword [edi + TCB_STATE], TASK_RUNNING
        jmp .sleep_abort

.sleep_switch:
        mov dword [ebx + TCB_STATE], TASK_RUNNING
        mov [task_current], esi
        mov rax, [rbx + TCB_KSTACK]
        mov [tss_struct + 4], rax
        mov rsp, [rbx + TCB_RSP]
        ; Set CR0.TS for lazy FPU switching
        mov rax, cr0
        or eax, (1 << 3)
        mov cr0, rax
        POPALL
        iretq

.sleep_abort:
        POPALL
        ; HLT fallback: shell/single-program context, no tasks to switch to
.sleep_hlt_fallback:
        ; EBX = ticks to sleep; sleep via HLT loop (timer fires every 10 ms)
        test ebx, ebx
        jz .sleep_hlt_done
        push rcx
        mov ecx, [tick_count]
        add ecx, ebx            ; wakeup tick = now + EBX
.sleep_hlt_loop:
        hlt
        cmp [tick_count], ecx
        jb .sleep_hlt_loop
        pop rcx
.sleep_hlt_done:
        xor eax, eax
        iretq

;---------------------------------------
; sched_wake_sleepers - Check all BLOCKED
;     tasks and wake those whose wakeup
;     tick has been reached.
; Called from irq_timer every tick.
; Clobbers: EAX, ECX, EDI (caller saves)
;---------------------------------------
sched_wake_sleepers:
        mov edi, task_table
        xor ecx, ecx
.wake_loop:
        cmp ecx, MAX_TASKS
        jge .wake_done
        cmp dword [edi + TCB_STATE], TASK_BLOCKED
        jne .wake_next
        ; Check if wakeup tick has been reached
        mov eax, [tick_count]
        cmp eax, [edi + TCB_WAKEUP]
        jb .wake_next
        ; Wake this task
        mov dword [edi + TCB_STATE], TASK_READY
.wake_next:
        add edi, TCB_SIZE
        inc ecx
        jmp .wake_loop
.wake_done:
        ret

;---------------------------------------
; sched_task_alive - Check if a task slot is still active
; EAX = task ID (slot index)
; Returns: EAX = 1 if alive (READY/RUNNING/BLOCKED), 0 if FREE
;---------------------------------------
sched_task_alive:
        push rbx
        cmp eax, MAX_TASKS
        jge .sta_dead
        mov rbx, rax
        imul rbx, TCB_SIZE
        add rbx, task_table
        cmp dword [ebx + TCB_STATE], TASK_FREE
        je .sta_dead
        pop rbx
        mov eax, 1
        ret
.sta_dead:
        pop rbx
        xor eax, eax
        ret

;---------------------------------------
; fpu_init - Initialize x87 FPU and enable lazy context switching
; Called once during kernel boot after sched_init.
; Sets up CR0 bits: clear EM (no emulation), set MP+NE, set TS.
; Initializes the FPU with FNINIT.
;---------------------------------------
fpu_init:
        PUSHALL
        ; Configure CR0 for hardware FPU with lazy switching:
        ;   MP (bit 1) = 1 — Monitor coprocessor (WAIT traps if TS set)
        ;   EM (bit 2) = 0 — No FPU emulation (use real hardware)
        ;   TS (bit 3) = 1 — Task switched (triggers #NM on FPU use)
        ;   NE (bit 5) = 1 — Native FPU exceptions (use INT 16, not IRQ 13)
        mov rax, cr0
        and eax, ~(1 << 2)     ; Clear EM
        or  eax, (1 << 1) | (1 << 3) | (1 << 5)  ; Set MP, TS, NE
        mov cr0, rax
        ; Initialize the FPU
        clts                    ; Temporarily clear TS so FNINIT doesn't fault
        fninit
        ; Re-set TS bit
        mov rax, cr0
        or eax, (1 << 3)
        mov cr0, rax
        ; Zero out fpu_state_table
        mov edi, fpu_state_table
        xor eax, eax
        mov ecx, (MAX_TASKS * FPU_STATE_SIZE) / 4
        rep stosd
        POPALL
        ret

;---------------------------------------
; sys_signal - Install a signal handler for current task
;   EBX = signal number (1-4: SIGTERM, SIGKILL, SIGUSR1, SIGUSR2)
;   ECX = handler address (0 = default, ring-3 function pointer)
; Returns: EAX = previous handler (0 if none)
; SIGKILL (2) cannot be caught — always returns -1.
;---------------------------------------
sys_signal:
        ; Validate signal number
        cmp ebx, 1
        jb .sig_bad
        cmp ebx, SIG_MAX
        ja .sig_bad
        ; SIGKILL cannot be caught
        cmp ebx, SIGKILL
        je .sig_bad

        ; Find current task index
        push rcx
        push rbx
        mov edi, task_table
        xor edx, edx
.sig_find:
        cmp edx, MAX_TASKS
        jge .sig_no_task
        cmp dword [edi + TCB_STATE], TASK_RUNNING
        je .sig_found
        add edi, TCB_SIZE
        inc edx
        jmp .sig_find

.sig_found:
        pop rbx                 ; signal number
        pop rcx                 ; handler address

        ; Calculate handler table offset: sig_handlers[task * SIG_ENTRY_SIZE + (sig-1)*8]
        mov eax, edx
        imul rax, SIG_ENTRY_SIZE
        lea rdi, [sig_handlers + rax]
        dec ebx                 ; signals are 1-based → 0-based index
        ; Read old handler
        mov rax, [rdi + rbx * 8]
        ; Store new handler
        mov [rdi + rbx * 8], rcx
        ; Return old handler in EAX
        iretq

.sig_no_task:
        pop rbx
        pop rcx
.sig_bad:
        mov eax, -1
        iretq

;---------------------------------------
; sys_raise - Send a signal to a task by PID
;   EBX = target PID
;   ECX = signal number (1-4)
; Returns: EAX = 0 on success, -1 on failure
;---------------------------------------
sys_raise:
        ; Validate signal number
        cmp ecx, 1
        jb .raise_bad
        cmp ecx, SIG_MAX
        ja .raise_bad

        ; Find task with matching PID
        mov edi, task_table
        xor edx, edx
.raise_find:
        cmp edx, MAX_TASKS
        jge .raise_bad
        cmp dword [edi + TCB_STATE], TASK_FREE
        je .raise_next
        cmp [edi + TCB_PID], ebx
        je .raise_found
.raise_next:
        add edi, TCB_SIZE
        inc edx
        jmp .raise_find

.raise_found:
        ; SIGKILL: immediately kill the task (cannot be caught)
        cmp ecx, SIGKILL
        je .raise_kill

        ; Set pending bit for this signal
        ; sig_pending[task_index] |= (1 << (sig - 1))
        dec ecx                 ; 1-based → 0-based bit
        mov al, 1
        shl al, cl
        or [sig_pending + edx], al

        xor eax, eax           ; EAX = 0 = success
        iretq

.raise_kill:
        ; Force-kill the task (reuse sys_kill logic)
        ; Release FPU ownership if needed
        cmp [fpu_owner], edx
        jne .raise_kill_no_fpu
        mov dword [fpu_owner], -1
.raise_kill_no_fpu:
        ; Free kernel stack
        mov rax, [rdi + TCB_KSTACK]
        test rax, rax
        jz .raise_kill_clear
        sub rax, 4096
        call pmm_free_page
.raise_kill_clear:
        mov dword [edi + TCB_STATE], TASK_FREE
        mov qword [edi + TCB_RSP], 0
        mov qword [edi + TCB_KSTACK], 0
        dec dword [task_count]
        ; Clear signal state
        mov byte [sig_pending + edx], 0
        xor eax, eax
        iretq

.raise_bad:
        mov eax, -1
        iretq

;---------------------------------------
; sched_deliver_signals - Check and deliver pending signals for a task
; Called from irq_timer just before switching to a new task.
; Input: EBX = pointer to new task's TCB, ESI = new task index
;        RSP = new task's saved kernel RSP (PUSHALL+iretq frame)
; Modifies the IRETQ frame to redirect execution to signal handler.
;---------------------------------------
sched_deliver_signals:
        PUSHALL
        ; Check if this task has pending signals
        movzx eax, byte [sig_pending + esi]
        test eax, eax
        jz .sds_done

        ; Scan bits 0-3 for pending signals
        xor ecx, ecx           ; bit index (signal - 1)
.sds_scan:
        cmp ecx, SIG_MAX
        jge .sds_done
        bt eax, ecx
        jnc .sds_next_bit

        ; Signal (ecx+1) is pending — check for handler
        push rax
        push rcx
        mov eax, esi
        imul rax, SIG_ENTRY_SIZE
        lea rdi, [sig_handlers + rax]
        mov rdx, [rdi + rcx * 8]   ; handler address (qword)
        test rdx, rdx
        jz .sds_default_action

        ; Handler exists — redirect the task's execution
        ; The 64-bit PUSHALL+IRETQ frame layout (from TCB_RSP):
        ;   RSP+0:   R15 (PUSHALL)
        ;   ...
        ;   RSP+112: RAX  (FRAME_RAX)
        ;   RSP+120: RIP  (FRAME_RIP — ring-3 return address)
        ;   RSP+128: CS   (FRAME_CS)
        ;   RSP+136: RFLAGS (FRAME_RFLAGS)
        ;   RSP+144: RSP3 (FRAME_RSP3 — ring-3 stack pointer)
        ;   RSP+152: SS3  (FRAME_SS3)
        ;
        ; Read the outer stack pointer from the TCB.
        mov rax, [rbx + TCB_RSP]   ; Points to start of PUSHALL frame

        ; Push the original RIP onto the user stack so handler can `ret` to it
        mov rdi, [rax + FRAME_RSP3] ; RSP3 (user stack pointer)
        sub rdi, 8                   ; Make room for 8-byte return address
        ; Write original RIP to user stack
        mov r8, [rax + FRAME_RIP]    ; Original RIP
        mov [rdi], r8                ; [RSP3 - 8] = original RIP
        mov [rax + FRAME_RSP3], rdi  ; Update RSP3

        ; Set RIP to signal handler
        mov [rax + FRAME_RIP], rdx

        ; Set RAX in PUSHALL frame to signal number (ecx+1)
        lea edx, [ecx + 1]
        mov qword [rax + FRAME_RAX], rdx

        ; Clear this pending bit
        pop rcx
        pop rax
        btr eax, ecx
        mov [sig_pending + esi], al

        ; Only deliver one signal per scheduling tick
        jmp .sds_done

.sds_default_action:
        pop rcx
        pop rax
        ; Default action for uncaught signal:
        ;   SIGTERM (bit 0): kill the task
        ;   SIGUSR1/SIGUSR2 (bits 2-3): ignore
        cmp ecx, 0             ; SIGTERM = bit 0
        jne .sds_clear_bit

        ; Default SIGTERM: terminate the task
        ; Clear pending, mark as FREE
        btr eax, ecx
        mov [sig_pending + esi], al
        ; Release FPU
        cmp [fpu_owner], esi
        jne .sds_term_no_fpu
        mov dword [fpu_owner], -1
.sds_term_no_fpu:
        mov dword [ebx + TCB_STATE], TASK_FREE
        mov qword [ebx + TCB_RSP], 0
        push rax
        mov rax, [rbx + TCB_KSTACK]
        test rax, rax
        jz .sds_term_no_stack
        sub rax, 4096
        call pmm_free_page
.sds_term_no_stack:
        pop rax
        dec dword [task_count]
        jmp .sds_done

.sds_clear_bit:
        ; Ignore SIGUSR signals with no handler
        btr eax, ecx
        mov [sig_pending + esi], al

.sds_next_bit:
        inc ecx
        jmp .sds_scan

.sds_done:
        POPALL
        ret
