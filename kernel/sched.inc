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
TCB_PPID     equ 68         ; v9.0: Parent PID (-1 = no parent / shell)
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
TCB_CR3         equ 128     ; v13: Physical address of this task's page directory
                            ;      (0 = not allocated; the kernel/shell uses the
                            ;       master directory at PAGE_DIR_ADDR)
TCB_SIZE        equ 136     ; Bytes per TCB (TCB_CR3 occupies 128..131)

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

        ; --- Save the live view into the outgoing task's private page ---
        cmp edi, 0
        je .sft_save_shell          ; outgoing is the shell

        mov eax, [edi + TCB_FD_TABLE_PTR]
        test eax, eax
        jz .sft_load                ; no private page — skip save
        ; The page layout is legacy fd table first, then the VFD table, so two
        ; consecutive movsd runs fill it without recomputing destinations.
        push edi
        mov edi, eax                ; dest = private page
        mov esi, fd_table
        mov ecx, (FD_MAX * FD_ENTRY_SIZE) / 4
        cld
        rep movsd
        mov esi, vfs_fd_table
        mov ecx, FD_PAGE_VFD_BYTES / 4
        rep movsd
        pop edi
        jmp .sft_load

.sft_save_shell:
        ; outgoing is shell — save to the shell's copies
        push edi
        mov edi, shell_fd_table
        mov esi, fd_table
        mov ecx, (FD_MAX * FD_ENTRY_SIZE) / 4
        cld
        rep movsd
        mov edi, shell_vfd_table
        mov esi, vfs_fd_table
        mov ecx, FD_PAGE_VFD_BYTES / 4
        rep movsd
        pop edi

.sft_load:
        ; --- Load incoming task's private copy into the live tables ---
        cmp ebx, 0
        je .sft_load_shell          ; incoming is the shell

        mov eax, [ebx + TCB_FD_TABLE_PTR]
        test eax, eax
        jz .sft_done                ; no private page — leave global as-is
        mov esi, eax
        mov edi, fd_table
        mov ecx, (FD_MAX * FD_ENTRY_SIZE) / 4
        cld
        rep movsd
        mov edi, vfs_fd_table
        mov ecx, FD_PAGE_VFD_BYTES / 4
        rep movsd
        jmp .sft_done

.sft_load_shell:
        mov esi, shell_fd_table
        mov edi, fd_table
        mov ecx, (FD_MAX * FD_ENTRY_SIZE) / 4
        cld
        rep movsd
        mov esi, shell_vfd_table
        mov edi, vfs_fd_table
        mov ecx, FD_PAGE_VFD_BYTES / 4
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
        ; No job owns the console yet. -1 rather than 0: PGIDs are task PIDs
        ; and the task in slot 0 really does have PGID 0.
        mov dword [tty_fg_pgid], -1
        mov dword [sched_kbd_tcb], 0
        mov dword [sched_sig_self], 0
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
        ; Allocate this task's page directory (a copy of the master directory)
        push edx
        push edi
        call paging_new_pd          ; EAX = PD physical address, or 0
        pop edi
        pop edx
        test eax, eax
        jz .create_fail
        mov [edi + TCB_CR3], eax

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
        ; A task created by the shell has no parent. Set this explicitly:
        ; TCB slots are reused and sched_exit_task reads TCB_PPID to decide
        ; whether a zombie can ever be collected, so a stale value from the
        ; previous occupant would mis-route SIGCHLD / strand the slot.
        ; -1 (not 0) is the sentinel: slot 0 is a real PID, and a child of the
        ; task in slot 0 must still be treated as having a parent.
        mov [edi + TCB_PPID], dword -1
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

        ; Allocate a private fd table page and inherit the launcher's fd view.
        ; A new task sees the stdin/stdout redirections that were active when it
        ; was created, but later changes are isolated because each task owns its
        ; own page (sched_swap_fd_tables swaps whole pages).
        push edx
        push edi
        call pmm_alloc_page         ; EAX = page address or 0
        pop edi
        pop edx
        test eax, eax
        jz .no_fd_page
        push eax
        push edi
        mov edi, eax
        mov esi, fd_table
        mov ecx, (FD_MAX * FD_ENTRY_SIZE) / 4
        cld
        rep movsd
        mov esi, vfs_fd_table
        mov ecx, FD_PAGE_VFD_BYTES / 4
        rep movsd
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
        ; Kernel-stack allocation failed: release the page directory first.
        ; EDI still holds the TCB pointer (it was pushed, not modified).
        mov eax, [edi + TCB_CR3]
        test eax, eax
        jz  .create_fail_pop_done
        mov dword [edi + TCB_CR3], 0
        call pmm_free_page
.create_fail_pop_done:
        pop ecx
        pop ebx
        pop edi
        pop edx
.create_fail:
        mov dword [esp + 28], -1        ; Return -1
        popad
        ret

;---------------------------------------
; sched_free_ready_task - release a READY task that never ran
;   In : EAX = task id
;
; Used when a GUI launch cannot isolate the new task's program window. The
; task's page directory is not active (we are in the master directory), so it
; can be freed directly.
;---------------------------------------
sched_free_ready_task:
        pushad
        cmp eax, MAX_TASKS
        jae .sfr_done
        imul eax, TCB_SIZE
        add eax, task_table
        mov edi, eax
        cmp dword [edi + TCB_STATE], TASK_READY
        jne .sfr_done

        ; Release private program-window frames.
        mov eax, [edi + TCB_CR3]
        test eax, eax
        jz .sfr_kstack
        mov ebx, PROGRAM_BASE
        mov ecx, PROGRAM_MAX_SIZE / 4096
        call paging_free_window_private

.sfr_kstack:
        mov eax, [edi + TCB_KSTACK]
        test eax, eax
        jz .sfr_fd
        sub eax, 4096
        call pmm_free_page
        mov dword [edi + TCB_KSTACK], 0
.sfr_fd:
        mov eax, [edi + TCB_FD_TABLE_PTR]
        test eax, eax
        jz .sfr_sig
        call pmm_free_page
        mov dword [edi + TCB_FD_TABLE_PTR], 0
