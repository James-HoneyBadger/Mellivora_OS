; blauncher.asm - Burrows desktop launcher / task switcher
;
; Two sources feed the list. The manifest-backed catalog is built at compile
; time and names the bundled applications with their descriptions. `pkg
; install` then writes a link file into /Burrows, and those links are read at
; startup - so installing a package is what puts it here, and uninstalling it
; is what takes it away. The catalog entry wins when both name the same
; program.

%include "syscalls.inc"
%include "lib/gui.inc"
%include "lib/string.inc"

%include "generated_app_catalog.inc"

DISC_MAX        equ 384         ; discovered programs kept per session
DISC_NAME       equ 32          ; bytes per discovered name
ROW_Y           equ 38
ROW_H           equ 22
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
; Runtime tables: the catalog's entries first, discovered programs after.
rt_exec         resd APP_CATALOG_COUNT + DISC_MAX
rt_label        resd APP_CATALOG_COUNT + DISC_MAX
rt_search       resd APP_CATALOG_COUNT + DISC_MAX
app_cat_count   resd 1          ; entries before the "Exit Desktop" row
app_total       resd 1          ; app_cat_count + 1
disc_count      resd 1
disc_names      resb DISC_MAX * DISC_NAME
scan_entry      resb 288
cwd_save        resb 128
dir_ptr         resd 1
dec_buf         resb 16
visible_indices resd APP_CATALOG_COUNT + 1 + DISC_MAX
query_buffer    resb 32

section .data
launcher_title  db "Burrows Launcher", 0
search_label    db "Search:", 0
launcher_exit_exec db "exit", 0
launcher_exit_label db "Exit Desktop", 0
footer_str      db "Enter: launch   Esc: close   Type: search", 0
app_icon_color  equ 0x003060A0
; pkg install registers a program by writing a link here.
launcher_dirs   dd launcher_dir_links, 0
launcher_dir_links   db "/Burrows", 0
lnk_suffix      db ".lnk", 0
log_app_prefix  db "[launcher] app: ", 0
log_apps_prefix db "[launcher] apps: ", 0
log_crlf        db 13, 10, 0

section .text
start:
        ; Build the app list before the window, so the list is what the
        ; machine has now rather than what it had at build time.
        call launcher_build_list

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
        call activate_selected
        jmp .close

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
        call activate_selected
        jmp .close

.close:
        mov eax, [win_id]
        call gui_destroy_window

.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; activate_selected - launch the highlighted app, or leave the desktop
;---------------------------------------
activate_selected:
        call selected_is_exit
        test eax, eax
        jz .as_launch
        mov eax, [win_id]
        call gui_destroy_window
        call gui_quit_desktop
        jmp .as_done
.as_launch:
        call launch_selected
.as_done:
        ret

launch_selected:
        mov eax, [selected]
        cmp eax, [visible_count]
        jge .done
        mov eax, [visible_indices + eax*4]
        cmp eax, [app_cat_count]
        je .done
        mov esi, [rt_exec + eax*4]
        call gui_launch
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
        cmp eax, [app_cat_count]
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

        ; Search box: a sunken border and a white face.
        mov eax, [win_id]
        mov ebx, 6
        mov ecx, 6
        mov edx, 348
        mov esi, 26
        mov edi, 0x00808080
        call gui_fill_rect
        mov eax, [win_id]
        mov ebx, 8
        mov ecx, 8
        mov edx, 344
        mov esi, 22
        mov edi, 0x00FFFFFF
        call gui_fill_rect

        ; Search prompt and current query.
        mov eax, [win_id]
        mov ebx, 14
        mov ecx, 11
        mov esi, search_label
        mov edi, 0x00000000
        call gui_draw_text
        mov eax, [win_id]
        mov ebx, 78
        mov ecx, 11
        mov esi, query_buffer
        mov edi, 0x00000000
        call gui_draw_text

        ; Caret after the query.
        mov eax, [win_id]
        mov ebx, [query_len]
        shl ebx, 3
        add ebx, 78
        mov ecx, 11
        mov edx, 1
        mov esi, 16
        xor edi, edi
        call gui_fill_rect

        ; List panel
        mov eax, [win_id]
        mov ebx, 8
        mov ecx, 34
        mov edx, 344
        mov esi, 356
        mov edi, 0x00F4F4F4
        call gui_fill_rect

        ; Footer hint
        mov eax, [win_id]
        mov ebx, 12
        mov ecx, 396
        mov esi, footer_str
        mov edi, 0x00606060
        call gui_draw_text

        mov dword [visible_count], 0
        xor eax, eax
