; bview.asm - BMP Image Viewer for Mellivora OS (Burrows GUI)
; Loads and displays 24-bit uncompressed BMP files.
; Usage: bview <filename.bmp>

%include "syscalls.inc"
%include "lib/gui.inc"

MAX_FILE_SIZE   equ 512000      ; 500KB max
MAX_IMG_W       equ 600
MAX_IMG_H       equ 440

; BMP header offsets
BMP_MAGIC       equ 0           ; 'BM' (word)
BMP_FILE_SIZE   equ 2           ; dword
BMP_DATA_OFS    equ 10          ; dword: offset to pixel data
BMP_DIB_SIZE    equ 14          ; dword: DIB header size
BMP_WIDTH       equ 18          ; dword (signed)
BMP_HEIGHT      equ 22          ; dword (signed, negative=top-down)
BMP_PLANES      equ 26          ; word: must be 1
BMP_BPP         equ 28          ; word: bits per pixel
BMP_COMPRESS    equ 30          ; dword: 0=none

; Colors
COL_BG          equ 0x00333333
COL_ERR         equ 0x00FF4444
COL_INFO        equ 0x00CCCCCC

start:
        ; Get filename argument
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        cmp eax, 0
        jle .no_args

        ; Skip leading spaces
        mov esi, arg_buf
.skip_sp:
        cmp byte [esi], ' '
        jne .got_filename
        inc esi
        jmp .skip_sp

.got_filename:
        cmp byte [esi], 0
        je .no_args

        ; Load file
        mov eax, SYS_FREAD
        mov ebx, esi
        mov ecx, file_buf
        int 0x80
        cmp eax, 0
        jle .file_error
        mov [file_size], eax

        ; Validate BMP header
        cmp word [file_buf + BMP_MAGIC], 0x4D42   ; 'BM'
        jne .not_bmp

        ; Get image dimensions
        mov eax, [file_buf + BMP_WIDTH]
        mov [img_w], eax
        mov eax, [file_buf + BMP_HEIGHT]
        ; Height can be negative (top-down)
        test eax, eax
        jns .height_positive
        neg eax
        mov byte [top_down], 1
.height_positive:
        mov [img_h], eax

        ; Check BPP (must be 24)
        cmp word [file_buf + BMP_BPP], 24
        jne .unsupported_bpp

        ; Check compression (must be 0)
        cmp dword [file_buf + BMP_COMPRESS], 0
        jne .unsupported_bpp

        ; Clamp to max display size
        mov eax, [img_w]
        cmp eax, MAX_IMG_W
        jle .w_ok
        mov eax, MAX_IMG_W
        mov [img_w], eax
.w_ok:
        mov eax, [img_h]
        cmp eax, MAX_IMG_H
        jle .h_ok
        mov eax, MAX_IMG_H
        mov [img_h], eax
.h_ok:

        ; Create window to fit image
        mov eax, 10
        mov ebx, 10
        mov ecx, [img_w]
        mov edx, [img_h]
        mov esi, win_title
        call gui_create_window
        mov [win_id], eax

        ; Render image
        call render_bmp

.view_loop:
        mov eax, [win_id]
        call gui_poll_event
        cmp eax, EVT_CLOSE
        je .close_win
        cmp eax, EVT_KEY_PRESS
        jne .view_loop
        cmp ebx, 27             ; ESC
        je .close_win
        cmp ebx, 'q'
        je .close_win
        cmp ebx, 'Q'
        je .close_win
        jmp .view_loop

.close_win:
        mov eax, [win_id]
        call gui_destroy_window
        mov eax, SYS_EXIT
        int 0x80

.no_args:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0F
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_usage
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

.file_error:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_file_err
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

.not_bmp:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_not_bmp
        int 0x80
        mov eax, SYS_EXIT
        int 0x80

.unsupported_bpp:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0C
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_unsupported
        int 0x80
        mov eax, SYS_EXIT
        int 0x80


; ─── render_bmp ──────────────────────────────────────────────
; Render the loaded BMP file to the GUI window pixel by pixel
render_bmp:
        pushad

        ; Background
        mov eax, [win_id]
        xor ebx, ebx
        xor ecx, ecx
        mov edx, [img_w]
        mov esi, [img_h]
        mov edi, COL_BG
        call gui_fill_rect

        ; Calculate BMP row stride (each row padded to 4 bytes)
        mov eax, [file_buf + BMP_WIDTH]  ; original width (not clamped)
        imul eax, 3                      ; bytes per row
        mov ecx, eax
        add ecx, 3
        and ecx, ~3                      ; align to 4 bytes
        mov [row_stride], ecx

        ; Pixel data offset
        mov eax, [file_buf + BMP_DATA_OFS]
        add eax, file_buf
        mov [pixel_base], eax

        ; Draw pixels
        mov dword [draw_y], 0

.render_row:
        mov eax, [draw_y]
        cmp eax, [img_h]
        jge .render_done

        ; Calculate source row
        ; BMP is bottom-up by default (row 0 = bottom)
        cmp byte [top_down], 1
        je .row_topdown
        ; Bottom-up: source_row = (orig_height - 1 - draw_y)
        mov ecx, [file_buf + BMP_HEIGHT]
        test ecx, ecx
        jns .row_pos_h
        neg ecx
.row_pos_h:
        dec ecx
        sub ecx, eax
        jmp .row_calc
.row_topdown:
        mov ecx, eax
.row_calc:
        ; ESI = pixel_base + source_row * row_stride
        mov esi, ecx
        imul esi, [row_stride]
        add esi, [pixel_base]

        mov dword [draw_x], 0

.render_col:
        mov ebx, [draw_x]
        cmp ebx, [img_w]
        jge .render_row_next

        ; Read BGR triplet
        mov eax, ebx
        imul eax, 3
        movzx edx, byte [esi + eax]       ; B
        movzx ecx, byte [esi + eax + 1]   ; G
        movzx eax, byte [esi + eax + 2]   ; R

        ; Build 0x00RRGGBB
        shl eax, 16
        shl ecx, 8
        or eax, ecx
        or eax, edx
        mov [pixel_color], eax

        ; Draw pixel
        push esi
        mov eax, [win_id]
        mov ebx, [draw_x]
        mov ecx, [draw_y]
        mov esi, [pixel_color]
        call gui_draw_pixel
        pop esi

        inc dword [draw_x]
        jmp .render_col

.render_row_next:
        inc dword [draw_y]
        jmp .render_row

.render_done:
        ; Compose and flip
        mov eax, [win_id]
        call gui_compose
        mov eax, [win_id]
        call gui_flip

        popad
        ret


; ═════════════════════════════════════════════════════════════
; DATA
; ═════════════════════════════════════════════════════════════

win_title:      db "BView", 0
str_usage:      db "Usage: bview <filename.bmp>", 10, 0
str_file_err:   db "Error: Cannot read file.", 10, 0
str_not_bmp:    db "Error: Not a valid BMP file.", 10, 0
str_unsupported: db "Error: Only 24-bit uncompressed BMP supported.", 10, 0

top_down:       db 0

; ═════════════════════════════════════════════════════════════
; BSS
; ═════════════════════════════════════════════════════════════

section .bss

win_id:         resd 1
file_size:      resd 1
img_w:          resd 1
img_h:          resd 1
row_stride:     resd 1
pixel_base:     resd 1
pixel_color:    resd 1
draw_x:         resd 1
draw_y:         resd 1
arg_buf:        resb 256
file_buf:       resb MAX_FILE_SIZE
