;=======================================================================
; OUTPOST - A Space Colony Simulation
; A modern reimagining of the classic KINGDOM (Hamurabi) game
;
; You are the Commander of humanity's first colony on Kepler-442b.
; Manage colonists, food, energy, and territory over 10 years to
; build a thriving settlement -- or watch it collapse.
;
; Each year you must decide:
;   - Buy or sell land (hectares) at fluctuating prices
;   - Allocate food rations to your colonists
;   - Assign colonists to plant and tend crops
;   - Invest energy into colony defenses and infrastructure
;
; Random events: dust storms, alien artifacts, plagues, solar flares,
; bountiful harvests, supply ship arrivals, and more.
;=======================================================================

%include "syscalls.inc"

;-----------------------------------------------------------------------
; Constants
;-----------------------------------------------------------------------
; Colors
C_BLACK     equ 0x00
C_BLUE      equ 0x01
C_GREEN     equ 0x02
C_CYAN      equ 0x03
C_RED       equ 0x04
C_MAGENTA   equ 0x05
C_BROWN     equ 0x06
C_LGRAY     equ 0x07
C_DGRAY     equ 0x08
C_LBLUE     equ 0x09
C_LGREEN    equ 0x0A
C_LCYAN     equ 0x0B
C_LRED      equ 0x0C
C_LMAGENTA  equ 0x0D
C_YELLOW    equ 0x0E
C_WHITE     equ 0x0F

; Game parameters
START_POP       equ 100
START_FOOD      equ 2800
START_LAND      equ 1000
START_ENERGY    equ 500
MAX_YEARS       equ 10
FOOD_PER_PERSON equ 20          ; units of food per colonist per year
SEED_PER_HECTARE equ 2          ; food units needed to seed 1 hectare
HECTARES_PER_COLONIST equ 10    ; max hectares one colonist can farm
INPUT_MAX       equ 12

; Event IDs
EVT_NONE        equ 0
EVT_DUST_STORM  equ 1
EVT_PLAGUE      equ 2
EVT_SOLAR_FLARE equ 3
EVT_ARTIFACT    equ 4
EVT_BOUNTIFUL   equ 5
EVT_SUPPLY_SHIP equ 6
EVT_PESTS       equ 7
EVT_DISCOVERY   equ 8

;=======================================================================
; ENTRY POINT
;=======================================================================
start:
        ; Seed PRNG from system time
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_seed], eax

        call show_title

.title_loop:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, '1'
        je .start_game
        cmp al, '2'
        je .show_how
        cmp al, '3'
        je .quit
        cmp al, 27
        je .quit
        jmp .title_loop

.show_how:
        call show_howto
        call show_title
        jmp .title_loop

.start_game:
        call init_game
        call show_intro
        jmp year_loop

.quit:
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================================================
; TITLE SCREEN
;=======================================================================
show_title:
        pushad
        mov eax, SYS_CLEAR
        int 0x80

        ; Top border
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 1
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_border
        int 0x80

        ; Title
        mov eax, SYS_SETCURSOR
        mov ebx, 18
        mov ecx, 3
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_title1
        int 0x80

        ; Subtitle
        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 5
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_title2
        int 0x80

        ; Planet art
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov esi, planet_art
        mov edx, 7
        mov ecx, 7
.art_loop:
        push ecx
        push edx
        mov eax, SYS_SETCURSOR
        mov ebx, 28
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, [esi]
        int 0x80
        add esi, 4
        pop edx
        pop ecx
        inc ecx
        dec edx
        jnz .art_loop

        ; Bottom border
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 15
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_border
        int 0x80

        ; Menu
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 26
        mov ecx, 17
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_menu1
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 26
        mov ecx, 18
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_menu2
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 26
        mov ecx, 19
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_menu3
        int 0x80

        ; Footer
        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 23
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_footer
        int 0x80

        ; Title sound
        mov eax, SYS_BEEP
        mov ebx, 330
        mov ecx, 3
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 440
        mov ecx, 3
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 554
        mov ecx, 5
        int 0x80

        popad
        ret

;=======================================================================
; HOW TO PLAY
;=======================================================================
show_howto:
        pushad
        mov eax, SYS_CLEAR
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 20
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_howto_title
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80

        mov esi, howto_lines
        mov ecx, 2
        mov edx, 18
.howto_loop:
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
        jnz .howto_loop

        mov eax, SYS_SETCURSOR
        mov ebx, 18
        mov ecx, 23
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
; INTRO
;=======================================================================
show_intro:
        pushad
        mov eax, SYS_CLEAR
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80

        mov esi, intro_lines
        mov edx, 12
        xor ecx, ecx
.intro_loop:
        push ecx
        push edx
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, [esi]
        int 0x80
        add esi, 4

        mov eax, SYS_SLEEP
        mov ebx, 25
        int 0x80

        pop edx
        pop ecx
        inc ecx
        dec edx
        jnz .intro_loop

        mov eax, SYS_BEEP
        mov ebx, 440
        mov ecx, 5
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 16
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_press_begin
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
        mov dword [population], START_POP
        mov dword [food], START_FOOD
        mov dword [land], START_LAND
        mov dword [energy], START_ENERGY
        mov dword [year], 1
        mov dword [total_starved], 0
        mov dword [total_immigrants], 0
        mov dword [max_starved_pct], 0
        mov dword [land_price], 20
        mov dword [harvest_yield], 3
        mov dword [plague_flag], 0
        popad
        ret

