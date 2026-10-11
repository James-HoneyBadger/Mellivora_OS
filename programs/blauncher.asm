; blauncher.asm - Burrows desktop launcher / task switcher
; Table-driven launcher foundation for manifest-backed app discovery.

%include "syscalls.inc"
%include "lib/gui.inc"
%include "lib/string.inc"

%include "generated_app_catalog.inc"

APP_COUNT       equ APP_CATALOG_COUNT + 1
ROW_Y           equ 54
ROW_H           equ 24
ROW_X           equ 18
ROW_W           equ 320
VIEW_ROWS       equ 15

section .bss
win_id          resd 1
selected        resd 1
row_idx         resd 1
row_y           resd 1
row_ptr         resd 1
app_idx         resd 1
query_len       resd 1
visible_count   resd 1
scroll_offset   resd 1
visible_indices resd APP_COUNT
query_buffer    resb 32

section .data
launcher_title  db "Burrows Launcher", 0
search_label    db "Search:", 0
launcher_exit_exec db "exit", 0
launcher_exit_label db "Exit Desktop", 0

section .text
start:
        mov eax, 120
        mov ebx, 80
        mov ecx, 360
        mov edx, 420
        mov esi, launcher_title
        call gui_create_window
        cmp eax, -1
        je .exit
        mov [win_id], eax
        mov dword [selected], 0
        mov dword [query_len], 0
        mov dword [scroll_offset], 0
        mov byte [query_buffer], 0

.main_loop:
        call draw_launcher
        mov eax, [win_id]
        call gui_compose
        mov eax, [win_id]
        call gui_flip

        mov eax, SYS_SLEEP
        mov ebx, 2
        int 0x80

        mov eax, [win_id]
        call gui_poll_event
        cmp eax, EVT_CLOSE
        je .close
        cmp eax, EVT_KEY_PRESS
        je .handle_key
        cmp eax, EVT_MOUSE_CLICK
        jne .main_loop

        ; Click y determines the launcher item index.
        mov eax, ecx
        cmp eax, ROW_Y
        jl .main_loop
        sub eax, ROW_Y
        xor edx, edx
        mov ebx, ROW_H
        div ebx
        add eax, [scroll_offset]
        cmp eax, [visible_count]
        jge .main_loop
        mov [selected], eax
        call launch_selected
        call selected_is_exit
        test eax, eax
        jnz .close
        jmp .main_loop

.handle_key:
        cmp ebx, 27
        je .close
        cmp ebx, 8
        je .backspace
        cmp ebx, KEY_UP
        je .move_up
        cmp ebx, KEY_DOWN
        je .move_down
        cmp ebx, 13
        je .activate
        cmp ebx, 32
        je .activate
        cmp ebx, 32
        jb .main_loop
        cmp ebx, 126
        ja .main_loop
        cmp dword [query_len], 31
        jge .main_loop
        mov eax, [query_len]
        mov [query_buffer + eax], bl
        inc eax
        mov [query_len], eax
        mov byte [query_buffer + eax], 0
        mov dword [selected], 0
        mov dword [scroll_offset], 0
        jmp .main_loop

.backspace:
        cmp dword [query_len], 0
        je .main_loop
        dec dword [query_len]
        mov eax, [query_len]
        mov byte [query_buffer + eax], 0
        mov dword [selected], 0
        mov dword [scroll_offset], 0
        jmp .main_loop

.move_up:
        mov eax, [selected]
        cmp eax, 0
        je .main_loop
        dec dword [selected]
        call keep_selection_visible
        jmp .main_loop

.move_down:
        mov eax, [selected]
        mov edx, [visible_count]
        dec edx
        cmp eax, edx
        je .main_loop
        inc dword [selected]
        call keep_selection_visible
        jmp .main_loop

.activate:
        call launch_selected
        call selected_is_exit
        test eax, eax
        jnz .close
        jmp .main_loop

.close:
        mov eax, [win_id]
        call gui_destroy_window

.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

launch_selected:
        mov eax, [selected]
        cmp eax, [visible_count]
        jge .done
        mov eax, [visible_indices + eax*4]
        cmp eax, APP_COUNT - 1
        je .done
        mov esi, [app_exec_table + eax*4]
        mov eax, SYS_EXEC
        mov ebx, esi
        xor ecx, ecx
        int 0x80
.done:
        ret

keep_selection_visible:
        mov eax, [selected]
        cmp eax, [scroll_offset]
        jge .check_bottom
        mov [scroll_offset], eax
        ret
.check_bottom:
        mov edx, [scroll_offset]
        add edx, VIEW_ROWS
        cmp eax, edx
        jl .done
        sub eax, VIEW_ROWS - 1
        mov [scroll_offset], eax
.done:
        ret

launcher_search_ci:
        push esi
        push edi
        push ebx
        push ecx
        push edx
.outer:
        cmp byte [esi], 0
        je .not_found
        mov ebx, esi
        mov edx, edi
.inner:
        mov al, [edx]
        cmp al, 0
        je .found
        mov cl, [ebx]
        cmp cl, 0
        je .not_found
        cmp al, 'A'
        jb .needle_ready
        cmp al, 'Z'
        ja .needle_ready
        add al, 32
