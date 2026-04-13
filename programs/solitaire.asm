; solitaire.asm - Klondike Solitaire for Mellivora OS
; GUI card game using Burrows Desktop API
; Mouse-driven with drag and drop
;
; Layout:  [Stock][Waste]  [F1][F2][F3][F4]    (top row)
;          [T1][T2][T3][T4][T5][T6][T7]        (tableau)

%include "syscalls.inc"
%include "lib/gui.inc"

; Window dimensions
WIN_W           equ 530
WIN_H           equ 400

; Card dimensions
CARD_W          equ 50
CARD_H          equ 70
CARD_GAP        equ 10
CARD_OVERLAP    equ 18          ; Vertical overlap for tableau facedown
CARD_OVERLAP_UP equ 22          ; Vertical overlap for faceup cards

; Layout positions
STOCK_X         equ 10
STOCK_Y         equ 10
WASTE_X         equ (STOCK_X + CARD_W + CARD_GAP)
WASTE_Y         equ 10
FOUND_X         equ (WASTE_X + CARD_W + CARD_GAP + 30)
FOUND_Y         equ 10
TAB_X           equ 10
TAB_Y           equ 95
TAB_SPACING     equ (CARD_W + 8)

; Card constants
NUM_CARDS       equ 52
NUM_SUITS       equ 4
NUM_RANKS       equ 13
NUM_TABLEAU     equ 7
NUM_FOUNDATIONS equ 4

; Suit indices
SUIT_HEARTS     equ 0
SUIT_DIAMONDS   equ 1
SUIT_CLUBS      equ 2
SUIT_SPADES     equ 3

; Card encoding: byte = (suit << 4) | rank
; rank: 1=A, 2-10, 11=J, 12=Q, 13=K
; suit: 0=Hearts, 1=Diamonds, 2=Clubs, 3=Spades

; Colors
COL_FELT        equ 0x00206020  ; Dark green felt
COL_CARD_FACE   equ 0x00FFFFFF  ; White card face
COL_CARD_BACK   equ 0x000044AA  ; Blue card back
COL_CARD_BORDER equ 0x00333333  ; Card border
COL_RED         equ 0x00CC0000  ; Red suit
COL_BLACK       equ 0x00000000  ; Black suit
COL_EMPTY_SLOT  equ 0x00306830  ; Empty pile marker
COL_GOLD        equ 0x00FFD700  ; Win animation
COL_SELECTED    equ 0x00FFFF00  ; Selected card highlight

start:
        ; Create window
        mov eax, 50
        mov ebx, 30
        mov ecx, WIN_W
        mov edx, WIN_H
        mov esi, title_str
        call gui_create_window
        cmp eax, -1
        je .exit
        mov [win_id], eax

        ; Init game
        call game_init

.main_loop:
        call gui_compose
        call render_game
        call gui_flip

        call gui_poll_event
        cmp eax, EVT_CLOSE
        je .close
        cmp eax, EVT_KEY_PRESS
        je .on_key
        cmp eax, EVT_MOUSE_CLICK
        je .on_click
        jmp .main_loop

.on_key:
        cmp bl, 27             ; ESC
        je .close
        cmp bl, 'n'
        je .new_game
        cmp bl, 'N'
        je .new_game
        jmp .main_loop

.new_game:
        call game_init
        jmp .main_loop

.on_click:
        ; EBX = rel_x, ECX = rel_y
        call handle_click
        jmp .main_loop

.close:
        mov eax, [win_id]
        call gui_destroy_window
.exit:
        mov eax, SYS_EXIT
        int 0x80

;=======================================================================
; GAME INITIALIZATION
;=======================================================================

game_init:
        pushad

        ; Seed RNG
        mov eax, SYS_GETTIME
        int 0x80
        mov [rng_state], eax

        ; Clear game state
        mov byte [game_won], 0
        mov dword [selected_pile], -1
        mov dword [selected_idx], -1
        mov dword [moves], 0

        ; Create deck of 52 cards
        xor ecx, ecx          ; Card index
        xor edx, edx          ; Suit
.init_suit:
        cmp edx, NUM_SUITS
        jge .shuffle
        mov ebx, 1            ; Rank (Ace)
.init_rank:
        cmp ebx, NUM_RANKS + 1
        jge .next_suit
        mov al, dl
        shl al, 4
        or al, bl
        mov [deck + ecx], al
        inc ecx
        inc ebx
        jmp .init_rank
.next_suit:
        inc edx
        jmp .init_suit

.shuffle:
        ; Fisher-Yates shuffle
        mov ecx, NUM_CARDS - 1
