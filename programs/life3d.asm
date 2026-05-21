; life3d.asm — Conway's Game of Life with isometric display
;
; A 40×20 cell grid is drawn as an isometric diamond tilemap.
; Live cells appear as bright raised tiles; dead cells are dark.
; Press ESC to quit.
;
; Build:  nasm -f bin -O0 life3d.asm -o life3d.bin

%include "syscalls.inc"

; ---- Screen ----------------------------------------------------------
SCR_W   equ 640
SCR_H   equ 480

; ---- Grid ------------------------------------------------------------
GW      equ 40          ; grid width  (columns)
GH      equ 20          ; grid height (rows)

; ---- Isometric tile --------------------------------------------------
; iso_x = OX + (col - row) * THW
; iso_y = OY + (col + row) * THH
; Diamonds tile perfectly: no overlap, no gaps.
THW     equ 8           ; tile half-width  (full diamond = 16 px)
THH     equ 4           ; tile half-height (full diamond =  8 px)
OX      equ 320         ; isometric origin X  (screen coords)
OY      equ 100         ; isometric origin Y

; ---- Timing ----------------------------------------------------------
DELAY   equ 12          ; sleep ticks per generation  (~120 ms at 100 Hz)

; ---- Colours ---------------------------------------------------------
COL_BG   equ 0x04080C
COL_DEAD equ 0x091509
COL_LIVE equ 0x44FF44
COL_SHAD equ 0x104410   ; shadow cast by live tile
COL_TOP  equ 0xBBFFBB   ; highlight row of live tile

; ---- Other -----------------------------------------------------------
KEY_ESC  equ 0x01

;=======================================================================
start:
        mov eax, SYS_TASKNAME
        mov ebx, tname
        int 0x80

        ; ---- VBE 640×480×32 -----------------------------------------
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

        ; ---- Seed RNG ------------------------------------------------
        mov eax, SYS_GETTIME
        int 0x80
        add eax, 0xA5A5A5A5
        mov [rng], eax

        ; ---- Seed grid (~37.5% density) ------------------------------
        mov ecx, GW * GH
        mov edi, grid
.seed_loop:
        call lcg
        and eax, 7              ; 0..7
        xor edx, edx
        cmp eax, 3              ; alive if < 3  (37.5%)
        setb dl
        mov [edi], dl
        inc edi
        dec ecx
        jnz .seed_loop

;=======================================================================
.main_loop:
        ; ---- Clear screen -------------------------------------------
        mov ebx, 0
        mov ecx, 0
        mov edx, SCR_W
        mov esi, SCR_H
        mov edi, COL_BG
        call fb_fill_rect

        ; ---- Draw grid in depth order (back to front) ---------------
        ; Iterate diagonal d = col+row, from 0 to GW+GH-2.
        ; Within each diagonal: col decreasing, row increasing.
        xor ecx, ecx            ; d = 0
.draw_diag:
        ; row_start = max(0, d - (GW-1))
        mov eax, ecx
        sub eax, GW - 1
        jge .rs_use
        xor eax, eax
.rs_use:
        mov [cur_row], eax      ; starting row for this diagonal
        ; col_start = d - row_start
        mov eax, ecx
        sub eax, [cur_row]
        mov [cur_col], eax

.draw_cell:
        ; bounds check
        mov eax, [cur_col]
        cmp eax, GW
        jge .draw_diag_next
        cmp eax, 0
        jl  .draw_diag_next
        mov eax, [cur_row]
        cmp eax, GH
        jge .draw_diag_next

        ; iso_cx = OX + (col - row) * THW
        mov eax, [cur_col]
        sub eax, [cur_row]
        imul eax, THW
        add eax, OX
        mov [iso_cx], eax

        ; iso_cy = OY + (col + row) * THH
        mov eax, [cur_col]
        add eax, [cur_row]
        imul eax, THH
        add eax, OY
        mov [iso_cy], eax

        ; Look up cell state: grid[row*GW + col]
        mov eax, [cur_row]
        imul eax, GW
        add eax, [cur_col]
        movzx eax, byte [grid + eax]

        test eax, eax
        jz .draw_dead

        ; ---- Live cell: shadow at (cx+2, cy+3) then tile at (cx, cy-2)
        mov eax, [iso_cx]
        add eax, 2
        mov [draw_cx], eax
        mov eax, [iso_cy]
        add eax, 3
        mov [draw_cy], eax
        mov dword [draw_color], COL_SHAD
        call draw_diamond

        mov eax, [iso_cx]
        mov [draw_cx], eax
        mov eax, [iso_cy]
        sub eax, 2
        mov [draw_cy], eax
        mov dword [draw_color], COL_LIVE
        call draw_diamond

        ; Bright top scanline for highlight
        mov eax, [iso_cx]
        mov [draw_cx], eax
        mov eax, [iso_cy]
        sub eax, 2
        mov [draw_cy], eax
        mov dword [draw_color], COL_TOP
        ; Just paint the very top row of the diamond (y_off=-4, w=1)
        mov ebx, [draw_cx]
        mov ecx, [draw_cy]
        sub ecx, THH
        mov edx, 1
        mov esi, 1
        mov edi, COL_TOP
        call fb_fill_rect

        jmp .draw_next

