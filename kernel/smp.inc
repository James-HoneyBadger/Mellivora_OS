;=======================================================================
; smp.inc - wake the other processors
;
; An application processor starts in real mode, at the physical page named by
; the SIPI vector the bootstrap processor sends it - it knows nothing about the
; kernel. So the kernel leaves a small blob of 16-bit code at a known page, and
; that blob is all an AP ever runs of "the machine's own" code: it builds a
; flat GDT, enters protected mode, loads the page directory, and calls into the
; kernel proper at a fixed address.
;
; What arrives in the kernel parks. This is deliberate: an AP that joined the
; scheduler would need per-CPU state, locking on everything the kernel shares,
; and a lock-order gate to prove the locking is right - each of which is its
; own increment. A parked AP proves the hard part (delivery, mode change,
; shared address space) without putting a second thread through code that was
; written assuming it is alone.
;
; The trampoline is position-dependent by construction: it is assembled to run
; at SMP_TRAMPOLINE_BASE, and every absolute reference inside it is expressed
; as that base plus an offset within the blob, so copying the bytes there is
; all that is needed.
;=======================================================================

SMP_TRAMPOLINE_BASE  equ 0x8000        ; SIPI vector 8; free low memory
SMP_AP_STACK_TEMP    equ 0x7A00        ; below the boot stack at 0x7C00
SMP_MAX_CPUS         equ 8
SMP_AP_TIMEOUT_MS    equ 300

LAPIC_ICR_LOW        equ 0x300         ; interrupt command register
LAPIC_ICR_HIGH       equ 0x310
ICR_DELIVERY_STATUS  equ (1 << 12)
ICR_LEVEL_ASSERT     equ (1 << 14)
ICR_MODE_INIT        equ (5 << 8)
ICR_MODE_SIPI        equ (6 << 8)

; APs that reached protected mode and parked. Written with a locked increment
; by each processor, read by the bootstrap processor.
smp_ap_online:      dd 0
smp_work_done:      dd 0
smp_stress_frames:  dd 0
smp_stress_errors:  dd 0
smp_worker_mask:    dd 0
smp_ap_expected:    dd 0
smp_bsp_apic_id:    db 0

; One stack per processor slot, claimed atomically by the processor itself so
; that a processor which arrives twice cannot share a stack with another.
align 16
ap_stacks: times SMP_MAX_CPUS * 4096 db 0

;-----------------------------------------------------------------------
; smp_delay_ms - busy-wait, timed by the PIT's counter
;   In : ECX = milliseconds
;
; The PIT is used rather than the APIC timer because the APIC timer's rate is
; measured, and this is needed before anything is trusted enough to measure it.
;-----------------------------------------------------------------------
smp_delay_ms:
        push eax
        push ebx
        push ecx
        push edx
        push esi
        mov ebx, PIT_FREQ / 1000
        imul ebx, ecx                   ; counts to wait
        call pit_counter
        mov esi, eax                    ; previous reading, in a register: a
                                        ; static here would be shared by every
                                        ; processor waiting at the same time
        xor edx, edx                    ; elapsed
.sdm_loop:
        call pit_counter
        mov ecx, esi
        sub ecx, eax
        and ecx, 0xFFFF                 ; the counter wraps every 65536 counts
        add edx, ecx
        mov esi, eax
        cmp edx, ebx
        jb .sdm_loop
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

;-----------------------------------------------------------------------
; smp_delay_tsc - wait a while without touching any shared hardware
;   In : ECX = approximate delay in thousands of cycles
;
; smp_delay_ms reads the PIT, and the PIT has one set of registers for the whole
; machine: the latch command and the two count bytes are shared, so four
; processors reading it at once can each get half of somebody else's reading,
; and a delay built on that either returns at once or waits far too long. On the
; bootstrap processor that almost never matters - it is alone when it uses the
; PIT - but a delay inside a job runs on every processor at once.
;
; The time-stamp counter is per processor, so reading it cannot interfere. Its
; rate is not architectural, which is why the unit here is cycles and the caller
; only ever asks for "roughly this long".
;-----------------------------------------------------------------------
smp_delay_tsc:
        push eax
        push ebx
        push ecx
        push edx
        test ecx, ecx
        jz .sdt_done
        mov ebx, ecx
        imul ebx, 1000                  ; thousands -> cycles
        rdtsc
        mov [.sdt_start], eax
.sdt_loop:
        rdtsc
        sub eax, [.sdt_start]
        cmp eax, ebx
        jb .sdt_loop
.sdt_done:
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret
.sdt_start: dd 0

