; freecell.asm - FreeCell Solitaire for Mellivora OS (Burrows GUI)
; Classic FreeCell with 4 free cells, 4 foundation piles, 8 cascades.
; Click to select a card, then click destination to move.

%include "syscalls.inc"
%include "lib/gui.inc"

; Window
WIN_W           equ 520
WIN_H           equ 420

; Card dimensions
CARD_W          equ 45
CARD_H          equ 60
CARD_OVERLAP    equ 16          ; vertical overlap in cascades

; Layout
FREE_X          equ 10
FREE_Y          equ 10
FOUND_X         equ 280
FOUND_Y         equ 10
CASCADE_X       equ 10
CASCADE_Y       equ 85
CASCADE_GAP     equ 63          ; horizontal gap between cascades

; Card encoding: byte = (suit << 6) | rank
; rank: 1=A, 2-10, 11=J, 12=Q, 13=K
; suit: 0=Spades(black), 1=Hearts(red), 2=Diamonds(red), 3=Clubs(black)
SUIT_SPADES     equ 0
SUIT_HEARTS     equ 1
SUIT_DIAMONDS   equ 2
SUIT_CLUBS      equ 3
CARD_NONE       equ 0xFF

; Colors
COL_BG          equ 0x00006600
COL_CARD_BG     equ 0x00FFFFFF
COL_CARD_BORDER equ 0x00888888
COL_RED         equ 0x00CC0000
COL_BLACK       equ 0x00000000
COL_SLOT_BG     equ 0x00004400
COL_SELECT      equ 0x0000CCFF
COL_HUD         equ 0x00FFFFFF
COL_WIN         equ 0x00FFD700

; Max cascade depth
MAX_CASCADE     equ 20

start:
        ; Create window
        mov eax, 30
        mov ebx, 20
        mov ecx, WIN_W
        mov edx, WIN_H
        mov esi, win_title
        call gui_create_window
        mov [win_id], eax

        call init_game

.main_loop:
        call draw_all

        mov eax, [win_id]
        call gui_poll_event

        cmp eax, EVT_CLOSE
        je .quit
        cmp eax, EVT_KEY_PRESS
        je .handle_key
        cmp eax, EVT_MOUSE_CLICK
        je .handle_click
        jmp .main_loop

.handle_key:
        cmp ebx, 27
        je .quit
        cmp ebx, 'r'
        je .restart
        cmp ebx, 'R'
        je .restart
        jmp .main_loop

.restart:
        call init_game
        jmp .main_loop

.handle_click:
        ; EBX=x, ECX=y
        call process_click
        ; Check for win
        call check_win
        jmp .main_loop

.quit:
        mov eax, [win_id]
        call gui_destroy_window
        mov eax, SYS_EXIT
        int 0x80


; ─── init_game ───────────────────────────────────────────────
init_game:
        pushad
        ; Clear all
        mov edi, free_cells
        mov ecx, 4
        mov eax, CARD_NONE
        rep stosb

        mov edi, foundations
        mov ecx, 4
        xor eax, eax
        rep stosb                ; 0 = empty (no cards)

        mov edi, cascades
        mov ecx, 8 * MAX_CASCADE
        mov al, CARD_NONE
        rep stosb

        mov edi, cascade_len
        mov ecx, 8
        xor eax, eax
        rep stosb

        mov byte [selected], 0
        mov byte [sel_type], 0
        mov byte [sel_index], 0
        mov byte [game_won], 0
        mov dword [moves], 0

        ; Build deck (52 cards)
        mov edi, deck
        xor esi, esi             ; card index
        xor ecx, ecx             ; suit
.build_suit:
        cmp ecx, 4
        jge .deck_done
        mov edx, 1               ; rank
.build_rank:
        cmp edx, 14
        jge .next_suit
        mov al, cl
        shl al, 6
        or al, dl
        mov [edi + esi], al
        inc esi
        inc edx
        jmp .build_rank
.next_suit:
        inc ecx
        jmp .build_suit
.deck_done:

        ; Fisher-Yates shuffle
        mov eax, SYS_GETTIME
        int 0x80
        mov [rng_state], eax

        mov ecx, 51
.shuffle:
        ; Random index 0..ecx
        push ecx
        call random
        pop ecx
        xor edx, edx
        push ecx
        inc ecx
        div ecx
        pop ecx
        ; edx = random index 0..ecx
        ; Swap deck[ecx] and deck[edx]
        movzx eax, byte [deck + ecx]
        movzx ebx, byte [deck + edx]
        mov [deck + ecx], bl
        mov [deck + edx], al
        dec ecx
        jnz .shuffle

        ; Deal cards to 8 cascades
        ; First 4 cascades get 7 cards, last 4 get 6
        xor esi, esi             ; deck position
        xor ecx, ecx             ; cascade index
