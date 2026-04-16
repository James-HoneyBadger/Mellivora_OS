;=======================================================================
; gui.inc - Burrows GUI API for user-space programs
;
; Wrapper functions for SYS_GUI syscall sub-functions.
; Programs should: %include "lib/gui.inc"
;
; Coordinate packing: many calls use hi16:lo16 packing in registers.
;   pack_xy(x, y) = (x << 16) | (y & 0xFFFF)
;=======================================================================

;---------------------------------------
; gui_create_window - Create a new window
; EAX = x, EBX = y, ECX = w, EDX = h, ESI = title
; Returns: EAX = window id (0-15) or -1
;---------------------------------------
gui_create_window:
        PUSHALL
        ; Pack x|y into ECX, w|h into EDX
        shl eax, 16
        and ebx, 0xFFFF
        or eax, ebx         ; ECX_new = x|y
        shl ecx, 16
        and edx, 0xFFFF
        or ecx, edx         ; EDX_new = w|h
        mov edx, ecx        ; EDX = w|h
        mov ecx, eax        ; ECX = x|y
        mov eax, SYS_GUI
        mov ebx, GUI_CREATE_WINDOW
        int 0x80
        mov [esp + 28], eax ; Return window id via PUSHALL frame
        POPALL
        ret

;---------------------------------------
; gui_destroy_window - Destroy a window
; EAX = window id
;---------------------------------------
gui_destroy_window:
        push rbx
        push rcx
        mov ecx, eax
        mov eax, SYS_GUI
        mov ebx, GUI_DESTROY_WINDOW
        int 0x80
        pop rcx
        pop rbx
        ret

;---------------------------------------
; gui_fill_rect - Fill rectangle in window
; EAX = win_id, EBX = x, ECX = y, EDX = w, ESI = h, EDI = color
;---------------------------------------
gui_fill_rect:
        PUSHALL
        ; Pack args
        push rdi             ; color
        push rax             ; win_id
        shl ebx, 16
        and ecx, 0xFFFF
        or ebx, ecx         ; EDX_new = x|y
        shl edx, 16
        and esi, 0xFFFF
        or edx, esi          ; ESI_new = w|h
        mov esi, edx         ; ESI = w|h
        mov edx, ebx         ; EDX = x|y
        pop rcx              ; ECX = win_id
        pop rdi              ; EDI = color
        mov eax, SYS_GUI
        mov ebx, GUI_FILL_RECT
        int 0x80
        POPALL
        ret

;---------------------------------------
; gui_draw_text - Draw text in window
; EAX = win_id, EBX = x, ECX = y, ESI = text, EDI = color
;---------------------------------------
gui_draw_text:
        PUSHALL
        push rdi             ; color
        push rax             ; win_id
        shl ebx, 16
        and ecx, 0xFFFF
        or ebx, ecx         ; EDX_new = x|y
        mov edx, ebx
        pop rcx              ; ECX = win_id
        pop rdi              ; EDI = color
        mov eax, SYS_GUI
        mov ebx, GUI_DRAW_TEXT
        int 0x80
        POPALL
        ret

;---------------------------------------
; gui_draw_pixel - Draw pixel in window
; EAX = win_id, EBX = x, ECX = y, ESI = color
;---------------------------------------
gui_draw_pixel:
        PUSHALL
        push rax             ; win_id
        shl ebx, 16
        and ecx, 0xFFFF
        or ebx, ecx
        mov edx, ebx         ; EDX = x|y
        pop rcx              ; ECX = win_id
        mov eax, SYS_GUI
        mov ebx, GUI_DRAW_PIXEL
        int 0x80
        POPALL
        ret

;---------------------------------------
; gui_poll_event - Poll for GUI event
; Returns: EAX = event type, EBX = param1, ECX = param2
;---------------------------------------
gui_poll_event:
        push rdx
        mov eax, SYS_GUI
        mov ebx, GUI_POLL_EVENT
        int 0x80
        pop rdx
        ret

;---------------------------------------
; gui_compose - Compose desktop (bg + windows + taskbar) to backbuffer
;---------------------------------------
gui_compose:
        push rax
        push rbx
        mov eax, SYS_GUI
        mov ebx, GUI_COMPOSE
        int 0x80
        pop rbx
        pop rax
        ret

;---------------------------------------
; gui_flip - Draw cursor and flip backbuffer to screen
;---------------------------------------
gui_flip:
        push rax
        push rbx
        mov eax, SYS_GUI
        mov ebx, GUI_FLIP
        int 0x80
        pop rbx
        pop rax
        ret

;---------------------------------------
; gui_get_theme - Copy current theme to buffer
; EAX = destination buffer (48 bytes)
;---------------------------------------
gui_get_theme:
        push rbx
        push rcx
        mov ecx, eax
        mov eax, SYS_GUI
        mov ebx, GUI_GET_THEME
        int 0x80
        pop rcx
        pop rbx
        ret

;---------------------------------------
; gui_set_theme - Apply theme from buffer
; EAX = source buffer (48 bytes)
;---------------------------------------
gui_set_theme:
        push rbx
        push rcx
        mov ecx, eax
        mov eax, SYS_GUI
        mov ebx, GUI_SET_THEME
        int 0x80
        pop rcx
        pop rbx
        ret
