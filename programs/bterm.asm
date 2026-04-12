; bterm.asm - Burrows Terminal Emulator
; A full shell-like terminal running inside a GUI window.
; Supports: ls, cat, cd, pwd, mkdir, rm, touch, echo, clear,
;           date, help, exit, ver, size, hex, write, whoami

%include "syscalls.inc"
%include "lib/gui.inc"

TERM_LINES      equ 18          ; visible output lines
TERM_COLS       equ 58          ; chars per line
INPUT_MAX       equ 58          ; max input length
FILE_BUF_SIZE   equ 32768       ; 32 KB file read buffer

start:
        ; Create window
        mov eax, 40             ; x
        mov ebx, 40             ; y
        mov ecx, 480            ; w
        mov edx, 320            ; h
        mov esi, title_str
        call gui_create_window
        cmp eax, -1
        je .exit
        mov [win_id], eax

        ; Initialize terminal state
        mov dword [input_len], 0
        mov byte [input_buf], 0
        mov dword [num_lines], 0

        ; Show welcome
        mov esi, msg_welcome
        call term_add_line

.main_loop:
        ; Compose + draw + flip
        call gui_compose
        call term_draw_content
        call gui_flip

        ; Poll events
        call gui_poll_event
        cmp eax, EVT_CLOSE
        je .close
        cmp eax, EVT_KEY_PRESS
        jne .main_loop

        ; Key pressed (EBX = key code)
        cmp bl, 27              ; ESC
        je .close
        cmp bl, 13              ; Enter
        je .handle_enter
        cmp bl, 8               ; Backspace
        je .handle_bs
        cmp bl, 32
        jl .main_loop
        cmp bl, 126
        jg .main_loop

        ; Add char to input
        mov ecx, [input_len]
        cmp ecx, INPUT_MAX
        jge .main_loop
        mov [input_buf + ecx], bl
        inc dword [input_len]
        mov byte [input_buf + ecx + 1], 0
        jmp .main_loop

.handle_bs:
        cmp dword [input_len], 0
        je .main_loop
        dec dword [input_len]
        mov ecx, [input_len]
        mov byte [input_buf + ecx], 0
        jmp .main_loop

.handle_enter:
        ; Show prompt line in output
        call term_add_prompt_line
        ; Execute
        call term_exec_cmd
        ; Reset input
        mov dword [input_len], 0
        mov byte [input_buf], 0
        jmp .main_loop

.close:
        mov eax, [win_id]
        call gui_destroy_window
.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; term_draw_content - Draw terminal text
;---------------------------------------
term_draw_content:
        pushad
        ; Clear content area
        mov eax, [win_id]
        mov ebx, 0
        mov ecx, 0
        mov edx, 480
        mov esi, 320
        mov edi, 0x00101010     ; dark bg
        call gui_fill_rect

        ; Draw output lines
        xor ecx, ecx
        mov edx, 4
.draw_lines:
        cmp ecx, [num_lines]
        jge .draw_prompt
        cmp ecx, TERM_LINES
        jge .draw_prompt
        push ecx
        push edx
        mov eax, ecx
        shl eax, 6             ; * 64 bytes per line
        lea esi, [output_buf + eax]
        mov eax, [win_id]
        mov ebx, 4
        mov ecx, edx
        mov edi, 0x0000CC00     ; green text
        call gui_draw_text
        pop edx
        pop ecx
        add edx, 16
        inc ecx
        jmp .draw_lines

.draw_prompt:
        ; Draw "> " prompt
        mov eax, [win_id]
        mov ebx, 4
        mov ecx, edx
        mov esi, prompt_str
        mov edi, 0x0000FF00
        call gui_draw_text

        ; Draw input text
        mov eax, [win_id]
        mov ebx, 20
        mov ecx, edx
        mov esi, input_buf
        mov edi, 0x00FFFFFF
        call gui_draw_text

        ; Draw cursor
        mov eax, [input_len]
        shl eax, 3
        add eax, 20
        mov ebx, eax
        mov eax, [win_id]
        mov ecx, edx
        mov esi, cursor_str
        mov edi, 0x00FFFFFF
        call gui_draw_text

        popad
        ret

