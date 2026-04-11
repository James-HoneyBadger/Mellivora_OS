; starfield.asm - 3D Starfield Screensaver for Mellivora OS
; Classic flying-through-space effect with perspective projection.
; Press any key to exit.
%include "syscalls.inc"

NUM_STARS       equ 80
SCREEN_W        equ 80
SCREEN_H        equ 25
CENTER_X        equ 40
CENTER_Y        equ 12
SPEED           equ 3           ; Z decrease per frame
TICK_DELAY      equ 3           ; ~33fps
MAX_Z           equ 200
MIN_Z           equ 1

start:
        ; Seed random
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_state], eax

        ; Initialize stars
        xor esi, esi
.init_loop:
        cmp esi, NUM_STARS
        jge .init_done
        call init_star
        ; Also randomize initial Z for spread
        call rand
        xor edx, edx
        mov ecx, MAX_Z
        div ecx
        inc edx
        mov [star_z + esi*4], edx
        inc esi
        jmp .init_loop
.init_done:

;=== Main loop ===
.main_loop:
        ; Check for keypress
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jnz .exit

        ; Clear VGA buffer
        call clear_screen

        ; Update and draw each star
        xor esi, esi
.star_loop:
        cmp esi, NUM_STARS
        jge .star_frame_done

        ; Move star closer (decrease Z)
        mov eax, [star_z + esi*4]
        sub eax, SPEED
        cmp eax, MIN_Z
        jg .star_alive
        ; Respawn
        call init_star
        jmp .star_next

.star_alive:
        mov [star_z + esi*4], eax

        ; Project: screen_x = CENTER_X + (star_x * 128) / star_z
        mov eax, [star_x + esi*4]
        imul eax, 128
        cdq
        mov ecx, [star_z + esi*4]
        test ecx, ecx
        jz .star_next
        idiv ecx
        add eax, CENTER_X
        mov ebx, eax            ; ebx = screen_x

        ; Project: screen_y = CENTER_Y + (star_y * 64) / star_z
        mov eax, [star_y + esi*4]
        imul eax, 64
        cdq
        mov ecx, [star_z + esi*4]
        test ecx, ecx
        jz .star_next
        idiv ecx
        add eax, CENTER_Y
        mov edx, eax            ; edx = screen_y

        ; Bounds check
        cmp ebx, 0
        jl .star_oob
        cmp ebx, SCREEN_W
        jge .star_oob
        cmp edx, 0
        jl .star_oob
        cmp edx, SCREEN_H
        jge .star_oob

        ; Choose star character/color based on Z distance
        mov ecx, [star_z + esi*4]
        cmp ecx, 150
        jg .star_far
        cmp ecx, 80
        jg .star_mid
        cmp ecx, 30
        jg .star_near
        ; Very close
        mov al, 0xDB            ; Full block
        mov ah, 0x0F            ; Bright white
        jmp .star_draw
.star_near:
        mov al, '*'
        mov ah, 0x0F
        jmp .star_draw
.star_mid:
        mov al, '+'
        mov ah, 0x07            ; Gray
        jmp .star_draw
.star_far:
        mov al, 0xFA            ; Dot
        mov ah, 0x08            ; Dark gray

.star_draw:
        ; Write to VGA at (ebx, edx)
        push esi
        mov ecx, edx
        imul ecx, SCREEN_W
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        mov [ecx], ax
        pop esi
        jmp .star_next

.star_oob:
        ; Star went off-screen, respawn
        call init_star

.star_next:
        inc esi
        jmp .star_loop

.star_frame_done:
        ; Frame delay
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
; init_star - Initialize star[esi] with random position
;---------------------------------------
init_star:
        push eax
        push ecx
        push edx

        ; Random X: -100 to +100
        call rand
        xor edx, edx
        mov ecx, 201
        div ecx
        sub edx, 100
        mov [star_x + esi*4], edx

        ; Random Y: -60 to +60
        call rand
        xor edx, edx
        mov ecx, 121
        div ecx
        sub edx, 60
        mov [star_y + esi*4], edx

        ; Z starts at max
        mov dword [star_z + esi*4], MAX_Z

        pop edx
        pop ecx
        pop eax
        ret

;---------------------------------------
; clear_screen - Fill VGA buffer with black spaces
;---------------------------------------
clear_screen:
        pushad
        mov edi, VGA_BASE
        mov eax, 0x00200020     ; Two black spaces
        mov ecx, SCREEN_W * SCREEN_H / 2
        rep stosd
        popad
        ret

;---------------------------------------
; rand - LCG PRNG -> EAX
;---------------------------------------
rand:
        mov eax, [rand_state]
        imul eax, 1103515245
        add eax, 12345
        mov [rand_state], eax
        shr eax, 16
        and eax, 0x7FFF
        ret

; === Data ===
rand_state:     dd 0
star_x:         times NUM_STARS dd 0
star_y:         times NUM_STARS dd 0
star_z:         times NUM_STARS dd 0
