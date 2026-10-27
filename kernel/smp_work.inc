;=======================================================================
; smp_work.inc - work for the other processors to do
;
; An application processor that only parks proves delivery; it earns its keep
; by running kernel work that the bootstrap processor does not have to. The
; shape is deliberately small: a queue of function addresses, protected by one
; ranked lock, with an inter-processor interrupt to wake a processor that is
; idling. A processor that finds nothing to do halts, which on a machine with a
; battery is the difference between a busy machine and an idle one.
;
; What belongs on this queue: kernel work with no user context - allocator
; churn, page and block cache maintenance, I/O completion. It is not a
; scheduler: user threads stay on the processor that owns the scheduler, and a
; job must not block, sleep, or wait for another job, because there is no other
; thread on that processor to run while it waits.
;
; Two rules keep it safe, and both are checked rather than assumed:
;
;   * the queue lock outranks nothing a job takes, so a job that takes the frame
;     allocator's lock cannot be holding the queue's - the rank gate refuses the
;     pair rather than deadlocking on it;
;   * the job is popped and run with the queue lock released, so a job that
;     takes a long time does not stop the other processors from being fed.
;=======================================================================

SMP_WORK_MAX        equ 64
SMP_IPI_VECTOR      equ 0xFB            ; "there is work", delivered as a fixed IPI
ICR_SHORTHAND_ALL   equ (3 << 18)       ; all processors except this one
ICR_MODE_FIXED      equ (0 << 8)

smp_work_ring:      times SMP_WORK_MAX dd 0
smp_work_head:      dd 0                ; next slot to write
smp_work_tail:      dd 0                ; next slot to take
smp_work_count:     dd 0
smp_work_push_fail: dd 0                ; refused because the ring was full
smp_jobs_run:       dd 0                ; jobs every processor has finished
smp_aps_idle:       dd 0                ; processors currently halted for work
smp_ipi_count:      dd 0
smp_stress_want:    dd 0                ; jobs the last stress asked for


align 16
smp_work_lock:
        dd 0                            ; LK_LOCKED
        dd RANK_SMP_WORK
        dd -1                           ; owner (unused until held)
        dd smp_work_lock_name
smp_work_lock_name: db "smp work queue", 0

;-----------------------------------------------------------------------
; smp_work_push - hand a job to whichever processor is free
;   In : EAX = job entry (called with no arguments, may clobber anything)
;   Out: EAX = 0 queued, -1 refused (ring full)
;
; Pushing is cheap and never blocks on a job: the queue lock is held for the
; few instructions it takes to advance two indices.
;-----------------------------------------------------------------------
smp_work_push:
        push ebx
        push ecx
        push edx
        push edi
        ; The job stays in a register. A static slot here would be shared by
        ; every processor that calls this - and a job that is written by one
        ; caller and read by another is a job that goes missing, which is the
        ; failure this queue spent two rounds of diagnosis finding.
        mov ebx, eax

        mov edi, smp_work_lock
        call spin_lock
        test eax, eax
        jnz .swp_full                   ; rank violation already reported

        mov eax, [smp_work_count]
        cmp eax, SMP_WORK_MAX
        jae .swp_unlock_full

        mov eax, [smp_work_head]
        mov [smp_work_ring + eax*4], ebx
        inc eax
        cmp eax, SMP_WORK_MAX
        jb .swp_no_wrap
        xor eax, eax
.swp_no_wrap:
        mov [smp_work_head], eax
        inc dword [smp_work_count]

        mov edi, smp_work_lock
        call spin_unlock

        ; Wake somebody. The interrupt does no work itself - it interrupts a
        ; halted processor, which then looks at the queue.
        call smp_ipi_all
        xor ebx, ebx                    ; 0 = queued
        jmp .swp_out

.swp_unlock_full:
        mov edi, smp_work_lock
        call spin_unlock
.swp_full:
        inc dword [smp_work_push_fail]
        mov ebx, -1