;-----------------------------------------------------------------------
; smp_icr_send - put one command in the interrupt command register
;   In : EAX = low dword, EBX = destination APIC ID
; Waits for the previous command to leave before writing, as the register is
; busy until the delivery status bit clears.
;-----------------------------------------------------------------------
smp_icr_send:
        push eax
        push ebx
        push ecx
        mov [.sis_low], eax
        mov [.sis_dest], ebx

.sis_wait:
        mov ecx, LAPIC_ICR_LOW
        call lapic_read
        test eax, ICR_DELIVERY_STATUS
        jz .sis_ready
        mov ecx, 1
        call smp_delay_ms
        jmp .sis_wait

.sis_ready:
        mov ecx, LAPIC_ICR_HIGH
        mov eax, [.sis_dest]
        shl eax, 24                     ; destination APIC ID in the top byte
        call lapic_write
        mov ecx, LAPIC_ICR_LOW
        mov eax, [.sis_low]
        call lapic_write
        pop ecx
        pop ebx
        pop eax
        ret
.sis_low:  dd 0
.sis_dest: dd 0

;-----------------------------------------------------------------------
; smp_ap_main - first kernel code an application processor runs
;   Never returns: it says hello, hammers the allocator alongside every other
;   processor, and parks.
;-----------------------------------------------------------------------
smp_ap_main:
        ; The stack is already this processor's own - the trampoline claimed a
        ; slot and switched to it before calling here.
        xor ebp, ebp

        ; Per-CPU state first: everything below addresses this processor's own
        ; block through GS, and the blocks are what make "the current task" and
        ; "how many jobs have I run" questions with an answer per processor.
        call lock_self_ipi
        call cpu_local_init

        ; The trampoline brought a flat GDT of its own and no interrupt table.
        ; The kernel's gates are shared - one table, one set of handlers - so
        ; loading the descriptor is all an AP needs.
        lidt [idt_descriptor]

        ; Turn on this processor's local APIC: a halted processor whose LAPIC
        ; is disabled cannot be woken by an inter-processor interrupt.
        call apic_enable_this

        lock inc dword [smp_ap_online]

        ; A processor that only parks proves delivery. This proves the kernel
        ; survives it: both the allocator and this processor's own writes run
        ; against the bootstrap processor at the same time.
        call smp_stress_round
        lock inc dword [smp_work_done]

        ; Then earn its keep: run queued kernel work until the machine stops.
        jmp smp_ap_idle

;-----------------------------------------------------------------------
; smp_stress_round - hammer the frame allocator from this processor
;
; Each worker takes a batch of frames, stamps a pattern into every one that is
; unique to (worker, index), reads it back, and frees the batch. If two
; processors are ever handed the same frame, one stamp overwrites the other and
; the read-back disagrees - which is exactly how a missing lock shows itself.
;
; Frames are written through the identity map, so the test stays inside the
; first PAGING_MAP_MB and never asks the fault handler to map anything: this is
; a test of the allocator, not of paging.
;-----------------------------------------------------------------------
SMP_STRESS_FRAMES equ 16
SMP_STRESS_ROUNDS equ 24

smp_stress_round:
        pushad
        ; Everything this round needs lives on the processor's own stack. Static
        ; locals here would be shared by every worker at once - which is the
        ; very mistake this test exists to catch, and it caught it.
        mov  ebp, esp
        sub  esp, SMP_STRESS_FRAMES * 4 + 8
        call lock_self_ipi
        mov  [ebp - SMP_STRESS_FRAMES * 4 - 4], eax     ; this worker's slot
        mov  dword [ebp - SMP_STRESS_FRAMES * 4 - 8], 0 ; round counter
        ; Record which slot this worker believes it is: two workers sharing one
        ; looks exactly like a lock bug, so it is worth being able to see.
        mov  ecx, eax
        mov  eax, 1
        shl  eax, cl
        lock or dword [smp_worker_mask], eax

.ssr_round:
        ; --- take the batch ---
        xor  ebx, ebx
.ssr_alloc:
        cmp  ebx, SMP_STRESS_FRAMES
        jae  .ssr_hold
        call pmm_alloc_page
        test eax, eax
        jz   .ssr_oom
        lea  ecx, [ebp - SMP_STRESS_FRAMES * 4]
        mov  [ecx + ebx*4], eax
        inc  ebx
        jmp  .ssr_alloc

