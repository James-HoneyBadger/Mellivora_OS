;=======================================================================
; NEUROVAULT - A Literary Interactive Fiction Adventure
; Inspired by Mindwheel (1984) by Robert Pinsky
;
; The year is 2147. The last great AI, ATLAS, is dying. Its neural
; cores hold the uploaded minds of four legendary thinkers. You are
; a Neural Diver -- jacking into the decaying mindscape to recover
; four Memory Keys before they are lost forever.
;
; Each mind is a surreal world shaped by its owner's inner life:
;   The Poet    - A crumbling library of living words
;   The General - An endless battlefield of frozen time
;   The Artist  - A gallery where paintings breathe
;   The Scholar - A labyrinth of pure logic
;
; Commands: LOOK, GO <dir>, TAKE <item>, USE <item>, TALK, INVENTORY
;           HELP, QUIT, NORTH/SOUTH/EAST/WEST (or N/S/E/W)
;=======================================================================

%include "syscalls.inc"

;-----------------------------------------------------------------------
; Constants
;-----------------------------------------------------------------------
; Colors
C_BLACK         equ 0x00
C_BLUE          equ 0x01
C_GREEN         equ 0x02
C_CYAN          equ 0x03
C_RED           equ 0x04
C_MAGENTA       equ 0x05
C_BROWN         equ 0x06
C_LGRAY         equ 0x07
C_DGRAY         equ 0x08
C_LBLUE         equ 0x09
C_LGREEN        equ 0x0A
C_LCYAN         equ 0x0B
C_LRED          equ 0x0C
C_LMAGENTA      equ 0x0D
C_YELLOW        equ 0x0E
C_WHITE         equ 0x0F

; Input
INPUT_MAX       equ 80
CMD_MAX         equ 16

; Room IDs
R_NEXUS         equ 0           ; Hub - the neural nexus
; Poet's Mind (rooms 1-6)
R_POET_GATE     equ 1
R_LIBRARY       equ 2
R_VERSE_HALL    equ 3
R_INK_POOL      equ 4
R_BURNING_SHELF equ 5
R_POET_SANCTUM  equ 6
; General's Mind (rooms 7-12)
R_GEN_GATE      equ 7
R_TRENCH        equ 8
R_NO_MANS       equ 9
R_COMMAND_POST  equ 10
R_FROZEN_FIELD  equ 11
R_GEN_SANCTUM   equ 12
; Artist's Mind (rooms 13-18)
R_ART_GATE      equ 13
R_FOYER         equ 14
R_PORTRAIT_HALL equ 15
R_SCULPTURE_GDN equ 16
R_STUDIO        equ 17
R_ART_SANCTUM   equ 18
; Scholar's Mind (rooms 19-24)
R_SCH_GATE      equ 19
R_ARCHIVES      equ 20
R_PUZZLE_ROOM   equ 21
R_MIRROR_HALL   equ 22
R_CLOCK_TOWER   equ 23
R_SCH_SANCTUM   equ 24
NUM_ROOMS       equ 25

; Item IDs
I_QUILL         equ 0           ; Poet's world
I_INKWELL       equ 1
I_TORN_PAGE     equ 2
I_POET_KEY      equ 3
I_COMPASS       equ 4           ; General's world
I_MEDAL         equ 5
I_FIELD_GLASS   equ 6
I_GEN_KEY       equ 7
I_BRUSH         equ 8           ; Artist's world
I_CANVAS        equ 9
I_PALETTE       equ 10
I_ART_KEY       equ 11
I_LENS          equ 12          ; Scholar's world
I_CODEX         equ 13
I_GEAR          equ 14
I_SCH_KEY       equ 15
NUM_ITEMS       equ 16

; Item location special values
LOC_INVENTORY   equ 250
LOC_NOWHERE     equ 251

; Direction indices
DIR_NORTH       equ 0
DIR_SOUTH       equ 1
DIR_EAST        equ 2
DIR_WEST        equ 3

; Game flags
F_POET_TALKED   equ 0
F_INK_FILLED    equ 1
F_PAGE_WRITTEN  equ 2
F_GEN_TALKED    equ 3
F_FIELD_VIEWED  equ 4
F_MEDAL_PLACED  equ 5
F_ART_TALKED    equ 6
F_CANVAS_PLACED equ 7
F_PAINTED       equ 8
F_SCH_TALKED    equ 9
F_LENS_USED     equ 10
F_GEAR_PLACED   equ 11
F_GAME_WON      equ 12
NUM_FLAGS       equ 16

; Sound
SND_STEP        equ 600
SND_TAKE        equ 1000
SND_USE         equ 800
SND_ERROR       equ 200
SND_KEY         equ 1400
SND_WIN         equ 1600
SND_TALK        equ 500

;=======================================================================
; ENTRY POINT
;=======================================================================
start:
        mov eax, SYS_CLEAR
        int 0x80

        ; Seed PRNG
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_seed], eax

        ; Initialize game state
        call init_game

        ; Show title screen
        call show_title

        ; Show intro
        call show_intro

        ; Main game loop
        jmp game_loop

;=======================================================================
; TITLE SCREEN
;=======================================================================
show_title:
        pushad
        mov eax, SYS_CLEAR
        int 0x80

        ; Border top
        mov eax, SYS_SETCOLOR
        mov ebx, C_LMAGENTA
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 1
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_title_border
        int 0x80

        ; Title
        mov eax, SYS_SETCURSOR
        mov ebx, 22
        mov ecx, 3
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_title_name
        int 0x80

        ; Subtitle
        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 5
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_title_sub
        int 0x80

        ; Brain art
        mov eax, SYS_SETCOLOR
        mov ebx, C_LMAGENTA
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 28
        mov ecx, 7
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, brain_art1
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 28
        mov ecx, 8
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, brain_art2
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 28
        mov ecx, 9
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, brain_art3
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 28
        mov ecx, 10
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, brain_art4
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 28
        mov ecx, 11
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, brain_art5
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 28
        mov ecx, 12
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, brain_art6
        int 0x80

        ; Tagline
        mov eax, SYS_SETCURSOR
        mov ebx, 14
        mov ecx, 14
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_title_tag
        int 0x80

        ; Border bottom
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 16
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LMAGENTA
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_title_border
        int 0x80

        ; Menu
        mov eax, SYS_SETCURSOR
        mov ebx, 25
        mov ecx, 18
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_menu_play
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 25
        mov ecx, 19
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_menu_about
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 25
        mov ecx, 20
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_menu_quit
        int 0x80

        ; Play title sounds
        mov eax, SYS_BEEP
        mov ebx, 440
        mov ecx, 3
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 554
        mov ecx, 3
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 659
        mov ecx, 5
        int 0x80

        ; Footer
        mov eax, SYS_SETCURSOR
        mov ebx, 16
        mov ecx, 23
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_title_footer
        int 0x80

.title_wait:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, '1'
        je .title_done
        cmp al, '2'
        je .show_about
        cmp al, '3'
        je .title_exit
        cmp al, 27
        je .title_exit
        jmp .title_wait

.show_about:
        call show_about
        jmp show_title

.title_exit:
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.title_done:
        popad
        ret

;=======================================================================
; ABOUT SCREEN
;=======================================================================
show_about:
        pushad
        mov eax, SYS_CLEAR
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 22
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_about_title
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80

        mov esi, about_lines
        mov ecx, 1
        mov edx, 16            ; number of lines
.about_loop:
        push ecx
        push edx
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, [esi]
        int 0x80
        add esi, 4
        pop edx
        pop ecx
        inc ecx
        dec edx
        jnz .about_loop

        mov eax, SYS_SETCURSOR
        mov ebx, 18
        mov ecx, 22
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_press_key
        int 0x80
        mov eax, SYS_GETCHAR
        int 0x80
        popad
        ret

;=======================================================================
; INTRO SEQUENCE
;=======================================================================
show_intro:
        pushad
        mov eax, SYS_CLEAR
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80

        ; Print intro text with pauses for atmosphere
        mov esi, intro_lines
        mov edx, 14
        xor ecx, ecx
