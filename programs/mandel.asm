; mandel.asm - Mandelbrot set renderer for Mellivora OS
; Uses fixed-point arithmetic (16.16)
%include "syscalls.inc"

; Fixed-point scale: 1.0 = 65536
FP_ONE  equ 65536
FP_TWO  equ 131072
FP_FOUR equ 262144

; Screen dimensions
SCR_W   equ 78
SCR_H   equ 23

; Iteration limit
MAX_ITER equ 32

; Viewport: x in [-2.5, 1.0], y in [-1.1, 1.1]
; x_min = -2.5 * 65536 = -163840
; x_max = 1.0 * 65536 = 65536
; y_min = -1.1 * 65536 = -72090
; y_max = 1.1 * 65536 = 72090

X_MIN   equ -163840
X_MAX   equ 65536
Y_MIN   equ -72090
Y_MAX   equ 72090

start:
        mov eax, SYS_CLEAR
        int 0x80

        ; Compute x_step = (X_MAX - X_MIN) / SCR_W
        mov eax, X_MAX - X_MIN
        cdq
        mov ebx, SCR_W
        idiv ebx
        mov [x_step], eax

        ; Compute y_step = (Y_MAX - Y_MIN) / SCR_H
        mov eax, Y_MAX - Y_MIN
        cdq
        mov ebx, SCR_H
        idiv ebx
        mov [y_step], eax

        mov dword [c_im], Y_MIN
        xor esi, esi            ; row counter

.row_loop:
        cmp esi, SCR_H
        jge .done

        mov dword [c_re], X_MIN
        xor edi, edi            ; col counter

.col_loop:
        cmp edi, SCR_W
        jge .next_row

        ; Iterate z = z^2 + c
        mov dword [zx], 0
        mov dword [zy], 0
        xor ecx, ecx           ; iteration count

.iter_loop:
        cmp ecx, MAX_ITER
        jge .iter_done

        ; zx^2, zy^2
        mov eax, [zx]
        imul dword [zx]         ; edx:eax = zx*zx (32.32)
        shrd eax, edx, 16       ; eax = zx^2 in 16.16
        mov [zx2], eax

        mov eax, [zy]
        imul dword [zy]
        shrd eax, edx, 16
        mov [zy2], eax

        ; Check |z|^2 > 4
        mov eax, [zx2]
        add eax, [zy2]
        cmp eax, FP_FOUR
        jg .iter_done

        ; zy = 2*zx*zy + cy
        mov eax, [zx]
        imul dword [zy]
        shrd eax, edx, 16
        shl eax, 1
        add eax, [c_im]
        mov [zy], eax

        ; zx = zx^2 - zy^2 + c_re
        mov eax, [zx2]
        sub eax, [zy2]
        add eax, [c_re]
        mov [zx], eax

        inc ecx
        jmp .iter_loop

.iter_done:
        ; Map iteration count to character
        cmp ecx, MAX_ITER
        je .in_set
        ; Use a gradient
        mov eax, ecx
        and eax, 0x0F
        movzx ebx, byte [gradient + eax]
        jmp .put_char

.in_set:
        mov ebx, ' '

.put_char:
        ; Set color based on iteration
        push ecx
        push edi
        cmp ecx, MAX_ITER
        je .color_black
        ; Map iterations to colors
        mov eax, ecx
        shr eax, 2
        and eax, 7
        movzx eax, byte [color_table + eax]
        mov ebx, eax
        jmp .set_color
.color_black:
        mov ebx, 0x00           ; Black on black
.set_color:
        mov eax, SYS_SETCOLOR
        int 0x80
        pop edi
        pop ecx

        ; Put the character
        cmp ecx, MAX_ITER
        je .in_set2
        mov eax, ecx
        and eax, 0x0F
        movzx ebx, byte [gradient + eax]
        jmp .put2
.in_set2:
        mov ebx, ' '
.put2:
        mov eax, SYS_PUTCHAR
        int 0x80

        ; Advance c_re
        mov eax, [x_step]
        add [c_re], eax
        inc edi
        jmp .col_loop

.next_row:
        ; Newline
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        ; Advance c_im
        mov eax, [y_step]
        add [c_im], eax
        inc esi
        jmp .row_loop

.done:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0E
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_done
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80

        mov eax, SYS_GETCHAR
        int 0x80

        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

;---------------------------------------
; Data
;---------------------------------------
msg_done:   db "Mandelbrot set (press any key)", 0x0A, 0

; Character gradient for iteration depth
gradient:   db '.', ',', ':', ';', '+', '=', 'x', 'X'
            db '#', '%', '@', '8', '&', 'W', 'M', '0'

; Color table for iteration bands
color_table: db 0x01, 0x09, 0x03, 0x0B, 0x02, 0x0A, 0x0E, 0x0F

;---------------------------------------
; Variables
;---------------------------------------
c_re:     dd 0
c_im:     dd 0
zx:       dd 0
zy:       dd 0
zx2:      dd 0
zy2:      dd 0
x_step:   dd 0
y_step:   dd 0
