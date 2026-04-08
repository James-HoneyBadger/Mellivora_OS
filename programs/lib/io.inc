;=======================================================================
; io.inc - Mellivora I/O API
;
; Console input/output, file operations, argument parsing, and
; formatted printing routines.
;
; Requires: syscalls.inc (must be included before this file)
;           string.inc (for io_print_padded and io_print_centered)
;
; Functions:
;   io_read_line    - Read a line with editing (backspace, echo)
;   io_read_num     - Read a decimal number from console
;   io_read_key     - Non-blocking key check
;   io_print        - Print null-terminated string
;   io_println      - Print string followed by newline
;   io_putchar      - Output a single character
;   io_newline      - Output a newline (LF)
;   io_print_repeat - Print a character N times
;   io_clear        - Clear the screen
;   io_get_args     - Get command-line arguments
;   io_parse_args   - Parse args into argc/argv style
;   io_file_read    - Read entire file into buffer
;   io_file_write   - Write buffer to file
;   io_file_exists  - Check if file exists
;   io_file_size    - Get file size
;   io_file_delete  - Delete a file
;   io_dir_read     - Read directory entry
;   io_dir_create   - Create a directory
;   io_dir_change   - Change current directory
;   io_dir_getcwd   - Get current working directory
;   io_print_padded - Print string padded to width
;   io_print_centered - Print string centered on screen
;   io_beep         - Play a beep
;   io_sleep        - Sleep for N ticks
;   io_get_time     - Get system tick count
;=======================================================================

;-----------------------------------------------------------------------
; io_read_line - Read a line from console with editing
; Input:  EDI = buffer pointer
;         ECX = max buffer size (including null terminator)
; Output: EAX = number of characters read (not counting null)
; Features: Echoes typed chars, handles backspace, Enter terminates
;-----------------------------------------------------------------------
io_read_line:
        push ebx
        push ecx
        push edx
        push edi
        dec ecx                 ; reserve for null
        xor edx, edx           ; char count

.irl_loop:
        mov eax, SYS_GETCHAR
        int 0x80

        cmp al, 0x0D           ; Enter
        je .irl_done
        cmp al, 0x0A           ; LF
        je .irl_done
        cmp al, 0x08           ; Backspace
        je .irl_bs
        cmp al, 0x7F           ; Delete key
        je .irl_bs
        cmp al, 27             ; Escape
        je .irl_esc

        ; Printable range check
        cmp al, 32
        jb .irl_loop
        cmp al, 126
        ja .irl_loop

        ; Buffer full?
        cmp edx, ecx
        jge .irl_loop

        ; Store and echo
        mov [edi + edx], al
        inc edx
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        jmp .irl_loop

.irl_bs:
        cmp edx, 0
        je .irl_loop
        dec edx
        ; Erase on screen: backspace + space + backspace
        mov eax, SYS_PUTCHAR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x08
        int 0x80
        jmp .irl_loop

.irl_esc:
        ; Clear entire input
        cmp edx, 0
        je .irl_loop
.irl_esc_del:
        cmp edx, 0
        je .irl_loop
        dec edx
        mov eax, SYS_PUTCHAR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x08
        int 0x80
        jmp .irl_esc_del

.irl_done:
        mov byte [edi + edx], 0
        mov eax, edx
        pop edi
        pop edx
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; io_read_num - Read a decimal number from console
; Input:  ECX = max digits
; Output: EAX = parsed number (0 if empty/invalid)
;         Carry flag set if empty input
;-----------------------------------------------------------------------
io_read_num:
        push ebx
        push ecx
        push edx
        push esi
        push edi
        sub esp, 16             ; local buffer on stack
        mov edi, esp
        mov edx, ecx
        cmp edx, 14
        jle .irn_ok
        mov edx, 14
.irn_ok:
        xor ecx, ecx           ; count

