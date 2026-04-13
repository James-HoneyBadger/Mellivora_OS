; adventure.asm - Interactive Fiction Engine for Mellivora OS
; A dungeon exploration text adventure with verb-noun parser.
; Explore rooms, collect items, solve puzzles, defeat enemies.

%include "syscalls.inc"

; Colors
C_DEFAULT   equ 0x07
C_TITLE     equ 0x0E
C_DESC      equ 0x03
C_PROMPT    equ 0x0A
C_ERROR     equ 0x0C
C_ITEM      equ 0x0D
C_COMBAT    equ 0x04
C_SUCCESS   equ 0x02

; Game limits
MAX_ROOMS       equ 16
MAX_ITEMS       equ 16
MAX_INVENTORY   equ 8

; Room flags
ROOM_VISITED    equ 0x01
ROOM_DARK       equ 0x02
ROOM_LOCKED     equ 0x04

; Item flags
ITEM_TAKEABLE   equ 0x01
ITEM_INROOM     equ 0x02       ; Placed in a room
ITEM_CARRIED    equ 0x04       ; In player inventory
ITEM_HIDDEN     equ 0x08

; Direction indices
DIR_NORTH       equ 0
DIR_SOUTH       equ 1
DIR_EAST        equ 2
DIR_WEST        equ 3
DIR_UP          equ 4
DIR_DOWN        equ 5

; Room struct (52 bytes each)
; +0:  dd description_ptr
; +4:  dd name_ptr
; +8:  db flags
; +9:  db reserved[3]
; +12: dd exits[6] (N,S,E,W,Up,Down) - room indices, -1 = no exit
; +36: dd on_enter_ptr  (event handler, 0 = none)
; +40: dd extra_desc_ptr (0 = none)
; +44: dd extra_data     (keycard needed, etc.)
; +48: dd reserved2
ROOM_SIZE       equ 52

; Item struct (24 bytes each)
; +0:  dd name_ptr
; +4:  dd description_ptr
; +8:  db flags
; +9:  db room_id        (which room, if INROOM)
; +10: db reserved[2]
; +12: dd use_handler_ptr (0 = no special use)
; +16: dd examine_ptr     (detailed description)
; +20: dd reserved2
ITEM_SIZE       equ 24

; Verb IDs
VERB_UNKNOWN    equ 0
VERB_GO         equ 1
VERB_LOOK       equ 2
VERB_TAKE       equ 3
VERB_DROP       equ 4
VERB_USE        equ 5
VERB_EXAMINE    equ 6
VERB_INVENTORY  equ 7
VERB_HELP       equ 8
VERB_QUIT       equ 9
VERB_ATTACK     equ 10
VERB_OPEN       equ 11
VERB_NORTH      equ 12
VERB_SOUTH      equ 13
VERB_EAST       equ 14
VERB_WEST       equ 15
VERB_UP         equ 16
VERB_DOWN       equ 17
VERB_SAVE       equ 18

; Player state
HP_MAX          equ 100
HP_START        equ 100

start:
        ; Clear screen
        mov eax, SYS_CLEAR
        int 0x80

        ; Title screen
        mov al, C_TITLE
        mov [color], al
        call set_color
        mov esi, str_title
        call print_str

        mov al, C_DEFAULT
        mov [color], al
        call set_color
        mov esi, str_intro
        call print_str

        ; Wait for keypress
        mov esi, str_press_key
        call print_str
        call wait_key

        ; Initialize game state
        call game_init

        ; Show first room
        call describe_room

game_loop:
        ; Show prompt
        mov al, C_PROMPT
        call set_color
        mov esi, str_prompt
        call print_str

        ; Read input
        mov al, C_DEFAULT
        call set_color
        call read_input

        ; Parse input
        call parse_input        ; Sets [verb_id] and [noun_ptr]

        ; Dispatch verb
        mov eax, [verb_id]

        cmp eax, VERB_QUIT
        je game_quit
        cmp eax, VERB_LOOK
        je do_look
        cmp eax, VERB_GO
        je do_go
        cmp eax, VERB_NORTH
        je do_north
        cmp eax, VERB_SOUTH
        je do_south
        cmp eax, VERB_EAST
        je do_east
        cmp eax, VERB_WEST
        je do_west
        cmp eax, VERB_UP
        je do_up
        cmp eax, VERB_DOWN
        je do_down
        cmp eax, VERB_TAKE
        je do_take
        cmp eax, VERB_DROP
        je do_drop
        cmp eax, VERB_INVENTORY
        je do_inventory
        cmp eax, VERB_EXAMINE
        je do_examine
        cmp eax, VERB_USE
        je do_use
        cmp eax, VERB_ATTACK
        je do_attack
        cmp eax, VERB_OPEN
        je do_open
        cmp eax, VERB_HELP
        je do_help
        ; Unknown verb
        mov al, C_ERROR
        call set_color
        mov esi, str_unknown
        call print_str
        jmp game_loop

game_quit:
        mov al, C_TITLE
        call set_color
        mov esi, str_goodbye
        call print_str
        mov eax, SYS_EXIT
        int 0x80

;=======================================================================
; VERB HANDLERS
;=======================================================================

do_look:
        call describe_room
        jmp game_loop

do_north:
        mov dword [go_dir], DIR_NORTH
        jmp do_go_dir
do_south:
        mov dword [go_dir], DIR_SOUTH
        jmp do_go_dir
do_east:
        mov dword [go_dir], DIR_EAST
        jmp do_go_dir
do_west:
        mov dword [go_dir], DIR_WEST
        jmp do_go_dir
do_up:
        mov dword [go_dir], DIR_UP
        jmp do_go_dir
do_down:
        mov dword [go_dir], DIR_DOWN
        jmp do_go_dir

do_go:
        ; Parse direction from noun
        mov esi, [noun_ptr]
        cmp esi, 0
        je .go_where
        call parse_direction    ; Returns direction in EAX, or -1
        cmp eax, -1
        je .go_where
        mov [go_dir], eax
        jmp do_go_dir
.go_where:
        mov al, C_ERROR
        call set_color
        mov esi, str_go_where
        call print_str
        jmp game_loop

do_go_dir:
        ; Get current room exits
        mov eax, [player_room]
        imul eax, ROOM_SIZE
        add eax, rooms
        mov ebx, [go_dir]
        mov eax, [eax + 12 + ebx*4]    ; exits[dir]
        cmp eax, -1
        je .cant_go

        ; Check if the destination room is locked
        push eax
        imul eax, ROOM_SIZE
        add eax, rooms
        test byte [eax + 8], ROOM_LOCKED
        pop eax
        jnz .locked

        ; Move player
        mov [player_room], eax
        ; Mark as visited
        push eax
        imul eax, ROOM_SIZE
        add eax, rooms
        or byte [eax + 8], ROOM_VISITED
        pop eax

        ; Check on_enter event
        push eax
        imul eax, ROOM_SIZE
        add eax, rooms
        mov ebx, [eax + 36]    ; on_enter_ptr
        pop eax
        cmp ebx, 0
        je .no_event
        call ebx
.no_event:
        call describe_room
        jmp game_loop

.cant_go:
        mov al, C_ERROR
        call set_color
        mov esi, str_cant_go
        call print_str
        jmp game_loop

.locked:
        mov al, C_ERROR
        call set_color
        mov esi, str_locked
        call print_str
        jmp game_loop

