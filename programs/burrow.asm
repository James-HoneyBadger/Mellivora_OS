;=======================================================================
; burrow.asm - HB Burrow - File Manager TUI for Mellivora OS
;
; A Midnight Commander-style dual-pane file browser.
;
; Controls:
;   Up/Down     Navigate file list
;   Tab         Switch active panel
;   Enter       Open directory / run program
;   F3          View file (inline viewer)
;   F5          Copy file to other panel
;   F7          Create new directory
;   F8          Delete file (with confirmation)
;   Ctrl+R      Refresh panels
;   Ctrl+Q / q  Quit
;
; Layout (80x25):
;   Row  0      Title bar
;   Row  1      Panel path headers
;   Row  2      Separator line
;   Rows 3-22   File listings (20 visible entries)
;   Row  23     Info bar (selected file details)
;   Row  24     Function key bar
;=======================================================================

%include "syscalls.inc"
%include "lib/string.inc"
%include "lib/io.inc"
%include "lib/vga.inc"
%include "lib/mem.inc"

;-----------------------------------------------------------------------
; Layout constants
;-----------------------------------------------------------------------
LEFT_X          equ 0
RIGHT_X         equ 40
PANEL_W         equ 40
LIST_Y          equ 3
LIST_ROWS       equ 20
MAX_ENTRIES     equ 200
FNAME_SZ        equ 32

;-----------------------------------------------------------------------
; Colors (classic MC blue)
;-----------------------------------------------------------------------
C_NORMAL        equ 0x17        ; White on blue
C_SELECT        equ 0x30        ; Black on cyan
C_DIR           equ 0x1F        ; Bright white on blue
C_EXEC          equ 0x1A        ; Bright green on blue
C_SDIR          equ 0x3F        ; Bright white on cyan (selected dir)
C_SEXEC         equ 0x3A        ; Bright green on cyan (selected exec)
C_HDR           equ 0x1E        ; Yellow on blue
C_BORDER        equ 0x19        ; Bright blue on blue
C_TITLE         equ 0x70        ; Black on gray
C_INFO          equ 0x30        ; Black on cyan
C_FKEY          equ 0x70        ; Black on gray
C_FNUM          equ 0x07        ; Gray on black
C_DIALOG        equ 0x4F        ; White on red

;-----------------------------------------------------------------------
; Key codes
;-----------------------------------------------------------------------
K_TAB           equ 0x09
K_ENTER         equ 0x0D
K_CTRLQ         equ 0x11
K_CTRLR         equ 0x12
K_F3            equ 0x86
K_F5            equ 0x88
K_F7            equ 0x8A
K_F8            equ 0x8B

;=======================================================================
; Entry Point
;=======================================================================
start:
        mov edi, saved_cwd
        call io_dir_getcwd

        ; Left panel = current dir
        mov edi, l_cwd
        call io_dir_getcwd
        call load_left

        ; Right panel = root
        mov esi, s_root
        call io_dir_change
        mov edi, r_cwd
        call io_dir_getcwd
        call load_right

        ; Restore to left panel dir
        mov esi, l_cwd
        call io_dir_change

        mov byte [active], 0
        call full_redraw

;-----------------------------------------------------------------------
; Main loop
;-----------------------------------------------------------------------
main_loop:
        call draw_files
        call draw_info
        call place_cursor

        mov eax, SYS_GETCHAR
        int 0x80

        cmp al, K_CTRLQ
        je quit
        cmp al, 'q'
        je quit
        cmp al, KEY_UP
        je key_up
        cmp al, KEY_DOWN
        je key_down
        cmp al, K_TAB
        je key_tab
        cmp al, K_ENTER
        je key_enter
        cmp al, K_CTRLR
        je key_refresh
        cmp al, K_F3
        je key_f3
        cmp al, K_F5
        je key_f5
        cmp al, K_F7
        je key_f7
        cmp al, K_F8
        je key_f8
        jmp main_loop