.sfr_sig:
        mov eax, [edi + TCB_SIG_HAND]
        test eax, eax
        jz .sfr_pd
        mov dword [edi + TCB_SIG_HAND], 0
        call pmm_free_page
.sfr_pd:
        mov eax, [edi + TCB_CR3]
        test eax, eax
        jz .sfr_clear
        mov dword [edi + TCB_CR3], 0
        call pmm_free_page
.sfr_clear:
        mov dword [edi + TCB_STATE], TASK_FREE
        mov dword [edi + TCB_PID], 0
        dec dword [task_count]
.sfr_done:
        popad
        ret

;---------------------------------------
; sched_kill_all_tasks - tear down every task at once
;
; Used when the desktop shuts down: a GUI app must never outlive Burrows,
; because it would keep polling windows and drawing over the restored text
; console. Must run with the master page directory active.
;---------------------------------------
sched_kill_all_tasks:
        pushad
        xor esi, esi                    ; task index
.skat_loop:
        cmp esi, MAX_TASKS
        jge .skat_done
        mov edi, esi
        imul edi, TCB_SIZE
        add edi, task_table
        cmp dword [edi + TCB_STATE], TASK_FREE
        je .skat_next

        ; Release the private program-window frames.
        mov eax, [edi + TCB_CR3]
        test eax, eax
        jz .skat_ustack
        mov ebx, PROGRAM_BASE
        mov ecx, PROGRAM_MAX_SIZE / 4096
        call paging_free_window_private

.skat_ustack:
        mov eax, [edi + TCB_USTACK]
        test eax, eax
        jz .skat_kstack
        add eax, 4
        sub eax, BG_STACK_PAGES * 4096
        cmp eax, PROGRAM_BASE
        jb .skat_free_ustack
        mov ebx, PROGRAM_BASE + PROGRAM_MAX_SIZE
        cmp eax, ebx
        jb .skat_kstack                 ; inside the program window
.skat_free_ustack:
        mov ecx, BG_STACK_PAGES
        call cow_release_range

.skat_kstack:
        mov eax, [edi + TCB_KSTACK]
        test eax, eax
        jz .skat_fd
        sub eax, 4096
        call pmm_free_page
        mov dword [edi + TCB_KSTACK], 0
.skat_fd:
        mov eax, [edi + TCB_FD_TABLE_PTR]
        test eax, eax
        jz .skat_sig
        call pmm_free_page
        mov dword [edi + TCB_FD_TABLE_PTR], 0
.skat_sig:
        mov eax, [edi + TCB_SIG_HAND]
        test eax, eax
        jz .skat_pd
        mov dword [edi + TCB_SIG_HAND], 0
        call pmm_free_page
.skat_pd:
        ; The master directory is active here, so the task's directory is not
        ; in use and can be released immediately.
        mov eax, [edi + TCB_CR3]
        test eax, eax
        jz .skat_clear
        mov dword [edi + TCB_CR3], 0
        call pmm_free_page
.skat_clear:
        mov dword [edi + TCB_STATE], TASK_FREE
        mov dword [edi + TCB_PID], 0
        mov dword [edi + TCB_USTACK], 0
.skat_next:
        inc esi
        jmp .skat_loop
.skat_done:
        mov dword [task_count], 0
        mov dword [task_current], 0
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
        ; Reap any windows this task owned before the TCB is recycled.
        mov eax, [edi + TCB_PID]
        call gui_cleanup_pid

        ; Free the kernel stack page (kstack_top - 4096 = page base).
        ; Callers switch to KERNEL_STACK before invoking us, so the stack this
        ; runs on is never the page being released here.
        mov eax, [edi + TCB_KSTACK]
        sub eax, 4096
        call pmm_free_page

        ; Release any program-window frames this task copied for itself at exec
        ; time. Frames still shared with the master are left untouched.
        push edi
        mov eax, [edi + TCB_CR3]
        mov ebx, PROGRAM_BASE
        mov ecx, PROGRAM_MAX_SIZE / 4096
        call paging_free_window_private
        pop edi

        ; Release the user stack when it is a separate allocation (background
        ; tasks, and children forked from them). Foreground programs stack at
        ; the top of the program window, covered by the release above. Each page
        ; is released through its reference count, so a stack still shared with
        ; a fork parent/child survives until the last owner exits.
        mov eax, [edi + TCB_USTACK]
        test eax, eax
        jz .exit_no_ustack
        add eax, 4
        sub eax, BG_STACK_PAGES * 4096
        cmp eax, PROGRAM_BASE
        jb  .exit_free_ustack
        mov ebx, PROGRAM_BASE + PROGRAM_MAX_SIZE
        cmp eax, ebx
        jb  .exit_no_ustack            ; inside the program window
.exit_free_ustack:
        mov ecx, BG_STACK_PAGES
        call cow_release_range
.exit_no_ustack:

        ; Queue this task's page directory for release. It cannot be freed here:
        ; this runs in the dying task's context, so CR3 may still point at it,
        ; and reusing a directory the CPU is walking corrupts the kernel. The
        ; next CR3 switch releases it (see sched_apply_cr3).
        mov eax, [edi + TCB_CR3]
        test eax, eax
        jz .exit_no_pd
        mov dword [edi + TCB_CR3], 0
        mov [pd_deferred_free], eax
.exit_no_pd:

        ; Free the private fd table page if allocated
        mov eax, [edi + TCB_FD_TABLE_PTR]
        test eax, eax
        jz .exit_no_fd_page
        call pmm_free_page
        mov [edi + TCB_FD_TABLE_PTR], dword 0
.exit_no_fd_page:
        ; Free the signal handler table page (allocated lazily by sigaction)
        mov eax, [edi + TCB_SIG_HAND]
        test eax, eax
        jz .exit_no_hand
        mov dword [edi + TCB_SIG_HAND], 0
        call pmm_free_page
