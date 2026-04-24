;=======================================================================
; burrows.inc - Burrows Desktop Environment (Window Manager)
;
; Full windowed GUI desktop for Mellivora OS:
;   - Double-buffered 1024x768x32 compositor
;   - Draggable windows with title bar and close button
;   - Customizable color themes (3 presets)
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
WIN_STRUCT_SIZE equ 112

; Window struct field offsets
WIN_FLAGS       equ 0           ; dword: bit0=active, bit1=visible, bit2=maximized
WIN_X           equ 4           ; dword: content area x
WIN_Y           equ 8           ; dword: content area y
WIN_W           equ 12          ; dword: content area width
WIN_H           equ 16          ; dword: content area height
WIN_Z           equ 20          ; dword: z-order (higher = on top)
WIN_TITLE       equ 24          ; 64 bytes: null-terminated title
WIN_APP_ID      equ 88          ; dword: owning app id (0=desktop)
WIN_SAVE_X      equ 92          ; saved x before maximize
WIN_SAVE_Y      equ 96          ; saved y before maximize
WIN_SAVE_W      equ 100         ; saved w before maximize
WIN_SAVE_H      equ 104         ; saved h before maximize

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

; Menu constants
GUI_MENU_X      equ 4
GUI_MENU_W      equ 160
GUI_MENU_ITEM_H equ 24
GUI_MENU_ITEMS  equ 12

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
        mov byte [gui_exit_flag], 0
        mov byte [gui_dragging], 0
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
        mov byte [vga_color], COLOR_ERROR
        mov esi, .no_vbe_msg
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
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
        mov byte [gui_ctx_open], 0
        mov byte [gui_dragging], 0
        mov dword [gui_focused_win], -1
        mov dword [gui_win_count], 0
        mov dword [gui_next_z], 0
        mov byte [gui_prev_mouse_btn], 0

        ; Clear window table
        mov edi, gui_win_table
        xor eax, eax
        mov ecx, (MAX_WINDOWS * WIN_STRUCT_SIZE) / 4
        rep stosd

        ; Post welcome notification
        mov esi, .notify_welcome
        xor edx, edx           ; default color
        call gui_notify

gui_desktop_loop:
        ; Track idle time / trigger screensaver
        call scr_idle_tick

        ; Compose frame: background + windows + taskbar + menu + cursor
        call gui_compose

        ; Handle input
        call gui_handle_input

        ; Check exit flag
        cmp byte [gui_exit_flag], 1
        jne gui_desktop_loop

        ; --- Exit desktop ---
        call gui_exit_desktop
        ; If we were launched normally, ret works.
        ; If stack was reset by sys_exit after an app, jump to shell.
        ; Use shell_main unconditionally — it's always safe.
        jmp shell_main

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
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_vbe_no
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
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
        call gui_draw_all_windows
        call gui_draw_taskbar
        cmp byte [gui_menu_open], 1
        jne .no_menu
        call gui_draw_menu
.no_menu:
        cmp byte [gui_ctx_open], 1
        jne .no_ctx
        call gui_draw_ctx_menu
.no_ctx:
        call gui_draw_notifications
        call gui_draw_cursor
        call gui_flip
        popad
        ret

;---------------------------------------
; gui_draw_background - Fill backbuf with desktop color
;---------------------------------------
gui_draw_background:
        pushad
        cld
        mov edi, GUI_BACKBUF
        mov eax, [gui_theme + THEME_DESKTOP_BG]
        mov ecx, GUI_SCREEN_W * GUI_SCREEN_H
        rep stosd
        popad
        ret

;---------------------------------------
; Desktop icon layout constants
;---------------------------------------
ICON_SIZE       equ 32          ; icon box width/height
ICON_TEXT_H     equ 12          ; space below icon for label
ICON_CELL_W     equ 72          ; grid cell width
ICON_CELL_H     equ 64          ; grid cell height
ICON_START_X    equ 20          ; first column x
ICON_START_Y    equ 16          ; first row y
ICON_COLS       equ 1           ; single column of icons
ICON_COUNT      equ 8           ; number of desktop icons

;---------------------------------------
; gui_draw_desktop_icons - Draw icons on desktop surface
;---------------------------------------
gui_draw_desktop_icons:
        pushad
        xor ecx, ecx           ; icon index
.di_loop:
        cmp ecx, ICON_COUNT
        jge .di_done

        ; Calculate position: single column, vertical layout
        mov eax, ICON_START_X
        mov ebx, ecx
        imul ebx, ICON_CELL_H
        add ebx, ICON_START_Y

        push ecx
        push eax
        push ebx

        ; Draw icon background (rounded-ish box with accent tint)
        push ecx
        push edx
        mov ecx, ICON_SIZE
        mov edx, ICON_SIZE
        ; Semi-transparent accent box
        mov esi, [gui_theme + THEME_ACCENT]
        ; Brighten slightly for icon bg
        push eax
        mov eax, esi
        shr eax, 1
        and eax, 0x007F7F7F
        add eax, 0x00404040    ; lighten
        mov esi, eax
        pop eax
        call gui_bb_fill_rect
        pop edx
        pop ecx

        ; Draw border around icon
        pop ebx
        pop eax
        push eax
        push ebx
        push ecx
        push edx
        mov ecx, ICON_SIZE
        mov edx, ICON_SIZE
        mov esi, [gui_theme + THEME_WINDOW_BORDER]
        call gui_bb_rect
        pop edx
        pop ecx

        ; Draw mini-symbol inside icon (a simple letter glyph)
        pop ebx
        pop eax
        push eax
        push ebx
        add eax, 12            ; center the char
        add ebx, 8
        push ecx
        push edx
        mov edx, 0x00FFFFFF
        mov ecx, [esp + 16]    ; icon index from outer push
        movzx ecx, byte [gui_icon_symbols + ecx]
        call gui_bb_draw_char
        pop edx
        pop ecx

        ; Draw label text below icon
        pop ebx
        pop eax
        push eax
        push ebx
        add ebx, ICON_SIZE + 2
        add eax, 0             ; left-align label
        push ecx
        mov ecx, [esp + 12]    ; icon index
        mov esi, [gui_icon_labels + ecx*4]
        mov edx, 0x00FFFFFF
        call gui_bb_draw_string
        pop ecx

        pop ebx
        pop eax
        pop ecx
        inc ecx
        jmp .di_loop
