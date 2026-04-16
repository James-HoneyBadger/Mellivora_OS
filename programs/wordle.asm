; wordle.asm - Word guessing game (Wordle-style) for Mellivora OS
; Guess the 5-letter word in 6 attempts.
; Green = correct position, Yellow = wrong position, Gray = not in word

%include "syscalls.inc"

WORD_LEN        equ 5
MAX_TRIES       equ 6
NUM_WORDS       equ 50

start:
.new_game:
        mov eax, SYS_CLEAR
        int 0x80
        call pick_secret
        mov dword [try_num], 0

.try_loop:
        call draw_state
        cmp dword [try_num], MAX_TRIES
        jge .lose

        call get_word
        cmp byte [quit_flag], 1
        je .quit

        call evaluate_word
        ; Check if solved
        cmp dword [greens], WORD_LEN
        je .win

        inc dword [try_num]
        jmp .try_loop

.win:
        call draw_state
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_win
        int 0x80
        mov eax, [try_num]
        inc eax
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_tries
        int 0x80
        jmp .again

.lose:
        call draw_state
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_lose
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, secret
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

.again:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_again
        int 0x80
.ag_key:
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
        jmp .ag_key

.quit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
pick_secret:
        PUSHALL
        mov eax, SYS_GETTIME
        int 0x80
        xor edx, edx
        mov ecx, NUM_WORDS
        div ecx
        ; EDX = index
        imul edx, 8            ; each word padded to 8 bytes
        lea esi, [word_list + edx]
        mov edi, secret
        mov ecx, WORD_LEN
.ps_copy:
        mov al, [esi]
        mov [edi], al
        inc esi
        inc edi
        dec ecx
        jnz .ps_copy
        mov byte [edi], 0
        mov byte [quit_flag], 0
        ; Clear guess history
        mov edi, guesses
        mov ecx, MAX_TRIES * 8
        xor eax, eax
        rep stosb
        mov edi, fb_buf
        mov ecx, MAX_TRIES * WORD_LEN
        rep stosb
        POPALL
        ret

;---------------------------------------
get_word:
        PUSHALL
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_prompt
        int 0x80

        xor ecx, ecx           ; char count
.gw_key:
        push rcx
        mov eax, SYS_GETCHAR
        int 0x80
        pop rcx

        cmp al, 'q'
        je .gw_quit_check
        cmp al, 'Q'
        je .gw_quit_check

        ; Backspace
        cmp al, 8
        je .gw_back
        cmp al, 127
        je .gw_back

        ; Enter
        cmp al, 13
        je .gw_enter
        cmp al, 10
        je .gw_enter

        ; Only letters
        cmp al, 'A'
        jl .gw_key
        cmp al, 'z'
        jg .gw_key
        cmp al, 'Z'
        jle .gw_upper
        cmp al, 'a'
        jl .gw_key
        ; Lowercase — convert to upper
        sub al, 32
.gw_upper:
        cmp ecx, WORD_LEN
        jge .gw_key             ; already full
        ; Convert to lowercase for storage
        add al, 32
        mov [input_buf + ecx], al
        ; Echo uppercase
        push rcx
        sub al, 32
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rcx
        inc ecx
        jmp .gw_key

.gw_back:
        cmp ecx, 0
        jle .gw_key
        dec ecx
        push rcx
        mov eax, SYS_PUTCHAR
        mov ebx, 8
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 8
        int 0x80
        pop rcx
        jmp .gw_key

.gw_enter:
        cmp ecx, WORD_LEN
        jne .gw_key             ; must be exactly 5 chars
        mov byte [input_buf + ecx], 0
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        POPALL
        ret

.gw_quit_check:
        cmp ecx, 0
        jne .gw_key             ; only quit if no chars typed
        mov byte [quit_flag], 1
        POPALL
        ret

;---------------------------------------
evaluate_word:
        PUSHALL
        mov dword [greens], 0

        ; Copy to history
        mov ecx, [try_num]
        shl ecx, 3             ; * 8
        lea edi, [guesses + ecx]
        mov esi, input_buf
        push rcx
        mov ecx, WORD_LEN
        rep movsb
        pop rcx

        ; Track used positions
        mov dword [s_used], 0
        mov dword [s_used + 4], 0

        ; Feedback index
        mov edx, [try_num]
        imul edx, WORD_LEN
        ; fb_buf[edx..edx+4] = feedback (0=gray, 1=yellow, 2=green)

        ; Pass 1: green (exact matches)
        xor ecx, ecx
.ew_green:
        cmp ecx, WORD_LEN
        jge .ew_pass2
        mov al, [input_buf + ecx]
        cmp al, [secret + ecx]
        jne .ew_gnext
        mov byte [fb_buf + edx + ecx], 2
        mov byte [s_used + ecx], 1
        inc dword [greens]
.ew_gnext:
        inc ecx
        jmp .ew_green

.ew_pass2:
        ; Pass 2: yellow (right letter wrong position)
        xor ecx, ecx
