;=======================================================================
; OUTBREAK SHIELD - An Educational Vaccination Simulation Game
; Inspired by The Oregon Trail, themed around public health
; Idea inspired by a good friend Robin
;
; You are Dr. Pryor, chief epidemiologist leading a community of 200
; people through a 12-month outbreak of "Ratel Fever." Manage your
; vaccine supply, conduct vaccination drives, treat the sick, and make
; critical decisions to save as many lives as possible.
;
; Controls: Number keys for menu choices, arrow keys where indicated
;=======================================================================

%include "syscalls.inc"

;-----------------------------------------------------------------------
; Constants
;-----------------------------------------------------------------------
COMMUNITY_SIZE  equ 200         ; Starting population
MAX_MONTHS      equ 12          ; Game length
MAX_VACCINES    equ 999
MAX_SUPPLIES    equ 999
MAX_MORALE      equ 100

; VGA colors
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

; Background colors (shifted)
BG_BLUE         equ 0x10
BG_GREEN        equ 0x20
BG_RED          equ 0x40

; Sound frequencies
SND_GOOD        equ 1200
SND_BAD         equ 200
SND_ALARM       equ 400
SND_VICTORY     equ 1600
SND_DEATH       equ 100
SND_VACCINE     equ 1000

; Difficulty thresholds
OUTBREAK_BASE   equ 8           ; Base infection rate per month (%)
VACCINE_EFFECT  equ 3           ; Each 10% vaccinated reduces rate by this

; Default settings
DEF_VACCINES    equ 50
DEF_SUPPLIES    equ 40
DEF_MORALE      equ 70
DEF_MONTHS      equ 12
DEF_DIFF        equ 1           ; 0=Easy, 1=Normal, 2=Hard

start:
        ; Clear screen
        mov eax, SYS_CLEAR
        int 0x80

        ; Seed PRNG from system time
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_seed], eax

        ; Initialize settings to defaults
        mov dword [set_vaccines], DEF_VACCINES
        mov dword [set_supplies], DEF_SUPPLIES
        mov dword [set_morale], DEF_MORALE
        mov dword [set_months], DEF_MONTHS
        mov dword [set_diff], DEF_DIFF

        jmp title_screen

;=======================================================================
; TITLE SCREEN - Animated intro with sound
;=======================================================================
title_screen:
        mov eax, SYS_CLEAR
        int 0x80

        ; Draw biohazard border (top)
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 10
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_border_top
        int 0x80

        ; Title - large text
        mov eax, SYS_SETCURSOR
        mov ebx, 18
        mov ecx, 2
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_title1
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 3
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_title2
        int 0x80

        ; Subtitle
        mov eax, SYS_SETCURSOR
        mov ebx, 12
        mov ecx, 5
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_subtitle
        int 0x80

        ; Draw virus ASCII art
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 30
        mov ecx, 7
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, virus_art1
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 30
        mov ecx, 8
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, virus_art2
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 30
        mov ecx, 9
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, virus_art3
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 30
        mov ecx, 10
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, virus_art4
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 30
        mov ecx, 11
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, virus_art5
        int 0x80

        ; Syringe art on left side
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 8
        mov ecx, 8
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, syringe_art1
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 8
        mov ecx, 9
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, syringe_art2
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 8
        mov ecx, 10
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, syringe_art3
        int 0x80

        ; Shield art on right side
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 56
        mov ecx, 8
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, shield_art1
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 56
        mov ecx, 9
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, shield_art2
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 56
        mov ecx, 10
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, shield_art3
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 56
        mov ecx, 11
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, shield_art4
        int 0x80

        ; Bottom border
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 10
        mov ecx, 13
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_border_bot
        int 0x80

        ; Story intro
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 15
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_story1
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 16
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_story2
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 17
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_story3
        int 0x80

        ; Menu
        mov eax, SYS_SETCURSOR
        mov ebx, 20
        mov ecx, 19
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_menu_play
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 20
        mov ecx, 20
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_menu_howto
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 20
        mov ecx, 21
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_menu_settings
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 20
        mov ecx, 22
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_menu_quit
        int 0x80

        ; Play intro melody
        call play_title_melody

        ; Footer
        mov eax, SYS_SETCURSOR
        mov ebx, 14
        mov ecx, 23
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_footer
        int 0x80

.title_wait:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, '1'
        je new_game
        cmp al, '2'
        je show_howto
        cmp al, '3'
        je show_settings
        cmp al, '4'
        je exit_game
        cmp al, 27             ; ESC
        je exit_game
        jmp .title_wait

;=======================================================================
; HOW TO PLAY SCREEN
;=======================================================================
show_howto:
        mov eax, SYS_CLEAR
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 25
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_howto_title
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80

        ; Print all how-to lines
        mov esi, howto_lines
        mov ecx, 1             ; starting row
        mov edx, 18            ; number of lines
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
        mov ebx, 20
        mov ecx, 22
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_press_any
        int 0x80

        mov eax, SYS_GETCHAR
        int 0x80
        jmp title_screen

;=======================================================================
; SETTINGS SCREEN - Customize initial parameters
;=======================================================================
show_settings:
        mov eax, SYS_CLEAR
        int 0x80

.settings_redraw:
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        xor ecx, ecx
        int 0x80

        ; Header
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 22
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_settings_title
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 10
        mov ecx, 2
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_settings_hint
        int 0x80

        ; --- Setting 1: Starting Vaccines ---
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 4
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_set_vaccines
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, [set_vaccines]
        call print_number

        ; --- Setting 2: Starting Supplies ---
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 6
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_set_supplies
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, [set_supplies]
        call print_number

        ; --- Setting 3: Starting Morale ---
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 8
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_set_morale
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, [set_morale]
        call print_number
        mov eax, SYS_PUTCHAR
        mov ebx, '%'
        int 0x80

        ; --- Setting 4: Game Length ---
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 10
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LMAGENTA
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_set_months
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, [set_months]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_months_suffix
        int 0x80

        ; --- Setting 5: Difficulty ---
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 12
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_set_diff
        int 0x80
        ; Print difficulty name
        mov eax, [set_diff]
        imul eax, 4
        mov ebx, [diff_names + eax]
        mov eax, SYS_SETCOLOR
        push ebx
        ; Color code difficulty
        mov eax, [set_diff]
        cmp eax, 0
        je .diff_easy_col
        cmp eax, 1
        je .diff_norm_col
        mov ebx, C_LRED
        jmp .diff_col_set
.diff_easy_col:
        mov ebx, C_LGREEN
        jmp .diff_col_set
.diff_norm_col:
        mov ebx, C_YELLOW
.diff_col_set:
        mov eax, SYS_SETCOLOR
        int 0x80
        pop ebx
        mov eax, SYS_PRINT
        int 0x80

        ; --- Separator ---
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 14
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_sep_short
        int 0x80

        ; --- Presets ---
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 16
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_set_presets
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 7
        mov ecx, 17
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_preset_easy
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 7
        mov ecx, 18
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_preset_normal
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 7
        mov ecx, 19
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_preset_hard
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 7
        mov ecx, 20
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_preset_custom
        int 0x80

        ; --- Footer ---
        mov eax, SYS_SETCURSOR
        mov ebx, 12
        mov ecx, 22
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_set_footer
        int 0x80

.settings_wait:
        mov eax, SYS_GETCHAR
        int 0x80

        cmp al, '1'
        je .preset_easy
        cmp al, '2'
        je .preset_normal
        cmp al, '3'
        je .preset_hard
        cmp al, '4'
        je .custom_settings
        cmp al, 27             ; ESC = back
        je title_screen
        jmp .settings_wait

.preset_easy:
        mov dword [set_vaccines], 75
        mov dword [set_supplies], 60
        mov dword [set_morale], 90
        mov dword [set_months], 12
        mov dword [set_diff], 0
        mov eax, SYS_BEEP
        mov ebx, SND_GOOD
        mov ecx, 2
        int 0x80
        mov eax, SYS_CLEAR
        int 0x80
        jmp .settings_redraw

.preset_normal:
        mov dword [set_vaccines], DEF_VACCINES
        mov dword [set_supplies], DEF_SUPPLIES
        mov dword [set_morale], DEF_MORALE
        mov dword [set_months], DEF_MONTHS
        mov dword [set_diff], DEF_DIFF
        mov eax, SYS_BEEP
        mov ebx, SND_GOOD
        mov ecx, 2
        int 0x80
        mov eax, SYS_CLEAR
        int 0x80
        jmp .settings_redraw

