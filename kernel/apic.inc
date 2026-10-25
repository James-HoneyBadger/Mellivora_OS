;=======================================================================
; apic.inc - deliver interrupts through the local APIC and the I/O APIC
;
; A machine with more than one processor does not hand its interrupts to the
; 8259: the firmware describes an I/O APIC in the MADT and routes ISA and PCI
; lines through it to the local APIC of whichever processor it likes. This
; kernel used to listen on the 8259 alone, which is why an SMP machine went
; silent the moment it booted - no tick, no keyboard, nothing to wake a halt.
;
; Two modes, decided at boot from what the firmware described:
;
;   APIC mode   - the MADT names a local APIC and an I/O APIC. The 8259s are
;                 masked outright, the local APIC is enabled, and every
;                 redirection entry is programmed (masked) from the MADT's
;                 interrupt source overrides. irq_enable then unmasks the entry
;                 for a line, and irq_eoi acknowledges through the local APIC.
;   PIC mode    - no MADT, so nothing describes an APIC. Everything behaves as
;                 it did before: the 8259 is the interrupt controller.
;
; Vectors do not change: IRQ n arrives on vector 0x20+n either way, so the
; handlers and the IDT are untouched.
;
; Interrupt source overrides matter: on a PC the PIT is wired to I/O APIC pin
; 2, not pin 0, and the MADT says so. Where the table says nothing, an ISA line
; is taken as edge-triggered active-high, except the two PCI link lines this
; chipset routes (GSI 10 and 11), which are level-triggered active-low.
; Programmed PCI interrupt routing (_PRT) replaces that last rule in M2.
;=======================================================================

LAPIC_ID        equ 0x020               ; local APIC ID register
LAPIC_TIMER_LVT equ 0x320               ; timer local vector table
LAPIC_TIMER_INIT equ 0x380              ; timer initial count
LAPIC_TIMER_CUR equ 0x390               ; timer current count
LAPIC_TIMER_DIV equ 0x3E0               ; timer divide configuration

LAPIC_TIMER_PERIODIC equ (1 << 17)
LAPIC_TIMER_MASKED   equ (1 << 16)
LAPIC_TPR       equ 0x080               ; task priority
LAPIC_EOI       equ 0x0B0               ; end of interrupt
LAPIC_SVR       equ 0x0F0               ; spurious-interrupt vector / enable

LAPIC_SVR_ENABLE    equ 0x100
LAPIC_SPURIOUS_VEC  equ 0xFF

IOAPIC_REGSEL   equ 0x00
IOAPIC_IOWIN    equ 0x10
IOAPIC_REDTBL   equ 0x10                ; entry n lives at 0x10 + 2n

IOAPIC_MASK         equ (1 << 16)
IOAPIC_POLARITY_LOW equ (1 << 13)
IOAPIC_LEVEL        equ (1 << 15)
IRQ_VECTOR_BASE     equ 0x20            ; IRQ n lands on vector 0x20+n

; Interrupt source override flags, as the MADT states them
MADT_FLAG_POLARITY  equ (1 << 1)        ; 1 = active low
MADT_FLAG_TRIGGER   equ (1 << 3)        ; 1 = level

; Set once the I/O APIC is programmed. Read by irq_eoi on every interrupt, so
; it stays a byte and a compare.
irq_mode_apic:      db 0
; Which IRQs drivers have asked for. Drivers run before paging is up, so they
; cannot touch MMIO yet: they set a bit here and apic_init applies them.
irq_wanted:         dd 0
; Ticks per second the local APIC timer was measured to run at, or 0 when the
; PIT still owns the tick.
lapic_timer_hz:     dd 0
; Processors that answered, counting this one. 1 until smp_start_aps runs.
smp_cpus_online:    dd 1
lapic_timer_count:  dd 0

;-----------------------------------------------------------------------
; lapic_write / lapic_read - one local APIC register
;   write: ECX = offset, EAX = value    read: ECX = offset, EAX = value
;-----------------------------------------------------------------------
lapic_write:
        push edx
        mov edx, [acpi_lapic_addr]
        add edx, ecx
        mov [edx], eax
        pop edx
        ret

