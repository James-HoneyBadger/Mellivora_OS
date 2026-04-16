; rogue.asm - ASCII Roguelike Dungeon Crawler for Mellivora OS
; Explore procedurally generated dungeons, fight monsters, collect loot.
; Arrow keys to move, 'q' to quit, '?' for help.
%include "syscalls.inc"

; Map constants
MAP_W           equ 80
MAP_H           equ 23          ; Rows 0-22 for map, 23 for stats, 24 for messages
MAX_ROOMS       equ 9
MAX_MONSTERS    equ 20
MAX_ITEMS       equ 12
ROOM_MIN_W      equ 5
ROOM_MAX_W      equ 14
ROOM_MIN_H      equ 3
ROOM_MAX_H      equ 7

; Tile types
TILE_VOID       equ 0
TILE_WALL       equ 1
TILE_FLOOR      equ 2
TILE_DOOR       equ 3
TILE_CORRIDOR   equ 4
TILE_STAIRS     equ 5

; Monster types
MON_NONE        equ 0
MON_RAT         equ 1
MON_BAT         equ 2
MON_SNAKE       equ 3
MON_GOBLIN      equ 4
MON_ORC         equ 5
MON_TROLL       equ 6

; Item types
ITEM_NONE       equ 0
ITEM_POTION     equ 1
ITEM_GOLD       equ 2
ITEM_SWORD      equ 3
ITEM_ARMOR      equ 4

start:
        ; Seed random
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_state], eax

        ; Init player
        mov dword [player_hp], 20
        mov dword [player_max_hp], 20
        mov dword [player_atk], 3
        mov dword [player_def], 1
        mov dword [player_gold], 0
        mov dword [player_level], 1
        mov dword [player_xp], 0
        mov dword [depth], 1

        call generate_dungeon
        call full_redraw

;=== Main game loop ===
.game_loop:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .idle

        cmp al, 'q'
        je .quit
        cmp al, 27
        je .quit

        ; Movement
        cmp al, KEY_UP
        je .move_up
        cmp al, KEY_DOWN
        je .move_down
        cmp al, KEY_LEFT
        je .move_left
        cmp al, KEY_RIGHT
        je .move_right
        cmp al, 'k'
        je .move_up
        cmp al, 'j'
        je .move_down
        cmp al, 'h'
        je .move_left
        cmp al, 'l'
        je .move_right

        ; Stairs
        cmp al, '>'
        je .try_descend
        cmp al, '?'
        je .show_help

        jmp .idle

.move_up:
        xor ebx, ebx
        mov ecx, -1
        jmp .do_move
.move_down:
        xor ebx, ebx
        mov ecx, 1
        jmp .do_move
.move_left:
        mov ebx, -1
        xor ecx, ecx
        jmp .do_move
.move_right:
        mov ebx, 1
        xor ecx, ecx
        ; fall through
.do_move:
        ; EBX=dx, ECX=dy
        mov eax, [player_x]
        add eax, ebx
        mov edx, [player_y]
        add edx, ecx

        ; Bounds check
        cmp eax, 0
        jl .idle
        cmp eax, MAP_W
        jge .idle
        cmp edx, 0
        jl .idle
        cmp edx, MAP_H
        jge .idle

        ; Check for monster at (eax,edx)
        push rax
        push rdx
        call find_monster_at    ; EAX=idx or -1
        cmp eax, -1
        jne .combat
        pop rdx
        pop rax

        ; Check tile walkability
        push rax
        push rdx
        imul edx, MAP_W
        add edx, eax
        movzx eax, byte [map + edx]
        cmp al, TILE_WALL
        je .blocked
        cmp al, TILE_VOID
        je .blocked

        pop rdx
        pop rax
        mov [player_x], eax
        mov [player_y], edx

        ; Check for items at new position
        call pickup_item

        call full_redraw
        jmp .idle

.blocked:
        pop rdx
        pop rax
        jmp .idle

.combat:
        ; EAX = monster index
        pop rdx                 ; discard
        pop rdx
        call attack_monster
        call full_redraw

        ; Check if player died
        cmp dword [player_hp], 0
        jle .death
        jmp .idle