.preset_hard:
        mov dword [set_vaccines], 25
        mov dword [set_supplies], 20
        mov dword [set_morale], 50
        mov dword [set_months], 12
        mov dword [set_diff], 2
        mov eax, SYS_BEEP
        mov ebx, SND_ALARM
        mov ecx, 2
        int 0x80
        mov eax, SYS_CLEAR
        int 0x80
        jmp .settings_redraw

;-----------------------------------------------------------------------
; Custom settings - cycle through each parameter
;-----------------------------------------------------------------------
.custom_settings:
        mov eax, SYS_CLEAR
        int 0x80

        ; --- Vaccines ---
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 18
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_custom_title
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 3
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_cust_vaccines
        int 0x80

.cust_vax_wait:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, '1'
        je .cust_vax_25
        cmp al, '2'
        je .cust_vax_50
        cmp al, '3'
        je .cust_vax_75
        cmp al, '4'
        je .cust_vax_100
        jmp .cust_vax_wait
.cust_vax_25:
        mov dword [set_vaccines], 25
        jmp .cust_supplies
.cust_vax_50:
        mov dword [set_vaccines], 50
        jmp .cust_supplies
.cust_vax_75:
        mov dword [set_vaccines], 75
        jmp .cust_supplies
.cust_vax_100:
        mov dword [set_vaccines], 100
        jmp .cust_supplies

        ; --- Supplies ---
.cust_supplies:
        mov eax, SYS_BEEP
        mov ebx, SND_GOOD
        mov ecx, 1
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 5
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_cust_supplies
        int 0x80

.cust_sup_wait:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, '1'
        je .cust_sup_20
        cmp al, '2'
        je .cust_sup_40
        cmp al, '3'
        je .cust_sup_60
        cmp al, '4'
        je .cust_sup_80
        jmp .cust_sup_wait
.cust_sup_20:
        mov dword [set_supplies], 20
        jmp .cust_morale
.cust_sup_40:
        mov dword [set_supplies], 40
        jmp .cust_morale
.cust_sup_60:
        mov dword [set_supplies], 60
        jmp .cust_morale
.cust_sup_80:
        mov dword [set_supplies], 80
        jmp .cust_morale

        ; --- Morale ---
.cust_morale:
        mov eax, SYS_BEEP
        mov ebx, SND_GOOD
        mov ecx, 1
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 7
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_cust_morale
        int 0x80

.cust_mor_wait:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, '1'
        je .cust_mor_40
        cmp al, '2'
        je .cust_mor_60
        cmp al, '3'
        je .cust_mor_80
        cmp al, '4'
        je .cust_mor_100
        jmp .cust_mor_wait
.cust_mor_40:
        mov dword [set_morale], 40
        jmp .cust_months
.cust_mor_60:
        mov dword [set_morale], 60
        jmp .cust_months
.cust_mor_80:
        mov dword [set_morale], 80
        jmp .cust_months
.cust_mor_100:
        mov dword [set_morale], 100
        jmp .cust_months

        ; --- Game Length ---
.cust_months:
        mov eax, SYS_BEEP
        mov ebx, SND_GOOD
        mov ecx, 1
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 9
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LMAGENTA
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_cust_months
        int 0x80

.cust_mon_wait:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, '1'
        je .cust_mon_6
        cmp al, '2'
        je .cust_mon_12
        cmp al, '3'
        je .cust_mon_18
        cmp al, '4'
        je .cust_mon_24
        jmp .cust_mon_wait
.cust_mon_6:
        mov dword [set_months], 6
        jmp .cust_diff
.cust_mon_12:
        mov dword [set_months], 12
        jmp .cust_diff
.cust_mon_18:
        mov dword [set_months], 18
        jmp .cust_diff
.cust_mon_24:
        mov dword [set_months], 24
        jmp .cust_diff

        ; --- Difficulty ---
.cust_diff:
        mov eax, SYS_BEEP
        mov ebx, SND_GOOD
        mov ecx, 1
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 11
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_cust_diff
        int 0x80

.cust_diff_wait:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, '1'
        je .cust_diff_easy
        cmp al, '2'
        je .cust_diff_norm
        cmp al, '3'
        je .cust_diff_hard
        jmp .cust_diff_wait
.cust_diff_easy:
        mov dword [set_diff], 0
        jmp .custom_done
.cust_diff_norm:
        mov dword [set_diff], 1
        jmp .custom_done
.cust_diff_hard:
        mov dword [set_diff], 2

.custom_done:
        mov eax, SYS_BEEP
        mov ebx, SND_VICTORY
        mov ecx, 3
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 10
        mov ecx, 14
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_custom_saved
        int 0x80

        call pause_message
        mov eax, SYS_CLEAR
        int 0x80
        jmp show_settings

;=======================================================================
; NEW GAME - Initialize all state
;=======================================================================
new_game:
        ; Play start sound
        mov eax, SYS_BEEP
        mov ebx, SND_GOOD
        mov ecx, 3
        int 0x80

        ; Initialize game state
        mov dword [month], 1
        mov dword [population], COMMUNITY_SIZE
        mov dword [healthy], COMMUNITY_SIZE
        mov dword [vaccinated], 0
        mov dword [infected], 0
        mov dword [recovered], 0
        mov dword [dead], 0
        mov eax, [set_vaccines]
        mov [vaccines], eax
        mov eax, [set_supplies]
        mov [supplies], eax
        mov eax, [set_morale]
        mov [morale], eax
        mov dword [research], 0         ; Research progress
        mov dword [actions_left], 3     ; Actions per month
        mov dword [total_vaccinated], 0
        mov dword [total_treated], 0
        mov dword [outbreaks_survived], 0
        mov byte  [hospital_built], 0
        mov byte  [lab_built], 0
        mov dword [difficulty], 0       ; Increases over time
        mov dword [event_type], 0

        ; Fall through to main game loop

;=======================================================================
; MAIN GAME LOOP
;=======================================================================
game_month:
        ; Check win condition
        mov eax, [set_months]
        inc eax
        cmp [month], eax
        jge game_win

        ; Check lose condition
        mov eax, [healthy]
        add eax, [vaccinated]
        add eax, [recovered]
        cmp eax, 0
        jle game_over

        ; Reset actions for this month
        mov dword [actions_left], 3

        ; Calculate month difficulty
        call calc_difficulty

        ; Draw main game screen
        call draw_game_screen

;-----------------------------------------------------------------------
; Action selection loop
;-----------------------------------------------------------------------
action_loop:
        cmp dword [actions_left], 0
        jle month_end

        ; Draw action menu at bottom
        call draw_action_menu

.action_wait:
        mov eax, SYS_GETCHAR
        int 0x80

        cmp al, '1'
        je action_vaccinate
        cmp al, '2'
        je action_treat
        cmp al, '3'
        je action_supply_run
        cmp al, '4'
        je action_research
        cmp al, '5'
        je action_awareness
        cmp al, '6'
        je action_rest
        cmp al, 27             ; ESC to quit
        je confirm_quit
        jmp .action_wait

;-----------------------------------------------------------------------
; ACTION 1: Vaccination Drive
;-----------------------------------------------------------------------
action_vaccinate:
        cmp dword [vaccines], 0
        jle .no_vaccines

        ; Calculate how many we can vaccinate (up to 20, limited by supply and healthy)
        mov eax, [vaccines]
        cmp eax, 20
        jle .vax_cap
        mov eax, 20
.vax_cap:
        mov ebx, [healthy]
        cmp eax, ebx
        jle .vax_ok
        mov eax, ebx
.vax_ok:
        cmp eax, 0
        jle .no_targets

        ; Apply vaccination
        mov [temp_val], eax
        sub [vaccines], eax
        sub [healthy], eax
        add [vaccinated], eax
        add [total_vaccinated], eax

        ; Boost morale
        add dword [morale], 3
        call clamp_morale

        ; Show result
        call draw_game_screen
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 22
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_vax_success
        int 0x80
        mov eax, [temp_val]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_people_vaxxed
        int 0x80

        ; Sound effect
        mov eax, SYS_BEEP
        mov ebx, SND_VACCINE
        mov ecx, 3
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 1200
        mov ecx, 2
        int 0x80

        dec dword [actions_left]
        call pause_message
        call draw_game_screen
        jmp action_loop

