; hangman.asm - Classic Hangman word guessing game for Mellivora OS
; Guess the word one letter at a time. 6 wrong guesses = game over.

%include "syscalls.inc"

MAX_WRONG       equ 6
MAX_WORD        equ 20

start:
.main_loop:
        mov eax, SYS_CLEAR
        int 0x80
        call pick_word
        call init_round
.round_loop:
        call draw_screen
        cmp byte [won], 1
        je .win
        cmp dword [wrong], MAX_WRONG
        jge .lose

        call get_guess
        cmp al, 0
        je .quit_game
        call check_guess
        jmp .round_loop

.win:
        call draw_screen
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_win
        int 0x80
        jmp .again

.lose:
        call draw_screen
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_lose
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_answer
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, cur_word
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
.again_key:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'y'
        je .main_loop
        cmp al, 'Y'
        je .main_loop
        cmp al, 'n'
        je .quit_game
        cmp al, 'N'
        je .quit_game
        jmp .again_key

.quit_game:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
pick_word:
        pushad
        ; Use gettime as index into word list
        mov eax, SYS_GETTIME
        int 0x80
        xor edx, edx
        mov ecx, WORD_COUNT
        div ecx                 ; EDX = index
        ; Calculate pointer: word_list + EDX * MAX_WORD
        imul edx, MAX_WORD
        lea esi, [word_list + edx]
        mov edi, cur_word
        ; Copy word
        xor ecx, ecx
.pw_copy:
        mov al, [esi + ecx]
        mov [edi + ecx], al
        cmp al, 0
        je .pw_done
        inc ecx
        cmp ecx, MAX_WORD - 1
        jl .pw_copy
        mov byte [edi + ecx], 0
.pw_done:
        mov [word_len], ecx
        popad
        ret

;---------------------------------------
init_round:
        pushad
        mov dword [wrong], 0
        mov byte [won], 0
        ; Clear guessed letters
        mov edi, guessed
        mov ecx, 26
        xor eax, eax
        rep stosb
        ; Clear reveal buffer
        mov edi, reveal
        mov ecx, MAX_WORD
        rep stosb
        popad
        ret

;---------------------------------------
get_guess:
        ; Returns letter in AL (lowercase), 0 = quit
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_guess
        int 0x80
.gg_key:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 27             ; ESC
        je .gg_quit
        ; Convert to lowercase
        cmp al, 'A'
        jl .gg_key
        cmp al, 'z'
        jg .gg_key
        cmp al, 'Z'
        jg .gg_check_lower
        add al, 32             ; uppercase to lowercase
.gg_check_lower:
        cmp al, 'a'
        jl .gg_key
        cmp al, 'z'
        jg .gg_key
        ; Check if already guessed
        movzx ecx, al
        sub ecx, 'a'
        cmp byte [guessed + ecx], 1
        je .gg_key             ; already guessed, ignore
        ; Mark as guessed
        mov byte [guessed + ecx], 1
        ; Echo letter
        push eax
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        pop eax
        ret
.gg_quit:
        xor eax, eax
        ret

;---------------------------------------
check_guess:
        ; AL = guessed letter (lowercase)
        pushad
        mov dl, al              ; save letter
        xor ecx, ecx
        xor ebx, ebx           ; hit flag
.cg_loop:
        cmp ecx, [word_len]
        jge .cg_done
        mov al, [cur_word + ecx]
        ; Compare lowercase
        cmp al, 'A'
        jl .cg_cmp
        cmp al, 'Z'
        jg .cg_cmp
        add al, 32
.cg_cmp:
        cmp al, dl
        jne .cg_next
        mov byte [reveal + ecx], 1
        mov ebx, 1
.cg_next:
        inc ecx
        jmp .cg_loop

.cg_done:
        cmp ebx, 0
        jne .cg_check_win
        inc dword [wrong]
        jmp .cg_end

.cg_check_win:
        ; Check if all letters revealed
        xor ecx, ecx
        mov byte [won], 1
.cg_wloop:
        cmp ecx, [word_len]
        jge .cg_end
        cmp byte [reveal + ecx], 0
        jne .cg_wnext
        mov byte [won], 0
        jmp .cg_end
.cg_wnext:
        inc ecx
        jmp .cg_wloop

.cg_end:
        popad
        ret

;---------------------------------------
draw_screen:
        pushad
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        xor ecx, ecx
        int 0x80

        ; Title
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_title
        int 0x80

        ; Draw hangman figure
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, [wrong]
        ; Line 1: top bar
        mov eax, SYS_PRINT
        mov ebx, hang_top
        int 0x80

        ; Line 2: head
        cmp dword [wrong], 1
        jl .ds_no_head
        mov eax, SYS_PRINT
        mov ebx, hang_head
        int 0x80
        jmp .ds_body
.ds_no_head:
        mov eax, SYS_PRINT
        mov ebx, hang_empty
        int 0x80

.ds_body:
        ; Line 3: body + arms
        cmp dword [wrong], 4
        jge .ds_both_arms
        cmp dword [wrong], 3
        jge .ds_left_arm
        cmp dword [wrong], 2
        jge .ds_body_only
        mov eax, SYS_PRINT
        mov ebx, hang_empty
        int 0x80
        jmp .ds_legs
.ds_body_only:
        mov eax, SYS_PRINT
        mov ebx, hang_body
        int 0x80
        jmp .ds_legs
.ds_left_arm:
        mov eax, SYS_PRINT
        mov ebx, hang_larm
        int 0x80
        jmp .ds_legs
.ds_both_arms:
        mov eax, SYS_PRINT
        mov ebx, hang_arms
        int 0x80