.draw_rows:
        cmp eax, [app_total]
        jge .rows_done
        mov [app_idx], eax
        cmp dword [query_len], 0
        je .show_row
        cmp eax, [app_cat_count]
        je .search_exit
        mov esi, [rt_search + eax*4]
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
        cmp edx, [app_cat_count]
        je .exit_label
        mov ebx, [rt_label + edx*4]
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

;----------------------------------------------------------
; launcher_build_list
; Copy the catalog into the runtime tables, then append every executable
; installed under the program directories that the catalog does not already
; name. A package installed while the machine runs therefore shows up
; without a rebuild, which is what "install software" has to mean.
;----------------------------------------------------------
launcher_build_list:
        pushad
        mov dword [disc_count], 0
        mov dword [app_cat_count], APP_CATALOG_COUNT

        ; Catalog entries, in order.
        xor ecx, ecx
.copy_catalog:
        cmp ecx, APP_CATALOG_COUNT
        jge .catalog_done
        mov eax, [app_exec_table + ecx*4]
        mov [rt_exec + ecx*4], eax
        mov eax, [app_label_table + ecx*4]
        mov [rt_label + ecx*4], eax
        mov eax, [app_search_table + ecx*4]
        mov [rt_search + ecx*4], eax
        inc ecx
        jmp .copy_catalog
.catalog_done:

        ; Scan each program directory. The previous directory is restored, so
        ; a launcher started from the desktop does not move the shell.
        mov eax, SYS_GETCWD
        mov ebx, cwd_save
        int 0x80
        mov esi, launcher_dirs
.dir_loop:
        mov eax, [esi]
        test eax, eax
        jz .dirs_done
        mov [dir_ptr], esi
        mov ebx, eax
        mov eax, SYS_CHDIR
        int 0x80
        test eax, eax
        jnz .next_dir

        xor ebp, ebp
.entry_loop:
        mov eax, SYS_READDIR
        mov ebx, scan_entry
        mov ecx, ebp
        int 0x80
        cmp eax, -1
        je .next_dir
        cmp eax, 5                      ; FTYPE_LINK: a registered program
        jne .next_entry
        cmp byte [scan_entry], 0
        je .next_entry
        call launcher_add_program
.next_entry:
        inc ebp
        cmp ebp, 400
        jb .entry_loop
.next_dir:
        mov esi, [dir_ptr]
        add esi, 4
        jmp .dir_loop
.dirs_done:

        ; Put the shell back where it was.
        mov eax, SYS_CHDIR
        mov ebx, cwd_save
        int 0x80

        mov eax, [app_cat_count]
        inc eax
        mov [app_total], eax

        mov eax, SYS_SERIAL
        mov ebx, log_apps_prefix
        int 0x80
        mov eax, [app_cat_count]
        call serial_dec
        mov eax, SYS_SERIAL
        mov ebx, log_crlf
        int 0x80

        popad
        ret

;----------------------------------------------------------
; launcher_add_program
; Add the executable named in scan_entry, unless the catalog already lists it
; or it is already in the list. Announces each addition on COM1 so a headless
; test can see what the menu would show.
;----------------------------------------------------------
launcher_add_program:
        pushad

        ; The link is named <program>.lnk; the program is what it starts.
        xor ecx, ecx
.len_loop:
        cmp byte [scan_entry + ecx], 0
        je .len_done
        inc ecx
        jmp .len_loop