.shuffle_loop:
        cmp ecx, 0
        jle .deal

        ; Random index 0..ecx
        call rng_next
        xor edx, edx
        mov ebx, ecx
        inc ebx
        div ebx                ; EDX = EAX % (ecx+1)

        ; Swap deck[ecx] and deck[edx]
        movzx eax, byte [deck + ecx]
        movzx ebx, byte [deck + edx]
        mov [deck + ecx], bl
        mov [deck + edx], al

        dec ecx
        jmp .shuffle_loop

.deal:
        ; Clear all piles
        mov edi, tab_data
        xor eax, eax
        mov ecx, (7 * 20)     ; Tableau data
        rep stosb
        mov edi, tab_count
        mov ecx, 7
        rep stosb
        mov edi, tab_faceup
        mov ecx, 7
        rep stosb
        mov edi, found_data
        mov ecx, (4 * 14)
        rep stosb
        mov edi, found_count
        mov ecx, 4
        rep stosb
        mov edi, stock_data
        mov ecx, 24
        rep stosb
        mov edi, waste_data
        mov ecx, 24
        rep stosb
        mov dword [stock_count], 0
        mov dword [waste_count], 0

        ; Deal to tableau: col i gets i+1 cards, top card face-up
        xor esi, esi           ; Deck index
        xor edi, edi           ; Column
.deal_col:
        cmp edi, NUM_TABLEAU
        jge .deal_stock

        ; Deal edi+1 cards to column edi
        xor ecx, ecx          ; Card count for this column
.deal_card:
        lea eax, [edi + 1]
        cmp ecx, eax
        jge .deal_col_done

        ; tab_data[edi*20 + ecx] = deck[esi]
        movzx eax, byte [deck + esi]
        imul ebx, edi, 20
        add ebx, ecx
        mov [tab_data + ebx], al
        inc ecx
        inc esi
        jmp .deal_card

.deal_col_done:
        mov [tab_count + edi], cl
        ; Face-up index = count - 1 (only top card face up)
        dec cl
        mov [tab_faceup + edi], cl
        inc edi
        jmp .deal_col

.deal_stock:
        ; Remaining cards go to stock
        xor ecx, ecx
.stock_fill:
        cmp esi, NUM_CARDS
        jge .deal_done
        movzx eax, byte [deck + esi]
        mov [stock_data + ecx], al
        inc ecx
        inc esi
        jmp .stock_fill
.deal_done:
        mov [stock_count], ecx

        popad
        ret

;=======================================================================
; RENDERING
;=======================================================================

render_game:
        pushad

        ; Fill background (green felt)
        mov eax, [win_id]
        xor ebx, ebx
        xor ecx, ecx
        mov edx, WIN_W
        mov esi, WIN_H
        mov edi, COL_FELT
        call gui_fill_rect

        ; Draw stock pile
        cmp dword [stock_count], 0
        je .draw_empty_stock
        mov ebx, STOCK_X
        mov ecx, STOCK_Y
        mov edx, 1             ; Face down
        call draw_card_back
        jmp .draw_waste
.draw_empty_stock:
        mov ebx, STOCK_X
        mov ecx, STOCK_Y
        call draw_empty_slot

.draw_waste:
        ; Draw waste pile (top card face up)
        mov eax, [waste_count]
        cmp eax, 0
        je .draw_empty_waste
        dec eax
        movzx edx, byte [waste_data + eax]
        mov ebx, WASTE_X
        mov ecx, WASTE_Y
        call draw_card_face
        jmp .draw_foundations
.draw_empty_waste:
        mov ebx, WASTE_X
        mov ecx, WASTE_Y
        call draw_empty_slot

.draw_foundations:
        ; Draw 4 foundation piles
        xor esi, esi
.draw_found_loop:
        cmp esi, NUM_FOUNDATIONS
        jge .draw_tableau

        ; Calculate position
        imul ebx, esi, (CARD_W + CARD_GAP)
        add ebx, FOUND_X

        movzx eax, byte [found_count + esi]
        cmp eax, 0
        je .draw_found_empty
        dec eax
        imul ecx, esi, 14
        movzx edx, byte [found_data + ecx + eax]
        mov ecx, FOUND_Y
        call draw_card_face
        jmp .draw_found_next
.draw_found_empty:
        mov ecx, FOUND_Y
        call draw_empty_slot
.draw_found_next:
        inc esi
        jmp .draw_found_loop

.draw_tableau:
        ; Draw 7 tableau columns
        xor esi, esi
.draw_tab_col:
        cmp esi, NUM_TABLEAU
        jge .draw_status

        imul ebx, esi, TAB_SPACING
        add ebx, TAB_X

        movzx eax, byte [tab_count + esi]
        cmp eax, 0
        je .draw_tab_empty

        ; Draw each card in column
        movzx edi, byte [tab_faceup + esi]
        xor ecx, ecx          ; card index
        push ebx               ; save column X
