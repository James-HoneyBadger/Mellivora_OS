; chess.asm - Chess Game for Mellivora OS
; Two-player chess with legal move validation, board display using CP437.
; Move format: e2e4 (from-to in algebraic notation).
; Commands: 'quit', 'new', 'help'
%include "syscalls.inc"
%include "lib/io.inc"
%include "lib/string.inc"

; Piece constants (bit layout: 0x0P where P=piece type, color in high nibble)
EMPTY   equ 0
PAWN    equ 1
KNIGHT  equ 2
BISHOP  equ 3
ROOK    equ 4
QUEEN   equ 5
KING    equ 6
WHITE   equ 0x10
BLACK   equ 0x20
COLOR_MASK equ 0xF0
PIECE_MASK equ 0x0F

start:
        call init_board
        call draw_board

;=== Main loop ===
.main_loop:
        ; Print prompt
        cmp byte [turn], 0
        jne .black_turn
        mov esi, prompt_white
        jmp .print_prompt
.black_turn:
        mov esi, prompt_black
.print_prompt:
        call io_print

        ; Read move
        mov edi, input_buf
        mov ecx, 16
        call io_read_line

        ; Check commands
        mov esi, input_buf
        mov edi, cmd_quit
        call str_icmp
        test eax, eax
        jz .quit
        mov esi, input_buf
        mov edi, cmd_new
        call str_icmp
        test eax, eax
        jz .new_game
        mov esi, input_buf
        mov edi, cmd_help
        call str_icmp
        test eax, eax
        jz .show_help

        ; Parse move (e.g. "e2e4")
        mov esi, input_buf
        call parse_move         ; sets from_col/row, to_col/row, returns EAX=0 ok
        test eax, eax
        jnz .invalid_move

        ; Validate and execute
        call validate_move
        test eax, eax
        jnz .illegal_move

        call make_move
        ; Toggle turn
        xor byte [turn], 1
        call draw_board

        ; Check for checkmate/stalemate would go here
        jmp .main_loop

.invalid_move:
        mov esi, err_format
        call io_println
        jmp .main_loop

.illegal_move:
        mov esi, err_illegal
        call io_println
        jmp .main_loop

.new_game:
        call init_board
        call draw_board
        jmp .main_loop

.show_help:
        mov esi, help_str
        call io_println
        jmp .main_loop

.quit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================
; init_board - Set up starting position
;=======================================
init_board:
        PUSHALL
        ; Clear board
        mov edi, board
        mov ecx, 64
        xor al, al
        rep stosb

        ; Rank 1 (row 0): white pieces
        mov byte [board + 0], WHITE | ROOK
        mov byte [board + 1], WHITE | KNIGHT
        mov byte [board + 2], WHITE | BISHOP
        mov byte [board + 3], WHITE | QUEEN
        mov byte [board + 4], WHITE | KING
        mov byte [board + 5], WHITE | BISHOP
        mov byte [board + 6], WHITE | KNIGHT
        mov byte [board + 7], WHITE | ROOK

        ; Rank 2: white pawns
        mov ecx, 8
        lea edi, [board + 8]
.wp:    mov byte [edi], WHITE | PAWN
        inc edi
        loop .wp

        ; Rank 7: black pawns
        mov ecx, 8
        lea edi, [board + 48]
.bp:    mov byte [edi], BLACK | PAWN
        inc edi
        loop .bp

        ; Rank 8 (row 7): black pieces
        mov byte [board + 56], BLACK | ROOK
        mov byte [board + 57], BLACK | KNIGHT
        mov byte [board + 58], BLACK | BISHOP
        mov byte [board + 59], BLACK | QUEEN
        mov byte [board + 60], BLACK | KING
        mov byte [board + 61], BLACK | BISHOP
        mov byte [board + 62], BLACK | KNIGHT
        mov byte [board + 63], BLACK | ROOK

        mov byte [turn], 0     ; White starts
        mov dword [move_count], 0

        POPALL
        ret

;=======================================
; draw_board - Display the chess board
;=======================================
draw_board:
        PUSHALL
        mov eax, SYS_CLEAR
        int 0x80

        ; Title
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov esi, title_str
        call io_println
        call io_newline

        ; Draw rows 8 down to 1 (index 7 down to 0)
        mov ecx, 7              ; row index