.deal_cascade:
        cmp ecx, 8
        jge .deal_done
        mov edx, 7
        cmp ecx, 4
        jl .deal_count_ok
        mov edx, 6
.deal_count_ok:
        mov [cascade_len + ecx], dl
        xor ebx, ebx            ; position in cascade
.deal_card:
        cmp ebx, edx
        jge .deal_next
        mov eax, ecx
        imul eax, MAX_CASCADE
        push edx
        mov dl, [deck + esi]
        mov [cascades + eax + ebx], dl
        pop edx
        inc esi
        inc ebx
        jmp .deal_card
.deal_next:
        inc ecx
        jmp .deal_cascade
.deal_done:

        popad
        ret


; ─── random ──────────────────────────────────────────────────
; Simple LCG PRNG. Returns EAX = random value
random:
        mov eax, [rng_state]
        imul eax, 1103515245
        add eax, 12345
        mov [rng_state], eax
        shr eax, 16
        ret


; ─── process_click ───────────────────────────────────────────
; EBX=x, ECX=y
process_click:
        pushad
        mov [click_x], ebx
        mov [click_y], ecx

        ; Determine what was clicked
        ; 1) Free cells (top-left, 4 slots)
        cmp ecx, FREE_Y
        jb .pc_done
        cmp ecx, FREE_Y + CARD_H
        jg .check_cascades

        ; Check free cells
        cmp ebx, FREE_X
        jb .pc_done
        mov eax, ebx
        sub eax, FREE_X
        xor edx, edx
        push ecx
        mov ecx, (CARD_W + 8)
        div ecx
        pop ecx
        cmp eax, 4
        jge .check_foundations
        ; Clicked free cell [eax]
        cmp byte [selected], 0
        je .select_free
        ; Try to place in free cell
        call place_in_free
        jmp .pc_done

.select_free:
        cmp byte [free_cells + eax], CARD_NONE
        je .pc_done
        mov byte [selected], 1
        mov byte [sel_type], 0   ; 0=free cell
        mov [sel_index], al
        jmp .pc_done

.check_foundations:
        cmp ebx, FOUND_X
        jb .pc_done
        mov eax, ebx
        sub eax, FOUND_X
        xor edx, edx
        push ecx
        mov ecx, (CARD_W + 8)
        div ecx
        pop ecx
        cmp eax, 4
        jge .pc_done
        ; Clicked foundation [eax]
        cmp byte [selected], 0
        je .pc_done              ; can't select from foundation
        call place_in_foundation
        jmp .pc_done

.check_cascades:
        cmp ecx, CASCADE_Y
        jb .pc_done
        cmp ebx, CASCADE_X
        jb .pc_done
        mov eax, ebx
        sub eax, CASCADE_X
        xor edx, edx
        push ecx
        mov ecx, CASCADE_GAP
        div ecx
        pop ecx
        cmp eax, 8
        jge .pc_done
        ; Clicked cascade [eax]
        cmp byte [selected], 0
        je .select_cascade
        ; Try to place on cascade
        call place_on_cascade
        jmp .pc_done

.select_cascade:
        ; Select top card of cascade
        movzx ebx, byte [cascade_len + eax]
        test ebx, ebx
        jz .pc_done
        mov byte [selected], 1
        mov byte [sel_type], 1   ; 1=cascade
        mov [sel_index], al
        jmp .pc_done

.pc_done:
        popad
        ret


; ─── place_in_free ───────────────────────────────────────────
; EAX = free cell index to place into
place_in_free:
        pushad
        cmp byte [free_cells + eax], CARD_NONE
        jne .pif_cancel          ; slot not empty

        ; Get selected card
        call get_selected_card   ; -> BL = card
        cmp bl, CARD_NONE
        je .pif_cancel

        ; Place it
        mov [free_cells + eax], bl
        call remove_selected_card
        mov byte [selected], 0
        inc dword [moves]
        popad
        ret
.pif_cancel:
        mov byte [selected], 0
        popad
        ret


