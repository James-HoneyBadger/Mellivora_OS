;=======================================================================
; sched.inc - Preemptive task scheduler
;
; Provides TCB management, priority-based scheduling, SYS_YIELD, and
; preemptive time-slicing via the PIT timer (100 Hz).
;
; The PIT IRQ handler (irq_timer in isr.inc) counts ticks per task.
; When the quantum expires, the current ring-3 task is preempted
; and the highest-priority READY task runs.  Programs can also call
; SYS_YIELD for cooperative yielding.
;
; v4.0: Priority-based scheduling (4 levels), 64 tasks max,
;       task names, signal delivery, process groups.
;=======================================================================

; Task states
TASK_FREE    equ 0
TASK_READY   equ 1
TASK_RUNNING equ 2
TASK_BLOCKED equ 3          ; Blocked (sleeping / waiting on I/O)
TASK_STOPPED equ 4          ; v4.0: Stopped by signal (SIGTSTP / Ctrl+Z)

; Priority levels (v4.0)
PRIO_HIGH    equ 0           ; System tasks, real-time — 4x quantum
PRIO_NORMAL  equ 1           ; Default for user programs — 2x quantum
PRIO_LOW     equ 2           ; Background tasks — 1x quantum
PRIO_IDLE    equ 3           ; Idle tasks — only run when nothing else ready

; Preemptive scheduling quantum (in PIT ticks, 100 Hz)
; Base quantum = 5 ticks (50 ms); scaled by priority
SCHED_QUANTUM_BASE equ 5
SCHED_QUANTUM equ 10          ; Kept for backward compat (NORMAL = 2x base)

; Signal numbers (v4.0)
SIGINT       equ 2            ; Interrupt (Ctrl+C)
SIGKILL      equ 9            ; Kill (non-catchable)
SIGUSR1      equ 10           ; User-defined 1
SIGUSR2      equ 12           ; User-defined 2
SIGALRM      equ 14           ; Timer alarm
SIGTERM      equ 15           ; Termination request
SIGCHLD      equ 17           ; Child process terminated
SIGTSTP      equ 20           ; Terminal stop (Ctrl+Z)
SIGCONT      equ 25           ; Continue stopped process
NUM_SIGNALS  equ 32           ; Total signal bits

; TCB field offsets (64 bytes per entry — v4.0 expanded)
TCB_ESP      equ 0          ; Saved kernel-mode ESP (pushad+iretd frame)
TCB_STATE    equ 4          ; Task state (FREE / READY / RUNNING / BLOCKED / STOPPED)
TCB_KSTACK   equ 8          ; Kernel stack top (for TSS ESP0)
TCB_USTACK   equ 12         ; User-mode stack top
TCB_ENTRY    equ 16         ; Program entry point
TCB_PID      equ 20         ; Task ID
TCB_PRIORITY equ 24         ; Priority level (PRIO_HIGH..PRIO_IDLE)
TCB_WAKEUP   equ 28         ; Wakeup tick count (for sys_sleep)
TCB_NAME     equ 32         ; Task name (16 bytes, null-terminated)
TCB_SIG_PEND equ 48         ; Pending signal bitmask (32 bits)
TCB_SIG_MASK equ 52         ; Signal mask (blocked signals, 32 bits)
TCB_PGID     equ 56         ; Process group ID (v4.0)
TCB_EXIT_CODE equ 60        ; Exit code (v4.0)
TCB_SIZE     equ 64

MAX_TASKS    equ 64          ; v4.0: Doubled from 32

;---------------------------------------
; sched_init - Initialize the scheduler
; Clears the task table. No tasks are
; created — the shell is not a task.
;---------------------------------------
sched_init:
        pushad
        ; Zero out the task table
        mov edi, task_table
        xor eax, eax
        mov ecx, (MAX_TASKS * TCB_SIZE) / 4
        rep stosd
        mov dword [task_count], 0
        mov dword [task_current], 0
        mov dword [sched_tick], 0
        popad
        ret

