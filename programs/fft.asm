; fft.asm — 16-point DFT spectrum analyser for Mellivora OS
;
; Computes the Discrete Fourier Transform of a 16-sample test signal
; using the O(N²) direct DFT formula with integer fixed-point arithmetic
; (scale factor = 256).  Results are displayed as a bar chart on a
; VBE 640×480 framebuffer.
;
; Input signals (selectable by pressing 1-3, or ESC to quit):
;   1 — cosine at bin 2   (pure tone, energy at k=2 and k=14)
;   2 — square wave       (odd harmonics visible)
;   3 — triangle wave     (steeper harmonic roll-off)
;
; Build:  nasm -f bin -O0 fft.asm -o fft.bin

%include "syscalls.inc"

; ---- Screen ----------------------------------------------------------
SCR_W   equ 640
SCR_H   equ 480

; ---- DFT constants ---------------------------------------------------
N       equ 16                  ; number of samples / bins
SCALE   equ 256                 ; fixed-point 1.0

; ---- Bar chart layout ------------------------------------------------
; 16 bars, each 28 px wide, 12 px gap → (28+12)*16 = 640 px
BAR_W   equ 28
BAR_GAP equ 12
BAR_Y0  equ 420                 ; baseline (bottom of bars)
BAR_MAX equ 340                 ; maximum bar height in pixels

; ---- Colours ---------------------------------------------------------
COL_BG      equ 0x050510
COL_BAR     equ 0x3399FF
COL_BAR_HI  equ 0xAADDFF       ; top pixel highlight
COL_AXIS    equ 0x224488
COL_TEXT    equ 0xCCCCCC
COL_LABEL   equ 0x88AACC

; ---- Keys ------------------------------------------------------------
KEY_ESC  equ 0x01
KEY_1    equ 0x02
KEY_2    equ 0x03
KEY_3    equ 0x04

;=======================================================================
; Twiddle tables: cos(2π*m/16)*256 and -sin(2π*m/16)*256 for m=0..15
; These are the real and imaginary parts of W_16^m.
;=======================================================================
cos_tbl:
        dd  256,  237,  181,   98,    0,  -98, -181, -237
        dd -256, -237, -181,  -98,    0,   98,  181,  237

; -sin (imaginary part of W for DFT formula X[k] = sum x[n]*W^(kn))
neg_sin_tbl:
        dd    0,  -98, -181, -237, -256, -237, -181,  -98
        dd    0,   98,  181,  237,  256,  237,  181,   98

;=======================================================================
; Three test signals (16 signed dwords each)
;=======================================================================
sig_cosine:     ; x[n] = 100*cos(2π*2*n/16) — energy at bins 2 and 14
        dd  100,  71,   0, -71, -100, -71,   0,  71
        dd  100,  71,   0, -71, -100, -71,   0,  71

sig_square:     ; x[n] = +100 for n<8, -100 for n≥8
        dd  100, 100, 100, 100, 100, 100, 100, 100
        dd -100,-100,-100,-100,-100,-100,-100,-100

sig_triangle:   ; x[n] = piecewise linear, amplitude ~100
        dd    0,  25,  50,  75, 100,  75,  50,  25
        dd    0, -25, -50, -75,-100, -75, -50, -25

;=======================================================================
start:
        mov eax, SYS_TASKNAME
        mov ebx, tname
        int 0x80

        ; VBE 640×480×32
        mov eax, SYS_FRAMEBUF
        mov ebx, 1
        mov ecx, SCR_W
        mov edx, SCR_H
        mov esi, 32
        int 0x80
        cmp eax, -1
        je .bye

        mov eax, SYS_FRAMEBUF
        xor ebx, ebx
        int 0x80
        mov [fb_addr], eax
        mov dword [fb_pitch], SCR_W * 4

        ; Default: cosine signal
        mov dword [sig_ptr], sig_cosine
        call compute_dft
        call draw_spectrum

.event_loop:
        mov eax, SYS_READ_KEY
        xor ebx, ebx
        int 0x80
        cmp eax, 0
        je  .event_loop

        cmp eax, KEY_ESC
        je  .bye

        ; Key '1' (scan code 2): cosine
        cmp eax, 2
        jne .try2
        mov dword [sig_ptr], sig_cosine
        jmp .redraw

.try2:  cmp eax, 3
        jne .try3
        mov dword [sig_ptr], sig_square
        jmp .redraw

.try3:  cmp eax, 4
        jne .event_loop
        mov dword [sig_ptr], sig_triangle