.di_done:
        popad
        ret

;---------------------------------------
; gui_icon_hit_test - Check if click is on a desktop icon
; EAX = x, EBX = y
; Returns: EAX = icon index (0-5) or -1
;---------------------------------------
gui_icon_hit_test:
        push ebx
        push ecx
        push edx
        push esi

        ; Check x range
        cmp eax, ICON_START_X
        jl .ih_miss
        cmp eax, ICON_START_X + ICON_SIZE
        jg .ih_miss

        ; Check y range and compute index
        sub ebx, ICON_START_Y
        cmp ebx, 0
        jl .ih_miss
        xor edx, edx
        mov eax, ebx
        mov ecx, ICON_CELL_H
        div ecx             ; eax = row index, edx = offset in cell
        cmp eax, ICON_COUNT
        jge .ih_miss
        ; Check that click is within the icon cell (not in gap below label)
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
; gui_bb_pixel - Draw single pixel
; EAX = x, EBX = y, ECX = color
;---------------------------------------
gui_bb_pixel:
        cmp eax, 0
        jl .bp_skip
        cmp eax, GUI_SCREEN_W
        jge .bp_skip
        cmp ebx, 0
        jl .bp_skip
        cmp ebx, GUI_SCREEN_H
        jge .bp_skip
        push edx
        imul edx, ebx, GUI_PITCH
        lea edx, [GUI_BACKBUF + edx + eax*4]
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
        cmp ebx, GUI_SCREEN_H
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
        cmp edi, GUI_SCREEN_W
        jle .hl_rclip
        mov ecx, GUI_SCREEN_W
        sub ecx, eax
.hl_rclip:
        cmp ecx, 0
        jle .hl_done
        imul ebx, GUI_PITCH
        lea edi, [GUI_BACKBUF + ebx + eax*4]
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
        cmp eax, GUI_SCREEN_W
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
        cmp edi, GUI_SCREEN_H
        jle .vl_bclip
        mov ecx, GUI_SCREEN_H
        sub ecx, ebx
.vl_bclip:
        cmp ecx, 0
        jle .vl_done
        imul ebx, GUI_PITCH
        lea edi, [GUI_BACKBUF + ebx + eax*4]
.vl_loop:
        cmp ecx, 0
        jle .vl_done
        mov [edi], edx
        add edi, GUI_PITCH
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
        cmp eax, GUI_SCREEN_W
        jge .fr_done
        cmp ebx, GUI_SCREEN_H
        jge .fr_done
        ; Clip right
        mov edi, eax
        add edi, ecx
        cmp edi, GUI_SCREEN_W
        jle .fr_rclip
        mov ecx, GUI_SCREEN_W
        sub ecx, eax
.fr_rclip:
        ; Clip bottom
        mov edi, ebx
        add edi, edx
        cmp edi, GUI_SCREEN_H
        jle .fr_bclip
        mov edx, GUI_SCREEN_H
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
        imul ebx, GUI_PITCH
        lea edi, [GUI_BACKBUF + ebx + eax*4]
.fr_row:
        cmp dword [.fr_h], 0
        jle .fr_done
        push edi
        mov ecx, [.fr_w]
        mov eax, esi
        rep stosd
        pop edi
        add edi, GUI_PITCH
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
        cmp ebx, GUI_SCREEN_H
        jge .dc_done
        cmp ebx, 0
        jl .dc_skip_row

        movzx edx, byte [esi]
        mov eax, [.dc_x]
        mov edi, 8
.dc_bit:
        cmp edi, 0
        je .dc_next_row
        cmp eax, GUI_SCREEN_W
        jge .dc_next_row
        cmp eax, 0
        jl .dc_skip_bit
        test edx, 0x80
        jz .dc_skip_bit
        ; Write pixel
        push edx
        push ecx
        imul ecx, ebx, GUI_PITCH
        lea ecx, [GUI_BACKBUF + ecx + eax*4]
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
        mov dword [ebp + WIN_FLAGS], 0x03       ; active + visible
        mov eax, [.cw_x]
        mov [ebp + WIN_X], eax
        mov eax, [.cw_y]
        mov [ebp + WIN_Y], eax
        mov eax, [.cw_w]
        mov [ebp + WIN_W], eax
        mov eax, [.cw_h]
        mov [ebp + WIN_H], eax

        ; Copy title (up to 63 chars)
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

        ; Z-order
        inc dword [gui_next_z]
        mov eax, [gui_next_z]
        mov [ebp + WIN_Z], eax

        ; Set focused
        mov [gui_focused_win], edi
        inc dword [gui_win_count]

        ; Return window index
        mov [esp + 28], edi     ; EAX in pushad frame
        popad
        ret

.cw_full:
        mov dword [esp + 28], -1
        popad
        ret

.cw_x: dd 0
.cw_y: dd 0
.cw_w: dd 0
.cw_h: dd 0
.cw_title: dd 0

;---------------------------------------
; gui_destroy_window_n - Destroy window by index
; EAX = window index
;---------------------------------------
gui_destroy_window_n:
        pushad
        cmp eax, MAX_WINDOWS
        jge .dw_done
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        mov dword [eax + WIN_FLAGS], 0
        dec dword [gui_win_count]
        cmp dword [gui_win_count], 0
        jge .dw_done
        mov dword [gui_win_count], 0
.dw_done:
        popad
        ret

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
        pushad
        xor ecx, ecx
        mov esi, gui_win_table
.ca_loop:
        cmp ecx, MAX_WINDOWS
        jge .ca_done
        test dword [esi + WIN_FLAGS], 0x01
        jz .ca_next
        ; Check if owned by an app (non-zero APP_ID)
        cmp dword [esi + WIN_APP_ID], 0
        je .ca_next
        ; Destroy it
        mov dword [esi + WIN_FLAGS], 0
        dec dword [gui_win_count]