;---------------------------------------
; term_exec_cmd - Execute the input buffer command
;---------------------------------------
term_exec_cmd:
        pushad
        cmp dword [input_len], 0
        je .done

        ; --- Match commands ---
        mov esi, input_buf

        ; ls / dir
        call .try_ls
        cmp eax, 1
        je .done
        ; cat
        call .try_cat
        cmp eax, 1
        je .done
        ; cd
        call .try_cd
        cmp eax, 1
        je .done
        ; pwd
        mov edi, cmd_pwd
        call str_eq
        je .do_pwd
        ; mkdir
        call .try_mkdir
        cmp eax, 1
        je .done
        ; rm / del
        call .try_rm
        cmp eax, 1
        je .done
        ; touch
        call .try_touch
        cmp eax, 1
        je .done
        ; write
        call .try_write
        cmp eax, 1
        je .done
        ; echo
        mov esi, input_buf
        mov edi, cmd_echo
        call str_starts
        cmp eax, 1
        je .do_echo
        ; clear
        mov esi, input_buf
        mov edi, cmd_clear
        call str_eq
        je .do_clear
        ; date
        mov esi, input_buf
        mov edi, cmd_date
        call str_eq
        je .do_date
        ; ver
        mov esi, input_buf
        mov edi, cmd_ver
        call str_eq
        je .do_ver
        ; size
        call .try_size
        cmp eax, 1
        je .done
        ; hex
        call .try_hex
        cmp eax, 1
        je .done
        ; whoami
        mov esi, input_buf
        mov edi, cmd_whoami
        call str_eq
        je .do_whoami
        ; help
        mov esi, input_buf
        mov edi, cmd_help
        call str_eq
        je .do_help
        ; exit
        mov esi, input_buf
        mov edi, cmd_exit
        call str_eq
        je .do_exit

        ; Unknown command
        mov esi, msg_unknown
        call term_add_line
        jmp .done

; --- Command: ls ---
.try_ls:
        push esi
        mov esi, input_buf
        mov edi, cmd_ls
        call str_eq
        je .do_ls_ok
        mov esi, input_buf
        mov edi, cmd_dir
        call str_eq
        je .do_ls_ok
        pop esi
        xor eax, eax
        ret
.do_ls_ok:
        pop esi
        ; Read directory entries
        xor ecx, ecx           ; index
.ls_loop:
        push ecx               ; save index
        mov eax, SYS_READDIR
        mov ebx, tmp_buf       ; buf for entry name
        int 0x80
        ; EAX = type (-1=end, 0=free, 1=text, 2=dir, 3=exec)
        ; ECX = size (clobbered)
        cmp eax, -1
        je .ls_end
        cmp eax, 0              ; skip free/empty slots
        je .ls_next
        cmp byte [tmp_buf], 0   ; skip empty names
        je .ls_next

        ; Check if directory
        cmp eax, 2              ; FTYPE_DIR
        jne .ls_file
        ; Directory: append /
        mov esi, tmp_buf
        mov edi, fmt_buf
        call str_copy
        mov edi, fmt_buf
        call str_len
        mov byte [fmt_buf + eax], '/'
        mov byte [fmt_buf + eax + 1], 0
        mov esi, fmt_buf
        call term_add_line
        jmp .ls_next
.ls_file:
        ; File: just show name
        mov esi, tmp_buf
        call term_add_line
.ls_next:
        pop ecx
        inc ecx
        jmp .ls_loop
.ls_end:
        pop ecx                 ; balance the push
        mov eax, 1
        ret

