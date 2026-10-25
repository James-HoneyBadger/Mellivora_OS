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
        call vga_print_hex
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
msg_acpi_tables:  db " tables 0x", 0
msg_acpi_list:    db "ACPI: ", 0
msg_acpi_madt:    db "ACPI: MADT at 0x", 0
msg_acpi_cpus:    db "ACPI: CPUs: ", 0
msg_acpi_lapic:   db "  local APIC at 0x", 0
msg_acpi_ioapic:  db "ACPI: IOAPICs: ", 0
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
msg_acpi_no_rsdp: db "ACPI: no RSDP found - this machine describes nothing", 10, 0
