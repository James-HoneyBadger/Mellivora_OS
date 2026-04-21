; weather.asm - Live Weather Dashboard for Mellivora OS
; Fetches real-time weather data from wttr.in via HTTP
; Usage: weather [city]   (e.g., weather London, weather New York)
; Press R to refresh, Q or ESC to quit
%include "syscalls.inc"
%include "lib/net.inc"
%include "lib/http.inc"

SCREEN_W        equ 80
SCREEN_H        equ 25
TICK_DELAY      equ 10
HTTP_PORT       equ 80

; Weather types for animation overlay
W_CLEAR         equ 0
W_RAIN          equ 1
W_SNOW          equ 2

start:
        ; Get city from command line args
        mov eax, SYS_GETARGS
        mov ebx, city_buf
        int 0x80

        ; Trim trailing newline/whitespace from city
        mov edi, city_buf
.trim:
        cmp byte [edi], 0
        je .trimmed
        cmp byte [edi], 10
        je .do_trim
        cmp byte [edi], 13
        je .do_trim
        inc edi
        jmp .trim
.do_trim:
        mov byte [edi], 0
.trimmed:
        ; Seed RNG from system clock
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_state], eax

;=== Fetch weather data from wttr.in ===
.fetch:
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0B
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_fetch
        int 0x80

        ; DNS resolve wttr.in
        mov esi, api_host
        call net_dns
        test eax, eax
        jz .err_dns
        mov [server_ip], eax

        ; Build URL path: /<city>?0TFdu (URL-encode spaces as '+')
        mov edi, path_buf
        mov byte [edi], '/'
        inc edi
        mov esi, city_buf
.fetch_city:
        lodsb
        test al, al
        jz .fetch_params
        cmp al, ' '
        jne .fetch_nsp
        mov al, '+'
.fetch_nsp:
        stosb
        jmp .fetch_city
.fetch_params:
        mov esi, wttr_params
.fetch_params_copy:
        lodsb
        test al, al
        jz .fetch_path_done
        stosb
        jmp .fetch_params_copy
.fetch_path_done:
        mov byte [edi], 0

        ; Fetch via HTTP GET (http_req_host set for virtual hosting)
        mov dword [http_req_host], api_host
        mov eax, [server_ip]
        mov ebx, HTTP_PORT
        mov ecx, path_buf
        mov edx, body_buf
        mov esi, 4095
        call http_get
        cmp eax, -1
        je .err_conn
        mov [body_len], eax

        ; Check we got body data
        cmp dword [body_len], 0
        je .err_nodata

        ; Clean UTF-8 text to CP437/ASCII
        call clean_body

        ; Detect weather type for animation overlay
        call detect_weather

        ; Record update timestamp
        mov eax, SYS_GETTIME
        int 0x80
        mov [cur_hours], al
        mov [cur_mins], ah
        mov dword [frame], 0

        ; Draw initial dashboard
        call draw_dashboard

;=== Main display loop (animated for rain/snow) ===
.main_loop:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz .no_key
        cmp al, 'q'
        je .exit
        cmp al, 'Q'
        je .exit
        cmp al, 27
        je .exit
        cmp al, 'r'
        je .fetch
        cmp al, 'R'
        je .fetch
.no_key:
        ; Animate only if rain/snow detected
        cmp dword [weather_type], W_CLEAR
        je .skip_anim
        inc dword [frame]
        call draw_dashboard
.skip_anim:
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

;=== Error handlers ===
.err_dns:
        mov esi, err_dns
        jmp .show_err
.err_conn:
        mov esi, err_conn
        jmp .show_err
.err_nodata:
        mov esi, err_nodata

.show_err:
        push esi
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        pop ebx
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_retry
        int 0x80

.err_wait:
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'r'
        je .fetch
        cmp al, 'R'
        je .fetch
        cmp al, 'q'
        je .exit
        cmp al, 'Q'
        je .exit
        cmp al, 27
        je .exit
        jmp .err_wait

;=======================================================================
; clean_body - Convert UTF-8 sequences to CP437-safe characters in place
;   Handles: degree sign, arrows, dashes; strips other non-ASCII
;=======================================================================
clean_body:
        pushad
        mov esi, body_buf
        mov edi, body_buf       ; in-place (output <= input)

.cb_loop:
        lodsb
        test al, al
        jz .cb_done
        cmp al, 0x0D            ; strip carriage return
        je .cb_loop
        cmp al, 0x80            ; ASCII range: pass through
        jb .cb_store

        ; --- C2 B0: degree sign -> CP437 0xF8 ---
        cmp al, 0xC2
        jne .cb_not_c2
        cmp byte [esi], 0xB0
        jne .cb_skip_c2
        mov al, 0xF8
        stosb
        inc esi
        jmp .cb_loop