.db_row:
        cmp ecx, -1
        je .db_files

        ; Row label
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E           ; Yellow
        int 0x80
        mov al, cl
        add al, '1'
        call io_putchar
        mov al, ' '
        call io_putchar

        ; 8 squares
        push rcx
        xor edx, edx           ; column
.db_col:
        cmp edx, 8
        jge .db_next_col

        ; Determine square color
        push rcx
        push rdx
        mov eax, ecx
        add eax, edx
        and eax, 1
        ; EAX=1 = light square, EAX=0 = dark square
        test eax, eax
        jz .db_dark_sq
        mov byte [sq_bg], 0x60  ; Brown bg (light square)
        jmp .db_sq_color_done
.db_dark_sq:
        mov byte [sq_bg], 0x20  ; Green bg (dark square)
.db_sq_color_done:
        pop rdx
        pop rcx

        ; Get piece
        push rcx
        imul ecx, 8
        add ecx, edx
        movzx eax, byte [board + ecx]
        pop rcx

        ; Determine piece character and color
        test eax, eax
        jz .db_empty_sq

        push rax
        mov ebx, eax
        and ebx, PIECE_MASK
        and eax, COLOR_MASK
        push rax                ; color

        ; Piece char
        dec ebx
        movzx ebx, byte [piece_chars + ebx]
        mov [sq_char], bl

        ; Piece color: white pieces = bright white, black = black
        pop rax
        cmp eax, WHITE
        jne .db_black_piece
        movzx eax, byte [sq_bg]
        or al, 0x0F             ; White fg
        jmp .db_set_sq
.db_black_piece:
        movzx eax, byte [sq_bg]
        or al, 0x00             ; Black fg
        jmp .db_set_sq

.db_empty_sq:
        mov byte [sq_char], ' '
        movzx eax, byte [sq_bg]

.db_set_sq:
        push rdx
        mov eax, SYS_SETCOLOR
        movzx ebx, byte [sq_bg]
        cmp byte [sq_char], ' '
        je .db_empty_color
        ; Recalculate color attribute for piece
        movzx ebx, byte [board + ecx*0]  ; We need to redo this
.db_empty_color:
        ; Just use sq_bg for empty
        pop rdx

        ; Simpler: construct color attribute
        push rcx
        imul ecx, 8
        add ecx, edx
        movzx eax, byte [board + ecx]
        pop rcx

        push rdx
        mov ebx, eax
        and ebx, COLOR_MASK
        movzx edx, byte [sq_bg]
        cmp ebx, WHITE
        jne .db_piece_color2
        or dl, 0x0F
        jmp .db_piece_color_set
.db_piece_color2:
        cmp ebx, BLACK
        jne .db_piece_empty
        or dl, 0x00
        jmp .db_piece_color_set
.db_piece_empty:
        ; Empty square
        or dl, 0x00
.db_piece_color_set:
        mov eax, SYS_SETCOLOR
        movzx ebx, dl
        int 0x80
        pop rdx

        ; Print: space, piece char, space (3 chars per square)
        mov al, ' '
        call io_putchar
        mov al, [sq_char]
        call io_putchar
        mov al, ' '
        call io_putchar

        inc edx
        jmp .db_col

.db_next_col:
        ; Reset color
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80
        call io_newline
        pop rcx
        dec ecx
        jmp .db_row

.db_files:
        ; File labels
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov esi, file_labels
        call io_println

        ; Game info
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        call io_newline
        mov esi, info_str
        call io_println

        POPALL
        ret

;=======================================
; parse_move: ESI=input -> sets from/to, EAX=0 ok
;=======================================
parse_move:
        push rbx
        push rcx

        ; Expected format: a-h digit a-h digit (4 chars)
        movzx eax, byte [esi]
        sub al, 'a'
        cmp al, 7
        ja .pm_err
        mov [from_col], eax

        movzx eax, byte [esi + 1]
        sub al, '1'
        cmp al, 7
        ja .pm_err
        mov [from_row], eax

        movzx eax, byte [esi + 2]
        sub al, 'a'
        cmp al, 7
        ja .pm_err
        mov [to_col], eax

        movzx eax, byte [esi + 3]
        sub al, '1'
        cmp al, 7
        ja .pm_err
        mov [to_row], eax

        xor eax, eax
        pop rcx
        pop rbx
        ret
