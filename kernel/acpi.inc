;=======================================================================
; acpi.inc - read the machine description the firmware left behind
;
; The firmware publishes what the machine is made of in ACPI tables: how many
; processors there are, where the local APIC lives, which I/O APICs exist and
; which interrupts they own. Nothing here programs any of that yet - this is
; the inventory the rest of M2 (LAPIC, IOAPIC, PnP) is built on, and the
; reason a machine can be asked what it is instead of being assumed.
;
; Discovery is the documented dance:
;
;   * the RSDP sits either in the first kilobyte of the Extended BIOS Data Area
;     (whose segment the firmware stores at 0x040E) or in the BIOS read-only
;     area 0xE0000..0xFFFFF, on a 16-byte boundary, and its first 20 bytes must
;     sum to zero;
;   * from revision 2 the RSDP also describes an XSDT, whose entries are 64 bits
;     wide. This is a 32-bit machine, so the low half of each entry is used -
;     every table QEMU and real firmware publish is below 4 GB.
;
; acpi_init is lazy. The first caller pays for the walk, so boot costs nothing
; and no driver has to run before the tables could be read.
;=======================================================================

ACPI_RSDP_SIG0      equ 0x20445352      ; "RSD "
ACPI_RSDP_SIG1      equ 0x20525450      ; "PTR "
ACPI_SIG_FACP       equ 0x50434146      ; "FACP"
ACPI_SIG_MADT       equ 0x43495041      ; "APIC" - the MADT's signature
ACPI_SIG_DSDT       equ 0x54445344      ; "DSDT"

; RSDP field offsets
RSDP_CHECKSUM       equ 8
RSDP_OEMID          equ 9
RSDP_REVISION       equ 15
RSDP_RSDT           equ 16
RSDP_LENGTH         equ 20
RSDP_XSDT           equ 24

; System description table header
SDT_SIGNATURE       equ 0
SDT_LENGTH          equ 4
SDT_CHECKSUM        equ 9
SDT_OEMID           equ 10
SDT_HEADER_SIZE     equ 36

; MADT: the bytes after the header, then the variable-length entry records
MADT_LAPIC_ADDR     equ 36
MADT_FLAGS          equ 40
MADT_ENTRIES        equ 44
MADT_TYPE_CPU       equ 0              ; processor local APIC
MADT_TYPE_IOAPIC    equ 1
MADT_TYPE_IRQ_OVR   equ 2                ; interrupt source override
MADT_TYPE_LAPIC_OVR equ 5              ; 64-bit local APIC address
MADT_TYPE_X2APIC    equ 9

; FADT (the "FACP"): the fixed description of the machine's power hardware.
; Offsets are the ACPI 1.0 layout, which every later revision keeps for these
; fields; the 64-bit GAS blocks that ACPI 2.0 appended start at FACP_X_FIRMWARE.
FACP_FIRMWARE_CTRL  equ 36             ; dword: FACS address
FACP_DSDT           equ 40             ; 32-bit DSDT address
FACP_SCI_INT        equ 46             ; word: the interrupt the SCI is wired to
FACP_SMI_CMD        equ 48             ; dword: I/O port that speaks to the SMM
FACP_ACPI_ENABLE    equ 52             ; byte written to SMI_CMD to take ownership
FACP_ACPI_DISABLE   equ 53
FACP_PM1A_EVT       equ 56             ; dword: PM1a status/enable block
FACP_PM1B_EVT       equ 60
FACP_PM1A_CNT       equ 64             ; dword: PM1a control block
FACP_PM1B_CNT       equ 68
FACP_PM2_CNT        equ 72
FACP_PM_TMR         equ 76
FACP_GPE0           equ 80
FACP_GPE1           equ 84
FACP_PM1_EVT_LEN    equ 88
FACP_PM1_CNT_LEN    equ 89
FACP_PM2_CNT_LEN    equ 90
FACP_PM_TMR_LEN     equ 91
FACP_GPE0_LEN       equ 92
FACP_GPE1_LEN       equ 93
FACP_GPE1_BASE      equ 94
FACP_DAY_ALRM       equ 106            ; byte: the RTC's day-of-month alarm
FACP_MON_ALRM       equ 107
FACP_CENTURY        equ 108
FACP_IAPC_BOOT_ARCH equ 109            ; word
FACP_FLAGS          equ 112            ; dword: WBINVD, RTC_S4, RESET_REG_SUP ...
FACP_RESET_REG      equ 116            ; 12-byte GAS: the reset register
FACP_RESET_VALUE    equ 128
FACP_X_FIRMWARE     equ 132            ; 8-byte FACS address (revision >= 2)
FACP_X_DSDT         equ 140
FACP_X_PM1A_EVT     equ 148            ; 12-byte GAS blocks from here on
FACP_X_PM1A_CNT     equ 172
FACP_MIN_LEN        equ 116            ; up to and including FLAGS

; FACP flags worth naming
FACP_FLAG_WBINVD    equ (1 << 0)
FACP_FLAG_RESET_REG equ (1 << 10)      ; RESET_REG holds a usable reset register

; PM1 control register bits
PM1_SCI_EN          equ (1 << 0)
PM1_SLP_TYP_SHIFT   equ 10
PM1_SLP_EN          equ (1 << 13)

; PM1 status bits. The status register is write-one-to-clear.
PM1_TMR_STS         equ (1 << 0)
PM1_PWRBTN_STS      equ (1 << 8)
PM1_RTC_STS         equ (1 << 10)
PM1_WAK_STS         equ (1 << 15)
; PM1 enable bits, same positions as the status bits they arm
PM1_PWRBTN_EN       equ (1 << 8)

; GAS address-space identifiers
GAS_SYSTEM_MEMORY   equ 0
GAS_SYSTEM_IO       equ 1

ACPI_EBDA_PTR       equ 0x040E         ; word: EBDA segment
ACPI_BIOS_ROM_START equ 0xE0000
ACPI_BIOS_ROM_END   equ 0x100000
ACPI_SCAN_STEP      equ 16

;-----------------------------------------------------------------------
; acpi_state - what acpi_init found, in the order it found it
;-----------------------------------------------------------------------
acpi_ready:         db 0
acpi_revision:      db 0
acpi_uses_xsdt:     db 0
acpi_rsdp_addr:     dd 0
acpi_root_addr:     dd 0               ; RSDT, or XSDT when revision >= 2
acpi_table_count:   dd 0
acpi_madt_addr:     dd 0
acpi_facp_addr:     dd 0
acpi_cpu_count:     dd 0
acpi_lapic_addr:    dd 0
acpi_ioapic_count:  dd 0
acpi_ioapic_addr:   dd 0               ; the first one, for a readable summary