.try_descend:
        ; Check if on stairs
        mov eax, [player_y]
        imul eax, MAP_W
        add eax, [player_x]
        movzx eax, byte [map + eax]
        cmp al, TILE_STAIRS
        jne .idle
        inc dword [depth]
        call generate_dungeon
        call set_message
        db "You descend deeper...", 0
        call full_redraw
        jmp .idle

.show_help:
        call draw_help
        ; Wait for key
.help_wait:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .help_wait
        call full_redraw
        jmp .idle

.death:
        call draw_death
.death_wait:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .death_wait
        jmp .quit

.idle:
        mov eax, SYS_SLEEP
        mov ebx, 5
        int 0x80
        jmp .game_loop

.quit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================
; Generate dungeon level
;=======================================
generate_dungeon:
        PUSHALL

        ; Clear map to void
        mov edi, map
        mov ecx, MAP_W * MAP_H
        xor al, al
        rep stosb

        ; Clear monsters
        mov edi, mon_type
        mov ecx, MAX_MONSTERS
        xor al, al
        rep stosb

        ; Clear items
        mov edi, item_type
        mov ecx, MAX_ITEMS
        xor al, al
        rep stosb

        mov dword [room_count], 0
        mov dword [msg_buf], 0

        ; Generate rooms
        xor esi, esi            ; attempts
.gen_rooms:
        cmp dword [room_count], MAX_ROOMS
        jge .gen_corridors
        cmp esi, 50
        jge .gen_corridors
        inc esi

        ; Random room dimensions
        call rand
        xor edx, edx
        mov ecx, ROOM_MAX_W - ROOM_MIN_W + 1
        div ecx
        add edx, ROOM_MIN_W
        mov [tmp_w], edx

        call rand
        xor edx, edx
        mov ecx, ROOM_MAX_H - ROOM_MIN_H + 1
        div ecx
        add edx, ROOM_MIN_H
        mov [tmp_h], edx

        ; Random position
        call rand
        xor edx, edx
        mov ecx, MAP_W
        sub ecx, [tmp_w]
        sub ecx, 2
        cmp ecx, 1
        jle .gen_rooms
        div ecx
        inc edx
        mov [tmp_x], edx

        call rand
        xor edx, edx
        mov ecx, MAP_H
        sub ecx, [tmp_h]
        sub ecx, 2
        cmp ecx, 1
        jle .gen_rooms
        div ecx
        inc edx
        mov [tmp_y], edx

        ; Check overlap
        call check_room_overlap
        test eax, eax
        jnz .gen_rooms

        ; Place room
        call place_room
        jmp .gen_rooms

.gen_corridors:
        ; Connect rooms with corridors
        cmp dword [room_count], 2
        jl .gen_populate

        mov esi, 1              ; start from room 1
.corridor_loop:
        cmp esi, [room_count]
        jge .gen_populate

        ; Connect room[esi-1] to room[esi]
        mov eax, esi
        dec eax
        call get_room_center    ; returns (EAX, EDX) = center of room eax
        mov [tmp_x], eax
        mov [tmp_y], edx

        mov eax, esi
        call get_room_center
        mov [tmp_w], eax        ; reuse as x2
        mov [tmp_h], edx        ; reuse as y2

        ; Dig L-shaped corridor
        call dig_corridor

        inc esi
        jmp .corridor_loop

.gen_populate:
        ; Place player in first room
        mov eax, 0
        call get_room_center
        mov [player_x], eax
        mov [player_y], edx

        ; Place stairs in last room
        mov eax, [room_count]
        dec eax
        cmp eax, 0
        jl .skip_stairs
        call get_room_center
        imul edx, MAP_W
        add edx, eax
        mov byte [map + edx], TILE_STAIRS
.skip_stairs:

        ; Place monsters
        call populate_monsters

        ; Place items
        call populate_items

        POPALL
        ret

;---------------------------------------
; place_room - Carve walls+floor into map
;---------------------------------------
place_room:
        PUSHALL
        mov eax, [room_count]
        shl eax, 4             ; *16 for room record
        lea edi, [rooms + eax]
        mov eax, [tmp_x]
        mov [edi], eax          ; rx
        mov eax, [tmp_y]
        mov [edi + 4], eax      ; ry
        mov eax, [tmp_w]
        mov [edi + 8], eax      ; rw
        mov eax, [tmp_h]
        mov [edi + 12], eax     ; rh
        inc dword [room_count]

        ; Carve floor
        mov ecx, [tmp_y]