;=======================================================================
; MAIN YEAR LOOP
;=======================================================================
year_loop:
        ; Check if game is over
        mov eax, [year]
        cmp eax, MAX_YEARS + 1
        jg game_over_good

        ; Check if colony collapsed
        cmp dword [population], 0
        jle game_over_dead

        ; Generate new land price (17-28 food per hectare)
        call random
        xor edx, edx
        mov ebx, 12
        div ebx
        add edx, 17
        mov [land_price], edx

        ; Show status report
        call show_status

        ; --- Decision Phase ---
        ; 1. Buy/Sell land
        call phase_land

        ; 2. Feed colonists
        call phase_feed

        ; 3. Plant crops
        call phase_plant

        ; --- Simulation Phase ---
        call simulate_year

        ; Advance year
        inc dword [year]
        jmp year_loop

;=======================================================================
; STATUS REPORT
;=======================================================================
show_status:
        pushad
        mov eax, SYS_CLEAR
        int 0x80

        ; Status bar
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE | 0x10
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

        mov eax, SYS_SETCURSOR
        mov ebx, 2
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_outpost
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 60
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_year_lbl
        int 0x80
        mov eax, [year]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_of_ten
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80

        ; Report header
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 2
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_status_hdr
        int 0x80

        ; Separator
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 3
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_separator
        int 0x80

        ; Population
        mov eax, SYS_SETCURSOR
        mov ebx, 7
        mov ecx, 5
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_pop_lbl
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, [population]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_colonists
        int 0x80

        ; Population bar
        mov eax, SYS_SETCURSOR
        mov ebx, 45
        mov ecx, 5
        int 0x80
        mov eax, [population]
        mov ebx, 10
        call draw_mini_bar

        ; Food
        mov eax, SYS_SETCURSOR
        mov ebx, 7
        mov ecx, 7
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_food_lbl
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, [food]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_units
        int 0x80

        ; Food bar
        mov eax, SYS_SETCURSOR
        mov ebx, 45
        mov ecx, 7
        int 0x80
        mov eax, [food]
        mov ebx, 200
        call draw_mini_bar

        ; Land
        mov eax, SYS_SETCURSOR
        mov ebx, 7
        mov ecx, 9
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_BROWN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_land_lbl
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, [land]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_hectares
        int 0x80

        ; Land bar
        mov eax, SYS_SETCURSOR
        mov ebx, 45
        mov ecx, 9
        int 0x80
        mov eax, [land]
        mov ebx, 100
        call draw_mini_bar

        ; Energy
        mov eax, SYS_SETCURSOR
        mov ebx, 7
        mov ecx, 11
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_energy_lbl
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, [energy]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_cells
        int 0x80

        ; Land price
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 13
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_separator
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 7
        mov ecx, 14
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_price_lbl
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, [land_price]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_food_per_ha
        int 0x80

        ; Show last year's event if any
        cmp dword [last_event], EVT_NONE
        je .no_event_msg
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 16
        int 0x80
        mov eax, [last_event]
        imul eax, 4
        mov ebx, [event_msg_table + eax]
        cmp ebx, 0
        je .no_event_msg
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, [last_event]
        imul eax, 4
        mov ebx, [event_msg_table + eax]
        mov eax, SYS_PRINT
        int 0x80
.no_event_msg:

        ; Show starved/immigrants from last year
        cmp dword [year], 1
        je .skip_last_year

        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 18
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80

        cmp dword [last_starved], 0
        je .no_starved_msg
        mov eax, SYS_PRINT
        mov ebx, str_starved_pre
        int 0x80
        mov eax, [last_starved]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_starved_suf
        int 0x80
.no_starved_msg:

        cmp dword [last_immigrants], 0
        je .no_immig_msg
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 19
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_immig_pre
        int 0x80
        mov eax, [last_immigrants]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_immig_suf
        int 0x80
.no_immig_msg:

.skip_last_year:
        ; Prompt to continue
        mov eax, SYS_SETCURSOR
        mov ebx, 18
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
; draw_mini_bar: EAX=value, EBX=scale (value/scale = bars, max 20)
;---------------------------------------
draw_mini_bar:
        pushad
        ; bars = min(value / scale, 20)
        xor edx, edx
        div ebx
        cmp eax, 20
        jle .bar_ok
        mov eax, 20
.bar_ok:
        mov ecx, eax
        cmp ecx, 0
        je .bar_done
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
.bar_loop:
        mov eax, SYS_PUTCHAR
        mov ebx, 0xDB         ; solid block
        int 0x80
        dec ecx
        jnz .bar_loop
.bar_done:
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        popad
        ret

