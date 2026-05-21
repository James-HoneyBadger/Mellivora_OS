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
; v4.0: Priority-based scheduling (4 levels),
;       task names, signal delivery, process groups.
; v5.0: Max task count raised to 128.
;=======================================================================

; Task states
TASK_FREE    equ 0
TASK_READY   equ 1
TASK_RUNNING equ 2
TASK_BLOCKED equ 3          ; Blocked (sleeping / waiting on I/O)
TASK_STOPPED equ 4          ; v4.0: Stopped by signal (SIGTSTP / Ctrl+Z)
TASK_ZOMBIE  equ 5          ; v5.0: Exited, exit code not yet reaped

; Priority levels (v4.0)
PRIO_HIGH    equ 0           ; System tasks, real-time — 4x quantum
PRIO_NORMAL  equ 1           ; Default for user programs — 2x quantum
PRIO_LOW     equ 2           ; Background tasks — 1x quantum
PRIO_IDLE    equ 3           ; Idle tasks — only run when nothing else ready

; Preemptive scheduling quantum (in PIT ticks, 100 Hz)
; Base quantum = 5 ticks (50 ms); scaled by priority
SCHED_QUANTUM_BASE equ 5
SCHED_QUANTUM equ 10          ; Kept for backward compat (NORMAL = 2x base)

; Signal disposition sentinels (v11.0)
SIG_DFL      equ 0            ; Default action for a signal
SIG_IGN      equ 1            ; Ignore this signal

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
TCB_BRK      equ 64         ; Program break address (for sbrk)
TCB_PPID     equ 68         ; v9.0: Parent PID (set on fork; 0 = shell)
TCB_UID      equ 72         ; v10: Real user ID (0 = root)
TCB_GID      equ 76         ; v10: Real group ID (0 = root)
TCB_ALARM    equ 80         ; v10: Alarm deadline (PIT tick; 0 = no alarm)
TCB_SIG_HAND equ 84         ; v11: Pointer to 32-dword signal handler table (0 = not allocated)
; --- v12.0 POSIX extensions ---
TCB_EUID     equ 88         ; v12: Effective user ID
TCB_EGID     equ 92         ; v12: Effective group ID
TCB_SID      equ 96         ; v12: Session ID (0 = unset)
TCB_UMASK    equ 100        ; v12: File creation mask (default 0022)
TCB_ITIMER_VAL  equ 104     ; v12: ITIMER_REAL countdown (ticks; 0 = disarmed)
TCB_ITIMER_INT  equ 108     ; v12: ITIMER_REAL reload interval (ticks; 0 = one-shot)
TCB_SAVED_MASK  equ 112     ; v12: Signal mask saved for sigsuspend restore
TCB_WAIT_PID    equ 116     ; v12: PID being waited on (0 = not waiting); for sys_waitpid block
TCB_FD_TABLE_PTR equ 120    ; v12: Physical/virtual address of per-task fd table page (0 = not alloc)
TCB_ERRNO       equ 124     ; v12: Per-task errno value
TCB_SIZE        equ 128     ; Bytes per TCB

MAX_TASKS    equ 128         ; v5.0: Doubled from 64

;-----------------------------------------------------------------------
; sched_swap_fd_tables  — save outgoing task's fd view, load incoming's
;
;  Call at every context switch BEFORE updating esp.
;  EDI = outgoing TCB ptr  (0 = outgoing is the shell)
;  EBX = incoming TCB ptr  (0 = incoming is the shell)
;
;  Preserves ALL registers.
;-----------------------------------------------------------------------
sched_swap_fd_tables:
        pushad

        ; --- Save global fd_table into outgoing task's private copy ---
        cmp edi, 0
        je .sft_save_shell          ; outgoing is the shell

        mov eax, [edi + TCB_FD_TABLE_PTR]
        test eax, eax
        jz .sft_load                ; no private page — skip save
        ; copy global fd_table (FD_MAX*FD_ENTRY_SIZE = 256 bytes = 64 dwords) → private page
        push edi
        mov edi, eax                ; dest = private page
        mov esi, fd_table           ; src  = global fd_table
        mov ecx, (FD_MAX * FD_ENTRY_SIZE) / 4
        rep movsd
        pop edi
        jmp .sft_load

.sft_save_shell:
        ; outgoing is shell — save to shell_fd_table
        push edi
        mov edi, shell_fd_table
        mov esi, fd_table
        mov ecx, (FD_MAX * FD_ENTRY_SIZE) / 4
        rep movsd
        pop edi

