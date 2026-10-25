;=======================================================================
; lock.inc - spinlocks, and a gate that refuses to deadlock
;
; One processor never needed these. Several do: the moment two of them are in
; the kernel at once, anything shared - the scheduler's run queue, the frame
; allocator's free list, the block cache - is a race unless it is guarded.
;
; Two things live here. The first is the lock itself, a word and a compare-and-
; swap. The second matters more: every lock carries a *rank*, and a processor
; may only take a lock of higher rank than any it already holds. Two processors
; taking the same pair in opposite orders is the classic deadlock, and a rank
; order makes it impossible to express rather than merely unlikely. The check
; is on the acquiring side, where the mistake is written, so the report names
; both locks instead of leaving a hung machine.
;
; Ranks are declared next to the locks and the order between them is a
; decision, not an accident: a lock held across a call to another subsystem
; must outrank what that subsystem takes.
;=======================================================================

; Spinlock record
LK_LOCKED       equ 0                  ; dword: 0 free, 1 held
LK_RANK         equ 4                  ; dword: acquisition order
LK_OWNER        equ 8                  ; dword: processor index of the holder
LK_NAME         equ 12                 ; dword: pointer to a name for the report
LK_SIZE         equ 16

MAX_HELD_LOCKS  equ 8                  ; nesting depth per processor

; Ranks in use. Higher runs later. Gaps are deliberate: a new lock can be
; placed between two existing ones without renumbering either.
RANK_PMM        equ 100                ; frame allocator
RANK_BLOCKCACHE equ 200                ; block cache and its device
RANK_VFS        equ 300                ; virtual filesystem tables
RANK_SCHED      equ 400                ; run queue and task table
; Test locks live above everything, so the self-test cannot deadlock against
; a real lock by accident.
RANK_SELFTEST_LOW   equ 900
RANK_SELFTEST_HIGH  equ 910

; When set, an out-of-order acquisition is reported instead of halting. Only
; the self-test clears it; the kernel never runs with the gate disarmed.
lock_gate_armed:    db 1
lock_violations:    dd 0               ; inversions refused, for the report

cpu_held_count:     times SMP_MAX_CPUS dd 0
cpu_held_rank:      times SMP_MAX_CPUS * MAX_HELD_LOCKS dd 0
cpu_held_lock:      times SMP_MAX_CPUS * MAX_HELD_LOCKS dd 0

;-----------------------------------------------------------------------
; lock_self_ipi - which processor is asking?
;   Out: EAX = processor index, 0 for the bootstrap processor
;
; The index is a position in the MADT's list, found by matching this
; processor's APIC ID, so it is stable across a boot and needs no per-CPU
; register the hardware does not have.
;-----------------------------------------------------------------------
lock_self_ipi:
        push ecx
        push edx
        push esi
        cmp byte [irq_mode_apic], 1
        jne .lsi_single                 ; no APIC: the LAPIC page is not mapped
        mov ecx, LAPIC_ID
        call lapic_read
        shr eax, 24                     ; this processor's APIC ID
        xor edx, edx
        mov esi, acpi_cpu_apic_ids
.lsi_find:
        cmp edx, [acpi_cpu_count]
        jae .lsi_done                   ; not listed: treat as the first
        cmp al, [esi + edx]
        je .lsi_done
        inc edx
        jmp .lsi_find
.lsi_done:
        mov eax, edx
        pop esi
        pop edx
        pop ecx
        ret
.lsi_single:
        xor eax, eax
        pop esi
        pop edx
        pop ecx
        ret

;-----------------------------------------------------------------------
; spin_lock - take a lock, refusing to break the order
;   In : EDI = lock
;   Out: EAX = 0 acquired, -1 refused because it would invert the order
;
; A refusal is a bug in the caller, not a race: the same pair of ranks would
; invert on every run. When the gate is armed - always, outside the self-test -
; a refusal is reported and the machine stops rather than deadlocking later
; with no trace of why.
;-----------------------------------------------------------------------
spin_lock:
        pushad
        call lock_self_ipi
        mov [.sl_cpu], eax

        ; Depth limit: a lock taken deeper than the record allows is a bug too.
        mov ecx, [cpu_held_count + eax*4]
        cmp ecx, MAX_HELD_LOCKS
        jae .sl_refuse

        ; Order check: strictly greater than everything already held.
        mov edx, [edi + LK_RANK]
        xor ebx, ebx
        test ecx, ecx
        jz .sl_order_ok
.sl_scan:
        mov eax, [.sl_cpu]
        imul eax, MAX_HELD_LOCKS
        add eax, ebx
        mov esi, [cpu_held_rank + eax*4]
        cmp edx, esi
        jbe .sl_refuse                  ; equal ranks are refused too: that is a
        inc ebx                         ; processor waiting for itself
        cmp ebx, ecx
        jb .sl_scan
.sl_order_ok:

        ; Acquire. Compare-and-swap from free to held; a held lock is retried.
        mov ecx, 1
.sl_try:
        xor eax, eax
        lock cmpxchg [edi + LK_LOCKED], ecx
        jz .sl_got
        jmp .sl_try
.sl_got:
        mov eax, [.sl_cpu]
        mov [edi + LK_OWNER], eax

        ; Record it on this processor's stack of held locks.
        mov ecx, [cpu_held_count + eax*4]
        mov ebx, eax
        imul ebx, MAX_HELD_LOCKS
        add ebx, ecx
        mov esi, [edi + LK_RANK]
        mov [cpu_held_rank + ebx*4], esi
        mov [cpu_held_lock + ebx*4], edi
        inc ecx
        mov [cpu_held_count + eax*4], ecx

        mov dword [esp + 28], 0
        popad
        ret

