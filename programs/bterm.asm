; bterm.asm - Burrows Terminal Emulator
; A simple shell-like terminal running inside a GUI window.
; Supports commands: echo, ls, cat, date, uptime, clear, exit

%include "syscalls.inc"
%include "lib/gui.inc"

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
        mov dword [cursor_x], 4
        mov dword [cursor_y], 4
        mov dword [input_len], 0
        mov byte [input_buf], 0

        ; Draw initial prompt
        call term_redraw

.main_loop:
        ; Compose + flip
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
        cmp ecx, 60
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
        ; Process command
        call term_scroll_check
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
        cmp ecx, 18            ; max visible lines
        jge .draw_prompt
        push ecx
        push edx
        ; Get line pointer
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
        ; Draw prompt "> "
        mov eax, [win_id]
        mov ebx, 4
        mov ecx, edx
        mov esi, prompt_str
        mov edi, 0x0000FF00     ; bright green
        call gui_draw_text

        ; Draw input text
        mov eax, [win_id]
        mov ebx, 20
        mov ecx, edx
        mov esi, input_buf
        mov edi, 0x00FFFFFF     ; white
        call gui_draw_text

        ; Draw cursor
        mov eax, [input_len]
        shl eax, 3              ; * 8 pixels per char
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
        ; Check if empty
        cmp dword [input_len], 0
        je .done

        ; Add command to output, prefixed with "> "
        call term_add_prompt_line

        ; Compare commands
        mov esi, input_buf
        mov edi, cmd_echo
        call str_starts
        cmp eax, 1
        je .do_echo

        mov esi, input_buf
        mov edi, cmd_clear
        call str_eq
        cmp eax, 1
        je .do_clear

        mov esi, input_buf
        mov edi, cmd_date
        call str_eq
        cmp eax, 1
        je .do_date

        mov esi, input_buf
        mov edi, cmd_exit
        call str_eq
        cmp eax, 1
        je .do_exit

        mov esi, input_buf
        mov edi, cmd_help
        call str_eq
        cmp eax, 1
        je .do_help

        ; Unknown command
        mov esi, msg_unknown
        call term_add_line
        jmp .done

.do_echo:
        lea esi, [input_buf + 5]
        call term_add_line
        jmp .done

.do_clear:
        mov dword [num_lines], 0
        jmp .done

.do_date:
        mov eax, SYS_DATE
        xor ebx, ebx           ; EBX=0: get date string
        mov ecx, date_buf
        int 0x80
        mov esi, date_buf
        call term_add_line
        jmp .done

.do_help:
        mov esi, msg_help1
        call term_add_line
        mov esi, msg_help2
        call term_add_line
        jmp .done

.do_exit:
        popad
        jmp .close_term
        
.done:
        popad
        ret

.close_term:
        mov eax, [win_id]
        call gui_destroy_window
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; term_add_line - Add string to output buffer
; ESI = string
;---------------------------------------
term_add_line:
        pushad
        mov ecx, [num_lines]
        cmp ecx, 18
        jl .add
        ; Scroll: shift all lines up by 1
        cld
        mov edi, output_buf
        mov ecx, 17 * 64 / 4
        push esi
        mov esi, output_buf + 64
        rep movsd
        pop esi
        mov ecx, 17
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
        cmp ecx, 18
        jl .add
        cld
        mov edi, output_buf
        push esi
        mov esi, output_buf + 64
        mov ecx, 17 * 64 / 4
        rep movsd
        pop esi
        mov ecx, 17
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

term_scroll_check:
        ret

term_redraw:
        ret

;---------------------------------------
; str_starts - Check if ESI starts with EDI
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
; str_eq - Check if ESI equals EDI
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
        pop edi
        pop esi
        ret
.no:
        xor eax, eax
        pop edi
        pop esi
        ret

; Data
title_str:      db "Terminal", 0
prompt_str:     db "> ", 0
cursor_str:     db "_", 0
cmd_echo:       db "echo ", 0
cmd_clear:      db "clear", 0
cmd_date:       db "date", 0
cmd_exit:       db "exit", 0
cmd_help:       db "help", 0
msg_unknown:    db "Unknown command. Type help.", 0
msg_help1:      db "Commands: echo, clear, date,", 0
msg_help2:      db "  help, exit", 0

win_id:         dd 0
cursor_x:       dd 0
cursor_y:       dd 0
input_len:      dd 0
num_lines:      dd 0

input_buf:      times 64 db 0
date_buf:       times 32 db 0
output_buf:     times 18 * 64 db 0      ; 18 lines x 64 chars