.pr_row:
        mov edx, [tmp_y]
        add edx, [tmp_h]
        cmp ecx, edx
        jge .pr_done
        mov ebx, [tmp_x]
.pr_col:
        mov edx, [tmp_x]
        add edx, [tmp_w]
        cmp ebx, edx
        jge .pr_next_row

        ; Edge = wall, interior = floor
        push rdx
        mov eax, ecx
        cmp eax, [tmp_y]
        je .pr_wall
        mov edx, [tmp_y]
        add edx, [tmp_h]
        dec edx
        cmp eax, edx
        je .pr_wall
        cmp ebx, [tmp_x]
        je .pr_wall
        mov edx, [tmp_x]
        add edx, [tmp_w]
        dec edx
        cmp ebx, edx
        je .pr_wall

        ; Interior - floor
        mov eax, ecx
        imul eax, MAP_W
        add eax, ebx
        mov byte [map + eax], TILE_FLOOR
        pop rdx
        jmp .pr_next_col
.pr_wall:
        mov eax, ecx
        imul eax, MAP_W
        add eax, ebx
        cmp byte [map + eax], TILE_FLOOR
        je .pr_skip_wall        ; Don't overwrite floor
        cmp byte [map + eax], TILE_CORRIDOR
        je .pr_skip_wall
        mov byte [map + eax], TILE_WALL
.pr_skip_wall:
        pop rdx
.pr_next_col:
        inc ebx
        jmp .pr_col
.pr_next_row:
        inc ecx
        jmp .pr_row
.pr_done:
        POPALL
        ret

;---------------------------------------
; check_room_overlap - returns EAX=1 if overlapping
;---------------------------------------
check_room_overlap:
        push rbx
        push rcx
        push rdx
        push rsi

        xor esi, esi
.co_loop:
        cmp esi, [room_count]
        jge .co_ok

        mov eax, esi
        shl eax, 4
        lea ebx, [rooms + eax]

        ; Check X overlap (with 1 cell margin)
        mov eax, [tmp_x]
        dec eax
        mov ecx, [ebx + 8]     ; existing rw
        add ecx, [ebx]         ; existing rx + rw
        inc ecx
        cmp eax, ecx
        jge .co_next

        mov eax, [tmp_x]
        add eax, [tmp_w]
        inc eax
        cmp eax, [ebx]
        jle .co_next

        ; Check Y overlap
        mov eax, [tmp_y]
        dec eax
        mov ecx, [ebx + 12]
        add ecx, [ebx + 4]
        inc ecx
        cmp eax, ecx
        jge .co_next

        mov eax, [tmp_y]
        add eax, [tmp_h]
        inc eax
        cmp eax, [ebx + 4]
        jle .co_next

        ; Overlapping
        mov eax, 1
        jmp .co_ret

.co_next:
        inc esi
        jmp .co_loop
.co_ok:
        xor eax, eax
.co_ret:
        pop rsi
        pop rdx
        pop rcx
        pop rbx
        ret

;---------------------------------------
; get_room_center: EAX=room_index -> EAX=cx, EDX=cy
;---------------------------------------
get_room_center:
        push rbx
        shl eax, 4
        lea ebx, [rooms + eax]
        mov eax, [ebx]          ; rx
        mov edx, [ebx + 8]      ; rw
        shr edx, 1
        add eax, edx
        mov edx, [ebx + 4]      ; ry
        push rax
        mov eax, [ebx + 12]     ; rh
        shr eax, 1
        add edx, eax
        pop rax
        pop rbx
        ret

;---------------------------------------
; dig_corridor: connects (tmp_x,tmp_y) to (tmp_w,tmp_h)
;---------------------------------------
dig_corridor:
        PUSHALL
        ; Horizontal first, then vertical
        mov ebx, [tmp_x]
        mov ecx, [tmp_y]
