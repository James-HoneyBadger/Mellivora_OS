; pipes.asm - Pipe Dream puzzle game for Mellivora OS (Burrows GUI)
; Place pipe pieces to connect the source to the drain before
; the water flows. Click grid cells to place the next piece.

%include "syscalls.inc"
%include "lib/gui.inc"

; Window dimensions
WIN_W           equ 320
WIN_H           equ 300

; Grid
GRID_X          equ 10
GRID_Y          equ 40
GRID_COLS       equ 10
GRID_ROWS       equ 8
CELL_SIZE       equ 28
GRID_W          equ (GRID_COLS * CELL_SIZE)
GRID_H          equ (GRID_ROWS * CELL_SIZE)

; Pipe types (packed bits: NESW = bit3..bit0)
PIPE_NONE       equ 0
PIPE_H          equ 0x05        ; E+W (horizontal) ═
PIPE_V          equ 0x0A        ; N+S (vertical)   ║
PIPE_NE         equ 0x09        ; N+E (elbow)      ╚
PIPE_NW         equ 0x0C        ; N+W              ╝
PIPE_SE         equ 0x03        ; S+E              ╔
PIPE_SW         equ 0x06        ; S+W              ╗
PIPE_CROSS      equ 0x0F        ; all 4            ╬
NUM_PIECES      equ 7

; Direction bits
DIR_N           equ 8           ; bit 3
DIR_E           equ 1           ; bit 0
DIR_S           equ 2           ; bit 1
DIR_W           equ 4           ; bit 2

; Cell states
CELL_EMPTY      equ 0
CELL_PLACED     equ 1
CELL_FILLED     equ 2
CELL_SOURCE     equ 3
CELL_DRAIN      equ 4

; Game states
STATE_PLAY      equ 0
STATE_FLOWING   equ 1
STATE_WIN       equ 2
STATE_LOSE      equ 3

; Colors
COL_BG          equ 0x00334455
COL_GRID_BG     equ 0x00556677
COL_GRID_LINE   equ 0x00445566
COL_PIPE        equ 0x00CCCCCC
COL_PIPE_FILL   equ 0x003399FF
COL_SOURCE      equ 0x0044DD44
COL_DRAIN       equ 0x00DD4444
COL_HUD         equ 0x00FFFFFF
COL_PREVIEW     equ 0x00AAAACC
COL_WIN_TEXT    equ 0x0044FF44
COL_LOSE_TEXT   equ 0x00FF4444

start:
        ; Create window
        mov eax, 100
        mov ebx, 40
        mov ecx, WIN_W
        mov edx, WIN_H
        mov esi, win_title
        call gui_create_window
        mov [win_id], eax

        call init_game

.main_loop:
        call draw_all

        ; In flowing state, advance water
        cmp dword [game_state], STATE_FLOWING
        jne .not_flowing
        call advance_flow
.not_flowing:

        ; Poll events
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
        cmp ebx, 27             ; ESC
        je .quit
        cmp ebx, ' '
        je .start_flow
        cmp ebx, 'r'
        je .restart
        cmp ebx, 'R'
        je .restart
        jmp .main_loop

.start_flow:
        cmp dword [game_state], STATE_PLAY
        jne .main_loop
        mov dword [game_state], STATE_FLOWING
        mov dword [flow_timer], 0
        ; Set flow start at source
        movzx eax, byte [source_row]
        imul eax, GRID_COLS
        movzx ebx, byte [source_col]
        add eax, ebx
        mov [flow_pos], eax
        mov byte [flow_dir], DIR_E  ; start flowing east from source
        jmp .main_loop

.restart:
        call init_game
        jmp .main_loop