.intro_loop:
        push ecx
        push edx
        mov eax, SYS_SETCURSOR
        mov ebx, 3
        int 0x80
        mov eax, SYS_SETCOLOR
        ; Alternate colors for atmosphere
        mov ebx, C_LCYAN
        test ecx, 1
        jz .intro_color_ok
        mov ebx, C_LGRAY
.intro_color_ok:
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, [esi]
        int 0x80
        add esi, 4

        ; Brief pause between lines
        mov eax, SYS_SLEEP
        mov ebx, 20
        int 0x80

        pop edx
        pop ecx
        inc ecx
        dec edx
        jnz .intro_loop

        ; Dramatic pause
        mov eax, SYS_BEEP
        mov ebx, 220
        mov ecx, 8
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 18
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_intro_ready
        int 0x80
        mov eax, SYS_GETCHAR
        int 0x80

        popad
        ret

;=======================================================================
; GAME INITIALIZATION
;=======================================================================
init_game:
        pushad

        ; Start in the Nexus
        mov dword [current_room], R_NEXUS
        mov dword [keys_found], 0
        mov dword [moves], 0

        ; Clear all flags
        mov edi, flags
        mov ecx, NUM_FLAGS
        xor al, al
        rep stosb

        ; Initialize item locations (where items start)
        mov byte [item_loc + I_QUILL],      R_VERSE_HALL
        mov byte [item_loc + I_INKWELL],     R_INK_POOL
        mov byte [item_loc + I_TORN_PAGE],   R_BURNING_SHELF
        mov byte [item_loc + I_POET_KEY],    LOC_NOWHERE    ; Created by puzzle
        mov byte [item_loc + I_COMPASS],     R_TRENCH
        mov byte [item_loc + I_MEDAL],       R_NO_MANS
        mov byte [item_loc + I_FIELD_GLASS], R_COMMAND_POST
        mov byte [item_loc + I_GEN_KEY],     LOC_NOWHERE
        mov byte [item_loc + I_BRUSH],       R_FOYER
        mov byte [item_loc + I_CANVAS],      R_STUDIO
        mov byte [item_loc + I_PALETTE],     R_SCULPTURE_GDN
        mov byte [item_loc + I_ART_KEY],     LOC_NOWHERE
        mov byte [item_loc + I_LENS],        R_ARCHIVES
        mov byte [item_loc + I_CODEX],       R_PUZZLE_ROOM
        mov byte [item_loc + I_GEAR],        R_MIRROR_HALL
        mov byte [item_loc + I_SCH_KEY],     LOC_NOWHERE

        popad
        ret

;=======================================================================
; MAIN GAME LOOP
;=======================================================================
game_loop:
        ; Check win condition
        cmp dword [keys_found], 4
        jge game_win

        ; Describe current room
        call describe_room

        ; Show prompt and get input
        call get_input

        ; Parse and execute command
        call parse_command

        jmp game_loop

;=======================================================================
; ROOM DESCRIPTION
;=======================================================================
describe_room:
        pushad
        mov eax, SYS_CLEAR
        int 0x80

        ; Draw top status bar
        call draw_status_bar

        ; Get room description
        mov eax, [current_room]
        imul eax, 4
        mov ebx, [room_desc_table + eax]

        ; Get room name
        mov eax, [current_room]
        imul eax, 4
        mov ecx, [room_name_table + eax]

        ; Print room name
        push ebx
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 2
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, [current_room]
        imul eax, 4
        mov ebx, [room_name_table + eax]
        mov eax, SYS_PRINT
        int 0x80

        ; Separator
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 3
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_room_sep
        int 0x80

        ; Print room description
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 5
        int 0x80
        call get_room_color
        mov eax, SYS_SETCOLOR
        int 0x80
        pop ebx
        mov eax, SYS_PRINT
        int 0x80

        ; Print any extra description lines
        mov eax, [current_room]
        imul eax, 4
        mov ebx, [room_desc2_table + eax]
        cmp ebx, 0
        je .no_desc2
        push ebx
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 7
        int 0x80
        pop ebx
        mov eax, SYS_PRINT
        int 0x80
.no_desc2:

        ; List items in room
        call list_room_items

        ; List exits
        call list_exits

        popad
        ret

;---------------------------------------
; draw_status_bar
;---------------------------------------
draw_status_bar:
        pushad
        ; Blue background bar
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE | 0x10   ; white on blue
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        xor ecx, ecx
        int 0x80
        mov ecx, 80
.stat_space:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        dec ecx
        jnz .stat_space

        ; Print game title
        mov eax, SYS_SETCURSOR
        mov ebx, 1
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_status_title
        int 0x80

        ; Print keys found
        mov eax, SYS_SETCURSOR
        mov ebx, 35
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_status_keys
        int 0x80
        mov eax, [keys_found]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_of_four
        int 0x80

        ; Print moves
        mov eax, SYS_SETCURSOR
        mov ebx, 60
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_status_moves
        int 0x80
        mov eax, [moves]
        call print_number

        ; Reset color
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80

        popad
        ret

;---------------------------------------
; get_room_color - Returns color in EBX based on current mind-world
;---------------------------------------
get_room_color:
        push eax
        mov eax, [current_room]
        cmp eax, R_POET_GATE
        jl .col_nexus
        cmp eax, R_GEN_GATE
        jl .col_poet
        cmp eax, R_ART_GATE
        jl .col_gen
        cmp eax, R_SCH_GATE
        jl .col_art
        mov ebx, C_LCYAN
        jmp .col_done
.col_nexus:
        mov ebx, C_LMAGENTA
        jmp .col_done
.col_poet:
        mov ebx, C_LCYAN
        jmp .col_done
.col_gen:
        mov ebx, C_LRED
        jmp .col_done
.col_art:
        mov ebx, C_YELLOW
.col_done:
        pop eax
        ret

;---------------------------------------
; list_room_items - Show items in current room
;---------------------------------------
list_room_items:
        pushad
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 9
        int 0x80

        mov edx, 0             ; item counter
        mov esi, 0             ; found any?
.item_loop:
        cmp edx, NUM_ITEMS
        jge .items_done
        movzx eax, byte [item_loc + edx]
        cmp eax, [current_room]
        jne .item_next

        ; First item? Print header
        cmp esi, 0
        jne .item_skip_hdr
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_you_see
        int 0x80
        mov esi, 1
.item_skip_hdr:
        ; Print item name
        push edx
        mov eax, SYS_PRINT
        mov ebx, str_item_bullet
        int 0x80
        mov eax, edx
        imul eax, 4
        mov ebx, [item_name_table + eax]
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_newline
        int 0x80
        pop edx

.item_next:
        inc edx
        jmp .item_loop
.items_done:
        popad
        ret

;---------------------------------------
; list_exits - Show available exits
;---------------------------------------
list_exits:
        pushad

        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 14
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_exits
        int 0x80

        mov eax, [current_room]
        imul eax, 16            ; 4 dirs * 4 bytes each
        lea esi, [room_exits + eax]

        ; North
        mov eax, [esi + DIR_NORTH * 4]
        cmp eax, 0xFF
        je .no_north
        mov eax, SYS_PRINT
        mov ebx, str_dir_n
        int 0x80
.no_north:
        ; South
        mov eax, [esi + DIR_SOUTH * 4]
        cmp eax, 0xFF
        je .no_south
        mov eax, SYS_PRINT
        mov ebx, str_dir_s
        int 0x80
.no_south:
        ; East
        mov eax, [esi + DIR_EAST * 4]
        cmp eax, 0xFF
        je .no_east
        mov eax, SYS_PRINT
        mov ebx, str_dir_e
        int 0x80
.no_east:
        ; West
        mov eax, [esi + DIR_WEST * 4]
        cmp eax, 0xFF
        je .no_west
        mov eax, SYS_PRINT
        mov ebx, str_dir_w
        int 0x80
.no_west:
        popad
        ret

;=======================================================================
; INPUT HANDLING
;=======================================================================