.no_vaccines:
        call draw_game_screen
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 22
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_no_vaccines
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, SND_BAD
        mov ecx, 3
        int 0x80
        call pause_message
        call draw_game_screen
        jmp action_loop

.no_targets:
        call draw_game_screen
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 22
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_all_vaxxed
        int 0x80
        call pause_message
        call draw_game_screen
        jmp action_loop

;-----------------------------------------------------------------------
; ACTION 2: Treat the Sick
;-----------------------------------------------------------------------
action_treat:
        cmp dword [infected], 0
        jle .no_sick

        cmp dword [supplies], 0
        jle .no_supplies

        ; Treat up to 15 infected (costs supplies)
        mov eax, [supplies]
        cmp eax, 15
        jle .treat_cap
        mov eax, 15
.treat_cap:
        mov ebx, [infected]
        cmp eax, ebx
        jle .treat_ok
        mov eax, ebx
.treat_ok:
        mov [temp_val], eax
        sub [supplies], eax
        sub [infected], eax
        add [recovered], eax
        add [total_treated], eax

        ; Hospital bonus
        cmp byte [hospital_built], 1
        jne .no_hosp_bonus
        ; Treat 5 more for free
        mov eax, [infected]
        cmp eax, 5
        jle .hosp_cap
        mov eax, 5
.hosp_cap:
        sub [infected], eax
        add [recovered], eax
        add [total_treated], eax
        add [temp_val], eax
.no_hosp_bonus:

        call draw_game_screen
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 22
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_treated
        int 0x80
        mov eax, [temp_val]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_people_treated
        int 0x80

        add dword [morale], 2
        call clamp_morale

        mov eax, SYS_BEEP
        mov ebx, SND_GOOD
        mov ecx, 2
        int 0x80

        dec dword [actions_left]
        call pause_message
        call draw_game_screen
        jmp action_loop

.no_sick:
        call draw_game_screen
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 22
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_no_sick
        int 0x80
        call pause_message
        call draw_game_screen
        jmp action_loop

.no_supplies:
        call draw_game_screen
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 22
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_no_supplies
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, SND_BAD
        mov ecx, 3
        int 0x80
        call pause_message
        call draw_game_screen
        jmp action_loop

;-----------------------------------------------------------------------
; ACTION 3: Supply Run (gather vaccines + supplies)
;-----------------------------------------------------------------------
action_supply_run:
        ; Random supply gain
        call random
        xor edx, edx
        mov ebx, 20
        div ebx
        add edx, 10            ; 10-29 vaccines
        mov [temp_val], edx
        add [vaccines], edx

        call random
        xor edx, edx
        mov ebx, 15
        div ebx
        add edx, 8             ; 8-22 supplies
        mov [temp_val2], edx
        add [supplies], edx

        ; Clamp
        cmp dword [vaccines], MAX_VACCINES
        jle .vclamp
        mov dword [vaccines], MAX_VACCINES
.vclamp:
        cmp dword [supplies], MAX_SUPPLIES
        jle .sclamp
        mov dword [supplies], MAX_SUPPLIES
.sclamp:

        ; Risk: small chance of infection during supply run
        call random
        xor edx, edx
        mov ebx, 100
        div ebx
        cmp edx, 15            ; 15% chance of risk
        jge .supply_safe

        ; Someone got infected on the run
        cmp dword [healthy], 2
        jl .supply_safe
        sub dword [healthy], 2
        add dword [infected], 2
        sub dword [morale], 3
        call clamp_morale

        call draw_game_screen
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 22
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_supply_risk
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, SND_ALARM
        mov ecx, 4
        int 0x80
        dec dword [actions_left]
        call pause_message
        call draw_game_screen
        jmp action_loop

.supply_safe:
        call draw_game_screen
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 22
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_supply_ok
        int 0x80
        mov eax, [temp_val]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_vax_and
        int 0x80
        mov eax, [temp_val2]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_med_supplies
        int 0x80

        mov eax, SYS_BEEP
        mov ebx, SND_GOOD
        mov ecx, 2
        int 0x80

        dec dword [actions_left]
        call pause_message
        call draw_game_screen
        jmp action_loop

;-----------------------------------------------------------------------
; ACTION 4: Research (build toward hospital/lab)
;-----------------------------------------------------------------------
action_research:
        call random
        xor edx, edx
        mov ebx, 10
        div ebx
        add edx, 5             ; 5-14 research points
        ; Lab bonus
        cmp byte [lab_built], 1
        jne .no_lab_bonus
        add edx, 5
.no_lab_bonus:
        add [research], edx
        mov [temp_val], edx

        ; Check if we unlocked something
        cmp byte [hospital_built], 0
        jne .check_lab
        cmp dword [research], 30
        jl .research_done
        mov byte [hospital_built], 1
        call draw_game_screen
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 22
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_hospital_built
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, SND_VICTORY
        mov ecx, 5
        int 0x80
        add dword [morale], 10
        call clamp_morale
        dec dword [actions_left]
        call pause_message
        call draw_game_screen
        jmp action_loop

.check_lab:
        cmp byte [lab_built], 0
        jne .research_done
        cmp dword [research], 70
        jl .research_done
        mov byte [lab_built], 1
        call draw_game_screen
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 22
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_lab_built
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, SND_VICTORY
        mov ecx, 5
        int 0x80
        add dword [morale], 10
        call clamp_morale
        dec dword [actions_left]
        call pause_message
        call draw_game_screen
        jmp action_loop

.research_done:
        call draw_game_screen
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 22
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_research_pts
        int 0x80
        mov eax, [temp_val]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_research_gained
        int 0x80

        mov eax, SYS_BEEP
        mov ebx, 600
        mov ecx, 2
        int 0x80

        dec dword [actions_left]
        call pause_message
        call draw_game_screen
        jmp action_loop

;-----------------------------------------------------------------------
; ACTION 5: Public Awareness Campaign
;-----------------------------------------------------------------------
action_awareness:
        add dword [morale], 8
        call clamp_morale

        ; Small chance to convince unvaccinated
        call random
        xor edx, edx
        mov ebx, 100
        div ebx
        cmp edx, 40            ; 40% chance
        jge .awareness_morale_only

        ; Some people voluntarily get vaccinated
        mov eax, [healthy]
        cmp eax, 5
        jl .awareness_morale_only
        mov eax, [vaccines]
        cmp eax, 5
        jl .awareness_morale_only

        sub dword [healthy], 5
        add dword [vaccinated], 5
        sub dword [vaccines], 5
        add dword [total_vaccinated], 5

        call draw_game_screen
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 22
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_awareness_vax
        int 0x80
        jmp .awareness_done

.awareness_morale_only:
        call draw_game_screen
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 22
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_awareness_ok
        int 0x80

.awareness_done:
        mov eax, SYS_BEEP
        mov ebx, SND_GOOD
        mov ecx, 2
        int 0x80
        dec dword [actions_left]
        call pause_message
        call draw_game_screen
        jmp action_loop

;-----------------------------------------------------------------------
; ACTION 6: Rest (skip action, small morale boost)
;-----------------------------------------------------------------------
action_rest:
        add dword [morale], 2
        call clamp_morale
        dec dword [actions_left]
        call draw_game_screen
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 22
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_rest
        int 0x80
        call pause_message
        call draw_game_screen
        jmp action_loop

;-----------------------------------------------------------------------
; Confirm quit
;-----------------------------------------------------------------------
confirm_quit:
        call draw_game_screen
        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 22
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_confirm_quit
        int 0x80
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'y'
        je exit_game
        cmp al, 'Y'
        je exit_game
        call draw_game_screen
        jmp action_loop

;=======================================================================
; MONTH END - Disease spreads, random events
;=======================================================================
month_end:
        ; === INFECTION PHASE ===
        call calc_infection_rate
        ; EAX = number of new infections

        mov [temp_val], eax

        ; Move healthy -> infected
        mov ebx, [healthy]
        cmp eax, ebx
        jle .inf_ok
        mov eax, ebx
        mov [temp_val], eax
.inf_ok:
        sub [healthy], eax
        add [infected], eax

        ; === DEATH PHASE ===
        ; Some infected die (death rate depends on treatment/morale)
        mov eax, [infected]
        cmp eax, 0
        jle .no_deaths

        ; Base death rate: 10% of infected
        mov ebx, 10
        ; Low morale increases death rate
        cmp dword [morale], 30
        jge .morale_ok
        add ebx, 5