; ─── place_in_foundation ─────────────────────────────────────
; EAX = foundation index
place_in_foundation:
        pushad
        mov [.pifn_idx], al

        call get_selected_card   ; -> BL = card
        cmp bl, CARD_NONE
        je .pifn_cancel

        ; Get card's suit and rank
        mov cl, bl
        and cl, 0x3F             ; rank
        mov ch, bl
        shr ch, 6                ; suit

        ; Foundation must match suit
        movzx eax, byte [.pifn_idx]
        movzx edx, byte [foundations + eax]
        ; If foundation empty, need an Ace (rank=1)
        test edx, edx
        jnz .pifn_check_rank
        cmp cl, 1
        jne .pifn_cancel
        ; Ace goes on empty foundation of matching suit
        ; Foundation index should match suit
        cmp ch, al
        jne .pifn_cancel
        mov byte [foundations + eax], 1
        jmp .pifn_ok

.pifn_check_rank:
        ; Must be same suit and rank = foundation_rank + 1
        cmp ch, al               ; suit must match foundation index
        jne .pifn_cancel
        mov dl, [foundations + eax]
        inc dl
        cmp cl, dl
        jne .pifn_cancel
        mov [foundations + eax], cl
.pifn_ok:
        call remove_selected_card
        mov byte [selected], 0
        inc dword [moves]
        popad
        ret
.pifn_cancel:
        mov byte [selected], 0
        popad
        ret
.pifn_idx: db 0


; ─── place_on_cascade ────────────────────────────────────────
; EAX = cascade index to place onto
place_on_cascade:
        pushad
        mov [.poc_idx], al

        call get_selected_card   ; -> BL = card
        cmp bl, CARD_NONE
        je .poc_cancel

        movzx eax, byte [.poc_idx]
        movzx ecx, byte [cascade_len + eax]

        ; If cascade empty, any card can go
        test ecx, ecx
        jz .poc_place

        ; Check: must be opposite color and rank - 1
        ; Get top card of target cascade
        mov edx, eax
        imul edx, MAX_CASCADE
        movzx edi, byte [cascades + edx + ecx - 1]
        mov [.poc_top_card], edi

        ; Compare colors
        mov al, bl               ; selected card
        call card_is_red         ; -> CF set if red
        jc .poc_sel_red
        ; Selected is black, top must be red
        mov al, [.poc_top_card]
        call card_is_red
        jnc .poc_cancel          ; both black
        jmp .poc_rank_check
.poc_sel_red:
        ; Selected is red, top must be black
        mov al, [.poc_top_card]
        call card_is_red
        jc .poc_cancel           ; both red

.poc_rank_check:
        ; Selected rank must be top_rank - 1
        mov al, bl
        and al, 0x3F             ; sel rank
        mov ah, [.poc_top_card]
        and ah, 0x3F             ; top rank
        inc al
        cmp al, ah
        jne .poc_cancel

.poc_place:
        movzx eax, byte [.poc_idx]
        movzx ecx, byte [cascade_len + eax]
        cmp ecx, MAX_CASCADE - 1
        jge .poc_cancel
        mov edx, eax
        imul edx, MAX_CASCADE
        mov [cascades + edx + ecx], bl
        inc byte [cascade_len + eax]
        call remove_selected_card
        mov byte [selected], 0
        inc dword [moves]
        popad
        ret
.poc_cancel:
        mov byte [selected], 0
        popad
        ret
.poc_idx: db 0
.poc_top_card: db 0


; ─── get_selected_card ───────────────────────────────────────
; Returns BL = selected card (CARD_NONE if invalid)
get_selected_card:
        cmp byte [sel_type], 0
        je .gsc_free
        ; From cascade
        movzx eax, byte [sel_index]
        movzx ecx, byte [cascade_len + eax]
        test ecx, ecx
        jz .gsc_none
        imul eax, MAX_CASCADE
        movzx ebx, byte [cascades + eax + ecx - 1]
        ret
.gsc_free:
        movzx eax, byte [sel_index]
        movzx ebx, byte [free_cells + eax]
        ret
.gsc_none:
        mov bl, CARD_NONE
        ret


; ─── remove_selected_card ────────────────────────────────────
remove_selected_card:
        cmp byte [sel_type], 0
        je .rsc_free
        ; From cascade - remove top
        movzx eax, byte [sel_index]
        dec byte [cascade_len + eax]
        movzx ecx, byte [cascade_len + eax]
        imul eax, MAX_CASCADE
        mov byte [cascades + eax + ecx], CARD_NONE
        ret
.rsc_free:
        movzx eax, byte [sel_index]
        mov byte [free_cells + eax], CARD_NONE
        ret


; ─── card_is_red ─────────────────────────────────────────────
; AL = card. Sets CF if red (Hearts or Diamonds)
card_is_red:
        push eax
        shr al, 6
        cmp al, SUIT_HEARTS
        je .red
        cmp al, SUIT_DIAMONDS
        je .red
        clc
        pop eax
        ret
.red:
        stc
        pop eax
        ret