quit:
        mov esi, saved_cwd
        call io_dir_change
        call io_clear
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

key_up:
        call cur_ptr
        cmp dword [eax], 0
        je main_loop
        dec dword [eax]
        call scr_ptr
        mov ebx, [eax]
        call cur_ptr
        mov ecx, [eax]
        cmp ecx, ebx
        jge main_loop
        call scr_ptr
        mov [eax], ecx
        jmp main_loop

key_down:
        call cur_ptr
        mov ecx, [eax]
        call cnt_ptr
        mov edx, [eax]
        inc ecx
        cmp ecx, edx
        jge main_loop
        call cur_ptr
        mov [eax], ecx
        call scr_ptr
        mov ebx, [eax]
        add ebx, LIST_ROWS
        cmp ecx, ebx
        jl main_loop
        call scr_ptr
        inc dword [eax]
        jmp main_loop

key_tab:
        xor byte [active], 1
        call draw_headers
        jmp main_loop

key_enter:
        call do_enter
        jmp main_loop

key_refresh:
        call reload_both
        call full_redraw
        jmp main_loop

key_f3:
        call do_view
        call full_redraw
        jmp main_loop

key_f5:
        call do_copy
        call full_redraw
        jmp main_loop

key_f7:
        call do_mkdir
        call full_redraw
        jmp main_loop

key_f8:
        call do_delete
        call full_redraw
        jmp main_loop

;=======================================================================
; Panel field helpers — return ptr in EAX
;=======================================================================
cur_ptr:
        cmp byte [active], 0
        je .l
        lea eax, [r_cursor]
        ret
.l:     lea eax, [l_cursor]
        ret

scr_ptr:
        cmp byte [active], 0
        je .l
        lea eax, [r_scroll]
        ret
.l:     lea eax, [l_scroll]
        ret

cnt_ptr:
        cmp byte [active], 0
        je .l
        lea eax, [r_count]
        ret
.l:     lea eax, [l_count]
        ret

cwd_ptr:
        cmp byte [active], 0
        je .l
        lea eax, [r_cwd]
        ret
.l:     lea eax, [l_cwd]
        ret

other_cwd:
        cmp byte [active], 0
        jne .l
        lea eax, [r_cwd]
        ret
.l:     lea eax, [l_cwd]
        ret

;=======================================================================
; get_sel_name — put selected filename in tmp_name, ESI = tmp_name
;=======================================================================
get_sel_name:
        pushad
        call cur_ptr
        mov ecx, [eax]
        cmp byte [active], 0
        je .l
        lea eax, [r_names]
        jmp .go
.l:     lea eax, [l_names]
.go:    imul ecx, FNAME_SZ
        add eax, ecx
        mov esi, eax
        mov edi, tmp_name
        call str_copy
        popad
        mov esi, tmp_name
        ret

;=======================================================================
; get_sel_type — return file type in AL
;=======================================================================
get_sel_type:
        push ebx
        call cur_ptr
        mov ebx, [eax]
        cmp byte [active], 0
        je .l
        movzx eax, byte [r_types + ebx]
        pop ebx
        ret
.l:     movzx eax, byte [l_types + ebx]
        pop ebx
        ret

;=======================================================================
; load_left / load_right — scan directory
;=======================================================================
load_left:
        pushad
        mov esi, l_cwd
        call io_dir_change
        lea eax, [l_names]
        mov [_lp_names], eax
        lea eax, [l_types]
        mov [_lp_types], eax
        lea eax, [l_sizes]
        mov [_lp_sizes], eax
        lea eax, [l_count]
        mov [_lp_count], eax
        lea eax, [l_cwd]
        mov [_lp_cwd], eax
        call load_panel_impl
        mov dword [l_cursor], 0
        mov dword [l_scroll], 0
        popad
        ret