.ssr_hold:
        ; Hold the batch across a millisecond, so the other processors are
        ; allocating and freeing inside the same window. Without this the
        ; workers tend to run one after another and a missing lock can pass
        ; unnoticed - which is what a single short round did here.
        ; The TSC, not the PIT: this runs on every processor at once, and the
        ; PIT's latch is one set of registers for the whole machine.
        mov  ecx, 1000                  ; ~1000 x 1000 cycles, roughly a ms
        call smp_delay_tsc

        ; --- stamp: every frame gets (worker<<16)|index, which names its owner
        xor  ebx, ebx
.ssr_stamp:
        cmp  ebx, SMP_STRESS_FRAMES
        jae  .ssr_verify
        lea  ecx, [ebp - SMP_STRESS_FRAMES * 4]
        mov  edi, [ecx + ebx*4]
        cmp  edi, PAGING_MAP_MB * 0x100000
        jae  .ssr_stamp_next           ; outside the identity map: do not touch
        mov  eax, [ebp - SMP_STRESS_FRAMES * 4 - 4]
        shl  eax, 16
        mov  ecx, ebx
        inc  ecx
        or   eax, ecx
        mov  ecx, 1024
        cld
        rep  stosd
.ssr_stamp_next:
        inc  ebx
        jmp  .ssr_stamp

.ssr_verify:
        xor  ebx, ebx
.ssr_verify_loop:
        cmp  ebx, SMP_STRESS_FRAMES
        jae  .ssr_free
        lea  ecx, [ebp - SMP_STRESS_FRAMES * 4]
        mov  esi, [ecx + ebx*4]
        cmp  esi, PAGING_MAP_MB * 0x100000
        jae  .ssr_verify_next
        mov  eax, [ebp - SMP_STRESS_FRAMES * 4 - 4]
        shl  eax, 16
        mov  ecx, ebx
        inc  ecx
        or   eax, ecx
        mov  ecx, 1024
.ssr_verify_word:
        cmp  [esi], eax
        je   .ssr_verify_ok
        lock inc dword [smp_stress_errors]
        jmp  .ssr_verify_next
.ssr_verify_ok:
        add  esi, 4
        loop .ssr_verify_word
.ssr_verify_next:
        inc  ebx
        jmp  .ssr_verify_loop

.ssr_free:
        xor  ebx, ebx
.ssr_free_loop:
        cmp  ebx, SMP_STRESS_FRAMES
        jae  .ssr_next_round
        lea  ecx, [ebp - SMP_STRESS_FRAMES * 4]
        mov  eax, [ecx + ebx*4]
        call pmm_free_page
        lock inc dword [smp_stress_frames]
        inc  ebx
        jmp  .ssr_free_loop

.ssr_next_round:
        inc  dword [ebp - SMP_STRESS_FRAMES * 4 - 8]
        cmp  dword [ebp - SMP_STRESS_FRAMES * 4 - 8], SMP_STRESS_ROUNDS
        jb   .ssr_round

.ssr_done:
        mov  esp, ebp
        popad
        ret
.ssr_oom:
        lock inc dword [smp_stress_errors]
        jmp  .ssr_done

;-----------------------------------------------------------------------
; smp_start_aps - send INIT-SIPI-SIPI to every other processor described
;   Out: EAX = processors online, counting this one
;
; Called with interrupts off, before any driver has asked for a line, so an AP
; that arrives cannot be handed work it is not ready for.
;-----------------------------------------------------------------------
smp_start_aps:
        push ebx
        push ecx
        push edx
        push esi
        push edi

        cmp byte [irq_mode_apic], 1
        jne .ssa_report                 ; no local APIC: there is nobody to wake
        cmp dword [acpi_cpu_count], 1
        jbe .ssa_report

        mov ecx, LAPIC_ID
        call lapic_read
        shr eax, 24
        mov [smp_bsp_apic_id], al

        ; Leave the trampoline where a SIPI vector can find it.
        mov esi, smp_trampoline
        mov edi, SMP_TRAMPOLINE_BASE
        mov ecx, smp_trampoline_end - smp_trampoline
        cld
        rep movsb

        xor ebx, ebx