;---------------------------------------
; get_input - Read a line from user into input_buf
;---------------------------------------
get_input:
        pushad

        ; Print prompt
        mov eax, SYS_SETCURSOR
        mov ebx, 0
        mov ecx, 16
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_input_sep
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 17
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_prompt
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80

        ; Read characters
        mov edi, input_buf
        xor ecx, ecx           ; char count

.input_loop:
        mov eax, SYS_GETCHAR
        int 0x80

        cmp al, 0x0D           ; Enter
        je .input_done
        cmp al, 0x0A
        je .input_done
        cmp al, 0x08           ; Backspace
        je .input_bs
        cmp al, 0x7F           ; Delete
        je .input_bs

        ; Printable char?
        cmp al, 32
        jb .input_loop
        cmp al, 126
        ja .input_loop

        ; Buffer full?
        cmp ecx, INPUT_MAX - 1
        jge .input_loop

        ; Store and echo
        mov [edi + ecx], al
        inc ecx

        ; Convert to uppercase for display (lowercase stored)
        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80

        jmp .input_loop

.input_bs:
        cmp ecx, 0
        je .input_loop
        dec ecx
        ; Erase character on screen
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
        jmp .input_loop

.input_done:
        mov byte [edi + ecx], 0 ; Null-terminate
        mov [input_len], ecx

        popad
        ret

;=======================================================================
; COMMAND PARSER
;=======================================================================
parse_command:
        pushad

        ; Skip if empty input
        cmp dword [input_len], 0
        je .parse_done

        ; Convert input to uppercase for matching
        mov esi, input_buf
        mov edi, cmd_buf
        xor ecx, ecx
.to_upper:
        mov al, [esi + ecx]
        cmp al, 0
        je .upper_done
        cmp al, 'a'
        jb .no_upper
        cmp al, 'z'
        ja .no_upper
        sub al, 32
.no_upper:
        mov [edi + ecx], al
        inc ecx
        cmp ecx, INPUT_MAX - 1
        jl .to_upper
.upper_done:
        mov byte [edi + ecx], 0

        ; Try to match commands
        ; --- LOOK ---
        mov esi, cmd_buf
        mov edi, str_cmd_look
        call str_starts_with
        cmp eax, 1
        je .do_look

        ; --- NORTH / N ---
        mov esi, cmd_buf
        mov edi, str_cmd_north
        call str_starts_with
        cmp eax, 1
        je .do_north
        mov esi, cmd_buf
        cmp byte [esi], 'N'
        jne .not_n
        cmp byte [esi+1], 0
        je .do_north
.not_n:

        ; --- SOUTH / S ---
        mov esi, cmd_buf
        mov edi, str_cmd_south
        call str_starts_with
        cmp eax, 1
        je .do_south
        mov esi, cmd_buf
        cmp byte [esi], 'S'
        jne .not_s
        cmp byte [esi+1], 0
        je .do_south
.not_s:

        ; --- EAST / E ---
        mov esi, cmd_buf
        mov edi, str_cmd_east
        call str_starts_with
        cmp eax, 1
        je .do_east
        mov esi, cmd_buf
        cmp byte [esi], 'E'
        jne .not_e
        cmp byte [esi+1], 0
        je .do_east
.not_e:

        ; --- WEST / W ---
        mov esi, cmd_buf
        mov edi, str_cmd_west
        call str_starts_with
        cmp eax, 1
        je .do_west
        mov esi, cmd_buf
        cmp byte [esi], 'W'
        jne .not_w
        cmp byte [esi+1], 0
        je .do_west
.not_w:

        ; --- GO <dir> ---
        mov esi, cmd_buf
        mov edi, str_cmd_go
        call str_starts_with
        cmp eax, 1
        je .do_go

        ; --- TAKE / GET ---
        mov esi, cmd_buf
        mov edi, str_cmd_take
        call str_starts_with
        cmp eax, 1
        je .do_take
        mov esi, cmd_buf
        mov edi, str_cmd_get
        call str_starts_with
        cmp eax, 1
        je .do_take

        ; --- USE ---
        mov esi, cmd_buf
        mov edi, str_cmd_use
        call str_starts_with
        cmp eax, 1
        je .do_use

        ; --- INVENTORY / I ---
        mov esi, cmd_buf
        mov edi, str_cmd_inv
        call str_starts_with
        cmp eax, 1
        je .do_inventory
        mov esi, cmd_buf
        cmp byte [esi], 'I'
        jne .not_i
        cmp byte [esi+1], 0
        je .do_inventory
.not_i:

        ; --- TALK ---
        mov esi, cmd_buf
        mov edi, str_cmd_talk
        call str_starts_with
        cmp eax, 1
        je .do_talk

        ; --- HELP ---
        mov esi, cmd_buf
        mov edi, str_cmd_help
        call str_starts_with
        cmp eax, 1
        je .do_help

        ; --- QUIT ---
        mov esi, cmd_buf
        mov edi, str_cmd_quit
        call str_starts_with
        cmp eax, 1
        je .do_quit

        ; Unknown command
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_unknown_cmd
        int 0x80
        call wait_key
        jmp .parse_done

; === Command Handlers ===

.do_look:
        ; Re-describe room (already happens on next loop iteration)
        jmp .parse_done

.do_north:
        mov eax, DIR_NORTH
        call try_move
        jmp .parse_done

.do_south:
        mov eax, DIR_SOUTH
        call try_move
        jmp .parse_done

.do_east:
        mov eax, DIR_EAST
        call try_move
        jmp .parse_done

.do_west:
        mov eax, DIR_WEST
        call try_move
        jmp .parse_done

.do_go:
        ; Parse direction after "GO "
        mov esi, cmd_buf
        add esi, 3
        cmp byte [esi], 'N'
        je .do_north
        cmp byte [esi], 'S'
        je .do_south
        cmp byte [esi], 'E'
        je .do_east
        cmp byte [esi], 'W'
        je .do_west
        call print_msg_line
        mov eax, SYS_PRINT
        mov ebx, str_go_where
        int 0x80
        call wait_key
        jmp .parse_done

.do_take:
        call cmd_take
        jmp .parse_done

.do_use:
        call cmd_use
        jmp .parse_done

.do_inventory:
        call cmd_inventory
        jmp .parse_done

.do_talk:
        call cmd_talk
        jmp .parse_done

.do_help:
        call show_help
        jmp .parse_done

.do_quit:
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_quit_confirm
        int 0x80
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'y'
        je .do_real_quit
        cmp al, 'Y'
        je .do_real_quit
        jmp .parse_done

.do_real_quit:
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.parse_done:
        popad
        ret

;=======================================================================
; MOVEMENT
;=======================================================================
try_move:
        ; EAX = direction (0-3)
        pushad
        mov edx, eax            ; save direction

        ; Look up exit for current room
        mov eax, [current_room]
        imul eax, 16
        lea esi, [room_exits + eax]
        mov eax, [esi + edx * 4]

        cmp eax, 0xFF           ; No exit
        je .no_exit

        ; Move to new room
        mov [current_room], eax
        inc dword [moves]

        ; Step sound
        push eax
        mov eax, SYS_BEEP
        mov ebx, SND_STEP
        mov ecx, 1
        int 0x80
        pop eax

        popad
        ret

.no_exit:
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_no_exit
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, SND_ERROR
        mov ecx, 2
        int 0x80
        call wait_key
        popad
        ret

;=======================================================================
; TAKE COMMAND
;=======================================================================
cmd_take:
        pushad

        ; Find which item names match the input after "TAKE " or "GET "
        mov esi, cmd_buf
        ; Skip past command word
        cmp byte [esi], 'T'
        jne .take_get
        add esi, 5             ; skip "TAKE "
        jmp .take_search
.take_get:
        add esi, 4             ; skip "GET "

.take_search:
        ; Try each item
        xor edx, edx
.take_loop:
        cmp edx, NUM_ITEMS
        jge .take_not_found

        ; Is item in this room?
        movzx eax, byte [item_loc + edx]
        cmp eax, [current_room]
        jne .take_next

        ; Does input match item keyword?
        push esi
        push edx
        mov eax, edx
        imul eax, 4
        mov edi, [item_keyword_table + eax]
        call str_starts_with
        pop edx
        pop esi
        cmp eax, 1
        je .take_found