.morale_ok:
        ; Hospital reduces death rate
        cmp byte [hospital_built], 1
        jne .no_hosp_save
        sub ebx, 3
        cmp ebx, 2
        jge .no_hosp_save
        mov ebx, 2
.no_hosp_save:
        imul eax, ebx
        xor edx, edx
        mov ebx, 100
        div ebx                 ; EAX = deaths
        cmp eax, 0
        jg .has_deaths
        ; At least 1 death if infected > 10
        cmp dword [infected], 10
        jl .no_deaths
        mov eax, 1
.has_deaths:
        mov [temp_val2], eax
        sub [infected], eax
        add [dead], eax
        sub [population], eax

        ; Morale hit from deaths
        mov ebx, eax
        shr ebx, 1             ; half of deaths
        cmp ebx, 0
        jle .no_deaths
        sub [morale], ebx
        call clamp_morale
        jmp .deaths_done

.no_deaths:
        mov dword [temp_val2], 0
.deaths_done:

        ; === NATURAL RECOVERY ===
        ; 20% of infected recover naturally each month
        mov eax, [infected]
        imul eax, 20
        xor edx, edx
        mov ebx, 100
        div ebx
        cmp eax, 0
        jle .no_recovery
        mov ebx, [infected]
        cmp eax, ebx
        jle .rec_ok
        mov eax, ebx
.rec_ok:
        sub [infected], eax
        add [recovered], eax
.no_recovery:

        ; === MORALE DECAY ===
        cmp dword [infected], 20
        jl .no_morale_decay
        sub dword [morale], 2
        call clamp_morale
.no_morale_decay:

        ; === Show month summary ===
        call draw_month_summary

        ; === RANDOM EVENT ===
        call random
        xor edx, edx
        mov ebx, 100
        div ebx
        cmp edx, 45            ; 45% chance of event
        jl .has_event
        jmp .no_event

.has_event:
        call trigger_random_event

.no_event:
        ; Advance month
        inc dword [month]
        inc dword [outbreaks_survived]

        ; Wait for player to continue
        mov eax, SYS_SETCURSOR
        mov ebx, 18
        mov ecx, 23
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_press_continue
        int 0x80
        mov eax, SYS_GETCHAR
        int 0x80

        jmp game_month

;=======================================================================
; RANDOM EVENTS
;=======================================================================
trigger_random_event:
        call random
        xor edx, edx
        mov ebx, 8
        div ebx
        ; EDX = event type 0-7

        cmp edx, 0
        je event_donation
        cmp edx, 1
        je event_antivax_rally
        cmp edx, 2
        je event_volunteer
        cmp edx, 3
        je event_mutation
        cmp edx, 4
        je event_supply_theft
        cmp edx, 5
        je event_medical_team
        cmp edx, 6
        je event_quarantine_break
        jmp event_good_news

;--- Event: Supply donation ---
event_donation:
        add dword [vaccines], 25
        cmp dword [vaccines], MAX_VACCINES
        jle .don_vclamp
        mov dword [vaccines], MAX_VACCINES
.don_vclamp:
        add dword [supplies], 15
        cmp dword [supplies], MAX_SUPPLIES
        jle .don_sclamp
        mov dword [supplies], MAX_SUPPLIES
.don_sclamp:
        add dword [morale], 5
        call clamp_morale
        call show_event_screen
        mov ebx, str_evt_donation
        call print_event_msg
        mov eax, SYS_BEEP
        mov ebx, SND_GOOD
        mov ecx, 4
        int 0x80
        ret

;--- Event: Anti-vax rally ---
event_antivax_rally:
        sub dword [morale], 12
        call clamp_morale
        ; Some vaccinated "refuse" boosters (lose some vaccine stock)
        cmp dword [vaccines], 10
        jl .av_skip
        sub dword [vaccines], 10
.av_skip:
        call show_event_screen
        mov ebx, str_evt_antivax
        call print_event_msg
        mov eax, SYS_BEEP
        mov ebx, SND_BAD
        mov ecx, 5
        int 0x80
        ret

;--- Event: Volunteers arrive ---
event_volunteer:
        add dword [morale], 8
        add dword [supplies], 10
        call clamp_morale
        call show_event_screen
        mov ebx, str_evt_volunteer
        call print_event_msg
        mov eax, SYS_BEEP
        mov ebx, SND_GOOD
        mov ecx, 3
        int 0x80
        ret

;--- Event: Virus mutation ---
event_mutation:
        ; Some vaccinated become susceptible again
        mov eax, [vaccinated]
        shr eax, 3             ; 12.5% lose immunity
        cmp eax, 0
        jle .mut_skip
        sub [vaccinated], eax
        add [healthy], eax
.mut_skip:
        sub dword [morale], 8
        call clamp_morale
        call show_event_screen
        mov ebx, str_evt_mutation
        call print_event_msg
        mov eax, SYS_BEEP
        mov ebx, SND_ALARM
        mov ecx, 6
        int 0x80
        ret

;--- Event: Supply theft ---
event_supply_theft:
        mov eax, [vaccines]
        shr eax, 2             ; Lose 25%
        sub [vaccines], eax
        mov eax, [supplies]
        shr eax, 2
        sub [supplies], eax
        sub dword [morale], 6
        call clamp_morale
        call show_event_screen
        mov ebx, str_evt_theft
        call print_event_msg
        mov eax, SYS_BEEP
        mov ebx, SND_BAD
        mov ecx, 4
        int 0x80
        ret

;--- Event: Medical team arrives ---
event_medical_team:
        ; Treat 10 infected for free
        mov eax, [infected]
        cmp eax, 10
        jle .mt_cap
        mov eax, 10
.mt_cap:
        sub [infected], eax
        add [recovered], eax
        add [total_treated], eax
        add dword [morale], 6
        call clamp_morale
        call show_event_screen
        mov ebx, str_evt_medteam
        call print_event_msg
        mov eax, SYS_BEEP
        mov ebx, SND_GOOD
        mov ecx, 4
        int 0x80
        ret

;--- Event: Quarantine break ---
event_quarantine_break:
        ; Infected mingle; extra infections
        mov eax, [infected]
        shr eax, 1             ; Half as many new infections
        mov ebx, [healthy]
        cmp eax, ebx
        jle .qb_ok
        mov eax, ebx
.qb_ok:
        sub [healthy], eax
        add [infected], eax
        sub dword [morale], 5
        call clamp_morale
        call show_event_screen
        mov ebx, str_evt_quarantine
        call print_event_msg
        mov eax, SYS_BEEP
        mov ebx, SND_ALARM
        mov ecx, 5
        int 0x80
        ret

;--- Event: Good news ---
event_good_news:
        add dword [morale], 10
        call clamp_morale
        call show_event_screen
        mov ebx, str_evt_goodnews
        call print_event_msg
        mov eax, SYS_BEEP
        mov ebx, SND_GOOD
        mov ecx, 3
        int 0x80
        ret

;=======================================================================
; GAME WIN
;=======================================================================
game_win:
        mov eax, SYS_CLEAR
        int 0x80

        ; Victory melody
        call play_victory_melody

        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 2
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_win_title
        int 0x80

        ; Draw victory art
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 28
        mov ecx, 4
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, trophy_art1
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 28
        mov ecx, 5
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, trophy_art2
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 28
        mov ecx, 6
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, trophy_art3
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 28
        mov ecx, 7
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, trophy_art4
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 28
        mov ecx, 8
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, trophy_art5
        int 0x80

        ; Stats
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 10
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_final_stats
        int 0x80

        call draw_final_stats

        ; Rating
        call calc_rating

        mov eax, SYS_SETCURSOR
        mov ebx, 20
        mov ecx, 21
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_play_again
        int 0x80

.win_wait:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'y'
        je title_screen
        cmp al, 'Y'
        je title_screen
        cmp al, 'n'
        je exit_game
        cmp al, 'N'
        je exit_game
        cmp al, 27
        je exit_game
        jmp .win_wait