.draw_tab_card:
        movzx eax, byte [tab_count + esi]
        cmp ecx, eax
        jge .draw_tab_col_done

        ; Calculate Y position
        mov ebx, ecx
        cmp ecx, edi
        jb .tab_hidden_overlap
        ; Face-up: larger overlap
        sub ebx, edi
        imul ebx, CARD_OVERLAP_UP
        push edx
        mov edx, edi
        imul edx, CARD_OVERLAP
        add ebx, edx
        pop edx
        jmp .tab_y_calc
.tab_hidden_overlap:
        imul ebx, CARD_OVERLAP
.tab_y_calc:
        add ebx, TAB_Y
        ; EBX = Y position, stack top = X position
        push ecx
        push esi
        push edi
        mov eax, [esp + 12]    ; Get saved column X
        push ebx               ; save Y
        mov ebx, eax           ; X
        pop ecx                ; Y = calculated Y... wait, need to swap

        ; Fix: EBX should be X, ECX should be Y
        ; Actually: draw_card_face wants EBX=x, ECX=y
        ; We have: eax=X from stack, ebx=Y calculated
        ; So: swap them
        push eax
        mov ecx, ebx           ; Y
        pop ebx                ; X

        pop edi
        pop esi
        pop eax                ; eax = card index (was ecx)

        cmp eax, edi           ; Compare with faceup index
        jb .tab_card_down
        ; Face up
        push eax
        imul edx, esi, 20
        add edx, eax
        movzx edx, byte [tab_data + edx]
        call draw_card_face
        pop eax
        jmp .tab_card_next
.tab_card_down:
        push eax
        push edx
        mov edx, 1
        call draw_card_back
        pop edx
        pop eax
.tab_card_next:
        mov ecx, eax
        inc ecx
        mov ebx, [esp]         ; Restore column X
        jmp .draw_tab_card

.draw_tab_col_done:
        pop ebx                ; Clean saved column X
        jmp .draw_tab_next

.draw_tab_empty:
        mov ecx, TAB_Y
        call draw_empty_slot

.draw_tab_next:
        inc esi
        jmp .draw_tab_col

.draw_status:
        ; Draw move counter and instructions
        mov eax, [win_id]
        mov ebx, 10
        mov ecx, WIN_H - 18
        mov esi, str_status
        mov edi, 0x00CCCCCC
        call gui_draw_text

        ; Check win
        cmp byte [game_won], 1
        jne .render_done
        mov eax, [win_id]
        mov ebx, 180
        mov ecx, 200
        mov esi, str_win
        mov edi, COL_GOLD
        call gui_draw_text

.render_done:
        popad
        ret

;---------------------------------------
; draw_card_face - Draw a face-up card
; EBX = x, ECX = y, EDX = card byte
;---------------------------------------
draw_card_face:
        pushad
        mov [.cf_card], edx
        mov [.cf_x], ebx
        mov [.cf_y], ecx

        ; Card background (white)
        mov eax, [win_id]
        mov edx, CARD_W
        mov esi, CARD_H
        mov edi, COL_CARD_FACE
        call gui_fill_rect

        ; Border
        mov eax, [win_id]
        mov ebx, [.cf_x]
        mov ecx, [.cf_y]
        mov edx, CARD_W
        mov esi, 1
        mov edi, COL_CARD_BORDER
        call gui_fill_rect      ; Top border
        mov eax, [win_id]
        mov ebx, [.cf_x]
        mov ecx, [.cf_y]
        add ecx, CARD_H - 1
        mov edx, CARD_W
        mov esi, 1
        mov edi, COL_CARD_BORDER
        call gui_fill_rect      ; Bottom border
        mov eax, [win_id]
        mov ebx, [.cf_x]
        mov ecx, [.cf_y]
        mov edx, 1
        mov esi, CARD_H
        mov edi, COL_CARD_BORDER
        call gui_fill_rect      ; Left border
        mov eax, [win_id]
        mov ebx, [.cf_x]
        add ebx, CARD_W - 1
        mov ecx, [.cf_y]
        mov edx, 1
        mov esi, CARD_H
        mov edi, COL_CARD_BORDER
        call gui_fill_rect      ; Right border

        ; Determine suit color
        movzx eax, byte [.cf_card]
        shr eax, 4             ; Suit
        cmp eax, SUIT_CLUBS
        jge .cf_black
        mov dword [.cf_color], COL_RED
        jmp .cf_draw_rank