do_take:
        mov esi, [noun_ptr]
        cmp esi, 0
        je .take_what
        ; Find item in current room matching noun
        call find_room_item     ; EAX = item index, or -1
        cmp eax, -1
        je .take_not_here
        ; Check if takeable
        imul ebx, eax, ITEM_SIZE
        add ebx, items
        test byte [ebx + 8], ITEM_TAKEABLE
        jz .take_cant
        ; Check inventory space
        cmp dword [inv_count], MAX_INVENTORY
        jge .take_full
        ; Move to inventory
        and byte [ebx + 8], ~ITEM_INROOM
        or byte [ebx + 8], ITEM_CARRIED
        inc dword [inv_count]
        mov al, C_SUCCESS
        call set_color
        mov esi, str_taken
        call print_str
        jmp game_loop
.take_what:
        mov al, C_ERROR
        call set_color
        mov esi, str_take_what
        call print_str
        jmp game_loop
.take_not_here:
        mov al, C_ERROR
        call set_color
        mov esi, str_not_here
        call print_str
        jmp game_loop
.take_cant:
        mov al, C_ERROR
        call set_color
        mov esi, str_cant_take
        call print_str
        jmp game_loop
.take_full:
        mov al, C_ERROR
        call set_color
        mov esi, str_inv_full
        call print_str
        jmp game_loop

do_drop:
        mov esi, [noun_ptr]
        cmp esi, 0
        je .drop_what
        ; Find item in inventory matching noun
        call find_inv_item      ; EAX = item index, or -1
        cmp eax, -1
        je .drop_not_have
        ; Move to current room
        imul ebx, eax, ITEM_SIZE
        add ebx, items
        and byte [ebx + 8], ~ITEM_CARRIED
        or byte [ebx + 8], ITEM_INROOM
        mov ecx, [player_room]
        mov [ebx + 9], cl
        dec dword [inv_count]
        mov al, C_SUCCESS
        call set_color
        mov esi, str_dropped
        call print_str
        jmp game_loop
.drop_what:
        mov al, C_ERROR
        call set_color
        mov esi, str_drop_what
        call print_str
        jmp game_loop
.drop_not_have:
        mov al, C_ERROR
        call set_color
        mov esi, str_dont_have
        call print_str
        jmp game_loop

do_inventory:
        mov al, C_ITEM
        call set_color
        mov esi, str_inv_header
        call print_str
        cmp dword [inv_count], 0
        je .inv_empty
        ; List carried items
        xor ecx, ecx
        mov esi, items
.inv_loop:
        cmp ecx, MAX_ITEMS
        jge .inv_done
        test byte [esi + 8], ITEM_CARRIED
        jz .inv_next
        ; Print item name
        push ecx
        push esi
        mov esi, str_bullet
        call print_str
        pop esi
        push esi
        mov esi, [esi + 0]     ; name_ptr
        call print_str
        mov esi, str_newline
        call print_str
        pop esi
        pop ecx
.inv_next:
        add esi, ITEM_SIZE
        inc ecx
        jmp .inv_loop
.inv_empty:
        mov esi, str_inv_empty
        call print_str
.inv_done:
        jmp game_loop

do_examine:
        mov esi, [noun_ptr]
        cmp esi, 0
        je .exam_what
        ; Check inventory first, then room
        call find_inv_item
        cmp eax, -1
        jne .exam_found
        call find_room_item
        cmp eax, -1
        je .exam_not_here
.exam_found:
        imul ebx, eax, ITEM_SIZE
        add ebx, items
        mov esi, [ebx + 16]    ; examine_ptr
        cmp esi, 0
        je .exam_generic
        mov al, C_DESC
        call set_color
        call print_str
        jmp game_loop
.exam_generic:
        mov al, C_DESC
        call set_color
        mov esi, [ebx + 4]     ; description_ptr
        call print_str
        jmp game_loop
.exam_what:
        mov al, C_ERROR
        call set_color
        mov esi, str_examine_what
        call print_str
        jmp game_loop
.exam_not_here:
        mov al, C_ERROR
        call set_color
        mov esi, str_not_here
        call print_str
        jmp game_loop

do_use:
        mov esi, [noun_ptr]
        cmp esi, 0
        je .use_what
        ; Must have item in inventory
        call find_inv_item
        cmp eax, -1
        je .use_not_have
        ; Check use handler
        imul ebx, eax, ITEM_SIZE
        add ebx, items
        mov ecx, [ebx + 12]    ; use_handler_ptr
        cmp ecx, 0
        je .use_nothing
        push eax
        call ecx
        pop eax
        jmp game_loop
.use_what:
        mov al, C_ERROR
        call set_color
        mov esi, str_use_what
        call print_str
        jmp game_loop
.use_not_have:
        mov al, C_ERROR
        call set_color
        mov esi, str_dont_have
        call print_str
        jmp game_loop
.use_nothing:
        mov al, C_DESC
        call set_color
        mov esi, str_use_nothing
        call print_str
        jmp game_loop

do_attack:
        ; Check if monster is in current room
        cmp dword [monster_room], -1
        je .atk_nothing
        mov eax, [player_room]
        cmp eax, [monster_room]
        jne .atk_nothing

        ; Check if player has sword
        mov dword [.atk_has_sword], 0
        xor ecx, ecx
        mov esi, items
.atk_check:
        cmp ecx, MAX_ITEMS
        jge .atk_no_sword
        test byte [esi + 8], ITEM_CARRIED
        jz .atk_check_next
        ; Check if item name matches "sword"
        push ecx
        push esi
        mov esi, [esi + 0]
        mov edi, str_sword_name
        call str_equal
        pop esi
        pop ecx
        cmp eax, 1
        je .atk_has_it
.atk_check_next:
        add esi, ITEM_SIZE
        inc ecx
        jmp .atk_check
.atk_has_it:
        mov dword [.atk_has_sword], 1
.atk_no_sword:
        cmp dword [.atk_has_sword], 1
        jne .atk_bare

        ; Attack with sword — always kills
        mov al, C_COMBAT
        call set_color
        mov esi, str_atk_sword
        call print_str
        mov dword [monster_room], -1    ; Monster dead
        mov dword [monster_hp], 0

        ; Check win condition (monster was guarding treasure room)
        mov eax, 10                     ; Treasure room
        imul eax, ROOM_SIZE
        add eax, rooms
        and byte [eax + 8], ~ROOM_LOCKED ; Unlock treasure room
        mov al, C_SUCCESS
        call set_color
        mov esi, str_path_clear
        call print_str
        jmp game_loop

.atk_bare:
        ; Bare-handed — take damage
        mov al, C_COMBAT
        call set_color
        mov esi, str_atk_bare
        call print_str
        sub dword [player_hp], 25
        cmp dword [player_hp], 0
        jle game_death
        ; Show HP
        mov esi, str_hp_left
        call print_str
        mov eax, [player_hp]
        call print_number
        mov esi, str_hp_suffix
        call print_str
        jmp game_loop

.atk_nothing:
        mov al, C_DESC
        call set_color
        mov esi, str_atk_nothing
        call print_str
        jmp game_loop

.atk_has_sword: dd 0

do_open:
        mov al, C_DESC
        call set_color
        mov esi, str_open_nothing
        call print_str
        jmp game_loop

do_help:
        mov al, C_TITLE
        call set_color
        mov esi, str_help
        call print_str
        jmp game_loop

game_death:
        mov al, C_COMBAT
        call set_color
        mov esi, str_death
        call print_str
        mov esi, str_press_key
        call print_str
        call wait_key
        mov eax, SYS_EXIT
        int 0x80

game_victory:
        mov al, C_TITLE
        call set_color
        mov esi, str_victory
        call print_str
        mov esi, str_press_key
        call print_str
        call wait_key
        mov eax, SYS_EXIT
        int 0x80

;=======================================================================
; ROOM DESCRIPTION
;=======================================================================

