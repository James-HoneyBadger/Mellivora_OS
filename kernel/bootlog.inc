;=======================================================================
; bootlog.inc - what the machine did on the way up, in order, with levels
;
; A machine that fails to enumerate anything has one question to answer: which
; step, on which processor, at what time, said no. printf-style output answers
; it only if somebody already thought to print there, and on a serial console
; the answer scrolls away. This keeps the same information in a ring the
; machine can be asked for afterwards - `bootlog` prints it - and mirrors it to
; COM1 as it happens so a machine that dies mid-boot still said why.
;
; A record is level, processor, tick and text. Four things matter about the
; implementation:
;
;   * it can be called from anywhere, including an interrupt handler, so it
;     takes no blocking lock: a writer that finds the ring busy drops the
;     record and counts it. A diagnostic that can deadlock the machine it is
;     diagnosing is worse than one that loses a line under contention;
;   * it allocates nothing: the ring is a static array, so logging works before
;     the frame allocator does and inside the allocator's own failure paths;
;   * the processor number comes from the same source the lock gate uses, so a
;     log line and a lock report cannot disagree about which processor spoke;
;   * text is copied, never referenced, because most callers pass a scratch
;     buffer they are about to reuse.
;=======================================================================

BOOTLOG_MAX         equ 256             ; records kept
BOOTLOG_TEXT        equ 80              ; bytes of text per record
BOOTLOG_SIZE        equ 96              ; record stride

BL_LEVEL            equ 0               ; byte: LOG_* below
BL_CPU              equ 1               ; byte: processor index
BL_TICK             equ 4               ; dword: tick_count when written
BL_TEXT             equ 8
BL_TEXT_MAX         equ BOOTLOG_TEXT - 1

LOG_ERROR           equ 0
LOG_WARN            equ 1
LOG_INFO            equ 2
LOG_DEBUG           equ 3

; Records at or above this level are mirrored to COM1 as they are written.
; DEBUG is kept for `bootlog`: a driver saying "no device" is useful in the ring
; and noise on the console of a machine that boots every ten seconds.
;
; Nothing is mirrored until the port is initialised: serial_print waits for the
; transmitter to be ready, and before serial_init that wait is forever. Records
; written earlier are still kept in the ring - which is the point of having a
; ring rather than printing as you go.
bootlog_serial_min: db 0xFF             ; 0xFF = no level reaches it

bootlog_ring:       times BOOTLOG_MAX * BOOTLOG_SIZE db 0
bootlog_head:       dd 0                ; next slot to write
bootlog_count:      dd 0                ; records held, up to BOOTLOG_MAX
bootlog_dropped:    dd 0                ; records lost to contention
bootlog_busy:       db 0
bootlog_levels:     db "EWID"           ; index by LOG_*

;-----------------------------------------------------------------------
; bootlog_write - append one record, and say it on COM1
;   In : AL = level (LOG_*), ESI = NUL-terminated text (copied)
;   Out: EAX = 0 written, -1 dropped because another processor was writing
;
; Every register is preserved. The text is truncated to the record's width
; rather than truncated with an ellipsis: a diagnostic that fits is the caller's
; problem, and losing the tail is losing the reason.
;-----------------------------------------------------------------------
bootlog_write:
        pushad
        mov [.blw_level], al
        mov [.blw_text], esi

        ; Claim the ring without ever waiting for it.
        mov al, 1
        xchg al, [bootlog_busy]
        test al, al
        jz .blw_claimed
        inc dword [bootlog_dropped]
        mov dword [.blw_status], -1
        jmp .blw_out

.blw_claimed:
        call lock_self_ipi
        mov [.blw_cpu], al

        ; Slot = head, then head advances and wraps.
        mov eax, [bootlog_head]
        imul eax, BOOTLOG_SIZE
        lea edi, [bootlog_ring + eax]

        mov al, [.blw_level]
        mov [edi + BL_LEVEL], al
        mov al, [.blw_cpu]
        mov [edi + BL_CPU], al
        mov eax, [tick_count]
        mov [edi + BL_TICK], eax

        lea edi, [edi + BL_TEXT]
        mov esi, [.blw_text]
        mov ecx, BL_TEXT_MAX
.blw_copy:
        test ecx, ecx
        jz .blw_terminate
        lodsb
        test al, al
        jz .blw_terminate
        stosb
        dec ecx
        jmp .blw_copy
.blw_terminate:
        mov byte [edi], 0

        inc dword [bootlog_head]
        cmp dword [bootlog_head], BOOTLOG_MAX
        jb .blw_counted
        mov dword [bootlog_head], 0
.blw_counted:
        cmp dword [bootlog_count], BOOTLOG_MAX
        jae .blw_release
        inc dword [bootlog_count]
.blw_release:
        mov byte [bootlog_busy], 0

        ; Mirror it. From here on the ring is not held, so a fault in the
        ; serial path cannot leave the log locked for the rest of time.
        mov al, [.blw_level]
        movzx eax, al
        cmp al, [bootlog_serial_min]
        ja .blw_done

        mov edi, bootlog_line
        mov al, '['
        stosb
        mov eax, [.blw_cpu]
        add al, '0'
        stosb
        mov al, ' '
        stosb
        movzx eax, byte [.blw_level]
        mov al, [bootlog_levels + eax]
        stosb
        mov al, ']'
        stosb
        mov al, ' '
        stosb
        mov esi, [.blw_text]
.blw_mirror:
        lodsb
        test al, al
        jz .blw_mirror_done
        stosb
        jmp .blw_mirror
