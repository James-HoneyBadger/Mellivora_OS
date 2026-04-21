; lunar.asm - Lunar Lander simulation
; Land the module safely: touchdown speed <= 5 m/s, fuel >= 0

%include "syscalls.inc"

; Physics constants (fixed-point, scaled by 1000)
GRAVITY_FP      equ 1620    ; Moon gravity 1.62 m/s², *1000
THRUST_FP       equ 10000   ; 10 m/s² thrust per unit
DT_FP           equ 1000    ; 1 second time step, *1000
SAFE_SPEED      equ 5       ; m/s max landing speed

start:
        call init_state
        call print_banner
.game_loop:
        call print_status
        ; Check altitude <= 0 → landed
        cmp dword [altitude], 0
        jle .landed
        call get_input
        call update_physics
        jmp .game_loop

.landed:
        call print_landing
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

init_state:
        mov dword [altitude], 10000     ; 10000 m
        mov dword [velocity_fp], 0      ; positive = downward (m/s * 1000)
        mov dword [fuel], 5000          ; 5000 units
        mov dword [thrust], 0
        mov dword [time_elapsed], 0
        ret

print_banner:
        mov eax, SYS_PRINT
        mov ebx, msg_banner
        int 0x80
        ret

print_status:
        mov eax, SYS_PRINT
        mov ebx, msg_status1
        int 0x80

        ; Altitude
        mov eax, SYS_PRINT
        mov ebx, msg_alt
        int 0x80
        mov eax, [altitude]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_m
        int 0x80

        ; Velocity (velocity_fp / 1000)
        mov eax, SYS_PRINT
        mov ebx, msg_vel
        int 0x80
        mov eax, [velocity_fp]
        ; Can be negative (upward)
        test eax, eax
        jns .v_pos
        mov eax, SYS_PUTCHAR
        mov ebx, '-'
        int 0x80
        mov eax, [velocity_fp]
        neg eax
.v_pos:
        mov ebx, 1000
        xor edx, edx
        div ebx
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_ms
        int 0x80

        ; Fuel
        mov eax, SYS_PRINT
        mov ebx, msg_fuel
        int 0x80
        mov eax, [fuel]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_nl
        int 0x80
        ret

get_input:
        mov eax, SYS_PRINT
        mov ebx, msg_ask_thrust
        int 0x80
        mov eax, SYS_STDIN_READ
        mov ebx, input_buf
        mov ecx, 10
        int 0x80

        ; Parse thrust value
        xor eax, eax
        mov esi, input_buf
.parse:
        mov bl, [esi]
        cmp bl, '0'
        jb .p_done
        cmp bl, '9'
        ja .p_done
        sub bl, '0'
        imul eax, 10
        movzx ebx, bl
        add eax, ebx
        inc esi
        jmp .parse
.p_done:
        ; Clamp thrust to available fuel
        cmp eax, [fuel]
        jle .t_ok
        mov eax, [fuel]
.t_ok:
        mov [thrust], eax
        ret

update_physics:
        ; velocity_fp += GRAVITY_FP (downward = positive)
        ; velocity_fp -= thrust * THRUST_FP / 1000 (upward thrust)
        mov eax, GRAVITY_FP
        add [velocity_fp], eax

        mov eax, [thrust]
        imul eax, THRUST_FP
        mov ebx, 1000
        xor edx, edx
        div ebx
        sub [velocity_fp], eax

        ; Subtract fuel used
        mov eax, [thrust]
        sub [fuel], eax
        jns .fuel_ok
        mov dword [fuel], 0
.fuel_ok:

        ; altitude -= velocity_fp / 1000 (positive velocity = descending)
        mov eax, [velocity_fp]
        mov ebx, 1000
        xor edx, edx
        ; Handle signed division
        test eax, eax
        jns .d_pos
        neg eax
        xor edx, edx
        div ebx
        neg eax
        jmp .d_done
.d_pos:
        xor edx, edx
        div ebx
.d_done:
        sub [altitude], eax

        inc dword [time_elapsed]
        ret

print_landing:
        mov eax, SYS_PRINT
        mov ebx, msg_touchdown
        int 0x80

        ; Final speed = |velocity_fp| / 1000
        mov eax, [velocity_fp]
        test eax, eax
        jns .lp_pos
        neg eax
.lp_pos:
        mov ebx, 1000
        xor edx, edx
        div ebx
        mov [final_speed], eax

        mov eax, SYS_PRINT
        mov ebx, msg_speed
        int 0x80
        mov eax, [final_speed]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_ms
        int 0x80

        cmp dword [final_speed], SAFE_SPEED
        jg .crash

        mov eax, SYS_PRINT
        mov ebx, msg_success
        int 0x80
        ret

.crash:
        mov eax, SYS_PRINT
        mov ebx, msg_crash
        int 0x80
        ret


msg_banner:     db "=== LUNAR LANDER ===", 10
                db "Land safely! Max touchdown speed: 5 m/s", 10
                db "Enter thrust each second (0 = free fall).", 10, 10, 0
msg_status1:    db "----------------------------------------", 10, 0
msg_alt:        db "Altitude:  ", 0
msg_vel:        db "  Velocity: ", 0
msg_fuel:       db "  Fuel: ", 0
msg_m:          db " m", 0
msg_ms:         db " m/s", 10, 0
msg_nl:         db 10, 0
msg_ask_thrust: db "Thrust (0-100): ", 0
msg_touchdown:  db "========== TOUCHDOWN ==========", 10, 0
msg_speed:      db "Impact speed: ", 0
msg_success:    db "SAFE LANDING! Well done!", 10, 0
msg_crash:      db "CRASH! Too fast — mission failed.", 10, 0

altitude:       dd 10000
velocity_fp:    dd 0            ; m/s * 1000, positive = down
fuel:           dd 5000
thrust:         dd 0
time_elapsed:   dd 0
final_speed:    dd 0
input_buf:      times 16 db 0