.len_done:
        cmp ecx, 4
        jb .add_done
        cmp byte [scan_entry + ecx - 4], '.'
        jne .add_done
        cmp byte [scan_entry + ecx - 3], 'l'
        jne .add_done
        cmp byte [scan_entry + ecx - 2], 'n'
        jne .add_done
        cmp byte [scan_entry + ecx - 1], 'k'
        jne .add_done
        mov byte [scan_entry + ecx - 4], 0
        cmp byte [scan_entry], 0
        je .add_done

        cmp dword [disc_count], DISC_MAX
        jae .add_done

        ; Already named by the catalog?
        xor ecx, ecx
.cat_check:
        cmp ecx, APP_CATALOG_COUNT
        jge .not_in_catalog
        mov esi, [rt_exec + ecx*4]
        mov edi, scan_entry
        call str_equal
        test eax, eax
        jnz .add_done
        inc ecx
        jmp .cat_check
.not_in_catalog:

        ; Already discovered (the same name can live in two directories)?
        xor ecx, ecx
.disc_check:
        cmp ecx, [disc_count]
        jge .append
        mov edi, ecx
        imul edi, DISC_NAME
        add edi, disc_names
        mov esi, scan_entry
        call str_equal
        test eax, eax
        jnz .add_done
        inc ecx
        jmp .disc_check

.append:
        mov edi, [disc_count]
        imul edi, DISC_NAME
        add edi, disc_names
        mov esi, scan_entry
        mov ecx, DISC_NAME - 1
.copy_name:
        lodsb
        stosb
        test al, al
        jz .name_done
        dec ecx
        jnz .copy_name
        mov byte [edi], 0
.name_done:

        mov eax, [disc_count]
        imul eax, DISC_NAME
        add eax, disc_names
        mov ecx, [app_cat_count]
        mov [rt_exec + ecx*4], eax
        mov [rt_label + ecx*4], eax
        mov [rt_search + ecx*4], eax
        inc dword [app_cat_count]
        inc dword [disc_count]

        mov eax, SYS_SERIAL
        mov ebx, log_app_prefix
        int 0x80
        mov eax, SYS_SERIAL
        mov ebx, scan_entry
        int 0x80
        mov eax, SYS_SERIAL
        mov ebx, log_crlf
        int 0x80
.add_done:
        popad
        ret

;----------------------------------------------------------
; serial_dec - write EAX in decimal to COM1
;----------------------------------------------------------
serial_dec:
        pushad
        mov edi, dec_buf + 15
        mov byte [edi], 0
        mov ebx, 10
.digit:
        xor edx, edx
        div ebx
        add dl, '0'
        dec edi
        mov [edi], dl
        test eax, eax
        jnz .digit
        mov eax, SYS_SERIAL
        mov ebx, edi
        int 0x80
        popad
        ret

;----------------------------------------------------------
; str_equal - ESI and EDI point at strings; EAX = 1 when equal
;----------------------------------------------------------
str_equal:
        push esi
        push edi
.se_loop:
        mov al, [esi]
        cmp al, [edi]
        jne .se_no
        test al, al
        jz .se_yes
        inc esi
        inc edi
        jmp .se_loop
.se_yes:
        mov eax, 1
        pop edi
        pop esi
        ret
.se_no:
        xor eax, eax
        pop edi
        pop esi
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

        ; Row icon: every application gets a mark, so a row reads as an
        ; application rather than as a line of text.
        mov eax, [win_id]
        mov ebx, ROW_X + 4
        mov ecx, [row_y]
        add ecx, 5
        mov edx, 10
        mov esi, 10
        mov edi, app_icon_color
        call gui_fill_rect
        mov eax, [win_id]
        mov ebx, ROW_X + 6
        mov ecx, [row_y]
        add ecx, 7
        mov edx, 6
        mov esi, 6
        mov edi, 0x00FFFFFF
        call gui_fill_rect

        ; Row label.
        mov eax, [win_id]
        mov ebx, ROW_X + 20
        mov ecx, [row_y]
        add ecx, 4
        mov esi, [row_ptr]
        mov edi, 0x00000000
        call gui_draw_text

        popad
        ret