;=======================================================================
; PHASE 1: BUY/SELL LAND
;=======================================================================
phase_land:
        pushad
        mov eax, SYS_CLEAR
        int 0x80

        call draw_phase_header
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 2
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_phase_land
        int 0x80

        ; Show current stats
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 4
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_you_have
        int 0x80
        mov eax, [land]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_hectares_and
        int 0x80
        mov eax, [food]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_food_stored
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 5
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_land_costs
        int 0x80
        mov eax, [land_price]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_food_per_ha2
        int 0x80

        ; Ask to buy
.ask_buy:
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 7
        int 0x80
        ; Clear line
        mov eax, SYS_PRINT
        mov ebx, str_clear_line
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 7
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_buy_land
        int 0x80

        call read_number
        mov [tmp_val], eax

        ; Validate: cost = amount * land_price
        cmp eax, 0
        je .ask_sell            ; 0 = skip buying

        mov ebx, [land_price]
        imul eax, ebx
        cmp eax, [food]
        jle .buy_ok

        ; Can't afford
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 9
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_cant_afford
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 200
        mov ecx, 2
        int 0x80
        jmp .ask_buy

.buy_ok:
        ; Apply purchase
        mov eax, [tmp_val]
        add [land], eax
        mov ebx, [land_price]
        imul eax, ebx
        sub [food], eax

        mov eax, SYS_BEEP
        mov ebx, 800
        mov ecx, 2
        int 0x80

        jmp .land_done

.ask_sell:
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 9
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_clear_line
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 9
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_sell_land
        int 0x80

        call read_number
        cmp eax, 0
        je .land_done

        ; Can't sell more than you have (keep at least 1)
        mov ebx, [land]
        dec ebx
        cmp eax, ebx
        jle .sell_ok

        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 11
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_not_enough_land
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 200
        mov ecx, 2
        int 0x80
        jmp .ask_sell

.sell_ok:
        ; Apply sale
        sub [land], eax
        mov ebx, [land_price]
        imul eax, ebx
        add [food], eax

        mov eax, SYS_BEEP
        mov ebx, 800
        mov ecx, 2
        int 0x80

.land_done:
        popad
        ret

;=======================================================================
; PHASE 2: FEED COLONISTS
;=======================================================================
phase_feed:
        pushad
        mov eax, SYS_CLEAR
        int 0x80

        call draw_phase_header
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 2
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_phase_feed
        int 0x80

        ; Show info
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 4
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_pop_is
        int 0x80
        mov eax, [population]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_need
        int 0x80
        ; calculate minimum
        mov eax, [population]
        imul eax, FOOD_PER_PERSON
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_food_min
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 5
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_food_avail
        int 0x80
        mov eax, [food]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_units
        int 0x80

.ask_feed:
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 7
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_clear_line
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 7
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_feed_how
        int 0x80

        call read_number
        mov [food_fed], eax

        ; Can't feed more than available
        cmp eax, [food]
        jle .feed_ok

        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 9
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_not_enough_food
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 200
        mov ecx, 2
        int 0x80
        jmp .ask_feed

.feed_ok:
        ; Deduct food
        mov eax, [food_fed]
        sub [food], eax

        mov eax, SYS_BEEP
        mov ebx, 600
        mov ecx, 2
        int 0x80

        popad
        ret

;=======================================================================
; PHASE 3: PLANT CROPS
;=======================================================================
phase_plant:
        pushad
        mov eax, SYS_CLEAR
        int 0x80

        call draw_phase_header
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 2
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_phase_plant
        int 0x80

        ; Max plantable = min(land, food/SEED_PER_HECTARE, pop*HECTARES_PER_COLONIST)

        ; Calculate max by food
        mov eax, [food]
        xor edx, edx
        mov ebx, SEED_PER_HECTARE
        div ebx
        mov [tmp_val], eax     ; max by food

        ; Calculate max by labor
        mov eax, [population]
        imul eax, HECTARES_PER_COLONIST
        cmp eax, [tmp_val]
        jge .plant_food_limit
        mov [tmp_val], eax     ; max by labor limit
.plant_food_limit:
        ; Max by land
        mov eax, [land]
        cmp eax, [tmp_val]
        jge .plant_show
        mov [tmp_val], eax
.plant_show:

        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 4
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_land_avail
        int 0x80
        mov eax, [land]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_ha
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 5
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_seed_avail
        int 0x80
        mov eax, [food]
        xor edx, edx
        mov ebx, SEED_PER_HECTARE
        div ebx
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_ha
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 6
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_labor_avail
        int 0x80
        mov eax, [population]
        imul eax, HECTARES_PER_COLONIST
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_ha
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 7
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_max_plant
        int 0x80
        mov eax, [tmp_val]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_ha
        int 0x80

.ask_plant:
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 9
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_clear_line
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 9
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_plant_how
        int 0x80

        call read_number
        mov [acres_planted], eax

        ; Validate: doesn't exceed max
        cmp eax, [tmp_val]
        jle .plant_ok

        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 11
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_too_many_plant
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 200
        mov ecx, 2
        int 0x80
        jmp .ask_plant