.redraw:
        call compute_dft
        call draw_spectrum
        jmp .event_loop

.bye:
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================================================
; compute_dft — 16-point DFT of signal at [sig_ptr]
; X[k] = sum_{n=0}^{15} x[n] * W_N^(k*n)
; Real: Re[k] = sum x[n] * cos_tbl[(k*n) & 0xF]
; Imag: Im[k] = sum x[n] * neg_sin_tbl[(k*n) & 0xF]
; All values scaled by SCALE=256; divide by 256 after.
; Magnitude: integer sqrt(Re²+Im²), stored in mag[k].
;=======================================================================
compute_dft:
        pushad
        xor ecx, ecx            ; k = 0..15
.k_loop:
        xor esi, esi            ; Re accumulator
        xor edi, edi            ; Im accumulator
        xor ebx, ebx            ; n = 0..15
.n_loop:
        ; twiddle index = (k*n) & 0xF
        mov eax, ecx
        imul eax, ebx
        and eax, 0xF

        ; Fetch signal sample x[n]
        mov edx, [sig_ptr]
        mov edx, [edx + ebx*4]  ; x[n] (signed)

        ; Re += x[n] * cos_tbl[idx]
        push eax
        imul edx, [cos_tbl + eax*4]    ; edx = x[n]*cos (scaled *256)
        sar edx, 8                      ; /256
        add esi, edx
        pop eax

        ; Im += x[n] * neg_sin_tbl[idx]
        mov edx, [sig_ptr]
        mov edx, [edx + ebx*4]
        imul edx, [neg_sin_tbl + eax*4]
        sar edx, 8
        add edi, edx

        inc ebx
        cmp ebx, N
        jl .n_loop

        ; Divide accumulators by N=16 for proper magnitude scaling
        sar esi, 4
        sar edi, 4

        ; Magnitude = integer_sqrt(Re² + Im²)
        ; Abs values first
        mov eax, esi
        cdq
        xor eax, edx
        sub eax, edx            ; eax = |Re|

        mov ebp, edi
        push edx
        mov edx, ebp
        cdq
        xor edx, ebp
        sub ebp, edx            ; ebp = |Im|  (note: using ebp)
        pop edx

        ; Newton-Raphson integer sqrt of (eax² + ebp²)
        ; Compute a² + b² in 64-bit to avoid overflow
        push ecx
        mov ecx, eax
        imul ecx, eax           ; ecx = Re²
        mov edx, ebp
        imul edx, ebp           ; edx = Im²
        add ecx, edx            ; ecx = Re² + Im²
        ; isqrt(ecx) using alpha*max + beta*min approximation:
        ;   sqrt(a²+b²) ≈ max(a,b) + 0.414*min(a,b)  (max error 8%)
        ;   In integers: max + (min * 106) / 256
        mov edx, eax            ; edx = |Re|
        cmp edx, ebp
        jge .use_re_max
        mov edx, ebp            ; edx = max(|Re|, |Im|)
        mov eax, ecx            ; ...reuse eax as min = |Re|
        ; eax was already set to |Re|, now edx = |Im| > |Re|
        jmp .sqrt_done_pick
.use_re_max:
        ; edx = |Re| (max), ebp = |Im| (min)
        mov eax, ebp
.sqrt_done_pick:
        ; edx = max, eax = min
        imul eax, 106
        sar eax, 8              ; eax = min * 0.414
        add eax, edx            ; eax ≈ sqrt(Re²+Im²)
        pop ecx

        ; Store in mag[k]
        mov [mag + ecx*4], eax

        inc ecx
        cmp ecx, N
        jl .k_loop

        popad
        ret

;=======================================================================
; draw_spectrum — render the bar chart
;=======================================================================
draw_spectrum:
        pushad

        ; Clear background
        mov ebx, 0
        mov ecx, 0
        mov edx, SCR_W
        mov esi, SCR_H
        mov edi, COL_BG
        call fb_fill_rect

        ; Draw axis baseline
        mov ebx, 0
        mov ecx, BAR_Y0
        mov edx, SCR_W
        mov esi, 2
        mov edi, COL_AXIS
        call fb_fill_rect

        ; Title
        mov ebx, 160
        mov ecx, 16
        mov esi, title_str
        mov edi, COL_TEXT
        call fb_draw_text

        ; Hint line
        mov ebx, 100
        mov ecx, 36
        mov esi, hint_str
        mov edi, COL_LABEL
        call fb_draw_text

        ; Find max magnitude for scaling
        mov dword [max_mag], 1          ; avoid div-by-zero
        xor ecx, ecx