.cb_skip_c2:
        inc esi                 ; skip 1 continuation byte
        jmp .cb_loop

.cb_not_c2:
        ; --- E2 xx xx: arrows and dashes ---
        cmp al, 0xE2
        jne .cb_other

        ; E2 86 xx: Unicode arrows
        cmp byte [esi], 0x86
        jne .cb_e2_80
        movzx ebx, byte [esi + 1]
        add esi, 2
        cmp bl, 0x90            ; <- left
        je .cb_arrow_l
        cmp bl, 0x91            ; ^ up
        je .cb_arrow_u
        cmp bl, 0x92            ; -> right
        je .cb_arrow_r
        cmp bl, 0x93            ; v down
        je .cb_arrow_d
        jmp .cb_loop
.cb_arrow_l: mov al, '<'
             jmp .cb_store
.cb_arrow_u: mov al, '^'
             jmp .cb_store
.cb_arrow_r: mov al, '>'
             jmp .cb_store
.cb_arrow_d: mov al, 'v'
             jmp .cb_store

        ; E2 80 93/94/95: en-dash, em-dash, horizontal bar -> '-'
.cb_e2_80:
        cmp byte [esi], 0x80
        jne .cb_e2_skip
        movzx ebx, byte [esi + 1]
        add esi, 2
        cmp bl, 0x93
        je .cb_dash
        cmp bl, 0x94
        je .cb_dash
        cmp bl, 0x95
        je .cb_dash
        jmp .cb_loop
.cb_dash:
        mov al, '-'
        jmp .cb_store

.cb_e2_skip:
        add esi, 2              ; skip 2 continuation bytes
        jmp .cb_loop

.cb_other:
        ; Skip other multi-byte UTF-8 sequences
        cmp al, 0xE0
        jb .cb_skip1            ; C0-DF: skip 1 continuation
        cmp al, 0xF0
        jb .cb_skip2            ; E0-EF: skip 2 continuations
        add esi, 3              ; F0-FF: skip 3 continuations
        jmp .cb_loop
.cb_skip2:
        add esi, 2
        jmp .cb_loop
.cb_skip1:
        inc esi
        jmp .cb_loop

.cb_store:
        stosb
        jmp .cb_loop

.cb_done:
        stosb                   ; null terminator
        mov eax, edi
        sub eax, body_buf
        dec eax
        mov [body_len], eax
        popad
        ret

;=======================================================================
; detect_weather - Scan body for condition keywords, set weather_type
;=======================================================================
detect_weather:
        pushad
        mov dword [weather_type], W_CLEAR

        mov esi, body_buf
.dw_scan:
        cmp byte [esi], 0
        je .dw_done
        ; Rain/storm keywords
        cmp dword [esi], 'rain'
        je .dw_rain
        cmp dword [esi], 'Rain'
        je .dw_rain
        cmp dword [esi], 'Driz'
        je .dw_rain
        cmp dword [esi], 'driz'
        je .dw_rain
        cmp dword [esi], 'Show'
        je .dw_rain
        cmp dword [esi], 'show'
        je .dw_rain
        cmp dword [esi], 'Thun'
        je .dw_rain
        cmp dword [esi], 'thun'
        je .dw_rain
        ; Snow/sleet keywords
        cmp dword [esi], 'Snow'
        je .dw_snow
        cmp dword [esi], 'snow'
        je .dw_snow
        cmp dword [esi], 'Slee'
        je .dw_snow
        cmp dword [esi], 'slee'
        je .dw_snow
        cmp dword [esi], 'Bliz'
        je .dw_snow
        inc esi
        jmp .dw_scan
.dw_rain:
        mov dword [weather_type], W_RAIN
        jmp .dw_done
.dw_snow:
        mov dword [weather_type], W_SNOW
.dw_done:
        popad
        ret

;=======================================================================
; draw_dashboard - Render weather display with title/status bars
;=======================================================================
draw_dashboard:
        pushad

        ; Clear VGA to black
        mov edi, VGA_BASE
        mov eax, 0x00200020
        mov ecx, SCREEN_W * SCREEN_H / 2
        rep stosd

        ; === Title bar (row 0, blue background) ===
        mov eax, SYS_SETCOLOR
        mov ebx, 0x1F
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        xor ecx, ecx
        int 0x80
        ; Fill row with spaces for background color
        mov edi, SCREEN_W
.fill_t:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        dec edi
        jnz .fill_t

        ; Title text at col 2
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, title_str
        int 0x80

        ; "Updated HH:MM" at col 60
        mov eax, SYS_SETCURSOR
        mov ebx, 60
        xor ecx, ecx
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, lbl_updated
        int 0x80
        movzx eax, byte [cur_hours]
        cmp eax, 10
        jge .no_hpad
        push eax
        mov eax, SYS_PUTCHAR
        mov ebx, '0'
        int 0x80
        pop eax