load_right:
        pushad
        mov esi, r_cwd
        call io_dir_change
        lea eax, [r_names]
        mov [_lp_names], eax
        lea eax, [r_types]
        mov [_lp_types], eax
        lea eax, [r_sizes]
        mov [_lp_sizes], eax
        lea eax, [r_count]
        mov [_lp_count], eax
        lea eax, [r_cwd]
        mov [_lp_cwd], eax
        call load_panel_impl
        mov dword [r_cursor], 0
        mov dword [r_scroll], 0
        popad
        ret

;-----------------------------------------------------------------------
; load_panel_impl — uses _lp_* variables
;-----------------------------------------------------------------------
load_panel_impl:
        pushad
        mov dword [_lp_n], 0

        ; Add ".." unless at root
        mov esi, [_lp_cwd]
        cmp byte [esi], '/'
        jne .scan
        cmp byte [esi + 1], 0
        je .scan
        mov edi, [_lp_names]
        mov byte [edi], '.'
        mov byte [edi + 1], '.'
        mov byte [edi + 2], 0
        mov edi, [_lp_types]
        mov byte [edi], FTYPE_DIR
        mov edi, [_lp_sizes]
        mov dword [edi], 0
        mov dword [_lp_n], 1

.scan:
        xor esi, esi                    ; dir index
.next:
        mov edi, tmp_name
        mov ecx, esi
        push esi
        call io_dir_read
        pop esi
        cmp eax, -1
        je .done
        cmp eax, 0
        je .skip

        push esi
        push eax                        ; type
        push ecx                        ; size

        mov edx, [_lp_n]
        cmp edx, MAX_ENTRIES
        jge .pop_done

        ; Copy name
        imul edi, edx, FNAME_SZ
        add edi, [_lp_names]
        push edi
        mov esi, tmp_name
        mov ecx, FNAME_SZ - 1
.cpn:   lodsb
        cmp al, 0
        je .pad
        stosb
        dec ecx
        jnz .cpn
.pad:   mov byte [edi], 0

        pop edi                         ; discard saved edi
        pop ecx                         ; size
        pop eax                         ; type

        ; Store type
        mov edi, [_lp_types]
        mov [edi + edx], al

        ; Store size
        mov edi, [_lp_sizes]
        mov [edi + edx * 4], ecx

        inc dword [_lp_n]
        pop esi
.skip:
        inc esi
        jmp .next

.pop_done:
        pop ecx
        pop eax
        pop esi
.done:
        mov eax, [_lp_count]
        mov ecx, [_lp_n]
        mov [eax], ecx
        popad
        ret

;=======================================================================
; reload_both / reload_active
;=======================================================================
reload_both:
        pushad
        call load_left
        call load_right
        call cwd_ptr
        mov esi, eax
        call io_dir_change
        popad
        ret

reload_active:
        cmp byte [active], 0
        je load_left
        jmp load_right

;=======================================================================
; full_redraw
;=======================================================================
full_redraw:
        pushad
        call vga_hide_cursor

        ; Blue background
        mov ebx, 0
        mov ecx, 0
        mov edx, 80
        mov esi, 25
        mov al, ' '
        mov ah, C_NORMAL
        call vga_draw_filled

        ; Title bar
        mov esi, s_title
        mov ecx, 0
        mov dl, C_TITLE
        call vga_status_bar

        ; Left box (rows 1-22)
        mov ebx, LEFT_X
        mov ecx, 1
        mov edx, PANEL_W
        mov esi, 22
        mov ah, C_BORDER
        call vga_draw_box

        ; Right box
        mov ebx, RIGHT_X
        mov ecx, 1
        mov edx, PANEL_W
        mov esi, 22
        mov ah, C_BORDER
        call vga_draw_box

        ; Header separator lines (row 2)
        mov ebx, 1
        mov ecx, 2
        mov edx, 38
        mov al, BOX_H
        mov ah, C_BORDER
        call vga_draw_hline

        mov ebx, 41
        mov ecx, 2
        mov edx, 38
        mov al, BOX_H
        mov ah, C_BORDER
        call vga_draw_hline

        call draw_headers
        call draw_fkeys
        popad
        ret