.exit_no_hand:

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
        mov eax, [edi + TCB_PPID]       ; parent PID (-1 = no parent / shell)
        cmp eax, -1
        je .exit_reap                   ; no parent → nothing will waitpid()

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
        jmp .exit_done

.exit_reap:
        ; A shell-launched task (PPID = -1) has no parent that could ever call
        ; waitpid(), so this zombie would stay in the table forever and every
        ; launch would leak a TCB slot. Reap it immediately.
        mov dword [edi + TCB_STATE], TASK_FREE
        mov dword [edi + TCB_PID], 0

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
        ; Install the incoming address space and deliver before committing:
        ; the signal frame lives on that task's user stack, and a task that a
        ; default signal action kills here must not be resumed.
        mov eax, ebx
        call sched_apply_cr3
        push edx
        push edi                        ; EDI = our TCB, needed by the fd swap
        mov edi, [ebx + TCB_ESP]
        call sched_deliver_signals
        pop edi
        pop edx
        test eax, eax
        js  .swp_find_next          ; dead — keep scanning from this slot

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
        xor eax, eax
        call sched_apply_cr3          ; back to the master directory
        mov esp, KERNEL_STACK
        cmp byte [gui_desktop_active], 1
        je gui_desktop_resume
        jmp shell_main

.swp_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;---------------------------------------
; sched_set_create_name - name the next task created
;   In : ESI = NUL-terminated program name
; sched_create_task copies task_create_name into the new TCB; without this a
; foreground task would inherit whatever name the last background launch left
; behind (and ps would print the wrong label).
;---------------------------------------
sched_set_create_name:
        pushad
        mov edi, task_create_name
        mov ecx, 16
        cld
        rep movsb
        mov byte [task_create_name + 15], 0
        popad
        ret

;---------------------------------------
; sched_set_task_args - snapshot the launch arguments for a task
;   In : EAX = task id (0..MAX_TASKS-1)
; Copies program_args_buf into the task's private slot. TCB slots are reused,
; so a new launch always overwrites whatever the previous occupant left.
;---------------------------------------
sched_set_task_args:
        pushad
        cmp eax, MAX_TASKS
        jae .ssta_done
        mov edi, eax
        shl edi, 9                  ; * 512 bytes per slot
        add edi, task_argv_table
        mov esi, program_args_buf
        mov ecx, 512 / 4
        cld
        rep movsd
.ssta_done:
        popad
        ret

;---------------------------------------
; sched_restore_shell_fds - install the shell's saved fd view
;---------------------------------------
sched_restore_shell_fds:
        pushad
        mov esi, shell_fd_table
        mov edi, fd_table
        mov ecx, (FD_MAX * FD_ENTRY_SIZE) / 4
        cld
        rep movsd
        mov esi, shell_vfd_table
        mov edi, vfs_fd_table
        mov ecx, FD_PAGE_VFD_BYTES / 4
        rep movsd
        popad
        ret

;---------------------------------------
; sched_start_ready - Run the first READY task, if there is one
;
;   Returns immediately (all registers and EFLAGS preserved) when no task is
;   READY. Otherwise switches to ring 3 and does NOT return: the task's exit
;   path (sys_exit -> sched_force_switch) brings control back to shell_main.
;
;   Why this exists: the PIT handler only preempts ring-3 code, and the shell
;   runs at ring 0. A task created by the shell's `&` operator is therefore
;   never picked up by the timer - nothing is RUNNING to preempt - so without
;   this hook it sits READY forever. The shell calls it while waiting for a
;   fresh command line, which is the one point where it has no context worth
;   preserving.
;
;   The outgoing context is the shell, so its fd view is saved to
;   shell_fd_table before the incoming task's private table is loaded.
;---------------------------------------
sched_start_ready:
        pushad
        pushfd
        cli
        mov edi, task_table
        xor edx, edx
.ssr_scan:
        cmp edx, MAX_TASKS
        jge .ssr_none
        cmp dword [edi + TCB_STATE], TASK_READY
        je .ssr_found
        add edi, TCB_SIZE
        inc edx
        jmp .ssr_scan

.ssr_none:
        popfd
        popad
        ret

.ssr_found:
        ; EDI = TCB, EDX = slot; the helper switches to it and never returns.
        jmp sched_switch_to_tcb

;---------------------------------------
; sched_switch_to_tcb - switch from the shell to a READY task
;   In : EDI = TCB pointer, EDX = slot index
;   Out: does not return (iretd into ring 3)
;
; The outgoing context is always the shell, so the global fd view is saved to
; shell_fd_table before the incoming task's private table is loaded.
;---------------------------------------
sched_switch_to_tcb:
        mov dword [edi + TCB_STATE], TASK_RUNNING
        mov [task_current], edx

        push edi
        push edx
        mov ebx, edi            ; incoming TCB
        xor edi, edi            ; outgoing = shell
        call sched_swap_fd_tables
        pop edx
        pop edi

        mov eax, edi
        call sched_apply_cr3
        mov eax, [edi + TCB_KSTACK]
        mov [tss_struct + 4], eax
        mov esp, [edi + TCB_ESP]
        popad
        iretd

;---------------------------------------
; sched_start_task - switch to a specific READY task by id
;   In : EAX = task id (0..MAX_TASKS-1)
;   Out: does not return; falls back to shell_main if the task is not READY
;---------------------------------------
sched_start_task:
        cmp eax, MAX_TASKS
        jae .sst_bad
        mov edx, eax
        imul eax, TCB_SIZE
        add eax, task_table
        mov edi, eax
        cmp dword [edi + TCB_STATE], TASK_READY
        jne .sst_bad
        jmp sched_switch_to_tcb

.sst_bad:
        xor eax, eax
        call sched_apply_cr3
        call sched_restore_shell_fds
        mov dword [tss_struct + 4], KERNEL_STACK
        mov esp, KERNEL_STACK
        jmp shell_main