.plant_ok:
        ; Deduct seed cost
        mov eax, [acres_planted]
        imul eax, SEED_PER_HECTARE
        sub [food], eax

        mov eax, SYS_BEEP
        mov ebx, 700
        mov ecx, 2
        int 0x80

        popad
        ret

;=======================================================================
; SIMULATE YEAR
;=======================================================================
simulate_year:
        pushad

        ; --- Harvest ---
        ; Yield = 1-6 food per hectare planted
        call random
        xor edx, edx
        mov ebx, 6
        div ebx
        inc edx                 ; 1-6
        mov [harvest_yield], edx

        mov eax, [acres_planted]
        imul eax, edx
        mov [harvest_amount], eax
        add [food], eax

        ; --- Random Event ---
        call random_event

        ; --- Calculate starvation ---
        ; People fed = food_fed / FOOD_PER_PERSON
        mov eax, [food_fed]
        xor edx, edx
        mov ebx, FOOD_PER_PERSON
        div ebx                 ; eax = people fully fed
        mov ecx, eax

        ; Starved = population - people_fed (if negative, = 0)
        mov eax, [population]
        sub eax, ecx
        cmp eax, 0
        jg .some_starved
        xor eax, eax
.some_starved:
        mov [last_starved], eax
        add [total_starved], eax

        ; Calculate starvation percentage for catastrophe check
        cmp dword [population], 0
        je .skip_pct
        push eax
        imul eax, 100
        xor edx, edx
        mov ebx, [population]
        div ebx                 ; eax = pct starved
        cmp eax, [max_starved_pct]
        jle .pct_not_max
        mov [max_starved_pct], eax
.pct_not_max:
        pop eax

        ; If > 45% starve in one year, people revolt
        cmp eax, 0
        je .skip_pct
        push eax
        imul eax, 100
        xor edx, edx
        mov ebx, [population]
        div ebx
        pop eax                 ; restore starved count (not pct)
        cmp eax, 45
        jl .skip_pct
        ; Revolt! (handled later, just flag)
.skip_pct:

        ; Remove dead
        mov eax, [last_starved]
        sub [population], eax
        cmp dword [population], 0
        jge .pop_ok
        mov dword [population], 0
.pop_ok:

        ; --- Immigration ---
        ; Immigrants attracted by prosperity
        ; immigrants = (20 * land + food) / (100 * population + 1)
        ; Simplified: random 0-10 if well-fed, 0 if starving
        cmp dword [last_starved], 0
        jne .no_immigrants

        call random
        xor edx, edx
        mov ebx, 15
        div ebx
        inc edx                 ; 1-15
        ; Scale by land availability
        mov eax, edx
        cmp eax, 10
        jle .immig_ok
        mov eax, 10
.immig_ok:
        mov [last_immigrants], eax
        add [population], eax
        add [total_immigrants], eax
        jmp .immig_done

.no_immigrants:
        mov dword [last_immigrants], 0
.immig_done:

        ; --- Plague check (5% chance) ---
        mov dword [plague_flag], 0
        call random
        xor edx, edx
        mov ebx, 20
        div ebx
        cmp edx, 0
        jne .no_plague
        ; Plague: kill half the population
        mov eax, [population]
        shr eax, 1
        sub [population], eax
        mov dword [plague_flag], 1
        cmp dword [population], 0
        jge .no_plague
        mov dword [population], 0
.no_plague:

        ; --- Energy generation ---
        ; Energy from solar: gain 50-150 per year
        call random
        xor edx, edx
        mov ebx, 100
        div ebx
        add edx, 50
        add [energy], edx

        ; --- Show year results ---
        call show_year_results

        popad
        ret

;=======================================================================
; RANDOM EVENTS
;=======================================================================
random_event:
        pushad

        ; 40% chance of an event
        call random
        xor edx, edx
        mov ebx, 10
        div ebx
        cmp edx, 3             ; 0,1,2,3 = event (40%)
        jg .no_event

        ; Pick event type (1-8)
        call random
        xor edx, edx
        mov ebx, 8
        div ebx
        inc edx                 ; 1-8
        mov [last_event], edx

        ; Apply event effects
        cmp edx, EVT_DUST_STORM
        je .evt_dust
        cmp edx, EVT_PLAGUE
        je .evt_plague
        cmp edx, EVT_SOLAR_FLARE
        je .evt_flare
        cmp edx, EVT_ARTIFACT
        je .evt_artifact
        cmp edx, EVT_BOUNTIFUL
        je .evt_bountiful
        cmp edx, EVT_SUPPLY_SHIP
        je .evt_supply
        cmp edx, EVT_PESTS
        je .evt_pests
        cmp edx, EVT_DISCOVERY
        je .evt_discovery
        jmp .no_event

.evt_dust:
        ; Dust storm: destroy 10-30% of planted crops
        mov eax, [acres_planted]
        shr eax, 2             ; ~25%
        sub [acres_planted], eax
        cmp dword [acres_planted], 0
        jge .evt_done
        mov dword [acres_planted], 0
        jmp .evt_done