; --- Command: cat <file> ---
.try_cat:
        mov esi, input_buf
        mov edi, cmd_cat
        call str_starts
        cmp eax, 1
        jne .try_cat_no
        ; Get filename
        lea ebx, [input_buf + 4]
        ; Skip leading spaces
        call skip_spaces
        cmp byte [ebx], 0
        je .cat_usage
        ; Read file
        mov eax, SYS_FREAD
        mov ecx, file_buf
        int 0x80
        cmp eax, 0
        jl .cat_err
        cmp eax, 0
        je .cat_empty
        ; Display content line by line
        mov [.cat_len], eax
        mov esi, file_buf
        mov edi, fmt_buf
        xor ecx, ecx           ; column position
.cat_char:
        cmp dword [.cat_len], 0
        jle .cat_done
        lodsb
        dec dword [.cat_len]
        cmp al, 0x0A            ; newline
        je .cat_nl
        cmp al, 0x0D            ; skip CR
        je .cat_char
        cmp al, 0x09            ; tab -> spaces
        je .cat_tab
        cmp al, 32
        jl .cat_char            ; skip control chars
        mov [edi], al
        inc edi
        inc ecx
        cmp ecx, TERM_COLS
        jl .cat_char
.cat_nl:
        mov byte [edi], 0
        push esi
        mov esi, fmt_buf
        call term_add_line
        pop esi
        mov edi, fmt_buf
        xor ecx, ecx
        jmp .cat_char
.cat_tab:
        mov byte [edi], ' '
        inc edi
        inc ecx
        cmp ecx, TERM_COLS
        jl .cat_char
        jmp .cat_nl
.cat_done:
        ; Flush remaining
        cmp ecx, 0
        je .cat_ret
        mov byte [edi], 0
        push esi
        mov esi, fmt_buf
        call term_add_line
        pop esi
.cat_ret:
        mov eax, 1
        ret
.cat_empty:
        mov esi, msg_empty_file
        call term_add_line
        mov eax, 1
        ret
.cat_err:
        mov esi, msg_file_err
        call term_add_line
        mov eax, 1
        ret
.cat_usage:
        mov esi, msg_cat_usage
        call term_add_line
        mov eax, 1
        ret
.try_cat_no:
        xor eax, eax
        ret
.cat_len: dd 0

; --- Command: cd <dir> ---
.try_cd:
        mov esi, input_buf
        mov edi, cmd_cd
        call str_starts
        cmp eax, 1
        jne .try_cd_no
        lea ebx, [input_buf + 3]
        call skip_spaces
        cmp byte [ebx], 0
        je .cd_home
        mov eax, SYS_CHDIR
        int 0x80
        cmp eax, 0
        jne .cd_err
        mov eax, 1
        ret
.cd_home:
        ; cd with no args goes to /
        mov ebx, root_path
        mov eax, SYS_CHDIR
        int 0x80
        mov eax, 1
        ret
.cd_err:
        mov esi, msg_cd_err
        call term_add_line
        mov eax, 1
        ret
.try_cd_no:
        xor eax, eax
        ret

; --- Command: pwd ---
.do_pwd:
        mov eax, SYS_GETCWD
        mov ebx, tmp_buf
        int 0x80
        mov esi, tmp_buf
        call term_add_line
        jmp .done

; --- Command: mkdir <name> ---
.try_mkdir:
        mov esi, input_buf
        mov edi, cmd_mkdir
        call str_starts
        cmp eax, 1
        jne .try_mkdir_no
        lea ebx, [input_buf + 6]
        call skip_spaces
        cmp byte [ebx], 0
        je .mkdir_usage
        mov eax, SYS_MKDIR
        int 0x80
        cmp eax, 0
        jne .mkdir_err
        mov esi, msg_ok
        call term_add_line
        mov eax, 1
        ret
.mkdir_usage:
        mov esi, msg_mkdir_usage
        call term_add_line
        mov eax, 1
        ret
.mkdir_err:
        mov esi, msg_mkdir_err
        call term_add_line
        mov eax, 1
        ret
.try_mkdir_no:
        xor eax, eax
        ret

; --- Command: rm <file> / del <file> ---
.try_rm:
        mov esi, input_buf
        mov edi, cmd_rm
        call str_starts
        cmp eax, 1
        je .rm_go
        mov esi, input_buf
        mov edi, cmd_del
        call str_starts
        cmp eax, 1
        je .rm_go_del
        xor eax, eax
        ret