;---------------------------------------
; sys_yield - SYS_YIELD syscall handler
;---------------------------------------
; sched_try_yield_frame - cooperative yield from a syscall handler
;
; Call with a pushad+iretd frame on the current kernel stack (the normal
; INT 0x80 handler shape). If another task is READY, save the caller's frame,
; mark it READY and switch to that task. Returns immediately when no task is
; ready. Used by GUI_FLIP so a busy GUI app cannot starve its peers.
;
; The caller should write its desired EAX return value into the pushad frame
; before calling; it is restored when the task resumes.
;---------------------------------------
sched_try_yield_frame:
        pushad
        call sched_get_current_task
        test eax, eax
        jz .sty_ret
        mov edi, eax

        ; Round-robin from the slot after the caller so a busy foreground
        ; task cannot starve a later-created GUI app.
        mov edx, [task_current]
        inc edx
        cmp edx, MAX_TASKS
        jb .sty_start_ok
        xor edx, edx
.sty_start_ok:
        mov eax, edx
        imul eax, TCB_SIZE
        lea ebx, [task_table + eax]
        mov ecx, MAX_TASKS
.sty_scan:
        cmp dword [ebx + TCB_STATE], TASK_READY
        je .sty_found
        add ebx, TCB_SIZE
        inc edx
        cmp edx, MAX_TASKS
        jb .sty_no_wrap
        xor edx, edx
        mov ebx, task_table
.sty_no_wrap:
        dec ecx
        jnz .sty_scan
        jmp .sty_ret

.sty_found:
        ; Our pushad is 32 bytes and the call return address another 4, so the
        ; caller's pushad frame starts at esp+36.
        lea eax, [esp + 36]
        mov [edi + TCB_ESP], eax
        mov dword [edi + TCB_STATE], TASK_READY

        ; sched_swap_fd_tables wants EDI=outgoing, EBX=incoming.
        call sched_swap_fd_tables
        mov dword [ebx + TCB_STATE], TASK_RUNNING
        mov [task_current], edx
        mov eax, [ebx + TCB_KSTACK]
        mov [tss_struct + 4], eax
        mov eax, ebx
        call sched_apply_cr3
        mov esp, [ebx + TCB_ESP]
        popad
        iretd

.sty_ret:
        popad
        ret

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
        mov eax, edi
        call sched_apply_cr3
        mov eax, [edi + TCB_KSTACK]
        mov [tss_struct + 4], eax
        ; Load incoming task's fd table (EDI=incoming TCB, EBX=0 unused)
        ; Use a direct load: copy the private page into both live tables
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
        mov edi, vfs_fd_table
        mov ecx, FD_PAGE_VFD_BYTES / 4
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
        call sched_restore_shell_fds
        xor eax, eax
        call sched_apply_cr3          ; back to the master directory
        mov dword [tss_struct + 4], KERNEL_STACK
        mov esp, KERNEL_STACK
        cmp byte [gui_desktop_active], 1
        je gui_desktop_resume
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
        ; Fast path: if 0 or 1 tasks, there is nothing to switch to — but a
        ; lone task can still have a signal pending, and nothing else would
        ; deliver it: the timer only advances the quantum when it interrupted
        ; ring 3, and this loop lives in ring 0. So dispatch it here.
        cmp dword [task_count], 2
        jae .yld_switch_path
        call sched_get_current_task
        test eax, eax
        jz .yield_nop               ; shell context: nothing to deliver to
        mov ebx, eax                ; EBX = our own TCB
        pushad
        mov edi, esp                ; EDI = frame base (pushad + iretd)
        mov dword [sched_sig_self], 1
        call sched_deliver_signals
        cmp dword [ebx + TCB_STATE], TASK_RUNNING
        jne sched_self_leave        ; terminated or stopped: leave this stack
        popad
        iretd

.yld_switch_path:
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
        ; Deliver pending signals before re-entering ring 3. A program that
        ; yields in a tight loop spends most of its time inside this syscall
        ; (ring 0), where the timer never advances the quantum, so relying on
        ; the timer to deliver would starve it forever.
        mov ebx, edi            ; EBX = our TCB
        mov edi, esp            ; EDI = our kernel frame
        mov dword [sched_sig_self], 1
        call sched_deliver_signals
        cmp dword [ebx + TCB_STATE], TASK_RUNNING
        jne sched_self_leave
        jmp .yld_abort

; Local temporaries (in .data-ish section within code)
.best_slot: dd 0
.best_prio: dd 0
.yld_res:   dd 0

.yld_switch:
        ; EBX = pointer to new task's TCB, ESI = new task index
        ; EDX = caller's slot
        ; Install the incoming address space first: the signal frame is written
        ; onto its user stack, which only exists in its own page directory.
        ; Nothing is committed yet, so a candidate killed by a default action
        ; can be dropped without touching our own fd view or state.
        mov eax, ebx
        call sched_apply_cr3

        ; sched_deliver_signals clobbers EAX/ECX/EDX/ESI — ESI holds the
        ; incoming slot that task_current needs below, so both must survive.
        push edx
        push esi
        mov edi, [ebx + TCB_ESP]
        call sched_deliver_signals
        mov [.yld_res], eax
        pop esi
        pop edx
        cmp dword [.yld_res], 0
        js .yld_resume_self

        ; EDI = outgoing (caller) TCB
        mov eax, edx
        imul eax, TCB_SIZE
        lea edi, [task_table + eax]

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

.yld_resume_self:
        ; The candidate died before we committed to it — resume the caller and
        ; put its address space and TSS back.
        mov eax, edx
        imul eax, TCB_SIZE
        lea edi, [task_table + eax]
        mov dword [edi + TCB_STATE], TASK_RUNNING
        mov eax, edi
        call sched_apply_cr3
        mov eax, [edi + TCB_KSTACK]
        mov [tss_struct + 4], eax
        jmp .yld_abort

.yld_abort:
        ; Couldn't switch — restore current task state and return
        popad
.yield_nop:
        iretd

