;=======================================================================
; UTILITY FUNCTIONS
;=======================================================================

; Detect file type from filename extension
; ESI = filename (preserved)
; Returns: EDX = file type (FTYPE_TEXT, FTYPE_EXEC, FTYPE_BATCH)
; Input EDX is used as default if no extension matched
detect_file_type:
        pushad
        ; Find end of filename and last '.'
        mov edi, esi
        xor ecx, ecx           ; Track position
        xor ebx, ebx           ; Last dot position (0 = none)
.dft_scan:
        mov al, [edi + ecx]
        cmp al, 0
        je .dft_check
        cmp al, '.'
        jne .dft_not_dot
        lea ebx, [edi + ecx + 1] ; EBX = pointer past the dot
.dft_not_dot:
        inc ecx
        jmp .dft_scan
.dft_check:
        cmp ebx, 0
        je .dft_done            ; No dot found, keep default EDX
        ; Compare extension (case insensitive)
        ; Check for .bat
        cmp byte [ebx], 'b'
        jne .dft_try_B
        cmp byte [ebx+1], 'a'
        jne .dft_done
        cmp byte [ebx+2], 't'
        jne .dft_done
        cmp byte [ebx+3], 0
        jne .dft_done
        mov dword [esp + 20], FTYPE_BATCH ; EDX in pushad frame
        jmp .dft_done
.dft_try_B:
        cmp byte [ebx], 'B'
        jne .dft_try_bin
        cmp byte [ebx+1], 'A'
        jne .dft_done
        cmp byte [ebx+2], 'T'
        jne .dft_done
        cmp byte [ebx+3], 0
        jne .dft_done
        mov dword [esp + 20], FTYPE_BATCH
        jmp .dft_done
.dft_try_bin:
        ; Check for .bin
        cmp byte [ebx], 'b'
        jne .dft_try_BIN
        cmp byte [ebx+1], 'i'
        jne .dft_done
        cmp byte [ebx+2], 'n'
        jne .dft_done
        cmp byte [ebx+3], 0
        jne .dft_done
        mov dword [esp + 20], FTYPE_EXEC
        jmp .dft_done
.dft_try_BIN:
        cmp byte [ebx], 'B'
        jne .dft_done
        cmp byte [ebx+1], 'I'
        jne .dft_done
        cmp byte [ebx+2], 'N'
        jne .dft_done
        cmp byte [ebx+3], 0
        jne .dft_done
        mov dword [esp + 20], FTYPE_EXEC
.dft_done:
        popad
        ret

;---------------------------------------
; parse_flags - Parse '-xyz' flag arguments from command line
; Input:  ESI = command line (after command name)
; Output: [cmd_flags] = bitmask of flags found (bits 0-25 = a-z)
;         [cmd_flag_num] = numeric value if -n N was present (0 if not)
;         ESI advanced past all flag arguments
; Flags are single letters after '-'. Multiple flags can be given:
;   -l -n 20    or    -ln 20
; '--' stops flag parsing (everything after is positional)
;---------------------------------------
parse_flags:
        pushad
        mov dword [cmd_flags], 0
        mov dword [cmd_flag_num], 0
.pf_loop:
        cmp byte [esi], '-'
        jne .pf_done
        cmp byte [esi + 1], 0   ; lone '-' is not a flag
        je .pf_done
        cmp byte [esi + 1], ' ' ; '-' followed by space is not a flag
        je .pf_done
        ; Check for '--' (end of flags)
        cmp byte [esi + 1], '-'
        jne .pf_parse
        add esi, 2              ; skip '--'
        call skip_spaces
        jmp .pf_done
.pf_parse:
        inc esi                 ; skip '-'
.pf_chars:
        movzx eax, byte [esi]
        cmp al, 0
        je .pf_next
        cmp al, ' '
        je .pf_next
        ; Is it a letter a-z?
        cmp al, 'a'
        jl .pf_try_upper
        cmp al, 'z'
        jg .pf_next
        sub al, 'a'
        bts [cmd_flags], eax
        inc esi
        ; If flag is 'n', read the next numeric arg
        cmp al, 'n' - 'a'
        je .pf_read_num_after
        jmp .pf_chars
.pf_try_upper:
        cmp al, 'A'
        jl .pf_next
        cmp al, 'Z'
        jg .pf_next
        sub al, 'A'
        bts [cmd_flags], eax    ; map to same bit as lowercase
        inc esi
        cmp al, 'n' - 'a'
        je .pf_read_num_after
        jmp .pf_chars
.pf_next:
        call skip_spaces
        jmp .pf_loop
.pf_read_num_after:
        ; Check if next char (after possible space) is a digit
        cmp byte [esi], ' '
        jne .pf_check_digit
        call skip_spaces
.pf_check_digit:
        cmp byte [esi], '0'
        jl .pf_chars
        cmp byte [esi], '9'
        jg .pf_chars
        ; Parse decimal number
        xor eax, eax
.pf_num:
        movzx ebx, byte [esi]
        sub ebx, '0'
        cmp ebx, 9
        ja .pf_num_done
        imul eax, 10
        add eax, ebx
        inc esi
        jmp .pf_num
.pf_num_done:
        mov [cmd_flag_num], eax
        jmp .pf_next
.pf_done:
        popad
        ret

; Test if flag bit is set. EAX = bit number (0='a', 1='b', etc.)
; Returns: CF set if flag is present
test_flag:
        bt [cmd_flags], eax
        ret

; Skip spaces in string at ESI
skip_spaces:
        cmp byte [esi], ' '
        jne .done
        inc esi
        jmp skip_spaces
.done:
        ret

; Copy word from ESI to EDI (stop at space or null)
; Advances ESI past the word
copy_word:
        lodsb
        cmp al, ' '
        je .done
        cmp al, 0
        je .done_null
        stosb
        jmp copy_word
.done:
        mov byte [edi], 0
        ret
.done_null:
        mov byte [edi], 0
        dec esi                 ; Put back the null
        ret

; Bounded copy word from ESI to EDI (stop at space, null, or ECX limit)
; Input:  ESI = source, EDI = destination, ECX = max bytes (excluding null)
; Advances ESI past the word; always null-terminates destination
copy_word_n:
        push ecx
.cwn_loop:
        cmp ecx, 0
        jle .cwn_limit
        lodsb
        cmp al, ' '
        je .cwn_space
        cmp al, 0
        je .cwn_null
        stosb
        dec ecx
        jmp .cwn_loop
.cwn_limit:
        ; Hit limit — skip remaining word chars in source
.cwn_skip:
        lodsb
        cmp al, ' '
        je .cwn_space
        cmp al, 0
        je .cwn_null
        jmp .cwn_skip
.cwn_space:
        mov byte [edi], 0
        pop ecx
        ret
.cwn_null:
        mov byte [edi], 0
        dec esi
        pop ecx
        ret

; Check for wildcard characters '*' or '?' in string at ESI
; Returns: CF set if wildcard exists
str_has_wildcards:
        push esi
.shw_loop:
        mov al, [esi]
        test al, al
        jz .shw_no
        cmp al, '*'
        je .shw_yes
        cmp al, '?'
        je .shw_yes
        inc esi
        jmp .shw_loop
.shw_yes:
        pop esi
        stc
        ret
.shw_no:
        pop esi
        clc
        ret

; Check for '*' in string at ESI
; Returns: CF set if found
; Preserves: ESI
str_has_asterisk:
        push esi
.sha_loop:
        mov al, [esi]
        test al, al
        jz .sha_no
        cmp al, '*'
        je .sha_yes
        inc esi
        jmp .sha_loop
.sha_yes:
        pop esi
        stc
        ret
.sha_no:
        pop esi
        clc
        ret

; Wildcard match
; ESI = pattern (supports '*' and '?')
; EDI = candidate string
; Returns: CF set if match, CF clear if not
wildcard_match:
        push eax
        push ebx
        push edx

        mov dword [wild_star_pat], 0
        mov dword [wild_star_txt], 0

.wm_loop:
        mov al, [esi]
        mov bl, [edi]

        cmp al, '*'
        jne .wm_not_star
        mov [wild_star_pat], esi
        mov [wild_star_txt], edi
        inc esi
        jmp .wm_loop

.wm_not_star:
        test al, al
        jne .wm_pat_not_end
        test bl, bl
        jz .wm_match
        jmp .wm_backtrack

.wm_pat_not_end:
        test bl, bl
        jz .wm_backtrack

        cmp al, '?'
        je .wm_consume
        cmp al, bl
        jne .wm_backtrack

.wm_consume:
        inc esi
        inc edi
        jmp .wm_loop

.wm_backtrack:
        mov edx, [wild_star_pat]
        test edx, edx
        jz .wm_fail

        mov edx, [wild_star_txt]
        cmp byte [edx], 0
        je .wm_fail
        inc edx
        mov [wild_star_txt], edx
        mov edi, edx
        mov esi, [wild_star_pat]
        inc esi
        jmp .wm_loop

.wm_match:
        stc
        jmp .wm_done

.wm_fail:
        clc

.wm_done:
        pop edx
        pop ebx
        pop eax
        ret

; Expand destination template with '*'
; ESI = template
; EDI = output buffer
; EDX = replacement text (usually matched source filename)
wildcard_expand_star:
        push eax
        push ebx
        push ecx
        push esi
        push edx
        xor ecx, ecx

.wes_loop:
        lodsb
        test al, al
        jz .wes_done
        cmp ecx, HBFS_MAX_FILENAME
        jge .wes_done
        cmp al, '*'
        jne .wes_copy_char

        ; splice replacement text
        mov ebx, edx
.wes_rep:
        mov al, [ebx]
        test al, al
        jz .wes_loop
        cmp ecx, HBFS_MAX_FILENAME
        jge .wes_done
        stosb
        inc ebx
        inc ecx
        jmp .wes_rep

.wes_copy_char:
        stosb
        inc ecx
        jmp .wes_loop

.wes_done:
        mov byte [edi], 0
        pop edx
        pop esi
        pop ecx
        pop ebx
        pop eax
        ret

; Compare two null-terminated strings at ESI and EDI
; Returns: ZF set if strings are equal
; Preserves: ESI, EDI
str_compare:
        push esi
        push edi