.needle_ready:
        cmp cl, 'A'
        jb .haystack_ready
        cmp cl, 'Z'
        ja .haystack_ready
        add cl, 32
.haystack_ready:
        cmp al, cl
        jne .advance
        inc ebx
        inc edx
        jmp .inner
.advance:
        inc esi
        jmp .outer
.found:
        mov eax, esi
        pop edx
        pop ecx
        pop ebx
        pop edi
        pop esi
        ret
.not_found:
        xor eax, eax
        pop edx
        pop ecx
        pop ebx
        pop edi
        pop esi
        ret

selected_is_exit:
        push ebx
        mov eax, [selected]
        cmp eax, [visible_count]
        jge .not_exit
        mov eax, [visible_indices + eax*4]
        cmp eax, APP_COUNT - 1
        sete al
        movzx eax, al
        pop ebx
        ret
.not_exit:
        xor eax, eax
        pop ebx
        ret

;----------------------------------------------------------
; draw_launcher
; Draw the list of apps and highlight the selected row.
;----------------------------------------------------------
draw_launcher:
        pushad

        ; Window background
        mov eax, [win_id]
        mov ebx, 0
        mov ecx, 0
        mov edx, 360
        mov esi, 420
        mov edi, 0x00D0D0D0
        call gui_fill_rect

        ; Search prompt and current query.
        mov eax, [win_id]
        mov ebx, 16
        mov ecx, 31
        mov esi, search_label
        mov edi, 0x00000000
        call gui_draw_text
        mov eax, [win_id]
        mov ebx, 80
        mov ecx, 31
        mov esi, query_buffer
        mov edi, 0x00000000
        call gui_draw_text

        ; Title bar
        mov eax, [win_id]
        mov ebx, 0
        mov ecx, 0
        mov edx, 360
        mov esi, 24
        mov edi, 0x005A7FBA
        call gui_fill_rect

        ; Title text
        mov eax, [win_id]
        mov ebx, 10
        mov ecx, 6
        mov esi, launcher_title
        mov edi, 0x00FFFFFF
        call gui_draw_text

        ; List panel
        mov eax, [win_id]
        mov ebx, 8
        mov ecx, 28
        mov edx, 332
        mov esi, 380
        mov edi, 0x00F4F4F4
        call gui_fill_rect

        mov dword [visible_count], 0
        xor eax, eax
.draw_rows:
        cmp eax, APP_COUNT
        jge .rows_done
        mov [app_idx], eax
        cmp dword [query_len], 0
        je .show_row
        cmp eax, APP_CATALOG_COUNT
        je .search_exit
        mov esi, [app_search_table + eax*4]
        mov edi, query_buffer
        call launcher_search_ci
        test eax, eax
        jz .next_app
        jmp .show_row
.search_exit:
        mov esi, launcher_exit_exec
        mov edi, query_buffer
        call launcher_search_ci
        test eax, eax
        jz .next_app
.show_row:
        mov eax, [visible_count]
        mov edx, [app_idx]
        mov [visible_indices + eax*4], edx
        inc dword [visible_count]
        cmp edx, APP_CATALOG_COUNT
        je .exit_label
        mov ebx, [app_label_table + edx*4]
        jmp .check_viewport
.exit_label:
        mov ebx, launcher_exit_label
.check_viewport:
        mov eax, [visible_count]
        cmp eax, [scroll_offset]
        jb .next_app
        mov edx, [scroll_offset]
        add edx, VIEW_ROWS
        cmp eax, edx
        jge .next_app
        sub eax, [scroll_offset]
        call draw_row
.next_app:
        mov eax, [app_idx]
        inc eax
        jmp .draw_rows
.rows_done:

        cmp dword [visible_count], 0
        je .clear_selection
        mov eax, [visible_count]
        cmp dword [selected], eax
        jb .selection_ready
        dec eax
        mov [selected], eax
        jmp .selection_ready
.clear_selection:
        mov dword [selected], 0
.selection_ready:

        popad
        ret

; draw_row: eax = row index, ebx = string pointer
; draws one row and highlights the selected entry.
draw_row:
        pushad
        mov [row_idx], eax
        mov [row_ptr], ebx

        ; Compute row Y position.
        mov eax, [row_idx]
        imul eax, ROW_H
        add eax, ROW_Y
        mov [row_y], eax

        ; Background panel.
        mov eax, [win_id]
        mov ebx, ROW_X
        mov ecx, [row_y]
        mov edx, ROW_W
        mov esi, ROW_H
        sub esi, 2
        mov edi, 0x00EAEAEA
        mov eax, [row_idx]
        cmp eax, [selected]
        jne .after_highlight
        mov edi, 0x00B5D6FF
.after_highlight:
        call gui_fill_rect

        ; Row label.
        mov eax, [win_id]
        mov ebx, ROW_X + 8
        mov ecx, [row_y]
        add ecx, 4
        mov esi, [row_ptr]
        mov edi, 0x00000000
        call gui_draw_text

        popad
        ret