; The FADT's power hardware. A zero block address means the machine does not
; have it - firmware says so by leaving the field empty, and a write to port 0
; is not a harmless way to find out.
acpi_pm_ready:      db 0               ; FADT read and a PM1a control block found
acpi_sci_en:        db 0               ; SCI_EN: firmware handed ACPI over to us
acpi_dsdt_addr:     dd 0
acpi_facs_addr:     dd 0
acpi_sci_irq:       dd 0               ; from the FADT, not guessed
acpi_smi_cmd:       dd 0
acpi_acpi_enable:   db 0
acpi_acpi_disable:  db 0
acpi_pm1a_evt:      dd 0
acpi_pm1b_evt:      dd 0
acpi_pm1a_cnt:      dd 0
acpi_pm1b_cnt:      dd 0
acpi_pm2_cnt:       dd 0
acpi_pm_tmr:        dd 0
acpi_gpe0:          dd 0
acpi_gpe1:          dd 0
acpi_pm1_evt_len:   db 0
acpi_pm1_cnt_len:   db 0
acpi_pm_tmr_len:    db 0
acpi_pm_flags:      dd 0
acpi_reset_value:   db 0
acpi_reset_space:   db 0               ; GAS address space of RESET_REG (0 = none)
acpi_reset_addr:    dd 0
acpi_rtc_century:   db 0               ; RTC register holding the century, or 0
acpi_slp_typa:      times 8 db 0xFF    ; SLP_TYP per state, 0xFF = not in the DSDT
acpi_slp_typb:      times 8 db 0xFF
acpi_pwrbtn_count:  dd 0               ; power-button presses seen
acpi_pwrbtn_seen:   db 0               ; set by the SCI, acted on by deferred work

; ISA IRQ -> global system interrupt, from the MADT's interrupt source
; overrides. Identity until an override says otherwise, which is why the PIT
; (IRQ 0, wired to IOAPIC pin 2 on a PC) needs the table to be read at all.
acpi_irq_gsi:   db 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
acpi_irq_flags: times 16 dw 0

; APIC ID of each processor the MADT described, in the order it listed them.
; The IDs are what an INIT-SIPI is addressed to, so they are worth keeping.
acpi_cpu_apic_ids: times 16 db 0

;-----------------------------------------------------------------------
; acpi_map - make a physical address readable
;   In : EAX = address (clobbered)
;
; The identity map covers the first PAGING_MAP_MB, which is where the RSDP and
; small tables usually are. A table above it is mapped page by page, supervisor
; only: firmware tables are data to read, never something ring 3 may touch.
; Mapping an address that is already mapped with the same translation is
; harmless, so no lookup is needed.
;-----------------------------------------------------------------------
acpi_map:
        push eax
        push ebx
        push ecx
        cmp eax, PAGING_MAP_MB * 0x100000
        jb .amap_done                   ; inside the boot identity map
        ; Only memory that is NOT RAM needs a mapping. A frame below the top of
        ; usable RAM already belongs to the page allocator, and mapping it here
        ; would pin a permanent translation in the master directory for a frame
        ; a task may later own - which is how a table read turned into a fault
        ; in the copy-on-write path. Kernel faults inside RAM are handled by
        ; the identity-demand path when they happen.
        cmp eax, [pmm_mem_top]
        jb .amap_done
        and eax, 0xFFFFF000
        mov ebx, eax
        mov ecx, PG_PRESENT | PG_WRITABLE
        call paging_map_page_all
.amap_done:
        pop ecx
        pop ebx
        pop eax
        ret

;-----------------------------------------------------------------------
; acpi_checksum_ok - does this block's bytes sum to zero?
;   In : ESI = block, ECX = length
;   Out: EAX = 1 valid, 0 not. ESI is preserved.
;-----------------------------------------------------------------------
acpi_checksum_ok:
        push esi
        push ecx
        push edx
        xor edx, edx
        xor eax, eax
        test ecx, ecx
        jz .aco_bad                     ; an empty block proves nothing
.aco_sum:
        lodsb
        add edx, eax
        loop .aco_sum
        test dl, dl
        jnz .aco_bad
        mov eax, 1
        jmp .aco_out
.aco_bad:
        xor eax, eax
.aco_out:
        pop edx
        pop ecx
        pop esi
        ret

;-----------------------------------------------------------------------
; acpi_probe_rsdp - is there an RSDP at this address?
;   In : EAX = address to test
;   Out: EAX = 1 found and valid, 0 not
;-----------------------------------------------------------------------
acpi_probe_rsdp:
        push esi
        push ebx
        mov ebx, eax
        cmp eax, ACPI_BIOS_ROM_END
        jae .apr_no                     ; never read past the BIOS area
        mov esi, eax
        cmp dword [esi], ACPI_RSDP_SIG0
        jne .apr_no
        cmp dword [esi + 4], ACPI_RSDP_SIG1
        jne .apr_no
        mov ecx, 20
        call acpi_checksum_ok
        test eax, eax
        jz .apr_no
        mov eax, 1
        jmp .apr_out
.apr_no:
        xor eax, eax
.apr_out:
        pop ebx
        pop esi
        ret

;-----------------------------------------------------------------------
; acpi_find_rsdp - search the two places the specification allows
;   Out: EAX = RSDP address, or 0
;-----------------------------------------------------------------------
acpi_find_rsdp:
        push esi
        push ebx
        push ecx

        ;--- the EBDA's first kilobyte, if the firmware says there is one ---
        movzx eax, word [ACPI_EBDA_PTR]
        shl eax, 4
        test eax, eax
        jz .afr_rom
        cmp eax, 0x80000
        jb .afr_rom                     ; not a plausible EBDA
        mov ebx, eax
        mov ecx, 1024 / ACPI_SCAN_STEP
.afr_ebda_loop:
        mov eax, ebx
        call acpi_probe_rsdp
        test eax, eax
        jnz .afr_found_ebda
        add ebx, ACPI_SCAN_STEP
        loop .afr_ebda_loop
        jmp .afr_rom
.afr_found_ebda:
        mov eax, ebx
        jmp .afr_out

        ;--- the BIOS read-only area ---
.afr_rom:
        mov ebx, ACPI_BIOS_ROM_START
.afr_rom_loop:
        mov eax, ebx
        call acpi_probe_rsdp
        test eax, eax
        jnz .afr_found_rom
        add ebx, ACPI_SCAN_STEP
        cmp ebx, ACPI_BIOS_ROM_END
        jb .afr_rom_loop
        xor eax, eax
        jmp .afr_out
.afr_found_rom:
        mov eax, ebx
.afr_out:
        pop ecx
        pop ebx
        pop esi
        ret

;-----------------------------------------------------------------------
; acpi_find_table - look a table up by signature
;   In : EBX = signature, little-endian, as the bytes appear ("FACP" is
;        'F' | 'A'<<8 | 'C'<<16 | 'P'<<24)
;   Out: EAX = table address, or 0
;-----------------------------------------------------------------------
acpi_find_table:
        push esi
        push ecx
        push edx

        mov edx, [acpi_root_addr]
        test edx, edx
        jz .aft_none

        mov esi, edx
        mov ecx, [esi + SDT_LENGTH]
        sub ecx, SDT_HEADER_SIZE
        jbe .aft_none
        mov eax, ACPI_SCAN_STEP          ; entry stride, set below
        cmp byte [acpi_uses_xsdt], 1
        je .aft_xsdt
        mov eax, 4
        jmp .aft_stride_ok
.aft_xsdt:
        mov eax, 8
.aft_stride_ok:
        mov [.aft_stride], eax
        lea esi, [edx + SDT_HEADER_SIZE]