;---------------------------------------
; sched_self_leave - leave a task whose signal delivery ran on its own stack
;   In : EBX = TCB of the task being left (TASK_FREE or TASK_STOPPED)
;
;   The stack underneath us belongs to that task, so switch to the shared
;   kernel stack before touching anything. A terminated task's kernel stack
;   page was deliberately kept allocated by sched_deliver_signals and is
;   released here; a stopped task keeps its stack, fd view, address space and
;   TCB so fg/bg can resume it.
;
;   Never returns: control goes back to the shell.
;---------------------------------------
sched_self_leave:
        mov esp, KERNEL_STACK
        cmp dword [ebx + TCB_STATE], TASK_FREE
        jne .ssl_keep_kstack
        mov eax, [ebx + TCB_KSTACK]
        test eax, eax
        jz .ssl_keep_kstack
        sub eax, 4096
        call pmm_free_page
        mov dword [ebx + TCB_KSTACK], 0
.ssl_keep_kstack:
        ; Save the outgoing task's fd view (needed if it was only stopped) and
        ; install the shell's.
        mov edi, ebx
        xor ebx, ebx
        call sched_swap_fd_tables
        call sched_restore_shell_fds
        xor eax, eax
        call sched_apply_cr3
        mov dword [tss_struct + 4], KERNEL_STACK
        mov dword [tty_fg_pgid], -1
        mov byte [program_running], 0
        call shell_redir_reset
        call vga_reset_color
        sti
        jmp shell_main


;---------------------------------------
; sys_getpid - Return current task PID
; Returns EAX = task PID (or -1 if shell)
;
; EDI/ECX are the scan registers, so save them: only EAX may carry a value
; back to user space. sys_getpid used to hand back its loop counter in ECX,
; which silently corrupted any caller that kept something there across the
; call (a signal number, for instance).
;---------------------------------------
sys_getpid:
        ; Find RUNNING task
        push ecx
        push edi
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
        pop edi
        pop ecx
        iretd
.gp_shell:
        mov eax, -1
        pop edi
        pop ecx
        iretd

;---------------------------------------
; sched_signal_task - deliver a signal to a task, or tear it down now
;   In : EDI = TCB, EAX = signal number (1..NUM_SIGNALS-1)
;   Out: nothing (all registers preserved)
;
; A task that installed a handler gets the signal queued; if it is blocked it
; is made runnable so the next context switch delivers it. Tasks with SIG_DFL
; (or no handler table at all) are destroyed immediately, which keeps kill()
; as decisive as before for programs that never handle signals.
;
; A RUNNING task is always queued: it is the caller itself, and freeing the
; kernel stack we are standing on would corrupt the machine.
;---------------------------------------
sched_signal_task:
        pushad
        mov [.sst_tcb], edi
        mov [.sst_sig], eax

        ; A RUNNING task is the caller itself: always queue, never tear it down
        ; (we are standing on its kernel stack). This also covers SIGKILL.
        cmp dword [edi + TCB_STATE], TASK_RUNNING
        je  .sst_queue

        ; SIGKILL can never be caught, blocked or ignored
        cmp eax, SIGKILL
        je  .sst_kill

        ; SIGCONT resumes a stopped task and is otherwise a no-op
        cmp eax, SIGCONT
        je  .sst_cont

        ; SIGTSTP stops the task unless a handler wants it
        cmp eax, SIGTSTP
        je  .sst_tstp

        ; SIGCHLD's default action is "ignore": never tear a task down for it
        cmp eax, SIGCHLD
        je  .sst_chld

        mov ebx, [edi + TCB_SIG_HAND]
        test ebx, ebx
        jz  .sst_kill
        mov ecx, [ebx + eax * 4]
        cmp ecx, SIG_DFL
        je  .sst_kill
        cmp ecx, SIG_IGN
        je  .sst_done
        jmp .sst_queue             ; a real handler: queue for delivery

.sst_cont:
        cmp dword [edi + TCB_STATE], TASK_STOPPED
        jne .sst_done
        mov dword [edi + TCB_STATE], TASK_READY
        jmp .sst_done

.sst_tstp:
        mov ebx, [edi + TCB_SIG_HAND]
        test ebx, ebx
        jz  .sst_stop
        mov ecx, [ebx + SIGTSTP * 4]
        cmp ecx, SIG_DFL
        jne .sst_queue             ; handler (or SIG_IGN) decides at delivery
.sst_stop:
        ; Ignore a stop request aimed at the caller: it would never resume
        cmp dword [edi + TCB_STATE], TASK_RUNNING
        je  .sst_done
        mov dword [edi + TCB_STATE], TASK_STOPPED
        jmp .sst_done

.sst_chld:
        ; Only a real handler is worth queueing; SIG_DFL/SIG_IGN mean nothing
        ; happens to the task.
        mov ebx, [edi + TCB_SIG_HAND]
        test ebx, ebx
        jz  .sst_done
        mov ecx, [ebx + SIGCHLD * 4]
        cmp ecx, SIG_IGN
        je  .sst_done
        cmp ecx, SIG_DFL
        je  .sst_done
        jmp .sst_queue

.sst_queue:
        mov edi, [.sst_tcb]
        mov eax, [.sst_sig]
        bts dword [edi + TCB_SIG_PEND], eax
        cmp dword [edi + TCB_STATE], TASK_BLOCKED
        jne .sst_done
        mov dword [edi + TCB_STATE], TASK_READY
        jmp .sst_done

.sst_kill:
        mov edi, [.sst_tcb]

        ; Destroy the target's windows/surfaces.
        mov eax, [edi + TCB_PID]
        call gui_cleanup_pid

        ; Free the signal handler table page (allocated lazily by sigaction)
        mov eax, [edi + TCB_SIG_HAND]
        test eax, eax
        jz .sst_no_hand
        mov dword [edi + TCB_SIG_HAND], 0
        call pmm_free_page
.sst_no_hand:

        ; Free the kernel stack page
        mov eax, [edi + TCB_KSTACK]
        test eax, eax
        jz .sst_free_fd
        sub eax, 4096
        call pmm_free_page

.sst_free_fd:
        ; Free private fd table page
        mov eax, [edi + TCB_FD_TABLE_PTR]
        test eax, eax
        jz .sst_free_pd
        call pmm_free_page
        mov dword [edi + TCB_FD_TABLE_PTR], 0