describe_room:
        pushad
        ; Print room name
        mov al, C_TITLE
        call set_color
        mov esi, str_newline
        call print_str
        mov eax, [player_room]
        imul eax, ROOM_SIZE
        add eax, rooms
        mov ebp, eax
        mov esi, str_divider
        call print_str
        mov esi, [ebp + 4]     ; name_ptr
        call print_str
        mov esi, str_newline
        call print_str
        mov esi, str_divider
        call print_str

        ; Print description
        mov al, C_DESC
        call set_color
        mov esi, [ebp + 0]     ; description_ptr
        call print_str

        ; Print extra description if any
        mov esi, [ebp + 40]
        cmp esi, 0
        je .dr_items
        call print_str

.dr_items:
        ; List items in room
        mov al, C_ITEM
        call set_color
        xor ecx, ecx
        mov esi, items
        mov dword [.dr_found_items], 0
.dr_item_loop:
        cmp ecx, MAX_ITEMS
        jge .dr_monster
        test byte [esi + 8], ITEM_INROOM
        jz .dr_item_next
        test byte [esi + 8], ITEM_HIDDEN
        jnz .dr_item_next
        movzx eax, byte [esi + 9]
        cmp eax, [player_room]
        jne .dr_item_next
        ; First item? Print header
        cmp dword [.dr_found_items], 0
        jne .dr_item_print
        push ecx
        push esi
        mov esi, str_items_here
        call print_str
        pop esi
        pop ecx
        mov dword [.dr_found_items], 1
.dr_item_print:
        push ecx
        push esi
        mov esi, str_bullet
        call print_str
        pop esi
        push esi
        mov esi, [esi + 0]     ; name_ptr
        call print_str
        mov esi, str_newline
        call print_str
        pop esi
        pop ecx
.dr_item_next:
        add esi, ITEM_SIZE
        inc ecx
        jmp .dr_item_loop

.dr_monster:
        ; Show monster if present
        cmp dword [monster_room], -1
        je .dr_exits
        mov eax, [player_room]
        cmp eax, [monster_room]
        jne .dr_exits
        mov al, C_COMBAT
        call set_color
        mov esi, str_monster_here
        call print_str

.dr_exits:
        ; List exits
        mov al, C_PROMPT
        call set_color
        mov esi, str_exits
        call print_str
        mov eax, [player_room]
        imul eax, ROOM_SIZE
        add eax, rooms
        mov ebp, eax
        mov dword [.dr_first_exit], 1

        mov eax, [ebp + 12 + DIR_NORTH*4]
        cmp eax, -1
        je .dr_ex_s
        call .dr_sep
        mov esi, str_north
        call print_str
.dr_ex_s:
        mov eax, [ebp + 12 + DIR_SOUTH*4]
        cmp eax, -1
        je .dr_ex_e
        call .dr_sep
        mov esi, str_south
        call print_str
.dr_ex_e:
        mov eax, [ebp + 12 + DIR_EAST*4]
        cmp eax, -1
        je .dr_ex_w
        call .dr_sep
        mov esi, str_east
        call print_str
.dr_ex_w:
        mov eax, [ebp + 12 + DIR_WEST*4]
        cmp eax, -1
        je .dr_ex_u
        call .dr_sep
        mov esi, str_west
        call print_str
.dr_ex_u:
        mov eax, [ebp + 12 + DIR_UP*4]
        cmp eax, -1
        je .dr_ex_d
        call .dr_sep
        mov esi, str_up_dir
        call print_str
.dr_ex_d:
        mov eax, [ebp + 12 + DIR_DOWN*4]
        cmp eax, -1
        je .dr_ex_done
        call .dr_sep
        mov esi, str_down_dir
        call print_str
.dr_ex_done:
        mov esi, str_newline
        call print_str
        mov al, C_DEFAULT
        call set_color
        popad
        ret

.dr_sep:
        cmp dword [.dr_first_exit], 1
        je .dr_is_first
        push esi
        mov esi, str_comma
        call print_str
        pop esi
        ret
.dr_is_first:
        mov dword [.dr_first_exit], 0
        ret

.dr_found_items: dd 0
.dr_first_exit:  dd 0

;=======================================================================
; INPUT PARSING
;=======================================================================

read_input:
        pushad
        ; Read a line of text from keyboard
        mov edi, input_buf
        xor ecx, ecx           ; Character count
.ri_loop:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 0x0D           ; Enter
        je .ri_done
        cmp al, 0x0A           ; Newline
        je .ri_done
        cmp al, 0x08           ; Backspace
        je .ri_bs
        cmp ecx, 78            ; Max length
        jge .ri_loop
        ; Convert to lowercase
        cmp al, 'A'
        jl .ri_store
        cmp al, 'Z'
        jg .ri_store
        add al, 32
.ri_store:
        mov [edi + ecx], al
        inc ecx
        ; Echo character
        push ecx
        mov eax, SYS_PUTCHAR
        movzx ebx, al
        int 0x80
        pop ecx
        jmp .ri_loop
.ri_bs:
        cmp ecx, 0
        je .ri_loop
        dec ecx
        mov byte [edi + ecx], 0
        ; Echo backspace
        push ecx
        mov eax, SYS_PUTCHAR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x08
        int 0x80
        pop ecx
        jmp .ri_loop
.ri_done:
        mov byte [edi + ecx], 0
        ; Echo newline
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        popad
        ret

parse_input:
        pushad
        mov esi, input_buf
        mov dword [verb_id], VERB_UNKNOWN
        mov dword [noun_ptr], 0

        ; Skip leading spaces
.pi_skip:
        lodsb
        cmp al, ' '
        je .pi_skip
        cmp al, 0
        je .pi_done
        dec esi

        ; Extract first word into word_buf
        mov edi, word_buf
.pi_word:
        lodsb
        cmp al, ' '
        je .pi_word_done
        cmp al, 0
        je .pi_word_end
        stosb
        jmp .pi_word
.pi_word_done:
        mov byte [edi], 0
        ; Skip spaces to find noun
.pi_noun_skip:
        lodsb
        cmp al, ' '
        je .pi_noun_skip
        cmp al, 0
        je .pi_match
        dec esi
        mov [noun_ptr], esi     ; Point to start of noun text
        jmp .pi_match
.pi_word_end:
        mov byte [edi], 0

.pi_match:
        ; Match verb word against known verbs
        mov esi, word_buf

        mov edi, str_v_look
        call str_equal
        cmp eax, 1
        jne .pi_c1
        mov dword [verb_id], VERB_LOOK
        jmp .pi_done
.pi_c1:
        mov esi, word_buf
        mov edi, str_v_l
        call str_equal
        cmp eax, 1
        jne .pi_c1b
        mov dword [verb_id], VERB_LOOK
        jmp .pi_done
.pi_c1b:
        mov esi, word_buf
        mov edi, str_v_go
        call str_equal
        cmp eax, 1
        jne .pi_c2
        mov dword [verb_id], VERB_GO
        jmp .pi_done
.pi_c2:
        mov esi, word_buf
        mov edi, str_v_take
        call str_equal
        cmp eax, 1
        jne .pi_c2b
        mov dword [verb_id], VERB_TAKE
        jmp .pi_done
.pi_c2b:
        mov esi, word_buf
        mov edi, str_v_get
        call str_equal
        cmp eax, 1
        jne .pi_c3
        mov dword [verb_id], VERB_TAKE
        jmp .pi_done
.pi_c3:
        mov esi, word_buf
        mov edi, str_v_drop
        call str_equal
        cmp eax, 1
        jne .pi_c4
        mov dword [verb_id], VERB_DROP
        jmp .pi_done