.take_next:
        inc edx
        jmp .take_loop

.take_found:
        ; Move item to inventory
        mov byte [item_loc + edx], LOC_INVENTORY

        ; Sound
        mov eax, SYS_BEEP
        mov ebx, SND_TAKE
        mov ecx, 2
        int 0x80

        ; Message
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_taken
        int 0x80
        mov eax, edx
        imul eax, 4
        mov ebx, [item_name_table + eax]
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_period
        int 0x80
        call wait_key
        popad
        ret

.take_not_found:
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_take_what
        int 0x80
        call wait_key
        popad
        ret

;=======================================================================
; USE COMMAND - Puzzle logic
;=======================================================================
cmd_use:
        pushad

        ; Parse item name after "USE "
        mov esi, cmd_buf
        add esi, 4             ; skip "USE "

        ; Find which item
        xor edx, edx
.use_loop:
        cmp edx, NUM_ITEMS
        jge .use_not_found

        ; Must be in inventory
        cmp byte [item_loc + edx], LOC_INVENTORY
        jne .use_next

        ; Does input match?
        push esi
        push edx
        mov eax, edx
        imul eax, 4
        mov edi, [item_keyword_table + eax]
        call str_starts_with
        pop edx
        pop esi
        cmp eax, 1
        je .use_found

.use_next:
        inc edx
        jmp .use_loop

.use_found:
        ; === PUZZLE LOGIC ===
        ; Each item "use" depends on context (room + flags)

        ; --- POET WORLD PUZZLES ---
        ; USE QUILL in Poet's Sanctum with INK and PAGE -> creates Poet Key
        cmp edx, I_QUILL
        je .use_quill
        ; USE INKWELL at Ink Pool -> fill it
        cmp edx, I_INKWELL
        je .use_inkwell

        ; --- GENERAL WORLD PUZZLES ---
        ; USE FIELD_GLASS at Frozen Field -> see path
        cmp edx, I_FIELD_GLASS
        je .use_fieldglass
        ; USE MEDAL at General's Sanctum -> unlock key
        cmp edx, I_MEDAL
        je .use_medal

        ; --- ARTIST WORLD PUZZLES ---
        ; USE CANVAS in Studio -> place it
        cmp edx, I_CANVAS
        je .use_canvas
        ; USE BRUSH in Studio after CANVAS -> paint
        cmp edx, I_BRUSH
        je .use_brush

        ; --- SCHOLAR WORLD PUZZLES ---
        ; USE LENS in Mirror Hall -> reveal truth
        cmp edx, I_LENS
        je .use_lens
        ; USE GEAR at Clock Tower -> fix mechanism
        cmp edx, I_GEAR
        je .use_gear

        ; Default: can't use here
        jmp .use_no_effect

; --- Poet Puzzles ---
.use_quill:
        cmp dword [current_room], R_POET_SANCTUM
        jne .use_no_effect
        ; Need filled inkwell and torn page
        cmp byte [flags + F_INK_FILLED], 1
        jne .use_need_ink
        cmp byte [item_loc + I_TORN_PAGE], LOC_INVENTORY
        jne .use_need_page
        ; Success! Write the poem, get Poet Key
        mov byte [flags + F_PAGE_WRITTEN], 1
        mov byte [item_loc + I_POET_KEY], LOC_INVENTORY
        inc dword [keys_found]
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_poet_solve
        int 0x80
        call play_key_sound
        call wait_key
        popad
        ret

.use_need_ink:
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_quill_dry
        int 0x80
        call wait_key
        popad
        ret

.use_need_page:
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_need_page
        int 0x80
        call wait_key
        popad
        ret

.use_inkwell:
        cmp dword [current_room], R_INK_POOL
        jne .use_no_effect
        mov byte [flags + F_INK_FILLED], 1
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_ink_filled
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, SND_USE
        mov ecx, 2
        int 0x80
        call wait_key
        popad
        ret

; --- General Puzzles ---
.use_fieldglass:
        cmp dword [current_room], R_FROZEN_FIELD
        jne .use_no_effect
        mov byte [flags + F_FIELD_VIEWED], 1
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_field_viewed
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, SND_USE
        mov ecx, 2
        int 0x80
        call wait_key
        popad
        ret

.use_medal:
        cmp dword [current_room], R_GEN_SANCTUM
        jne .use_no_effect
        cmp byte [flags + F_FIELD_VIEWED], 1
        jne .use_need_view
        ; Success! Honor the fallen, get General Key
        mov byte [flags + F_MEDAL_PLACED], 1
        mov byte [item_loc + I_GEN_KEY], LOC_INVENTORY
        inc dword [keys_found]
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_gen_solve
        int 0x80
        call play_key_sound
        call wait_key
        popad
        ret

.use_need_view:
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_medal_reject
        int 0x80
        call wait_key
        popad
        ret

; --- Artist Puzzles ---
.use_canvas:
        cmp dword [current_room], R_STUDIO
        jne .use_no_effect
        mov byte [flags + F_CANVAS_PLACED], 1
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_canvas_placed
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, SND_USE
        mov ecx, 2
        int 0x80
        call wait_key
        popad
        ret

.use_brush:
        cmp dword [current_room], R_STUDIO
        jne .use_no_effect
        cmp byte [flags + F_CANVAS_PLACED], 1
        jne .use_need_canvas
        cmp byte [item_loc + I_PALETTE], LOC_INVENTORY
        jne .use_need_palette
        ; Success! Paint the masterwork, get Art Key
        mov byte [flags + F_PAINTED], 1
        mov byte [item_loc + I_ART_KEY], LOC_INVENTORY
        inc dword [keys_found]
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_art_solve
        int 0x80
        call play_key_sound
        call wait_key
        popad
        ret

.use_need_canvas:
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_need_canvas
        int 0x80
        call wait_key
        popad
        ret

.use_need_palette:
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_need_palette
        int 0x80
        call wait_key
        popad
        ret

; --- Scholar Puzzles ---
.use_lens:
        cmp dword [current_room], R_MIRROR_HALL
        jne .use_no_effect
        mov byte [flags + F_LENS_USED], 1
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_lens_used
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, SND_USE
        mov ecx, 2
        int 0x80
        call wait_key
        popad
        ret

.use_gear:
        cmp dword [current_room], R_CLOCK_TOWER
        jne .use_no_effect
        cmp byte [flags + F_LENS_USED], 1
        jne .use_need_lens
        cmp byte [item_loc + I_CODEX], LOC_INVENTORY
        jne .use_need_codex
        ; Success! Fix the clock mechanism, get Scholar Key
        mov byte [flags + F_GEAR_PLACED], 1
        mov byte [item_loc + I_SCH_KEY], LOC_INVENTORY
        inc dword [keys_found]
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_sch_solve
        int 0x80
        call play_key_sound
        call wait_key
        popad
        ret

.use_need_lens:
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_need_mirror
        int 0x80
        call wait_key
        popad
        ret

.use_need_codex:
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_need_codex
        int 0x80
        call wait_key
        popad
        ret

.use_no_effect:
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_no_effect
        int 0x80
        call wait_key
        popad
        ret

.use_not_found:
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_dont_have
        int 0x80
        call wait_key
        popad
        ret

;=======================================================================
; INVENTORY
;=======================================================================
cmd_inventory:
        pushad
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_inv_header
        int 0x80

        xor edx, edx
        xor esi, esi           ; count
.inv_loop:
        cmp edx, NUM_ITEMS
        jge .inv_done
        cmp byte [item_loc + edx], LOC_INVENTORY
        jne .inv_next

        inc esi
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_item_bullet
        int 0x80
        mov eax, edx
        imul eax, 4
        mov ebx, [item_name_table + eax]
        mov eax, SYS_PRINT
        int 0x80
        ; If it's a key, mark it special
        cmp edx, I_POET_KEY
        je .inv_key_mark
        cmp edx, I_GEN_KEY
        je .inv_key_mark
        cmp edx, I_ART_KEY
        je .inv_key_mark
        cmp edx, I_SCH_KEY
        je .inv_key_mark
        jmp .inv_no_mark