; ─── check_win ───────────────────────────────────────────────
check_win:
        ; Win if all 4 foundations have rank 13 (King)
        cmp byte [foundations], 13
        jne .no_win
        cmp byte [foundations + 1], 13
        jne .no_win
        cmp byte [foundations + 2], 13
        jne .no_win
        cmp byte [foundations + 3], 13
        jne .no_win
        mov byte [game_won], 1
.no_win:
        ret


; ─── draw_all ────────────────────────────────────────────────
draw_all:
        pushad

        ; Green background
        mov eax, [win_id]
        xor ebx, ebx
        xor ecx, ecx
        mov edx, WIN_W
        mov esi, WIN_H
        mov edi, COL_BG
        call gui_fill_rect

        ; Draw free cells
        xor ecx, ecx
.draw_free:
        cmp ecx, 4
        jge .draw_foundations
        push ecx
        mov ebx, ecx
        imul ebx, (CARD_W + 8)
        add ebx, FREE_X
        mov ecx, FREE_Y
        movzx eax, byte [free_cells]
        ; Get actual card for this cell
        pop edx
        push edx
        movzx eax, byte [free_cells + edx]
        ; Draw slot background
        push eax
        push ebx
        push ecx
        mov eax, [win_id]
        mov edx, CARD_W
        mov esi, CARD_H
        mov edi, COL_SLOT_BG
        call gui_fill_rect
        pop ecx
        pop ebx
        pop eax
        ; Draw card if present
        cmp al, CARD_NONE
        je .free_next
        call draw_card
.free_next:
        pop ecx
        inc ecx
        jmp .draw_free

.draw_foundations:
        xor ecx, ecx
.draw_found:
        cmp ecx, 4
        jge .draw_cascades
        push ecx
        mov ebx, ecx
        imul ebx, (CARD_W + 8)
        add ebx, FOUND_X
        mov ecx, FOUND_Y
        ; Draw slot
        push ebx
        push ecx
        mov eax, [win_id]
        mov edx, CARD_W
        mov esi, CARD_H
        mov edi, COL_SLOT_BG
        call gui_fill_rect
        pop ecx
        pop ebx
        ; Draw top card if foundation has cards
        pop edx
        push edx
        movzx eax, byte [foundations + edx]
        test al, al
        jz .found_next
        ; Build card: suit=edx, rank=eax
        push edx
        mov ah, dl
        shl ah, 6
        or al, ah
        pop edx
        call draw_card
.found_next:
        pop ecx
        inc ecx
        jmp .draw_found

.draw_cascades:
        xor ecx, ecx            ; cascade index
.draw_casc:
        cmp ecx, 8
        jge .draw_hud
        push ecx

        movzx esi, byte [cascade_len + ecx]
        test esi, esi
        jz .casc_empty

        ; Draw each card in cascade
        xor edx, edx            ; card index in cascade
.draw_casc_card:
        cmp edx, esi
        jge .casc_done

        push edx
        push esi
        mov eax, ecx
        imul eax, MAX_CASCADE
        movzx eax, byte [cascades + eax + edx]
        mov ebx, ecx
        imul ebx, CASCADE_GAP
        add ebx, CASCADE_X
        push ecx
        mov ecx, edx
        imul ecx, CARD_OVERLAP
        add ecx, CASCADE_Y
        ; Highlight if selected and this is top card
        pop edi                  ; cascade index (was ecx)
        push edi
        pop ecx
        pop esi
        pop edx

        push edx
        push esi
        push ecx
        ; Recalc position
        mov ebx, ecx
        imul ebx, CASCADE_GAP
        add ebx, CASCADE_X
        mov ecx, edx
        imul ecx, CARD_OVERLAP
        add ecx, CASCADE_Y
        call draw_card
        pop ecx
        pop esi
        pop edx

        inc edx
        jmp .draw_casc_card

.casc_empty:
        ; Draw empty slot
        push ecx
        mov ebx, ecx
        imul ebx, CASCADE_GAP
        add ebx, CASCADE_X
        mov ecx, CASCADE_Y
        mov eax, [win_id]
        mov edx, CARD_W
        mov esi, CARD_H
        mov edi, COL_SLOT_BG
        call gui_fill_rect
        pop ecx

.casc_done:
        pop ecx
        inc ecx
        jmp .draw_casc