.evt_plague:
        ; Minor plague: lose 5-15% of food
        mov eax, [food]
        shr eax, 3             ; ~12.5%
        sub [food], eax
        cmp dword [food], 0
        jge .evt_done
        mov dword [food], 0
        jmp .evt_done

.evt_flare:
        ; Solar flare: lose 20% energy
        mov eax, [energy]
        shr eax, 2             ; 25%
        sub [energy], eax
        cmp dword [energy], 0
        jge .evt_done
        mov dword [energy], 0
        jmp .evt_done

.evt_artifact:
        ; Alien artifact: +200 energy
        add dword [energy], 200
        jmp .evt_done

.evt_bountiful:
        ; Bountiful season: +50% harvest (applied before harvest calc)
        mov eax, [acres_planted]
        shr eax, 1
        add [acres_planted], eax
        jmp .evt_done

.evt_supply:
        ; Supply ship arrives: +500 food, +10 people
        add dword [food], 500
        add dword [population], 10
        jmp .evt_done

.evt_pests:
        ; Alien pests eat food stores: lose 10%
        mov eax, [food]
        xor edx, edx
        mov ebx, 10
        div ebx
        sub [food], eax
        cmp dword [food], 0
        jge .evt_done
        mov dword [food], 0
        jmp .evt_done

.evt_discovery:
        ; Scientific discovery: +100 food from new technique
        add dword [food], 100
        jmp .evt_done

.no_event:
        mov dword [last_event], EVT_NONE
.evt_done:
        popad
        ret

;=======================================================================
; YEAR RESULTS SCREEN
;=======================================================================
show_year_results:
        pushad
        mov eax, SYS_CLEAR
        int 0x80

        call draw_phase_header

        ; Title
        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 2
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_results_hdr
        int 0x80

        ; Harvest
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 4
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_harvested
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, [harvest_amount]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_food_from
        int 0x80
        mov eax, [acres_planted]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_ha_at
        int 0x80
        mov eax, [harvest_yield]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_per_ha
        int 0x80

        ; Event
        mov ecx, 6
        cmp dword [last_event], EVT_NONE
        je .res_no_event
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LMAGENTA
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_event_lbl
        int 0x80
        mov eax, [last_event]
        imul eax, 4
        mov ebx, [event_msg_table + eax]
        mov eax, SYS_PRINT
        int 0x80
        mov ecx, 8
.res_no_event:

        ; Starvation
        cmp dword [last_starved], 0
        je .res_no_starve
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_starved_res
        int 0x80
        mov eax, [last_starved]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_colonists
        int 0x80
        inc ecx
        inc ecx
.res_no_starve:

        ; Immigration
        cmp dword [last_immigrants], 0
        je .res_no_immig
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_immig_res
        int 0x80
        mov eax, [last_immigrants]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_new_col
        int 0x80
        inc ecx
        inc ecx
.res_no_immig:

        ; Plague
        cmp dword [plague_flag], 0
        je .res_no_plague
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_plague_msg
        int 0x80
        inc ecx
        inc ecx
        mov eax, SYS_BEEP
        mov ebx, 150
        mov ecx, 5
        int 0x80
.res_no_plague:

        ; Final stats
        push ecx
        add ecx, 2
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_separator
        int 0x80
        pop ecx

        add ecx, 3
        mov eax, SYS_SETCURSOR
        mov ebx, 7
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_end_pop
        int 0x80
        mov eax, [population]
        call print_number

        inc ecx
        mov eax, SYS_SETCURSOR
        mov ebx, 7
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_end_food
        int 0x80
        mov eax, [food]
        call print_number

        inc ecx
        mov eax, SYS_SETCURSOR
        mov ebx, 7
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_end_land
        int 0x80
        mov eax, [land]
        call print_number

        inc ecx
        mov eax, SYS_SETCURSOR
        mov ebx, 7
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_end_energy
        int 0x80
        mov eax, [energy]
        call print_number

        ; Sound
        mov eax, SYS_BEEP
        mov ebx, 440
        mov ecx, 2
        int 0x80

        ; Wait
        mov eax, SYS_SETCURSOR
        mov ebx, 18
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

;=======================================================================
; GAME OVER SCREENS
;=======================================================================
game_over_dead:
        mov eax, SYS_CLEAR
        int 0x80

        mov eax, SYS_BEEP
        mov ebx, 200
        mov ecx, 4
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 150
        mov ecx, 4
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 100
        mov ecx, 8
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 4
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_dead_title
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 7
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_dead_text1
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 9
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_dead_text2
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 11
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_dead_text3
        int 0x80

        ; Stats
        mov eax, SYS_SETCURSOR
        mov ebx, 10
        mov ecx, 14
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_lasted
        int 0x80
        mov eax, [year]
        dec eax
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_years
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 10
        mov ecx, 15
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_total_lost
        int 0x80
        mov eax, [total_starved]
        call print_number

        jmp game_exit

