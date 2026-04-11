; weather.asm - Animated Weather Station Dashboard for Mellivora OS
; Simulated weather with ASCII art, bar charts, and animations.
; Press 'q' to quit, space to cycle weather.
%include "syscalls.inc"

SCREEN_W        equ 80
SCREEN_H        equ 25
TICK_DELAY      equ 10          ; ~10fps

; Weather types
W_SUNNY         equ 0
W_CLOUDY        equ 1
W_RAINY         equ 2
W_STORM         equ 3
W_SNOWY         equ 4
W_COUNT         equ 5

start:
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_state], eax
        mov dword [weather], W_SUNNY
        mov dword [temperature], 72
        mov dword [humidity], 45
        mov dword [wind_speed], 8
        mov dword [pressure], 1013
        mov dword [frame], 0

;=== Main loop ===
.main_loop:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .no_key
        cmp al, 'q'
        je .exit
        cmp al, 27
        je .exit
        cmp al, ' '
        je .cycle_weather
        jmp .no_key

.cycle_weather:
        inc dword [weather]
        cmp dword [weather], W_COUNT
        jl .weather_ok
        mov dword [weather], 0
.weather_ok:
        call randomize_stats

.no_key:
        inc dword [frame]
        call draw_dashboard

        mov eax, SYS_SLEEP
        mov ebx, TICK_DELAY
        int 0x80
        jmp .main_loop

.exit:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; randomize_stats
;---------------------------------------
randomize_stats:
        pushad
        mov eax, [weather]

        cmp eax, W_SUNNY
        jne .rs_cloudy
        mov dword [temperature], 82
        mov dword [humidity], 30
        mov dword [wind_speed], 5
        mov dword [pressure], 1025
        jmp .rs_done
.rs_cloudy:
        cmp eax, W_CLOUDY
        jne .rs_rainy
        mov dword [temperature], 65
        mov dword [humidity], 60
        mov dword [wind_speed], 12
        mov dword [pressure], 1015
        jmp .rs_done
.rs_rainy:
        cmp eax, W_RAINY
        jne .rs_storm
        mov dword [temperature], 55
        mov dword [humidity], 85
        mov dword [wind_speed], 18
        mov dword [pressure], 1005
        jmp .rs_done
.rs_storm:
        cmp eax, W_STORM
        jne .rs_snowy
        mov dword [temperature], 48
        mov dword [humidity], 95
        mov dword [wind_speed], 35
        mov dword [pressure], 990
        jmp .rs_done
.rs_snowy:
        mov dword [temperature], 28
        mov dword [humidity], 70
        mov dword [wind_speed], 10
        mov dword [pressure], 1010
.rs_done:
        popad
        ret

;---------------------------------------
; draw_dashboard
;---------------------------------------
draw_dashboard:
        pushad

        ; Clear to black
        mov edi, VGA_BASE
        mov eax, 0x00200020
        mov ecx, SCREEN_W * SCREEN_H / 2
        rep stosd

        ; === Title bar (row 0) ===
        mov eax, SYS_SETCOLOR
        mov ebx, 0x1F
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, title_str
        int 0x80

        ; === Weather name (row 2, col 3) ===
        mov eax, SYS_SETCURSOR
        mov ebx, 3
        mov ecx, 2
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lbl_cond
        int 0x80

        ; Get weather name pointer
        mov eax, [weather]
        shl eax, 2
        mov ebx, [weather_names + eax]
        mov eax, SYS_PRINT
        int 0x80

        ; === ASCII art weather icon (rows 4-9, cols 3-20) ===
        call draw_weather_art

        ; === Temperature (rows 2-4, col 35) ===
        mov eax, SYS_SETCURSOR
        mov ebx, 35
        mov ecx, 2
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C           ; Light red for temp
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lbl_temp
        int 0x80
        mov eax, [temperature]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, str_degf
        int 0x80

        ; Big temperature display (row 4, col 35)
        mov eax, SYS_SETCURSOR
        mov ebx, 38
        mov ecx, 4
        int 0x80
        mov eax, SYS_SETCOLOR
        mov eax, [temperature]
        cmp eax, 32
        jl .temp_cold_color
        cmp eax, 70
        jl .temp_mild_color
        ; Hot
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        jmp .temp_big
.temp_cold_color:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x09
        int 0x80
        jmp .temp_big
