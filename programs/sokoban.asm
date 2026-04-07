; sokoban.asm - Sokoban puzzle game for Mellivora OS
; Converted from bootsector sokoban (Public Domain) by Ish
; 32-bit protected mode, uses INT 0x80 syscalls + direct VGA
%include "syscalls.inc"

; Tile types (bitfield)
TILE_EMPTY      equ 0x00       ; 0000
TILE_SPOT       equ 0x01       ; 0001
TILE_BRICK      equ 0x02       ; 0010
TILE_BRICK_SPOT equ 0x03       ; 0011
TILE_WALL       equ 0x04       ; 0100
TILE_PLAYER     equ 0x08       ; 1000
TILE_PLAYER_SPOT equ 0x09      ; 1001

; Display characters and colors for each tile type (char, color pairs)
; Index by tile type (0-9)
display_chars:
        db ' ', 0x07            ; 0: empty
        db 0xFA, 0x06           ; 1: spot (middle dot, brown)
        db 0xFE, 0x0C           ; 2: brick (square, bright red)
        db 0xFE, 0x0A           ; 3: brick on spot (square, bright green)
        db 0xDB, 0x71           ; 4: wall (full block, white bg blue fg)
        db ' ', 0x07            ; 5: unused
        db ' ', 0x07            ; 6: unused
        db ' ', 0x07            ; 7: unused
        db 0x02, 0x0F           ; 8: player (smiley, bright white)
        db 0x02, 0x0F           ; 9: player on spot

; Level data format: width, height, player_x, player_y, then tiles (compressed: 2 tiles per byte)
; Level 1 (14x10) - from original sokoban
level1:
        db 14, 10               ; width, height
        dw 63                   ; player position (linear index)
        db 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x00
        db 0x41, 0x10, 0x04, 0x00, 0x00, 0x04, 0x44
        db 0x41, 0x10, 0x04, 0x02, 0x00, 0x20, 0x04
        db 0x41, 0x10, 0x04, 0x24, 0x44, 0x40, 0x04
        db 0x41, 0x10, 0x00, 0x08, 0x04, 0x40, 0x04
        db 0x41, 0x10, 0x04, 0x04, 0x00, 0x20, 0x44
        db 0x44, 0x44, 0x44, 0x04, 0x42, 0x02, 0x04
        db 0x00, 0x40, 0x20, 0x02, 0x02, 0x02, 0x04
        db 0x00, 0x40, 0x00, 0x04, 0x00, 0x00, 0x04
        db 0x00, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44

; Level 2 (9x7) - simple level
level2:
        db 9, 7                 ; width, height
        dw 32                   ; player position
        db 0x44, 0x44, 0x40, 0x00, 0x04   ; row 0 (9 tiles = 5 bytes, last byte has 1 tile + padding)
        db 0x40, 0x00, 0x40, 0x00, 0x04
        db 0x40, 0x02, 0x04, 0x40, 0x04
        db 0x40, 0x24, 0x19, 0x44, 0x04   ; note: contains player (8) at pos 32
        db 0x44, 0x00, 0x31, 0x20, 0x04
        db 0x04, 0x00, 0x00, 0x00, 0x04
        db 0x04, 0x44, 0x44, 0x44, 0x04

; Level 3 (8x6) - easier level
level3:
        db 8, 6
        dw 9                    ; player position
        db 0x44, 0x44, 0x44, 0x44         ; row 0
        db 0x48, 0x00, 0x00, 0x04         ; row 1 (player at pos 9 -> col1)
        db 0x40, 0x20, 0x10, 0x04         ; row 2
        db 0x40, 0x04, 0x20, 0x04         ; row 3
        db 0x40, 0x01, 0x00, 0x04         ; row 4
        db 0x44, 0x44, 0x44, 0x44         ; row 5

; Level table
level_table:
        dd level3               ; Level 1 (easiest)
        dd level2               ; Level 2
        dd level1               ; Level 3 (hardest)
NUM_LEVELS      equ 3

start:
        mov dword [current_level], 0
        mov dword [moves], 0