game_over_good:
        mov eax, SYS_CLEAR
        int 0x80

        ; Calculate rating
        ; Score based on: final population, total starved, food, land, energy
        ; simple: pop*3 + food/10 + land/10 + energy/5 - total_starved*5

        mov eax, [population]
        imul eax, 3
        mov [score], eax

        mov eax, [food]
        xor edx, edx
        mov ebx, 10
        div ebx
        add [score], eax

        mov eax, [land]
        xor edx, edx
        mov ebx, 10
        div ebx
        add [score], eax

        mov eax, [energy]
        xor edx, edx
        mov ebx, 5
        div ebx
        add [score], eax

        mov eax, [total_starved]
        imul eax, 5
        sub [score], eax
        cmp dword [score], 0
        jge .score_ok
        mov dword [score], 0
.score_ok:

        ; Victory fanfare
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
        mov ecx, 4
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 1047
        mov ecx, 6
        int 0x80

        ; Title
        mov eax, SYS_SETCURSOR
        mov ebx, 12
        mov ecx, 2
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_win_title
        int 0x80

        ; Narrative
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 4
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_win_text1
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 5
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_win_text2
        int 0x80

        ; Final Stats
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 7
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_separator
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 10
        mov ecx, 9
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_fin_pop
        int 0x80
        mov eax, [population]
        call print_number

        mov eax, SYS_SETCURSOR
        mov ebx, 10
        mov ecx, 10
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_fin_food
        int 0x80
        mov eax, [food]
        call print_number

        mov eax, SYS_SETCURSOR
        mov ebx, 10
        mov ecx, 11
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_fin_land
        int 0x80
        mov eax, [land]
        call print_number

        mov eax, SYS_SETCURSOR
        mov ebx, 10
        mov ecx, 12
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_fin_energy
        int 0x80
        mov eax, [energy]
        call print_number

        mov eax, SYS_SETCURSOR
        mov ebx, 10
        mov ecx, 13
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_fin_starved
        int 0x80
        mov eax, [total_starved]
        call print_number

        mov eax, SYS_SETCURSOR
        mov ebx, 10
        mov ecx, 14
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_fin_immig
        int 0x80
        mov eax, [total_immigrants]
        call print_number

        ; Score
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 16
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_separator
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 18
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_score_lbl
        int 0x80
        mov eax, [score]
        call print_number

        ; Rating
        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 19
        int 0x80

        mov eax, [score]
        cmp eax, 800
        jge .r_legend
        cmp eax, 500
        jge .r_great
        cmp eax, 300
        jge .r_good
        cmp eax, 100
        jge .r_poor
        ; terrible
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_rate_terrible
        int 0x80
        jmp .r_done

.r_poor:
        mov eax, SYS_SETCOLOR
        mov ebx, C_BROWN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_rate_poor
        int 0x80
        jmp .r_done

.r_good:
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_rate_good
        int 0x80
        jmp .r_done

.r_great:
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_rate_great
        int 0x80
        jmp .r_done

.r_legend:
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_rate_legend
        int 0x80
.r_done:

game_exit:
        mov eax, SYS_SETCURSOR
        mov ebx, 18
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
; draw_phase_header - Blue status band
;---------------------------------------
draw_phase_header:
        pushad
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE | 0x10
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        xor ecx, ecx
        int 0x80
        mov ecx, 80
.dph_sp:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        dec ecx
        jnz .dph_sp

        mov eax, SYS_SETCURSOR
        mov ebx, 2
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_outpost
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 60
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_year_lbl
        int 0x80
        mov eax, [year]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_of_ten
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        popad
        ret

;---------------------------------------
; read_number - Read a decimal number from user, return in EAX
;---------------------------------------
read_number:
        pushad
        mov edi, input_buf
        xor ecx, ecx

        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80

.rn_loop:
        mov eax, SYS_GETCHAR
        int 0x80

        cmp al, 0x0D
        je .rn_done
        cmp al, 0x0A
        je .rn_done
        cmp al, 0x08
        je .rn_bs
        cmp al, 0x7F
        je .rn_bs

        ; Only accept digits
        cmp al, '0'
        jb .rn_loop
        cmp al, '9'
        ja .rn_loop

        cmp ecx, INPUT_MAX - 1
        jge .rn_loop

        mov [edi + ecx], al
        inc ecx

        movzx ebx, al
        mov eax, SYS_PUTCHAR
        int 0x80
        jmp .rn_loop

.rn_bs:
        cmp ecx, 0
        je .rn_loop
        dec ecx
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
        jmp .rn_loop

.rn_done:
        mov byte [edi + ecx], 0

        ; Convert string to integer
        mov esi, input_buf
        xor eax, eax
        xor ebx, ebx
.rn_conv:
        movzx ebx, byte [esi]
        cmp bl, 0
        je .rn_conv_done
        cmp bl, '0'
        jb .rn_conv_done
        cmp bl, '9'
        ja .rn_conv_done
        imul eax, 10
        sub bl, '0'
        add eax, ebx
        inc esi
        jmp .rn_conv

.rn_conv_done:
        mov [tmp_result], eax
        popad
        mov eax, [tmp_result]
        ret

