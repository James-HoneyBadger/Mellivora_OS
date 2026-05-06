; periodic.asm - Graphical Periodic Table of the Elements for Mellivora OS
;
; Burrows GUI application (1000x680 window).
;
; Layout:
;   Row 0-39  px  : Title bar (dark blue)
;   Row 40-43  px  : gap
;   Grid: 18 cols × 9 rows of 48×44 px cells  (cols 0..17, rows 0..8)
;         positioned at GRID_X=4, GRID_Y=44
;         → right edge at 4 + 18*48 = 868
;   Info panel: x=870..998, y=44..439  (element details)
;   Legend bar: y=440..461
;
; Controls:
;   Arrow keys / left-click   — navigate / select
;   Q / Esc / close button    — exit
;
; Info panel shows: full name, symbol, atomic number, category (with
; colour swatch), electron configuration, and two educational facts.
;
%include "syscalls.inc"
%include "lib/gui.inc"

; === Window geometry ===
WIN_W           equ 1000
WIN_H           equ 680

; === Grid ===
GRID_X          equ 4
GRID_Y          equ 44
CELL_W          equ 48
CELL_H          equ 44
TABLE_COLS      equ 18
TABLE_ROWS      equ 9

; === Info panel ===
INFO_X          equ 870         ; 4 + 18*48 + 2
INFO_Y          equ GRID_Y
INFO_W          equ WIN_W - INFO_X - 2
INFO_H          equ TABLE_ROWS * CELL_H   ; 396

; === Legend bar ===
LEG_Y           equ INFO_Y + INFO_H + 4   ; 444
LEG_H           equ 22

; === Palette ===
COL_BG          equ 0x00141820
COL_TITLE_BG    equ 0x00243050
COL_GRID_BG     equ 0x001A2030
COL_INFO_BG     equ 0x001C2840
COL_INFO_HDR    equ 0x00304878
COL_INFO_TXT    equ 0x00D0E0F0
COL_INFO_VAL    equ 0x00A8D4FF
COL_INFO_ACC    equ 0x0060C0FF
COL_LEGEND_BG   equ 0x00181E28
COL_SEP         equ 0x00304860

; Category cell background colours
COL_ALKALI      equ 0x00B03018
COL_ALKALINE    equ 0x00905010
COL_TRANS       equ 0x00205870
COL_BASIC       equ 0x00205040
COL_SEMIMETAL   equ 0x00386030
COL_NONMETAL    equ 0x00506090
COL_HALOGEN     equ 0x00703080
COL_NOBLE       equ 0x00104070
COL_LANTHANIDE  equ 0x00804820
COL_ACTINIDE    equ 0x00803020

; Category index constants (match elem_category bytes)
CAT_NONE        equ 0
CAT_ALKALI      equ 1
CAT_ALKALINE    equ 2
CAT_TRANS       equ 3
CAT_BASIC       equ 4
CAT_SEMIMETAL   equ 5
CAT_NONMETAL    equ 6
CAT_HALOGEN     equ 7
CAT_NOBLE       equ 8
CAT_LANTHANIDE  equ 9
CAT_ACTINIDE    equ 10

;=======================================================================
start:
        ; Activate VBE (1024x768x32) so gui_flip can write to the LFB.
        ; Required when the program is launched from the text-mode shell
        ; before the Burrows desktop has called vbe_set_mode.  Safe to
        ; call again from within the desktop — it just re-sets the same mode.
        mov eax, SYS_FRAMEBUF
        mov ebx, 1
        mov ecx, 1024
        mov edx, 768
        mov esi, 32
        int 0x80
        cmp eax, -1
        je .exit

        mov eax, 20
        mov ebx, 20
        mov ecx, WIN_W
        mov edx, WIN_H
        mov esi, win_title
        call gui_create_window
        cmp eax, -1
        je .exit
        mov [win_id], eax

        mov dword [sel_col], 0
        mov dword [sel_row], 0

        call render_all
        call gui_compose
        call gui_flip

;=======================================================================
.main_loop:
        call gui_poll_event
        cmp eax, EVT_NONE
        je .idle
        cmp eax, EVT_CLOSE
        je .close
        cmp eax, EVT_KEY_PRESS
        je .key
        cmp eax, EVT_MOUSE_CLICK
        je .click
        jmp .main_loop

.idle:
        mov eax, SYS_SLEEP
        mov ebx, 5
        int 0x80
        jmp .main_loop

.key:
        cmp bl, 'q'
        je .close
        cmp bl, 'Q'
        je .close
        cmp bl, 27
        je .close
        cmp bl, KEY_UP
        je .ku
        cmp bl, KEY_DOWN
        je .kd
        cmp bl, KEY_LEFT
        je .kl
        cmp bl, KEY_RIGHT
        je .kr
        jmp .main_loop

.ku:    cmp dword [sel_row], 0
        je .main_loop
        dec dword [sel_row]
        call skip_empty_up
        jmp .redraw
.kd:    mov eax, [sel_row]
        cmp eax, TABLE_ROWS - 1
        jge .main_loop
        inc dword [sel_row]
        call skip_empty_down
        jmp .redraw
.kl:    cmp dword [sel_col], 0
        je .main_loop
        dec dword [sel_col]
        call skip_empty_left
        jmp .redraw
.kr:    mov eax, [sel_col]
        cmp eax, TABLE_COLS - 1
        jge .main_loop
        inc dword [sel_col]
        call skip_empty_right
        jmp .redraw

.click:
        ; EBX = click x, ECX = click y (relative to window client area)
        sub ebx, GRID_X
        jl .main_loop
        sub ecx, GRID_Y
        jl .main_loop
        cmp ebx, TABLE_COLS * CELL_W
        jge .main_loop
        cmp ecx, TABLE_ROWS * CELL_H
        jge .main_loop
        ; col = ebx / CELL_W
        push edx
        mov eax, ebx
        xor edx, edx
        mov ebx, CELL_W
        div ebx
        mov [sel_col], eax
        ; row = ecx / CELL_H
        mov eax, ecx
        xor edx, edx
        mov ebx, CELL_H
        div ebx
        mov [sel_row], eax
        pop edx
        ; Reject empty cells
        mov eax, [sel_row]
        imul eax, TABLE_COLS
        add eax, [sel_col]
        movzx eax, byte [table_layout + eax]
        test eax, eax
        jz .main_loop

.redraw:
        call render_all
        call gui_compose
        call gui_flip
        jmp .main_loop

.close:
        mov eax, [win_id]
        call gui_destroy_window
.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================================================
; skip_empty_* — after moving cursor, advance past empty cells
;=======================================================================
%macro SKIP_EMPTY 2 ; %1 = variable, %2 = direction (+1 or -1), %3=%4 boundary
%endmacro

skip_empty_right:
        push eax
        push ecx
.ser_l: mov eax, [sel_row]
        imul eax, TABLE_COLS
        add eax, [sel_col]
        movzx ecx, byte [table_layout + eax]
        test ecx, ecx
        jnz .ser_e
        cmp dword [sel_col], TABLE_COLS - 1
        je .ser_e
        inc dword [sel_col]
        jmp .ser_l
.ser_e: pop ecx
        pop eax
        ret

skip_empty_left:
        push eax
        push ecx
.sel_l: mov eax, [sel_row]
        imul eax, TABLE_COLS
        add eax, [sel_col]
        movzx ecx, byte [table_layout + eax]
        test ecx, ecx
        jnz .sel_e
        cmp dword [sel_col], 0
        je .sel_e
        dec dword [sel_col]
        jmp .sel_l
.sel_e: pop ecx
        pop eax
        ret

skip_empty_down:
        push eax
        push ecx