.sc_loop:
        mov al, [esi]
        cmp al, [edi]
        jne .sc_ne
        test al, al
        jz .sc_eq
        inc esi
        inc edi
        jmp .sc_loop
.sc_eq:
        pop edi
        pop esi
        xor eax, eax          ; ZF=1
        ret
.sc_ne:
        pop edi
        pop esi
        or eax, 1             ; ZF=0
        ret

; Check if string at ESI starts with string at EDI
; Returns: CF set if match, ESI advanced past match
str_starts_with:
        push esi
        push edi
.loop:
        mov al, [edi]
        test al, al
        jz .match               ; End of pattern = match

        cmp al, [esi]
        jne .no_match

        inc esi
        inc edi
        jmp .loop

.match:
        ; Check that next char is space or null (word boundary)
        mov al, [esi]
        cmp al, ' '
        je .yes
        cmp al, 0
        je .yes
        jmp .no_match

.yes:
        pop edi
        add esp, 4              ; Discard saved ESI
        ; Skip spaces after command
        call skip_spaces
        stc
        ret

.no_match:
        pop edi
        pop esi
        clc
        ret

; Parse hex byte from string at ESI
; Returns: AL = byte value, CF clear if valid byte, CF set if end of input
parse_hex_byte:
.skip:
        cmp byte [esi], ' '
        jne .start
        inc esi
        jmp .skip

.start:
        cmp byte [esi], 0
        je .end

        call .nibble
        jc .end                 ; No valid nibble found = end of input
        shl al, 4
        mov ah, al
        call .nibble
        jc .end_one             ; Only got high nibble
        or al, ah
        clc                     ; CF=0 = valid byte
        ret

.end_one:
        mov al, ah              ; Return just the high nibble
        clc                     ; CF=0 = valid byte
        ret

.end:
        stc                     ; CF=1 = no more data
        ret

; Read one hex nibble from [ESI]
; Returns: AL = 0-15, CF clear on success; CF set on end-of-string
.nibble:
        mov al, [esi]
        cmp al, 0
        je .nib_end
        inc esi

        cmp al, '0'
        jl .nib_skip
        cmp al, '9'
        jle .nib_digit
        cmp al, 'A'
        jl .nib_skip
        cmp al, 'F'
        jle .nib_upper
        cmp al, 'a'
        jl .nib_skip
        cmp al, 'f'
        jle .nib_lower
.nib_skip:
        jmp .nibble             ; Skip non-hex chars

.nib_digit:
        sub al, '0'
        clc                     ; CF=0 = success
        ret
.nib_upper:
        sub al, 'A' - 10
        clc
        ret
.nib_lower:
        sub al, 'a' - 10
        clc
        ret
.nib_end:
        stc                     ; CF=1 = end of string
        ret

;---------------------------------------
; Print system information at boot
;---------------------------------------
print_sysinfo:
        pushad
        mov byte [vga_color], COLOR_INFO

        mov esi, msg_cpu
        call vga_print

        mov esi, msg_mem_detect
        call vga_print
        mov eax, [total_free_pages]
        shl eax, 2
        shr eax, 10
        call vga_print_dec
        mov esi, msg_mb
        call vga_print

        mov esi, msg_serial_info
        call vga_print

        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;=======================================================================
; SERIAL PORT (COM1) DRIVER
;=======================================================================

serial_init:
        pushad

        ; Probe: write scratch register, read back to detect hardware
        mov dx, COM1_PORT + 7   ; Scratch register
        mov al, 0xAE
        out dx, al
        in al, dx
        cmp al, 0xAE
        jne .no_serial

        mov dx, COM1_PORT + 1   ; IER - disable interrupts
        xor al, al
        out dx, al
        mov dx, COM1_PORT + 3   ; LCR - enable DLAB
        mov al, 0x80
        out dx, al
        mov dx, COM1_PORT       ; Divisor low (1 = 115200 baud)
        mov al, 1
        out dx, al
        mov dx, COM1_PORT + 1   ; Divisor high
        xor al, al
        out dx, al
        mov dx, COM1_PORT + 3   ; 8N1
        mov al, 0x03
        out dx, al
        mov dx, COM1_PORT + 2   ; Enable FIFO, clear, 14-byte threshold
        mov al, 0xC7
        out dx, al
        mov dx, COM1_PORT + 4   ; RTS/DTR
        mov al, 0x03
        out dx, al
        mov byte [serial_present], 1
        popad
        ret

.no_serial:
        mov byte [serial_present], 0
        popad
        ret

; Write one character to COM1
; AL = character to send
; No-op if serial not present
serial_putchar:
        cmp byte [serial_present], 0
        je .skip
        push edx
        push eax
        mov ah, al
.wait:  mov dx, COM1_LSR
        in al, dx
        test al, 0x20           ; Transmit Holding Register Empty
        jz .wait
        mov al, ah
        mov dx, COM1_PORT
        out dx, al
        pop eax
        pop edx
.skip:  ret

; Write null-terminated string to COM1
; ESI = string pointer
serial_print:
        pushad
.loop:  lodsb
        or al, al
        jz .done
        call serial_putchar
        jmp .loop
.done:  popad
        ret

; Non-blocking read one character from COM1
; Returns: AL = character if available, or 0xFF (-1 unsigned) if none
; Preserves all registers except EAX
serial_getchar:
        cmp byte [serial_present], 0
        je .none
        push edx
        mov dx, COM1_LSR
        in al, dx
        test al, 0x01           ; Data Ready bit
        jz .no_data
        mov dx, COM1_PORT
        in al, dx
        pop edx
        ret
.no_data:
        pop edx
.none:  mov al, 0xFF
        ret

;=======================================================================
; RTC (Real-Time Clock) via CMOS
;=======================================================================

rtc_read_reg:
        out RTC_INDEX, al
        jmp short $+2
        in al, RTC_DATA
        ret

rtc_bcd_to_bin:
        push ecx
        mov cl, al
        shr al, 4
        mov ah, 10
        mul ah              ; AX = high_nibble * 10
        and cl, 0x0F
        add al, cl
        xor ah, ah
        pop ecx
        ret

rtc_read_time:
        pushad
.wait:  mov al, 0x0A
        call rtc_read_reg
        test al, 0x80
        jnz .wait
        mov al, 0x00
        call rtc_read_reg
        call rtc_bcd_to_bin
        mov [rtc_sec], al
        mov al, 0x02
        call rtc_read_reg
        call rtc_bcd_to_bin
        mov [rtc_min], al
        mov al, 0x04
        call rtc_read_reg
        call rtc_bcd_to_bin
        mov [rtc_hour], al
        mov al, 0x07
        call rtc_read_reg
        call rtc_bcd_to_bin
        mov [rtc_day], al
        mov al, 0x08
        call rtc_read_reg
        call rtc_bcd_to_bin
        mov [rtc_month], al
        mov al, 0x09
        call rtc_read_reg
        call rtc_bcd_to_bin
        mov [rtc_year], al
        mov al, 0x32
        call rtc_read_reg
        cmp al, 0
        je .def_century
        call rtc_bcd_to_bin
        mov [rtc_century], al
        jmp .rtc_done
.def_century:
        mov byte [rtc_century], 20
.rtc_done:
        popad
        ret

;=======================================================================
; PC SPEAKER
;=======================================================================

speaker_beep:
        pushad
        mov eax, PIT_FREQ
        xor edx, edx
        div ebx
        push eax
        mov al, 0xB6
        out PIT_CMD, al
        pop eax
        out PIT_CH2, al
        mov al, ah
        out PIT_CH2, al
        in al, SPEAKER_PORT
        or al, 0x03
        out SPEAKER_PORT, al
        mov eax, [tick_count]
        add ecx, eax
.bwait: hlt
        cmp [tick_count], ecx
        jl .bwait
        call speaker_off
        popad
        ret

speaker_off:
        push eax
        push edx
        in al, SPEAKER_PORT
        and al, 0xFC
        out SPEAKER_PORT, al
        pop edx
        pop eax
        ret

;=======================================================================
; TSS (Task State Segment) for Ring 3
;=======================================================================

tss_init:
        pushad
        mov edi, tss_struct
        mov ecx, 104 / 4
        xor eax, eax
        rep stosd
        mov dword [tss_struct + 4], KERNEL_STACK
        mov dword [tss_struct + 8], 0x10
        mov word [tss_struct + 102], 104

        sub esp, 8
        sgdt [esp]
        mov ebx, [esp + 2]
        add esp, 8
        add ebx, TSS_SEL
        mov eax, tss_struct
        mov [ebx + 2], ax
        shr eax, 16
        mov [ebx + 4], al
        mov [ebx + 7], ah

        mov ax, TSS_SEL
        ltr ax
        popad
        ret

;=======================================================================
; ELF32 LOADER
;=======================================================================

elf_load_program:
        pushad
        cmp byte [PROGRAM_BASE + 4], 1
        jne .elf_bad
        cmp byte [PROGRAM_BASE + 5], 1
        jne .elf_bad

        movzx ecx, word [PROGRAM_BASE + 44]
        mov ebx, [PROGRAM_BASE + 28]
        mov edi, [PROGRAM_BASE + 24]
        mov [.elf_entry], edi

        add ebx, PROGRAM_BASE
.load_seg:
        cmp ecx, 0
        je .elf_ok
        cmp dword [ebx], ELF_PT_LOAD
        jne .next_ph

        push ecx
        mov esi, PROGRAM_BASE
        add esi, [ebx + 4]
        mov edi, [ebx + 8]
        mov ecx, [ebx + 16]
        cmp ecx, 0
        je .zero_bss
        cmp esi, edi
        je .zero_bss
        rep movsb
.zero_bss:
        mov ecx, [ebx + 20]
        sub ecx, [ebx + 16]
        jle .seg_ok
        xor al, al
        rep stosb
.seg_ok:
        pop ecx

.next_ph:
        add ebx, ELF_PHDR_SIZE
        dec ecx
        jmp .load_seg

.elf_ok:
        mov eax, [.elf_entry]
        mov [esp + 28], eax
        popad
        clc
        ret

.elf_bad:
        popad
        stc
        ret

.elf_entry: dd 0