.aft_loop:
        cmp ecx, [.aft_stride]
        jb .aft_none
        mov eax, [esi]                   ; the entry is a pointer, not a name:
        mov eax, [eax]                   ; follow it to the table header
        cmp eax, ebx
        je .aft_found
        add esi, [.aft_stride]
        sub ecx, [.aft_stride]
        jmp .aft_loop

.aft_found:
        ; The entry is 4 bytes wide under the RSDT and 8 under the XSDT; the
        ; lower half is the address either way on a 32-bit machine.
        mov eax, [esi]
        jmp .aft_out
.aft_none:
        xor eax, eax
.aft_out:
        pop edx
        pop ecx
        pop esi
        ret
.aft_stride: dd 4

;-----------------------------------------------------------------------
; acpi_scan_madt - count the processors and the I/O APICs
;   In : EAX = MADT address (0 = none)
;
; Only the entry types that describe *what exists* are counted; interrupt
; source overrides and NMI descriptions are left for the IOAPIC driver, which
; is the only code that can act on them.
;-----------------------------------------------------------------------
acpi_scan_madt:
        push esi
        push ecx
        push edx
        push ebx

        test eax, eax
        jz .asm_done
        mov [acpi_madt_addr], eax
        mov esi, eax
        mov eax, [esi + MADT_LAPIC_ADDR]
        mov [acpi_lapic_addr], eax

        mov ecx, [esi + SDT_LENGTH]
        ; The MADT header is the 36-byte table header plus the local APIC
        ; address and flags, so entry records begin at 44, not at 36.
        cmp ecx, MADT_ENTRIES
        jbe .asm_done
        mov edx, ecx
        sub edx, MADT_ENTRIES           ; bytes of entry records
        jbe .asm_done
        mov ecx, edx
        add esi, MADT_ENTRIES
        jmp .asm_loop

.asm_loop:
        cmp ecx, 2
        jb .asm_done
        movzx eax, byte [esi]           ; entry type
        movzx ebx, byte [esi + 1]       ; entry length
        test ebx, ebx
        jz .asm_done                    ; a zero length would loop forever

        cmp eax, MADT_TYPE_CPU
        jne .asm_not_cpu
        mov edx, [acpi_cpu_count]
        cmp edx, 16
        jae .asm_cpu_counted
        mov al, [esi + 3]               ; this processor's APIC ID
        mov [acpi_cpu_apic_ids + edx], al
.asm_cpu_counted:
        inc dword [acpi_cpu_count]
        jmp .asm_next
.asm_not_cpu:
        cmp eax, MADT_TYPE_X2APIC
        jne .asm_not_x2
        inc dword [acpi_cpu_count]
        jmp .asm_next
.asm_not_x2:
        cmp eax, MADT_TYPE_IOAPIC
        jne .asm_not_ioapic
        inc dword [acpi_ioapic_count]
        cmp dword [acpi_ioapic_addr], 0
        jne .asm_next
        mov eax, [esi + 4]              ; I/O APIC MMIO address
        mov [acpi_ioapic_addr], eax
        jmp .asm_next
.asm_not_ioapic:
        cmp eax, MADT_TYPE_IRQ_OVR
        jne .asm_not_override
        ; bus(1), source IRQ(1), GSI(4), flags(2). Source 0xFF means "any line",
        ; which no single redirection entry can stand for.
        movzx eax, byte [esi + 3]       ; source ISA IRQ
        cmp eax, 16
        jae .asm_next
        mov edx, [esi + 4]              ; global system interrupt
        cmp edx, 255
        ja .asm_next
        mov [acpi_irq_gsi + eax], dl
        mov dx, [esi + 8]               ; polarity/trigger flags
        mov [acpi_irq_flags + eax*2], dx
        jmp .asm_next
.asm_not_override:
        cmp eax, MADT_TYPE_LAPIC_OVR    ; a 64-bit local APIC address wins
        jne .asm_next
        mov eax, [esi + 4]
        test eax, eax
        jz .asm_next
        mov [acpi_lapic_addr], eax

.asm_next:
        add esi, ebx
        sub ecx, ebx
        jmp .asm_loop
.asm_done:
        pop ebx
        pop edx
        pop ecx
        pop esi
        ret

;-----------------------------------------------------------------------
; acpi_init - discover the tables. Idempotent; the first call does the work.
;   Out: EAX = 0 when the tables were read, -1 when there is no usable RSDP
;-----------------------------------------------------------------------
acpi_init:
        push esi
        push ecx
        push edx
        push ebx

        cmp byte [acpi_ready], 1
        je .ai_ok

        call acpi_find_rsdp
        test eax, eax
        jz .ai_fail
        mov [acpi_rsdp_addr], eax

        mov esi, eax
        mov al, [esi + RSDP_REVISION]
        mov [acpi_revision], al

        ; Revision 2 and later publish an XSDT; prefer it, and fall back to the
        ; 32-bit RSDT when the field is empty (some firmware leaves it so).
        xor ebx, ebx
        cmp al, 2
        jb .ai_rsdt
        mov ebx, [esi + RSDP_XSDT]
        test ebx, ebx
        jz .ai_rsdt
        mov byte [acpi_uses_xsdt], 1
        jmp .ai_root_ok
.ai_rsdt:
        mov ebx, [esi + RSDP_RSDT]
        test ebx, ebx
        jz .ai_fail
.ai_root_ok:
        mov [acpi_root_addr], ebx

        ; Walk the root table and count what is there.
        mov eax, ebx
        call acpi_map
        mov esi, ebx
        mov ecx, [esi + SDT_LENGTH]
        sub ecx, SDT_HEADER_SIZE
        jbe .ai_fail
        mov eax, 4
        cmp byte [acpi_uses_xsdt], 1
        jne .ai_stride_ok
        mov eax, 8
.ai_stride_ok:
        mov edx, eax
        add esi, SDT_HEADER_SIZE
.ai_count:
        cmp ecx, edx
        jb .ai_counted
        inc dword [acpi_table_count]
        add esi, edx
        sub ecx, edx
        jmp .ai_count
.ai_counted:
        ; Pointers in the root table are physical addresses; make sure the
        ; tables themselves are readable before anything walks them.
        mov esi, [acpi_root_addr]
        mov ecx, [esi + SDT_LENGTH]
        sub ecx, SDT_HEADER_SIZE
        mov eax, 4
        cmp byte [acpi_uses_xsdt], 1
        jne .ai_map_stride_ok
        mov eax, 8
.ai_map_stride_ok:
        mov edx, eax
        lea esi, [esi + SDT_HEADER_SIZE]
.ai_map_loop:
        cmp ecx, edx
        jb .ai_mapped
        mov eax, [esi]
        call acpi_map
        add esi, edx
        sub ecx, edx
        jmp .ai_map_loop
.ai_mapped:

        mov ebx, ACPI_SIG_FACP
        call acpi_find_table
        mov [acpi_facp_addr], eax
        call acpi_parse_fadt

        mov ebx, ACPI_SIG_MADT
        call acpi_find_table
        call acpi_scan_madt

        mov byte [acpi_ready], 1
.ai_ok:
        xor eax, eax
        jmp .ai_out
.ai_fail:
        mov eax, -1
.ai_out:
        pop ebx
        pop edx
        pop ecx
        pop esi
        ret