.handle_click:
        ; EBX = x, ECX = y
        cmp dword [game_state], STATE_PLAY
        jne .main_loop

        ; Check if click is in grid
        sub ebx, GRID_X
        js .main_loop
        sub ecx, GRID_Y
        js .main_loop
        ; Col = ebx / CELL_SIZE, Row = ecx / CELL_SIZE
        push rdx
        mov eax, ebx
        xor edx, edx
        push rcx
        mov ecx, CELL_SIZE
        div ecx
        mov ebx, eax             ; col
        pop rax
        xor edx, edx
        push rcx
        mov ecx, CELL_SIZE
        div ecx
        mov ecx, eax             ; row
        pop rdx
        pop rdx

        ; Bounds check
        cmp ebx, GRID_COLS
        jge .main_loop
        cmp ecx, GRID_ROWS
        jge .main_loop

        ; Check if cell is occupied (source, drain, or already placed)
        mov eax, ecx
        imul eax, GRID_COLS
        add eax, ebx
        cmp byte [grid_state + eax], CELL_EMPTY
        jne .main_loop

        ; Place current piece
        movzx edx, byte [next_piece]
        mov [grid_pipes + eax], dl
        mov byte [grid_state + eax], CELL_PLACED

        ; Generate next piece
        call gen_next_piece
        inc dword [pieces_placed]
        jmp .main_loop

.quit:
        mov eax, [win_id]
        call gui_destroy_window
        mov eax, SYS_EXIT
        int 0x80


; ─── init_game ───────────────────────────────────────────────
init_game:
        PUSHALL
        ; Clear grid
        mov edi, grid_pipes
        mov ecx, GRID_COLS * GRID_ROWS
        xor eax, eax
        rep stosb
        mov edi, grid_state
        mov ecx, GRID_COLS * GRID_ROWS
        rep stosb

        mov dword [game_state], STATE_PLAY
        mov dword [pieces_placed], 0
        mov dword [score], 0
        mov dword [flow_timer], 0

        ; Place source at left side, random row
        mov eax, SYS_GETTIME
        int 0x80
        xor edx, edx
        mov ecx, GRID_ROWS - 2
        div ecx
        inc edx                  ; row 1..ROWS-2
        mov [source_row], dl
        mov byte [source_col], 0
        movzx eax, dl
        imul eax, GRID_COLS
        mov byte [grid_state + eax], CELL_SOURCE
        mov byte [grid_pipes + eax], DIR_E  ; source opens east

        ; Place drain at right side, random row
        mov eax, SYS_GETTIME
        int 0x80
        shr eax, 4
        xor edx, edx
        mov ecx, GRID_ROWS - 2
        div ecx
        inc edx
        mov [drain_row], dl
        mov byte [drain_col], GRID_COLS - 1
        movzx eax, dl
        imul eax, GRID_COLS
        add eax, GRID_COLS - 1
        mov byte [grid_state + eax], CELL_DRAIN
        mov byte [grid_pipes + eax], DIR_W  ; drain opens west

        ; Generate first piece
        call gen_next_piece

        POPALL
        ret


; ─── gen_next_piece ──────────────────────────────────────────
gen_next_piece:
        push rax
        push rcx
        push rdx
        mov eax, SYS_GETTIME
        int 0x80
        xor edx, edx
        mov ecx, NUM_PIECES
        div ecx
        movzx eax, byte [piece_types + edx]
        mov [next_piece], al
        pop rdx
        pop rcx
        pop rax
        ret


; ─── advance_flow ────────────────────────────────────────────
; Advance water one cell. Called each frame.
advance_flow:
        PUSHALL
        ; Flow speed: advance every 15 frames
        inc dword [flow_timer]
        cmp dword [flow_timer], 15
        jl .af_done
        mov dword [flow_timer], 0

        ; Current position
        mov eax, [flow_pos]
        cmp eax, 0
        jl .af_lose
        cmp eax, GRID_COLS * GRID_ROWS
        jge .af_lose

        ; Mark current cell as filled
        mov byte [grid_state + eax], CELL_FILLED
        add dword [score], 10

        ; Determine next cell based on flow direction
        movzx ebx, byte [flow_dir]

        ; Get current pipe connectivity
        movzx ecx, byte [grid_pipes + eax]

        ; Check if current pipe has an opening in the flow direction
        ; (for source/pipe cells, check the bits)

        ; Find exit direction: flow_dir enters, need to find the other opening
        ; Invert entry direction to get entry side
        ; If flowing East, we enter from West -> entry bit = DIR_W
        ; Exit = pipe_bits & ~entry_bit
        mov edx, ebx               ; flow direction (which side we exit from prev cell)
        ; Convert to entry side of current cell
        ; N->S, E->W, S->N, W->E
        call invert_dir             ; edx = entry side
        mov esi, ecx
        and esi, edx                ; Does pipe have opening on entry side?
        test esi, esi
        jz .af_lose                 ; No entry -> water leaks

        ; Find exit: remove entry direction, remaining bits = possible exits
        not edx
        and ecx, edx
        and ecx, 0x0F              ; mask to 4 bits

        ; If no exit bits, dead end
        test ecx, ecx
        jz .af_lose

        ; Pick first available exit direction
        test ecx, DIR_N
        jnz .af_go_n
        test ecx, DIR_E
        jnz .af_go_e
        test ecx, DIR_S
        jnz .af_go_s
        test ecx, DIR_W
        jnz .af_go_w
        jmp .af_lose

