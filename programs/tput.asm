; =============================================================================
; tput.asm - Terminal control utility
;
; Usage: tput <command> [args...]
;
; Commands:
;   clear       - Clear the screen
;   cols        - Print number of columns (80)
;   lines       - Print number of lines (25)
;   cup R C     - Move cursor to row R, column C (0-based)
;   setaf N     - Set foreground color (0-15)
;   setab N     - Set background color (0-15, shifted to high nibble)
;   sgr0        - Reset colors to default (light gray on black)
;   bold        - Set bright/bold attribute (OR 0x08)
;   rev         - Reverse video (swap fg/bg)
;   bel         - Terminal bell
;   reset       - Clear screen and reset colors
; =============================================================================

%include "syscalls.inc"

start:
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        test eax, eax
        jz .usage

        mov esi, args_buf
        call skip_spaces
        cmp byte [esi], 0
        je .usage

        ; Dispatch on command
        mov edi, cmd_clear
        call str_match
        je .do_clear

        mov edi, cmd_cols
        call str_match
        je .do_cols

        mov edi, cmd_lines
        call str_match
        je .do_lines

        mov edi, cmd_cup
        call str_match
        je .do_cup

        mov edi, cmd_setaf
        call str_match
        je .do_setaf

        mov edi, cmd_setab
        call str_match
        je .do_setab

        mov edi, cmd_sgr0
        call str_match
        je .do_sgr0

        mov edi, cmd_bold
        call str_match
        je .do_bold

        mov edi, cmd_rev
        call str_match
        je .do_rev

        mov edi, cmd_bel
        call str_match
        je .do_bel

        mov edi, cmd_reset
        call str_match
        je .do_reset

        ; Unknown command
        mov eax, SYS_PRINT
        mov ebx, err_unknown
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; ----- Commands -----
.do_clear:
        mov eax, SYS_CLEAR
        int 0x80
        jmp .done

.do_cols:
        mov eax, VGA_WIDTH
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        jmp .done

.do_lines:
        mov eax, VGA_HEIGHT
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        jmp .done

.do_cup:
        ; Parse row and column
        call skip_spaces
        cmp byte [esi], 0
        je .cup_usage
        call parse_num          ; EAX = row
        mov [cup_row], eax
        call skip_spaces
        cmp byte [esi], 0
        je .cup_usage
        call parse_num          ; EAX = col
        ; EBX = row | (col << 8) for SYS_SETCURSOR
        mov ebx, [cup_row]
        and ebx, 0xFF
        shl eax, 8
        or ebx, eax
        mov eax, SYS_SETCURSOR
        int 0x80
        jmp .done
.cup_usage:
        mov eax, SYS_PRINT
        mov ebx, err_cup
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.do_setaf:
        call skip_spaces
        cmp byte [esi], 0
        je .color_usage
        call parse_num
        and eax, 0x0F
        mov [cur_color], al
        ; Read current bg, preserve it
        ; Color byte: bg<<4 | fg
        ; We keep bg and replace fg
        mov ebx, eax
        mov eax, SYS_SETCOLOR
        int 0x80
        jmp .done

.do_setab:
        call skip_spaces
        cmp byte [esi], 0
        je .color_usage
        call parse_num
        and eax, 0x0F
        shl eax, 4             ; Background in high nibble
        or eax, 0x07           ; Default fg
        mov ebx, eax
        mov eax, SYS_SETCOLOR
        int 0x80
        jmp .done

.color_usage:
        mov eax, SYS_PRINT
        mov ebx, err_color
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.do_sgr0:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07           ; Light gray on black
        int 0x80
        jmp .done

.do_bold:
        ; Set bright attribute (color | 0x08)
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F           ; Bright white on black
        int 0x80
        jmp .done

.do_rev:
        ; Reverse video: black on light gray
        mov eax, SYS_SETCOLOR
        mov ebx, 0x70
        int 0x80
        jmp .done

.do_bel:
        mov eax, SYS_BEEP
        mov ebx, 800            ; 800 Hz
        mov ecx, 100            ; 100 ms
        int 0x80
        jmp .done

.do_reset:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_CLEAR
        int 0x80
        jmp .done

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, usage_msg
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; =========================================================================
; Helper: str_match - Check if ESI starts with string at EDI
; Advances ESI past the match + trailing spaces on success.
; Returns: ZF set if match, ZF clear if no match
; =========================================================================
str_match:
        push rsi
        push rdi
.sm_loop:
        mov al, [edi]
        test al, al
        jz .sm_check_end
        cmp al, [esi]
        jne .sm_fail
        inc esi
        inc edi
        jmp .sm_loop
.sm_check_end:
        ; Match if ESI is at space or null
        mov al, [esi]
        cmp al, 0
        je .sm_ok
        cmp al, ' '
        je .sm_ok_skip
.sm_fail:
        pop rdi
        pop rsi
        or eax, 1              ; clear ZF
        ret
.sm_ok_skip:
        inc esi                 ; skip space
.sm_ok:
        ; Update ESI on stack to point past match
        add rsp, 4             ; discard saved EDI
        add rsp, 4             ; discard saved ESI
        xor eax, eax           ; set ZF
        ret

; =========================================================================
; Helper: parse_num - Parse unsigned decimal from ESI
; Output: EAX = value, ESI advanced past digits
; =========================================================================
parse_num:
        xor eax, eax
.pn_loop:
        movzx edx, byte [esi]
        sub dl, '0'
        cmp dl, 9
        ja .pn_done
        imul eax, 10
        add eax, edx
        inc esi
        jmp .pn_loop
.pn_done:
        ret

; =========================================================================
; Helper: skip_spaces
; =========================================================================
skip_spaces:
        cmp byte [esi], ' '
        jne .ss_ret
        inc esi
        jmp skip_spaces
.ss_ret:
        ret

section .data
cmd_clear:   db "clear", 0
cmd_cols:    db "cols", 0
cmd_lines:   db "lines", 0
cmd_cup:     db "cup", 0
cmd_setaf:   db "setaf", 0
cmd_setab:   db "setab", 0
cmd_sgr0:    db "sgr0", 0
cmd_bold:    db "bold", 0
cmd_rev:     db "rev", 0
cmd_bel:     db "bel", 0
cmd_reset:   db "reset", 0

usage_msg:   db "Usage: tput <command> [args...]", 0x0A
             db "Commands: clear cols lines cup setaf setab sgr0 bold rev bel reset", 0x0A, 0
err_unknown: db "tput: unknown command", 0x0A, 0
err_cup:     db "Usage: tput cup <row> <col>", 0x0A, 0
err_color:   db "Usage: tput setaf|setab <color>", 0x0A, 0

section .bss
args_buf:    resb 256
cup_row:     resd 1
cur_color:   resb 1