;---------------------------------------
; sched_create_task - Register a new task
;  EBX = program entry point (ring 3 EIP)
;  ECX = user-mode stack top (ring 3 ESP)
; Returns:
;  EAX = task ID (0..MAX_TASKS-1), or -1 on failure
;---------------------------------------
sched_create_task:
        pushad
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
        push edx
        push edi
        push ebx
        push ecx
        call pmm_alloc_page         ; EAX = physical page address (or 0)
        test eax, eax
        jz .create_fail_pop

        ; Kernel stack top = page + 4096
        mov esi, eax
        add esi, 4096               ; ESI = kstack_top

        pop ecx                     ; user stack
        pop ebx                     ; entry point
        pop edi                     ; TCB pointer
        pop edx                     ; slot index

        ; Store kernel stack page top
        mov [edi + TCB_KSTACK], esi

        ; Build an initial switch frame on the kernel stack
        ; Layout (52 bytes, pushad frame + iretd frame):
        ;   [kstack_top - 4]  = SS3       (USER_DS)
        ;   [kstack_top - 8]  = ESP3      (user stack)
        ;   [kstack_top - 12] = EFLAGS    (0x200 = IF set)
        ;   [kstack_top - 16] = CS3       (USER_CS)
        ;   [kstack_top - 20] = EIP       (entry point)
        ;   [kstack_top - 24] = EAX       (0)
        ;   [kstack_top - 28] = ECX       (0)
        ;   [kstack_top - 32] = EDX       (0)
        ;   [kstack_top - 36] = EBX       (0)
        ;   [kstack_top - 40] = ESP_orig  (ignored by popad)
        ;   [kstack_top - 44] = EBP       (0)
        ;   [kstack_top - 48] = ESI       (0)
        ;   [kstack_top - 52] = EDI       (0)

        ; IRETD frame (top of stack, pushed last by CPU)
        mov dword [esi - 4],  USER_DS       ; SS3
        mov dword [esi - 8],  ecx           ; ESP3
        mov dword [esi - 12], 0x200         ; EFLAGS (IF=1)
        mov dword [esi - 16], USER_CS       ; CS3
        mov dword [esi - 20], ebx           ; EIP = entry point

        ; pushad frame (general-purpose registers, all zero)
        xor eax, eax
        mov dword [esi - 24], eax           ; EAX
        mov dword [esi - 28], eax           ; ECX
        mov dword [esi - 32], eax           ; EDX
        mov dword [esi - 36], eax           ; EBX
        mov dword [esi - 40], eax           ; ESP_orig (ignored)
        mov dword [esi - 44], eax           ; EBP
        mov dword [esi - 48], eax           ; ESI
        mov dword [esi - 52], eax           ; EDI

        ; Saved ESP = kstack_top - 52
        sub esi, 52
        mov [edi + TCB_ESP], esi

        ; Fill remaining TCB fields
        mov [edi + TCB_STATE], dword TASK_READY
        mov [edi + TCB_USTACK], ecx
        mov [edi + TCB_ENTRY], ebx
        mov [edi + TCB_PID], edx
        mov [edi + TCB_PRIORITY], dword PRIO_NORMAL
        mov [edi + TCB_SIG_PEND], dword 0
        mov [edi + TCB_SIG_MASK], dword 0
        mov [edi + TCB_PGID], edx        ; Default PGID = own PID
        mov [edi + TCB_EXIT_CODE], dword 0

        ; Copy task name from task_create_name (set by caller, or default)
        push edi
        push esi
        lea edi, [edi + TCB_NAME]
        mov esi, task_create_name
        mov ecx, 15             ; Max 15 chars + null
.copy_name:
        lodsb
        stosb
        cmp al, 0
        je .name_done
        dec ecx
        jnz .copy_name
        mov byte [edi], 0       ; Force null-terminate
.name_done:
        pop esi
        pop edi

        ; Restore task count increment with correct ECX
        mov ecx, [edi + TCB_USTACK]  ; Restore ECX clobbered by name copy

        inc dword [task_count]

        ; Return task ID in EAX (via pushad slot)
        mov [esp + 28], edx             ; Patch EAX in pushad frame
        popad
        ret

.create_fail_pop:
        pop ecx
        pop ebx
        pop edi
        pop edx
.create_fail:
        mov dword [esp + 28], -1        ; Return -1
        popad
        ret

;---------------------------------------
; sched_exit_task - Terminate current task
; Called when a task's program exits.
; Frees the kernel stack page and TCB.
;---------------------------------------
sched_exit_task:
        pushad
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
        ; Free the kernel stack page (kstack_top - 4096 = page base)
        mov eax, [edi + TCB_KSTACK]
        sub eax, 4096
        call pmm_free_page

        ; Clear the TCB
        mov dword [edi + TCB_STATE], TASK_FREE
        mov dword [edi + TCB_ESP], 0
        mov dword [edi + TCB_KSTACK], 0

        dec dword [task_count]