.inv_key_mark:
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_key_mark
        int 0x80
.inv_no_mark:
        mov eax, SYS_PRINT
        mov ebx, str_newline
        int 0x80

.inv_next:
        inc edx
        jmp .inv_loop

.inv_done:
        cmp esi, 0
        jne .inv_has_items
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_inv_empty
        int 0x80
.inv_has_items:
        call wait_key
        popad
        ret

;=======================================================================
; TALK COMMAND
;=======================================================================
cmd_talk:
        pushad
        mov eax, [current_room]

        ; Poet's ghost (in sanctum or gate)
        cmp eax, R_POET_GATE
        je .talk_poet
        cmp eax, R_POET_SANCTUM
        je .talk_poet

        ; General's ghost
        cmp eax, R_GEN_GATE
        je .talk_general
        cmp eax, R_GEN_SANCTUM
        je .talk_general

        ; Artist's ghost
        cmp eax, R_ART_GATE
        je .talk_artist
        cmp eax, R_ART_SANCTUM
        je .talk_artist

        ; Scholar's ghost
        cmp eax, R_SCH_GATE
        je .talk_scholar
        cmp eax, R_SCH_SANCTUM
        je .talk_scholar

        ; Nexus: ATLAS speaks
        cmp eax, R_NEXUS
        je .talk_atlas

        ; Nobody here
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_nobody
        int 0x80
        call wait_key
        popad
        ret

.talk_atlas:
        mov eax, SYS_BEEP
        mov ebx, SND_TALK
        mov ecx, 3
        int 0x80
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LMAGENTA
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_atlas_talk
        int 0x80
        call wait_key
        popad
        ret

.talk_poet:
        mov byte [flags + F_POET_TALKED], 1
        mov eax, SYS_BEEP
        mov ebx, SND_TALK
        mov ecx, 3
        int 0x80
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_poet_talk
        int 0x80
        call wait_key
        popad
        ret

.talk_general:
        mov byte [flags + F_GEN_TALKED], 1
        mov eax, SYS_BEEP
        mov ebx, SND_TALK
        mov ecx, 3
        int 0x80
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_gen_talk
        int 0x80
        call wait_key
        popad
        ret

.talk_artist:
        mov byte [flags + F_ART_TALKED], 1
        mov eax, SYS_BEEP
        mov ebx, SND_TALK
        mov ecx, 3
        int 0x80
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_art_talk
        int 0x80
        call wait_key
        popad
        ret

.talk_scholar:
        mov byte [flags + F_SCH_TALKED], 1
        mov eax, SYS_BEEP
        mov ebx, SND_TALK
        mov ecx, 3
        int 0x80
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_sch_talk
        int 0x80
        call wait_key
        popad
        ret

;=======================================================================
; HELP SCREEN
;=======================================================================
show_help:
        pushad
        call print_msg_line
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_help_text
        int 0x80
        call wait_key
        popad
        ret

;=======================================================================
; GAME WIN
;=======================================================================
game_win:
        mov eax, SYS_CLEAR
        int 0x80

        ; Victory melody
        mov eax, SYS_BEEP
        mov ebx, 523
        mov ecx, 3
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 659
        mov ecx, 3
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 784
        mov ecx, 3
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 1047
        mov ecx, 5
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 1319
        mov ecx, 8
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 10
        mov ecx, 2
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_win_title
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 5
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_win_text1
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 7
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_win_text2
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 9
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_win_text3
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 11
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_win_text4
        int 0x80

        ; Show stats
        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 14
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_win_moves
        int 0x80
        mov eax, [moves]
        call print_number

        ; Rating
        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 16
        int 0x80
        mov eax, [moves]
        cmp eax, 50
        jl .rate_master
        cmp eax, 100
        jl .rate_expert
        cmp eax, 150
        jl .rate_adept
        jmp .rate_novice

.rate_master:
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_rate_master
        int 0x80
        jmp .rate_done
.rate_expert:
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_rate_expert
        int 0x80
        jmp .rate_done
.rate_adept:
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_rate_adept
        int 0x80
        jmp .rate_done
.rate_novice:
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_rate_novice
        int 0x80
.rate_done:

        mov eax, SYS_SETCURSOR
        mov ebx, 18
        mov ecx, 20
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_press_key
        int 0x80
        mov eax, SYS_GETCHAR
        int 0x80

        ; Exit
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================================================
; UTILITY FUNCTIONS
;=======================================================================

;---------------------------------------
; str_starts_with - Check if [ESI] starts with [EDI]
; Returns EAX=1 if match, 0 if not
;---------------------------------------
str_starts_with:
        push esi
        push edi
        push ecx
.sw_loop:
        mov al, [edi]
        cmp al, 0
        je .sw_match            ; pattern ended = match
        mov cl, [esi]
        cmp cl, 0
        je .sw_fail             ; input ended before pattern
        cmp al, cl
        jne .sw_fail
        inc esi
        inc edi
        jmp .sw_loop
.sw_match:
        mov eax, 1
        jmp .sw_done
.sw_fail:
        xor eax, eax
.sw_done:
        pop ecx
        pop edi
        pop esi
        ret

;---------------------------------------
; print_number - Print EAX as decimal
;---------------------------------------
print_number:
        pushad
        mov ecx, 0
        mov ebx, 10
        cmp eax, 0
        jne .pn_nonzero
        mov eax, SYS_PUTCHAR
        mov ebx, '0'
        int 0x80
        popad
        ret
.pn_nonzero:
.pn_div:
        xor edx, edx
        div ebx
        push edx
        inc ecx
        cmp eax, 0
        jne .pn_div
.pn_print:
        pop edx
        add edx, '0'
        push ecx
        mov eax, SYS_PUTCHAR
        mov ebx, edx
        int 0x80
        pop ecx
        dec ecx
        jnz .pn_print
        popad
        ret

;---------------------------------------
; print_msg_line - Position cursor at message area
;---------------------------------------
print_msg_line:
        push eax
        push ebx
        push ecx
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 18
        int 0x80
        pop ecx
        pop ebx
        pop eax
        ret

;---------------------------------------
; wait_key - Show prompt and wait for keypress
;---------------------------------------
wait_key:
        pushad
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 22
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_press_key
        int 0x80
        mov eax, SYS_GETCHAR
        int 0x80
        popad
        ret

;---------------------------------------
; play_key_sound - Triumphant key-found jingle
;---------------------------------------
play_key_sound:
        pushad
        mov eax, SYS_BEEP
        mov ebx, SND_KEY
        mov ecx, 3
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 1200
        mov ecx, 2
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, SND_KEY
        mov ecx, 4
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, SND_WIN
        mov ecx, 6
        int 0x80
        popad
        ret

;---------------------------------------
; random - LCG PRNG, result in EAX
;---------------------------------------
random:
        push ebx
        push edx
        mov eax, [rand_seed]
        imul eax, 1103515245
        add eax, 12345
        mov [rand_seed], eax
        shr eax, 16
        and eax, 0x7FFF
        pop edx
        pop ebx
        ret

;=======================================================================
; DATA SECTION
;=======================================================================

; === Title Screen ===
str_title_border: db 0xC9, "====================================================================", 0xBB, 0
str_title_name:  db "N E U R O V A U L T", 0
str_title_sub:   db "A Literary Interactive Fiction Adventure", 0
str_title_tag:   db "Four minds. Four keys. One chance to save everything.", 0
str_title_footer: db "A Mellivora OS Production  |  Inspired by Mindwheel", 0

brain_art1: db "      _.---._", 0
brain_art2: db "   .'  ___  '.", 0
brain_art3: db "  /  .'   '.  \\", 0
brain_art4: db " |  | () () |  |", 0
brain_art5: db "  \\  '.___.'  /", 0
brain_art6: db "   '._______.'", 0