.dc_hloop:
        cmp ebx, [tmp_w]
        je .dc_vert
        ; Dig
        mov eax, ecx
        imul eax, MAP_W
        add eax, ebx
        cmp byte [map + eax], TILE_VOID
        jne .dc_hskip
        mov byte [map + eax], TILE_CORRIDOR
.dc_hskip:
        cmp byte [map + eax], TILE_WALL
        jne .dc_hskip2
        mov byte [map + eax], TILE_DOOR
.dc_hskip2:
        cmp ebx, [tmp_w]
        jl .dc_hinc
        dec ebx
        jmp .dc_hloop
.dc_hinc:
        inc ebx
        jmp .dc_hloop

.dc_vert:
        mov ebx, [tmp_w]
.dc_vloop:
        cmp ecx, [tmp_h]
        je .dc_done
        mov eax, ecx
        imul eax, MAP_W
        add eax, ebx
        cmp byte [map + eax], TILE_VOID
        jne .dc_vskip
        mov byte [map + eax], TILE_CORRIDOR
.dc_vskip:
        cmp byte [map + eax], TILE_WALL
        jne .dc_vskip2
        mov byte [map + eax], TILE_DOOR
.dc_vskip2:
        cmp ecx, [tmp_h]
        jl .dc_vinc
        dec ecx
        jmp .dc_vloop
.dc_vinc:
        inc ecx
        jmp .dc_vloop
.dc_done:
        POPALL
        ret

;---------------------------------------
; populate_monsters
;---------------------------------------
populate_monsters:
        PUSHALL
        xor esi, esi            ; monster slot
        mov edi, [depth]        ; difficulty scales with depth

        ; Place 3 + depth monsters (up to MAX)
        mov ecx, 3
        add ecx, edi
        cmp ecx, MAX_MONSTERS
        jle .pm_count_ok
        mov ecx, MAX_MONSTERS
.pm_count_ok:
        mov [tmp_x], ecx        ; count to place

.pm_loop:
        cmp dword [tmp_x], 0
        jle .pm_done
        cmp esi, MAX_MONSTERS
        jge .pm_done

        ; Random floor position
        call rand_floor_pos     ; EAX=x, EDX=y
        cmp eax, -1
        je .pm_done

        ; Don't place on player
        cmp eax, [player_x]
        jne .pm_place
        cmp edx, [player_y]
        je .pm_loop
.pm_place:
        mov [mon_x + esi*4], eax
        mov [mon_y + esi*4], edx

        ; Random monster type scaled by depth
        push rsi
        call rand
        xor edx, edx
        mov ecx, edi            ; max type = depth (capped at 6)
        cmp ecx, 6
        jle .pm_type_ok
        mov ecx, 6
.pm_type_ok:
        inc ecx
        div ecx
        inc edx
        pop rsi
        mov [mon_type + esi], dl

        ; HP = type * 3 + depth
        movzx eax, dl
        imul eax, 3
        add eax, edi
        mov [mon_hp + esi*4], eax

        inc esi
        dec dword [tmp_x]
        jmp .pm_loop
.pm_done:
        POPALL
        ret

;---------------------------------------
; populate_items
;---------------------------------------
populate_items:
        PUSHALL
        xor esi, esi
        mov ecx, 5              ; 5 items per level
.pi_loop:
        cmp ecx, 0
        jle .pi_done
        cmp esi, MAX_ITEMS
        jge .pi_done

        call rand_floor_pos
        cmp eax, -1
        je .pi_done

        mov [item_x + esi*4], eax
        mov [item_y + esi*4], edx

        ; Random item type
        push rcx
        call rand
        xor edx, edx
        mov ecx, 4
        div ecx
        inc edx
        pop rcx
        mov [item_type + esi], dl

        inc esi
        dec ecx
        jmp .pi_loop
.pi_done:
        POPALL
        ret

;---------------------------------------
; rand_floor_pos: find random floor tile -> EAX=x, EDX=y (or EAX=-1 if fail)
;---------------------------------------
rand_floor_pos:
        push rcx
        push rbx
        mov ecx, 100            ; attempts