.sft_load:
        ; --- Load incoming task's private copy into global fd_table ---
        cmp ebx, 0
        je .sft_load_shell          ; incoming is the shell

        mov eax, [ebx + TCB_FD_TABLE_PTR]
        test eax, eax
        jz .sft_done                ; no private page — leave global as-is
        ; copy private page → global fd_table
        mov esi, eax
        mov edi, fd_table
        mov ecx, (FD_MAX * FD_ENTRY_SIZE) / 4
        rep movsd
        jmp .sft_done

.sft_load_shell:
        mov esi, shell_fd_table
        mov edi, fd_table
        mov ecx, (FD_MAX * FD_ENTRY_SIZE) / 4
        rep movsd

.sft_done:
        popad
        ret

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
        mov [edi + TCB_BRK], dword PROGRAM_END_ADDR ; Initial break = end of static data
        mov [edi + TCB_SIG_HAND], dword 0           ; v11: no handler table yet
        ; v12 POSIX fields
        mov eax, [edi + TCB_UID]
        mov [edi + TCB_EUID], eax          ; EUID = real UID at creation
        mov eax, [edi + TCB_GID]
        mov [edi + TCB_EGID], eax          ; EGID = real GID at creation
        mov [edi + TCB_SID], edx           ; SID = own PID (new session leader)
        mov [edi + TCB_UMASK], dword 0022h ; default umask 022
        mov [edi + TCB_ITIMER_VAL],  dword 0
        mov [edi + TCB_ITIMER_INT],  dword 0
        mov [edi + TCB_SAVED_MASK],  dword 0
        mov [edi + TCB_WAIT_PID],    dword 0
        mov [edi + TCB_ERRNO],       dword 0

        ; Allocate a private fd table page (4KB, zero-filled)
        push edx
        push edi
        call pmm_alloc_page         ; EAX = page address or 0
        pop edi
        pop edx
        test eax, eax
        jz .no_fd_page
        ; Zero the page (4096 bytes = 1024 dwords)
        push eax
        push edi
        mov edi, eax
        xor eax, eax
        mov ecx, 1024
        rep stosd
        pop edi
        pop eax
        mov [edi + TCB_FD_TABLE_PTR], eax
        jmp .fd_page_done
.no_fd_page:
        mov [edi + TCB_FD_TABLE_PTR], dword 0
.fd_page_done:

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

        ; Free the private fd table page if allocated
        mov eax, [edi + TCB_FD_TABLE_PTR]
        test eax, eax
        jz .exit_no_fd_page
        call pmm_free_page
        mov [edi + TCB_FD_TABLE_PTR], dword 0
.exit_no_fd_page:

        ; Get PID of the dying task for parent wake-up
        mov esi, [edi + TCB_PID]

        ; Clear execution fields but leave PID and exit code for waitpid
        ; v5.0: TASK_ZOMBIE lets waitpid collect the exit code before reaping
        mov dword [edi + TCB_STATE], TASK_ZOMBIE
        mov dword [edi + TCB_ESP], 0
        mov dword [edi + TCB_KSTACK], 0
        ; v12.2: Store real exit code (saved in bg_exit_code by sys_exit before EBX was clobbered)
        mov eax, [bg_exit_code]
        mov [edi + TCB_EXIT_CODE], eax

        dec dword [task_count]

        ; Wake any task blocked in sys_waitpid waiting for this PID
        push edi
        push esi
        mov edi, task_table
        xor edx, edx
.exit_wake_scan:
        cmp edx, MAX_TASKS
        jge .exit_wake_done
        cmp dword [edi + TCB_STATE], TASK_BLOCKED
        jne .exit_wake_next
        mov eax, [edi + TCB_WAIT_PID]
        ; waiting for specific PID?
        cmp eax, esi
        je .exit_wake_found
        ; waiting for any child (-1)?
        cmp eax, -1
        jne .exit_wake_next
.exit_wake_found:
        mov dword [edi + TCB_WAIT_PID], 0
        mov dword [edi + TCB_STATE], TASK_READY
        ; v12.2: Write real exit code into the waiter's EAX slot in its pushad frame
        ; [TCB_ESP+28] = EAX slot
        mov ebx, [edi + TCB_ESP]
        mov eax, [bg_exit_code]
        mov [ebx + 28], eax
.exit_wake_next:
        add edi, TCB_SIZE
        inc edx
        jmp .exit_wake_scan