;---------------------------------------
; print_number - Print EAX as decimal
;---------------------------------------
print_number:
        pushad
        cmp eax, 0
        jne .pn_nonzero
        mov eax, SYS_PUTCHAR
        mov ebx, '0'
        int 0x80
        popad
        ret
.pn_nonzero:
        mov ecx, 0
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

; === Title ===
str_border:     db "======================================================================", 0
str_title1:     db "O U T P O S T  :  K E P L E R", 0
str_title2:     db "A Space Colony Simulation for Mellivora OS", 0
str_footer:     db "Inspired by KINGDOM (1968)  |  A Mellivora Production", 0

str_menu1:      db "[1] Launch Colony", 0
str_menu2:      db "[2] How to Play", 0
str_menu3:      db "[3] Abort Mission", 0

; Planet art
p_art1: db "       .  *  .", 0
p_art2: db "    .  * (   ) *  .", 0
p_art3: db "   * ( Kepler-442b )", 0
p_art4: db "    *  (       )  *", 0
p_art5: db "       '  *  '", 0
p_art6: db "      *       *", 0
p_art7: db "         * *", 0

planet_art: dd p_art1, p_art2, p_art3, p_art4, p_art5, p_art6, p_art7

; === How to Play ===
str_howto_title: db "=== HOW TO PLAY ===", 0

ht1:  db "You are the Commander of humanity's first colony on Kepler-442b.", 0
ht2:  db "Your mission: keep the colony alive for 10 years.", 0
ht3:  db " ", 0
ht4:  db "Each year you make three critical decisions:", 0
ht5:  db " ", 0
ht6:  db "  1. LAND - Buy or sell colony territory (hectares).", 0
ht7:  db "     Land prices fluctuate each year (17-28 food/hectare).", 0
ht8:  db " ", 0
ht9:  db "  2. FOOD - Feed your colonists. Each needs 20 units/year.", 0
ht10: db "     If you don't feed enough, colonists starve and die.", 0
ht11: db " ", 0
ht12: db "  3. PLANT - Assign hectares to crop production.", 0
ht13: db "     Planting costs 2 food/hectare as seed. Each colonist", 0
ht14: db "     can tend up to 10 hectares. Yield varies: 1-6 food/ha.", 0
ht15: db " ", 0
ht16: db "Random events -- dust storms, plagues, supply ships, alien", 0
ht17: db "artifacts -- will test your leadership. Good luck, Commander.", 0
ht18: db " ", 0

howto_lines: dd ht1, ht2, ht3, ht4, ht5, ht6, ht7, ht8, ht9
             dd ht10, ht11, ht12, ht13, ht14, ht15, ht16, ht17, ht18

; === Intro ===
intro1:  db " ", 0
intro2:  db "Mission Log - Kepler Colony Initiative", 0
intro3:  db " ", 0
intro4:  db "After 87 years in cryo-sleep, the colony ship PERSEVERANCE", 0
intro5:  db "has reached Kepler-442b -- a rocky world with a breathable", 0
intro6:  db "atmosphere orbiting a distant orange star.", 0
intro7:  db " ", 0
intro8:  db "You have 100 colonists, 2,800 units of food,", 0
intro9:  db "1,000 hectares of arable land, and 500 energy cells.", 0
intro10: db " ", 0
intro11: db "The colony must survive 10 years until the next supply", 0
intro12: db "fleet can reach you. Every decision matters, Commander.", 0

intro_lines: dd intro1, intro2, intro3, intro4, intro5, intro6
             dd intro7, intro8, intro9, intro10, intro11, intro12

str_press_begin: db "Press any key to begin Year 1...", 0
str_press_key:   db "Press any key to continue...", 0

; === Status Screen ===
str_outpost:    db "OUTPOST: KEPLER", 0
str_year_lbl:   db "Year ", 0
str_of_ten:     db "/10", 0
str_status_hdr: db "=== COLONY STATUS REPORT ===", 0
str_separator:  db "----------------------------------------------", 0

str_pop_lbl:    db "Colonists:  ", 0
str_food_lbl:   db "Food:       ", 0
str_land_lbl:   db "Territory:  ", 0
str_energy_lbl: db "Energy:     ", 0
str_colonists:  db " colonists", 0
str_units:      db " units", 0
str_hectares:   db " hectares", 0
str_cells:      db " cells", 0
str_price_lbl:  db "Land price: ", 0
str_food_per_ha: db " food per hectare", 0

str_starved_pre: db "Last year, ", 0
str_starved_suf: db " colonists starved.", 0
str_immig_pre:   db "  ", 0
str_immig_suf:   db " new colonists arrived from cryo-pods.", 0

; === Phase Prompts ===
str_phase_land:  db "=== PHASE 1: LAND MANAGEMENT ===", 0
str_phase_feed:  db "=== PHASE 2: FOOD DISTRIBUTION ===", 0
str_phase_plant: db "=== PHASE 3: CROP PLANTING ===", 0

str_you_have:       db "You have ", 0
str_hectares_and:   db " hectares and ", 0
str_food_stored:    db " units of food in storage.", 0
str_land_costs:     db "Land costs ", 0
str_food_per_ha2:   db " food per hectare this year.", 0

