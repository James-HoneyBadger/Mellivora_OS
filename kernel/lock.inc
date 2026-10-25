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
; placed between two existing ones without renumbering either. The frame
; allocator sits at the top because almost every subsystem may need a frame
; while holding its own; nothing it does takes a lock, so it can be last.
RANK_BLOCKCACHE equ 100                ; block cache and its device
RANK_VFS        equ 200                ; virtual filesystem tables
RANK_SCHED      equ 300                ; run queue and task table
RANK_PMM        equ 400                ; frame allocator
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
cpu_held_flags:     times SMP_MAX_CPUS * MAX_HELD_LOCKS dd 0

;-----------------------------------------------------------------------
; lock_self_ipi - which processor is asking?
;   Out: EAX = this processor's slot, 0 for the bootstrap processor
;
; The slot is claimed once, atomically, the first time a processor asks, and
; remembered against its APIC ID. Deriving it from the firmware's list instead
; looked simpler and was wrong: anything that makes two processors resolve to
; the same slot - a table that was not what it claimed, an ID it did not list -
; gives them one lock stack and one lock stack is a corruption, not a warning.
; A claim cannot collide: slots run out only past SMP_MAX_CPUS processors, and
; smp_start_aps never wakes that many.
;-----------------------------------------------------------------------
cpu_slot_next:      dd 1                      ; 0 belongs to the first caller
cpu_slot_by_apic:   times 256 db 0xFF         ; 0xFF = not yet claimed

lock_self_ipi:
        push ecx
        push edx
        push esi
        cmp byte [irq_mode_apic], 1
        jne .lsi_single                 ; no APIC: the LAPIC page is not mapped
        mov ecx, LAPIC_ID
        call lapic_read
        shr eax, 24                     ; this processor's APIC ID
        and eax, 255
        mov esi, eax
        movzx edx, byte [cpu_slot_by_apic + esi]
        cmp edx, 0xFF
        jne .lsi_have

        ; First time this processor has asked: take the next slot. Slots run
        ; 0..SMP_MAX_CPUS-1 and smp_start_aps wakes at most SMP_MAX_CPUS-1
        ; processors, so the claim never runs out; if it ever did, clamping is
        ; safer than wrapping onto a slot another processor already owns.
        mov edx, 1
        lock xadd dword [cpu_slot_next], edx    ; EDX = the slot claimed
        cmp edx, SMP_MAX_CPUS
        jb .lsi_bounded
        mov edx, SMP_MAX_CPUS - 1
.lsi_bounded:
        mov eax, edx
        mov [cpu_slot_by_apic + esi], al
.lsi_have:
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
        ; The slot lives in a register, not in a variable: a static "local"
        ; here would be shared by every processor at once, and two processors
        ; exchanging slots corrupt exactly the bookkeeping that is meant to
        ; catch corruption. pushad has saved EBP, so it is free to hold.
        call lock_self_ipi
        mov ebp, eax

        ; Depth limit: a lock taken deeper than the record allows is a bug too.
        mov eax, ebp
        mov ecx, [cpu_held_count + eax*4]
        cmp ecx, MAX_HELD_LOCKS
        jae .sl_refuse

        ; Order check: strictly greater than everything already held.
        mov edx, [edi + LK_RANK]
        xor ebx, ebx
        test ecx, ecx
        jz .sl_order_ok
.sl_scan:
        mov eax, ebp
        imul eax, MAX_HELD_LOCKS
        add eax, ebx
        mov esi, [cpu_held_rank + eax*4]
        cmp edx, esi
        jbe .sl_refuse                  ; equal ranks are refused too: that is a
        inc ebx                         ; processor waiting for itself
        cmp ebx, ecx
        jb .sl_scan
.sl_order_ok:

        ; From here until the release this processor takes no interrupts. A
        ; lock the kernel takes can also be wanted by an interrupt handler on
        ; the same processor - the timer handler walks scheduler lists - and a
        ; processor that interrupts itself while holding a lock deadlocks on
        ; itself with no second processor involved.
        pushfd
        cli

        ; Acquire. Compare-and-swap from free to held; a held lock is retried.
        mov ecx, 1
.sl_try:
        xor eax, eax
        lock cmpxchg [edi + LK_LOCKED], ecx
        jz .sl_got
        jmp .sl_try
.sl_got:
        mov eax, ebp
        mov [edi + LK_OWNER], eax

        ; Record it on this processor's stack of held locks.
        mov ecx, [cpu_held_count + eax*4]
        mov ebx, eax
        imul ebx, MAX_HELD_LOCKS
        add ebx, ecx
        mov esi, [edi + LK_RANK]
        mov [cpu_held_rank + ebx*4], esi
        mov [cpu_held_lock + ebx*4], edi
        mov esi, [esp]                  ; the flags saved a few lines above
        mov [cpu_held_flags + ebx*4], esi
        inc ecx
        mov [cpu_held_count + eax*4], ecx

        add esp, 4                      ; drop the saved flags: they are recorded
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
        mov ebp, eax

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
        mov esi, [cpu_held_flags + ebx*4]
        mov dword [cpu_held_flags + ebx*4], 0

        ; Drop the owner first, then the held flag. A processor that wins the
        ; lock in between writes its own owner over the cleared one; clearing
        ; the flag first would let it take the lock and then have its ownership
        ; erased here, which its own release would report as an imbalance.
        mov dword [edi + LK_OWNER], 0
        mov dword [edi + LK_LOCKED], 0
        push esi
        popfd                           ; interrupts back to what the caller had
        mov dword [esp + 28], 0
        popad
        ret

.su_imbalance:
        mov [dbg_slot], eax
        mov ebx, [edi + LK_OWNER]
        mov [dbg_owner], ebx
        mov ebx, [edi + LK_LOCKED]
        mov [dbg_locked], ebx
        mov ebx, [cpu_held_count + eax*4]
        mov [dbg_count], ebx
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

dbg_slot:   dd 0
dbg_owner:  dd 0
dbg_locked: dd 0
dbg_count:  dd 0

lock_report_imbalance:
        push esi
        ; Facts first: which slot, who owns the lock, and how deep this
        ; processor's own record is.
        mov esi, msg_dbg_slot
        call serial_print
        mov eax, [dbg_slot]
        call serial_print_hex
        mov esi, msg_dbg_owner
        call serial_print
        mov eax, [dbg_owner]
        call serial_print_hex
        mov esi, msg_dbg_locked
        call serial_print
        mov eax, [dbg_locked]
        call serial_print_hex
        mov esi, msg_dbg_count
        call serial_print
        mov eax, [dbg_count]
        call serial_print_hex
        mov esi, msg_newline
        call serial_print
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

msg_dbg_slot:   db "LOCKDBG slot=0x", 0
msg_dbg_owner:  db " owner=0x", 0
msg_dbg_locked: db " locked=0x", 0
msg_dbg_count:  db " held=0x", 0
msg_newline:    db 10, 0