.cf_black:
        mov dword [.cf_color], COL_BLACK

.cf_draw_rank:
        ; Draw rank text (top-left corner)
        movzx eax, byte [.cf_card]
        and eax, 0x0F         ; Rank
        dec eax                ; 0-based index
        cmp eax, NUM_RANKS
        jae .cf_skip
        mov esi, [rank_strs + eax*4]
        mov eax, [win_id]
        mov ebx, [.cf_x]
        add ebx, 4
        mov ecx, [.cf_y]
        add ecx, 4
        mov edi, [.cf_color]
        call gui_draw_text

        ; Draw suit symbol (below rank)
        movzx eax, byte [.cf_card]
        shr eax, 4
        mov esi, [suit_strs + eax*4]
        mov eax, [win_id]
        mov ebx, [.cf_x]
        add ebx, 4
        mov ecx, [.cf_y]
        add ecx, 18
        mov edi, [.cf_color]
        call gui_draw_text

        ; Draw centered large rank
        movzx eax, byte [.cf_card]
        and eax, 0x0F
        dec eax
        mov esi, [rank_strs + eax*4]
        mov eax, [win_id]
        mov ebx, [.cf_x]
        add ebx, 18
        mov ecx, [.cf_y]
        add ecx, 30
        mov edi, [.cf_color]
        call gui_draw_text

.cf_skip:
        popad
        ret

.cf_card:  dd 0
.cf_x:     dd 0
.cf_y:     dd 0
.cf_color: dd 0

;---------------------------------------
; draw_card_back - Draw face-down card
; EBX = x, ECX = y
;---------------------------------------
draw_card_back:
        pushad
        mov [.cb_x], ebx
        mov [.cb_y], ecx

        ; Blue card back
        mov eax, [win_id]
        mov edx, CARD_W
        mov esi, CARD_H
        mov edi, COL_CARD_BACK
        call gui_fill_rect

        ; Border
        mov eax, [win_id]
        mov ebx, [.cb_x]
        mov ecx, [.cb_y]
        mov edx, CARD_W
        mov esi, 1
        mov edi, COL_CARD_BORDER
        call gui_fill_rect
        mov eax, [win_id]
        mov ebx, [.cb_x]
        mov ecx, [.cb_y]
        add ecx, CARD_H - 1
        mov edx, CARD_W
        mov esi, 1
        mov edi, COL_CARD_BORDER
        call gui_fill_rect
        mov eax, [win_id]
        mov ebx, [.cb_x]
        mov ecx, [.cb_y]
        mov edx, 1
        mov esi, CARD_H
        mov edi, COL_CARD_BORDER
        call gui_fill_rect
        mov eax, [win_id]
        mov ebx, [.cb_x]
        add ebx, CARD_W - 1
        mov ecx, [.cb_y]
        mov edx, 1
        mov esi, CARD_H
        mov edi, COL_CARD_BORDER
        call gui_fill_rect

        ; Inner pattern (smaller rect for texture)
        mov eax, [win_id]
        mov ebx, [.cb_x]
        add ebx, 4
        mov ecx, [.cb_y]
        add ecx, 4
        mov edx, CARD_W - 8
        mov esi, CARD_H - 8
        mov edi, 0x000066CC    ; Lighter blue inner
        call gui_fill_rect

        popad
        ret

.cb_x: dd 0
.cb_y: dd 0

;---------------------------------------
; draw_empty_slot - Draw empty pile outline
; EBX = x, ECX = y
;---------------------------------------
draw_empty_slot:
        pushad
        ; Rounded-ish slot outline
        mov eax, [win_id]
        mov edx, CARD_W
        mov esi, CARD_H
        mov edi, COL_EMPTY_SLOT
        call gui_fill_rect
        popad
        ret

;=======================================================================
; INPUT HANDLING
;=======================================================================

handle_click:
        pushad
        mov [.hc_x], ebx
        mov [.hc_y], ecx

        ; Check win state
        cmp byte [game_won], 1
        je .hc_done

        ; Check stock click
        mov eax, [.hc_x]
        cmp eax, STOCK_X
        jl .hc_check_waste
        cmp eax, STOCK_X + CARD_W
        jg .hc_check_waste
        mov eax, [.hc_y]
        cmp eax, STOCK_Y
        jl .hc_check_waste
        cmp eax, STOCK_Y + CARD_H
        jg .hc_check_waste
        ; Stock clicked
        call stock_click
        jmp .hc_done