str_buy_land:       db "How many hectares to BUY? (0 = none): ", 0
str_sell_land:      db "How many hectares to SELL? (0 = none): ", 0
str_cant_afford:    db "You can't afford that much land!", 0
str_not_enough_land: db "You don't have that much land to sell!", 0

str_pop_is:         db "Population: ", 0
str_need:           db ". They need at least ", 0
str_food_min:       db " food units.", 0
str_food_avail:     db "Food available: ", 0
str_feed_how:       db "How many food units to distribute? ", 0
str_not_enough_food: db "You don't have that much food!", 0

str_land_avail:     db "Colony territory: ", 0
str_ha:             db " ha", 0
str_seed_avail:     db "Seed available for: ", 0
str_labor_avail:    db "Labor capacity: ", 0
str_max_plant:      db "Maximum plantable: ", 0
str_plant_how:      db "How many hectares to plant? ", 0
str_too_many_plant: db "You don't have the resources to plant that much!", 0

str_clear_line: db "                                                                  ", 0

; === Year Results ===
str_results_hdr: db "=== YEAR-END REPORT ===", 0
str_harvested:   db "Harvest: ", 0
str_food_from:   db " food from ", 0
str_ha_at:       db " ha (yield: ", 0
str_per_ha:      db "/ha)", 0
str_event_lbl:   db "Event: ", 0
str_starved_res: db "Starvation: ", 0
str_immig_res:   db "Immigration: ", 0
str_new_col:     db " new colonists arrived.", 0
str_plague_msg:  db "A PLAGUE swept through the colony! Half the population was lost.", 0

str_end_pop:     db "Population: ", 0
str_end_food:    db "Food stores: ", 0
str_end_land:    db "Territory:   ", 0
str_end_energy:  db "Energy:      ", 0

; === Events ===
evt_msg_none:   db 0
evt_msg_dust:   db "A violent dust storm ravaged the croplands!", 0
evt_msg_plague: db "A mysterious illness spread through the food stores.", 0
evt_msg_flare:  db "A solar flare damaged the colony's power grid!", 0
evt_msg_artifact: db "Colonists unearthed an alien energy artifact!", 0
evt_msg_bount:  db "Ideal weather conditions -- a bountiful growing season!", 0
evt_msg_supply: db "A supply ship arrived with food and colonists!", 0
evt_msg_pests:  db "Alien pests infested the food storage silos!", 0
evt_msg_disc:   db "Scientists discovered a new crop cultivation technique!", 0

event_msg_table:
        dd evt_msg_none, evt_msg_dust, evt_msg_plague, evt_msg_flare
        dd evt_msg_artifact, evt_msg_bount, evt_msg_supply, evt_msg_pests
        dd evt_msg_disc

; === Game Over ===
str_dead_title: db "*** COLONY LOST ***", 0
str_dead_text1: db "The last colonist has perished. The domes stand silent on", 0
str_dead_text2: db "the alien plain, slowly being reclaimed by Kepler's dust.", 0
str_dead_text3: db "Humanity's first colony... has failed.", 0
str_lasted:     db "Colony lasted: ", 0
str_years:      db " years.", 0
str_total_lost: db "Total lives lost: ", 0

str_win_title:  db "*** COLONY SURVIVED -- 10 YEARS COMPLETE ***", 0
str_win_text1:  db "The supply fleet's signal cuts through the static. You've done it.", 0
str_win_text2:  db "Kepler Colony will endure. Humanity has a new home among the stars.", 0

str_fin_pop:     db "Final population:  ", 0
str_fin_food:    db "Food reserves:     ", 0
str_fin_land:    db "Territory held:    ", 0
str_fin_energy:  db "Energy reserves:   ", 0
str_fin_starved: db "Total lives lost:  ", 0
str_fin_immig:   db "Total immigrants:  ", 0

str_score_lbl:   db "COLONY SCORE: ", 0
str_rate_terrible: db "Rating: FAILED STATE - The colony barely survived.", 0
str_rate_poor:   db "Rating: STRUGGLING  - Many suffered under your command.", 0
str_rate_good:   db "Rating: ESTABLISHED - A solid foundation for the future.", 0
str_rate_great:  db "Rating: THRIVING    - An inspiring colony! Well led.", 0
str_rate_legend: db "Rating: LEGENDARY   - They will name cities after you!", 0

;=======================================================================
; BSS
;=======================================================================
section .bss

rand_seed:          resd 1
population:         resd 1
food:               resd 1
land:               resd 1
energy:             resd 1
year:               resd 1
land_price:         resd 1
harvest_yield:      resd 1
harvest_amount:     resd 1
food_fed:           resd 1
acres_planted:      resd 1
last_starved:       resd 1
last_immigrants:    resd 1
last_event:         resd 1
total_starved:      resd 1
total_immigrants:   resd 1
max_starved_pct:    resd 1
plague_flag:        resd 1
score:              resd 1
tmp_val:            resd 1
tmp_result:         resd 1
input_buf:          resb INPUT_MAX