.sst_free_pd:
        ; Queue the page directory (see sched_exit_task: it may be the active one)
        mov eax, [edi + TCB_CR3]
        test eax, eax
        jz .sst_clear
        mov dword [edi + TCB_CR3], 0
        mov [pd_deferred_free], eax

.sst_clear:
        ; Clear the TCB. A zombie was already counted out of task_count by
        ; sched_exit_task, so decrementing again would undercount live tasks.
        cmp dword [edi + TCB_STATE], TASK_ZOMBIE
        je .sst_clear_uncounted
        dec dword [task_count]
.sst_clear_uncounted:
        mov dword [edi + TCB_STATE], TASK_FREE
        mov dword [edi + TCB_ESP], 0
        mov dword [edi + TCB_KSTACK], 0

.sst_done:
        popad
        ret
.sst_tcb: dd 0
.sst_sig: dd 0

;---------------------------------------
; sched_kbd_default_terminate - would this signal's default action kill?
;   In : EDI = TCB, EAX = signal number
;   Out: EAX = 1  SIG_DFL (or no handler table) means "terminate"
;              = 0  a real handler, SIG_IGN, or a non-terminating default
;   Preserves: EBX, ECX, EDI
;---------------------------------------
sched_kbd_default_terminate:
        push ebx
        push ecx
        cmp eax, SIGKILL
        je .kdt_yes
        cmp eax, SIGCONT
        je .kdt_no
        cmp eax, SIGTSTP
        je .kdt_no
        cmp eax, SIGCHLD
        je .kdt_no
        mov ebx, [edi + TCB_SIG_HAND]
        test ebx, ebx
        jz .kdt_yes
        mov ecx, [ebx + eax * 4]
        cmp ecx, SIG_DFL
        je .kdt_yes
.kdt_no:
        xor eax, eax
        jmp .kdt_done
.kdt_yes:
        mov eax, 1
.kdt_done:
        pop ecx
        pop ebx
        ret

;---------------------------------------
; sched_kbd_signal - deliver a keyboard signal to the foreground job
;   In : EAX = signal number
;   Out: EAX = 1  the interrupting context survives (a handler was queued, or
;                 it was not a target at all) — just return to it
;        EAX = 0  the foreground context is gone (killed or stopped) and the
;                 caller must hand the console back to the shell
;
; A SIG_DFL terminate signal aimed at the interrupted task is not queued: it
; stays RUNNING and the caller's abort path tears it down from the shared
; kernel stack. Queueing it would leave the timer to free the very stack the
; task is executing on.
;
; Ctrl+C/Ctrl+Z belong to the terminal, not to "whatever task happens to be
; running": they go to the terminal's foreground process group (tty_fg_pgid),
; so a pipeline is signalled as a unit. When no group is set, the running task
; is the target (a program launched before any group was established).
;
; The TCB that was interrupted is left in sched_kbd_tcb so the caller can save
; its fd view before restoring the shell's.
;---------------------------------------
sched_kbd_signal:
        pushad
        mov [.ksf_sig], eax
        mov dword [.ksf_self_dfl], 0

        ; Remember who we interrupted (may be 0 in the shell/legacy context)
        call sched_get_current_task
        mov [sched_kbd_tcb], eax
        mov [.ksf_cur], eax

        mov ecx, [tty_fg_pgid]
        cmp ecx, -1
        je .ksf_single
        test ecx, ecx
        js  .ksf_single              ; any other negative value: no group

        ; --- Whole foreground process group ---
        mov dword [.ksf_hits], 0
        mov edi, task_table
        xor edx, edx
.ksf_lock:
        cmp edx, MAX_TASKS
        jge .ksf_lock_end
        cmp dword [edi + TCB_STATE], TASK_FREE
        je  .ksf_lock_next
        cmp [edi + TCB_PGID], ecx
        jne .ksf_lock_next
        inc dword [.ksf_hits]
        cmp edi, [.ksf_cur]
        jne .ksf_lock_send
        mov eax, [.ksf_sig]
        call sched_kbd_default_terminate
        test eax, eax
        jz .ksf_lock_send
        mov dword [.ksf_self_dfl], 1
        jmp .ksf_lock_next
.ksf_lock_send:
        mov eax, [.ksf_sig]
        call sched_signal_task
.ksf_lock_next:
        add edi, TCB_SIZE
        inc edx
        jmp .ksf_lock
.ksf_lock_end:
        ; A group that no longer exists (its leader is gone, or the shell never
        ; claimed one) must not swallow the keystroke.
        cmp dword [.ksf_hits], 0
        jne .ksf_verdict
        jmp .ksf_single

.ksf_single:
        ; --- No group: target the running task, if any ---
        mov edi, [.ksf_cur]
        test edi, edi
        jz  .ksf_verdict             ; shell context: nothing to signal
        mov eax, [.ksf_sig]
        call sched_kbd_default_terminate
        test eax, eax
        jz .ksf_single_send
        mov dword [.ksf_self_dfl], 1
        jmp .ksf_verdict
.ksf_single_send:
        mov eax, [.ksf_sig]
        call sched_signal_task

.ksf_verdict:
        cmp dword [.ksf_self_dfl], 0
        jne .ksf_gone                ; default action: caller aborts the task
        ; Can we return to the interrupted context? Only if it still belongs to
        ; a task that is runnable. A task destroyed by a default action is FREE;
        ; a stopped one is TASK_STOPPED. Either way the console goes back to the
        ; shell.
        mov eax, [.ksf_cur]
        test eax, eax
        jz .ksf_no_task
        cmp dword [eax + TCB_STATE], TASK_FREE
        je  .ksf_gone
        cmp dword [eax + TCB_STATE], TASK_STOPPED
        je  .ksf_gone
        mov dword [.ksf_kept], 1
        jmp .ksf_done
.ksf_no_task:
        ; No task (shell or legacy direct-IRET launch): the caller decides. A
        ; legacy program still needs Ctrl+C to abort, and it has no TCB.
        mov dword [.ksf_kept], 0
        jmp .ksf_done