.exit_done:
        popad
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
        pushad

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
        ; Save current kernel ESP
        mov [edi + TCB_ESP], esp
        ; Mark as READY
        mov dword [edi + TCB_STATE], TASK_READY

        ; v4.0: Priority-based scheduling with round-robin within same level
        ; Find highest-priority (lowest number) READY task, preferring
        ; tasks after current index for fairness within same priority.
        mov dword [.best_slot], -1
        mov dword [.best_prio], PRIO_IDLE + 1  ; Worse than worst
        mov ecx, MAX_TASKS
        mov esi, edx                ; start from current slot + 1
.yld_prio_scan:
        inc esi
        cmp esi, MAX_TASKS
        jb .yld_prio_check
        xor esi, esi                ; wrap to 0
.yld_prio_check:
        cmp esi, edx               ; back to starting point?
        je .yld_prio_done
        ; Calculate TCB address
        mov eax, esi
        imul eax, TCB_SIZE
        lea ebx, [task_table + eax]
        cmp dword [ebx + TCB_STATE], TASK_READY
        jne .yld_prio_next
        ; Compare priority: lower number = higher priority
        mov eax, [ebx + TCB_PRIORITY]
        cmp eax, [.best_prio]
        jge .yld_prio_next         ; Not better — skip (>= keeps round-robin fairness)
        mov [.best_prio], eax
        mov [.best_slot], esi
.yld_prio_next:
        dec ecx
        jnz .yld_prio_scan

.yld_prio_done:
        ; Check if we found a candidate
        mov esi, [.best_slot]
        cmp esi, -1
        je .yld_no_switch

        ; Calculate TCB for best candidate
        mov eax, esi
        imul eax, TCB_SIZE
        lea ebx, [task_table + eax]
        jmp .yld_switch

.yld_no_switch:
        ; No other ready task found — re-mark self as RUNNING
        mov dword [edi + TCB_STATE], TASK_RUNNING
        jmp .yld_abort

; Local temporaries (in .data-ish section within code)
.best_slot: dd 0
.best_prio: dd 0

.yld_switch:
        ; EBX = pointer to new task's TCB, ESI = new task index
        mov dword [ebx + TCB_STATE], TASK_RUNNING
        mov [task_current], esi

        ; Update TSS ESP0 so future ring-3→0 transitions use the new kernel stack
        mov eax, [ebx + TCB_KSTACK]
        mov [tss_struct + 4], eax

        ; Load the new task's kernel ESP (with its pushad+iretd frame)
        mov esp, [ebx + TCB_ESP]

        ; Restore GP regs and return to the new task's ring-3 context
        popad
        iretd

.yld_abort:
        ; Couldn't switch — restore current task state and return
        popad
.yield_nop:
        iretd

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
        iretd
.gp_shell:
        mov eax, -1
        iretd

;---------------------------------------
; sys_kill - Terminate task by PID
;   EBX = PID to kill
; Returns EAX = 0 on success, -1 on fail
;---------------------------------------
sys_kill:
        pushad
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
        ; Free the kernel stack page
        mov eax, [edi + TCB_KSTACK]
        test eax, eax
        jz .sk_clear
        sub eax, 4096
        call pmm_free_page

.sk_clear:
        ; Clear the TCB
        mov dword [edi + TCB_STATE], TASK_FREE
        mov dword [edi + TCB_ESP], 0
        mov dword [edi + TCB_KSTACK], 0
        dec dword [task_count]

        mov dword [esp + 28], 0    ; Return EAX=0
        popad
        iretd

.sk_fail:
        mov dword [esp + 28], -1   ; Return EAX=-1
        popad
        iretd