.irn_loop:
        mov eax, SYS_GETCHAR
        int 0x80

        cmp al, 0x0D
        je .irn_parse
        cmp al, 0x0A
        je .irn_parse
        cmp al, 0x08
        je .irn_bs
        cmp al, 0x7F
        je .irn_bs

        ; Digits only
        cmp al, '0'
        jb .irn_loop
        cmp al, '9'
        ja .irn_loop

        cmp ecx, edx
        jge .irn_loop

        mov [edi + ecx], al
        inc ecx
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        jmp .irn_loop

.irn_bs:
        cmp ecx, 0
        je .irn_loop
        dec ecx
        mov eax, SYS_PUTCHAR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x08
        int 0x80
        jmp .irn_loop

.irn_parse:
        mov byte [edi + ecx], 0
        cmp ecx, 0
        je .irn_empty

        ; Convert ASCII to integer
        mov esi, edi
        xor eax, eax
        mov ebx, 10
.irn_conv:
        movzx edx, byte [esi]
        cmp dl, 0
        je .irn_result
        cmp dl, '0'
        jb .irn_result
        cmp dl, '9'
        ja .irn_result
        imul eax, ebx
        sub dl, '0'
        movzx edx, dl
        add eax, edx
        inc esi
        jmp .irn_conv

.irn_result:
        add esp, 16
        clc                     ; clear carry = valid
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret

.irn_empty:
        xor eax, eax
        add esp, 16
        stc                     ; set carry = empty
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; io_read_key - Non-blocking key check
; Output: EAX = key code, or 0 if no key pressed
;-----------------------------------------------------------------------
io_read_key:
        mov eax, SYS_READ_KEY
        int 0x80
        ret

;-----------------------------------------------------------------------
; io_print - Print null-terminated string
; Input:  ESI = string pointer
; Output: none
;-----------------------------------------------------------------------
io_print:
        push eax
        push ebx
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        pop ebx
        pop eax
        ret

;-----------------------------------------------------------------------
; io_println - Print string followed by newline
; Input:  ESI = string pointer
; Output: none
;-----------------------------------------------------------------------
io_println:
        call io_print
        call io_newline
        ret

;-----------------------------------------------------------------------
; io_putchar - Output a single character
; Input:  AL = character
; Output: none
;-----------------------------------------------------------------------
io_putchar:
        push eax
        push ebx
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop ebx
        pop eax
        ret

;-----------------------------------------------------------------------
; io_newline - Output a newline character
;-----------------------------------------------------------------------
io_newline:
        push eax
        push ebx
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        pop ebx
        pop eax
        ret

;-----------------------------------------------------------------------
; io_print_repeat - Print a character N times
; Input:  AL = character, ECX = count
; Output: none
;-----------------------------------------------------------------------
io_print_repeat:
        push eax
        push ebx
        push ecx
        movzx ebx, al
.ipr_loop:
        cmp ecx, 0
        jle .ipr_done
        mov eax, SYS_PUTCHAR
        int 0x80
        dec ecx
        jmp .ipr_loop
.ipr_done:
        pop ecx
        pop ebx
        pop eax
        ret

;-----------------------------------------------------------------------
; io_clear - Clear the screen
;-----------------------------------------------------------------------
io_clear:
        push eax
        mov eax, SYS_CLEAR
        int 0x80
        pop eax
        ret

;-----------------------------------------------------------------------
; io_get_args - Get command-line arguments
; Input:  EDI = buffer (256 bytes recommended)
; Output: EAX = argument string length (0 = no args)
;-----------------------------------------------------------------------
io_get_args:
        push ebx
        mov eax, SYS_GETARGS
        mov ebx, edi
        int 0x80
        pop ebx
        ret