.temp_mild_color:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
.temp_big:
        mov eax, [temperature]
        call print_dec
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xF8           ; degree symbol
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 'F'
        int 0x80

        ; === Humidity (row 6) ===
        mov eax, SYS_SETCURSOR
        mov ebx, 35
        mov ecx, 6
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lbl_humid
        int 0x80
        mov eax, [humidity]
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, '%'
        int 0x80

        ; Humidity bar (row 7)
        mov eax, SYS_SETCURSOR
        mov ebx, 35
        mov ecx, 7
        int 0x80
        mov eax, [humidity]
        mov ecx, 20             ; bar width
        call draw_bar

        ; === Wind (row 9) ===
        mov eax, SYS_SETCURSOR
        mov ebx, 35
        mov ecx, 9
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lbl_wind
        int 0x80
        mov eax, [wind_speed]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, str_mph
        int 0x80

        ; Wind animation (row 10)
        mov eax, SYS_SETCURSOR
        mov ebx, 35
        mov ecx, 10
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        ; Animate wind lines
        mov ecx, [frame]
        and ecx, 3
        cmp ecx, 0
        je .wind0
        cmp ecx, 1
        je .wind1
        cmp ecx, 2
        je .wind2
        mov eax, SYS_PRINT
        mov ebx, wind_anim3
        int 0x80
        jmp .wind_done
.wind0: mov eax, SYS_PRINT
        mov ebx, wind_anim0
        int 0x80
        jmp .wind_done
.wind1: mov eax, SYS_PRINT
        mov ebx, wind_anim1
        int 0x80
        jmp .wind_done
.wind2: mov eax, SYS_PRINT
        mov ebx, wind_anim2
        int 0x80
.wind_done:

        ; === Pressure (row 12) ===
        mov eax, SYS_SETCURSOR
        mov ebx, 35
        mov ecx, 12
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0D
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lbl_press
        int 0x80
        mov eax, [pressure]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, str_hpa
        int 0x80

        ; === Forecast bar chart (rows 15-22) ===
        mov eax, SYS_SETCURSOR
        mov ebx, 3
        mov ecx, 14
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lbl_forecast
        int 0x80

        ; 7-day forecast bars
        call draw_forecast

        ; === Status bar (row 24) ===
        mov eax, SYS_SETCOLOR
        mov ebx, 0x70
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, 24
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, status_str
        int 0x80

        ; Animate weather effects
        call animate_weather

        popad
        ret

;---------------------------------------
; draw_weather_art
;---------------------------------------
draw_weather_art:
        pushad
        mov eax, [weather]

        cmp eax, W_SUNNY
        je .art_sunny
        cmp eax, W_CLOUDY
        je .art_cloudy
        cmp eax, W_RAINY
        je .art_rainy
        cmp eax, W_STORM
        je .art_storm
        jmp .art_snowy

.art_sunny:
        mov esi, art_sun
        jmp .art_draw
.art_cloudy:
        mov esi, art_cloud
        jmp .art_draw
.art_rainy:
        mov esi, art_rain
        jmp .art_draw
.art_storm:
        mov esi, art_storm_a
        jmp .art_draw
.art_snowy:
        mov esi, art_snow

.art_draw:
        mov ecx, 4              ; start row
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
.art_line:
        cmp ecx, 11
        jge .art_done
        mov eax, SYS_SETCURSOR
        mov ebx, 3
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        ; Advance past null to next line
.art_scan:
        lodsb
        test al, al
        jnz .art_scan
        inc ecx
        jmp .art_line
.art_done:
        popad
        ret

;---------------------------------------
; draw_bar: EAX=value(0-100), ECX=max_width
;---------------------------------------
draw_bar:
        pushad
        ; filled = value * max_width / 100
        imul eax, ecx
        xor edx, edx
        push ecx
        mov ecx, 100
        div ecx
        pop ecx
        mov esi, eax            ; filled count

        xor edi, edi