.pi_c4:
        mov esi, word_buf
        mov edi, str_v_use
        call str_equal
        cmp eax, 1
        jne .pi_c5
        mov dword [verb_id], VERB_USE
        jmp .pi_done
.pi_c5:
        mov esi, word_buf
        mov edi, str_v_examine
        call str_equal
        cmp eax, 1
        jne .pi_c5b
        mov dword [verb_id], VERB_EXAMINE
        jmp .pi_done
.pi_c5b:
        mov esi, word_buf
        mov edi, str_v_x
        call str_equal
        cmp eax, 1
        jne .pi_c6
        mov dword [verb_id], VERB_EXAMINE
        jmp .pi_done
.pi_c6:
        mov esi, word_buf
        mov edi, str_v_inventory
        call str_equal
        cmp eax, 1
        jne .pi_c6b
        mov dword [verb_id], VERB_INVENTORY
        jmp .pi_done
.pi_c6b:
        mov esi, word_buf
        mov edi, str_v_i
        call str_equal
        cmp eax, 1
        jne .pi_c7
        mov dword [verb_id], VERB_INVENTORY
        jmp .pi_done
.pi_c7:
        mov esi, word_buf
        mov edi, str_v_help
        call str_equal
        cmp eax, 1
        jne .pi_c8
        mov dword [verb_id], VERB_HELP
        jmp .pi_done
.pi_c8:
        mov esi, word_buf
        mov edi, str_v_quit
        call str_equal
        cmp eax, 1
        jne .pi_c8b
        mov dword [verb_id], VERB_QUIT
        jmp .pi_done
.pi_c8b:
        mov esi, word_buf
        mov edi, str_v_exit
        call str_equal
        cmp eax, 1
        jne .pi_c9
        mov dword [verb_id], VERB_QUIT
        jmp .pi_done
.pi_c9:
        mov esi, word_buf
        mov edi, str_v_attack
        call str_equal
        cmp eax, 1
        jne .pi_c9b
        mov dword [verb_id], VERB_ATTACK
        jmp .pi_done
.pi_c9b:
        mov esi, word_buf
        mov edi, str_v_kill
        call str_equal
        cmp eax, 1
        jne .pi_c9c
        mov dword [verb_id], VERB_ATTACK
        jmp .pi_done
.pi_c9c:
        mov esi, word_buf
        mov edi, str_v_fight
        call str_equal
        cmp eax, 1
        jne .pi_c10
        mov dword [verb_id], VERB_ATTACK
        jmp .pi_done
.pi_c10:
        mov esi, word_buf
        mov edi, str_v_open
        call str_equal
        cmp eax, 1
        jne .pi_c11
        mov dword [verb_id], VERB_OPEN
        jmp .pi_done
.pi_c11:
        ; Direction words as verbs
        mov esi, word_buf
        mov edi, str_north
        call str_equal
        cmp eax, 1
        jne .pi_c12
        mov dword [verb_id], VERB_NORTH
        jmp .pi_done
.pi_c12:
        mov esi, word_buf
        mov edi, str_v_n
        call str_equal
        cmp eax, 1
        jne .pi_c13
        mov dword [verb_id], VERB_NORTH
        jmp .pi_done
.pi_c13:
        mov esi, word_buf
        mov edi, str_south
        call str_equal
        cmp eax, 1
        jne .pi_c14
        mov dword [verb_id], VERB_SOUTH
        jmp .pi_done
.pi_c14:
        mov esi, word_buf
        mov edi, str_v_s
        call str_equal
        cmp eax, 1
        jne .pi_c15
        mov dword [verb_id], VERB_SOUTH
        jmp .pi_done
.pi_c15:
        mov esi, word_buf
        mov edi, str_east
        call str_equal
        cmp eax, 1
        jne .pi_c16
        mov dword [verb_id], VERB_EAST
        jmp .pi_done
.pi_c16:
        mov esi, word_buf
        mov edi, str_v_e
        call str_equal
        cmp eax, 1
        jne .pi_c17
        mov dword [verb_id], VERB_EAST
        jmp .pi_done
.pi_c17:
        mov esi, word_buf
        mov edi, str_west
        call str_equal
        cmp eax, 1
        jne .pi_c18
        mov dword [verb_id], VERB_WEST
        jmp .pi_done
.pi_c18:
        mov esi, word_buf
        mov edi, str_v_w
        call str_equal
        cmp eax, 1
        jne .pi_c19
        mov dword [verb_id], VERB_WEST
        jmp .pi_done
.pi_c19:
        mov esi, word_buf
        mov edi, str_up_dir
        call str_equal
        cmp eax, 1
        jne .pi_c19b
        mov dword [verb_id], VERB_UP
        jmp .pi_done
.pi_c19b:
        mov esi, word_buf
        mov edi, str_v_u
        call str_equal
        cmp eax, 1
        jne .pi_c20
        mov dword [verb_id], VERB_UP
        jmp .pi_done
.pi_c20:
        mov esi, word_buf
        mov edi, str_down_dir
        call str_equal
        cmp eax, 1
        jne .pi_done
        mov dword [verb_id], VERB_DOWN
.pi_done:
        popad
        ret

; parse_direction - Parse direction from noun text
; ESI = text pointer
; Returns: EAX = direction index, or -1
parse_direction:
        push ebx
        push ecx
        push edi
        mov edi, str_north
        call str_equal
        cmp eax, 1
        je .pd_n
        mov edi, str_v_n
        call str_equal
        cmp eax, 1
        je .pd_n
        mov edi, str_south
        call str_equal
        cmp eax, 1
        je .pd_s
        mov edi, str_v_s
        call str_equal
        cmp eax, 1
        je .pd_s
        mov edi, str_east
        call str_equal
        cmp eax, 1
        je .pd_e
        mov edi, str_v_e
        call str_equal
        cmp eax, 1
        je .pd_e
        mov edi, str_west
        call str_equal
        cmp eax, 1
        je .pd_w
        mov edi, str_v_w
        call str_equal
        cmp eax, 1
        je .pd_w
        mov edi, str_up_dir
        call str_equal
        cmp eax, 1
        je .pd_u
        mov edi, str_v_u
        call str_equal
        cmp eax, 1
        je .pd_u
        mov edi, str_down_dir
        call str_equal
        cmp eax, 1
        je .pd_d
        mov eax, -1
        jmp .pd_ret
.pd_n:  mov eax, DIR_NORTH
        jmp .pd_ret
.pd_s:  mov eax, DIR_SOUTH
        jmp .pd_ret
.pd_e:  mov eax, DIR_EAST
        jmp .pd_ret
.pd_w:  mov eax, DIR_WEST
        jmp .pd_ret
.pd_u:  mov eax, DIR_UP
        jmp .pd_ret
.pd_d:  mov eax, DIR_DOWN
.pd_ret:
        pop edi
        pop ecx
        pop ebx
        ret

;=======================================================================
; ITEM SEARCH
;=======================================================================

; find_room_item - Find item in current room matching noun
; ESI = noun text; must be preserved across calls
; Returns: EAX = item index, or -1
find_room_item:
        push ebx
        push ecx
        push edx
        push edi
        mov edx, esi            ; Save noun ptr
        xor ecx, ecx
        mov ebx, items
.fri_loop:
        cmp ecx, MAX_ITEMS
        jge .fri_none
        test byte [ebx + 8], ITEM_INROOM
        jz .fri_next
        test byte [ebx + 8], ITEM_HIDDEN
        jnz .fri_next
        movzx eax, byte [ebx + 9]
        cmp eax, [player_room]
        jne .fri_next
        ; Compare name
        mov esi, edx
        mov edi, [ebx + 0]
        call str_equal
        cmp eax, 1
        je .fri_found