.exit_wake_done:
        pop esi
        pop edi

        ; --- v12.2: Deliver SIGCHLD to parent ---
        ; edi = dying task TCB, esi = dying task PID
        mov eax, [edi + TCB_PPID]       ; parent PID (0 = no real parent / shell)
        test eax, eax
        jz .exit_done                   ; PPID=0 → shell-launched, skip SIGCHLD

        push edi                        ; save dying TCB
        push eax                        ; save parent PID on stack
        mov edi, task_table
        xor edx, edx
.exit_sigchld_scan:
        cmp edx, MAX_TASKS
        jge .exit_sigchld_done
        cmp dword [edi + TCB_STATE], TASK_FREE
        je .exit_sigchld_next
        mov ebx, [esp]                  ; parent PID
        cmp [edi + TCB_PID], ebx
        je .exit_sigchld_found
.exit_sigchld_next:
        add edi, TCB_SIZE
        inc edx
        jmp .exit_sigchld_scan
.exit_sigchld_found:
        ; Check whether parent has explicitly set SIGCHLD to SIG_IGN
        mov ebx, [edi + TCB_SIG_HAND]
        test ebx, ebx
        jz .exit_sigchld_deliver        ; no handler table → SIG_DFL → deliver
        cmp dword [ebx + SIGCHLD * 4], SIG_IGN
        je .exit_sigchld_done           ; explicitly ignored → skip