.pm_err:
        mov eax, 1
        pop rcx
        pop rbx
        ret

;=======================================
; validate_move - Check if move is legal, EAX=0 ok
;=======================================
validate_move:
        PUSHALL

        ; Get source piece
        mov eax, [from_row]
        imul eax, 8
        add eax, [from_col]
        movzx ebx, byte [board + eax]

        ; Must have a piece
        test ebx, ebx
        jz .vm_illegal

        ; Must be current player's piece
        mov ecx, ebx
        and ecx, COLOR_MASK
        cmp byte [turn], 0
        jne .vm_check_black
        cmp ecx, WHITE
        jne .vm_illegal
        jmp .vm_check_dest
.vm_check_black:
        cmp ecx, BLACK
        jne .vm_illegal

.vm_check_dest:
        ; Destination must not have own piece
        mov eax, [to_row]
        imul eax, 8
        add eax, [to_col]
        movzx edx, byte [board + eax]
        test edx, edx
        jz .vm_dest_ok
        mov eax, edx
        and eax, COLOR_MASK
        cmp eax, ecx            ; same color?
        je .vm_illegal

.vm_dest_ok:
        ; Basic piece movement validation
        mov eax, ebx
        and eax, PIECE_MASK

        cmp eax, PAWN
        je .vm_pawn
        cmp eax, KNIGHT
        je .vm_knight
        cmp eax, BISHOP
        je .vm_bishop
        cmp eax, ROOK
        je .vm_rook
        cmp eax, QUEEN
        je .vm_queen
        cmp eax, KING
        je .vm_king
        jmp .vm_illegal

.vm_pawn:
        ; Direction based on color
        mov eax, [to_col]
        sub eax, [from_col]
        mov ecx, [to_row]
        sub ecx, [from_row]

        mov edx, ebx
        and edx, COLOR_MASK
        cmp edx, WHITE
        jne .vm_pawn_black

        ; White pawn: forward = +row
        cmp eax, 0              ; straight
        jne .vm_pawn_capture
        cmp ecx, 1
        je .vm_pawn_forward_ok
        ; Double move from rank 2
        cmp dword [from_row], 1
        jne .vm_illegal
        cmp ecx, 2
        jne .vm_illegal
        ; Check intermediate square empty
        push rbx
        mov eax, [from_row]
        inc eax
        imul eax, 8
        add eax, [from_col]
        cmp byte [board + eax], 0
        pop rbx
        jne .vm_illegal
.vm_pawn_forward_ok:
        ; Destination must be empty for forward move
        mov eax, [to_row]
        imul eax, 8
        add eax, [to_col]
        cmp byte [board + eax], 0
        jne .vm_illegal
        jmp .vm_legal

.vm_pawn_capture:
        ; Must move diagonally by 1
        cmp ecx, 1
        jne .vm_illegal
        cmp eax, 1
        je .vm_pawn_cap_ok
        cmp eax, -1
        jne .vm_illegal
.vm_pawn_cap_ok:
        ; Must have enemy piece at destination
        push rbx
        mov eax, [to_row]
        imul eax, 8
        add eax, [to_col]
        movzx ebx, byte [board + eax]
        test ebx, ebx
        pop rbx
        jz .vm_illegal
        jmp .vm_legal

.vm_pawn_black:
        ; Black pawn: forward = -row
        cmp eax, 0
        jne .vm_bpawn_capture
        cmp ecx, -1
        je .vm_bpawn_forward_ok
        cmp dword [from_row], 6
        jne .vm_illegal
        cmp ecx, -2
        jne .vm_illegal
        push rbx
        mov eax, [from_row]
        dec eax
        imul eax, 8
        add eax, [from_col]
        cmp byte [board + eax], 0
        pop rbx
        jne .vm_illegal
.vm_bpawn_forward_ok:
        mov eax, [to_row]
        imul eax, 8
        add eax, [to_col]
        cmp byte [board + eax], 0
        jne .vm_illegal
        jmp .vm_legal