.swp_out:
        mov eax, ebx
        pop edi
        pop edx
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; smp_work_pop - take one job, if there is one
;   Out: EAX = job, or 0 when the queue is empty
;-----------------------------------------------------------------------
smp_work_pop:
        push ebx
        push ecx
        push edx
        push edi
        ; The result is returned in EBX, never through a static slot. Two
        ; processors pop concurrently, and a slot written under the lock but read
        ; after it is released is a slot the other processor can overwrite in
        ; between: one job runs twice and another is lost, the queue drains to
        ; empty, and every processor idles waiting for work that no longer exists.
        xor ebx, ebx

        mov edi, smp_work_lock
        call spin_lock
        test eax, eax
        jnz .swq_out

        cmp dword [smp_work_count], 0
        je .swq_unlock

        mov eax, [smp_work_tail]
        mov ebx, [smp_work_ring + eax*4]
        inc eax
        cmp eax, SMP_WORK_MAX
        jb .swq_no_wrap
        xor eax, eax
.swq_no_wrap:
        mov [smp_work_tail], eax
        dec dword [smp_work_count]
.swq_unlock:
        mov edi, smp_work_lock
        call spin_unlock
.swq_out:
        mov eax, ebx
        pop edi
        pop edx
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; smp_ipi_all - poke every other processor
;
; Sent to the "all excluding self" shorthand, so it costs one ICR write however
; many processors there are. A processor that is running a job takes the
; interrupt and carries on; one that is halted wakes and looks at the queue.
;-----------------------------------------------------------------------
smp_ipi_all:
        cmp byte [irq_mode_apic], 1
        jne .sia_done                   ; no local APIC: there is nobody to poke
        push ecx
        push eax
        ; Wait for any earlier IPI to be accepted before writing another.
.sia_wait:
        mov ecx, LAPIC_ICR_LOW
        call lapic_read
        test eax, ICR_DELIVERY_STATUS
        jz .sia_ready
        mov ecx, 1
        call smp_delay_ms
        jmp .sia_wait
.sia_ready:
        mov ecx, LAPIC_ICR_HIGH
        xor eax, eax                    ; destination ignored with shorthand
        call lapic_write
        mov ecx, LAPIC_ICR_LOW
        mov eax, ICR_SHORTHAND_ALL | ICR_MODE_FIXED | SMP_IPI_VECTOR
        call lapic_write
        inc dword [smp_ipi_count]
        pop eax
        pop ecx
.sia_done:
        ret

;-----------------------------------------------------------------------
; irq_ipi - the wake-up interrupt
;
; Nothing to do: the interrupt exists to stop a halted processor halting, and
; the loop it returns to reads the queue. Acknowledged like any APIC interrupt.
;-----------------------------------------------------------------------
irq_ipi:
        pushad
        inc dword [gs:CPU_IRQ_COUNT]
        xor al, al
        call irq_eoi
        popad
        iretd

;-----------------------------------------------------------------------
; smp_install_ipi - put the interrupt gate in place
;-----------------------------------------------------------------------
smp_install_ipi:
        push eax
        push ecx
        mov ecx, SMP_IPI_VECTOR
        mov eax, irq_ipi
        call idt_set_gate
        pop ecx
        pop eax
        ret

;-----------------------------------------------------------------------
; smp_ap_idle - what an application processor does with the rest of its life
;
; Run the queue, and halt when it is empty. Interrupts are on throughout: the
; halt is woken by the IPI that means work arrived, and a processor that
; disabled them would never be woken again.
;
; Never returns.
;-----------------------------------------------------------------------
smp_ap_idle:
.sai_loop:
        ; The check and the decision to halt have to be indivisible. Interrupts
        ; are on while a processor runs, so the interrupt that announces work
        ; can arrive *after* the queue was found empty and *before* the halt -
        ; and an interrupt whose handler does nothing then leaves the processor
        ; asleep with work queued behind it. That is a lost wakeup, and it shows
        ; up as a machine that stops answering under load, an hour in.
        ;
        ; So: interrupts off around the check, then the sti/hlt pair. STI
        ; inhibits delivery for exactly one instruction, so an interrupt that
        ; arrives in that window is taken after the halt - which is what wakes
        ; it. Nothing can slip between the two.
        cli
        call smp_work_pop
        test eax, eax
        jnz .sai_job

        inc dword [gs:CPU_SPINS]
        mov dword [gs:CPU_IDLE], 1
        lock inc dword [smp_aps_idle]
        sti
        hlt
        lock dec dword [smp_aps_idle]
        mov dword [gs:CPU_IDLE], 0
        jmp .sai_loop