;=======================================================================
; FILE DESCRIPTOR TABLE
;=======================================================================

fd_open:
        pushad
        mov [.fd_mode], ecx
        xor ebx, ebx
.find_fd:
        cmp ebx, FD_MAX
        jge .no_fd
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE
        cmp dword [fd_table + eax], FD_FLAG_CLOSED
        je .got_fd
        inc ebx
        jmp .find_fd
.got_fd:
        push ebx
        call hbfs_find_file_global
        pop ebx
        jc .no_fd
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE
        mov ecx, [.fd_mode]
        mov [fd_table + eax], ecx
        mov ecx, [edi + DIRENT_SIZE]
        mov [fd_table + eax + 4], ecx
        mov ecx, [edi + DIRENT_START_BLOCK]
        mov [fd_table + eax + 8], ecx
        mov dword [fd_table + eax + 12], 0
        mov ecx, [edi + DIRENT_BLOCK_COUNT]
        mov [fd_table + eax + 16], ecx
        ; Record which directory this file lives in (for fd_close)
        mov ecx, [current_dir_lba]
        mov [fd_table + eax + 20], ecx
        mov ecx, [current_dir_sects]
        mov [fd_table + eax + 24], ecx
        ; Restore CWD if global search changed it
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .fd_no_restore
        call gff_restore_cwd
.fd_no_restore:
        mov [esp + 28], ebx
        popad
        ret
.no_fd:
        popad
        mov eax, -1
        ret
.fd_mode: dd 0

fd_read:
        pushad
        cmp ebx, FD_MAX
        jge .frd_err
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE
        cmp dword [fd_table + eax], FD_FLAG_CLOSED
        je .frd_err
        mov esi, [fd_table + eax + 12]
        mov edi, [fd_table + eax + 4]
        sub edi, esi
        jle .frd_eof
        cmp edx, edi
        jle .frd_ok
        mov edx, edi
.frd_ok:
        ; Read only the block containing current position
        push ecx
        push edx
        push eax
        ; Calculate which block the current offset is in
        mov esi, [fd_table + eax + 12]  ; current position
        mov ecx, esi
        shr ecx, 12             ; position / 4096 = block offset within file
        mov eax, [fd_table + eax + 8]   ; start block
        add eax, ecx            ; + block offset = actual block
        shl eax, 3              ; * 8 = sector
        add eax, HBFS_DATA_START
        mov ecx, HBFS_SECTORS_PER_BLK   ; read exactly 1 block (8 sectors)
        mov edi, hbfs_block_buf
        call ata_read_sectors
        pop eax
        pop edx
        pop ecx
        jc .frd_err
        ; Calculate offset within the block
        mov esi, [fd_table + eax + 12]
        and esi, (HBFS_BLOCK_SIZE - 1)  ; position mod 4096
        ; Clamp bytes to read to not exceed block boundary
        push edx
        mov edi, HBFS_BLOCK_SIZE
        sub edi, esi            ; bytes remaining in this block
        cmp edx, edi
        jle .frd_blk_ok
        mov edx, edi
.frd_blk_ok:
        pop edi                 ; restore original edx into edi temporarily
        ; edx = actual bytes to read (may be clamped)
        add esi, hbfs_block_buf ; offset into read buffer
        mov edi, ecx            ; dest buffer (original ECX)
        push eax
        mov ecx, edx
        rep movsb
        pop eax
        add [fd_table + eax + 12], edx
        mov [esp + 28], edx
        popad
        ret
.frd_eof:
        popad
        xor eax, eax
        ret
.frd_err:
        popad
        mov eax, -1
        ret

fd_write:
        pushad
        cmp ebx, FD_MAX
        jge .fwr_err
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE
        cmp dword [fd_table + eax], FD_FLAG_WRITE
        jne .fwr_err

        ; Save fd offset and parameters
        mov [.fw_fdoff], eax
        mov [.fw_src], ecx          ; source buffer
        mov [.fw_count], edx        ; byte count

        ; Check if position is within allocated blocks
        mov esi, [fd_table + eax + 12]  ; current position
        mov ecx, esi
        shr ecx, 12                      ; block index within file
        cmp ecx, [fd_table + eax + 16]  ; block count
        jge .fwr_full

        ; Read the block that contains the current position
        mov eax, [fd_table + eax + 8]   ; start block
        add eax, ecx                     ; + offset = actual block
        mov [.fw_blknum], eax
        shl eax, 3
        add eax, HBFS_DATA_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov edi, hbfs_block_buf
        call ata_read_sectors
        jc .fwr_ioerr

        ; Calculate offset within block and clamp write size
        mov eax, [.fw_fdoff]
        mov esi, [fd_table + eax + 12]  ; current position
        and esi, (HBFS_BLOCK_SIZE - 1)  ; offset within block
        mov ebx, HBFS_BLOCK_SIZE
        sub ebx, esi                     ; space remaining in block
        mov edx, [.fw_count]
        cmp edx, ebx
        jle .fw_cnt_ok
        mov edx, ebx                     ; clamp to block boundary
.fw_cnt_ok:
        mov [.fw_actual], edx

        ; Copy from user buffer to block buffer at offset
        mov ecx, edx
        mov edi, hbfs_block_buf
        add edi, esi                     ; offset within block
        mov esi, [.fw_src]
        rep movsb

        ; Write block back to disk
        mov eax, [.fw_blknum]
        shl eax, 3
        add eax, HBFS_DATA_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov esi, hbfs_block_buf
        call ata_write_sectors
        jc .fwr_ioerr

        ; Update file position and size
        mov eax, [.fw_fdoff]
        mov edx, [.fw_actual]
        add [fd_table + eax + 12], edx   ; advance position
        mov ecx, [fd_table + eax + 12]
        cmp ecx, [fd_table + eax + 4]
        jle .fw_no_grow
        mov [fd_table + eax + 4], ecx     ; extend file size
.fw_no_grow:
        mov [esp + 28], edx              ; return bytes written via EAX
        popad
        ret

.fwr_full:
.fwr_ioerr:
        popad
        xor eax, eax                     ; 0 = nothing written
        ret
.fwr_err:
        popad
        mov eax, -1
        ret

.fw_fdoff:  dd 0
.fw_src:    dd 0
.fw_count:  dd 0
.fw_blknum: dd 0
.fw_actual: dd 0

fd_close:
        cmp ebx, FD_MAX
        jge .fcl_bad
        pushad
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE

        ; If opened for writing, persist updated file size to directory
        cmp dword [fd_table + eax], FD_FLAG_WRITE
        jne .fcl_mark_closed

        ; Save start_block and file_size from FD entry
        mov ebp, [fd_table + eax + 8]    ; start_block
        mov edx, [fd_table + eax + 4]    ; file_size (possibly grown)

        ; Switch to the directory where this file lives (recorded at open time)
        ; Save current CWD first
        push dword [current_dir_lba]
        push dword [current_dir_sects]
        mov ecx, [fd_table + eax + 20]   ; directory LBA from fd entry
        mov [current_dir_lba], ecx
        mov ecx, [fd_table + eax + 24]   ; directory sector count from fd entry
        mov [current_dir_sects], ecx

        ; Find directory entry by matching start_block
        call hbfs_load_root_dir
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
.fcl_scan:
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .fcl_skip
        cmp [edi + DIRENT_START_BLOCK], ebp
        je .fcl_found
.fcl_skip:
        add edi, HBFS_DIR_ENTRY_SIZE
        loop .fcl_scan
        jmp .fcl_restore_cwd             ; entry not found, just close

.fcl_found:
        ; Update file size and modified timestamp in directory entry
        mov [edi + DIRENT_SIZE], edx
        mov eax, [tick_count]
        mov [edi + DIRENT_MODIFIED], eax
        call hbfs_save_root_dir

.fcl_restore_cwd:
        ; Restore original CWD
        pop dword [current_dir_sects]
        pop dword [current_dir_lba]

.fcl_mark_closed:
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE
        mov dword [fd_table + eax], FD_FLAG_CLOSED
        popad
.fcl_bad:
        ret

fd_seek:
        pushad
        cmp ebx, FD_MAX
        jge .fsk_err
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE
        cmp dword [fd_table + eax], FD_FLAG_CLOSED
        je .fsk_err
        cmp edx, 0
        je .fsk_set
        cmp edx, 1
        je .fsk_cur
        cmp edx, 2
        je .fsk_end
        jmp .fsk_err
.fsk_set:
        mov [fd_table + eax + 12], ecx
        jmp .fsk_done
.fsk_cur:
        add [fd_table + eax + 12], ecx
        jmp .fsk_done
.fsk_end:
        mov edx, [fd_table + eax + 4]
        add edx, ecx
        mov [fd_table + eax + 12], edx
.fsk_done:
        mov edx, [fd_table + eax + 12]
        mov [esp + 28], edx
        popad
        ret
.fsk_err:
        popad
        mov eax, -1
        ret

;=======================================================================
; ENVIRONMENT VARIABLES
;=======================================================================

env_get:
        pushad
        mov edi, env_table
        mov ecx, ENV_MAX
.eg_search:
        cmp byte [edi], 0
        je .eg_skip
        push esi
        push edi
.eg_cmp:
        mov al, [esi]
        cmp al, 0
        jne .eg_cont
        cmp byte [edi], '='
        je .eg_found
        jmp .eg_no
.eg_cont:
        cmp al, [edi]
        jne .eg_no
        inc esi
        inc edi
        jmp .eg_cmp
.eg_found:
        inc edi
        pop eax
        pop eax
        mov [esp + 28], edi
        popad
        ret
.eg_no:
        pop edi
        pop esi
.eg_skip:
        add edi, ENV_ENTRY_SIZE
        loop .eg_search
        popad
        xor eax, eax
        ret

;---------------------------------------
; env_get_var - Get environment variable value
; ESI = variable name (null-terminated)
; EDI = destination buffer for value
; Returns: CF=0 if found, CF=1 if not found
;---------------------------------------
env_get_var:
        push eax
        push esi
        push edi
        push ecx
        mov ecx, edi            ; save dest buffer
        call env_get
        ; env_get: on success EAX = value ptr, on failure EAX = 0
        test eax, eax
        jz .egv_not_found
        ; Copy value from EAX to our dest buffer
        mov esi, eax
        mov edi, ecx