.sed_l: mov eax, [sel_row]
        imul eax, TABLE_COLS
        add eax, [sel_col]
        movzx ecx, byte [table_layout + eax]
        test ecx, ecx
        jnz .sed_e
        cmp dword [sel_row], TABLE_ROWS - 1
        je .sed_e
        inc dword [sel_row]
        jmp .sed_l
.sed_e: pop ecx
        pop eax
        ret

skip_empty_up:
        push eax
        push ecx
.seu_l: mov eax, [sel_row]
        imul eax, TABLE_COLS
        add eax, [sel_col]
        movzx ecx, byte [table_layout + eax]
        test ecx, ecx
        jnz .seu_e
        cmp dword [sel_row], 0
        je .seu_e
        dec dword [sel_row]
        jmp .seu_l
.seu_e: pop ecx
        pop eax
        ret

;=======================================================================
; render_all - Full window repaint
;=======================================================================
render_all:
        pushad
        ; --- background ---
        mov eax, [win_id]
        xor ebx, ebx
        xor ecx, ecx
        mov edx, WIN_W
        mov esi, WIN_H
        mov edi, COL_BG
        call gui_fill_rect

        ; --- title bar ---
        mov eax, [win_id]
        xor ebx, ebx
        xor ecx, ecx
        mov edx, WIN_W
        mov esi, 40
        mov edi, COL_TITLE_BG
        call gui_fill_rect

        mov eax, [win_id]
        mov ebx, 10
        mov ecx, 10
        mov esi, title_str
        mov edi, 0x00A0C8FF
        call gui_draw_text

        call draw_grid
        call draw_info_panel
        call draw_legend
        popad
        ret

;=======================================================================
; draw_grid - Render all element cells
;=======================================================================
draw_grid:
        pushad
        mov dword [dg_row], 0
.row:
        mov eax, [dg_row]
        cmp eax, TABLE_ROWS
        jge .done

        mov dword [dg_col], 0
.col:
        mov eax, [dg_col]
        cmp eax, TABLE_COLS
        jge .next_row

        ; pixel origin of this cell
        mov eax, [dg_col]
        imul eax, CELL_W
        add eax, GRID_X
        mov [dg_px], eax

        mov eax, [dg_row]
        imul eax, CELL_H
        add eax, GRID_Y
        mov [dg_py], eax

        ; element number at this grid position
        mov eax, [dg_row]
        imul eax, TABLE_COLS
        add eax, [dg_col]
        movzx eax, byte [table_layout + eax]
        mov [dg_elem], eax

        test eax, eax
        jz .empty_cell

        ; ---- filled cell ----
        ; Is this the selected cell?
        mov eax, [dg_row]
        cmp eax, [sel_row]
        jne .normal_cell
        mov eax, [dg_col]
        cmp eax, [sel_col]
        jne .normal_cell

        ; Selected: white background
        mov eax, [win_id]
        mov ebx, [dg_px]
        mov ecx, [dg_py]
        mov edx, CELL_W - 1
        mov esi, CELL_H - 1
        mov edi, 0x00FFFFFF
        call gui_fill_rect
        mov dword [dg_sel], 1
        jmp .draw_border

.normal_cell:
        ; Category colour background
        mov dword [dg_sel], 0
        mov eax, [dg_elem]
        call get_cat_color              ; EAX = colour
        mov [dg_color], eax
        mov eax, [win_id]
        mov ebx, [dg_px]
        mov ecx, [dg_py]
        mov edx, CELL_W - 1
        mov esi, CELL_H - 1
        mov edi, [dg_color]
        call gui_fill_rect

.draw_border:
        ; Top border line (1 px dark)
        mov eax, [win_id]
        mov ebx, [dg_px]
        mov ecx, [dg_py]
        mov edx, CELL_W - 1
        mov esi, 1
        mov edi, COL_BG
        call gui_fill_rect

        ; Atomic number (top-left, small)
        mov eax, [dg_elem]
        mov edi, dg_numbuf
        call uint_to_str

        cmp dword [dg_sel], 1
        je .num_black
        mov edi, 0x00B0C8D8
        jmp .num_draw
.num_black:
        mov edi, 0x00202020
.num_draw:
        mov eax, [win_id]
        mov ebx, [dg_px]
        add ebx, 2
        mov ecx, [dg_py]
        add ecx, 3
        mov esi, dg_numbuf
        call gui_draw_text

        ; Symbol (centred-ish)
        mov eax, [dg_elem]
        dec eax
        shl eax, 1
        lea eax, [symbols + eax]
        movzx ebx, byte [eax]
        mov [dg_symbuf], bl
        movzx ebx, byte [eax + 1]
        cmp bl, ' '
        je .sym1
        mov [dg_symbuf + 1], bl
        mov byte [dg_symbuf + 2], 0
        jmp .sym_ok
.sym1:  mov byte [dg_symbuf + 1], 0
.sym_ok:
        cmp dword [dg_sel], 1
        je .sym_black
        mov edi, 0x00FFFFFF
        jmp .sym_draw
.sym_black:
        mov edi, 0x00101010
.sym_draw:
        mov eax, [win_id]
        mov ebx, [dg_px]
        add ebx, 12
        mov ecx, [dg_py]
        add ecx, 20
        mov esi, dg_symbuf
        call gui_draw_text

        jmp .next_col

.empty_cell:
        ; Dark background for empty grid positions
        mov eax, [win_id]
        mov ebx, [dg_px]
        mov ecx, [dg_py]
        mov edx, CELL_W - 1
        mov esi, CELL_H - 1
        mov edi, COL_GRID_BG
        call gui_fill_rect

.next_col:
        inc dword [dg_col]
        jmp .col

.next_row:
        inc dword [dg_row]
        jmp .row

.done:
        popad
        ret

;=======================================================================
; draw_info_panel - Right-side element detail panel
;=======================================================================
draw_info_panel:
        pushad

        ; Background
        mov eax, [win_id]
        mov ebx, INFO_X
        mov ecx, INFO_Y
        mov edx, INFO_W
        mov esi, INFO_H
        mov edi, COL_INFO_BG
        call gui_fill_rect

        ; Header strip
        mov eax, [win_id]
        mov ebx, INFO_X
        mov ecx, INFO_Y
        mov edx, INFO_W
        mov esi, 28
        mov edi, COL_INFO_HDR
        call gui_fill_rect

        mov eax, [win_id]
        mov ebx, INFO_X + 5
        mov ecx, INFO_Y + 7
        mov esi, hdr_info_str
        mov edi, 0x0090B8E0
        call gui_draw_text

        ; Get selected element
        mov eax, [sel_row]
        imul eax, TABLE_COLS
        add eax, [sel_col]
        movzx eax, byte [table_layout + eax]
        test eax, eax
        jz .no_elem
        mov [dip_elem], eax

        ; ---- Element name (accent colour) ----
        mov eax, [dip_elem]
        dec eax
        call get_name_ptr               ; EBX = name string ptr
        mov eax, [win_id]
        mov ecx, INFO_X + 4
        mov edx, INFO_Y + 32
        mov esi, ebx
        mov edi, COL_INFO_ACC
        ; gui_draw_text: EAX=win_id EBX=x ECX=y ESI=text EDI=color
        mov ebx, ecx
        mov ecx, edx
        call gui_draw_text

        ; ---- Symbol ----
        mov eax, [win_id]
        mov ebx, INFO_X + 4
        mov ecx, INFO_Y + 50
        mov esi, lbl_sym
        mov edi, COL_INFO_TXT
        call gui_draw_text

        ; build sym_buf
        mov eax, [dip_elem]
        dec eax
        shl eax, 1
        lea eax, [symbols + eax]
        movzx ebx, byte [eax]
        mov [sym_buf], bl
        movzx ebx, byte [eax + 1]
        cmp bl, ' '
        je .sym1c
        mov [sym_buf + 1], bl
        mov byte [sym_buf + 2], 0
        jmp .sym_ok2
