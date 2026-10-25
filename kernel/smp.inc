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
smp_ap_expected:    dd 0
smp_bsp_apic_id:    db 0

; One stack per application processor, chosen by APIC ID so that a processor
; that arrives twice by accident cannot share a stack with itself.
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
        mov ebx, PIT_FREQ / 1000
        imul ebx, ecx                   ; counts to wait
        call pit_counter
        mov [.sdm_prev], eax
        xor edx, edx                    ; elapsed
.sdm_loop:
        call pit_counter
        mov ecx, [.sdm_prev]
        sub ecx, eax
        and ecx, 0xFFFF                 ; the counter wraps every 65536 counts
        add edx, ecx
        mov [.sdm_prev], eax
        cmp edx, ebx
        jb .sdm_loop
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret
.sdm_prev: dd 0

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
;   Never returns: it takes a stack, says hello, and parks.
;-----------------------------------------------------------------------
smp_ap_main:
        mov ecx, LAPIC_ID
        call lapic_read
        shr eax, 24                     ; this processor's APIC ID
        and eax, SMP_MAX_CPUS - 1
        shl eax, 12
        add eax, ap_stacks
        add eax, 4096 - 16
        mov esp, eax
        xor ebp, ebp
        lock inc dword [smp_ap_online]
.sam_park:
        cli
        hlt
        jmp .sam_park

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
        ; Bounded: a processor that never arrives must not hang the boot.
        mov ecx, SMP_AP_TIMEOUT_MS
.ssa_wait_loop:
        mov eax, [smp_ap_online]
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
        mov esp, SMP_AP_STACK_TEMP

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