str_menu_play:   db "[1] Jack In", 0
str_menu_about:  db "[2] About", 0
str_menu_quit:   db "[3] Quit", 0

; === About Screen ===
str_about_title: db "=== ABOUT NEUROVAULT ===", 0

about_line1:  db " ", 0
about_line2:  db "The year is 2147. Humanity's last great artificial intelligence,", 0
about_line3:  db "ATLAS, is dying. Its neural cores hold the uploaded minds of four", 0
about_line4:  db "legendary thinkers -- a Poet, a General, an Artist, and a Scholar.", 0
about_line5:  db " ", 0
about_line6:  db "You are a Neural Diver. Your mission: jack into the decaying", 0
about_line7:  db "mindscape and recover the four Memory Keys before they are lost", 0
about_line8:  db "forever in the digital void.", 0
about_line9:  db " ", 0
about_line10: db "Each mind is a surreal world shaped by its owner's inner life.", 0
about_line11: db "Explore, collect items, solve puzzles, and talk to the ghosts", 0
about_line12: db "of minds long gone.", 0
about_line13: db " ", 0
about_line14: db "Commands: LOOK, GO/N/S/E/W, TAKE, USE, TALK, INVENTORY, HELP", 0
about_line15: db " ", 0
about_line16: db "Inspired by Mindwheel (1984) by Robert Pinsky.", 0

about_lines:
        dd about_line1, about_line2, about_line3, about_line4
        dd about_line5, about_line6, about_line7, about_line8
        dd about_line9, about_line10, about_line11, about_line12
        dd about_line13, about_line14, about_line15, about_line16

; === Intro Sequence ===
intro_line1:  db " ", 0
intro_line2:  db "  The year is 2147.", 0
intro_line3:  db " ", 0
intro_line4:  db "  ATLAS, the last great AI, is dying.", 0
intro_line5:  db "  Its quantum cores flicker like candles in a storm.", 0
intro_line6:  db " ", 0
intro_line7:  db "  Within its fading memory banks lie the uploaded minds", 0
intro_line8:  db "  of four visionaries -- preserved at the moment of death,", 0
intro_line9:  db "  their knowledge the key to humanity's survival.", 0
intro_line10: db " ", 0
intro_line11: db "  You are a Neural Diver.", 0
intro_line12: db "  You must enter the mindscape before it collapses.", 0
intro_line13: db "  Four minds. Four Memory Keys. Time is running out.", 0
intro_line14: db " ", 0

intro_lines:
        dd intro_line1, intro_line2, intro_line3, intro_line4
        dd intro_line5, intro_line6, intro_line7, intro_line8
        dd intro_line9, intro_line10, intro_line11, intro_line12
        dd intro_line13, intro_line14

str_intro_ready: db "Press any key to begin your dive...", 0

; === Room Names ===
rn_nexus:       db "The Neural Nexus", 0
rn_poet_gate:   db "Gate of the Poet", 0
rn_library:     db "The Crumbling Library", 0
rn_verse_hall:  db "Hall of Living Verses", 0
rn_ink_pool:    db "The Pool of Ink", 0
rn_burn_shelf:  db "The Burning Shelves", 0
rn_poet_sanc:   db "The Poet's Sanctum", 0
rn_gen_gate:    db "Gate of the General", 0
rn_trench:      db "The Endless Trench", 0
rn_no_mans:     db "No Man's Land", 0
rn_cmd_post:    db "The Command Post", 0
rn_frozen:      db "The Frozen Battlefield", 0
rn_gen_sanc:    db "The General's Sanctum", 0
rn_art_gate:    db "Gate of the Artist", 0
rn_foyer:       db "The Grand Foyer", 0
rn_portrait:    db "The Portrait Hall", 0
rn_sculpture:   db "The Sculpture Garden", 0
rn_studio:      db "The Studio", 0
rn_art_sanc:    db "The Artist's Sanctum", 0
rn_sch_gate:    db "Gate of the Scholar", 0
rn_archives:    db "The Dusty Archives", 0
rn_puzzle:      db "The Puzzle Chamber", 0
rn_mirror:      db "The Mirror Hall", 0
rn_clock:       db "The Clock Tower", 0
rn_sch_sanc:    db "The Scholar's Sanctum", 0

room_name_table:
        dd rn_nexus, rn_poet_gate, rn_library, rn_verse_hall
        dd rn_ink_pool, rn_burn_shelf, rn_poet_sanc
        dd rn_gen_gate, rn_trench, rn_no_mans, rn_cmd_post
        dd rn_frozen, rn_gen_sanc
        dd rn_art_gate, rn_foyer, rn_portrait, rn_sculpture
        dd rn_studio, rn_art_sanc
        dd rn_sch_gate, rn_archives, rn_puzzle, rn_mirror
        dd rn_clock, rn_sch_sanc

; === Room Descriptions ===
rd_nexus:  db "You float in a vast digital void. Four shimmering portals pulse", 0
rd_nexus2: db "with fading light: Cyan to the NORTH, Red SOUTH, Gold EAST, Blue WEST.", 0

rd_poet_gate:  db "A doorway of swirling letters and half-formed words. The ghost of", 0
rd_poet_gate2: db "the Poet shimmers here, whispering fragments of verse.", 0

rd_library:    db "Towering shelves stretch into darkness, books crumbling to dust as", 0
rd_library2:   db "you watch. Pages flutter like trapped birds. Passages lead N and E.", 0

rd_verse:      db "Words float in the air like luminous fireflies. Some form into", 0
rd_verse2:     db "stanzas before dissolving. A quill pen hovers, waiting for a hand.", 0

rd_ink:        db "A deep pool of liquid midnight. The ink is alive -- swirling with", 0
rd_ink2:       db "half-remembered metaphors and unfinished thoughts.", 0

rd_burn:       db "Shelves ablaze with slow, cold fire. The flames consume knowledge", 0
rd_burn2:      db "but one torn page has caught on a nail, just out of reach of fire.", 0

rd_poet_sanc:  db "A circular chamber of pure white light. An empty pedestal bears", 0
rd_poet_sanc2: db "the inscription: 'Write truth, and truth shall set the key free.'", 0

rd_gen_gate:   db "Iron gates scarred by shrapnel. The ghost of the General stands at", 0
rd_gen_gate2:  db "attention, medals gleaming on a phantom chest.", 0

rd_trench:     db "Muddy walls rise on either side. The smell of cordite and rain.", 0
rd_trench2:    db "Soldiers frozen mid-step line the trench like statues of sorrow.", 0

rd_no_mans:    db "A desolate wasteland between the trenches. Barbed wire catches the", 0
rd_no_mans2:   db "pale light. Something glints among the fallen -- a medal of valor.", 0

rd_cmd_post:   db "Maps cover every surface, pins marking battles long forgotten. A", 0
rd_cmd_post2:  db "pair of field glasses rests on the strategy table.", 0

rd_frozen:     db "Time itself is frozen here. Soldiers and shells hang suspended in", 0
rd_frozen2:    db "mid-air. The truth of war waits to be witnessed.", 0

rd_gen_sanc:   db "A solemn memorial hall. Names of the fallen cover the walls from", 0
rd_gen_sanc2:  db "floor to ceiling. A niche awaits an offering of honor.", 0

rd_art_gate:   db "An archway dripping with liquid color. The ghost of the Artist", 0
rd_art_gate2:  db "appears in shifting hues, a living impressionist painting.", 0

rd_foyer:      db "A grand entrance hall with marble floors splashed with paint. Empty", 0
rd_foyer2:     db "frames line the walls. A fine brush lies abandoned by the door.", 0

rd_portrait:   db "Portraits of people who never existed stare from the walls. Their", 0
rd_portrait2:  db "eyes follow you, their mouths move in silent conversation.", 0

rd_sculpture:  db "Statues twist and reshape themselves endlessly. A palette of", 0
rd_sculpture2: db "impossible colors sits on a stone bench, vibrating with potential.", 0

