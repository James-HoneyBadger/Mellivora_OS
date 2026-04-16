; mastermind.asm - Code-breaking game for Mellivora OS
; Guess the 4-color code in 10 attempts.
; Colors: R G B Y O P (Red Green Blue Yellow Orange Purple)
; Feedback: * = right color+position, + = right color wrong position

%include "syscalls.inc"

CODE_LEN        equ 4
MAX_GUESSES     equ 10
NUM_COLORS      equ 6

start:
.new_game:
        mov eax, SYS_CLEAR
        int 0x80
        call generate_code
        mov dword [guess_num], 0

.guess_loop:
        call draw_state
        cmp dword [guess_num], MAX_GUESSES
        jge .lose

        call get_guess
        cmp byte [quit_flag], 1
        je .quit

        call evaluate_guess
        ; Check if all exact
        cmp dword [exact], CODE_LEN
        je .win

        inc dword [guess_num]
        jmp .guess_loop

.win:
        call draw_state
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_win
        int 0x80
        jmp .ask_again

.lose:
        call draw_state
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_lose
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_code_was
        int 0x80
        ; Show code
        xor ecx, ecx
.show_code:
        cmp ecx, CODE_LEN
        jge .show_code_end
        movzx eax, byte [secret + ecx]
        push rcx
        call print_color_char
        pop rcx
        inc ecx
        jmp .show_code
.show_code_end:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

.ask_again:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_again
        int 0x80
.aa_key:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'y'
        je .new_game
        cmp al, 'Y'
        je .new_game
        cmp al, 'n'
        je .quit
        cmp al, 'N'
        je .quit
        jmp .aa_key

.quit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
generate_code:
        PUSHALL
        mov eax, SYS_GETTIME
        int 0x80
        mov [rng], eax

        xor ecx, ecx
.gc_loop:
        cmp ecx, CODE_LEN
        jge .gc_done
        ; Generate random 0..NUM_COLORS-1
        mov eax, [rng]
        imul eax, eax, 1103515245
        add eax, 12345
        mov [rng], eax
        shr eax, 16
        xor edx, edx
        push rcx
        mov ecx, NUM_COLORS
        div ecx
        pop rcx
        mov [secret + ecx], dl
        inc ecx
        jmp .gc_loop
.gc_done:
        mov byte [quit_flag], 0
        POPALL
        ret

;---------------------------------------
get_guess:
        ; Reads 4 color chars from user
        PUSHALL
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_prompt
        int 0x80

        xor ecx, ecx
.gg_char:
        cmp ecx, CODE_LEN
        jge .gg_done
.gg_key:
        push rcx
        mov eax, SYS_GETCHAR
        int 0x80
        pop rcx
        ; Check quit
        cmp al, 'q'
        je .gg_quit
        cmp al, 'Q'
        je .gg_quit

        ; Convert to uppercase
        cmp al, 'a'
        jl .gg_check
        cmp al, 'z'
        jg .gg_key
        sub al, 32
.gg_check:
        ; Validate color
        push rcx
        push rax
        call color_index
        cmp eax, -1
        pop rax
        pop rcx
        je .gg_key

        ; Store and echo
        push rax
        push rcx
        call color_index
        mov edx, eax
        pop rcx
        pop rax
        mov [cur_guess + ecx], dl
        ; Echo
        push rcx
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rcx
        inc ecx
        jmp .gg_char

.gg_done:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        POPALL
        ret

.gg_quit:
        mov byte [quit_flag], 1
        POPALL
        ret

;---------------------------------------
color_index:
        ; AL = color char (uppercase), returns EAX = 0-5 or -1
        cmp al, 'R'
        je .ci_0
        cmp al, 'G'
        je .ci_1
        cmp al, 'B'
        je .ci_2
        cmp al, 'Y'
        je .ci_3
        cmp al, 'O'
        je .ci_4
        cmp al, 'P'
        je .ci_5
        mov eax, -1
        ret
.ci_0:  mov eax, 0
        ret
.ci_1:  mov eax, 1
        ret
.ci_2:  mov eax, 2
        ret
.ci_3:  mov eax, 3
        ret
.ci_4:  mov eax, 4
        ret
.ci_5:  mov eax, 5
        ret

;---------------------------------------
evaluate_guess:
        ; Compare cur_guess with secret, set exact and misplaced
        PUSHALL
        mov dword [exact], 0
        mov dword [misplaced], 0

        ; Clear used flags
        mov dword [s_used], 0
        mov dword [g_used], 0

        ; Pass 1: exact matches
        xor ecx, ecx
.ev_exact:
        cmp ecx, CODE_LEN
        jge .ev_pass2
        movzx eax, byte [cur_guess + ecx]
        cmp al, [secret + ecx]
        jne .ev_exact_next
        inc dword [exact]
        mov byte [s_used + ecx], 1
        mov byte [g_used + ecx], 1
.ev_exact_next:
        inc ecx
        jmp .ev_exact

.ev_pass2:
        ; Pass 2: misplaced (right color, wrong position)
        xor ecx, ecx           ; guess index