.egv_copy:
        lodsb
        stosb
        cmp al, 0
        jne .egv_copy
        pop ecx
        pop edi
        pop esi
        pop eax
        clc
        ret
.egv_not_found:
        pop ecx
        pop edi
        pop esi
        pop eax
        stc
        ret

env_set_str:
        pushad
        mov edi, env_table
        mov ecx, ENV_MAX
        xor ebx, ebx
.es_scan:
        cmp byte [edi], 0
        jne .es_check
        cmp ebx, 0
        jne .es_next
        mov ebx, edi
        jmp .es_next
.es_check:
        push esi
        push edi
.es_cmp:
        mov al, [esi]
        cmp al, '='
        je .es_eq
        cmp al, 0
        je .es_eq
        cmp al, [edi]
        jne .es_nomatch
        inc esi
        inc edi
        jmp .es_cmp
.es_eq:
        cmp byte [edi], '='
        je .es_overwrite
.es_nomatch:
        pop edi
        pop esi
.es_next:
        add edi, ENV_ENTRY_SIZE
        loop .es_scan
        cmp ebx, 0
        je .es_full
        mov edi, ebx
        jmp .es_write
.es_overwrite:
        pop edi
        pop esi
.es_write:
        mov ecx, ENV_ENTRY_SIZE - 1
.es_copy:
        lodsb
        stosb
        cmp al, 0
        je .es_pad
        loop .es_copy
        mov byte [edi], 0
        jmp .es_done
.es_pad:
        xor al, al
        rep stosb
.es_done:
        popad
        ret
.es_full:
        popad
        ret

env_unset:
        pushad
        mov edi, env_table
        mov ecx, ENV_MAX
.eu_scan:
        cmp byte [edi], 0
        je .eu_skip
        push esi
        push edi
.eu_cmp:
        mov al, [esi]
        cmp al, 0
        jne .eu_cont
        cmp byte [edi], '='
        je .eu_found
        jmp .eu_no
.eu_cont:
        cmp al, [edi]
        jne .eu_no
        inc esi
        inc edi
        jmp .eu_cmp
.eu_found:
        pop edi
        pop esi
        push ecx
        mov ecx, ENV_ENTRY_SIZE / 4
        xor eax, eax
        rep stosd
        pop ecx
        popad
        ret
.eu_no:
        pop edi
        pop esi
.eu_skip:
        add edi, ENV_ENTRY_SIZE
        loop .eu_scan
        popad
        ret

env_expand:
        pushad
.ee_loop:
        lodsb
        cmp al, 0
        je .ee_done
        cmp al, '$'
        je .ee_var
        stosb
        jmp .ee_loop
.ee_var:
        push edi
        mov edi, env_name_buf
.ee_rname:
        mov al, [esi]
        cmp al, 'A'
        jl .ee_name_end
        cmp al, 'z'
        jg .ee_name_end
        cmp al, '['
        jge .ee_chklow
        jmp .ee_namechar
.ee_chklow:
        cmp al, 'a'
        jl .ee_name_end
.ee_namechar:
        stosb
        inc esi
        jmp .ee_rname
.ee_name_end:
        mov byte [edi], 0
        pop edi
        push esi
        mov esi, env_name_buf
        call env_get
        pop esi
        cmp eax, 0
        je .ee_loop
        push esi
        mov esi, eax
.ee_cpval:
        lodsb
        cmp al, 0
        je .ee_cpvdone
        stosb
        jmp .ee_cpval
.ee_cpvdone:
        pop esi
        jmp .ee_loop
.ee_done:
        mov byte [edi], 0
        popad
        ret

;=======================================================================
; SUBDIRECTORY SUPPORT
;=======================================================================

; cd_internal: Change directory
; ESI = path string
; Returns: [esp+28] = 0 on success, -1 on failure
; Supports: "..", "/", single name, multi-component "a/b/c"
cmd_cd_internal:
        pushad

        ; Check for empty argument
        cmp byte [esi], 0
        je .cd_fail

        ; Check for "cd /" (absolute root)
        cmp byte [esi], '/'
        jne .cd_check_dotdot
        cmp byte [esi + 1], 0
        je .cd_goto_root
        ; Absolute path like /a/b/c: go to root first, then resolve rest
        inc esi
        call .cd_reset_to_root
        jmp .cd_resolve_path

.cd_check_dotdot:
        ; Check for ".."
        cmp byte [esi], '.'
        jne .cd_resolve_path
        cmp byte [esi + 1], '.'
        jne .cd_resolve_path
        ; Could be ".." or "../something"
        cmp byte [esi + 2], 0
        je .cd_parent
        cmp byte [esi + 2], '/'
        je .cd_parent_continue
        jmp .cd_resolve_path    ; Not "..", just a name starting with ".."

.cd_parent:
        ; cd .. - go up one level
        call .cd_pop_stack
        mov dword [esp + 28], 0
        popad
        ret

.cd_parent_continue:
        ; "../something" - go up, then resolve rest
        call .cd_pop_stack
        add esi, 3              ; skip "../"
        jmp .cd_resolve_path

.cd_goto_root:
        call .cd_reset_to_root
        mov dword [esp + 28], 0
        popad
        ret

; Resolve a (possibly multi-component) relative path
; ESI = remaining path components like "a/b/c"
.cd_resolve_path:
        ; Extract next path component (up to '/' or null)
        mov edi, filename_buf
        xor ecx, ecx
.cd_copy_component:
        lodsb
        cmp al, '/'
        je .cd_component_end
        cmp al, 0
        je .cd_component_last
        stosb
        inc ecx
        cmp ecx, HBFS_MAX_FILENAME
        jl .cd_copy_component
.cd_component_end:
        mov byte [edi], 0
        cmp ecx, 0
        je .cd_resolve_path     ; empty component (double slash), skip it
        ; Check for ".." as intermediate component
        cmp byte [filename_buf], '.'
        jne .cd_comp_enter
        cmp byte [filename_buf + 1], 0
        je .cd_resolve_path     ; "." = current dir, skip it
        cmp byte [filename_buf + 1], '.'
        jne .cd_comp_enter
        cmp byte [filename_buf + 2], 0
        jne .cd_comp_enter
        ; ".." component - go up one level
        call .cd_pop_stack
        jmp .cd_resolve_path
.cd_comp_enter:
        ; Named directory component - enter it
        push esi                ; save remaining path
        call .cd_enter_subdir
        pop esi
        jc .cd_fail             ; subdirectory not found
        jmp .cd_resolve_path    ; process next component

.cd_component_last:
        mov byte [edi], 0
        cmp ecx, 0
        je .cd_success          ; trailing slash, already done
        ; Check for "." or ".." as final component
        cmp byte [filename_buf], '.'
        jne .cd_enter_last
        cmp byte [filename_buf + 1], 0
        je .cd_success          ; "." = current dir, done
        cmp byte [filename_buf + 1], '.'
        jne .cd_enter_last
        cmp byte [filename_buf + 2], 0
        jne .cd_enter_last
        call .cd_pop_stack
        jmp .cd_success

.cd_enter_last:
        call .cd_enter_subdir
        jc .cd_fail

.cd_success:
        mov dword [esp + 28], 0
        popad
        ret

.cd_fail:
        mov dword [esp + 28], -1
        popad
        ret

; Enter a subdirectory by name (filename_buf)
; Pushes current dir onto stack, switches to new dir
; Returns CF set on failure
.cd_enter_subdir:
        ; Load current directory and find the named entry
        call hbfs_load_root_dir
        push esi
        mov esi, filename_buf
        call hbfs_find_file
        pop esi
        jc .cd_sub_fail
        cmp byte [edi + DIRENT_TYPE], FTYPE_DIR
        jne .cd_sub_fail

        ; Push current directory onto stack
        mov eax, [dir_depth]
        cmp eax, DIR_STACK_MAX
        jge .cd_sub_fail        ; stack full

        ; Calculate stack entry address: dir_stack + depth * DIR_STACK_ENTRY_SIZE
        push edi                ; save dir entry pointer
        imul eax, DIR_STACK_ENTRY_SIZE
        lea ebx, [dir_stack + eax]
        ; Save current state (interrupts off to keep dir globals consistent)
        cli
        mov eax, [current_dir_lba]
        mov [ebx], eax
        mov eax, [current_dir_sects]
        mov [ebx + 4], eax
        ; Copy current_dir_name
        push esi
        lea edi, [ebx + 8]
        mov esi, current_dir_name
.cd_push_name:
        lodsb
        stosb
        cmp al, 0
        jne .cd_push_name
        pop esi

        inc dword [dir_depth]
        pop edi                 ; restore dir entry pointer

        ; Switch to new directory
        mov eax, [edi + DIRENT_START_BLOCK]
        shl eax, 3              ; blocks to sectors
        add eax, HBFS_DATA_START
        mov [current_dir_lba], eax
        mov eax, [edi + DIRENT_BLOCK_COUNT]
        shl eax, 3
        mov [current_dir_sects], eax

        ; Copy directory name
        push esi
        mov esi, edi            ; entry name
        mov edi, current_dir_name
.cd_ent_name:
        lodsb
        stosb
        cmp al, 0
        jne .cd_ent_name
        pop esi
        sti                     ; state consistent, re-enable interrupts

        ; Load the new directory contents
        mov eax, [current_dir_lba]
        mov ecx, [current_dir_sects]
        mov edi, hbfs_dir_buf
        call ata_read_sectors

        clc
        ret

.cd_sub_fail:
        stc
        ret

; Pop one level from the directory stack (cd ..)
; If already at root, do nothing
.cd_pop_stack:
        mov eax, [dir_depth]
        cmp eax, 0
        je .cd_pop_done         ; already at root

        cli                     ; protect dir state from Ctrl+C
        dec eax
        mov [dir_depth], eax

        ; Calculate stack entry address
        imul eax, DIR_STACK_ENTRY_SIZE
        lea ebx, [dir_stack + eax]

        ; Restore parent directory state
        mov eax, [ebx]
        mov [current_dir_lba], eax
        mov eax, [ebx + 4]
        mov [current_dir_sects], eax

        ; Copy parent name back
        push esi
        lea esi, [ebx + 8]
        mov edi, current_dir_name