.rfp_loop:
        dec ecx
        js .rfp_fail

        call rand
        xor edx, edx
        push rcx
        mov ecx, MAP_W * MAP_H
        div ecx
        pop rcx
        ; EDX = offset
        movzx eax, byte [map + edx]
        cmp al, TILE_FLOOR
        jne .rfp_loop

        ; Convert offset to x,y
        mov eax, edx
        xor edx, edx
        push rcx
        mov ecx, MAP_W
        div ecx
        pop rcx
        ; EAX=y, EDX=x
        xchg eax, edx
        pop rbx
        pop rcx
        ret
.rfp_fail:
        mov eax, -1
        pop rbx
        pop rcx
        ret

;=======================================
; Combat
;=======================================
attack_monster:
        ; EAX = monster index
        PUSHALL
        mov esi, eax

        ; Player attacks
        mov eax, [player_atk]
        call rand
        xor edx, edx
        mov ecx, 3
        div ecx                 ; 0-2 random bonus
        add eax, [player_atk]
        mov ebx, eax            ; damage

        sub [mon_hp + esi*4], ebx

        ; Set attack message
        call set_message
        db "You strike! ", 0

        cmp dword [mon_hp + esi*4], 0
        jg .mon_alive

        ; Monster killed
        mov byte [mon_type + esi], MON_NONE
        ; Give XP
        movzx eax, byte [mon_type + esi]
        add eax, 2
        imul eax, [depth]
        add [player_xp], eax
        ; Check level up (every 20 XP)
        mov eax, [player_xp]
        xor edx, edx
        mov ecx, 20
        div ecx
        test edx, edx
        jnz .am_done
        cmp eax, [player_level]
        jle .am_done
        inc dword [player_level]
        add dword [player_max_hp], 5
        mov eax, [player_max_hp]
        mov [player_hp], eax
        inc dword [player_atk]
        jmp .am_done

.mon_alive:
        ; Monster counter-attacks
        movzx eax, byte [mon_type + esi]
        inc eax                 ; base damage
        add eax, [depth]
        sub eax, [player_def]
        cmp eax, 1
        jge .dam_ok
        mov eax, 1
.dam_ok:
        sub [player_hp], eax

.am_done:
        POPALL
        ret

;---------------------------------------
; find_monster_at: (EAX=x, EDX=y) -> EAX=index or -1
;---------------------------------------
find_monster_at:
        push rcx
        push rbx
        xor ecx, ecx
.fma_loop:
        cmp ecx, MAX_MONSTERS
        jge .fma_none
        cmp byte [mon_type + ecx], MON_NONE
        je .fma_next
        cmp [mon_x + ecx*4], eax
        jne .fma_next
        cmp [mon_y + ecx*4], edx
        jne .fma_next
        mov eax, ecx
        pop rbx
        pop rcx
        ret
.fma_next:
        inc ecx
        jmp .fma_loop
.fma_none:
        mov eax, -1
        pop rbx
        pop rcx
        ret

;---------------------------------------
; pickup_item
;---------------------------------------
pickup_item:
        PUSHALL
        xor ecx, ecx
.pu_loop:
        cmp ecx, MAX_ITEMS
        jge .pu_done
        cmp byte [item_type + ecx], ITEM_NONE
        je .pu_next
        mov eax, [player_x]
        cmp [item_x + ecx*4], eax
        jne .pu_next
        mov eax, [player_y]
        cmp [item_y + ecx*4], eax
        jne .pu_next

        ; Found item
        movzx eax, byte [item_type + ecx]
        mov byte [item_type + ecx], ITEM_NONE

        cmp al, ITEM_POTION
        je .pu_potion
        cmp al, ITEM_GOLD
        je .pu_gold
        cmp al, ITEM_SWORD
        je .pu_sword
        cmp al, ITEM_ARMOR
        je .pu_armor
        jmp .pu_next

.pu_potion:
        add dword [player_hp], 8
        mov eax, [player_max_hp]
        cmp [player_hp], eax
        jle .pu_next
        mov [player_hp], eax
        jmp .pu_next
.pu_gold:
        add dword [player_gold], 10
        jmp .pu_next
.pu_sword:
        inc dword [player_atk]
        jmp .pu_next
.pu_armor:
        inc dword [player_def]
.pu_next:
        inc ecx
        jmp .pu_loop
.pu_done:
        POPALL
        ret

