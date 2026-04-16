; mine.asm - Minesweeper for Mellivora OS
; Converted from boot-sector-minesweeper (MIT License) by blevy
; 32-bit protected mode, uses INT 0x80 syscalls + direct VGA
%include "syscalls.inc"

; Game dimensions
MAP_W           equ 40
MAP_H           equ 20
MAP_SIZE        equ MAP_W * MAP_H
MAP_OFFSET_X    equ 20          ; center 40-wide field on 80-col screen
MAP_OFFSET_Y    equ 2           ; leave room for header

; Cell values in map_unveiled
CELL_MINE       equ 0xFF        ; mine marker

; Colors
COL_HIDDEN      equ 0xA0        ; green bg, black fg (hidden cell)
COL_REVEALED    equ 0x07        ; normal (revealed)
COL_MINE        equ 0x4F        ; white on red (mine)
COL_CURSOR      equ 0xE0        ; yellow bg (cursor highlight)
COL_FLAG        equ 0x0C        ; bright red
COL_HEADER      equ 0x0E        ; yellow
COL_ZERO        equ 0x08        ; dark gray (empty revealed)

; Number colors (1-8)
num_colors:     db 0x09, 0x02, 0x0C, 0x01, 0x04, 0x03, 0x00, 0x08

start:
        ; Seed random from timer
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_seed], eax

new_game:
        mov dword [cursor_x], MAP_W / 2
        mov dword [cursor_y], MAP_H / 2
        mov dword [mines_total], 0
        mov byte  [game_active], 1
        mov dword [cells_revealed], 0

        ; Clear mine and unveiled maps
        mov edi, map_mines
        mov ecx, MAP_SIZE
        xor al, al
        rep stosb

        mov edi, map_unveiled
        mov ecx, MAP_SIZE
        xor al, al
        rep stosb

        mov edi, map_visible
        mov ecx, MAP_SIZE
        xor al, al
        rep stosb               ; 0 = hidden, 1 = revealed, 2 = flagged

        ; Populate mines (~12.5% chance per cell = 1/8)
        xor ecx, ecx
.populate:
        cmp ecx, MAP_SIZE
        jge .populate_done
        call rand
        test al, 0x07           ; if low 3 bits == 0 -> mine
        jnz .no_mine
        mov byte [map_mines + ecx], 1
        inc dword [mines_total]
.no_mine:
        inc ecx
        jmp .populate
.populate_done:

        ; Calculate numbers for each cell
        xor ecx, ecx           ; cell index
.number_loop:
        cmp ecx, MAP_SIZE
        jge .number_done

        ; If mine, store CELL_MINE
        cmp byte [map_mines + ecx], 1
        jne .count_neighbors
        mov byte [map_unveiled + ecx], CELL_MINE
        jmp .next_cell

.count_neighbors:
        ; Count mines around cell ECX
        xor edx, edx           ; neighbor mine count

        ; Get row/col
        mov eax, ecx
        xor ebx, ebx           ; remainder
        push rcx
        push rdx
        mov ebx, MAP_W
        xor edx, edx
        div ebx                 ; eax=row, edx=col
        mov esi, eax            ; row
        mov edi, edx            ; col
        pop rdx
        pop rcx

        ; Check all 8 neighbors
        ; Up (row-1)
        cmp esi, 0
        je .skip_up
        mov eax, ecx
        sub eax, MAP_W
        call check_mine
        add edx, eax
        ; Up-left
        cmp edi, 0
        je .skip_ul
        mov eax, ecx
        sub eax, MAP_W
        dec eax
        call check_mine
        add edx, eax
.skip_ul:
        ; Up-right
        lea eax, [edi + 1]
        cmp eax, MAP_W
        jge .skip_ur
        mov eax, ecx
        sub eax, MAP_W
        inc eax
        call check_mine
        add edx, eax