.ca_next:
        add esi, WIN_STRUCT_SIZE
        inc ecx
        jmp .ca_loop
.ca_done:
        cmp dword [gui_win_count], 0
        jge .ca_ok
        mov dword [gui_win_count], 0
.ca_ok:
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
; gui_draw_window_n - Draw window by index
; EBX = window index
;---------------------------------------
gui_draw_window_n:
        pushad
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

        ; --- Draw shadow (3-pixel offset, dark color) ---
        push ecx
        push edx
        push esi
        push edi
        mov eax, ecx
        sub eax, BORDER_W
        add eax, 3             ; Shadow offset right
        mov ebx, edx
        sub ebx, TITLEBAR_H
        add ebx, 3             ; Shadow offset down
        mov ecx, esi
        add ecx, 2 * BORDER_W
        mov edx, edi
        add edx, TITLEBAR_H + BORDER_W
        mov esi, 0x00111111    ; Very dark shadow color
        call gui_bb_fill_rect
        pop edi
        pop esi
        pop edx
        pop ecx

        ; --- Draw border ---
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
        mov esi, [gui_theme + THEME_WINDOW_BORDER]
        call gui_bb_rect
        pop esi
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
        ; ESI = base color. Draw line by line with gradient.
        ; At top (row 0): lighter color (base + highlight)
        ; At bottom (row TITLEBAR_H-2): base color
        ; Save params: ecx=win_x, edx=win_y, esi=win_w (on stack)
        mov [gui_grad_base], esi
        mov [gui_grad_x], ecx
        mov [gui_grad_y], edx
        ; width for title bar fill = win_w + 2*BORDER_W - 2
        ; Note: esi now holds the color, but win_w is on the stack
        ; from the 'push esi' at the start of this block (before esi
        ; was overwritten with the theme color).
        mov eax, [esp]             ; win_w (saved on stack)
        add eax, 2 * BORDER_W - 2
        mov [gui_grad_w], eax
        mov dword [gui_grad_row], 0

.grad_loop:
        mov eax, [gui_grad_row]
        cmp eax, TITLEBAR_H - 1
        jge .grad_done

        ; Compute interpolated color: blend from light to base
        ; light_factor = (TITLEBAR_H - 1 - row) * 80 / (TITLEBAR_H - 1)
        ; Each channel: min(255, base_ch + light_factor)
        mov ecx, TITLEBAR_H - 1
        sub ecx, eax               ; TITLEBAR_H - 1 - row
        imul ecx, 80               ; scale
        xor edx, edx
        mov eax, TITLEBAR_H - 1
        push eax
        xchg eax, ecx
        pop ecx
        div ecx                    ; eax = light_factor
        mov [gui_grad_factor], eax

        ; Build gradient color from base
        mov esi, [gui_grad_base]
        ; Red channel
        mov eax, esi
        shr eax, 16
        and eax, 0xFF
        add eax, [gui_grad_factor]
        cmp eax, 255
        jbe .grad_r_ok
        mov eax, 255
.grad_r_ok:
        shl eax, 16
        mov [gui_grad_color], eax
        ; Green channel
        mov eax, esi
        shr eax, 8
        and eax, 0xFF
        add eax, [gui_grad_factor]
        cmp eax, 255
        jbe .grad_g_ok
        mov eax, 255
.grad_g_ok:
        shl eax, 8
        or [gui_grad_color], eax
        ; Blue channel
        mov eax, esi
        and eax, 0xFF
        add eax, [gui_grad_factor]
        cmp eax, 255
        jbe .grad_b_ok
        mov eax, 255
.grad_b_ok:
        or [gui_grad_color], eax

        ; Draw this line: gui_bb_fill_rect(x, y, w, 1, color)
        mov eax, [gui_grad_x]
        sub eax, BORDER_W
        inc eax
        mov ebx, [gui_grad_y]
        sub ebx, TITLEBAR_H
        add ebx, [gui_grad_row]
        inc ebx
        mov ecx, [gui_grad_w]
        mov edx, 1
        mov esi, [gui_grad_color]
        call gui_bb_fill_rect
        inc dword [gui_grad_row]
        jmp .grad_loop

.grad_done:
        pop esi
        pop edx
        pop ecx

        ; --- Draw title text ---
        push ecx
        push edx
        push esi
        mov eax, ecx
        add eax, 4
        mov ebx, edx
        sub ebx, TITLEBAR_H
        add ebx, 3
        lea esi, [ebp + WIN_TITLE]
        mov edx, [gui_theme + THEME_TITLE_TEXT]
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
        mov esi, 0x00CC3333     ; Red
        call gui_bb_button_3d
        pop ebx
        pop eax
        ; Draw "X"
        add eax, 4
        add ebx, 0
        mov cl, 'X'
        mov edx, 0x00FFFFFF
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
        mov esi, 0x00666666     ; Dark gray
        call gui_bb_button_3d
        pop ebx
        pop eax
        add eax, 3
        mov cl, '#'             ; maximize icon
        mov edx, 0x00FFFFFF
        call gui_bb_draw_char
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
        mov esi, 0x00666666     ; Dark gray
        call gui_bb_button_3d
        pop ebx
        pop eax
        add eax, 3
        mov cl, '_'             ; minimize icon
        mov edx, 0x00FFFFFF
        call gui_bb_draw_char
        pop esi
        pop edx
        pop ecx

        ; --- Draw content area background ---
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

.wn_done:
        popad
        ret

;=======================================================================
; TASKBAR
;=======================================================================