.ssa_loop:
        cmp ebx, [acpi_cpu_count]
        jae .ssa_wait
        ; Never wake more processors than there are slots for: each one claims
        ; a lock stack and an AP stack by slot, and a slot past the table would
        ; be shared.
        cmp dword [smp_ap_expected], SMP_MAX_CPUS - 1
        jae .ssa_wait
        movzx eax, byte [acpi_cpu_apic_ids + ebx]
        cmp al, [smp_bsp_apic_id]
        je .ssa_next                    ; that one is us

        mov [.ssa_id], eax
        inc dword [smp_ap_expected]

        ; INIT: put it in a known state.
        mov ebx, eax
        mov eax, ICR_MODE_INIT | ICR_LEVEL_ASSERT
        call smp_icr_send
        mov ecx, 10                     ; the specification asks for 10 ms
        call smp_delay_ms

        ; SIPI twice, as the specification requires for a processor that may
        ; be in either state.
        mov ebx, [.ssa_id]
        mov eax, ICR_MODE_SIPI | ICR_LEVEL_ASSERT | (SMP_TRAMPOLINE_BASE >> 12)
        call smp_icr_send
        mov ecx, 1
        call smp_delay_ms
        mov ebx, [.ssa_id]
        mov eax, ICR_MODE_SIPI | ICR_LEVEL_ASSERT | (SMP_TRAMPOLINE_BASE >> 12)
        call smp_icr_send
.ssa_next:
        inc ebx
        jmp .ssa_loop

.ssa_wait:
        ; The bootstrap processor takes its own round now, while the others are
        ; running theirs: contention is the point, so it must be simultaneous.
        call smp_stress_round

        ; Bounded: a processor that never arrives must not hang the boot.
        mov ecx, SMP_AP_TIMEOUT_MS
.ssa_wait_loop:
        mov eax, [smp_work_done]
        cmp eax, [smp_ap_expected]
        jae .ssa_report
        push ecx
        mov ecx, 1
        call smp_delay_ms
        pop ecx
        loop .ssa_wait_loop

.ssa_report:
        mov eax, [smp_ap_online]
        inc eax                         ; plus this processor
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret
.ssa_id: dd 0

;=======================================================================
; The blob an application processor actually starts executing.
;=======================================================================
align 16
smp_trampoline:
[bits 16]
        cli
        xor ax, ax
        mov ds, ax
        mov es, ax
        mov ss, ax
        mov sp, SMP_AP_STACK_TEMP
        lgdt [SMP_TRAMPOLINE_BASE + (smp_gdt_desc - smp_trampoline)]
        mov eax, cr0
        or  eax, 1                      ; protected mode
        mov cr0, eax
        jmp 0x08:(SMP_TRAMPOLINE_BASE + (smp_pm_entry - smp_trampoline))

align 8
smp_gdt:
        dq 0x0000000000000000           ; 0x00 null
        dq 0x00CF9A000000FFFF           ; 0x08 flat 32-bit code, ring 0
        dq 0x00CF92000000FFFF           ; 0x10 flat 32-bit data, ring 0
smp_gdt_desc:
        dw (smp_gdt_desc - smp_gdt) - 1
        dd SMP_TRAMPOLINE_BASE + (smp_gdt - smp_trampoline)

[bits 32]
smp_pm_entry:
        mov ax, 0x10
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax

        ; A stack of this processor's own, claimed before anything is called:
        ; every AP arrives through this same page, so a shared temporary stack
        ; here means two processors overwriting each other's return address -
        ; which is a triple fault, not a warning. The claim is therefore made
        ; in registers only: no call and no push until ESP is this processor's
        ; own. (lock_self_ipi would do the same work, but reaching it means
        ; calling through the stack that is not yet safe to use.)
        mov ecx, LAPIC_ID
        mov edx, [acpi_lapic_addr]
        add edx, ecx
        mov eax, [edx]                  ; local APIC ID register
        shr eax, 24
        and eax, 255
        mov esi, eax
        movzx edx, byte [cpu_slot_by_apic + esi]
        cmp edx, 0xFF
        jne .pm_slot_ready
        mov edx, 1
        lock xadd dword [cpu_slot_next], edx    ; EDX = the slot claimed
        cmp edx, SMP_MAX_CPUS
        jb .pm_slot_bounded
        mov edx, SMP_MAX_CPUS - 1       ; cannot happen: smp_start_aps caps APs
.pm_slot_bounded:
        mov eax, edx
        mov [cpu_slot_by_apic + esi], al
.pm_slot_ready:
        shl edx, 12
        mov eax, edx
        add eax, ap_stacks
        add eax, 4096 - 64
        mov esp, eax

        ; The kernel's address space, so that already-parked code and data are
        ; the same memory this processor now sees.
        mov eax, PAGE_DIR_ADDR
        mov cr3, eax
        mov eax, cr0
        or  eax, 0x80000000             ; paging
        mov cr0, eax

        ; Absolute, not relative: this blob was assembled somewhere in the
        ; kernel image and is executed from 0x8000, so a relative call would
        ; land at an offset from the wrong place.
        mov eax, smp_ap_main
        call eax
.spt_halt:
        cli
        hlt
        jmp .spt_halt
smp_trampoline_end:
[bits 32]