lapic_read:
        push edx
        mov edx, [acpi_lapic_addr]
        add edx, ecx
        mov eax, [edx]
        pop edx
        ret

;-----------------------------------------------------------------------
; ioapic_write / ioapic_read - one I/O APIC register, through IOREGSEL/IOWIN
;   write: ECX = register, EAX = value  read: ECX = register, EAX = value
;-----------------------------------------------------------------------
ioapic_write:
        push edx
        push eax
        mov edx, [acpi_ioapic_addr]
        mov [edx + IOAPIC_REGSEL], ecx
        pop eax
        mov [edx + IOAPIC_IOWIN], eax
        pop edx
        ret

ioapic_read:
        push edx
        mov edx, [acpi_ioapic_addr]
        mov [edx + IOAPIC_REGSEL], ecx
        mov eax, [edx + IOAPIC_IOWIN]
        pop edx
        ret

;-----------------------------------------------------------------------
; irq_route - program one I/O APIC redirection entry for an ISA IRQ
;   In : AL = IRQ (0-15), EBX = 1 unmask / 0 mask
;
; The line is mapped to its GSI through the MADT's interrupt source overrides,
; which is how the PIT ends up on pin 2 rather than pin 0.
;-----------------------------------------------------------------------
irq_route:
        pushad
        movzx esi, al
        cmp esi, 16
        jae .ir_done

        movzx edi, byte [acpi_irq_gsi + esi]    ; GSI for this line
        cmp edi, 23
        ja .ir_done                             ; beyond a 24-pin I/O APIC

        mov eax, esi
        add eax, IRQ_VECTOR_BASE                ; vector for this IRQ

        ; Polarity and trigger: the override's flags when it states them,
        ; otherwise the bus default - except the PCI link lines on this
        ; chipset, which are level, active low.
        movzx ecx, word [acpi_irq_flags + esi*2]
        test ecx, ecx
        jnz .ir_have_flags
        cmp edi, 10
        jb .ir_have_flags                       ; ISA: edge, active high
        cmp edi, 11
        ja .ir_have_flags
        mov ecx, MADT_FLAG_TRIGGER | MADT_FLAG_POLARITY
.ir_have_flags:
        test ecx, MADT_FLAG_POLARITY
        jz .ir_no_low
        or eax, IOAPIC_POLARITY_LOW
.ir_no_low:
        test ecx, MADT_FLAG_TRIGGER
        jz .ir_no_level
        or eax, IOAPIC_LEVEL
.ir_no_level:
        test ebx, ebx
        jnz .ir_unmask
        or eax, IOAPIC_MASK
.ir_unmask:
        push eax
        mov ecx, LAPIC_ID
        call lapic_read
        and eax, 0xFF000000                     ; destination: this processor
        mov edx, eax
        pop eax

        push edx
        mov ecx, IOAPIC_REDTBL
        add ecx, edi
        add ecx, edi                            ; low dword of this entry
        call ioapic_write
        pop edx
        inc ecx
        mov eax, edx
        call ioapic_write                       ; high dword: destination
.ir_done:
        popad
        ret

;-----------------------------------------------------------------------
; irq_enable - let one line through, in whichever mode is active
;   In : AL = IRQ (0-15)
;
; Drivers call this. Before apic_init has run the I/O APIC is not mapped, so
; the request is only recorded; apic_init applies it.
;-----------------------------------------------------------------------
irq_enable:
        push eax
        push ebx
        push ecx
        cmp al, 16
        jae .ie_out
        movzx ecx, al
        bts dword [irq_wanted], ecx
        cmp byte [irq_mode_apic], 1
        je .ie_apic

        ; 8259: clear this line's mask bit, plus the cascade for 8-15.
        mov ah, 1
        shl ah, cl
        not ah
        cmp cl, 8
        jae .ie_slave
        in al, PIC1_DATA
        and al, ah
        out PIC1_DATA, al
        jmp .ie_out