.no_hpad:
        call print_dec
        mov eax, SYS_PUTCHAR
        mov ebx, ':'
        int 0x80
        movzx eax, byte [cur_mins]
        cmp eax, 10
        jge .no_mpad
        push eax
        mov eax, SYS_PUTCHAR
        mov ebx, '0'
        int 0x80
        pop eax
.no_mpad:
        call print_dec

        ; === Separator line (row 1, dark gray) ===
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, 1
        int 0x80
        mov edi, SCREEN_W
.fill_sep:
        mov eax, SYS_PUTCHAR
        mov ebx, 0xC4
        int 0x80
        dec edi
        jnz .fill_sep

        ; === Weather body (starting row 3) ===
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, 3
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, body_buf
        int 0x80

        ; === Source credit (row 13, dark gray) ===
        mov eax, SYS_SETCURSOR
        mov ebx, 3
        mov ecx, 13
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x08
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, source_str
        int 0x80

        ; === Status bar (row 24, gray background) ===
        mov eax, SYS_SETCOLOR
        mov ebx, 0x70
        int 0x80
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, 24
        int 0x80
        mov edi, SCREEN_W
.fill_st:
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        dec edi
        jnz .fill_st
        mov eax, SYS_SETCURSOR
        mov ebx, 2
        mov ecx, 24
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, status_str
        int 0x80

        ; === Weather animation overlay ===
        call animate_weather

        popad
        ret

;=======================================================================
; animate_weather - Scatter rain/snow particles in content area
;=======================================================================
animate_weather:
        pushad
        cmp dword [weather_type], W_RAIN
        je .anim_rain
        cmp dword [weather_type], W_SNOW
        je .anim_snow
        jmp .anim_done

.anim_rain:
        mov ecx, 12
.rain_loop:
        push ecx
        call rand
        xor edx, edx
        mov ecx, 76
        div ecx
        add edx, 2
        mov ebx, edx            ; col
        call rand
        xor edx, edx
        mov ecx, 18
        div ecx
        add edx, 3              ; rows 3-20
        mov ecx, edx
        cmp ecx, 23
        jge .rain_skip
        imul ecx, SCREEN_W
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        mov byte [ecx], '|'
        mov byte [ecx + 1], 0x09
.rain_skip:
        pop ecx
        dec ecx
        jnz .rain_loop
        jmp .anim_done

.anim_snow:
        mov ecx, 8
.snow_loop:
        push ecx
        call rand
        xor edx, edx
        mov ecx, 76
        div ecx
        add edx, 2
        mov ebx, edx            ; col
        call rand
        xor edx, edx
        mov ecx, 18
        div ecx
        add edx, 3              ; rows 3-20
        mov ecx, edx
        cmp ecx, 23
        jge .snow_skip
        imul ecx, SCREEN_W
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        mov byte [ecx], '*'
        mov byte [ecx + 1], 0x0F
.snow_skip:
        pop ecx
        dec ecx
        jnz .snow_loop

.anim_done:
        popad
        ret

;=======================================================================
; rand - LCG PRNG, returns EAX = 0..32767
;=======================================================================
rand:
        mov eax, [rand_state]
        imul eax, 1103515245
        add eax, 12345
        mov [rand_state], eax
        shr eax, 16
        and eax, 0x7FFF
        ret

;=======================================================================
; DATA
;=======================================================================
api_host:       db "wttr.in", 0
wttr_params:    db "?0TFdu", 0

title_str:      db "Mellivora Weather ", 0xC4, 0xC4, 0xC4, " Live Data", 0
lbl_updated:    db "Updated ", 0
source_str:     db "Powered by wttr.in", 0
status_str:     db "R = Refresh  ", 0xB3, "  Q = Quit  ", 0xB3, "  Data: wttr.in", 0

msg_fetch:      db "  Fetching weather data from wttr.in...", 10, 0
msg_retry:      db 10, "  Press R to retry, Q to quit.", 10, 0

err_dns:        db "  Error: Could not resolve wttr.in", 10
                db "  Check your network connection.", 10
                db "  Run 'dhcp' to obtain an IP address.", 10, 0
err_conn:       db "  Error: Could not connect to wttr.in.", 10, 0
err_nodata:     db "  Error: No weather data received.", 10, 0

; Variables
cur_hours:      db 0
cur_mins:       db 0
rand_state:     dd 0
server_ip:      dd 0
body_len:       dd 0
weather_type:   dd 0
frame:          dd 0

; Buffers
city_buf:       times 128 db 0
path_buf:       times 256 db 0
http_req_buf:   times 512 db 0
http_resp_buf:  times 65536 db 0
body_buf:       times 4096 db 0