.cd_pop_name:
        lodsb
        stosb
        cmp al, 0
        jne .cd_pop_name
        pop esi
        sti                     ; dir state consistent

.cd_pop_done:
        ret

; Reset to root directory and clear stack
.cd_reset_to_root:
        mov dword [current_dir_lba], HBFS_ROOT_DIR_START
        mov dword [current_dir_sects], HBFS_ROOT_DIR_SECTS
        mov byte [current_dir_name], '/'
        mov byte [current_dir_name + 1], 0
        mov dword [dir_depth], 0
        ret

;=======================================================================
; NEW SYSCALL HANDLERS
;=======================================================================

sys_beep:
        cmp ebx, 0
        je .beep_off
        call speaker_beep
        xor eax, eax
        iretd
.beep_off:
        call speaker_off
        xor eax, eax
        iretd

sys_date:
        push esi
        call rtc_read_time
        mov al, [rtc_sec]
        mov [ebx], al
        mov al, [rtc_min]
        mov [ebx + 1], al
        mov al, [rtc_hour]
        mov [ebx + 2], al
        mov al, [rtc_day]
        mov [ebx + 3], al
        mov al, [rtc_month]
        mov [ebx + 4], al
        mov al, [rtc_year]
        mov [ebx + 5], al
        movzx eax, byte [rtc_century]
        imul eax, 100
        movzx ecx, byte [rtc_year]
        add eax, ecx
        pop esi
        iretd

sys_chdir:
        push esi
        push edi
        mov esi, ebx
        call cmd_cd_internal
        pop edi
        pop esi
        iretd

sys_getcwd:
        push edi
        mov edi, ebx            ; destination buffer
        call build_cwd_path
        pop edi
        xor eax, eax
        iretd

sys_serial:
        push esi
        mov esi, ebx
        call serial_print
        pop esi
        xor eax, eax
        iretd

sys_serial_in:
        call serial_getchar
        movzx eax, al           ; 0x00-0xFE = valid char, 0xFF = no data
        cmp al, 0xFF
        jne .si_ok
        mov eax, -1             ; Return -1 (0xFFFFFFFF) when no data
.si_ok:
        iretd

sys_getenv:
        push esi
        push edi
        mov esi, ebx
        call env_get
        pop edi
        pop esi
        iretd

;---------------------------------------
; SYS_GETARGS (32): Get command-line arguments
; EBX = pointer to destination buffer (max 512 bytes)
; Returns: EAX = length of argument string (0 if none)
;---------------------------------------
sys_getargs:
        push esi
        push edi
        push ecx
        mov esi, program_args_buf
        mov edi, ebx
        xor ecx, ecx
.ga_copy:
        lodsb
        stosb
        inc ecx
        test al, al
        jnz .ga_copy
        dec ecx                 ; Don't count null terminator
        mov eax, ecx
        pop ecx
        pop edi
        pop esi
        iretd

;---------------------------------------
; SYS_STDIN_READ (34): Copy piped/redirected stdin into user buffer
; EBX = destination buffer pointer
; Returns: EAX = bytes copied (>= 0), or -1 if no stdin available
;---------------------------------------
sys_stdin_read:
        cmp byte [stdin_redir_active], 1
        jne .ssr_no_stdin
        push esi
        push edi
        push ecx
        mov ecx, [stdin_redir_len]
        cmp ecx, 0
        je .ssr_empty
        cmp ecx, BATCH_BUFFER_SIZE - 1
        jbe .ssr_size_ok
        mov ecx, BATCH_BUFFER_SIZE - 1
.ssr_size_ok:
        mov esi, redir_in_buf
        mov edi, ebx
        mov eax, ecx            ; save count before rep movsb clobbers ECX
        rep movsb
        mov byte [edi], 0       ; null-terminate
        mov byte [stdin_redir_active], 0
        pop ecx
        pop edi
        pop esi
        iretd
.ssr_empty:
        pop ecx
        pop edi
        pop esi
        xor eax, eax
        iretd
.ssr_no_stdin:
        mov eax, -1
        iretd

;--- Read entire file into buffer ---
; EBX = filename, ECX = buffer pointer
; Returns EAX = bytes read (0 if not found)
sys_fread:
        pushad
        mov esi, ebx            ; filename
        mov edi, [esp + 24]     ; recover original ECX (dest buffer) from pushad frame
        call hbfs_read_file     ; ESI=filename, EDI=dest buffer; returns ECX=size, CF on error
        jc .fread_fail
        ; ECX = file size returned by hbfs_read_file
        mov [esp + 28], ecx     ; set EAX in pushad frame to file size
        popad
        iretd
.fread_fail:
        popad
        xor eax, eax
        iretd

;--- Write entire file (create/overwrite) ---
; EBX = filename, ECX = buffer pointer, EDX = size
; Returns EAX = 0 on success
sys_fwrite:
        pushad
        ; Save parameters before hbfs_find_file clobbers them
        mov [.fw_name], ebx
        mov [.fw_buf], ecx
        mov [.fw_size], edx
        ; ESI = file type (0 = default to FTYPE_TEXT)
        cmp esi, FTYPE_TEXT
        jb .fw_default_type
        cmp esi, FTYPE_BATCH
        ja .fw_default_type
        mov [.fw_type], esi
        jmp .fw_type_set
.fw_default_type:
        mov dword [.fw_type], FTYPE_TEXT
.fw_type_set:
        ; Delete old file if it exists
        mov esi, ebx
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .fwrite_new
        ; EDI = dir entry pointer from hbfs_find_file
        call hbfs_delete_file_entry
.fwrite_new:
        ; Create new file with specified type
        mov esi, [.fw_name]     ; filename
        mov edi, [.fw_buf]      ; data buffer
        mov ecx, [.fw_size]     ; size
        mov edx, [.fw_type]
        call hbfs_create_file
        jc .fwrite_err
        mov dword [esp + 28], 0 ; set EAX = 0 in pushad frame
        popad
        iretd
.fwrite_err:
        popad
        mov eax, -1
        iretd
.fw_name: dd 0
.fw_buf:  dd 0
.fw_size: dd 0
.fw_type: dd 0

sys_open_fd:
        push esi
        push edi
        mov esi, ebx
        call fd_open
        pop edi
        pop esi
        iretd

sys_read_fd:
        push esi
        push edi
        call fd_read
        pop edi
        pop esi
        iretd

sys_write_fd:
        push esi
        push edi
        call fd_write
        pop edi
        pop esi
        iretd

sys_close_fd:
        push ebx
        call fd_close
        pop ebx
        xor eax, eax
        iretd

sys_seek_fd:
        push esi
        call fd_seek
        pop esi
        iretd

;---------------------------------------
; SYS_MKDIR (12): Create a subdirectory
; EBX = pointer to directory name
; Returns: EAX = 0 on success, -1 on error
;---------------------------------------
sys_mkdir:
        push esi
        mov esi, ebx
        call hbfs_mkdir
        pop esi
        jc .smk_err
        xor eax, eax
        iretd
.smk_err:
        mov eax, -1
        iretd

;---------------------------------------
; SYS_READDIR (13): Read directory entry
; EBX = buffer for filename, ECX = entry index (0-based)
; Returns: EAX = file type (0=free, -1=end), ECX = file size
;---------------------------------------
sys_readdir:
        push esi
        push edi
        push edx
        push ebp

        call hbfs_load_root_dir
        call hbfs_get_max_entries
        cmp ecx, eax
        jge .srd_end             ; index >= max entries

        ; Point to entry at index ECX
        mov edi, hbfs_dir_buf
        imul edx, ecx, HBFS_DIR_ENTRY_SIZE
        add edi, edx
        mov ebp, edi             ; save entry pointer

        cmp byte [ebp + DIRENT_TYPE], FTYPE_FREE
        je .srd_free

        ; Copy filename to user buffer (EBX), capped at HBFS_MAX_FILENAME+1
        mov esi, ebp
        mov edi, ebx
        mov edx, HBFS_MAX_FILENAME
.srd_cpn:
        lodsb
        stosb
        test al, al
        jz .srd_cpn_done
        dec edx
        jnz .srd_cpn
        mov byte [edi], 0       ; Force null-terminate if at limit
.srd_cpn_done:

        ; Return type in EAX and size in ECX
        movzx eax, byte [ebp + DIRENT_TYPE]
        mov ecx, [ebp + DIRENT_SIZE]

        pop ebp
        pop edx
        pop edi
        pop esi
        iretd

.srd_free:
        ; Empty slot - return type 0
        mov byte [ebx], 0
        xor eax, eax
        xor ecx, ecx
        pop ebp
        pop edx
        pop edi
        pop esi
        iretd

.srd_end:
        ; Past end of directory
        mov byte [ebx], 0
        mov eax, -1
        xor ecx, ecx
        pop ebp
        pop edx
        pop edi
        pop esi
        iretd

;=======================================================================
; NEW SHELL COMMANDS
;=======================================================================

;---------------------------------------
; Command: echo - Print text with $VAR expansion
; Flags: -n  no trailing newline
;---------------------------------------
cmd_echo:
        pushad
        mov edi, expand_buf
        call env_expand
        mov esi, expand_buf
        call vga_print
        ; Check for -n flag (suppress newline)
        mov eax, 13             ; 'n' - 'a'
        call test_flag
        jc .echo_done
        mov al, 0x0A
        call vga_putchar
.echo_done:
        popad
        ret

;---------------------------------------
; Command: wc - Count lines, words, bytes
; Flags: -l  show only line count
;        -w  show only word count
;        -c  show only byte count
;        (no flags = show all three)
;---------------------------------------
cmd_wc_file:
        pushad
        cmp byte [esi], 0
        jne .wc_from_file
        call shell_use_stdin_as_input
        jc .wc_usage
        jmp .wc_loaded
.wc_from_file:
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .wc_nf
.wc_loaded:
        mov [.wc_bytes], ecx
        mov esi, PROGRAM_BASE
        xor ebx, ebx
        xor edx, edx
        xor ebp, ebp
.wc_loop:
        cmp ecx, 0
        je .wc_print
        dec ecx
        lodsb
        cmp al, 0x0A
        jne .wc_nnl
        inc ebx