.ie_slave:
        sub cl, 8
        mov ah, 1
        shl ah, cl
        not ah
        in al, PIC2_DATA
        and al, ah
        out PIC2_DATA, al
        in al, PIC1_DATA
        and al, ~(1 << 2)
        out PIC1_DATA, al
        jmp .ie_out
.ie_apic:
        movzx eax, byte [esp + 8]               ; saved AL = the IRQ
        mov ebx, 1
        call irq_route
.ie_out:
        pop ecx
        pop ebx
        pop eax
        ret

;-----------------------------------------------------------------------
; irq_disable - stop one line, in whichever mode is active
;   In : AL = IRQ (0-15)
;-----------------------------------------------------------------------
irq_disable:
        push eax
        push ebx
        push ecx
        cmp al, 16
        jae .id_out
        movzx ecx, al
        btr dword [irq_wanted], ecx
        cmp byte [irq_mode_apic], 1
        je .id_apic

        mov ah, 1
        shl ah, cl
        cmp cl, 8
        jae .id_slave
        in al, PIC1_DATA
        or al, ah
        out PIC1_DATA, al
        jmp .id_out
.id_slave:
        sub cl, 8
        mov ah, 1
        shl ah, cl
        in al, PIC2_DATA
        or al, ah
        out PIC2_DATA, al
        jmp .id_out
.id_apic:
        movzx eax, byte [esp + 8]
        xor ebx, ebx
        call irq_route
.id_out:
        pop ecx
        pop ebx
        pop eax
        ret

;-----------------------------------------------------------------------
; apic_map_window - make the local and I/O APIC registers reachable
;
; Both live in the 4 MB region selected by page-directory entry 0x3FB, so one
; static page table covers them. Supervisor-only: firmware tables and device
; registers are the kernel's, never ring 3's.
;-----------------------------------------------------------------------
apic_map_window:
        pushad
        mov edi, PAGE_DIR_ADDR
        mov eax, apic_page_table
        or  eax, PG_PRESENT | PG_WRITABLE
        mov [edi + APIC_PDE_INDEX * 4], eax
        mov eax, cr3
        cmp eax, PAGE_DIR_ADDR
        je .amw_ptes
        mov edi, eax
        mov eax, apic_page_table
        or  eax, PG_PRESENT | PG_WRITABLE
        mov [edi + APIC_PDE_INDEX * 4], eax

.amw_ptes:
        mov edi, apic_page_table
        mov eax, 0xFEE00000
        or  eax, PG_PRESENT | PG_WRITABLE
        mov [edi + ((0xFEE00000 >> 12) & 0x3FF) * 4], eax
        mov eax, 0xFEC00000
        or  eax, PG_PRESENT | PG_WRITABLE
        mov [edi + ((0xFEC00000 >> 12) & 0x3FF) * 4], eax
        ; The window may have been read through the demand path before, so the
        ; TLB may hold an older translation for it.
        mov eax, 0xFEE00000
        invlpg [eax]
        mov eax, 0xFEC00000
        invlpg [eax]
        popad
        ret

APIC_PDE_INDEX equ (0xFEE00000 >> 22)

align 4096
apic_page_table: times 1024 dd 0

; A spurious local-APIC interrupt: nothing to acknowledge, nothing to do.
irq_spurious:
        iretd

;-----------------------------------------------------------------------
; irq_eoi - acknowledge the interrupt that is being serviced
;   In : AL = IRQ (0-15), or 0 when only "the controller" needs telling
;
; With the I/O APIC there is nothing per-line to acknowledge: the local APIC
; takes one EOI. With the 8259, a line on the slave must be acknowledged there
; before the master, or the slave stays blocked.
;-----------------------------------------------------------------------
irq_eoi:
        cmp byte [irq_mode_apic], 1
        je .ie_apic
        cmp al, 8
        jb .ie_master
        push eax
        mov al, 0x20
        out PIC2_CMD, al
        pop eax
.ie_master:
        mov al, 0x20
        out PIC1_CMD, al
        ret