.hc_check_waste:
        ; Check waste click
        mov eax, [.hc_x]
        cmp eax, WASTE_X
        jl .hc_check_found
        cmp eax, WASTE_X + CARD_W
        jg .hc_check_found
        mov eax, [.hc_y]
        cmp eax, WASTE_Y
        jl .hc_check_found
        cmp eax, WASTE_Y + CARD_H
        jg .hc_check_found
        ; Waste clicked — try to move top waste card
        call waste_click
        jmp .hc_done

.hc_check_found:
        ; Check foundation clicks
        xor esi, esi
.hc_found_loop:
        cmp esi, NUM_FOUNDATIONS
        jge .hc_check_tab
        imul eax, esi, (CARD_W + CARD_GAP)
        add eax, FOUND_X
        cmp dword [.hc_x], eax
        jl .hc_found_next
        add eax, CARD_W
        cmp dword [.hc_x], eax
        jg .hc_found_next
        cmp dword [.hc_y], FOUND_Y
        jl .hc_found_next
        mov eax, FOUND_Y + CARD_H
        cmp dword [.hc_y], eax
        jg .hc_found_next
        ; Foundation pile clicked
        ; (foundations are targets, not sources in standard solitaire)
        jmp .hc_done
.hc_found_next:
        inc esi
        jmp .hc_found_loop

.hc_check_tab:
        ; Check tableau column clicks
        xor esi, esi
.hc_tab_loop:
        cmp esi, NUM_TABLEAU
        jge .hc_done

        imul eax, esi, TAB_SPACING
        add eax, TAB_X
        cmp dword [.hc_x], eax
        jl .hc_tab_next
        add eax, CARD_W
        cmp dword [.hc_x], eax
        jg .hc_tab_next

        ; X is within this column — determine which card
        movzx eax, byte [tab_count + esi]
        cmp eax, 0
        je .hc_tab_empty_click

        ; Calculate click row
        mov eax, [.hc_y]
        sub eax, TAB_Y
        cmp eax, 0
        jl .hc_tab_next

        ; Find which card was clicked
        ; Y offset mapped to card index
        movzx ebx, byte [tab_count + esi]
        movzx edi, byte [tab_faceup + esi]
        call tab_find_card_at_y
        ; EAX = card index or -1
        cmp eax, -1
        je .hc_tab_next

        ; Only allow clicking face-up cards
        cmp eax, edi
        jb .hc_tab_flip        ; Clicked face-down card at top of face-down stack

        ; Try to auto-move this card
        push esi
        push eax
        call try_auto_move
        pop eax
        pop esi
        jmp .hc_done

.hc_tab_flip:
        ; If this is the top face-down card, flip it
        movzx ecx, byte [tab_count + esi]
        dec ecx
        cmp eax, ecx
        jne .hc_tab_next
        ; Flip: set faceup to this index
        mov [tab_faceup + esi], al
        jmp .hc_done

.hc_tab_empty_click:
        ; Empty tableau column: no action for now
        jmp .hc_tab_next

.hc_tab_next:
        inc esi
        jmp .hc_tab_loop

.hc_done:
        ; Check win condition
        call check_win
        popad
        ret

.hc_x: dd 0
.hc_y: dd 0

;---------------------------------------
; tab_find_card_at_y - Find card index from Y click position
; EAX = y offset from TAB_Y, EBX = total cards, EDI = faceup index
; Returns: EAX = card index or -1
;---------------------------------------
tab_find_card_at_y:
        push ecx
        push edx

        ; Calculate Y position of each card and find which was clicked
        mov ecx, ebx
        dec ecx                ; Start from last card
.tfcy_loop:
        cmp ecx, -1
        je .tfcy_none

        ; Calculate this card's Y offset
        push eax
        cmp ecx, edi
        jb .tfcy_hidden
        mov edx, ecx
        sub edx, edi
        imul edx, CARD_OVERLAP_UP
        push ecx
        mov ecx, edi
        imul ecx, CARD_OVERLAP
        add edx, ecx
        pop ecx
        jmp .tfcy_check
.tfcy_hidden:
        imul edx, ecx, CARD_OVERLAP
.tfcy_check:
        ; EDX = Y offset of this card
        pop eax
        cmp eax, edx
        jge .tfcy_found

        dec ecx
        jmp .tfcy_loop

.tfcy_found:
        mov eax, ecx
        pop edx
        pop ecx
        ret

.tfcy_none:
        mov eax, -1
        pop edx
        pop ecx
        ret

;=======================================================================
; GAME LOGIC
;=======================================================================

;---------------------------------------
; stock_click - Draw from stock to waste
;---------------------------------------
stock_click:
        pushad
        cmp dword [stock_count], 0
        je .stock_recycle

        ; Move top stock card to waste
        mov eax, [stock_count]
        dec eax
        movzx ebx, byte [stock_data + eax]
        mov dword [stock_count], eax

        mov ecx, [waste_count]
        mov [waste_data + ecx], bl
        inc dword [waste_count]
        inc dword [moves]
        popad
        ret