.rm_go:
        lea ebx, [input_buf + 3]
        call skip_spaces
        jmp .rm_exec
.rm_go_del:
        lea ebx, [input_buf + 4]
        call skip_spaces
.rm_exec:
        cmp byte [ebx], 0
        je .rm_usage
        mov eax, SYS_DELETE
        int 0x80
        cmp eax, 0
        jne .rm_err
        mov esi, msg_ok
        call term_add_line
        mov eax, 1
        ret
.rm_usage:
        mov esi, msg_rm_usage
        call term_add_line
        mov eax, 1
        ret
.rm_err:
        mov esi, msg_rm_err
        call term_add_line
        mov eax, 1
        ret

; --- Command: touch <file> ---
.try_touch:
        mov esi, input_buf
        mov edi, cmd_touch
        call str_starts
        cmp eax, 1
        jne .try_touch_no
        lea ebx, [input_buf + 6]
        call skip_spaces
        cmp byte [ebx], 0
        je .touch_usage
        ; Write 0 bytes to create empty file
        mov eax, SYS_FWRITE
        mov ecx, tmp_buf
        mov byte [tmp_buf], 0
        xor edx, edx           ; size = 0
        xor esi, esi            ; type = 0 (text)
        int 0x80
        mov esi, msg_ok
        call term_add_line
        mov eax, 1
        ret
.touch_usage:
        mov esi, msg_touch_usage
        call term_add_line
        mov eax, 1
        ret
.try_touch_no:
        xor eax, eax
        ret

; --- Command: write <file> <text> ---
.try_write:
        mov esi, input_buf
        mov edi, cmd_write
        call str_starts
        cmp eax, 1
        jne .try_write_no
        lea esi, [input_buf + 6]
        call skip_spaces_esi
        cmp byte [esi], 0
        je .write_usage
        ; Find end of filename (space separator)
        mov edi, tmp_buf
.write_fn:
        lodsb
        cmp al, ' '
        je .write_fn_done
        cmp al, 0
        je .write_usage
        stosb
        jmp .write_fn
.write_fn_done:
        mov byte [edi], 0
        ; ESI now points to the text content
        call skip_spaces_esi
        ; Measure text length
        push esi
        xor ecx, ecx
.write_len:
        cmp byte [esi + ecx], 0
        je .write_len_done
        inc ecx
        jmp .write_len
.write_len_done:
        mov edx, ecx           ; EDX = size
        pop ecx                 ; ECX = buf (the text)
        mov ebx, tmp_buf        ; EBX = filename
        xor esi, esi            ; type = 0
        mov eax, SYS_FWRITE
        int 0x80
        mov esi, msg_ok
        call term_add_line
        mov eax, 1
        ret
.write_usage:
        mov esi, msg_write_usage
        call term_add_line
        mov eax, 1
        ret
.try_write_no:
        xor eax, eax
        ret

; --- Command: size <file> ---
.try_size:
        mov esi, input_buf
        mov edi, cmd_size
        call str_starts
        cmp eax, 1
        jne .try_size_no
        lea ebx, [input_buf + 5]
        call skip_spaces
        cmp byte [ebx], 0
        je .size_usage
        mov eax, SYS_STAT
        int 0x80
        cmp eax, -1
        je .size_err
        ; EAX = file size in bytes
        push eax
        mov edi, fmt_buf
        pop eax
        call int_to_str
        mov esi, edi            ; point to formatted string
        ; Append " bytes"
        call str_len
        lea edi, [fmt_buf + eax]
        mov dword [edi], ' byt'
        mov word [edi + 4], 'es'
        mov byte [edi + 6], 0
        mov esi, fmt_buf
        call term_add_line
        mov eax, 1
        ret
.size_usage:
        mov esi, msg_size_usage
        call term_add_line
        mov eax, 1
        ret
.size_err:
        mov esi, msg_file_err
        call term_add_line
        mov eax, 1
        ret
.try_size_no:
        xor eax, eax
        ret