.bar_loop:
        cmp edi, ecx
        jge .bar_done
        cmp edi, esi
        jge .bar_empty
        ; Filled
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xDB           ; Full block
        int 0x80
        jmp .bar_next
.bar_empty:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0xB0           ; Light shade
        int 0x80
.bar_next:
        inc edi
        jmp .bar_loop
.bar_done:
        popad
        ret

;---------------------------------------
; draw_forecast - 7-day temp bars
;---------------------------------------
draw_forecast:
        pushad
        ; Base temp from current
        mov edx, [temperature]
        xor esi, esi            ; day counter
        mov ecx, 15             ; start row
.fc_loop:
        cmp esi, 7
        jge .fc_done

        mov eax, SYS_SETCURSOR
        mov ebx, 3
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        ; Day label
        push esi
        push ecx
        mov eax, esi
        shl eax, 2
        mov ebx, [day_names + eax]
        mov eax, SYS_PRINT
        int 0x80
        pop ecx
        pop esi

        ; Forecast temp = base + (hash from day)
        mov eax, esi
        imul eax, 7
        add eax, [rand_state]
        xor edx, edx
        push ecx
        mov ecx, 21
        div ecx
        pop ecx
        sub edx, 10
        add edx, [temperature]

        ; Clamp 0-120
        cmp edx, 0
        jge .fc_clamp_hi
        xor edx, edx
.fc_clamp_hi:
        cmp edx, 120
        jle .fc_draw
        mov edx, 120
.fc_draw:
        ; Print temp
        push edx
        push ecx
        mov eax, edx
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, 0xF8
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

        ; Draw bar (value/120 * 30 chars)
        pop ecx
        pop edx
        mov eax, edx
        imul eax, 30
        xor edx, edx
        push ecx
        mov ecx, 120
        div ecx
        pop ecx
        mov edi, eax            ; bar length
        push ecx
        xor ecx, ecx
.fc_bar:
        cmp ecx, edi
        jge .fc_bar_done
        ; Color by temp
        cmp ecx, 8
        jl .fc_blue
        cmp ecx, 18
        jl .fc_green
        ; Red/warm
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        jmp .fc_char
.fc_blue:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x09
        int 0x80
        jmp .fc_char
.fc_green:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A
        int 0x80
.fc_char:
        mov eax, SYS_PUTCHAR
        mov ebx, 0xDB
        int 0x80
        inc ecx
        jmp .fc_bar
.fc_bar_done:
        pop ecx
        inc ecx                 ; next row
        inc esi
        jmp .fc_loop
.fc_done:
        popad
        ret

;---------------------------------------
; animate_weather - draw rain/snow particles
;---------------------------------------
animate_weather:
        pushad
        mov eax, [weather]
        cmp eax, W_RAINY
        je .anim_rain
        cmp eax, W_STORM
        je .anim_rain
        cmp eax, W_SNOWY
        je .anim_snow
        jmp .anim_done

.anim_rain:
        ; Scatter some rain drops in the art area
        mov ecx, 8
.rain_loop:
        call rand
        xor edx, edx
        push ecx
        mov ecx, 25
        div ecx
        pop ecx
        add edx, 3
        mov ebx, edx            ; col

        push ecx
        call rand
        xor edx, edx
        mov ecx, 6
        div ecx
        pop ecx
        add edx, 5
        ; Write rain char
        push ecx
        mov ecx, edx            ; row
        cmp ecx, SCREEN_H
        jge .rain_skip
        mov al, '|'
        mov ah, 0x09            ; Light blue
        imul ecx, SCREEN_W
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        mov [ecx], ax
.rain_skip:
        pop ecx
        dec ecx
        jnz .rain_loop
        jmp .anim_done

.anim_snow:
        mov ecx, 6