.sym1c: mov byte [sym_buf + 1], 0
.sym_ok2:

        mov eax, [win_id]
        mov ebx, INFO_X + 72
        mov ecx, INFO_Y + 50
        mov esi, sym_buf
        mov edi, COL_INFO_VAL
        call gui_draw_text

        ; ---- Atomic number ----
        mov eax, [win_id]
        mov ebx, INFO_X + 4
        mov ecx, INFO_Y + 66
        mov esi, lbl_anum
        mov edi, COL_INFO_TXT
        call gui_draw_text

        mov eax, [dip_elem]
        mov edi, tmp_buf
        call uint_to_str

        mov eax, [win_id]
        mov ebx, INFO_X + 72
        mov ecx, INFO_Y + 66
        mov esi, tmp_buf
        mov edi, COL_INFO_VAL
        call gui_draw_text

        ; ---- Category ----
        mov eax, [win_id]
        mov ebx, INFO_X + 4
        mov ecx, INFO_Y + 82
        mov esi, lbl_cat
        mov edi, COL_INFO_TXT
        call gui_draw_text

        ; Colour swatch (12×12)
        mov eax, [dip_elem]
        call get_cat_color              ; EAX = colour
        mov [dip_col_tmp], eax

        mov eax, [win_id]
        mov ebx, INFO_X + 72
        mov ecx, INFO_Y + 84
        mov edx, 12
        mov esi, 12
        mov edi, [dip_col_tmp]
        call gui_fill_rect

        ; Category name
        mov eax, [dip_elem]
        call get_cat_name_ptr           ; EBX = name ptr
        mov eax, [win_id]
        mov ecx, INFO_X + 88
        mov edx, INFO_Y + 82
        mov esi, ebx
        mov edi, COL_INFO_VAL
        mov ebx, ecx
        mov ecx, edx
        call gui_draw_text

        ; ---- Electron configuration ----
        mov eax, [win_id]
        mov ebx, INFO_X + 4
        mov ecx, INFO_Y + 106
        mov esi, lbl_config
        mov edi, COL_INFO_TXT
        call gui_draw_text

        mov eax, [dip_elem]
        call get_config_ptr             ; EBX = config string ptr
        mov eax, [win_id]
        mov ebx, INFO_X + 4
        mov ecx, INFO_Y + 120
        mov esi, ebx
        ; BUG: esi clobbered by call — redo:
        mov eax, [dip_elem]
        call get_config_ptr
        mov [dip_ptr_tmp], ebx
        mov eax, [win_id]
        mov ebx, INFO_X + 4
        mov ecx, INFO_Y + 120
        mov esi, [dip_ptr_tmp]
        mov edi, COL_INFO_VAL
        call gui_draw_text

        ; ---- Separator ----
        mov eax, [win_id]
        mov ebx, INFO_X + 4
        mov ecx, INFO_Y + 140
        mov edx, INFO_W - 8
        mov esi, 1
        mov edi, COL_SEP
        call gui_fill_rect

        ; ---- Did you know? ----
        mov eax, [win_id]
        mov ebx, INFO_X + 4
        mov ecx, INFO_Y + 148
        mov esi, lbl_fact
        mov edi, 0x0080B0C8
        call gui_draw_text

        ; Fact line 1
        mov eax, [dip_elem]
        call get_fact_ptr
        mov [dip_ptr_tmp], ebx

        mov eax, [win_id]
        mov ebx, INFO_X + 4
        mov ecx, INFO_Y + 162
        mov esi, [dip_ptr_tmp]
        mov edi, 0x0098B0B8
        call gui_draw_text

        ; Fact line 2
        mov eax, [dip_elem]
        call get_fact2_ptr
        mov [dip_ptr_tmp], ebx

        mov eax, [win_id]
        mov ebx, INFO_X + 4
        mov ecx, INFO_Y + 178
        mov esi, [dip_ptr_tmp]
        mov edi, 0x0098B0B8
        call gui_draw_text

        ; ---- Navigation hint (bottom) ----
        mov eax, [win_id]
        mov ebx, INFO_X + 4
        mov ecx, INFO_Y + INFO_H - 18
        mov esi, nav_hint
        mov edi, 0x00485868
        call gui_draw_text

        jmp .end

.no_elem:
        mov eax, [win_id]
        mov ebx, INFO_X + 4
        mov ecx, INFO_Y + 40
        mov esi, no_elem_str
        mov edi, 0x00506070
        call gui_draw_text
.end:
        popad
        ret

;=======================================================================
; draw_legend - Bottom colour key bar
;=======================================================================
draw_legend:
        pushad

        ; Background
        mov eax, [win_id]
        xor ebx, ebx
        mov ecx, LEG_Y
        mov edx, WIN_W
        mov esi, LEG_H
        mov edi, COL_LEGEND_BG
        call gui_fill_rect

        ; "Legend:" label
        mov eax, [win_id]
        mov ebx, 4
        mov ecx, LEG_Y + 5
        mov esi, lbl_legend
        mov edi, 0x00708090
        call gui_draw_text

        ; Iterate through 10 category entries
        mov dword [leg_x], 58   ; start after "Legend: " label
        mov dword [leg_idx], 1  ; start at CAT_ALKALI

.leg_loop:
        mov eax, [leg_idx]
        cmp eax, 11
        jge .leg_done

        ; Swatch (12×14)
        call get_cat_color_by_idx       ; EAX = colour (arg already in EAX)
        mov [leg_col_tmp], eax

        mov eax, [win_id]
        mov ebx, [leg_x]
        mov ecx, LEG_Y + 4
        mov edx, 12
        mov esi, 14
        mov edi, [leg_col_tmp]
        call gui_fill_rect

        ; Label
        mov eax, [leg_idx]
        dec eax
        shl eax, 2
        mov esi, [cat_leg_labels + eax]

        mov eax, [win_id]
        mov ebx, [leg_x]
        add ebx, 14
        mov ecx, LEG_Y + 5
        mov edi, 0x00A0B0C0
        call gui_draw_text

        ; Advance x: swatch(12) + gap(4) + text width
        ; Approximate text width: count string chars * 8 + 10
        push esi
        xor ecx, ecx
.leg_strlen:
        cmp byte [esi], 0
        je .leg_strlen_done
        inc esi
        inc ecx
        jmp .leg_strlen
.leg_strlen_done:
        pop esi
        ; advance = 12 + 4 + ecx*8 + 6
        imul ecx, 8
        add ecx, 22             ; 12 swatch + 4 gap + 6 extra
        add [leg_x], ecx

        inc dword [leg_idx]
        jmp .leg_loop

.leg_done:
        popad
        ret

;=======================================================================
; get_cat_color_by_idx: EAX = category index -> EAX = colour
;=======================================================================
get_cat_color_by_idx:
        cmp eax, CAT_ALKALI
        je .c1
        cmp eax, CAT_ALKALINE
        je .c2
        cmp eax, CAT_TRANS
        je .c3
        cmp eax, CAT_BASIC
        je .c4
        cmp eax, CAT_SEMIMETAL
        je .c5
        cmp eax, CAT_NONMETAL
        je .c6
        cmp eax, CAT_HALOGEN
        je .c7
        cmp eax, CAT_NOBLE
        je .c8
        cmp eax, CAT_LANTHANIDE
        je .c9
        cmp eax, CAT_ACTINIDE
        je .c10
        mov eax, COL_GRID_BG
        ret
.c1:  mov eax, COL_ALKALI      ; ret
      ret
.c2:  mov eax, COL_ALKALINE    ; ret
      ret