; --- Command: hex <file> (hex dump, first 256 bytes) ---
.try_hex:
        mov esi, input_buf
        mov edi, cmd_hex
        call str_starts
        cmp eax, 1
        jne .try_hex_no
        lea ebx, [input_buf + 4]
        call skip_spaces
        cmp byte [ebx], 0
        je .hex_usage
        mov eax, SYS_FREAD
        mov ecx, file_buf
        int 0x80
        cmp eax, 0
        jle .hex_err
        ; Show first 256 bytes (or less)
        cmp eax, 256
        jle .hex_ok
        mov eax, 256
.hex_ok:
        mov [.hex_total], eax
        xor ecx, ecx           ; offset
.hex_line:
        cmp ecx, [.hex_total]
        jge .hex_done
        ; Format "OOOO: XX XX XX XX XX XX XX XX"
        mov edi, fmt_buf
        ; Offset (4 hex digits)
        mov eax, ecx
        call hex_word
        mov byte [edi], ':'
        inc edi
        mov byte [edi], ' '
        inc edi
        ; 8 hex bytes per line
        xor edx, edx
.hex_byte:
        cmp edx, 8
        jge .hex_eol
        mov eax, ecx
        add eax, edx
        cmp eax, [.hex_total]
        jge .hex_pad
        movzx eax, byte [file_buf + eax]
        call hex_byte_out
        mov byte [edi], ' '
        inc edi
        inc edx
        jmp .hex_byte
.hex_pad:
        mov byte [edi], ' '
        mov byte [edi + 1], ' '
        mov byte [edi + 2], ' '
        add edi, 3
        inc edx
        jmp .hex_byte
.hex_eol:
        mov byte [edi], 0
        push ecx
        mov esi, fmt_buf
        call term_add_line
        pop ecx
        add ecx, 8
        jmp .hex_line
.hex_done:
        mov eax, 1
        ret
.hex_err:
        mov esi, msg_file_err
        call term_add_line
        mov eax, 1
        ret
.hex_usage:
        mov esi, msg_hex_usage
        call term_add_line
        mov eax, 1
        ret
.try_hex_no:
        xor eax, eax
        ret
.hex_total: dd 0

; --- Simple commands ---
.do_echo:
        lea esi, [input_buf + 5]
        call term_add_line
        jmp .done

.do_clear:
        mov dword [num_lines], 0
        jmp .done

.do_date:
        mov eax, SYS_DATE
        xor ebx, ebx
        mov ecx, tmp_buf
        int 0x80
        mov esi, tmp_buf
        call term_add_line
        jmp .done

.do_ver:
        mov esi, msg_version
        call term_add_line
        jmp .done

.do_whoami:
        mov esi, msg_whoami
        call term_add_line
        jmp .done

.do_help:
        mov esi, msg_help1
        call term_add_line
        mov esi, msg_help2
        call term_add_line
        mov esi, msg_help3
        call term_add_line
        mov esi, msg_help4
        call term_add_line
        jmp .done

.do_exit:
        popad
        jmp .close_exit

.close_exit:
        mov eax, [win_id]
        call gui_destroy_window
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.done:
        popad
        ret

;---------------------------------------
; term_add_line - Add string to output buffer
; ESI = string
;---------------------------------------
term_add_line:
        pushad
        mov ecx, [num_lines]
        cmp ecx, TERM_LINES
        jl .add
        ; Scroll up
        cld
        mov edi, output_buf
        push esi
        mov esi, output_buf + 64
        mov ecx, (TERM_LINES - 1) * 64 / 4
        rep movsd
        pop esi
        mov ecx, TERM_LINES - 1
        mov [num_lines], ecx
.add:
        mov eax, [num_lines]
        shl eax, 6
        lea edi, [output_buf + eax]
        mov ecx, 63
.copy:
        lodsb
        stosb
        cmp al, 0
        je .pad
        dec ecx
        jnz .copy
        mov byte [edi], 0
.pad:
        inc dword [num_lines]
        popad
        ret

