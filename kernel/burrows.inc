;=======================================================================
; burrows.inc - Burrows Desktop Environment (Window Manager)
;
; Full windowed GUI desktop for Mellivora OS:
;   - Double-buffered 1024x768x32 compositor
;   - Draggable windows with title bar and close button
;   - Customizable color themes (blue, dark, light, amber)
;   - Gradient or solid wallpaper, persisted in burrows.cfg
;   - Taskbar with app launcher menu and clock
;   - 8x16 bitmap font for pixel text rendering
;   - Mouse cursor with pixel-perfect tracking
;   - Event system for GUI applications (SYS_GUI / INT 0x80)
;   - Built-in applets: About, Clock, Settings
;   - External app support via SYS_GUI syscall API
;=======================================================================

; Back buffer lives at a fixed address within the 128 MB identity map
GUI_BACKBUF     equ 0x02000000

; Screen geometry
GUI_SCREEN_W    equ 1024
GUI_SCREEN_H    equ 768
GUI_BPP         equ 32                     ; current Burrows compositor uses 32-bit ARGB
GUI_PITCH       equ GUI_SCREEN_W * 4       ; 4096 bytes per row

; Window system
MAX_WINDOWS     equ 16
WIN_STRUCT_SIZE equ 132

; Window struct field offsets
WIN_FLAGS       equ 0           ; dword: bit0=active, bit1=visible, bit2=maximized
WIN_X           equ 4           ; dword: content area x
WIN_Y           equ 8           ; dword: content area y
WIN_W           equ 12          ; dword: content area width
WIN_H           equ 16          ; dword: content area height
WIN_Z           equ 20          ; dword: z-order (higher = on top)
WIN_TITLE       equ 24          ; 64 bytes: null-terminated title
WIN_APP_ID      equ 88          ; legacy owner flag (unused; see WIN_OWNER_PID)
WIN_SAVE_X      equ 92          ; saved x before maximize
WIN_SAVE_Y      equ 96          ; saved y before maximize
WIN_SAVE_W      equ 100         ; saved w before maximize
WIN_SAVE_H      equ 104         ; saved h before maximize
WIN_WORKSPACE   equ 108         ; v9.0: virtual desktop (0-3; 0xFF = show on all)
WIN_ALPHA       equ 109         ; v9.0: opacity 0-255 (255 = fully opaque)
WIN_BUF         equ 112         ; dword: private content buffer (w*h*4)
WIN_BUF_PAGES   equ 116         ; dword: pages allocated for WIN_BUF
WIN_OWNER_PID   equ 120         ; dword: owning task PID (-1 = desktop/applet)
WIN_BUF_W       equ 124         ; dword: allocated buffer width
WIN_BUF_H       equ 128         ; dword: allocated buffer height

; Target surface for the gui_bb_* primitives. The compositor points this at
; the screen back buffer; window draw syscalls point it at the window's own
; content buffer so overlapping windows no longer erase each other.
gui_target_buf:   dd GUI_BACKBUF
gui_target_pitch: dd GUI_PITCH
gui_target_w:     dd GUI_SCREEN_W
gui_target_h:     dd GUI_SCREEN_H

; Window decoration sizes
TITLEBAR_H      equ 22
BORDER_W        equ 2
CLOSE_BTN_SIZE  equ 16

; Taskbar
TASKBAR_H       equ 28
TASKBAR_Y       equ GUI_SCREEN_H - TASKBAR_H   ; 452

; Font
FONT_W          equ 8
FONT_H          equ 16
FONT_FIRST_CHAR equ 32
FONT_LAST_CHAR  equ 126

; Event types
EVT_NONE        equ 0
EVT_MOUSE_CLICK equ 1
EVT_MOUSE_MOVE  equ 2
EVT_KEY_PRESS   equ 3
EVT_CLOSE       equ 4

; Menu constants — Windows 2000 Start menu proportions
GUI_SW_W        equ 260         ; Alt+Tab switcher width
GUI_SW_ROW_H    equ 22          ; switcher row height
GUI_SW_MAX      equ 8           ; switcher shows at most this many windows
GUI_MENU_X      equ 2
GUI_MENU_W      equ 248
GUI_MENU_ITEM_H equ 22
GUI_MENU_ITEMS  equ 7
GUI_MENU_HEADER_H equ 46
GUI_MENU_BANNER_W equ 26
GUI_MENU_BODY_X equ GUI_MENU_X + GUI_MENU_BANNER_W
GUI_MENU_H      equ GUI_MENU_HEADER_H + GUI_MENU_ITEMS * GUI_MENU_ITEM_H
GUI_SUB_W       equ 220
GUI_SUB_ITEM_H  equ 22
GUI_SUB_NONE    equ 0xFF

; Taskbar metrics
TASKBAR_START_W equ 66
TASKBAR_TASKS_X equ TASKBAR_START_W + 10
TASKBAR_TASK_W  equ 100
TASKBAR_TASK_GAP equ 4
TASKBAR_TRAY_W  equ 142

; Theme struct offsets (12 colors x 4 bytes = 48 bytes)
THEME_DESKTOP_BG        equ 0
THEME_TASKBAR_BG        equ 4
THEME_TITLE_ACTIVE      equ 8
THEME_TITLE_INACTIVE    equ 12
THEME_TITLE_TEXT        equ 16
THEME_WINDOW_BG         equ 20
THEME_WINDOW_BORDER     equ 24
THEME_MENU_BG           equ 28
THEME_MENU_TEXT         equ 32
THEME_MENU_HIGHLIGHT    equ 36
THEME_BUTTON_BG         equ 40
THEME_ACCENT            equ 44
THEME_SIZE              equ 48

; SYS_GUI sub-function numbers
GUI_CREATE_WINDOW  equ 0
GUI_DESTROY_WINDOW equ 1
GUI_FILL_RECT      equ 2
GUI_DRAW_TEXT      equ 3
GUI_POLL_EVENT     equ 4
GUI_GET_THEME      equ 5
GUI_SET_THEME      equ 6
GUI_DRAW_PIXEL     equ 7
GUI_DRAW_LINE      equ 8
GUI_COMPOSE        equ 9
GUI_FLIP           equ 10
GUI_DESKTOP        equ 11
GUI_DRAW_BUTTON    equ 20
GUI_DRAW_CHECKBOX  equ 21
GUI_DRAW_PROGRESS  equ 22
GUI_DRAW_TEXTBOX   equ 23
GUI_DRAW_LISTBOX   equ 24
GUI_DRAW_LABEL     equ 25
GUI_DRAW_RECT      equ 26
GUI_LAUNCH         equ 30      ; ESI = program name; spawns a GUI task
GUI_EXIT_DESKTOP   equ 31      ; Leave Burrows and return to the shell
GUI_SET_TITLE      equ 32      ; ECX=win_id ESI=title; retitle a window you own

;=======================================================================
; INITIALIZATION
;=======================================================================

burrows_init:
        pushad
        ; Copy default theme to runtime theme buffer
        mov esi, gui_theme_blue
        mov edi, gui_theme
        mov ecx, THEME_SIZE / 4
        rep movsd
        ; A saved theme in burrows.cfg wins over the built-in default.
        call burrows_config_load
        ; Clear window table
        mov edi, gui_win_table
        xor eax, eax
        mov ecx, (MAX_WINDOWS * WIN_STRUCT_SIZE) / 4
        rep stosd
        ; Reset state
        mov dword [gui_win_count], 0
        mov dword [gui_focused_win], -1
        mov dword [gui_next_z], 0
        mov byte [gui_desktop_active], 0
        mov byte [gui_menu_open], 0
        mov dword [gui_menu_sub], -1
        mov dword [gui_menu_sub_sel], -1
        mov dword [gui_icon_sel], -1
        mov dword [gui_icon_click_ttl], 0
        mov dword [gui_title_dbl_win], -1
        mov dword [gui_title_dbl_until], 0
        mov byte [gui_exit_flag], 0
        mov byte [gui_dragging], 0
        mov byte [gui_active_workspace], 0  ; v9.0: start on desktop 0
        ; v10: register this task as compositor so ipc_surface_create wakes us
        call sched_get_current_task
        test eax, eax
        jz .bi_nopid
        mov eax, [eax + TCB_PID]
        mov [burrows_compositor_pid], eax
.bi_nopid:
        popad
        ret

;=======================================================================
; DESKTOP MAIN LOOP
;=======================================================================

gui_desktop_main:
        cmp byte [vbe_available], 0
        je .no_vbe

        ; Switch to 640x480x32
        mov eax, GUI_SCREEN_W
        mov ebx, GUI_SCREEN_H
        mov ecx, GUI_BPP
        call vbe_set_mode
        jmp .vbe_ok
.no_vbe:
        call vga_set_error
        mov esi, .no_vbe_msg
        call vga_print
        call vga_reset_color
        ret
.no_vbe_msg: db "Error: VBE not available", 0x0A, 0
.notify_welcome: db "Welcome to Mellivora OS!", 0
.vbe_ok:

        ; Switch mouse to pixel-resolution tracking
        mov dword [mouse_max_x], GUI_SCREEN_W - 1
        mov dword [mouse_max_y], GUI_SCREEN_H - 1
        mov dword [mouse_scale], 1
        mov dword [mouse_clamp], 6
        mov dword [mouse_x], GUI_SCREEN_W / 2
        mov dword [mouse_y], GUI_SCREEN_H / 2

        ; Activate desktop
        mov byte [gui_desktop_active], 1
        mov byte [gui_exit_flag], 0
        mov byte [gui_menu_open], 0
        mov dword [gui_menu_sub], -1
        mov dword [gui_menu_sub_sel], -1
        mov byte [gui_ctx_open], 0
        mov byte [gui_dragging], 0
        mov dword [gui_focused_win], -1
        mov dword [gui_win_count], 0
        mov dword [gui_next_z], 0
        mov byte [gui_prev_mouse_btn], 0
        mov dword [gui_title_dbl_win], -1
        mov dword [gui_title_dbl_until], 0

        ; Clear window table
        mov edi, gui_win_table
        xor eax, eax
        mov ecx, (MAX_WINDOWS * WIN_STRUCT_SIZE) / 4
        rep stosd

        ; Post welcome notification
        mov esi, .notify_welcome
        xor edx, edx           ; default color
        call gui_notify

        ; Test/diagnostic markers on COM1 (regression tests key off these).
        mov esi, burrows_log_up
        call serial_print

gui_desktop_loop:
        ; Track idle time / trigger screensaver
        call scr_idle_tick

        ; Compose frame: background + windows + taskbar + menu + cursor
        call gui_compose

        ; Handle input
        call gui_handle_input

        ; Check exit flag
        cmp byte [gui_exit_flag], 1
        je .desktop_exit

        ; Run a ready GUI task. This returns immediately when none is ready;
        ; otherwise it switches to ring 3 and comes back here when the task
        ; blocks (or through gui_desktop_resume when it exits).
        call sched_start_ready
        jmp gui_desktop_loop

.desktop_exit:

        ; --- Exit desktop ---
        mov esi, burrows_log_down
        call serial_print
        call gui_exit_desktop
        ; If we were launched normally, ret works.
        ; If stack was reset by sys_exit after an app, jump to shell.
        ; Use shell_main unconditionally — it's always safe.
        jmp shell_main

;---------------------------------------
; gui_fault_recover - a ring 3 GUI app faulted; kill it, keep the desktop
;
; Entered from the exception handlers with the faulting task's kernel stack
; below us and interrupts off. Never returns: control goes back to the
; desktop loop, which repaints without the dead window.
;---------------------------------------
gui_fault_recover:
        cli
        mov esp, KERNEL_STACK

        call gui_fault_message

        ; Tear the task down (this also reaps its windows through
        ; gui_cleanup_pid) and restore the shell's view of the machine.
        call fault_kill_current_task

        ; Tell the test harness (and anyone on COM1) that containment worked.
        mov esi, burrows_log_fault
        call serial_print

        ; Post the toast after the teardown so the notification is not
        ; cleared along with the app's windows.
        mov esi, gui_fault_msg
        mov edx, 0x00B03030
        call gui_notify

        sti
        jmp gui_desktop_resume

;---------------------------------------
; gui_fault_message - build "<task> fault @ 0x<eip>" for the toast
;---------------------------------------
gui_fault_message:
        pushad
        mov edi, gui_fault_msg

        ; Task name, if the scheduler can find one
        call sched_get_current_task
        test eax, eax
        jz .gfm_no_name
        lea esi, [eax + TCB_NAME]
        mov ecx, 15
.gfm_name:
        lodsb
        test al, al
        jz .gfm_name_done
        stosb
        dec ecx
        jnz .gfm_name
        jmp .gfm_name_done
.gfm_no_name:
        mov esi, gui_str_fault_anon
        call gui_str_copy_edi
.gfm_name_done:
        mov esi, gui_str_fault_at
        call gui_str_copy_edi

        ; EIP as 8 hex digits
        mov eax, [exc_eip]
        mov ecx, 8
.gfm_hex:
        rol eax, 4
        mov ebx, eax
        and ebx, 0x0F
        cmp bl, 10
        jb .gfm_digit
        add bl, 'A' - 10 - '0'
.gfm_digit:
        add bl, '0'
        mov [edi], bl
        inc edi
        dec ecx
        jnz .gfm_hex
        mov byte [edi], 0
        popad
        ret

; gui_str_copy_edi - append a null-terminated string at EDI
gui_str_copy_edi:
        lodsb
        test al, al
        jz .gsc_done
        stosb
        jmp gui_str_copy_edi
.gsc_done:
        mov byte [edi], 0
        ret

gui_str_fault_anon: db "Application", 0
gui_str_fault_at:   db " fault @ 0x", 0
gui_fault_msg:      times 48 db 0

gui_desktop_resume:
        ; Reached when an external GUI app exits (SYS_EXIT -> here)
        ; Stack was reset to KERNEL_STACK by sys_exit
        call gui_cleanup_app_windows
        ; Restore VBE mode in case app changed it
        mov eax, GUI_SCREEN_W
        mov ebx, GUI_SCREEN_H
        mov ecx, GUI_BPP
        call vbe_set_mode
        ; Re-configure mouse for desktop
        mov dword [mouse_max_x], GUI_SCREEN_W - 1
        mov dword [mouse_max_y], GUI_SCREEN_H - 1
        mov dword [mouse_scale], 1
        mov dword [mouse_clamp], 6
        jmp gui_desktop_loop

.no_vbe:
        call vga_set_error
        mov esi, msg_vbe_no
        call vga_print
        call vga_reset_color
        ret

gui_exit_desktop:
        pushad
        mov byte [gui_desktop_active], 0
        mov byte [gui_exit_flag], 0
        mov byte [vbe_restore_pending], 1

        ; Reset mouse to text mode
        mov dword [mouse_max_x], 79
        mov dword [mouse_max_y], 24
        mov dword [mouse_scale], 1
        mov dword [mouse_clamp], 127
        mov dword [mouse_x], 40
        mov dword [mouse_y], 12

        ; Destroy all windows
        call gui_destroy_all_windows

        ; GUI apps cannot outlive the desktop; a surviving task would keep
        ; composing into the restored text console.
        call sched_kill_all_tasks

        ; Restore text mode
        call vbe_restore_text
        call vga_init
        popad
        ret

LOGIN_NAME_LEN  equ 16

; (login screen removed — single-user OS)
; Stub retained only so the label address is valid if referenced.
gui_login_screen:
        mov byte [login_authenticated], 1
        ret

%if 0  ; login screen body removed

.login_loop:
        ; Draw background — gradient blue
        mov eax, 0
        mov ebx, 0
        mov ecx, GUI_SCREEN_W
        mov edx, GUI_SCREEN_H
        mov esi, 0x00182848
        call gui_bb_fill_rect

        ; Draw login box shadow
        mov eax, LOGIN_BOX_X + 3
        mov ebx, LOGIN_BOX_Y + 3
        mov ecx, LOGIN_BOX_W
        mov edx, LOGIN_BOX_H
        mov esi, 0x00101828
        call gui_bb_fill_rect

        ; Draw login box background
        mov eax, LOGIN_BOX_X
        mov ebx, LOGIN_BOX_Y
        mov ecx, LOGIN_BOX_W
        mov edx, LOGIN_BOX_H
        mov esi, 0x00303848
        call gui_bb_fill_rect

        ; Draw title bar
        mov eax, LOGIN_BOX_X
        mov ebx, LOGIN_BOX_Y
        mov ecx, LOGIN_BOX_W
        mov edx, 28
        mov esi, 0x003366CC
        call gui_bb_fill_rect

        ; Title text
        mov eax, LOGIN_BOX_X + 80
        mov ebx, LOGIN_BOX_Y + 7
        mov esi, .login_title
        mov edx, 0x00FFFFFF
        call gui_bb_draw_string

        ; Draw user list (clickable names)
        xor ecx, ecx
.login_draw_users:
        cmp ecx, LOGIN_MAX_USERS
        jge .login_draw_pass

        ; Check if this user exists
        mov eax, ecx
        imul eax, LOGIN_NAME_LEN
        cmp byte [login_users + eax], 0
        je .login_next_user

        ; Highlight selected user
        push ecx
        cmp ecx, [login_sel_user]
        jne .login_no_highlight
        mov eax, LOGIN_BOX_X + 10
        mov ebx, ecx
        imul ebx, 20
        add ebx, LOGIN_BOX_Y + 40
        mov ecx, LOGIN_BOX_W - 20
        mov edx, 18
        mov esi, 0x004488CC
        call gui_bb_fill_rect
.login_no_highlight:
        pop ecx

        ; Draw user name
        push ecx
        mov eax, LOGIN_BOX_X + 20
        mov ebx, ecx
        imul ebx, 20
        add ebx, LOGIN_BOX_Y + 42
        mov esi, ecx
        imul esi, LOGIN_NAME_LEN
        add esi, login_users
        mov edx, 0x00DDDDDD
        call gui_bb_draw_string
        pop ecx

.login_next_user:
        inc ecx
        jmp .login_draw_users

.login_draw_pass:
        ; "Password:" label
        mov eax, LOGIN_BOX_X + 20
        mov ebx, LOGIN_BOX_Y + 125
        mov esi, .login_pass_label
        mov edx, 0x00AAAAAA
        call gui_bb_draw_string

        ; Password field background
        mov eax, LOGIN_BOX_X + 100
        mov ebx, LOGIN_BOX_Y + 120
        mov ecx, 180
        mov edx, 18
        mov esi, 0x00202838
        call gui_bb_fill_rect

        ; Draw password asterisks
        mov ecx, [login_pass_len]
        test ecx, ecx
        jz .login_no_stars
        mov eax, LOGIN_BOX_X + 104
        mov ebx, LOGIN_BOX_Y + 122
        mov edx, 0x00FFFFFF
.login_star_loop:
        push ecx
        push eax
        mov cl, '*'
        call gui_bb_draw_char
        pop eax
        add eax, 8
        pop ecx
        dec ecx
        jnz .login_star_loop
.login_no_stars:

        ; Draw "Login" button
        mov eax, LOGIN_BOX_X + 100
        mov ebx, LOGIN_BOX_Y + 155
        mov ecx, 80
        mov edx, 24
        mov esi, 0x003366CC
        call gui_bb_fill_rect

        mov eax, LOGIN_BOX_X + 120
        mov ebx, LOGIN_BOX_Y + 160
        mov esi, .login_btn_text
        mov edx, 0x00FFFFFF
        call gui_bb_draw_string

        ; Draw "Guest" button
        mov eax, LOGIN_BOX_X + 195
        mov ebx, LOGIN_BOX_Y + 155
        mov ecx, 80
        mov edx, 24
        mov esi, 0x00555566
        call gui_bb_fill_rect

        mov eax, LOGIN_BOX_X + 215
        mov ebx, LOGIN_BOX_Y + 160
        mov esi, .login_guest_text
        mov edx, 0x00CCCCCC
        call gui_bb_draw_string

        ; Draw error message if set
        cmp byte [login_error], 0
        je .login_no_error
        mov eax, LOGIN_BOX_X + 60
        mov ebx, LOGIN_BOX_Y + 185
        mov esi, .login_err_text
        mov edx, 0x00FF4444
        call gui_bb_draw_string
.login_no_error:

        ; Draw cursor
        mov eax, [mouse_x]
        mov ebx, [mouse_y]
        mov ecx, 2
        mov edx, 12
        mov esi, 0x00FFFFFF
        call gui_bb_fill_rect
        mov eax, [mouse_x]
        mov ebx, [mouse_y]
        mov ecx, 12
        mov edx, 2
        mov esi, 0x00FFFFFF
        call gui_bb_fill_rect

        ; Flip
        call gui_flip

        ; Handle input — check keyboard
        mov eax, [kb_read_idx]
        cmp eax, [kb_write_idx]
        je .login_check_mouse

        ; Read key
        mov eax, [kb_read_idx]
        movzx eax, byte [kb_buffer + eax]
        inc dword [kb_read_idx]
        and dword [kb_read_idx], 255

        ; Enter key?
        cmp al, 0x0D
        je .login_attempt

        ; Escape?
        cmp al, 0x1B
        je .login_cancelled

        ; Backspace?
        cmp al, 0x08
        jne .login_not_bs
        cmp dword [login_pass_len], 0
        je .login_loop
        dec dword [login_pass_len]
        jmp .login_loop
.login_not_bs:

        ; Tab — cycle users
        cmp al, 0x09
        jne .login_not_tab
        inc dword [login_sel_user]
        cmp dword [login_sel_user], LOGIN_MAX_USERS
        jl .login_tab_ok
        mov dword [login_sel_user], 0
.login_tab_ok:
        ; Skip empty user slots
        mov eax, [login_sel_user]
        imul eax, LOGIN_NAME_LEN
        cmp byte [login_users + eax], 0
        jne .login_loop
        inc dword [login_sel_user]
        cmp dword [login_sel_user], LOGIN_MAX_USERS
        jl .login_tab_ok
        mov dword [login_sel_user], 0
        jmp .login_loop
.login_not_tab:

        ; Printable character → add to password
        cmp al, 32
        jb .login_loop
        cmp al, 126
        ja .login_loop
        cmp dword [login_pass_len], LOGIN_PASS_LEN
        jge .login_loop
        mov edi, [login_pass_len]
        mov [login_pass_buf + edi], al
        inc dword [login_pass_len]
        mov byte [login_error], 0
        jmp .login_loop

.login_check_mouse:
        ; Button released? Reset debounce
        cmp byte [mouse_buttons], 0
        jne .login_mouse_pressed
        mov byte [.login_prev_btn], 0
        jmp .login_loop
.login_mouse_pressed:
        ; Debounce - only act on new click
        cmp byte [.login_prev_btn], 0
        jne .login_loop
        mov byte [.login_prev_btn], 1

        ; Check click on user names
        mov eax, [mouse_x]
        mov ebx, [mouse_y]
        cmp eax, LOGIN_BOX_X + 10
        jl .login_check_btns
        cmp eax, LOGIN_BOX_X + LOGIN_BOX_W - 10
        jg .login_check_btns
        ; Which user row?
        sub ebx, (LOGIN_BOX_Y + 40)
        js .login_check_btns
        cmp ebx, LOGIN_MAX_USERS * 20
        jge .login_check_btns
        ; Row = ebx / 20
        xor edx, edx
        mov ecx, 20
        div ecx             ; eax = row
        ; Check user exists
        mov ecx, eax
        imul ecx, LOGIN_NAME_LEN
        cmp byte [login_users + ecx], 0
        je .login_check_btns
        mov [login_sel_user], eax
        mov dword [login_pass_len], 0
        mov byte [login_error], 0
        jmp .login_loop

.login_check_btns:
        mov eax, [mouse_x]
        mov ebx, [mouse_y]
        ; Login button: (BOX_X+100, BOX_Y+155) to (BOX_X+180, BOX_Y+179)
        cmp eax, LOGIN_BOX_X + 100
        jl .login_check_guest
        cmp eax, LOGIN_BOX_X + 180
        jg .login_check_guest
        cmp ebx, LOGIN_BOX_Y + 155
        jl .login_check_guest
        cmp ebx, LOGIN_BOX_Y + 179
        jg .login_check_guest
        jmp .login_attempt

.login_check_guest:
        ; Guest button: (BOX_X+195, BOX_Y+155) to (BOX_X+275, BOX_Y+179)
        cmp eax, LOGIN_BOX_X + 195
        jl .login_release
        cmp eax, LOGIN_BOX_X + 275
        jg .login_release
        cmp ebx, LOGIN_BOX_Y + 155
        jl .login_release
        cmp ebx, LOGIN_BOX_Y + 179
        jg .login_release
        ; Guest login — no password needed
        mov esi, .login_guest_name
        mov edi, login_current_user
        mov ecx, LOGIN_NAME_LEN
        rep movsb
        mov byte [login_authenticated], 1
        popad
        ret

.login_release:
        jmp .login_loop

.login_attempt:
        ; Compare entered password with stored password for selected user
        mov eax, [login_sel_user]
        imul eax, LOGIN_PASS_LEN
        lea esi, [login_passwords + eax]
        lea edi, [login_pass_buf]
        mov ecx, [login_pass_len]

        ; First check: is the stored password empty? (no password required)
        cmp byte [esi], 0
        jne .login_check_len
        ; Empty stored password — accept if user typed nothing
        test ecx, ecx
        jz .login_success
        ; User typed something, but password is empty — wrong
        jmp .login_wrong

.login_check_len:
        ; Get stored password length
        push ecx
        push esi
        xor ecx, ecx
.login_slen:
        cmp byte [esi + ecx], 0
        je .login_slen_done
        inc ecx
        jmp .login_slen
.login_slen_done:
        mov edx, ecx   ; edx = stored password length
        pop esi
        pop ecx
        ; Compare lengths
        cmp ecx, edx
        jne .login_wrong
        ; Compare bytes
.login_cmp:
        test ecx, ecx
        jz .login_success
        mov al, [esi]
        cmp al, [edi]
        jne .login_wrong
        inc esi
        inc edi
        dec ecx
        jmp .login_cmp

.login_success:
        ; Copy username to current user
        mov eax, [login_sel_user]
        imul eax, LOGIN_NAME_LEN
        lea esi, [login_users + eax]
        mov edi, login_current_user
        mov ecx, LOGIN_NAME_LEN
        rep movsb
        mov byte [login_authenticated], 1
        popad
        ret

.login_wrong:
        mov byte [login_error], 1
        mov dword [login_pass_len], 0
        jmp .login_loop

%endif  ; end of removed login screen body

login_current_user: db "user", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
login_authenticated: db 1

;=======================================================================
; FILE DIALOGS
;=======================================================================
; Reusable Open / Save file dialogs for GUI apps
; gui_file_open_dialog: Modal file picker
;   Input:  ESI = title string (e.g. "Open File"), EDX = type filter (0=all)
;   Output: EAX = 1 if file selected (name in fdlg_result), 0 if cancelled
; gui_file_save_dialog: Modal save-as dialog
;   Input:  ESI = title string, EDX = type filter (0=all)
;   Output: EAX = 1 if confirmed (name in fdlg_result), 0 if cancelled

FDLG_X          equ 120
FDLG_Y          equ 60
FDLG_W          equ 400
FDLG_H          equ 320
FDLG_MAX_FILES  equ 48
FDLG_VISIBLE    equ 14
FDLG_LIST_X     equ (FDLG_X + 10)
FDLG_LIST_Y     equ (FDLG_Y + 40)
FDLG_LIST_W     equ 380
FDLG_LIST_H     equ (FDLG_VISIBLE * 18)
FDLG_NAME_X     equ (FDLG_X + 10)
FDLG_NAME_Y     equ (FDLG_Y + FDLG_H - 56)
FDLG_NAME_W     equ 380
FDLG_NAME_H     equ 20
FDLG_BTN_OK_X   equ (FDLG_X + FDLG_W - 170)
FDLG_BTN_OK_Y   equ (FDLG_Y + FDLG_H - 30)
FDLG_BTN_CAN_X  equ (FDLG_X + FDLG_W - 80)
FDLG_BTN_CAN_Y  equ (FDLG_Y + FDLG_H - 30)

;---------------------------------------
; gui_file_open_dialog
; ESI = title, EDX = type filter (0=all, 1=text, 3=exec)
; Returns: EAX = 1 + fdlg_result filled, or EAX = 0 cancelled
;---------------------------------------
gui_file_open_dialog:
        mov [fdlg_title], esi
        mov [fdlg_filter], edx
        mov byte [fdlg_mode], 0        ; open mode
        mov byte [fdlg_name_buf], 0
        mov byte [fdlg_name_len], 0
        jmp fdlg_run

;---------------------------------------
; gui_file_save_dialog
; ESI = title, EDX = type filter
; Returns: EAX = 1 + fdlg_result filled, or EAX = 0 cancelled
;---------------------------------------
gui_file_save_dialog:
        mov [fdlg_title], esi
        mov [fdlg_filter], edx
        mov byte [fdlg_mode], 1        ; save mode
        mov byte [fdlg_name_buf], 0
        mov byte [fdlg_name_len], 0
        jmp fdlg_run

;---------------------------------------
; fdlg_run — main dialog loop (shared by open/save)
;---------------------------------------
fdlg_run:
        pushad
        mov dword [fdlg_scroll], 0
        mov dword [fdlg_sel], 0
        mov byte [fdlg_prev_btn], 0

        ; Enumerate files from filesystem
        call fdlg_load_files

.fdlg_loop:
        ; --- Draw the dialog ---
        call fdlg_draw

        ; Flip display
        call gui_flip

        ; Small delay
        mov eax, SYS_SLEEP
        mov ebx, 2
        int 0x80

        ; --- Check keyboard ---
        mov eax, SYS_GETCHAR
        int 0x80
        cmp al, 0
        je .fdlg_mouse

        cmp al, 27             ; Escape
        je .fdlg_cancel

        cmp al, 13             ; Enter
        je .fdlg_confirm

        cmp al, KEY_UP
        je .fdlg_key_up
        cmp al, KEY_DOWN
        je .fdlg_key_down

        ; If save mode, type into name field
        cmp byte [fdlg_mode], 1
        jne .fdlg_mouse

        cmp al, 8              ; Backspace
        je .fdlg_backspace
        cmp al, 32
        jb .fdlg_mouse
        cmp al, 126
        ja .fdlg_mouse
        ; Add character to name buf
        movzx ecx, byte [fdlg_name_len]
        cmp ecx, 59
        jge .fdlg_mouse
        mov [fdlg_name_buf + ecx], al
        inc byte [fdlg_name_len]
        movzx ecx, byte [fdlg_name_len]
        mov byte [fdlg_name_buf + ecx], 0
        jmp .fdlg_loop

.fdlg_backspace:
        cmp byte [fdlg_name_len], 0
        je .fdlg_loop
        dec byte [fdlg_name_len]
        movzx ecx, byte [fdlg_name_len]
        mov byte [fdlg_name_buf + ecx], 0
        jmp .fdlg_loop

.fdlg_key_up:
        cmp dword [fdlg_sel], 0
        je .fdlg_loop
        dec dword [fdlg_sel]
        ; Scroll up if needed
        mov eax, [fdlg_sel]
        cmp eax, [fdlg_scroll]
        jge .fdlg_sel_to_name
        mov [fdlg_scroll], eax
        jmp .fdlg_sel_to_name

.fdlg_key_down:
        mov eax, [fdlg_sel]
        inc eax
        cmp eax, [fdlg_file_count]
        jge .fdlg_loop
        mov [fdlg_sel], eax
        ; Scroll down if needed
        sub eax, FDLG_VISIBLE
        inc eax
        cmp eax, [fdlg_scroll]
        jle .fdlg_sel_to_name
        mov [fdlg_scroll], eax
        jmp .fdlg_sel_to_name

.fdlg_sel_to_name:
        ; Copy selected filename into name buf
        mov eax, [fdlg_sel]
        imul eax, 64
        lea esi, [fdlg_files + eax]
        mov edi, fdlg_name_buf
        xor ecx, ecx
.fdlg_cpn:
        mov al, [esi + ecx]
        mov [edi + ecx], al
        test al, al
        jz .fdlg_cpn_done
        inc ecx
        cmp ecx, 59
        jb .fdlg_cpn
        mov byte [edi + ecx], 0
.fdlg_cpn_done:
        mov [fdlg_name_len], cl
        jmp .fdlg_loop

.fdlg_mouse:
        ; Check mouse button
        cmp byte [mouse_buttons], 1
        jne .fdlg_mouse_up
        cmp byte [fdlg_prev_btn], 1
        je .fdlg_loop           ; debounce — already pressed
        mov byte [fdlg_prev_btn], 1

        ; Check click in file list area
        mov eax, [mouse_x]
        mov ebx, [mouse_y]
        cmp eax, FDLG_LIST_X
        jl .fdlg_check_btns
        cmp eax, FDLG_LIST_X + FDLG_LIST_W
        jg .fdlg_check_btns
        cmp ebx, FDLG_LIST_Y
        jl .fdlg_check_btns
        cmp ebx, FDLG_LIST_Y + FDLG_LIST_H
        jg .fdlg_check_btns
        ; Clicked in file list — compute index
        sub ebx, FDLG_LIST_Y
        xor edx, edx
        push eax
        mov eax, ebx
        mov ecx, 18
        div ecx
        add eax, [fdlg_scroll]
        cmp eax, [fdlg_file_count]
        jge .fdlg_pop_loop
        mov [fdlg_sel], eax
        pop eax
        jmp .fdlg_sel_to_name
.fdlg_pop_loop:
        pop eax
        jmp .fdlg_loop

.fdlg_check_btns:
        mov eax, [mouse_x]
        mov ebx, [mouse_y]
        ; OK button (60×22)
        cmp eax, FDLG_BTN_OK_X
        jl .fdlg_check_cancel
        cmp eax, FDLG_BTN_OK_X + 60
        jg .fdlg_check_cancel
        cmp ebx, FDLG_BTN_OK_Y
        jl .fdlg_check_cancel
        cmp ebx, FDLG_BTN_OK_Y + 22
        jg .fdlg_check_cancel
        jmp .fdlg_confirm

.fdlg_check_cancel:
        ; Cancel button (60×22)
        cmp eax, FDLG_BTN_CAN_X
        jl .fdlg_loop
        cmp eax, FDLG_BTN_CAN_X + 60
        jg .fdlg_loop
        cmp ebx, FDLG_BTN_CAN_Y
        jl .fdlg_loop
        cmp ebx, FDLG_BTN_CAN_Y + 22
        jg .fdlg_loop
        jmp .fdlg_cancel

.fdlg_mouse_up:
        mov byte [fdlg_prev_btn], 0
        jmp .fdlg_loop

.fdlg_confirm:
        ; Copy name_buf → fdlg_result
        cmp byte [fdlg_name_buf], 0
        je .fdlg_loop           ; no name entered/selected
        mov esi, fdlg_name_buf
        mov edi, fdlg_result
        xor ecx, ecx
.fdlg_cp_res:
        mov al, [esi + ecx]
        mov [edi + ecx], al
        test al, al
        jz .fdlg_ok_ret
        inc ecx
        cmp ecx, 63
        jb .fdlg_cp_res
        mov byte [edi + ecx], 0
.fdlg_ok_ret:
        popad
        mov eax, 1
        ret

.fdlg_cancel:
        popad
        xor eax, eax
        ret

;---------------------------------------
; fdlg_load_files — enumerate FS into fdlg_files array
;---------------------------------------
fdlg_load_files:
        pushad
        mov dword [fdlg_file_count], 0

        call hbfs_load_root_dir
        call hbfs_get_max_entries
        mov ecx, eax            ; max entries
        mov esi, hbfs_dir_buf
        xor ebp, ebp            ; file index counter

.fl_loop:
        cmp ebp, FDLG_MAX_FILES
        jge .fl_done
        cmp ecx, 0
        jle .fl_done

        movzx eax, byte [esi + DIRENT_TYPE]
        cmp eax, FTYPE_FREE
        je .fl_next

        ; Apply type filter
        mov edx, [fdlg_filter]
        test edx, edx
        jz .fl_accept           ; 0 = all types
        cmp eax, edx
        jne .fl_next

.fl_accept:
        ; Copy filename into fdlg_files[ebp*64]
        push ecx
        push esi
        mov eax, ebp
        shl eax, 6              ; *64
        lea edi, [fdlg_files + eax]
        mov ecx, 63
.fl_cpname:
        lodsb
        stosb
        test al, al
        jz .fl_pad
        dec ecx
        jnz .fl_cpname
        mov byte [edi], 0
.fl_pad:
        ; Store type for icon display
        pop esi
        push esi
        movzx eax, byte [esi + DIRENT_TYPE]
        mov edx, ebp
        mov [fdlg_ftypes + edx], al
        ; Store size
        mov eax, [esi + DIRENT_SIZE]
        mov [fdlg_fsizes + edx * 4], eax

        pop esi
        pop ecx
        inc ebp

.fl_next:
        add esi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jmp .fl_loop

.fl_done:
        mov [fdlg_file_count], ebp
        popad
        ret

;---------------------------------------
; fdlg_draw — render the file dialog
;---------------------------------------
fdlg_draw:
        pushad

        ; Dim overlay behind dialog
        mov eax, 0
        mov ebx, 0
        mov ecx, GUI_SCREEN_W
        mov edx, GUI_SCREEN_H
        mov esi, 0x80000000     ; semi-dark
        call gui_bb_fill_rect

        ; Dialog shadow
        mov eax, FDLG_X + 3
        mov ebx, FDLG_Y + 3
        mov ecx, FDLG_W
        mov edx, FDLG_H
        mov esi, 0x00222222
        call gui_bb_fill_rect

        ; Dialog background
        mov eax, FDLG_X
        mov ebx, FDLG_Y
        mov ecx, FDLG_W
        mov edx, FDLG_H
        mov esi, 0x00D4D0C8     ; classic grey
        call gui_bb_fill_rect

        ; Title bar
        mov eax, FDLG_X
        mov ebx, FDLG_Y
        mov ecx, FDLG_W
        mov edx, 24
        mov esi, 0x000A246A     ; dark blue
        call gui_bb_fill_rect

        ; Title text
        mov eax, FDLG_X + 8
        mov ebx, FDLG_Y + 4
        mov esi, [fdlg_title]
        mov edx, 0x00FFFFFF
        call gui_bb_draw_string

        ; --- File list background ---
        mov eax, FDLG_LIST_X
        mov ebx, FDLG_LIST_Y
        mov ecx, FDLG_LIST_W
        mov edx, FDLG_LIST_H
        mov esi, 0x00FFFFFF     ; white
        call gui_bb_fill_rect

        ; File list border
        mov eax, FDLG_LIST_X - 1
        mov ebx, FDLG_LIST_Y - 1
        mov ecx, FDLG_LIST_W + 2
        mov edx, FDLG_LIST_H + 2
        mov esi, 0x00808080
        call gui_bb_rect

        ; --- Draw file entries ---
        xor ebp, ebp            ; visible index
        mov ecx, [fdlg_scroll]
.fd_entry:
        cmp ebp, FDLG_VISIBLE
        jge .fd_entries_done
        cmp ecx, [fdlg_file_count]
        jge .fd_entries_done

        ; Highlight selected row
        mov eax, ecx
        cmp eax, [fdlg_sel]
        jne .fd_no_hl

        ; Draw selection highlight
        push ecx
        mov eax, FDLG_LIST_X
        mov ebx, ebp
        imul ebx, 18
        add ebx, FDLG_LIST_Y
        push ebx
        mov ecx, FDLG_LIST_W
        mov edx, 18
        mov esi, 0x000A246A     ; dark blue highlight
        call gui_bb_fill_rect
        pop ebx
        pop ecx

        ; Draw filename in white on blue
        push ecx
        mov eax, ecx
        shl eax, 6
        lea esi, [fdlg_files + eax]
        mov eax, FDLG_LIST_X + 20
        mov edx, 0x00FFFFFF
        add ebx, 1
        call gui_bb_draw_string
        pop ecx

        ; Draw type icon letter
        push ecx
        movzx edx, byte [fdlg_ftypes + ecx]
        mov eax, FDLG_LIST_X + 4
        sub ebx, 1
        cmp edx, FTYPE_EXEC
        je .fd_icon_exec
        cmp edx, FTYPE_DIR
        je .fd_icon_dir
        ; Text file icon
        mov cl, 0xFE            ; ■ block
        mov edx, 0x00FFFFFF
        jmp .fd_draw_icon
.fd_icon_exec:
        mov cl, 0x10            ; ► arrow
        mov edx, 0x0000FF00
        jmp .fd_draw_icon
.fd_icon_dir:
        mov cl, 0x07            ; folder-like
        mov edx, 0x00FFFF00
.fd_draw_icon:
        call gui_bb_draw_char
        pop ecx
        jmp .fd_next_entry

.fd_no_hl:
        ; Draw filename in black on white
        push ecx
        mov eax, ecx
        shl eax, 6
        lea esi, [fdlg_files + eax]
        mov eax, FDLG_LIST_X + 20
        mov ebx, ebp
        imul ebx, 18
        add ebx, FDLG_LIST_Y + 1
        mov edx, 0x00000000
        call gui_bb_draw_string

        ; Draw type icon
        pop ecx
        push ecx
        movzx edx, byte [fdlg_ftypes + ecx]
        mov eax, FDLG_LIST_X + 4
        mov ebx, ebp
        imul ebx, 18
        add ebx, FDLG_LIST_Y
        cmp edx, FTYPE_EXEC
        je .fd_icon_exec2
        cmp edx, FTYPE_DIR
        je .fd_icon_dir2
        mov cl, 0xFE
        mov edx, 0x00808080
        jmp .fd_draw_icon2
.fd_icon_exec2:
        mov cl, 0x10
        mov edx, 0x00008000
        jmp .fd_draw_icon2
.fd_icon_dir2:
        mov cl, 0x07
        mov edx, 0x00B0B000
.fd_draw_icon2:
        call gui_bb_draw_char
        pop ecx

.fd_next_entry:
        inc ebp
        inc ecx
        jmp .fd_entry

.fd_entries_done:
        ; --- Filename label ---
        mov eax, FDLG_NAME_X
        mov ebx, FDLG_NAME_Y - 16
        mov esi, .fd_name_label
        mov edx, 0x00000000
        call gui_bb_draw_string

        ; --- Filename input box ---
        mov eax, FDLG_NAME_X
        mov ebx, FDLG_NAME_Y
        mov ecx, FDLG_NAME_W
        mov edx, FDLG_NAME_H
        mov esi, 0x00FFFFFF
        call gui_bb_fill_rect

        ; Border
        mov eax, FDLG_NAME_X - 1
        mov ebx, FDLG_NAME_Y - 1
        mov ecx, FDLG_NAME_W + 2
        mov edx, FDLG_NAME_H + 2
        mov esi, 0x00808080
        call gui_bb_rect

        ; Filename text
        mov eax, FDLG_NAME_X + 4
        mov ebx, FDLG_NAME_Y + 2
        mov esi, fdlg_name_buf
        mov edx, 0x00000000
        call gui_bb_draw_string

        ; Cursor blink in name field (save mode)
        cmp byte [fdlg_mode], 1
        jne .fd_no_cursor
        movzx eax, byte [fdlg_name_len]
        shl eax, 3              ; * FONT_W
        add eax, FDLG_NAME_X + 4
        mov ebx, FDLG_NAME_Y + 2
        mov cl, '_'
        mov edx, 0x00000000
        call gui_bb_draw_char
.fd_no_cursor:

        ; --- OK Button ---
        mov eax, FDLG_BTN_OK_X
        mov ebx, FDLG_BTN_OK_Y
        mov ecx, 60
        mov edx, 22
        mov esi, 0x00D4D0C8
        call gui_bb_button_3d
        mov eax, FDLG_BTN_OK_X + 8
        mov ebx, FDLG_BTN_OK_Y + 3
        cmp byte [fdlg_mode], 0
        jne .fd_save_btn
        mov esi, .fd_btn_open
        jmp .fd_draw_ok
.fd_save_btn:
        mov esi, .fd_btn_save
.fd_draw_ok:
        mov edx, 0x00000000
        call gui_bb_draw_string

        ; --- Cancel Button ---
        mov eax, FDLG_BTN_CAN_X
        mov ebx, FDLG_BTN_CAN_Y
        mov ecx, 60
        mov edx, 22
        mov esi, 0x00D4D0C8
        call gui_bb_button_3d
        mov eax, FDLG_BTN_CAN_X + 8
        mov ebx, FDLG_BTN_CAN_Y + 3
        mov esi, .fd_btn_cancel
        mov edx, 0x00000000
        call gui_bb_draw_string

        ; --- Scroll indicator ---
        mov eax, [fdlg_file_count]
        cmp eax, FDLG_VISIBLE
        jle .fd_no_scroll
        ; Show "▲ / ▼" indicator
        mov eax, FDLG_LIST_X + FDLG_LIST_W - 40
        mov ebx, FDLG_LIST_Y - 14
        mov esi, .fd_scroll_ind
        mov edx, 0x00808080
        call gui_bb_draw_string
.fd_no_scroll:

        ; Draw mouse cursor over dialog
        call gui_draw_cursor

        popad
        ret

.fd_name_label: db "File name:", 0
.fd_btn_open:   db " Open", 0
.fd_btn_save:   db " Save", 0
.fd_btn_cancel: db "Cancel", 0
.fd_scroll_ind: db 24, "/", 25, 0      ; ↑/↓

; ----- File dialog data -----
fdlg_title:      dd 0
fdlg_filter:     dd 0
fdlg_mode:       db 0          ; 0=open, 1=save
fdlg_scroll:     dd 0
fdlg_sel:        dd 0
fdlg_file_count: dd 0
fdlg_prev_btn:   db 0
fdlg_name_len:   db 0
fdlg_name_buf:   times 64 db 0
fdlg_result:     times 64 db 0
fdlg_files:      times (FDLG_MAX_FILES * 64) db 0  ; filenames
fdlg_ftypes:     times FDLG_MAX_FILES db 0         ; file types
fdlg_fsizes:     times FDLG_MAX_FILES dd 0         ; file sizes

;=======================================================================
; COMPOSITOR
;=======================================================================

gui_compose:
        pushad
        call gui_draw_background
        call gui_draw_desktop_icons
        call gui_compose_surfaces   ; v10: blit IPC surface layer
        call gui_draw_all_windows
        call gui_draw_taskbar
        cmp byte [gui_menu_open], 1
        jne .no_menu
        call gui_draw_menu
        call gui_draw_submenu
.no_menu:
        cmp byte [gui_ctx_open], 1
        jne .no_ctx
        call gui_draw_ctx_menu
.no_ctx:
        call gui_draw_notifications
        call gui_draw_notify_center
        call gui_draw_switcher
        call gui_draw_cursor
        call gui_flip
        popad
        ret

;---------------------------------------
; gui_draw_switcher - Alt+Tab overlay listing windows on the active workspace
;---------------------------------------
gui_draw_switcher:
        pushad
        mov eax, [gui_switcher_until]
        test eax, eax
        jz .dsw_done
        cmp [tick_count], eax
        jb .dsw_draw
        mov dword [gui_switcher_until], 0
        jmp .dsw_done

.dsw_draw:
        call gui_target_screen

        ; Collect the windows shown on this workspace.
        mov dword [gui_sw_count], 0
        xor ecx, ecx
        mov esi, gui_win_table
.dsw_collect:
        cmp ecx, MAX_WINDOWS
        jge .dsw_collected
        mov eax, [esi + WIN_FLAGS]
        and eax, 0x03
        cmp eax, 0x03
        jne .dsw_next
        movzx eax, byte [esi + WIN_WORKSPACE]
        cmp al, 0xFF
        je .dsw_next
        cmp al, [gui_active_workspace]
        jne .dsw_next
        mov eax, [gui_sw_count]
        cmp eax, GUI_SW_MAX
        jge .dsw_collected
        mov [gui_sw_list + eax*4], ecx
        inc dword [gui_sw_count]
.dsw_next:
        add esi, WIN_STRUCT_SIZE
        inc ecx
        jmp .dsw_collect
.dsw_collected:
        cmp dword [gui_sw_count], 0
        je .dsw_done

        ; Panel geometry: one 22px row per window plus a title row.
        mov eax, [gui_sw_count]
        imul eax, GUI_SW_ROW_H
        add eax, GUI_SW_ROW_H
        mov [gui_sw_h], eax
        mov dword [gui_sw_w], GUI_SW_W
        mov eax, GUI_SCREEN_W - GUI_SW_W
        shr eax, 1
        mov [gui_sw_x], eax
        mov eax, GUI_SCREEN_H
        sub eax, [gui_sw_h]
        shr eax, 1
        mov [gui_sw_y], eax

        ; Shadow
        mov eax, [gui_sw_x]
        add eax, 4
        mov ebx, [gui_sw_y]
        add ebx, 4
        mov ecx, [gui_sw_w]
        mov edx, [gui_sw_h]
        mov esi, 0x00101828
        call gui_bb_fill_rect

        ; Panel body and a highlight strip along the top edge
        mov eax, [gui_sw_x]
        mov ebx, [gui_sw_y]
        mov ecx, [gui_sw_w]
        mov edx, [gui_sw_h]
        mov esi, [gui_theme + THEME_WINDOW_BG]
        call gui_bb_fill_rect
        mov eax, [gui_sw_x]
        mov ebx, [gui_sw_y]
        mov ecx, [gui_sw_w]
        mov edx, GUI_SW_ROW_H
        mov esi, [gui_theme + THEME_MENU_HIGHLIGHT]
        call gui_bb_fill_rect

        ; Panel title
        mov eax, [gui_sw_x]
        add eax, 8
        mov ebx, [gui_sw_y]
        add ebx, 5
        mov esi, gui_str_switcher
        mov edx, [gui_theme + THEME_TITLE_TEXT]
        call gui_bb_draw_string

        ; One row per window
        xor ebp, ebp
.dsw_rows:
        cmp ebp, [gui_sw_count]
        jge .dsw_done

        mov eax, ebp
        imul eax, GUI_SW_ROW_H
        add eax, [gui_sw_y]
        add eax, GUI_SW_ROW_H
        mov [gui_sw_row_y], eax

        ; The focused window gets the highlight colour and light text.
        mov esi, [gui_theme + THEME_WINDOW_BG]
        mov dword [gui_sw_text], 0x00202020
        mov eax, [gui_sw_list + ebp*4]
        cmp eax, [gui_focused_win]
        jne .dsw_row_bg
        mov esi, [gui_theme + THEME_ACCENT]
        mov dword [gui_sw_text], 0x00FFFFFF
.dsw_row_bg:
        mov eax, [gui_sw_x]
        add eax, 4
        mov ebx, [gui_sw_row_y]
        mov ecx, GUI_SW_W - 8
        mov edx, GUI_SW_ROW_H - 2
        call gui_bb_fill_rect

        ; Row label, truncated to the panel width
        mov esi, [gui_sw_list + ebp*4]
        imul esi, WIN_STRUCT_SIZE
        add esi, gui_win_table
        add esi, WIN_TITLE
        mov edi, gui_title_buf
        mov ecx, 15
        call gui_copy_trunc
        mov eax, [gui_sw_x]
        add eax, 12
        mov ebx, [gui_sw_row_y]
        add ebx, 5
        mov esi, gui_title_buf
        mov edx, [gui_sw_text]
        call gui_bb_draw_string

        inc ebp
        jmp .dsw_rows
.dsw_done:
        popad
        ret

;---------------------------------------
gui_draw_background:
        pushad
        call gui_target_screen

        ; Vertical gradient derived from the theme's desktop colour: the top
        ; scanline is the theme colour and the bottom is 1.5x brighter, so a
        ; theme change repaints the wallpaper too.
        mov eax, [gui_theme + THEME_DESKTOP_BG]

        ; The Settings applet can swap the gradient for a flat fill.
        cmp dword [gui_wallpaper], 1
        jne .gb_gradient
        mov edi, GUI_BACKBUF
        mov ecx, GUI_SCREEN_W * GUI_SCREEN_H
        cld
        rep stosd
        popad
        ret
.gb_gradient:

        mov ecx, eax
        shr ecx, 16
        and ecx, 0xFF
        mov [.gb_r0], ecx
        mov edi, ecx
        shr edi, 1
        add edi, ecx
        cmp edi, 255
        jbe .gb_r_ok
        mov edi, 255
.gb_r_ok:
        mov [.gb_r1], edi

        mov ecx, eax
        shr ecx, 8
        and ecx, 0xFF
        mov [.gb_g0], ecx
        mov edi, ecx
        shr edi, 1
        add edi, ecx
        cmp edi, 255
        jbe .gb_g_ok
        mov edi, 255
.gb_g_ok:
        mov [.gb_g1], edi

        mov ecx, eax
        and ecx, 0xFF
        mov [.gb_b0], ecx
        mov edi, ecx
        shr edi, 1
        add edi, ecx
        cmp edi, 255
        jbe .gb_b_ok
        mov edi, 255
.gb_b_ok:
        mov [.gb_b1], edi

        xor ebx, ebx                    ; y
.gb_loop:
        ; Red
        mov eax, [.gb_r1]
        sub eax, [.gb_r0]
        imul eax, ebx
        xor edx, edx
        mov ecx, GUI_SCREEN_H - 1
        div ecx
        add eax, [.gb_r0]
        shl eax, 16
        mov [.gb_color], eax
        ; Green
        mov eax, [.gb_g1]
        sub eax, [.gb_g0]
        imul eax, ebx
        xor edx, edx
        div ecx
        add eax, [.gb_g0]
        shl eax, 8
        or [.gb_color], eax
        ; Blue
        mov eax, [.gb_b1]
        sub eax, [.gb_b0]
        imul eax, ebx
        xor edx, edx
        div ecx
        add eax, [.gb_b0]
        or [.gb_color], eax

        mov edi, ebx
        imul edi, GUI_PITCH
        add edi, GUI_BACKBUF
        mov eax, [.gb_color]
        mov ecx, GUI_SCREEN_W
        cld
        rep stosd
        inc ebx
        cmp ebx, GUI_SCREEN_H
        jl .gb_loop

        ; Keep the taskbar area from carrying the gradient behind it
        popad
        ret
.gb_color: dd 0
.gb_r0: dd 0
.gb_r1: dd 0
.gb_g0: dd 0
.gb_g1: dd 0
.gb_b0: dd 0
.gb_b1: dd 0

;---------------------------------------
; gui_compose_surfaces  — blit IPC surface table into back buffer
; Iterates ipc_surface_table in slot order (slot 0 = bottom z).
; Each SURF_VISIBLE surface is row-blitted with full screen clipping.
; Scratch variables are safe here: compositor is single-task.
;---------------------------------------
gui_compose_surfaces:
        push eax
        push ebx
        push ecx
        push edx
        push esi
        push edi
        push ebp
        xor ebp, ebp            ; EBP = slot index

.gcs_slot:
        cmp ebp, SURF_MAX
        jge .gcs_done

        ; Entry address -> EAX
        mov eax, ebp
        imul eax, SURF_ENTRY_SIZE
        add eax, ipc_surface_table

        cmp dword [eax + SURF_STATE], SURF_VISIBLE
        jne .gcs_next

        mov esi, [eax + SURF_BUF_PTR]
        test esi, esi
        jz .gcs_next

        ; Load geometry into scratch vars
        mov ebx, [eax + SURF_X]
        mov edx, [eax + SURF_Y]
        mov ecx, [eax + SURF_WIDTH]
        mov [.surf_stride], ecx     ; original row stride (pixels)
        mov ecx, [eax + SURF_HEIGHT]
        mov [.blit_h], ecx

        ; --- Clip top: dst_y < 0 ---
        test edx, edx
        jns .gcs_clip_bot
        neg edx                     ; rows_skip = -dst_y
        sub dword [.blit_h], edx
        jle .gcs_next
        mov eax, edx
        imul eax, [.surf_stride]
        shl eax, 2                  ; bytes = rows_skip * stride * 4
        add esi, eax
        xor edx, edx

.gcs_clip_bot:
        ; blit_h = min(blit_h, GUI_SCREEN_H - dst_y)
        mov eax, GUI_SCREEN_H
        sub eax, edx
        cmp [.blit_h], eax
        jle .gcs_clip_left
        mov [.blit_h], eax

.gcs_clip_left:
        cmp dword [.blit_h], 0
        jle .gcs_next
        ; blit_w starts as full stride
        mov ecx, [.surf_stride]
        mov [.blit_w], ecx

        ; --- Clip left: dst_x < 0 ---
        test ebx, ebx
        jns .gcs_clip_right
        neg ebx                     ; cols_skip = -dst_x
        sub dword [.blit_w], ebx
        jle .gcs_next
        lea esi, [esi + ebx*4]      ; advance src ptr
        xor ebx, ebx

.gcs_clip_right:
        ; blit_w = min(blit_w, GUI_SCREEN_W - dst_x)
        mov eax, GUI_SCREEN_W
        sub eax, ebx
        cmp [.blit_w], eax
        jle .gcs_do_blit
        mov [.blit_w], eax

.gcs_do_blit:
        cmp dword [.blit_w], 0
        jle .gcs_next

        ; Destination pointer: GUI_BACKBUF + (dst_y * GUI_SCREEN_W + dst_x) * 4
        mov eax, edx
        imul eax, GUI_SCREEN_W
        add eax, ebx
        shl eax, 2
        add eax, GUI_BACKBUF
        mov edi, eax                ; EDI = dst row pointer

        ; Source row stride in bytes
        mov eax, [.surf_stride]
        shl eax, 2
        mov [.src_stride_b], eax

.gcs_row:
        ; Copy one row
        push esi
        push edi
        mov ecx, [.blit_w]
        cld
        rep movsd
        pop edi
        pop esi
        add esi, [.src_stride_b]
        add edi, GUI_PITCH
        dec dword [.blit_h]
        jnz .gcs_row

.gcs_next:
        inc ebp
        jmp .gcs_slot

.gcs_done:
        pop ebp
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

; Scratch dwords (single-task compositor only)
.surf_stride    dd 0
.blit_h         dd 0
.blit_w         dd 0
.src_stride_b   dd 0

;---------------------------------------
; Desktop icon layout constants
;---------------------------------------
ICON_TEXT_H     equ 14          ; space below icon for label
ICON_CELL_W     equ 76          ; grid cell width
ICON_CELL_H     equ 64          ; grid cell height
ICON_SIZE       equ 32          ; icon box width/height
ICON_START_X    equ 12          ; first column x
ICON_START_Y    equ 12          ; first row y
ICON_COLS       equ 1           ; single column of icons
ICON_COUNT      equ 9           ; number of desktop icons

;---------------------------------------
; gui_draw_icon32 - Draw a 32x32 application icon
;   EAX = icon index, EBX = x, ECX = y
;---------------------------------------
gui_draw_icon32:
        pushad
        mov [.i_idx], eax
        mov [.i_x], ebx
        mov [.i_y], ecx
        cmp eax, 0
        je .i_term
        cmp eax, 1
        je .i_folder
        cmp eax, 2
        je .i_globe
        cmp eax, 3
        je .i_edit
        cmp eax, 4
        je .i_calc
        cmp eax, 5
        je .i_paint
        cmp eax, 6
        je .i_clock
        cmp eax, 7
        je .i_mon
        jmp .i_launcher
.i_term:
        ; Monitor body and dark screen
        mov eax, [.i_x]
        mov ebx, [.i_y]
        mov ecx, 32
        mov edx, 27
        mov esi, 0x00C0C0C0
        call gui_bb_fill_rect
        mov eax, [.i_x]
        mov ebx, [.i_y]
        mov ecx, 32
        mov edx, 27
        mov esi, 0x00000000
        call gui_bb_rect
        mov eax, [.i_x]
        add eax, 3
        mov ebx, [.i_y]
        add ebx, 3
        mov ecx, 26
        mov edx, 16
        mov esi, 0x00000000
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 5
        mov ebx, [.i_y]
        add ebx, 2
        mov cl, '>'
        mov edx, 0x0033FF66
        call gui_bb_draw_char
        mov eax, [.i_x]
        add eax, 14
        mov ebx, [.i_y]
        add ebx, 2
        mov cl, '_'
        mov edx, 0x0033FF66
        call gui_bb_draw_char
        ; Stand
        mov eax, [.i_x]
        add eax, 11
        mov ebx, [.i_y]
        add ebx, 27
        mov ecx, 10
        mov edx, 3
        mov esi, 0x00808080
        call gui_bb_fill_rect
        jmp .i_done
.i_folder:
        ; Folder tab and body
        mov eax, [.i_x]
        mov ebx, [.i_y]
        mov ecx, 14
        mov edx, 7
        mov esi, 0x00C8A000
        call gui_bb_fill_rect
        mov eax, [.i_x]
        mov ebx, [.i_y]
        add ebx, 4
        mov ecx, 32
        mov edx, 26
        mov esi, 0x00FFD24A
        call gui_bb_fill_rect
        mov eax, [.i_x]
        mov ebx, [.i_y]
        add ebx, 4
        mov ecx, 32
        mov edx, 26
        mov esi, 0x00000000
        call gui_bb_rect
        ; Paper sheet
        mov eax, [.i_x]
        add eax, 6
        mov ebx, [.i_y]
        add ebx, 9
        mov ecx, 20
        mov edx, 17
        mov esi, 0x00FFFFFF
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 6
        mov ebx, [.i_y]
        add ebx, 9
        mov ecx, 20
        mov edx, 17
        mov esi, 0x00808080
        call gui_bb_rect
        jmp .i_done
.i_globe:
        ; Blue globe with latitude/longitude lines
        mov eax, [.i_x]
        add eax, 2
        mov ebx, [.i_y]
        add ebx, 2
        mov ecx, 28
        mov edx, 28
        mov esi, 0x000060C0
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 2
        mov ebx, [.i_y]
        add ebx, 2
        mov ecx, 28
        mov edx, 28
        mov esi, 0x00000000
        call gui_bb_rect
        mov eax, [.i_x]
        add eax, 3
        mov ebx, [.i_y]
        add ebx, 3
        mov ecx, 26
        mov edx, 6
        mov esi, 0x00A8D0FF
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 2
        mov ebx, [.i_y]
        add ebx, 12
        mov ecx, 28
        mov edx, 0x00FFFFFF
        call gui_bb_hline
        mov eax, [.i_x]
        add eax, 2
        mov ebx, [.i_y]
        add ebx, 20
        mov ecx, 28
        mov edx, 0x00FFFFFF
        call gui_bb_hline
        mov eax, [.i_x]
        add eax, 15
        mov ebx, [.i_y]
        add ebx, 3
        mov ecx, 26
        mov edx, 0x00FFFFFF
        call gui_bb_vline
        jmp .i_done
.i_edit:
        ; White page with blue text lines and folded corner
        mov eax, [.i_x]
        add eax, 4
        mov ebx, [.i_y]
        mov ecx, 24
        mov edx, 32
        mov esi, 0x00FFFFFF
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 4
        mov ebx, [.i_y]
        mov ecx, 24
        mov edx, 32
        mov esi, 0x00000000
        call gui_bb_rect
        mov eax, [.i_x]
        add eax, 22
        mov ebx, [.i_y]
        mov ecx, 6
        mov edx, 6
        mov esi, 0x00C0C0C0
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 7
        mov ebx, [.i_y]
        add ebx, 8
        mov ecx, 18
        mov edx, 0x000040A0
        call gui_bb_hline
        mov eax, [.i_x]
        add eax, 7
        mov ebx, [.i_y]
        add ebx, 13
        mov ecx, 18
        mov edx, 0x000040A0
        call gui_bb_hline
        mov eax, [.i_x]
        add eax, 7
        mov ebx, [.i_y]
        add ebx, 18
        mov ecx, 18
        mov edx, 0x000040A0
        call gui_bb_hline
        mov eax, [.i_x]
        add eax, 7
        mov ebx, [.i_y]
        add ebx, 23
        mov ecx, 11
        mov edx, 0x000040A0
        call gui_bb_hline
        jmp .i_done
.i_calc:
        ; Calculator body, display and keypad
        mov eax, [.i_x]
        add eax, 2
        mov ebx, [.i_y]
        add ebx, 2
        mov ecx, 28
        mov edx, 28
        mov esi, 0x00C0C0C0
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 2
        mov ebx, [.i_y]
        add ebx, 2
        mov ecx, 28
        mov edx, 28
        mov esi, 0x00000000
        call gui_bb_rect
        mov eax, [.i_x]
        add eax, 6
        mov ebx, [.i_y]
        add ebx, 5
        mov ecx, 20
        mov edx, 6
        mov esi, 0x00FFFFFF
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 6
        mov ebx, [.i_y]
        add ebx, 5
        mov ecx, 20
        mov edx, 6
        mov esi, 0x00808080
        call gui_bb_rect
        mov eax, [.i_x]
        add eax, 6
        mov ebx, [.i_y]
        add ebx, 15
        mov ecx, 4
        mov edx, 4
        mov esi, 0x00404040
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 13
        mov ebx, [.i_y]
        add ebx, 15
        mov ecx, 4
        mov edx, 4
        mov esi, 0x00404040
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 20
        mov ebx, [.i_y]
        add ebx, 15
        mov ecx, 4
        mov edx, 4
        mov esi, 0x00404040
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 6
        mov ebx, [.i_y]
        add ebx, 22
        mov ecx, 4
        mov edx, 4
        mov esi, 0x00404040
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 13
        mov ebx, [.i_y]
        add ebx, 22
        mov ecx, 4
        mov edx, 4
        mov esi, 0x00404040
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 20
        mov ebx, [.i_y]
        add ebx, 22
        mov ecx, 4
        mov edx, 4
        mov esi, 0x00A00000
        call gui_bb_fill_rect
        jmp .i_done
.i_paint:
        ; Palette and brush
        mov eax, [.i_x]
        add eax, 2
        mov ebx, [.i_y]
        add ebx, 7
        mov ecx, 28
        mov edx, 22
        mov esi, 0x00E8E8E8
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 2
        mov ebx, [.i_y]
        add ebx, 7
        mov ecx, 28
        mov edx, 22
        mov esi, 0x00000000
        call gui_bb_rect
        mov eax, [.i_x]
        add eax, 6
        mov ebx, [.i_y]
        add ebx, 11
        mov ecx, 5
        mov edx, 5
        mov esi, 0x00C00000
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 13
        mov ebx, [.i_y]
        add ebx, 11
        mov ecx, 5
        mov edx, 5
        mov esi, 0x0000A000
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 20
        mov ebx, [.i_y]
        add ebx, 11
        mov ecx, 5
        mov edx, 5
        mov esi, 0x000000A0
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 6
        mov ebx, [.i_y]
        add ebx, 19
        mov ecx, 5
        mov edx, 5
        mov esi, 0x00C0A000
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 20
        mov ebx, [.i_y]
        mov ecx, 4
        mov edx, 9
        mov esi, 0x00804020
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 20
        mov ebx, [.i_y]
        add ebx, 9
        mov ecx, 4
        mov edx, 4
        mov esi, 0x00C00000
        call gui_bb_fill_rect
        jmp .i_done
.i_clock:
        ; Clock face with hands
        mov eax, [.i_x]
        add eax, 4
        mov ebx, [.i_y]
        add ebx, 3
        mov ecx, 24
        mov edx, 27
        mov esi, 0x00FFFFFF
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 4
        mov ebx, [.i_y]
        add ebx, 3
        mov ecx, 24
        mov edx, 27
        mov esi, 0x00000000
        call gui_bb_rect
        mov eax, [.i_x]
        add eax, 12
        mov ebx, [.i_y]
        mov ecx, 8
        mov edx, 4
        mov esi, 0x00808080
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 8
        mov ebx, [.i_y]
        add ebx, 16
        mov ecx, 12
        mov edx, 0x00000000
        call gui_bb_hline
        mov eax, [.i_x]
        add eax, 16
        mov ebx, [.i_y]
        add ebx, 9
        mov ecx, 10
        mov edx, 0x00000000
        call gui_bb_vline
        jmp .i_done
.i_mon:
        ; System monitor with green bars
        mov eax, [.i_x]
        add eax, 2
        mov ebx, [.i_y]
        add ebx, 2
        mov ecx, 28
        mov edx, 25
        mov esi, 0x00C0C0C0
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 2
        mov ebx, [.i_y]
        add ebx, 2
        mov ecx, 28
        mov edx, 25
        mov esi, 0x00000000
        call gui_bb_rect
        mov eax, [.i_x]
        add eax, 5
        mov ebx, [.i_y]
        add ebx, 5
        mov ecx, 22
        mov edx, 17
        mov esi, 0x00000000
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 7
        mov ebx, [.i_y]
        add ebx, 14
        mov ecx, 3
        mov edx, 6
        mov esi, 0x0033FF66
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 13
        mov ebx, [.i_y]
        add ebx, 11
        mov ecx, 3
        mov edx, 9
        mov esi, 0x0033FF66
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 19
        mov ebx, [.i_y]
        add ebx, 8
        mov ecx, 3
        mov edx, 12
        mov esi, 0x0033FF66
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 11
        mov ebx, [.i_y]
        add ebx, 27
        mov ecx, 10
        mov edx, 3
        mov esi, 0x00808080
        call gui_bb_fill_rect
        jmp .i_done
.i_launcher:
        ; Four-pane Windows flag
        mov eax, [.i_x]
        add eax, 5
        mov ebx, [.i_y]
        add ebx, 5
        mov ecx, 10
        mov edx, 10
        mov esi, 0x00C00000
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 17
        mov ebx, [.i_y]
        add ebx, 5
        mov ecx, 10
        mov edx, 10
        mov esi, 0x0000A000
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 5
        mov ebx, [.i_y]
        add ebx, 17
        mov ecx, 10
        mov edx, 10
        mov esi, 0x000000A0
        call gui_bb_fill_rect
        mov eax, [.i_x]
        add eax, 17
        mov ebx, [.i_y]
        add ebx, 17
        mov ecx, 10
        mov edx, 10
        mov esi, 0x00C0A000
        call gui_bb_fill_rect
.i_done:
        popad
        ret
.i_idx: dd 0
.i_x:   dd 0
.i_y:   dd 0

;---------------------------------------
; gui_draw_icon16 - Draw a 16x16 application icon
;   EAX = icon index, EBX = x, ECX = y
;---------------------------------------
gui_draw_icon16:
        pushad
        mov [.j_idx], eax
        mov [.j_x], ebx
        mov [.j_y], ecx
        cmp eax, 0
        je .j_term
        cmp eax, 1
        je .j_folder
        cmp eax, 2
        je .j_globe
        cmp eax, 3
        je .j_edit
        cmp eax, 4
        je .j_calc
        cmp eax, 5
        je .j_paint
        cmp eax, 6
        je .j_clock
        cmp eax, 7
        je .j_mon
        cmp eax, 8
        je .j_flag
        cmp eax, 9
        je .j_settings
        cmp eax, 10
        je .j_about
        cmp eax, 11
        je .j_exit
        jmp .j_window
.j_term:
        mov eax, [.j_x]
        mov ebx, [.j_y]
        mov ecx, 16
        mov edx, 14
        mov esi, 0x00000000
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 2
        mov ebx, [.j_y]
        add ebx, 1
        mov cl, '>'
        mov edx, 0x0033FF66
        call gui_bb_draw_char
        jmp .j_done
.j_folder:
        mov eax, [.j_x]
        mov ebx, [.j_y]
        mov ecx, 7
        mov edx, 4
        mov esi, 0x00C8A000
        call gui_bb_fill_rect
        mov eax, [.j_x]
        mov ebx, [.j_y]
        add ebx, 3
        mov ecx, 16
        mov edx, 12
        mov esi, 0x00FFD24A
        call gui_bb_fill_rect
        mov eax, [.j_x]
        mov ebx, [.j_y]
        add ebx, 3
        mov ecx, 16
        mov edx, 12
        mov esi, 0x00000000
        call gui_bb_rect
        jmp .j_done
.j_globe:
        mov eax, [.j_x]
        mov ebx, [.j_y]
        mov ecx, 16
        mov edx, 15
        mov esi, 0x000060C0
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 1
        mov ebx, [.j_y]
        add ebx, 5
        mov ecx, 14
        mov edx, 0x00FFFFFF
        call gui_bb_hline
        mov eax, [.j_x]
        add eax, 7
        mov ebx, [.j_y]
        mov ecx, 13
        mov edx, 0x00FFFFFF
        call gui_bb_vline
        jmp .j_done
.j_edit:
        mov eax, [.j_x]
        add eax, 2
        mov ebx, [.j_y]
        mov ecx, 12
        mov edx, 15
        mov esi, 0x00FFFFFF
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 2
        mov ebx, [.j_y]
        mov ecx, 12
        mov edx, 15
        mov esi, 0x00000000
        call gui_bb_rect
        mov eax, [.j_x]
        add eax, 4
        mov ebx, [.j_y]
        add ebx, 5
        mov ecx, 8
        mov edx, 0x000040A0
        call gui_bb_hline
        mov eax, [.j_x]
        add eax, 4
        mov ebx, [.j_y]
        add ebx, 9
        mov ecx, 8
        mov edx, 0x000040A0
        call gui_bb_hline
        jmp .j_done
.j_calc:
        mov eax, [.j_x]
        add eax, 1
        mov ebx, [.j_y]
        add ebx, 1
        mov ecx, 14
        mov edx, 14
        mov esi, 0x00C0C0C0
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 1
        mov ebx, [.j_y]
        add ebx, 1
        mov ecx, 14
        mov edx, 14
        mov esi, 0x00000000
        call gui_bb_rect
        mov eax, [.j_x]
        add eax, 3
        mov ebx, [.j_y]
        add ebx, 3
        mov ecx, 10
        mov edx, 4
        mov esi, 0x00FFFFFF
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 3
        mov ebx, [.j_y]
        add ebx, 9
        mov ecx, 3
        mov edx, 3
        mov esi, 0x00404040
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 8
        mov ebx, [.j_y]
        add ebx, 9
        mov ecx, 3
        mov edx, 3
        mov esi, 0x00404040
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 3
        mov ebx, [.j_y]
        add ebx, 12
        mov ecx, 3
        mov edx, 3
        mov esi, 0x00404040
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 8
        mov ebx, [.j_y]
        add ebx, 12
        mov ecx, 3
        mov edx, 3
        mov esi, 0x00A00000
        call gui_bb_fill_rect
        jmp .j_done
.j_paint:
        mov eax, [.j_x]
        mov ebx, [.j_y]
        add ebx, 2
        mov ecx, 16
        mov edx, 12
        mov esi, 0x00E8E8E8
        call gui_bb_fill_rect
        mov eax, [.j_x]
        mov ebx, [.j_y]
        add ebx, 2
        mov ecx, 16
        mov edx, 12
        mov esi, 0x00000000
        call gui_bb_rect
        mov eax, [.j_x]
        add eax, 3
        mov ebx, [.j_y]
        add ebx, 5
        mov ecx, 3
        mov edx, 3
        mov esi, 0x00C00000
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 8
        mov ebx, [.j_y]
        add ebx, 5
        mov ecx, 3
        mov edx, 3
        mov esi, 0x0000A000
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 12
        mov ebx, [.j_y]
        add ebx, 5
        mov ecx, 2
        mov edx, 3
        mov esi, 0x000000A0
        call gui_bb_fill_rect
        jmp .j_done
.j_clock:
        mov eax, [.j_x]
        add eax, 1
        mov ebx, [.j_y]
        add ebx, 1
        mov ecx, 14
        mov edx, 14
        mov esi, 0x00FFFFFF
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 1
        mov ebx, [.j_y]
        add ebx, 1
        mov ecx, 14
        mov edx, 14
        mov esi, 0x00000000
        call gui_bb_rect
        mov eax, [.j_x]
        add eax, 4
        mov ebx, [.j_y]
        add ebx, 7
        mov ecx, 8
        mov edx, 0x00000000
        call gui_bb_hline
        mov eax, [.j_x]
        add eax, 7
        mov ebx, [.j_y]
        add ebx, 4
        mov ecx, 5
        mov edx, 0x00000000
        call gui_bb_vline
        jmp .j_done
.j_mon:
        mov eax, [.j_x]
        mov ebx, [.j_y]
        add ebx, 1
        mov ecx, 16
        mov edx, 13
        mov esi, 0x00000000
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 2
        mov ebx, [.j_y]
        add ebx, 9
        mov ecx, 2
        mov edx, 4
        mov esi, 0x0033FF66
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 6
        mov ebx, [.j_y]
        add ebx, 6
        mov ecx, 2
        mov edx, 7
        mov esi, 0x0033FF66
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 10
        mov ebx, [.j_y]
        add ebx, 4
        mov ecx, 2
        mov edx, 9
        mov esi, 0x0033FF66
        call gui_bb_fill_rect
        jmp .j_done
.j_flag:
        mov eax, [.j_x]
        add eax, 1
        mov ebx, [.j_y]
        add ebx, 1
        mov ecx, 6
        mov edx, 6
        mov esi, 0x00C00000
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 9
        mov ebx, [.j_y]
        add ebx, 1
        mov ecx, 6
        mov edx, 6
        mov esi, 0x0000A000
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 1
        mov ebx, [.j_y]
        add ebx, 9
        mov ecx, 6
        mov edx, 6
        mov esi, 0x000000A0
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 9
        mov ebx, [.j_y]
        add ebx, 9
        mov ecx, 6
        mov edx, 6
        mov esi, 0x00C0A000
        call gui_bb_fill_rect
        jmp .j_done
.j_settings:
        mov eax, [.j_x]
        add eax, 2
        mov ebx, [.j_y]
        add ebx, 2
        mov ecx, 12
        mov edx, 12
        mov esi, 0x00808080
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 2
        mov ebx, [.j_y]
        add ebx, 2
        mov ecx, 12
        mov edx, 12
        mov esi, 0x00000000
        call gui_bb_rect
        mov eax, [.j_x]
        add eax, 6
        mov ebx, [.j_y]
        add ebx, 6
        mov ecx, 4
        mov edx, 4
        mov esi, 0x00000000
        call gui_bb_fill_rect
        jmp .j_done
.j_about:
        mov eax, [.j_x]
        add eax, 2
        mov ebx, [.j_y]
        add ebx, 2
        mov ecx, 12
        mov edx, 12
        mov esi, 0x00000080
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 7
        mov ebx, [.j_y]
        add ebx, 4
        mov ecx, 2
        mov edx, 2
        mov esi, 0x00FFFFFF
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 7
        mov ebx, [.j_y]
        add ebx, 7
        mov ecx, 2
        mov edx, 5
        mov esi, 0x00FFFFFF
        call gui_bb_fill_rect
        jmp .j_done
.j_exit:
        mov eax, [.j_x]
        add eax, 2
        mov ebx, [.j_y]
        add ebx, 2
        mov ecx, 12
        mov edx, 12
        mov esi, 0x00A00000
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 4
        mov ebx, [.j_y]
        add ebx, 1
        mov cl, 'X'
        mov edx, 0x00FFFFFF
        call gui_bb_draw_char
        jmp .j_done
.j_window:
        mov eax, [.j_x]
        add eax, 1
        mov ebx, [.j_y]
        add ebx, 1
        mov ecx, 14
        mov edx, 14
        mov esi, 0x00FFFFFF
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 1
        mov ebx, [.j_y]
        add ebx, 1
        mov ecx, 14
        mov edx, 4
        mov esi, 0x00000080
        call gui_bb_fill_rect
        mov eax, [.j_x]
        add eax, 1
        mov ebx, [.j_y]
        add ebx, 1
        mov ecx, 14
        mov edx, 14
        mov esi, 0x00000000
        call gui_bb_rect
.j_done:
        popad
        ret
.j_idx: dd 0
.j_x:   dd 0
.j_y:   dd 0

;---------------------------------------
; gui_draw_desktop_icons - Draw icons and labels on the desktop
;---------------------------------------
gui_draw_desktop_icons:
        pushad
        xor ecx, ecx
.di_loop:
        cmp ecx, ICON_COUNT
        jge .di_done

        mov [.di_idx], ecx
        mov eax, ICON_START_X
        mov [.di_x], eax
        mov eax, ecx
        imul eax, ICON_CELL_H
        add eax, ICON_START_Y
        mov [.di_y], eax

        ; Selected icon gets a classic dotted focus box.
        cmp ecx, [gui_icon_sel]
        jne .di_no_focus
        mov eax, [.di_x]
        sub eax, 2
        mov ebx, [.di_y]
        sub ebx, 2
        mov ecx, ICON_SIZE + 4
        mov edx, ICON_SIZE + 4
        mov esi, 0x00FFFFFF
        call gui_bb_rect
        mov eax, [.di_x]
        sub eax, 1
        mov ebx, [.di_y]
        sub ebx, 1
        mov ecx, ICON_SIZE + 2
        mov edx, ICON_SIZE + 2
        mov esi, 0x00000080
        call gui_bb_rect

.di_no_focus:
        mov eax, [.di_idx]
        mov ebx, [.di_x]
        mov ecx, [.di_y]
        call gui_draw_icon32

        ; Label background when selected
        cmp dword [gui_icon_sel], -1
        je .di_label_plain
        mov eax, [.di_idx]
        cmp eax, [gui_icon_sel]
        jne .di_label_plain
        mov eax, [.di_x]
        sub eax, 2
        mov ebx, [.di_y]
        add ebx, ICON_SIZE + 2
        mov ecx, ICON_CELL_W - 2
        mov edx, ICON_TEXT_H
        mov esi, 0x00000080
        call gui_bb_fill_rect

.di_label_plain:
        ; Shadowed white label, or white on navy when selected.
        mov eax, [.di_x]
        add eax, 2
        mov ebx, [.di_y]
        add ebx, ICON_SIZE + 3
        mov ecx, [.di_idx]
        mov esi, [gui_icon_labels + ecx*4]
        mov edx, 0x00000000
        call gui_bb_draw_string

        mov eax, [.di_x]
        add eax, 1
        mov ebx, [.di_y]
        add ebx, ICON_SIZE + 2
        mov ecx, [.di_idx]
        mov esi, [gui_icon_labels + ecx*4]
        mov edx, 0x00FFFFFF
        call gui_bb_draw_string

        mov ecx, [.di_idx]
        inc ecx
        jmp .di_loop
.di_done:
        popad
        ret
.di_idx: dd 0
.di_x:   dd 0
.di_y:   dd 0

;---------------------------------------
; gui_icon_hit_test - Check if click is on a desktop icon
; EAX = x, EBX = y
; Returns: EAX = icon index or -1
;---------------------------------------
gui_icon_hit_test:
        push ebx
        push ecx
        push edx
        push esi

        cmp eax, ICON_START_X
        jl .ih_miss
        mov ecx, ICON_START_X + ICON_CELL_W - 4
        cmp eax, ecx
        jg .ih_miss

        sub ebx, ICON_START_Y
        cmp ebx, 0
        jl .ih_miss
        xor edx, edx
        mov eax, ebx
        mov ecx, ICON_CELL_H
        div ecx             ; eax = row index, edx = offset in cell
        cmp eax, ICON_COUNT
        jge .ih_miss
        cmp edx, ICON_SIZE + ICON_TEXT_H
        jg .ih_miss

        pop esi
        pop edx
        pop ecx
        pop ebx
        ret
.ih_miss:
        mov eax, -1
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret

;---------------------------------------
; gui_flip - Copy backbuffer to LFB
;---------------------------------------
gui_flip:
        pushad
        cld
        mov esi, GUI_BACKBUF
        mov edi, [vbe_lfb_addr]
        mov ecx, GUI_SCREEN_W * GUI_SCREEN_H
        rep movsd
        popad
        ret

;---------------------------------------
; gui_draw_cursor - Draw 16x20 arrow cursor with shadow
; Uses 16-bit-wide bitmaps (fill, border, shadow) stored
; in gui_cursor_fill / gui_cursor_border / gui_cursor_shadow.
;---------------------------------------
CURSOR_H        equ 20
CURSOR_W        equ 16

gui_draw_cursor:
        pushad
        call gui_target_screen
        ; --- Pass 1: draw shadow (offset +1,+1, dark translucent) ---
        mov ebp, [mouse_x]
        inc ebp                 ; shadow offset x+1
        mov edx, [mouse_y]
        inc edx                 ; shadow offset y+1
        mov esi, gui_cursor_shadow
        mov ecx, CURSOR_H
.shd_row:
        cmp ecx, 0
        jle .shd_done
        movzx edi, word [esi]
        add esi, 2
        mov ebx, CURSOR_W
        push ebp
.shd_bit:
        cmp ebx, 0
        je .shd_next
        test edi, 0x8000
        jz .shd_skip
        cmp ebp, 0
        jl .shd_skip
        cmp ebp, GUI_SCREEN_W
        jge .shd_skip
        cmp edx, 0
        jl .shd_skip
        cmp edx, GUI_SCREEN_H
        jge .shd_skip
        push edx
        imul edx, GUI_PITCH
        lea edx, [GUI_BACKBUF + edx + ebp*4]
        ; Darken existing pixel (shift each channel right by 1)
        mov eax, [edx]
        shr eax, 1
        and eax, 0x007F7F7F
        mov [edx], eax
        pop edx
.shd_skip:
        shl edi, 1
        inc ebp
        dec ebx
        jmp .shd_bit
.shd_next:
        pop ebp
        inc edx
        dec ecx
        jmp .shd_row
.shd_done:

        ; --- Pass 2: draw border (black) ---
        mov ebp, [mouse_x]
        mov edx, [mouse_y]
        mov esi, gui_cursor_border
        mov ecx, CURSOR_H
.bdr_row:
        cmp ecx, 0
        jle .bdr_done
        movzx edi, word [esi]
        add esi, 2
        mov ebx, CURSOR_W
        push ebp
.bdr_bit:
        cmp ebx, 0
        je .bdr_next
        test edi, 0x8000
        jz .bdr_skip
        cmp ebp, 0
        jl .bdr_skip
        cmp ebp, GUI_SCREEN_W
        jge .bdr_skip
        cmp edx, 0
        jl .bdr_skip
        cmp edx, GUI_SCREEN_H
        jge .bdr_skip
        push edx
        imul edx, GUI_PITCH
        lea edx, [GUI_BACKBUF + edx + ebp*4]
        mov dword [edx], 0x00000000
        pop edx
.bdr_skip:
        shl edi, 1
        inc ebp
        dec ebx
        jmp .bdr_bit
.bdr_next:
        pop ebp
        inc edx
        dec ecx
        jmp .bdr_row
.bdr_done:

        ; --- Pass 3: draw fill (white) ---
        mov ebp, [mouse_x]
        mov edx, [mouse_y]
        mov esi, gui_cursor_fill
        mov ecx, CURSOR_H
.fil_row:
        cmp ecx, 0
        jle .fil_done
        movzx edi, word [esi]
        add esi, 2
        mov ebx, CURSOR_W
        push ebp
.fil_bit:
        cmp ebx, 0
        je .fil_next
        test edi, 0x8000
        jz .fil_skip
        cmp ebp, 0
        jl .fil_skip
        cmp ebp, GUI_SCREEN_W
        jge .fil_skip
        cmp edx, 0
        jl .fil_skip
        cmp edx, GUI_SCREEN_H
        jge .fil_skip
        push edx
        imul edx, GUI_PITCH
        lea edx, [GUI_BACKBUF + edx + ebp*4]
        mov dword [edx], 0x00FFFFFF
        pop edx
.fil_skip:
        shl edi, 1
        inc ebp
        dec ebx
        jmp .fil_bit
.fil_next:
        pop ebp
        inc edx
        dec ecx
        jmp .fil_row
.fil_done:
        popad
        ret

;=======================================================================
; BACKBUFFER DRAWING PRIMITIVES
;=======================================================================

;---------------------------------------
; gui_target_screen - direct subsequent gui_bb_* calls at the back buffer
;---------------------------------------
gui_target_screen:
        mov eax, GUI_BACKBUF
        mov [gui_target_buf], eax
        mov dword [gui_target_pitch], GUI_PITCH
        mov dword [gui_target_w], GUI_SCREEN_W
        mov dword [gui_target_h], GUI_SCREEN_H
        ret

;---------------------------------------
; gui_target_window - direct subsequent gui_bb_* calls at a window's buffer
;   In : EAX = window index
;   Out: EAX = 0 on success, -1 if the window has no buffer
;---------------------------------------
;---------------------------------------
; gui_caller_owns_window - may the caller draw to / destroy this window?
;   In : EAX = window index
;   Out: EAX = 1 (allowed), 0 (refused)
;
; Kernel code (desktop applets, the window manager) is always allowed; a
; user task may only touch windows it created.
;---------------------------------------
gui_caller_owns_window:
        push ebx
        push edx
        cmp eax, MAX_WINDOWS
        jae .gow_no
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        mov ebx, eax
        call sched_get_current_task
        test eax, eax
        jz .gow_yes
        mov edx, [eax + TCB_PID]
        cmp edx, [ebx + WIN_OWNER_PID]
        jne .gow_no
.gow_yes:
        mov eax, 1
        pop edx
        pop ebx
        ret
.gow_no:
        xor eax, eax
        pop edx
        pop ebx
        ret

;---------------------------------------
; gui_target_window - point the draw primitives at a window's buffer
;   In : EAX = window index
;   Out: EAX = 0 on success, -1 if the window is unusable or not the caller's
;---------------------------------------
gui_target_window:
        push ebx
        push ecx
        push edx
        mov ebx, eax
        call gui_caller_owns_window
        test eax, eax
        jz .tw_fail
        mov eax, ebx
        cmp eax, MAX_WINDOWS
        jae .tw_fail
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        mov ecx, [eax + WIN_BUF]
        test ecx, ecx
        jz .tw_fail
        mov [gui_target_buf], ecx
        mov ecx, [eax + WIN_BUF_W]
        mov [gui_target_pitch], ecx
        shl dword [gui_target_pitch], 2
        mov [gui_target_w], ecx
        mov ecx, [eax + WIN_BUF_H]
        mov [gui_target_h], ecx
        xor eax, eax
        pop edx
        pop ecx
        pop ebx
        ret
.tw_fail:
        mov eax, -1
        pop edx
        pop ecx
        pop ebx
        ret

;---------------------------------------
; gui_copy_trunc - copy a title, truncating with '.' to ECX characters
;   In : ESI = source, EDI = destination, ECX = max chars
;---------------------------------------
gui_copy_trunc:
        push eax
        test ecx, ecx
        jz .ct_end
.ct_loop:
        lodsb
        test al, al
        jz .ct_end
        stosb
        dec ecx
        jnz .ct_loop
        mov byte [edi - 1], '.'
.ct_end:
        mov byte [edi], 0
        pop eax
        ret

;---------------------------------------
; gui_bb_pixel - Draw single pixel
; EAX = x, EBX = y, ECX = color
;---------------------------------------
gui_bb_pixel:
        cmp eax, 0
        jl .bp_skip
        cmp eax, [gui_target_w]
        jge .bp_skip
        cmp ebx, 0
        jl .bp_skip
        cmp ebx, [gui_target_h]
        jge .bp_skip
        push edx
        mov edx, ebx
        imul edx, [gui_target_pitch]
        add edx, [gui_target_buf]
        lea edx, [edx + eax*4]
        mov [edx], ecx
        pop edx
.bp_skip:
        ret

;---------------------------------------
; gui_bb_hline - Horizontal line
; EAX = x, EBX = y, ECX = width, EDX = color
;---------------------------------------
gui_bb_hline:
        pushad
        cmp ebx, 0
        jl .hl_done
        cmp ebx, [gui_target_h]
        jge .hl_done
        ; Clip left
        cmp eax, 0
        jge .hl_lclip
        add ecx, eax
        xor eax, eax
.hl_lclip:
        ; Clip right
        mov edi, eax
        add edi, ecx
        cmp edi, [gui_target_w]
        jle .hl_rclip
        mov ecx, [gui_target_w]
        sub ecx, eax
.hl_rclip:
        cmp ecx, 0
        jle .hl_done
        mov edi, ebx
        imul edi, [gui_target_pitch]
        add edi, [gui_target_buf]
        lea edi, [edi + eax*4]
        mov eax, edx
        rep stosd
.hl_done:
        popad
        ret

;---------------------------------------
; gui_bb_vline - Vertical line
; EAX = x, EBX = y, ECX = height, EDX = color
;---------------------------------------
gui_bb_vline:
        pushad
        cmp eax, 0
        jl .vl_done
        cmp eax, [gui_target_w]
        jge .vl_done
        ; Clip top
        cmp ebx, 0
        jge .vl_tclip
        add ecx, ebx
        xor ebx, ebx
.vl_tclip:
        ; Clip bottom
        mov edi, ebx
        add edi, ecx
        cmp edi, [gui_target_h]
        jle .vl_bclip
        mov ecx, [gui_target_h]
        sub ecx, ebx
.vl_bclip:
        cmp ecx, 0
        jle .vl_done
        mov edi, ebx
        imul edi, [gui_target_pitch]
        add edi, [gui_target_buf]
        lea edi, [edi + eax*4]
.vl_loop:
        cmp ecx, 0
        jle .vl_done
        mov [edi], edx
        add edi, [gui_target_pitch]
        dec ecx
        jmp .vl_loop
.vl_done:
        popad
        ret

;---------------------------------------
; gui_bb_fill_rect - Filled rectangle
; EAX = x, EBX = y, ECX = w, EDX = h, ESI = color
;---------------------------------------
gui_bb_fill_rect:
        pushad
        ; Bounds check
        cmp eax, [gui_target_w]
        jge .fr_done
        cmp ebx, [gui_target_h]
        jge .fr_done
        ; Clip right
        mov edi, eax
        add edi, ecx
        cmp edi, [gui_target_w]
        jle .fr_rclip
        mov ecx, [gui_target_w]
        sub ecx, eax
.fr_rclip:
        ; Clip bottom
        mov edi, ebx
        add edi, edx
        cmp edi, [gui_target_h]
        jle .fr_bclip
        mov edx, [gui_target_h]
        sub edx, ebx
.fr_bclip:
        ; Clip left
        cmp eax, 0
        jge .fr_lclip
        add ecx, eax
        xor eax, eax
.fr_lclip:
        ; Clip top
        cmp ebx, 0
        jge .fr_tclip
        add edx, ebx
        xor ebx, ebx
.fr_tclip:
        cmp ecx, 0
        jle .fr_done
        cmp edx, 0
        jle .fr_done
        ; Calculate starting address
        mov [.fr_w], ecx
        mov [.fr_h], edx
        mov edi, ebx
        imul edi, [gui_target_pitch]
        add edi, [gui_target_buf]
        lea edi, [edi + eax*4]
.fr_row:
        cmp dword [.fr_h], 0
        jle .fr_done
        push edi
        mov ecx, [.fr_w]
        mov eax, esi
        rep stosd
        pop edi
        add edi, [gui_target_pitch]
        dec dword [.fr_h]
        jmp .fr_row
.fr_done:
        popad
        ret
.fr_w: dd 0
.fr_h: dd 0

;---------------------------------------
; gui_bb_rect - Rectangle outline
; EAX = x, EBX = y, ECX = w, EDX = h, ESI = color
;---------------------------------------
gui_bb_rect:
        pushad
        push edx
        mov edx, esi
        ; Top
        call gui_bb_hline
        ; Bottom
        pop edx
        push edx
        push ebx
        add ebx, edx
        dec ebx
        push edx
        mov edx, esi
        call gui_bb_hline
        pop edx
        pop ebx
        ; Left
        push ecx
        mov ecx, edx
        mov edx, esi
        call gui_bb_vline
        pop ecx
        ; Right
        push eax
        add eax, ecx
        dec eax
        mov ecx, [esp + 4]  ; h (saved on stack past push eax)
        ; edx = color (already set from left vline)
        call gui_bb_vline
        pop eax
        pop edx
        popad
        ret

;---------------------------------------
; gui_bb_button_3d - Draw a 3D raised button
; EAX = x, EBX = y, ECX = w, EDX = h, ESI = face color
;---------------------------------------
gui_bb_button_3d:
        pushad
        ; Fill face
        call gui_bb_fill_rect

        ; --- Top highlight ---
        push edx
        mov edx, 0x00FFFFFF
        call gui_bb_hline       ; hline(x, y, w, white)
        pop edx

        ; --- Left highlight ---
        push ecx
        push edx               ; save h (edx is clobbered by color)
        mov ecx, edx           ; ecx = h
        mov edx, 0x00FFFFFF
        call gui_bb_vline       ; vline(x, y, h, white)
        pop edx                 ; restore h
        pop ecx

        ; --- Bottom shadow ---
        push ebx
        add ebx, edx           ; ebx = y + h
        dec ebx                 ; ebx = y + h - 1
        push edx
        mov edx, 0x00222222
        call gui_bb_hline       ; hline(x, y+h-1, w, dark)
        pop edx
        pop ebx

        ; --- Right shadow ---
        push eax
        add eax, ecx           ; eax = x + w
        dec eax                 ; eax = x + w - 1
        push ecx
        mov ecx, edx           ; ecx = h
        mov edx, 0x00222222
        call gui_bb_vline       ; vline(x+w-1, y, h, dark)
        pop ecx
        pop eax

        popad
        ret

;---------------------------------------
; gui_bb_raise_rect - classic raised 3-D border
; EAX = x, EBX = y, ECX = w, EDX = h
;---------------------------------------
gui_bb_raise_rect:
        pushad
        mov [.rr_x], eax
        mov [.rr_y], ebx
        mov [.rr_w], ecx
        mov [.rr_h], edx

        mov esi, 0x00FFFFFF
        call gui_bb_hline

        mov eax, [.rr_x]
        mov ebx, [.rr_y]
        mov ecx, [.rr_h]
        mov edx, 0x00FFFFFF
        call gui_bb_vline

        mov eax, [.rr_x]
        mov ebx, [.rr_y]
        add ebx, [.rr_h]
        dec ebx
        mov ecx, [.rr_w]
        mov edx, 0x00808080
        call gui_bb_hline

        mov eax, [.rr_x]
        add eax, [.rr_w]
        dec eax
        mov ebx, [.rr_y]
        mov ecx, [.rr_h]
        mov edx, 0x00808080
        call gui_bb_vline

        popad
        ret
.rr_x: dd 0
.rr_y: dd 0
.rr_w: dd 0
.rr_h: dd 0

;---------------------------------------
; gui_bb_sink_rect - classic sunken 3-D border
; EAX = x, EBX = y, ECX = w, EDX = h
;---------------------------------------
gui_bb_sink_rect:
        pushad
        mov [.sr_x], eax
        mov [.sr_y], ebx
        mov [.sr_w], ecx
        mov [.sr_h], edx

        mov esi, 0x00808080
        call gui_bb_hline

        mov eax, [.sr_x]
        mov ebx, [.sr_y]
        mov ecx, [.sr_h]
        mov edx, 0x00808080
        call gui_bb_vline

        mov eax, [.sr_x]
        mov ebx, [.sr_y]
        add ebx, [.sr_h]
        dec ebx
        mov ecx, [.sr_w]
        mov edx, 0x00FFFFFF
        call gui_bb_hline

        mov eax, [.sr_x]
        add eax, [.sr_w]
        dec eax
        mov ebx, [.sr_y]
        mov ecx, [.sr_h]
        mov edx, 0x00FFFFFF
        call gui_bb_vline

        popad
        ret
.sr_x: dd 0
.sr_y: dd 0
.sr_w: dd 0
.sr_h: dd 0

;---------------------------------------
; gui_bb_tri_right - small right-pointing submenu arrow
;   EAX = x, EBX = y, EDX = color; 4 x 7 pixels
;---------------------------------------
gui_bb_tri_right:
        pushad
        mov [.tr_x], eax
        mov [.tr_y], ebx
        mov [.tr_c], edx
        xor ecx, ecx
.tr_loop:
        mov eax, ecx
        sub eax, 3
        jge .tr_abs
        neg eax
.tr_abs:
        mov edi, 4
        sub edi, eax            ; width 1..4
        mov eax, [.tr_x]
        mov ebx, [.tr_y]
        add ebx, ecx
        mov edx, [.tr_c]
        push ecx
        mov ecx, edi
        call gui_bb_hline
        pop ecx
        inc ecx
        cmp ecx, 7
        jb .tr_loop
        popad
        ret
.tr_x: dd 0
.tr_y: dd 0
.tr_c: dd 0

;=======================================================================
; FONT RENDERING
;=======================================================================

;---------------------------------------
; gui_bb_draw_char - Draw 8x16 character
; EAX = x, EBX = y, CL = char, EDX = color
;---------------------------------------
gui_bb_draw_char:
        pushad
        mov [.dc_x], eax
        mov [.dc_color], edx

        ; Get glyph offset
        movzx esi, cl
        sub esi, FONT_FIRST_CHAR
        jb .dc_done
        cmp esi, FONT_LAST_CHAR - FONT_FIRST_CHAR
        ja .dc_done
        shl esi, 4             ; * 16 bytes per glyph
        add esi, gui_font_8x16

        mov ecx, FONT_H        ; 16 rows
.dc_row:
        cmp ecx, 0
        je .dc_done
        cmp ebx, [gui_target_h]
        jge .dc_done
        cmp ebx, 0
        jl .dc_skip_row

        movzx edx, byte [esi]
        mov eax, [.dc_x]
        mov edi, 8
.dc_bit:
        cmp edi, 0
        je .dc_next_row
        cmp eax, [gui_target_w]
        jge .dc_next_row
        cmp eax, 0
        jl .dc_skip_bit
        test edx, 0x80
        jz .dc_skip_bit
        ; Write pixel
        push edx
        push ecx
        mov ecx, ebx
        imul ecx, [gui_target_pitch]
        add ecx, [gui_target_buf]
        lea ecx, [ecx + eax*4]
        mov edx, [.dc_color]
        mov [ecx], edx
        pop ecx
        pop edx
.dc_skip_bit:
        shl edx, 1
        inc eax
        dec edi
        jmp .dc_bit
.dc_skip_row:
.dc_next_row:
        inc esi
        inc ebx
        dec ecx
        jmp .dc_row
.dc_done:
        popad
        ret
.dc_x:     dd 0
.dc_color: dd 0

;---------------------------------------
; gui_bb_draw_string - Draw null-terminated string
; EAX = x, EBX = y, ESI = string ptr, EDX = color
;---------------------------------------
gui_bb_draw_string:
        pushad
.ds_loop:
        movzx ecx, byte [esi]
        cmp cl, 0
        je .ds_done
        call gui_bb_draw_char
        add eax, FONT_W
        inc esi
        jmp .ds_loop
.ds_done:
        popad
        ret

;---------------------------------------
; gui_bb_draw_char_v - Draw one 8x16 character rotated 90° anticlockwise
;
;   EAX = x, EBX = y, CL = char, EDX = color
;
; The rotated glyph occupies 16 px horizontally and 8 px vertically. Used for
; the vertical "Windows 2000" banner on the Start menu, which in Windows 2000
; reads bottom-to-top.
;---------------------------------------
gui_bb_draw_char_v:
        pushad
        mov [.dcv_x], eax
        mov [.dcv_y], ebx
        mov [.dcv_color], edx

        movzx esi, cl
        sub esi, FONT_FIRST_CHAR
        jb .dcv_done
        cmp esi, FONT_LAST_CHAR - FONT_FIRST_CHAR
        ja .dcv_done
        shl esi, 4             ; * 16 bytes per glyph
        add esi, gui_font_8x16

        xor ebx, ebx           ; glyph row 0..15
.dcv_row:
        movzx edx, byte [esi]
        xor ecx, ecx           ; glyph column 0..7
.dcv_col:
        test dl, 0x80
        jz .dcv_next
        ; (col,row) -> (x + row, y + 7 - col)
        mov eax, [.dcv_x]
        add eax, ebx
        mov [.dcv_px], eax
        mov eax, [.dcv_y]
        add eax, FONT_W - 1
        sub eax, ecx
        mov [.dcv_py], eax
        push edx
        push ecx
        push ebx
        mov eax, [.dcv_px]
        mov ebx, [.dcv_py]
        mov ecx, [.dcv_color]
        call gui_bb_pixel
        pop ebx
        pop ecx
        pop edx
.dcv_next:
        shl edx, 1
        inc ecx
        cmp ecx, FONT_W
        jb .dcv_col
        inc esi
        inc ebx
        cmp ebx, FONT_H
        jb .dcv_row
.dcv_done:
        popad
        ret
.dcv_x:     dd 0
.dcv_y:     dd 0
.dcv_px:    dd 0
.dcv_py:    dd 0
.dcv_color: dd 0

;---------------------------------------
; gui_bb_draw_string_v - Draw a string bottom-to-top, rotated 90° CCW
;   EAX = x, EBX = y (top-left of the first character), ESI = string,
;   EDX = color
;---------------------------------------
gui_bb_draw_string_v:
        pushad
.dsv_loop:
        movzx ecx, byte [esi]
        cmp cl, 0
        je .dsv_done
        call gui_bb_draw_char_v
        sub ebx, 14             ; next character sits above this one
        inc esi
        jmp .dsv_loop
.dsv_done:
        popad
        ret

;=======================================================================
; WINDOW MANAGEMENT
;=======================================================================

;---------------------------------------
; gui_create_window
; EAX = x, EBX = y, ECX = w, EDX = h, ESI = title
; Returns: EAX = window id (0-15) or -1
;---------------------------------------
gui_create_window:
        pushad
        mov [.cw_x], eax
        mov [.cw_y], ebx
        mov [.cw_w], ecx
        mov [.cw_h], edx
        mov [.cw_title], esi

        ; Find free slot
        xor edi, edi
        mov ebp, gui_win_table
.cw_find:
        cmp edi, MAX_WINDOWS
        jge .cw_full
        test dword [ebp + WIN_FLAGS], 0x01
        jz .cw_got
        add ebp, WIN_STRUCT_SIZE
        inc edi
        jmp .cw_find
.cw_got:
        mov [.cw_slot], edi
        mov dword [ebp + WIN_FLAGS], 0x03       ; active + visible
        mov eax, [.cw_x]
        mov [ebp + WIN_X], eax
        mov eax, [.cw_y]
        mov [ebp + WIN_Y], eax

        ; Clamp the content size to the screen and to at least one pixel.
        mov eax, [.cw_w]
        test eax, eax
        jg .cw_w_min_ok
        mov eax, 1
.cw_w_min_ok:
        cmp eax, GUI_SCREEN_W
        jle .cw_w_max_ok
        mov eax, GUI_SCREEN_W
.cw_w_max_ok:
        mov [ebp + WIN_W], eax
        mov [.cw_w], eax
        mov eax, [.cw_h]
        test eax, eax
        jg .cw_h_min_ok
        mov eax, 1
.cw_h_min_ok:
        cmp eax, GUI_SCREEN_H
        jle .cw_h_max_ok
        mov eax, GUI_SCREEN_H
.cw_h_max_ok:
        mov [ebp + WIN_H], eax
        mov [.cw_h], eax

        ; Allocate the private content buffer (w*h*4, page rounded).
        mov eax, [.cw_w]
        imul eax, [.cw_h]
        shl eax, 2
        add eax, 0xFFF
        shr eax, 12
        mov [.cw_pages], eax
        mov ecx, eax
        call pmm_alloc_pages
        test eax, eax
        jz .cw_nomem
        mov [ebp + WIN_BUF], eax
        mov ecx, [.cw_pages]
        mov [ebp + WIN_BUF_PAGES], ecx
        mov ecx, [.cw_w]
        mov [ebp + WIN_BUF_W], ecx
        mov ecx, [.cw_h]
        mov [ebp + WIN_BUF_H], ecx
        ; Clear the content to the theme window colour.
        mov edi, eax
        mov eax, [gui_theme + THEME_WINDOW_BG]
        mov ecx, [.cw_w]
        imul ecx, [.cw_h]
        cld
        rep stosd
        mov dword [ebp + WIN_APP_ID], 0

        ; Record the owning task. 0 means a kernel applet/desktop window.
        call sched_get_current_task
        test eax, eax
        jz .cw_owner_none
        mov eax, [eax + TCB_PID]
        jmp .cw_owner_set
.cw_owner_none:
        mov eax, -1             ; kernel applet: no task owns this window
.cw_owner_set:
        mov [ebp + WIN_OWNER_PID], eax

        ; Copy title (up to 63 chars)
        ; (the launcher check follows the copy)
        push edi
        mov esi, [.cw_title]
        lea edi, [ebp + WIN_TITLE]
        mov ecx, 63
.cw_cp:
        lodsb
        stosb
        cmp al, 0
        je .cw_cp_done
        dec ecx
        jnz .cw_cp
        mov byte [edi], 0
.cw_cp_done:
        pop edi

        ; Track the launcher window so F1/Menu focus it instead of spawning
        ; duplicate launchers.
        lea esi, [ebp + WIN_TITLE]
        mov edi, gui_str_launcher_title
        call str_compare
        test eax, eax
        jnz .cw_not_launcher
        mov eax, [.cw_slot]
        mov [gui_launcher_win], eax
.cw_not_launcher:

        ; Z-order
        inc dword [gui_next_z]
        mov eax, [gui_next_z]
        mov [ebp + WIN_Z], eax

        ; v9.0: new-window placed on current workspace; alpha = fully opaque
        movzx eax, byte [gui_active_workspace]
        mov [ebp + WIN_WORKSPACE], al
        mov byte [ebp + WIN_ALPHA], 255

        ; Set focused
        mov eax, [.cw_slot]
        mov [gui_focused_win], eax
        inc dword [gui_win_count]

        ; Return window index
        mov eax, [.cw_slot]
        mov [esp + 28], eax     ; EAX in pushad frame
        popad
        ret

.cw_nomem:
        ; Release the half-initialised slot so a later create can use it.
        mov dword [ebp + WIN_FLAGS], 0
        mov dword [ebp + WIN_BUF], 0
        mov dword [ebp + WIN_BUF_PAGES], 0
.cw_full:
        mov dword [esp + 28], -1
        popad
        ret

.cw_x: dd 0
.cw_y: dd 0
.cw_w: dd 0
.cw_h: dd 0
.cw_pages: dd 0
.cw_slot: dd 0
.cw_title: dd 0

;---------------------------------------
; gui_destroy_window_n - Destroy window by index
; EAX = window index
;---------------------------------------
gui_destroy_window_n:
        pushad
        cmp eax, MAX_WINDOWS
        jge .dw_done
        mov edx, eax            ; EDX = window index
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table  ; EAX = window entry
        mov esi, eax

        ; Release the private content buffer.
        mov eax, [esi + WIN_BUF]
        test eax, eax
        jz .dw_nobuf
        mov ecx, [esi + WIN_BUF_PAGES]
        test ecx, ecx
        jz .dw_clear_buf
        call pmm_free_pages
.dw_clear_buf:
        mov dword [esi + WIN_BUF], 0
        mov dword [esi + WIN_BUF_PAGES], 0
.dw_nobuf:
        cmp edx, [gui_launcher_win]
        jne .dw_not_launcher
        mov dword [gui_launcher_win], -1
.dw_not_launcher:
        mov dword [esi + WIN_FLAGS], 0
        dec dword [gui_win_count]
        cmp dword [gui_win_count], 0
        jge .dw_focus
        mov dword [gui_win_count], 0
.dw_focus:
        cmp edx, [gui_focused_win]
        jne .dw_done
        ; Hand input to the next window instead of leaving the desktop deaf.
        call gui_focus_topmost
.dw_done:
        popad
        ret

;---------------------------------------
; gui_cleanup_pid - destroy every window owned by a task
;   In : EAX = owner PID
;---------------------------------------
gui_cleanup_pid:
        pushad
        mov [.gcp_pid], eax
        xor ecx, ecx
        mov esi, gui_win_table
.gcp_loop:
        cmp ecx, MAX_WINDOWS
        jge .gcp_done
        test dword [esi + WIN_FLAGS], 0x01
        jz .gcp_next
        mov eax, [esi + WIN_OWNER_PID]
        cmp eax, [.gcp_pid]
        jne .gcp_next
        mov eax, ecx
        call gui_destroy_window_n
.gcp_next:
        add esi, WIN_STRUCT_SIZE
        inc ecx
        jmp .gcp_loop
.gcp_done:
        popad
        ret
.gcp_pid: dd 0

;---------------------------------------
; gui_realloc_window_buf - grow a window buffer to the current WIN_W/H
;   In : EAX = window index
;   Out: EAX = 0 on success (or not needed), -1 on allocation failure
;
; Called after maximize/restore/edge-resize. Content is preserved for the
; overlapping region; new pixels start at the theme window colour.
;---------------------------------------
gui_realloc_window_buf:
        pushad
        cmp eax, MAX_WINDOWS
        jae .rw_fail
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        mov [.rw_entry], eax

        ; Keep the existing buffer when it already covers both dimensions.
        mov ecx, [eax + WIN_W]
        cmp ecx, [eax + WIN_BUF_W]
        jg .rw_grow
        mov ecx, [eax + WIN_H]
        cmp ecx, [eax + WIN_BUF_H]
        jg .rw_grow
        jmp .rw_ok

.rw_grow:
        mov eax, [.rw_entry]
        mov ecx, [eax + WIN_W]
        imul ecx, [eax + WIN_H]
        shl ecx, 2
        add ecx, 0xFFF
        shr ecx, 12
        mov [.rw_pages], ecx
        call pmm_alloc_pages
        test eax, eax
        jz .rw_fail

        ; Fill the new buffer with the window's own background colour (sampled
        ; from its top-left pixel) so space added by maximize/tile looks like
        ; the app instead of a black hole until it repaints.
        mov [.rw_new], eax
        mov edx, [gui_theme + THEME_WINDOW_BG]
        mov eax, [.rw_entry]
        mov ebx, [eax + WIN_BUF]
        test ebx, ebx
        jz .rw_fill_ready
        mov edx, [ebx]
.rw_fill_ready:
        mov edi, [.rw_new]
        mov eax, [.rw_entry]
        mov ecx, [eax + WIN_W]
        imul ecx, [eax + WIN_H]
        mov eax, edx
        cld
        rep stosd

        ; Copy the overlapping region row by row.
        mov eax, [.rw_entry]
        mov esi, [eax + WIN_BUF]
        test esi, esi
        jz .rw_free_old
        mov edi, [.rw_new]
        mov ebx, [eax + WIN_BUF_H]
        mov edx, [eax + WIN_H]
        cmp ebx, edx
        jle .rw_h_ok
        mov ebx, edx
.rw_h_ok:
        mov ebp, [eax + WIN_BUF_W]
        mov edx, [eax + WIN_W]
        cmp ebp, edx
        jle .rw_w_ok
        mov ebp, edx
.rw_w_ok:
        mov [.rw_rows], ebx
        mov [.rw_dwords], ebp
        mov ecx, [eax + WIN_BUF_W]
        shl ecx, 2
        mov [.rw_src_pitch], ecx
        mov ecx, [eax + WIN_W]
        shl ecx, 2
        mov [.rw_dst_pitch], ecx
        mov [.rw_src], esi
        mov [.rw_dst], edi
.rw_copy_row:
        cmp dword [.rw_rows], 0
        jle .rw_free_old
        mov esi, [.rw_src]
        mov edi, [.rw_dst]
        mov ecx, [.rw_dwords]
        rep movsd
        mov ecx, [.rw_src_pitch]
        add [.rw_src], ecx
        mov ecx, [.rw_dst_pitch]
        add [.rw_dst], ecx
        dec dword [.rw_rows]
        jmp .rw_copy_row

.rw_free_old:
        mov eax, [.rw_entry]
        mov eax, [eax + WIN_BUF]
        test eax, eax
        jz .rw_install
        mov edx, [.rw_entry]
        mov ecx, [edx + WIN_BUF_PAGES]
        call pmm_free_pages

.rw_install:
        mov eax, [.rw_entry]
        mov ecx, [.rw_new]
        mov [eax + WIN_BUF], ecx
        mov ecx, [.rw_pages]
        mov [eax + WIN_BUF_PAGES], ecx
        mov ecx, [eax + WIN_W]
        mov [eax + WIN_BUF_W], ecx
        mov ecx, [eax + WIN_H]
        mov [eax + WIN_BUF_H], ecx
.rw_ok:
        mov dword [esp + 28], 0
        popad
        ret
.rw_fail:
        mov dword [esp + 28], -1
        popad
        ret
.rw_entry:      dd 0
.rw_new:        dd 0
.rw_pages:      dd 0
.rw_rows:       dd 0
.rw_dwords:     dd 0
.rw_src:        dd 0
.rw_dst:        dd 0
.rw_src_pitch:  dd 0
.rw_dst_pitch:  dd 0

;---------------------------------------
; gui_destroy_all_windows
;---------------------------------------
gui_destroy_all_windows:
        pushad
        mov edi, gui_win_table
        xor eax, eax
        mov ecx, (MAX_WINDOWS * WIN_STRUCT_SIZE) / 4
        rep stosd
        mov dword [gui_win_count], 0
        mov dword [gui_focused_win], -1
        mov dword [gui_next_z], 0
        popad
        ret

;---------------------------------------
; gui_cleanup_app_windows - Destroy windows created by programs
;---------------------------------------
gui_cleanup_app_windows:
        ; Called when a GUI app returns to the desktop. Destroy only that
        ; task's windows; other GUI apps keep running.
        pushad
        mov eax, [gui_exit_pid]
        test eax, eax
        js .ca_done
        call gui_cleanup_pid
        mov dword [gui_exit_pid], -1
.ca_done:
        popad
        ret

;---------------------------------------
; gui_find_window_at - Find topmost window at point
; EAX = x, EBX = y
; Returns: EAX = window index or -1
;---------------------------------------
gui_find_window_at:
        push ebx
        push ecx
        push edx
        push esi
        push edi
        push ebp

        mov [.fw_px], eax
        mov [.fw_py], ebx
        mov dword [.fw_best], -1
        mov dword [.fw_best_z], 0

        xor ecx, ecx
        mov esi, gui_win_table
.fw_scan:
        cmp ecx, MAX_WINDOWS
        jge .fw_ret
        test dword [esi + WIN_FLAGS], 0x01  ; active?
        jz .fw_next
        test dword [esi + WIN_FLAGS], 0x02  ; visible?
        jz .fw_next

        ; Full window rect: x-BORDER, y-TITLEBAR, w+2*BORDER, h+TITLEBAR+BORDER
        mov eax, [esi + WIN_X]
        sub eax, BORDER_W
        cmp dword [.fw_px], eax
        jl .fw_next
        mov edx, [esi + WIN_W]
        add edx, 2 * BORDER_W
        add eax, edx
        cmp dword [.fw_px], eax
        jge .fw_next
        mov eax, [esi + WIN_Y]
        sub eax, TITLEBAR_H
        cmp dword [.fw_py], eax
        jl .fw_next
        mov edx, [esi + WIN_H]
        add edx, TITLEBAR_H + BORDER_W
        add eax, edx
        cmp dword [.fw_py], eax
        jge .fw_next

        ; Check z-order
        mov eax, [esi + WIN_Z]
        cmp eax, [.fw_best_z]
        jle .fw_next
        mov [.fw_best_z], eax
        mov [.fw_best], ecx
.fw_next:
        add esi, WIN_STRUCT_SIZE
        inc ecx
        jmp .fw_scan
.fw_ret:
        mov eax, [.fw_best]
        pop ebp
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret
.fw_px: dd 0
.fw_py: dd 0
.fw_best: dd 0
.fw_best_z: dd 0

;---------------------------------------
; gui_set_workspace - switch virtual desktop
;   In : EAX = workspace 0..3
;---------------------------------------
gui_set_workspace:
        pushad
        and eax, 3
        mov [gui_active_workspace], al
        mov esi, burrows_log_ws
        call serial_print
        movzx eax, byte [gui_active_workspace]
        add al, '1'
        call serial_putchar
        mov al, 13
        call serial_putchar
        mov al, 10
        call serial_putchar
        ; Focus the topmost window that lives on the new workspace.
        call gui_focus_topmost
        popad
        ret

;---------------------------------------
; gui_focus_topmost - focus the highest-z window on the active workspace
;
; Used after a workspace switch and whenever the focused window goes away,
; so keyboard input always has an owner.
;---------------------------------------
gui_focus_topmost:
        pushad
        mov dword [gui_focused_win], -1
        xor ecx, ecx
        mov edx, -1             ; best window index
        xor ebx, ebx            ; best z (window z values start at 1)
.ft_scan:
        cmp ecx, MAX_WINDOWS
        jge .ft_scan_done
        mov esi, ecx
        imul esi, WIN_STRUCT_SIZE
        add esi, gui_win_table
        mov ax, [esi + WIN_FLAGS]
        and ax, 0x03
        cmp ax, 0x03
        jne .ft_next
        movzx eax, byte [esi + WIN_WORKSPACE]
        cmp al, 0xFF
        je .ft_next
        cmp al, [gui_active_workspace]
        jne .ft_next
        cmp [esi + WIN_Z], ebx
        jbe .ft_next
        mov ebx, [esi + WIN_Z]
        mov edx, ecx
.ft_next:
        inc ecx
        jmp .ft_scan
.ft_scan_done:
        cmp edx, -1
        je .ft_done
        mov eax, edx
        call gui_focus_window
.ft_done:
        popad
        ret

;---------------------------------------
; gui_win_save_geom - remember geometry so restore can put it back
;   In : ESI = window entry
;---------------------------------------
gui_win_save_geom:
        mov eax, [esi + WIN_X]
        mov [esi + WIN_SAVE_X], eax
        mov eax, [esi + WIN_Y]
        mov [esi + WIN_SAVE_Y], eax
        mov eax, [esi + WIN_W]
        mov [esi + WIN_SAVE_W], eax
        mov eax, [esi + WIN_H]
        mov [esi + WIN_SAVE_H], eax
        ret

;---------------------------------------
; gui_win_maximize - fill the desktop area, remembering the old geometry
;   In : EAX = window index
;---------------------------------------
gui_win_maximize:
        pushad
        cmp eax, MAX_WINDOWS
        jae .wmax_done
        mov ebp, eax
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        mov esi, eax
        test dword [esi + WIN_FLAGS], 0x04
        jnz .wmax_done                  ; already maximized
        call gui_win_save_geom
        mov dword [esi + WIN_X], BORDER_W
        mov dword [esi + WIN_Y], TITLEBAR_H
        mov eax, GUI_SCREEN_W - 2 * BORDER_W
        mov [esi + WIN_W], eax
        mov eax, TASKBAR_Y - TITLEBAR_H - BORDER_W
        mov [esi + WIN_H], eax
        or dword [esi + WIN_FLAGS], 0x04
        mov eax, ebp
        call gui_realloc_window_buf
.wmax_done:
        popad
        ret

;---------------------------------------
; gui_win_restore - undo maximize or tile
;   In : EAX = window index
;---------------------------------------
gui_win_restore:
        pushad
        cmp eax, MAX_WINDOWS
        jae .wres_done
        mov ebp, eax
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        mov esi, eax
        mov eax, [esi + WIN_SAVE_X]
        mov [esi + WIN_X], eax
        mov eax, [esi + WIN_SAVE_Y]
        mov [esi + WIN_Y], eax
        mov eax, [esi + WIN_SAVE_W]
        mov [esi + WIN_W], eax
        mov eax, [esi + WIN_SAVE_H]
        mov [esi + WIN_H], eax
        and dword [esi + WIN_FLAGS], ~0x04
        mov eax, ebp
        call gui_realloc_window_buf
.wres_done:
        popad
        ret

;---------------------------------------
; gui_win_tile - snap a window to one half of the desktop
;   In : EAX = window index, EBX = 0 for the left half, 1 for the right
;---------------------------------------
gui_win_tile:
        pushad
        cmp eax, MAX_WINDOWS
        jae .wtil_done
        mov ebp, eax
        mov [.wtil_side], ebx
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        mov esi, eax
        call gui_win_save_geom
        and dword [esi + WIN_FLAGS], ~0x04

        mov eax, GUI_SCREEN_W - 3 * BORDER_W
        shr eax, 1
        mov [esi + WIN_W], eax
        mov dword [esi + WIN_Y], TITLEBAR_H
        mov ecx, TASKBAR_Y - TITLEBAR_H - BORDER_W
        mov [esi + WIN_H], ecx
        mov dword [esi + WIN_X], BORDER_W
        cmp dword [.wtil_side], 0
        je .wtil_realloc
        add eax, BORDER_W
        mov [esi + WIN_X], eax
.wtil_realloc:
        mov eax, ebp
        call gui_realloc_window_buf
.wtil_done:
        popad
        ret
.wtil_side:     dd 0

;---------------------------------------
; gui_focus_window - Bring window to front
; EAX = window index
;---------------------------------------
gui_focus_window:
        pushad
        mov [gui_focused_win], eax
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        inc dword [gui_next_z]
        mov ebx, [gui_next_z]
        mov [eax + WIN_Z], ebx
        popad
        ret

;---------------------------------------
; gui_cycle_window - Alt+Tab: cycle to next visible window
;---------------------------------------
gui_cycle_window:
        pushad
        ; Find next active+visible window after focused one
        mov eax, [gui_focused_win]
        cmp eax, -1
        jne .cw_start
        xor eax, eax           ; Start from 0 if none focused
        jmp .cw_check
.cw_start:
        inc eax                 ; Start from one past focused
.cw_check:
        ; Try MAX_WINDOWS times to find next valid window
        xor ecx, ecx
.cw_loop:
        cmp ecx, MAX_WINDOWS
        jge .cw_done            ; No other windows found
        ; Wrap around
        cmp eax, MAX_WINDOWS
        jl .cw_no_wrap
        xor eax, eax
.cw_no_wrap:
        ; Check if this window is active + visible
        mov ebx, eax
        imul ebx, WIN_STRUCT_SIZE
        add ebx, gui_win_table
        mov edx, [ebx + WIN_FLAGS]
        and edx, 0x03           ; Mask active(1) + visible(2)
        cmp edx, 0x03           ; Must have BOTH bits set
        jne .cw_next
        ; Found a valid window — focus it
        call gui_focus_window
        ; Show the switcher overlay briefly so the user can see where focus went.
        mov eax, [tick_count]
        add eax, 150                    ; ~1.5 s at 100 Hz
        mov [gui_switcher_until], eax
        jmp .cw_done
.cw_next:
        inc eax
        inc ecx
        jmp .cw_loop
.cw_done:
        popad
        ret

;=======================================================================
; WINDOW RENDERING
;=======================================================================

;---------------------------------------
; gui_draw_all_windows - Draw in z-order (lowest first)
;---------------------------------------
gui_draw_all_windows:
        pushad
        call gui_target_screen
        ; Selection sort: repeatedly find min-z undrawn window
        xor edi, edi            ; drawn bitmask
.aw_pass:
        mov dword [.aw_min_z], 0x7FFFFFFF
        mov dword [.aw_min_idx], -1
        xor ecx, ecx
        mov esi, gui_win_table
.aw_find:
        cmp ecx, MAX_WINDOWS
        jge .aw_draw_one
        bt edi, ecx
        jc .aw_skip
        test dword [esi + WIN_FLAGS], 0x01  ; active?
        jz .aw_skip
        test dword [esi + WIN_FLAGS], 0x02  ; visible?
        jz .aw_skip
        ; v9.0: skip windows on other workspaces (0xFF = show on all)
        movzx eax, byte [esi + WIN_WORKSPACE]
        cmp al, 0xFF
        je .aw_ws_ok
        movzx ebx, byte [gui_active_workspace]
        cmp al, bl
        jne .aw_skip
.aw_ws_ok:
        mov eax, [esi + WIN_Z]
        cmp eax, [.aw_min_z]
        jge .aw_skip
        mov [.aw_min_z], eax
        mov [.aw_min_idx], ecx
.aw_skip:
        add esi, WIN_STRUCT_SIZE
        inc ecx
        jmp .aw_find
.aw_draw_one:
        cmp dword [.aw_min_idx], -1
        je .aw_done
        mov ebx, [.aw_min_idx]
        push edi
        call gui_draw_window_n
        pop edi
        mov ecx, [.aw_min_idx]
        bts edi, ecx
        jmp .aw_pass
.aw_done:
        popad
        ret
.aw_min_z:   dd 0
.aw_min_idx: dd 0

;---------------------------------------
; gui_blit_window - copy a window's content buffer to the back buffer
;   In : EBP = window entry
;   Clips to the screen. All registers preserved.
;---------------------------------------
gui_blit_window:
        pushad
        mov esi, [ebp + WIN_BUF]
        test esi, esi
        jz .bw_done
        mov eax, [ebp + WIN_X]
        mov ebx, [ebp + WIN_Y]
        test eax, eax
        jl .bw_done
        test ebx, ebx
        jl .bw_done
        cmp eax, GUI_SCREEN_W
        jge .bw_done
        cmp ebx, GUI_SCREEN_H
        jge .bw_done

        ; w = min(WIN_W, WIN_BUF_W, screen_w - x)
        mov ecx, [ebp + WIN_W]
        mov edi, [ebp + WIN_BUF_W]
        cmp ecx, edi
        jle .bw_w_buf_ok
        mov ecx, edi
.bw_w_buf_ok:
        mov edx, GUI_SCREEN_W
        sub edx, eax
        cmp ecx, edx
        jle .bw_w_ok
        mov ecx, edx
.bw_w_ok:
        ; h = min(WIN_H, WIN_BUF_H, screen_h - y)
        mov edx, [ebp + WIN_H]
        mov edi, [ebp + WIN_BUF_H]
        cmp edx, edi
        jle .bw_h_buf_ok
        mov edx, edi
.bw_h_buf_ok:
        mov edi, GUI_SCREEN_H
        sub edi, ebx
        cmp edx, edi
        jle .bw_h_ok
        mov edx, edi
.bw_h_ok:
        cmp ecx, 0
        jle .bw_done
        cmp edx, 0
        jle .bw_done
        mov [.bw_w], ecx
        mov [.bw_h], edx

        ; Source pitch = WIN_BUF_W * 4.
        mov eax, [ebp + WIN_BUF_W]
        shl eax, 2
        mov [.bw_src_pitch], eax
        mov [.bw_src], esi

        ; Destination = back buffer + y*GUI_PITCH + x*4.
        mov eax, ebx
        imul eax, GUI_PITCH
        add eax, GUI_BACKBUF
        mov ebx, [ebp + WIN_X]
        lea eax, [eax + ebx*4]
        mov [.bw_dst], eax

.bw_row:
        cmp dword [.bw_h], 0
        jle .bw_done
        mov esi, [.bw_src]
        mov edi, [.bw_dst]
        mov ecx, [.bw_w]
        cld
        rep movsd
        mov eax, [.bw_src_pitch]
        add [.bw_src], eax
        add dword [.bw_dst], GUI_PITCH
        dec dword [.bw_h]
        jmp .bw_row
.bw_done:
        popad
        ret
.bw_w:         dd 0
.bw_h:         dd 0
.bw_src:       dd 0
.bw_dst:       dd 0
.bw_src_pitch: dd 0

;---------------------------------------
; gui_draw_window_n - Draw window by index
; EBX = window index
;---------------------------------------
gui_draw_window_n:
        pushad
        call gui_target_screen
        cmp ebx, MAX_WINDOWS
        jge .wn_done

        imul eax, ebx, WIN_STRUCT_SIZE
        add eax, gui_win_table
        test dword [eax + WIN_FLAGS], 0x01  ; active?
        jz .wn_done
        test dword [eax + WIN_FLAGS], 0x02  ; visible?
        jz .wn_done

        ; Save entry pointer
        mov ebp, eax
        ; Content area
        mov ecx, [ebp + WIN_X]
        mov edx, [ebp + WIN_Y]
        mov esi, [ebp + WIN_W]
        mov edi, [ebp + WIN_H]

        ; --- Classic 3-D window frame (Windows 2000 had no drop shadow) ---
        push ecx
        push edx
        mov eax, ecx
        sub eax, BORDER_W
        mov ebx, edx
        sub ebx, TITLEBAR_H
        mov ecx, esi
        add ecx, 2 * BORDER_W
        mov edx, edi
        add edx, TITLEBAR_H + BORDER_W
        push esi
        mov esi, 0x00000000
        call gui_bb_rect
        pop esi
        pop edx
        pop ecx

        push ecx
        push edx
        mov eax, ecx
        sub eax, BORDER_W
        mov ebx, edx
        sub ebx, TITLEBAR_H
        mov ecx, esi
        add ecx, 2 * BORDER_W
        mov edx, edi
        add edx, TITLEBAR_H + BORDER_W
        call gui_bb_raise_rect
        pop edx
        pop ecx

        ; --- Draw title bar (gradient) ---
        push ecx
        push edx
        push esi
        ; Choose base color based on focus
        mov esi, [gui_theme + THEME_TITLE_INACTIVE]
        push eax
        mov eax, [gui_focused_win]
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        cmp eax, ebp
        pop eax
        jne .wn_unfocused
        mov esi, [gui_theme + THEME_TITLE_ACTIVE]
.wn_unfocused:
        ; Windows 2000 paints an active title bar as a horizontal gradient:
        ; dark blue at the left edge fading to pale blue at the right. Build
        ; the start (base) and end (lightened) colours, then one vertical
        ; line per column.
        mov [gui_grad_base], esi
        mov [gui_grad_x], ecx
        mov [gui_grad_y], edx
        ; width for title bar fill = win_w + 2*BORDER_W - 2
        ; ESI holds the colour; win_w is still on the stack from the
        ; 'push esi' at the start of this block.
        mov eax, [esp]             ; win_w (saved on stack)
        add eax, 2 * BORDER_W - 2
        mov [gui_grad_w], eax

        ; Right-edge target: blend 55% toward white (same rule for every
        ; theme, so inactive grey becomes the familiar #C0C0C0).
        mov ecx, esi
        shr ecx, 16
        and ecx, 0xFF
        mov [gui_grad_r0], ecx
        mov eax, 255
        sub eax, ecx
        imul eax, eax, 140
        shr eax, 8
        add eax, ecx
        mov [gui_grad_r1], eax

        mov ecx, esi
        shr ecx, 8
        and ecx, 0xFF
        mov [gui_grad_g0], ecx
        mov eax, 255
        sub eax, ecx
        imul eax, eax, 140
        shr eax, 8
        add eax, ecx
        mov [gui_grad_g1], eax

        mov ecx, esi
        and ecx, 0xFF
        mov [gui_grad_b0], ecx
        mov eax, 255
        sub eax, ecx
        imul eax, eax, 140
        shr eax, 8
        add eax, ecx
        mov [gui_grad_b1], eax

        mov dword [gui_grad_row], 0

.grad_loop:
        mov eax, [gui_grad_row]
        cmp eax, [gui_grad_w]
        jge .grad_done

        ; factor = column * 256 / (width - 1)
        mov ecx, [gui_grad_w]
        dec ecx
        jz .grad_one_col
        imul eax, 256
        xor edx, edx
        div ecx
        jmp .grad_have_factor
.grad_one_col:
        xor eax, eax
.grad_have_factor:
        mov [gui_grad_factor], eax

        ; Red
        mov eax, [gui_grad_r1]
        sub eax, [gui_grad_r0]
        imul eax, [gui_grad_factor]
        shr eax, 8
        add eax, [gui_grad_r0]
        shl eax, 16
        mov [gui_grad_color], eax
        ; Green
        mov eax, [gui_grad_g1]
        sub eax, [gui_grad_g0]
        imul eax, [gui_grad_factor]
        shr eax, 8
        add eax, [gui_grad_g0]
        shl eax, 8
        or [gui_grad_color], eax
        ; Blue
        mov eax, [gui_grad_b1]
        sub eax, [gui_grad_b0]
        imul eax, [gui_grad_factor]
        shr eax, 8
        add eax, [gui_grad_b0]
        or [gui_grad_color], eax

        ; Draw this column.
        mov eax, [gui_grad_x]
        sub eax, BORDER_W
        inc eax
        add eax, [gui_grad_row]
        mov ebx, [gui_grad_y]
        sub ebx, TITLEBAR_H
        inc ebx
        mov ecx, TITLEBAR_H - 1
        mov edx, [gui_grad_color]
        call gui_bb_vline
        inc dword [gui_grad_row]
        jmp .grad_loop

.grad_done:
        pop esi
        pop edx
        pop ecx

        ; --- Draw title icon and text ---
        push ecx
        push edx
        push esi
        mov eax, 12
        mov ebx, ecx
        add ebx, 3
        mov ecx, edx
        sub ecx, TITLEBAR_H
        add ecx, 3
        call gui_draw_icon16

        mov eax, [esp + 8]      ; saved win_x
        add eax, 22
        mov ebx, [esp + 4]      ; saved win_y
        sub ebx, TITLEBAR_H
        add ebx, 3
        lea esi, [ebp + WIN_TITLE]
        mov edx, 0x00000000        ; inactive: black on the grey gradient
        mov eax, [gui_focused_win]
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        cmp eax, ebp
        je .wn_title_focused
        ; A dark inactive title bar (the Dark theme) needs light text.
        mov eax, [gui_theme + THEME_TITLE_INACTIVE]
        shr eax, 8
        and eax, 0xFF
        cmp eax, 0x80
        jae .wn_title_color_ready
        mov edx, [gui_theme + THEME_TITLE_TEXT]
        jmp .wn_title_color_ready
.wn_title_focused:
        mov edx, [gui_theme + THEME_TITLE_TEXT]
.wn_title_color_ready:
        mov eax, [esp + 8]
        add eax, 22
        call gui_bb_draw_string
        pop esi
        pop edx
        pop ecx

        ; --- Draw close button (3D red square with X) ---
        push ecx
        push edx
        push esi
        mov eax, ecx
        add eax, esi            ; win_x + win_w
        add eax, BORDER_W
        sub eax, CLOSE_BTN_SIZE + 4
        mov ebx, edx
        sub ebx, TITLEBAR_H
        add ebx, 3
        push eax
        push ebx
        mov ecx, CLOSE_BTN_SIZE
        mov edx, CLOSE_BTN_SIZE
        mov esi, [gui_theme + THEME_BUTTON_BG]
        call gui_bb_button_3d
        pop ebx
        pop eax
        ; Draw "X"
        add eax, 4
        mov cl, 'X'
        mov edx, 0x00000000
        call gui_bb_draw_char
        pop esi
        pop edx
        pop ecx

        ; --- Draw maximize button (3D gray square with box char) ---
        push ecx
        push edx
        push esi
        mov eax, ecx
        add eax, esi
        add eax, BORDER_W
        sub eax, 2 * (CLOSE_BTN_SIZE + 2) + 4
        mov ebx, edx
        sub ebx, TITLEBAR_H
        add ebx, 3
        push eax
        push ebx
        mov ecx, CLOSE_BTN_SIZE
        mov edx, CLOSE_BTN_SIZE
        mov esi, [gui_theme + THEME_BUTTON_BG]
        call gui_bb_button_3d
        pop ebx
        pop eax
        ; Maximize / restore glyph
        add eax, 4
        add ebx, 3
        push eax
        push ebx
        mov ecx, 9
        mov edx, 9
        mov esi, 0x00000000
        call gui_bb_rect
        pop ebx
        pop eax
        test dword [ebp + WIN_FLAGS], 0x04
        jz .wn_max_glyph_done
        add eax, 2
        add ebx, 2
        mov ecx, 9
        mov edx, 9
        mov esi, 0x00000000
        call gui_bb_rect
.wn_max_glyph_done:
        pop esi
        pop edx
        pop ecx

        ; --- Draw minimize button (3D gray square with underscore) ---
        push ecx
        push edx
        push esi
        mov eax, ecx
        add eax, esi
        add eax, BORDER_W
        sub eax, 3 * (CLOSE_BTN_SIZE + 2) + 4
        mov ebx, edx
        sub ebx, TITLEBAR_H
        add ebx, 3
        push eax
        push ebx
        mov ecx, CLOSE_BTN_SIZE
        mov edx, CLOSE_BTN_SIZE
        mov esi, [gui_theme + THEME_BUTTON_BG]
        call gui_bb_button_3d
        pop ebx
        pop eax
        add eax, 4
        add ebx, 12
        mov ecx, 9
        mov edx, 0x00000000
        call gui_bb_hline
        pop esi
        pop edx
        pop ecx

        ; --- Fill the content area, then blit the private content buffer ---
        push ecx
        push edx
        mov eax, ecx
        mov ebx, edx
        mov ecx, esi
        mov edx, edi
        mov esi, [gui_theme + THEME_WINDOW_BG]
        call gui_bb_fill_rect
        pop edx
        pop ecx
        call gui_blit_window

        ; v9.0: Apply alpha transparency if WIN_ALPHA < 255
        movzx eax, byte [ebp + WIN_ALPHA]
        cmp al, 255
        je .wn_done
        ; Compute full window bounding box for the blend pass
        mov ecx, [ebp + WIN_X]
        mov edx, [ebp + WIN_Y]
        mov esi, [ebp + WIN_W]
        mov edi, [ebp + WIN_H]
        sub ecx, BORDER_W           ; full rect x
        sub edx, TITLEBAR_H         ; full rect y
        add esi, 2 * BORDER_W       ; full rect w
        add edi, TITLEBAR_H + BORDER_W  ; full rect h
        ; EAX = alpha, ECX = x, EDX = y, ESI = w, EDI = h
        call gui_alpha_blend_rect

.wn_done:
        popad
        ret

;=======================================================================
; gui_alpha_blend_rect — v9.0 per-window alpha transparency
;
; Blends a rectangular region of the back buffer against the desktop
; background color using the given alpha value.
;
;   In:  EAX = alpha (0=transparent, 255=fully opaque)
;        ECX = rect x, EDX = rect y, ESI = rect width, EDI = rect height
;   All registers preserved.
;   Formula (per channel, fast /256 approx):
;     out = (buf * alpha + bg * (255-alpha)) >> 8
;=======================================================================
gui_alpha_blend_rect:
        pushad
        ; Nothing to do for fully opaque or zero-size rects
        cmp eax, 255
        jge .gab_done
        test esi, esi
        jz .gab_done
        test edi, edi
        jz .gab_done

        ; Save alpha and inverse alpha
        mov [.gab_alpha],  eax
        mov ebx, 255
        sub ebx, eax
        mov [.gab_ialpha], ebx

        ; Clamp x1, y1 to screen bounds
        test ecx, ecx
        jge .gab_x1_ok
        xor ecx, ecx
.gab_x1_ok:
        test edx, edx
        jge .gab_y1_ok
        xor edx, edx
.gab_y1_ok:

        ; Compute x2 = min(x1+w, GUI_SCREEN_W)
        mov ebx, ecx
        add ebx, esi
        cmp ebx, GUI_SCREEN_W
        jle .gab_x2_ok
        mov ebx, GUI_SCREEN_W
.gab_x2_ok:
        mov [.gab_x1], ecx
        mov [.gab_x2], ebx

        ; Compute y2 = min(y1+h, GUI_SCREEN_H)
        mov ebx, edx
        add ebx, edi
        cmp ebx, GUI_SCREEN_H
        jle .gab_y2_ok
        mov ebx, GUI_SCREEN_H
.gab_y2_ok:
        mov [.gab_y1], edx
        mov [.gab_y2], ebx

        ; Extract desktop background R, G, B from theme
        ; [gui_theme + THEME_DESKTOP_BG] = dword 0x00RRGGBB
        mov esi, [gui_theme + THEME_DESKTOP_BG]
        mov eax, esi
        and eax, 0xFF               ; B (bits 0-7)
        mov [.gab_bg_b], eax
        shr esi, 8
        mov eax, esi
        and eax, 0xFF               ; G
        mov [.gab_bg_g], eax
        shr esi, 8
        mov eax, esi
        and eax, 0xFF               ; R
        mov [.gab_bg_r], eax

        ; Row loop — EDX = current y
        mov edx, [.gab_y1]
.gab_row:
        cmp edx, [.gab_y2]
        jge .gab_done

        ; Row base address in back buffer (GUI_PITCH = 4096 = 1<<12)
        mov eax, edx
        shl eax, 12                 ; eax = y * 4096
        add eax, GUI_BACKBUF        ; eax = row base ptr

        ; Col loop — ECX = current x
        mov ecx, [.gab_x1]
.gab_col:
        cmp ecx, [.gab_x2]
        jge .gab_next_row

        ; Pixel address = row_base + x * 4
        mov ebp, eax                ; preserve row_base
        lea ebp, [ebp + ecx * 4]

        ; Read pixel 0x00RRGGBB into ESI
        mov esi, [ebp]

        ; Blend Blue (bits 0-7)
        mov ebx, esi
        and ebx, 0xFF
        imul ebx, [.gab_alpha]
        push edx
        mov edx, [.gab_bg_b]
        imul edx, [.gab_ialpha]
        add ebx, edx
        pop edx
        shr ebx, 8
        mov [.gab_tmp_b], ebx

        ; Blend Green (bits 8-15)
        shr esi, 8
        mov ebx, esi
        and ebx, 0xFF
        imul ebx, [.gab_alpha]
        push edx
        mov edx, [.gab_bg_g]
        imul edx, [.gab_ialpha]
        add ebx, edx
        pop edx
        shr ebx, 8
        mov [.gab_tmp_g], ebx

        ; Blend Red (bits 16-23)
        shr esi, 8
        mov ebx, esi
        and ebx, 0xFF
        imul ebx, [.gab_alpha]
        push edx
        mov edx, [.gab_bg_r]
        imul edx, [.gab_ialpha]
        add ebx, edx
        pop edx
        shr ebx, 8                  ; EBX = blended_R

        ; Pack result 0x00RRGGBB and write back
        shl ebx, 16                 ; R in bits 16-23
        mov edi, [.gab_tmp_g]
        shl edi, 8
        or  ebx, edi
        or  ebx, [.gab_tmp_b]
        mov [ebp], ebx

        inc ecx
        jmp .gab_col

.gab_next_row:
        inc edx
        jmp .gab_row

.gab_done:
        popad
        ret
; Local data
.gab_alpha:  dd 0
.gab_ialpha: dd 0
.gab_x1:     dd 0
.gab_x2:     dd 0
.gab_y1:     dd 0
.gab_y2:     dd 0
.gab_bg_r:   dd 0
.gab_bg_g:   dd 0
.gab_bg_b:   dd 0
.gab_tmp_b:  dd 0
.gab_tmp_g:  dd 0

;=======================================================================
; TASKBAR
;=======================================================================

;---------------------------------------
; gui_taskbar_layout - choose the task-button width for this workspace
;
; Windows 2000 stretches task buttons up to 160 px while there are few
; windows, then shrinks them so they always fit between Start and the tray.
;---------------------------------------
gui_taskbar_layout:
        pushad
        xor ebx, ebx            ; window count
        xor ecx, ecx
        mov esi, gui_win_table
.tbl_loop:
        cmp ecx, MAX_WINDOWS
        jge .tbl_counted
        test dword [esi + WIN_FLAGS], 0x01
        jz .tbl_next
        movzx eax, byte [esi + WIN_WORKSPACE]
        cmp al, 0xFF
        je .tbl_count
        cmp al, [gui_active_workspace]
        jne .tbl_next
.tbl_count:
        inc ebx
.tbl_next:
        add esi, WIN_STRUCT_SIZE
        inc ecx
        jmp .tbl_loop
.tbl_counted:
        test ebx, ebx
        jnz .tbl_calc
        mov dword [gui_task_btn_w], TASKBAR_TASK_W
        popad
        ret
.tbl_calc:
        mov eax, GUI_SCREEN_W - TASKBAR_TRAY_W - 8
        sub eax, TASKBAR_TASKS_X
        mov edx, ebx
        dec edx
        shl edx, 2                  ; gaps between buttons
        sub eax, edx
        xor edx, edx
        div ebx
        cmp eax, 160
        jle .tbl_max_ok
        mov eax, 160
.tbl_max_ok:
        cmp eax, 56
        jge .tbl_min_ok
        mov eax, 56
.tbl_min_ok:
        mov [gui_task_btn_w], eax
        popad
        ret

gui_draw_taskbar:
        pushad
        call gui_target_screen
        call gui_taskbar_layout

        ; Taskbar face and top 3-D edge
        mov eax, 0
        mov ebx, TASKBAR_Y
        mov ecx, GUI_SCREEN_W
        mov edx, TASKBAR_H
        mov esi, [gui_theme + THEME_TASKBAR_BG]
        call gui_bb_fill_rect
        mov eax, 0
        mov ebx, TASKBAR_Y
        mov ecx, GUI_SCREEN_W
        mov edx, 0x00FFFFFF
        call gui_bb_hline
        mov eax, 0
        mov ebx, TASKBAR_Y + 1
        mov ecx, GUI_SCREEN_W
        mov edx, [gui_theme + THEME_TASKBAR_BG]
        call gui_bb_hline

        ; --- Start button ---
        mov eax, 2
        mov ebx, TASKBAR_Y + 2
        mov ecx, TASKBAR_START_W
        mov edx, 24
        mov esi, [gui_theme + THEME_BUTTON_BG]
        call gui_bb_fill_rect
        cmp byte [gui_menu_open], 1
        je .tb_start_down
        mov eax, 2
        mov ebx, TASKBAR_Y + 2
        mov ecx, TASKBAR_START_W
        mov edx, 24
        call gui_bb_raise_rect
        jmp .tb_start_icon
.tb_start_down:
        mov eax, 2
        mov ebx, TASKBAR_Y + 2
        mov ecx, TASKBAR_START_W
        mov edx, 24
        call gui_bb_sink_rect

.tb_start_icon:
        ; Waving four-colour Windows flag, pane by pane.
        xor ecx, ecx
.tb_flag_row:
        movzx eax, byte [gui_flag_wave + ecx]
        add eax, 6
        mov ebx, TASKBAR_Y + 7
        add ebx, ecx
        cmp ecx, 5
        jae .tb_flag_lower
        mov esi, 0x00C00000           ; red
        jmp .tb_flag_left
.tb_flag_lower:
        mov esi, 0x000000A0           ; blue
.tb_flag_left:
        mov edx, 1
        push ecx
        mov ecx, 5
        call gui_bb_fill_rect
        pop ecx

        movzx eax, byte [gui_flag_wave + ecx]
        add eax, 12
        mov ebx, TASKBAR_Y + 7
        add ebx, ecx
        cmp ecx, 5
        jae .tb_flag_right_lower
        mov esi, 0x0000A000           ; green
        jmp .tb_flag_right
.tb_flag_right_lower:
        mov esi, 0x00C0A000           ; yellow
.tb_flag_right:
        mov edx, 1
        push ecx
        mov ecx, 5
        call gui_bb_fill_rect
        pop ecx

        inc ecx
        cmp ecx, 10
        jb .tb_flag_row

        ; "Start" label
        mov eax, 23
        mov ebx, TASKBAR_Y + 5
        mov esi, gui_str_start
        mov edx, 0x00000000
        call gui_bb_draw_string

        ; --- Tray (sunken status area) ---
        mov eax, GUI_SCREEN_W - TASKBAR_TRAY_W
        mov ebx, TASKBAR_Y + 3
        mov ecx, TASKBAR_TRAY_W - 2
        mov edx, 22
        mov esi, [gui_theme + THEME_BUTTON_BG]
        call gui_bb_fill_rect
        mov eax, GUI_SCREEN_W - TASKBAR_TRAY_W
        mov ebx, TASKBAR_Y + 3
        mov ecx, TASKBAR_TRAY_W - 2
        mov edx, 22
        call gui_bb_sink_rect

        ; Tray gripper, as on the Windows 2000 notification area.
        mov eax, GUI_SCREEN_W - TASKBAR_TRAY_W + 3
        mov ebx, TASKBAR_Y + 5
        mov ecx, 18
        mov edx, 0x00FFFFFF
        call gui_bb_vline
        mov eax, GUI_SCREEN_W - TASKBAR_TRAY_W + 4
        mov ebx, TASKBAR_Y + 5
        mov ecx, 18
        mov edx, 0x00808080
        call gui_bb_vline

        ; Workspace button inside the tray
        movzx eax, byte [gui_active_workspace]
        add al, '1'
        mov [gui_ws_buf], al
        mov byte [gui_ws_buf + 1], 0
        mov eax, GUI_SCREEN_W - TASKBAR_TRAY_W + 10
        mov ebx, TASKBAR_Y + 5
        mov ecx, 20
        mov edx, 18
        mov esi, 0x00FFFFFF
        call gui_bb_fill_rect
        mov eax, GUI_SCREEN_W - TASKBAR_TRAY_W + 10
        mov ebx, TASKBAR_Y + 5
        mov ecx, 20
        mov edx, 18
        call gui_bb_sink_rect
        mov eax, GUI_SCREEN_W - TASKBAR_TRAY_W + 16
        mov ebx, TASKBAR_Y + 7
        mov esi, gui_ws_buf
        mov edx, 0x00000000
        call gui_bb_draw_string

        ; Clock: 12-hour, right-aligned inside the tray.
        call gui_read_time
        call gui_format_time_12
        mov esi, gui_clock_buf
        xor ecx, ecx
.tb_clock_len:
        cmp byte [esi + ecx], 0
        je .tb_clock_len_done
        inc ecx
        jmp .tb_clock_len
.tb_clock_len_done:
        mov eax, ecx
        shl eax, 3                  ; text width in pixels
        mov ecx, GUI_SCREEN_W - 8
        sub ecx, eax
        mov eax, ecx
        mov ebx, TASKBAR_Y + 6
        mov edx, 0x00000000
        call gui_bb_draw_string

        ; --- Window task buttons ---
        mov dword [.tb_x], TASKBAR_TASKS_X
        xor ecx, ecx
.tb_loop:
        cmp ecx, MAX_WINDOWS
        jge .tb_end
        mov eax, ecx
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        test dword [eax + WIN_FLAGS], 0x01
        jz .tb_next
        movzx edx, byte [eax + WIN_WORKSPACE]
        cmp dl, 0xFF
        je .tb_ws_ok
        cmp dl, [gui_active_workspace]
        jne .tb_next
.tb_ws_ok:

        push ecx
        push eax
        mov eax, [.tb_x]
        mov ebx, TASKBAR_Y + 3
        mov ecx, [gui_task_btn_w]
        mov edx, 22
        mov esi, [gui_theme + THEME_BUTTON_BG]
        call gui_bb_fill_rect

        pop eax
        push eax
        mov esi, [gui_focused_win]
        imul esi, WIN_STRUCT_SIZE
        add esi, gui_win_table
        cmp eax, esi
        jne .tb_raise
        ; Focused task looks pressed.
        mov eax, [.tb_x]
        mov ebx, TASKBAR_Y + 3
        mov ecx, [gui_task_btn_w]
        mov edx, 22
        call gui_bb_sink_rect
        jmp .tb_icon
.tb_raise:
        mov eax, [.tb_x]
        mov ebx, TASKBAR_Y + 3
        mov ecx, [gui_task_btn_w]
        mov edx, 22
        call gui_bb_raise_rect

.tb_icon:
        ; Generic application icon
        mov eax, 12
        mov ebx, [.tb_x]
        add ebx, 4
        mov ecx, TASKBAR_Y + 6
        call gui_draw_icon16

        ; Truncated title, sized to the button
        pop eax
        push eax
        mov esi, eax
        add esi, WIN_TITLE
        mov edi, gui_title_buf
        mov ecx, [gui_task_btn_w]
        sub ecx, 24
        shr ecx, 3
        cmp ecx, 15
        jbe .tb_trunc_ok
        mov ecx, 15
.tb_trunc_ok:
        call gui_copy_trunc
        mov eax, [.tb_x]
        add eax, 22
        mov ebx, TASKBAR_Y + 5
        mov esi, gui_title_buf
        mov edx, 0x00000000
        call gui_bb_draw_string

        pop eax
        pop ecx
        mov eax, [gui_task_btn_w]
        add [.tb_x], eax
        add dword [.tb_x], TASKBAR_TASK_GAP
        mov eax, GUI_SCREEN_W - TASKBAR_TRAY_W - 8
        cmp dword [.tb_x], eax
        jge .tb_end
.tb_next:
        inc ecx
        jmp .tb_loop
.tb_end:

        popad
        ret
.tb_x  dd 0

;---------------------------------------
; gui_taskbar_btn_click - Handle click on taskbar window buttons
;---------------------------------------
gui_taskbar_btn_click:
        pushad
        call gui_taskbar_layout
        mov dword [.tbc_x], TASKBAR_TASKS_X
        xor ecx, ecx
.tbc_loop:
        cmp ecx, MAX_WINDOWS
        jge .tbc_done
        mov eax, ecx
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        test dword [eax + WIN_FLAGS], 0x01
        jz .tbc_next
        movzx edx, byte [eax + WIN_WORKSPACE]
        cmp dl, 0xFF
        je .tbc_ws_ok
        cmp dl, [gui_active_workspace]
        jne .tbc_next
.tbc_ws_ok:

        mov edx, [.tbc_x]
        cmp dword [mouse_x], edx
        jl .tbc_skip
        add edx, [gui_task_btn_w]
        cmp dword [mouse_x], edx
        jge .tbc_skip

        test dword [eax + WIN_FLAGS], 0x02
        jz .tbc_show
        push eax
        mov edx, [gui_focused_win]
        imul edx, WIN_STRUCT_SIZE
        add edx, gui_win_table
        cmp eax, edx
        pop eax
        jne .tbc_focus
        and dword [eax + WIN_FLAGS], ~0x02
        jmp .tbc_done
.tbc_show:
        or dword [eax + WIN_FLAGS], 0x02
.tbc_focus:
        push ecx
        mov eax, ecx
        call gui_focus_window
        pop ecx
        jmp .tbc_done

.tbc_skip:
        mov eax, [gui_task_btn_w]
        add [.tbc_x], eax
        add dword [.tbc_x], TASKBAR_TASK_GAP
        mov eax, GUI_SCREEN_W - TASKBAR_TRAY_W - 8
        cmp dword [.tbc_x], eax
        jge .tbc_done
.tbc_next:
        inc ecx
        jmp .tbc_loop
.tbc_done:
        popad
        ret
.tbc_x dd 0

;---------------------------------------
; gui_read_time - Read RTC into clock vars
;---------------------------------------
gui_read_time:
        pushad
        ; Read hours (CMOS reg 4)
        mov al, 4
        out 0x70, al
        in al, 0x71
        mov [.rt_hour], al
        ; Read minutes (CMOS reg 2)
        mov al, 2
        out 0x70, al
        in al, 0x71
        mov [.rt_min], al
        popad
        ret
.rt_hour: db 0
.rt_min:  db 0

;---------------------------------------
; gui_format_time - Format HH:MM into gui_clock_buf
;---------------------------------------
gui_format_time:
        pushad
        ; Hours (BCD)
        movzx eax, byte [gui_read_time.rt_hour]
        mov ebx, eax
        shr ebx, 4
        and eax, 0x0F
        add bl, '0'
        add al, '0'
        mov [gui_clock_buf], bl
        mov [gui_clock_buf+1], al
        mov byte [gui_clock_buf+2], ':'
        ; Minutes (BCD)
        movzx eax, byte [gui_read_time.rt_min]
        mov ebx, eax
        shr ebx, 4
        and eax, 0x0F
        add bl, '0'
        add al, '0'
        mov [gui_clock_buf+3], bl
        mov [gui_clock_buf+4], al
        mov byte [gui_clock_buf+5], 0
        popad
        ret

;---------------------------------------
; gui_format_time_12 - Format "H:MM AM/PM" into gui_clock_buf
;
; Windows 2000's default tray clock is 12-hour, without a leading zero on
; the hour. Reads the BCD values left by gui_read_time.
;---------------------------------------
gui_format_time_12:
        pushad
        ; BCD hour -> binary
        movzx eax, byte [gui_read_time.rt_hour]
        mov ebx, eax
        shr ebx, 4
        and eax, 0x0F
        imul ebx, ebx, 10
        add eax, ebx
        mov ecx, eax

        mov edi, gui_str_am
        cmp ecx, 12
        jb .ft_ampm_ready
        mov edi, gui_str_pm
.ft_ampm_ready:
        mov [.ft_ampm], edi

        ; 0 -> 12, 13..23 -> 1..11
        mov eax, ecx
        xor edx, edx
        mov ebx, 12
        div ebx
        mov eax, edx
        test eax, eax
        jnz .ft_hour_ready
        mov eax, 12
.ft_hour_ready:
        mov edi, gui_clock_buf
        xor edx, edx
        mov ebx, 10
        div ebx                    ; EAX = tens, EDX = ones
        test eax, eax
        jz .ft_no_tens
        add al, '0'
        mov [edi], al
        inc edi
.ft_no_tens:
        mov al, dl
        add al, '0'
        mov [edi], al
        inc edi
        mov byte [edi], ':'
        inc edi

        movzx eax, byte [gui_read_time.rt_min]
        mov ebx, eax
        shr ebx, 4
        and eax, 0x0F
        add bl, '0'
        add al, '0'
        mov [edi], bl
        mov [edi+1], al
        add edi, 2
        mov byte [edi], ' '
        inc edi

        mov esi, [.ft_ampm]
        mov al, [esi]
        mov [edi], al
        mov al, [esi+1]
        mov [edi+1], al
        mov byte [edi+2], 0
        popad
        ret
.ft_ampm: dd 0

;=======================================================================
; APP LAUNCHER MENU
;=======================================================================

gui_draw_menu:
        pushad
        ; Start menu sits directly above the taskbar.
        mov eax, GUI_MENU_H
        mov ebx, TASKBAR_Y
        sub ebx, eax
        mov [.dm_y], ebx

        ; Face and classic border
        mov eax, GUI_MENU_X
        mov ebx, [.dm_y]
        mov ecx, GUI_MENU_W
        mov edx, GUI_MENU_H
        mov esi, [gui_theme + THEME_MENU_BG]
        call gui_bb_fill_rect
        mov eax, GUI_MENU_X
        mov ebx, [.dm_y]
        mov ecx, GUI_MENU_W
        mov edx, GUI_MENU_H
        mov esi, 0x00000000
        call gui_bb_rect
        mov eax, GUI_MENU_X
        mov ebx, [.dm_y]
        mov ecx, GUI_MENU_W
        mov edx, GUI_MENU_H
        call gui_bb_raise_rect

        ; Vertical Windows 2000 banner with a top-to-bottom blue gradient.
        mov dword [.dm_row], 0
.dm_banner_loop:
        mov eax, [.dm_row]
        cmp eax, GUI_MENU_H - 2
        jge .dm_banner_done
        mov ebx, eax
        imul ebx, 110
        xor edx, edx
        mov ecx, GUI_MENU_H - 2
        div ecx                     ; eax = gradient factor
        mov esi, eax
        shl esi, 16                 ; red
        mov edx, eax
        shl edx, 8                  ; green
        or esi, edx
        add eax, 0x80               ; blue
        cmp eax, 255
        jbe .dm_banner_blue_ok
        mov eax, 255
.dm_banner_blue_ok:
        or esi, eax
        mov eax, GUI_MENU_X + 1
        mov ebx, [.dm_y]
        inc ebx
        add ebx, [.dm_row]
        mov ecx, GUI_MENU_BANNER_W - 1
        mov edx, 1
        call gui_bb_fill_rect
        inc dword [.dm_row]
        jmp .dm_banner_loop
.dm_banner_done:
        ; Vertical product name, rotated like the Windows 2000 banner and
        ; read from the bottom of the menu upwards.
        mov esi, gui_str_start_banner
        mov eax, GUI_MENU_X + 9
        mov ebx, [.dm_y]
        add ebx, GUI_MENU_H - 18
        mov edx, 0x00FFFFFF
        call gui_bb_draw_string_v
.dm_banner_text_done:
        ; Separator between banner and body.
        mov eax, GUI_MENU_BODY_X
        mov ebx, [.dm_y]
        inc ebx
        mov ecx, 1
        mov edx, GUI_MENU_H - 2
        mov esi, 0x00000000
        call gui_bb_vline

        ; Header: user tile and product line
        mov eax, GUI_MENU_BODY_X + 6
        mov ebx, [.dm_y]
        add ebx, 8
        mov ecx, 22
        mov edx, 22
        mov esi, 0x00000080
        call gui_bb_fill_rect
        mov eax, GUI_MENU_BODY_X + 6
        mov ebx, [.dm_y]
        add ebx, 8
        mov ecx, 22
        mov edx, 22
        mov esi, 0x00FFFFFF
        call gui_bb_rect
        mov eax, GUI_MENU_BODY_X + 13
        mov ebx, [.dm_y]
        add ebx, 10
        mov cl, 'B'
        mov edx, 0x00FFFFFF
        call gui_bb_draw_char

        mov eax, GUI_MENU_BODY_X + 34
        mov ebx, [.dm_y]
        add ebx, 8
        mov esi, gui_str_start_user
        mov edx, 0x00000000
        call gui_bb_draw_string
        mov eax, GUI_MENU_BODY_X + 34
        mov ebx, [.dm_y]
        add ebx, 26
        mov esi, gui_str_start_banner
        mov edx, 0x00404040
        call gui_bb_draw_string

        mov eax, GUI_MENU_BODY_X
        mov ebx, [.dm_y]
        add ebx, GUI_MENU_HEADER_H - 2
        mov ecx, GUI_MENU_W - GUI_MENU_BANNER_W - 2
        mov edx, 0x00808080
        call gui_bb_hline
        mov eax, GUI_MENU_BODY_X
        mov ebx, [.dm_y]
        add ebx, GUI_MENU_HEADER_H - 1
        mov ecx, GUI_MENU_W - GUI_MENU_BANNER_W - 2
        mov edx, 0x00FFFFFF
        call gui_bb_hline

        ; Items
        xor ecx, ecx
.dm_item:
        cmp ecx, GUI_MENU_ITEMS
        jge .dm_done

        mov [.dm_idx], ecx
        mov eax, ecx
        imul eax, GUI_MENU_ITEM_H
        add eax, GUI_MENU_HEADER_H
        add eax, [.dm_y]
        mov [.dm_iy], eax

        ; Section separators before Search and before Run...
        cmp ecx, 3
        je .dm_separator
        cmp ecx, 5
        jne .dm_no_sep
.dm_separator:
        mov eax, GUI_MENU_BODY_X + 4
        mov ebx, [.dm_iy]
        mov ecx, GUI_MENU_W - GUI_MENU_BANNER_W - 8
        mov edx, 0x00808080
        call gui_bb_hline
        mov eax, GUI_MENU_BODY_X + 4
        mov ebx, [.dm_iy]
        inc ebx
        mov ecx, GUI_MENU_W - GUI_MENU_BANNER_W - 8
        mov edx, 0x00FFFFFF
        call gui_bb_hline
.dm_no_sep:
        ; Hover / selection highlight
        mov ecx, [.dm_idx]
        cmp ecx, [gui_menu_sel]
        jne .dm_no_hl
        mov eax, GUI_MENU_BODY_X + 1
        mov ebx, [.dm_iy]
        inc ebx
        mov ecx, GUI_MENU_W - GUI_MENU_BANNER_W - 3
        mov edx, GUI_MENU_ITEM_H - 1
        mov esi, [gui_theme + THEME_MENU_HIGHLIGHT]
        call gui_bb_fill_rect
.dm_no_hl:
        ; Small application icon
        mov ecx, [.dm_idx]
        movzx eax, byte [gui_menu_icon_map + ecx]
        mov ebx, GUI_MENU_BODY_X + 7
        mov ecx, [.dm_iy]
        add ecx, 3
        call gui_draw_icon16

        ; Label
        mov ecx, [.dm_idx]
        mov eax, GUI_MENU_BODY_X + 29
        mov ebx, [.dm_iy]
        add ebx, 3
        mov esi, [gui_menu_strings + ecx*4]
        mov edx, [gui_theme + THEME_MENU_TEXT]
        cmp ecx, [gui_menu_sel]
        jne .dm_text_ready
        mov edx, 0x00FFFFFF
.dm_text_ready:
        call gui_bb_draw_string

        ; Fly-out marker
        mov ecx, [.dm_idx]
        movzx eax, byte [gui_menu_sub_map + ecx]
        cmp al, GUI_SUB_NONE
        je .dm_no_arrow
        mov edx, 0x00000000
        cmp ecx, [gui_menu_sel]
        jne .dm_arrow_color
        mov edx, 0x00FFFFFF
.dm_arrow_color:
        mov eax, GUI_MENU_X + GUI_MENU_W - 14
        mov ebx, [.dm_iy]
        add ebx, 7
        call gui_bb_tri_right
.dm_no_arrow:

        mov ecx, [.dm_idx]
        inc ecx
        jmp .dm_item
.dm_done:
        popad
        ret
.dm_y:   dd 0
.dm_row: dd 0
.dm_idx: dd 0
.dm_iy:  dd 0

;---------------------------------------
; gui_menu_hit_test - Check if point is in the Start menu
; EAX = x, EBX = y
; Returns: EAX = item index or -1
;---------------------------------------
gui_menu_hit_test:
        push ebx
        push ecx
        push edx
        mov ecx, TASKBAR_Y
        sub ecx, GUI_MENU_H         ; menu top

        cmp eax, GUI_MENU_BODY_X
        jl .mh_miss
        mov edx, GUI_MENU_X + GUI_MENU_W
        cmp eax, edx
        jge .mh_miss
        cmp ebx, ecx
        jl .mh_miss
        cmp ebx, TASKBAR_Y
        jge .mh_miss

        add ecx, GUI_MENU_HEADER_H
        cmp ebx, ecx
        jl .mh_miss
        sub ebx, ecx
        xor edx, edx
        mov eax, ebx
        mov ecx, GUI_MENU_ITEM_H
        div ecx
        cmp eax, GUI_MENU_ITEMS
        jge .mh_miss
        pop edx
        pop ecx
        pop ebx
        ret
.mh_miss:
        mov eax, -1
        pop edx
        pop ecx
        pop ebx
        ret

;---------------------------------------
; gui_submenu_geometry - compute the fly-out panel rectangle
;   Reads gui_menu_sub, writes gui_sub_x / gui_sub_y / gui_sub_h.
;---------------------------------------
gui_submenu_geometry:
        pushad
        mov eax, [gui_menu_sub]
        cmp eax, -1
        je .sg_none
        movzx ecx, byte [gui_sub_counts + eax]
        imul ecx, GUI_SUB_ITEM_H
        add ecx, 2
        ; Start level with the parent item, then clamp so the panel ends
        ; above the taskbar.
        mov edx, TASKBAR_Y
        sub edx, GUI_MENU_H
        add edx, GUI_MENU_HEADER_H
        mov ebx, eax
        imul ebx, GUI_MENU_ITEM_H
        add edx, ebx
        dec edx
        mov ebx, TASKBAR_Y
        sub ebx, ecx
        sub ebx, 1
        cmp edx, ebx
        jle .sg_y_ok
        mov edx, ebx
.sg_y_ok:
        mov dword [gui_sub_x], GUI_MENU_X + GUI_MENU_W + 1
        mov [gui_sub_y], edx
        mov [gui_sub_h], ecx
.sg_none:
        popad
        ret

;---------------------------------------
; gui_draw_submenu - draw the open Start-menu fly-out
;---------------------------------------
gui_draw_submenu:
        pushad
        cmp dword [gui_menu_sub], -1
        je .dsm_done
        call gui_submenu_geometry

        mov eax, [gui_sub_x]
        mov ebx, [gui_sub_y]
        mov ecx, GUI_SUB_W
        mov edx, [gui_sub_h]
        mov esi, [gui_theme + THEME_MENU_BG]
        call gui_bb_fill_rect
        mov eax, [gui_sub_x]
        mov ebx, [gui_sub_y]
        mov ecx, GUI_SUB_W
        mov edx, [gui_sub_h]
        mov esi, 0x00000000
        call gui_bb_rect
        mov eax, [gui_sub_x]
        mov ebx, [gui_sub_y]
        mov ecx, GUI_SUB_W
        mov edx, [gui_sub_h]
        call gui_bb_raise_rect

        mov ecx, [gui_menu_sub]
        movzx eax, byte [gui_sub_counts + ecx]
        mov [.dsm_count], eax
        mov eax, [gui_sub_strings_ptr + ecx*4]
        mov [.dsm_strings], eax
        mov eax, [gui_sub_icons_ptr + ecx*4]
        mov [.dsm_icons], eax

        xor ecx, ecx
.dsm_item:
        cmp ecx, [.dsm_count]
        jge .dsm_done
        mov [.dsm_idx], ecx
        mov eax, ecx
        imul eax, GUI_SUB_ITEM_H
        add eax, [gui_sub_y]
        inc eax
        mov [.dsm_iy], eax

        cmp ecx, [gui_menu_sub_sel]
        jne .dsm_no_hl
        mov eax, [gui_sub_x]
        inc eax
        mov ebx, [.dsm_iy]
        mov ecx, GUI_SUB_W - 2
        mov edx, GUI_SUB_ITEM_H - 1
        mov esi, [gui_theme + THEME_MENU_HIGHLIGHT]
        call gui_bb_fill_rect
.dsm_no_hl:
        mov ecx, [.dsm_idx]
        mov eax, [.dsm_icons]
        movzx eax, byte [eax + ecx]
        mov ebx, [gui_sub_x]
        add ebx, 6
        mov ecx, [.dsm_iy]
        add ecx, 3
        call gui_draw_icon16

        mov ecx, [.dsm_idx]
        mov eax, [.dsm_strings]
        mov esi, [eax + ecx*4]
        mov eax, [gui_sub_x]
        add eax, 28
        mov ebx, [.dsm_iy]
        add ebx, 3
        mov edx, [gui_theme + THEME_MENU_TEXT]
        cmp ecx, [gui_menu_sub_sel]
        jne .dsm_text_ok
        mov edx, 0x00FFFFFF
.dsm_text_ok:
        call gui_bb_draw_string

        mov ecx, [.dsm_idx]
        inc ecx
        jmp .dsm_item
.dsm_done:
        popad
        ret
.dsm_count:   dd 0
.dsm_icons:   dd 0
.dsm_strings: dd 0
.dsm_idx:     dd 0
.dsm_iy:      dd 0

;---------------------------------------
; gui_sub_hit_test - point inside the fly-out?
;   In : EAX = x, EBX = y
;   Out: EAX = item index, or -1
;---------------------------------------
gui_sub_hit_test:
        push ebx
        push ecx
        push edx
        cmp dword [gui_menu_sub], -1
        je .sht_miss
        push eax
        push ebx
        call gui_submenu_geometry
        pop ebx
        pop eax

        cmp eax, [gui_sub_x]
        jl .sht_miss
        mov ecx, [gui_sub_x]
        add ecx, GUI_SUB_W
        cmp eax, ecx
        jge .sht_miss
        cmp ebx, [gui_sub_y]
        jl .sht_miss
        mov ecx, [gui_sub_y]
        add ecx, [gui_sub_h]
        cmp ebx, ecx
        jge .sht_miss

        sub ebx, [gui_sub_y]
        dec ebx
        cmp ebx, 0
        jl .sht_miss
        xor edx, edx
        mov eax, ebx
        mov ecx, GUI_SUB_ITEM_H
        div ecx
        mov ecx, [gui_menu_sub]
        movzx edx, byte [gui_sub_counts + ecx]
        cmp eax, edx
        jge .sht_miss
        pop edx
        pop ecx
        pop ebx
        ret
.sht_miss:
        mov eax, -1
        pop edx
        pop ecx
        pop ebx
        ret

;---------------------------------------
; gui_menu_sync_sub - open/close the fly-out matching gui_menu_sel
;---------------------------------------
gui_menu_sync_sub:
        pushad
        mov eax, [gui_menu_sel]
        cmp eax, 0
        jl .sync_close
        cmp eax, GUI_MENU_ITEMS
        jge .sync_close
        movzx ecx, byte [gui_menu_sub_map + eax]
        cmp cl, GUI_SUB_NONE
        je .sync_close
        movzx ecx, cl
        cmp ecx, [gui_menu_sub]
        je .sync_done
        mov [gui_menu_sub], ecx
        mov dword [gui_menu_sub_sel], 0
        jmp .sync_done
.sync_close:
        mov dword [gui_menu_sub], -1
        mov dword [gui_menu_sub_sel], -1
.sync_done:
        popad
        ret

;---------------------------------------
; gui_menu_select - Execute menu item
; EAX = item index
;---------------------------------------
gui_menu_select:
        pushad
        cmp eax, GUI_MENU_ITEMS
        jae .ms_ret
        movzx ecx, byte [gui_menu_sub_map + eax]
        cmp cl, GUI_SUB_NONE
        je .ms_action
        ; A parent item opens its fly-out and leaves the Start menu up.
        mov [gui_menu_sub], ecx
        mov dword [gui_menu_sub_sel], 0
        popad
        ret
.ms_action:
        mov byte [gui_menu_open], 0
        mov dword [gui_menu_sub], -1
        mov dword [gui_menu_sub_sel], -1
        cmp eax, 3
        je .ms_search
        cmp eax, 4
        je .ms_help
        cmp eax, 5
        je .ms_run
        cmp eax, 6
        je .ms_shutdown
.ms_ret:
        popad
        ret
.ms_search:
.ms_run:
        mov esi, gui_prog_blauncher
        call gui_launch_app
        popad
        ret
.ms_help:
        popad
        call gui_applet_about
        ret
.ms_shutdown:
        popad
        call gui_applet_shutdown
        ret

;---------------------------------------
; gui_sub_select - Execute the selected fly-out item
;   In: EAX = item index, gui_menu_sub = submenu id
;---------------------------------------
gui_sub_select:
        pushad
        mov ecx, [gui_menu_sub]
        cmp ecx, 0
        jl .ss_ret
        cmp ecx, 3
        jge .ss_ret

        mov byte [gui_menu_open], 0
        mov dword [gui_menu_sub], -1
        mov dword [gui_menu_sub_sel], -1

        cmp ecx, 0
        je .ss_programs
        cmp ecx, 1
        je .ss_docs
        jmp .ss_settings
.ss_programs:
        cmp eax, 9
        jae .ss_ret
        mov esi, [gui_sub_programs_progs + eax*4]
        call gui_launch_app
        popad
        ret
.ss_docs:
        ; Both entries open the file manager; it is the shell's document view.
        mov esi, gui_prog_bhive
        call gui_launch_app
        popad
        ret
.ss_settings:
        cmp eax, 0
        jne .ss_datetime
        popad
        call gui_applet_settings
        ret
.ss_datetime:
        popad
        call gui_applet_clock
        ret
.ss_ret:
        popad
        ret

;---------------------------------------
; gui_spawn_program - load a program and create a READY GUI task
;   In : ESI = program name (valid in the caller's address space)
;   Out: EAX = task id, CF set on failure
;
; The loader must run in the master directory: PROGRAM_BASE belongs to the
; caller when this is invoked from a ring-3 launcher, and overwriting it would
; destroy the calling app. The name is copied to kernel memory first because
; the caller's user mapping is not present in the master directory.
;---------------------------------------
gui_spawn_program:
        ; Log the launch so tests and debugging can follow desktop activity.
        push esi
        pushad
        mov esi, burrows_log_launch
        call serial_print
        popad
        mov esi, [esp]
        call serial_print
        mov esi, burrows_log_crlf
        call serial_print
        pop esi
        pushad
        mov edi, gui_launch_name_buf
        mov ecx, 63
.gsp_copy:
        lodsb
        stosb
        test al, al
        jz .gsp_copied
        dec ecx
        jnz .gsp_copy
        mov byte [edi], 0
.gsp_copied:
        call sched_get_current_task
        mov [.gsp_tcb], eax
        test eax, eax
        jz .gsp_master
        xor eax, eax
        call sched_apply_cr3
.gsp_master:
        mov esi, gui_launch_name_buf
        mov byte [gui_launch_flag], 1
        call cmd_exec_program
        mov byte [gui_launch_flag], 0
        mov [.gsp_result], eax
        pushfd
        pop ecx
        mov [.gsp_flags], ecx
        mov ecx, [.gsp_tcb]
        test ecx, ecx
        jz .gsp_done
        mov eax, ecx
        call sched_apply_cr3
.gsp_done:
        mov eax, [.gsp_result]
        mov [esp + 28], eax
        mov ecx, [.gsp_flags]
        test ecx, 1
        jz .gsp_ok
        popad
        stc
        ret
.gsp_ok:
        popad
        clc
        ret
.gsp_tcb:    dd 0
.gsp_result: dd 0
.gsp_flags:  dd 0
gui_launch_name_buf: times 64 db 0

;---------------------------------------
; gui_launch_app - Launch external program
; ESI = program name
;---------------------------------------
gui_launch_app:
        ; Launcher is a singleton: focus the existing window instead of
        ; stacking duplicate launchers.
        push esi
        mov edi, gui_prog_blauncher
        call str_compare
        pop esi
        test eax, eax
        jnz .la_new
        mov eax, [gui_launcher_win]
        cmp eax, -1
        je .la_new
        call gui_focus_window
        ret
.la_new:
        call gui_spawn_program
        ret

;---------------------------------------
; gui_icon_launch - Launch program for desktop icon
; EAX = icon index (0-5)
;---------------------------------------
gui_icon_launch:
        pushad
        cmp eax, ICON_COUNT
        jge .il_done
        mov esi, [gui_icon_progs + eax*4]
        cmp esi, 0
        je .il_applet
        call gui_launch_app
        jmp .il_done
.il_applet:
        ; No external program — reserved for future applets
.il_done:
        popad
        ret

;=======================================================================
; INPUT HANDLING
;=======================================================================

gui_handle_input:
        pushad
        ; Expire the desktop icon double-click window.
        cmp dword [gui_icon_click_ttl], 0
        jle .hi_ttl_icon_done
        dec dword [gui_icon_click_ttl]
.hi_ttl_icon_done:
.hi_ttl_done:
        ; Track whether the pointer moved this frame. Menu hover must not
        ; overwrite a keyboard selection while the mouse sits still.
        mov byte [gui_mouse_moved], 0
        mov eax, [mouse_x]
        cmp eax, [gui_prev_mouse_x]
        jne .hi_mouse_moved
        mov eax, [mouse_y]
        cmp eax, [gui_prev_mouse_y]
        je .hi_mouse_move_done
.hi_mouse_moved:
        mov byte [gui_mouse_moved], 1
.hi_mouse_move_done:
        ; Get current mouse buttons
        movzx ecx, byte [mouse_buttons]

        ; Read and clear the button latch (captures brief clicks between frames)
        movzx eax, byte [mouse_btn_latch]
        mov byte [mouse_btn_latch], 0
        mov [.hi_latch], al

        ; New left-click: check latch bit 0
        test al, 0x01
        jz .hi_no_click

        ; --- Left click detected ---
        ; If context menu is open, check context menu hit
        cmp byte [gui_ctx_open], 1
        jne .hi_check_app_menu
        mov eax, [mouse_x]
        mov ebx, [mouse_y]
        call gui_ctx_hit_test
        cmp eax, -1
        je .hi_close_ctx
        call gui_ctx_select
        jmp .hi_update
.hi_close_ctx:
        mov byte [gui_ctx_open], 0
        jmp .hi_update

.hi_check_app_menu:
        ; If menu is open, check the fly-out first, then the main menu.
        cmp byte [gui_menu_open], 1
        jne .hi_check_menu_btn

        cmp dword [gui_menu_sub], -1
        je .hi_menu_main
        mov eax, [mouse_x]
        mov ebx, [mouse_y]
        call gui_sub_hit_test
        cmp eax, -1
        je .hi_menu_main
        call gui_sub_select
        jmp .hi_update
.hi_menu_main:
        mov eax, [mouse_x]
        mov ebx, [mouse_y]
        call gui_menu_hit_test
        cmp eax, -1
        je .hi_close_menu
        call gui_menu_select
        jmp .hi_update
.hi_close_menu:
        mov byte [gui_menu_open], 0
        mov dword [gui_menu_sub], -1
        mov dword [gui_menu_sub_sel], -1
        jmp .hi_update

.hi_check_menu_btn:
        ; Check Start button area
        cmp dword [mouse_x], 2
        jl .hi_check_win
        cmp dword [mouse_x], TASKBAR_START_W + 4
        jg .hi_check_win
        cmp dword [mouse_y], TASKBAR_Y + 2
        jl .hi_check_win
        cmp dword [mouse_y], TASKBAR_Y + 26
        jg .hi_check_win
        xor byte [gui_menu_open], 1
        mov dword [gui_menu_sel], -1
        mov dword [gui_menu_sub], -1
        mov dword [gui_menu_sub_sel], -1
        jmp .hi_update

.hi_check_win:
        ; Check taskbar window buttons and the tray
        cmp dword [mouse_y], TASKBAR_Y + 2
        jl .hi_check_icons
        cmp dword [mouse_y], TASKBAR_Y + 26
        jg .hi_check_icons
        ; Tray: the clock opens Date/Time, the workspace box cycles desktops.
        cmp dword [mouse_x], GUI_SCREEN_W - 70
        jge .hi_clock
        cmp dword [mouse_x], GUI_SCREEN_W - TASKBAR_TRAY_W + 10
        jl .hi_taskbtn
        cmp dword [mouse_x], GUI_SCREEN_W - TASKBAR_TRAY_W + 34
        jg .hi_update
        movzx eax, byte [gui_active_workspace]
        inc eax
        and eax, 3
        call gui_set_workspace
        jmp .hi_update
.hi_clock:
        call gui_applet_clock
        jmp .hi_update
.hi_taskbtn:
        cmp dword [mouse_x], TASKBAR_TASKS_X
        jl .hi_check_icons
        call gui_taskbar_btn_click
        jmp .hi_update

.hi_check_icons:
        ; Windows sit above desktop icons: a window covering an icon must
        ; receive the click, exactly as it does on Windows 2000.
        mov eax, [mouse_x]
        mov ebx, [mouse_y]
        call gui_find_window_at
        cmp eax, -1
        jne .hi_check_win2

        ; Otherwise a desktop icon may claim it. First click selects; a
        ; second click while the selection timer is live opens the icon.
        mov eax, [mouse_x]
        mov ebx, [mouse_y]
        call gui_icon_hit_test
        cmp eax, -1
        je .hi_update
        cmp eax, [gui_icon_sel]
        jne .hi_icon_select
        cmp dword [gui_icon_click_ttl], 0
        jle .hi_icon_select
        mov eax, [gui_icon_sel]
        call gui_icon_launch
        mov dword [gui_icon_sel], -1
        mov dword [gui_icon_click_ttl], 0
        jmp .hi_update
.hi_icon_select:
        mov [gui_icon_sel], eax
        mov dword [gui_icon_click_ttl], 20
        jmp .hi_update
.hi_check_win2:
        ; Check window clicks (close button / title bar drag)
        call gui_handle_window_click
        jmp .hi_update

.hi_no_click:
        ; Detect right click from latch (bit 1 = right button)
        test byte [.hi_latch], 0x02
        jz .hi_no_rclick

        ; Right click on desktop (not on taskbar/window)
        cmp dword [mouse_y], TASKBAR_Y
        jge .hi_no_rclick
        ; Close app menu, open context menu
        mov byte [gui_menu_open], 0
        mov byte [gui_ctx_open], 1
        mov eax, [mouse_x]
        mov [gui_ctx_x], eax
        mov eax, [mouse_y]
        mov [gui_ctx_y], eax
        mov dword [gui_ctx_sel], -1
        jmp .hi_update

.hi_no_rclick:
        ; Handle drag in progress
        cmp byte [gui_dragging], 1
        jne .hi_check_resize
        call gui_handle_drag

.hi_check_resize:
        ; v9.0: Handle edge resize in progress
        cmp byte [gui_resizing], 1
        jne .hi_check_release
        call gui_handle_resize

.hi_check_release:
        ; Mouse release ends drag and resize
        test cl, 0x01
        jnz .hi_hover
        mov byte [gui_dragging], 0
        mov byte [gui_resizing], 0

.hi_hover:
        cmp byte [gui_mouse_moved], 0
        je .hi_update
        ; Update menu hover. While the pointer is over a fly-out, that menu
        ; owns the selection; otherwise re-evaluate the main item.
        cmp byte [gui_menu_open], 1
        jne .hi_ctx_hover
        cmp dword [gui_menu_sub], -1
        je .hi_hover_main
        mov eax, [mouse_x]
        mov ebx, [mouse_y]
        call gui_sub_hit_test
        cmp eax, -1
        je .hi_hover_main
        mov [gui_menu_sub_sel], eax
        jmp .hi_update
.hi_hover_main:
        mov eax, [mouse_x]
        mov ebx, [mouse_y]
        call gui_menu_hit_test
        mov [gui_menu_sel], eax
        cmp eax, -1
        je .hi_hover_close_sub
        call gui_menu_sync_sub
        jmp .hi_update
.hi_hover_close_sub:
        mov dword [gui_menu_sub], -1
        mov dword [gui_menu_sub_sel], -1
        jmp .hi_update
.hi_ctx_hover:
        cmp byte [gui_ctx_open], 1
        jne .hi_update
        mov eax, [mouse_x]
        mov ebx, [mouse_y]
        call gui_ctx_hit_test
        mov [gui_ctx_sel], eax

.hi_update:
        ; Save mouse state
        movzx eax, byte [mouse_buttons]
        mov [gui_prev_mouse_btn], al
        mov eax, [mouse_x]
        mov [gui_prev_mouse_x], eax
        mov eax, [mouse_y]
        mov [gui_prev_mouse_y], eax

        ; Check keyboard
        call kb_pollchar
        cmp al, 0
        je .hi_done
        cmp al, 27             ; ESC
        je .hi_esc
        cmp al, 13             ; Enter activates the menu or selected icon
        je .hi_enter
        cmp al, 0x8E           ; Alt+Tab (special code)
        je .hi_alt_tab
        ; v9.0: Ctrl+Alt+1-4 → switch virtual desktop
        cmp al, 0x91           ; Ctrl+Alt+1 (kernel keyboard driver code)
        je .hi_ws1
        cmp al, 0x92           ; Ctrl+Alt+2
        je .hi_ws2
        cmp al, 0x93           ; Ctrl+Alt+3
        je .hi_ws3
        cmp al, 0x94           ; Ctrl+Alt+4
        je .hi_ws4
        cmp al, 0x95           ; Ctrl+Esc: open the Start menu
        je .hi_start_key
        ; Arrow keys navigate an open Start menu.
        cmp byte [gui_menu_open], 1
        jne .hi_no_arrows
        cmp al, 0x80           ; Up
        je .hi_menu_up
        cmp al, 0x81           ; Down
        je .hi_menu_down
        cmp al, 0x82           ; Left
        je .hi_menu_left
        cmp al, 0x83           ; Right
        je .hi_menu_right
.hi_no_arrows:
        cmp al, 0x84           ; F1 — open the launcher
        je .hi_launcher
        cmp al, 0x85           ; F2 — open a terminal
        je .hi_bterm
        jmp .hi_done

.hi_start_key:
        ; The keyboard's way into the Start menu, and the only one a headless
        ; test can drive: the first item is highlighted so the arrows and Enter
        ; behave the way they do on the real thing.
        cmp byte [gui_menu_open], 1
        jne .hi_start_open
        mov byte [gui_menu_open], 0
        mov dword [gui_menu_sub], -1
        mov dword [gui_menu_sel], -1
        jmp .hi_done
.hi_start_open:
        mov byte [gui_menu_open], 1
        mov dword [gui_menu_sub], -1
        mov dword [gui_menu_sub_sel], -1
        mov dword [gui_menu_sel], 0
        jmp .hi_done

.hi_launcher:
        mov esi, gui_prog_blauncher
        call gui_launch_app
        jmp .hi_done
.hi_bterm:
        mov esi, gui_prog_bterm
        call gui_launch_app
        jmp .hi_done

.hi_menu_down:
        cmp dword [gui_menu_sub], -1
        je .hi_md_main
        mov eax, [gui_menu_sub_sel]
        inc eax
        mov ecx, [gui_menu_sub]
        movzx edx, byte [gui_sub_counts + ecx]
        dec edx
        cmp eax, edx
        jle .hi_md_sub_store
        mov eax, edx
.hi_md_sub_store:
        mov [gui_menu_sub_sel], eax
        jmp .hi_done
.hi_md_main:
        mov eax, [gui_menu_sel]
        inc eax
        cmp eax, GUI_MENU_ITEMS
        jl .hi_md_main_store
        mov eax, GUI_MENU_ITEMS - 1
.hi_md_main_store:
        mov [gui_menu_sel], eax
        ; Moving through the main list does not open a fly-out: on the real
        ; thing the submenu opens on Right, Enter or hover, and opening it on
        ; every move traps the arrow keys inside it so the later items of the
        ; main list cannot be reached from the keyboard at all.
        mov dword [gui_menu_sub], -1
        mov dword [gui_menu_sub_sel], -1
        jmp .hi_done

.hi_menu_up:
        cmp dword [gui_menu_sub], -1
        je .hi_mu_main
        mov eax, [gui_menu_sub_sel]
        dec eax
        cmp eax, 0
        jge .hi_mu_sub_store
        xor eax, eax
.hi_mu_sub_store:
        mov [gui_menu_sub_sel], eax
        jmp .hi_done
.hi_mu_main:
        mov eax, [gui_menu_sel]
        dec eax
        cmp eax, 0
        jge .hi_mu_main_store
        xor eax, eax
.hi_mu_main_store:
        mov [gui_menu_sel], eax
        mov dword [gui_menu_sub], -1
        mov dword [gui_menu_sub_sel], -1
        jmp .hi_done

.hi_menu_right:
        mov eax, [gui_menu_sel]
        cmp eax, 0
        jl .hi_done
        cmp eax, GUI_MENU_ITEMS
        jge .hi_done
        movzx ecx, byte [gui_menu_sub_map + eax]
        cmp cl, GUI_SUB_NONE
        je .hi_done
        mov [gui_menu_sub], ecx
        mov dword [gui_menu_sub_sel], 0
        jmp .hi_done

.hi_menu_left:
        mov dword [gui_menu_sub], -1
        mov dword [gui_menu_sub_sel], -1
        jmp .hi_done

.hi_enter:
        cmp byte [gui_menu_open], 1
        jne .hi_enter_icon
        cmp dword [gui_menu_sub], -1
        je .hi_enter_main
        cmp dword [gui_menu_sub_sel], -1
        je .hi_done
        mov eax, [gui_menu_sub_sel]
        call gui_sub_select
        jmp .hi_done
.hi_enter_main:
        cmp dword [gui_menu_sel], -1
        je .hi_done
        mov eax, [gui_menu_sel]
        call gui_menu_select
        jmp .hi_done
.hi_enter_icon:
        cmp dword [gui_icon_sel], -1
        je .hi_done
        mov eax, [gui_icon_sel]
        call gui_icon_launch
        jmp .hi_done

.hi_alt_tab:
        ; Cycle to next visible window
        call gui_cycle_window
        jmp .hi_done

; v9.0: Virtual desktop switch helpers
.hi_ws1: xor eax, eax
         call gui_set_workspace
         jmp .hi_done
.hi_ws2: mov eax, 1
         call gui_set_workspace
         jmp .hi_done
.hi_ws3: mov eax, 2
         call gui_set_workspace
         jmp .hi_done
.hi_ws4: mov eax, 3
         call gui_set_workspace
         jmp .hi_done

.hi_esc:
        ; ESC closes menus if open, else exits desktop
        cmp byte [gui_ctx_open], 1
        jne .hi_esc_menu
        mov byte [gui_ctx_open], 0
        jmp .hi_done
.hi_esc_menu:
        cmp byte [gui_menu_open], 1
        jne .hi_exit
        cmp dword [gui_menu_sub], -1
        je .hi_esc_close_menu
        mov dword [gui_menu_sub], -1
        mov dword [gui_menu_sub_sel], -1
        jmp .hi_done
.hi_esc_close_menu:
        mov byte [gui_menu_open], 0
        jmp .hi_done
.hi_exit:
        mov byte [gui_exit_flag], 1
.hi_done:
        popad
        ret
.hi_latch: db 0

;---------------------------------------
; gui_toggle_maximize - maximize or restore a window
;   In: EAX = window index
;---------------------------------------
gui_toggle_maximize:
        pushad
        cmp eax, MAX_WINDOWS
        jae .tm_done
        mov [.tm_idx], eax
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        mov ebx, eax
        test dword [ebx + WIN_FLAGS], 0x04
        jnz .tm_restore

        mov ecx, [ebx + WIN_X]
        mov [ebx + WIN_SAVE_X], ecx
        mov ecx, [ebx + WIN_Y]
        mov [ebx + WIN_SAVE_Y], ecx
        mov ecx, [ebx + WIN_W]
        mov [ebx + WIN_SAVE_W], ecx
        mov ecx, [ebx + WIN_H]
        mov [ebx + WIN_SAVE_H], ecx

        mov dword [ebx + WIN_X], BORDER_W
        mov dword [ebx + WIN_Y], TITLEBAR_H
        mov dword [ebx + WIN_W], GUI_SCREEN_W - 2 * BORDER_W
        mov dword [ebx + WIN_H], TASKBAR_Y - TITLEBAR_H - BORDER_W
        or dword [ebx + WIN_FLAGS], 0x04
        jmp .tm_realloc

.tm_restore:
        mov ecx, [ebx + WIN_SAVE_X]
        mov [ebx + WIN_X], ecx
        mov ecx, [ebx + WIN_SAVE_Y]
        mov [ebx + WIN_Y], ecx
        mov ecx, [ebx + WIN_SAVE_W]
        mov [ebx + WIN_W], ecx
        mov ecx, [ebx + WIN_SAVE_H]
        mov [ebx + WIN_H], ecx
        and dword [ebx + WIN_FLAGS], ~0x04
.tm_realloc:
        mov eax, [.tm_idx]
        call gui_realloc_window_buf
.tm_done:
        popad
        ret
.tm_idx: dd 0

;---------------------------------------
; gui_handle_window_click - Process click on windows
;---------------------------------------
gui_handle_window_click:
        pushad
        mov eax, [mouse_x]
        mov ebx, [mouse_y]
        call gui_find_window_at
        cmp eax, -1
        je .wc_done

        ; Focus this window
        mov [.wc_idx], eax
        push eax
        call gui_focus_window
        pop eax

        ; Get window entry
        mov ebx, eax
        imul ebx, WIN_STRUCT_SIZE
        add ebx, gui_win_table

        ; --- Check close button ---
        mov ecx, [ebx + WIN_X]
        add ecx, [ebx + WIN_W]
        add ecx, BORDER_W
        sub ecx, CLOSE_BTN_SIZE + 4
        mov edx, [ebx + WIN_Y]
        sub edx, TITLEBAR_H
        add edx, 3

        cmp dword [mouse_x], ecx
        jl .wc_not_close
        push ecx
        add ecx, CLOSE_BTN_SIZE
        cmp dword [mouse_x], ecx
        pop ecx
        jg .wc_not_close
        cmp dword [mouse_y], edx
        jl .wc_not_close
        add edx, CLOSE_BTN_SIZE
        cmp dword [mouse_y], edx
        jg .wc_not_close
        ; Close!
        call gui_destroy_window_n
        jmp .wc_done

.wc_not_close:
        ; --- Check maximize button ---
        mov ecx, [ebx + WIN_X]
        add ecx, [ebx + WIN_W]
        add ecx, BORDER_W
        sub ecx, 2 * (CLOSE_BTN_SIZE + 2) + 4
        mov edx, [ebx + WIN_Y]
        sub edx, TITLEBAR_H
        add edx, 3

        cmp dword [mouse_x], ecx
        jl .wc_not_max
        push ecx
        add ecx, CLOSE_BTN_SIZE
        cmp dword [mouse_x], ecx
        pop ecx
        jg .wc_not_max
        cmp dword [mouse_y], edx
        jl .wc_not_max
        add edx, CLOSE_BTN_SIZE
        cmp dword [mouse_y], edx
        jg .wc_not_max

        ; Toggle maximize
        mov eax, [.wc_idx]
        call gui_toggle_maximize
        jmp .wc_done

.wc_not_max:
        ; --- Check minimize button ---
        mov ecx, [ebx + WIN_X]
        add ecx, [ebx + WIN_W]
        add ecx, BORDER_W
        sub ecx, 3 * (CLOSE_BTN_SIZE + 2) + 4
        mov edx, [ebx + WIN_Y]
        sub edx, TITLEBAR_H
        add edx, 3

        cmp dword [mouse_x], ecx
        jl .wc_not_min
        push ecx
        add ecx, CLOSE_BTN_SIZE
        cmp dword [mouse_x], ecx
        pop ecx
        jg .wc_not_min
        cmp dword [mouse_y], edx
        jl .wc_not_min
        add edx, CLOSE_BTN_SIZE
        cmp dword [mouse_y], edx
        jg .wc_not_min

        ; Minimize: clear visible bit (bit1)
        and dword [ebx + WIN_FLAGS], ~0x02
        jmp .wc_done

.wc_not_min:
        ; --- Check all eight resize borders (4-pixel hit zones) ---
        ; ECX bits: 0=right, 1=bottom, 2=left, 3=top
        xor ecx, ecx
        ; Right
        mov edx, [ebx + WIN_X]
        add edx, [ebx + WIN_W]
        sub edx, 4
        cmp dword [mouse_x], edx
        jl .wc_check_left
        add edx, 4
        cmp dword [mouse_x], edx
        jg .wc_check_left
        or ecx, 0x01
.wc_check_left:
        mov edx, [ebx + WIN_X]
        sub edx, BORDER_W
        cmp dword [mouse_x], edx
        jl .wc_check_bottom
        add edx, 4
        cmp dword [mouse_x], edx
        jg .wc_check_bottom
        or ecx, 0x04
.wc_check_bottom:
        mov edx, [ebx + WIN_Y]
        add edx, [ebx + WIN_H]
        sub edx, 4
        cmp dword [mouse_y], edx
        jl .wc_check_top
        add edx, 4
        cmp dword [mouse_y], edx
        jg .wc_check_top
        or ecx, 0x02
.wc_check_top:
        mov edx, [ebx + WIN_Y]
        sub edx, TITLEBAR_H
        cmp dword [mouse_y], edx
        jl .wc_edge_done
        add edx, 4
        cmp dword [mouse_y], edx
        jg .wc_edge_done
        or ecx, 0x08
.wc_edge_done:
        test ecx, ecx
        jz .wc_check_titlebar_drag  ; no edge hit, check title drag
        ; Start resize
        mov byte [gui_resizing], 1
        mov eax, [.wc_idx]
        mov [gui_resize_win], eax
        mov [gui_resize_edge], cl
        ; Save original mouse position and window geometry
        mov edx, [mouse_x]
        mov [gui_resize_mx0], edx
        mov edx, [mouse_y]
        mov [gui_resize_my0], edx
        mov edx, [ebx + WIN_X]
        mov [gui_resize_x0], edx
        mov edx, [ebx + WIN_Y]
        mov [gui_resize_y0], edx
        mov edx, [ebx + WIN_W]
        mov [gui_resize_w0], edx
        mov edx, [ebx + WIN_H]
        mov [gui_resize_h0], edx
        jmp .wc_done

.wc_check_titlebar_drag:
        mov ecx, [ebx + WIN_Y]
        sub ecx, TITLEBAR_H
        cmp dword [mouse_y], ecx
        jl .wc_done
        mov edx, [ebx + WIN_Y]
        cmp [mouse_y], edx
        jge .wc_done

        ; Double-clicking the title bar toggles maximize, as in Windows 2000.
        mov eax, [.wc_idx]
        cmp eax, [gui_title_dbl_win]
        jne .wc_title_single
        mov ecx, [tick_count]
        cmp ecx, [gui_title_dbl_until]
        ja .wc_title_single
        mov dword [gui_title_dbl_until], 0
        mov dword [gui_title_dbl_win], -1
        call gui_toggle_maximize
        jmp .wc_done
.wc_title_single:
        mov [gui_title_dbl_win], eax
        mov eax, [tick_count]
        add eax, 30
        mov [gui_title_dbl_until], eax

        ; Start drag
        mov byte [gui_dragging], 1
        mov eax, [.wc_idx]
        mov [gui_drag_win], eax
        mov ecx, [mouse_x]
        sub ecx, [ebx + WIN_X]
        add ecx, BORDER_W
        mov [gui_drag_off_x], ecx
        mov ecx, [mouse_y]
        sub ecx, [ebx + WIN_Y]
        add ecx, TITLEBAR_H
        mov [gui_drag_off_y], ecx
.wc_done:
        popad
        ret
.wc_idx: dd 0

;---------------------------------------
; gui_handle_drag - Move window with mouse
;---------------------------------------
gui_handle_drag:
        pushad
        mov eax, [gui_drag_win]
        cmp eax, MAX_WINDOWS
        jge .dr_done
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table

        mov ebx, [mouse_x]
        sub ebx, [gui_drag_off_x]
        add ebx, BORDER_W
        cmp ebx, 0
        jge .dr_x_ok
        xor ebx, ebx
.dr_x_ok:
        mov ecx, GUI_SCREEN_W - 64
        cmp ebx, ecx
        jle .dr_x_max
        mov ebx, ecx
.dr_x_max:
        mov [eax + WIN_X], ebx

        mov ebx, [mouse_y]
        sub ebx, [gui_drag_off_y]
        add ebx, TITLEBAR_H
        cmp ebx, 0
        jge .dr_y_ok
        xor ebx, ebx
.dr_y_ok:
        mov ecx, TASKBAR_Y - 24
        cmp ebx, ecx
        jle .dr_y_max
        mov ebx, ecx
.dr_y_max:
        mov [eax + WIN_Y], ebx
.dr_done:
        popad
        ret

;---------------------------------------
; gui_handle_resize - Resize window with mouse (v9.0)
; Called when gui_resizing==1. Uses gui_resize_* scratch vars.
;---------------------------------------
gui_handle_resize:
        pushad
        mov eax, [gui_resize_win]
        cmp eax, MAX_WINDOWS
        jge .hrs_done
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table

        ; Delta from original mouse position
        mov ebx, [mouse_x]
        sub ebx, [gui_resize_mx0]
        mov ecx, [mouse_y]
        sub ecx, [gui_resize_my0]

        ; Apply right-edge resize
        test byte [gui_resize_edge], 0x01
        jz .hrs_no_right
        mov edx, [gui_resize_w0]
        add edx, ebx                ; new_w = orig_w + dx
        cmp edx, 64                 ; minimum width 64px
        jge .hrs_right_ok
        mov edx, 64
.hrs_right_ok:
        mov [eax + WIN_W], edx
.hrs_no_right:

        ; Apply left-edge resize (the right edge stays put)
        test byte [gui_resize_edge], 0x04
        jz .hrs_no_left
        mov edx, [gui_resize_w0]
        sub edx, ebx                ; new_w = orig_w - dx
        cmp edx, 64
        jge .hrs_left_ok
        mov edx, 64
.hrs_left_ok:
        mov [eax + WIN_W], edx
        mov edx, [gui_resize_w0]
        sub edx, [eax + WIN_W]
        add edx, [gui_resize_x0]
        mov [eax + WIN_X], edx
.hrs_no_left:

        ; Apply bottom-edge resize
        test byte [gui_resize_edge], 0x02
        jz .hrs_no_bottom
        mov edx, [gui_resize_h0]
        add edx, ecx                ; new_h = orig_h + dy
        cmp edx, 32                 ; minimum height 32px
        jge .hrs_bottom_ok
        mov edx, 32
.hrs_bottom_ok:
        mov [eax + WIN_H], edx
.hrs_no_bottom:

        ; Apply top-edge resize (the bottom edge stays put)
        test byte [gui_resize_edge], 0x08
        jz .hrs_done
        mov edx, [gui_resize_h0]
        sub edx, ecx                ; new_h = orig_h - dy
        cmp edx, 32
        jge .hrs_top_ok
        mov edx, 32
.hrs_top_ok:
        mov [eax + WIN_H], edx
        mov edx, [gui_resize_h0]
        sub edx, [eax + WIN_H]
        add edx, [gui_resize_y0]
        mov [eax + WIN_Y], edx

.hrs_done:
        mov eax, [gui_resize_win]
        call gui_realloc_window_buf
        popad
        ret

;=======================================================================
; RIGHT-CLICK CONTEXT MENU
;=======================================================================

GUI_CTX_W       equ 150
GUI_CTX_ITEM_H  equ 22
GUI_CTX_ITEMS   equ 5

;---------------------------------------
; gui_draw_ctx_menu - Draw right-click context menu
;---------------------------------------
gui_draw_ctx_menu:
        pushad
        mov eax, [gui_ctx_x]
        mov ebx, [gui_ctx_y]

        ; Clip menu to screen
        mov ecx, eax
        add ecx, GUI_CTX_W
        cmp ecx, GUI_SCREEN_W
        jle .ctx_xok
        sub eax, GUI_CTX_W
.ctx_xok:
        mov ecx, ebx
        add ecx, GUI_CTX_ITEMS * GUI_CTX_ITEM_H
        cmp ecx, TASKBAR_Y
        jle .ctx_yok
        sub ebx, GUI_CTX_ITEMS * GUI_CTX_ITEM_H
.ctx_yok:
        mov [.ctx_dx], eax
        mov [.ctx_dy], ebx

        ; Background
        push ecx
        push edx
        mov ecx, GUI_CTX_W
        mov edx, GUI_CTX_ITEMS * GUI_CTX_ITEM_H
        mov esi, [gui_theme + THEME_MENU_BG]
        call gui_bb_fill_rect
        pop edx
        pop ecx

        ; Border: a black outline with the classic raised inner edge.
        mov eax, [.ctx_dx]
        mov ebx, [.ctx_dy]
        push ecx
        push edx
        mov ecx, GUI_CTX_W
        mov edx, GUI_CTX_ITEMS * GUI_CTX_ITEM_H
        mov esi, 0x00000000
        call gui_bb_rect
        pop edx
        pop ecx
        mov eax, [.ctx_dx]
        inc eax
        mov ebx, [.ctx_dy]
        inc ebx
        mov ecx, GUI_CTX_W - 2
        mov edx, GUI_CTX_ITEMS * GUI_CTX_ITEM_H - 2
        call gui_bb_raise_rect

        ; Draw items
        xor ecx, ecx
.ctx_item:
        cmp ecx, GUI_CTX_ITEMS
        jge .ctx_done

        mov ebx, ecx
        imul ebx, GUI_CTX_ITEM_H
        add ebx, [.ctx_dy]

        ; Section separator before Refresh and before Exit Desktop
        cmp ecx, 2
        je .ctx_sep
        cmp ecx, 4
        jne .ctx_no_sep
.ctx_sep:
        push ecx
        mov eax, [.ctx_dx]
        add eax, 4
        mov ecx, GUI_CTX_W - 8
        mov edx, 0x00808080
        call gui_bb_hline
        pop ecx
        push ecx
        push ebx
        mov eax, [.ctx_dx]
        add eax, 4
        inc ebx
        mov ecx, GUI_CTX_W - 8
        mov edx, 0x00FFFFFF
        call gui_bb_hline
        pop ebx
        pop ecx
.ctx_no_sep:

        ; Highlight if hovered
        cmp ecx, [gui_ctx_sel]
        jne .ctx_no_hl
        push ecx
        mov eax, [.ctx_dx]
        inc eax
        inc ebx
        mov ecx, GUI_CTX_W - 2
        mov edx, GUI_CTX_ITEM_H - 1
        mov esi, [gui_theme + THEME_MENU_HIGHLIGHT]
        call gui_bb_fill_rect
        dec ebx
        pop ecx
.ctx_no_hl:
        push ecx
        mov eax, [.ctx_dx]
        add eax, 12
        add ebx, 3
        mov esi, [gui_ctx_strings + ecx*4]
        mov edx, [gui_theme + THEME_MENU_TEXT]
        cmp ecx, [gui_ctx_sel]
        jne .ctx_txt_ok
        mov edx, 0x00FFFFFF
.ctx_txt_ok:
        call gui_bb_draw_string
        pop ecx

        inc ecx
        jmp .ctx_item
.ctx_done:
        popad
        ret
.ctx_dx: dd 0
.ctx_dy: dd 0

;---------------------------------------
; gui_ctx_hit_test - Check if click is in context menu
; EAX = x, EBX = y  Returns: EAX = item or -1
;---------------------------------------
gui_ctx_hit_test:
        push ebx
        push ecx
        push edx

        ; Reconstruct clipped position
        mov ecx, [gui_ctx_x]
        mov edx, ecx
        add edx, GUI_CTX_W
        cmp edx, GUI_SCREEN_W
        jle .cth_xok
        sub ecx, GUI_CTX_W
.cth_xok:
        mov edx, [gui_ctx_y]
        push edx
        mov edi, edx
        add edi, GUI_CTX_ITEMS * GUI_CTX_ITEM_H
        cmp edi, TASKBAR_Y
        jle .cth_yok
        sub edx, GUI_CTX_ITEMS * GUI_CTX_ITEM_H
.cth_yok:

        ; Check bounds
        cmp eax, ecx
        jl .cth_miss
        add ecx, GUI_CTX_W
        cmp eax, ecx
        jge .cth_miss
        cmp ebx, edx
        jl .cth_miss
        push edx
        add edx, GUI_CTX_ITEMS * GUI_CTX_ITEM_H
        cmp ebx, edx
        pop edx
        jge .cth_miss

        ; Compute item index
        sub ebx, edx
        xor edx, edx
        mov eax, ebx
        mov ecx, GUI_CTX_ITEM_H
        div ecx
        cmp eax, GUI_CTX_ITEMS
        jge .cth_miss
        pop edx
        pop edx
        pop ecx
        pop ebx
        ret
.cth_miss:
        pop edx
        mov eax, -1
        pop edx
        pop ecx
        pop ebx
        ret

;---------------------------------------
; gui_ctx_select - Execute context menu item
; EAX = item index
;---------------------------------------
gui_ctx_select:
        pushad
        mov byte [gui_ctx_open], 0
        cmp eax, 0
        je .cx_term
        cmp eax, 1
        je .cx_files
        cmp eax, 2
        je .cx_refresh
        cmp eax, 3
        je .cx_settings
        cmp eax, 4
        je .cx_exit
        popad
        ret
.cx_term:
        mov esi, gui_prog_bterm
        call gui_launch_app
        popad
        ret
.cx_files:
        mov esi, gui_prog_bhive
        call gui_launch_app
        popad
        ret
.cx_refresh:
        ; The compositor repaints every frame; closing the menu is enough.
        popad
        ret
.cx_settings:
        popad
        call gui_applet_settings
        ret
.cx_exit:
        mov byte [gui_exit_flag], 1
        popad
        ret

;=======================================================================
; BUILT-IN APPLETS
;=======================================================================

;---------------------------------------
; gui_applet_about - About Mellivora OS
;---------------------------------------
gui_applet_about:
        pushad
        mov eax, 170
        mov ebx, 140
        mov ecx, 300
        mov edx, 210
        mov esi, gui_str_about
        call gui_create_window
        cmp eax, -1
        je .abt_ret
        mov [.abt_win], eax

.abt_loop:
        ; Draw desktop + all windows
        call gui_draw_background
        call gui_draw_all_windows

        ; Draw about content
        mov eax, [.abt_win]
        imul eax, WIN_STRUCT_SIZE
        lea ebp, [gui_win_table + eax]
        mov ecx, [ebp + WIN_X]
        mov edx, [ebp + WIN_Y]

        ; Line 1: "Mellivora OS"
        lea eax, [ecx + 80]
        lea ebx, [edx + 26]
        mov esi, gui_str_about_l1
        mov edx, [gui_theme + THEME_ACCENT]
        call gui_bb_draw_string

        ; Line 2: "v3.0.0"
        mov edx, [ebp + WIN_Y]
        lea eax, [ecx + 110]
        lea ebx, [edx + 50]
        mov esi, gui_str_about_l2
        mov edx, 0x00606060
        call gui_bb_draw_string

        ; Line 3: "Burrows Desktop"
        mov edx, [ebp + WIN_Y]
        lea eax, [ecx + 40]
        lea ebx, [edx + 82]
        mov esi, gui_str_about_l3
        mov edx, 0x00000000
        call gui_bb_draw_string

        ; Line 4: "32-bit Protected Mode"
        mov edx, [ebp + WIN_Y]
        lea eax, [ecx + 30]
        lea ebx, [edx + 106]
        mov esi, gui_str_about_l4
        mov edx, 0x00404040
        call gui_bb_draw_string

        ; Line 5: "HBFS / 2 GB"
        mov edx, [ebp + WIN_Y]
        lea eax, [ecx + 60]
        lea ebx, [edx + 130]
        mov esi, gui_str_about_l5
        mov edx, 0x00404040
        call gui_bb_draw_string

        ; Separator and the classic OK button
        mov eax, [ebp + WIN_X]
        add eax, 16
        mov ebx, [ebp + WIN_Y]
        add ebx, 152
        mov ecx, 268
        mov edx, 0x00808080
        call gui_bb_hline
        mov eax, [ebp + WIN_X]
        add eax, 120
        mov ebx, [ebp + WIN_Y]
        add ebx, 162
        mov ecx, 60
        mov edx, 24
        mov esi, [gui_theme + THEME_BUTTON_BG]
        call gui_bb_button_3d
        mov eax, [ebp + WIN_X]
        add eax, 142
        mov ebx, [ebp + WIN_Y]
        add ebx, 166
        mov esi, gui_str_ok
        mov edx, 0x00000000
        call gui_bb_draw_string

        call gui_draw_taskbar
        call gui_draw_cursor
        call gui_flip

        ; Input
        call kb_pollchar
        cmp al, 27
        je .abt_close
        cmp al, 0
        je .abt_mouse
        jmp .abt_loop

.abt_mouse:
        ; Check close button click
        movzx eax, byte [mouse_buttons]
        test eax, 0x01
        jz .abt_loop

        ; OK button: x+120..x+180, y+162..y+186
        mov eax, [.abt_win]
        imul eax, WIN_STRUCT_SIZE
        lea eax, [gui_win_table + eax]
        mov ebx, [eax + WIN_X]
        mov ecx, [eax + WIN_Y]
        mov edx, [mouse_x]
        sub edx, ebx
        mov ebx, [mouse_y]
        sub ebx, ecx
        cmp edx, 120
        jl .abt_check_close
        cmp edx, 180
        jg .abt_check_close
        cmp ebx, 162
        jl .abt_check_close
        cmp ebx, 186
        jg .abt_check_close
        jmp .abt_close
.abt_check_close:

        mov eax, [.abt_win]
        imul eax, WIN_STRUCT_SIZE
        lea eax, [gui_win_table + eax]
        ; Close button X range
        mov ebx, [eax + WIN_X]
        add ebx, [eax + WIN_W]
        add ebx, BORDER_W
        sub ebx, CLOSE_BTN_SIZE + 4
        cmp dword [mouse_x], ebx
        jl .abt_loop
        add ebx, CLOSE_BTN_SIZE
        cmp dword [mouse_x], ebx
        jg .abt_loop
        ; Close button Y range
        mov ecx, [eax + WIN_Y]
        sub ecx, TITLEBAR_H
        add ecx, 3
        cmp dword [mouse_y], ecx
        jl .abt_loop
        add ecx, CLOSE_BTN_SIZE
        cmp dword [mouse_y], ecx
        jg .abt_loop

.abt_close:
        ; Wait for button release
.abt_rel:
        movzx eax, byte [mouse_buttons]
        test eax, 0x01
        jnz .abt_rel
        mov eax, [.abt_win]
        call gui_destroy_window_n
.abt_ret:
        popad
        ret
.abt_win: dd 0

;---------------------------------------
; gui_applet_shutdown - Shut Down Windows
;
; The desktop's own power menu. Until now "Shut Down..." only left the desktop
; for the command prompt, which is not what the words say: this offers the four
; things a Windows 2000 machine offers, and the two that stop the machine go
; through the same ACPI paths the shell uses, so the filesystem is synced and
; the power comes off rather than the kernel halting with unsaved writes.
;---------------------------------------
SD_BTN_W        equ 180
SD_BTN_H        equ 26
SD_BTN_X        equ 60
SD_BTN_Y0       equ 44
SD_BTN_GAP      equ 34

gui_applet_shutdown:
        pushad
        mov eax, 170
        mov ebx, 120
        mov ecx, 300
        mov edx, 236
        mov esi, gui_str_shutdown
        call gui_create_window
        cmp eax, -1
        je .sd_ret
        mov [.sd_win], eax
        mov dword [.sd_sel], 0          ; "Shut down" is the default, as in 2000
        pushad
        mov esi, burrows_log_sd_dialog
        call serial_print
        popad

.sd_loop:
        call gui_draw_background
        call gui_draw_all_windows

        mov eax, [.sd_win]
        imul eax, WIN_STRUCT_SIZE
        lea ebp, [gui_win_table + eax]

        mov esi, gui_str_sd_prompt
        mov eax, [ebp + WIN_X]
        add eax, 20
        mov ebx, [ebp + WIN_Y]
        add ebx, 18
        mov edx, 0x00000000
        call gui_bb_draw_string

        ; Four buttons, each its own label
        xor edi, edi                    ; index
.sd_buttons:
        cmp edi, 4
        jae .sd_flip
        mov eax, [ebp + WIN_X]
        add eax, SD_BTN_X
        mov ebx, [ebp + WIN_Y]
        add ebx, SD_BTN_Y0
        mov ecx, edi
        imul ecx, SD_BTN_GAP
        add ebx, ecx
        mov ecx, SD_BTN_W
        mov edx, SD_BTN_H
        mov esi, [gui_theme + THEME_BUTTON_BG]
        cmp edi, [.sd_sel]
        jne .sd_face
        mov esi, [gui_theme + THEME_ACCENT]
.sd_face:
        call gui_bb_button_3d
        ; Label, left-aligned inside the button
        mov eax, [ebp + WIN_X]
        add eax, SD_BTN_X + 10
        mov ebx, [ebp + WIN_Y]
        add ebx, SD_BTN_Y0 + 7
        mov ecx, edi
        imul ecx, SD_BTN_GAP
        add ebx, ecx
        mov esi, [gui_sd_labels + edi*4]
        mov edx, 0x00000000
        call gui_bb_draw_string
        inc edi
        jmp .sd_buttons

.sd_flip:
        call gui_draw_taskbar
        call gui_draw_cursor
        call gui_flip

        call kb_pollchar
        cmp al, 27                      ; Esc cancels
        je .sd_close
        cmp al, 0x80                    ; Up
        je .sd_up
        cmp al, 0x81                    ; Down
        je .sd_down
        cmp al, 13                      ; Enter activates the highlighted button
        je .sd_enter
        cmp al, 0
        je .sd_mouse
        jmp .sd_loop

.sd_up:
        mov eax, [.sd_sel]
        test eax, eax
        jz .sd_loop
        dec dword [.sd_sel]
        jmp .sd_loop
.sd_down:
        mov eax, [.sd_sel]
        inc eax
        cmp eax, 4
        jae .sd_loop
        mov [.sd_sel], eax
        jmp .sd_loop
.sd_enter:
        mov eax, [.sd_sel]
        call .sd_activate
        jmp .sd_close

.sd_mouse:
        movzx eax, byte [mouse_buttons]
        test eax, 0x01
        jz .sd_loop

        ; Relative to the window (EBP is reloaded: the drawing calls in the
        ; loop above do not have to preserve it)
        mov eax, [.sd_win]
        imul eax, WIN_STRUCT_SIZE
        lea ebp, [gui_win_table + eax]
        mov eax, [ebp + WIN_X]
        mov ebx, [mouse_x]
        sub ebx, eax
        mov eax, [ebp + WIN_Y]
        mov ecx, [mouse_y]
        sub ecx, eax

        cmp ebx, SD_BTN_X
        jl .sd_close_check
        mov eax, SD_BTN_X + SD_BTN_W
        cmp ebx, eax
        jg .sd_close_check
        mov eax, SD_BTN_Y0
        cmp ecx, eax
        jl .sd_close_check

        ; Which button? (ecx - Y0) / GAP
        sub ecx, SD_BTN_Y0
        mov eax, ecx
        xor edx, edx
        mov ecx, SD_BTN_GAP
        div ecx
        cmp eax, 4
        jae .sd_close_check
        ; A click in the gap below a button still selects it only if it is
        ; inside the button's own height.
        mov ecx, edx                    ; offset within the gap
        cmp ecx, SD_BTN_H
        jg .sd_close_check
        mov [.sd_sel], eax
        call .sd_activate
        jmp .sd_close

.sd_close_check:
        ; The title bar's close button cancels, like any other window.
        mov eax, [.sd_win]
        imul eax, WIN_STRUCT_SIZE
        lea eax, [gui_win_table + eax]
        mov ebx, [eax + WIN_X]
        add ebx, [eax + WIN_W]
        add ebx, BORDER_W
        sub ebx, CLOSE_BTN_SIZE + 4
        cmp dword [mouse_x], ebx
        jl .sd_loop
        add ebx, CLOSE_BTN_SIZE
        cmp dword [mouse_x], ebx
        jg .sd_loop
        mov ecx, [eax + WIN_Y]
        sub ecx, TITLEBAR_H
        add ecx, 3
        cmp dword [mouse_y], ecx
        jl .sd_loop
        add ecx, CLOSE_BTN_SIZE
        cmp dword [mouse_y], ecx
        jg .sd_loop

.sd_close:
.sd_rel:
        movzx eax, byte [mouse_buttons]
        test eax, 0x01
        jnz .sd_rel
        mov eax, [.sd_win]
        call gui_destroy_window_n
.sd_ret:
        popad
        ret

; .sd_activate - run the chosen action. EAX = button index.
;   0 shut down, 1 restart, 2 command prompt, 3 cancel
.sd_activate:
        cmp eax, 0
        je .sda_off
        cmp eax, 1
        je .sda_reset
        cmp eax, 2
        je .sda_shell
        ret
.sda_off:
        call hbfs_sync
        call acpi_poweroff              ; does not return
        ret
.sda_reset:
        call hbfs_sync
        cli
        call acpi_pm_reset
        ; acpi_pm_reset returns only when the FADT has no reset register: fall
        ; back to the keyboard controller, as the shell's reboot does.
        mov ecx, 0x20000
.sda_kbc:
        in al, 0x64
        test al, 0x02
        jz .sda_kbc_send
        dec ecx
        jnz .sda_kbc
        jmp .sda_triple
.sda_kbc_send:
        mov al, 0xFE
        out 0x64, al
.sda_triple:
        ; Nothing answered: a null IDT turns the next interrupt into a triple
        ; fault, which resets the machine.
        lidt [sda_null_idt]
        int 3
        hlt
        ret
.sda_shell:
        mov byte [gui_exit_flag], 1
        ret
.sd_win: dd 0
.sd_sel: dd 0

sda_null_idt:
        dw 0
        dd 0

;---------------------------------------
; gui_applet_clock - Digital clock
;---------------------------------------
gui_applet_clock:
        pushad
        mov eax, 180
        mov ebx, 140
        mov ecx, 260
        mov edx, 180
        mov esi, gui_str_clock
        call gui_create_window
        cmp eax, -1
        je .clk_ret
        mov [.clk_win], eax
        mov byte [.clk_edit], 0         ; 0=display mode, 1=edit mode
        ; Read current time into edit fields
        call rtc_read_time
        mov al, [rtc_hour]
        mov [.clk_ehour], al
        mov al, [rtc_min]
        mov [.clk_emin], al
        mov al, [rtc_sec]
        mov [.clk_esec], al
        mov al, [rtc_day]
        mov [.clk_eday], al
        mov al, [rtc_month]
        mov [.clk_emon], al
        mov al, [rtc_year]
        mov [.clk_eyear], al
        mov al, [rtc_century]
        mov [.clk_ecent], al

.clk_loop:
        call gui_draw_background
        call gui_draw_all_windows

        ; Get window position
        mov eax, [.clk_win]
        imul eax, WIN_STRUCT_SIZE
        lea ebp, [gui_win_table + eax]
        mov ecx, [ebp + WIN_X]
        mov edx, [ebp + WIN_Y]
        mov [.clk_wx], ecx
        mov [.clk_wy], edx

        cmp byte [.clk_edit], 0
        jne .clk_draw_edit

        ; --- DISPLAY MODE ---
        call gui_read_time
        call gui_format_time

        ; Time display
        lea eax, [ecx + 84]
        lea ebx, [edx + 36]
        mov esi, gui_clock_buf
        mov edx, [gui_theme + THEME_ACCENT]
        call gui_bb_draw_string

        ; Label
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 72]
        lea ebx, [edx + 12]
        mov esi, gui_str_clk_label
        mov edx, 0x00404040
        call gui_bb_draw_string

        ; Date line (read from RTC)
        call rtc_read_time
        call .clk_format_date
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 70]
        lea ebx, [edx + 56]
        mov esi, .clk_date_buf
        mov edx, 0x00606060
        call gui_bb_draw_string

        ; "Set" button (bottom area)
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        ; Classic 3-D button
        lea eax, [ecx + 80]
        lea ebx, [edx + 88]
        mov ecx, 88
        mov edx, 20
        mov esi, [gui_theme + THEME_BUTTON_BG]
        call gui_bb_button_3d
        ; Button text
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 84]
        lea ebx, [edx + 90]
        mov esi, gui_str_clk_set
        mov edx, 0x00000000
        call gui_bb_draw_string

        jmp .clk_draw_done

.clk_draw_edit:
        ; --- EDIT MODE ---
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]

        ; Title: "Set Date & Time"
        lea eax, [ecx + 48]
        lea ebx, [edx + 8]
        mov esi, gui_str_clk_edit
        mov edx, [gui_theme + THEME_ACCENT]
        call gui_bb_draw_string

        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]

        ; Date: YYYY-MM-DD with +/- on year, month, day
        ; Year label and value
        lea eax, [ecx + 16]
        lea ebx, [edx + 32]
        mov esi, gui_str_clk_year
        mov edx, 0x00606060
        call gui_bb_draw_string

        movzx eax, byte [.clk_ecent]
        imul eax, 100
        movzx ebx, byte [.clk_eyear]
        add eax, ebx
        mov edi, .clk_num_buf
        call .clk_itoa
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 56]
        lea ebx, [edx + 32]
        mov esi, .clk_num_buf
        mov edx, [gui_theme + THEME_ACCENT]
        call gui_bb_draw_string

        ; +/- buttons for year
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 100]
        lea ebx, [edx + 32]
        mov esi, gui_str_clk_pm
        mov edx, 0x00FFFFFF
        call gui_bb_draw_string

        ; Month
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 136]
        lea ebx, [edx + 32]
        mov esi, gui_str_clk_mon
        mov edx, 0x00606060
        call gui_bb_draw_string

        movzx eax, byte [.clk_emon]
        mov edi, .clk_num_buf
        call .clk_itoa
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 160]
        lea ebx, [edx + 32]
        mov esi, .clk_num_buf
        mov edx, [gui_theme + THEME_ACCENT]
        call gui_bb_draw_string

        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 188]
        lea ebx, [edx + 32]
        mov esi, gui_str_clk_pm
        mov edx, 0x00FFFFFF
        call gui_bb_draw_string

        ; Day
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 220]
        lea ebx, [edx + 32]
        mov esi, gui_str_clk_day
        mov edx, 0x00606060
        call gui_bb_draw_string

        movzx eax, byte [.clk_eday]
        mov edi, .clk_num_buf
        call .clk_itoa
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 236]
        lea ebx, [edx + 32]
        mov esi, .clk_num_buf
        mov edx, [gui_theme + THEME_ACCENT]
        call gui_bb_draw_string

        ; Time: HH : MM : SS with +/- each
        ; Hour
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 16]
        lea ebx, [edx + 64]
        mov esi, gui_str_clk_hr
        mov edx, 0x00606060
        call gui_bb_draw_string

        movzx eax, byte [.clk_ehour]
        mov edi, .clk_num_buf
        call .clk_itoa
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 40]
        lea ebx, [edx + 64]
        mov esi, .clk_num_buf
        mov edx, [gui_theme + THEME_ACCENT]
        call gui_bb_draw_string

        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 68]
        lea ebx, [edx + 64]
        mov esi, gui_str_clk_pm
        mov edx, 0x00FFFFFF
        call gui_bb_draw_string

        ; Minute
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 104]
        lea ebx, [edx + 64]
        mov esi, gui_str_clk_min
        mov edx, 0x00606060
        call gui_bb_draw_string

        movzx eax, byte [.clk_emin]
        mov edi, .clk_num_buf
        call .clk_itoa
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 128]
        lea ebx, [edx + 64]
        mov esi, .clk_num_buf
        mov edx, [gui_theme + THEME_ACCENT]
        call gui_bb_draw_string

        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 156]
        lea ebx, [edx + 64]
        mov esi, gui_str_clk_pm
        mov edx, 0x00FFFFFF
        call gui_bb_draw_string

        ; Second
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 192]
        lea ebx, [edx + 64]
        mov esi, gui_str_clk_sec
        mov edx, 0x00606060
        call gui_bb_draw_string

        movzx eax, byte [.clk_esec]
        mov edi, .clk_num_buf
        call .clk_itoa
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 212]
        lea ebx, [edx + 64]
        mov esi, .clk_num_buf
        mov edx, [gui_theme + THEME_ACCENT]
        call gui_bb_draw_string

        ; Apply / Cancel buttons, classic 3-D
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 40]
        lea ebx, [edx + 96]
        mov ecx, 64
        mov edx, 20
        mov esi, [gui_theme + THEME_BUTTON_BG]
        call gui_bb_button_3d
        ; Apply text
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 48]
        lea ebx, [edx + 98]
        mov esi, gui_str_clk_apply
        mov edx, 0x00000000
        call gui_bb_draw_string

        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 144]
        lea ebx, [edx + 96]
        mov ecx, 72
        mov edx, 20
        mov esi, [gui_theme + THEME_BUTTON_BG]
        call gui_bb_button_3d
        ; Cancel text
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 152]
        lea ebx, [edx + 98]
        mov esi, gui_str_clk_cancel
        mov edx, 0x00000000
        call gui_bb_draw_string

.clk_draw_done:
        call gui_draw_taskbar
        call gui_draw_cursor
        call gui_flip

        ; Input
        call kb_pollchar
        cmp al, 27
        je .clk_close
        cmp al, 0
        jne .clk_loop

        ; Check mouse click
        movzx eax, byte [mouse_buttons]
        test eax, 0x01
        jz .clk_loop

        ; Get relative mouse position within window content
        mov eax, [mouse_x]
        sub eax, [.clk_wx]
        mov [.clk_mx], eax
        mov eax, [mouse_y]
        sub eax, [.clk_wy]
        mov [.clk_my], eax

        cmp byte [.clk_edit], 0
        jne .clk_edit_click

        ; --- DISPLAY MODE CLICKS ---
        ; Check "Set" button: x=80..168, y=88..108
        cmp dword [.clk_mx], 80
        jl .clk_check_close
        cmp dword [.clk_mx], 168
        jg .clk_check_close
        cmp dword [.clk_my], 88
        jl .clk_check_close
        cmp dword [.clk_my], 108
        jg .clk_check_close
        ; Enter edit mode
        mov byte [.clk_edit], 1
        ; Refresh edit fields from RTC
        call rtc_read_time
        mov al, [rtc_hour]
        mov [.clk_ehour], al
        mov al, [rtc_min]
        mov [.clk_emin], al
        mov al, [rtc_sec]
        mov [.clk_esec], al
        mov al, [rtc_day]
        mov [.clk_eday], al
        mov al, [rtc_month]
        mov [.clk_emon], al
        mov al, [rtc_year]
        mov [.clk_eyear], al
        mov al, [rtc_century]
        mov [.clk_ecent], al
        call .clk_debounce
        jmp .clk_loop

.clk_check_close:
        ; Check window close button
        mov eax, [.clk_win]
        imul eax, WIN_STRUCT_SIZE
        lea eax, [gui_win_table + eax]
        mov ebx, [eax + WIN_X]
        add ebx, [eax + WIN_W]
        add ebx, BORDER_W
        sub ebx, CLOSE_BTN_SIZE + 4
        cmp dword [mouse_x], ebx
        jl .clk_loop
        add ebx, CLOSE_BTN_SIZE
        cmp dword [mouse_x], ebx
        jg .clk_loop
        mov ecx, [eax + WIN_Y]
        sub ecx, TITLEBAR_H
        add ecx, 3
        cmp dword [mouse_y], ecx
        jl .clk_loop
        add ecx, CLOSE_BTN_SIZE
        cmp dword [mouse_y], ecx
        jg .clk_loop
        jmp .clk_close

.clk_edit_click:
        ; --- EDIT MODE CLICKS ---
        mov eax, [.clk_my]

        ; Row 1: date fields (y=28..44)
        cmp eax, 28
        jl .clk_edit_check_close
        cmp eax, 44
        jle .clk_edit_date_row

        ; Row 2: time fields (y=60..76)
        cmp eax, 60
        jl .clk_edit_check_close
        cmp eax, 76
        jle .clk_edit_time_row

        ; Row 3: Apply/Cancel (y=96..116)
        cmp eax, 94
        jl .clk_edit_check_close
        cmp eax, 116
        jle .clk_edit_buttons

        jmp .clk_edit_check_close

.clk_edit_date_row:
        ; Year +: x=100..108, -: x=108..116
        mov eax, [.clk_mx]
        cmp eax, 100
        jl .clk_edit_check_mon
        cmp eax, 116
        jg .clk_edit_check_mon
        cmp eax, 108
        jl .clk_year_inc
        jmp .clk_year_dec

.clk_edit_check_mon:
        ; Month +/-: x=188..204
        cmp eax, 188
        jl .clk_edit_check_day
        cmp eax, 204
        jg .clk_edit_check_day
        cmp eax, 196
        jl .clk_mon_inc
        jmp .clk_mon_dec

.clk_edit_check_day:
        ; Day: no explicit +/- drawn, skip
        jmp .clk_edit_check_close

.clk_year_inc:
        movzx eax, byte [.clk_eyear]
        inc eax
        cmp eax, 100
        jl .clk_yr_store
        xor eax, eax
        inc byte [.clk_ecent]
.clk_yr_store:
        mov [.clk_eyear], al
        call .clk_debounce
        jmp .clk_loop
.clk_year_dec:
        movzx eax, byte [.clk_eyear]
        dec eax
        cmp eax, 0
        jge .clk_yrd_store
        mov eax, 99
        dec byte [.clk_ecent]
.clk_yrd_store:
        mov [.clk_eyear], al
        call .clk_debounce
        jmp .clk_loop

.clk_mon_inc:
        movzx eax, byte [.clk_emon]
        inc eax
        cmp eax, 13
        jl .clk_mon_store
        mov eax, 1
.clk_mon_store:
        mov [.clk_emon], al
        call .clk_debounce
        jmp .clk_loop
.clk_mon_dec:
        movzx eax, byte [.clk_emon]
        dec eax
        cmp eax, 1
        jge .clk_mond_store
        mov eax, 12
.clk_mond_store:
        mov [.clk_emon], al
        call .clk_debounce
        jmp .clk_loop

.clk_edit_time_row:
        mov eax, [.clk_mx]
        ; Hour +/-: x=68..84
        cmp eax, 68
        jl .clk_edit_check_min
        cmp eax, 84
        jg .clk_edit_check_min
        cmp eax, 76
        jl .clk_hour_inc
        jmp .clk_hour_dec

.clk_edit_check_min:
        ; Minute +/-: x=156..172
        cmp eax, 156
        jl .clk_edit_check_close
        cmp eax, 172
        jg .clk_edit_check_close
        cmp eax, 164
        jl .clk_min_inc
        jmp .clk_min_dec

.clk_hour_inc:
        movzx eax, byte [.clk_ehour]
        inc eax
        cmp eax, 24
        jl .clk_hr_store
        xor eax, eax
.clk_hr_store:
        mov [.clk_ehour], al
        call .clk_debounce
        jmp .clk_loop
.clk_hour_dec:
        movzx eax, byte [.clk_ehour]
        dec eax
        cmp eax, 0
        jge .clk_hrd_store
        mov eax, 23
.clk_hrd_store:
        mov [.clk_ehour], al
        call .clk_debounce
        jmp .clk_loop

.clk_min_inc:
        movzx eax, byte [.clk_emin]
        inc eax
        cmp eax, 60
        jl .clk_mi_store
        xor eax, eax
.clk_mi_store:
        mov [.clk_emin], al
        call .clk_debounce
        jmp .clk_loop
.clk_min_dec:
        movzx eax, byte [.clk_emin]
        dec eax
        cmp eax, 0
        jge .clk_mid_store
        mov eax, 59
.clk_mid_store:
        mov [.clk_emin], al
        call .clk_debounce
        jmp .clk_loop

.clk_edit_buttons:
        mov eax, [.clk_mx]
        ; Apply: x=40..104
        cmp eax, 40
        jl .clk_edit_check_cancel
        cmp eax, 104
        jle .clk_apply

.clk_edit_check_cancel:
        ; Cancel: x=144..216
        cmp eax, 144
        jl .clk_edit_check_close
        cmp eax, 216
        jle .clk_cancel
        jmp .clk_edit_check_close

.clk_apply:
        ; Write edit fields to RTC
        mov al, [.clk_ehour]
        mov [rtc_hour], al
        mov al, [.clk_emin]
        mov [rtc_min], al
        mov al, [.clk_esec]
        mov [rtc_sec], al
        mov al, [.clk_eday]
        mov [rtc_day], al
        mov al, [.clk_emon]
        mov [rtc_month], al
        mov al, [.clk_eyear]
        mov [rtc_year], al
        mov al, [.clk_ecent]
        mov [rtc_century], al
        call rtc_write_time
        mov byte [.clk_edit], 0
        call .clk_debounce
        jmp .clk_loop

.clk_cancel:
        mov byte [.clk_edit], 0
        call .clk_debounce
        jmp .clk_loop

.clk_edit_check_close:
        ; Check window close button
        mov eax, [.clk_win]
        imul eax, WIN_STRUCT_SIZE
        lea eax, [gui_win_table + eax]
        mov ebx, [eax + WIN_X]
        add ebx, [eax + WIN_W]
        add ebx, BORDER_W
        sub ebx, CLOSE_BTN_SIZE + 4
        cmp dword [mouse_x], ebx
        jl .clk_loop
        add ebx, CLOSE_BTN_SIZE
        cmp dword [mouse_x], ebx
        jg .clk_loop
        mov ecx, [eax + WIN_Y]
        sub ecx, TITLEBAR_H
        add ecx, 3
        cmp dword [mouse_y], ecx
        jl .clk_loop
        add ecx, CLOSE_BTN_SIZE
        cmp dword [mouse_y], ecx
        jg .clk_loop

.clk_close:
.clk_rel:
        movzx eax, byte [mouse_buttons]
        test eax, 0x01
        jnz .clk_rel
        mov eax, [.clk_win]
        call gui_destroy_window_n
.clk_ret:
        popad
        ret

; Debounce: wait for mouse release
.clk_debounce:
.clk_db_wait:
        movzx eax, byte [mouse_buttons]
        test eax, 0x01
        jnz .clk_db_wait
        ret

; Format date into .clk_date_buf as "YYYY-MM-DD"
.clk_format_date:
        pushad
        movzx eax, byte [rtc_century]
        imul eax, 100
        movzx ebx, byte [rtc_year]
        add eax, ebx
        mov edi, .clk_date_buf
        call .clk_itoa
        ; Find end
        mov edi, .clk_date_buf
.cfd_find:
        cmp byte [edi], 0
        je .cfd_dash1
        inc edi
        jmp .cfd_find
.cfd_dash1:
        mov byte [edi], '-'
        inc edi
        movzx eax, byte [rtc_month]
        cmp eax, 10
        jge .cfd_m2
        mov byte [edi], '0'
        inc edi
.cfd_m2:
        call .clk_itoa_at
        mov byte [edi], '-'
        inc edi
        movzx eax, byte [rtc_day]
        cmp eax, 10
        jge .cfd_d2
        mov byte [edi], '0'
        inc edi
.cfd_d2:
        call .clk_itoa_at
        mov byte [edi], 0
        popad
        ret

; Integer to ASCII: EAX -> [EDI], null-terminated
.clk_itoa:
        push eax
        push ebx
        push ecx
        push edx
        xor ecx, ecx
        mov ebx, 10
.clk_it_push:
        xor edx, edx
        div ebx
        push edx
        inc ecx
        test eax, eax
        jnz .clk_it_push
.clk_it_pop:
        pop eax
        add al, '0'
        mov [edi], al
        inc edi
        dec ecx
        jnz .clk_it_pop
        mov byte [edi], 0
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

; Integer to ASCII at EDI (no null term), advances EDI
.clk_itoa_at:
        push eax
        push ebx
        push ecx
        push edx
        xor ecx, ecx
        mov ebx, 10
.clk_ita_push:
        xor edx, edx
        div ebx
        push edx
        inc ecx
        test eax, eax
        jnz .clk_ita_push
.clk_ita_pop:
        pop eax
        add al, '0'
        mov [edi], al
        inc edi
        dec ecx
        jnz .clk_ita_pop
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

.clk_win:       dd 0
.clk_edit:      db 0
.clk_wx:        dd 0
.clk_wy:        dd 0
.clk_mx:        dd 0
.clk_my:        dd 0
.clk_ehour:     db 0
.clk_emin:      db 0
.clk_esec:      db 0
.clk_eday:      db 0
.clk_emon:      db 0
.clk_eyear:     db 0
.clk_ecent:     db 0
.clk_date_buf:  db "0000-00-00", 0, 0
.clk_num_buf:   db "0000", 0, 0, 0, 0

;---------------------------------------
; gui_draw_theme_choice - one 100x36 theme button
;   EAX = x, EBX = y, ECX = swatch colour, EDX = label, ESI = selected
;---------------------------------------
gui_draw_theme_choice:
        pushad
        mov [.tc_x], eax
        mov [.tc_y], ebx
        mov [.tc_color], ecx
        mov [.tc_label], edx
        mov [.tc_sel], esi

        mov eax, [.tc_x]
        mov ebx, [.tc_y]
        mov ecx, 100
        mov edx, 36
        mov esi, [gui_theme + THEME_BUTTON_BG]
        call gui_bb_fill_rect

        mov eax, [.tc_x]
        mov ebx, [.tc_y]
        mov ecx, 100
        mov edx, 36
        cmp dword [.tc_sel], 0
        je .tc_raise
        call gui_bb_sink_rect
        jmp .tc_swatch
.tc_raise:
        call gui_bb_raise_rect
.tc_swatch:
        mov eax, [.tc_x]
        add eax, 6
        mov ebx, [.tc_y]
        add ebx, 7
        mov ecx, 22
        mov edx, 22
        mov esi, [.tc_color]
        call gui_bb_fill_rect
        mov eax, [.tc_x]
        add eax, 6
        mov ebx, [.tc_y]
        add ebx, 7
        mov ecx, 22
        mov edx, 22
        call gui_bb_sink_rect

        mov eax, [.tc_x]
        add eax, 34
        mov ebx, [.tc_y]
        add ebx, 10
        mov esi, [.tc_label]
        mov edx, 0x00000000
        call gui_bb_draw_string
        popad
        ret
.tc_x:     dd 0
.tc_y:     dd 0
.tc_color: dd 0
.tc_label: dd 0
.tc_sel:   dd 0

;---------------------------------------
; gui_applet_settings - Theme picker
;---------------------------------------
gui_applet_settings:
        pushad
        mov eax, 140
        mov ebx, 120
        mov ecx, 360
        mov edx, 240
        mov esi, gui_str_settings
        call gui_create_window
        cmp eax, -1
        je .set_ret
        mov [.set_win], eax

.set_loop:
        call gui_draw_background
        call gui_draw_all_windows

        mov eax, [.set_win]
        imul eax, WIN_STRUCT_SIZE
        lea ebp, [gui_win_table + eax]
        mov ecx, [ebp + WIN_X]
        mov edx, [ebp + WIN_Y]

        ; Title
        lea eax, [ecx + 60]
        lea ebx, [edx + 12]
        mov esi, gui_str_set_title
        mov edx, 0x00000000
        call gui_bb_draw_string

        ; Theme choices: classic 3-D buttons with a desktop-colour swatch.
        ; The button for the active theme is drawn pressed.
        mov esi, 0
        cmp dword [gui_theme + THEME_DESKTOP_BG], 0x00008080
        jne .set_sel_blue
        mov esi, 1
.set_sel_blue:
        mov eax, [ebp + WIN_X]
        add eax, 20
        mov ebx, [ebp + WIN_Y]
        add ebx, 50
        mov ecx, 0x00008080
        mov edx, gui_str_theme_blue
        call gui_draw_theme_choice

        mov esi, 0
        cmp dword [gui_theme + THEME_DESKTOP_BG], 0x00202020
        jne .set_sel_dark
        mov esi, 1
.set_sel_dark:
        mov eax, [ebp + WIN_X]
        add eax, 130
        mov ebx, [ebp + WIN_Y]
        add ebx, 50
        mov ecx, 0x00202020
        mov edx, gui_str_theme_dark
        call gui_draw_theme_choice

        mov esi, 0
        cmp dword [gui_theme + THEME_DESKTOP_BG], 0x00B0C4DE
        jne .set_sel_light
        mov esi, 1
.set_sel_light:
        mov eax, [ebp + WIN_X]
        add eax, 240
        mov ebx, [ebp + WIN_Y]
        add ebx, 50
        mov ecx, 0x00B0C4DE
        mov edx, gui_str_theme_light
        call gui_draw_theme_choice

        mov esi, 0
        cmp dword [gui_theme + THEME_DESKTOP_BG], 0x00201000
        jne .set_sel_amber
        mov esi, 1
.set_sel_amber:
        mov eax, [ebp + WIN_X]
        add eax, 20
        mov ebx, [ebp + WIN_Y]
        add ebx, 96
        mov ecx, 0x00201000
        mov edx, gui_str_theme_amber
        call gui_draw_theme_choice

        ; Wallpaper row: gradient or flat fill, toggled by click or '5'.
        mov eax, [ebp + WIN_X]
        add eax, 20
        mov ebx, [ebp + WIN_Y]
        add ebx, 132
        mov esi, gui_str_set_wall
        mov edx, 0x00000000
        call gui_bb_draw_string

        mov eax, [ebp + WIN_X]
        add eax, 130
        mov ebx, [ebp + WIN_Y]
        add ebx, 126
        mov ecx, 110
        mov edx, 26
        mov esi, [gui_theme + THEME_BUTTON_BG]
        call gui_bb_button_3d

        mov eax, [ebp + WIN_X]
        add eax, 144
        mov ebx, [ebp + WIN_Y]
        add ebx, 131
        mov esi, gui_str_wall_gradient
        cmp dword [gui_wallpaper], 0
        je .set_wall_label
        mov esi, gui_str_wall_solid
.set_wall_label:
        mov edx, 0x00000000
        call gui_bb_draw_string

        ; Current theme preview, in a sunken well.
        mov eax, [ebp + WIN_X]
        add eax, 20
        mov ebx, [ebp + WIN_Y]
        add ebx, 160
        mov ecx, 320
        mov edx, 30
        mov esi, [gui_theme + THEME_DESKTOP_BG]
        call gui_bb_fill_rect
        mov eax, [ebp + WIN_X]
        add eax, 20
        mov ebx, [ebp + WIN_Y]
        add ebx, 160
        mov ecx, 320
        mov edx, 30
        call gui_bb_sink_rect

        mov eax, [ebp + WIN_X]
        add eax, 100
        mov ebx, [ebp + WIN_Y]
        add ebx, 194
        mov esi, gui_str_set_cur
        mov edx, 0x00000000
        call gui_bb_draw_string

        ; Instructions
        mov eax, [ebp + WIN_X]
        add eax, 40
        mov ebx, [ebp + WIN_Y]
        add ebx, 214
        mov esi, gui_str_set_instr
        mov edx, 0x00404040
        call gui_bb_draw_string

        call gui_draw_taskbar
        call gui_draw_cursor
        call gui_flip

        ; Input
        call kb_pollchar
        cmp al, 27
        je .set_close
        cmp al, '1'
        je .set_blue
        cmp al, '2'
        je .set_dark
        cmp al, '3'
        je .set_light
        cmp al, '4'
        je .set_amber
        cmp al, '5'
        je .set_wall_toggle
        cmp al, 0
        jne .set_loop

        ; Check mouse clicks
        movzx eax, byte [mouse_buttons]
        test eax, 0x01
        jz .set_loop

        ; Check close button
        mov eax, [.set_win]
        imul eax, WIN_STRUCT_SIZE
        lea eax, [gui_win_table + eax]
        mov ebx, [eax + WIN_X]
        add ebx, [eax + WIN_W]
        add ebx, BORDER_W
        sub ebx, CLOSE_BTN_SIZE + 4
        cmp dword [mouse_x], ebx
        jl .set_check_btns
        add ebx, CLOSE_BTN_SIZE
        cmp dword [mouse_x], ebx
        jg .set_check_btns
        mov ecx, [eax + WIN_Y]
        sub ecx, TITLEBAR_H
        add ecx, 3
        cmp dword [mouse_y], ecx
        jl .set_check_btns
        add ecx, CLOSE_BTN_SIZE
        cmp dword [mouse_y], ecx
        jg .set_check_btns
        jmp .set_close

.set_check_btns:
        ; Check theme button clicks
        mov eax, [.set_win]
        imul eax, WIN_STRUCT_SIZE
        lea eax, [gui_win_table + eax]
        mov ebx, [eax + WIN_X]
        mov ecx, [eax + WIN_Y]
        ; Blue: x+20..x+120, y+50..y+86
        mov edx, [mouse_x]
        sub edx, ebx
        mov edi, [mouse_y]
        sub edi, ecx

        ; Wallpaper button: x+130..x+240, y+126..y+152
        cmp edi, 126
        jl .set_row01
        cmp edi, 152
        jg .set_row01
        cmp edx, 130
        jl .set_row01
        cmp edx, 240
        jle .set_wall_click
.set_row01:
        ; Row 1 buttons: y+50..y+86
        cmp edi, 50
        jl .set_loop
        cmp edi, 86
        jg .set_row2_check
        cmp edx, 20
        jl .set_loop
        cmp edx, 120
        jle .set_blue_click
        cmp edx, 130
        jl .set_loop
        cmp edx, 230
        jle .set_dark_click
        cmp edx, 240
        jl .set_loop
        cmp edx, 340
        jle .set_light_click
        jmp .set_loop

.set_row2_check:
        ; Row 2 buttons: y+96..y+132
        cmp edi, 96
        jl .set_loop
        cmp edi, 132
        jg .set_loop
        cmp edx, 20
        jl .set_loop
        cmp edx, 120
        jle .set_amber_click
        jmp .set_loop

.set_blue_click:
        ; Wait for release
.set_brel:
        movzx eax, byte [mouse_buttons]
        test eax, 0x01
        jnz .set_brel
.set_blue:
        mov esi, gui_theme_blue
        mov edi, gui_theme
        mov ecx, THEME_SIZE / 4
        rep movsd
        call burrows_config_save
        jmp .set_loop

.set_dark_click:
.set_drel:
        movzx eax, byte [mouse_buttons]
        test eax, 0x01
        jnz .set_drel
.set_dark:
        mov esi, gui_theme_dark
        mov edi, gui_theme
        mov ecx, THEME_SIZE / 4
        rep movsd
        call burrows_config_save
        jmp .set_loop

.set_light_click:
.set_lrel:
        movzx eax, byte [mouse_buttons]
        test eax, 0x01
        jnz .set_lrel
.set_light:
        mov esi, gui_theme_light
        mov edi, gui_theme
        mov ecx, THEME_SIZE / 4
        rep movsd
        call burrows_config_save
        jmp .set_loop

.set_amber_click:
.set_arel:
        movzx eax, byte [mouse_buttons]
        test eax, 0x01
        jnz .set_arel
.set_amber:
        mov esi, gui_theme_amber
        mov edi, gui_theme
        mov ecx, THEME_SIZE / 4
        rep movsd
        call burrows_config_save
        jmp .set_loop

.set_wall_click:
.set_wrel:
        movzx eax, byte [mouse_buttons]
        test eax, 0x01
        jnz .set_wrel
.set_wall_toggle:
        xor dword [gui_wallpaper], 1
        call burrows_config_save
        jmp .set_loop

.set_close:
.set_crel:
        movzx eax, byte [mouse_buttons]
        test eax, 0x01
        jnz .set_crel
        mov eax, [.set_win]
        call gui_destroy_window_n
.set_ret:
        popad
        ret
.set_win: dd 0

;=======================================================================
; DESKTOP SETTINGS - theme and wallpaper persisted to burrows.cfg
;
; Layout: "BCFG", THEME_SIZE bytes of theme colours, then a wallpaper word.
; The wallpaper word is optional so a file written by an older build (theme
; only) still loads instead of silently reverting to the default theme.
;=======================================================================

BURROWS_CFG_SIZE equ 4 + THEME_SIZE + 4

;---------------------------------------
; burrows_config_load - apply saved theme/wallpaper, if they exist
;---------------------------------------
burrows_config_load:
        pushad
        mov esi, burrows_cfg_name
        mov edi, burrows_cfg_buf
        call hbfs_read_file
        jc .bcl_done                      ; no settings file yet
        mov ebx, ecx                      ; size read (ECX is consumed below)
        cmp ebx, 4 + THEME_SIZE
        jb .bcl_done
        cmp dword [burrows_cfg_buf], 'BCFG'
        jne .bcl_done
        cld
        mov esi, burrows_cfg_buf + 4
        mov edi, gui_theme
        mov ecx, THEME_SIZE / 4
        rep movsd
        cmp ebx, BURROWS_CFG_SIZE
        jb .bcl_done
        mov eax, [burrows_cfg_buf + 4 + THEME_SIZE]
        cmp eax, 1
        jbe .bcl_store
        xor eax, eax                      ; unknown value: keep the gradient
.bcl_store:
        mov [gui_wallpaper], eax
.bcl_done:
        popad
        ret

;---------------------------------------
; burrows_config_save - write the current theme back to burrows.cfg
;---------------------------------------
burrows_config_save:
        pushad
        cld
        mov dword [burrows_cfg_buf], 'BCFG'
        mov esi, gui_theme
        mov edi, burrows_cfg_buf + 4
        mov ecx, THEME_SIZE / 4
        rep movsd
        mov eax, [gui_wallpaper]
        mov [burrows_cfg_buf + 4 + THEME_SIZE], eax
        mov esi, burrows_cfg_name
        mov ecx, BURROWS_CFG_SIZE
        mov edi, burrows_cfg_buf
        mov edx, FTYPE_TEXT
        call hbfs_create_file
        popad
        ret

burrows_cfg_name: db "burrows.cfg", 0

;=======================================================================
; FOCUS-AWARE INPUT HELPERS
;=======================================================================

;---------------------------------------
; gui_poll_can_read - may the caller consume keyboard/mouse input?
;   Out: EAX = 1 for the owner of the focused window (or a kernel applet),
;              = 0 for any other task
;---------------------------------------
gui_poll_can_read:
        mov eax, [gui_focused_win]
        cmp eax, -1
        je .pcr_no_focus
        push eax
        call sched_get_current_task
        test eax, eax
        jz .pcr_kernel
        mov ebx, [eax + TCB_PID]
        pop eax
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        cmp ebx, [eax + WIN_OWNER_PID]
        sete al
        movzx eax, al
        ret
.pcr_kernel:
        pop eax
        mov eax, 1
        ret
.pcr_no_focus:
        call sched_get_current_task
        test eax, eax
        jz .pcr_yes
        xor eax, eax
        ret
.pcr_yes:
        mov eax, 1
        ret

;---------------------------------------
; Window-manager shortcut helpers: act on the focused window
;
; Shared by the focused-app key path and the no-focus fallback so the
; keyboard shortcuts behave the same wherever focus happens to be.
;---------------------------------------
gui_apply_tile:
        pushad
        mov eax, [gui_focused_win]
        cmp eax, -1
        je .wat_done
        call gui_win_tile
.wat_done:
        popad
        ret

gui_apply_maximize:
        pushad
        mov eax, [gui_focused_win]
        cmp eax, -1
        je .wam_done
        call gui_win_maximize
.wam_done:
        popad
        ret

gui_apply_restore:
        pushad
        mov eax, [gui_focused_win]
        cmp eax, -1
        je .war_done
        call gui_win_restore
.war_done:
        popad
        ret

gui_apply_close:
        pushad
        mov eax, [gui_focused_win]
        cmp eax, -1
        je .wac_done
        mov [gui_close_req], eax
.wac_done:
        popad
        ret

;---------------------------------------
; gui_poll_global_key - consume and handle a desktop shortcut
;   Out: EAX = 1 if a shortcut was handled, 0 otherwise
;
; Called even when the caller does not own the focused window (for example
; after a workspace switch dropped focus), so the desktop stays reachable.
;---------------------------------------
gui_poll_global_key:
        call kb_peekchar
        test al, al
        jz .pgk_no
        cmp al, 0x84
        je .pgk_launcher
        cmp al, 0x85
        je .pgk_bterm
        cmp al, 0x8E
        je .pgk_alt_tab
        cmp al, 0x8F
        je .pgk_tile_left
        cmp al, 0x90
        je .pgk_tile_right
        cmp al, 0x95
        je .pgk_maximize
        cmp al, 0x96
        je .pgk_restore
        cmp al, 0x97
        je .pgk_close
        cmp al, 0x86
        je .pgk_notify_center
        cmp al, 0x91
        jb .pgk_no
        cmp al, 0x94
        ja .pgk_no
        push eax
        call kb_pollchar
        pop eax
        sub al, 0x91
        movzx eax, al
        call gui_set_workspace
        mov eax, 1
        ret
.pgk_launcher:
        call kb_pollchar
        mov esi, gui_prog_blauncher
        call gui_launch_app
        mov eax, 1
        ret
.pgk_bterm:
        call kb_pollchar
        mov esi, gui_prog_bterm
        call gui_launch_app
        mov eax, 1
        ret
.pgk_alt_tab:
        call kb_pollchar
        call gui_cycle_window
        mov eax, 1
        ret
.pgk_tile_left:
        call kb_pollchar
        xor ebx, ebx
        call gui_apply_tile
        mov eax, 1
        ret
.pgk_tile_right:
        call kb_pollchar
        mov ebx, 1
        call gui_apply_tile
        mov eax, 1
        ret
.pgk_maximize:
        call kb_pollchar
        call gui_apply_maximize
        mov eax, 1
        ret
.pgk_restore:
        call kb_pollchar
        call gui_apply_restore
        mov eax, 1
        ret
.pgk_close:
        call kb_pollchar
        call gui_apply_close
        mov eax, 1
        ret
.pgk_notify_center:
        call kb_pollchar
        call gui_notify_center_toggle
        mov eax, 1
        ret
.pgk_no:
        xor eax, eax
        ret

;---------------------------------------
; gui_poll_wm_click - process WM-level clicks during an app poll
;
; Handles taskbar/menu clicks, focuses a clicked window, and lets the window
; manager handle title-bar drag/min/max. A click inside the focused window's
; content area is left in the latch for the application.
;---------------------------------------
gui_poll_wm_click:
        pushad

        ; A title-bar drag or border resize started by a WM click has to keep
        ; running while the focused application, not the desktop loop, is the
        ; one calling us every frame.
        cmp byte [gui_dragging], 1
        jne .pwc_check_resize
        call gui_handle_drag
.pwc_check_resize:
        cmp byte [gui_resizing], 1
        jne .pwc_check_release
        call gui_handle_resize
.pwc_check_release:
        movzx eax, byte [mouse_buttons]
        test al, 0x01
        jnz .pwc_after_release
        mov byte [gui_dragging], 0
        mov byte [gui_resizing], 0
.pwc_after_release:
        movzx eax, byte [mouse_btn_latch]
        test al, 0x01
        jz .pwc_done

        ; Taskbar?
        cmp dword [mouse_y], TASKBAR_Y + 4
        jl .pwc_window
        cmp dword [mouse_y], TASKBAR_Y + 24
        jg .pwc_window
        cmp dword [mouse_x], GUI_SCREEN_W - 108
        jl .pwc_taskbtn
        movzx eax, byte [gui_active_workspace]
        inc eax
        and eax, 3
        call gui_set_workspace
        mov byte [mouse_btn_latch], 0
        jmp .pwc_done
.pwc_taskbtn:
        cmp dword [mouse_x], 76
        jl .pwc_menu
        call gui_taskbar_btn_click
        mov byte [mouse_btn_latch], 0
        jmp .pwc_done

.pwc_menu:
        cmp dword [mouse_x], 4
        jl .pwc_window
        cmp dword [mouse_x], 68
        jg .pwc_window
        mov byte [mouse_btn_latch], 0
        mov esi, gui_prog_blauncher
        call gui_launch_app
        jmp .pwc_done

.pwc_window:
        mov eax, [mouse_x]
        mov ebx, [mouse_y]
        call gui_find_window_at
        cmp eax, -1
        je .pwc_done
        mov [.pwc_idx], eax
        mov dword [.pwc_new_focus], 0
        cmp eax, [gui_focused_win]
        je .pwc_have_window
        mov dword [.pwc_new_focus], 1
        call gui_focus_window
.pwc_have_window:
        mov eax, [.pwc_idx]
        mov ebx, eax
        imul ebx, WIN_STRUCT_SIZE
        add ebx, gui_win_table
        mov [.pwc_win], ebx

        ; The resize borders are window-manager chrome even though they sit
        ; beside the content area. Use the same 4-pixel zones as
        ; gui_handle_window_click so both paths agree.
        mov ecx, [ebx + WIN_X]
        add ecx, 2
        cmp dword [mouse_x], ecx
        jle .pwc_chrome
        mov ecx, [ebx + WIN_X]
        add ecx, [ebx + WIN_W]
        sub ecx, 4
        cmp dword [mouse_x], ecx
        jge .pwc_chrome
        mov ecx, [ebx + WIN_Y]
        add ecx, [ebx + WIN_H]
        sub ecx, 4
        cmp dword [mouse_y], ecx
        jge .pwc_chrome

        ; Everything above the content area is the title bar.
        mov ecx, [ebx + WIN_Y]
        cmp dword [mouse_y], ecx
        jl .pwc_chrome

        ; A content click activates the window and is consumed; a click in an
        ; already-focused window belongs to the app.
        cmp dword [.pwc_new_focus], 0
        jne .pwc_consume
        jmp .pwc_done

.pwc_consume:
        mov byte [mouse_btn_latch], 0
        jmp .pwc_done

.pwc_chrome:
        ; Let the app handle its own close button (it receives EVT_CLOSE).
        mov ebx, [.pwc_win]
        mov ecx, [ebx + WIN_X]
        add ecx, [ebx + WIN_W]
        add ecx, BORDER_W
        sub ecx, CLOSE_BTN_SIZE + 4
        cmp dword [mouse_x], ecx
        jl .pwc_wmchrome
        add ecx, CLOSE_BTN_SIZE
        cmp dword [mouse_x], ecx
        jg .pwc_wmchrome
        mov ecx, [ebx + WIN_Y]
        sub ecx, TITLEBAR_H
        add ecx, 3
        cmp dword [mouse_y], ecx
        jl .pwc_wmchrome
        add ecx, CLOSE_BTN_SIZE
        cmp dword [mouse_y], ecx
        jg .pwc_wmchrome
        cmp dword [.pwc_new_focus], 0
        jne .pwc_consume
        jmp .pwc_done
.pwc_wmchrome:
        call gui_handle_window_click
        mov byte [mouse_btn_latch], 0
.pwc_done:
        popad
        ret
.pwc_idx:        dd 0
.pwc_new_focus:  dd 0
.pwc_win:        dd 0

;=======================================================================
; SYS_GUI SYSCALL HANDLER
;=======================================================================

sys_gui:
        cmp ebx, GUI_CREATE_WINDOW
        je .sg_create
        cmp ebx, GUI_DESTROY_WINDOW
        je .sg_destroy
        cmp ebx, GUI_FILL_RECT
        je .sg_fill
        cmp ebx, GUI_DRAW_TEXT
        je .sg_text
        cmp ebx, GUI_POLL_EVENT
        je .sg_poll
        cmp ebx, GUI_GET_THEME
        je .sg_get_theme
        cmp ebx, GUI_SET_THEME
        je .sg_set_theme
        cmp ebx, GUI_DRAW_PIXEL
        je .sg_pixel
        cmp ebx, GUI_COMPOSE
        je .sg_compose
        cmp ebx, GUI_FLIP
        je .sg_flip
        cmp ebx, GUI_DRAW_BUTTON
        je .sg_draw_button
        cmp ebx, GUI_DRAW_CHECKBOX
        je .sg_draw_checkbox
        cmp ebx, GUI_DRAW_PROGRESS
        je .sg_draw_progress
        cmp ebx, GUI_DRAW_TEXTBOX
        je .sg_draw_textbox
        cmp ebx, GUI_DRAW_LISTBOX
        je .sg_draw_listbox
        cmp ebx, GUI_DRAW_LABEL
        je .sg_draw_label
        cmp ebx, GUI_DRAW_RECT
        je .sg_draw_rect
        cmp ebx, GUI_LAUNCH
        je .sg_launch
        cmp ebx, GUI_EXIT_DESKTOP
        je .sg_exit_desktop
        cmp ebx, GUI_SET_TITLE
        je .sg_set_title
        mov eax, -1
        iretd

.sg_exit_desktop:
        mov byte [gui_exit_flag], 1
        xor eax, eax
        iretd

.sg_set_title:
        ; ECX = win_id, ESI = new title. Only the owner may retitle a window.
        pushad
        mov eax, ecx
        call gui_caller_owns_window
        test eax, eax
        jz .sg_st_done
        mov eax, ecx
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        lea edi, [eax + WIN_TITLE]
        mov ecx, 47
        call gui_copy_trunc
.sg_st_done:
        popad
        xor eax, eax
        iretd

.sg_launch:
        ; ESI = program name. Create a READY GUI task and return its id so a
        ; launcher can stay alive and open more than one app.
        pushad
        call gui_spawn_program
        mov [esp + 28], eax
        popad
        iretd

.sg_create:
        ; ECX = x|y (hi16=x, lo16=y), EDX = w|h, ESI = title
        push esi
        mov eax, ecx
        shr eax, 16            ; x
        movzx ebx, cx          ; y
        mov ecx, edx
        shr ecx, 16            ; w
        movzx edx, dx          ; h
        call gui_create_window
        pop esi
        iretd

.sg_destroy:
        ; ECX = win_id
        mov eax, ecx
        call gui_caller_owns_window
        test eax, eax
        jz .sg_d_fail
        mov eax, ecx
        call gui_destroy_window_n
        xor eax, eax
        iretd
.sg_d_fail:
        mov eax, -1
        iretd

.sg_fill:
        ; ECX = win_id, EDX = x|y, ESI = w|h, EDI = color
        pushad
        mov eax, ecx
        call gui_target_window
        test eax, eax
        js .sg_f_done
        ; Draw into the window's own buffer, in content-relative coordinates.
        mov eax, edx
        shr eax, 16            ; x
        mov ebx, edx
        and ebx, 0xFFFF        ; y
        mov ecx, esi
        shr ecx, 16            ; w
        mov edx, esi
        and edx, 0xFFFF        ; h
        mov esi, edi           ; color
        call gui_bb_fill_rect
.sg_f_done:
        popad
        xor eax, eax
        iretd

.sg_text:
        ; ECX = win_id, EDX = x|y, ESI = text, EDI = color
        pushad
        mov eax, ecx
        call gui_target_window
        test eax, eax
        js .sg_t_done
        mov eax, edx
        shr eax, 16            ; x
        mov ebx, edx
        and ebx, 0xFFFF        ; y
        mov edx, edi           ; color
        call gui_bb_draw_string
.sg_t_done:
        popad
        xor eax, eax
        iretd

.sg_poll:
        ; A window-manager close request (Alt+F4) is delivered as EVT_CLOSE to
        ; the owning task, even if another window has focus.
        mov eax, [gui_close_req]
        cmp eax, -1
        je .sg_p_no_close
        push eax
        call sched_get_current_task
        test eax, eax
        jz .sg_p_no_close_pop
        mov ebx, [eax + TCB_PID]
        pop eax
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        cmp ebx, [eax + WIN_OWNER_PID]
        jne .sg_p_no_close
        mov dword [gui_close_req], -1
        mov eax, EVT_CLOSE
        iretd
.sg_p_no_close_pop:
        pop eax
.sg_p_no_close:

        ; WM-level clicks (taskbar, focus changes, title-bar drag) are handled
        ; for every caller. Then only the owner of the focused window may
        ; consume the shared keyboard/mouse queues.
        call gui_poll_wm_click
        call gui_poll_can_read
        test eax, eax
        jnz .sg_p_read
        ; Unfocused callers still handle global desktop shortcuts.
        call gui_poll_global_key
        xor eax, eax
        iretd
.sg_p_read:
        ; Poll for events, return in EAX/EBX/ECX
        call kb_pollchar
        cmp al, 0
        jne .sg_p_key
        ; Check mouse button latch (catches brief clicks between polls)
        movzx eax, byte [mouse_btn_latch]
        test al, al
        jnz .sg_p_mouse_btn
        ; Check mouse movement
        mov eax, [mouse_x]
        cmp eax, [gui_prev_mouse_x]
        jne .sg_p_mouse_mv
        mov eax, [mouse_y]
        cmp eax, [gui_prev_mouse_y]
        jne .sg_p_mouse_mv
.sg_p_none:
        xor eax, eax
        iretd
.sg_p_key:
        movzx ebx, al
        ; Global desktop shortcuts work regardless of the focused app.
        cmp bl, 0x84           ; F1 — launcher
        je .sg_p_launcher
        cmp bl, 0x85           ; F2 — terminal
        je .sg_p_bterm
        cmp bl, 0x8E           ; Alt+Tab — cycle windows
        je .sg_p_alt_tab
        cmp bl, 0x8F           ; Alt+Left — tile left
        je .sg_p_tile_left
        cmp bl, 0x90           ; Alt+Right — tile right
        je .sg_p_tile_right
        cmp bl, 0x95           ; Alt+Up — maximize
        je .sg_p_maximize
        cmp bl, 0x96           ; Alt+Down — restore
        je .sg_p_restore
        cmp bl, 0x97           ; Alt+F4 — close focused window
        je .sg_p_close_win
        cmp bl, 0x86           ; F3 — notification centre
        je .sg_p_notify_center
        cmp bl, 0x91           ; Ctrl+Alt+1..4 — workspace
        je .sg_p_ws0
        cmp bl, 0x92
        je .sg_p_ws1
        cmp bl, 0x93
        je .sg_p_ws2
        cmp bl, 0x94
        je .sg_p_ws3
        mov eax, EVT_KEY_PRESS
        iretd
.sg_p_tile_left:
        xor ebx, ebx
        call gui_apply_tile
        xor eax, eax
        iretd
.sg_p_tile_right:
        mov ebx, 1
        call gui_apply_tile
        xor eax, eax
        iretd
.sg_p_maximize:
        call gui_apply_maximize
        xor eax, eax
        iretd
.sg_p_restore:
        call gui_apply_restore
        xor eax, eax
        iretd
.sg_p_close_win:
        call gui_apply_close
        xor eax, eax
        iretd
.sg_p_notify_center:
        call gui_notify_center_toggle
        xor eax, eax
        iretd
.sg_p_launcher:
        mov esi, gui_prog_blauncher
        call gui_launch_app
        xor eax, eax
        iretd
.sg_p_bterm:
        mov esi, gui_prog_bterm
        call gui_launch_app
        xor eax, eax
        iretd
.sg_p_alt_tab:
        call gui_cycle_window
        xor eax, eax
        iretd
.sg_p_ws0:
        xor eax, eax
        call gui_set_workspace
        xor eax, eax
        iretd
.sg_p_ws1:
        mov eax, 1
        call gui_set_workspace
        xor eax, eax
        iretd
.sg_p_ws2:
        mov eax, 2
        call gui_set_workspace
        xor eax, eax
        iretd
.sg_p_ws3:
        mov eax, 3
        call gui_set_workspace
        xor eax, eax
        iretd
.sg_p_mouse_btn:
        ; Clear latch and update prev state
        mov byte [mouse_btn_latch], 0
        movzx eax, byte [mouse_buttons]
        mov [gui_prev_mouse_btn], al
        mov eax, [mouse_x]
        mov [gui_prev_mouse_x], eax
        mov eax, [mouse_y]
        mov [gui_prev_mouse_y], eax
        ; Check for close button on focused window
        mov eax, [gui_focused_win]
        cmp eax, -1
        je .sg_p_click
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        mov ebx, [eax + WIN_X]
        add ebx, [eax + WIN_W]
        add ebx, BORDER_W
        sub ebx, CLOSE_BTN_SIZE + 4
        cmp dword [mouse_x], ebx
        jl .sg_p_click
        add ebx, CLOSE_BTN_SIZE
        cmp dword [mouse_x], ebx
        jg .sg_p_click
        mov ecx, [eax + WIN_Y]
        sub ecx, TITLEBAR_H
        add ecx, 3
        cmp dword [mouse_y], ecx
        jl .sg_p_click
        add ecx, CLOSE_BTN_SIZE
        cmp dword [mouse_y], ecx
        jg .sg_p_click
        mov eax, EVT_CLOSE
        iretd
.sg_p_click:
        ; Return click with coords relative to focused window
        mov eax, [gui_focused_win]
        cmp eax, -1
        je .sg_p_abs
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        mov ebx, [mouse_x]
        sub ebx, [eax + WIN_X]
        mov ecx, [mouse_y]
        sub ecx, [eax + WIN_Y]
        mov eax, EVT_MOUSE_CLICK
        iretd
.sg_p_abs:
        mov ebx, [mouse_x]
        mov ecx, [mouse_y]
        mov eax, EVT_MOUSE_CLICK
        iretd
.sg_p_mouse_mv:
        mov eax, [mouse_x]
        mov [gui_prev_mouse_x], eax
        mov eax, [mouse_y]
        mov [gui_prev_mouse_y], eax
        ; Return coords relative to focused window if possible
        mov eax, [gui_focused_win]
        cmp eax, -1
        je .sg_p_mv_abs
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        mov ebx, [mouse_x]
        sub ebx, [eax + WIN_X]
        mov ecx, [mouse_y]
        sub ecx, [eax + WIN_Y]
        mov eax, EVT_MOUSE_MOVE
        iretd
.sg_p_mv_abs:
        mov ebx, [mouse_x]
        mov ecx, [mouse_y]
        mov eax, EVT_MOUSE_MOVE
        iretd

.sg_get_theme:
        ; ECX = dest buffer
        pushad
        cld
        mov edi, ecx
        mov esi, gui_theme
        mov ecx, THEME_SIZE / 4
        rep movsd
        popad
        xor eax, eax
        iretd

.sg_set_theme:
        ; ECX = source buffer
        pushad
        cld
        mov esi, ecx
        mov edi, gui_theme
        mov ecx, THEME_SIZE / 4
        rep movsd
        call burrows_config_save
        popad
        xor eax, eax
        iretd

.sg_pixel:
        ; ECX = win_id, EDX = x|y, ESI = color
        pushad
        mov eax, ecx
        call gui_target_window
        test eax, eax
        js .sg_px_done
        mov eax, edx
        shr eax, 16            ; x
        mov ebx, edx
        and ebx, 0xFFFF        ; y
        mov ecx, esi           ; color
        call gui_bb_pixel
.sg_px_done:
        popad
        xor eax, eax
        iretd

.sg_compose:
        pushad
        call gui_target_screen
        call gui_draw_background
        call gui_draw_all_windows
        call gui_draw_taskbar
        popad
        xor eax, eax
        iretd

.sg_flip:
        ; Window content now lives in per-window buffers, so compose the
        ; desktop before presenting it. gui_compose also draws the cursor and
        ; flips the back buffer.
        pushad
        mov dword [esp + 28], 0     ; syscall returns 0 when resumed
        call gui_compose
        ; Give other GUI tasks a slice. Busy GUI loops spend most of their time
        ; in ring 0 (compose/flip), where the timer never preempts.
        call sched_try_yield_frame
        popad
        xor eax, eax
        iretd

;---------------------------------------
; Widget drawing sub-handlers
; All widget calls use: ECX=win_id, ESI=param_block_ptr
; Parameter block layout varies by widget type.
;---------------------------------------

; Helper: resolve window screen coords from win_id in ECX
; Returns: EAX=win_screen_x, EBX=win_screen_y, CF set if invalid
.wgt_resolve_win:
        cmp ecx, MAX_WINDOWS
        jge .wrw_fail
        push ecx
        mov eax, ecx
        call gui_target_window
        pop ecx
        test eax, eax
        js .wrw_fail
        ; Widgets draw in content-relative coordinates now.
        xor eax, eax
        xor ebx, ebx
        clc
        ret
.wrw_fail:
        stc
        ret

; ---- GUI_DRAW_BUTTON (20) ----
; Param block: [0]=x(w), [2]=y(w), [4]=w(w), [6]=h(w), [8]=text_ptr(d), [12]=pressed(b)
.sg_draw_button:
        pushad
        call .wgt_resolve_win
        jc .sg_wgt_done
        mov [.wgt_ox], eax
        mov [.wgt_oy], ebx

        movzx eax, word [esi]        ; x
        add eax, [.wgt_ox]
        movzx ebx, word [esi + 2]    ; y
        add ebx, [.wgt_oy]
        movzx ecx, word [esi + 4]    ; w
        movzx edx, word [esi + 6]    ; h
        mov edi, 0x00D4D0C8          ; face color

        cmp byte [esi + 12], 0
        jne .sg_btn_pressed

        ; Raised button
        mov esi, edi
        call gui_bb_button_3d
        jmp .sg_btn_text

.sg_btn_pressed:
        ; Sunken button
        mov esi, 0x00B0B0B0
        call gui_bb_fill_rect
        ; Dark top/left
        push edx
        mov edx, 0x00404040
        call gui_bb_hline
        pop edx
        push ecx
        mov ecx, edx
        mov edx, 0x00404040
        call gui_bb_vline
        pop ecx

.sg_btn_text:
        ; Center text
        push ebx
        mov esi, [esp + 4 + 8]       ; original ESI from pushad
        mov esi, [esi + 8]           ; text_ptr
        ; Measure text length
        push eax
        push ecx
        xor ecx, ecx
.sg_btn_tlen:
        cmp byte [esi + ecx], 0
        je .sg_btn_tlen_done
        inc ecx
        jmp .sg_btn_tlen
.sg_btn_tlen_done:
        ; ecx = text length
        shl ecx, 3               ; *8 = pixel width
        pop eax                   ; restore eax (was pushed as placeholder)
        ; Actually we need to center: x + (w - text_px_w) / 2
        ; Get w again
        push eax
        mov eax, [esp + 8]       ; This is getting complex; simplify
        pop eax
        pop ecx

        ; Simple approach: offset x by 4, y by (h-16)/2
        add eax, 4
        pop ebx
        push edx
        sub edx, FONT_H
        shr edx, 1
        add ebx, edx
        pop edx
        mov esi, [esp + 8]           ; original ESI
        mov esi, [esi + 8]           ; text_ptr
        mov edx, 0x00000000
        call gui_bb_draw_string
        jmp .sg_wgt_done

; ---- GUI_DRAW_CHECKBOX (21) ----
; Param block: [0]=x(w), [2]=y(w), [4]=checked(b), [6]=text_ptr(d)
.sg_draw_checkbox:
        pushad
        call .wgt_resolve_win
        jc .sg_wgt_done
        mov [.wgt_ox], eax
        mov [.wgt_oy], ebx

        movzx eax, word [esi]
        add eax, [.wgt_ox]
        movzx ebx, word [esi + 2]
        add ebx, [.wgt_oy]

        ; Draw checkbox box (14x14)
        push esi
        push eax
        push ebx
        mov ecx, 14
        mov edx, 14
        mov esi, 0x00FFFFFF
        call gui_bb_fill_rect
        ; Border
        mov esi, 0x00808080
        call gui_bb_rect
        pop ebx
        pop eax
        pop esi

        ; Draw check mark if checked
        cmp byte [esi + 4], 0
        je .sg_chk_text
        push esi
        push eax
        push ebx
        add eax, 3
        add ebx, 3
        mov ecx, 8
        mov edx, 8
        mov esi, 0x00008000
        call gui_bb_fill_rect
        pop ebx
        pop eax
        pop esi

.sg_chk_text:
        ; Draw label text
        add eax, 18              ; offset past checkbox
        inc ebx
        push esi
        mov esi, [esi + 6]       ; text_ptr (at offset 6, but it's a dword)
        ; Actually the text ptr is at offset 6 as a 2-byte aligned dword
        ; Let me fix: offset 8 for dword alignment
        pop esi
        push esi
        mov esi, [esi + 8]       ; text_ptr at offset 8
        mov edx, 0x00000000
        call gui_bb_draw_string
        pop esi
        jmp .sg_wgt_done

; ---- GUI_DRAW_PROGRESS (22) ----
; Param block: [0]=x(w), [2]=y(w), [4]=w(w), [6]=h(w), [8]=value(b 0-100)
.sg_draw_progress:
        pushad
        call .wgt_resolve_win
        jc .sg_wgt_done
        mov [.wgt_ox], eax
        mov [.wgt_oy], ebx

        movzx eax, word [esi]
        add eax, [.wgt_ox]
        movzx ebx, word [esi + 2]
        add ebx, [.wgt_oy]
        movzx ecx, word [esi + 4]    ; w
        movzx edx, word [esi + 6]    ; h

        ; Draw background (grey)
        push esi
        push eax
        push ebx
        push ecx
        push edx
        mov esi, 0x00C0C0C0
        call gui_bb_fill_rect
        ; Border
        mov esi, 0x00808080
        call gui_bb_rect
        pop edx
        pop ecx
        pop ebx
        pop eax
        pop esi

        ; Draw fill
        movzx edi, byte [esi + 8]     ; value 0-100
        cmp edi, 0
        je .sg_wgt_done
        cmp edi, 100
        jle .sg_prog_clamp
        mov edi, 100
.sg_prog_clamp:
        ; fill_w = (w * value) / 100
        push eax
        push edx
        push esi
        movzx eax, word [esi + 4]    ; w
        sub eax, 2                   ; inner width
        imul eax, edi
        xor edx, edx
        push ecx
        mov ecx, 100
        div ecx
        pop ecx
        mov ecx, eax                 ; fill_w
        pop esi
        pop edx
        pop eax

        push eax
        push ebx
        inc eax                       ; inside border
        inc ebx
        sub edx, 2                   ; inner height
        push esi
        mov esi, 0x000066CC          ; blue fill
        call gui_bb_fill_rect
        pop esi
        pop ebx
        pop eax
        jmp .sg_wgt_done

; ---- GUI_DRAW_TEXTBOX (23) ----
; Param block: [0]=x(w), [2]=y(w), [4]=w(w), [6]=h(w), [8]=text_ptr(d), [12]=focused(b)
.sg_draw_textbox:
        pushad
        call .wgt_resolve_win
        jc .sg_wgt_done
        mov [.wgt_ox], eax
        mov [.wgt_oy], ebx

        movzx eax, word [esi]
        add eax, [.wgt_ox]
        movzx ebx, word [esi + 2]
        add ebx, [.wgt_oy]
        movzx ecx, word [esi + 4]
        movzx edx, word [esi + 6]

        ; White background
        push esi
        push eax
        push ebx
        push ecx
        push edx
        mov esi, 0x00FFFFFF
        call gui_bb_fill_rect

        ; Border — blue if focused, grey if not
        mov esi, [esp + 20]           ; original ESI
        cmp byte [esi + 12], 0
        je .sg_tb_grey
        mov esi, 0x000066CC
        jmp .sg_tb_bdr
.sg_tb_grey:
        mov esi, 0x00808080
.sg_tb_bdr:
        call gui_bb_rect
        pop edx
        pop ecx
        pop ebx
        pop eax
        pop esi

        ; Draw text (clipped to box)
        push eax
        push ebx
        add eax, 4
        add ebx, 2
        push esi
        mov esi, [esi + 8]           ; text_ptr
        mov edx, 0x00000000
        call gui_bb_draw_string
        pop esi
        pop ebx
        pop eax
        jmp .sg_wgt_done

; ---- GUI_DRAW_LISTBOX (24) ----
; Param block: [0]=x(w), [2]=y(w), [4]=w(w), [6]=h(w),
;              [8]=items_ptr(d), [12]=item_count(w), [14]=selected(w), [16]=scroll(w), [18]=item_len(w)
.sg_draw_listbox:
        pushad
        call .wgt_resolve_win
        jc .sg_wgt_done
        mov [.wgt_ox], eax
        mov [.wgt_oy], ebx

        movzx eax, word [esi]
        add eax, [.wgt_ox]
        movzx ebx, word [esi + 2]
        add ebx, [.wgt_oy]
        movzx ecx, word [esi + 4]
        movzx edx, word [esi + 6]

        ; White background
        push esi
        push eax
        push ebx
        push ecx
        push edx
        mov esi, 0x00FFFFFF
        call gui_bb_fill_rect
        mov esi, 0x00808080
        call gui_bb_rect
        pop edx
        pop ecx
        pop ebx
        pop eax
        pop esi

        ; Draw items
        mov [.wgt_lbx], eax
        mov [.wgt_lby], ebx
        mov [.wgt_lbw], ecx
        mov [.wgt_lbh], edx

        movzx ecx, word [esi + 16]   ; scroll offset
        movzx edx, word [esi + 12]   ; item_count
        movzx edi, word [esi + 18]   ; item_len (bytes per item string)
        mov ebp, [esi + 8]           ; items_ptr

        ; Calculate visible items: h / 18
        push eax
        mov eax, [.wgt_lbh]
        sub eax, 2
        xor edx, edx
        push ecx
        push ebx
        mov ebx, 18
        div ebx
        pop ebx
        pop ecx
        mov [.wgt_lb_vis], eax
        pop eax

        xor edx, edx             ; visible row counter
.sg_lb_loop:
        cmp edx, [.wgt_lb_vis]
        jge .sg_wgt_done
        movzx eax, word [esi + 12]
        cmp ecx, eax
        jge .sg_wgt_done

        ; Check if this item is selected
        push esi
        movzx eax, word [esi + 14]   ; selected
        cmp eax, ecx
        jne .sg_lb_no_hl

        ; Draw highlight bar
        push ecx
        push edx
        mov eax, [.wgt_lbx]
        inc eax
        mov ebx, edx
        imul ebx, 18
        add ebx, [.wgt_lby]
        inc ebx
        mov ecx, [.wgt_lbw]
        sub ecx, 2
        mov edx, 18
        mov esi, 0x000A246A
        call gui_bb_fill_rect
        pop edx
        pop ecx

        ; Draw text in white
        push ecx
        push edx
        mov eax, [.wgt_lbx]
        add eax, 4
        mov ebx, edx
        imul ebx, 18
        add ebx, [.wgt_lby]
        add ebx, 1
        ; Get item string: items_ptr + index * item_len
        mov esi, ecx
        imul esi, edi
        add esi, ebp
        mov edx, 0x00FFFFFF
        call gui_bb_draw_string
        pop edx
        pop ecx
        pop esi
        jmp .sg_lb_next

.sg_lb_no_hl:
        ; Draw text in black
        push ecx
        push edx
        mov eax, [.wgt_lbx]
        add eax, 4
        mov ebx, edx
        imul ebx, 18
        add ebx, [.wgt_lby]
        add ebx, 1
        mov esi, ecx
        imul esi, edi
        add esi, ebp
        mov edx, 0x00000000
        call gui_bb_draw_string
        pop edx
        pop ecx
        pop esi

.sg_lb_next:
        inc ecx
        inc edx
        jmp .sg_lb_loop

; ---- GUI_DRAW_LABEL (25) ----
; Param block: [0]=x(w), [2]=y(w), [4]=text_ptr(d), [8]=color(d)
.sg_draw_label:
        pushad
        call .wgt_resolve_win
        jc .sg_wgt_done
        movzx eax, word [esi]
        add eax, [.wgt_ox]
        movzx ebx, word [esi + 2]
        add ebx, [.wgt_oy]
        mov edx, [esi + 8]
        mov esi, [esi + 4]
        call gui_bb_draw_string
        jmp .sg_wgt_done

; ---- GUI_DRAW_RECT (26) ----
; Param block: [0]=x(w), [2]=y(w), [4]=w(w), [6]=h(w), [8]=color(d), [12]=filled(b)
.sg_draw_rect:
        pushad
        call .wgt_resolve_win
        jc .sg_wgt_done
        movzx eax, word [esi]
        add eax, [.wgt_ox]
        movzx ebx, word [esi + 2]
        add ebx, [.wgt_oy]
        movzx ecx, word [esi + 4]
        movzx edx, word [esi + 6]
        cmp byte [esi + 12], 0
        je .sg_rect_outline
        mov esi, [esi + 8]
        call gui_bb_fill_rect
        jmp .sg_wgt_done
.sg_rect_outline:
        mov esi, [esi + 8]
        call gui_bb_rect
        jmp .sg_wgt_done

.sg_wgt_done:
        popad
        xor eax, eax
        iretd

.wgt_ox:        dd 0
.wgt_oy:        dd 0
.wgt_lbx:       dd 0
.wgt_lby:       dd 0
.wgt_lbw:       dd 0
.wgt_lbh:       dd 0
.wgt_lb_vis:    dd 0

;=======================================================================
; DATA: 8x16 BITMAP FONT (ASCII 32-126)
;=======================================================================

gui_font_8x16:
; Space (32)
db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
; ! (33)
db 0x00,0x00,0x18,0x3C,0x3C,0x3C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00
; " (34)
db 0x00,0x66,0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
; # (35)
db 0x00,0x00,0x00,0x6C,0x6C,0xFE,0x6C,0x6C,0xFE,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00
; $ (36)
db 0x18,0x18,0x7C,0xC6,0xC2,0xC0,0x7C,0x06,0x06,0x86,0xC6,0x7C,0x18,0x18,0x00,0x00
; % (37)
db 0x00,0x00,0x00,0x00,0xC2,0xC6,0x0C,0x18,0x30,0x66,0xC6,0x00,0x00,0x00,0x00,0x00
; & (38)
db 0x00,0x00,0x38,0x6C,0x6C,0x38,0x76,0xDC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00
; ' (39)
db 0x00,0x30,0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
; ( (40)
db 0x00,0x00,0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00,0x00
; ) (41)
db 0x00,0x00,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00,0x00
; * (42)
db 0x00,0x00,0x00,0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00
; + (43)
db 0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00
; , (44)
db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x30,0x00,0x00,0x00
; - (45)
db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
; . (46)
db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00
; / (47)
db 0x00,0x00,0x00,0x00,0x02,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00,0x00,0x00,0x00
; 0 (48)
db 0x00,0x00,0x7C,0xC6,0xC6,0xCE,0xDE,0xF6,0xE6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00
; 1 (49)
db 0x00,0x00,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00
; 2 (50)
db 0x00,0x00,0x7C,0xC6,0x06,0x0C,0x18,0x30,0x60,0xC0,0xC6,0xFE,0x00,0x00,0x00,0x00
; 3 (51)
db 0x00,0x00,0x7C,0xC6,0x06,0x06,0x3C,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00
; 4 (52)
db 0x00,0x00,0x0C,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x0C,0x1E,0x00,0x00,0x00,0x00
; 5 (53)
db 0x00,0x00,0xFE,0xC0,0xC0,0xC0,0xFC,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00
; 6 (54)
db 0x00,0x00,0x38,0x60,0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00
; 7 (55)
db 0x00,0x00,0xFE,0xC6,0x06,0x06,0x0C,0x18,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00
; 8 (56)
db 0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00
; 9 (57)
db 0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7E,0x06,0x06,0x06,0x0C,0x78,0x00,0x00,0x00,0x00
; : (58)
db 0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00
; ; (59)
db 0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00
; < (60)
db 0x00,0x00,0x00,0x06,0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x06,0x00,0x00,0x00,0x00
; = (61)
db 0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00
; > (62)
db 0x00,0x00,0x00,0x60,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x60,0x00,0x00,0x00,0x00
; ? (63)
db 0x00,0x00,0x7C,0xC6,0xC6,0x0C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00
; @ (64)
db 0x00,0x00,0x00,0x7C,0xC6,0xC6,0xDE,0xDE,0xDE,0xDC,0xC0,0x7C,0x00,0x00,0x00,0x00
; A (65)
db 0x00,0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00
; B (66)
db 0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0xFC,0x00,0x00,0x00,0x00
; C (67)
db 0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xC0,0xC0,0xC2,0x66,0x3C,0x00,0x00,0x00,0x00
; D (68)
db 0x00,0x00,0xF8,0x6C,0x66,0x66,0x66,0x66,0x66,0x66,0x6C,0xF8,0x00,0x00,0x00,0x00
; E (69)
db 0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00
; F (70)
db 0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00
; G (71)
db 0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xDE,0xC6,0xC6,0x66,0x3A,0x00,0x00,0x00,0x00
; H (72)
db 0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00
; I (73)
db 0x00,0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00
; J (74)
db 0x00,0x00,0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00
; K (75)
db 0x00,0x00,0xE6,0x66,0x66,0x6C,0x78,0x78,0x6C,0x66,0x66,0xE6,0x00,0x00,0x00,0x00
; L (76)
db 0x00,0x00,0xF0,0x60,0x60,0x60,0x60,0x60,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00
; M (77)
db 0x00,0x00,0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00
; N (78)
db 0x00,0x00,0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00
; O (79)
db 0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00
; P (80)
db 0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00
; Q (81)
db 0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x0C,0x0E,0x00,0x00
; R (82)
db 0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x6C,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00
; S (83)
db 0x00,0x00,0x7C,0xC6,0xC6,0x60,0x38,0x0C,0x06,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00
; T (84)
db 0x00,0x00,0xFF,0xDB,0x99,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00
; U (85)
db 0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00
; V (86)
db 0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00
; W (87)
db 0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0xEE,0x6C,0x00,0x00,0x00,0x00
; X (88)
db 0x00,0x00,0xC6,0xC6,0x6C,0x7C,0x38,0x38,0x7C,0x6C,0xC6,0xC6,0x00,0x00,0x00,0x00
; Y (89)
db 0x00,0x00,0x66,0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00
; Z (90)
db 0x00,0x00,0xFE,0xC6,0x86,0x0C,0x18,0x30,0x60,0xC2,0xC6,0xFE,0x00,0x00,0x00,0x00
; [ (91)
db 0x00,0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00,0x00
; \ (92)
db 0x00,0x00,0x00,0x80,0xC0,0xE0,0x70,0x38,0x1C,0x0E,0x06,0x02,0x00,0x00,0x00,0x00
; ] (93)
db 0x00,0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00
; ^ (94)
db 0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
; _ (95)
db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00
; ` (96)
db 0x30,0x30,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
; a (97)
db 0x00,0x00,0x00,0x00,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00
; b (98)
db 0x00,0x00,0xE0,0x60,0x60,0x78,0x6C,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00
; c (99)
db 0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00
; d (100)
db 0x00,0x00,0x1C,0x0C,0x0C,0x3C,0x6C,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00
; e (101)
db 0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00
; f (102)
db 0x00,0x00,0x38,0x6C,0x64,0x60,0xF0,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00
; g (103)
db 0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0xCC,0x78,0x00
; h (104)
db 0x00,0x00,0xE0,0x60,0x60,0x6C,0x76,0x66,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00
; i (105)
db 0x00,0x00,0x18,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00
; j (106)
db 0x00,0x00,0x06,0x06,0x00,0x0E,0x06,0x06,0x06,0x06,0x06,0x06,0x66,0x66,0x3C,0x00
; k (107)
db 0x00,0x00,0xE0,0x60,0x60,0x66,0x6C,0x78,0x78,0x6C,0x66,0xE6,0x00,0x00,0x00,0x00
; l (108)
db 0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00
; m (109)
db 0x00,0x00,0x00,0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0xD6,0xC6,0x00,0x00,0x00,0x00
; n (110)
db 0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00
; o (111)
db 0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00
; p (112)
db 0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00
; q (113)
db 0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0x0C,0x1E,0x00
; r (114)
db 0x00,0x00,0x00,0x00,0x00,0xDC,0x76,0x66,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00
; s (115)
db 0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00,0x00,0x00,0x00
; t (116)
db 0x00,0x00,0x10,0x30,0x30,0xFC,0x30,0x30,0x30,0x30,0x36,0x1C,0x00,0x00,0x00,0x00
; u (117)
db 0x00,0x00,0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00
; v (118)
db 0x00,0x00,0x00,0x00,0x00,0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00,0x00,0x00,0x00
; w (119)
db 0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0x6C,0x00,0x00,0x00,0x00
; x (120)
db 0x00,0x00,0x00,0x00,0x00,0xC6,0x6C,0x38,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00
; y (121)
db 0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7E,0x06,0x0C,0xF8,0x00
; z (122)
db 0x00,0x00,0x00,0x00,0x00,0xFE,0xCC,0x18,0x30,0x60,0xC6,0xFE,0x00,0x00,0x00,0x00
; { (123)
db 0x00,0x00,0x0E,0x18,0x18,0x18,0x70,0x18,0x18,0x18,0x18,0x0E,0x00,0x00,0x00,0x00
; | (124)
db 0x00,0x00,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00
; } (125)
db 0x00,0x00,0x70,0x18,0x18,0x18,0x0E,0x18,0x18,0x18,0x18,0x70,0x00,0x00,0x00,0x00
; ~ (126)
db 0x00,0x00,0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00

;=======================================================================
; DATA: CURSOR BITMAPS (16-bit wide × 20 rows: fill, border, shadow)
;=======================================================================

; Fill (white interior) - 20 rows × 16 bits (MSB=left)
gui_cursor_fill:
        dw 0x0000               ; row  0
        dw 0x4000               ; row  1
        dw 0x6000               ; row  2
        dw 0x7000               ; row  3
        dw 0x7800               ; row  4
        dw 0x7C00               ; row  5
        dw 0x7E00               ; row  6
        dw 0x7F00               ; row  7
        dw 0x7F80               ; row  8
        dw 0x7FC0               ; row  9
        dw 0x7FE0               ; row 10
        dw 0x7F00               ; row 11
        dw 0x7F00               ; row 12
        dw 0x6780               ; row 13
        dw 0x4380               ; row 14
        dw 0x03C0               ; row 15
        dw 0x01C0               ; row 16
        dw 0x01E0               ; row 17
        dw 0x00E0               ; row 18
        dw 0x0000               ; row 19

; Border (black outline) - 20 rows × 16 bits
gui_cursor_border:
        dw 0x8000               ; row  0
        dw 0xA000               ; row  1
        dw 0x9000               ; row  2
        dw 0x8800               ; row  3
        dw 0x8400               ; row  4
        dw 0x8200               ; row  5
        dw 0x8100               ; row  6
        dw 0x8080               ; row  7
        dw 0x8040               ; row  8
        dw 0x8020               ; row  9
        dw 0x8010               ; row 10
        dw 0x80F0               ; row 11
        dw 0x8080               ; row 12
        dw 0x9840               ; row 13
        dw 0x3C40               ; row 14
        dw 0x0420               ; row 15
        dw 0x0220               ; row 16
        dw 0x0210               ; row 17
        dw 0x0110               ; row 18
        dw 0x00F0               ; row 19

; Shadow (soft outline for visibility) - 20 rows × 16 bits
gui_cursor_shadow:
        dw 0xC000               ; row  0
        dw 0xF000               ; row  1
        dw 0xF800               ; row  2
        dw 0xFC00               ; row  3
        dw 0xFE00               ; row  4
        dw 0xFF00               ; row  5
        dw 0xFF80               ; row  6
        dw 0xFFC0               ; row  7
        dw 0xFFE0               ; row  8
        dw 0xFFF0               ; row  9
        dw 0xFFF8               ; row 10
        dw 0xFFFC               ; row 11
        dw 0xFFF0               ; row 12
        dw 0xFFC0               ; row 13
        dw 0x7FC0               ; row 14
        dw 0x07E0               ; row 15
        dw 0x03E0               ; row 16
        dw 0x03F0               ; row 17
        dw 0x01F0               ; row 18
        dw 0x00F0               ; row 19

;=======================================================================
; DATA: THEME PRESETS
;=======================================================================

; Windows 2000 classic: teal desktop, grey 3-D chrome, navy title/menu.
gui_theme_blue:
        dd 0x00008080           ; desktop bg - teal
        dd 0x00C0C0C0           ; taskbar bg - classic grey
        dd 0x00000080           ; title active - navy
        dd 0x00808080           ; title inactive - grey
        dd 0x00FFFFFF           ; title text
        dd 0x00C0C0C0           ; window bg - classic grey
        dd 0x00000000           ; window border - black
        dd 0x00C0C0C0           ; menu bg - classic grey
        dd 0x00000000           ; menu text - black
        dd 0x00000080           ; menu highlight - navy
        dd 0x00C0C0C0           ; button bg - classic grey
        dd 0x00000080           ; accent - navy

gui_theme_dark:
        dd 0x00202020
        dd 0x00181818
        dd 0x00505050
        dd 0x00383838
        dd 0x00E0E0E0
        dd 0x00282828
        dd 0x00505050
        dd 0x00303030
        dd 0x00D0D0D0
        dd 0x00505050
        dd 0x00404040
        dd 0x00608060

gui_theme_light:
        dd 0x00B0C4DE
        dd 0x00E0E0E0
        dd 0x004080C0
        dd 0x00C0C0C0
        dd 0x00000000
        dd 0x00FFFFFF
        dd 0x00A0A0A0
        dd 0x00FFFFFF
        dd 0x00000000
        dd 0x004080C0
        dd 0x00E0E0E0
        dd 0x004080C0

gui_theme_amber:
        dd 0x00201000           ; desktop bg - deep brown
        dd 0x00301800           ; taskbar bg - dark brown
        dd 0x00CC8800           ; title active - amber
        dd 0x00604020           ; title inactive - muted brown
        dd 0x00FFCC00           ; title text - bright amber
        dd 0x00281400           ; window bg - very dark brown
        dd 0x00CC8800           ; window border - amber
        dd 0x00301800           ; menu bg - dark brown
        dd 0x00FFAA00           ; menu text - amber
        dd 0x00CC8800           ; menu highlight - amber
        dd 0x00503000           ; button bg - brown
        dd 0x00FF9900           ; accent - orange-amber

;=======================================================================
; DATA: STRINGS
;=======================================================================

gui_str_menu:       db "Menu", 0
gui_str_start:      db "Start", 0
gui_str_am:         db "AM", 0
gui_str_pm:         db "PM", 0
gui_str_start_user: db "Burrows", 0
gui_str_start_banner: db "Windows 2000", 0
gui_str_about:      db "About", 0
gui_str_clock:      db "Clock", 0
gui_str_settings:   db "Settings", 0

gui_str_about_l1:   db "Mellivora OS", 0
gui_str_about_l2:   db "v3.0.0", 0
gui_str_about_l3:   db "Burrows Desktop Environment", 0
gui_str_about_l4:   db "32-bit Protected Mode / Pentium 4 SSE2", 0
gui_str_about_l5:   db "HBFS Filesystem / 2 GB", 0
gui_str_about_l6:   db "Press ESC to close", 0
gui_str_ok:         db "OK", 0

gui_str_clk_label:  db "Current Time", 0
gui_str_clk_set:    db "  Set...  ", 0
gui_str_clk_edit:   db "Set Date & Time", 0
gui_str_clk_apply:  db " Apply", 0
gui_str_clk_cancel: db " Cancel", 0
gui_str_clk_pm:     db "+/-", 0
gui_str_clk_year:   db "Y:", 0
gui_str_clk_mon:    db "M:", 0
gui_str_clk_day:    db "D:", 0
gui_str_clk_hr:     db "H:", 0
gui_str_clk_min:    db "M:", 0
gui_str_clk_sec:    db "S:", 0

gui_str_set_title:  db "Select Color Theme", 0
gui_str_theme_blue: db "Win2000", 0
gui_str_theme_dark: db "Dark", 0
gui_str_theme_light: db "Light", 0
gui_str_theme_amber: db "Amber", 0
gui_str_set_cur:    db "Current Theme", 0
gui_str_set_wall:   db "Wallpaper", 0
gui_str_wall_gradient: db "Gradient", 0
gui_str_wall_solid: db "Solid", 0
gui_str_set_instr:  db "Click or press 1/2/3/4, 5 = wallpaper", 0

; Menu item strings
gui_str_launcher_title: db "Burrows Launcher", 0
gui_str_ws:         db "WS", 0
gui_str_m_term:     db "BTerm", 0
gui_str_m_files:    db "BHive", 0
gui_str_m_forager:  db "BForager", 0
gui_str_m_edit:     db "BEdit", 0
gui_str_m_calc:     db "BCalc", 0
gui_str_m_paint:    db "BPaint", 0
gui_str_m_timewarp: db "Time Warp", 0
gui_str_m_clock:    db "Clock", 0
gui_str_m_settings: db "Settings", 0
gui_str_m_sysmon:   db "BSysMon", 0
gui_str_m_launcher: db "Launcher", 0
gui_str_m_about:    db "About", 0
; Serial markers used by tests/test_desktop.py and for debugging.
burrows_log_up:     db "[burrows] desktop up", 13, 10, 0
burrows_log_down:   db "[burrows] desktop exit", 13, 10, 0
burrows_log_launch: db "[burrows] launch ", 0
burrows_log_ws:     db "[burrows] workspace ", 0
burrows_log_fault:  db "[burrows] fault contained", 13, 10, 0
burrows_log_crlf:   db 13, 10, 0

gui_str_nc_title:   db "Notifications", 0
gui_str_nc_empty:   db "Nothing yet", 0
gui_str_nc_hint:    db "F3 to close", 0

gui_str_switcher:   db "Switch window", 0
gui_str_m_exit:     db "Exit Desktop", 0

; Start menu, Windows 2000 hierarchy. Items 0-2 open fly-out submenus.
gui_str_m_programs:  db "Programs", 0
gui_str_m_documents: db "Documents", 0
gui_str_m_search:    db "Search", 0
gui_str_m_help:      db "Help", 0
gui_str_m_run:       db "Run...", 0
gui_str_m_shutdown:  db "Shut Down...", 0
gui_str_shutdown:    db "Shut Down Windows", 0
gui_str_sd_prompt:   db "What do you want the computer to do?", 0
gui_str_sd_off:      db "Shut down", 0
gui_str_sd_restart:  db "Restart", 0
gui_str_sd_shell:    db "Command prompt", 0
gui_str_sd_cancel:   db "Cancel", 0
burrows_log_sd_dialog: db "[burrows] shutdown dialog", 10, 0
gui_sd_labels:
        dd gui_str_sd_off
        dd gui_str_sd_restart
        dd gui_str_sd_shell
        dd gui_str_sd_cancel

; Main item -> icon index (see gui_draw_icon16)
gui_menu_icon_map:
        db 1, 1, 9, 2, 10, 8, 11
; Main item -> submenu id, or GUI_SUB_NONE
gui_menu_sub_map:
        db 0, 1, 2, GUI_SUB_NONE, GUI_SUB_NONE, GUI_SUB_NONE, GUI_SUB_NONE
gui_menu_strings:
        dd gui_str_m_programs
        dd gui_str_m_documents
        dd gui_str_m_settings
        dd gui_str_m_search
        dd gui_str_m_help
        dd gui_str_m_run
        dd gui_str_m_shutdown

; Programs fly-out
gui_sub_programs_strings:
        dd gui_str_m_term
        dd gui_str_m_files
        dd gui_str_m_forager
        dd gui_str_m_edit
        dd gui_str_m_calc
        dd gui_str_m_paint
        dd gui_str_m_timewarp
        dd gui_str_m_sysmon
        dd gui_str_m_launcher
gui_sub_programs_icons:
        db 0, 1, 2, 3, 4, 5, 6, 7, 8
gui_sub_programs_progs:
        dd gui_prog_bterm
        dd gui_prog_bhive
        dd gui_prog_bforager
        dd gui_prog_bedit
        dd gui_prog_bcalc
        dd gui_prog_bpaint
        dd gui_prog_timewarp
        dd gui_prog_bsysmon
        dd gui_prog_blauncher

; Documents fly-out
gui_str_doc_mine:    db "My Documents", 0
gui_str_doc_samples: db "Samples", 0
gui_sub_documents_strings:
        dd gui_str_doc_mine
        dd gui_str_doc_samples
gui_sub_documents_icons:
        db 1, 1

; Settings fly-out
gui_str_set_control:  db "Control Panel", 0
gui_str_set_datetime: db "Date/Time", 0
gui_sub_settings_strings:
        dd gui_str_set_control
        dd gui_str_set_datetime
gui_sub_settings_icons:
        db 9, 6

; Submenu descriptor tables, indexed by submenu id.
gui_sub_counts:      db 9, 2, 2
gui_sub_strings_ptr: dd gui_sub_programs_strings, gui_sub_documents_strings, gui_sub_settings_strings
gui_sub_icons_ptr:   dd gui_sub_programs_icons, gui_sub_documents_icons, gui_sub_settings_icons

; Program names for external apps
gui_prog_bterm:     db "bterm", 0
gui_prog_bhive:     db "bhive", 0
gui_prog_bforager:  db "bforager", 0
gui_prog_bedit:     db "bedit", 0
gui_prog_bcalc:     db "bcalc", 0
gui_prog_bpaint:    db "bpaint", 0
gui_prog_bsysmon:   db "bsysmon", 0
gui_prog_blauncher: db "blauncher", 0
gui_prog_timewarp:  db "timewarp", 0

; Horizontal offset of each Start-flag row, giving the flag its wave.
gui_flag_wave:      db 0, 0, 1, 1, 2, 2, 1, 1, 0, 0

; Desktop icon data
gui_icon_symbols:   db 'T', 'H', 'W', 'E', 'C', 'P', 'Z', 'M', 'L', 0

gui_icon_labels:
        dd gui_str_ic_term
        dd gui_str_ic_files
        dd gui_str_ic_forager
        dd gui_str_ic_edit
        dd gui_str_ic_calc
        dd gui_str_ic_paint
        dd gui_str_ic_timewarp
        dd gui_str_ic_sysmon
        dd gui_str_ic_launcher

gui_icon_progs:
        dd gui_prog_bterm
        dd gui_prog_bhive
        dd gui_prog_bforager
        dd gui_prog_bedit
        dd gui_prog_bcalc
        dd gui_prog_bpaint
        dd gui_prog_timewarp
        dd gui_prog_bsysmon
        dd gui_prog_blauncher

gui_str_ic_term:     db "BTerm", 0
gui_str_ic_files:   db "BHive", 0
gui_str_ic_forager: db "BForager", 0
gui_str_ic_edit:    db "BEdit", 0
gui_str_ic_calc:    db "BCalc", 0
gui_str_ic_paint:   db "BPaint", 0
gui_str_ic_timewarp: db "Time Warp", 0
gui_str_ic_sysmon:  db "BSysMon", 0
gui_str_ic_launcher: db "Launcher", 0

; Right-click desktop menu strings
gui_str_ctx_term:    db "Open BTerm", 0
gui_str_ctx_files:   db "Open BHive", 0
gui_str_ctx_refresh: db "Refresh", 0
gui_str_ctx_set:     db "Properties...", 0
gui_str_ctx_exit:    db "Exit Desktop", 0

gui_ctx_strings:
        dd gui_str_ctx_term
        dd gui_str_ctx_files
        dd gui_str_ctx_refresh
        dd gui_str_ctx_set
        dd gui_str_ctx_exit

;=======================================================================
; NOTIFICATION SYSTEM
;=======================================================================

; Constants
NOTIFY_MAX      equ 3           ; max simultaneous notifications
NOTIFY_W        equ 220
NOTIFY_H        equ 40
NOTIFY_X        equ (GUI_SCREEN_W - NOTIFY_W - 10)
NOTIFY_Y_START  equ 10
NOTIFY_GAP      equ 44
NOTIFY_DURATION equ 400         ; ticks (4 seconds at 100Hz)
NOTIFY_TEXT_LEN equ 60
NOTIFY_HIST_MAX equ 8           ; notification centre keeps the last 8 entries

;---------------------------------------
; gui_notify - Post a notification toast
;  ESI = notification text string (copied, max 60 chars)
;  EDX = color (0=default blue)
;---------------------------------------
gui_notify:
        pushad
        mov [.gn_color], edx
        test edx, edx
        jnz .gn_color_ok
        mov edx, 0x003366CC     ; default blue
.gn_color_ok:

        ; Find free notification slot
        xor ecx, ecx
.gn_find_slot:
        cmp ecx, NOTIFY_MAX
        jge .gn_full
        cmp byte [notify_active + ecx], 0
        je .gn_found
        inc ecx
        jmp .gn_find_slot

.gn_full:
        ; All slots used, overwrite oldest (slot 0)
        xor ecx, ecx

.gn_found:
        ; Activate slot
        mov byte [notify_active + ecx], 1
        ; Set expiry time
        mov eax, [tick_count]
        add eax, NOTIFY_DURATION
        mov [notify_timer + ecx * 4], eax
        ; Set color
        mov [notify_color + ecx * 4], edx
        ; Record the entry in the notification centre history.
        mov [.gn_slot], ecx
        mov edi, [notify_hist_head]
        imul edi, NOTIFY_TEXT_LEN
        add edi, notify_hist_text
        mov ebx, NOTIFY_TEXT_LEN - 1
        push esi
.gn_hist:
        lodsb
        test al, al
        jz .gn_hist_pad
        stosb
        dec ebx
        jnz .gn_hist
.gn_hist_pad:
        mov byte [edi], 0
        pop esi
        mov ecx, [.gn_slot]
        mov eax, [notify_hist_head]
        mov [notify_hist_color + eax*4], edx
        inc eax
        and eax, NOTIFY_HIST_MAX - 1
        mov [notify_hist_head], eax
        mov eax, [notify_hist_count]
        cmp eax, NOTIFY_HIST_MAX
        jae .gn_hist_full
        inc dword [notify_hist_count]
.gn_hist_full:
        mov ecx, [.gn_slot]

        ; Copy text
        mov eax, ecx
        imul eax, NOTIFY_TEXT_LEN
        lea edi, [notify_text + eax]
        mov ebx, NOTIFY_TEXT_LEN - 1
.gn_copy:
        mov al, [esi]
        test al, al
        jz .gn_pad
        mov [edi], al
        inc esi
        inc edi
        dec ebx
        jnz .gn_copy
.gn_pad:
        mov byte [edi], 0

        popad
        ret
.gn_slot:  dd 0
.gn_color: dd 0


;---------------------------------------
; gui_notify_center_toggle - Show or hide the notification history panel
;---------------------------------------
gui_notify_center_toggle:
        pushad
        xor byte [gui_center_open], 1
        popad
        ret

;---------------------------------------
; gui_draw_notify_center - Right-hand history panel (F3)
;---------------------------------------
NPANEL_W        equ 320
NPANEL_ROW_H    equ 34

gui_draw_notify_center:
        pushad
        cmp byte [gui_center_open], 0
        je .dnc_done
        call gui_target_screen

        mov dword [gui_nc_x], GUI_SCREEN_W - NPANEL_W - 8
        mov dword [gui_nc_y], 40
        mov dword [gui_nc_w], NPANEL_W

        ; Panel size: title row plus one row per stored notification (min 3)
        mov eax, [notify_hist_count]
        cmp eax, 3
        jae .dnc_rows_ok
        mov eax, 3
.dnc_rows_ok:
        imul eax, NPANEL_ROW_H
        add eax, NPANEL_ROW_H
        mov [gui_nc_h], eax

        ; Shadow
        mov eax, [gui_nc_x]
        add eax, 4
        mov ebx, [gui_nc_y]
        add ebx, 4
        mov ecx, [gui_nc_w]
        mov edx, [gui_nc_h]
        mov esi, 0x00101828
        call gui_bb_fill_rect

        ; Body and title strip
        mov eax, [gui_nc_x]
        mov ebx, [gui_nc_y]
        mov ecx, [gui_nc_w]
        mov edx, [gui_nc_h]
        mov esi, [gui_theme + THEME_WINDOW_BG]
        call gui_bb_fill_rect
        mov eax, [gui_nc_x]
        mov ebx, [gui_nc_y]
        mov ecx, [gui_nc_w]
        mov edx, NPANEL_ROW_H
        mov esi, [gui_theme + THEME_MENU_HIGHLIGHT]
        call gui_bb_fill_rect
        mov eax, [gui_nc_x]
        add eax, 10
        mov ebx, [gui_nc_y]
        add ebx, 10
        mov esi, gui_str_nc_title
        mov edx, [gui_theme + THEME_TITLE_TEXT]
        call gui_bb_draw_string

        ; Newest first: walk the ring backwards from head-1.
        mov dword [gui_nc_i], 0
.dnc_row:
        mov eax, [gui_nc_i]
        cmp eax, [notify_hist_count]
        jge .dnc_footer

        ; history index = (head - 1 - i) mod HIST_MAX
        mov ebx, [notify_hist_head]
        dec ebx
        sub ebx, eax
        and ebx, NOTIFY_HIST_MAX - 1

        ; Row background
        mov eax, [gui_nc_i]
        imul eax, NPANEL_ROW_H
        add eax, [gui_nc_y]
        add eax, NPANEL_ROW_H
        mov [gui_nc_ry], eax
        mov eax, [gui_nc_x]
        mov ebx, [gui_nc_ry]
        mov ecx, [gui_nc_w]
        mov edx, 1
        mov esi, [gui_theme + THEME_WINDOW_BORDER]
        call gui_bb_fill_rect

        ; Colour bar showing where the notification came from
        mov eax, [gui_nc_i]
        mov ebx, [notify_hist_head]
        dec ebx
        sub ebx, eax
        and ebx, NOTIFY_HIST_MAX - 1
        mov esi, [notify_hist_color + ebx*4]
        mov eax, [gui_nc_x]
        add eax, 6
        mov ebx, [gui_nc_ry]
        add ebx, 6
        mov ecx, 4
        mov edx, NPANEL_ROW_H - 12
        call gui_bb_fill_rect

        ; Text
        mov eax, [gui_nc_i]
        mov ebx, [notify_hist_head]
        dec ebx
        sub ebx, eax
        and ebx, NOTIFY_HIST_MAX - 1
        imul ebx, NOTIFY_TEXT_LEN
        add ebx, notify_hist_text
        mov esi, ebx
        mov eax, [gui_nc_x]
        add eax, 16
        mov ebx, [gui_nc_ry]
        add ebx, 5
        mov edx, 0x00202020
        call gui_bb_draw_string

        inc dword [gui_nc_i]
        jmp .dnc_row

.dnc_footer:
        cmp dword [notify_hist_count], 0
        jne .dnc_hint
        mov eax, [gui_nc_x]
        add eax, 16
        mov ebx, [gui_nc_y]
        add ebx, NPANEL_ROW_H + 8
        mov esi, gui_str_nc_empty
        mov edx, 0x00606060
        call gui_bb_draw_string
.dnc_hint:
        mov eax, [gui_nc_x]
        add eax, 10
        mov ebx, [gui_nc_y]
        add ebx, [gui_nc_h]
        sub ebx, NPANEL_ROW_H - 8
        mov esi, gui_str_nc_hint
        mov edx, 0x00606060
        call gui_bb_draw_string
.dnc_done:
        popad
        ret

;---------------------------------------
; gui_draw_notifications - Draw active notification toasts
;---------------------------------------
gui_draw_notifications:
        pushad
        xor ecx, ecx
.dn_loop:
        cmp ecx, NOTIFY_MAX
        jge .dn_done

        cmp byte [notify_active + ecx], 0
        je .dn_next

        ; Check if expired
        mov eax, [tick_count]
        cmp eax, [notify_timer + ecx * 4]
        jge .dn_expire

        ; Save index, pre-compute position into temp vars
        mov [.dn_idx], ecx
        mov eax, NOTIFY_X
        mov [.dn_tmp_x], eax
        mov eax, ecx
        imul eax, NOTIFY_GAP
        add eax, NOTIFY_Y_START
        mov [.dn_tmp_y], eax

        ; Draw shadow
        mov eax, [.dn_tmp_x]
        add eax, 2
        mov ebx, [.dn_tmp_y]
        add ebx, 2
        mov ecx, NOTIFY_W
        mov edx, NOTIFY_H
        mov esi, 0x00222222
        call gui_bb_fill_rect

        ; Balloon-tip face, as Windows 2000 draws near the tray.
        mov eax, [.dn_tmp_x]
        mov ebx, [.dn_tmp_y]
        mov ecx, NOTIFY_W
        mov edx, NOTIFY_H
        mov esi, 0x00FFFFE1
        call gui_bb_fill_rect
        mov eax, [.dn_tmp_x]
        mov ebx, [.dn_tmp_y]
        mov ecx, NOTIFY_W
        mov edx, NOTIFY_H
        mov esi, 0x00000000
        call gui_bb_rect

        ; Draw color accent bar on left (4px wide)
        mov eax, [.dn_tmp_x]
        mov ebx, [.dn_tmp_y]
        mov ecx, 4
        mov edx, NOTIFY_H
        mov esi, [.dn_idx]
        mov esi, [notify_color + esi * 4]
        call gui_bb_fill_rect

        ; Draw text
        mov eax, [.dn_tmp_x]
        add eax, 10
        mov ebx, [.dn_tmp_y]
        add ebx, 14
        mov edx, 0x00000000
        mov esi, [.dn_idx]
        imul esi, NOTIFY_TEXT_LEN
        add esi, notify_text
        call gui_bb_draw_string

        mov ecx, [.dn_idx]
        jmp .dn_next

.dn_expire:
        mov byte [notify_active + ecx], 0

.dn_next:
        inc ecx
        jmp .dn_loop

.dn_done:
        popad
        ret

.dn_idx:   dd 0
.dn_tmp_x: dd 0
.dn_tmp_y: dd 0


; Notification data
notify_active:  times NOTIFY_MAX db 0
notify_timer:   times NOTIFY_MAX dd 0
notify_color:   times NOTIFY_MAX dd 0
notify_text:    times (NOTIFY_MAX * NOTIFY_TEXT_LEN) db 0

; Notification centre: history survives the toast expiring.
notify_hist_head:  dd 0
notify_hist_count: dd 0
notify_hist_color: times NOTIFY_HIST_MAX dd 0
notify_hist_text:  times (NOTIFY_HIST_MAX * NOTIFY_TEXT_LEN) db 0
gui_center_open:   db 0

;=======================================================================
; STATE VARIABLES (initialized, in binary image)
;=======================================================================

gui_desktop_active: db 0
gui_exit_flag:      db 0
gui_launch_flag:    db 0        ; set by GUI_LAUNCH: create READY task, return
gui_exit_pid:       dd -1       ; PID whose windows must be reaped
gui_launcher_win:   dd -1       ; launcher window index for singleton launch
gui_title_buf:      times 16 db 0
gui_ws_buf:         db "1", 0
gui_menu_open:      db 0
gui_ctx_open:       db 0
gui_dragging:       db 0
gui_active_workspace: db 0         ; v9.0: current virtual desktop (0-3)
gui_resizing:       db 0           ; v9.0: edge-resize in progress
gui_resize_edge:    db 0           ; v9.0: bit0=right, bit1=bottom
                    db 0, 0        ; padding
gui_drag_win:       dd 0
gui_drag_off_x:     dd 0
gui_drag_off_y:     dd 0
gui_resize_win:     dd 0           ; v9.0: window being resized
gui_resize_mx0:     dd 0           ; initial mouse x for resize
gui_resize_my0:     dd 0           ; initial mouse y
gui_resize_w0:      dd 0           ; original window width
gui_resize_h0:      dd 0           ; original window height
gui_resize_x0:      dd 0           ; original window x (left-edge resize)
gui_resize_y0:      dd 0           ; original window y (top-edge resize)
gui_title_dbl_win:  dd -1          ; title-bar double-click tracking
gui_title_dbl_until: dd 0
gui_focused_win:    dd -1
gui_close_req:      dd -1         ; window whose owner should receive EVT_CLOSE
gui_switcher_until: dd 0          ; tick deadline for the Alt+Tab overlay
gui_sw_count:       dd 0
gui_sw_x:           dd 0
gui_sw_y:           dd 0
gui_sw_w:           dd 0
gui_sw_h:           dd 0
gui_sw_row_y:       dd 0
gui_sw_text:        dd 0
gui_nc_x:           dd 0
gui_nc_y:           dd 0
gui_nc_w:           dd 0
gui_nc_h:           dd 0
gui_nc_ry:          dd 0
gui_nc_i:           dd 0
gui_sw_list:        times 8 dd 0
gui_win_count:      dd 0
gui_next_z:         dd 0
gui_menu_sel:       dd -1
gui_menu_sub:       dd -1
gui_menu_sub_sel:   dd -1
gui_sub_x:          dd 0
gui_sub_y:          dd 0
gui_sub_h:          dd 0
gui_task_btn_w:     dd TASKBAR_TASK_W
gui_icon_sel:       dd -1
gui_icon_click_ttl: dd 0
gui_ctx_sel:        dd -1
gui_ctx_x:          dd 0
gui_ctx_y:          dd 0
gui_prev_mouse_x:   dd 0
gui_prev_mouse_y:   dd 0
gui_prev_mouse_btn: db 0
gui_mouse_moved:    db 0
                    db 0, 0     ; padding
gui_clock_buf:      times 12 db 0

; Gradient title bar temporaries
gui_grad_base:      dd 0
gui_grad_x:         dd 0
gui_grad_y:         dd 0
gui_grad_w:         dd 0
gui_grad_row:       dd 0
gui_grad_factor:    dd 0
gui_grad_color:     dd 0
gui_grad_r0:        dd 0
gui_grad_r1:        dd 0
gui_grad_g0:        dd 0
gui_grad_g1:        dd 0
gui_grad_b0:        dd 0
gui_grad_b1:        dd 0

; Runtime theme (48 bytes, copied from preset at init)
gui_theme:          times THEME_SIZE db 0
; 0 = vertical gradient, 1 = flat desktop colour. Persisted in burrows.cfg.
; Windows 2000 shipped with a flat teal desktop.
gui_wallpaper:      dd 1

; Window table (16 windows x 96 bytes = 1536 bytes)
gui_win_table:      times MAX_WINDOWS * WIN_STRUCT_SIZE db 0