.fri_next:
        add ebx, ITEM_SIZE
        inc ecx
        jmp .fri_loop
.fri_found:
        mov eax, ecx
        jmp .fri_ret
.fri_none:
        mov eax, -1
.fri_ret:
        pop edi
        pop edx
        pop ecx
        pop ebx
        ret

; find_inv_item - Find item in player inventory matching noun
; ESI = noun text (from [noun_ptr])
; Returns: EAX = item index, or -1
find_inv_item:
        push ebx
        push ecx
        push edx
        push edi
        mov edx, esi
        xor ecx, ecx
        mov ebx, items
.fii_loop:
        cmp ecx, MAX_ITEMS
        jge .fii_none
        test byte [ebx + 8], ITEM_CARRIED
        jz .fii_next
        mov esi, edx
        mov edi, [ebx + 0]
        call str_equal
        cmp eax, 1
        je .fii_found
.fii_next:
        add ebx, ITEM_SIZE
        inc ecx
        jmp .fii_loop
.fii_found:
        mov eax, ecx
        jmp .fii_ret
.fii_none:
        mov eax, -1
.fii_ret:
        pop edi
        pop edx
        pop ecx
        pop ebx
        ret

;=======================================================================
; UTILITY FUNCTIONS
;=======================================================================

; str_equal - Compare two null-terminated strings (case-insensitive)
; ESI = string A, EDI = string B
; Returns: EAX = 1 if equal, 0 if not
str_equal:
        push ebx
        push ecx
        push esi
        push edi
.se_loop:
        lodsb
        mov cl, [edi]
        inc edi
        ; Convert both to lowercase
        cmp al, 'A'
        jl .se_no_lower1
        cmp al, 'Z'
        jg .se_no_lower1
        add al, 32
.se_no_lower1:
        cmp cl, 'A'
        jl .se_no_lower2
        cmp cl, 'Z'
        jg .se_no_lower2
        add cl, 32
.se_no_lower2:
        cmp al, cl
        jne .se_ne
        cmp al, 0
        je .se_eq
        jmp .se_loop
.se_eq:
        mov eax, 1
        jmp .se_ret
.se_ne:
        xor eax, eax
.se_ret:
        pop edi
        pop esi
        pop ecx
        pop ebx
        ret

; print_str - Print null-terminated string
; ESI = string pointer
print_str:
        pushad
        mov ebx, esi
        mov eax, SYS_PRINT
        int 0x80
        popad
        ret

; print_number - Print unsigned decimal number
; EAX = number
print_number:
        pushad
        mov ecx, 0              ; Digit count
        mov ebx, 10
.pn_div:
        xor edx, edx
        div ebx
        push edx
        inc ecx
        cmp eax, 0
        jne .pn_div
.pn_print:
        pop edx
        add dl, '0'
        push ecx
        mov eax, SYS_PUTCHAR
        movzx ebx, dl
        int 0x80
        pop ecx
        dec ecx
        jnz .pn_print
        popad
        ret

; set_color - Set text color
; AL = color byte
set_color:
        pushad
        movzx ebx, al
        mov eax, SYS_SETCOLOR
        int 0x80
        popad
        ret

; wait_key - Wait for a keypress
wait_key:
        pushad
        mov eax, SYS_GETCHAR
        int 0x80
        popad
        ret

;=======================================================================
; GAME INITIALIZATION
;=======================================================================

