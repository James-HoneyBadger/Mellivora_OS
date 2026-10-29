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
BOOTLOG_SIZE        equ 104             ; record stride
BOOTLOG_SRC         equ 16              ; bytes of source name per record

BL_LEVEL            equ 0               ; byte: LOG_* below
BL_CPU              equ 1               ; byte: processor index
BL_TICK             equ 4               ; dword: tick_count when written
BL_TEXT             equ 8
BL_TEXT_MAX         equ BOOTLOG_TEXT - 1
BL_SOURCE           equ 88              ; the word before the first ':'

; Chronicle: the source is derived from the text at the one place a record is
; written, so every line already in the tree gains a source without a hundred
; call sites being edited. "hbfs: volume mounted" is source "hbfs";
; "drv ata: bound" is source "ata" (the word before the colon, not the first
; word); a line with no colon is source "kernel".

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
bootlog_evicted:    dd 0                ; records overwritten when the ring was full
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
        add eax, bootlog_ring
        mov [.blw_slot], eax
        mov edi, eax

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

        ; Source: the word immediately before the first ':', else "kernel".
        mov esi, [.blw_text]
        xor ebx, ebx                    ; start of the current word
        xor ecx, ecx                    ; offset into the text
.blw_scan:
        mov al, [esi + ecx]
        test al, al
        jz .blw_src_kernel
        cmp al, ':'
        je .blw_src_found
        cmp al, ' '
        jne .blw_scan_next
        lea ebx, [ecx + 1]
.blw_scan_next:
        inc ecx
        cmp ecx, 48
        jb .blw_scan
        jmp .blw_src_kernel
.blw_src_found:
        mov edx, ecx
        sub edx, ebx                    ; length of the word
        jz .blw_src_kernel
        cmp edx, BOOTLOG_SRC - 1
        jbe .blw_src_len_ok
        mov edx, BOOTLOG_SRC - 1        ; a name longer than the field is cut
.blw_src_len_ok:
        mov esi, [.blw_text]
        add esi, ebx
        mov edi, [.blw_slot]
        add edi, BL_SOURCE
        mov ecx, edx
        cld
        rep movsb
        mov byte [edi], 0
        jmp .blw_src_done
.blw_src_kernel:
        mov edi, [.blw_slot]
        add edi, BL_SOURCE
        mov esi, msg_src_kernel
        mov ecx, 6
        cld
        rep movsb
        mov byte [edi], 0
.blw_src_done:

        inc dword [bootlog_head]
        cmp dword [bootlog_head], BOOTLOG_MAX
        jb .blw_counted
        mov dword [bootlog_head], 0
.blw_counted:
        cmp dword [bootlog_count], BOOTLOG_MAX
        jb .blw_grow
        inc dword [bootlog_evicted]      ; the ring was full: the oldest went
        jmp .blw_release
.blw_grow:
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
.blw_slot:   dd 0

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