;=======================================================================
; draw_headers — paths in row 1
;=======================================================================
draw_headers:
        pushad
        ; Clear header area
        mov ebx, 1
        mov ecx, 1
        mov edx, 38
        mov esi, 1
        mov al, ' '
        mov ah, C_NORMAL
        call vga_draw_filled
        mov ebx, 41
        mov ecx, 1
        mov edx, 38
        mov esi, 1
        mov al, ' '
        mov ah, C_NORMAL
        call vga_draw_filled

        ; Left path
        mov esi, l_cwd
        mov ebx, 2
        mov ecx, 1
        cmp byte [active], 0
        jne .l_off
        mov dl, C_INFO
        jmp .l_draw
.l_off: mov dl, C_HDR
.l_draw:
        call vga_write_color

        ; Right path
        mov esi, r_cwd
        mov ebx, 42
        mov ecx, 1
        cmp byte [active], 1
        jne .r_off
        mov dl, C_INFO
        jmp .r_draw
.r_off: mov dl, C_HDR
.r_draw:
        call vga_write_color
        popad
        ret

;=======================================================================
; draw_files — render both panels' file lists
;=======================================================================
draw_files:
        pushad

        ;--- Left panel ---
        mov ebx, 1
        mov ecx, LIST_Y
        mov edx, 38
        mov esi, LIST_ROWS
        mov al, ' '
        mov ah, C_NORMAL
        call vga_draw_filled

        xor edi, edi
.lrow:
        cmp edi, LIST_ROWS
        jge .right

        mov eax, [l_scroll]
        add eax, edi
        cmp eax, [l_count]
        jge .right

        ; Build display line
        mov [_df_idx], eax
        mov dword [_df_side], 0         ; 0 = left
        call build_line
        call pick_color

        ; Draw it
        mov esi, line_buf
        mov ebx, 1
        mov ecx, edi
        add ecx, LIST_Y
        mov dl, [_df_color]
        call vga_write_color

        inc edi
        jmp .lrow

.right:
        ;--- Right panel ---
        mov ebx, 41
        mov ecx, LIST_Y
        mov edx, 38
        mov esi, LIST_ROWS
        mov al, ' '
        mov ah, C_NORMAL
        call vga_draw_filled

        xor edi, edi
.rrow:
        cmp edi, LIST_ROWS
        jge .done

        mov eax, [r_scroll]
        add eax, edi
        cmp eax, [r_count]
        jge .done

        mov [_df_idx], eax
        mov dword [_df_side], 1
        call build_line
        call pick_color

        mov esi, line_buf
        mov ebx, 41
        mov ecx, edi
        add ecx, LIST_Y
        mov dl, [_df_color]
        call vga_write_color

        inc edi
        jmp .rrow

.done:
        popad
        ret

;-----------------------------------------------------------------------
; build_line — build 38-char display string in line_buf
; Input: [_df_idx] = file index, [_df_side] = 0/1
;-----------------------------------------------------------------------
build_line:
        pushad

        ; Fill line_buf with 38 spaces + null
        mov edi, line_buf
        mov ecx, 38
        mov al, ' '
        rep stosb
        mov byte [line_buf + 38], 0

        ; Get name pointer
        mov eax, [_df_idx]
        imul eax, FNAME_SZ
        cmp dword [_df_side], 0
        jne .bl_rn
        add eax, l_names
        jmp .bl_cpn
.bl_rn: add eax, r_names

        ; Copy name to line_buf (max 29 chars)
.bl_cpn:
        mov esi, eax
        mov edi, line_buf
        mov ecx, 29
.bl_cp: lodsb
        cmp al, 0
        je .bl_rcol
        stosb
        dec ecx
        jnz .bl_cp