;=======================================
; Rendering
;=======================================
full_redraw:
        PUSHALL
        mov eax, SYS_CLEAR
        int 0x80

        ; Draw map tiles
        xor ecx, ecx           ; row
.draw_row:
        cmp ecx, MAP_H
        jge .draw_entities

        xor ebx, ebx           ; col
.draw_col:
        cmp ebx, MAP_W
        jge .draw_next_row

        ; Compute FOV (simple distance check from player)
        mov eax, ebx
        sub eax, [player_x]
        imul eax, eax
        mov edx, ecx
        sub edx, [player_y]
        imul edx, edx
        add eax, edx
        cmp eax, 100            ; radius ~10
        jg .draw_dark

        ; Get tile
        mov eax, ecx
        imul eax, MAP_W
        add eax, ebx

        movzx edx, byte [map + eax]
        cmp dl, TILE_VOID
        je .draw_dark
        cmp dl, TILE_WALL
        je .draw_wall
        cmp dl, TILE_FLOOR
        je .draw_floor
        cmp dl, TILE_DOOR
        je .draw_door
        cmp dl, TILE_CORRIDOR
        je .draw_corr
        cmp dl, TILE_STAIRS
        je .draw_stairs
        jmp .draw_dark

.draw_wall:
        mov al, '#'
        mov ah, 0x08            ; Dark gray
        jmp .draw_put
.draw_floor:
        mov al, 0xFA            ; Middle dot
        mov ah, 0x07            ; Gray
        jmp .draw_put
.draw_door:
        mov al, '+'
        mov ah, 0x06            ; Brown
        jmp .draw_put
.draw_corr:
        mov al, 0xFA
        mov ah, 0x07
        jmp .draw_put
.draw_stairs:
        mov al, '>'
        mov ah, 0x0E            ; Yellow
        jmp .draw_put
.draw_dark:
        mov al, ' '
        mov ah, 0x00
.draw_put:
        ; Write directly to VGA
        push rcx
        push rbx
        imul ecx, MAP_W
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        mov [ecx], ax
        pop rbx
        pop rcx
        inc ebx
        jmp .draw_col

.draw_next_row:
        inc ecx
        jmp .draw_row

.draw_entities:
        ; Draw items
        xor esi, esi
.de_items:
        cmp esi, MAX_ITEMS
        jge .de_mons
        cmp byte [item_type + esi], ITEM_NONE
        je .de_inext

        ; Check FOV
        mov eax, [item_x + esi*4]
        sub eax, [player_x]
        imul eax, eax
        mov edx, [item_y + esi*4]
        sub edx, [player_y]
        imul edx, edx
        add eax, edx
        cmp eax, 100
        jg .de_inext

        ; Draw item
        movzx eax, byte [item_type + esi]
        cmp al, ITEM_POTION
        je .draw_potion
        cmp al, ITEM_GOLD
        je .draw_gold_i
        cmp al, ITEM_SWORD
        je .draw_sword_i
        cmp al, ITEM_ARMOR
        je .draw_armor_i
        jmp .de_inext

.draw_potion:
        mov al, '!'
        mov ah, 0x0D            ; Light magenta
        jmp .de_iput
.draw_gold_i:
        mov al, '$'
        mov ah, 0x0E            ; Yellow
        jmp .de_iput
.draw_sword_i:
        mov al, '/'
        mov ah, 0x0F            ; White
        jmp .de_iput
.draw_armor_i:
        mov al, '['
        mov ah, 0x03            ; Cyan
.de_iput:
        push rcx
        mov ecx, [item_y + esi*4]
        imul ecx, MAP_W
        add ecx, [item_x + esi*4]
        shl ecx, 1
        add ecx, VGA_BASE
        mov [ecx], ax
        pop rcx
.de_inext:
        inc esi
        jmp .de_items

.de_mons:
        ; Draw monsters
        xor esi, esi