game_init:
        pushad
        ; Initialize player state
        mov dword [player_room], 0
        mov dword [player_hp], HP_START
        mov dword [inv_count], 0

        ; Initialize monster (skeleton warrior in room 7)
        mov dword [monster_room], 7
        mov dword [monster_hp], 50

        ; Initialize rooms
        ; Room 0: Dungeon Entrance
        mov esi, rooms
        mov dword [esi + 0], desc_r0
        mov dword [esi + 4], name_r0
        mov byte  [esi + 8], ROOM_VISITED
        mov dword [esi + 12 + DIR_NORTH*4], 1
        mov dword [esi + 12 + DIR_SOUTH*4], -1
        mov dword [esi + 12 + DIR_EAST*4], 2
        mov dword [esi + 12 + DIR_WEST*4], -1
        mov dword [esi + 12 + DIR_UP*4], -1
        mov dword [esi + 12 + DIR_DOWN*4], -1
        mov dword [esi + 36], 0
        mov dword [esi + 40], 0

        ; Room 1: Great Hall
        add esi, ROOM_SIZE
        mov dword [esi + 0], desc_r1
        mov dword [esi + 4], name_r1
        mov byte  [esi + 8], 0
        mov dword [esi + 12 + DIR_NORTH*4], 3
        mov dword [esi + 12 + DIR_SOUTH*4], 0
        mov dword [esi + 12 + DIR_EAST*4], 4
        mov dword [esi + 12 + DIR_WEST*4], 5
        mov dword [esi + 12 + DIR_UP*4], -1
        mov dword [esi + 12 + DIR_DOWN*4], -1
        mov dword [esi + 36], 0
        mov dword [esi + 40], 0

        ; Room 2: Guard Room
        add esi, ROOM_SIZE
        mov dword [esi + 0], desc_r2
        mov dword [esi + 4], name_r2
        mov byte  [esi + 8], 0
        mov dword [esi + 12 + DIR_NORTH*4], 4
        mov dword [esi + 12 + DIR_SOUTH*4], -1
        mov dword [esi + 12 + DIR_EAST*4], -1
        mov dword [esi + 12 + DIR_WEST*4], 0
        mov dword [esi + 12 + DIR_UP*4], -1
        mov dword [esi + 12 + DIR_DOWN*4], -1
        mov dword [esi + 36], 0
        mov dword [esi + 40], 0

        ; Room 3: Library
        add esi, ROOM_SIZE
        mov dword [esi + 0], desc_r3
        mov dword [esi + 4], name_r3
        mov byte  [esi + 8], 0
        mov dword [esi + 12 + DIR_NORTH*4], -1
        mov dword [esi + 12 + DIR_SOUTH*4], 1
        mov dword [esi + 12 + DIR_EAST*4], 6
        mov dword [esi + 12 + DIR_WEST*4], -1
        mov dword [esi + 12 + DIR_UP*4], -1
        mov dword [esi + 12 + DIR_DOWN*4], -1
        mov dword [esi + 36], 0
        mov dword [esi + 40], 0

        ; Room 4: Crossroads
        add esi, ROOM_SIZE
        mov dword [esi + 0], desc_r4
        mov dword [esi + 4], name_r4
        mov byte  [esi + 8], 0
        mov dword [esi + 12 + DIR_NORTH*4], 7
        mov dword [esi + 12 + DIR_SOUTH*4], 2
        mov dword [esi + 12 + DIR_EAST*4], 8
        mov dword [esi + 12 + DIR_WEST*4], 1
        mov dword [esi + 12 + DIR_UP*4], -1
        mov dword [esi + 12 + DIR_DOWN*4], -1
        mov dword [esi + 36], 0
        mov dword [esi + 40], 0

        ; Room 5: Chapel
        add esi, ROOM_SIZE
        mov dword [esi + 0], desc_r5
        mov dword [esi + 4], name_r5
        mov byte  [esi + 8], 0
        mov dword [esi + 12 + DIR_NORTH*4], -1
        mov dword [esi + 12 + DIR_SOUTH*4], -1
        mov dword [esi + 12 + DIR_EAST*4], 1
        mov dword [esi + 12 + DIR_WEST*4], -1
        mov dword [esi + 12 + DIR_UP*4], -1
        mov dword [esi + 12 + DIR_DOWN*4], -1
        mov dword [esi + 36], 0
        mov dword [esi + 40], 0

        ; Room 6: Wizard's Study
        add esi, ROOM_SIZE
        mov dword [esi + 0], desc_r6
        mov dword [esi + 4], name_r6
        mov byte  [esi + 8], 0
        mov dword [esi + 12 + DIR_NORTH*4], -1
        mov dword [esi + 12 + DIR_SOUTH*4], -1
        mov dword [esi + 12 + DIR_EAST*4], -1
        mov dword [esi + 12 + DIR_WEST*4], 3
        mov dword [esi + 12 + DIR_UP*4], -1
        mov dword [esi + 12 + DIR_DOWN*4], 9
        mov dword [esi + 36], 0
        mov dword [esi + 40], 0

        ; Room 7: Skeleton Chamber
        add esi, ROOM_SIZE
        mov dword [esi + 0], desc_r7
        mov dword [esi + 4], name_r7
        mov byte  [esi + 8], 0
        mov dword [esi + 12 + DIR_NORTH*4], 10
        mov dword [esi + 12 + DIR_SOUTH*4], 4
        mov dword [esi + 12 + DIR_EAST*4], -1
        mov dword [esi + 12 + DIR_WEST*4], -1
        mov dword [esi + 12 + DIR_UP*4], -1
        mov dword [esi + 12 + DIR_DOWN*4], -1
        mov dword [esi + 36], 0
        mov dword [esi + 40], 0

        ; Room 8: Armory
        add esi, ROOM_SIZE
        mov dword [esi + 0], desc_r8
        mov dword [esi + 4], name_r8
        mov byte  [esi + 8], 0
        mov dword [esi + 12 + DIR_NORTH*4], -1
        mov dword [esi + 12 + DIR_SOUTH*4], -1
        mov dword [esi + 12 + DIR_EAST*4], -1
        mov dword [esi + 12 + DIR_WEST*4], 4
        mov dword [esi + 12 + DIR_UP*4], -1
        mov dword [esi + 12 + DIR_DOWN*4], -1
        mov dword [esi + 36], 0
        mov dword [esi + 40], 0

        ; Room 9: Hidden Crypt
        add esi, ROOM_SIZE
        mov dword [esi + 0], desc_r9
        mov dword [esi + 4], name_r9
        mov byte  [esi + 8], 0
        mov dword [esi + 12 + DIR_NORTH*4], -1
        mov dword [esi + 12 + DIR_SOUTH*4], -1
        mov dword [esi + 12 + DIR_EAST*4], -1
        mov dword [esi + 12 + DIR_WEST*4], -1
        mov dword [esi + 12 + DIR_UP*4], 6
        mov dword [esi + 12 + DIR_DOWN*4], -1
        mov dword [esi + 36], 0
        mov dword [esi + 40], 0

        ; Room 10: Treasure Vault (locked until skeleton defeated)
        add esi, ROOM_SIZE
        mov dword [esi + 0], desc_r10
        mov dword [esi + 4], name_r10
        mov byte  [esi + 8], ROOM_LOCKED
        mov dword [esi + 12 + DIR_NORTH*4], -1
        mov dword [esi + 12 + DIR_SOUTH*4], 7
        mov dword [esi + 12 + DIR_EAST*4], -1
        mov dword [esi + 12 + DIR_WEST*4], -1
        mov dword [esi + 12 + DIR_UP*4], -1
        mov dword [esi + 12 + DIR_DOWN*4], -1
        mov dword [esi + 36], event_treasure
        mov dword [esi + 40], 0

        ; Initialize items
        ; Item 0: Rusty Key
        mov esi, items
        mov dword [esi + 0], iname_key
        mov dword [esi + 4], idesc_key
        mov byte  [esi + 8], ITEM_TAKEABLE | ITEM_INROOM
        mov byte  [esi + 9], 2         ; Guard Room
        mov dword [esi + 12], use_key
        mov dword [esi + 16], iexam_key

        ; Item 1: Torch
        add esi, ITEM_SIZE
        mov dword [esi + 0], iname_torch
        mov dword [esi + 4], idesc_torch
        mov byte  [esi + 8], ITEM_TAKEABLE | ITEM_INROOM
        mov byte  [esi + 9], 0         ; Entrance
        mov dword [esi + 12], 0
        mov dword [esi + 16], iexam_torch

        ; Item 2: Sword
        add esi, ITEM_SIZE
        mov dword [esi + 0], iname_sword
        mov dword [esi + 4], idesc_sword
        mov byte  [esi + 8], ITEM_TAKEABLE | ITEM_INROOM
        mov byte  [esi + 9], 8         ; Armory
        mov dword [esi + 12], 0
        mov dword [esi + 16], iexam_sword

        ; Item 3: Healing Potion
        add esi, ITEM_SIZE
        mov dword [esi + 0], iname_potion
        mov dword [esi + 4], idesc_potion
        mov byte  [esi + 8], ITEM_TAKEABLE | ITEM_INROOM
        mov byte  [esi + 9], 5         ; Chapel
        mov dword [esi + 12], use_potion
        mov dword [esi + 16], iexam_potion

        ; Item 4: Ancient Scroll
        add esi, ITEM_SIZE
        mov dword [esi + 0], iname_scroll
        mov dword [esi + 4], idesc_scroll
        mov byte  [esi + 8], ITEM_TAKEABLE | ITEM_INROOM
        mov byte  [esi + 9], 3         ; Library
        mov dword [esi + 12], use_scroll
        mov dword [esi + 16], iexam_scroll

        ; Item 5: Gold Crown (in treasure vault)
        add esi, ITEM_SIZE
        mov dword [esi + 0], iname_crown
        mov dword [esi + 4], idesc_crown
        mov byte  [esi + 8], ITEM_TAKEABLE | ITEM_INROOM
        mov byte  [esi + 9], 10        ; Treasure Vault
        mov dword [esi + 12], 0
        mov dword [esi + 16], iexam_crown

        ; Item 6: Shield
        add esi, ITEM_SIZE
        mov dword [esi + 0], iname_shield
        mov dword [esi + 4], idesc_shield
        mov byte  [esi + 8], ITEM_TAKEABLE | ITEM_INROOM
        mov byte  [esi + 9], 8         ; Armory
        mov dword [esi + 12], 0
        mov dword [esi + 16], iexam_shield

        ; Item 7: Magic Gem (in crypt)
        add esi, ITEM_SIZE
        mov dword [esi + 0], iname_gem
        mov dword [esi + 4], idesc_gem
        mov byte  [esi + 8], ITEM_TAKEABLE | ITEM_INROOM
        mov byte  [esi + 9], 9         ; Hidden Crypt
        mov dword [esi + 12], 0
        mov dword [esi + 16], iexam_gem

        ; Clear remaining items
        add esi, ITEM_SIZE
        mov ecx, (MAX_ITEMS - 8) * ITEM_SIZE
        xor al, al
        mov edi, esi
        rep stosb

        popad
        ret

;=======================================================================
; ITEM USE HANDLERS
;=======================================================================

use_key:
        ; Key does nothing special when used outside context
        mov al, C_DESC
        call set_color
        mov esi, str_key_use
        call print_str
        ret

use_potion:
        ; Heal player to max
        mov dword [player_hp], HP_MAX
        ; Remove potion from inventory
        mov esi, items + 3 * ITEM_SIZE
        and byte [esi + 8], ~ITEM_CARRIED
        dec dword [inv_count]
        mov al, C_SUCCESS
        call set_color
        mov esi, str_potion_use
        call print_str
        ret

use_scroll:
        mov al, C_DESC
        call set_color
        mov esi, str_scroll_use
        call print_str
        ret

;=======================================================================
; ROOM EVENTS
;=======================================================================