load_level:
        ; Get level pointer
        mov eax, [current_level]
        mov esi, [level_table + eax*4]

        ; Read width, height
        movzx eax, byte [esi]
        mov [level_w], eax
        movzx eax, byte [esi + 1]
        mov [level_h], eax
        movzx eax, word [esi + 2]
        mov [player_pos], eax
        add esi, 4              ; skip header

        ; Calculate level size
        mov eax, [level_w]
        imul eax, [level_h]
        mov [level_size], eax

        ; Decompress level into current_map
        mov edi, current_map
        mov ecx, eax            ; total tiles
.decompress:
        cmp ecx, 0
        jle .decompress_done
        movzx eax, byte [esi]

        ; High nibble first
        mov edx, eax
        shr edx, 4
        mov [edi], dl
        inc edi
        dec ecx
        cmp ecx, 0
        jle .decompress_next

        ; Low nibble
        and eax, 0x0F
        mov [edi], al
        inc edi
        dec ecx

.decompress_next:
        inc esi
        jmp .decompress
.decompress_done:

        mov dword [moves], 0

        ; Draw
        mov eax, SYS_CLEAR
        int 0x80
        call draw_header
        call draw_level
        jmp game_loop

;=== Main game loop ===
game_loop:
        mov eax, SYS_GETCHAR
        int 0x80

        cmp al, 27              ; ESC
        je exit_game
        cmp al, 'q'
        je exit_game
        cmp al, 'r'
        je .restart_level

        ; Movement
        cmp al, KEY_UP
        je .try_up
        cmp al, 'w'
        je .try_up
        cmp al, KEY_DOWN
        je .try_down
        cmp al, 's'
        je .try_down
        cmp al, KEY_LEFT
        je .try_left
        cmp al, 'a'
        je .try_left
        cmp al, KEY_RIGHT
        je .try_right
        cmp al, 'd'
        je .try_right

        jmp game_loop

.restart_level:
        jmp load_level

.try_up:
        mov eax, [level_w]
        neg eax
        call try_move
        jmp .after_move

.try_down:
        mov eax, [level_w]
        call try_move
        jmp .after_move

.try_left:
        mov eax, -1
        call try_move
        jmp .after_move

.try_right:
        mov eax, 1
        call try_move

.after_move:
        call draw_level
        call check_win
        cmp eax, 1
        je .level_complete
        jmp game_loop

.level_complete:
        ; Show win message
        mov eax, SYS_SETCURSOR
        mov ebx, 30
        mov ecx, 1
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x2F           ; white on green
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_level_complete
        int 0x80

        ; Next level or game complete
        inc dword [current_level]
        mov eax, [current_level]
        cmp eax, NUM_LEVELS
        jge .all_complete

        ; Wait for key
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 27
        je exit_game

        jmp load_level

.all_complete:
        mov eax, SYS_SETCURSOR
        mov ebx, 25
        mov ecx, 2
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_all_complete
        int 0x80

        ; Wait for key then exit
        mov eax, SYS_GETCHAR
        int 0x80
        jmp exit_game

;=== Try to move player by offset EAX ===
try_move:
        pushad
        mov [move_offset], eax

        ; Calculate destination position
        mov ebx, [player_pos]
        add ebx, eax            ; dest = player + offset

        ; Bounds check
        cmp ebx, 0
        jl .cant_move
        cmp ebx, [level_size]
        jge .cant_move

        ; Get tile at destination
        movzx ecx, byte [current_map + ebx]

        ; Wall?
        cmp ecx, TILE_WALL
        je .cant_move

        ; Brick?
        test ecx, TILE_BRICK
        jz .just_move           ; no brick, just move

        ; Try pushing brick
        mov edx, ebx
        add edx, [move_offset] ; position beyond brick

        ; Bounds check for push destination
        cmp edx, 0
        jl .cant_move
        cmp edx, [level_size]
        jge .cant_move

        ; Check push destination tile
        movzx ecx, byte [current_map + edx]
        test ecx, 0x0E         ; wall (4) or brick (2) or player (8)?
        jnz .cant_move          ; can't push there

        ; Push the brick!
        ; Add brick bit at push destination
        or byte [current_map + edx], TILE_BRICK

        ; Remove brick bit at brick's old position (where player moves to)
        and byte [current_map + ebx], ~TILE_BRICK  ; 0xFD