.skip_ur:
.skip_up:

        ; Down (row+1)
        lea eax, [esi + 1]
        cmp eax, MAP_H
        jge .skip_down
        mov eax, ecx
        add eax, MAP_W
        call check_mine
        add edx, eax
        ; Down-left
        cmp edi, 0
        je .skip_dl
        mov eax, ecx
        add eax, MAP_W
        dec eax
        call check_mine
        add edx, eax
.skip_dl:
        ; Down-right
        lea eax, [edi + 1]
        cmp eax, MAP_W
        jge .skip_dr
        mov eax, ecx
        add eax, MAP_W
        inc eax
        call check_mine
        add edx, eax
.skip_dr:
.skip_down:

        ; Left
        cmp edi, 0
        je .skip_left
        mov eax, ecx
        dec eax
        call check_mine
        add edx, eax
.skip_left:

        ; Right
        lea eax, [edi + 1]
        cmp eax, MAP_W
        jge .skip_right
        mov eax, ecx
        inc eax
        call check_mine
        add edx, eax
.skip_right:

        mov [map_unveiled + ecx], dl

.next_cell:
        inc ecx
        jmp .number_loop
.number_done:

        ; Calculate safe cells count
        mov eax, MAP_SIZE
        sub eax, [mines_total]
        mov [safe_cells], eax

        ; Draw initial screen
        mov eax, SYS_CLEAR
        int 0x80
        call draw_header
        call draw_field
        call draw_cursor

;=== Main game loop ===
game_loop:
        mov eax, SYS_GETCHAR
        int 0x80

        cmp al, 27              ; ESC
        je exit_game
        cmp al, 'q'
        je exit_game

        cmp byte [game_active], 0
        je .dead_keys

        ; Movement
        cmp al, KEY_UP
        je .move_up
        cmp al, 'w'
        je .move_up
        cmp al, KEY_DOWN
        je .move_down
        cmp al, 's'
        je .move_down
        cmp al, KEY_LEFT
        je .move_left
        cmp al, 'a'
        je .move_left
        cmp al, KEY_RIGHT
        je .move_right
        cmp al, 'd'
        je .move_right

        ; Actions
        cmp al, ' '
        je .reveal
        cmp al, 0x0D            ; Enter
        je .reveal
        cmp al, 'f'
        je .flag

        jmp game_loop

.dead_keys:
        cmp al, 'r'
        je .restart
        jmp game_loop

.restart:
        jmp new_game

.move_up:
        cmp dword [cursor_y], 0
        jle game_loop
        call erase_cursor
        dec dword [cursor_y]
        call draw_cursor
        jmp game_loop

.move_down:
        cmp dword [cursor_y], MAP_H - 1
        jge game_loop
        call erase_cursor
        inc dword [cursor_y]
        call draw_cursor
        jmp game_loop

.move_left:
        cmp dword [cursor_x], 0
        jle game_loop
        call erase_cursor
        dec dword [cursor_x]
        call draw_cursor
        jmp game_loop

.move_right:
        cmp dword [cursor_x], MAP_W - 1
        jge game_loop
        call erase_cursor
        inc dword [cursor_x]
        call draw_cursor
        jmp game_loop

.flag:
        ; Toggle flag on current cell
        call cursor_to_index
        cmp byte [map_visible + eax], 1
        je game_loop            ; already revealed
        xor byte [map_visible + eax], 2  ; toggle flag (0<->2)
        call draw_cell_at_cursor
        call draw_cursor
        jmp game_loop

.reveal:
        call cursor_to_index
        cmp byte [map_visible + eax], 0
        jne game_loop           ; already revealed or flagged

        ; Check if mine
        cmp byte [map_unveiled + eax], CELL_MINE
        je .hit_mine

        ; Reveal with flood fill
        mov eax, [cursor_x]
        mov ebx, [cursor_y]
        call flood_reveal

        call draw_field
        call draw_cursor

        ; Check win
        mov eax, [cells_revealed]
        cmp eax, [safe_cells]
        jge .win

        jmp game_loop