.af_go_n:
        sub eax, GRID_COLS
        mov byte [flow_dir], DIR_N
        jmp .af_moved
.af_go_e:
        inc eax
        mov byte [flow_dir], DIR_E
        jmp .af_moved
.af_go_s:
        add eax, GRID_COLS
        mov byte [flow_dir], DIR_S
        jmp .af_moved
.af_go_w:
        dec eax
        mov byte [flow_dir], DIR_W
        jmp .af_moved

.af_moved:
        ; Bounds check
        cmp eax, 0
        jl .af_lose
        cmp eax, GRID_COLS * GRID_ROWS
        jge .af_lose

        ; Check if we reached the drain
        cmp byte [grid_state + eax], CELL_DRAIN
        je .af_win

        ; Check if next cell has a pipe or is empty
        cmp byte [grid_state + eax], CELL_EMPTY
        je .af_lose
        cmp byte [grid_state + eax], CELL_FILLED
        je .af_lose              ; already filled = loop, lose

        mov [flow_pos], eax
        jmp .af_done

.af_win:
        mov byte [grid_state + eax], CELL_FILLED
        add dword [score], 100
        mov dword [game_state], STATE_WIN
        jmp .af_done

.af_lose:
        mov dword [game_state], STATE_LOSE

.af_done:
        POPALL
        ret


; ─── invert_dir ──────────────────────────────────────────────
; EDX = direction bit -> EDX = opposite direction
invert_dir:
        cmp edx, DIR_N
        je .inv_s
        cmp edx, DIR_S
        je .inv_n
        cmp edx, DIR_E
        je .inv_w
        cmp edx, DIR_W
        je .inv_e
        ret
.inv_n: mov edx, DIR_N
        ret
.inv_s: mov edx, DIR_S
        ret
.inv_e: mov edx, DIR_E
        ret
.inv_w: mov edx, DIR_W
        ret


; ─── draw_all ────────────────────────────────────────────────
draw_all:
        PUSHALL

        ; Background
        mov eax, [win_id]
        xor ebx, ebx
        xor ecx, ecx
        mov edx, WIN_W
        mov esi, WIN_H
        mov edi, COL_BG
        call gui_fill_rect

        ; HUD: score and next piece
        mov eax, [win_id]
        mov ebx, 10
        mov ecx, 8
        mov esi, str_score
        mov edi, COL_HUD
        call gui_draw_text

        ; Score number
        mov eax, [score]
        call itoa
        mov eax, [win_id]
        mov ebx, 70
        mov ecx, 8
        mov esi, num_buf
        mov edi, COL_HUD
        call gui_draw_text

        ; Next piece label
        mov eax, [win_id]
        mov ebx, 140
        mov ecx, 8
        mov esi, str_next
        mov edi, COL_HUD
        call gui_draw_text

        ; Draw next piece preview
        movzx eax, byte [next_piece]
        mov ebx, 200
        mov ecx, 2
        mov edx, 24
        call draw_pipe_at

        ; Controls hint
        mov eax, [win_id]
        mov ebx, 10
        mov ecx, WIN_H - 16
        mov esi, str_controls
        mov edi, 0x00888888
        call gui_draw_text

        ; Draw grid background
        mov eax, [win_id]
        mov ebx, GRID_X
        mov ecx, GRID_Y
        mov edx, GRID_W
        mov esi, GRID_H
        mov edi, COL_GRID_BG
        call gui_fill_rect

        ; Draw grid cells
        xor esi, esi             ; row
