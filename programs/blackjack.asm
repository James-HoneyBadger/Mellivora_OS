; blackjack.asm - Blackjack (21) card game for Mellivora OS
; Classic casino rules: hit/stand, dealer hits on 16 or below.
; Aces count as 11 or 1 automatically.

%include "syscalls.inc"

MAX_HAND        equ 12          ; max cards in one hand

start:
        call new_game

.play_loop:
        call shuffle_deck
        call deal_initial
        call player_turn
        cmp byte [player_bust], 1
        je .round_over
        call dealer_turn

.round_over:
        call show_result
        call prompt_continue
        cmp al, 'q'
        je .quit
        cmp al, 'Q'
        je .quit
        jmp .play_loop

.quit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
new_game:
        mov dword [p_wins], 0
        mov dword [d_wins], 0
        mov dword [ties], 0
        ret

;---------------------------------------
shuffle_deck:
        PUSHALL
        ; Initialize deck: 0-51 (suit*13 + rank)
        xor ecx, ecx
.sd_init:
        cmp ecx, 52
        jge .sd_shuf
        mov [deck + ecx], cl
        inc ecx
        jmp .sd_init

.sd_shuf:
        ; Fisher-Yates shuffle using tick_count as seed
        mov ecx, 51
.sd_loop:
        cmp ecx, 0
        jle .sd_done
        ; Get pseudo-random j in [0..ecx]
        mov eax, SYS_GETTIME
        int 0x80
        ; Mix the time value
        imul eax, eax, 1103515245
        add eax, 12345
        mov [rng_state], eax
        xor edx, edx
        inc ecx
        div ecx                 ; EDX = EAX mod (ecx+1)
        dec ecx
        ; Swap deck[ecx] and deck[edx]
        movzx eax, byte [deck + ecx]
        movzx ebx, byte [deck + edx]
        mov [deck + ecx], bl
        mov [deck + edx], al
        dec ecx
        jmp .sd_loop

.sd_done:
        mov dword [deck_pos], 0
        POPALL
        ret

;---------------------------------------
draw_card:
        ; Returns card index in AL (0-51)
        mov eax, [deck_pos]
        movzx eax, byte [deck + eax]
        inc dword [deck_pos]
        ret

;---------------------------------------
card_value:
        ; Input: AL = card (0-51), returns value in EAX
        PUSHALL
        movzx eax, al
        xor edx, edx
        mov ecx, 13
        div ecx                 ; EDX = rank (0=A,1=2,...,9=10,10=J,11=Q,12=K)
        cmp edx, 0
        je .cv_ace
        cmp edx, 10
        jge .cv_face
        ; Pip card: value = rank + 1
        inc edx
        mov [rsp + 112], edx    ; return via EAX in PUSHALL frame
        POPALL
        ret
.cv_face:
        mov dword [rsp + 112], 10
        POPALL
        ret
.cv_ace:
        mov dword [rsp + 112], 11
        POPALL
        ret

;---------------------------------------
hand_total:
        ; Input: ESI=hand array, ECX=count. Returns EAX=total (with ace adjustment)
        PUSHALL
        xor edi, edi            ; total
        xor ebp, ebp            ; ace count
        xor ebx, ebx
.ht_loop:
        cmp ebx, ecx
        jge .ht_adj
        push rcx
        movzx eax, byte [esi + ebx]
        call card_value
        add edi, eax
        ; Check if ace
        movzx eax, byte [esi + ebx]
        xor edx, edx
        mov ecx, 13
        div ecx
        cmp edx, 0
        jne .ht_notace
        inc ebp
.ht_notace:
        pop rcx
        inc ebx
        jmp .ht_loop
.ht_adj:
        ; Convert aces from 11 to 1 while total > 21
.ht_adj_loop:
        cmp edi, 21
        jle .ht_done
        cmp ebp, 0
        je .ht_done
        sub edi, 10
        dec ebp
        jmp .ht_adj_loop
.ht_done:
        mov [rsp + 112], edi
        POPALL
        ret

;---------------------------------------
deal_initial:
        PUSHALL
        mov byte [player_bust], 0
        mov dword [p_count], 0
        mov dword [d_count], 0

        ; Player card 1
        call draw_card
        mov [p_hand], al
        mov dword [p_count], 1
        ; Dealer card 1
        call draw_card
        mov [d_hand], al
        mov dword [d_count], 1
        ; Player card 2
        call draw_card
        mov ecx, [p_count]
        mov [p_hand + ecx], al
        inc dword [p_count]
        ; Dealer card 2
        call draw_card
        mov ecx, [d_count]
        mov [d_hand + ecx], al
        inc dword [d_count]

        POPALL
        ret