.ev_mis_g:
        cmp ecx, CODE_LEN
        jge .ev_store
        cmp byte [g_used + ecx], 1
        je .ev_mis_gnext
        ; Check against all secret positions
        xor edx, edx
.ev_mis_s:
        cmp edx, CODE_LEN
        jge .ev_mis_gnext
        cmp byte [s_used + edx], 1
        je .ev_mis_snext
        movzx eax, byte [cur_guess + ecx]
        cmp al, [secret + edx]
        jne .ev_mis_snext
        inc dword [misplaced]
        mov byte [s_used + edx], 1
        mov byte [g_used + ecx], 1
        jmp .ev_mis_gnext       ; found match, next guess pos
.ev_mis_snext:
        inc edx
        jmp .ev_mis_s
.ev_mis_gnext:
        inc ecx
        jmp .ev_mis_g

.ev_store:
        ; Save guess history
        mov ecx, [guess_num]
        ; Store guess
        mov eax, ecx
        shl eax, 2              ; * 4 (CODE_LEN)
        mov edx, [cur_guess]    ; 4 bytes at once
        mov [history + eax], edx
        ; Store feedback
        mov al, [exact]
        mov [hist_exact + ecx], al
        mov al, [misplaced]
        mov [hist_mispl + ecx], al

        POPALL
        ret

;---------------------------------------
draw_state:
        PUSHALL
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        xor ecx, ecx
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_title
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_colors
        int 0x80

        ; Show previous guesses
        xor esi, esi
.ds_hist:
        cmp esi, [guess_num]
        jge .ds_done

        ; Guess number
        push rsi
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_indent
        int 0x80
        lea eax, [esi + 1]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_sep
        int 0x80

        ; Show guess colors
        mov eax, esi
        shl eax, 2
        xor ecx, ecx
.ds_gc:
        cmp ecx, CODE_LEN
        jge .ds_fb
        push rcx
        push rax
        movzx eax, byte [history + eax + ecx]
        call print_color_char
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop rax
        pop rcx
        inc ecx
        jmp .ds_gc

.ds_fb:
        ; Show feedback
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_fb
        int 0x80
        movzx ecx, byte [hist_exact + esi]
        ; Print * for each exact
.ds_fb_ex:
        cmp ecx, 0
        jle .ds_fb_mis
        push rcx
        mov eax, SYS_PUTCHAR
        mov ebx, '*'
        int 0x80
        pop rcx
        dec ecx
        jmp .ds_fb_ex

.ds_fb_mis:
        movzx ecx, byte [hist_mispl + esi]
.ds_fb_m:
        cmp ecx, 0
        jle .ds_fb_end
        push rcx
        mov eax, SYS_PUTCHAR
        mov ebx, '+'
        int 0x80
        pop rcx
        dec ecx
        jmp .ds_fb_m

.ds_fb_end:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        pop rsi
        inc esi
        jmp .ds_hist

.ds_done:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        POPALL
        ret

;---------------------------------------
print_color_char:
        ; EAX = color index 0-5, prints colored letter
        PUSHALL
        cmp eax, 0
        je .pcc_r
        cmp eax, 1
        je .pcc_g
        cmp eax, 2
        je .pcc_b
        cmp eax, 3
        je .pcc_y
        cmp eax, 4
        je .pcc_o
        ; Purple
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0D
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 'P'
        int 0x80
        jmp .pcc_done
.pcc_r:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 'R'
        int 0x80
        jmp .pcc_done
.pcc_g:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 'G'
        int 0x80
        jmp .pcc_done
.pcc_b:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x09
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 'B'
        int 0x80
        jmp .pcc_done
.pcc_y:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 'Y'
        int 0x80
        jmp .pcc_done
.pcc_o:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x06
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 'O'
        int 0x80
.pcc_done:
        POPALL
        ret

;=======================================
; Data
;=======================================
msg_title:      db "  === MASTERMIND ===", 10, 10, 0
msg_colors:     db "  Colors: R G B Y O P     * = exact  + = misplaced", 10, 10, 0
msg_prompt:     db "  Enter 4 colors: ", 0
msg_indent:     db "  ", 0
msg_sep:        db ". ", 0
msg_fb:         db "  -> ", 0
msg_win:        db 10, "  You cracked the code!", 10, 0
msg_lose:       db 10, "  Out of guesses!", 10, 0
msg_code_was:   db "  The code was: ", 0
msg_again:      db 10, "  Play again? (Y/N) ", 0

; Game state
secret:         times CODE_LEN db 0
cur_guess:      times CODE_LEN db 0
history:        times MAX_GUESSES * CODE_LEN db 0
hist_exact:     times MAX_GUESSES db 0
hist_mispl:     times MAX_GUESSES db 0
guess_num:      dd 0
exact:          dd 0
misplaced:      dd 0
s_used:         times CODE_LEN db 0
g_used:         times CODE_LEN db 0
quit_flag:      db 0
rng:            dd 0