rd_studio:     db "The heart of creation. An empty easel stands in golden light,", 0
rd_studio2:    db "waiting for canvas, color, and vision to converge.", 0

rd_art_sanc:   db "A chamber of pure emotion made visible. Colors pulse like", 0
rd_art_sanc2:  db "heartbeats. The air hums: 'Create what has never been seen.'", 0

rd_sch_gate:   db "A geometric archway of interlocking equations. The ghost of the", 0
rd_sch_gate2:  db "Scholar adjusts spectral spectacles and beckons you inward.", 0

rd_archives:   db "Endless rows of scrolls and tablets. Knowledge from a thousand", 0
rd_archives2:  db "civilizations. A strange crystalline lens catches the light.", 0

rd_puzzle:     db "The floor is a grid of sliding tiles. Symbols shift and rearrange.", 0
rd_puzzle2:    db "A leather-bound codex sits on a pedestal, its pages full of logic.", 0

rd_mirror:     db "Reflections that don't match reality. The mirrors show what was,", 0
rd_mirror2:    db "what is, and what could be -- all at once, endlessly overlapping.", 0

rd_clock:      db "A vast clockwork mechanism, gears frozen mid-turn. One gear is", 0
rd_clock2:     db "missing from the central shaft. Time waits to resume.", 0

rd_sch_sanc:   db "A chamber of perfect order. Every surface is covered in proofs", 0
rd_sch_sanc2:  db "and theorems. Inscribed: 'Truth is the mechanism that moves all.'", 0

room_desc_table:
        dd rd_nexus, rd_poet_gate, rd_library, rd_verse, rd_ink, rd_burn
        dd rd_poet_sanc
        dd rd_gen_gate, rd_trench, rd_no_mans, rd_cmd_post, rd_frozen
        dd rd_gen_sanc
        dd rd_art_gate, rd_foyer, rd_portrait, rd_sculpture, rd_studio
        dd rd_art_sanc
        dd rd_sch_gate, rd_archives, rd_puzzle, rd_mirror, rd_clock
        dd rd_sch_sanc

room_desc2_table:
        dd rd_nexus2, rd_poet_gate2, rd_library2, rd_verse2, rd_ink2, rd_burn2
        dd rd_poet_sanc2
        dd rd_gen_gate2, rd_trench2, rd_no_mans2, rd_cmd_post2, rd_frozen2
        dd rd_gen_sanc2
        dd rd_art_gate2, rd_foyer2, rd_portrait2, rd_sculpture2, rd_studio2
        dd rd_art_sanc2
        dd rd_sch_gate2, rd_archives2, rd_puzzle2, rd_mirror2, rd_clock2
        dd rd_sch_sanc2

; === Room Exits ===
; Each room has 4 dwords: [N, S, E, W] -- 0xFF = no exit
; Macro for readability
%define X 0xFF

room_exits:
        ; R_NEXUS (0): N=Poet, S=General, E=Artist, W=Scholar
        dd R_POET_GATE, R_GEN_GATE, R_ART_GATE, R_SCH_GATE

        ; Poet's Mind (1-6)
        ; R_POET_GATE (1): N=Library, S=Nexus
        dd R_LIBRARY, R_NEXUS, X, X
        ; R_LIBRARY (2): N=Verse, S=PoetGate, E=InkPool
        dd R_VERSE_HALL, R_POET_GATE, R_INK_POOL, X
        ; R_VERSE_HALL (3): S=Library, E=BurningShelf
        dd X, R_LIBRARY, R_BURNING_SHELF, X
        ; R_INK_POOL (4): W=Library, N=PoetSanctum
        dd R_POET_SANCTUM, X, X, R_LIBRARY
        ; R_BURNING_SHELF (5): W=VerseHall
        dd X, X, X, R_VERSE_HALL
        ; R_POET_SANCTUM (6): S=InkPool
        dd X, R_INK_POOL, X, X

        ; General's Mind (7-12)
        ; R_GEN_GATE (7): N=Nexus, S=Trench
        dd R_NEXUS, R_TRENCH, X, X
        ; R_TRENCH (8): N=GenGate, S=NoMans, E=CmdPost
        dd R_GEN_GATE, R_NO_MANS, R_COMMAND_POST, X
        ; R_NO_MANS (9): N=Trench, S=FrozenField
        dd R_TRENCH, R_FROZEN_FIELD, X, X
        ; R_COMMAND_POST (10): W=Trench, S=GenSanctum
        dd X, R_GEN_SANCTUM, X, R_TRENCH
        ; R_FROZEN_FIELD (11): N=NoMans
        dd R_NO_MANS, X, X, X
        ; R_GEN_SANCTUM (12): N=CmdPost
        dd R_COMMAND_POST, X, X, X

        ; Artist's Mind (13-18)
        ; R_ART_GATE (13): W=Nexus, E=Foyer
        dd X, X, R_FOYER, R_NEXUS
        ; R_FOYER (14): W=ArtGate, N=Portrait, E=Sculpture
        dd R_PORTRAIT_HALL, X, R_SCULPTURE_GDN, R_ART_GATE
        ; R_PORTRAIT_HALL (15): S=Foyer, E=Studio
        dd X, R_FOYER, R_STUDIO, X
        ; R_SCULPTURE_GDN (16): W=Foyer, N=ArtSanctum
        dd R_ART_SANCTUM, X, X, R_FOYER
        ; R_STUDIO (17): W=Portrait
        dd X, X, X, R_PORTRAIT_HALL
        ; R_ART_SANCTUM (18): S=SculptureGdn
        dd X, R_SCULPTURE_GDN, X, X

        ; Scholar's Mind (19-24)
        ; R_SCH_GATE (19): E=Nexus, W=Archives
        dd X, X, R_NEXUS, R_ARCHIVES
        ; R_ARCHIVES (20): E=SchGate, N=Puzzle, W=MirrorHall
        dd R_PUZZLE_ROOM, X, R_SCH_GATE, R_MIRROR_HALL
        ; R_PUZZLE_ROOM (21): S=Archives, N=SchSanctum
        dd R_SCH_SANCTUM, R_ARCHIVES, X, X
        ; R_MIRROR_HALL (22): E=Archives, N=ClockTower
        dd R_CLOCK_TOWER, X, R_ARCHIVES, X
        ; R_CLOCK_TOWER (23): S=MirrorHall
        dd X, R_MIRROR_HALL, X, X
        ; R_SCH_SANCTUM (24): S=PuzzleRoom
        dd X, R_PUZZLE_ROOM, X, X

; === Item Names ===
in_quill:       db "a shimmering quill pen", 0
in_inkwell:     db "an empty crystal inkwell", 0
in_torn_page:   db "a singed torn page", 0
in_poet_key:    db "Memory Key of the Poet", 0
in_compass:     db "a battered trench compass", 0
in_medal:       db "a medal of valor", 0
in_fieldglass:  db "a pair of field glasses", 0
in_gen_key:     db "Memory Key of the General", 0
in_brush:       db "a fine sable brush", 0
in_canvas:      db "a blank canvas", 0
in_palette:     db "a palette of impossible colors", 0
in_art_key:     db "Memory Key of the Artist", 0
in_lens:        db "a crystalline lens", 0
in_codex:       db "a leather-bound codex", 0
in_gear:        db "a brass clockwork gear", 0
in_sch_key:     db "Memory Key of the Scholar", 0

item_name_table:
        dd in_quill, in_inkwell, in_torn_page, in_poet_key
        dd in_compass, in_medal, in_fieldglass, in_gen_key
        dd in_brush, in_canvas, in_palette, in_art_key
        dd in_lens, in_codex, in_gear, in_sch_key

; === Item Keywords (for parser matching) ===
ik_quill:       db "QUILL", 0
ik_inkwell:     db "INKWELL", 0
ik_page:        db "PAGE", 0
ik_poetkey:     db "POET", 0
ik_compass:     db "COMPASS", 0
ik_medal:       db "MEDAL", 0
ik_fieldglass:  db "FIELD", 0
ik_genkey:      db "GENERAL", 0
ik_brush:       db "BRUSH", 0
ik_canvas:      db "CANVAS", 0
ik_palette:     db "PALETTE", 0
ik_artkey:      db "ARTIST", 0
ik_lens:        db "LENS", 0
ik_codex:       db "CODEX", 0
ik_gear:        db "GEAR", 0
ik_schkey:      db "SCHOLAR", 0