.c3:  mov eax, COL_TRANS       ; ret
      ret
.c4:  mov eax, COL_BASIC       ; ret
      ret
.c5:  mov eax, COL_SEMIMETAL   ; ret
      ret
.c6:  mov eax, COL_NONMETAL    ; ret
      ret
.c7:  mov eax, COL_HALOGEN     ; ret
      ret
.c8:  mov eax, COL_NOBLE       ; ret
      ret
.c9:  mov eax, COL_LANTHANIDE  ; ret
      ret
.c10: mov eax, COL_ACTINIDE    ; ret
      ret

;=======================================================================
; get_cat_color: EAX = element# (1-based) -> EAX = colour
;=======================================================================
get_cat_color:
        push ebx
        dec eax
        cmp eax, 117
        ja .def
        movzx eax, byte [elem_category + eax]
        call get_cat_color_by_idx
        pop ebx
        ret
.def:   mov eax, COL_GRID_BG
        pop ebx
        ret

;=======================================================================
; get_cat_name_ptr: EAX = element# (1-based) -> EBX = ptr to category name
;=======================================================================
get_cat_name_ptr:
        push eax
        dec eax
        cmp eax, 117
        ja .def
        movzx eax, byte [elem_category + eax]
        cmp eax, 10
        ja .def
        shl eax, 2
        mov ebx, [cat_names_tbl + eax]
        pop eax
        ret
.def:   mov ebx, cat_name_none
        pop eax
        ret

;=======================================================================
; get_name_ptr: EAX = 0-based index -> EBX = ptr to name
;=======================================================================
get_name_ptr:
        push ecx
        mov ebx, elem_names
        mov ecx, eax
        test ecx, ecx
        jz .done
.loop:  cmp byte [ebx], 0
        jne .next
        dec ecx
        js .done
        jz .found
.next:  inc ebx
        jmp .loop
.found: inc ebx
.done:  pop ecx
        ret

;=======================================================================
; get_config_ptr: EAX = element# (1-based) -> EBX = ptr to config string
;=======================================================================
get_config_ptr:
        push eax
        push ecx
        dec eax
        mov ecx, eax
        mov ebx, elem_configs
        test ecx, ecx
        jz .done
.loop:  cmp byte [ebx], 0
        jne .next
        dec ecx
        jz .found
.next:  inc ebx
        jmp .loop
.found: inc ebx
.done:  pop ecx
        pop eax
        ret

;=======================================================================
; get_fact_ptr: EAX = element# (1-based) -> EBX = first fact line
;=======================================================================
get_fact_ptr:
        push eax
        push ecx
        dec eax
        mov ecx, eax
        shl ecx, 1              ; 2 null-separated entries per element
        mov ebx, elem_facts
        test ecx, ecx
        jz .done
.loop:  cmp byte [ebx], 0
        jne .next
        dec ecx
        jz .found
.next:  inc ebx
        jmp .loop
.found: inc ebx
.done:  pop ecx
        pop eax
        ret

;=======================================================================
; get_fact2_ptr: EAX = element# (1-based) -> EBX = second fact line
;=======================================================================
get_fact2_ptr:
        push eax
        push ecx
        dec eax
        mov ecx, eax
        shl ecx, 1
        inc ecx                 ; second entry
        mov ebx, elem_facts
        test ecx, ecx
        jz .done
.loop:  cmp byte [ebx], 0
        jne .next
        dec ecx
        jz .found
.next:  inc ebx
        jmp .loop
.found: inc ebx
.done:  pop ecx
        pop eax
        ret

;=======================================================================
; uint_to_str: EAX = value, EDI = dest buffer -> null-terminated decimal
;=======================================================================
uint_to_str:
        push eax
        push ebx
        push ecx
        push edx
        push edi
        mov ebx, 10
        xor ecx, ecx
        test eax, eax
        jnz .loop
        mov byte [edi], '0'
        mov byte [edi + 1], 0
        jmp .done
.loop:  test eax, eax
        jz .emit
        xor edx, edx
        div ebx
        add dl, '0'
        push edx
        inc ecx
        jmp .loop
.emit:  test ecx, ecx
        jz .term
        pop edx
        mov [edi], dl
        inc edi
        dec ecx
        jmp .emit
.term:  mov byte [edi], 0
.done:  pop edi
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

;=======================================================================
; DATA SECTION
;=======================================================================

table_layout:
        ; Row 0: H  .................He
        db  1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2
        ; Row 1: Li Be  ..........  B  C  N  O  F  Ne
        db  3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 6, 7, 8, 9,10
        ; Row 2: Na Mg  ..........  Al Si P  S  Cl Ar
        db 11,12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,13,14,15,16,17,18
        ; Row 3: K  Ca  Sc-Zn       Ga Ge As Se Br Kr
        db 19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36
        ; Row 4: Rb Sr  Y-Cd        In Sn Sb Te I  Xe
        db 37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54
        ; Row 5: Cs Ba La* Hf-Hg    Tl Pb Bi Po At Rn
        db 55,56,57,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86
        ; Row 6: Fr Ra Ac**Rf-Og
        db 87,88,89,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118
        ; Row 7: Lanthanides Ce-Lu (cols 2-16)
        db  0, 0,58,59,60,61,62,63,64,65,66,67,68,69,70,71, 0, 0
        ; Row 8: Actinides Th-Lr (cols 2-16)
        db  0, 0,90,91,92,93,94,95,96,97,98,99,100,101,102,103, 0, 0

symbols:
        db "H ","He","Li","Be","B ","C ","N ","O ","F ","Ne"   ;   1-10
        db "Na","Mg","Al","Si","P ","S ","Cl","Ar"             ;  11-18
        db "K ","Ca","Sc","Ti","V ","Cr","Mn","Fe","Co","Ni"   ;  19-28
        db "Cu","Zn","Ga","Ge","As","Se","Br","Kr"             ;  29-36
        db "Rb","Sr","Y ","Zr","Nb","Mo","Tc","Ru","Rh","Pd"   ;  37-46
        db "Ag","Cd","In","Sn","Sb","Te","I ","Xe"             ;  47-54
        db "Cs","Ba","La","Ce","Pr","Nd","Pm","Sm","Eu","Gd"   ;  55-64
        db "Tb","Dy","Ho","Er","Tm","Yb","Lu"                  ;  65-71
        db "Hf","Ta","W ","Re","Os","Ir","Pt","Au","Hg"        ;  72-80
        db "Tl","Pb","Bi","Po","At","Rn"                       ;  81-86
        db "Fr","Ra","Ac","Th","Pa","U ","Np","Pu","Am","Cm"   ;  87-96
        db "Bk","Cf","Es","Fm","Md","No","Lr"                  ;  97-103
        db "Rf","Db","Sg","Bh","Hs","Mt","Ds","Rg","Cn"        ; 104-112
        db "Nh","Fl","Mc","Lv","Ts","Og"                       ; 113-118