gui_draw_taskbar:
        pushad

        ; Taskbar background
        mov eax, 0
        mov ebx, TASKBAR_Y
        mov ecx, GUI_SCREEN_W
        mov edx, TASKBAR_H
        mov esi, [gui_theme + THEME_TASKBAR_BG]
        call gui_bb_fill_rect

        ; Top accent line
        mov eax, 0
        mov ebx, TASKBAR_Y
        mov ecx, GUI_SCREEN_W
        mov edx, [gui_theme + THEME_ACCENT]
        call gui_bb_hline

        ; Menu button (3D)
        mov eax, 4
        mov ebx, TASKBAR_Y + 4
        mov ecx, 64
        mov edx, 20
        mov esi, [gui_theme + THEME_ACCENT]
        call gui_bb_button_3d

        ; Menu button text
        mov eax, 12
        mov ebx, TASKBAR_Y + 6
        mov esi, gui_str_menu
        mov edx, 0x00FFFFFF
        call gui_bb_draw_string

        ; Clock
        call gui_read_time
        call gui_format_time
        mov eax, GUI_SCREEN_W - 56
        mov ebx, TASKBAR_Y + 6
        mov esi, gui_clock_buf
        mov edx, 0x00FFFFFF
        call gui_bb_draw_string

        ; --- Draw window buttons on taskbar ---
        mov dword [.tb_x], 76       ; start after menu button
        xor ecx, ecx               ; window index
.tb_loop:
        cmp ecx, MAX_WINDOWS
        jge .tb_end
        mov eax, ecx
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        test dword [eax + WIN_FLAGS], 0x01  ; active?
        jz .tb_next

        ; Draw button background
        push ecx
        push eax
        mov eax, [.tb_x]
        mov ebx, TASKBAR_Y + 4
        mov ecx, 80
        mov edx, 20
        ; Highlight focused window
        pop esi                     ; eax = ptr
        push esi
        mov edi, [gui_focused_win]
        imul edi, WIN_STRUCT_SIZE
        add edi, gui_win_table
        cmp esi, edi
        jne .tb_unfocused
        mov esi, [gui_theme + THEME_ACCENT]
        jmp .tb_draw_btn
.tb_unfocused:
        mov esi, 0x00444444
.tb_draw_btn:
        call gui_bb_button_3d

        ; Draw truncated title
        pop eax                     ; window entry
        push eax
        mov ebx, TASKBAR_Y + 6
        mov esi, eax
        add esi, WIN_TITLE
        mov eax, [.tb_x]
        add eax, 4
        mov edx, 0x00FFFFFF
        call gui_bb_draw_string

        pop eax
        pop ecx
        add dword [.tb_x], 84      ; button width + gap
        ; Stop if we'd overlap the clock
        cmp dword [.tb_x], GUI_SCREEN_W - 72
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
; Determines which button was clicked by X offset, toggles visibility
;---------------------------------------
gui_taskbar_btn_click:
        pushad
        mov dword [.tbc_x], 76
        xor ecx, ecx
.tbc_loop:
        cmp ecx, MAX_WINDOWS
        jge .tbc_done
        mov eax, ecx
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        test dword [eax + WIN_FLAGS], 0x01  ; active?
        jz .tbc_next

        ; Check if mouse_x is within this button
        mov edx, [.tbc_x]
        cmp dword [mouse_x], edx
        jl .tbc_skip
        add edx, 80
        cmp dword [mouse_x], edx
        jg .tbc_skip

        ; Found the button - toggle visibility or focus
        test dword [eax + WIN_FLAGS], 0x02  ; visible?
        jz .tbc_show
        ; If visible and focused, minimize; if visible but not focused, just focus
        push eax
        mov edx, [gui_focused_win]
        imul edx, WIN_STRUCT_SIZE
        add edx, gui_win_table
        cmp eax, edx
        pop eax
        jne .tbc_focus
        ; Minimize (hide)
        and dword [eax + WIN_FLAGS], ~0x02
        jmp .tbc_done
.tbc_show:
        or dword [eax + WIN_FLAGS], 0x02   ; make visible
.tbc_focus:
        push ecx
        mov eax, ecx
        call gui_focus_window
        pop ecx
        jmp .tbc_done

.tbc_skip:
        add dword [.tbc_x], 84
        cmp dword [.tbc_x], GUI_SCREEN_W - 72
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

;=======================================================================
; APP LAUNCHER MENU
;=======================================================================

gui_draw_menu:
        pushad
        ; Calculate menu Y position (above taskbar)
        mov eax, GUI_MENU_ITEMS
        imul eax, GUI_MENU_ITEM_H
        mov ebx, TASKBAR_Y
        sub ebx, eax
        mov [.dm_y], ebx

        ; Menu background
        mov eax, GUI_MENU_X
        mov ecx, GUI_MENU_W
        mov edx, GUI_MENU_ITEMS * GUI_MENU_ITEM_H
        mov esi, [gui_theme + THEME_MENU_BG]
        call gui_bb_fill_rect

        ; Menu border
        mov eax, GUI_MENU_X
        mov ebx, [.dm_y]
        mov ecx, GUI_MENU_W
        mov edx, GUI_MENU_ITEMS * GUI_MENU_ITEM_H
        mov esi, [gui_theme + THEME_WINDOW_BORDER]
        call gui_bb_rect

        ; Draw items
        xor ecx, ecx
.dm_item:
        cmp ecx, GUI_MENU_ITEMS
        jge .dm_done

        ; Item Y
        mov ebx, ecx
        imul ebx, GUI_MENU_ITEM_H
        add ebx, [.dm_y]

        ; Highlight if hovered
        cmp ecx, [gui_menu_sel]
        jne .dm_no_hl
        push ecx
        mov eax, GUI_MENU_X + 1
        inc ebx
        mov ecx, GUI_MENU_W - 2
        mov edx, GUI_MENU_ITEM_H - 1
        mov esi, [gui_theme + THEME_MENU_HIGHLIGHT]
        call gui_bb_fill_rect
        dec ebx
        pop ecx
.dm_no_hl:
        ; Draw text
        push ecx
        mov eax, GUI_MENU_X + 28
        add ebx, 4
        mov esi, [gui_menu_strings + ecx*4]
        mov edx, [gui_theme + THEME_MENU_TEXT]
        cmp ecx, [gui_menu_sel]
        jne .dm_txt_ok
        mov edx, 0x00FFFFFF