item_keyword_table:
        dd ik_quill, ik_inkwell, ik_page, ik_poetkey
        dd ik_compass, ik_medal, ik_fieldglass, ik_genkey
        dd ik_brush, ik_canvas, ik_palette, ik_artkey
        dd ik_lens, ik_codex, ik_gear, ik_schkey

; === Command Strings ===
str_cmd_look:   db "LOOK", 0
str_cmd_north:  db "NORTH", 0
str_cmd_south:  db "SOUTH", 0
str_cmd_east:   db "EAST", 0
str_cmd_west:   db "WEST", 0
str_cmd_go:     db "GO ", 0
str_cmd_take:   db "TAKE ", 0
str_cmd_get:    db "GET ", 0
str_cmd_use:    db "USE ", 0
str_cmd_inv:    db "INVENTORY", 0
str_cmd_talk:   db "TALK", 0
str_cmd_help:   db "HELP", 0
str_cmd_quit:   db "QUIT", 0

; === UI Strings ===
str_prompt:     db "> ", 0
str_room_sep:   db "----------------------------------------------", 0
str_input_sep:  db "________________________________________________________________________________", 0
str_you_see:    db "You can see:", 10, 0
str_item_bullet: db "  * ", 0
str_exits:      db "Exits: ", 0
str_dir_n:      db "[N] ", 0
str_dir_s:      db "[S] ", 0
str_dir_e:      db "[E] ", 0
str_dir_w:      db "[W] ", 0
str_newline:    db 10, 0
str_period:     db ".", 0
str_press_key:  db "Press any key to continue...", 0

str_status_title: db "NEUROVAULT", 0
str_status_keys:  db "Keys: ", 0
str_of_four:      db "/4", 0
str_status_moves: db "Moves: ", 0

; === Messages ===
str_unknown_cmd: db "I don't understand that command. Type HELP for a list.", 0
str_no_exit:     db "You can't go that way.", 0
str_go_where:    db "Go where? Try: GO NORTH, GO SOUTH, GO EAST, GO WEST", 0
str_taken:       db "Taken: ", 0
str_take_what:   db "There's nothing here by that name to take.", 0
str_no_effect:   db "You can't use that here. Try a different place.", 0
str_dont_have:   db "You don't have that item.", 0
str_nobody:      db "There is no one here to talk to.", 0
str_quit_confirm: db "Leave the mindscape? The keys may be lost forever. (Y/N) ", 0

str_inv_header:  db "You are carrying:", 10, 0
str_inv_empty:   db "  (nothing)", 0
str_key_mark:    db " [MEMORY KEY]", 0

; === NPC Dialog ===
str_atlas_talk: db "ATLAS speaks in fracturing tones: 'Diver... the minds are", 10
                db "  collapsing. Four keys, one in each mind. Find them before", 10
                db "  the last light goes out. North, South, East, West... hurry.'", 0

str_poet_talk:  db "The Poet's ghost recites: 'My words are fading.  To restore my", 10
                db "  key, you must write Truth on the torn page. Fill the inkwell", 10
                db "  at the Pool. Find my quill. Write in my Sanctum.'", 0

str_gen_talk:   db "The General salutes: 'I have seen too much death. My key is", 10
                db "  locked behind honor. Witness the frozen truth of battle,", 10
                db "  then place the medal of valor in my Sanctum.'", 0

str_art_talk:   db "The Artist swirls into form: 'Creation is the only key.", 10
                db "  Set canvas upon the easel in my Studio. Find the palette", 10
                db "  of true colors, take up the brush, and paint what never was.'", 0

str_sch_talk:   db "The Scholar peers over spectral spectacles: 'Logic unlocks all.", 10
                db "  The mirror shows truth to those with the right lens. The clock", 10
                db "  awaits its missing gear -- and the codex holds the pattern.'", 0

; === Puzzle Messages ===
str_quill_dry:   db "The quill moves across the page but leaves no mark. You need ink.", 0
str_need_page:   db "The quill writes in air. You need something to write upon.", 0
str_ink_filled:  db "You dip the inkwell into the living ink. It fills with shimmering", 10
                 db "  midnight liquid, alive with half-remembered dreams.", 0
str_poet_solve:  db "The quill dances across the torn page! Words of truth flow like", 10
                 db "  light. The page glows, transforms -- the POET'S MEMORY KEY appears!", 0

str_field_viewed: db "Through the field glasses, time unfolds. You see the General's", 10
                  db "  last stand -- courage amid chaos, sacrifice for those behind.", 0
str_medal_reject: db "The niche resists. You have not yet witnessed the truth of battle.", 0
str_gen_solve:   db "You place the medal in the niche. The names on the walls glow with", 10
                 db "  golden light. The GENERAL'S MEMORY KEY materializes in your hand!", 0

str_canvas_placed: db "You set the blank canvas upon the easel. It catches the golden", 10
                   db "  light of the Studio, waiting for the spark of creation.", 0
str_need_canvas: db "The brush moves but there is nothing to paint upon.", 0
str_need_palette: db "The canvas is ready, but you need colors to paint with.", 0
str_art_solve:  db "Brush meets palette meets canvas -- a masterwork blooms into being!", 10
                db "  Colors never seen before swirl into form. The ARTIST'S MEMORY KEY appears!", 0

str_lens_used:  db "The crystalline lens refracts the mirror's light. Hidden symbols", 10
                db "  appear on every surface -- a secret mechanism revealed!", 0
str_need_mirror: db "The gear doesn't fit anywhere here. You haven't found where it goes.", 0
str_need_codex:  db "The mechanism needs a precise pattern. Something with instructions.", 0
str_sch_solve:  db "Following the codex's pattern, you insert the gear. The clock", 10
                db "  resumes! Time itself bows. The SCHOLAR'S MEMORY KEY appears!", 0

; === Help Text ===
str_help_text: db "Commands:", 10
               db "  LOOK          - Examine the current room", 10
               db "  N/S/E/W       - Move in a direction (or GO NORTH, etc)", 10
               db "  TAKE <item>   - Pick up an item (e.g., TAKE QUILL)", 10
               db "  USE <item>    - Use an item in the current room", 10
               db "  TALK          - Speak to anyone present", 10
               db "  INVENTORY (I) - Check what you're carrying", 10
               db "  HELP          - Show this help", 10
               db "  QUIT          - Leave the game", 10
               db "  Tip: Talk to ghosts for hints on solving puzzles!", 0

; === Win Screen ===
str_win_title:  db "*** ALL FOUR MEMORY KEYS RECOVERED ***", 0

str_win_text1: db "The Neural Nexus blazes with light. Four keys orbit the central", 0
str_win_text2: db "core, locking into place. ATLAS draws a shuddering breath as the", 0
str_win_text3: db "knowledge of four great minds floods back into its neural paths.", 0
str_win_text4: db "'You did it, Diver. Humanity's legacy... is saved.'", 0

str_win_moves:  db "Total moves: ", 0
str_rate_master: db "Rating: NEURAL MASTER - Incredibly efficient!", 0
str_rate_expert: db "Rating: EXPERT DIVER  - Well navigated!", 0
str_rate_adept:  db "Rating: ADEPT         - Solid exploration.", 0
str_rate_novice: db "Rating: WANDERER      - The scenic route, but you made it!", 0

;=======================================================================
; BSS - Game State
;=======================================================================
section .bss

rand_seed:      resd 1
current_room:   resd 1
keys_found:     resd 1
moves:          resd 1
input_len:      resd 1
item_loc:       resb NUM_ITEMS          ; Location of each item
flags:          resb NUM_FLAGS          ; Game flags
input_buf:      resb INPUT_MAX          ; Player input
cmd_buf:        resb INPUT_MAX          ; Uppercased copy