elem_category:
        ; H   He
        db CAT_NONMETAL, CAT_NOBLE
        ; Li  Be  B           C           N           O           F           Ne
        db CAT_ALKALI, CAT_ALKALINE, CAT_SEMIMETAL, CAT_NONMETAL
        db CAT_NONMETAL, CAT_NONMETAL, CAT_HALOGEN, CAT_NOBLE
        ; Na  Mg  Al          Si          P           S           Cl          Ar
        db CAT_ALKALI, CAT_ALKALINE, CAT_BASIC, CAT_SEMIMETAL
        db CAT_NONMETAL, CAT_NONMETAL, CAT_HALOGEN, CAT_NOBLE
        ; K   Ca  Sc..Zn (10 transition)
        db CAT_ALKALI, CAT_ALKALINE
        db CAT_TRANS,CAT_TRANS,CAT_TRANS,CAT_TRANS,CAT_TRANS
        db CAT_TRANS,CAT_TRANS,CAT_TRANS,CAT_TRANS,CAT_TRANS
        ; Ga  Ge  As          Se          Br          Kr
        db CAT_BASIC, CAT_SEMIMETAL, CAT_SEMIMETAL, CAT_NONMETAL, CAT_HALOGEN, CAT_NOBLE
        ; Rb  Sr  Y..Cd (10 transition)
        db CAT_ALKALI, CAT_ALKALINE
        db CAT_TRANS,CAT_TRANS,CAT_TRANS,CAT_TRANS,CAT_TRANS
        db CAT_TRANS,CAT_TRANS,CAT_TRANS,CAT_TRANS,CAT_TRANS
        ; In  Sn  Sb          Te          I           Xe
        db CAT_BASIC, CAT_BASIC, CAT_SEMIMETAL, CAT_SEMIMETAL, CAT_HALOGEN, CAT_NOBLE
        ; Cs  Ba  La  (lanthanides Ce-Lu = 15)
        db CAT_ALKALI, CAT_ALKALINE, CAT_LANTHANIDE
        db CAT_LANTHANIDE,CAT_LANTHANIDE,CAT_LANTHANIDE,CAT_LANTHANIDE
        db CAT_LANTHANIDE,CAT_LANTHANIDE,CAT_LANTHANIDE,CAT_LANTHANIDE
        db CAT_LANTHANIDE,CAT_LANTHANIDE,CAT_LANTHANIDE,CAT_LANTHANIDE
        db CAT_LANTHANIDE,CAT_LANTHANIDE
        ; Hf..Hg (9 transition)
        db CAT_TRANS,CAT_TRANS,CAT_TRANS,CAT_TRANS,CAT_TRANS
        db CAT_TRANS,CAT_TRANS,CAT_TRANS,CAT_TRANS
        ; Tl  Pb  Bi          Po          At          Rn
        db CAT_BASIC, CAT_BASIC, CAT_BASIC, CAT_SEMIMETAL, CAT_HALOGEN, CAT_NOBLE
        ; Fr  Ra  Ac  (actinides Th-Lr = 15)
        db CAT_ALKALI, CAT_ALKALINE, CAT_ACTINIDE
        db CAT_ACTINIDE,CAT_ACTINIDE,CAT_ACTINIDE,CAT_ACTINIDE
        db CAT_ACTINIDE,CAT_ACTINIDE,CAT_ACTINIDE,CAT_ACTINIDE
        db CAT_ACTINIDE,CAT_ACTINIDE,CAT_ACTINIDE,CAT_ACTINIDE
        db CAT_ACTINIDE,CAT_ACTINIDE
        ; Rf..Cn (9 transactinides)
        db CAT_TRANS,CAT_TRANS,CAT_TRANS,CAT_TRANS,CAT_TRANS
        db CAT_TRANS,CAT_TRANS,CAT_TRANS,CAT_TRANS
        ; Nh  Fl  Mc  Lv  Ts  Og
        db CAT_BASIC, CAT_BASIC, CAT_BASIC, CAT_BASIC, CAT_HALOGEN, CAT_NOBLE

elem_names:
        db "Hydrogen",0,"Helium",0,"Lithium",0,"Beryllium",0
        db "Boron",0,"Carbon",0,"Nitrogen",0,"Oxygen",0
        db "Fluorine",0,"Neon",0,"Sodium",0,"Magnesium",0
        db "Aluminium",0,"Silicon",0,"Phosphorus",0,"Sulfur",0
        db "Chlorine",0,"Argon",0,"Potassium",0,"Calcium",0
        db "Scandium",0,"Titanium",0,"Vanadium",0,"Chromium",0
        db "Manganese",0,"Iron",0,"Cobalt",0,"Nickel",0
        db "Copper",0,"Zinc",0,"Gallium",0,"Germanium",0
        db "Arsenic",0,"Selenium",0,"Bromine",0,"Krypton",0
        db "Rubidium",0,"Strontium",0,"Yttrium",0,"Zirconium",0
        db "Niobium",0,"Molybdenum",0,"Technetium",0,"Ruthenium",0
        db "Rhodium",0,"Palladium",0,"Silver",0,"Cadmium",0
        db "Indium",0,"Tin",0,"Antimony",0,"Tellurium",0
        db "Iodine",0,"Xenon",0,"Cesium",0,"Barium",0
        db "Lanthanum",0,"Cerium",0,"Praseodymium",0,"Neodymium",0
        db "Promethium",0,"Samarium",0,"Europium",0,"Gadolinium",0
        db "Terbium",0,"Dysprosium",0,"Holmium",0,"Erbium",0
        db "Thulium",0,"Ytterbium",0,"Lutetium",0
        db "Hafnium",0,"Tantalum",0,"Tungsten",0,"Rhenium",0
        db "Osmium",0,"Iridium",0,"Platinum",0,"Gold",0
        db "Mercury",0,"Thallium",0,"Lead",0,"Bismuth",0
        db "Polonium",0,"Astatine",0,"Radon",0
        db "Francium",0,"Radium",0,"Actinium",0,"Thorium",0
        db "Protactinium",0,"Uranium",0,"Neptunium",0,"Plutonium",0
        db "Americium",0,"Curium",0,"Berkelium",0,"Californium",0
        db "Einsteinium",0,"Fermium",0,"Mendelevium",0,"Nobelium",0
        db "Lawrencium",0
        db "Rutherfordium",0,"Dubnium",0,"Seaborgium",0,"Bohrium",0
        db "Hassium",0,"Meitnerium",0,"Darmstadtium",0,"Roentgenium",0
        db "Copernicium",0,"Nihonium",0,"Flerovium",0,"Moscovium",0
        db "Livermorium",0,"Tennessine",0,"Oganesson",0