.ew_yellow:
        cmp ecx, WORD_LEN
        jge .ew_done
        cmp byte [fb_buf + edx + ecx], 2
        je .ew_ynext            ; skip greens

        mov al, [input_buf + ecx]
        ; Search secret for this letter in unused positions
        xor ebx, ebx
.ew_ysearch:
        cmp ebx, WORD_LEN
        jge .ew_ynext
        cmp byte [s_used + ebx], 1
        je .ew_ysnext
        cmp al, [secret + ebx]
        jne .ew_ysnext
        ; Found!
        mov byte [fb_buf + edx + ecx], 1
        mov byte [s_used + ebx], 1
        jmp .ew_ynext
.ew_ysnext:
        inc ebx
        jmp .ew_ysearch
.ew_ynext:
        inc ecx
        jmp .ew_yellow

.ew_done:
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

        ; Show previous guesses with color feedback
        xor esi, esi
.ds_row:
        cmp esi, [try_num]
        jge .ds_blanks

        mov eax, SYS_PRINT
        mov ebx, msg_indent
        int 0x80

        ; Get feedback offset
        mov edx, esi
        imul edx, WORD_LEN

        xor ecx, ecx
.ds_char:
        cmp ecx, WORD_LEN
        jge .ds_eol

        ; Color based on feedback
        movzx eax, byte [fb_buf + edx + ecx]
        cmp eax, 2
        je .ds_green
        cmp eax, 1
        je .ds_yellow
        ; Gray
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        jmp .ds_letter
.ds_green:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A
        int 0x80
        jmp .ds_letter
.ds_yellow:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80

.ds_letter:
        ; Print uppercase letter
        mov eax, esi
        shl eax, 3
        movzx ebx, byte [guesses + eax + ecx]
        sub ebx, 32            ; uppercase
        mov eax, SYS_PUTCHAR
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

        inc ecx
        jmp .ds_char

.ds_eol:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        inc esi
        jmp .ds_row

.ds_blanks:
        ; Show remaining blank rows
        mov ecx, [try_num]
.ds_brow:
        cmp ecx, MAX_TRIES
        jge .ds_end
        push rcx
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_blank_row
        int 0x80
        pop rcx
        inc ecx
        jmp .ds_brow

.ds_end:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_legend
        int 0x80
        POPALL
        ret

;=======================================
; Data
;=======================================
msg_title:      db "  === WORDLE ===", 10, 10, 0
msg_prompt:     db "  Guess (5 letters): ", 0
msg_indent:     db "  ", 0
msg_blank_row:  db "  _ _ _ _ _", 10, 0
msg_legend:     db "  Green=correct  Yellow=wrong spot  Gray=not in word", 10, 0
msg_win:        db 10, "  Excellent! You got it in ", 0
msg_tries:      db " tries!", 10, 0
msg_lose:       db 10, "  Out of guesses! The word was: ", 0
msg_again:      db 10, "  Play again? (Y/N) ", 0

; Word list: 50 common 5-letter words, padded to 8 bytes each
word_list:
        db "apple", 0, 0, 0
        db "brain", 0, 0, 0
        db "chair", 0, 0, 0
        db "dance", 0, 0, 0
        db "early", 0, 0, 0
        db "flame", 0, 0, 0
        db "grape", 0, 0, 0
        db "horse", 0, 0, 0
        db "index", 0, 0, 0
        db "jewel", 0, 0, 0
        db "knife", 0, 0, 0
        db "lemon", 0, 0, 0
        db "music", 0, 0, 0
        db "noble", 0, 0, 0
        db "ocean", 0, 0, 0
        db "piano", 0, 0, 0
        db "queen", 0, 0, 0
        db "river", 0, 0, 0
        db "stone", 0, 0, 0
        db "tiger", 0, 0, 0
        db "ultra", 0, 0, 0
        db "voice", 0, 0, 0
        db "water", 0, 0, 0
        db "youth", 0, 0, 0
        db "zebra", 0, 0, 0
        db "angel", 0, 0, 0
        db "beach", 0, 0, 0
        db "cloud", 0, 0, 0
        db "dream", 0, 0, 0
        db "eagle", 0, 0, 0
        db "frost", 0, 0, 0
        db "ghost", 0, 0, 0
        db "heart", 0, 0, 0
        db "ivory", 0, 0, 0
        db "judge", 0, 0, 0
        db "knack", 0, 0, 0
        db "light", 0, 0, 0
        db "magic", 0, 0, 0
        db "night", 0, 0, 0
        db "olive", 0, 0, 0
        db "pearl", 0, 0, 0
        db "quiet", 0, 0, 0
        db "royal", 0, 0, 0
        db "shine", 0, 0, 0
        db "trail", 0, 0, 0
        db "unity", 0, 0, 0
        db "vigor", 0, 0, 0
        db "wheat", 0, 0, 0
        db "chess", 0, 0, 0
        db "pixel", 0, 0, 0

; Game state
secret:         times 8 db 0
input_buf:      times 8 db 0
guesses:        times MAX_TRIES * 8 db 0
fb_buf:         times MAX_TRIES * WORD_LEN db 0
s_used:         times 8 db 0
try_num:        dd 0
greens:         dd 0
quit_flag:      db 0