;---------------------------------------
; term_add_prompt_line - Add "> <input>" line
;---------------------------------------
term_add_prompt_line:
        pushad
        mov ecx, [num_lines]
        cmp ecx, TERM_LINES
        jl .add
        cld
        mov edi, output_buf
        push esi
        mov esi, output_buf + 64
        mov ecx, (TERM_LINES - 1) * 64 / 4
        rep movsd
        pop esi
        mov ecx, TERM_LINES - 1
        mov [num_lines], ecx
.add:
        mov eax, [num_lines]
        shl eax, 6
        lea edi, [output_buf + eax]
        mov byte [edi], '>'
        mov byte [edi+1], ' '
        add edi, 2
        mov esi, input_buf
        mov ecx, 61
.copy:
        lodsb
        stosb
        cmp al, 0
        je .done
        dec ecx
        jnz .copy
        mov byte [edi], 0
.done:
        inc dword [num_lines]
        popad
        ret

;---------------------------------------
; Utility: str_starts - Check if ESI starts with EDI
; Returns: EAX = 1 if match, 0 otherwise
;---------------------------------------
str_starts:
        push esi
        push edi
.loop:
        mov al, [edi]
        cmp al, 0
        je .match
        cmp al, [esi]
        jne .no
        inc esi
        inc edi
        jmp .loop
.match:
        mov eax, 1
        pop edi
        pop esi
        ret
.no:
        xor eax, eax
        pop edi
        pop esi
        ret

;---------------------------------------
; Utility: str_eq - Check if ESI equals EDI
; Returns: EAX = 1 if match (also ZF set); 0 otherwise
;---------------------------------------
str_eq:
        push esi
        push edi
.loop:
        mov al, [esi]
        mov bl, [edi]
        cmp al, bl
        jne .no
        cmp al, 0
        je .yes
        inc esi
        inc edi
        jmp .loop
.yes:
        mov eax, 1
        test eax, eax           ; set ZF=0 (eax!=0 means match, but je tests ZF)
        ; For je to work, we need ZF set on match. Use cmp eax, 1.
        cmp eax, 1              ; ZF=1
        pop edi
        pop esi
        ret
.no:
        xor eax, eax
        ; eax=0, need ZF clear for jne. cmp eax, 1 sets ZF=0.
        cmp eax, 1              ; ZF=0
        pop edi
        pop esi
        ret

;---------------------------------------
; Utility: str_copy - Copy ESI to EDI
;---------------------------------------
str_copy:
        push esi
        push edi
.cloop:
        lodsb
        stosb
        cmp al, 0
        jne .cloop
        pop edi
        pop esi
        ret

;---------------------------------------
; Utility: str_len - Length of string at EDI -> EAX
;---------------------------------------
str_len:
        push edi
        xor eax, eax
.sloop:
        cmp byte [edi + eax], 0
        je .sdone
        inc eax
        jmp .sloop
.sdone:
        pop edi
        ret

;---------------------------------------
; Utility: skip_spaces - Advance EBX past spaces
;---------------------------------------
skip_spaces:
.skip:
        cmp byte [ebx], ' '
        jne .sdone
        inc ebx
        jmp .skip
.sdone:
        ret

;---------------------------------------
; Utility: skip_spaces_esi - Advance ESI past spaces
;---------------------------------------
skip_spaces_esi:
.skip:
        cmp byte [esi], ' '
        jne .sdone
        inc esi
        jmp .skip
.sdone:
        ret

;---------------------------------------
; Utility: int_to_str - Convert EAX to decimal in EDI
; EDI = output buffer, EAX = number
;---------------------------------------
int_to_str:
        pushad
        mov ebx, edi
        mov ecx, 10
        xor edx, edx
        ; Handle 0
        cmp eax, 0
        jne .nonzero
        mov byte [edi], '0'
        mov byte [edi + 1], 0
        popad
        ret
.nonzero:
        ; Push digits in reverse
        xor esi, esi            ; digit count