event_treasure:
        ; Entering treasure vault triggers victory
        call game_victory
        ret

;=======================================================================
; DATA - STRINGS
;=======================================================================

str_title:
        db 0x0A
        db "  ========================================", 0x0A
        db "  =    THE CATACOMBS OF MELLIVORA        =", 0x0A
        db "  =    An Interactive Fiction Adventure   =", 0x0A
        db "  ========================================", 0x0A, 0x0A, 0

str_intro:
        db "  You are an adventurer who has discovered the entrance", 0x0A
        db "  to the legendary Catacombs of Mellivora, an ancient", 0x0A
        db "  underground fortress said to contain a treasure of", 0x0A
        db "  immense value -- the Golden Crown of the Honey Badger", 0x0A
        db "  King. But beware! The catacombs are guarded by an", 0x0A
        db "  undead skeleton warrior that has kept watch for", 0x0A
        db "  centuries.", 0x0A, 0x0A
        db "  Explore the dungeon, collect useful items, defeat", 0x0A
        db "  the guardian, and claim the crown to win!", 0x0A, 0x0A, 0

str_press_key: db 0x0A, "  [Press any key to continue]", 0x0A, 0
str_prompt:    db 0x0A, "> ", 0
str_newline:   db 0x0A, 0
str_divider:   db "  ----------------------------------------", 0x0A, 0
str_comma:     db ", ", 0
str_bullet:    db "  * ", 0
str_unknown:   db "  I don't understand that command. Type 'help' for a list.", 0x0A, 0
str_goodbye:   db 0x0A, "  Thank you for playing! Farewell, adventurer.", 0x0A, 0
str_go_where:  db "  Go where? Specify a direction (north, south, east, west, up, down).", 0x0A, 0
str_cant_go:   db "  You can't go that way.", 0x0A, 0
str_locked:    db "  That way is blocked. Something prevents your passage.", 0x0A, 0
str_taken:     db "  Taken.", 0x0A, 0
str_dropped:   db "  Dropped.", 0x0A, 0
str_take_what: db "  Take what?", 0x0A, 0
str_drop_what: db "  Drop what?", 0x0A, 0
str_not_here:  db "  You don't see that here.", 0x0A, 0
str_cant_take: db "  You can't take that.", 0x0A, 0
str_inv_full:  db "  You can't carry any more items.", 0x0A, 0
str_dont_have: db "  You don't have that.", 0x0A, 0
str_examine_what: db "  Examine what?", 0x0A, 0
str_use_what:  db "  Use what?", 0x0A, 0
str_use_nothing: db "  You can't figure out how to use that here.", 0x0A, 0
str_open_nothing: db "  There's nothing here to open.", 0x0A, 0
str_items_here: db 0x0A, "  You can see:", 0x0A, 0
str_monster_here:
        db 0x0A, "  *** A SKELETON WARRIOR stands here, bones rattling,", 0x0A
        db "      its hollow eyes blazing with unholy fire! ***", 0x0A, 0
str_exits:     db 0x0A, "  Exits: ", 0
str_inv_header: db 0x0A, "  You are carrying:", 0x0A, 0
str_inv_empty: db "  Nothing.", 0x0A, 0
str_hp_left:   db "  Your HP: ", 0
str_hp_suffix: db "/100", 0x0A, 0
str_path_clear:
        db "  The path to the north is now clear!", 0x0A, 0
str_atk_nothing:
        db "  There's nothing here to attack.", 0x0A, 0
str_atk_sword:
        db 0x0A, "  You swing your sword in a mighty arc!", 0x0A
        db "  The blade crashes through the skeleton's ribcage!", 0x0A
        db "  With a tremendous clatter, the undead warrior", 0x0A
        db "  collapses into a pile of broken bones!", 0x0A, 0
str_atk_bare:
        db "  You punch the skeleton! Your fist crunches against", 0x0A
        db "  bone. It hurts you more than it hurts the skeleton.", 0x0A
        db "  The skeleton retaliates with a vicious claw swipe!", 0x0A, 0
str_death:
        db 0x0A
        db "  ========================================", 0x0A
        db "  =          YOU HAVE DIED               =", 0x0A
        db "  ========================================", 0x0A
        db 0x0A
        db "  The skeleton warrior's bony claws find their mark.", 0x0A
        db "  You collapse to the cold stone floor as darkness", 0x0A
        db "  claims you. The catacombs have claimed another victim.", 0x0A, 0
str_victory:
        db 0x0A
        db "  ========================================", 0x0A
        db "  =     *** VICTORY! ***                 =", 0x0A
        db "  ========================================", 0x0A
        db 0x0A
        db "  You enter the Treasure Vault and behold the legendary", 0x0A
        db "  Golden Crown of the Honey Badger King, gleaming atop", 0x0A
        db "  a marble pedestal. As you lift it, you feel its warmth", 0x0A
        db "  and power flowing through your hands.", 0x0A
        db 0x0A
        db "  You have conquered the Catacombs of Mellivora!", 0x0A
        db "  The treasure is yours. The Honey Badger King would", 0x0A
        db "  be proud.", 0x0A, 0

str_help:
        db 0x0A
        db "  ---- COMMANDS ----", 0x0A
        db "  look (l)       - Describe your surroundings", 0x0A
        db "  go <dir>       - Move in a direction", 0x0A
        db "  n/s/e/w/u/d    - Move north/south/east/west/up/down", 0x0A
        db "  take <item>    - Pick up an item", 0x0A
        db "  drop <item>    - Drop an item", 0x0A
        db "  use <item>     - Use an item", 0x0A
        db "  examine <item> - Look closely at an item (x)", 0x0A
        db "  inventory (i)  - List carried items", 0x0A
        db "  attack         - Attack a creature", 0x0A
        db "  help           - Show this help", 0x0A
        db "  quit           - Leave the game", 0x0A, 0

str_key_use:   db "  The rusty key doesn't seem to fit anything here.", 0x0A, 0
str_potion_use:
        db "  You drink the healing potion. A warm glow fills your body.", 0x0A
        db "  You feel completely restored! HP: 100/100", 0x0A, 0
str_scroll_use:
        db "  You unroll the scroll and read aloud the ancient words:", 0x0A
        db '  "Beyond the bones, beyond the blade,', 0x0A
        db '   The crown awaits where kings were laid.', 0x0A
        db '   Steel will shatter what fists cannot --', 0x0A
        db '   Seek the armory, find what you sought."', 0x0A, 0

; Direction strings
str_north:     db "north", 0
str_south:     db "south", 0
str_east:      db "east", 0
str_west:      db "west", 0
str_up_dir:    db "up", 0
str_down_dir:  db "down", 0

; Verb strings
str_v_look:    db "look", 0
str_v_l:       db "l", 0
str_v_go:      db "go", 0
str_v_take:    db "take", 0
str_v_get:     db "get", 0
str_v_drop:    db "drop", 0
str_v_use:     db "use", 0
str_v_examine: db "examine", 0
str_v_x:       db "x", 0
str_v_inventory: db "inventory", 0
str_v_i:       db "i", 0
str_v_help:    db "help", 0
str_v_quit:    db "quit", 0
str_v_exit:    db "exit", 0
str_v_attack:  db "attack", 0
str_v_kill:    db "kill", 0
str_v_fight:   db "fight", 0
str_v_open:    db "open", 0
str_v_n:       db "n", 0
str_v_s:       db "s", 0
str_v_e:       db "e", 0
str_v_w:       db "w", 0
str_v_u:       db "u", 0

str_sword_name: db "sword", 0

;=======================================================================
; ROOM DESCRIPTIONS
;=======================================================================