.bl_rcol:
        ; Get type
        mov eax, [_df_idx]
        cmp dword [_df_side], 0
        jne .bl_rt
        movzx edx, byte [l_types + eax]
        jmp .bl_chk
.bl_rt: movzx edx, byte [r_types + eax]

.bl_chk:
        cmp edx, FTYPE_DIR
        jne .bl_sz

        ; Show <DIR>
        mov byte [line_buf + 31], '<'
        mov byte [line_buf + 32], 'D'
        mov byte [line_buf + 33], 'I'
        mov byte [line_buf + 34], 'R'
        mov byte [line_buf + 35], '>'
        jmp .bl_done

.bl_sz:
        ; Get size
        mov eax, [_df_idx]
        cmp dword [_df_side], 0
        jne .bl_rs
        mov eax, [l_sizes + eax * 4]
        jmp .bl_fmt
.bl_rs: mov eax, [r_sizes + eax * 4]

.bl_fmt:
        ; Convert to decimal in size_buf
        call uint_to_buf

        ; Measure length
        mov esi, size_buf
        xor ecx, ecx
.bl_ml: cmp byte [esi + ecx], 0
        je .bl_rj
        inc ecx
        jmp .bl_ml

        ; Right-justify in cols 30-37 of line_buf
.bl_rj: mov edx, 38
        sub edx, ecx                    ; start offset
        cmp edx, 30
        jge .bl_cpsz
        mov edx, 30
.bl_cpsz:
        mov esi, size_buf
        lea edi, [line_buf + edx]
.bl_cs: lodsb
        cmp al, 0
        je .bl_done
        stosb
        jmp .bl_cs

.bl_done:
        popad
        ret

;-----------------------------------------------------------------------
; pick_color — determine color for entry
; Input: [_df_idx], [_df_side]
; Output: [_df_color]
;-----------------------------------------------------------------------
pick_color:
        pushad

        ; Get type
        mov eax, [_df_idx]
        cmp dword [_df_side], 0
        jne .pc_rt
        movzx edx, byte [l_types + eax]
        jmp .pc_sel
.pc_rt: movzx edx, byte [r_types + eax]

.pc_sel:
        ; Is this the selected entry?
        mov ecx, [_df_idx]
        cmp dword [_df_side], 0
        jne .pc_rsel
        cmp byte [active], 0
        jne .pc_nosela
        cmp ecx, [l_cursor]
        je .pc_is_sel
.pc_nosela:
        jmp .pc_nosel
.pc_rsel:
        cmp byte [active], 1
        jne .pc_nosel
        cmp ecx, [r_cursor]
        je .pc_is_sel

.pc_nosel:
        ; Not selected — color by type
        mov al, C_NORMAL
        cmp edx, FTYPE_DIR
        jne .pc_ne
        mov al, C_DIR
.pc_ne: cmp edx, FTYPE_EXEC
        jne .pc_nb
        mov al, C_EXEC
.pc_nb: cmp edx, FTYPE_BATCH
        jne .pc_store
        mov al, C_EXEC
        jmp .pc_store

.pc_is_sel:
        ; Selected — highlight color by type
        mov al, C_SELECT
        cmp edx, FTYPE_DIR
        jne .pc_sne
        mov al, C_SDIR
.pc_sne:
        cmp edx, FTYPE_EXEC
        jne .pc_snb
        mov al, C_SEXEC
.pc_snb:
        cmp edx, FTYPE_BATCH
        jne .pc_store
        mov al, C_SEXEC

.pc_store:
        mov [_df_color], al
        popad
        ret

;-----------------------------------------------------------------------
; uint_to_buf — EAX -> decimal string in size_buf
;-----------------------------------------------------------------------
uint_to_buf:
        pushad
        mov edi, size_buf
        cmp eax, 0
        jne .nz
        mov byte [edi], '0'
        mov byte [edi + 1], 0
        popad
        ret
.nz:
        xor ecx, ecx
        mov ebx, 10
