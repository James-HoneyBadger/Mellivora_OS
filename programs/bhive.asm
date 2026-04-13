; bhive.asm - BHive - Burrows GUI File Manager
; Browse files in the current directory with a GUI window.

%include "syscalls.inc"
%include "lib/gui.inc"

start:
        ; Create window
        mov eax, 80
        mov ebx, 50
        mov ecx, 400
        mov edx, 340
        mov esi, title_str
        call gui_create_window
        cmp eax, -1
        je .exit
        mov [win_id], eax

        ; Load directory listing
        call load_dir

.main_loop:
        call gui_compose
        call draw_content
        call gui_flip

        call gui_poll_event
        cmp eax, EVT_CLOSE
        je .close
        cmp eax, EVT_KEY_PRESS
        jne .check_mouse
        cmp bl, 27
        je .close
        cmp bl, KEY_UP
        je .scroll_up
        cmp bl, KEY_DOWN
        je .scroll_down
        jmp .main_loop

.check_mouse:
        cmp eax, EVT_MOUSE_CLICK
        jne .main_loop
        ; EBX = relative x, ECX = relative y
        cmp ecx, 0
        jl .main_loop
        ; Calculate which file was clicked
        sub ecx, 28            ; header height
        cmp ecx, 0
        jl .main_loop
        mov eax, ecx
        xor edx, edx
        mov ecx, 18            ; row height
        div ecx
        add eax, [scroll_pos]
        cmp eax, [file_count]
        jge .main_loop
        mov [selected], eax
        jmp .main_loop

.scroll_up:
        cmp dword [scroll_pos], 0
        je .main_loop
        dec dword [scroll_pos]
        jmp .main_loop

.scroll_down:
        mov eax, [file_count]
        sub eax, 16
        cmp eax, 0
        jle .main_loop
        cmp [scroll_pos], eax
        jge .main_loop
        inc dword [scroll_pos]
        jmp .main_loop

.close:
        mov eax, [win_id]
        call gui_destroy_window
.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; draw_content
;---------------------------------------
draw_content:
        pushad
        ; Background
        mov eax, [win_id]
        mov ebx, 0
        mov ecx, 0
        mov edx, 400
        mov esi, 340
        mov edi, 0x00FFFFFF
        call gui_fill_rect

        ; Header
        mov eax, [win_id]
        mov ebx, 0
        mov ecx, 0
        mov edx, 400
        mov esi, 24
        mov edi, 0x00E0E0E0
        call gui_fill_rect

        ; Path/title
        mov eax, [win_id]
        mov ebx, 8
        mov ecx, 4
        mov esi, hdr_str
        mov edi, 0x00000000
        call gui_draw_text

        ; File count
        mov eax, [win_id]
        mov ebx, 280
        mov ecx, 4
        mov esi, count_label
        mov edi, 0x00606060
        call gui_draw_text

        ; Draw files
        xor ecx, ecx           ; visible index
        mov ebx, [scroll_pos]  ; file index
.draw_file:
        cmp ecx, 16            ; max visible
        jge .draw_done
        cmp ebx, [file_count]
        jge .draw_done

        ; Highlight selected
        push ecx
        push ebx
        cmp ebx, [selected]
        jne .no_hl
        mov eax, [win_id]
        push ecx
        imul ecx, 18
        add ecx, 28
        mov ebx, 0
        mov edx, 400
        mov esi, 18
        mov edi, 0x003060A0
        call gui_fill_rect
        pop ecx
.no_hl:
        pop ebx
        pop ecx

        ; Get file entry
        push ecx
        push ebx
        mov eax, ebx
        shl eax, 6             ; * 64
        lea esi, [file_names + eax]

        ; Draw icon (folder or file indicator)
        mov eax, [win_id]
        push ecx
        imul ecx, 18
        add ecx, 30
        mov ebx, 8
        push esi
        ; Check file type
        pop esi
        push esi
        mov edi, 0x00000000
        pop esi
        pop ecx
        pop ebx
        pop ecx                ; restore saved vis_idx from "Get file entry"

        ; Draw filename
        push ecx
        push ebx
        mov eax, ebx
        shl eax, 6
        lea esi, [file_names + eax]
        mov eax, [win_id]
        mov ebx, 8
        imul ecx, 18
        add ecx, 30
        cmp [esp + 4], dword 0  ; check if this is selected
        mov edi, 0x00000000
        push eax
        mov eax, [esp + 4]      ; file index from pushed ebx
        cmp eax, [selected]
        pop eax
        jne .file_dark
        mov edi, 0x00FFFFFF
.file_dark:
        call gui_draw_text
        pop ebx
        pop ecx

        inc ecx
        inc ebx
        jmp .draw_file

.draw_done:
        ; Scrollbar hint
        mov eax, [win_id]
        mov ebx, 8
        mov ecx, 320
        mov esi, scroll_hint
        mov edi, 0x00909090
        call gui_draw_text

        popad
        ret

;---------------------------------------
; load_dir - Load current directory listing
;---------------------------------------
load_dir:
        pushad
        mov dword [file_count], 0
        xor ecx, ecx           ; index

.ld_loop:
        mov eax, SYS_READDIR
        mov ebx, dir_entry_buf
        int 0x80
        cmp eax, -1
        je .ld_done
        cmp eax, 0             ; free entry
        je .ld_next

        ; Copy name to file_names array
        push ecx
        cmp dword [file_count], 64
        jge .ld_full
        mov eax, [file_count]
        shl eax, 6
        lea edi, [file_names + eax]
        mov esi, dir_entry_buf
        push ecx
        mov ecx, 63
.ld_copy:
        lodsb
        stosb
        cmp al, 0
        je .ld_pad
        dec ecx
        jnz .ld_copy
        mov byte [edi], 0
.ld_pad:
        pop ecx
        inc dword [file_count]
.ld_full:
        pop ecx

.ld_next:
        inc ecx
        cmp ecx, 200           ; max entries to check
        jl .ld_loop

.ld_done:
        popad
        ret

; Data
title_str:      db "BHive", 0
hdr_str:        db "BHive Directory", 0
count_label:    db "Items:", 0
scroll_hint:    db "Up/Down to scroll", 0

win_id:         dd 0
file_count:     dd 0
scroll_pos:     dd 0
selected:       dd -1

dir_entry_buf:  times 288 db 0
file_names:     times 64 * 64 db 0      ; 64 files x 64 char names