.wc_nnl:
        cmp al, ' '
        je .wc_sp
        cmp al, 0x09
        je .wc_sp
        cmp al, 0x0A
        je .wc_sp
        cmp al, 0x0D
        je .wc_sp
        cmp ebp, 0
        jne .wc_loop
        mov ebp, 1
        inc edx
        jmp .wc_loop
.wc_sp:
        xor ebp, ebp
        jmp .wc_loop
.wc_print:
        ; Check if any specific flag is set
        mov eax, 11             ; 'l' - 'a'
        call test_flag
        jc .wc_only_lines
        mov eax, 22             ; 'w' - 'a'
        call test_flag
        jc .wc_only_words
        mov eax, 2              ; 'c' - 'a'
        call test_flag
        jc .wc_only_bytes
        ; No flags - print all
        push edx
        mov eax, ebx
        call vga_print_dec
        mov al, ' '
        call vga_putchar
        pop eax
        call vga_print_dec
        mov al, ' '
        call vga_putchar
        mov eax, [.wc_bytes]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar
        popad
        ret
.wc_only_lines:
        mov eax, ebx
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar
        popad
        ret
.wc_only_words:
        mov eax, edx
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar
        popad
        ret
.wc_only_bytes:
        mov eax, [.wc_bytes]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar
        popad
        ret
.wc_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.wc_nf:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.wc_bytes: dd 0