;---------------------------------------
player_turn:
        PUSHALL
.pt_loop:
        call display_table
        ; Show prompt
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_choice
        int 0x80

        ; Check for blackjack
        mov esi, p_hand
        mov ecx, [p_count]
        call hand_total
        cmp eax, 21
        je .pt_stand            ; Natural 21 or hitting to 21

.pt_key:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'h'
        je .pt_hit
        cmp al, 'H'
        je .pt_hit
        cmp al, 's'
        je .pt_stand
        cmp al, 'S'
        je .pt_stand
        cmp al, 'q'
        je .pt_quit
        cmp al, 'Q'
        je .pt_quit
        jmp .pt_key

.pt_hit:
        call draw_card
        mov ecx, [p_count]
        cmp ecx, MAX_HAND
        jge .pt_stand           ; safety limit
        mov [p_hand + ecx], al
        inc dword [p_count]

        ; Check bust
        mov esi, p_hand
        mov ecx, [p_count]
        call hand_total
        cmp eax, 21
        jg .pt_bust
        jmp .pt_loop

.pt_bust:
        mov byte [player_bust], 1
        POPALL
        ret

.pt_stand:
        POPALL
        ret

.pt_quit:
        ; Exit directly
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
dealer_turn:
        PUSHALL
.dt_loop:
        mov esi, d_hand
        mov ecx, [d_count]
        call hand_total
        cmp eax, 17
        jge .dt_done            ; Dealer stands on 17+

        ; Hit
        call draw_card
        mov ecx, [d_count]
        cmp ecx, MAX_HAND
        jge .dt_done
        mov [d_hand + ecx], al
        inc dword [d_count]
        jmp .dt_loop

.dt_done:
        POPALL
        ret

;---------------------------------------
display_table:
        PUSHALL
        mov eax, SYS_CLEAR
        int 0x80

        ; Title
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A           ; green
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_title
        int 0x80

        ; Score line
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_score
        int 0x80
        mov eax, [p_wins]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_dash
        int 0x80
        mov eax, [d_wins]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_tied
        int 0x80
        mov eax, [ties]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, ')'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Dealer hand (hide second card)
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C           ; red
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_dealer
        int 0x80
        ; Show first card
        movzx eax, byte [d_hand]
        call print_card
        ; Show "??" for hidden card(s)
        mov eax, SYS_PRINT
        mov ebx, msg_hidden
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Player hand
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B           ; cyan
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_player
        int 0x80
        xor ecx, ecx
.dt_ploop:
        cmp ecx, [p_count]
        jge .dt_ptotal
        push rcx
        movzx eax, byte [p_hand + ecx]
        call print_card
        pop rcx
        inc ecx
        jmp .dt_ploop

.dt_ptotal:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_total
        int 0x80
        mov esi, p_hand
        mov ecx, [p_count]
        call hand_total
        push rax
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, ')'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        pop rax

        POPALL
        ret