.dm_txt_ok:
        call gui_bb_draw_string
        pop ecx

        inc ecx
        jmp .dm_item
.dm_done:
        popad
        ret
.dm_y: dd 0

;---------------------------------------
; gui_menu_hit_test - Check if point is in menu
; EAX = x, EBX = y
; Returns: EAX = item index or -1
;---------------------------------------
gui_menu_hit_test:
        push ebx
        push ecx
        push edx
        ; Calculate menu top
        mov ecx, GUI_MENU_ITEMS
        imul ecx, GUI_MENU_ITEM_H
        mov edx, TASKBAR_Y
        sub edx, ecx           ; menu top y

        cmp eax, GUI_MENU_X
        jl .mh_miss
        cmp eax, GUI_MENU_X + GUI_MENU_W
        jge .mh_miss
        cmp ebx, edx
        jl .mh_miss
        cmp ebx, TASKBAR_Y
        jge .mh_miss
        ; Compute item index
        sub ebx, edx
        xor edx, edx
        mov eax, ebx
        mov ecx, GUI_MENU_ITEM_H
        div ecx             ; eax = index
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
; gui_menu_select - Execute menu item
; EAX = item index
;---------------------------------------
gui_menu_select:
        pushad
        mov byte [gui_menu_open], 0
        cmp eax, 0
        je .ms_term
        cmp eax, 1
        je .ms_files
        cmp eax, 2
        je .ms_forager
        cmp eax, 3
        je .ms_edit
        cmp eax, 4
        je .ms_calc
        cmp eax, 5
        je .ms_paint
        cmp eax, 6
        je .ms_timewarp
        cmp eax, 7
        je .ms_clock
        cmp eax, 8
        je .ms_settings
        cmp eax, 9
        je .ms_sysmon
        cmp eax, 10
        je .ms_about
        cmp eax, 11
        je .ms_exit
        popad
        ret
.ms_term:
        mov esi, gui_prog_bterm
        call gui_launch_app
        popad
        ret
.ms_files:
        mov esi, gui_prog_bhive
        call gui_launch_app
        popad
        ret
.ms_forager:
        mov esi, gui_prog_bforager
        call gui_launch_app
        popad
        ret
.ms_edit:
        mov esi, gui_prog_bedit
        call gui_launch_app
        popad
        ret
.ms_calc:
        mov esi, gui_prog_bcalc
        call gui_launch_app
        popad
        ret
.ms_paint:
        mov esi, gui_prog_bpaint
        call gui_launch_app
        popad
        ret
.ms_timewarp:
        mov esi, gui_prog_timewarp
        call gui_launch_app
        popad
        ret
.ms_clock:
        popad
        call gui_applet_clock
        ret
.ms_settings:
        popad
        call gui_applet_settings
        ret
.ms_sysmon:
        mov esi, gui_prog_bsysmon
        call gui_launch_app
        popad
        ret
.ms_about:
        popad
        call gui_applet_about
        ret
.ms_exit:
        mov byte [gui_exit_flag], 1
        popad
        ret

;---------------------------------------
; gui_launch_app - Launch external program
; ESI = program name
;---------------------------------------
gui_launch_app:
        call cmd_exec_program
        ; Only reached if program not found
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
        ; If menu is open, check menu hit
        cmp byte [gui_menu_open], 1
        jne .hi_check_menu_btn

        mov eax, [mouse_x]
        mov ebx, [mouse_y]
        call gui_menu_hit_test
        cmp eax, -1
        je .hi_close_menu
        call gui_menu_select
        jmp .hi_update
.hi_close_menu:
        mov byte [gui_menu_open], 0
        jmp .hi_update

.hi_check_menu_btn:
        ; Check Menu button area (x:4-68, y:TASKBAR_Y+4..+24)
        cmp dword [mouse_x], 4
        jl .hi_check_win
        cmp dword [mouse_x], 68
        jg .hi_check_win
        cmp dword [mouse_y], TASKBAR_Y + 4
        jl .hi_check_win
        cmp dword [mouse_y], TASKBAR_Y + 24
        jg .hi_check_win
        xor byte [gui_menu_open], 1
        mov dword [gui_menu_sel], -1
        jmp .hi_update

.hi_check_win:
        ; Check taskbar window buttons (y in TASKBAR_Y+4 .. TASKBAR_Y+24)
        cmp dword [mouse_y], TASKBAR_Y + 4
        jl .hi_check_icons
        cmp dword [mouse_y], TASKBAR_Y + 24
        jg .hi_check_icons
        cmp dword [mouse_x], 76
        jl .hi_check_icons
        call gui_taskbar_btn_click
        jmp .hi_update

.hi_check_icons:
        ; Check desktop icon clicks first
        mov eax, [mouse_x]
        mov ebx, [mouse_y]
        call gui_icon_hit_test
        cmp eax, -1
        je .hi_check_win2
        ; Launch the app for this icon
        call gui_icon_launch
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
        jne .hi_check_release
        call gui_handle_drag

.hi_check_release:
        ; Mouse release ends drag
        test cl, 0x01
        jnz .hi_hover
        mov byte [gui_dragging], 0

.hi_hover:
        ; Update menu hover
        cmp byte [gui_menu_open], 1
        jne .hi_ctx_hover
        mov eax, [mouse_x]
        mov ebx, [mouse_y]
        call gui_menu_hit_test
        mov [gui_menu_sel], eax
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

        ; Check keyboard
        call kb_pollchar
        cmp al, 0
        je .hi_done
        cmp al, 27             ; ESC
        je .hi_esc
        cmp al, 0x8E           ; Alt+Tab (special code)
        je .hi_alt_tab
        jmp .hi_done

.hi_alt_tab:
        ; Cycle to next visible window
        call gui_cycle_window
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
        mov byte [gui_menu_open], 0
        jmp .hi_done
.hi_exit:
        mov byte [gui_exit_flag], 1
