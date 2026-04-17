; matrix.asm - The Matrix Digital Rain for Mellivora OS
; Cascading green characters just like the movie.
; Press any key to exit.
%include "syscalls.inc"

SCREEN_W        equ 80
SCREEN_H        equ 25
TICK_DELAY      equ 4           ; ~25fps
NUM_DROPS       equ 40          ; Number of active rain columns

start:
        ; Seed random
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_state], eax

        ; Clear screen to black
        call clear_black

        ; Initialize drops
        xor esi, esi
.init_drops:
        cmp esi, NUM_DROPS
        jge .main_loop

        ; Random column
        call rand
        xor edx, edx
        mov ecx, SCREEN_W
        div ecx
        mov [drop_col + esi*4], edx

        ; Random starting row (negative = delay before visible)
        call rand
        xor edx, edx
        mov ecx, 40
        div ecx
        sub edx, 15
        mov [drop_row + esi*4], edx

        ; Random length 5-20
        call rand
        xor edx, edx
        mov ecx, 16
        div ecx
        add edx, 5
        mov [drop_len + esi*4], edx

        ; Random speed 1-3
        call rand
        xor edx, edx
        mov ecx, 3
        div ecx
        inc edx
        mov [drop_spd + esi*4], edx

        inc esi
        jmp .init_drops

;=== Main loop ===
.main_loop:
        ; Check for keypress
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jnz .exit

        ; Update each drop
        xor esi, esi
.drop_loop:
        cmp esi, NUM_DROPS
        jge .frame_done

        ; Advance drop position by speed
        mov eax, [drop_spd + esi*4]
        add [drop_row + esi*4], eax

        mov ecx, [drop_row + esi*4]    ; head row
        mov ebx, [drop_col + esi*4]    ; column

        ; Draw the head (bright white/green)
        cmp ecx, 0
        jl .drop_tail
        cmp ecx, SCREEN_H
        jge .drop_tail

        ; Random character for head
        call rand
        xor edx, edx
        push rcx
        mov ecx, 94
        div ecx
        pop rcx
        add edx, 33             ; printable ASCII 33-126
        mov al, dl
        mov ah, 0x0F            ; Bright white (the leading char)
        call vga_write

.drop_tail:
        ; Make previous positions green, then dark green, then erase
        mov ecx, [drop_row + esi*4]
        dec ecx                 ; one behind head

        ; Bright green trail (3 chars behind)
        push rsi
        mov esi, 3
.trail_green:
        cmp ecx, 0
        jl .trail_skip
        cmp ecx, SCREEN_H
        jge .trail_skip

        ; Random char for shimmer effect
        call rand
        xor edx, edx
        push rcx
        mov ecx, 94
        div ecx
        pop rcx
        add edx, 33
        mov al, dl
        mov ah, 0x0A            ; Bright green
        call vga_write

.trail_skip:
        dec ecx
        dec esi
        jnz .trail_green
        pop rsi

        ; Dark green chars (next chunk)
        push rsi
        mov esi, [drop_len + esi*4]
        sub esi, 3
        cmp esi, 1
        jl .trail2_done
.trail_dark:
        cmp ecx, 0
        jl .trail2_skip
        cmp ecx, SCREEN_H
        jge .trail2_skip

        ; 50% chance to change char for flicker
        push rdx
        call rand
        test eax, 1
        pop rdx
        jz .trail2_keep

        call rand
        xor edx, edx
        push rcx
        mov ecx, 94
        div ecx
        pop rcx
        add edx, 33
        mov al, dl
        mov ah, 0x02            ; Dark green
        call vga_write
        jmp .trail2_skip

.trail2_keep:
        ; Just recolor existing char to dark green
        push rdx
        push rcx
        imul ecx, SCREEN_W
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        mov byte [ecx + 1], 0x02  ; Just change attribute
        pop rcx
        pop rdx

.trail2_skip:
        dec ecx
        dec esi
        jnz .trail_dark
.trail2_done:
        pop rsi

        ; Erase the very tail
        mov ecx, [drop_row + esi*4]
        sub ecx, [drop_len + esi*4]
        cmp ecx, 0
        jl .drop_check_reset
        cmp ecx, SCREEN_H
        jge .drop_check_reset
        mov al, ' '
        mov ah, 0x00            ; Black
        call vga_write

.drop_check_reset:
        ; Reset if fully off screen
        mov ecx, [drop_row + esi*4]
        sub ecx, [drop_len + esi*4]
        cmp ecx, SCREEN_H
        jl .drop_next

        ; Respawn
        call rand
        xor edx, edx
        mov ecx, SCREEN_W
        div ecx
        mov [drop_col + esi*4], edx

        call rand
        xor edx, edx
        mov ecx, 20
        div ecx
        neg edx
        mov [drop_row + esi*4], edx

        call rand
        xor edx, edx
        mov ecx, 16
        div ecx
        add edx, 5
        mov [drop_len + esi*4], edx

        call rand
        xor edx, edx
        mov ecx, 3
        div ecx
        inc edx
        mov [drop_spd + esi*4], edx

.drop_next:
        inc esi
        jmp .drop_loop

.frame_done:
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
; vga_write: write char AL with attr AH at (EBX=col, ECX=row)
;---------------------------------------
vga_write:
        push rcx
        push rdx
        imul ecx, SCREEN_W
        add ecx, ebx
        shl ecx, 1
        add ecx, VGA_BASE
        mov [ecx], ax
        pop rdx
        pop rcx
        ret

;---------------------------------------
; clear_black: fill screen with black spaces
;---------------------------------------
clear_black:
        PUSHALL
        mov edi, VGA_BASE
        mov eax, 0x00200020
        mov ecx, SCREEN_W * SCREEN_H / 2
        rep stosd
        POPALL
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

; === Data ===
rand_state:     dd 0
drop_col:       times NUM_DROPS dd 0
drop_row:       times NUM_DROPS dd 0
drop_len:       times NUM_DROPS dd 0
drop_spd:       times NUM_DROPS dd 0