;=======================================================================
; The fixed ACPI description: power hardware, and taking it over
;
; The MADT says what the machine has; the FADT says how to turn it off. The
; fields read here are the ones a machine needs to sleep, wake and die:
;
;   PM1a/PM1b status    - what happened (the power button is bit 8)
;   PM1a/PM1b control   - SCI_EN (firmware has handed ACPI over) and the
;                         sleep-type/enable bits a shutdown writes
;   SMI_CMD + ENABLE    - the port that asks the firmware to hand ACPI over
;   SCI_INT             - which interrupt the science of all this arrives on
;   RESET_REG           - how to reset when the keyboard controller will not
;
; Nothing here is assumed: a zero block address means the field was empty, and
; a machine whose FADT does not describe a PM1a control block cannot be powered
; off by ACPI at all. That is worth knowing before writing to a port.
;=======================================================================

;-----------------------------------------------------------------------
; acpi_parse_fadt - read the FADT into named state
;
; Called once, from acpi_init, as soon as the FACP is located. The DSDT is
; scanned for the sleep-type packages here rather than at first use, because a
; scan of a firmware table is not something to do inside interrupt context.
;-----------------------------------------------------------------------
acpi_parse_fadt:
        pushad
        mov eax, [acpi_facp_addr]
        test eax, eax
        jz .apf_done
        call acpi_map

        mov esi, [acpi_facp_addr]
        mov eax, [esi + SDT_LENGTH]
        cmp eax, FACP_MIN_LEN
        jb .apf_done                      ; too short to trust: leave it alone

        mov eax, [esi + FACP_DSDT]
        test eax, eax
        jnz .apf_dsdt
        ; ACPI 2.0 moved the DSDT into a 64-bit field; take the low half
        cmp dword [esi + SDT_LENGTH], FACP_X_DSDT + 8
        jb .apf_no_dsdt
        mov eax, [esi + FACP_X_DSDT]
        test eax, eax
        jz .apf_no_dsdt
        call acpi_map
        mov esi, [acpi_facp_addr]
        mov eax, [esi + FACP_X_DSDT]
        jmp .apf_dsdt_store
.apf_no_dsdt:
        xor eax, eax
        jmp .apf_dsdt_store
.apf_dsdt:
        call acpi_map                     ; a DSDT above the identity map
        mov esi, [acpi_facp_addr]
        mov eax, [esi + FACP_DSDT]
.apf_dsdt_store:
        mov [acpi_dsdt_addr], eax

        mov eax, [esi + FACP_PM1A_CNT]
        mov [acpi_pm1a_cnt], eax
        mov eax, [esi + FACP_PM1A_EVT]
        mov [acpi_pm1a_evt], eax
        mov eax, [esi + FACP_PM1B_EVT]
        mov [acpi_pm1b_evt], eax
        mov eax, [esi + FACP_PM1B_CNT]
        mov [acpi_pm1b_cnt], eax
        mov eax, [esi + FACP_PM2_CNT]
        mov [acpi_pm2_cnt], eax
        mov eax, [esi + FACP_PM_TMR]
        mov [acpi_pm_tmr], eax
        mov eax, [esi + FACP_GPE0]
        mov [acpi_gpe0], eax
        mov eax, [esi + FACP_GPE1]
        mov [acpi_gpe1], eax

        mov al, [esi + FACP_PM1_EVT_LEN]
        mov [acpi_pm1_evt_len], al
        mov al, [esi + FACP_PM1_CNT_LEN]
        mov [acpi_pm1_cnt_len], al
        mov al, [esi + FACP_PM_TMR_LEN]
        mov [acpi_pm_tmr_len], al
        mov al, [esi + FACP_CENTURY]
        mov [acpi_rtc_century], al
        movzx eax, word [esi + FACP_SCI_INT]
        mov [acpi_sci_irq], eax
        mov eax, [esi + FACP_SMI_CMD]
        mov [acpi_smi_cmd], eax
        mov al, [esi + FACP_ACPI_ENABLE]
        mov [acpi_acpi_enable], al
        mov al, [esi + FACP_ACPI_DISABLE]
        mov [acpi_acpi_disable], al
        mov eax, [esi + FACP_FLAGS]
        mov [acpi_pm_flags], eax
        mov al, [esi + FACP_RESET_VALUE]
        mov [acpi_reset_value], al

        ; RESET_REG is a 12-byte GAS: address space, bit width, bit offset,
        ; access size, then the 64-bit address (low half on this machine).
        ; Only believed when the FADT's own flag says it is implemented - the
        ; field is otherwise reserved space that firmware fills with rubbish.
        test dword [esi + FACP_FLAGS], FACP_FLAG_RESET_REG
        jz .apf_no_reset
        mov al, [esi + FACP_RESET_REG]
        mov [acpi_reset_space], al
        mov eax, [esi + FACP_RESET_REG + 4]
        mov [acpi_reset_addr], eax
        jmp .apf_reset_done
.apf_no_reset:
        mov byte [acpi_reset_space], 0
        mov dword [acpi_reset_addr], 0
.apf_reset_done:

        ; The FACS carries the firmware waking vector a resume would use. This
        ; kernel does not implement resume, but knowing whether the address is
        ; there is part of reporting the machine honestly.
        cmp dword [esi + SDT_LENGTH], FACP_X_FIRMWARE + 8
        jb .apf_legacy_facs
        mov eax, [esi + FACP_X_FIRMWARE]
        test eax, eax
        jnz .apf_facs_store
.apf_legacy_facs:
        mov eax, [esi + FACP_FIRMWARE_CTRL]
.apf_facs_store:
        mov [acpi_facs_addr], eax

        ; Is ACPI already ours? SCI_EN says the firmware has stepped aside.
        cmp dword [acpi_pm1a_cnt], 0
        je .apf_not_ready
        mov dx, word [acpi_pm1a_cnt]
        in ax, dx
        test ax, PM1_SCI_EN
        jz .apf_no_sci_en
        mov byte [acpi_sci_en], 1
.apf_no_sci_en:
        mov byte [acpi_pm_ready], 1
.apf_not_ready:

        ; Sleep types live in the DSDT as the packages \_S1, \_S3 and \_S5.
        mov al, 1
        call acpi_aml_sleep_typ
        jc .apf_s1_done
        call acpi_store_slp
.apf_s1_done:
        mov al, 3
        call acpi_aml_sleep_typ
        jc .apf_s3_done
        call acpi_store_slp
.apf_s3_done:
        mov al, 5
        call acpi_aml_sleep_typ
        jc .apf_s5_done
        call acpi_store_slp
.apf_s5_done:
.apf_done:
        popad
        ret

;-----------------------------------------------------------------------
; acpi_store_slp - keep the SLP_TYP values acpi_aml_sleep_typ just found
;   In : AL = state, EAX above it = SLP_TYPa, EDX = SLP_TYPb
;
; acpi_aml_sleep_typ returns the two types in EAX and EDX; the state it was
; asked about is kept in acpi_aml_last_state by that routine.
;-----------------------------------------------------------------------
acpi_store_slp:
        push ebx
        push ecx
        movzx ecx, byte [acpi_aml_last_state]
        cmp ecx, 8
        jae .ass_done
        mov ebx, acpi_slp_typa
        mov [ebx + ecx], al
        mov ebx, acpi_slp_typb
        mov [ebx + ecx], dl