.ds_legs:
        ; Line 4: legs
        cmp dword [wrong], 6
        jge .ds_both_legs
        cmp dword [wrong], 5
        jge .ds_left_leg
        mov eax, SYS_PRINT
        mov ebx, hang_empty
        int 0x80
        jmp .ds_base
.ds_left_leg:
        mov eax, SYS_PRINT
        mov ebx, hang_lleg
        int 0x80
        jmp .ds_base
.ds_both_legs:
        mov eax, SYS_PRINT
        mov ebx, hang_legs
        int 0x80

.ds_base:
        mov eax, SYS_PRINT
        mov ebx, hang_base
        int 0x80

        ; Show word with blanks
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_word
        int 0x80

        xor ecx, ecx
.ds_word:
        cmp ecx, [word_len]
        jge .ds_wdone
        cmp byte [reveal + ecx], 1
        jne .ds_blank
        ; Show letter
        push ecx
        movzx ebx, byte [cur_word + ecx]
        ; Uppercase for display
        cmp bl, 'a'
        jl .ds_wprint
        cmp bl, 'z'
        jg .ds_wprint
        sub bl, 32
.ds_wprint:
        mov eax, SYS_PUTCHAR
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop ecx
        jmp .ds_wnext
.ds_blank:
        push ecx
        mov eax, SYS_PUTCHAR
        mov ebx, '_'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop ecx
.ds_wnext:
        inc ecx
        jmp .ds_word
.ds_wdone:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Show guessed letters
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_guessed
        int 0x80

        xor ecx, ecx
.ds_gloop:
        cmp ecx, 26
        jge .ds_gdone
        cmp byte [guessed + ecx], 0
        je .ds_gnext
        push ecx
        lea ebx, [ecx + 'A']
        mov eax, SYS_PUTCHAR
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        pop ecx
.ds_gnext:
        inc ecx
        jmp .ds_gloop
.ds_gdone:
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Wrong count
        mov eax, SYS_PRINT
        mov ebx, msg_wrong
        int 0x80
        mov eax, [wrong]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, '/'
        int 0x80
        mov eax, MAX_WRONG
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        popad
        ret

;=======================================
; Data
;=======================================
msg_title:      db "  === HANGMAN ===", 10, 10, 0
msg_word:       db "  Word: ", 0
msg_guessed:    db "  Used: ", 0
msg_wrong:      db "  Wrong: ", 0
msg_guess:      db 10, "  Guess a letter: ", 0
msg_win:        db 10, "  Congratulations! You guessed the word!", 10, 0
msg_lose:       db 10, "  Game Over! You were hanged!", 10, 0
msg_answer:     db "  The word was: ", 0
msg_again:      db 10, "  Play again? (Y/N) ", 0

; Hangman ASCII art pieces
hang_top:       db "    +---+", 10, "    |   |", 10, 0
hang_head:      db "    |   O", 10, 0
hang_body:      db "    |   |", 10, 0
hang_larm:      db "    |  /|", 10, 0
hang_arms:      db "    |  /|\", 10, 0
hang_lleg:      db "    |  / ", 10, 0
hang_legs:      db "    |  / \", 10, 0
hang_empty:     db "    |", 10, 0
hang_base:      db "    +====", 10, 0

; Word list (lowercase, null-terminated, padded to MAX_WORD each)
WORD_COUNT      equ 40
word_list:
        db "computer",0,0,0,0,0,0,0,0,0,0,0,0
        db "keyboard",0,0,0,0,0,0,0,0,0,0,0,0
        db "assembly",0,0,0,0,0,0,0,0,0,0,0,0
        db "mellivora",0,0,0,0,0,0,0,0,0,0,0
        db "processor",0,0,0,0,0,0,0,0,0,0,0
        db "memory",0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db "network",0,0,0,0,0,0,0,0,0,0,0,0,0
        db "desktop",0,0,0,0,0,0,0,0,0,0,0,0,0
        db "program",0,0,0,0,0,0,0,0,0,0,0,0,0
        db "terminal",0,0,0,0,0,0,0,0,0,0,0,0
        db "function",0,0,0,0,0,0,0,0,0,0,0,0
        db "variable",0,0,0,0,0,0,0,0,0,0,0,0
        db "register",0,0,0,0,0,0,0,0,0,0,0,0
        db "hardware",0,0,0,0,0,0,0,0,0,0,0,0
        db "software",0,0,0,0,0,0,0,0,0,0,0,0
        db "monitor",0,0,0,0,0,0,0,0,0,0,0,0,0
        db "printer",0,0,0,0,0,0,0,0,0,0,0,0,0
        db "graphics",0,0,0,0,0,0,0,0,0,0,0,0
        db "internet",0,0,0,0,0,0,0,0,0,0,0,0
        db "protocol",0,0,0,0,0,0,0,0,0,0,0,0
        db "database",0,0,0,0,0,0,0,0,0,0,0,0
        db "compiler",0,0,0,0,0,0,0,0,0,0,0,0
        db "window",0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db "button",0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db "mouse",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db "driver",0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db "kernel",0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db "socket",0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db "buffer",0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db "system",0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db "binary",0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db "server",0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db "cipher",0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db "syntax",0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db "virtual",0,0,0,0,0,0,0,0,0,0,0,0,0
        db "pixel",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db "queue",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db "cache",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db "sector",0,0,0,0,0,0,0,0,0,0,0,0,0,0
        db "thread",0,0,0,0,0,0,0,0,0,0,0,0,0,0

; Game state
cur_word:       times MAX_WORD db 0
word_len:       dd 0
wrong:          dd 0
won:            db 0
guessed:        times 26 db 0
reveal:         times MAX_WORD db 0