.snow_loop:
        call rand
        xor edx, edx
        push ecx
        mov ecx, 25
        div ecx
        pop ecx
        add edx, 3
        mov ebx, edx

        push ecx
        call rand
        xor edx, edx
        mov ecx, 6
        div ecx
        pop ecx
        add edx, 5

        push ecx
        mov ecx, edx
        cmp ecx, SCREEN_H
        jge .snow_skip
        mov al, '*'
        mov ah, 0x0F
        imul ecx, SCREEN_W
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        mov [ecx], ax
.snow_skip:
        pop ecx
        dec ecx
        jnz .snow_loop

.anim_done:
        popad
        ret

;---------------------------------------
; rand: LCG -> EAX
;---------------------------------------
rand:
        mov eax, [rand_state]
        imul eax, 1103515245
        add eax, 12345
        mov [rand_state], eax
        shr eax, 16
        and eax, 0x7FFF
        ret

; === String Data ===
title_str:      db " Mellivora Weather Station            [SPACE] Cycle  [Q] Quit            ", 0
lbl_cond:       db "Conditions: ", 0
lbl_temp:       db "Temperature: ", 0
lbl_humid:      db "Humidity:    ", 0
lbl_wind:       db "Wind:        ", 0
lbl_press:      db "Pressure:    ", 0
lbl_forecast:   db "7-Day Forecast:", 0
str_degf:       db 0xF8, "F", 0
str_mph:        db " mph", 0
str_hpa:        db " hPa", 0
status_str:     db " Weather Station v1.0             Simulated Data             Mellivora OS ", 0

weather_names:
        dd wn_sunny, wn_cloudy, wn_rainy, wn_storm, wn_snowy
wn_sunny:       db "Sunny", 0
wn_cloudy:      db "Cloudy", 0
wn_rainy:       db "Rainy", 0
wn_storm:       db "Thunderstorm", 0
wn_snowy:       db "Snowy", 0

day_names:
        dd dn_mon, dn_tue, dn_wed, dn_thu, dn_fri, dn_sat, dn_sun
dn_mon: db "Mon ", 0
dn_tue: db "Tue ", 0
dn_wed: db "Wed ", 0
dn_thu: db "Thu ", 0
dn_fri: db "Fri ", 0
dn_sat: db "Sat ", 0
dn_sun: db "Sun ", 0

wind_anim0:     db "~~> ~~~> ~>  ~~~>", 0
wind_anim1:     db " ~~> ~~~> ~> ~~~>", 0
wind_anim2:     db "> ~~> ~~~> ~> ~~~", 0
wind_anim3:     db "~> ~~> ~~~> ~> ~~", 0

; ASCII art (null-separated lines)
art_sun:
        db "    \   |   /    ", 0
        db "     .---.       ", 0
        db "--- | o o | ---  ", 0
        db "     \ _ /       ", 0
        db "    /   |   \    ", 0
        db "   '    '    '   ", 0
        db "                 ", 0

art_cloud:
        db "      .-~~~-.    ", 0
        db "  .- ~       ~ -.", 0
        db " /               \", 0
        db " ~~~~~~~~~~~~~~~~~", 0
        db "                  ", 0
        db "                  ", 0
        db "                  ", 0

art_rain:
        db "      .-~~~-.    ", 0
        db "  .- ~       ~ -.", 0
        db "  ~~~~~~~~~~~~~~~~", 0
        db "   /  /  /  /  /  ", 0
        db "  /  /  /  /  /   ", 0
        db " /  /  /  /  /    ", 0
        db "                  ", 0

art_storm_a:
        db "      .-~~~-.    ", 0
        db "  .- ~       ~ -.", 0
        db "  ~~~~~~~~~~~~~~~~", 0
        db "    _/   /  _/    ", 0
        db "   /  __/  /      ", 0
        db "  /_ /  __/       ", 0
        db "                  ", 0

art_snow:
        db "      .-~~~-.    ", 0
        db "  .- ~       ~ -.", 0
        db "  ~~~~~~~~~~~~~~~~", 0
        db "   *  .  *  .  *  ", 0
        db "  .  *  .  *  .   ", 0
        db "   *  .  *  .  *  ", 0
        db "                  ", 0

; === BSS ===
rand_state:     dd 0
weather:        dd 0
temperature:    dd 0
humidity:       dd 0
wind_speed:     dd 0
pressure:       dd 0
frame:          dd 0