.exit_sigchld_deliver:
        or dword [edi + TCB_SIG_PEND], (1 << SIGCHLD)
        ; Wake parent if blocked in pause/sigsuspend (NOT in waitpid — that's handled above)
        cmp dword [edi + TCB_STATE], TASK_BLOCKED
        jne .exit_sigchld_done
        cmp dword [edi + TCB_WAIT_PID], 0
        jne .exit_sigchld_done          ; blocked in waitpid — waitpid scan handles it
        mov dword [edi + TCB_STATE], TASK_READY
.exit_sigchld_done:
        pop eax                         ; discard saved parent PID
        pop edi                         ; restore dying TCB pointer
        ; --- end SIGCHLD ---

.exit_done:
        popad
        ret

;---------------------------------------
; sys_waitpid - Wait for a task to exit
; EBX = PID to wait for (-1 = any task)
; Returns: EAX = exit code (0 if unknown/no-exit-code)
;          On error (bad PID / no tasks): EAX = -1
;---------------------------------------
WAITPID_RETRIES equ 2000        ; kept for compatibility (no longer used)

;---------------------------------------
; sys_waitpid - Wait for a task to exit
; EBX = PID to wait for (-1 = any child)
; Returns: EAX = exit code (0 if unknown)
;          On error (bad PID): EAX = -1
;
; If called from a ring-3 task, the caller
; is blocked until the child exits.
; If called from the shell (ring-0), we do
; a single non-blocking scan only.
;---------------------------------------
sys_waitpid:
        pushad
        cmp ebx, 0
        jl .swp_any             ; -1 = wait for any
        jmp .swp_check_pid
.swp_any:
        cmp ebx, -1
        jne .swp_fail           ; only -1 or >= 0 supported

.swp_check_pid:
        ; First, check if requested task is already a zombie
.swp_scan_zombie:
        mov edi, task_table
        xor ecx, ecx
.swp_scan:
        cmp ecx, MAX_TASKS
        jge .swp_not_found
        cmp dword [edi + TCB_STATE], TASK_FREE
        je .swp_next
        cmp ebx, -1
        je .swp_check_this      ; any PID
        cmp [edi + TCB_PID], ebx
        jne .swp_next
.swp_check_this:
        cmp dword [edi + TCB_STATE], TASK_ZOMBIE
        je .swp_reap
.swp_next:
        add edi, TCB_SIZE
        inc ecx
        jmp .swp_scan

.swp_reap:
        ; Collect exit code and free TCB slot
        mov eax, [edi + TCB_EXIT_CODE]
        mov dword [edi + TCB_STATE], TASK_FREE
        mov [esp + 28], eax
        popad
        iretd

.swp_not_found:
        ; No zombie found yet — check if we're a ring-3 task
        ; Inspect CS from the iretd frame: [esp+32..36] after pushad
        ; [esp+36] = CS
        mov eax, [esp + 36]
        and eax, 3
        cmp eax, 3
        jne .swp_fail           ; ring-0 (shell) — give up immediately

        ; We're ring-3: block until child exits
        ; Find our own TCB (TASK_RUNNING)
        push ebx
        call sched_get_current_task
        pop ebx
        test eax, eax
        jz .swp_fail            ; no TCB? shouldn't happen

        mov edi, eax            ; EDI = our TCB

        ; Set TCB_WAIT_PID so sched_exit_task will wake us
        mov [edi + TCB_WAIT_PID], ebx

        ; Save ESP and block
        mov [edi + TCB_ESP], esp
        mov dword [edi + TCB_STATE], TASK_BLOCKED

        ; Find next READY task to run
        mov ebx, task_table
        xor edx, edx
.swp_find_next:
        cmp edx, MAX_TASKS
        jge .swp_no_next
        cmp dword [ebx + TCB_STATE], TASK_READY
        je .swp_switch_to
        add ebx, TCB_SIZE
        inc edx
        jmp .swp_find_next

.swp_switch_to:
        ; Swap fd tables: outgoing=EDI, incoming=EBX
        call sched_swap_fd_tables
        mov dword [ebx + TCB_STATE], TASK_RUNNING
        mov [task_current], edx
        mov eax, [ebx + TCB_KSTACK]
        mov [tss_struct + 4], eax
        mov esp, [ebx + TCB_ESP]
        popad
        iretd

.swp_no_next:
        ; No other task to run — go to shell
        ; (our task remains BLOCKED; timer will wake it when child exits)
        ; EDI=our TCB (outgoing), EBX=0 (incoming=shell)
        xor ebx, ebx
        call sched_swap_fd_tables
        mov esp, KERNEL_STACK
        jmp shell_main

.swp_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;---------------------------------------
; sys_yield - SYS_YIELD syscall handler
;---------------------------------------
; sched_force_switch - Switch to another ready task
; Called after a background task has exited (sched_exit_task already
; freed its TCB). Picks the first READY task and IRET to it.
; If no task is ready, falls through to shell_main.
; DOES NOT RETURN (either iretd to ring-3 or jmp shell_main).
;---------------------------------------
sched_force_switch:
        mov edi, task_table
        xor edx, edx
.sfs_scan:
        cmp edx, MAX_TASKS
        jge .sfs_no_task
        cmp dword [edi + TCB_STATE], TASK_READY
        je .sfs_found
        add edi, TCB_SIZE
        inc edx
        jmp .sfs_scan
.sfs_found:
        mov dword [edi + TCB_STATE], TASK_RUNNING
        mov [task_current], edx
        mov eax, [edi + TCB_KSTACK]
        mov [tss_struct + 4], eax
        ; Load incoming task's fd table (EDI=incoming TCB, EBX=0 unused)
        ; Use a direct load: copy private page into global fd_table
        mov eax, [edi + TCB_FD_TABLE_PTR]
        test eax, eax
        jz .sfs_switch
        push esi
        push edi
        push ecx
        mov esi, eax
        mov edi, fd_table
        mov ecx, (FD_MAX * FD_ENTRY_SIZE) / 4
        rep movsd
        pop ecx
        pop edi
        pop esi
.sfs_switch:
        mov esp, [edi + TCB_ESP]  ; Switch to new task's kernel stack
        popad                      ; Restore its GP registers
        iretd                      ; Resume the ring-3 task
.sfs_no_task:
        ; No other ring-3 task running — return to shell prompt
        ; Load shell's fd table back into global fd_table
        push esi
        push edi
        push ecx
        mov esi, shell_fd_table
        mov edi, fd_table
        mov ecx, (FD_MAX * FD_ENTRY_SIZE) / 4
        rep movsd
        pop ecx
        pop edi
        pop esi
        mov esp, KERNEL_STACK
        jmp shell_main

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
        ; EDI = outgoing task TCB
        ; Swap fd tables before switching
        call sched_swap_fd_tables
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
        jz .sk_free_fd
        sub eax, 4096
        call pmm_free_page

.sk_free_fd:
        ; Free private fd table page
        mov eax, [edi + TCB_FD_TABLE_PTR]
        test eax, eax
        jz .sk_clear
        call pmm_free_page
        mov dword [edi + TCB_FD_TABLE_PTR], 0

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
        ; Phase 1.2: validate user-supplied output buffer pointer (ECX).
        ; Must lie in identity-mapped user range; otherwise we'd write the
        ; process list to arbitrary kernel memory.
        push eax
        mov eax, ecx
        call validate_user_ptr
        jc .spl_fail_pop
        ; Also check the last byte of the 48-byte output buffer.
        add eax, 47
        call validate_user_ptr
        jc .spl_fail_pop
        pop eax
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
.spl_fail_pop:
        pop eax
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
        ; EDI = outgoing task TCB, EBX = incoming task TCB
        call sched_swap_fd_tables
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
        ; TCB_WAKEUP == 0 means indefinite block (pause/sigsuspend) — skip
        cmp dword [edi + TCB_WAKEUP], 0
        je  .wake_next
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
; sched_check_alarms — Deliver SIGALRM to tasks whose alarm deadline
;   has been reached.  Called from irq_timer on every PIT tick.
; Clobbers: EAX, ECX, EDI  (caller saves)
;---------------------------------------
sched_check_alarms:
        mov edi, task_table
        xor ecx, ecx
.sca_loop:
        cmp ecx, MAX_TASKS
        jge .sca_done
        cmp dword [edi + TCB_STATE], TASK_FREE
        je  .sca_next
        cmp dword [edi + TCB_ALARM], 0
        je  .sca_next
        mov eax, [tick_count]
        cmp eax, [edi + TCB_ALARM]
        jb  .sca_next
        ; Deadline reached: set SIGALRM pending, disarm alarm
        or  dword [edi + TCB_SIG_PEND], (1 << SIGALRM)
        mov dword [edi + TCB_ALARM], 0
.sca_next:
        add edi, TCB_SIZE
        inc ecx
        jmp .sca_loop
.sca_done:
        ret

;---------------------------------------
; sched_check_itimers — Tick down ITIMER_REAL for all tasks and
;   deliver SIGALRM when the counter reaches zero.
;   Called from irq_timer every PIT tick.
; Clobbers: EAX, ECX, EDI  (caller saves)
;---------------------------------------
sched_check_itimers:
        mov edi, task_table
        xor ecx, ecx
.sci_loop:
        cmp ecx, MAX_TASKS
        jge .sci_done
        cmp dword [edi + TCB_STATE], TASK_FREE
        je  .sci_next
        cmp dword [edi + TCB_ITIMER_VAL], 0
        je  .sci_next
        dec dword [edi + TCB_ITIMER_VAL]
        jnz .sci_next
        ; Counter hit zero — deliver SIGALRM
        or  dword [edi + TCB_SIG_PEND], (1 << SIGALRM)
        ; Wake BLOCKED task so it gets scheduled and the signal delivered
        cmp dword [edi + TCB_STATE], TASK_BLOCKED
        jne .sci_reload
        mov dword [edi + TCB_STATE], TASK_READY
.sci_reload:
        ; Reload interval (0 = one-shot → stays disarmed)
        mov eax, [edi + TCB_ITIMER_INT]
        mov [edi + TCB_ITIMER_VAL], eax
.sci_next:
        add edi, TCB_SIZE
        inc ecx
        jmp .sci_loop
.sci_done:
        ret

; — Dispatch pending user-space signal
;   handlers before iretd to ring-3.
;
;   In : EBX = TCB pointer of the task about to be resumed
;        EDI = base address of the pushad+iretd frame for that task:
;              [edi+0 .. edi+31] = pushad (EDI,ESI,EBP,origESP,EBX,EDX,ECX,EAX)
;              [edi+32] = saved EIP3
;              [edi+36] = saved CS3
;              [edi+40] = saved EFLAGS
;              [edi+44] = saved ESP3
;              [edi+48] = saved SS3
;   Out: EAX = 0  — task is alive, frame may have been redirected to handler
;        EAX = -1 — task was terminated by a default signal action (TCB_STATE
;                   is now TASK_FREE; caller must NOT do popad+iretd)
;   Clobbers: EAX, ECX, EDX, ESI  (preserves EBX, EDI)
;=======================================================================
sched_deliver_signals:
.sdel_next:
        ; Compute pending & unmasked signals
        mov eax, [ebx + TCB_SIG_PEND]
        mov ecx, [ebx + TCB_SIG_MASK]
        not ecx
        and eax, ecx                    ; pending & ~masked
        test eax, eax
        jz  .sdel_ok

        bsf ecx, eax                    ; ecx = lowest pending signal number

        ; Clear the pending bit
        mov eax, 1
        shl eax, cl
        not eax
        and [ebx + TCB_SIG_PEND], eax

        ; Get handler table; if none → default action
        mov esi, [ebx + TCB_SIG_HAND]
        test esi, esi
        jz  .sdel_default

        ; Read disposition for this signal
        mov eax, [esi + ecx*4]          ; eax = handler fn, SIG_DFL, or SIG_IGN
        cmp eax, SIG_IGN
        je  .sdel_next                  ; Ignored — consume and check again

        cmp eax, SIG_DFL
        je  .sdel_default               ; Default action

        ; ---- User-space handler ----
        ; eax = handler fn address, ecx = signum, edi = frame base
        ; Build signal frame on user stack:
        ;   new_esp = old_ESP3 - 32
        ;   [new_esp + 0]  = return_address → stub at new_esp+24
        ;   [new_esp + 4]  = signum
        ;   [new_esp + 8]  = saved_eip
        ;   [new_esp + 12] = saved_eflags
        ;   [new_esp + 16] = saved_old_esp3
        ;   [new_esp + 20] = saved_sig_mask (restored by sys_sigreturn)
        ;   [new_esp + 24..31] = sigreturn stub: mov eax,144; int 0x80; nop
        push eax                        ; save handler fn
        push ecx                        ; save signum

        mov edx, [edi + 44]             ; old ESP3
        mov esi, [edi + 32]             ; old EIP3 (to save)
        sub edx, 32                     ; edx = new_esp

        ; Return address = read-only sigreturn trampoline page (no stub on stack)
        mov eax, SIGRETURN_TRAMPOLINE_VADDR
        mov [edx + 0], eax              ; return_address → trampoline

        mov eax, [esp]                  ; signum (from stack)
        mov [edx + 4], eax

        mov [edx + 8],  esi             ; saved_eip
        mov eax, [edi + 40]
        mov [edx + 12], eax             ; saved_eflags
        lea eax, [edx + 32]             ; saved_old_esp3 = new_esp + 32 = old ESP3
        mov [edx + 16], eax

        mov eax, [ebx + TCB_SIG_MASK]
        mov [edx + 20], eax             ; saved_sig_mask

        ; Zero the previously-used stub bytes (no W^X violation)
        mov dword [edx + 24], 0
        mov dword [edx + 28], 0

        pop ecx                         ; discard signum
        pop eax                         ; restore handler fn

        ; Redirect iretd frame to handler
        mov [edi + 32], eax             ; new EIP3 = handler
        and dword [edi + 40], 0xFFFFFEFF ; clear TF in EFLAGS
        mov [edi + 44], edx             ; new ESP3 = signal frame

        jmp .sdel_next                  ; check for more signals

        ; ---- Default signal action ----
.sdel_default:
        cmp ecx, SIGCONT
        je  .sdel_next              ; SIGCONT on running task = no-op
        cmp ecx, SIGTSTP
        je  .sdel_stop

        ; All others (SIGKILL, SIGTERM, SIGINT, SIGALRM, etc.): terminate
        ; Free kernel stack page if allocated
        mov eax, [ebx + TCB_KSTACK]
        test eax, eax
        jz  .sdel_no_kstack
        sub eax, 4096
        call pmm_free_page
.sdel_no_kstack:
        ; Free private fd table page if allocated (always check, independent of kstack)
        mov eax, [ebx + TCB_FD_TABLE_PTR]
        test eax, eax
        jz .sdel_term_clear
        call pmm_free_page
        mov dword [ebx + TCB_FD_TABLE_PTR], 0
.sdel_term_clear:
        mov dword [ebx + TCB_STATE],  TASK_FREE
        mov dword [ebx + TCB_ESP],    0
        mov dword [ebx + TCB_KSTACK], 0
        dec dword [task_count]
        mov eax, -1
        ret                             ; EAX = -1 : task killed

.sdel_stop:
        mov dword [ebx + TCB_STATE], TASK_STOPPED
        ; Fall through to sdel_ok (task stopped, not killed)
.sdel_ok:
        xor eax, eax
        ret                             ; EAX = 0 : task alive


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
        ; Other signals: set pending bit; wake BLOCKED task so pause/sigsuspend return
        mov dword [esp + 28], 0
        ; If target is BLOCKED (sleeping/paused), mark READY so it gets scheduled
        cmp dword [edi + TCB_STATE], TASK_BLOCKED
        jne .sig_ok_done
        mov dword [edi + TCB_STATE], TASK_READY
.sig_ok_done:
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
        ; Phase 1.4: serialize signal-mask read-modify-write. A timer IRQ
        ; landing between the load of EAX (old mask) and the store could
        ; deliver a signal under the wrong mask.
        cli
        mov eax, [edi + TCB_SIG_MASK]  ; Old mask
        cmp ebx, 0
        je .smk_ret              ; Just get
        cmp ebx, 1
        je .smk_set
        cmp ebx, 2
        je .smk_block
        cmp ebx, 3
        je .smk_unblock
        sti
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
        sti
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

;---------------------------------------
; sched_get_current_task - Find the TCB of the currently running task
; Returns: EAX = pointer to TCB, or 0 if no task is running
;---------------------------------------
sched_get_current_task:
        push edi
        push edx
        mov edi, task_table
        xor edx, edx
.find_running:
        cmp edx, MAX_TASKS
        jge .not_found
        cmp dword [edi + TCB_STATE], TASK_RUNNING
        je .found
        add edi, TCB_SIZE
        inc edx
        jmp .find_running
.found:
        mov eax, edi
        pop edx
        pop edi
        ret
.not_found:
        xor eax, eax
        pop edx
        pop edi
        ret

;---------------------------------------
; sys_getppid - return parent PID of calling task
; Returns: EAX = parent PID (0 = shell/init)
;---------------------------------------
sys_getppid:
        ; Find RUNNING task and read TCB_PPID
        mov edi, task_table
        xor ecx, ecx
.gpp_find:
        cmp ecx, MAX_TASKS
        jge .gpp_shell
        cmp dword [edi + TCB_STATE], TASK_RUNNING
        je .gpp_found
        add edi, TCB_SIZE
        inc ecx
        jmp .gpp_find
.gpp_found:
        mov eax, [edi + TCB_PPID]
        iretd
.gpp_shell:
        xor eax, eax
        iretd

;=======================================================================
; sys_fork — SYS_FORK (103) — Create child process (v9.0)
;
; Returns: EAX = child PID to parent, 0 to child, -1 on error.
;
; The child starts at the same user EIP as the parent (the instruction
; after "int 0x80") with EAX=0.  Both receive their own kernel and user
; stacks (BG_STACK_PAGES pages copied from parent).  Address space is
; shared (vfork-style) until per-process page tables land in v9.x.
;=======================================================================
sys_fork:
        pushad

        ;--- 1. Find the RUNNING task (parent) ---
        mov  edi, task_table
        xor  edx, edx
.fork_find_parent:
        cmp  edx, MAX_TASKS
        jge  .fork_fail         ; Not inside a task (shell context)
        cmp  dword [edi + TCB_STATE], TASK_RUNNING
        je   .fork_have_parent
        add  edi, TCB_SIZE
        inc  edx
        jmp  .fork_find_parent

.fork_have_parent:
        ; EDI = parent TCB; save key fields
        mov  eax, [edi + TCB_PID]
        mov  [fork_parent_pid], eax
        mov  [fork_parent_tcb], edi

        ;--- 2. Find a free TCB slot for the child ---
        mov  esi, task_table
        xor  edx, edx
.fork_find_slot:
        cmp  edx, MAX_TASKS
        jge  .fork_fail
        cmp  dword [esi + TCB_STATE], TASK_FREE
        je   .fork_have_slot
        add  esi, TCB_SIZE
        inc  edx
        jmp  .fork_find_slot

.fork_have_slot:
        ; ESI = child TCB, EDX = child slot index (= child PID)
        mov  [fork_child_tcb], esi
        mov  [fork_child_pid_slot], edx

        ;--- 3. Allocate child kernel stack (4 KB page) ---
        call pmm_alloc_page
        test eax, eax
        jz   .fork_fail
        add  eax, 4096              ; kstack_top
        mov  [fork_child_kstop], eax

        ;--- 4. Allocate child user stack (BG_STACK_PAGES + 1 pages) ---
        ; +1 for guard page (same layout as sched_create_task)
        push edx
        mov  ecx, BG_STACK_PAGES + 1
        call pmm_alloc_pages
        pop  edx
        test eax, eax
        jz   .fork_fail_kstack      ; OOM — release kernel stack

        mov  [fork_child_ubase], eax
        mov  ebx, eax
        add  ebx, (BG_STACK_PAGES + 1) * 4096
        mov  [fork_child_ustop], ebx

        ;--- 5. Copy parent user stack → child user stack ---
        ; Parent stack occupies [TCB_USTACK - BG_STACK_PAGES*4096, TCB_USTACK)
        ; (The guard page is one page BELOW the base, at TCB_USTACK - (BG+1)*4096)
        mov  edi, [fork_parent_tcb]
        mov  esi, [edi + TCB_USTACK]
        sub  esi, (BG_STACK_PAGES + 1) * 4096  ; start of parent allocation
        mov  edi, [fork_child_ubase]             ; start of child allocation
        mov  ecx, (BG_STACK_PAGES + 1) * 4096 / 4
        rep  movsd

        ;--- 6. Build child kernel-stack frame ---
        ; Current parent kernel stack (ESP after our pushad) layout:
        ;   [esp+ 0..31] = pushad frame (EDI ESI EBP _ EBX EDX ECX EAX)
        ;   [esp+32..51] = CPU IRETD frame (EIP CS EFLAGS ESP3 SS3)
        ; Copy 52 bytes → child kernel stack, then patch EAX=0 and ESP3.
        mov  edi, [fork_child_kstop]
        sub  edi, 52                ; child kernel ESP
        mov  [fork_child_kesp], edi
        mov  esi, esp               ; parent kernel ESP (pushad is top of frame)
        mov  ecx, 52 / 4
        rep  movsd

        ; Patch EAX slot in child pushad frame = 0 (child return value)
        ; pushad frame: [esp+0]=EDI [+4]=ESI [+8]=EBP [+12]=ESP_orig
        ;               [+16]=EBX  [+20]=EDX [+24]=ECX [+28]=EAX
        mov  edi, [fork_child_kesp]
        mov  dword [edi + 28], 0    ; child EAX = 0

        ; Patch ESP3 in child IRETD frame (at frame+32+12 = frame+44)
        ; ESP3 = child stack base + same offset as parent within stack
        mov  eax, [esp + 44]        ; parent's ESP3 from IRETD frame
        mov  ebx, [fork_parent_tcb]
        mov  ecx, [ebx + TCB_USTACK]
        sub  ecx, (BG_STACK_PAGES + 1) * 4096  ; parent stack base
        sub  eax, ecx               ; offset of parent ESP3 within stack
        mov  ecx, [fork_child_ubase]
        add  eax, ecx               ; child ESP3 = child_base + offset
        mov  [edi + 44], eax        ; patch into child frame

        ;--- 7. Populate child TCB ---
        ; Copy parent TCB as base (inherits open files, env, signals, brk, etc.)
        mov  esi, [fork_parent_tcb]
        mov  edi, [fork_child_tcb]
        mov  ecx, TCB_SIZE / 4
        rep  movsd

        ; Overwrite child-specific fields
        mov  edi, [fork_child_tcb]
        mov  eax, [fork_child_pid_slot]
        mov  [edi + TCB_PID],    eax        ; child PID = slot index
        mov  eax, [fork_parent_pid]
        mov  [edi + TCB_PPID],   eax        ; parent PID
        mov  dword [edi + TCB_STATE], TASK_READY
        mov  eax, [fork_child_kstop]
        mov  [edi + TCB_KSTACK], eax
        mov  eax, [fork_child_ustop]
        mov  [edi + TCB_USTACK], eax
        mov  eax, [fork_child_kesp]
        mov  [edi + TCB_ESP],    eax
        mov  dword [edi + TCB_SIG_PEND],  0  ; clear pending signals for child
        mov  dword [edi + TCB_ALARM],     0  ; no alarm inherited by child
        mov  dword [edi + TCB_SIG_HAND],  0  ; v11: child starts with no signal handlers
        ; v12: reset child's itimers and sigsuspend saved mask
        mov  dword [edi + TCB_ITIMER_VAL],  0
        mov  dword [edi + TCB_ITIMER_INT],  0
        mov  dword [edi + TCB_SAVED_MASK],  0
        ; Reset child's wait/errno/fd table fields (must NOT inherit parent's)
        mov  dword [edi + TCB_WAIT_PID],    0
        mov  dword [edi + TCB_ERRNO],       0

        ;--- 8b. Allocate child's private fd table page (copy parent's) ---
        push edi                        ; save child TCB
        call pmm_alloc_page             ; EAX = new page or 0
        pop  edi
        test eax, eax
        jnz  .fork_have_fd_page
        ; OOM: could not allocate fd page — child gets no open fds
        mov  dword [edi + TCB_FD_TABLE_PTR], 0
        jmp  .fork_fd_done
.fork_have_fd_page:
        ; Zero-copy parent fd table view (global fd_table) into child's private page
        push eax                        ; save child fd page addr
        push edi
        mov  edi, eax
        mov  esi, fd_table
        mov  ecx, (FD_MAX * FD_ENTRY_SIZE) / 4
        rep  movsd
        pop  edi
        pop  eax
        mov  [edi + TCB_FD_TABLE_PTR], eax
.fork_fd_done:

        inc  dword [task_count]

        ;--- 8. Return child PID to parent ---
        mov  eax, [fork_child_pid_slot]
        mov  [esp + 28], eax        ; patch EAX in parent pushad frame

        popad
        iretd

.fork_fail_kstack:
        ; Release kernel stack page before failing
        mov  eax, [fork_child_kstop]
        sub  eax, 4096
        call pmm_free_page
.fork_fail:
        mov  dword [esp + 28], -1
        popad
        iretd