;=======================================================================
; GAME OVER
;=======================================================================
game_over:
        mov eax, SYS_CLEAR
        int 0x80

        ; Death sound
        mov eax, SYS_BEEP
        mov ebx, SND_DEATH
        mov ecx, 15
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 20
        mov ecx, 2
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_gameover_title
        int 0x80

        ; Skull art
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 32
        mov ecx, 4
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, skull_art1
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 32
        mov ecx, 5
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, skull_art2
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 32
        mov ecx, 6
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, skull_art3
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 32
        mov ecx, 7
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, skull_art4
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 32
        mov ecx, 8
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, skull_art5
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 8
        mov ecx, 10
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_gameover_msg
        int 0x80

        ; Show stats
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 12
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_final_stats
        int 0x80
        call draw_final_stats

        ; Educational message
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 20
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_edu_msg
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 20
        mov ecx, 22
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_play_again
        int 0x80

.go_wait:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'y'
        je title_screen
        cmp al, 'Y'
        je title_screen
        cmp al, 'n'
        je exit_game
        cmp al, 'N'
        je exit_game
        cmp al, 27
        je exit_game
        jmp .go_wait

;=======================================================================
; EXIT
;=======================================================================
exit_game:
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================================================
; DRAWING ROUTINES
;=======================================================================

;---------------------------------------
; draw_game_screen - Main game display
;---------------------------------------
draw_game_screen:
        pushad
        mov eax, SYS_CLEAR
        int 0x80

        ; === TOP BAR: Month and status ===
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE | BG_BLUE
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        xor ecx, ecx
        int 0x80

        ; Print 80 spaces for background bar
        mov ecx, 80
.bar_space:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        dec ecx
        jnz .bar_space

        mov eax, SYS_SETCURSOR
        mov ebx, 1
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_month_label
        int 0x80

        ; Print month name
        mov eax, [month]
        dec eax
        imul eax, 4
        mov ebx, [month_names + eax]
        mov eax, SYS_PRINT
        int 0x80

        ; Print population on right
        mov eax, SYS_SETCURSOR
        mov ebx, 45
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_pop_label
        int 0x80
        mov eax, [population]
        call print_number
        mov eax, SYS_PUTCHAR
        mov ebx, '/'
        int 0x80
        mov eax, COMMUNITY_SIZE
        call print_number

        ; === POPULATION DISPLAY (visual bar chart) ===
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 2
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_community
        int 0x80

        ; Draw population bar
        ; Green=vaccinated, Cyan=healthy, Red=infected, Yellow=recovered, Gray=dead slots
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 3
        int 0x80

        ; Scale: each char = ~3 people (66 chars for 200 people)
        ; Vaccinated (green blocks)
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, [vaccinated]
        call draw_pop_segment

        ; Healthy (cyan blocks)
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, [healthy]
        call draw_pop_segment

        ; Recovered (yellow blocks)
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, [recovered]
        call draw_pop_segment

        ; Infected (red blocks)
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, [infected]
        call draw_pop_segment

        ; Dead (dark gray blocks)
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, [dead]
        call draw_pop_segment

        ; Legend row
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 4
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_leg_vax
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_leg_healthy
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_leg_recovered
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_leg_infected
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_leg_dead
        int 0x80

        ; === STATS PANEL ===
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80

        ; Left column: population numbers
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 6
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_stat_vax
        int 0x80
        mov eax, [vaccinated]
        call print_number

        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 7
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_stat_healthy
        int 0x80
        mov eax, [healthy]
        call print_number

        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 8
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_stat_infected
        int 0x80
        mov eax, [infected]
        call print_number

        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 9
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_stat_recovered
        int 0x80
        mov eax, [recovered]
        call print_number

        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 10
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_stat_dead
        int 0x80
        mov eax, [dead]
        call print_number

        ; Right column: resources
        mov eax, SYS_SETCURSOR
        mov ebx, 35
        mov ecx, 6
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_stat_vaccines
        int 0x80
        mov eax, [vaccines]
        call print_number

        mov eax, SYS_SETCURSOR
        mov ebx, 35
        mov ecx, 7
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_stat_supplies
        int 0x80
        mov eax, [supplies]
        call print_number

        ; Morale bar
        mov eax, SYS_SETCURSOR
        mov ebx, 35
        mov ecx, 8
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_stat_morale
        int 0x80
        call draw_morale_bar

        ; Research progress
        mov eax, SYS_SETCURSOR
        mov ebx, 35
        mov ecx, 9
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_stat_research
        int 0x80
        mov eax, [research]
        call print_number

        ; Buildings
        mov eax, SYS_SETCURSOR
        mov ebx, 35
        mov ecx, 10
        int 0x80
        cmp byte [hospital_built], 1
        jne .no_hosp_icon
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_hosp_yes
        int 0x80
        jmp .check_lab_icon
.no_hosp_icon:
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_hosp_no
        int 0x80
.check_lab_icon:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        cmp byte [lab_built], 1
        jne .no_lab_icon
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_lab_yes
        int 0x80
        jmp .icons_done
.no_lab_icon:
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_lab_no
        int 0x80
.icons_done:

        ; === OUTBREAK TRACKER (visual timeline) ===
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 12
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_timeline
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 13
        int 0x80
        call draw_timeline

        ; Actions remaining
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 15
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_actions_left
        int 0x80
        mov eax, [actions_left]
        call print_number

        ; Separator line
        mov eax, SYS_SETCURSOR
        mov ebx, 0
        mov ecx, 16
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_separator
        int 0x80

        popad
        ret

;---------------------------------------
; draw_action_menu
;---------------------------------------
draw_action_menu:
        pushad

        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 17
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_choose_action
        int 0x80

        ; Actions
        mov eax, SYS_SETCURSOR
        mov ebx, 4
        mov ecx, 18
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_act_vaccinate
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 4
        mov ecx, 19
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_act_treat
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 4
        mov ecx, 20
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_act_supply
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 42
        mov ecx, 18
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LMAGENTA
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_act_research
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 42
        mov ecx, 19
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LBLUE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_act_awareness
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 42
        mov ecx, 20
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_act_rest
        int 0x80

        popad
        ret

;---------------------------------------
; draw_month_summary
;---------------------------------------
draw_month_summary:
        pushad
        mov eax, SYS_CLEAR
        int 0x80

        ; Header
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_SETCURSOR
        mov ebx, 20
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_month_summary
        int 0x80

        ; Month name
        mov eax, SYS_SETCURSOR
        mov ebx, 30
        mov ecx, 1
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, [month]
        dec eax
        imul eax, 4
        mov ebx, [month_names + eax]
        mov eax, SYS_PRINT
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
        mov ebx, str_sep_short
        int 0x80

        ; New infections
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 5
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_new_infections
        int 0x80
        mov eax, [temp_val]
        call print_number

        ; Deaths
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 7
        int 0x80
        cmp dword [temp_val2], 0
        je .no_death_report
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_deaths_report
        int 0x80
        mov eax, [temp_val2]
        call print_number
        mov eax, SYS_PRINT
        mov ebx, str_ppl_lost
        int 0x80

        ; Death sound
        mov eax, SYS_BEEP
        mov ebx, SND_DEATH
        mov ecx, 5
        int 0x80
        jmp .after_death

.no_death_report:
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_no_deaths
        int 0x80
.after_death:

        ; Infection % bar
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 9
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_infection_rate
        int 0x80

        ; Draw community bar at summary
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 11
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_community
        int 0x80

        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 12
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, [vaccinated]
        call draw_pop_segment
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, [healthy]
        call draw_pop_segment
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, [recovered]
        call draw_pop_segment
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, [infected]
        call draw_pop_segment

        ; Morale indicator
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 14
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_stat_morale
        int 0x80
        call draw_morale_bar

        ; Educational tip
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 17
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_tip_label
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGRAY
        int 0x80
        call print_random_tip

        popad
        ret

;---------------------------------------
; show_event_screen
;---------------------------------------
show_event_screen:
        pushad
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 19
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_event_header
        int 0x80
        popad
        ret

;---------------------------------------
; print_event_msg - EBX = message string
;---------------------------------------
print_event_msg:
        pushad
        push ebx
        mov eax, SYS_SETCURSOR
        mov ebx, 5
        mov ecx, 20
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        pop ebx
        mov eax, SYS_PRINT
        int 0x80
        popad
        ret

