;=======================================================================
; dpc.inc - deferred procedure calls (M1)
;
; An interrupt handler has to be short and must not sleep, but some work that
; belongs to an interrupt can do neither: it may take a while, and it may want
; to run after the handler has finished so it cannot lengthen the critical
; section it interrupted. A deferred procedure call is the standard answer -
; the handler queues a routine, and the queue is drained once interrupts are
; back in a state where running arbitrary kernel code is safe.
;
; This is the analogue SMP needs in M2: a processor told to do something by
; another processor has nowhere to do it until it takes an interrupt, and the
; queue is where that work waits.
;
;   dpc_queue   EAX = routine   - append; the routine runs with interrupts on
;   dpc_drain                   - run everything queued, in order
;
; The queue is FIFO and holds DPC_MAX entries. A queue that is full is a bug
; in the caller, not a condition to paper over, so it is counted and reported
; rather than silently dropping work.
;
; Routines run with interrupts enabled and are expected to be short. Nothing
; may block in one: the caller is usually an interrupt handler's tail.
;=======================================================================

%ifndef DPC_INC
%define DPC_INC

DPC_MAX         equ 16

;-----------------------------------------------------------------------
; dpc_queue - append a routine to the deferred queue
;   In : EAX = routine address
;   Out: EAX = 1 queued, 0 when the queue is full (counted in dpc_overflow)
;
; Safe to call from an interrupt handler: it touches the tail index and one
; slot, and disables interrupts only for those two stores.
;-----------------------------------------------------------------------
dpc_queue:
        pushad
        mov [.dq_routine], eax
        pushfd
        cli
        mov eax, [dpc_tail]
        mov ecx, [dpc_head]
        sub eax, ecx
        cmp eax, DPC_MAX
        jae .dq_full
        mov eax, [dpc_tail]
        and eax, DPC_MAX - 1            ; ring of DPC_MAX slots
        mov ecx, [.dq_routine]
        mov [dpc_slots + eax*4], ecx
        inc dword [dpc_tail]
        popfd
        mov dword [esp + 28], 1
        popad
        ret
.dq_full:
        inc dword [dpc_overflow]
        popfd
        mov dword [esp + 28], 0
        popad
        ret
.dq_routine: dd 0

;-----------------------------------------------------------------------
; dpc_drain - run everything queued, oldest first
;   Out: EAX = the number of routines run
;
; Called from the tail of the timer interrupt, where the processor is in a
; state fit to run kernel code. A pass is bounded by the queue's own depth, so
; a routine that queues another routine is served in the next pass rather than
; turning one interrupt into an unbounded loop.
;-----------------------------------------------------------------------
dpc_drain:
        pushad
        mov dword [.dd_ran], 0
        mov ebx, [dpc_tail]             ; the depth this pass will serve
.dd_next:
        mov eax, [dpc_head]
        cmp eax, ebx
        jae .dd_done
        and eax, DPC_MAX - 1
        mov eax, [dpc_slots + eax*4]
        inc dword [dpc_head]            ; dequeue before running, so a routine
        test eax, eax                   ; that queues is not run twice
        jz .dd_next
        inc dword [.dd_ran]
        push ebx
        call eax
        pop ebx
        jmp .dd_next
.dd_done:
        mov eax, [.dd_ran]
        mov [esp + 28], eax
        popad
        ret
.dd_ran: dd 0

;-----------------------------------------------------------------------
; dpc_init - empty the queue
;-----------------------------------------------------------------------
dpc_init:
        pushad
        mov dword [dpc_head], 0
        mov dword [dpc_tail], 0
        mov dword [dpc_overflow], 0
        popad
        ret

;-----------------------------------------------------------------------
; dpc_selftest - the queue runs what it was given, in the order it was given
;   Out: EAX = 1 pass, 0 fail
;
; Runs at boot, where a broken queue would otherwise show up as a timer that
; silently stopped doing something rather than as a failure.
;-----------------------------------------------------------------------
dpc_selftest:
        pushad
        mov dword [dpc_trace], 0
        call dpc_init
        mov eax, .dt_a
        call dpc_queue
        mov eax, .dt_b
        call dpc_queue
        mov eax, .dt_c
        call dpc_queue
        call dpc_drain
        ; A, then B, then C: 0x010203
        cmp dword [dpc_trace], 0x010203
        jne .dt_fail
        ; A second drain with nothing queued must do nothing.
        call dpc_drain
        cmp eax, 0
        jne .dt_fail
        cmp dword [dpc_trace], 0x010203
        jne .dt_fail
        mov dword [esp + 28], 1
        popad
        ret
.dt_fail:
        mov dword [esp + 28], 0
        popad
        ret
.dt_a:
        shl dword [dpc_trace], 8
        or  dword [dpc_trace], 1
        ret
.dt_b:
        shl dword [dpc_trace], 8
        or  dword [dpc_trace], 2
        ret
.dt_c:
        shl dword [dpc_trace], 8
        or  dword [dpc_trace], 3
        ret

%endif ; DPC_INC