.divloop:
        cmp eax, 0
        je .reverse
        xor edx, edx
        div ecx
        add dl, '0'
        push edx
        inc esi
        jmp .divloop
.reverse:
        mov edi, ebx
        mov ecx, esi
.poploop:
        cmp ecx, 0
        je .tsdone
        pop eax
        stosb
        dec ecx
        jmp .poploop
.tsdone:
        mov byte [edi], 0
        popad
        ret

;---------------------------------------
; Utility: hex_word - Write 4-digit hex of AX to EDI
;---------------------------------------
hex_word:
        push eax
        push ecx
        mov ecx, 4
.hw_loop:
        rol ax, 4
        push eax
        and al, 0x0F
        cmp al, 10
        jl .hw_digit
        add al, 'A' - 10
        jmp .hw_store
.hw_digit:
        add al, '0'
.hw_store:
        stosb
        pop eax
        dec ecx
        jnz .hw_loop
        pop ecx
        pop eax
        ret

;---------------------------------------
; Utility: hex_byte_out - Write 2-digit hex of AL to EDI
;---------------------------------------
hex_byte_out:
        push eax
        push ecx
        mov ecx, 2
        rol al, 4
.hb_loop:
        push eax
        and al, 0x0F
        cmp al, 10
        jl .hb_digit
        add al, 'A' - 10
        jmp .hb_store
.hb_digit:
        add al, '0'
.hb_store:
        stosb
        pop eax
        rol al, 4
        dec ecx
        jnz .hb_loop
        pop ecx
        pop eax
        ret

; ======================= DATA =======================
title_str:      db "Terminal", 0
prompt_str:     db "> ", 0
cursor_str:     db "_", 0
root_path:      db "/", 0

; Command strings
cmd_ls:         db "ls", 0
cmd_dir:        db "dir", 0
cmd_cat:        db "cat ", 0
cmd_cd:         db "cd ", 0
cmd_pwd:        db "pwd", 0
cmd_mkdir:      db "mkdir ", 0
cmd_rm:         db "rm ", 0
cmd_del:        db "del ", 0
cmd_touch:      db "touch ", 0
cmd_write:      db "write ", 0
cmd_echo:       db "echo ", 0
cmd_clear:      db "clear", 0
cmd_date:       db "date", 0
cmd_ver:        db "ver", 0
cmd_size:       db "size ", 0
cmd_hex:        db "hex ", 0
cmd_whoami:     db "whoami", 0
cmd_help:       db "help", 0
cmd_exit:       db "exit", 0

; Messages
msg_welcome:    db "Mellivora Terminal v2.0", 0
msg_version:    db "Mellivora OS v2.1", 0
msg_whoami:     db "root", 0
msg_unknown:    db "Unknown command. Type 'help'.", 0
msg_ok:         db "OK", 0
msg_file_err:   db "Error: file not found", 0
msg_empty_file: db "(empty file)", 0
msg_cd_err:     db "Error: directory not found", 0
msg_mkdir_err:  db "Error: cannot create directory", 0
msg_rm_err:     db "Error: cannot delete", 0
msg_cat_usage:  db "Usage: cat <filename>", 0
msg_mkdir_usage: db "Usage: mkdir <name>", 0
msg_rm_usage:   db "Usage: rm <filename>", 0
msg_touch_usage: db "Usage: touch <filename>", 0
msg_write_usage: db "Usage: write <file> <text>", 0
msg_size_usage: db "Usage: size <filename>", 0
msg_hex_usage:  db "Usage: hex <filename>", 0
msg_help1:      db "Commands: ls cat cd pwd mkdir", 0
msg_help2:      db " rm touch write echo clear", 0
msg_help3:      db " date ver size hex whoami", 0
msg_help4:      db " help exit", 0

; Variables
win_id:         dd 0
input_len:      dd 0
num_lines:      dd 0

; Buffers
input_buf:      times 64 db 0
tmp_buf:        times 128 db 0
fmt_buf:        times 128 db 0
output_buf:     times TERM_LINES * 64 db 0
file_buf:       times FILE_BUF_SIZE db 0