.draw_row:
        cmp esi, GRID_ROWS
        jge .draw_grid_done
        xor edi, edi             ; col
.draw_col:
        cmp edi, GRID_COLS
        jge .draw_row_next

        ; Cell index
        mov eax, esi
        imul eax, GRID_COLS
        add eax, edi

        ; Draw cell border
        push rax
        push rsi
        push rdi
        mov ebx, edi
        imul ebx, CELL_SIZE
        add ebx, GRID_X
        mov ecx, esi
        imul ecx, CELL_SIZE
        add ecx, GRID_Y
        push rbx
        push rcx
        ; Cell outline
        mov eax, [win_id]
        mov edx, CELL_SIZE
        mov esi, CELL_SIZE
        mov edi, COL_GRID_LINE
        call gui_fill_rect
        ; Inner cell (1px border)
        pop rcx
        pop rbx
        inc ebx
        inc ecx
        mov eax, [win_id]
        mov edx, CELL_SIZE - 2
        mov esi, CELL_SIZE - 2
        mov edi, COL_GRID_BG
        call gui_fill_rect
        pop rdi
        pop rsi
        pop rax

        ; Draw pipe in cell
        cmp byte [grid_state + eax], CELL_EMPTY
        je .draw_cell_done

        push rax
        movzx eax, byte [grid_pipes + eax]
        mov ebx, edi
        imul ebx, CELL_SIZE
        add ebx, GRID_X
        mov ecx, esi
        imul ecx, CELL_SIZE
        add ecx, GRID_Y
        mov edx, CELL_SIZE
        ; Check if cell index is source or drain for special color
        mov ebp, [rsp]          ; get cell index
        cmp byte [grid_state + ebp], CELL_SOURCE
        je .draw_source_cell
        cmp byte [grid_state + ebp], CELL_DRAIN
        je .draw_drain_cell
        cmp byte [grid_state + ebp], CELL_FILLED
        je .draw_filled_cell
        call draw_pipe_at
        jmp .draw_cell_popped
.draw_source_cell:
        push rsi
        push rdi
        mov eax, [win_id]
        add ebx, 2
        add ecx, 2
        mov edx, CELL_SIZE - 4
        mov esi, CELL_SIZE - 4
        mov edi, COL_SOURCE
        call gui_fill_rect
        pop rdi
        pop rsi
        jmp .draw_cell_popped
.draw_drain_cell:
        push rsi
        push rdi
        mov eax, [win_id]
        add ebx, 2
        add ecx, 2
        mov edx, CELL_SIZE - 4
        mov esi, CELL_SIZE - 4
        mov edi, COL_DRAIN
        call gui_fill_rect
        pop rdi
        pop rsi
        jmp .draw_cell_popped
.draw_filled_cell:
        push rax
        mov eax, [rsp+4]        ; cell index
        push rbx
        push rcx
        movzx eax, byte [grid_pipes + eax]
        pop rcx
        pop rbx
        mov edx, CELL_SIZE
        call draw_pipe_filled_at
        pop rax
        jmp .draw_cell_popped
.draw_cell_popped:
        pop rax

.draw_cell_done:
        inc edi
        jmp .draw_col

.draw_row_next:
        inc esi
        jmp .draw_row

.draw_grid_done:
        ; Overlay messages
        cmp dword [game_state], STATE_WIN
        je .draw_win
        cmp dword [game_state], STATE_LOSE
        je .draw_lose
        jmp .draw_flip

.draw_win:
        mov eax, [win_id]
        mov ebx, 80
        mov ecx, GRID_Y + GRID_H / 2 - 8
        mov esi, str_win
        mov edi, COL_WIN_TEXT
        call gui_draw_text
        jmp .draw_flip

.draw_lose:
        mov eax, [win_id]
        mov ebx, 60
        mov ecx, GRID_Y + GRID_H / 2 - 8
        mov esi, str_lose
        mov edi, COL_LOSE_TEXT
        call gui_draw_text

.draw_flip:
        mov eax, [win_id]
        call gui_compose
        mov eax, [win_id]
        call gui_flip

        POPALL
        ret