;---------------------------------------
; draw_pop_segment - Draw EAX people as blocks (scaled to 66 chars = 200 people)
;---------------------------------------
draw_pop_segment:
        pushad
        ; chars = (people * 66) / 200
        imul eax, 66
        xor edx, edx
        mov ecx, COMMUNITY_SIZE
        div ecx
        mov ecx, eax
        cmp ecx, 0
        jle .seg_done
.seg_loop:
        mov eax, SYS_PUTCHAR
        mov ebx, 0xDB           ; Full block character
        int 0x80
        dec ecx
        jnz .seg_loop
.seg_done:
        popad
        ret

;---------------------------------------
; draw_morale_bar - Visual morale meter
;---------------------------------------
draw_morale_bar:
        pushad
        mov eax, [morale]
        ; Choose color based on morale level
        cmp eax, 60
        jge .mor_green
        cmp eax, 30
        jge .mor_yellow
        mov ebx, C_LRED
        jmp .mor_draw
.mor_green:
        mov ebx, C_LGREEN
        jmp .mor_draw
.mor_yellow:
        mov ebx, C_YELLOW
.mor_draw:
        push eax
        mov eax, SYS_SETCOLOR
        int 0x80
        pop eax

        ; Draw bar: morale/5 filled chars out of 20
        mov ecx, eax
        shr ecx, 2             ; /4 -> max 25 chars
        cmp ecx, 20
        jle .mor_cap
        mov ecx, 20
.mor_cap:
        mov edx, ecx           ; save filled count
        cmp ecx, 0
        jle .mor_empty
.mor_fill:
        push ecx
        mov eax, SYS_PUTCHAR
        mov ebx, 0xDB
        int 0x80
        pop ecx
        dec ecx
        jnz .mor_fill
.mor_empty:
        ; Draw remaining as dark
        mov ecx, 20
        sub ecx, edx
        cmp ecx, 0
        jle .mor_done
        push ecx
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        pop ecx
.mor_dim:
        push ecx
        mov eax, SYS_PUTCHAR
        mov ebx, 0xB0           ; Light shade
        int 0x80
        pop ecx
        dec ecx
        jnz .mor_dim
.mor_done:
        ; Print number
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        mov eax, [morale]
        call print_number
        mov eax, SYS_PUTCHAR
        mov ebx, '%'
        int 0x80
        popad
        ret

;---------------------------------------
; draw_timeline - Show month progress
;---------------------------------------
draw_timeline:
        pushad
        mov ecx, 1             ; month counter
.tl_loop:
        mov eax, [set_months]
        inc eax
        cmp ecx, eax
        jge .tl_done
        push ecx

        ; Color based on relation to current month
        cmp ecx, [month]
        jl .tl_past
        je .tl_current
        ; Future
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xB0
        int 0x80
        jmp .tl_sep

.tl_past:
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xDB
        int 0x80
        jmp .tl_sep

.tl_current:
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xFE           ; Center dot
        int 0x80

.tl_sep:
        ; Print separator between months
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, '-'
        int 0x80

        pop ecx
        inc ecx
        jmp .tl_loop
.tl_done:
        ; Print month labels
        mov eax, SYS_SETCOLOR
        mov ebx, C_DGRAY
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_tl_labels
        int 0x80
        popad
        ret

;---------------------------------------
; draw_final_stats
;---------------------------------------
draw_final_stats:
        pushad
        ; Row 1: Survivors
        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 13
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_fs_survivors
        int 0x80
        mov eax, [population]
        call print_number
        mov eax, SYS_PUTCHAR
        mov ebx, '/'
        int 0x80
        mov eax, COMMUNITY_SIZE
        call print_number

        ; Row 2: Total vaccinated
        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 14
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_fs_vaccinated
        int 0x80
        mov eax, [total_vaccinated]
        call print_number

        ; Row 3: Total treated
        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 15
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_fs_treated
        int 0x80
        mov eax, [total_treated]
        call print_number

        ; Row 4: Deaths
        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 16
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_fs_deaths
        int 0x80
        mov eax, [dead]
        call print_number

        ; Row 5: Months survived
        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 17
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, C_WHITE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_fs_months
        int 0x80
        mov eax, [outbreaks_survived]
        call print_number

        popad
        ret

;---------------------------------------
; calc_rating - Calculate and display performance rating
;---------------------------------------
calc_rating:
        pushad

        mov eax, SYS_SETCURSOR
        mov ebx, 15
        mov ecx, 19
        int 0x80

        ; Choose rating based on survival %
        mov eax, [population]
        imul eax, 100
        xor edx, edx
        mov ebx, COMMUNITY_SIZE
        div ebx                 ; EAX = survival percentage
        
        cmp eax, 90
        jge .rate_s
        cmp eax, 75
        jge .rate_a
        cmp eax, 60
        jge .rate_b
        cmp eax, 40
        jge .rate_c
        jmp .rate_d

.rate_s:
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_rate_s
        int 0x80
        jmp .rate_done
.rate_a:
        mov eax, SYS_SETCOLOR
        mov ebx, C_LGREEN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_rate_a
        int 0x80
        jmp .rate_done
.rate_b:
        mov eax, SYS_SETCOLOR
        mov ebx, C_LCYAN
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_rate_b
        int 0x80
        jmp .rate_done
.rate_c:
        mov eax, SYS_SETCOLOR
        mov ebx, C_YELLOW
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_rate_c
        int 0x80
        jmp .rate_done
.rate_d:
        mov eax, SYS_SETCOLOR
        mov ebx, C_LRED
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_rate_d
        int 0x80
.rate_done:
        popad
        ret

;=======================================================================
; UTILITY FUNCTIONS
;=======================================================================

;---------------------------------------
; random - LCG PRNG, result in EAX (0-32767)
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

;---------------------------------------
; print_number - Print EAX as decimal
;---------------------------------------
print_number:
        pushad
        mov ecx, 0              ; digit count
        mov ebx, 10
        cmp eax, 0
        jne .pn_nonzero
        ; Print "0"
        mov eax, SYS_PUTCHAR
        mov ebx, '0'
        int 0x80
        popad
        ret
.pn_nonzero:
        ; Extract digits
.pn_div:
        xor edx, edx
        div ebx
        push edx
        inc ecx
        cmp eax, 0
        jne .pn_div
        ; Print digits
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
; calc_difficulty - Increases as months progress
;---------------------------------------
calc_difficulty:
        pushad
        mov eax, [month]
        ; Difficulty modifier: Easy=-2, Normal=0, Hard=+3
        mov ebx, [set_diff]
        cmp ebx, 0
        je .diff_easy
        cmp ebx, 2
        je .diff_hard
        ; Normal: no modifier
        jmp .diff_store
.diff_easy:
        sub eax, 2
        cmp eax, 0
        jge .diff_store
        xor eax, eax
        jmp .diff_store
.diff_hard:
        add eax, 3
.diff_store:
        mov [difficulty], eax
        popad
        ret

;---------------------------------------
; calc_infection_rate - Returns new infections in EAX
;---------------------------------------
calc_infection_rate:
        push ebx
        push ecx
        push edx

        ; Get immunity percentage (vaccinated + recovered)
        mov eax, [vaccinated]
        add eax, [recovered]   ; Recovered also have immunity
        imul eax, 100
        xor edx, edx
        mov ecx, COMMUNITY_SIZE
        div ecx                 ; EAX = immune %

        ; Reduce infection rate: for each 10% immune, reduce by VACCINE_EFFECT
        xor edx, edx
        mov ecx, 10
        div ecx                 ; EAX = immune_pct / 10
        imul eax, VACCINE_EFFECT ; EAX = reduction amount

        ; Infection count = (base_rate + difficulty - reduction) * healthy / 100
        mov ebx, OUTBREAK_BASE
        add ebx, [difficulty]
        sub ebx, eax
        cmp ebx, 2
        jge .rate_floor
        mov ebx, 2              ; Minimum 2% infection rate
.rate_floor:
        ; Apply to healthy population
        mov eax, [healthy]
        imul eax, ebx
        xor edx, edx
        mov ecx, 100
        div ecx                 ; EAX = new infections

        ; Add some randomness (+/- 3)
        push eax
        call random
        xor edx, edx
        mov ecx, 7
        div ecx
        sub edx, 3             ; -3 to +3
        mov ebx, edx
        pop eax
        add eax, ebx
        cmp eax, 0
        jge .inf_nonneg
        xor eax, eax
.inf_nonneg:

        pop edx
        pop ecx
        pop ebx
        ret