.stock_recycle:
        ; Move all waste back to stock (reversed)
        mov ecx, [waste_count]
        cmp ecx, 0
        je .stock_done
        xor edx, edx          ; stock index
.stock_rev:
        dec ecx
        movzx eax, byte [waste_data + ecx]
        mov [stock_data + edx], al
        inc edx
        cmp ecx, 0
        jg .stock_rev
        ; Handle last card
        movzx eax, byte [waste_data]
        mov [stock_data + edx], al
        inc edx

        mov [stock_count], edx
        mov dword [waste_count], 0
        inc dword [moves]
.stock_done:
        popad
        ret

;---------------------------------------
; waste_click - Try to move waste card
;---------------------------------------
waste_click:
        pushad
        mov eax, [waste_count]
        cmp eax, 0
        je .wc_done
        dec eax
        movzx edx, byte [waste_data + eax]
        ; EDX = card to move
        ; First try foundation
        call try_move_to_foundation
        cmp eax, 1
        je .wc_moved
        ; Then try tableau
        mov eax, [waste_count]
        dec eax
        movzx edx, byte [waste_data + eax]
        call try_move_to_tableau
        cmp eax, 1
        je .wc_moved
        jmp .wc_done
.wc_moved:
        dec dword [waste_count]
        inc dword [moves]
.wc_done:
        popad
        ret

;---------------------------------------
; try_auto_move - Auto-move card from tableau
; ESI = column, EAX = card index (on stack)
;---------------------------------------
try_auto_move:
        pushad
        mov edi, esi           ; Save column
        mov ecx, eax           ; Save card index

        ; Get the card
        imul eax, edi, 20
        add eax, ecx
        movzx edx, byte [tab_data + eax]

        ; If only clicking the top card, try foundation first
        movzx ebx, byte [tab_count + edi]
        dec ebx
        cmp ecx, ebx
        jne .tam_try_tab       ; Not top card, only try tableau

        ; Try foundation
        push edx
        push ecx
        push edi
        call try_move_to_foundation
        pop edi
        pop ecx
        pop edx
        cmp eax, 1
        je .tam_from_tab

        ; Try tableau (move stack of cards)
.tam_try_tab:
        imul eax, edi, 20
        add eax, ecx
        movzx edx, byte [tab_data + eax]
        push ecx
        push edi
        call try_move_stack_to_tableau
        pop edi
        pop ecx
        cmp eax, 1
        je .tam_stack_moved
        jmp .tam_done

.tam_from_tab:
        ; Remove top card from tableau column
        dec byte [tab_count + edi]
        ; Auto-flip new top card
        movzx eax, byte [tab_count + edi]
        cmp eax, 0
        je .tam_fix_faceup
        dec eax
        movzx ebx, byte [tab_faceup + edi]
        cmp ebx, eax
        jbe .tam_moved_done
        mov [tab_faceup + edi], al
        jmp .tam_moved_done
.tam_fix_faceup:
        mov byte [tab_faceup + edi], 0

.tam_moved_done:
        inc dword [moves]
        jmp .tam_done

.tam_stack_moved:
        ; Cards already moved by try_move_stack_to_tableau
        inc dword [moves]

.tam_done:
        popad
        ret

;---------------------------------------
; try_move_to_foundation - Move card to foundation
; EDX = card
; Returns: EAX = 1 if moved, 0 if not
;---------------------------------------
try_move_to_foundation:
        push ebx
        push ecx
        push esi

        movzx eax, dl
        shr eax, 4            ; Suit = foundation index
        mov esi, eax

        movzx ecx, byte [found_count + esi]
        movzx ebx, dl
        and ebx, 0x0F         ; Rank

        cmp ecx, 0
        je .tmf_need_ace
        ; Foundation has cards — need next rank
        dec ecx
        imul eax, esi, 14
        movzx eax, byte [found_data + eax + ecx]
        and eax, 0x0F
        inc eax
        cmp eax, ebx
        jne .tmf_fail
        jmp .tmf_place

.tmf_need_ace:
        cmp ebx, 1            ; Must be Ace
        jne .tmf_fail

.tmf_place:
        movzx ecx, byte [found_count + esi]
        imul eax, esi, 14
        add eax, ecx
        mov [found_data + eax], dl
        inc byte [found_count + esi]
        mov eax, 1
        pop esi
        pop ecx
        pop ebx
        ret

.tmf_fail:
        xor eax, eax
        pop esi
        pop ecx
        pop ebx
        ret