.draw_dead:
        ; ---- Dead cell: flat dark diamond ---------------------------
        mov eax, [iso_cx]
        mov [draw_cx], eax
        mov eax, [iso_cy]
        mov [draw_cy], eax
        mov dword [draw_color], COL_DEAD
        call draw_diamond

.draw_next:
        ; Advance diagonal: col--, row++
        sub dword [cur_col], 1
        add dword [cur_row], 1
        jmp .draw_cell

.draw_diag_next:
        inc ecx
        cmp ecx, GW + GH - 1
        jl .draw_diag

        ; ---- Present ------------------------------------------------
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80

        ; ---- Sleep --------------------------------------------------
        mov eax, SYS_SLEEP
        mov ebx, DELAY
        int 0x80

        ; ---- Non-blocking key check ---------------------------------
        mov eax, SYS_READ_KEY
        xor ebx, ebx
        int 0x80
        cmp eax, KEY_ESC
        je .bye

        ; ---- Compute next generation --------------------------------
        call life_step

        jmp .main_loop

.bye:
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================================================
; draw_diamond — filled isometric diamond
; Reads: [draw_cx], [draw_cy], [draw_color]
; Diamond: half-width=THW=8, half-height=THH=4, 9 scanlines
; x_half for y_offset = THW * (THH - |y_off|) / THH = 2*(4-|y|)
;=======================================================================
draw_diamond:
        push esi
        push edi
        push ebx
        push ecx
        push edx
        mov edi, [draw_color]   ; EDI = color (preserved across fb_fill_rect)
        mov esi, 1              ; height = 1 (preserved across fb_fill_rect)

        ; y_off=-4: w=1,  x=cx+0
        mov ebx, [draw_cx]
        mov ecx, [draw_cy]
        sub ecx, 4
        mov edx, 1
        call fb_fill_rect

        ; y_off=-3: w=5,  x=cx-2
        mov ebx, [draw_cx]
        sub ebx, 2
        mov ecx, [draw_cy]
        sub ecx, 3
        mov edx, 5
        call fb_fill_rect

        ; y_off=-2: w=9,  x=cx-4
        mov ebx, [draw_cx]
        sub ebx, 4
        mov ecx, [draw_cy]
        sub ecx, 2
        mov edx, 9
        call fb_fill_rect

        ; y_off=-1: w=13, x=cx-6
        mov ebx, [draw_cx]
        sub ebx, 6
        mov ecx, [draw_cy]
        sub ecx, 1
        mov edx, 13
        call fb_fill_rect

        ; y_off=0:  w=17, x=cx-8
        mov ebx, [draw_cx]
        sub ebx, 8
        mov ecx, [draw_cy]
        mov edx, 17
        call fb_fill_rect

        ; y_off=+1: w=13, x=cx-6
        mov ebx, [draw_cx]
        sub ebx, 6
        mov ecx, [draw_cy]
        add ecx, 1
        mov edx, 13
        call fb_fill_rect

        ; y_off=+2: w=9,  x=cx-4
        mov ebx, [draw_cx]
        sub ebx, 4
        mov ecx, [draw_cy]
        add ecx, 2
        mov edx, 9
        call fb_fill_rect

        ; y_off=+3: w=5,  x=cx-2
        mov ebx, [draw_cx]
        sub ebx, 2
        mov ecx, [draw_cy]
        add ecx, 3
        mov edx, 5
        call fb_fill_rect

        ; y_off=+4: w=1,  x=cx+0
        mov ebx, [draw_cx]
        mov ecx, [draw_cy]
        add ecx, 4
        mov edx, 1
        call fb_fill_rect

        pop edx
        pop ecx
        pop ebx
        pop edi
        pop esi
        ret