;---------------------------------------
; clamp_morale - Keep morale in 0..MAX_MORALE
;---------------------------------------
clamp_morale:
        cmp dword [morale], 0
        jge .clamp_hi
        mov dword [morale], 0
.clamp_hi:
        cmp dword [morale], MAX_MORALE
        jle .clamp_done
        mov dword [morale], MAX_MORALE
.clamp_done:
        ret

;---------------------------------------
; pause_message - Brief pause for reading
;---------------------------------------
pause_message:
        pushad
        mov eax, SYS_SLEEP
        mov ebx, 60
        int 0x80
        popad
        ret

;---------------------------------------
; print_random_tip - Print an educational vaccination fact
;---------------------------------------
print_random_tip:
        pushad
        call random
        xor edx, edx
        mov ebx, 10
        div ebx
        ; EDX = tip index 0-9
        mov eax, edx
        imul eax, 4
        mov ebx, [tip_table + eax]
        mov eax, SYS_PRINT
        int 0x80
        popad
        ret

;---------------------------------------
; play_title_melody - Catchy intro tune
;---------------------------------------
play_title_melody:
        pushad
        ; C E G C' (ascending)
        mov eax, SYS_BEEP
        mov ebx, 523
        mov ecx, 4
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 659
        mov ecx, 4
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 784
        mov ecx, 4
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 1047
        mov ecx, 6
        int 0x80
        popad
        ret

;---------------------------------------
; play_victory_melody
;---------------------------------------
play_victory_melody:
        pushad
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
        mov ecx, 3
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 784
        mov ecx, 3
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 1047
        mov ecx, 6
        int 0x80
        mov eax, SYS_BEEP
        mov ebx, 1319
        mov ecx, 8
        int 0x80
        popad
        ret

;=======================================================================
; DATA SECTION
;=======================================================================

; --- Title Screen ---
str_border_top:  db 0xC9, "========================================", 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, "============", 0xCD, 0xCD, 0xCD, 0xCD, 0xBB, 0
str_border_bot:  db 0xC8, "========================================", 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, "============", 0xCD, 0xCD, 0xCD, 0xCD, 0xBC, 0
str_title1:      db "* OUTBREAK SHIELD *", 0
str_title2:      db "A Vaccination Simulation Game", 0
str_subtitle:    db "Can you protect your community from Ratel Fever?", 0

; Virus ASCII art
virus_art1:      db "    .::::.", 0
virus_art2:      db "  .::o]]:o::.", 0
virus_art3:      db " .:]:::::::]::.", 0
virus_art4:      db "  .::o]]:o::.", 0
virus_art5:      db "    '::::'", 0

; Syringe ASCII art
syringe_art1:    db "  ____", 0
syringe_art2:    db " |====|-->", 0
syringe_art3:    db "  ~~~~", 0

; Shield ASCII art
shield_art1:     db "  /IIIII\\", 0
shield_art2:     db " | +++ |", 0
shield_art3:     db "  \\ + /", 0
shield_art4:     db "   \\_/", 0

str_story1:      db "The year is 2024. A deadly virus called Ratel Fever has", 0
str_story2:      db "emerged. You are Dr. Pryor, chief epidemiologist. Lead your", 0
str_story3:      db "community through months of deadly outbreak.", 0

str_menu_play:   db "[1] Begin Outbreak Response", 0
str_menu_howto:  db "[2] How to Play", 0
str_menu_settings: db "[3] Settings", 0
str_menu_quit:   db "[4] Quit", 0
str_footer:      db "Vaccines save lives. Knowledge is your best weapon.", 0

; --- Settings Screen ---
str_settings_title: db "=== GAME SETTINGS ===", 0
str_settings_hint: db "Choose a preset or customize individual settings.", 0
str_set_vaccines: db "[1] Starting Vaccines:  ", 0
str_set_supplies: db "[2] Starting Supplies:  ", 0
str_set_morale:   db "[3] Starting Morale:    ", 0
str_set_months:   db "[4] Game Length:         ", 0
str_set_diff:     db "[5] Difficulty:          ", 0
str_months_suffix: db " months", 0
str_set_presets:  db "Quick Presets:", 0
str_preset_easy:  db "[1] Easy   - 75 vaccines, 60 supplies, 90% morale", 0
str_preset_normal:db "[2] Normal - 50 vaccines, 40 supplies, 70% morale", 0
str_preset_hard:  db "[3] Hard   - 25 vaccines, 20 supplies, 50% morale", 0
str_preset_custom:db "[4] Custom - Set each value individually", 0
str_set_footer:   db "Press ESC to return to title screen.", 0

; Custom settings screen
str_custom_title: db "=== CUSTOM SETTINGS ===", 0
str_cust_vaccines: db "Starting Vaccines: [1] 25  [2] 50  [3] 75  [4] 100", 0
str_cust_supplies: db "Starting Supplies: [1] 20  [2] 40  [3] 60  [4] 80", 0
str_cust_morale:   db "Starting Morale:   [1] 40% [2] 60% [3] 80% [4] 100%", 0
str_cust_months:   db "Game Length:       [1] 6   [2] 12  [3] 18  [4] 24 months", 0
str_cust_diff:     db "Difficulty:        [1] Easy  [2] Normal  [3] Hard", 0
str_custom_saved:  db "Settings saved! Returning to settings overview...", 0

; Difficulty names
diff_name_easy:  db "Easy", 0
diff_name_normal: db "Normal", 0
diff_name_hard:  db "Hard", 0

diff_names:
        dd diff_name_easy, diff_name_normal, diff_name_hard

; --- How To Play ---
str_howto_title: db "=== HOW TO PLAY ===", 0

howto_line1:     db " ", 0
howto_line2:     db "You lead a community of 200 people through a viral outbreak.", 0
howto_line3:     db "Each month you get 3 actions. Customize settings from the menu!", 0
howto_line4:     db " ", 0
howto_line5:     db "ACTIONS:", 0
howto_line6:     db "  [1] Vaccinate  - Use vaccine doses to immunize the healthy", 0
howto_line7:     db "  [2] Treat Sick - Use medical supplies to cure the infected", 0
howto_line8:     db "  [3] Supply Run - Gather more vaccines and medical supplies", 0
howto_line9:     db "  [4] Research   - Work toward building a Hospital & Lab", 0
howto_line10:    db "  [5] Awareness  - Boost morale, may convince people to vaccinate", 0
howto_line11:    db "  [6] Rest       - Skip an action for a small morale boost", 0
howto_line12:    db " ", 0
howto_line13:    db "TIPS:", 0
howto_line14:    db "  * Vaccinated people are immune to infection", 0
howto_line15:    db "  * High morale reduces death rates", 0
howto_line16:    db "  * The Hospital gives bonus treatment; the Lab boosts research", 0
howto_line17:    db "  * Random events can help or hinder your progress", 0
howto_line18:    db "  * The virus gets stronger each month -- stay ahead of it!", 0

howto_lines:
        dd howto_line1, howto_line2, howto_line3, howto_line4
        dd howto_line5, howto_line6, howto_line7, howto_line8
        dd howto_line9, howto_line10, howto_line11, howto_line12
        dd howto_line13, howto_line14, howto_line15, howto_line16
        dd howto_line17, howto_line18

str_press_any:   db "Press any key to return...", 0

; --- Game Screen ---
str_month_label: db " Month: ", 0
str_pop_label:   db "Population: ", 0
str_community:   db "Community Health:", 0

; Legend
str_leg_vax:     db "[Vax] ", 0
str_leg_healthy: db "[Healthy] ", 0
str_leg_recovered: db "[Recovered] ", 0
str_leg_infected:db "[Infected] ", 0
str_leg_dead:    db "[Dead]", 0

; Stats labels
str_stat_vax:     db "Vaccinated: ", 0
str_stat_healthy: db "Healthy:    ", 0
str_stat_infected:db "Infected:   ", 0
str_stat_recovered:db "Recovered:  ", 0
str_stat_dead:    db "Deceased:   ", 0
str_stat_vaccines:db "Vaccines:  ", 0
str_stat_supplies:db "Supplies:  ", 0
str_stat_morale:  db "Morale: ", 0
str_stat_research:db "Research: ", 0

str_hosp_yes:    db "[+Hospital]", 0
str_hosp_no:     db "[-Hospital]", 0
str_lab_yes:     db "[+Lab]", 0
str_lab_no:      db "[-Lab]", 0