;=======================================================================
; Chronicle - the same ring, asked a narrower question
;
; `chronicle` is `bootlog` with the two filters a log is actually read with:
; a source ("what did the disk say") and a severity floor ("only what went
; wrong"). The records are the ones already there - there is no second log.
;
; Retention is the ring's: 256 records, oldest overwritten first, and the
; overwrites are counted rather than hidden. A log that loses lines silently
; reads as a machine that did nothing.
;=======================================================================

msg_src_kernel: db "kernel", 0

;-----------------------------------------------------------------------
; chronicle_show - print the records that match a filter
;   In : ESI = source name to match, or 0 for every source
;        EAX = level mask, bit LOG_* set for each level to show
;        EDX = most records to print (0 = no limit)
;
; Oldest first. Saying the source on each line is what makes a filtered list
; readable: the text usually repeats it, and a line whose source was derived
; from the text is exactly the one somebody will wonder about.
;-----------------------------------------------------------------------
chronicle_show:
        pushad
        mov [.cs_src], esi
        mov [.cs_mask], eax
        mov [.cs_limit], edx
        mov dword [.cs_printed], 0

        mov esi, msg_chr_head
        call vga_print
        mov eax, [bootlog_count]
        call vga_print_dec
        mov esi, msg_chr_records
        call vga_print
        mov eax, [bootlog_evicted]
        call vga_print_dec
        mov esi, msg_chr_evicted
        call vga_print
        mov eax, [bootlog_dropped]
        call vga_print_dec
        mov esi, msg_chr_dropped
        call vga_print
        mov al, 0x0A
        call vga_putchar

        cmp dword [bootlog_count], 0
        jne .cs_first
        mov esi, msg_chr_empty
        call vga_print
        jmp .cs_out

.cs_first:
        mov eax, [bootlog_head]
        sub eax, [bootlog_count]
        jnc .cs_first_ok
        add eax, BOOTLOG_MAX
.cs_first_ok:
        mov [.cs_index], eax
        mov eax, [bootlog_count]
        mov [.cs_left], eax

.cs_loop:
        cmp dword [.cs_left], 0
        je .cs_done
        cmp dword [.cs_limit], 0
        je .cs_unlimited
        mov eax, [.cs_printed]
        cmp eax, [.cs_limit]
        jae .cs_done
.cs_unlimited:
        mov eax, [.cs_index]
        imul eax, BOOTLOG_SIZE
        lea esi, [bootlog_ring + eax]
        mov [.cs_cur], esi

        ; Level filter.
        movzx eax, byte [esi + BL_LEVEL]
        cmp eax, LOG_DEBUG
        ja .cs_next
        mov ecx, [.cs_mask]
        bt ecx, eax
        jnc .cs_next

        ; Source filter.
        mov ecx, [.cs_src]
        test ecx, ecx
        jz .cs_match
        mov edi, ecx
        lea esi, [esi + BL_SOURCE]
        call chr_ci_equal
        mov esi, [.cs_cur]
        jnc .cs_next
.cs_match:
        call chronicle_print_record
        inc dword [.cs_printed]

.cs_next:
        inc dword [.cs_index]
        cmp dword [.cs_index], BOOTLOG_MAX
        jb .cs_no_wrap
        mov dword [.cs_index], 0
.cs_no_wrap:
        dec dword [.cs_left]
        jmp .cs_loop
.cs_done:
        cmp dword [.cs_printed], 0
        jne .cs_out
        mov esi, msg_chr_none
        call vga_print
.cs_out:
        popad
        ret
.cs_src:     dd 0
.cs_mask:    dd 0
.cs_limit:   dd 0
.cs_printed: dd 0
.cs_index:   dd 0
.cs_left:    dd 0
.cs_cur:     dd 0

; CF set when the strings at ESI and EDI are equal, ASCII case-insensitively.
chr_ci_equal:
        push esi
        push edi
        push eax
        push ebx
.cce_loop:
        mov al, [esi]
        mov bl, [edi]
        cmp al, 'a'
        jb .cce_a_done
        cmp al, 'z'
        ja .cce_a_done
        sub al, 32
.cce_a_done:
        cmp bl, 'a'
        jb .cce_b_done
        cmp bl, 'z'
        ja .cce_b_done
        sub bl, 32
.cce_b_done:
        cmp al, bl
        jne .cce_no
        test al, al
        jz .cce_yes
        inc esi
        inc edi
        jmp .cce_loop
.cce_yes:
        pop ebx
        pop eax
        pop edi
        pop esi
        stc
        ret
.cce_no:
        pop ebx
        pop eax
        pop edi
        pop esi
        clc
        ret

; One word from ESI into EDI (EDX = room), spaces skipped. CF set when none.
chr_word:
        push eax
        push ecx
        push edi
.cw_skip:
        cmp byte [esi], ' '
        jne .cw_start
        inc esi
        jmp .cw_skip
.cw_start:
        cmp byte [esi], 0
        je .cw_none
        mov ecx, edx
        test ecx, ecx
        jz .cw_none
.cw_copy:
        mov al, [esi]
        test al, al
        jz .cw_end
        cmp al, ' '
        je .cw_end
        mov [edi], al
        inc esi
        inc edi
        dec ecx
        jnz .cw_copy
.cw_end:
        mov byte [edi], 0
        pop edi
        pop ecx
        pop eax
        clc
        ret
.cw_none:
        pop edi
        pop ecx
        pop eax
        stc
        ret

; ESI = record -> one line, on the screen and on COM1
chronicle_print_record:
        pushad
        mov [.cpr_rec], esi
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

        ; The source, padded to a fixed width so the columns line up.
        mov eax, BOOTLOG_SRC - 1
        push esi
        add esi, BL_SOURCE
.cpr_src:
        lodsb
        test al, al
        jz .cpr_src_pad
        stosb
        dec eax
        jnz .cpr_src
.cpr_src_pad:
        test eax, eax
        jz .cpr_src_done
.cpr_pad_loop:
        mov byte [edi], ' '
        inc edi
        dec eax
        jnz .cpr_pad_loop
.cpr_src_done:
        pop esi

        mov al, ' '
        stosb
        movzx eax, byte [esi + BL_LEVEL]
        mov al, [bootlog_levels + eax]
        stosb
        mov al, ' '
        stosb

        push esi
        add esi, BL_TEXT
.cpr_text:
        lodsb
        test al, al
        jz .cpr_text_done
        stosb
        jmp .cpr_text
.cpr_text_done:
        pop esi
        mov al, 0x0A
        stosb
        mov byte [edi], 0

        mov esi, bootlog_line
        call vga_print
        mov esi, bootlog_line
        call serial_print
        popad
        ret
.cpr_rec: dd 0

msg_chr_head:    db 0x0A, "Chronicle: ", 0
msg_chr_records: db " records, ", 0
msg_chr_evicted: db " overwritten by age, ", 0
msg_chr_dropped: db " dropped (ring busy)", 0x0A, 0
msg_chr_empty:   db "  (nothing recorded)", 0x0A, 0
msg_chr_usage:
        db "chronicle [-s SOURCE] [-l E|W|I|D] [-n COUNT]", 0x0A
        db "  No filter shows everything, oldest first.", 0x0A
        db 0
msg_chr_none:    db "  (nothing matches)", 0x0A, 0

chr_w_error: db "error", 0
chr_w_warn:  db "warn", 0
chr_w_info:  db "info", 0
chr_w_debug: db "debug", 0

align 4
chr_srcbuf: times BOOTLOG_SRC db 0
chr_tok:    times 12 db 0
chr_mask:   dd 0
chr_limit:  dd 0
chr_src:    dd 0
chr_line:   dd 0

;-----------------------------------------------------------------------
; chronicle_command - `chronicle [-s SOURCE] [-l LEVEL] [-n COUNT]`
;-----------------------------------------------------------------------
chronicle_command:
        pushad
        mov esi, line_buffer
        mov edi, cmd_chronicle_str
        call str_starts_with
        jc .cc_parse
        jmp .cc_usage
.cc_parse:
        mov dword [chr_mask], 0x0F
        mov dword [chr_limit], 0
        mov dword [chr_src], 0

.cc_loop:
        cmp byte [esi], ' '
        jne .cc_got
        inc esi
        jmp .cc_loop
.cc_got:
        cmp byte [esi], 0
        je .cc_show
        cmp byte [esi], '-'
        jne .cc_usage
        mov al, [esi + 1]
        add esi, 2
.cc_flag:
        cmp byte [esi], ' '
        jne .cc_flag_done
        inc esi
        jmp .cc_flag
.cc_flag_done:
        cmp al, 's'
        je .cc_src
        cmp al, 'l'
        je .cc_lev
        cmp al, 'n'
        je .cc_num
        jmp .cc_usage

.cc_src:
        mov edi, chr_srcbuf
        mov edx, BOOTLOG_SRC - 1
        call chr_word
        cmp byte [chr_srcbuf], 0
        je .cc_usage
        mov dword [chr_src], chr_srcbuf
        jmp .cc_loop

.cc_lev:
        mov edi, chr_tok
        mov edx, 11
        call chr_word
        cmp byte [chr_tok], 0
        je .cc_usage
        ; The compares below leave ESI on the token they were given, so the
        ; line cursor is kept here and put back once the level is decided.
        mov [chr_line], esi

        mov esi, chr_tok
        mov edi, chr_w_error
        call chr_ci_equal
        jc .cc_lev_error
        mov esi, chr_tok
        mov edi, chr_w_warn
        call chr_ci_equal
        jc .cc_lev_warn
        mov esi, chr_tok
        mov edi, chr_w_info
        call chr_ci_equal
        jc .cc_lev_info
        mov esi, chr_tok
        mov edi, chr_w_debug
        call chr_ci_equal
        jc .cc_lev_debug
        ; A single letter is also a word here: e, w, i or d.
        mov al, [chr_tok]
        cmp byte [chr_tok + 1], 0
        jne .cc_usage
        cmp al, 'e'
        je .cc_lev_error
        cmp al, 'E'
        je .cc_lev_error
        cmp al, 'w'
        je .cc_lev_warn
        cmp al, 'W'
        je .cc_lev_warn
        cmp al, 'i'
        je .cc_lev_info
        cmp al, 'I'
        je .cc_lev_info
        cmp al, 'd'
        je .cc_lev_debug
        cmp al, 'D'
        je .cc_lev_debug
        jmp .cc_usage
.cc_lev_error:
        mov dword [chr_mask], (1 << LOG_ERROR)
        jmp .cc_lev_done
.cc_lev_warn:
        mov dword [chr_mask], (1 << LOG_WARN)
        jmp .cc_lev_done
.cc_lev_info:
        mov dword [chr_mask], (1 << LOG_INFO)
        jmp .cc_lev_done
.cc_lev_debug:
        mov dword [chr_mask], (1 << LOG_DEBUG)
.cc_lev_done:
        mov esi, [chr_line]
        jmp .cc_loop

.cc_num:
        mov edi, chr_tok
        mov edx, 11
        call chr_word
        cmp byte [chr_tok], 0
        je .cc_usage
        lea esi, [chr_tok]
        call parse_decimal
        test eax, eax
        jz .cc_usage
        mov [chr_limit], eax
        jmp .cc_loop                     ; ESI is already past the count

.cc_show:
        mov esi, [chr_src]
        mov eax, [chr_mask]
        mov edx, [chr_limit]
        call chronicle_show
        jmp .cc_done
.cc_usage:
        mov esi, msg_chr_usage
        call vga_print
.cc_done:
        popad
        ret

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
log_sessions:    db "session: 0 exists for services, nobody logged on", 0
log_services:    db "scm: boot and system services started", 0
log_locks:       db "selftest: lock order gate refuses the wrong order", 0
log_pmmtest:     db "selftest: frame allocator preserves registers", 0
log_fputest:     db "selftest: x87 and SSE state round trip", 0
log_aestest:     db "selftest: AES-256 XTS known answer", 0
log_cmod:        db "cmod: C module loaded", 0
log_boot_done:   db "boot: ready for the shell", 0
log_core_devices: db "drivers: interrupt controller, clock, keyboard, serial bound", 0
log_devices:     db "drivers: device drivers bound", 0
log_acpi_none:   db "acpi: no tables; power features unavailable", 0