;---------------------------------------
; sys_proclist - Get task info for a scheduler slot
; EBX = slot index (0..MAX_TASKS-1)
; ECX = pointer to 48-byte output buffer (v4.0 expanded)
; Output buffer:
;   [state(4), pid(4), entry(4), esp(4),
;    priority(4), pgid(4), sig_pending(4), exit_code(4),
;    name(16)]
; Returns: EAX=0 on success, -1 if invalid slot
;---------------------------------------
sys_proclist:
        pushad
        cmp ebx, MAX_TASKS
        jge .spl_fail
        ; Calculate TCB address
        mov edi, ebx
        imul edi, TCB_SIZE
        add edi, task_table
        ; Fill output buffer
        mov eax, [edi + TCB_STATE]
        mov [ecx], eax
        mov eax, [edi + TCB_PID]
        mov [ecx + 4], eax
        mov eax, [edi + TCB_ENTRY]
        mov [ecx + 8], eax
        mov eax, [edi + TCB_ESP]
        mov [ecx + 12], eax
        ; v4.0 extended fields
        mov eax, [edi + TCB_PRIORITY]
        mov [ecx + 16], eax
        mov eax, [edi + TCB_PGID]
        mov [ecx + 20], eax
        mov eax, [edi + TCB_SIG_PEND]
        mov [ecx + 24], eax
        mov eax, [edi + TCB_EXIT_CODE]
        mov [ecx + 28], eax
        ; Copy 16-byte name
        push esi
        lea esi, [edi + TCB_NAME]
        lea edi, [ecx + 32]
        mov eax, [esi]
        mov [edi], eax
        mov eax, [esi + 4]
        mov [edi + 4], eax
        mov eax, [esi + 8]
        mov [edi + 8], eax
        mov eax, [esi + 12]
        mov [edi + 12], eax
        pop esi
        mov dword [esp + 28], 0
        popad
        iretd
.spl_fail:
        mov dword [esp + 28], -1
        popad
        iretd

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

        pushad

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

        ; Mark as BLOCKED and save ESP
        mov dword [edi + TCB_STATE], TASK_BLOCKED
        mov [edi + TCB_ESP], esp

        ; Try to find another READY task to run
        mov ecx, MAX_TASKS
        mov esi, edx
.sleep_scan:
        inc esi
        cmp esi, MAX_TASKS
        jb .sleep_check
        xor esi, esi
.sleep_check:
        mov eax, esi
        imul eax, TCB_SIZE
        lea ebx, [task_table + eax]
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
        mov eax, [ebx + TCB_KSTACK]
        mov [tss_struct + 4], eax
        mov esp, [ebx + TCB_ESP]
        popad
        iretd

.sleep_abort:
        popad
        ; HLT fallback: shell/single-program context, no tasks to switch to
.sleep_hlt_fallback:
        ; EBX = ticks to sleep; sleep via HLT loop (timer fires every 10 ms)
        test ebx, ebx
        jz .sleep_hlt_done
        push ecx
        mov ecx, [tick_count]
        add ecx, ebx            ; wakeup tick = now + EBX
.sleep_hlt_loop:
        hlt
        cmp [tick_count], ecx
        jb .sleep_hlt_loop
        pop ecx
.sleep_hlt_done:
        xor eax, eax
        iretd

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

;=======================================================================
; v4.0 NEW SYSCALLS - Priority, Signals, Process Groups
;=======================================================================

;---------------------------------------
; sys_setpriority - Set task priority
;   EBX = PID (or 0 for self)
;   ECX = new priority (PRIO_HIGH..PRIO_IDLE)
; Returns: EAX = 0 on success, -1 on fail
;---------------------------------------
sys_setpriority:
        pushad
        ; Validate priority
        cmp ecx, PRIO_IDLE
        ja .spri_fail
        ; Find task by PID (0 = self = RUNNING task)
        test ebx, ebx
        jnz .spri_by_pid
        ; Find RUNNING task
        mov edi, task_table
        xor edx, edx
.spri_find_run:
        cmp edx, MAX_TASKS
        jge .spri_fail
        cmp dword [edi + TCB_STATE], TASK_RUNNING
        je .spri_set
        add edi, TCB_SIZE
        inc edx
        jmp .spri_find_run
.spri_by_pid:
        mov edi, task_table
        xor edx, edx
.spri_find_pid:
        cmp edx, MAX_TASKS
        jge .spri_fail
        cmp dword [edi + TCB_STATE], TASK_FREE
        je .spri_skip
        cmp [edi + TCB_PID], ebx
        je .spri_set
.spri_skip:
        add edi, TCB_SIZE
        inc edx
        jmp .spri_find_pid
.spri_set:
        mov [edi + TCB_PRIORITY], ecx
        mov dword [esp + 28], 0
        popad
        iretd
.spri_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;---------------------------------------
; sys_getpriority - Get task priority
;   EBX = PID (or 0 for self)
; Returns: EAX = priority level, or -1
;---------------------------------------
sys_getpriority:
        pushad
        test ebx, ebx
        jnz .gpri_by_pid
        ; Find RUNNING task
        mov edi, task_table
        xor edx, edx
.gpri_find_run:
        cmp edx, MAX_TASKS
        jge .gpri_fail
        cmp dword [edi + TCB_STATE], TASK_RUNNING
        je .gpri_get
        add edi, TCB_SIZE
        inc edx
        jmp .gpri_find_run