.ass_done:
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; acpi_aml_sleep_typ - find \_S<n>'s SLP_TYP values in the DSDT
;   In : AL = state number (1, 3, 4, 5)
;   Out: EAX = SLP_TYPa, EDX = SLP_TYPb, CF = 0 found / 1 not found
;
; The values are not architecture: every machine chooses its own encoding, and
; the only place it is written down is the AML in the DSDT, as
;
;     Name (_S5, Package (0x02) { 0x00, 0x00 })
;
; That is a NameOp (0x08), the four-character name, a PackageOp (0x12), a
; package length, a count, then integer elements. Integers appear as ZeroOp
; (0x00), OneOp (0x01), BytePrefix (0x0A d), WordPrefix (0x0B dw) or
; DWordPrefix (0x0C dd), so a scanner that understands exactly those decodes
; the package without interpreting a single line of AML.
;
; A full AML interpreter is not what this needs to be: the search is for one
; known object at the top level of the definition block, and anything it does
; not understand it skips a byte at a time.
;-----------------------------------------------------------------------
acpi_aml_sleep_typ:
        push ebx
        push ecx
        push esi
        push edi
        push ebp

        mov [acpi_aml_last_state], al

        ; The name as it sits in memory: '_', 'S', digit, '_'.
        movzx eax, al
        add eax, '0'
        shl eax, 16
        or eax, '_' | ('S' << 8)
        or eax, ('_' << 24)
        mov [.ast_name], eax

        mov esi, [acpi_dsdt_addr]
        test esi, esi
        jz .ast_fail
        mov ecx, [esi + SDT_LENGTH]
        cmp ecx, SDT_HEADER_SIZE + 8
        jb .ast_fail
        sub ecx, SDT_HEADER_SIZE
        add esi, SDT_HEADER_SIZE

.ast_scan:
        cmp ecx, 7                        ; opcode + name + package opcode + length
        jb .ast_fail
        cmp byte [esi], 0x08              ; NameOp
        jne .ast_next
        mov eax, [esi + 1]
        cmp eax, [.ast_name]
        jne .ast_next
        cmp byte [esi + 5], 0x12          ; PackageOp
        jne .ast_next

        lea edi, [esi + 6]
        call acpi_aml_skip_pkglen         ; EDI past the length, no elements left
        cmp byte [edi], 2
        jb .ast_next                      ; fewer than two elements: not a sleep package
        inc edi
        call acpi_aml_int
        jc .ast_next
        mov ebx, eax                      ; SLP_TYPa
        call acpi_aml_int
        jc .ast_fail_one                  ; a one-element package: typb = typa
        mov edx, eax
        mov eax, ebx
        clc
        jmp .ast_out
.ast_fail_one:
        mov eax, ebx
        mov edx, ebx
        clc
        jmp .ast_out
.ast_next:
        inc esi
        dec ecx
        jmp .ast_scan

.ast_fail:
        stc
        jmp .ast_out
.ast_out:
        pop ebp
        pop edi
        pop esi
        pop ecx
        pop ebx
        ret
.ast_name:  dd 0

acpi_aml_last_state: db 0

;-----------------------------------------------------------------------
; acpi_aml_skip_pkglen - step over a package's length field
;   In : EDI = first byte of the length
;   Out: EDI = first byte after it
;
; The top two bits of the first byte say how many bytes follow it, 0 to 3, so
; the field is 1 to 4 bytes long. The value itself is not needed: the elements
; are decoded in order and the count says when to stop.
;-----------------------------------------------------------------------
acpi_aml_skip_pkglen:
        push eax
        movzx eax, byte [edi]
        shr eax, 6
        inc eax
        add edi, eax
        pop eax
        ret

;-----------------------------------------------------------------------
; acpi_aml_int - decode one AML integer element
;   In : EDI = element
;   Out: EAX = value, EDI advanced past it; CF = 1 if this is not an integer
;-----------------------------------------------------------------------
acpi_aml_int:
        movzx eax, byte [edi]
        cmp al, 0x00                      ; ZeroOp
        je .ai_zero
        cmp al, 0x01                      ; OneOp
        je .ai_one
        cmp al, 0x0A                      ; BytePrefix
        je .ai_byte
        cmp al, 0x0B                      ; WordPrefix
        je .ai_word
        cmp al, 0x0C                      ; DWordPrefix
        je .ai_dword
        stc
        ret
.ai_zero:
        xor eax, eax
        inc edi
        clc
        ret
.ai_one:
        mov eax, 1
        inc edi
        clc
        ret
.ai_byte:
        movzx eax, byte [edi + 1]
        add edi, 2
        clc
        ret
.ai_word:
        movzx eax, word [edi + 1]
        add edi, 3
        clc
        ret
.ai_dword:
        mov eax, [edi + 1]
        add edi, 5
        clc
        ret

;-----------------------------------------------------------------------
; acpi_pm_own - make ACPI this operating system's
;   Out: EAX = 0 when the PM1a control block is ours to write, -1 when not
;
; ACPI is the firmware's until the OS asks for it: writing ACPI_ENABLE to
; SMI_CMD makes the SMM hand the PM registers over and set SCI_EN in PM1
; control. A machine that is already handed over (firmware did it, or an
; earlier boot) says so by having SCI_EN set, and needs nothing.
;-----------------------------------------------------------------------
acpi_pm_own:
        pushad
        call acpi_init
        test eax, eax
        jnz .apo_fail
        cmp byte [acpi_pm_ready], 1
        jne .apo_fail
        cmp byte [acpi_sci_en], 1
        je .apo_ok
        cmp dword [acpi_smi_cmd], 0
        je .apo_fail                      ; nothing to ask: cannot take over
        cmp byte [acpi_acpi_enable], 0
        je .apo_fail

        mov dx, word [acpi_smi_cmd]
        mov al, [acpi_acpi_enable]
        out dx, al

        ; The handover is asynchronous: the SMM does it between instructions.
        ; Poll SCI_EN for a while before deciding the firmware refused.
        mov ebx, 40
.apo_wait:
        mov ecx, 5
        call smp_delay_ms
        mov dx, word [acpi_pm1a_cnt]
        in ax, dx
        test ax, PM1_SCI_EN
        jnz .apo_got_it
        dec ebx
        jnz .apo_wait
        jmp .apo_fail
.apo_got_it:
        mov byte [acpi_sci_en], 1
.apo_ok:
        xor eax, eax
        jmp .apo_out
.apo_fail:
        mov eax, -1
.apo_out:
        mov [esp + 28], eax               ; EAX of the pushad frame
        popad
        ret

;-----------------------------------------------------------------------
; acpi_poweroff - S5, or the best this machine can do instead
;
; SLP_TYPa comes from the DSDT: it is the machine's own encoding for "state 5",
; and there is no architectural value to guess. With no ACPI control block to
; write, or firmware that will not hand over, the ladder ends at a QEMU/Bochs
; chipset port and then at a halt - and it says which rung it reached, because
; a machine that will not power off is a bug report, not a mystery.
;-----------------------------------------------------------------------
acpi_poweroff:
        call acpi_pm_own
        test eax, eax
        jnz .apo_halt

        movzx eax, byte [acpi_slp_typa + 5]
        cmp eax, 0xFF
        jne .apo_have_typ
        xor eax, eax                      ; not in the DSDT: the common encoding