.push:  xor edx, edx
        div ebx
        push edx
        inc ecx
        cmp eax, 0
        jne .push
.pop:   pop edx
        add dl, '0'
        mov [edi], dl
        inc edi
        dec ecx
        jnz .pop
        mov byte [edi], 0
        popad
        ret

;=======================================================================
; draw_info — selected file info on row 23
;=======================================================================
draw_info:
        pushad
        mov esi, s_blank
        mov ecx, 23
        mov dl, C_INFO
        call vga_status_bar

        call cnt_ptr
        cmp dword [eax], 0
        je .done

        ; Filename
        call get_sel_name
        mov ebx, 1
        mov ecx, 23
        mov dl, C_INFO
        call vga_write_color

        ; Type label
        call get_sel_type
        mov esi, s_t_text
        cmp al, FTYPE_TEXT
        je .ty
        mov esi, s_t_dir
        cmp al, FTYPE_DIR
        je .ty
        mov esi, s_t_exec
        cmp al, FTYPE_EXEC
        je .ty
        mov esi, s_t_bat
        cmp al, FTYPE_BATCH
        je .ty
        mov esi, s_t_unk
.ty:
        mov ebx, 45
        mov ecx, 23
        mov dl, C_INFO
        call vga_write_color

        ; Size
        call cur_ptr
        mov ecx, [eax]
        cmp byte [active], 0
        je .szl
        mov eax, [r_sizes + ecx * 4]
        jmp .szfmt
.szl:   mov eax, [l_sizes + ecx * 4]
.szfmt:
        call uint_to_buf
        mov esi, size_buf
        mov ebx, 60
        mov ecx, 23
        mov dl, C_INFO
        call vga_write_color

        mov esi, s_bytes
        mov ebx, 69
        mov ecx, 23
        mov dl, C_INFO
        call vga_write_color

.done:  popad
        ret

;=======================================================================
; draw_fkeys — row 24
;=======================================================================
draw_fkeys:
        pushad
        mov esi, s_blank
        mov ecx, 24
        mov dl, C_FKEY
        call vga_status_bar

        mov ebx, 3
        mov ecx, 24
        mov esi, s_f3n
        mov dl, C_FNUM
        call vga_write_color
        inc ebx
        mov esi, s_f3l
        mov dl, C_FKEY
        call vga_write_color

        add ebx, 5
        mov esi, s_f5n
        mov dl, C_FNUM
        call vga_write_color
        inc ebx
        mov esi, s_f5l
        mov dl, C_FKEY
        call vga_write_color

        add ebx, 5
        mov esi, s_f7n
        mov dl, C_FNUM
        call vga_write_color
        inc ebx
        mov esi, s_f7l
        mov dl, C_FKEY
        call vga_write_color

        add ebx, 6
        mov esi, s_f8n
        mov dl, C_FNUM
        call vga_write_color
        inc ebx
        mov esi, s_f8l
        mov dl, C_FKEY
        call vga_write_color

        add ebx, 5
        mov esi, s_fqn
        mov dl, C_FNUM
        call vga_write_color
        inc ebx
        mov esi, s_fql
        mov dl, C_FKEY
        call vga_write_color

        popad
        ret

;=======================================================================
; place_cursor
;=======================================================================
place_cursor:
        pushad
        call vga_show_cursor
        call cur_ptr
        mov ecx, [eax]
        call scr_ptr
        sub ecx, [eax]
        add ecx, LIST_Y
        cmp byte [active], 0
        je .left
        mov ebx, 41
        jmp .set
.left:  mov ebx, 1
.set:   call vga_set_cursor
        popad
        ret

;=======================================================================
; do_enter — open dir or run/view
;=======================================================================
do_enter:
        pushad
        call cnt_ptr
        cmp dword [eax], 0
        je .ret

        call get_sel_type
        cmp al, FTYPE_DIR
        je .dir
        cmp al, FTYPE_EXEC
        je .exec
        ; Text/batch — view
        call do_view_impl
        call full_redraw
        jmp .ret