str_timeline:    db "Outbreak Timeline: [Month 1", 0xC4, 0xC4, 0xC4, 0xC4, "12]", 0
str_tl_labels:   db " (J F M A M J J A S O N D)", 0
str_actions_left: db "Actions remaining: ", 0
str_separator:   db "----------------------------------------------------------------------", 0xC4, 0xC4, 0xC4, 0xC4, 0xC4, 0xC4, 0xC4, 0xC4, 0

; Action menu
str_choose_action: db "Choose your action:", 0
str_act_vaccinate: db "[1] Vaccination Drive  (uses vaccines)", 0
str_act_treat:     db "[2] Treat the Sick     (uses supplies)", 0
str_act_supply:    db "[3] Supply Run         (gather resources)", 0
str_act_research:  db "[4] Research           (build upgrades)", 0
str_act_awareness: db "[5] Public Awareness   (boost morale)", 0
str_act_rest:      db "[6] Rest               (skip action)", 0

; Action results
str_vax_success:   db "Vaccination drive successful! Immunized: ", 0
str_people_vaxxed: db " people.", 0
str_no_vaccines:   db "No vaccine doses available! Do a supply run.", 0
str_all_vaxxed:    db "Everyone is already vaccinated or immune!", 0
str_treated:       db "Medical treatment administered! Cured: ", 0
str_people_treated:db " patients.", 0
str_no_sick:       db "Great news -- no one is currently infected!", 0
str_no_supplies:   db "No medical supplies left! Do a supply run.", 0
str_supply_ok:     db "Supply run successful! Got: ", 0
str_supply_risk:   db "Supply run complete, but 2 workers got infected!", 0
str_vax_and:       db " vaccines and ", 0
str_med_supplies:  db " medical supplies.", 0
str_research_pts:  db "Research progress: +", 0
str_research_gained:db " points.", 0
str_hospital_built:db "*** HOSPITAL BUILT! Treatment capacity increased! ***", 0
str_lab_built:     db "*** RESEARCH LAB BUILT! Research gains boosted! ***", 0
str_awareness_ok:  db "Public awareness campaign boosted community morale!", 0
str_awareness_vax: db "Campaign success! 5 people voluntarily got vaccinated!", 0
str_rest:          db "The team rests and regroups. Morale slightly improved.", 0

; Month summary
str_month_summary: db "=== END OF MONTH REPORT ===", 0
str_new_infections:db "New infections this month: ", 0
str_deaths_report: db "Lives lost: ", 0
str_ppl_lost:      db " people succumbed to the virus.", 0
str_no_deaths:     db "No deaths this month!", 0
str_infection_rate:db "Disease pressure is ", 0
str_sep_short:     db "--------------------------------------------", 0

; Events
str_event_header:  db "*** BREAKING NEWS ***", 0
str_evt_donation:  db "A neighboring region donated vaccines and supplies!", 0
str_evt_antivax:   db "Anti-vaccination rally: misinformation spread, morale dropped.", 0
str_evt_volunteer: db "Medical volunteers arrived! Supplies and morale boosted.", 0
str_evt_mutation:  db "The virus mutated! Some vaccinated people lost immunity.", 0
str_evt_theft:     db "Supply warehouse was raided. Lost 25% of vaccines & supplies.", 0
str_evt_medteam:   db "Emergency medical team treated 10 patients for free!", 0
str_evt_quarantine:db "Quarantine breach! Infected people spread the virus further.", 0
str_evt_goodnews:  db "Community spirit is high! Everyone is pulling together.", 0

; End screens
str_win_title:     db "*** OUTBREAK CONTAINED! YOU DID IT! ***", 0
str_gameover_title:db "*** THE OUTBREAK WAS LOST ***", 0
str_gameover_msg:  db "The community could not survive the Ratel Fever outbreak.", 0
str_final_stats:   db "--- Final Statistics ---", 0
str_fs_survivors:  db "Survivors:       ", 0
str_fs_vaccinated: db "Total vaccinated: ", 0
str_fs_treated:    db "Total treated:    ", 0
str_fs_deaths:     db "Lives lost:       ", 0
str_fs_months:     db "Months survived:  ", 0

str_edu_msg:       db "Remember: Vaccines are our strongest tool against outbreaks.", 0

; Ratings
str_rate_s:        db "Rating: S - LEGENDARY EPIDEMIOLOGIST! Herd immunity achieved!", 0
str_rate_a:        db "Rating: A - Excellent! Your leadership saved many lives.", 0
str_rate_b:        db "Rating: B - Good effort. The community survived, but at a cost.", 0
str_rate_c:        db "Rating: C - Many were lost. Earlier vaccination could have helped.", 0
str_rate_d:        db "Rating: D - Devastating losses. Prevention is better than cure.", 0

str_play_again:    db "Play again? (Y/N)", 0
str_confirm_quit:  db "Quit the game? Your community needs you! (Y/N)", 0
str_press_continue:db "Press any key to continue...", 0

str_tip_label:     db "Did you know? ", 0

; Trophy art
trophy_art1:       db "     ___________", 0
trophy_art2:       db "    '._==_==_=_.'", 0
trophy_art3:       db "    .-\\:      /-.", 0
trophy_art4:       db "   | (|:.-)(-.|) |", 0
trophy_art5:       db "    '-|:.):( |.-'", 0

; Skull art
skull_art1:        db "    _____", 0
skull_art2:        db "   /     \\", 0
skull_art3:        db "  | () () |", 0
skull_art4:        db "   \\  ^  /", 0
skull_art5:        db "    |||||", 0

; Educational tips (real vaccination facts)
tip_1:  db "Vaccines work by training your immune system to recognize threats.", 0
tip_2:  db "Herd immunity protects those who cannot be vaccinated.", 0
tip_3:  db "Smallpox was eradicated entirely through vaccination in 1980.", 0
tip_4:  db "The first vaccine was developed by Edward Jenner in 1796.", 0
tip_5:  db "Measles vaccination prevents ~2.6 million deaths per year.", 0
tip_6:  db "Vaccines undergo rigorous safety testing before approval.", 0
tip_7:  db "Clean water and vaccines are the two greatest public health tools.", 0
tip_8:  db "Polio cases have decreased by 99% since vaccination began.", 0
tip_9:  db "Community vaccination rates above 90% create herd immunity.", 0
tip_10: db "The WHO estimates vaccines prevent 3.5-5 million deaths yearly.", 0

tip_table:
        dd tip_1, tip_2, tip_3, tip_4, tip_5
        dd tip_6, tip_7, tip_8, tip_9, tip_10

; Month names
month_jan: db "January", 0, 0, 0
month_feb: db "February", 0, 0
month_mar: db "March", 0, 0, 0, 0, 0
month_apr: db "April", 0, 0, 0, 0, 0
month_may: db "May", 0, 0, 0, 0, 0, 0, 0
month_jun: db "June", 0, 0, 0, 0, 0, 0
month_jul: db "July", 0, 0, 0, 0, 0, 0
month_aug: db "August", 0, 0, 0, 0
month_sep: db "September", 0
month_oct: db "October", 0, 0, 0
month_nov: db "November", 0, 0
month_dec: db "December", 0, 0

month_names:
        dd month_jan, month_feb, month_mar, month_apr
        dd month_may, month_jun, month_jul, month_aug
        dd month_sep, month_oct, month_nov, month_dec

;=======================================================================
; BSS - Game State Variables
;=======================================================================
section .bss

rand_seed:          resd 1

; Core state
month:              resd 1
population:         resd 1
healthy:            resd 1
vaccinated:         resd 1
infected:           resd 1
recovered:          resd 1
dead:               resd 1

; Resources
vaccines:           resd 1
supplies:           resd 1
morale:             resd 1
research:           resd 1
actions_left:       resd 1
difficulty:         resd 1

; Lifetime stats
total_vaccinated:   resd 1
total_treated:      resd 1
outbreaks_survived: resd 1

; Buildings
hospital_built:     resb 1
lab_built:          resb 1

; Temp
event_type:         resd 1
temp_val:           resd 1
temp_val2:          resd 1

; Settings
set_vaccines:       resd 1
set_supplies:       resd 1
set_morale:         resd 1
set_months:         resd 1
set_diff:           resd 1