.hi_done:
        popad
        ret
.hi_latch: db 0

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
        test dword [ebx + WIN_FLAGS], 0x04  ; bit2 = maximized
        jnz .wc_restore

        ; Save current geometry then maximize
        mov ecx, [ebx + WIN_X]
        mov [ebx + WIN_SAVE_X], ecx
        mov ecx, [ebx + WIN_Y]
        mov [ebx + WIN_SAVE_Y], ecx
        mov ecx, [ebx + WIN_W]
        mov [ebx + WIN_SAVE_W], ecx
        mov ecx, [ebx + WIN_H]
        mov [ebx + WIN_SAVE_H], ecx

        ; Set maximized position/size (fill desktop minus taskbar)
        mov dword [ebx + WIN_X], BORDER_W
        mov dword [ebx + WIN_Y], TITLEBAR_H
        mov dword [ebx + WIN_W], GUI_SCREEN_W - 2 * BORDER_W
        mov dword [ebx + WIN_H], TASKBAR_Y - TITLEBAR_H - BORDER_W
        or dword [ebx + WIN_FLAGS], 0x04
        jmp .wc_done

.wc_restore:
        ; Restore saved geometry
        mov ecx, [ebx + WIN_SAVE_X]
        mov [ebx + WIN_X], ecx
        mov ecx, [ebx + WIN_SAVE_Y]
        mov [ebx + WIN_Y], ecx
        mov ecx, [ebx + WIN_SAVE_W]
        mov [ebx + WIN_W], ecx
        mov ecx, [ebx + WIN_SAVE_H]
        mov [ebx + WIN_H], ecx
        and dword [ebx + WIN_FLAGS], ~0x04
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
        ; --- Check title bar (start drag) ---
        mov ecx, [ebx + WIN_Y]
        sub ecx, TITLEBAR_H
        cmp dword [mouse_y], ecx
        jl .wc_done
        mov edx, [ebx + WIN_Y]
        cmp [mouse_y], edx
        jge .wc_done

        ; Start drag
        mov byte [gui_dragging], 1
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
        mov [eax + WIN_X], ebx

        mov ebx, [mouse_y]
        sub ebx, [gui_drag_off_y]
        add ebx, TITLEBAR_H
        mov [eax + WIN_Y], ebx
.dr_done:
        popad
        ret

;=======================================================================
; RIGHT-CLICK CONTEXT MENU
;=======================================================================

GUI_CTX_W       equ 140
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

        ; Border
        mov eax, [.ctx_dx]
        mov ebx, [.ctx_dy]
        push ecx
        push edx
        mov ecx, GUI_CTX_W
        mov edx, GUI_CTX_ITEMS * GUI_CTX_ITEM_H
        mov esi, [gui_theme + THEME_WINDOW_BORDER]
        call gui_bb_rect
        pop edx
        pop ecx

        ; Draw items
        xor ecx, ecx
.ctx_item:
        cmp ecx, GUI_CTX_ITEMS
        jge .ctx_done

        mov ebx, ecx
        imul ebx, GUI_CTX_ITEM_H
        add ebx, [.ctx_dy]

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
        je .cx_about
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
.cx_about:
        popad
        call gui_applet_about
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
        mov edx, 200
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
        lea eax, [ecx + 70]
        lea ebx, [edx + 20]
        mov esi, gui_str_about_l1
        mov edx, [gui_theme + THEME_ACCENT]
        call gui_bb_draw_string

        ; Line 2: "v3.0.0"
        mov edx, [ebp + WIN_Y]
        lea eax, [ecx + 100]
        lea ebx, [edx + 44]
        mov esi, gui_str_about_l2
        mov edx, 0x00606060
        call gui_bb_draw_string

        ; Line 3: "Burrows Desktop"
        mov edx, [ebp + WIN_Y]
        lea eax, [ecx + 50]
        lea ebx, [edx + 76]
        mov esi, gui_str_about_l3
        mov edx, 0x00000000
        call gui_bb_draw_string

        ; Line 4: "32-bit Protected Mode"
        mov edx, [ebp + WIN_Y]
        lea eax, [ecx + 30]
        lea ebx, [edx + 100]
        mov esi, gui_str_about_l4
        mov edx, 0x00404040
        call gui_bb_draw_string

        ; Line 5: "HBFS / 2 GB"
        mov edx, [ebp + WIN_Y]
        lea eax, [ecx + 60]
        lea ebx, [edx + 124]
        mov esi, gui_str_about_l5
        mov edx, 0x00404040
        call gui_bb_draw_string

        ; Line 6: "Press ESC to close"
        mov edx, [ebp + WIN_Y]
        lea eax, [ecx + 50]
        lea ebx, [edx + 164]
        mov esi, gui_str_about_l6
        mov edx, 0x00808080
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
        ; Button background
        lea eax, [ecx + 80]
        lea ebx, [edx + 88]
        mov ecx, 88
        mov edx, 20
        mov esi, 0x00505560
        call gui_bb_fill_rect
        ; Button text
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 84]
        lea ebx, [edx + 90]
        mov esi, gui_str_clk_set
        mov edx, 0x00FFFFFF
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

        ; Apply / Cancel buttons
        ; Apply button bg
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 40]
        lea ebx, [edx + 96]
        mov ecx, 64
        mov edx, 20
        mov esi, 0x00388E3C
        call gui_bb_fill_rect
        ; Apply text
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 48]
        lea ebx, [edx + 98]
        mov esi, gui_str_clk_apply
        mov edx, 0x00FFFFFF
        call gui_bb_draw_string

        ; Cancel button bg
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 144]
        lea ebx, [edx + 96]
        mov ecx, 72
        mov edx, 20
        mov esi, 0x00C62828
        call gui_bb_fill_rect
        ; Cancel text
        mov ecx, [.clk_wx]
        mov edx, [.clk_wy]
        lea eax, [ecx + 152]
        lea ebx, [edx + 98]
        mov esi, gui_str_clk_cancel
        mov edx, 0x00FFFFFF
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

        ; Theme buttons (3 rows)
        mov edx, [ebp + WIN_Y]

        ; Blue theme button
        lea eax, [ecx + 20]
        lea ebx, [edx + 50]
        push ecx
        push edx
        mov ecx, 100
        mov edx, 36
        mov esi, 0x003060A0
        call gui_bb_fill_rect
        pop edx
        pop ecx
        lea eax, [ecx + 36]
        push edx
        lea ebx, [edx + 60]
        mov esi, gui_str_theme_blue
        mov edx, 0x00FFFFFF
        call gui_bb_draw_string
        pop edx

        ; Dark theme button
        lea eax, [ecx + 130]
        lea ebx, [edx + 50]
        push ecx
        push edx
        mov ecx, 100
        mov edx, 36
        mov esi, 0x00303030
        call gui_bb_fill_rect
        pop edx
        pop ecx
        lea eax, [ecx + 150]
        push edx
        lea ebx, [edx + 60]
        mov esi, gui_str_theme_dark
        mov edx, 0x00D0D0D0
        call gui_bb_draw_string
        pop edx

        ; Light theme button
        lea eax, [ecx + 240]
        lea ebx, [edx + 50]
        push ecx
        push edx
        mov ecx, 100
        mov edx, 36
        mov esi, 0x00B0C4DE
        call gui_bb_fill_rect
        pop edx
        pop ecx
        lea eax, [ecx + 256]
        push edx
        lea ebx, [edx + 60]
        mov esi, gui_str_theme_light
        mov edx, 0x00000000
        call gui_bb_draw_string
        pop edx

        ; Amber theme button
        lea eax, [ecx + 20]
        lea ebx, [edx + 96]
        push ecx
        push edx
        mov ecx, 100
        mov edx, 36
        mov esi, 0x00CC8800
        call gui_bb_fill_rect
        pop edx
        pop ecx
        lea eax, [ecx + 36]
        push edx
        lea ebx, [edx + 106]
        mov esi, gui_str_theme_amber
        mov edx, 0x00FFCC00
        call gui_bb_draw_string
        pop edx

        ; Current theme preview (color bar)
        lea eax, [ecx + 20]
        lea ebx, [edx + 150]
        push ecx
        push edx
        mov ecx, 320
        mov edx, 40
        mov esi, [gui_theme + THEME_DESKTOP_BG]
        call gui_bb_fill_rect
        pop edx
        pop ecx

        lea eax, [ecx + 100]
        lea ebx, [edx + 162]
        mov esi, gui_str_set_cur
        push edx
        mov edx, 0x00FFFFFF
        call gui_bb_draw_string
        pop edx

        ; Instructions
        lea eax, [ecx + 40]
        lea ebx, [edx + 210]
        mov esi, gui_str_set_instr
        mov edx, 0x00808080
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
        mov eax, -1
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
        ; Mark window as app-owned
        cmp eax, -1
        je .sg_cr_done
        push eax
        imul eax, WIN_STRUCT_SIZE
        mov dword [gui_win_table + eax + WIN_APP_ID], 1
        pop eax