.ie_apic:
        push eax
        push ecx
        mov ecx, LAPIC_EOI
        xor eax, eax
        call lapic_write
        pop ecx
        pop eax
        ret

;-----------------------------------------------------------------------
; pit_counter - read the PIT's channel-0 countdown (it counts at 1.193 MHz)
;   Out: EAX = current count
;
; Used once, to time the local APIC timer against a clock of known frequency.
;-----------------------------------------------------------------------
pit_counter:
        push edx
        mov al, 0x00                    ; latch channel 0
        out PIT_CMD, al
        in  al, PIT_CH0
        mov dl, al
        in  al, PIT_CH0
        mov dh, al
        movzx eax, dx
        pop edx
        ret

;-----------------------------------------------------------------------
; lapic_timer_start - measure the local APIC timer and take over the tick
;   Out: EAX = 0 when the APIC timer now drives vector 0x20, -1 when the PIT
;        keeps it
;
; The APIC timer's frequency is not discoverable: it is derived from the
; processor's bus clock, which no table states. So it is measured against the
; PIT, whose 1.193 MHz is a constant of the machine: start the timer at its
; largest count, let ten milliseconds of PIT elapse, and see how far it fell.
;
; The measurement is checked before it is trusted. A machine whose timer
; reports something absurd - a broken emulation, a CPU without one - keeps the
; PIT rather than running a clock nobody can trust.
;
; Interrupts are off here: the tick is about to change hands, and a PIT
; interrupt arriving mid-measurement would be counted by nobody.
;-----------------------------------------------------------------------
lapic_timer_start:
        pushad
        cmp byte [irq_mode_apic], 1
        jne .lts_no

        pushf
        cli

        mov ecx, LAPIC_TIMER_DIV
        mov eax, 3                      ; divide by 16
        call lapic_write

        ; One shot, masked, at the largest count it has.
        mov ecx, LAPIC_TIMER_LVT
        mov eax, IRQ_VECTOR_BASE | LAPIC_TIMER_MASKED
        call lapic_write
        mov ecx, LAPIC_TIMER_INIT
        mov eax, 0xFFFFFFFF
        call lapic_write

        call pit_counter
        mov [.lts_pit_start], eax

        ; Ten milliseconds of PIT ticks: 1.193 MHz / 100.
        mov ebx, PIT_FREQ / 100
.lts_wait:
        call pit_counter
        mov edx, [.lts_pit_start]
        sub edx, eax
        and edx, 0xFFFF
        cmp edx, ebx
        jb .lts_wait

        ; How far did the APIC timer fall in that time?
        mov ecx, LAPIC_TIMER_CUR
        call lapic_read
        mov ebx, 0xFFFFFFFF
        sub ebx, eax                    ; counts in ten milliseconds

        ; Ticks per second is a hundred times that; the periodic reload is one
        ; hundredth of it.
        mov eax, ebx
        mov ebx, 100
        mul ebx
        test eax, eax
        jz .lts_unusable
        mov ecx, eax                    ; ECX = ticks per second
        div ebx                         ; EAX = ticks per tick period
        test eax, eax
        jz .lts_unusable

        ; Sanity: a PC's local APIC timer runs at tens of MHz to a few hundred,
        ; so a tenth of a second's worth of counts must be plausible. Anything
        ; wildly outside means the measurement, not the clock, is broken.
        cmp ecx, 100000
        jb .lts_unusable
        mov [lapic_timer_hz], ecx
        mov [lapic_timer_count], eax

        ; Periodic from here on, on the vector the tick already uses.
        mov ecx, LAPIC_TIMER_LVT
        mov eax, IRQ_VECTOR_BASE | LAPIC_TIMER_PERIODIC
        call lapic_write
        mov ecx, LAPIC_TIMER_INIT
        mov eax, [lapic_timer_count]
        call lapic_write

        ; One clock, not two: the PIT's line is masked, or every tick would be
        ; counted twice.
        xor al, al
        call irq_disable

        popf
        mov dword [esp + 28], 0
        popad
        ret

.lts_unusable:
        popf
.lts_no:
        mov dword [lapic_timer_hz], 0
        mov dword [esp + 28], -1
        popad
        ret