; ─── draw_pipe_at ────────────────────────────────────────────
; Draw a pipe piece (unfilled)
; EAX = pipe type (NESW bits), EBX = x, ECX = y, EDX = cell size
draw_pipe_at:
        PUSHALL
        mov [.dp_type], al
        mov [.dp_x], ebx
        mov [.dp_y], ecx
        mov [.dp_size], edx

        ; Center of cell
        mov esi, edx
        shr esi, 1
        add esi, ebx             ; center_x
        mov edi, edx
        shr edi, 1
        add edi, ecx             ; center_y

        ; Pipe width = cell_size/4, half = cell_size/8
        mov ebp, edx
        shr ebp, 2               ; pipe_w

        ; Draw segments as rectangles
        ; North: vertical from top to center
        test byte [.dp_type], DIR_N
        jz .dp_no_n
        mov eax, [win_id]
        mov ebx, esi
        sub ebx, ebp
        shr ebp, 1
        add ebx, ebp
        shl ebp, 1
        mov ecx, [.dp_y]
        mov edx, ebp             ; width
        push rsi
        mov esi, [.dp_size]
        shr esi, 1               ; height = half cell
        push rdi
        mov edi, COL_PIPE
        call gui_fill_rect
        pop rdi
        pop rsi
        mov ebp, [.dp_size]
        shr ebp, 2
.dp_no_n:
        ; South: vertical from center to bottom
        test byte [.dp_type], DIR_S
        jz .dp_no_s
        mov eax, [win_id]
        mov ebx, esi
        sub ebx, ebp
        shr ebp, 1
        add ebx, ebp
        shl ebp, 1
        mov ecx, edi             ; center_y
        mov edx, ebp             ; width
        push rsi
        mov esi, [.dp_size]
        shr esi, 1               ; height
        push rdi
        mov edi, COL_PIPE
        call gui_fill_rect
        pop rdi
        pop rsi
        mov ebp, [.dp_size]
        shr ebp, 2
.dp_no_s:
        ; East: horizontal from center to right
        test byte [.dp_type], DIR_E
        jz .dp_no_e
        mov eax, [win_id]
        mov ebx, esi             ; center_x
        mov ecx, edi
        sub ecx, ebp
        shr ebp, 1
        add ecx, ebp
        shl ebp, 1
        push rsi
        mov edx, [.dp_size]
        shr edx, 1               ; width = half cell
        mov esi, ebp             ; height
        push rdi
        mov edi, COL_PIPE
        call gui_fill_rect
        pop rdi
        pop rsi
        mov ebp, [.dp_size]
        shr ebp, 2
.dp_no_e:
        ; West: horizontal from left to center
        test byte [.dp_type], DIR_W
        jz .dp_no_w
        mov eax, [win_id]
        mov ebx, [.dp_x]
        mov ecx, edi
        sub ecx, ebp
        shr ebp, 1
        add ecx, ebp
        shl ebp, 1
        push rsi
        mov edx, [.dp_size]
        shr edx, 1
        mov esi, ebp
        push rdi
        mov edi, COL_PIPE
        call gui_fill_rect
        pop rdi
        pop rsi
        mov ebp, [.dp_size]
        shr ebp, 2
.dp_no_w:
        POPALL
        ret

.dp_type:  db 0
.dp_x:     dd 0
.dp_y:     dd 0
.dp_size:  dd 0


; ─── draw_pipe_filled_at ─────────────────────────────────────
; Same as draw_pipe_at but in blue (filled with water)
draw_pipe_filled_at:
        PUSHALL
        mov [.dpf_type], al
        mov [.dpf_x], ebx
        mov [.dpf_y], ecx
        mov [.dpf_size], edx

        mov esi, edx
        shr esi, 1
        add esi, ebx
        mov edi, edx
        shr edi, 1
        add edi, ecx
        mov ebp, edx
        shr ebp, 2

        test byte [.dpf_type], DIR_N
        jz .dpf_no_n
        mov eax, [win_id]
        mov ebx, esi
        sub ebx, ebp
        shr ebp, 1
        add ebx, ebp
        shl ebp, 1
        mov ecx, [.dpf_y]
        mov edx, ebp
        push rsi
        mov esi, [.dpf_size]
        shr esi, 1
        push rdi
        mov edi, COL_PIPE_FILL
        call gui_fill_rect
        pop rdi
        pop rsi
        mov ebp, [.dpf_size]
        shr ebp, 2