.sai_job:
        mov dword [gs:CPU_IDLE], 0
        sti                             ; jobs may take interrupts, and must
        call eax
        inc dword [gs:CPU_JOBS]
        lock inc dword [smp_jobs_run]
        jmp .sai_loop

;-----------------------------------------------------------------------
; smp_job_stress - one unit of allocator work, as a queued job
;
; The same round the boot-time stress runs, wrapped so it can be handed to a
; processor through the queue. It proves the queue carries real work: the
; allocator is taken and given back from every processor at once.
;-----------------------------------------------------------------------
smp_job_stress:
        call smp_stress_round
        ret

;-----------------------------------------------------------------------
; smp_spread_stress - queue `n` stress jobs and wait for them to finish
;   In : EAX = how many jobs
;   Out: EAX = jobs actually queued
;
; Used by the `smpstress` command, which is the time-compressed form of the
; wave's exit test: every processor doing allocator work while user programs
; run, and the machine still alive afterwards.
;-----------------------------------------------------------------------
smp_spread_stress:
        pushad
        mov [.sss_want], eax

        ; The baseline is taken *before* the first push, and the target is
        ; "baseline + what was actually pushed". Completing a job also raises
        ; the baseline count (any processor's job does), which can only make the
        ; target easier - never harder. Stopping the push loop on a full queue
        ; while still waiting for the full count was the deadlock this used to
        ; have: the wanted jobs were never queued, so the count could never
        ; arrive.
        mov eax, [smp_jobs_run]
        mov [.sss_start], eax
        mov dword [.sss_pushed], 0

.sss_push:
        mov eax, [.sss_pushed]
        cmp eax, [.sss_want]
        jae .sss_wait
        mov eax, smp_job_stress
        call smp_work_push
        test eax, eax
        jz .sss_counted
        ; The ring is full: let the processors drain a little, then try again.
        mov ecx, 200
        call smp_delay_tsc
        jmp .sss_push
.sss_counted:
        inc dword [.sss_pushed]
        jmp .sss_push

.sss_wait:
        mov eax, [.sss_start]
        add eax, [.sss_pushed]
        cmp [smp_jobs_run], eax
        jae .sss_out
        mov ecx, 1
        call smp_delay_ms
        ; Waiting for work to finish is not progress, but slow is not stuck: a
        ; job that is sixteen allocations and a megabyte of memory traffic takes
        ; a while under contention, and a diagnostic that shouts every tenth of
        ; a second would cry wolf on every round. Several seconds of *no
        ; completions at all* is the interesting case, and it says what the
        ; machine looks like: the queue empty with every processor idle means a
        ; lost wakeup, the queue full means a job that never returned.
        inc dword [.sss_spin]
        cmp dword [.sss_spin], 20000
        jne .sss_wait
        mov dword [.sss_spin], 0
        pushad
        mov esi, msg_work_stall
        call serial_print
        mov eax, [smp_work_head]
        call serial_print_dec
        mov esi, msg_work_tail
        call serial_print
        mov eax, [smp_work_tail]
        call serial_print_dec
        mov esi, msg_work_count
        call serial_print
        mov eax, [smp_work_count]
        call serial_print_dec
        mov esi, msg_work_jobs
        call serial_print
        mov eax, [smp_jobs_run]
        call serial_print_dec
        mov esi, msg_work_idle
        call serial_print
        mov eax, [smp_aps_idle]
        call serial_print_dec
        mov esi, msg_work_ipis
        call serial_print
        mov eax, [smp_ipi_count]
        call serial_print_dec
        mov al, 10
        call serial_putchar
        popad
        jmp .sss_wait
.sss_out:
        mov eax, [.sss_pushed]
        mov [esp + 28], eax
        popad
        ret
.sss_want:   dd 0
.sss_start:  dd 0
.sss_pushed: dd 0
.sss_spin:   dd 0

msg_work_stall: db "SMP_WORK_STALL head=", 0
msg_work_tail:  db " tail=", 0
msg_work_count: db " count=", 0
msg_work_jobs:  db " jobs=", 0
msg_work_idle:  db " idle=", 0
msg_work_ipis:  db " ipis=", 0
