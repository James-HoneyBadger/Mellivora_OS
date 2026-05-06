; mandelbrot.asm — VBE Mandelbrot Set renderer for Mellivora OS
;
; 640×480×32bpp using direct shadow-buffer pixel writes (proven pattern).
; Fixed-point 16.16 arithmetic.  Rainbow colour-escape map, black interior.
; Renders progressively (flush every 32 rows).  Press any key to exit.

%include "syscalls.inc"

SCR_W    equ 640
SCR_H    equ 480
PITCH    equ SCR_W * 4          ; bytes per row = 2560

MAX_ITER equ 128                ; iteration depth (higher = more detail)
FP_FOUR  equ 262144             ; 4.0 in 16.16 fixed-point (escape radius²)

; Viewport in 16.16 fixed-point:  x ∈ [-2.5, 1.0],  y ∈ [-1.2, 1.2]
X_MIN    equ -163840            ; -2.5 × 65536
Y_MIN    equ -78643             ; -1.2 × 65536 (≈ -78643.2)
X_STEP   equ 358                ; 3.5/640 × 65536  ≈ 358.4
Y_STEP   equ 328                ; 2.4/480 × 65536  ≈ 327.7

start:
        ; ---- set VBE mode 640×480×32bpp ----
        mov eax, SYS_FRAMEBUF
        mov ebx, 1
        mov ecx, SCR_W
        mov edx, SCR_H
        mov esi, 32
        int 0x80
        cmp eax, -1
        je  .novbe

        ; ---- get shadow-buffer address ----
        mov eax, SYS_FRAMEBUF
        xor ebx, ebx
        int 0x80
        mov [fb], eax

        ; ---- render ----
        mov dword [py], 0
        mov dword [fy], Y_MIN

.row:
        cmp dword [py], SCR_H
        jge .render_done

        mov dword [px], 0
        mov dword [fx], X_MIN

.pixel:
        cmp dword [px], SCR_W
        jge .next_row

        ; ---- Mandelbrot iterate: c = (fx, fy),  z₀ = 0+0i ----
        mov dword [zx], 0
        mov dword [zy], 0
        xor ecx, ecx            ; ECX = iteration counter

.iter:
        cmp ecx, MAX_ITER
        jge .in_set

        ; zx² (16.16 multiply: EDX:EAX = EAX×[mem], then shift right 16)
        mov eax, [zx]
        imul dword [zx]
        shrd eax, edx, 16
        mov [zx2], eax

        ; zy²
        mov eax, [zy]
        imul dword [zy]
        shrd eax, edx, 16
        mov [zy2], eax

        ; escape test: |z|² = zx² + zy² > 4
        mov eax, [zx2]
        add eax, [zy2]
        cmp eax, FP_FOUR
        jg  .escaped

        ; new_zy = 2·zx·zy + fy
        mov eax, [zx]
        imul dword [zy]
        shrd eax, edx, 16
        shl eax, 1              ; × 2
        add eax, [fy]
        mov [zy], eax

        ; new_zx = zx² − zy² + fx
        mov eax, [zx2]
        sub eax, [zy2]
        add eax, [fx]
        mov [zx], eax

        inc ecx
        jmp .iter

.in_set:
        xor eax, eax            ; interior → black
        jmp .plot

.escaped:
        ; map iteration count → hue → RGB
        ; hue = (iter × 2) & 0xFF  →  cycles rainbow over 128 iterations
        push ecx                ; hue_to_rgb clobbers ECX/EDX/EBX
        mov eax, ecx
        shl eax, 1
        and eax, 0xFF
        call hue_to_rgb         ; EAX → 0x00RRGGBB
        pop ecx

.plot:
        ; write pixel: shadow[py × PITCH + px × 4] = colour
        mov ebx, [py]
        imul ebx, ebx, PITCH
        add ebx, [fb]
        mov edx, [px]
        shl edx, 2
        mov [ebx + edx], eax

        inc dword [px]
        add dword [fx], X_STEP
        jmp .pixel

.next_row:
        inc dword [py]
        add dword [fy], Y_STEP

        ; progressive display: flush shadow → LFB every 32 rows
        mov eax, [py]
        test eax, 31
        jnz .row
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80
        jmp .row

.render_done:
        ; final blit
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80

        ; title overlay
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 155
        mov edx, 8
        mov esi, title_str
        mov edi, 0x00FFFFFF
        int 0x80

        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 196
        mov edx, 20
        mov esi, sub_str
        mov edi, 0x00FFFF44
        int 0x80

        ; flip with overlay
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80

        ; wait for any keypress
.wait:
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jz  .wait

        ; restore text mode
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80
        jmp .exit

.novbe:
        mov eax, SYS_PRINT
        mov ebx, msg_novbe
        int 0x80

.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================================================
; hue_to_rgb  —  EAX = hue (0-255)  →  EAX = 0x00RRGGBB
; Uses ECX, EDX, EBX as scratch.  Caller must preserve if needed.
;=======================================================================
hue_to_rgb:
        cmp eax, 85
        jl  .sec0
        cmp eax, 170
        jl  .sec1

        ; Sector 2 (170..254): R rises, G falls, B=0
        sub eax, 170
        imul ecx, eax, 3
        and ecx, 0xFF           ; R (rising)
        imul edx, eax, 3
        mov ebx, 255
        sub ebx, edx
        and ebx, 0xFF           ; G (falling)
        shl ecx, 16
        shl ebx, 8
        mov eax, ecx
        or  eax, ebx
        ret

.sec0:  ; Sector 0 (0..84): R falls, B rises, G=0
        imul ecx, eax, 3
        and ecx, 0xFF           ; B (rising)
        imul edx, eax, 3
        mov eax, 255
        sub eax, edx
        and eax, 0xFF           ; R (falling)
        shl eax, 16
        or  eax, ecx
        ret

.sec1:  ; Sector 1 (85..169): G rises, B falls, R=0
        sub eax, 85
        imul ecx, eax, 3
        and ecx, 0xFF           ; G (rising)
        imul edx, eax, 3
        mov eax, 255
        sub eax, edx
        and eax, 0xFF           ; B (falling)
        shl ecx, 8
        or  eax, ecx
        ret

;=======================================================================
; Data
;=======================================================================
fb:   dd 0
px:   dd 0
py:   dd 0
fx:   dd 0
fy:   dd 0
zx:   dd 0
zy:   dd 0
zx2:  dd 0
zy2:  dd 0

title_str: db "MELLIVORA OS -- MANDELBROT SET", 0
sub_str:   db "press any key to exit", 0
msg_novbe: db "mandelbrot: VBE not available", 0x0A, 0