.dpf_no_n:
        test byte [.dpf_type], DIR_S
        jz .dpf_no_s
        mov eax, [win_id]
        mov ebx, esi
        sub ebx, ebp
        shr ebp, 1
        add ebx, ebp
        shl ebp, 1
        mov ecx, edi
        mov edx, ebp
        push rsi
        mov esi, [.dpf_size]
        shr esi, 1
        push rdi
        mov edi, COL_PIPE_FILL
        call gui_fill_rect
        pop rdi
        pop rsi
        mov ebp, [.dpf_size]
        shr ebp, 2
.dpf_no_s:
        test byte [.dpf_type], DIR_E
        jz .dpf_no_e
        mov eax, [win_id]
        mov ebx, esi
        mov ecx, edi
        sub ecx, ebp
        shr ebp, 1
        add ecx, ebp
        shl ebp, 1
        push rsi
        mov edx, [.dpf_size]
        shr edx, 1
        mov esi, ebp
        push rdi
        mov edi, COL_PIPE_FILL
        call gui_fill_rect
        pop rdi
        pop rsi
        mov ebp, [.dpf_size]
        shr ebp, 2
.dpf_no_e:
        test byte [.dpf_type], DIR_W
        jz .dpf_no_w
        mov eax, [win_id]
        mov ebx, [.dpf_x]
        mov ecx, edi
        sub ecx, ebp
        shr ebp, 1
        add ecx, ebp
        shl ebp, 1
        push rsi
        mov edx, [.dpf_size]
        shr edx, 1
        mov esi, ebp
        push rdi
        mov edi, COL_PIPE_FILL
        call gui_fill_rect
        pop rdi
        pop rsi
        mov ebp, [.dpf_size]
        shr ebp, 2
.dpf_no_w:
        POPALL
        ret

.dpf_type:  db 0
.dpf_x:     dd 0
.dpf_y:     dd 0
.dpf_size:  dd 0


; ─── itoa ────────────────────────────────────────────────────
; Convert EAX to decimal string in num_buf
itoa:
        PUSHALL
        mov edi, num_buf + 11
        mov byte [edi], 0
        mov ebx, 10
.itoa_loop:
        dec edi
        xor edx, edx
        div ebx
        add dl, '0'
        mov [edi], dl
        test eax, eax
        jnz .itoa_loop
        ; Copy to beginning of num_buf
        mov esi, edi
        mov edi, num_buf
.itoa_copy:
        lodsb
        stosb
        test al, al
        jnz .itoa_copy
        POPALL
        ret


; ═════════════════════════════════════════════════════════════
; DATA
; ═════════════════════════════════════════════════════════════

win_title:      db "Pipe Dream", 0
str_score:      db "Score:", 0
str_next:       db "Next:", 0
str_controls:   db "Click=place  Space=flow  R=restart", 0
str_win:        db "YOU WIN! Press R", 0
str_lose:       db "WATER LEAKED! Press R", 0
num_buf:        times 12 db 0

; Available piece types
piece_types:    db PIPE_H, PIPE_V, PIPE_NE, PIPE_NW, PIPE_SE, PIPE_SW, PIPE_CROSS


; ═════════════════════════════════════════════════════════════
; BSS
; ═════════════════════════════════════════════════════════════

section .bss

win_id:         resd 1
game_state:     resd 1
score:          resd 1
pieces_placed:  resd 1
flow_timer:     resd 1
flow_pos:       resd 1           ; current flow cell index
flow_dir:       resb 1           ; current flow direction
next_piece:     resb 1
source_row:     resb 1
source_col:     resb 1
drain_row:      resb 1
drain_col:      resb 1

grid_pipes:     resb GRID_COLS * GRID_ROWS   ; pipe type per cell
grid_state:     resb GRID_COLS * GRID_ROWS   ; cell state per cell
