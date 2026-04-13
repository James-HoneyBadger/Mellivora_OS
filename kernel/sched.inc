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

; Preemptive scheduling quantum (in PIT ticks, 100 Hz)
; 10 ticks = 100 ms per time slice
SCHED_QUANTUM equ 10

; TCB field offsets (32 bytes per entry)
TCB_ESP      equ 0          ; Saved kernel-mode ESP (pushad+iretd frame)
TCB_STATE    equ 4          ; Task state (FREE / READY / RUNNING)
TCB_KSTACK   equ 8          ; Kernel stack top (for TSS ESP0)
TCB_USTACK   equ 12         ; User-mode stack top
TCB_ENTRY    equ 16         ; Program entry point
TCB_PID      equ 20         ; Task ID
TCB_PAD1     equ 24         ; Reserved
TCB_PAD2     equ 28         ; Reserved
TCB_SIZE     equ 32

MAX_TASKS    equ 16

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
        mov eax, esi
        imul eax, TCB_SIZE
        lea ebx, [task_table + eax]
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
; ECX = pointer to 16-byte output buffer
; Output buffer: [state(4), pid(4), entry(4), esp(4)]
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
        mov dword [esp + 28], 0
        popad
        iretd
.spl_fail:
        mov dword [esp + 28], -1
        popad
        iretd