;---------------------------------------
display_table_final:
        ; Show everything (dealer's hand revealed)
        PUSHALL
        mov eax, SYS_CLEAR
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_title
        int 0x80

        ; Score
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_score
        int 0x80
        mov eax, [p_wins]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_dash
        int 0x80
        mov eax, [d_wins]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_tied
        int 0x80
        mov eax, [ties]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, ')'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Dealer hand revealed
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_dealer
        int 0x80
        xor ecx, ecx
.dtf_dloop:
        cmp ecx, [d_count]
        jge .dtf_dtotal
        push rcx
        movzx eax, byte [d_hand + ecx]
        call print_card
        pop rcx
        inc ecx
        jmp .dtf_dloop
.dtf_dtotal:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_total
        int 0x80
        mov esi, d_hand
        mov ecx, [d_count]
        call hand_total
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, ')'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Player hand
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_player
        int 0x80
        xor ecx, ecx
.dtf_ploop:
        cmp ecx, [p_count]
        jge .dtf_ptotal
        push rcx
        movzx eax, byte [p_hand + ecx]
        call print_card
        pop rcx
        inc ecx
        jmp .dtf_ploop
.dtf_ptotal:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_total
        int 0x80
        mov esi, p_hand
        mov ecx, [p_count]
        call hand_total
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, ')'
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        POPALL
        ret

;---------------------------------------
show_result:
        PUSHALL
        call display_table_final

        mov esi, p_hand
        mov ecx, [p_count]
        call hand_total
        mov [p_total_save], eax

        mov esi, d_hand
        mov ecx, [d_count]
        call hand_total
        mov [d_total_save], eax

        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E           ; yellow
        int 0x80

        cmp byte [player_bust], 1
        je .sr_bust
        mov eax, [d_total_save]
        cmp eax, 21
        jg .sr_dealer_bust
        mov eax, [p_total_save]
        cmp eax, [d_total_save]
        jg .sr_win
        jl .sr_lose
        ; Tie
        inc dword [ties]
        mov eax, SYS_PRINT
        mov ebx, msg_push
        int 0x80
        jmp .sr_done

.sr_bust:
        inc dword [d_wins]
        mov eax, SYS_PRINT
        mov ebx, msg_bust
        int 0x80
        jmp .sr_done

.sr_dealer_bust:
        inc dword [p_wins]
        mov eax, SYS_PRINT
        mov ebx, msg_dbust
        int 0x80
        jmp .sr_done

.sr_win:
        inc dword [p_wins]
        mov eax, SYS_PRINT
        mov ebx, msg_win
        int 0x80
        jmp .sr_done

.sr_lose:
        inc dword [d_wins]
        mov eax, SYS_PRINT
        mov ebx, msg_lose
        int 0x80

.sr_done:
        POPALL
        ret

;---------------------------------------
prompt_continue:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_continue
        int 0x80
.pc_key:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 0
        je .pc_key
        ret                     ; AL = key pressed

;---------------------------------------
print_card:
        ; Input: AL = card index (0-51)
        PUSHALL
        movzx eax, al
        xor edx, edx
        mov ecx, 13
        div ecx                 ; EAX=suit(0-3), EDX=rank(0-12)

        ; Print rank
        push rax
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F           ; white
        int 0x80

        cmp edx, 0
        jne .pc_not_ace
        mov eax, SYS_PUTCHAR
        mov ebx, 'A'
        int 0x80
        jmp .pc_suit
.pc_not_ace:
        cmp edx, 10
        je .pc_jack
        cmp edx, 11
        je .pc_queen
        cmp edx, 12
        je .pc_king
        cmp edx, 9
        je .pc_ten
        ; Number card: rank + 1
        inc edx
        mov eax, SYS_PUTCHAR
        lea ebx, [edx + '0']
        int 0x80
        jmp .pc_suit
.pc_ten:
        mov eax, SYS_PRINT
        mov ebx, str_10
        int 0x80
        jmp .pc_suit
.pc_jack:
        mov eax, SYS_PUTCHAR
        mov ebx, 'J'
        int 0x80
        jmp .pc_suit
.pc_queen:
        mov eax, SYS_PUTCHAR
        mov ebx, 'Q'
        int 0x80
        jmp .pc_suit
.pc_king:
        mov eax, SYS_PUTCHAR
        mov ebx, 'K'
        int 0x80

.pc_suit:
        pop rax
        ; Print suit symbol
        cmp eax, 0
        je .pc_spade
        cmp eax, 1
        je .pc_heart
        cmp eax, 2
        je .pc_diamond
        ; Clubs
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 'C'
        int 0x80
        jmp .pc_space
.pc_spade:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 'S'
        int 0x80
        jmp .pc_space
.pc_heart:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C           ; red
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 'H'
        int 0x80
        jmp .pc_space
.pc_diamond:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 'D'
        int 0x80

.pc_space:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        POPALL
        ret

;=======================================
; Data
;=======================================
msg_title:      db "  === BLACKJACK ===", 10, 10, 0
msg_score:      db "  Record: You ", 0
msg_dash:       db " - Dealer ", 0
msg_tied:       db " (Ties: ", 0
msg_dealer:     db "  Dealer: ", 0
msg_player:     db "  You:    ", 0
msg_hidden:     db "[??] ", 0
msg_total:      db " (Total: ", 0
msg_choice:     db 10, "  [H]it or [S]tand? ", 0
msg_bust:       db "  BUST! You went over 21. Dealer wins.", 10, 0
msg_dbust:      db "  Dealer BUSTS! You win!", 10, 0
msg_win:        db "  You WIN!", 10, 0
msg_lose:       db "  Dealer wins.", 10, 0
msg_push:       db "  PUSH - It's a tie!", 10, 0
msg_continue:   db 10, "  Press any key to play again (Q to quit)...", 0
str_10:         db "10", 0

; Game state
deck:           times 52 db 0
deck_pos:       dd 0
rng_state:      dd 0
p_hand:         times MAX_HAND db 0
d_hand:         times MAX_HAND db 0
p_count:        dd 0
d_count:        dd 0
p_wins:         dd 0
d_wins:         dd 0
ties:           dd 0
player_bust:    db 0
p_total_save:   dd 0
d_total_save:   dd 0