.apo_have_typ:
        shl eax, PM1_SLP_TYP_SHIFT
        or eax, PM1_SLP_EN
        mov ebx, eax
        mov dx, word [acpi_pm1a_cnt]
        out dx, ax

        cmp dword [acpi_pm1b_cnt], 0
        je .apo_settle
        movzx eax, byte [acpi_slp_typb + 5]
        cmp eax, 0xFF
        jne .apo_have_typb
        movzx eax, byte [acpi_slp_typa + 5]
        cmp eax, 0xFF
        jne .apo_have_typb
        xor eax, eax
.apo_have_typb:
        shl eax, PM1_SLP_TYP_SHIFT
        or eax, PM1_SLP_EN
        mov dx, word [acpi_pm1b_cnt]
        out dx, ax

.apo_settle:
        ; Power does not drop within an instruction. Give the chipset a moment,
        ; then assume the ACPI write was not the way on this machine.
        mov ecx, 60
        call smp_delay_ms

        ; QEMU's PIIX4 and Bochs both treat a write of S5 to this port as a
        ; shutdown request without needing SCI_EN. It is the last automated
        ; rung before halting.
        mov dx, 0x604
        mov ax, 0x2000
        out dx, ax
        mov ecx, 60
        call smp_delay_ms

.apo_halt:
        mov esi, msg_acpi_off_failed
        call vga_print
        call serial_print
        cli
.apo_hang:
        hlt
        jmp .apo_hang

;-----------------------------------------------------------------------
; acpi_pm_reset - reset the machine through the FADT, if it says how
;   Out: returns only when the FADT describes no usable reset register
;-----------------------------------------------------------------------
acpi_pm_reset:
        call acpi_pm_own
        cmp byte [acpi_reset_space], GAS_SYSTEM_IO
        jne .apr_none
        cmp dword [acpi_reset_addr], 0
        je .apr_none
        mov dx, word [acpi_reset_addr]
        mov al, [acpi_reset_value]
        out dx, al
        mov ecx, 100
        call smp_delay_ms
.apr_none:
        ret

;-----------------------------------------------------------------------
; irq_sci - the system control interrupt
;
; Fixed events arrive here: the power button is the one worth acting on. The
; status register is write-one-to-clear, so the bit is written back before the
; handler returns or the interrupt will fire forever.
;
; The work itself is deferred: a clean shutdown flushes a filesystem, and doing
; that between the interrupt gate's cli and iretd is the wrong place for it.
;-----------------------------------------------------------------------
irq_sci:
        pushad
        cmp dword [acpi_pm1a_evt], 0
        je .isci_eoi
        mov dx, word [acpi_pm1a_evt]
        in ax, dx
        test ax, PM1_PWRBTN_STS
        jz .isci_other
        mov bx, ax
        mov ax, PM1_PWRBTN_STS            ; write-one-to-clear, only this bit
        out dx, ax
        lock inc dword [acpi_pwrbtn_count]
        mov byte [acpi_pwrbtn_seen], 1
        mov eax, acpi_power_button_dpc
        call dpc_queue
.isci_other:
.isci_eoi:
        movzx eax, word [acpi_sci_irq]    ; irq_eoi acknowledges a slave line too
        call irq_eoi
        popad
        iretd

;-----------------------------------------------------------------------
; acpi_power_button_dpc - a power button press, acted on outside the handler
;
; Windows 2000 asks; a single-user machine with no session to ask is better
; served by shutting down cleanly than by ignoring the button. The filesystem
; is synced first, because the alternative is losing the last writes.
;-----------------------------------------------------------------------
acpi_power_button_dpc:
        pushad
        mov byte [acpi_pwrbtn_seen], 0
        mov esi, msg_acpi_pwrbtn
        call vga_print
        call serial_print
        call hbfs_sync
        call acpi_poweroff
        popad
        ret

;-----------------------------------------------------------------------
; acpi_install_sci - route the SCI and arm the power button
;   Out: EAX = 0 when installed, -1 when the machine has no SCI
;
; Called once, after the interrupt controller is up: the SCI is a level line
; and cannot be enabled before there is something to acknowledge it with.
;-----------------------------------------------------------------------
acpi_install_sci:
        push ebx
        push ecx
        push edx
        call acpi_pm_own                 ; the PM registers must be ours to arm
        cmp byte [acpi_pm_ready], 1
        jne .ais_fail
        mov eax, [acpi_sci_irq]
        cmp eax, 16
        jae .ais_fail                    ; not a line this machine routes

        ; The gate first: enabling the line before a handler exists is an
        ; interrupt into the default stub, which acknowledges nothing.
        mov ecx, eax
        add ecx, IRQ_VECTOR_BASE
        mov eax, irq_sci
        call idt_set_gate

        ; Arm the fixed event: PM1 enable, at the status block's second half.
        cmp dword [acpi_pm1a_evt], 0
        je .ais_no_arm
        movzx ebx, byte [acpi_pm1_evt_len]
        shr ebx, 1                        ; status block length = enable offset
        test ebx, ebx
        jnz .ais_have_off
        mov ebx, 2
.ais_have_off:
        mov dx, word [acpi_pm1a_evt]
        add dx, bx
        in ax, dx
        or ax, PM1_PWRBTN_EN
        out dx, ax
.ais_no_arm:
        movzx eax, word [acpi_sci_irq]
        call irq_enable

        xor eax, eax
        jmp .ais_out
.ais_fail:
        mov eax, -1
.ais_out:
        pop edx
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; acpi_dump - print what was found, for the `acpi` command
;   Out: EAX = 0 when the tables were read, -1 when they were not
;-----------------------------------------------------------------------
acpi_dump:
        push esi
        push ecx
        push edx
        push ebx

        call acpi_init
        test eax, eax
        jnz .ad_no_rsdp

        mov esi, msg_acpi_rsdp
        call vga_print
        mov eax, [acpi_rsdp_addr]
        call vga_print_hex
        mov esi, msg_acpi_rev
        call vga_print
        movzx eax, byte [acpi_revision]
        call vga_print_hex
        mov al, 0x0A
        call vga_putchar

        mov esi, msg_acpi_root
        call vga_print
        cmp byte [acpi_uses_xsdt], 1
        jne .ad_rsdt
        mov esi, sig_xsdt
        call vga_print
        jmp .ad_root_named
.ad_rsdt:
        mov esi, sig_rsdt
        call vga_print
.ad_root_named:
        mov esi, msg_acpi_at
        call vga_print
        mov eax, [acpi_root_addr]
        call vga_print_hex
        mov esi, msg_acpi_tables
        call vga_print
        mov eax, [acpi_table_count]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar

        ; One line of signatures: this is the machine's table of contents.
        mov esi, msg_acpi_list
        call vga_print
        mov esi, [acpi_root_addr]
        add esi, SDT_HEADER_SIZE
        mov ecx, [acpi_root_addr]
        mov ecx, [ecx + SDT_LENGTH]
        sub ecx, SDT_HEADER_SIZE
        mov edx, 4
        cmp byte [acpi_uses_xsdt], 1
        jne .ad_list_stride
        mov edx, 8