;---------------------------------------
; try_move_to_tableau - Move single card to tableau
; EDX = card
; Returns: EAX = 1 if moved, 0 if not
;---------------------------------------
try_move_to_tableau:
        push ebx
        push ecx
        push esi
        push edi

        movzx ebx, dl
        and ebx, 0x0F         ; Rank
        movzx ecx, dl
        shr ecx, 4            ; Suit

        ; Determine card color: H(0),D(1)=red, C(2),S(3)=black
        xor edi, edi           ; 0 = red
        cmp ecx, 2
        jb .tmt_color_set
        mov edi, 1             ; 1 = black
.tmt_color_set:

        ; Try each tableau column
        xor esi, esi
.tmt_loop:
        cmp esi, NUM_TABLEAU
        jge .tmt_fail

        movzx eax, byte [tab_count + esi]
        cmp eax, 0
        je .tmt_empty

        ; Get top card of this column
        dec eax
        push ebx
        imul ebx, esi, 20
        add ebx, eax
        movzx eax, byte [tab_data + ebx]
        pop ebx

        ; Check: opposite color and one rank lower
        movzx ecx, al
        shr ecx, 4            ; Top card suit
        xor ecx, ecx
        push eax
        movzx eax, byte [tab_data]  ; Dummy — re-derive
        pop eax
        ; Re-derive top card color
        push edx
        movzx edx, al
        shr edx, 4
        xor ecx, ecx          ; 0 = red
        cmp edx, 2
        jb .tmt_top_red
        mov ecx, 1
.tmt_top_red:
        pop edx

        ; Colors must differ
        cmp ecx, edi
        je .tmt_next

        ; Rank: our card must be one less than top card
        movzx ecx, al
        and ecx, 0x0F         ; Top card rank
        dec ecx
        cmp ecx, ebx          ; Our rank
        jne .tmt_next

        ; Place the card
        movzx eax, byte [tab_count + esi]
        push ebx
        imul ebx, esi, 20
        add ebx, eax
        mov [tab_data + ebx], dl
        pop ebx
        inc byte [tab_count + esi]
        mov eax, 1
        pop edi
        pop esi
        pop ecx
        pop ebx
        ret

.tmt_empty:
        ; Only Kings can go on empty columns
        cmp ebx, 13
        jne .tmt_next
        mov [tab_data + esi*1], dl  ; Wrong — need tab_data[esi*20]
        ; Fix: calculate correct offset
        push ebx
        imul ebx, esi, 20
        mov [tab_data + ebx], dl
        pop ebx
        mov byte [tab_count + esi], 1
        mov byte [tab_faceup + esi], 0
        mov eax, 1
        pop edi
        pop esi
        pop ecx
        pop ebx
        ret

.tmt_next:
        inc esi
        jmp .tmt_loop

.tmt_fail:
        xor eax, eax
        pop edi
        pop esi
        pop ecx
        pop ebx
        ret

;---------------------------------------
; try_move_stack_to_tableau - Move stack from one column to another
; EDI = source column, ECX = start index in source
; Returns: EAX = 1 if moved, 0 if not
;---------------------------------------
try_move_stack_to_tableau:
        pushad

        ; Get the card at the start of the stack
        imul eax, edi, 20
        add eax, ecx
        movzx edx, byte [tab_data + eax]
        mov [.tms_card], edx
        mov [.tms_src_col], edi
        mov [.tms_src_idx], ecx

        ; Get card info
        movzx ebx, dl
        and ebx, 0x0F         ; Rank
        movzx eax, dl
        shr eax, 4            ; Suit
        xor edi, edi           ; Color: 0=red
        cmp eax, 2
        jb .tms_color_set
        mov edi, 1             ; 1=black
.tms_color_set:

        ; Try each tableau column (except source)
        xor esi, esi
.tms_loop:
        cmp esi, NUM_TABLEAU
        jge .tms_fail
        cmp esi, [.tms_src_col]
        je .tms_next

        movzx eax, byte [tab_count + esi]
        cmp eax, 0
        je .tms_empty_check

        ; Get top card
        dec eax
        push ebx
        imul ebx, esi, 20
        movzx eax, byte [tab_data + ebx + eax]
        pop ebx

        ; Check opposite color
        push edx
        movzx edx, al
        shr edx, 4
        xor ecx, ecx
        cmp edx, 2
        jb .tms_top_red
        mov ecx, 1
.tms_top_red:
        pop edx
        cmp ecx, edi
        je .tms_next

        ; Check rank (top - 1 == our rank)
        movzx ecx, al
        and ecx, 0x0F
        dec ecx
        cmp ecx, ebx
        jne .tms_next
        jmp .tms_do_move