.ksf_gone:
        mov dword [.ksf_kept], 0
.ksf_done:
        mov eax, [.ksf_kept]
        mov [esp + 28], eax          ; EAX of the pushad frame = return value
        popad
        ret
.ksf_sig:      dd 0
.ksf_cur:      dd 0
.ksf_kept:     dd 0
.ksf_hits:     dd 0
.ksf_self_dfl: dd 0

;---------------------------------------
; sys_kill - send a signal to a task or a process group
;   EBX = pid (> 0) or -pgid (< 0)
;   ECX = signal number; anything outside 1..NUM_SIGNALS-1 means SIGTERM
; Returns EAX = 0 if at least one target was signalled, -1 otherwise
;
; Group signalling (-pgid) is what a job-control shell needs to stop a whole
; pipeline. The old ABI only took EBX (pid) and destroyed the task outright;
; callers that leave ECX unset still get SIGTERM and the same visible result.
;---------------------------------------
sys_kill:
        pushad
        mov dword [.sk_sig], SIGTERM
        cmp ecx, 1
        jb  .sk_sig_ready
        cmp ecx, NUM_SIGNALS
        jae .sk_sig_ready
        mov [.sk_sig], ecx
.sk_sig_ready:
        mov dword [.sk_found], 0

        mov edi, task_table
        xor edx, edx
.sk_scan:
        cmp edx, MAX_TASKS
        jge .sk_scan_done
        cmp dword [edi + TCB_STATE], TASK_FREE
        je  .sk_next
        test ebx, ebx
        js  .sk_match_group
        cmp [edi + TCB_PID], ebx
        jne .sk_next
        jmp .sk_hit
.sk_match_group:
        mov eax, ebx
        neg eax                       ; EBX = -pgid -> EAX = pgid
        cmp [edi + TCB_PGID], eax
        jne .sk_next
.sk_hit:
        inc dword [.sk_found]
        mov eax, [.sk_sig]
        call sched_signal_task
.sk_next:
        add edi, TCB_SIZE
        inc edx
        jmp .sk_scan

.sk_scan_done:
        cmp dword [.sk_found], 0
        je .sk_fail
        mov dword [esp + 28], 0    ; Return EAX=0
        popad
        iretd

.sk_fail:
        mov dword [esp + 28], -1   ; Return EAX=-1
        popad
        iretd

.sk_sig:   dd 0
.sk_found: dd 0

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
        mov eax, ebx
        call sched_apply_cr3
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
;
;   When EBX is the task whose kernel stack this call is running on, set
;   sched_sig_self = 1 first. A default terminate action then leaves the
;   stack page allocated (TCB_KSTACK stays set) and the caller must leave via
;   sched_self_leave, which switches stacks and frees it. The flag is cleared
;   before this function returns.
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
        cmp ecx, SIGCHLD
        je  .sdel_next              ; POSIX: SIGCHLD's default action is ignore.
                                    ; sched_exit_task queues SIGCHLD on every
                                    ; parent, so terminating here killed any
                                    ; parent that yielded after a child died.

        ; All others (SIGKILL, SIGTERM, SIGINT, SIGALRM, etc.): terminate
        ; Destroy the task's windows/surfaces before recycling the TCB.
        mov eax, [ebx + TCB_PID]
        call gui_cleanup_pid
        ; Free the kernel stack page if allocated. A self-delivery runs on that
        ; very page, so it must stay allocated until the caller has switched
        ; away; sched_self_leave releases it there.
        cmp dword [sched_sig_self], 0
        jne .sdel_no_kstack
        mov eax, [ebx + TCB_KSTACK]
        test eax, eax
        jz  .sdel_no_kstack
        sub eax, 4096
        call pmm_free_page
.sdel_no_kstack:
        ; Queue the task's page directory for release at the next CR3 switch
        mov eax, [ebx + TCB_CR3]
        test eax, eax
        jz .sdel_no_pd
        mov dword [ebx + TCB_CR3], 0
        mov [pd_deferred_free], eax
.sdel_no_pd:
        ; Free private fd table page if allocated (always check, independent of kstack)
        mov eax, [ebx + TCB_FD_TABLE_PTR]
        test eax, eax
        jz .sdel_no_fdpage
        call pmm_free_page
        mov dword [ebx + TCB_FD_TABLE_PTR], 0
.sdel_no_fdpage:
        ; Free the signal handler table page
        mov eax, [ebx + TCB_SIG_HAND]
        test eax, eax
        jz .sdel_term_clear
        mov dword [ebx + TCB_SIG_HAND], 0
        call pmm_free_page
.sdel_term_clear:
        mov dword [ebx + TCB_STATE],  TASK_FREE
        mov dword [ebx + TCB_ESP],    0
        ; Keep TCB_KSTACK for a self-delivery: the caller frees the page after
        ; it has moved off this stack (see sched_self_leave).
        cmp dword [sched_sig_self], 0
        jne .sdel_keep_kstack
        mov dword [ebx + TCB_KSTACK], 0
.sdel_keep_kstack:
        dec dword [task_count]
        mov dword [sched_sig_self], 0
        mov eax, -1
        ret                             ; EAX = -1 : task killed

.sdel_stop:
        mov dword [ebx + TCB_STATE], TASK_STOPPED
        ; Fall through to sdel_ok (task stopped, not killed)
.sdel_ok:
        mov dword [sched_sig_self], 0
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

; sys_signal - Send a signal to a task (kill(pid, sig) rules)
;   EBX = target PID, ECX = signal number
; Returns: EAX = 0 on success, -1 on fail
;
; Kept as its own syscall number because the shell's kill/bg/fg builtins use
; it, but the work is done by sys_kill: a single implementation keeps handler
; dispatch, process groups, SIGCONT/SIGTSTP and teardown identical.
;---------------------------------------
sys_signal:
        jmp sys_kill

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
; sys_tcsetpgrp - set the terminal's foreground process group
;   EBX = PGID, or -1 to hand the console back with no owner
; Returns: EAX = 0 on success, -1 if no live task belongs to that group
;
; The terminal (the console) is the object that decides where Ctrl+C and
; Ctrl+Z go. Job control needs exactly this: the shell puts a job's group in
; the foreground, and the keyboard then signals the whole job.
;---------------------------------------
sys_tcsetpgrp:
        pushad
        cmp ebx, -1
        je  .tsp_clear

        ; Only accept a group that exists: otherwise Ctrl+C would be a no-op
        ; forever after a typo.
        mov edi, task_table
        xor edx, edx