;---------------------------------------
; Command: find - Search for pattern in file
; Flags: -i  case-insensitive matching
;        -c  count matches only (don't print lines)
;        -n  print line numbers (default: on, this is for compat)
;---------------------------------------
cmd_find_file:
        pushad
        mov edi, filename_buf
        mov ecx, 255
        call copy_word_n
        call skip_spaces
        push esi
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        pop esi
        jc .find_nf
        mov byte [PROGRAM_BASE + ecx], 0
        mov esi, PROGRAM_BASE
        xor ebx, ebx           ; line counter
        xor ebp, ebp           ; match counter
.find_line:
        inc ebx
        mov edi, esi
.find_eol:
        cmp byte [esi], 0
        je .find_end
        cmp byte [esi], 0x0A
        je .found_eol
        inc esi
        jmp .find_eol
.found_eol:
        mov byte [esi], 0
        push esi
        mov esi, edi
        ; Check for -i flag (case-insensitive)
        mov eax, 8              ; 'i' - 'a'
        call test_flag
        jc .find_ci
        call str_find_substr
        jmp .find_checked
.find_ci:
        call str_find_substr_ci
.find_checked:
        pop esi
        jc .find_nxt
        inc ebp
        ; Check for -c flag (count only)
        mov eax, 2              ; 'c' - 'a'
        call test_flag
        jc .find_nxt
        push esi
        push edi
        mov byte [vga_color], COLOR_INFO
        mov eax, ebx
        call vga_print_dec
        mov al, ':'
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
        mov esi, edi
        call vga_print
        mov al, 0x0A
        call vga_putchar
        pop edi
        pop esi
.find_nxt:
        mov byte [esi], 0x0A
        inc esi
        jmp .find_line
.find_end:
        cmp edi, esi
        je .find_done2
        push esi
        mov esi, edi
        ; Check for -i flag
        mov eax, 8
        call test_flag
        jc .find_end_ci
        call str_find_substr
        jmp .find_end_checked
.find_end_ci:
        call str_find_substr_ci
.find_end_checked:
        pop esi
        jc .find_done2
        inc ebp
        mov eax, 2              ; -c
        call test_flag
        jc .find_done2
        mov byte [vga_color], COLOR_INFO
        mov eax, ebx
        call vga_print_dec
        mov al, ':'
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
        mov esi, edi
        call vga_print
        mov al, 0x0A
        call vga_putchar
.find_done2:
        ; If -c flag, print match count
        mov eax, 2
        call test_flag
        jnc .find_exit
        mov eax, ebp
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar
.find_exit:
        popad
        ret
.find_nf:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

; Substring search: find filename_buf in string at ESI
; Returns: CF clear=found, CF set=not found
str_find_substr:
        push esi
        push edi
.sfs_loop:
        cmp byte [esi], 0
        je .sfs_no
        push esi
        mov edi, filename_buf
.sfs_cmp:
        cmp byte [edi], 0
        je .sfs_yes
        mov al, [esi]
        cmp al, 0
        je .sfs_cmpfail
        cmp al, [edi]
        jne .sfs_cmpfail
        inc esi
        inc edi
        jmp .sfs_cmp
.sfs_yes:
        pop esi
        pop edi
        pop esi
        clc
        ret
.sfs_cmpfail:
        pop esi
        inc esi
        jmp .sfs_loop
.sfs_no:
        pop edi
        pop esi
        stc
        ret

; Case-insensitive substring search: find filename_buf in string at ESI
; Returns: CF clear=found, CF set=not found
str_find_substr_ci:
        push esi
        push edi
.sfci_loop:
        cmp byte [esi], 0
        je .sfci_no
        push esi
        mov edi, filename_buf
.sfci_cmp:
        cmp byte [edi], 0
        je .sfci_yes
        mov al, [esi]
        cmp al, 0
        je .sfci_fail
        mov ah, [edi]
        ; Lowercase both
        cmp al, 'A'
        jl .sfci_a_ok
        cmp al, 'Z'
        jg .sfci_a_ok
        or al, 0x20
.sfci_a_ok:
        cmp ah, 'A'
        jl .sfci_b_ok
        cmp ah, 'Z'
        jg .sfci_b_ok
        or ah, 0x20
.sfci_b_ok:
        cmp al, ah
        jne .sfci_fail
        inc esi
        inc edi
        jmp .sfci_cmp
.sfci_yes:
        pop esi
        pop edi
        pop esi
        clc
        ret
.sfci_fail:
        pop esi
        inc esi
        jmp .sfci_loop
.sfci_no:
        pop edi
        pop esi
        stc
        ret

cmd_append_file:
        pushad
        mov edi, filename_buf
        mov ecx, 255
        call copy_word_n
        push esi
        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .app_new
        mov [.app_sz], ecx
        jmp .app_prompt
.app_new:
        mov dword [.app_sz], 0
        mov byte [last_file_type], FTYPE_FREE
.app_prompt:
        pop esi
        mov esi, msg_write_prompt
        call vga_print
.app_rdln:
        mov edi, temp_line_buf
        xor ecx, ecx
.app_ch:
        call kb_getchar
        cmp al, 0x0D
        je .app_eol
        cmp al, 0x0A
        je .app_eol
        cmp al, 0x08
        je .app_bs
        mov [edi], al
        inc edi
        inc ecx
        call vga_putchar
        jmp .app_ch
.app_bs:
        or ecx, ecx
        jz .app_ch
        dec edi
        dec ecx
        mov al, 0x08
        call vga_putchar
        jmp .app_ch
.app_eol:
        mov byte [edi], 0
        mov al, 0x0A
        call vga_putchar
        cmp ecx, 0
        je .app_save
        mov esi, temp_line_buf
        mov edi, PROGRAM_BASE
        add edi, [.app_sz]
.app_cp:
        lodsb
        stosb
        cmp al, 0
        jne .app_cp
        dec edi
        mov byte [edi], 0x0A
        inc edi
        sub edi, PROGRAM_BASE
        mov [.app_sz], edi
        jmp .app_rdln
.app_save:
        mov esi, filename_buf
        mov ecx, [.app_sz]
        mov edi, PROGRAM_BASE
        ; Preserve file type if existing, else default to text
        movzx edx, byte [last_file_type]
        cmp edx, FTYPE_FREE
        jne .app_type_ok
        mov edx, FTYPE_TEXT
.app_type_ok:
        call hbfs_create_file
        jc .app_err
        mov esi, msg_saved
        call vga_print
        popad
        ret
.app_err:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_write_err
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.app_sz: dd 0

cmd_date_show:
        pushad
        call rtc_read_time
        movzx eax, byte [rtc_century]
        imul eax, 100
        movzx ecx, byte [rtc_year]
        add eax, ecx
        call vga_print_dec
        mov al, '-'
        call vga_putchar
        movzx eax, byte [rtc_month]
        call .p2d
        mov al, '-'
        call vga_putchar
        movzx eax, byte [rtc_day]
        call .p2d
        mov al, ' '
        call vga_putchar
        movzx eax, byte [rtc_hour]
        call .p2d
        mov al, ':'
        call vga_putchar
        movzx eax, byte [rtc_min]
        call .p2d
        mov al, ':'
        call vga_putchar
        movzx eax, byte [rtc_sec]
        call .p2d
        mov al, 0x0A
        call vga_putchar
        popad
        ret
.p2d:
        cmp eax, 10
        jge .p2d_ok
        push eax
        mov al, '0'
        call vga_putchar
        pop eax
.p2d_ok:
        call vga_print_dec
        ret

cmd_beep:
        pushad
        mov ebx, 1000
        mov ecx, 20
        call speaker_beep
        popad
        ret

cmd_exec_batch:
        pushad

        ; Guard against nested batch execution (shared buffers would corrupt)
        cmp byte [batch_running], 1
        jne .bat_not_nested
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_batch_nested
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.bat_not_nested:
        mov byte [batch_running], 1

        mov edi, batch_script_buf
        call hbfs_read_file
        jc .bat_nf
        ; Clamp script to buffer size
        cmp ecx, BATCH_BUFFER_SIZE - 1
        jle .bat_size_ok
        mov ecx, BATCH_BUFFER_SIZE - 1
.bat_size_ok:
        mov byte [batch_script_buf + ecx], 0
        mov esi, batch_script_buf
        call batch_run_loop
        popad
        ret
.bat_nf:
        mov byte [batch_running], 0
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; batch_run_loop - Process batch script lines starting at ESI
; Called from cmd_exec_batch and shell_main (resume after external program)
; Input: ESI = current position in batch_script_buf
; On completion, clears batch_running flag
;---------------------------------------
batch_run_loop:
.brl_line:
        cmp byte [esi], 0
        je .brl_done
        mov edi, batch_line_buf
        xor ecx, ecx
.brl_cpln:
        lodsb
        cmp al, 0x0A
        je .brl_exec
        cmp al, 0x0D
        je .brl_cpln
        cmp al, 0
        je .brl_exec_last
        stosb
        inc ecx
        jmp .brl_cpln
.brl_exec_last:
        dec esi
.brl_exec:
        mov byte [edi], 0
        cmp ecx, 0
        je .brl_line
        ; --- Batch built-in directives ---
        ; 1. Skip label definitions (:LABEL) silently
        cmp byte [batch_line_buf], ':'
        je .brl_line

        ; 2. Skip REM comments (case-insensitive: rem<space> or rem<end>)
        mov al, [batch_line_buf]
        or al, 0x20
        cmp al, 'r'
        jne .brl_not_rem
        mov al, [batch_line_buf+1]
        or al, 0x20
        cmp al, 'e'
        jne .brl_not_rem
        mov al, [batch_line_buf+2]
        or al, 0x20
        cmp al, 'm'
        jne .brl_not_rem
        mov al, [batch_line_buf+3]
        cmp al, ' '
        je .brl_line
        cmp al, 0
        je .brl_line
.brl_not_rem:
        ; 3. goto directive
        call batch_check_goto
        jnc .brl_line           ; CF=0 = was a goto, ESI updated

        ; 4. if directive
        call batch_check_if
        jnc .brl_line           ; CF=0 = was an if, processed

        ; 5. Check for @ prefix (silent execution)
        xor ebx, ebx            ; EBX = 0: echo
        cmp byte [batch_line_buf], '@'
        jne .brl_show_echo
        mov ebx, 1              ; EBX = 1: suppress echo
.brl_show_echo:
        ; Save resume position before executing (in case of external program)
        mov eax, esi
        sub eax, batch_script_buf
        mov [batch_resume_pos], eax
        push esi
        test ebx, ebx
        jnz .brl_skip_echo
        mov byte [vga_color], COLOR_INFO
        mov esi, msg_batch_prefix
        call vga_print
        mov esi, batch_line_buf
        call vga_print
        mov al, 0x0A
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
.brl_skip_echo:
        ; Copy batch_line_buf into line_buffer (skip @ prefix if present)
        mov esi, batch_line_buf
        cmp byte [esi], '@'
        jne .brl_do_copy
        inc esi
.brl_do_copy:
        mov edi, line_buffer
.brl_copy_loop:
        lodsb
        stosb
        test al, al
        jnz .brl_copy_loop
        call shell_parse_cmd
        pop esi
        jmp .brl_line
.brl_done:
        mov byte [batch_running], 0
        ret

;---------------------------------------
; batch_check_goto - Handle 'goto LABEL' batch directive
; Input: batch_line_buf = current line, ESI = next-line ptr (batch loop pos)
; Output: CF=0: was a goto, ESI updated; CF=1: not a goto
; Clobbers: EAX, EBX, ECX, EDX, EDI
;---------------------------------------
batch_check_goto:
        mov al, [batch_line_buf]
        or al, 0x20
        cmp al, 'g'
        jne .bcg_not
        mov al, [batch_line_buf+1]
        or al, 0x20
        cmp al, 'o'
        jne .bcg_not
        mov al, [batch_line_buf+2]
        or al, 0x20
        cmp al, 't'
        jne .bcg_not
        mov al, [batch_line_buf+3]
        or al, 0x20
        cmp al, 'o'
        jne .bcg_not
        mov al, [batch_line_buf+4]
        cmp al, ' '
        je .bcg_have_label
        cmp al, 0
        jne .bcg_not            ; 'goto' prefix of longer word
        ; Bare 'goto' with no label: advance ESI to end of script
        mov esi, batch_script_buf
.bcg_eof_scan:
        cmp byte [esi], 0
        je .bcg_done_clc
        inc esi
        jmp .bcg_eof_scan
.bcg_have_label:
        mov ebx, batch_line_buf + 5     ; EBX = label name pointer
.bcg_sp:
        cmp byte [ebx], ' '
        jne .bcg_got_label
        inc ebx
        jmp .bcg_sp
.bcg_got_label:
        push esi                        ; save batch loop position
        mov edi, ebx                    ; EDI = label name for batch_find_label
        call batch_find_label           ; modifies ESI if found (CF=0)
        jnc .bcg_found
        ; Label not found
        pop esi                         ; restore batch loop position
        mov byte [vga_color], COLOR_ERROR
        push esi
        mov esi, msg_batch_label_nf
        call vga_print
        mov esi, ebx                    ; label name
        call vga_print
        mov al, 0x0A
        call vga_putchar
        pop esi
        mov byte [vga_color], COLOR_DEFAULT
        ; Advance ESI to end of script to abort
        mov esi, batch_script_buf
.bcg_abort_scan:
        cmp byte [esi], 0
        je .bcg_done_clc
        inc esi
        jmp .bcg_abort_scan
.bcg_found:
        mov eax, esi            ; EAX = new position from batch_find_label
        pop esi                 ; discard saved old position
        mov esi, eax            ; set new position
.bcg_done_clc:
        clc
        ret
.bcg_not:
        stc
        ret

;---------------------------------------
; batch_find_label - Find ':LABEL' in batch_script_buf
; Input: EDI = null-terminated label name (without ':')
; Output: CF=0: ESI = ptr to first char of line after ':LABEL' line
;         CF=1: not found, EDI restored, ESI trashed
; Clobbers: EAX, EBX (preserves ECX, EDX, EDI)
;---------------------------------------
batch_find_label:
        push edx
        mov edx, edi            ; EDX = label name (preserve EDI via EDX)
        mov esi, batch_script_buf
.bfl_outer:
        cmp byte [esi], 0
        je .bfl_nf
        cmp byte [esi], ':'
        je .bfl_try_match
        ; Not ':', skip to end of line
.bfl_skip_line:
        lodsb
        cmp al, 0
        je .bfl_at_end
        cmp al, 0x0A
        jne .bfl_skip_line
        jmp .bfl_outer
.bfl_at_end:
        dec esi
        jmp .bfl_nf
.bfl_try_match:
        inc esi                 ; skip ':'
        mov edi, edx            ; EDI = label name
.bfl_cmp:
        mov al, [esi]
        mov bl, [edi]
        or al, 0x20
        or bl, 0x20
        cmp bl, 0
        je .bfl_end_check       ; label name exhausted
        cmp al, bl
        jne .bfl_no_match
        inc esi
        inc edi
        jmp .bfl_cmp
.bfl_end_check:
        ; Label name matched - check ESI is at word boundary
        mov al, [esi]
        cmp al, 0
        je .bfl_matched
        cmp al, 0x0A
        je .bfl_matched
        cmp al, 0x0D
        je .bfl_matched
        cmp al, ' '
        je .bfl_matched
.bfl_no_match:
        ; Not a match - skip rest of line
        mov al, [esi]
        cmp al, 0
        je .bfl_nf
        inc esi
        cmp al, 0x0A
        jne .bfl_no_match
        jmp .bfl_outer
.bfl_matched:
        ; Skip to end of ':LABEL' line so ESI points to next line
.bfl_skip_rest:
        lodsb
        cmp al, 0
        je .bfl_found
        cmp al, 0x0A
        jne .bfl_skip_rest
.bfl_found:
        mov edi, edx            ; restore EDI
        pop edx
        clc
        ret
.bfl_nf:
        mov edi, edx            ; restore EDI
        pop edx
        stc
        ret

;---------------------------------------
; batch_check_if - Handle 'if [not] errorlevel N cmd' batch directive
; Input: batch_line_buf = current line, ESI = batch loop position
; Output: CF=0: was an if, ESI possibly updated (if cmd was goto)
;         CF=1: not an if, ESI unchanged
; Clobbers: EAX, EBX, ECX, EDX, EDI
;---------------------------------------
batch_check_if:
        mov al, [batch_line_buf]
        or al, 0x20
        cmp al, 'i'
        jne .bci_not
        mov al, [batch_line_buf+1]
        or al, 0x20
        cmp al, 'f'
        jne .bci_not
        cmp byte [batch_line_buf+2], ' '
        jne .bci_not
        ; It's an 'if' - save batch loop ESI
        push esi                ; PUSH#1: batch loop ESI
        ; EDI walks through the condition starting at batch_line_buf+3
        mov edi, batch_line_buf + 3
        ; Skip leading spaces
.bci_sp1:
        cmp byte [edi], ' '
        jne .bci_sp1_done
        inc edi
        jmp .bci_sp1
.bci_sp1_done:
        ; Check for 'not ' keyword
        xor ebx, ebx            ; EBX = 0: normal (not inverted)
        mov al, [edi]
        or al, 0x20
        cmp al, 'n'
        jne .bci_no_not
        mov al, [edi+1]
        or al, 0x20
        cmp al, 'o'
        jne .bci_no_not
        mov al, [edi+2]
        or al, 0x20
        cmp al, 't'
        jne .bci_no_not
        cmp byte [edi+3], ' '
        jne .bci_no_not
        mov ebx, 1              ; invert condition
        add edi, 4
.bci_sp2:
        cmp byte [edi], ' '
        jne .bci_no_not
        inc edi
        jmp .bci_sp2
.bci_no_not:
        ; Check for 'errorlevel ' keyword (case-insensitive)
        push edi                ; PUSH#2: saved EDI before keyword check
        push esi                ; PUSH#3: saved ESI (= batch loop ESI currently)
        mov esi, msg_if_el_kw   ; "errorlevel "
.bci_el_cmp:
        mov al, [esi]
        cmp al, 0
        je .bci_el_match        ; keyword exhausted = matched
        mov cl, [edi]
        or cl, 0x20
        cmp al, cl
        jne .bci_el_fail
        inc esi
        inc edi
        jmp .bci_el_cmp
.bci_el_match:
        pop esi                 ; discard PUSH#3 (keyword traversal ESI)
        add esp, 4              ; discard PUSH#2 (saved EDI)
        ; Stack: PUSH#1 only; EDI points to digit string for N
        xor edx, edx
.bci_getdig:
        mov al, [edi]
        cmp al, '0'
        jb .bci_n_done
        cmp al, '9'
        ja .bci_n_done
        sub al, '0'
        imul edx, 10
        movzx eax, al
        add edx, eax
        inc edi
        jmp .bci_getdig
.bci_n_done:
        cmp byte [edi], ' '
        jne .bci_check
        inc edi
.bci_check:
        ; EDI = command to run; EDX = N; EBX = invert flag
        mov eax, [shell_last_status]
        cmp eax, edx
        jb .bci_below           ; status < N
        ; status >= N: condition true unless inverted
        test ebx, ebx
        jnz .bci_skip
        jmp .bci_execute
.bci_below:
        ; status < N: condition true if inverted
        test ebx, ebx
        jz .bci_skip
.bci_execute:
        ; Check if command is 'goto LABEL'
        mov al, [edi]
        or al, 0x20
        cmp al, 'g'
        jne .bci_run_cmd
        mov al, [edi+1]
        or al, 0x20
        cmp al, 'o'
        jne .bci_run_cmd
        mov al, [edi+2]
        or al, 0x20
        cmp al, 't'
        jne .bci_run_cmd
        mov al, [edi+3]
        or al, 0x20
        cmp al, 'o'
        jne .bci_run_cmd
        cmp byte [edi+4], ' '
        jne .bci_run_cmd
        ; It's 'goto LABEL'
        add edi, 5
.bci_goto_sp:
        cmp byte [edi], ' '
        jne .bci_do_goto
        inc edi
        jmp .bci_goto_sp
.bci_do_goto:
        call batch_find_label   ; EDI = label name; sets ESI if found
        jnc .bci_goto_found
        ; Label not found
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_batch_label_nf
        call vga_print
        mov esi, edi            ; label name (EDI preserved by batch_find_label)
        call vga_print
        mov al, 0x0A
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
        pop esi                 ; restore batch loop ESI (PUSH#1)
        ; Abort: advance to end of script
        mov esi, batch_script_buf
.bci_if_abort:
        cmp byte [esi], 0
        je .bci_done_clc
        inc esi
        jmp .bci_if_abort
.bci_goto_found:
        ; ESI = new batch position (set by batch_find_label)
        mov eax, esi
        pop esi                 ; pop saved batch loop ESI (PUSH#1)
        mov esi, eax            ; set to new position
        jmp .bci_done_clc
.bci_run_cmd:
        ; Copy EDI (command) to line_buffer and execute
        push esi                ; PUSH#2: save ESI around shell_parse_cmd
        mov esi, edi
        mov edi, line_buffer
.bci_cp:
        lodsb
        stosb
        test al, al
        jnz .bci_cp
        call shell_parse_cmd
        pop esi                 ; restore PUSH#2 inner ESI
        pop esi                 ; restore PUSH#1 batch loop ESI
        jmp .bci_done_clc
.bci_skip:
        ; Condition not met
        pop esi                 ; restore PUSH#1 batch loop ESI
        jmp .bci_done_clc
.bci_el_fail:
        ; Unrecognized condition keyword
        pop esi                 ; discard PUSH#3
        pop edi                 ; restore PUSH#2 (EDI before keyword check)
        pop esi                 ; restore PUSH#1 batch loop ESI
.bci_done_clc:
        clc
        ret
.bci_not:
        stc
        ret

cmd_mkdir:
        pushad
        mov edi, filename_buf
        mov ecx, 255
        call copy_word_n
        mov esi, filename_buf
        cmp byte [esi], 0
        je .mk_nospace
        call hbfs_mkdir
        jc .mk_fail
        mov esi, msg_dir_created
        call vga_print
        popad
        ret
.mk_fail:
        ; Check if it already exists
        mov esi, filename_buf
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .mk_nospace
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_already_exists
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.mk_nospace:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_write_err
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

cmd_cd:
        pushad
        call cmd_cd_internal
        cmp eax, -1             ; cmd_cd_internal returns 0 or -1 in EAX
        jne .cd_ok
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
.cd_ok:
        popad
        ret

cmd_pwd:
        pushad
        mov edi, path_search_buf
        call build_cwd_path
        mov esi, path_search_buf
        call vga_print
        mov al, 0x0A
        call vga_putchar
        popad
        ret

cmd_set_env:
        pushad
        cmp byte [esi], 0
        je .set_show
        mov edi, env_temp_buf
.set_cp:
        lodsb
        cmp al, ' '
        je .set_space
        stosb
        cmp al, 0
        jne .set_cp
        jmp .set_check
.set_space:
        mov byte [edi], '='
        inc edi
.set_rest:
        lodsb
        stosb
        cmp al, 0
        jne .set_rest
.set_check:
        mov esi, env_temp_buf
        call env_set_str
        popad
        ret
.set_show:
        mov edi, env_table
        mov ecx, ENV_MAX
.set_loop:
        cmp byte [edi], 0
        je .set_skip
        push esi
        mov esi, edi
        call vga_print
        mov al, 0x0A
        call vga_putchar
        pop esi
.set_skip:
        add edi, ENV_ENTRY_SIZE
        loop .set_loop
        popad
        ret

cmd_unset_env:
        pushad
        call env_unset
        popad
        ret

;=======================================================================
; TAB COMPLETION
;=======================================================================

shell_tab_complete:
        push eax
        push ebx
        push edx
        push esi
        push edi
        push ebp
        mov byte [line_buffer + ecx], 0
        mov [.tc_count], ecx
        call hbfs_load_root_dir
        mov edi, hbfs_dir_buf
        xor ebx, ebx
        xor ebp, ebp
        call hbfs_get_max_entries
        mov edx, eax
.tc_scan:
        cmp edx, 0
        je .tc_result
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .tc_next
        push esi
        push edi
        mov esi, line_buffer
.tc_cmp:
        mov al, [esi]
        cmp al, 0
        je .tc_match
        cmp al, [edi]
        jne .tc_nomatch
        inc esi
        inc edi
        jmp .tc_cmp
.tc_match:
        pop edi
        pop esi
        inc ebx
        mov ebp, edi
        jmp .tc_next
.tc_nomatch:
        pop edi
        pop esi
.tc_next:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec edx
        jmp .tc_scan
.tc_result:
        cmp ebx, 0
        je .tc_done
        cmp ebx, 1
        je .tc_single
        mov al, 0x0A
        call vga_putchar
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov edx, eax
.tc_show:
        cmp edx, 0
        je .tc_reprint
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .tc_snext
        push esi
        push edi
        mov esi, line_buffer
.tc_scmp:
        mov al, [esi]
        cmp al, 0
        je .tc_syes
        cmp al, [edi]
        jne .tc_sno
        inc esi
        inc edi
        jmp .tc_scmp
.tc_syes:
        pop edi
        pop esi
        push esi
        mov esi, edi
        call vga_print
        mov al, ' '
        call vga_putchar
        call vga_putchar
        pop esi
        jmp .tc_snext
.tc_sno:
        pop edi
        pop esi
.tc_snext:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec edx
        jmp .tc_show
.tc_reprint:
        mov al, 0x0A
        call vga_putchar
        push esi
        mov byte [vga_color], COLOR_PROMPT
        mov esi, shell_prompt_pre
        call vga_print
        ; Build and print full CWD path
        mov edi, path_search_buf
        call build_cwd_path
        mov esi, path_search_buf
        call vga_print
        mov esi, shell_prompt_post
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        mov esi, line_buffer
        call vga_print
        pop esi
        jmp .tc_done
.tc_single:
        mov esi, ebp
        mov ecx, [.tc_count]
        add esi, ecx
.tc_type:
        lodsb
        cmp al, 0
        je .tc_space
        mov [line_buffer + ecx], al
        inc ecx
        call vga_putchar
        jmp .tc_type
.tc_space:
        mov byte [line_buffer + ecx], ' '
        inc ecx
        mov al, ' '
        call vga_putchar
        mov [.tc_count], ecx
.tc_done:
        mov ecx, [.tc_count]
        pop ebp
        pop edi
        pop esi
        pop edx
        pop ebx
        pop eax
        ret
.tc_count: dd 0

;=======================================================================
; UTILITY: String copy ESI -> EDI
;=======================================================================

;=======================================================================
; UTILITY: Build full CWD path into buffer
; EDI = destination buffer (must be >= 4096 bytes)
; Writes null-terminated full path like "/a/b/c" or "/"
;=======================================================================
PATH_BUF_MAX    equ 4095        ; Max chars (reserve 1 for null)

build_cwd_path:
        push esi
        push ecx
        push eax
        push ebx
        push edx
        mov edx, edi            ; save buffer start
        lea ebx, [edi + PATH_BUF_MAX] ; ebx = one-past-last writable byte

        ; If at root, just write "/"
        cmp dword [dir_depth], 0
        jne .bcp_build
        mov byte [edi], '/'
        mov byte [edi + 1], 0
        jmp .bcp_done

.bcp_build:
        xor ecx, ecx           ; stack index
.bcp_loop:
        cmp ecx, [dir_depth]
        jge .bcp_current
        mov eax, ecx
        imul eax, DIR_STACK_ENTRY_SIZE
        lea esi, [dir_stack + eax + 8]
        ; Skip root "/" entry
        cmp byte [esi], '/'
        jne .bcp_add_comp
        inc ecx
        jmp .bcp_loop
.bcp_add_comp:
        cmp edi, ebx
        jge .bcp_trunc
        mov byte [edi], '/'
        inc edi
.bcp_cp_stack:
        cmp edi, ebx
        jge .bcp_trunc
        lodsb
        cmp al, 0
        je .bcp_stack_done
        stosb
        jmp .bcp_cp_stack
.bcp_stack_done:
        inc ecx
        jmp .bcp_loop

.bcp_current:
        ; Append current directory
        cmp edi, ebx
        jge .bcp_trunc
        mov byte [edi], '/'
        inc edi
        mov esi, current_dir_name
.bcp_cp_cur:
        cmp edi, ebx
        jge .bcp_trunc
        lodsb
        stosb
        cmp al, 0
        jne .bcp_cp_cur
        ; Null terminator already written
        jmp .bcp_done

.bcp_trunc:
        mov byte [edi], 0      ; Force null-terminate at limit

.bcp_done:
        pop edx
        pop ebx
        pop eax
        pop ecx
        pop esi
        ret