.lts_pit_start: dd 0

;-----------------------------------------------------------------------
; apic_init - choose the interrupt controller and program it
;   Out: EAX = 0 in APIC mode, -1 when the machine has no usable APIC
;
; Called once, after paging is up (the APIC registers are MMIO) and before
; interrupts are enabled.
;-----------------------------------------------------------------------
apic_init:
        pushad
        call acpi_init
        test eax, eax
        jnz .ai_pic

        mov eax, [acpi_lapic_addr]
        test eax, eax
        jz .ai_pic
        mov eax, [acpi_ioapic_addr]
        test eax, eax
        jz .ai_pic

        ; The APIC registers are memory-mapped, and both windows sit in the
        ; same 4 MB region. They are mapped through a page table that lives in
        ; the kernel image rather than one the allocator hands out: asking the
        ; allocator for it here put a page-table frame somewhere the
        ; high-memory tests then tripped over, and the mapping itself never
        ; needed to be that clever.
        call apic_map_window

        ; Spurious interrupts arrive on their own vector and must be answered
        ; with nothing at all - no EOI, no work. The gate has to exist before
        ; the APIC can raise one.
        mov ecx, LAPIC_SPURIOUS_VEC
        mov eax, irq_spurious
        call idt_set_gate

        ; Enable the local APIC and accept spurious interrupts on their own
        ; vector, then clear the task priority so nothing is held back.
        ;
        ; Then read it back. A table describes a machine, it does not guarantee
        ; one: QEMU's 486 model reports an APIC in its MADT and has none, and
        ; the write simply vanishes. Masking the 8259s on that evidence left the
        ; machine with no interrupt controller at all, so the probe decides,
        ; and the firmware only gets a vote.
        mov ecx, LAPIC_SVR
        mov eax, LAPIC_SVR_ENABLE | LAPIC_SPURIOUS_VEC
        call lapic_write
        mov ecx, LAPIC_SVR
        call lapic_read
        test eax, LAPIC_SVR_ENABLE
        jz .ai_no_lapic
        mov ecx, LAPIC_TPR
        xor eax, eax
        call lapic_write

        ; The 8259s are now out of the picture: everything arrives through the
        ; I/O APIC, and an 8259 that still thinks it owns a line would deliver
        ; it twice.
        mov al, 0xFF
        out PIC1_DATA, al
        out PIC2_DATA, al

        ; Program every entry from scratch, all masked, then unmask what the
        ; drivers asked for before paging was up.
        xor ecx, ecx
.ai_program:
        mov al, cl
        xor ebx, ebx
        push ecx
        call irq_route
        pop ecx
        inc ecx
        cmp ecx, 16
        jb .ai_program

        mov byte [irq_mode_apic], 1

        ; This processor is slot 0, claimed before any other can ask.
        mov ecx, LAPIC_ID
        call lapic_read
        shr eax, 24
        and eax, 255
        mov byte [cpu_slot_by_apic + eax], 0

        xor ecx, ecx
.ai_apply:
        bt dword [irq_wanted], ecx
        jnc .ai_apply_next
        mov al, cl
        mov ebx, 1
        push ecx
        call irq_route
        pop ecx
.ai_apply_next:
        inc ecx
        cmp ecx, 16
        jb .ai_apply
        ; Take the tick from the PIT now that a CPU clock is available.
        call lapic_timer_start

        ; Then wake the other processors, with interrupts still off.
        call smp_start_aps
        mov [smp_cpus_online], eax

        mov dword [esp + 28], 0
        popad
        ret

.ai_no_lapic:
        ; The table promised an APIC the CPU does not have. Leave the 8259s as
        ; they were, spurious gate and all.
        mov ecx, LAPIC_SVR
        xor eax, eax
        call lapic_write
.ai_pic:
        ; No usable APIC: the 8259 stays in charge and nothing changes.
        mov byte [irq_mode_apic], 0
        mov dword [smp_cpus_online], 1
        mov dword [esp + 28], -1
        popad
        ret