;-----------------------------------------------------------------------
; io_parse_args - Parse whitespace-separated args into array
; Input:  ESI = argument string (will be modified - nulls inserted)
;         EDI = array of dword pointers (argv[])
;         ECX = max entries in argv
; Output: EAX = argc (number of arguments found)
;-----------------------------------------------------------------------
io_parse_args:
        push ebx
        push ecx
        push edx
        push esi
        push edi
        xor eax, eax           ; argc

.ipa_skip_spaces:
        cmp byte [esi], 0
        je .ipa_done
        cmp byte [esi], ' '
        je .ipa_next_space
        cmp byte [esi], 9
        je .ipa_next_space
        jmp .ipa_found_arg

.ipa_next_space:
        inc esi
        jmp .ipa_skip_spaces

.ipa_found_arg:
        cmp eax, ecx
        jge .ipa_done           ; argv full
        mov [edi], esi          ; store pointer
        add edi, 4
        inc eax

        ; Find end of this arg
.ipa_scan_arg:
        cmp byte [esi], 0
        je .ipa_done
        cmp byte [esi], ' '
        je .ipa_terminate
        cmp byte [esi], 9
        je .ipa_terminate
        inc esi
        jmp .ipa_scan_arg

.ipa_terminate:
        mov byte [esi], 0      ; null-terminate this arg
        inc esi
        jmp .ipa_skip_spaces

.ipa_done:
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; io_file_read - Read entire file into buffer
; Input:  ESI = filename (null-terminated)
;         EDI = buffer
; Output: EAX = bytes read, or -1 on error
;-----------------------------------------------------------------------
io_file_read:
        push ebx
        push ecx
        mov eax, SYS_FREAD
        mov ebx, esi
        mov ecx, edi
        int 0x80
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; io_file_write - Write buffer to file
; Input:  ESI = filename, EDI = buffer, ECX = size, EDX = file type
; Output: EAX = 0 on success, -1 on error
;-----------------------------------------------------------------------
io_file_write:
        push ebx
        push ecx
        push esi
        mov eax, SYS_FWRITE
        mov ebx, esi
        mov ecx, edi
        ; EDX = size already in place -- need to swap
        ; Actually: EBX=name, ECX=buf, EDX=size, ESI=type
        ; We have ESI=name, EDI=buf, ECX=size, EDX=type
        mov ebx, esi            ; filename
        mov esi, edx            ; type
        mov edx, ecx            ; size
        mov ecx, edi            ; buffer
        int 0x80
        pop esi
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; io_file_exists - Check if file exists
; Input:  ESI = filename
; Output: EAX = 1 if exists, 0 if not
;-----------------------------------------------------------------------
io_file_exists:
        push ebx
        mov eax, SYS_STAT
        mov ebx, esi
        int 0x80
        cmp eax, -1
        je .ife_no
        mov eax, 1
        pop ebx
        ret
.ife_no:
        xor eax, eax
        pop ebx
        ret

;-----------------------------------------------------------------------
; io_file_size - Get file size
; Input:  ESI = filename
; Output: EAX = file size in bytes, or -1 if not found
;-----------------------------------------------------------------------
io_file_size:
        push ebx
        mov eax, SYS_STAT
        mov ebx, esi
        int 0x80
        pop ebx
        ret

;-----------------------------------------------------------------------
; io_file_delete - Delete a file
; Input:  ESI = filename
; Output: EAX = 0 on success, -1 on error
;-----------------------------------------------------------------------
io_file_delete:
        push ebx
        mov eax, SYS_DELETE
        mov ebx, esi
        int 0x80
        pop ebx
        ret

;-----------------------------------------------------------------------
; io_dir_read - Read directory entry by index
; Input:  EDI = buffer for filename, ECX = index
; Output: EAX = file type (1=text, 2=dir, 3=exec, -1=end)
;         ECX = file size
;-----------------------------------------------------------------------
io_dir_read:
        push ebx
        mov eax, SYS_READDIR
        mov ebx, edi
        int 0x80
        pop ebx
        ret