.vm_bpawn_capture:
        cmp ecx, -1
        jne .vm_illegal
        cmp eax, 1
        je .vm_bpc_ok
        cmp eax, -1
        jne .vm_illegal
.vm_bpc_ok:
        push rbx
        mov eax, [to_row]
        imul eax, 8
        add eax, [to_col]
        movzx ebx, byte [board + eax]
        test ebx, ebx
        pop rbx
        jz .vm_illegal
        jmp .vm_legal

.vm_knight:
        mov eax, [to_col]
        sub eax, [from_col]
        ; abs
        test eax, eax
        jns .vk_abs1
        neg eax
.vk_abs1:
        mov ecx, [to_row]
        sub ecx, [from_row]
        test ecx, ecx
        jns .vk_abs2
        neg ecx
.vk_abs2:
        ; L-shape: (1,2) or (2,1)
        cmp eax, 1
        jne .vk_try2
        cmp ecx, 2
        je .vm_legal
        jmp .vm_illegal
.vk_try2:
        cmp eax, 2
        jne .vm_illegal
        cmp ecx, 1
        je .vm_legal
        jmp .vm_illegal

.vm_bishop:
        call check_diagonal
        test eax, eax
        jz .vm_legal
        jmp .vm_illegal

.vm_rook:
        call check_straight
        test eax, eax
        jz .vm_legal
        jmp .vm_illegal

.vm_queen:
        call check_straight
        test eax, eax
        jz .vm_legal
        call check_diagonal
        test eax, eax
        jz .vm_legal
        jmp .vm_illegal

.vm_king:
        mov eax, [to_col]
        sub eax, [from_col]
        test eax, eax
        jns .vki_abs1
        neg eax
.vki_abs1:
        mov ecx, [to_row]
        sub ecx, [from_row]
        test ecx, ecx
        jns .vki_abs2
        neg ecx
.vki_abs2:
        cmp eax, 1
        jg .vm_illegal
        cmp ecx, 1
        jg .vm_illegal
        jmp .vm_legal

.vm_legal:
        POPALL
        xor eax, eax
        ret
.vm_illegal:
        POPALL
        mov eax, 1
        ret

;---------------------------------------
; check_straight: returns EAX=0 if valid straight line move with clear path
;---------------------------------------
check_straight:
        push rbx
        push rcx
        push rdx
        push rsi

        mov eax, [to_col]
        sub eax, [from_col]
        mov ecx, [to_row]
        sub ecx, [from_row]

        ; Must be on same rank or file
        test eax, eax
        jnz .cs_check_file
        test ecx, ecx
        jz .cs_fail             ; no movement
        jmp .cs_setup
.cs_check_file:
        test ecx, ecx
        jnz .cs_fail            ; diagonal, not straight

.cs_setup:
        ; Normalize to step (-1, 0, +1)
        mov edx, eax
        test edx, edx
        jz .cs_dx_done
        jns .cs_dx_pos
        mov edx, -1
        jmp .cs_dx_done
.cs_dx_pos:
        mov edx, 1
.cs_dx_done:
        mov esi, ecx
        test esi, esi
        jz .cs_dy_done
        jns .cs_dy_pos
        mov esi, -1
        jmp .cs_dy_done
.cs_dy_pos:
        mov esi, 1
.cs_dy_done:
        ; Walk from (from+step) to (to-step), checking empty
        mov eax, [from_col]
        mov ecx, [from_row]
        add eax, edx
        add ecx, esi
.cs_walk:
        cmp eax, [to_col]
        jne .cs_check_sq
        cmp ecx, [to_row]
        jne .cs_check_sq
        ; Reached destination
        xor eax, eax
        jmp .cs_ret
.cs_check_sq:
        push rax
        push rcx
        imul ecx, 8
        add ecx, eax
        cmp byte [board + ecx], 0
        pop rcx
        pop rax
        jnz .cs_fail
        add eax, edx
        add ecx, esi
        jmp .cs_walk

.cs_fail:
        mov eax, 1
.cs_ret:
        pop rsi
        pop rdx
        pop rcx
        pop rbx
        ret