.gpri_by_pid:
        mov edi, task_table
        xor edx, edx
.gpri_find_pid:
        cmp edx, MAX_TASKS
        jge .gpri_fail
        cmp dword [edi + TCB_STATE], TASK_FREE
        je .gpri_skip
        cmp [edi + TCB_PID], ebx
        je .gpri_get
.gpri_skip:
        add edi, TCB_SIZE
        inc edx
        jmp .gpri_find_pid
.gpri_get:
        mov eax, [edi + TCB_PRIORITY]
        mov [esp + 28], eax
        popad
        iretd
.gpri_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;---------------------------------------
; sys_signal - Send signal to a task
;   EBX = target PID
;   ECX = signal number (0..31)
; Returns: EAX = 0 on success, -1 on fail
;---------------------------------------
sys_signal:
        pushad
        ; Validate signal number
        cmp ecx, NUM_SIGNALS
        jge .sig_fail
        ; Find task by PID
        mov edi, task_table
        xor edx, edx
.sig_find:
        cmp edx, MAX_TASKS
        jge .sig_fail
        cmp dword [edi + TCB_STATE], TASK_FREE
        je .sig_next
        cmp [edi + TCB_PID], ebx
        je .sig_found
.sig_next:
        add edi, TCB_SIZE
        inc edx
        jmp .sig_find
.sig_found:
        ; SIGKILL is always delivered — forcibly terminate
        cmp ecx, SIGKILL
        je .sig_kill
        ; SIGCONT wakes stopped tasks
        cmp ecx, SIGCONT
        je .sig_cont
        ; SIGTSTP stops the task
        cmp ecx, SIGTSTP
        je .sig_stop
        ; Check signal mask
        mov eax, 1
        shl eax, cl              ; bit for this signal
        test [edi + TCB_SIG_MASK], eax
        jnz .sig_masked          ; Masked — don't deliver
        ; Set pending bit
        or [edi + TCB_SIG_PEND], eax
        ; For SIGINT/SIGTERM on running/ready tasks: terminate
        cmp ecx, SIGINT
        je .sig_terminate
        cmp ecx, SIGTERM
        je .sig_terminate
        ; Other signals: just set pending bit (checked at task switch)
        mov dword [esp + 28], 0
        popad
        iretd
.sig_terminate:
        ; Terminate the task (same as kill)
        mov eax, [edi + TCB_KSTACK]
        test eax, eax
        jz .sig_clear
        sub eax, 4096
        call pmm_free_page
.sig_clear:
        mov dword [edi + TCB_STATE], TASK_FREE
        mov dword [edi + TCB_ESP], 0
        mov dword [edi + TCB_KSTACK], 0
        dec dword [task_count]
        mov dword [esp + 28], 0
        popad
        iretd
.sig_kill:
        ; Forcible kill — free kernel stack, clear TCB
        mov eax, [edi + TCB_KSTACK]
        test eax, eax
        jz .sigk_clear
        sub eax, 4096
        call pmm_free_page
.sigk_clear:
        mov dword [edi + TCB_STATE], TASK_FREE
        mov dword [edi + TCB_ESP], 0
        mov dword [edi + TCB_KSTACK], 0
        dec dword [task_count]
        mov dword [esp + 28], 0
        popad
        iretd
.sig_stop:
        ; Stop the task (Ctrl+Z)
        cmp dword [edi + TCB_STATE], TASK_FREE
        je .sig_fail
        mov dword [edi + TCB_STATE], TASK_STOPPED
        mov dword [esp + 28], 0
        popad
        iretd
.sig_cont:
        ; Continue a stopped task
        cmp dword [edi + TCB_STATE], TASK_STOPPED
        jne .sig_ok              ; Not stopped, just succeed
        mov dword [edi + TCB_STATE], TASK_READY
.sig_ok:
        mov dword [esp + 28], 0
        popad
        iretd
.sig_masked:
        ; Signal is masked — set pending but don't deliver
        mov eax, 1
        shl eax, cl
        or [edi + TCB_SIG_PEND], eax
        mov dword [esp + 28], 0
        popad
        iretd
.sig_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;---------------------------------------
; sys_setpgid - Set process group ID
;   EBX = PID (0 = self)
;   ECX = new PGID (0 = use own PID)
; Returns: EAX = 0 on success, -1 fail
;---------------------------------------
sys_setpgid:
        pushad
        test ebx, ebx
        jnz .spg_by_pid
        ; Self
        mov edi, task_table
        xor edx, edx