;-----------------------------------------------------------------------
; io_dir_create - Create a directory
; Input:  ESI = directory name
; Output: EAX = 0 on success, -1 on error
;-----------------------------------------------------------------------
io_dir_create:
        push ebx
        mov eax, SYS_MKDIR
        mov ebx, esi
        int 0x80
        pop ebx
        ret

;-----------------------------------------------------------------------
; io_dir_change - Change current directory
; Input:  ESI = path
; Output: EAX = 0 on success, -1 on error
;-----------------------------------------------------------------------
io_dir_change:
        push ebx
        mov eax, SYS_CHDIR
        mov ebx, esi
        int 0x80
        pop ebx
        ret

;-----------------------------------------------------------------------
; io_dir_getcwd - Get current working directory
; Input:  EDI = buffer
; Output: EAX = 0
;-----------------------------------------------------------------------
io_dir_getcwd:
        push ebx
        mov eax, SYS_GETCWD
        mov ebx, edi
        int 0x80
        pop ebx
        ret

;-----------------------------------------------------------------------
; io_print_padded - Print string left-padded or right-padded to width
; Input:  ESI = string, ECX = width, AL = pad char, AH = 0=left, 1=right
; Output: none
; Left-align:  "hello     "
; Right-align: "     hello"
;-----------------------------------------------------------------------
io_print_padded:
        pushad
        mov bl, al              ; pad char
        mov dl, ah              ; alignment

        ; Get string length
        push esi
        call str_len
        mov edi, eax            ; string len
        pop esi

        ; Padding needed
        sub ecx, edi
        cmp ecx, 0
        jle .ipp_just_print     ; string >= width, just print it

        cmp dl, 1
        je .ipp_right

        ; Left-align: print string, then pad
        push ecx
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        pop ecx
        mov al, bl
.ipp_pad_loop:
        cmp ecx, 0
        jle .ipp_done
        push ecx
        movzx ebx, al
        push eax
        mov eax, SYS_PUTCHAR
        int 0x80
        pop eax
        pop ecx
        dec ecx
        jmp .ipp_pad_loop

.ipp_right:
        ; Right-align: pad, then print string
        mov al, bl
.ipp_rpad_loop:
        cmp ecx, 0
        jle .ipp_rprint
        push ecx
        push eax
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop eax
        pop ecx
        dec ecx
        jmp .ipp_rpad_loop
.ipp_rprint:
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        jmp .ipp_done

.ipp_just_print:
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
.ipp_done:
        popad
        ret

;-----------------------------------------------------------------------
; io_print_centered - Print string centered in 80-column screen
; Input:  ESI = string, ECX = row
; Output: none
;-----------------------------------------------------------------------
io_print_centered:
        pushad
        ; Get length
        push esi
        call str_len
        mov edx, eax
        pop esi

        ; Column = (80 - len) / 2
        mov ebx, 80
        sub ebx, edx
        shr ebx, 1
        cmp ebx, 0
        jge .ipc_pos
        xor ebx, ebx
.ipc_pos:
        mov eax, SYS_SETCURSOR
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        popad
        ret

;-----------------------------------------------------------------------
; io_beep - Play a tone
; Input:  EBX = frequency (Hz), ECX = duration (ticks)
; Output: none
;-----------------------------------------------------------------------
io_beep:
        push eax
        mov eax, SYS_BEEP
        int 0x80
        pop eax
        ret

;-----------------------------------------------------------------------
; io_sleep - Sleep for N ticks (100 ticks = 1 second)
; Input:  EBX = ticks
; Output: none
;-----------------------------------------------------------------------
io_sleep:
        push eax
        mov eax, SYS_SLEEP
        int 0x80
        pop eax
        ret

;-----------------------------------------------------------------------
; io_get_time - Get system tick count
; Output: EAX = ticks since boot (100Hz)
;-----------------------------------------------------------------------
io_get_time:
        mov eax, SYS_GETTIME
        int 0x80
        ret
