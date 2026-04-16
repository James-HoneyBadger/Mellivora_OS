; simon.asm - Simon Says memory game for Mellivora OS
; Watch the sequence, then repeat it. Grows longer each round.
; Colors: R=Red, G=Green, B=Blue, Y=Yellow
;
; Controls: R/G/B/Y keys. Q to quit.

%include "syscalls.inc"

MAX_SEQ         equ 100
FLASH_TIME      equ 30          ; ticks per flash
PAUSE_TIME      equ 15          ; ticks between flashes

start:
        mov eax, SYS_CLEAR
        int 0x80
        call init_game

.round_loop:
        ; Add one to sequence
        call add_to_sequence
        ; Show sequence
        call show_sequence
        ; Player repeats
        call player_input
        cmp byte [failed], 1
        je .game_over

        ; Level up
        call show_correct
        mov eax, SYS_SLEEP
        mov ebx, 40
        int 0x80
        jmp .round_loop

.game_over:
        call draw_board
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_wrong
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_score
        int 0x80
        mov eax, [seq_len]
        dec eax                 ; score = length - 1 (failed on this one)
        cmp eax, 0
        jge .so_ok
        xor eax, eax
.so_ok:
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_restart
        int 0x80
.go_key:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'r'
        je start
        cmp al, 'R'
        je start
        cmp al, 'q'
        je .quit
        cmp al, 'Q'
        je .quit
        jmp .go_key

.quit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
init_game:
        PUSHALL
        mov dword [seq_len], 0
        mov byte [failed], 0
        ; Seed RNG
        mov eax, SYS_GETTIME
        int 0x80
        mov [rng], eax
        POPALL
        ret

;---------------------------------------
add_to_sequence:
        PUSHALL
        ; Generate random 0-3
        mov eax, [rng]
        imul eax, eax, 1103515245
        add eax, 12345
        mov [rng], eax
        shr eax, 16
        and eax, 3              ; 0-3

        mov ecx, [seq_len]
        cmp ecx, MAX_SEQ
        jge .ats_done
        mov [sequence + ecx], al
        inc dword [seq_len]
.ats_done:
        POPALL
        ret

;---------------------------------------
show_sequence:
        PUSHALL
        call draw_board
        mov eax, SYS_SLEEP
        mov ebx, 30
        int 0x80

        xor ecx, ecx
.ss_loop:
        cmp ecx, [seq_len]
        jge .ss_done

        push rcx
        movzx eax, byte [sequence + ecx]
        call flash_color
        pop rcx

        ; Pause between flashes
        push rcx
        mov eax, SYS_SLEEP
        mov ebx, PAUSE_TIME
        int 0x80
        pop rcx

        inc ecx
        jmp .ss_loop
.ss_done:
        call draw_board
        POPALL
        ret

;---------------------------------------
flash_color:
        ; EAX = color index (0-3)
        PUSHALL
        mov [flash_idx], eax
        call draw_board_flash
        ; Beep for color
        mov ecx, [flash_idx]
        mov eax, SYS_BEEP
        mov ebx, [beep_freq + ecx * 4]
        mov ecx, 150            ; duration
        int 0x80

        mov eax, SYS_SLEEP
        mov ebx, FLASH_TIME
        int 0x80
        call draw_board
        POPALL
        ret

;---------------------------------------
player_input:
        PUSHALL
        mov byte [failed], 0
        xor ecx, ecx           ; current position in sequence

.pi_loop:
        cmp ecx, [seq_len]
        jge .pi_done

        ; Wait for valid key
.pi_key:
        push rcx
        mov eax, SYS_GETCHAR
        int 0x80
        pop rcx
        cmp al, 'r'
        je .pi_red
        cmp al, 'R'
        je .pi_red
        cmp al, 'g'
        je .pi_green
        cmp al, 'G'
        je .pi_green
        cmp al, 'b'
        je .pi_blue
        cmp al, 'B'
        je .pi_blue
        cmp al, 'y'
        je .pi_yellow
        cmp al, 'Y'
        je .pi_yellow
        cmp al, 'q'
        je .pi_quit
        cmp al, 'Q'
        je .pi_quit
        jmp .pi_key