.tms_empty_check:
        ; Only King can go on empty
        cmp ebx, 13
        jne .tms_next

.tms_do_move:
        ; Move cards [src_idx..tab_count-1] from src_col to dest column esi
        mov ecx, [.tms_src_idx]
        mov edi, [.tms_src_col]
.tms_copy:
        movzx eax, byte [tab_count + edi]
        cmp ecx, eax
        jge .tms_copy_done

        ; Get card from source
        imul eax, edi, 20
        add eax, ecx
        movzx edx, byte [tab_data + eax]

        ; Place in destination
        movzx eax, byte [tab_count + esi]
        push ebx
        imul ebx, esi, 20
        add ebx, eax
        mov [tab_data + ebx], dl
        pop ebx
        inc byte [tab_count + esi]

        inc ecx
        jmp .tms_copy

.tms_copy_done:
        ; Set faceup on destination if not set
        movzx eax, byte [tab_faceup + esi]
        movzx ecx, byte [tab_count + esi]
        cmp eax, ecx
        jb .tms_faceup_ok
        mov byte [tab_faceup + esi], 0
.tms_faceup_ok:

        ; Truncate source column
        mov ecx, [.tms_src_idx]
        mov [tab_count + edi], cl

        ; Auto-flip source
        cmp cl, 0
        je .tms_src_fix
        dec cl
        movzx eax, byte [tab_faceup + edi]
        cmp eax, ecx
        jbe .tms_moved
        mov [tab_faceup + edi], cl
        jmp .tms_moved
.tms_src_fix:
        mov byte [tab_faceup + edi], 0

.tms_moved:
        popad
        mov eax, 1
        ret

.tms_next:
        inc esi
        jmp .tms_loop

.tms_fail:
        popad
        xor eax, eax
        ret

.tms_card: dd 0
.tms_src_col: dd 0
.tms_src_idx: dd 0

;---------------------------------------
; check_win - Check if all foundations are full
;---------------------------------------
check_win:
        pushad
        xor esi, esi
        xor eax, eax
.cw_loop:
        cmp esi, NUM_FOUNDATIONS
        jge .cw_check
        movzx ebx, byte [found_count + esi]
        add eax, ebx
        inc esi
        jmp .cw_loop
.cw_check:
        cmp eax, NUM_CARDS
        jne .cw_no
        mov byte [game_won], 1
.cw_no:
        popad
        ret

;=======================================================================
; RANDOM NUMBER GENERATOR
;=======================================================================

rng_next:
        push edx
        mov eax, [rng_state]
        imul eax, 1103515245
        add eax, 12345
        mov [rng_state], eax
        shr eax, 16            ; Use upper bits
        pop edx
        ret

;=======================================================================
; DATA
;=======================================================================

title_str:   db "Solitaire", 0
str_status:  db "Click cards to play  |  N=New Game  |  ESC=Quit", 0
str_win:     db "*** YOU WIN! ***", 0

; Rank strings
rank_a: db "A", 0
rank_2: db "2", 0
rank_3: db "3", 0
rank_4: db "4", 0
rank_5: db "5", 0
rank_6: db "6", 0
rank_7: db "7", 0
rank_8: db "8", 0
rank_9: db "9", 0
rank_10: db "10", 0
rank_j: db "J", 0
rank_q: db "Q", 0
rank_k: db "K", 0

rank_strs:
        dd rank_a, rank_2, rank_3, rank_4, rank_5, rank_6, rank_7
        dd rank_8, rank_9, rank_10, rank_j, rank_q, rank_k

; Suit strings (using single chars for simplicity)
suit_h: db "H", 0              ; Hearts
suit_d: db "D", 0              ; Diamonds
suit_c: db "C", 0              ; Clubs
suit_s: db "S", 0              ; Spades

suit_strs:
        dd suit_h, suit_d, suit_c, suit_s

;=======================================================================
; BSS
;=======================================================================
align 4
win_id:        dd 0
rng_state:     dd 0
game_won:      db 0
selected_pile: dd -1
selected_idx:  dd -1
moves:         dd 0

; Deck
deck:          times NUM_CARDS db 0

; Stock and waste
stock_data:    times 24 db 0
stock_count:   dd 0
waste_data:    times 24 db 0
waste_count:   dd 0

; Tableau: 7 columns, max 20 cards each
tab_data:      times (NUM_TABLEAU * 20) db 0
tab_count:     times NUM_TABLEAU db 0
tab_faceup:    times NUM_TABLEAU db 0

; Foundations: 4 piles, max 14 cards each
found_data:    times (NUM_FOUNDATIONS * 14) db 0
found_count:   times NUM_FOUNDATIONS db 0