.sg_cr_done:
        iretd

.sg_destroy:
        ; ECX = win_id
        mov eax, ecx
        call gui_destroy_window_n
        xor eax, eax
        iretd

.sg_fill:
        ; ECX = win_id, EDX = x|y, ESI = w|h, EDI = color
        pushad
        mov eax, ecx
        cmp eax, MAX_WINDOWS
        jge .sg_f_done
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        test dword [eax + WIN_FLAGS], 0x01
        jz .sg_f_done
        ; Convert to screen coords
        mov ebx, edx
        shr ebx, 16
        add ebx, [eax + WIN_X]
        movzx ecx, dx
        add ecx, [eax + WIN_Y]
        mov eax, ebx
        mov ebx, ecx
        mov ecx, esi
        shr ecx, 16
        movzx edx, si
        mov esi, edi
        call gui_bb_fill_rect
.sg_f_done:
        popad
        xor eax, eax
        iretd

.sg_text:
        ; ECX = win_id, EDX = x|y, ESI = text, EDI = color
        pushad
        mov eax, ecx
        cmp eax, MAX_WINDOWS
        jge .sg_t_done
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        test dword [eax + WIN_FLAGS], 0x01
        jz .sg_t_done
        mov ebx, edx
        shr ebx, 16
        add ebx, [eax + WIN_X]
        movzx ecx, dx
        add ecx, [eax + WIN_Y]
        mov eax, ebx
        mov ebx, ecx
        mov edx, edi
        call gui_bb_draw_string
.sg_t_done:
        popad
        xor eax, eax
        iretd

.sg_poll:
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
        xor eax, eax
        iretd
.sg_p_key:
        movzx ebx, al
        mov eax, EVT_KEY_PRESS
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
        popad
        xor eax, eax
        iretd

.sg_pixel:
        ; ECX = win_id, EDX = x|y, ESI = color
        pushad
        mov eax, ecx
        cmp eax, MAX_WINDOWS
        jge .sg_px_done
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        test dword [eax + WIN_FLAGS], 0x01
        jz .sg_px_done
        mov ebx, edx
        shr ebx, 16
        add ebx, [eax + WIN_X]
        movzx ecx, dx
        add ecx, [eax + WIN_Y]
        mov eax, ebx
        mov ebx, ecx
        mov ecx, esi
        call gui_bb_pixel
.sg_px_done:
        popad
        xor eax, eax
        iretd

.sg_compose:
        pushad
        call gui_draw_background
        call gui_draw_all_windows
        call gui_draw_taskbar
        popad
        xor eax, eax
        iretd

.sg_flip:
        pushad
        call gui_draw_cursor
        call gui_flip
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
        mov eax, ecx
        imul eax, WIN_STRUCT_SIZE
        add eax, gui_win_table
        test dword [eax + WIN_FLAGS], 0x01
        jz .wrw_fail
        mov ebx, [eax + WIN_Y]
        mov eax, [eax + WIN_X]
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

