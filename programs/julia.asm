; julia.asm — Interactive Julia Set renderer for Mellivora OS
;
; Renders the Julia set for z → z² + c  (c is the Julia parameter).
; Uses fixed-point 16.16 arithmetic.  Renders 320×240 pixels each written
; as a 2×2 block to fill 640×480.
;
; Controls:
;   Arrow keys  — move Julia parameter c (re/im) by 0.02
;   +/-         — zoom in/out
;   R           — reset to default view
;   Any other key — exit

%include "syscalls.inc"

SCR_W    equ 640
SCR_H    equ 480
PITCH    equ SCR_W * 4

; Render grid (half resolution, each pixel → 2×2 block)
GRID_W   equ 320
GRID_H   equ 240

MAX_ITER equ 64
FP_FOUR  equ 262144             ; 4.0 in 16.16

; Viewport step defaults (will be recomputed each frame)
; View: x ∈ [-2.0, 2.0], y ∈ [-1.5, 1.5]
X_RANGE  equ 262144             ; 4.0 * 65536
Y_RANGE  equ 196608             ; 3.0 * 65536
X_STEP   equ X_RANGE / GRID_W   ; 819  (≈4.0/320 * 65536)
Y_STEP   equ Y_RANGE / GRID_H   ; 819  (≈3.0/240 * 65536)
X_ORIGIN equ -131072            ; -2.0 * 65536
Y_ORIGIN equ  -98304            ; -1.5 * 65536

; Default Julia seed c = -0.7 + 0.27i  (a classic beautiful Julia set)
C_RE_DEF equ -45875             ; -0.7 * 65536
C_IM_DEF equ  17695             ;  0.27 * 65536
DELTA    equ   1311             ;  0.02 * 65536  (arrow key step)

start:
        ; ---- VBE 640×480×32 ----
        mov eax, SYS_FRAMEBUF
        mov ebx, 1
        mov ecx, SCR_W
        mov edx, SCR_H
        mov esi, 32
        int 0x80
        cmp eax, -1
        je  .novbe

        ; ---- get shadow buffer ----
        mov eax, SYS_FRAMEBUF
        xor ebx, ebx
        int 0x80
        mov [fb], eax

        ; ---- init parameter ----
        mov dword [c_re], C_RE_DEF
        mov dword [c_im], C_IM_DEF

.render:
        ; ---- render entire grid ----
        mov dword [gy], 0
        mov dword [fy], Y_ORIGIN

.row:
        cmp dword [gy], GRID_H
        jge .render_done

        mov dword [gx], 0
        mov dword [fx], X_ORIGIN

.pixel:
        cmp dword [gx], GRID_W
        jge .next_row

        ; Julia iterate: z₀ = (fx, fy), c = (c_re, c_im)
        mov eax, [fx]
        mov [zx], eax
        mov eax, [fy]
        mov [zy], eax
        xor ecx, ecx            ; iteration counter

.iter:
        cmp ecx, MAX_ITER
        jge .in_set

        ; zx²
        mov eax, [zx]
        imul dword [zx]
        shrd eax, edx, 16
        mov [zx2], eax

        ; zy²
        mov eax, [zy]
        imul dword [zy]
        shrd eax, edx, 16
        mov [zy2], eax

        ; escape test: |z|² > 4
        mov eax, [zx2]
        add eax, [zy2]
        cmp eax, FP_FOUR
        jg  .escaped

        ; new_zy = 2·zx·zy + c_im
        mov eax, [zx]
        imul dword [zy]
        shrd eax, edx, 16
        shl eax, 1
        add eax, [c_im]
        mov [zy], eax

        ; new_zx = zx² − zy² + c_re
        mov eax, [zx2]
        sub eax, [zy2]
        add eax, [c_re]
        mov [zx], eax

        inc ecx
        jmp .iter

.in_set:
        xor eax, eax            ; interior = black
        jmp .plot