.sl_refuse:
        inc dword [lock_violations]
        cmp byte [lock_gate_armed], 1
        jne .sl_return_error
        push edi
        push dword [edi + LK_NAME]
        call lock_report_inversion
        ; does not return
.sl_return_error:
        mov dword [esp + 28], -1
        popad
        ret
.sl_cpu: dd 0

;-----------------------------------------------------------------------
; spin_unlock - release a lock this processor holds
;   In : EDI = lock
;
; Releasing something not held - or held by another processor - is a bug and
; is reported as one rather than silently corrupting the bookkeeping.
;-----------------------------------------------------------------------
spin_unlock:
        pushad
        call lock_self_ipi
        mov [.su_cpu], eax

        cmp dword [edi + LK_LOCKED], 0
        je .su_imbalance
        cmp [edi + LK_OWNER], eax
        jne .su_imbalance

        ; Drop the top entry, which must be this lock: releases nest.
        mov ecx, [cpu_held_count + eax*4]
        test ecx, ecx
        jz .su_imbalance
        dec ecx
        mov ebx, eax
        imul ebx, MAX_HELD_LOCKS
        add ebx, ecx
        cmp [cpu_held_lock + ebx*4], edi
        jne .su_imbalance
        mov [cpu_held_count + eax*4], ecx
        mov dword [cpu_held_lock + ebx*4], 0
        mov dword [cpu_held_rank + ebx*4], 0

        mov dword [edi + LK_LOCKED], 0
        mov dword [edi + LK_OWNER], 0
        mov dword [esp + 28], 0
        popad
        ret

.su_imbalance:
        inc dword [lock_violations]
        cmp byte [lock_gate_armed], 1
        jne .su_return_error
        push edi
        push dword [edi + LK_NAME]
        call lock_report_imbalance
.su_return_error:
        mov dword [esp + 28], -1
        popad
        ret
.su_cpu: dd 0

;-----------------------------------------------------------------------
; lock_report_inversion - say which pair of locks inverted, and stop
;   In : [esp+4] = name of the lock that was refused
;
; Stops the machine. A deadlock found here is a bug with a stack trace; one
; found later is a hung machine with none.
;-----------------------------------------------------------------------
lock_report_inversion:
        push esi
        mov esi, msg_lock_inverted
        call serial_print
        mov esi, [esp + 8]
        test esi, esi
        jz .lri_done
        call serial_print
.lri_done:
        mov esi, msg_lock_stopping
        call serial_print
        mov esi, msg_lock_inverted
        call vga_print
        mov esi, [esp + 8]
        test esi, esi
        jz .lri_halt
        call vga_print
.lri_halt:
        cli
.halt_forever:
        hlt
        jmp .halt_forever

lock_report_imbalance:
        push esi
        mov esi, msg_lock_imbalance
        call serial_print
        mov esi, [esp + 8]
        test esi, esi
        jz .lrm_halt
        call serial_print
.lrm_halt:
        cli
.halt:
        hlt
        jmp .halt

msg_lock_inverted:  db "LOCK: acquisition would invert the order: ", 0
msg_lock_imbalance: db "LOCK: release does not match what this processor holds: ", 0
msg_lock_stopping:  db 10, "LOCK: stopping rather than deadlocking later", 10, 0

;=======================================================================
; The gate is only worth having if it is seen to say no, so it is exercised
; at boot: two test locks, taken in the wrong order and then the right one.
;=======================================================================
lock_selftest_ok:   db "LOCK_SELFTEST_OK", 10, 0
lock_selftest_bad:  db "LOCK_SELFTEST_FAIL", 10, 0

align 16
lock_test_high:
        dd 0
        dd RANK_SELFTEST_HIGH
        dd 0
        dd lock_test_high_name
lock_test_low:
        dd 0
        dd RANK_SELFTEST_LOW
        dd 0
        dd lock_test_low_name
lock_test_high_name: db "test-high", 0
lock_test_low_name:  db "test-low", 0

lock_selftest:
        pushad
        mov byte [lock_gate_armed], 0   ; refusals are answers here, not panics

        ; 1. take the higher-ranked lock
        mov edi, lock_test_high
        call spin_lock
        test eax, eax
        jnz .lst_fail

        ; 2. taking the lower-ranked one now must be refused
        mov edi, lock_test_low
        call spin_lock
        cmp eax, -1
        jne .lst_fail

        ; 3. taking the same lock again must be refused: a processor waiting
        ;    for itself is the same bug wearing a different hat
        mov edi, lock_test_high
        call spin_lock
        cmp eax, -1
        jne .lst_fail

        mov edi, lock_test_high
        call spin_unlock

        ; 4. in the right order both are granted, so the gate is not simply
        ;    refusing everything
        mov edi, lock_test_low
        call spin_lock
        test eax, eax
        jnz .lst_fail
        mov edi, lock_test_high
        call spin_lock
        test eax, eax
        jnz .lst_fail
        mov edi, lock_test_high
        call spin_unlock
        mov edi, lock_test_low
        call spin_unlock

        ; 5. releasing what is not held is refused
        mov edi, lock_test_low
        call spin_unlock
        cmp eax, -1
        jne .lst_fail

        mov byte [lock_gate_armed], 1
        mov dword [lock_violations], 0  ; the refusals above were the point
        mov esi, lock_selftest_ok
        call serial_print
        mov esi, lock_selftest_ok
        call vga_print
        popad
        ret
.lst_fail:
        mov byte [lock_gate_armed], 1
        mov esi, lock_selftest_bad
        call serial_print
        mov esi, lock_selftest_bad
        call vga_print
        popad
        ret