name_r0: db "  The Dungeon Entrance", 0
desc_r0:
        db "  You stand at the mouth of an ancient stone passage.", 0x0A
        db "  Cold air seeps from the darkness ahead. Moss covers", 0x0A
        db "  the crumbling walls, and the faint sound of dripping", 0x0A
        db "  water echoes from within. A rusted iron gate stands", 0x0A
        db "  open, as if inviting you inside.", 0x0A, 0

name_r1: db "  The Great Hall", 0
desc_r1:
        db "  A vast chamber opens before you, its vaulted ceiling", 0x0A
        db "  lost in shadow. Massive stone pillars line the hall,", 0x0A
        db "  carved with images of honey badgers in battle. A long", 0x0A
        db "  stone table, now cracked and dusty, runs down the", 0x0A
        db "  center. Passages lead in several directions.", 0x0A, 0

name_r2: db "  The Guard Room", 0
desc_r2:
        db "  This small chamber once housed the dungeon's guards.", 0x0A
        db "  A wooden rack on the wall holds rusted weapon hooks,", 0x0A
        db "  now mostly empty. A collapsed bunk lies in one corner.", 0x0A
        db "  Scratches in the wall suggest someone was counting", 0x0A
        db "  the days.", 0x0A, 0

name_r3: db "  The Library", 0
desc_r3:
        db "  Towering shelves of rotting books line every wall.", 0x0A
        db "  Most volumes have crumbled to dust, but a few leather-", 0x0A
        db "  bound tomes still stand upright. A reading desk holds", 0x0A
        db "  a partially melted candle. The air smells of ancient", 0x0A
        db "  parchment and forgotten knowledge.", 0x0A, 0

name_r4: db "  The Crossroads", 0
desc_r4:
        db "  A junction where four passages meet. Worn flagstones", 0x0A
        db "  mark where countless feet have trod over the centuries.", 0x0A
        db "  Faded directional carvings in the wall hint at what", 0x0A
        db "  lies in each direction, but the text is too eroded", 0x0A
        db "  to read.", 0x0A, 0

name_r5: db "  The Chapel", 0
desc_r5:
        db "  A small devotional chamber with a stone altar at the", 0x0A
        db "  far end. Carved symbols of protection adorn the walls.", 0x0A
        db "  Despite the ages, a sense of peace pervades this room.", 0x0A
        db "  Stained glass fragments litter the floor, remnants of", 0x0A
        db "  a once-beautiful window.", 0x0A, 0

name_r6: db "  The Wizard's Study", 0
desc_r6:
        db "  Arcane symbols are painted across every surface of", 0x0A
        db "  this circular chamber. A stone workbench holds dusty", 0x0A
        db "  bottles and alchemical apparatus. A trapdoor in the", 0x0A
        db "  floor leads to a dark space below. The air crackles", 0x0A
        db "  faintly with residual magical energy.", 0x0A, 0

name_r7: db "  The Skeleton Chamber", 0
desc_r7:
        db "  This chamber reeks of death. Bones are scattered", 0x0A
        db "  across the floor, and dark stains mark the stone.", 0x0A
        db "  The passage to the north is partially blocked by a", 0x0A
        db "  massive iron portcullis -- it looks like it could be", 0x0A
        db "  forced open if the guardian were defeated.", 0x0A, 0

name_r8: db "  The Armory", 0
desc_r8:
        db "  Weapon racks line the walls, though most are empty.", 0x0A
        db "  Dented helmets and torn chainmail are piled in", 0x0A
        db "  corners. The forge in the back wall has long gone", 0x0A
        db "  cold, but the quality of the remaining weapons", 0x0A
        db "  speaks to the craftsmanship of a bygone era.", 0x0A, 0

name_r9: db "  The Hidden Crypt", 0
desc_r9:
        db "  You descend through the trapdoor into a cold, dark", 0x0A
        db "  crypt. Stone sarcophagi line the walls, their lids", 0x0A
        db "  carved with stern-faced warriors. In the center, a", 0x0A
        db "  small pedestal holds something that glimmers faintly", 0x0A
        db "  in the darkness.", 0x0A, 0

name_r10: db "  The Treasure Vault", 0
desc_r10:
        db "  A magnificent chamber of polished marble. Gold coins", 0x0A
        db "  and precious gems overflow from ornate chests. But", 0x0A
        db "  atop a central pedestal, catching the light of an", 0x0A
        db "  eternal flame, rests the legendary Golden Crown of", 0x0A
        db "  the Honey Badger King.", 0x0A, 0

;=======================================================================
; ITEM DATA
;=======================================================================

iname_key:     db "key", 0
idesc_key:     db "  A rusty iron key.", 0x0A, 0
iexam_key:     db "  An old iron key, pitted with rust but still solid. A", 0x0A
               db "  small emblem of a honey badger is stamped into the bow.", 0x0A, 0

iname_torch:   db "torch", 0
idesc_torch:   db "  A wooden torch.", 0x0A, 0
iexam_torch:   db "  A sturdy oak torch wrapped in oil-soaked rags. It burns", 0x0A
               db "  steadily, casting dancing shadows on the walls.", 0x0A, 0

iname_sword:   db "sword", 0
idesc_sword:   db "  A gleaming steel sword.", 0x0A, 0
iexam_sword:   db "  A well-crafted longsword with a leather-wrapped grip.", 0x0A
               db "  The blade is etched with runes that seem to glow faintly.", 0x0A
               db "  It feels perfectly balanced in your hand.", 0x0A, 0

iname_potion:  db "potion", 0
idesc_potion:  db "  A healing potion in a glass vial.", 0x0A, 0
iexam_potion:  db "  A small glass vial filled with a luminous red liquid.", 0x0A
               db "  The cork is sealed with wax stamped with a cross.", 0x0A
               db "  It radiates gentle warmth.", 0x0A, 0

iname_scroll:  db "scroll", 0
idesc_scroll:  db "  An ancient parchment scroll.", 0x0A, 0
iexam_scroll:  db "  A rolled parchment tied with a faded ribbon. The text", 0x0A
               db "  is written in an old dialect but still readable. It", 0x0A
               db "  appears to be a riddle or hint of some kind.", 0x0A, 0

iname_crown:   db "crown", 0
idesc_crown:   db "  The Golden Crown of the Honey Badger King!", 0x0A, 0
iexam_crown:   db "  A magnificent crown of pure gold, set with rubies and", 0x0A
               db "  sapphires. A fierce honey badger is engraved on the", 0x0A
               db "  front. It practically hums with ancient power.", 0x0A, 0

iname_shield:  db "shield", 0
idesc_shield:  db "  A battered iron shield.", 0x0A, 0
iexam_shield:  db "  A round iron shield bearing the crest of the catacomb", 0x0A
               db "  guards -- a honey badger rampant on a field of stars.", 0x0A
               db "  Dented but still serviceable.", 0x0A, 0

iname_gem:     db "gem", 0
idesc_gem:     db "  A glowing magic gem.", 0x0A, 0
iexam_gem:     db "  A multifaceted gem that pulses with inner light, cycling", 0x0A
               db "  through shades of blue, green, and violet. It feels", 0x0A
               db "  strangely warm to the touch.", 0x0A, 0

;=======================================================================
; BSS
;=======================================================================
align 4
player_room:    dd 0
player_hp:      dd 0
inv_count:      dd 0
monster_room:   dd 0
monster_hp:     dd 0
verb_id:        dd 0
noun_ptr:       dd 0
go_dir:         dd 0
color:          db 0

align 4
input_buf:      times 80 db 0
word_buf:       times 40 db 0
rooms:          times MAX_ROOMS * ROOM_SIZE db 0
items:          times MAX_ITEMS * ITEM_SIZE db 0