.spg_find_run:
        cmp edx, MAX_TASKS
        jge .spg_fail
        cmp dword [edi + TCB_STATE], TASK_RUNNING
        je .spg_found
        add edi, TCB_SIZE
        inc edx
        jmp .spg_find_run
.spg_by_pid:
        mov edi, task_table
        xor edx, edx
.spg_find:
        cmp edx, MAX_TASKS
        jge .spg_fail
        cmp dword [edi + TCB_STATE], TASK_FREE
        je .spg_skip
        cmp [edi + TCB_PID], ebx
        je .spg_found
.spg_skip:
        add edi, TCB_SIZE
        inc edx
        jmp .spg_find
.spg_found:
        ; ECX=0 means use task's own PID
        test ecx, ecx
        jnz .spg_set
        mov ecx, [edi + TCB_PID]
.spg_set:
        mov [edi + TCB_PGID], ecx
        mov dword [esp + 28], 0
        popad
        iretd
.spg_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;---------------------------------------
; sys_getpgid - Get process group ID
;   EBX = PID (0 = self)
; Returns: EAX = PGID, or -1
;---------------------------------------
sys_getpgid:
        pushad
        test ebx, ebx
        jnz .ggp_by_pid
        mov edi, task_table
        xor edx, edx
.ggp_find_run:
        cmp edx, MAX_TASKS
        jge .ggp_fail
        cmp dword [edi + TCB_STATE], TASK_RUNNING
        je .ggp_get
        add edi, TCB_SIZE
        inc edx
        jmp .ggp_find_run
.ggp_by_pid:
        mov edi, task_table
        xor edx, edx
.ggp_find:
        cmp edx, MAX_TASKS
        jge .ggp_fail
        cmp dword [edi + TCB_STATE], TASK_FREE
        je .ggp_skip
        cmp [edi + TCB_PID], ebx
        je .ggp_get
.ggp_skip:
        add edi, TCB_SIZE
        inc edx
        jmp .ggp_find
.ggp_get:
        mov eax, [edi + TCB_PGID]
        mov [esp + 28], eax
        popad
        iretd
.ggp_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;---------------------------------------
; sys_sigmask - Set/get signal mask
;   EBX = 0: get mask, 1: set mask, 2: block signals, 3: unblock signals
;   ECX = new mask (for set/block/unblock)
; Returns: EAX = previous mask, or -1
;---------------------------------------
sys_sigmask:
        pushad
        ; Find RUNNING task
        mov edi, task_table
        xor edx, edx
.smk_find:
        cmp edx, MAX_TASKS
        jge .smk_fail
        cmp dword [edi + TCB_STATE], TASK_RUNNING
        je .smk_found
        add edi, TCB_SIZE
        inc edx
        jmp .smk_find
.smk_found:
        mov eax, [edi + TCB_SIG_MASK]  ; Old mask
        cmp ebx, 0
        je .smk_ret              ; Just get
        cmp ebx, 1
        je .smk_set
        cmp ebx, 2
        je .smk_block
        cmp ebx, 3
        je .smk_unblock
        jmp .smk_fail
.smk_set:
        mov [edi + TCB_SIG_MASK], ecx
        jmp .smk_ret
.smk_block:
        or [edi + TCB_SIG_MASK], ecx
        jmp .smk_ret
.smk_unblock:
        not ecx
        and [edi + TCB_SIG_MASK], ecx
.smk_ret:
        mov [esp + 28], eax
        popad
        iretd
.smk_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;---------------------------------------
; sys_taskname - Set name of current task
;   EBX = pointer to name string (max 15 chars)
; Returns: EAX = 0
;---------------------------------------
sys_taskname:
        pushad
        ; Find RUNNING task
        mov edi, task_table
        xor edx, edx
.stn_find:
        cmp edx, MAX_TASKS
        jge .stn_done
        cmp dword [edi + TCB_STATE], TASK_RUNNING
        je .stn_set
        add edi, TCB_SIZE
        inc edx
        jmp .stn_find
.stn_set:
        push edi
        lea edi, [edi + TCB_NAME]
        mov esi, ebx
        mov ecx, 15
.stn_copy:
        lodsb
        stosb
        cmp al, 0
        je .stn_pad
        dec ecx
        jnz .stn_copy
        mov byte [edi], 0
.stn_pad:
        pop edi
.stn_done:
        mov dword [esp + 28], 0
        popad
        iretd