gui_theme_blue:
        dd 0x00285078           ; desktop bg
        dd 0x00303030           ; taskbar bg
        dd 0x003060A0           ; title active
        dd 0x00808080           ; title inactive
        dd 0x00FFFFFF           ; title text
        dd 0x00F0F0F0           ; window bg
        dd 0x00606060           ; window border
        dd 0x00F8F8F8           ; menu bg
        dd 0x00000000           ; menu text
        dd 0x003060A0           ; menu highlight
        dd 0x00D0D0D0           ; button bg
        dd 0x003060A0           ; accent

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
gui_str_about:      db "About", 0
gui_str_clock:      db "Clock", 0
gui_str_settings:   db "Settings", 0

gui_str_about_l1:   db "Mellivora OS", 0
gui_str_about_l2:   db "v3.0.0", 0
gui_str_about_l3:   db "Burrows Desktop Environment", 0
gui_str_about_l4:   db "32-bit Protected Mode / i486+", 0
gui_str_about_l5:   db "HBFS Filesystem / 2 GB", 0
gui_str_about_l6:   db "Press ESC to close", 0

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
gui_str_theme_blue: db "Blue", 0
gui_str_theme_dark: db "Dark", 0
gui_str_theme_light: db "Light", 0
gui_str_theme_amber: db "Amber", 0
gui_str_set_cur:    db "Current Theme", 0
gui_str_set_instr:  db "Click or press 1/2/3/4", 0

; Menu item strings
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
gui_str_m_about:    db "About", 0
gui_str_m_exit:     db "Exit Desktop", 0

gui_menu_strings:
        dd gui_str_m_term
        dd gui_str_m_files
        dd gui_str_m_forager
        dd gui_str_m_edit
        dd gui_str_m_calc
        dd gui_str_m_paint
        dd gui_str_m_timewarp
        dd gui_str_m_clock
        dd gui_str_m_settings
        dd gui_str_m_sysmon
        dd gui_str_m_about
        dd gui_str_m_exit

; Program names for external apps
gui_prog_bterm:     db "bterm", 0
gui_prog_bhive:     db "bhive", 0
gui_prog_bforager:  db "bforager", 0
gui_prog_bedit:     db "bedit", 0
gui_prog_bcalc:     db "bcalc", 0
gui_prog_bpaint:    db "bpaint", 0
gui_prog_bsysmon:   db "bsysmon", 0
gui_prog_timewarp:  db "timewarp", 0

; Desktop icon data
gui_icon_symbols:   db 'T', 'H', 'W', 'E', 'C', 'P', 'Z', 'M', 0

gui_icon_labels:
        dd gui_str_ic_term
        dd gui_str_ic_files
        dd gui_str_ic_forager
        dd gui_str_ic_edit
        dd gui_str_ic_calc
        dd gui_str_ic_paint
        dd gui_str_ic_timewarp
        dd gui_str_ic_sysmon

gui_icon_progs:
        dd gui_prog_bterm
        dd gui_prog_bhive
        dd gui_prog_bforager
        dd gui_prog_bedit
        dd gui_prog_bcalc
        dd gui_prog_bpaint
        dd gui_prog_timewarp
        dd gui_prog_bsysmon

gui_str_ic_term:    db "BTerm", 0
gui_str_ic_files:   db "BHive", 0
gui_str_ic_forager: db "BForager", 0
gui_str_ic_edit:    db "BEdit", 0
gui_str_ic_calc:    db "BCalc", 0
gui_str_ic_paint:   db "BPaint", 0
gui_str_ic_timewarp: db "Time Warp", 0
gui_str_ic_sysmon:  db "BSysMon", 0

; Right-click context menu strings
gui_str_ctx_term:   db "Open BTerm", 0
gui_str_ctx_files:  db "Open BHive", 0
gui_str_ctx_about:  db "About", 0
gui_str_ctx_set:    db "Settings", 0
gui_str_ctx_exit:   db "Exit Desktop", 0

gui_ctx_strings:
        dd gui_str_ctx_term
        dd gui_str_ctx_files
        dd gui_str_ctx_about
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

;---------------------------------------
; gui_notify - Post a notification toast
;  ESI = notification text string (copied, max 60 chars)
;  EDX = color (0=default blue)
;---------------------------------------
gui_notify:
        pushad
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

        ; Draw dark background
        mov eax, [.dn_tmp_x]
        mov ebx, [.dn_tmp_y]
        mov ecx, NOTIFY_W
        mov edx, NOTIFY_H
        mov esi, 0x00333344
        call gui_bb_fill_rect

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
        mov edx, 0x00FFFFFF
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

;=======================================================================
; STATE VARIABLES (initialized, in binary image)
;=======================================================================

gui_desktop_active: db 0
gui_exit_flag:      db 0
gui_menu_open:      db 0
gui_ctx_open:       db 0
gui_dragging:       db 0
gui_drag_win:       dd 0
gui_drag_off_x:     dd 0
gui_drag_off_y:     dd 0
gui_focused_win:    dd -1
gui_win_count:      dd 0
gui_next_z:         dd 0
gui_menu_sel:       dd -1
gui_ctx_sel:        dd -1
gui_ctx_x:          dd 0
gui_ctx_y:          dd 0
gui_prev_mouse_x:   dd 0
gui_prev_mouse_y:   dd 0
gui_prev_mouse_btn: db 0
                    db 0, 0, 0  ; padding
gui_clock_buf:      db "00:00", 0, 0, 0

; Gradient title bar temporaries
gui_grad_base:      dd 0
gui_grad_x:         dd 0
gui_grad_y:         dd 0
gui_grad_w:         dd 0
gui_grad_row:       dd 0
gui_grad_factor:    dd 0
gui_grad_color:     dd 0

; Runtime theme (48 bytes, copied from preset at init)
gui_theme:          times THEME_SIZE db 0

; Window table (16 windows x 96 bytes = 1536 bytes)
gui_win_table:      times MAX_WINDOWS * WIN_STRUCT_SIZE db 0