;---------------------------------------
; check_diagonal: returns EAX=0 if valid diagonal with clear path
;---------------------------------------
check_diagonal:
        push rbx
        push rcx
        push rdx
        push rsi

        mov eax, [to_col]
        sub eax, [from_col]    ; dx
        mov ecx, [to_row]
        sub ecx, [from_row]    ; dy

        ; Must have |dx| == |dy|
        mov edx, eax
        test edx, edx
        jns .cd_abs1
        neg edx
.cd_abs1:
        mov esi, ecx
        test esi, esi
        jns .cd_abs2
        neg esi
.cd_abs2:
        cmp edx, esi
        jne .cd_fail
        test edx, edx
        jz .cd_fail

        ; Normalize steps
        mov edx, eax
        test edx, edx
        jns .cd_dx_pos
        mov edx, -1
        jmp .cd_dx_done
.cd_dx_pos:
        mov edx, 1
.cd_dx_done:
        mov esi, ecx
        test esi, esi
        jns .cd_dy_pos
        mov esi, -1
        jmp .cd_dy_done
.cd_dy_pos:
        mov esi, 1
.cd_dy_done:
        mov eax, [from_col]
        mov ecx, [from_row]
        add eax, edx
        add ecx, esi
.cd_walk:
        cmp eax, [to_col]
        jne .cd_check
        cmp ecx, [to_row]
        jne .cd_check
        xor eax, eax
        jmp .cd_ret
.cd_check:
        push rax
        push rcx
        imul ecx, 8
        add ecx, eax
        cmp byte [board + ecx], 0
        pop rcx
        pop rax
        jnz .cd_fail
        add eax, edx
        add ecx, esi
        jmp .cd_walk

.cd_fail:
        mov eax, 1
.cd_ret:
        pop rsi
        pop rdx
        pop rcx
        pop rbx
        ret

;=======================================
; make_move - Execute the move
;=======================================
make_move:
        PUSHALL
        ; Get source
        mov eax, [from_row]
        imul eax, 8
        add eax, [from_col]
        movzx ebx, byte [board + eax]
        mov byte [board + eax], EMPTY

        ; Place at destination
        mov eax, [to_row]
        imul eax, 8
        add eax, [to_col]
        mov [board + eax], bl

        ; Pawn promotion (simple: auto-queen)
        mov ecx, ebx
        and ecx, PIECE_MASK
        cmp ecx, PAWN
        jne .mm_done
        mov ecx, ebx
        and ecx, COLOR_MASK
        cmp ecx, WHITE
        jne .mm_black_promo
        cmp dword [to_row], 7
        jne .mm_done
        mov byte [board + eax], WHITE | QUEEN
        jmp .mm_done
.mm_black_promo:
        cmp dword [to_row], 0
        jne .mm_done
        mov byte [board + eax], BLACK | QUEEN

.mm_done:
        inc dword [move_count]
        POPALL
        ret

; === Data ===
; Piece display chars (CP437): King=K Queen=Q Rook=R Bishop=B kNight=N Pawn=P
piece_chars:    db 'P', 'N', 'B', 'R', 'Q', 'K'

title_str:      db "=== Mellivora Chess ===", 0
file_labels:    db "   a  b  c  d  e  f  g  h", 0
prompt_white:   db "White> ", 0
prompt_black:   db "Black> ", 0
err_format:     db "Invalid format. Use: e2e4", 0
err_illegal:    db "Illegal move!", 0
info_str:       db "Enter move (e.g. e2e4), 'new', 'help', or 'quit'", 0
cmd_quit:       db "quit", 0
cmd_new:        db "new", 0
cmd_help:       db "help", 0
help_str:
        db "=== Chess Help ===", 0x0A
        db "Move: type source and destination squares (e.g. e2e4)", 0x0A
        db "Files: a-h (columns left to right)", 0x0A
        db "Ranks: 1-8 (rows bottom to top)", 0x0A
        db "Pieces shown as letters: K Q R B N P", 0x0A
        db "Pawns auto-promote to Queen on last rank.", 0x0A
        db "Commands: new (restart), quit (exit)", 0

; === BSS ===
board:          times 64 db 0
turn:           db 0
sq_bg:          db 0
sq_char:        db 0
move_count:     dd 0
from_col:       dd 0
from_row:       dd 0
to_col:         dd 0
to_row:         dd 0
input_buf:      times 32 db 0