.de_mloop:
        cmp esi, MAX_MONSTERS
        jge .de_player
        cmp byte [mon_type + esi], MON_NONE
        je .de_mnext

        ; FOV check
        mov eax, [mon_x + esi*4]
        sub eax, [player_x]
        imul eax, eax
        mov edx, [mon_y + esi*4]
        sub edx, [player_y]
        imul edx, edx
        add eax, edx
        cmp eax, 100
        jg .de_mnext

        ; Monster glyph
        movzx eax, byte [mon_type + esi]
        mov ah, 0x04            ; Red
        cmp al, MON_RAT
        je .mg_rat
        cmp al, MON_BAT
        je .mg_bat
        cmp al, MON_SNAKE
        je .mg_snake
        cmp al, MON_GOBLIN
        je .mg_gob
        cmp al, MON_ORC
        je .mg_orc
        cmp al, MON_TROLL
        je .mg_troll
        jmp .de_mnext
.mg_rat:    mov al, 'r' & 0xFF
            mov ah, 0x06
            jmp .de_mput
.mg_bat:    mov al, 'b'
            mov ah, 0x05
            jmp .de_mput
.mg_snake:  mov al, 's'
            mov ah, 0x02
            jmp .de_mput
.mg_gob:    mov al, 'g'
            mov ah, 0x0A
            jmp .de_mput
.mg_orc:    mov al, 'o'
            mov ah, 0x0C
            jmp .de_mput
.mg_troll:  mov al, 'T'
            mov ah, 0x04

.de_mput:
        push rcx
        mov ecx, [mon_y + esi*4]
        imul ecx, MAP_W
        add ecx, [mon_x + esi*4]
        shl ecx, 1
        add ecx, VGA_BASE
        mov [ecx], ax
        pop rcx
.de_mnext:
        inc esi
        jmp .de_mloop

.de_player:
        ; Draw player '@' bright white on dark
        mov al, '@'
        mov ah, 0x0F
        mov ecx, [player_y]
        imul ecx, MAP_W
        add ecx, [player_x]
        shl ecx, 1
        add ecx, VGA_BASE
        mov [ecx], ax

        ; === Status bar (row 23) ===
        mov eax, SYS_SETCOLOR
        mov ebx, 0x70           ; Black on gray
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, 23
        int 0x80

        ; HP bar
        mov eax, SYS_PRINT
        mov ebx, str_hp
        int 0x80
        mov eax, [player_hp]
        call print_num
        mov eax, SYS_PUTCHAR
        mov ebx, '/'
        int 0x80
        mov eax, [player_max_hp]
        call print_num
        mov eax, SYS_PRINT
        mov ebx, str_space
        int 0x80

        ; ATK
        mov eax, SYS_PRINT
        mov ebx, str_atk
        int 0x80
        mov eax, [player_atk]
        call print_num
        mov eax, SYS_PRINT
        mov ebx, str_space
        int 0x80

        ; DEF
        mov eax, SYS_PRINT
        mov ebx, str_def
        int 0x80
        mov eax, [player_def]
        call print_num
        mov eax, SYS_PRINT
        mov ebx, str_space
        int 0x80

        ; Gold
        mov eax, SYS_PRINT
        mov ebx, str_gold
        int 0x80
        mov eax, [player_gold]
        call print_num
        mov eax, SYS_PRINT
        mov ebx, str_space
        int 0x80

        ; Level
        mov eax, SYS_PRINT
        mov ebx, str_level
        int 0x80
        mov eax, [player_level]
        call print_num
        mov eax, SYS_PRINT
        mov ebx, str_space
        int 0x80

        ; Depth
        mov eax, SYS_PRINT
        mov ebx, str_depth
        int 0x80
        mov eax, [depth]
        call print_num

        ; Pad rest of row
        mov eax, SYS_PRINT
        mov ebx, str_pad
        int 0x80

        ; Message line (row 24)
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E           ; Yellow
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, 24
        int 0x80
        cmp byte [msg_buf], 0
        je .no_msg
        mov eax, SYS_PRINT
        mov ebx, msg_buf
        int 0x80
.no_msg:
        POPALL
        ret

;---------------------------------------
; draw_help
;---------------------------------------
draw_help:
        PUSHALL
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 20
        mov ecx, 3
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, help_title
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B
        int 0x80
        mov esi, help_lines
        mov ecx, 5
.hl:    push rcx
        mov eax, SYS_SETCURSOR
        mov ebx, 15
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
.hl_scan:
        lodsb
        test al, al
        jnz .hl_scan
        pop rcx
        inc ecx
        cmp ecx, 18
        jl .hl
        POPALL
        ret