elem_configs:
        db "1s1",0,"1s2",0
        db "[He] 2s1",0,"[He] 2s2",0
        db "[He] 2s2 2p1",0,"[He] 2s2 2p2",0,"[He] 2s2 2p3",0
        db "[He] 2s2 2p4",0,"[He] 2s2 2p5",0,"[He] 2s2 2p6",0
        db "[Ne] 3s1",0,"[Ne] 3s2",0
        db "[Ne] 3s2 3p1",0,"[Ne] 3s2 3p2",0,"[Ne] 3s2 3p3",0
        db "[Ne] 3s2 3p4",0,"[Ne] 3s2 3p5",0,"[Ne] 3s2 3p6",0
        db "[Ar] 4s1",0,"[Ar] 4s2",0
        db "[Ar] 3d1 4s2",0,"[Ar] 3d2 4s2",0,"[Ar] 3d3 4s2",0
        db "[Ar] 3d5 4s1",0,"[Ar] 3d5 4s2",0,"[Ar] 3d6 4s2",0
        db "[Ar] 3d7 4s2",0,"[Ar] 3d8 4s2",0,"[Ar] 3d10 4s1",0
        db "[Ar] 3d10 4s2",0,"[Ar] 3d10 4s2 4p1",0,"[Ar] 3d10 4s2 4p2",0
        db "[Ar] 3d10 4s2 4p3",0,"[Ar] 3d10 4s2 4p4",0
        db "[Ar] 3d10 4s2 4p5",0,"[Ar] 3d10 4s2 4p6",0
        db "[Kr] 5s1",0,"[Kr] 5s2",0
        db "[Kr] 4d1 5s2",0,"[Kr] 4d2 5s2",0,"[Kr] 4d4 5s1",0
        db "[Kr] 4d5 5s1",0,"[Kr] 4d5 5s2",0,"[Kr] 4d7 5s1",0
        db "[Kr] 4d8 5s1",0,"[Kr] 4d10",0,"[Kr] 4d10 5s1",0
        db "[Kr] 4d10 5s2",0,"[Kr] 4d10 5s2 5p1",0,"[Kr] 4d10 5s2 5p2",0
        db "[Kr] 4d10 5s2 5p3",0,"[Kr] 4d10 5s2 5p4",0
        db "[Kr] 4d10 5s2 5p5",0,"[Kr] 4d10 5s2 5p6",0
        db "[Xe] 6s1",0,"[Xe] 6s2",0,"[Xe] 5d1 6s2",0
        db "[Xe] 4f1 5d1 6s2",0,"[Xe] 4f3 6s2",0,"[Xe] 4f4 6s2",0
        db "[Xe] 4f5 6s2",0,"[Xe] 4f6 6s2",0,"[Xe] 4f7 6s2",0
        db "[Xe] 4f7 5d1 6s2",0,"[Xe] 4f9 6s2",0,"[Xe] 4f10 6s2",0
        db "[Xe] 4f11 6s2",0,"[Xe] 4f12 6s2",0,"[Xe] 4f13 6s2",0
        db "[Xe] 4f14 6s2",0,"[Xe] 4f14 5d1 6s2",0
        db "[Xe] 4f14 5d2 6s2",0,"[Xe] 4f14 5d3 6s2",0
        db "[Xe] 4f14 5d4 6s2",0,"[Xe] 4f14 5d5 6s2",0
        db "[Xe] 4f14 5d6 6s2",0,"[Xe] 4f14 5d7 6s2",0
        db "[Xe] 4f14 5d9 6s1",0,"[Xe] 4f14 5d10 6s1",0
        db "[Xe] 4f14 5d10 6s2",0
        db "[Xe] 4f14 5d10 6s2 6p1",0,"[Xe] 4f14 5d10 6s2 6p2",0
        db "[Xe] 4f14 5d10 6s2 6p3",0,"[Xe] 4f14 5d10 6s2 6p4",0
        db "[Xe] 4f14 5d10 6s2 6p5",0,"[Xe] 4f14 5d10 6s2 6p6",0
        db "[Rn] 7s1",0,"[Rn] 7s2",0,"[Rn] 6d1 7s2",0
        db "[Rn] 6d2 7s2",0,"[Rn] 5f2 6d1 7s2",0,"[Rn] 5f3 6d1 7s2",0
        db "[Rn] 5f4 6d1 7s2",0,"[Rn] 5f6 7s2",0,"[Rn] 5f7 7s2",0
        db "[Rn] 5f7 6d1 7s2",0,"[Rn] 5f9 7s2",0,"[Rn] 5f10 7s2",0
        db "[Rn] 5f11 7s2",0,"[Rn] 5f12 7s2",0,"[Rn] 5f13 7s2",0
        db "[Rn] 5f14 7s2",0,"[Rn] 5f14 7s2 7p1",0
        db "[Rn] 5f14 6d2 7s2",0,"[Rn] 5f14 6d3 7s2",0
        db "[Rn] 5f14 6d4 7s2",0,"[Rn] 5f14 6d5 7s2",0
        db "[Rn] 5f14 6d6 7s2",0,"[Rn] 5f14 6d7 7s2",0
        db "[Rn] 5f14 6d8 7s2",0,"[Rn] 5f14 6d9 7s2",0
        db "[Rn] 5f14 6d10 7s2",0
        db "[Rn] 5f14 6d10 7s2 7p1",0,"[Rn] 5f14 6d10 7s2 7p2",0
        db "[Rn] 5f14 6d10 7s2 7p3",0,"[Rn] 5f14 6d10 7s2 7p4",0
        db "[Rn] 5f14 6d10 7s2 7p5",0,"[Rn] 5f14 6d10 7s2 7p6",0