.ad_list_stride:
.ad_list:
        cmp ecx, edx
        jb .ad_list_done
        mov eax, [esi]                   ; table address
        push ecx
        push edx
        push esi
        mov esi, eax
        mov ecx, 4
.ad_list_sig:
        lodsb
        call vga_putchar
        loop .ad_list_sig
        mov al, ' '
        call vga_putchar
        pop esi
        pop edx
        pop ecx
        add esi, edx
        sub ecx, edx
        jmp .ad_list
.ad_list_done:
        mov al, 0x0A
        call vga_putchar

        ; Power: what the machine can be told to do.
        mov esi, msg_acpi_pm
        call vga_print
        cmp byte [acpi_pm_ready], 0
        jne .ad_pm_have
        mov esi, msg_acpi_pm_none
        call vga_print
        jmp .ad_pm_done
.ad_pm_have:
        mov esi, msg_acpi_pm_cnt
        call vga_print
        mov eax, [acpi_pm1a_cnt]
        call vga_print_hex
        mov esi, msg_acpi_pm_evt
        call vga_print
        mov eax, [acpi_pm1a_evt]
        call vga_print_hex
        mov esi, msg_acpi_pm_sci
        call vga_print
        mov eax, [acpi_sci_irq]
        call vga_print_dec
        mov esi, msg_acpi_pm_en
        call vga_print
        cmp byte [acpi_sci_en], 1
        jne .ad_pm_off
        mov esi, msg_acpi_pm_on
        jmp .ad_pm_en_named
.ad_pm_off:
        mov esi, msg_acpi_pm_fw
.ad_pm_en_named:
        call vga_print

        mov esi, msg_acpi_pm_s5
        call vga_print
        movzx eax, byte [acpi_slp_typa + 5]
        cmp eax, 0xFF
        jne .ad_pm_s5_have
        mov esi, msg_acpi_pm_s5_none
        call vga_print
        jmp .ad_pm_s3
.ad_pm_s5_have:
        call vga_print_dec
.ad_pm_s3:
        mov esi, msg_acpi_pm_s3
        call vga_print
        movzx eax, byte [acpi_slp_typa + 3]
        cmp eax, 0xFF
        jne .ad_pm_s3_have
        mov esi, msg_acpi_pm_s5_none
        call vga_print
        jmp .ad_pm_btn
.ad_pm_s3_have:
        call vga_print_dec
.ad_pm_btn:
        mov esi, msg_acpi_pm_btn
        call vga_print
        mov eax, [acpi_pwrbtn_count]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar
.ad_pm_done:

        ; The MADT: what the machine is actually made of.
        mov eax, [acpi_madt_addr]
        test eax, eax
        jz .ad_no_madt
        mov esi, msg_acpi_madt
        call vga_print
        mov eax, [acpi_madt_addr]
        call vga_print_hex
        mov al, 0x0A
        call vga_putchar

        mov esi, msg_acpi_cpus
        call vga_print
        mov eax, [acpi_cpu_count]
        call vga_print_dec
        mov esi, msg_acpi_lapic
        call vga_print
        mov eax, [acpi_lapic_addr]
        call vga_print_hex
        mov al, 0x0A
        call vga_putchar

        mov esi, msg_acpi_irq_mode
        call vga_print
        mov esi, msg_acpi_irq_pic
        cmp byte [irq_mode_apic], 1
        jne .ad_irq_named
        mov esi, msg_acpi_irq_apic
.ad_irq_named:
        call vga_print

        mov esi, msg_acpi_stress
        call vga_print
        mov eax, [smp_stress_frames]
        call vga_print_dec
        mov esi, msg_acpi_stress_err
        call vga_print
        mov eax, [smp_stress_errors]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar

        mov esi, msg_acpi_ids
        call vga_print
        movzx eax, byte [acpi_cpu_apic_ids + 0]
        call vga_print_dec
        mov al, ','
        call vga_putchar
        movzx eax, byte [acpi_cpu_apic_ids + 1]
        call vga_print_dec
        mov al, ','
        call vga_putchar
        movzx eax, byte [acpi_cpu_apic_ids + 2]
        call vga_print_dec
        mov al, ','
        call vga_putchar
        movzx eax, byte [acpi_cpu_apic_ids + 3]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar

        mov esi, msg_acpi_mask
        call vga_print
        mov eax, [smp_worker_mask]
        call vga_print_hex
        mov al, 0x0A
        call vga_putchar

        mov esi, msg_acpi_online
        call vga_print
        mov eax, [smp_cpus_online]
        call vga_print_dec
        mov esi, msg_acpi_of
        call vga_print
        mov eax, [acpi_cpu_count]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar

        mov esi, msg_acpi_clock
        call vga_print
        mov esi, msg_acpi_clock_pit
        cmp dword [lapic_timer_hz], 0
        je .ad_clock_named
        mov esi, msg_acpi_clock_lapic
.ad_clock_named:
        call vga_print
        cmp dword [lapic_timer_hz], 0
        je .ad_clock_done
        mov eax, [lapic_timer_hz]
        call vga_print_dec
        mov esi, msg_acpi_hz
        call vga_print
.ad_clock_done:

        mov esi, msg_acpi_ioapic
        call vga_print
        mov eax, [acpi_ioapic_count]
        call vga_print_dec
        mov esi, msg_acpi_at
        call vga_print
        mov eax, [acpi_ioapic_addr]
        call vga_print_hex
        mov al, 0x0A
        call vga_putchar
        xor eax, eax
        jmp .ad_out
.ad_no_madt:
        mov esi, msg_acpi_none
        call vga_print
        xor eax, eax
        jmp .ad_out

.ad_no_rsdp:
        mov esi, msg_acpi_no_rsdp
        call vga_print
        mov eax, -1
.ad_out:
        pop ebx
        pop edx
        pop ecx
        pop esi
        ret

;-----------------------------------------------------------------------
; acpi_print_banner - one line of machine description for the boot screen
;
; The count is the firmware's, not an assumption: boot with a different number
; of CPUs and this line says so. It is printed before the first prompt, which
; is the only way to read it on a machine whose interrupts this kernel cannot
; yet take (an SMP machine routes them through an IOAPIC nothing programmes
; here, so typing stops working - see docs/ROADMAP.md M2).
;-----------------------------------------------------------------------
acpi_print_banner:
        push esi
        push eax
        push ebx
        call acpi_init
        test eax, eax
        jnz .apb_none

        mov esi, msg_acpi_banner
        call vga_print
        mov eax, [acpi_cpu_count]
        call vga_print_dec
        mov eax, [acpi_cpu_count]
        cmp eax, 1
        jne .apb_plural
        mov esi, msg_acpi_banner_cpu
        call vga_print
        jmp .apb_apic
.apb_plural:
        mov esi, msg_acpi_banner_cpus
        call vga_print
.apb_apic:
        mov esi, msg_acpi_banner_apic
        call vga_print
        mov eax, [acpi_lapic_addr]
        call vga_print_hex
        mov esi, msg_acpi_banner_io
        call vga_print
        mov eax, [acpi_ioapic_count]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar
        jmp .apb_out
.apb_none:
        mov esi, msg_acpi_banner_none
        call vga_print