.hit_mine:
        ; Reveal all mines
        call reveal_all_mines
        call draw_field

        ; Show game over message
        mov eax, SYS_SETCURSOR
        mov ebx, MAP_OFFSET_X + MAP_W / 2 - 6
        mov ecx, MAP_OFFSET_Y + MAP_H / 2
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x4F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_gameover
        int 0x80

        mov byte [game_active], 0
        call draw_restart_msg
        jmp game_loop

.win:
        mov eax, SYS_SETCURSOR
        mov ebx, MAP_OFFSET_X + MAP_W / 2 - 5
        mov ecx, MAP_OFFSET_Y + MAP_H / 2
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x2F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_win
        int 0x80

        mov byte [game_active], 0
        call draw_restart_msg
        jmp game_loop

;=== Helper: check mine at index EAX, return 0 or 1 in EAX ===
check_mine:
        cmp eax, 0
        jl .no
        cmp eax, MAP_SIZE
        jge .no
        movzx eax, byte [map_mines + eax]
        ret
.no:
        xor eax, eax
        ret

;=== Cursor index: return cell index in EAX ===
cursor_to_index:
        mov eax, [cursor_y]
        imul eax, MAP_W
        add eax, [cursor_x]
        ret

;=== Flood reveal from (EAX=x, EBX=y) - iterative with explicit stack ===
; Uses flood_stack[] array to avoid deep recursion
; Each entry is 2 dwords: [x, y]
FLOOD_STACK_MAX equ MAP_SIZE   ; max entries

flood_reveal:
        PUSHALL
        ; Initialize stack pointer
        mov dword [flood_sp], 0

        ; Push initial cell
        mov [flood_stack], eax           ; x
        mov [flood_stack + 4], ebx       ; y
        mov dword [flood_sp], 1

.flood_loop:
        ; Pop a cell from the stack
        cmp dword [flood_sp], 0
        je .flood_done
        dec dword [flood_sp]
        mov ecx, [flood_sp]
        mov eax, [flood_stack + ecx*8]       ; x
        mov ebx, [flood_stack + ecx*8 + 4]   ; y

        ; Bounds check
        cmp eax, 0
        jl .flood_loop
        cmp eax, MAP_W
        jge .flood_loop
        cmp ebx, 0
        jl .flood_loop
        cmp ebx, MAP_H
        jge .flood_loop

        ; Calculate index: idx = y * MAP_W + x
        push rax
        push rbx
        imul ebx, MAP_W
        add ebx, eax
        mov edx, ebx            ; EDX = index
        pop rbx
        pop rax

        ; Already revealed or flagged?
        cmp byte [map_visible + edx], 0
        jne .flood_loop

        ; Mark revealed
        mov byte [map_visible + edx], 1
        inc dword [cells_revealed]

        ; Is it a numbered cell? Don't expand further
        cmp byte [map_unveiled + edx], 0
        jne .flood_loop

        ; Empty cell: push all 8 neighbors
        ; Check stack space
        mov ecx, [flood_sp]
        add ecx, 8
        cmp ecx, FLOOD_STACK_MAX
        jg .flood_loop          ; Stack full, skip expansion

        ; Push neighbors (up, down, left, right, 4 diagonals)
        mov ecx, [flood_sp]

        ; Up (x, y-1)
        mov [flood_stack + ecx*8], eax
        mov esi, ebx
        dec esi
        mov [flood_stack + ecx*8 + 4], esi
        inc ecx

        ; Down (x, y+1)
        mov [flood_stack + ecx*8], eax
        mov esi, ebx
        inc esi
        mov [flood_stack + ecx*8 + 4], esi
        inc ecx

        ; Left (x-1, y)
        mov esi, eax
        dec esi
        mov [flood_stack + ecx*8], esi
        mov [flood_stack + ecx*8 + 4], ebx
        inc ecx

        ; Right (x+1, y)
        mov esi, eax
        inc esi
        mov [flood_stack + ecx*8], esi
        mov [flood_stack + ecx*8 + 4], ebx
        inc ecx

        ; Up-Left (x-1, y-1)
        mov esi, eax
        dec esi
        mov [flood_stack + ecx*8], esi
        mov esi, ebx
        dec esi
        mov [flood_stack + ecx*8 + 4], esi
        inc ecx

        ; Up-Right (x+1, y-1)
        mov esi, eax
        inc esi
        mov [flood_stack + ecx*8], esi
        mov esi, ebx
        dec esi
        mov [flood_stack + ecx*8 + 4], esi
        inc ecx

        ; Down-Left (x-1, y+1)
        mov esi, eax
        dec esi
        mov [flood_stack + ecx*8], esi
        mov esi, ebx
        inc esi
        mov [flood_stack + ecx*8 + 4], esi
        inc ecx

        ; Down-Right (x+1, y+1)
        mov esi, eax
        inc esi
        mov [flood_stack + ecx*8], esi
        mov esi, ebx
        inc esi
        mov [flood_stack + ecx*8 + 4], esi
        inc ecx

        mov [flood_sp], ecx
        jmp .flood_loop