; Two educational fact lines per element (null-separated, 236 strings total)
elem_facts:
        ; H (1)
        db "Most abundant element in the universe.",0
        db "Used in fuel cells & rocket propellant.",0
        ; He (2)
        db "Second lightest element; inert noble gas.",0
        db "Used in MRI machines and party balloons.",0
        ; Li (3)
        db "Lightest solid metal; highly reactive.",0
        db "Essential in modern lithium-ion batteries.",0
        ; Be (4)
        db "Extremely stiff, very light metal.",0
        db "Used in aerospace alloys and X-ray windows.",0
        ; B (5)
        db "Metalloid important in semiconductors.",0
        db "Boron neutron capture used in cancer therapy.",0
        ; C (6)
        db "Basis of all known organic life.",0
        db "Diamond and graphite are both pure carbon.",0
        ; N (7)
        db "Makes up 78% of Earth's atmosphere.",0
        db "Liquid nitrogen (-196 C) used for cryogenics.",0
        ; O (8)
        db "Essential for aerobic respiration.",0
        db "Most abundant element in Earth's crust.",0
        ; F (9)
        db "Most electronegative element known.",0
        db "Used in Teflon coatings and toothpaste.",0
        ; Ne (10)
        db "Noble gas; glows orange-red in discharge.",0
        db "Used in neon signs and gas lasers.",0
        ; Na (11)
        db "Soft alkali metal; reacts violently with water.",0
        db "Common table salt is sodium chloride (NaCl).",0
        ; Mg (12)
        db "Burns with brilliant white light.",0
        db "Essential mineral in human nutrition.",0
        ; Al (13)
        db "Most abundant metal in Earth's crust.",0
        db "Lightweight; used in aircraft and foil.",0
        ; Si (14)
        db "Foundation of modern semiconductors.",0
        db "Silicon Valley is named after this element.",0
        ; P (15)
        db "Essential for DNA, RNA, and ATP molecules.",0
        db "White phosphorus is dangerously flammable.",0
        ; S (16)
        db "Yellow solid; associated with volcanic areas.",0
        db "Used in gunpowder and vulcanising rubber.",0
        ; Cl (17)
        db "Reactive halogen; pale green-yellow gas.",0
        db "Used to disinfect drinking water worldwide.",0
        ; Ar (18)
        db "Third most abundant gas in the atmosphere.",0
        db "Used inside incandescent light bulbs.",0
        ; K (19)
        db "Vital electrolyte; regulates nerve signals.",0
        db "Symbol K from the Latin word 'Kalium'.",0
        ; Ca (20)
        db "Most abundant mineral in the human body.",0
        db "Essential for bones, teeth, and muscles.",0
        ; Sc (21)
        db "Rare transition metal; silvery-white.",0
        db "Used in aerospace aluminium alloys.",0
        ; Ti (22)
        db "Strong, light, corrosion-resistant metal.",0
        db "Used in jet engines and medical implants.",0
        ; V (23)
        db "Hard metal; high melting point.",0
        db "Used in steel alloys for tool manufacture.",0
        ; Cr (24)
        db "Gives stainless steel its corrosion resistance.",0
        db "Chromium plating adds a bright, hard finish.",0
        ; Mn (25)
        db "Essential trace element found in enzymes.",0
        db "Used in steel-making as a deoxidiser.",0
        ; Fe (26)
        db "Most common element on Earth by mass.",0
        db "Iron oxide (rust) forms when exposed to O2.",0
        ; Co (27)
        db "Hard ferromagnetic metal; makes blue pigments.",0
        db "Vitamin B12 contains a cobalt atom at its core.",0
        ; Ni (28)
        db "Silvery metal; resists corrosion well.",0
        db "Used in stainless steel and coins.",0
        ; Cu (29)
        db "Best electrical conductor of common metals.",0
        db "Used in virtually all electrical wiring.",0
        ; Zn (30)
        db "Protects iron from rust (galvanisation).",0
        db "Essential trace element; in 300+ enzymes.",0
        ; Ga (31)
        db "Melts in your hand (melting point 29.8 C).",0
        db "Used in GaAs semiconductors and LEDs.",0
        ; Ge (32)
        db "First used in transistors in 1947.",0
        db "Semiconductor bridging metals and non-metals.",0
        ; As (33)
        db "Toxic metalloid with a long history as poison.",0
        db "Used today in wood preservatives and LEDs.",0
        ; Se (34)
        db "Essential trace element in antioxidant enzymes.",0
        db "Used in photocopier drums and solar cells.",0
        ; Br (35)
        db "One of only two elements liquid at room temp.",0
        db "Used in flame retardants and photography.",0
        ; Kr (36)
        db "Dense noble gas; used in high-power lasers.",0
        db "Krypton-86 once defined the metre.",0
        ; Rb (37)
        db "Soft alkali metal; ignites on contact with air.",0
        db "Used in atomic clocks and GPS systems.",0
        ; Sr (38)
        db "Strontium gives fireworks their red colour.",0
        db "Sr-90 is a dangerous radioactive fission product.",0
        ; Y (39)
        db "Silvery-white transition metal.",0
        db "Used in LED phosphors and laser crystals.",0
        ; Zr (40)
        db "Highly corrosion-resistant; used in reactors.",0
        db "Zirconia (ZrO2) is used in ceramic knives.",0
        ; Nb (41)
        db "Superconducting at very low temperatures.",0
        db "Used in MRI superconducting magnets.",0
        ; Mo (42)
        db "Has the highest melting point of any transition metal.",0
        db "Used in high-strength steel alloys.",0
        ; Tc (43)
        db "First artificially produced element (1937).",0
        db "Tc-99m is the most used medical radioisotope.",0
        ; Ru (44)
        db "Rare platinum group metal; very hard.",0
        db "Used as a catalyst in the chemical industry.",0
        ; Rh (45)
        db "Rarest and most expensive platinum group metal.",0
        db "Used in catalytic converters for cars.",0
        ; Pd (46)
        db "Can absorb up to 900x its own volume of H2.",0
        db "Used in catalytic converters and jewellery.",0
        ; Ag (47)
        db "Best electrical conductor of all pure metals.",0
        db "Used in jewellery, coins, and photography.",0
        ; Cd (48)
        db "Toxic heavy metal; used in Ni-Cd batteries.",0
        db "Cadmium yellow is a vivid artist's pigment.",0
        ; In (49)
        db "Soft, malleable post-transition metal.",0
        db "ITO (indium tin oxide) is in every touchscreen.",0
        ; Sn (50)
        db "Symbol Sn from the Latin 'Stannum'.",0
        db "Solder (tin alloy) is used to join electronics.",0
        ; Sb (51)
        db "Metalloid used in flame retardants.",0
        db "Known since antiquity as kohl eye cosmetic.",0
        ; Te (52)
        db "Semiconductor metalloid; brittle silver solid.",0
        db "Used in thermoelectric devices and glass.",0
        ; I (53)
        db "Deep purple solid; sublimes at room temperature.",0
        db "Essential for thyroid hormone production.",0
        ; Xe (54)
        db "Heavy noble gas; used in ion thrusters.",0
        db "Xenon flash lamps are used in photography.",0
        ; Cs (55)
        db "Most electropositive stable element.",0
        db "Cesium atomic clocks define the SI second.",0
        ; Ba (56)
        db "Dense alkaline-earth metal; highly reactive.",0
        db "Barium meal used in medical X-ray imaging.",0
        ; La (57)
        db "First lanthanide; used in camera lenses.",0
        db "La2O3 significantly improves optical glass.",0
        ; Ce (58)
        db "Most abundant rare-earth element.",0
        db "Used in catalytic converters and glass.",0
        ; Pr (59)
        db "Gives a vivid green colour to glass.",0
        db "Used in powerful neodymium-type magnets.",0
        ; Nd (60)
        db "Nd-Fe-B magnets are the strongest known.",0
        db "Used in hard drives and electric motors.",0
        ; Pm (61)
        db "Only radioactive non-primordial lanthanide.",0
        db "Used in nuclear-powered pacemakers in 1960s.",0
        ; Sm (62)
        db "Used in samarium-cobalt permanent magnets.",0
        db "Sm-153 is used in cancer radiotherapy.",0
        ; Eu (63)
        db "Most reactive rare-earth metal.",0
        db "Europium phosphors produce red in TV screens.",0
        ; Gd (64)
        db "Highest magnetic moment of any element.",0
        db "Used as MRI contrast agent (Gd chelates).",0
        ; Tb (65)
        db "Terbium produces vivid green phosphors.",0
        db "Used in solid oxide fuel cells.",0
        ; Dy (66)
        db "Added to Nd magnets to retain strength at heat.",0
        db "Named after Greek 'dysprositos' (hard to get).",0
        ; Ho (67)
        db "Highest magnetic moment of any natural atom.",0
        db "Used in nuclear reactor control rods.",0
        ; Er (68)
        db "Er-doped fibre amplifiers power the internet.",0
        db "Erbium gives glass and porcelain a pink tint.",0
        ; Tm (69)
        db "Second rarest stable lanthanide.",0
        db "Used in portable X-ray devices.",0
        ; Yb (70)
        db "Used in high-precision optical atomic clocks.",0
        db "Ytterbium lasers cut metal with precision.",0
        ; Lu (71)
        db "Hardest and densest lanthanide.",0
        db "Used in PET scanner scintillator detectors.",0
        ; Hf (72)
        db "Absorbs neutrons; used in reactor control rods.",0
        db "Nearly identical chemically to zirconium.",0
        ; Ta (73)
        db "Extremely corrosion-resistant metal.",0
        db "Used in capacitors and surgical instruments.",0
        ; W (74)
        db "Highest melting point of any pure metal (3422 C).",0
        db "Used in incandescent bulb filaments.",0
        ; Re (75)
        db "Second highest melting point of all elements.",0
        db "Used in jet engine turbine blade alloys.",0
        ; Os (76)
        db "Densest naturally occurring element.",0
        db "Osmium tetroxide vapour is highly toxic.",0
        ; Ir (77)
        db "Most corrosion-resistant metal known.",0
        db "K-Pg boundary layer is enriched with iridium.",0
        ; Pt (78)
        db "Precious metal; excellent catalyst.",0
        db "Catalytic converters use platinum group metals.",0
        ; Au (79)
        db "Has been prized since antiquity; does not tarnish.",0
        db "All gold ever mined fits in a 21 metre cube.",0
        ; Hg (80)
        db "The only pure metal liquid at room temperature.",0
        db "Used in thermometers and fluorescent lights.",0
        ; Tl (81)
        db "Highly toxic; once used as rat and ant poison.",0
        db "Thallium compounds are colourless and odourless.",0
        ; Pb (82)
        db "Dense, soft metal; historically used in pipes.",0
        db "Lead poisoning causes serious brain damage.",0
        ; Bi (83)
        db "Heaviest effectively-stable element.",0
        db "Used in cosmetics, medicines, and low-mp alloys.",0
        ; Po (84)
        db "Highly radioactive; discovered by Marie Curie.",0
        db "Po-210 was used to poison Alexander Litvinenko.",0
        ; At (85)
        db "Rarest naturally occurring element on Earth.",0
        db "Less than 1 gram is estimated to exist in crust.",0
        ; Rn (86)
        db "Radioactive noble gas that seeps from rock.",0
        db "Second leading cause of lung cancer after smoking.",0
        ; Fr (87)
        db "Most unstable naturally occurring element.",0
        db "Longest-lived isotope has a half-life of 22 min.",0
        ; Ra (88)
        db "Discovered by Marie and Pierre Curie in 1898.",0
        db "Radium dial painters suffered radiation sickness.",0
        ; Ac (89)
        db "First non-primordial element to be isolated.",0
        db "Ac-225 used in targeted alpha cancer therapy.",0
        ; Th (90)
        db "More abundant in crust than uranium.",0
        db "Thorium molten salt reactors are being explored.",0
        ; Pa (91)
        db "Radioactive; extremely rare in nature.",0
        db "Decays via alpha to actinium and to uranium.",0
        ; U (92)
        db "Primary fuel for nuclear fission power plants.",0
        db "Depleted uranium is used in armour-piercing shells.",0
        ; Np (93)
        db "First transuranic element ever discovered (1940).",0
        db "Neptunium-237 is the most stable isotope.",0
        ; Pu (94)
        db "Used in nuclear weapons and power reactors.",0
        db "Pu-238 powers deep-space probes via RTGs.",0
        ; Am (95)
        db "Used in household ionisation smoke detectors.",0
        db "Am-241 emits alpha particles for ionisation.",0
        ; Cm (96)
        db "Named after Marie and Pierre Curie.",0
        db "Produced in nuclear reactors from plutonium.",0
        ; Bk (97)
        db "Named after Berkeley, California.",0
        db "Only microgram quantities have ever been made.",0
        ; Cf (98)
        db "Used for neutron startup sources in reactors.",0
        db "Californium-252 neutrons can image inside objects.",0
        ; Es (99)
        db "Discovered in debris of the first H-bomb test.",0
        db "Named in honour of Albert Einstein.",0
        ; Fm (100)
        db "Named after Enrico Fermi.",0
        db "No stable or long-lived isotopes exist.",0
        ; Md (101)
        db "Named after Dmitri Mendeleev.",0
        db "First element produced one atom at a time.",0
        ; No (102)
        db "Named after Alfred Nobel.",0
        db "Most stable isotope has a half-life of 58 min.",0
        ; Lr (103)
        db "Last member of the actinide series.",0
        db "Named after Ernest O. Lawrence.",0
        ; Rf (104)
        db "First transactinide (superheavy) element.",0
        db "Named after Ernest Rutherford.",0
        ; Db (105)
        db "Synthesised independently in US and USSR.",0
        db "Named after Dubna, Russia.",0
        ; Sg (106)
        db "Named after Glenn T. Seaborg.",0
        db "Heaviest element named while discoverer lived.",0
        ; Bh (107)
        db "Named after Niels Bohr.",0
        db "Only 37 atoms of Bh-272 were ever synthesised.",0
        ; Hs (108)
        db "Named after the German state of Hesse.",0
        db "Most stable isotope Hs-270 has t1/2 = 9 s.",0
        ; Mt (109)
        db "Named after Lise Meitner.",0
        db "Properties largely predicted from theory.",0
        ; Ds (110)
        db "Named after Darmstadt, Germany.",0
        db "First synthesised at GSI Darmstadt in 1994.",0
        ; Rg (111)
        db "Named after Wilhelm Rontgen.",0
        db "Only a few atoms have ever been observed.",0
        ; Cn (112)
        db "Named after Nicolaus Copernicus.",0
        db "Relativistic effects give it unusual properties.",0
        ; Nh (113)
        db "First element discovered in Asia (RIKEN, Japan).",0
        db "Named after Nihon (Japanese word for Japan).",0
        ; Fl (114)
        db "Named after Flerov Laboratory, Russia.",0
        db "Predicted to behave more like a noble gas.",0
        ; Mc (115)
        db "Named after Moscow Oblast, Russia.",0
        db "Most stable isotope Mc-290 has t1/2 = 0.65 s.",0
        ; Lv (116)
        db "Named after Lawrence Livermore National Lab.",0
        db "Predicted to be a liquid at room temperature.",0
        ; Ts (117)
        db "Named after Tennessee, USA.",0
        db "Only a handful of tennessine atoms created.",0
        ; Og (118)
        db "Heaviest known element; named after Oganessian.",0
        db "Predicted to behave unlike any other noble gas.",0