.dir:
        call cwd_ptr
        mov esi, eax
        call io_dir_change
        call get_sel_name
        call io_dir_change
        cmp eax, -1
        je .ret
        call cwd_ptr
        mov edi, eax
        call io_dir_getcwd
        call reload_active
        call full_redraw
        jmp .ret

.exec:
        call cwd_ptr
        mov esi, eax
        call io_dir_change
        call get_sel_name
        call io_clear
        mov eax, SYS_EXEC
        mov ebx, tmp_name
        int 0x80
        ; If we get here, exec failed — redraw
        call full_redraw

.ret:   popad
        ret

;=======================================================================
; do_view — F3 / Enter on text: inline viewer
;=======================================================================
do_view:
        pushad
        call cnt_ptr
        cmp dword [eax], 0
        je .ret
        call get_sel_type
        cmp al, FTYPE_DIR
        je .ret
        call do_view_impl
.ret:   popad
        ret

do_view_impl:
        pushad
        call cwd_ptr
        mov esi, eax
        call io_dir_change

        call get_sel_name
        call io_clear

        mov esi, s_view1
        call io_print
        mov esi, tmp_name
        call io_println
        mov esi, s_view2
        call io_println

        mov esi, tmp_name
        mov edi, file_buf
        call io_file_read
        cmp eax, -1
        je .err

        mov byte [file_buf + eax], 0
        mov esi, file_buf
        call io_print

        mov esi, s_view3
        call io_println
        mov eax, SYS_GETCHAR
        int 0x80
        jmp .ret

.err:
        mov esi, s_rderr
        call io_println
        mov eax, SYS_GETCHAR
        int 0x80
.ret:   popad
        ret

;=======================================================================
; do_copy — F5: copy to other panel dir
;=======================================================================
do_copy:
        pushad
        call cnt_ptr
        cmp dword [eax], 0
        je .ret

        call get_sel_type
        cmp al, FTYPE_DIR
        je .ret
        movzx edx, al
        mov [_cp_type], edx

        ; Read source
        call cwd_ptr
        mov esi, eax
        call io_dir_change
        call get_sel_name
        mov esi, tmp_name
        mov edi, file_buf
        call io_file_read
        cmp eax, -1
        je .ret
        mov [_cp_size], eax

        ; Write to dest
        call other_cwd
        mov esi, eax
        call io_dir_change
        mov esi, tmp_name
        mov edi, file_buf
        mov ecx, [_cp_size]
        mov edx, [_cp_type]
        call io_file_write

        ; Restore and reload
        call reload_both

.ret:   popad
        ret

;=======================================================================
; do_mkdir — F7: create directory
;=======================================================================
do_mkdir:
        pushad
        call draw_dialog
        mov esi, s_mkdir
        mov ebx, 22
        mov ecx, 11
        mov dl, C_DIALOG
        call vga_write_color

        mov ebx, 22
        mov ecx, 13
        call vga_set_cursor
        call vga_show_cursor

        mov eax, SYS_SETCOLOR
        mov ebx, C_DIALOG
        int 0x80

        mov edi, input_buf
        mov ecx, 28
        call io_read_line
        cmp eax, 0
        je .ret

        call cwd_ptr
        mov esi, eax
        call io_dir_change
        mov esi, input_buf
        call io_dir_create
        call reload_active

.ret:   popad
        ret

;=======================================================================
; do_delete — F8: with confirmation
;=======================================================================
do_delete:
        pushad
        call cnt_ptr
        cmp dword [eax], 0
        je .ret

        call get_sel_name
        ; Disallow ".."
        cmp byte [tmp_name], '.'
        jne .ask
        cmp byte [tmp_name + 1], '.'
        jne .ask
        cmp byte [tmp_name + 2], 0
        je .ret