.draw_hud:
        ; Moves counter
        mov eax, [win_id]
        mov ebx, 10
        mov ecx, WIN_H - 18
        mov esi, str_moves
        mov edi, COL_HUD
        call gui_draw_text

        mov eax, [moves]
        call itoa
        mov eax, [win_id]
        mov ebx, 70
        mov ecx, WIN_H - 18
        mov esi, num_buf
        mov edi, COL_HUD
        call gui_draw_text

        ; Controls
        mov eax, [win_id]
        mov ebx, 200
        mov ecx, WIN_H - 18
        mov esi, str_controls
        mov edi, 0x00888888
        call gui_draw_text

        ; Win message
        cmp byte [game_won], 1
        jne .no_win_msg
        mov eax, [win_id]
        mov ebx, 150
        mov ecx, 200
        mov esi, str_win
        mov edi, COL_WIN
        call gui_draw_text
.no_win_msg:

        ; Compose and flip
        mov eax, [win_id]
        call gui_compose
        mov eax, [win_id]
        call gui_flip

        popad
        ret


; ─── draw_card ───────────────────────────────────────────────
; AL = card byte, EBX = x, ECX = y
draw_card:
        pushad
        mov [.dc_card], al
        mov [.dc_x], ebx
        mov [.dc_y], ecx

        ; Card background
        mov eax, [win_id]
        mov edx, CARD_W
        mov esi, CARD_H
        mov edi, COL_CARD_BG
        call gui_fill_rect

        ; Get rank and suit strings
        movzx eax, byte [.dc_card]
        mov cl, al
        and cl, 0x3F             ; rank 1-13
        shr al, 6               ; suit 0-3

        ; Rank char
        movzx edx, cl
        dec edx
        cmp edx, 13
        jge .dc_done
        movzx edx, byte [rank_chars + edx]
        mov [.dc_rank_ch], dl
        mov byte [.dc_rank_ch + 1], 0

        ; Suit char
        movzx edx, al
        movzx edx, byte [suit_chars + edx]
        mov [.dc_suit_ch], dl
        mov byte [.dc_suit_ch + 1], 0

        ; Color
        mov al, [.dc_card]
        call card_is_red
        jc .dc_red_color
        mov dword [.dc_color], COL_BLACK
        jmp .dc_draw_text
.dc_red_color:
        mov dword [.dc_color], COL_RED
.dc_draw_text:
        ; Draw rank
        mov eax, [win_id]
        mov ebx, [.dc_x]
        add ebx, 3
        mov ecx, [.dc_y]
        add ecx, 3
        mov esi, .dc_rank_ch
        mov edi, [.dc_color]
        call gui_draw_text

        ; Draw suit
        mov eax, [win_id]
        mov ebx, [.dc_x]
        add ebx, 3
        mov ecx, [.dc_y]
        add ecx, 14
        mov esi, .dc_suit_ch
        mov edi, [.dc_color]
        call gui_draw_text

.dc_done:
        popad
        ret

.dc_card:     db 0
.dc_x:        dd 0
.dc_y:        dd 0
.dc_color:    dd 0
.dc_rank_ch:  db 0, 0
.dc_suit_ch:  db 0, 0


; ─── itoa ────────────────────────────────────────────────────
itoa:
        pushad
        mov edi, num_buf + 11
        mov byte [edi], 0
        mov ebx, 10
.itoa_lp:
        dec edi
        xor edx, edx
        div ebx
        add dl, '0'
        mov [edi], dl
        test eax, eax
        jnz .itoa_lp
        mov esi, edi
        mov edi, num_buf
.itoa_cp:
        lodsb
        stosb
        test al, al
        jnz .itoa_cp
        popad
        ret


; ═════════════════════════════════════════════════════════════
; DATA
; ═════════════════════════════════════════════════════════════

win_title:      db "FreeCell", 0
str_moves:      db "Moves:", 0
str_controls:   db "Click=move  R=restart  Esc=quit", 0
str_win:        db "!!! YOU WIN !!!", 0
num_buf:        times 12 db 0

rank_chars:     db 'A23456789TJQK'
suit_chars:     db 'SHDC'          ; Spades, Hearts, Diamonds, Clubs


; ═════════════════════════════════════════════════════════════
; BSS
; ═════════════════════════════════════════════════════════════

section .bss

win_id:         resd 1
moves:          resd 1
rng_state:      resd 1
click_x:        resd 1
click_y:        resd 1

game_won:       resb 1
selected:       resb 1          ; 0=none, 1=selected
sel_type:       resb 1          ; 0=free cell, 1=cascade
sel_index:      resb 1          ; index of selected source

free_cells:     resb 4          ; 4 free cell slots
foundations:    resb 4          ; 4 foundation piles (rank of top card)
cascades:       resb 8 * MAX_CASCADE  ; 8 cascades
cascade_len:    resb 8          ; length of each cascade
deck:           resb 52         ; temp deck for shuffling