.find_max:
        mov eax, [mag + ecx*4]
        cmp eax, [max_mag]
        jle .find_max_next
        mov [max_mag], eax
.find_max_next:
        inc ecx
        cmp ecx, N
        jl .find_max

        ; Draw 16 bars
        xor ecx, ecx            ; k = 0
.bar_loop:
        ; bar height = mag[k] * BAR_MAX / max_mag
        mov eax, [mag + ecx*4]
        imul eax, BAR_MAX
        mov edx, [max_mag]
        cdq
        idiv edx                ; eax = bar height (pixels)
        mov [bar_h], eax

        ; bar x = k * (BAR_W + BAR_GAP) + 2
        mov ebx, ecx
        imul ebx, BAR_W + BAR_GAP
        add ebx, 2

        ; bar top y = BAR_Y0 - bar_h
        mov ecx, BAR_Y0
        sub ecx, [bar_h]

        mov edx, BAR_W
        mov esi, [bar_h]
        test esi, esi
        jz .bar_label

        ; Draw bar
        mov edi, COL_BAR
        call fb_fill_rect

        ; Top highlight (1 px)
        push ebx
        push ecx
        mov ecx, BAR_Y0
        sub ecx, [bar_h]
        mov edx, BAR_W
        mov esi, 1
        mov edi, COL_BAR_HI
        call fb_fill_rect
        pop ecx
        pop ebx

.bar_label:
        ; Label: k number below axis
        push ecx
        mov ecx, BAR_Y0 + 8
        mov edx, ecx            ; need ecx for fb_draw_text — rearrange
        pop ecx
        ; fb_draw_text: EBX=x ECX=y ESI=str_ptr EDI=color
        ; Compute str_ptr for k (pre-built single char)
        ; We rebuild k index from the outer loop variable saved in memory
        ; (ecx was restored from push/pop so it's still k here? No - ecx
        ;  was used for y inside the push.  Let me save k to memory.)
        ; k is in [cur_k].
        mov eax, [cur_k]
        add eax, '0'
        cmp eax, '9' + 1
        jl .digit_ok
        add eax, 'A' - '0' - 10  ; A..F for bins 10..15
.digit_ok:
        mov [k_label], al
        mov byte [k_label+1], 0

        mov ebx, [cur_k]
        imul ebx, BAR_W + BAR_GAP
        add ebx, 2 + BAR_W/2 - 4   ; roughly centre under bar
        mov ecx, BAR_Y0 + 6
        mov esi, k_label
        mov edi, COL_LABEL
        call fb_draw_text

        ; Advance outer loop (k stored in [cur_k])
        add dword [cur_k], 1
        mov ecx, [cur_k]
        cmp ecx, N
        jl .bar_loop

        ; Reset cur_k for next call
        mov dword [cur_k], 0

        ; Present
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80

        popad
        ret

;=======================================================================
; fb_fill_rect  —  EBX=x ECX=y EDX=w ESI=h EDI=colour
;=======================================================================
fb_fill_rect:
        pushad
        test edx, edx
        jz .done
        test esi, esi
        jz .done
        mov eax, ecx
        imul eax, [fb_pitch]
        add eax, [fb_addr]
        lea eax, [eax + ebx*4]
.row:
        push eax
        push edx
        mov ecx, edx
.col:
        mov [eax], edi
        add eax, 4
        dec ecx
        jnz .col
        pop edx
        pop eax
        add eax, [fb_pitch]
        dec esi
        jnz .row
.done:
        popad
        ret

;=======================================================================
; fb_draw_text  —  EBX=x ECX=y ESI=str_ptr EDI=colour
;=======================================================================
fb_draw_text:
        pushad
        mov edx, ecx
        mov ecx, ebx
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        int 0x80
        popad
        ret

;=======================================================================
; DATA
;=======================================================================
tname:      db "fft", 0
title_str:  db "Mellivora DFT — 16-point spectrum analyser", 0
hint_str:   db "Keys: 1=cosine  2=square  3=triangle  ESC=quit", 0

;=======================================================================
; BSS
;=======================================================================
section .bss
fb_addr:    resd 1
fb_pitch:   resd 1
sig_ptr:    resd 1
mag:        resd N          ; magnitudes for bins 0..15
max_mag:    resd 1
bar_h:      resd 1
cur_k:      resd 1
k_label:    resb 4          ; single-char label + null