;---------------------------------------
; draw_death
;---------------------------------------
draw_death:
        PUSHALL
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x4F
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 25
        mov ecx, 10
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, death_str1
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 20
        mov ecx, 12
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, death_str2
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 18
        mov ecx, 14
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x4E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, death_str3
        int 0x80
        mov eax, [depth]
        call print_num
        mov eax, SYS_PRINT
        mov ebx, death_str4
        int 0x80
        mov eax, [player_gold]
        call print_num
        mov eax, SYS_PRINT
        mov ebx, death_str5
        int 0x80
        POPALL
        ret

;---------------------------------------
; set_message - inline string after CALL
;---------------------------------------
set_message:
        pop rsi                 ; return addr = string ptr
        mov edi, msg_buf
.sm_copy:
        lodsb
        stosb
        test al, al
        jnz .sm_copy
        push rsi                ; fixup return to after string
        ret

;---------------------------------------
; print_num - print EAX decimal
;---------------------------------------
print_num:
        PUSHALL
        test eax, eax
        jnz .pn_nz
        mov eax, SYS_PUTCHAR
        mov ebx, '0'
        int 0x80
        POPALL
        ret
.pn_nz:
        xor ecx, ecx
        mov ebx, 10
.pn_push:
        test eax, eax
        jz .pn_pop
        xor edx, edx
        div ebx
        push rdx
        inc ecx
        jmp .pn_push
.pn_pop:
        test ecx, ecx
        jz .pn_done
        pop rbx
        add ebx, '0'
        mov eax, SYS_PUTCHAR
        int 0x80
        dec ecx
        jmp .pn_pop
.pn_done:
        POPALL
        ret

;---------------------------------------
; rand - LFSR PRNG -> EAX
;---------------------------------------
rand:
        mov eax, [rand_state]
        imul eax, 1103515245
        add eax, 12345
        mov [rand_state], eax
        shr eax, 16
        and eax, 0x7FFF
        ret

; === Strings ===
str_hp:     db " HP:", 0
str_atk:    db "Atk:", 0
str_def:    db "Def:", 0
str_gold:   db "Au:", 0
str_level:  db "Lv:", 0
str_depth:  db "Depth:", 0
str_space:  db " ", 0
str_pad:    db "                        ", 0

help_title: db "=== ROGUE - Help ===", 0
help_lines:
        db "Arrow Keys / hjkl   Move", 0
        db ">                    Descend stairs", 0
        db "?                    This help screen", 0
        db "q / ESC              Quit", 0
        db "", 0
        db "Symbols:", 0
        db "  @  You          #  Wall", 0
        db "  >  Stairs       +  Door", 0
        db "  !  Potion       $  Gold", 0
        db "  /  Sword        [  Armor", 0
        db "  r  Rat          b  Bat", 0
        db "  s  Snake        g  Goblin", 0
        db "  o  Orc          T  Troll", 0

death_str1: db "*** YOU HAVE DIED ***", 0
death_str2: db "The dungeon claims another soul.", 0
death_str3: db "Reached depth ", 0
death_str4: db " with ", 0
death_str5: db " gold. Press any key.", 0

; === BSS ===
msg_buf:        times 80 db 0
rand_state:     dd 0
player_x:       dd 0
player_y:       dd 0
player_hp:      dd 0
player_max_hp:  dd 0
player_atk:     dd 0
player_def:     dd 0
player_gold:    dd 0
player_level:   dd 0
player_xp:      dd 0
depth:          dd 0
room_count:     dd 0
rooms:          times MAX_ROOMS * 16 db 0  ; 4 dwords per room: x,y,w,h
tmp_x:          dd 0
tmp_y:          dd 0
tmp_w:          dd 0
tmp_h:          dd 0
total_secs:     dd 0
map:            times MAP_W * MAP_H db 0
mon_type:       times MAX_MONSTERS db 0
mon_x:          times MAX_MONSTERS dd 0
mon_y:          times MAX_MONSTERS dd 0
mon_hp:         times MAX_MONSTERS dd 0
item_type:      times MAX_ITEMS db 0
item_x:         times MAX_ITEMS dd 0
item_y:         times MAX_ITEMS dd 0