.pi_red:
        mov eax, 0
        jmp .pi_check
.pi_green:
        mov eax, 1
        jmp .pi_check
.pi_blue:
        mov eax, 2
        jmp .pi_check
.pi_yellow:
        mov eax, 3

.pi_check:
        ; Flash the pressed color
        push rcx
        push rax
        call flash_color
        pop rax
        pop rcx

        ; Compare with sequence
        movzx edx, byte [sequence + ecx]
        cmp al, dl
        jne .pi_fail

        inc ecx
        jmp .pi_loop

.pi_fail:
        mov byte [failed], 1
.pi_done:
        POPALL
        ret

.pi_quit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
show_correct:
        PUSHALL
        call draw_board
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_correct
        int 0x80
        mov eax, [seq_len]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        POPALL
        ret

;---------------------------------------
draw_board:
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

        ; Round info
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_round
        int 0x80
        mov eax, [seq_len]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Draw color squares (text representations)
        ; R = Red
        mov eax, SYS_SETCOLOR
        mov ebx, 0x04           ; dark red
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, box_r
        int 0x80

        ; G = Green
        mov eax, SYS_SETCOLOR
        mov ebx, 0x02           ; dark green
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, box_g
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; B = Blue
        mov eax, SYS_SETCOLOR
        mov ebx, 0x01           ; dark blue
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, box_b
        int 0x80

        ; Y = Yellow
        mov eax, SYS_SETCOLOR
        mov ebx, 0x06           ; dark yellow
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, box_y
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Controls
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_keys
        int 0x80

        POPALL
        ret

;---------------------------------------
draw_board_flash:
        ; Like draw_board but highlights the color in flash_idx
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
        mov ebx, msg_round
        int 0x80
        mov eax, [seq_len]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Red (bright if flashing)
        mov ebx, 0x04
        cmp dword [flash_idx], 0
        jne .dbf_r
        mov ebx, 0x0C           ; bright red
.dbf_r:
        mov eax, SYS_SETCOLOR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, box_r
        int 0x80

        ; Green
        mov ebx, 0x02
        cmp dword [flash_idx], 1
        jne .dbf_g
        mov ebx, 0x0A           ; bright green
.dbf_g:
        mov eax, SYS_SETCOLOR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, box_g
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Blue
        mov ebx, 0x01
        cmp dword [flash_idx], 2
        jne .dbf_b
        mov ebx, 0x09           ; bright blue
.dbf_b:
        mov eax, SYS_SETCOLOR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, box_b
        int 0x80

        ; Yellow
        mov ebx, 0x06
        cmp dword [flash_idx], 3
        jne .dbf_y
        mov ebx, 0x0E           ; bright yellow
.dbf_y:
        mov eax, SYS_SETCOLOR
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, box_y
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_keys
        int 0x80

        POPALL
        ret

;=======================================
; Data
;=======================================
msg_title:      db "  === SIMON SAYS ===", 10, 10, 0
msg_round:      db "  Round: ", 0
msg_keys:       db "  Press: [R]ed [G]reen [B]lue [Y]ellow  Q=Quit", 10, 0
msg_correct:    db "  Correct! Sequence length: ", 0
msg_wrong:      db 10, "  WRONG! Game Over!", 10, 0
msg_score:      db "  You reached round: ", 0
msg_restart:    db "  R=Restart  Q=Quit", 10, 0

; Color box representations
box_r:  db "  [RRRRRR]  ", 0
box_g:  db "  [GGGGGG]", 10, 0
box_b:  db "  [BBBBBB]  ", 0
box_y:  db "  [YYYYYY]", 10, 0

; Beep frequencies for each color
beep_freq:      dd 262, 330, 392, 523     ; C4, E4, G4, C5

; Game state
sequence:       times MAX_SEQ db 0
seq_len:        dd 0
failed:         db 0
flash_idx:      dd 0
rng:            dd 0