.apb_out:
        pop ebx
        pop eax
        pop esi
        ret

;-----------------------------------------------------------------------
; acpi_dec_str - write EAX in decimal at EDI, advancing EDI
;   (the VGA printers take a number; the serial line needs a string)
;-----------------------------------------------------------------------
acpi_dec_str:
        push eax
        push ebx
        push ecx
        push edx
        mov ebx, 10
        xor ecx, ecx
        test eax, eax
        jnz .ads_push
        mov byte [edi], '0'
        inc edi
        jmp .ads_done
.ads_push:
        xor edx, edx
        div ebx
        add dl, '0'
        push edx
        inc ecx
        test eax, eax
        jnz .ads_push
.ads_pop:
        pop eax
        stosb
        loop .ads_pop
.ads_done:
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

;-----------------------------------------------------------------------
; acpi_print_serial - the same facts on COM1, for a headless test
;
; The boot screen is 25 rows and the capability banner fills it, so a line
; added near the end can scroll away before a harness reads the screen. The
; serial line is what a test asserts on: ACPI_BOOT cpus=N lapic=0x........
; ioapics=M.
;-----------------------------------------------------------------------
acpi_print_serial:
        pushad
        call acpi_init
        test eax, eax
        jnz .aps_none
        mov edi, acpi_serial_line
        mov esi, acpi_ser_cpus
        call .aps_append
        mov eax, [acpi_cpu_count]
        call acpi_dec_str
        mov esi, acpi_ser_lapic
        call .aps_append
        mov eax, [acpi_lapic_addr]
        call .aps_hex
        mov esi, acpi_ser_io
        call .aps_append
        mov eax, [acpi_ioapic_count]
        call acpi_dec_str
        mov esi, acpi_ser_online
        call .aps_append
        mov eax, [smp_cpus_online]
        call acpi_dec_str
        mov esi, acpi_ser_tick
        call .aps_append
        mov esi, acpi_ser_tick_pit
        cmp dword [lapic_timer_hz], 0
        je .aps_tick_named
        mov esi, acpi_ser_tick_lapic
.aps_tick_named:
        call .aps_append
        mov esi, acpi_ser_irq
        call .aps_append
        mov esi, acpi_ser_pic
        cmp byte [irq_mode_apic], 1
        jne .aps_irq_named
        mov esi, acpi_ser_apic
.aps_irq_named:
        call .aps_append
        mov al, 0x0A
        stosb
        mov byte [edi], 0
        mov esi, acpi_serial_line
        call serial_print
        jmp .aps_out
.aps_none:
        mov esi, acpi_ser_none
        call serial_print
.aps_out:
        popad
        ret
.aps_append:
        lodsb
        test al, al
        jz .aps_append_done
        stosb
        jmp .aps_append
.aps_append_done:
        ret
.aps_hex:
        push eax
        push ebx
        push ecx
        mov ebx, eax
        mov ecx, 8
.aps_hex_loop:
        rol ebx, 4
        mov al, bl
        and al, 0x0F
        cmp al, 10
        jl .aps_hex_digit
        add al, 'A' - 10
        jmp .aps_hex_store
.aps_hex_digit:
        add al, '0'
.aps_hex_store:
        stosb
        loop .aps_hex_loop
        pop ecx
        pop ebx
        pop eax
        ret

acpi_serial_line:  times 96 db 0
acpi_ser_cpus:     db "ACPI_BOOT cpus=", 0
acpi_ser_lapic:    db " lapic=0x", 0
acpi_ser_io:       db " ioapics=", 0
acpi_ser_online:   db " online=", 0
acpi_ser_tick:     db " tick=", 0
acpi_ser_tick_pit: db "pit", 0
acpi_ser_tick_lapic: db "lapic", 0
acpi_ser_irq:      db " irq=", 0
acpi_ser_apic:     db "apic", 0
acpi_ser_pic:      db "pic", 0
acpi_ser_none:     db "ACPI_BOOT none", 10, 0

msg_acpi_banner:      db "  ACPI: ", 0
msg_acpi_banner_cpu:  db " CPU", 0
msg_acpi_banner_cpus: db " CPUs", 0
msg_acpi_banner_apic: db ", local APIC at 0x", 0
msg_acpi_banner_io:   db ", IOAPICs: ", 0
msg_acpi_banner_none: db "  ACPI: no tables found", 0x0A, 0

sig_rsdt:   db "RSDT", 0
sig_xsdt:   db "XSDT", 0
msg_acpi_rsdp:    db "ACPI: RSDP at 0x", 0
msg_acpi_rev:     db " rev 0x", 0
msg_acpi_root:    db "ACPI: ", 0
msg_acpi_at:      db " at 0x", 0
msg_acpi_tables:  db " tables ", 0
msg_acpi_list:    db "ACPI: ", 0
msg_acpi_madt:    db "ACPI: MADT at 0x", 0
msg_acpi_cpus:    db "ACPI: CPUs: ", 0
msg_acpi_lapic:   db "  local APIC at 0x", 0
msg_acpi_ioapic:  db "ACPI: IOAPICs: ", 0
msg_acpi_stress: db "ACPI: allocator stress: ", 0
msg_acpi_stress_err: db " frames, errors ", 0
msg_acpi_ids: db "ACPI: APIC ids: ", 0
msg_acpi_mask: db "ACPI: worker mask: 0x", 0
msg_acpi_online: db "ACPI: processors online: ", 0
msg_acpi_of: db " of ", 0
msg_acpi_clock: db "ACPI: tick clock: ", 0
msg_acpi_clock_lapic: db "local APIC timer, ", 0
msg_acpi_clock_pit: db "PIT", 10, 0
msg_acpi_hz: db " Hz", 10, 0
msg_acpi_irq_mode: db "ACPI: interrupts: ", 0
msg_acpi_irq_apic: db "I/O APIC (8259 masked)", 10, 0
msg_acpi_irq_pic:  db "8259 (no usable local APIC)", 10, 0
msg_acpi_none:    db "ACPI: MADT: none", 10, 0
msg_acpi_pm:      db "ACPI: power: ", 0
msg_acpi_pm_none: db "the FADT describes no PM1 control block", 10, 0
msg_acpi_pm_cnt:  db "PM1a_CNT 0x", 0
msg_acpi_pm_evt:  db "  PM1a_EVT 0x", 0
msg_acpi_pm_sci:  db "  SCI ", 0
msg_acpi_pm_en:   db "  ACPI ", 0
msg_acpi_pm_on:   db "enabled (SCI_EN)", 10, 0
msg_acpi_pm_fw:   db "still the firmware's", 10, 0
msg_acpi_pm_s5:   db "ACPI: S5 sleep type: ", 0
msg_acpi_pm_s5_none: db "not in the DSDT", 0
msg_acpi_pm_s3:   db "  S3 type: ", 0
msg_acpi_pm_btn:  db "  power button presses: ", 0
msg_acpi_off_failed: db "POWER: ACPI could not power the machine off; halted", 10, 0
msg_acpi_pwrbtn:  db "POWER: button pressed, syncing and shutting down", 10, 0
msg_acpi_no_rsdp: db "ACPI: no RSDP found - this machine describes nothing", 10, 0