.flood_done:
        POPALL
        ret

;=== Reveal all mines (game over) ===
reveal_all_mines:
        PUSHALL
        xor ecx, ecx
.loop:
        cmp ecx, MAP_SIZE
        jge .done
        cmp byte [map_unveiled + ecx], CELL_MINE
        jne .skip
        mov byte [map_visible + ecx], 1
.skip:
        inc ecx
        jmp .loop
.done:
        POPALL
        ret

;=== Draw header ===
draw_header:
        PUSHALL
        mov eax, SYS_SETCURSOR
        mov ebx, MAP_OFFSET_X
        xor ecx, ecx
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COL_HEADER
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_title
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, MAP_OFFSET_X
        mov ecx, 1
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_controls
        int 0x80
        POPALL
        ret

;=== Draw the entire field ===
draw_field:
        PUSHALL
        xor ecx, ecx           ; cell index
.loop:
        cmp ecx, MAP_SIZE
        jge .done

        ; Get x,y from index
        mov eax, ecx
        xor edx, edx
        mov ebx, MAP_W
        div ebx                 ; eax=row, edx=col

        push rcx
        mov ebx, eax            ; row -> y
        mov eax, edx            ; col -> x

        ; Screen position
        add eax, MAP_OFFSET_X
        add ebx, MAP_OFFSET_Y

        pop rcx
        push rcx

        ; Determine what to display
        cmp byte [map_visible + ecx], 1
        je .revealed
        cmp byte [map_visible + ecx], 2
        je .flagged

        ; Hidden cell
        mov cl, 0xB0            ; ░ shaded block
        mov ch, COL_HIDDEN
        jmp .draw

.flagged:
        mov cl, 'F'
        mov ch, COL_FLAG
        jmp .draw

.revealed:
        movzx edx, byte [map_unveiled + ecx]
        cmp edx, CELL_MINE
        je .show_mine
        cmp edx, 0
        je .show_empty
        ; Number 1-8
        add dl, '0'
        mov cl, dl
        ; Color based on number
        sub edx, ('0' + 1)
        movzx edx, byte [num_colors + edx]
        mov ch, dl
        jmp .draw

.show_mine:
        mov cl, '*'
        mov ch, COL_MINE
        jmp .draw

.show_empty:
        mov cl, ' '
        mov ch, COL_REVEALED
        jmp .draw

.draw:
        ; EAX=screen_x, EBX=screen_y already set above
        call vga_putchar_at
        pop rcx
        inc ecx
        jmp .loop
.done:
        POPALL
        ret

;=== Draw cursor (highlight current cell) ===
draw_cursor:
        PUSHALL
        mov ecx, [cursor_y]
        imul ecx, MAP_W
        add ecx, [cursor_x]

        mov eax, [cursor_x]
        add eax, MAP_OFFSET_X
        mov ebx, [cursor_y]
        add ebx, MAP_OFFSET_Y

        ; Get current char at this cell
        cmp byte [map_visible + ecx], 1
        je .rev_cursor
        cmp byte [map_visible + ecx], 2
        je .flag_cursor

        ; Hidden
        mov cl, 0xB0
        mov ch, COL_CURSOR
        jmp .put