.escaped:
        ; hue from iteration count
        push ecx
        mov eax, ecx
        shl eax, 2              ; × 4 for wider colour range
        and eax, 0xFF
        call hue_to_rgb
        pop ecx

.plot:
        ; write 2×2 block at (gx*2, gy*2)
        mov ebx, [gy]
        shl ebx, 1              ; row = gy * 2
        imul ebx, ebx, PITCH
        add ebx, [fb]
        mov edx, [gx]
        shl edx, 3              ; col offset = gx * 2 * 4
        add ebx, edx

        ; top-left
        mov [ebx], eax
        ; top-right
        mov [ebx + 4], eax
        ; bottom-left
        mov [ebx + PITCH], eax
        ; bottom-right
        mov [ebx + PITCH + 4], eax

        inc dword [gx]
        add dword [fx], X_STEP
        jmp .pixel

.next_row:
        inc dword [gy]
        add dword [fy], Y_STEP

        ; Flush every 16 rows for progressive display
        mov eax, [gy]
        test eax, 15
        jnz .row
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80
        jmp .row

.render_done:
        ; Title overlay
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 130
        mov edx, 8
        mov esi, title_str
        mov edi, 0x00FFFFFF
        int 0x80
        ; Param line
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 130
        mov edx, 20
        mov esi, key_str
        mov edi, 0x00FFFF44
        int 0x80
        ; Final blit
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80

.wait:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz  .wait

        ; Arrow keys
        cmp al, 0x82            ; Left  → c_re -= DELTA
        jne .not_left
        sub dword [c_re], DELTA
        jmp .render
.not_left:
        cmp al, 0x83            ; Right → c_re += DELTA
        jne .not_right
        add dword [c_re], DELTA
        jmp .render
.not_right:
        cmp al, 0x80            ; Up    → c_im -= DELTA
        jne .not_up
        sub dword [c_im], DELTA
        jmp .render
.not_up:
        cmp al, 0x81            ; Down  → c_im += DELTA
        jne .not_down
        add dword [c_im], DELTA
        jmp .render
.not_down:
        cmp al, 'r'
        je  .reset
        cmp al, 'R'
        jne .exit
.reset:
        mov dword [c_re], C_RE_DEF
        mov dword [c_im], C_IM_DEF
        jmp .render

.exit:
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80
        jmp .done

.novbe:
        mov eax, SYS_PRINT
        mov ebx, msg_novbe
        int 0x80
.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================================================
; hue_to_rgb  EAX=hue(0-255) → EAX=0x00RRGGBB
;=======================================================================
hue_to_rgb:
        cmp eax, 85
        jl  .sec0
        cmp eax, 170
        jl  .sec1
        sub eax, 170
        imul ecx, eax, 3
        and ecx, 0xFF
        imul edx, eax, 3
        mov ebx, 255
        sub ebx, edx
        and ebx, 0xFF
        shl ecx, 16
        shl ebx, 8
        mov eax, ecx
        or  eax, ebx
        ret
.sec0:
        imul ecx, eax, 3
        and ecx, 0xFF
        imul edx, eax, 3
        mov eax, 255
        sub eax, edx
        and eax, 0xFF
        shl eax, 16
        or  eax, ecx
        ret
.sec1:
        sub eax, 85
        imul ecx, eax, 3
        and ecx, 0xFF
        imul edx, eax, 3
        mov eax, 255
        sub eax, edx
        and eax, 0xFF
        shl ecx, 8
        or  eax, ecx
        ret

;=======================================================================
fb:    dd 0
c_re:  dd 0
c_im:  dd 0
gx:    dd 0
gy:    dd 0
fx:    dd 0
fy:    dd 0
zx:    dd 0
zy:    dd 0
zx2:   dd 0
zy2:   dd 0

title_str: db "JULIA SET  (arrow keys: move seed  r: reset  other: exit)", 0
key_str:   db "c = arrow keys to explore the parameter space", 0
msg_novbe: db "julia: VBE not available", 0x0A, 0