;=======================================================================
; life_step — advance grid one generation (double-buffered)
; Rules: alive+{2,3}→alive  dead+3→alive  else→dead
; Uses memory variables to avoid register conflicts.
;=======================================================================
life_step:
        pushad
        mov dword [ls_row], 0
.ls_outer:
        mov dword [ls_col], 0
.ls_inner:
        ; Count live neighbours
        xor esi, esi            ; neighbour count
        mov dword [ls_dr], -1
.ls_dr:
        mov dword [ls_dc], -1
.ls_dc:
        ; Skip centre cell (dr=0 AND dc=0)
        mov eax, [ls_dr]
        or  eax, [ls_dc]
        jz  .ls_skip

        ; nr = (row + dr + GH) % GH  — wrap using add/sub (|delta|=1)
        mov eax, [ls_row]
        add eax, [ls_dr]
        js  .ls_nr_neg
        cmp eax, GH
        jl  .ls_nr_ok
        sub eax, GH
        jmp .ls_nr_ok
.ls_nr_neg:
        add eax, GH
.ls_nr_ok:
        mov [ls_nr], eax

        ; nc = (col + dc + GW) % GW
        mov eax, [ls_col]
        add eax, [ls_dc]
        js  .ls_nc_neg
        cmp eax, GW
        jl  .ls_nc_ok
        sub eax, GW
        jmp .ls_nc_ok
.ls_nc_neg:
        add eax, GW
.ls_nc_ok:
        ; Accumulate grid[nr*GW + nc]
        push eax                ; save nc
        mov eax, [ls_nr]
        imul eax, GW
        pop edx
        add eax, edx
        movzx edx, byte [grid + eax]
        add esi, edx

.ls_skip:
        add dword [ls_dc], 1
        cmp dword [ls_dc], 2
        jle .ls_dc

        add dword [ls_dr], 1
        cmp dword [ls_dr], 2
        jle .ls_dr

        ; Get current cell state
        mov eax, [ls_row]
        imul eax, GW
        add eax, [ls_col]
        movzx eax, byte [grid + eax]    ; eax = 0 or 1

        ; Apply rules → new state into dl
        xor edx, edx                    ; new state = dead
        test eax, eax
        jz  .ls_born_check              ; dead cell path
        ; Alive: survive if esi ∈ {2,3}
        cmp esi, 2
        je  .ls_set_alive
        cmp esi, 3
        je  .ls_set_alive
        jmp .ls_write
.ls_born_check:
        cmp esi, 3
        jne .ls_write
.ls_set_alive:
        mov edx, 1
.ls_write:
        ; next_grid[row*GW + col] = dl
        mov eax, [ls_row]
        imul eax, GW
        add eax, [ls_col]
        mov [next_grid + eax], dl       ; dl = low byte of EDX (valid 32-bit)

        add dword [ls_col], 1
        mov eax, [ls_col]
        cmp eax, GW
        jl  .ls_inner

        add dword [ls_row], 1
        mov eax, [ls_row]
        cmp eax, GH
        jl  .ls_outer

        ; Swap buffers: copy next_grid → grid
        mov esi, next_grid
        mov edi, grid
        mov ecx, GW * GH
        rep movsb
        popad
        ret

;=======================================================================
; lcg — linear congruential generator  (state → eax)
;=======================================================================
lcg:
        mov eax, [rng]
        imul eax, 1664525
        add eax, 1013904223
        mov [rng], eax
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
; DATA
;=======================================================================
tname: db "life3d", 0

;=======================================================================
; BSS
;=======================================================================
section .bss
fb_addr:        resd 1
fb_pitch:       resd 1
rng:            resd 1

iso_cx:         resd 1
iso_cy:         resd 1

draw_cx:        resd 1
draw_cy:        resd 1
draw_color:     resd 1

cur_row:        resd 1
cur_col:        resd 1

; life_step temporaries
ls_row:         resd 1
ls_col:         resd 1
ls_dr:          resd 1
ls_dc:          resd 1
ls_nr:          resd 1

; Double-buffered cell grids
grid:           resb GW * GH
next_grid:      resb GW * GH