.rev_cursor:
        movzx edx, byte [map_unveiled + ecx]
        cmp edx, 0
        je .empty_cursor
        cmp edx, CELL_MINE
        je .mine_cursor
        add dl, '0'
        mov cl, dl
        mov ch, COL_CURSOR
        jmp .put
.empty_cursor:
        mov cl, ' '
        mov ch, COL_CURSOR
        jmp .put
.mine_cursor:
        mov cl, '*'
        mov ch, COL_CURSOR
        jmp .put
.flag_cursor:
        mov cl, 'F'
        mov ch, COL_CURSOR
.put:
        call vga_putchar_at
        POPALL
        ret

;=== Erase cursor (redraw cell normally) ===
erase_cursor:
        PUSHALL
        ; Just redraw the cell at current cursor pos
        mov ecx, [cursor_y]
        imul ecx, MAP_W
        add ecx, [cursor_x]

        mov eax, [cursor_x]
        add eax, MAP_OFFSET_X
        mov ebx, [cursor_y]
        add ebx, MAP_OFFSET_Y

        cmp byte [map_visible + ecx], 1
        je .rev
        cmp byte [map_visible + ecx], 2
        je .flg

        mov cl, 0xB0
        mov ch, COL_HIDDEN
        jmp .put
.rev:
        movzx edx, byte [map_unveiled + ecx]
        cmp edx, CELL_MINE
        je .mine
        cmp edx, 0
        je .empty
        add dl, '0'
        mov cl, dl
        sub edx, ('0' + 1)
        movzx edx, byte [num_colors + edx]
        mov ch, dl
        jmp .put
.mine:
        mov cl, '*'
        mov ch, COL_MINE
        jmp .put
.empty:
        mov cl, ' '
        mov ch, COL_REVEALED
        jmp .put
.flg:
        mov cl, 'F'
        mov ch, COL_FLAG
.put:
        call vga_putchar_at
        POPALL
        ret

;=== Draw cell at cursor position ===
draw_cell_at_cursor:
        call erase_cursor
        ret

;=== Draw restart message ===
draw_restart_msg:
        PUSHALL
        mov eax, SYS_SETCURSOR
        mov ebx, MAP_OFFSET_X + MAP_W / 2 - 14
        mov ecx, MAP_OFFSET_Y + MAP_H + 1
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_restart
        int 0x80
        POPALL
        ret

exit_game:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

;=== Put char at screen position (direct VGA) ===
; EAX = x, EBX = y, CL = char, CH = color
vga_putchar_at:
        PUSHALL
        imul ebx, VGA_WIDTH * 2
        lea edi, [VGA_BASE + ebx + eax*2]
        mov [edi], cl
        mov [edi+1], ch
        POPALL
        ret

;=== Simple PRNG ===
rand:
        push rbx
        push rcx
        mov eax, [rand_seed]
        imul eax, 1103515245
        add eax, 12345
        mov [rand_seed], eax
        shr eax, 16
        and eax, 0x7FFF
        pop rcx
        pop rbx
        ret

;=== Data ===
msg_title:      db "MINESWEEPER - Mellivora OS", 0
msg_controls:   db "Arrows/WASD:Move  Space:Reveal  F:Flag  ESC:Quit", 0
msg_gameover:   db "  GAME OVER  ", 0
msg_win:        db "  YOU WIN!  ", 0
msg_restart:    db "Press R to restart, ESC to quit", 0

;=== BSS ===
rand_seed:      dd 0
cursor_x:       dd 0
cursor_y:       dd 0
mines_total:    dd 0
safe_cells:     dd 0
cells_revealed: dd 0
game_active:    db 0
map_mines:      times MAP_SIZE db 0
map_unveiled:   times MAP_SIZE db 0
map_visible:    times MAP_SIZE db 0
flood_sp:       dd 0
flood_stack:    times FLOOD_STACK_MAX * 2 dd 0   ; Each entry = 2 dwords (x, y)
