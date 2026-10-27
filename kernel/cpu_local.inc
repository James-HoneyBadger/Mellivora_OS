;=======================================================================
; cpu_local.inc - the state that belongs to one processor, not to the machine
;
; Everything a processor needs to know about itself looked up from a global
; until now, which is correct exactly as long as there is one processor. The
; moment there are several, "the current task" and "how deep am I in the
; kernel" are questions whose answer depends on who is asking.
;
; The answer lives behind a segment base: GS. Each processor loads the address
; of its own block into IA32_GS_BASE, and from then on `[gs:CPU_X]` is that
; processor's field with no lookup, no lock, and no arithmetic - the address
; translation does the work. The alternative, an array indexed by CPU number,
; needs the CPU number first, and reading that is the same puzzle.
;
; The blocks are static and their addresses never change, so a block's address
; is also its identity: CPU_SELF lets code learn which processor it is running
; on without asking the APIC.
;=======================================================================

IA32_GS_BASE        equ 0xC0000101      ; MSR: the GS base for this processor

CPU_ID              equ 0               ; dd: processor index, same as the lock slot
CPU_SELF            equ 4               ; dd: address of this block
CPU_ONLINE          equ 8               ; dd: 1 once the processor is taking work
CPU_IDLE            equ 12              ; dd: 1 while this processor has no work
CPU_PREEMPT         equ 16              ; dd: nested disable count
CPU_JOBS            equ 20              ; dd: work items this processor has run
CPU_SPINS           equ 24              ; dd: idle loop passes with nothing to do
CPU_STACK_TOP       equ 28              ; dd: this processor's idle stack top
CPU_IRQ_COUNT       equ 32              ; dd: interrupts taken here
CPU_WORK_CLOCK      equ 36              ; dd: TSC-free counter of work seen

CPU_BLOCK_SIZE      equ 64
CPU_MAX             equ SMP_MAX_CPUS

; One block per processor, 64-byte aligned so a block never straddles a cache
; line with its neighbour: two processors updating their own counters must not
; ping-pong the same line, or the accounting costs more than the work.
align 64
cpu_blocks:         times CPU_MAX * CPU_BLOCK_SIZE db 0

;-----------------------------------------------------------------------
; cpu_local_init - point this processor's GS at its block
;   In : EAX = processor index
;   Out: EAX = the block address, or 0 if the index is out of range
;
; Called once per processor, after the slot is known. The block is zeroed first
; so a recycled slot cannot inherit counters from whatever ran on it before.
;-----------------------------------------------------------------------
cpu_local_init:
        pushad
        cmp eax, CPU_MAX
        jae .cli_bad
        mov ebx, eax
        shl eax, 6                      ; CPU_BLOCK_SIZE = 64
        lea edi, [cpu_blocks + eax]
        mov [.cli_block], edi

        ; Zero it, then fill it: a stale CPU_ONLINE from a previous machine
        ; state would make every other processor think this one is working.
        mov ecx, CPU_BLOCK_SIZE / 4
        xor eax, eax
        cld
        rep stosd

        mov edi, [.cli_block]
        mov [edi + CPU_ID], ebx
        mov [edi + CPU_SELF], edi
        mov dword [edi + CPU_ONLINE], 1

        ; IA32_GS_BASE = the block
        mov ecx, IA32_GS_BASE
        mov eax, edi
        xor edx, edx
        wrmsr

        mov eax, edi
        mov [esp + 28], eax
        popad
        ret
.cli_bad:
        mov dword [esp + 28], 0
        popad
        ret
.cli_block: dd 0

;-----------------------------------------------------------------------
; cpu_id - which processor is asking?
;   Out: EAX = processor index
;
; Reads GS, so it is correct on any processor and costs one instruction. The
; lock package has its own answer (lock_self_ipi) for the boot window before
; GS is set up; this is the steady-state one.
;-----------------------------------------------------------------------
cpu_id:
        mov eax, [gs:CPU_ID]
        ret

;-----------------------------------------------------------------------
; cpu_gs_loaded - has this processor's GS base been set?
;   Out: EAX = 1 or 0
;
; Reading an MSR that was never written returns zero, and a null GS base means
; [gs:...] points at the interrupt vector table - so anything that touches the
; per-CPU block first asks.
;-----------------------------------------------------------------------
cpu_gs_loaded:
        push ecx
        push edx
        mov ecx, IA32_GS_BASE
        rdmsr
        or eax, edx
        setnz al
        movzx eax, al
        pop edx
        pop ecx
        ret