.just_move:
        ; Remove player bit from old position
        mov ecx, [player_pos]
        and byte [current_map + ecx], ~TILE_PLAYER ; 0xF7

        ; Add player bit at new position
        or byte [current_map + ebx], TILE_PLAYER

        ; Update player position
        mov [player_pos], ebx
        inc dword [moves]

.cant_move:
        popad
        ret

;=== Check win: return EAX=1 if all bricks are on spots ===
check_win:
        push ebx
        push ecx
        xor ebx, ebx           ; count of bricks NOT on spots
        xor ecx, ecx
.loop:
        cmp ecx, [level_size]
        jge .done
        cmp byte [current_map + ecx], TILE_BRICK
        jne .next
        inc ebx                ; found a brick not on a spot
.next:
        inc ecx
        jmp .loop
.done:
        xor eax, eax
        test ebx, ebx
        jnz .not_won
        mov eax, 1
.not_won:
        pop ecx
        pop ebx
        ret

;=== Draw header ===
draw_header:
        pushad
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

        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, 1
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_controls
        int 0x80

        ; Level number
        mov eax, SYS_SETCURSOR
        mov ebx, 60
        xor ecx, ecx
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_level
        int 0x80
        mov eax, [current_level]
        inc eax
        call print_dec

        mov eax, SYS_PUTCHAR
        mov ebx, '/'
        int 0x80
        mov eax, NUM_LEVELS
        call print_dec
        popad
        ret

;=== Draw the level centered on screen ===
draw_level:
        pushad

        ; Calculate offset to center
        mov eax, VGA_WIDTH
        sub eax, [level_w]
        shr eax, 1
        mov [draw_off_x], eax

        mov eax, VGA_HEIGHT
        sub eax, [level_h]
        shr eax, 1
        mov [draw_off_y], eax

        xor esi, esi            ; tile index
.loop:
        cmp esi, [level_size]
        jge .done

        ; Calculate x, y from index
        mov eax, esi
        xor edx, edx
        div dword [level_w]     ; eax=row, edx=col

        mov ebx, eax
        add ebx, [draw_off_y]  ; screen y
        mov eax, edx
        add eax, [draw_off_x]  ; screen x

        ; Look up display char and color
        movzx edx, byte [current_map + esi]
        cmp edx, 9
        jg .skip
        shl edx, 1              ; *2 for char,color pair
        push eax
        push ebx
        movzx ecx, byte [display_chars + edx]
        movzx edx, byte [display_chars + edx + 1]
        pop ebx
        pop eax
        mov ch, dl              ; color
        call vga_putchar_at

.skip:
        inc esi
        jmp .loop
.done:
        popad
        ret

;=== Put char at screen position (direct VGA) ===
; EAX = x, EBX = y, CL = char, CH = color
vga_putchar_at:
        pushad
        imul ebx, VGA_WIDTH * 2
        lea edi, [VGA_BASE + ebx + eax*2]
        mov [edi], cl
        mov [edi+1], ch
        popad
        ret

exit_game:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

;=== Data ===
msg_title:          db "SOKOBAN - Mellivora OS", 0
msg_controls:       db "Arrows/WASD:Move  R:Restart  ESC:Quit", 0
msg_level:          db "Level ", 0
msg_level_complete: db " LEVEL COMPLETE! Press any key... ", 0
msg_all_complete:   db "Congratulations! All levels complete!", 0

;=== BSS ===
current_level:  dd 0
level_w:        dd 0
level_h:        dd 0
level_size:     dd 0
player_pos:     dd 0
moves:          dd 0
move_offset:    dd 0
draw_off_x:     dd 0
draw_off_y:     dd 0
current_map:    times 256 db 0  ; max 256 tiles
