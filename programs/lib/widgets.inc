;=======================================================================
; widgets.inc - User-space widget drawing helpers
;
; Include after syscalls.inc and lib/gui.inc:
;   %include "lib/widgets.inc"
;
; All functions take a window ID and a parameter struct.
;=======================================================================

;---------------------------------------
; wgt_draw_button - Draw a 3D button
;   EAX = win_id
;   EBX = param_block:
;     [0]=x(w), [2]=y(w), [4]=w(w), [6]=h(w), [8]=text_ptr(d), [12]=pressed(b)
;---------------------------------------
wgt_draw_button:
        push ecx
        push esi
        mov ecx, eax
        mov esi, ebx
        mov ebx, GUI_DRAW_BUTTON
        mov eax, SYS_GUI
        int 0x80
        pop esi
        pop ecx
        ret

;---------------------------------------
; wgt_draw_checkbox - Draw a checkbox + label
;   EAX = win_id
;   EBX = param_block:
;     [0]=x(w), [2]=y(w), [4]=checked(b), [8]=text_ptr(d)
;---------------------------------------
wgt_draw_checkbox:
        push ecx
        push esi
        mov ecx, eax
        mov esi, ebx
        mov ebx, GUI_DRAW_CHECKBOX
        mov eax, SYS_GUI
        int 0x80
        pop esi
        pop ecx
        ret

;---------------------------------------
; wgt_draw_progress - Draw a progress bar
;   EAX = win_id
;   EBX = param_block:
;     [0]=x(w), [2]=y(w), [4]=w(w), [6]=h(w), [8]=value(b 0-100)
;---------------------------------------
wgt_draw_progress:
        push ecx
        push esi
        mov ecx, eax
        mov esi, ebx
        mov ebx, GUI_DRAW_PROGRESS
        mov eax, SYS_GUI
        int 0x80
        pop esi
        pop ecx
        ret

;---------------------------------------
; wgt_draw_textbox - Draw a text input box
;   EAX = win_id
;   EBX = param_block:
;     [0]=x(w), [2]=y(w), [4]=w(w), [6]=h(w), [8]=text_ptr(d), [12]=focused(b)
;---------------------------------------
wgt_draw_textbox:
        push ecx
        push esi
        mov ecx, eax
        mov esi, ebx
        mov ebx, GUI_DRAW_TEXTBOX
        mov eax, SYS_GUI
        int 0x80
        pop esi
        pop ecx
        ret

;---------------------------------------
; wgt_draw_listbox - Draw a scrollable list
;   EAX = win_id
;   EBX = param_block:
;     [0]=x(w), [2]=y(w), [4]=w(w), [6]=h(w),
;     [8]=items_ptr(d), [12]=item_count(w), [14]=selected(w),
;     [16]=scroll(w), [18]=item_len(w)
;---------------------------------------
wgt_draw_listbox:
        push ecx
        push esi
        mov ecx, eax
        mov esi, ebx
        mov ebx, GUI_DRAW_LISTBOX
        mov eax, SYS_GUI
        int 0x80
        pop esi
        pop ecx
        ret

;---------------------------------------
; wgt_draw_label - Draw a text label
;   EAX = win_id
;   EBX = param_block:
;     [0]=x(w), [2]=y(w), [4]=text_ptr(d), [8]=color(d)
;---------------------------------------
wgt_draw_label:
        push ecx
        push esi
        mov ecx, eax
        mov esi, ebx
        mov ebx, GUI_DRAW_LABEL
        mov eax, SYS_GUI
        int 0x80
        pop esi
        pop ecx
        ret

;---------------------------------------
; wgt_draw_rect - Draw a rectangle
;   EAX = win_id
;   EBX = param_block:
;     [0]=x(w), [2]=y(w), [4]=w(w), [6]=h(w), [8]=color(d), [12]=filled(b)
;---------------------------------------
wgt_draw_rect:
        push ecx
        push esi
        mov ecx, eax
        mov esi, ebx
        mov ebx, GUI_DRAW_RECT
        mov eax, SYS_GUI
        int 0x80
        pop esi
        pop ecx
        ret