; Category names table (indexed by CAT_*)
cat_names_tbl:
        dd cat_name_none        ; 0
        dd cat_name_alkali      ; 1
        dd cat_name_alkaline    ; 2
        dd cat_name_trans       ; 3
        dd cat_name_basic       ; 4
        dd cat_name_semimetal   ; 5
        dd cat_name_nonmetal    ; 6
        dd cat_name_halogen     ; 7
        dd cat_name_noble       ; 8
        dd cat_name_lanthanide  ; 9
        dd cat_name_actinide    ; 10

cat_name_none:      db "-",0
cat_name_alkali:    db "Alkali Metal",0
cat_name_alkaline:  db "Alkaline-Earth Metal",0
cat_name_trans:     db "Transition Metal",0
cat_name_basic:     db "Post-Transition Metal",0
cat_name_semimetal: db "Metalloid",0
cat_name_nonmetal:  db "Reactive Non-Metal",0
cat_name_halogen:   db "Halogen",0
cat_name_noble:     db "Noble Gas",0
cat_name_lanthanide:db "Lanthanide",0
cat_name_actinide:  db "Actinide",0

; Legend label pointers array (index 0 = CAT_ALKALI = 1)
cat_leg_labels:
        dd leg_alkali           ; index 0 → CAT_ALKALI
        dd leg_alkaline         ; index 1 → CAT_ALKALINE
        dd leg_trans            ; index 2 → CAT_TRANS
        dd leg_basic            ; index 3 → CAT_BASIC
        dd leg_semimetal        ; index 4 → CAT_SEMIMETAL
        dd leg_nonmetal         ; index 5 → CAT_NONMETAL
        dd leg_halogen          ; index 6 → CAT_HALOGEN
        dd leg_noble            ; index 7 → CAT_NOBLE
        dd leg_lanthanide       ; index 8 → CAT_LANTHANIDE
        dd leg_actinide         ; index 9 → CAT_ACTINIDE

leg_alkali:     db "Alkali Metal",0
leg_alkaline:   db "Alkaline-Earth",0
leg_trans:      db "Transition Metal",0
leg_basic:      db "Post-Transition",0
leg_semimetal:  db "Metalloid",0
leg_nonmetal:   db "Non-Metal",0
leg_halogen:    db "Halogen",0
leg_noble:      db "Noble Gas",0
leg_lanthanide: db "Lanthanide",0
leg_actinide:   db "Actinide",0

; Strings
win_title:  db "Periodic Table of the Elements - Mellivora OS",0
title_str:  db "  Periodic Table of the Elements  |  118 Elements  |  Q/Esc to exit",0
hdr_info_str: db "Element Information",0
lbl_sym:    db "Symbol:",0
lbl_anum:   db "Atomic #:",0
lbl_cat:    db "Category:",0
lbl_config: db "Electron Config:",0
lbl_fact:   db "Did you know?",0
lbl_legend: db "Legend:",0
nav_hint:   db "Arrow keys or click to explore",0
no_elem_str:db "(no element selected)",0

; BSS
win_id:         dd 0
sel_col:        dd 0
sel_row:        dd 0
dg_row:         dd 0
dg_col:         dd 0
dg_px:          dd 0
dg_py:          dd 0
dg_elem:        dd 0
dg_sel:         dd 0
dg_color:       dd 0
dg_numbuf:      times 8 db 0
dg_symbuf:      times 4 db 0
dip_elem:       dd 0
dip_col_tmp:    dd 0
dip_ptr_tmp:    dd 0
leg_x:          dd 0
leg_idx:        dd 0
leg_col_tmp:    dd 0
sym_buf:        times 4 db 0
tmp_buf:        times 12 db 0