.blw_mirror_done:
        mov al, 10
        stosb
        mov byte [edi], 0
        mov esi, bootlog_line
        call serial_print
.blw_done:
        mov dword [.blw_status], 0

.blw_out:
        mov eax, [.blw_status]
        mov [esp + 28], eax
        popad
        ret
.blw_level:  db 0
.blw_cpu:    db 0
.blw_text:   dd 0
.blw_status: dd 0

bootlog_line: times BOOTLOG_TEXT + 8 db 0

;-----------------------------------------------------------------------
; bootlog_show - the `bootlog` command
;
; Oldest first, one line per record, with the processor and the tick so two
; processors interleaving can be read as two stories rather than one confusing
; one. A drop counter that is not zero is printed too: a log that silently lost
; lines reads as a machine that did nothing.
;-----------------------------------------------------------------------
bootlog_show:
        pushad
        mov esi, msg_bootlog_head
        call vga_print
        mov eax, [bootlog_count]
        call vga_print_dec
        mov esi, msg_bootlog_records
        call vga_print
        mov eax, [bootlog_dropped]
        call vga_print_dec
        mov esi, msg_bootlog_dropped
        call vga_print
        mov al, 0x0A
        call vga_putchar

        mov eax, [bootlog_count]
        test eax, eax
        jz .bls_none

        ; Oldest record: head - count, wrapped.
        mov eax, [bootlog_head]
        sub eax, [bootlog_count]
        jnc .bls_first_ok
        add eax, BOOTLOG_MAX
.bls_first_ok:
        mov [.bls_index], eax
        mov dword [.bls_left], 0
        mov eax, [bootlog_count]
        mov [.bls_left], eax

.bls_loop:
        cmp dword [.bls_left], 0
        je .bls_done
        mov eax, [.bls_index]
        imul eax, BOOTLOG_SIZE
        lea esi, [bootlog_ring + eax]

        ; "  cpuN Lt: text", on the screen and on COM1. The screen scrolls, and
        ; a ring with more records than a screen has lines would leave the early
        ; ones unreadable from outside the guest; the serial transcript does not
        ; scroll, so the same line goes to both.
        mov edi, bootlog_line
        mov al, ' '
        stosb
        mov al, ' '
        stosb
        mov al, 'c'
        stosb
        mov al, 'p'
        stosb
        mov al, 'u'
        stosb
        movzx eax, byte [esi + BL_CPU]
        add al, '0'
        stosb
        mov al, ' '
        stosb
        movzx eax, byte [esi + BL_LEVEL]
        mov al, [bootlog_levels + eax]
        stosb
        mov al, ' '
        stosb

        push esi
        add esi, BL_TEXT
.bls_text:
        lodsb
        test al, al
        jz .bls_text_done
        stosb
        jmp .bls_text
.bls_text_done:
        pop esi
        mov al, 0x0A
        stosb
        mov byte [edi], 0

        ; The line is built; say it twice.
        push esi
        mov esi, bootlog_line
        call vga_print
        mov esi, bootlog_line
        call serial_print
        pop esi

        inc dword [.bls_index]
        cmp dword [.bls_index], BOOTLOG_MAX
        jb .bls_no_wrap
        mov dword [.bls_index], 0
.bls_no_wrap:
        dec dword [.bls_left]
        jmp .bls_loop
.bls_done:
        jmp .bls_out
.bls_none:
        mov esi, msg_bootlog_empty
        call vga_print
.bls_out:
        popad
        ret
.bls_index: dd 0
.bls_left:  dd 0

msg_bootlog_head:    db 0x0A, "Boot log: ", 0
msg_bootlog_records: db " records, ", 0
msg_bootlog_dropped: db " dropped (ring busy)", 0x0A, 0
msg_bootlog_empty:   db "  (nothing recorded)", 0x0A, 0

;-----------------------------------------------------------------------
; The stages the boot path reports. Named here rather than at the call sites
; so the wording stays consistent and one grep finds every stage.
;-----------------------------------------------------------------------
log_core:        db "core: vga, 8259, idt, pit and keyboard up", 0
log_pmm:         db "pmm: frame allocator ready", 0
log_kmem:        db "kmem: kernel heap ready", 0
log_ata:         db "ata: PIO disk ready", 0
log_serial:      db "serial: COM1 online", 0
log_cpu:         db "cpu: Pentium 4, SSE enabled", 0
log_sched:       db "sched: TSS and scheduler ready", 0
log_vm:          db "vm: paging and demand regions ready", 0
log_pci:         db "pci: enumeration complete", 0
log_acpi:        db "acpi: tables read, power hardware taken over", 0
log_apic:        db "apic: interrupt controller chosen", 0
log_fs:          db "hbfs: volume mounted", 0
log_volumes:     db "volumes: partition table read", 0
log_vfs:         db "vfs: file tables ready", 0
log_locks:       db "selftest: lock order gate refuses the wrong order", 0
log_pmmtest:     db "selftest: frame allocator preserves registers", 0
log_fputest:     db "selftest: x87 and SSE state round trip", 0
log_aestest:     db "selftest: AES-256 XTS known answer", 0
log_cmod:        db "cmod: C module loaded", 0
log_boot_done:   db "boot: ready for the shell", 0
log_core_devices: db "drivers: interrupt controller, clock, keyboard, serial bound", 0
log_devices:     db "drivers: device drivers bound", 0
log_acpi_none:   db "acpi: no tables; power features unavailable", 0