.ask:
        call draw_dialog
        mov esi, s_del1
        mov ebx, 22
        mov ecx, 11
        mov dl, C_DIALOG
        call vga_write_color
        mov esi, tmp_name
        mov ebx, 22
        mov ecx, 12
        mov dl, C_DIALOG
        call vga_write_color
        mov esi, s_del2
        mov ebx, 22
        mov ecx, 14
        mov dl, C_DIALOG
        call vga_write_color

        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 'y'
        jne .ret

        call cwd_ptr
        mov esi, eax
        call io_dir_change
        mov esi, tmp_name
        call io_file_delete
        call reload_active

.ret:   popad
        ret

;=======================================================================
; draw_dialog — centered box
;=======================================================================
draw_dialog:
        pushad
        mov ebx, 20
        mov ecx, 9
        mov edx, 40
        mov esi, 8
        mov al, ' '
        mov ah, C_DIALOG
        call vga_draw_filled
        mov ebx, 20
        mov ecx, 9
        mov edx, 40
        mov esi, 8
        mov ah, C_DIALOG
        call vga_draw_box
        popad
        ret

;=======================================================================
; vga_hide_cursor / vga_show_cursor — not in lib, implement locally
;=======================================================================
vga_hide_cursor:
        pushad
        mov ebx, 79
        mov ecx, 25                    ; Off-screen row
        call vga_set_cursor
        popad
        ret

vga_show_cursor:
        ret                             ; Cursor visible by default

;=======================================================================
; Strings
;=======================================================================
section .data

s_title:  db " HB Burrow - File Manager                                 Tab:Switch  Ctrl+Q:Quit", 0
s_root:   db "/", 0
s_blank:  db "                                                                                ", 0

s_t_text: db "[Text]    ", 0
s_t_dir:  db "[Dir]     ", 0
s_t_exec: db "[Program] ", 0
s_t_bat:  db "[Batch]   ", 0
s_t_unk:  db "[Unknown] ", 0
s_bytes:  db "bytes", 0

s_view1:  db "=== Viewing: ", 0
s_view2:  db "============================================", 0
s_view3:  db 10, "--- Press any key to return ---", 0
s_rderr:  db "Error: Could not read file.", 0

s_mkdir:  db "New directory name:", 0
s_del1:   db "Delete file:", 0
s_del2:   db "Press 'y' to confirm", 0

s_f3n:    db "3", 0
s_f3l:    db "View ", 0
s_f5n:    db "5", 0
s_f5l:    db "Copy ", 0
s_f7n:    db "7", 0
s_f7l:    db "MkDir", 0
s_f8n:    db "8", 0
s_f8l:    db "Del  ", 0
s_fqn:    db "Q", 0
s_fql:    db "Quit ", 0

;=======================================================================
; BSS
;=======================================================================
section .bss

active:         resb 1

l_cwd:          resb 256
l_count:        resd 1
l_cursor:       resd 1
l_scroll:       resd 1
l_names:        resb FNAME_SZ * MAX_ENTRIES
l_types:        resb MAX_ENTRIES
l_sizes:        resd MAX_ENTRIES

r_cwd:          resb 256
r_count:        resd 1
r_cursor:       resd 1
r_scroll:       resd 1
r_names:        resb FNAME_SZ * MAX_ENTRIES
r_types:        resb MAX_ENTRIES
r_sizes:        resd MAX_ENTRIES

; load_panel temps
_lp_names:      resd 1
_lp_types:      resd 1
_lp_sizes:      resd 1
_lp_count:      resd 1
_lp_cwd:        resd 1
_lp_n:          resd 1

; draw_files temps
_df_idx:        resd 1
_df_side:       resd 1
_df_color:      resb 1

; copy temps
_cp_type:       resd 1
_cp_size:       resd 1

; general
saved_cwd:      resb 256
tmp_name:       resb 256
input_buf:      resb 256
line_buf:       resb 64
size_buf:       resb 16
file_buf:       resb 65536