.tsp_scan:
        cmp edx, MAX_TASKS
        jge .tsp_fail
        cmp dword [edi + TCB_STATE], TASK_FREE
        je  .tsp_next
        cmp [edi + TCB_PGID], ebx
        je  .tsp_set
.tsp_next:
        add edi, TCB_SIZE
        inc edx
        jmp .tsp_scan

.tsp_set:
        mov [tty_fg_pgid], ebx
        mov dword [esp + 28], 0
        popad
        iretd
.tsp_clear:
        mov dword [tty_fg_pgid], -1
        mov dword [esp + 28], 0
        popad
        iretd
.tsp_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;---------------------------------------
; sys_tcgetpgrp - read the terminal's foreground process group
; Returns: EAX = PGID (-1 when no job owns the console)
;---------------------------------------
sys_tcgetpgrp:
        mov eax, [tty_fg_pgid]
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
; sched_apply_cr3 - install an address space
;   In : EAX = TCB pointer, or 0 for the kernel/shell (master directory)
; Clobbers EAX and EFLAGS only.
;
; Always installs a valid directory: after a task has exited its directory is no
; longer safe to run on, so "no task" means the master. This also releases any
; page directory queued in pd_deferred_free by a teardown path - that can only
; be done once CR3 no longer points at it, which is exactly here.
;---------------------------------------
sched_apply_cr3:
        test eax, eax
        jz   .sac_master
        mov  eax, [eax + TCB_CR3]
        test eax, eax
        jz   .sac_master
        mov  cr3, eax
        jmp  .sac_release
.sac_master:
        mov  eax, PAGE_DIR_ADDR
        mov  cr3, eax
.sac_release:
        mov  eax, [pd_deferred_free]
        test eax, eax
        jz   .sac_done
        mov  dword [pd_deferred_free], 0
        call pmm_free_page
.sac_done:
        ret

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
        ; Find RUNNING task and read TCB_PPID. Same register contract as
        ; sys_getpid: EDI/ECX are scratch, so preserve them for the caller.
        push ecx
        push edi
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
        cmp eax, -1             ; no parent → report 0 (shell/init)
        jne .gpp_ret
        xor eax, eax
.gpp_ret:
        pop edi
        pop ecx
        iretd
.gpp_shell:
        xor eax, eax
        pop edi
        pop ecx
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

        ;--- 4. Child user stack: shared with the parent, copy-on-write ---
        ; fork() no longer allocates and copies 64 KB: cow_fork_share marks the
        ; parent's frames read-only and lets the first write in either process
        ; take a private copy. The child's ESP3 and TCB_USTACK are the parent's
        ; (the TCB copy and frame copy below already inherit them).

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
        ; ESP3 needs no patch any more: the child runs on the parent's stack
        ; (shared copy-on-write), so the copied frame is already correct.
        mov  edi, [fork_child_kesp]
        mov  dword [edi + 28], 0    ; child EAX = 0

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
        mov  eax, [fork_child_kesp]
        mov  [edi + TCB_ESP],    eax
        ; TCB_USTACK is deliberately not touched: the TCB copy above already
        ; carries the parent's stack top, which the copy-on-write share below
        ; makes correct for the child.
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

        ;--- 8a. Child gets its own page directory ---
        ; The TCB copy above inherited the parent's TCB_CR3, but the directory
        ; itself must be cloned from the PARENT (not the master): if the parent
        ; already exec'd, its private program tables are the child's image.
        push edi
        mov eax, [edi + TCB_CR3]
        call paging_clone_pd
        pop edi
        test eax, eax
        jz   .fork_fail_pd
        mov  [edi + TCB_CR3], eax

        ;--- 8a-bis. Share the parent's user frames copy-on-write ---
        ; Both directories keep pointing at the same frames, marked read-only;
        ; the first write in either process copies its own private frame.
        push edi
        mov  eax, [edi + TCB_CR3]
        call cow_fork_share             ; EAX = 0 ok, -1 OOM
        pop  edi
        test eax, eax
        js   .fork_fail_cow

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
        ; Copy the parent's live fd view (legacy + VFD) into the child's page
        push eax                        ; save child fd page addr
        push edi
        mov  edi, eax
        mov  esi, fd_table
        mov  ecx, (FD_MAX * FD_ENTRY_SIZE) / 4
        cld
        rep  movsd
        mov  esi, vfs_fd_table
        mov  ecx, FD_PAGE_VFD_BYTES / 4
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

.fork_fail_pd:
        ; Could not allocate the child's page directory. The TCB currently holds
        ; the PARENT's PD (inherited by the copy): zero it so nothing frees the
        ; parent's directory, free the slot, and release the kernel stack.
        mov  edi, [fork_child_tcb]
        mov  dword [edi + TCB_CR3], 0
        mov  dword [edi + TCB_STATE], TASK_FREE
        mov  eax, [fork_child_kstop]
        sub  eax, 4096
        call pmm_free_page
        mov  dword [esp + 28], -1
        popad
        iretd

.fork_fail_cow:
        ; cow_fork_share could not privatise a page table (OOM). No PTE was
        ; touched, so the child directory can simply be released.
        mov  edi, [fork_child_tcb]
        mov  eax, [edi + TCB_CR3]
        call paging_free_pd
        mov  dword [edi + TCB_CR3], 0
        mov  dword [edi + TCB_STATE], TASK_FREE
        mov  eax, [fork_child_kstop]
        sub  eax, 4096
        call pmm_free_page
        mov  dword [esp + 28], -1
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
