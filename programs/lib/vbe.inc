;=======================================================================
; vbe.inc - Mellivora VBE Pixel Graphics Library (user-space)
;
; Drawing routines that operate on the VBE 32-bpp shadow buffer.
;
; Setup (required before calling any routine here):
;   1. Call SYS_FRAMEBUF (37), EBX=1  to enter VBE mode
;      (ECX=width, EDX=height, ESI=32)
;   2. Call SYS_FRAMEBUF (37), EBX=0  to query mode
;      Returns: EAX=shadow_buf, EBX=width, ECX=height, EDX=bpp
;   3. Populate the BSS vars declared at the end of this file:
;        mov [fb_addr],   eax
;        mov [fb_width],  ebx
;        mov [fb_height], ecx
;        ; fb_pitch = width * 4  (32-bpp, no row padding)
;        shl ebx, 2
;        mov [fb_pitch],  ebx
;
; All drawing targets the shadow buffer.  Call vbe_present (or
; SYS_FRAMEBUF/4 directly) to flip shadow -> LFB.
;
; Register contract:
;   All routines preserve every general-purpose register (pushad/popad).
;   Colour is always 0x00RRGGBB (32-bit, high byte ignored).
;
; Functions:
;   vbe_plot_pixel    EBX=x   ECX=y   EDX=colour
;   vbe_clear_screen  EDX=colour
;   vbe_fill_rect     EBX=x   ECX=y   EDX=w   ESI=h   EDI=colour
;   vbe_draw_hline    EBX=x   ECX=y   EDX=w   ESI=colour
;   vbe_draw_vline    EBX=x   ECX=y   EDX=h   ESI=colour
;   vbe_draw_line     EBX=x1  ECX=y1  EDX=x2  ESI=y2  EDI=colour
;   vbe_draw_circle   EBX=cx  ECX=cy  EDX=r   ESI=colour
;   vbe_present       (no arguments — blits shadow -> LFB)
;=======================================================================

%ifndef VBE_INC
%define VBE_INC

;-----------------------------------------------------------------------
; vbe_plot_pixel - Draw a single pixel in the shadow buffer
; Input:  EBX = x,  ECX = y,  EDX = colour
;-----------------------------------------------------------------------
vbe_plot_pixel:
        cmp ebx, [fb_width]
        jae .vpp_done           ; unsigned — catches negatives & out-of-range
        cmp ecx, [fb_height]
        jae .vpp_done
        pushad
        mov eax, ecx
        imul eax, [fb_pitch]    ; y * pitch  (2-op imul; result stays in EAX)
        mov edi, [fb_addr]
        add edi, eax            ; fb_addr + y*pitch
        lea edi, [edi + ebx*4]  ; + x*4
        mov [edi], edx          ; write colour
        popad
.vpp_done:
        ret

;-----------------------------------------------------------------------
; vbe_clear_screen - Fill the entire shadow buffer with one colour
; Input:  EDX = colour
;-----------------------------------------------------------------------
vbe_clear_screen:
        pushad
        mov edi, [fb_addr]
        mov eax, edx            ; colour
        mov ecx, [fb_height]
        imul ecx, [fb_width]    ; total pixels (32-bpp: pitch = width*4)
        rep stosd
        popad
        ret

;-----------------------------------------------------------------------
; vbe_fill_rect - Fill an axis-aligned rectangle
; Input:  EBX = x,  ECX = y,  EDX = width,  ESI = height,  EDI = colour
;
; Stack after pushad (PUSHAD order: EAX ECX EDX EBX ESP EBP ESI EDI):
;   [esp+ 0] = EDI (colour)   [esp+ 4] = ESI (height)
;   [esp+16] = EBX (x)        [esp+20] = EDX (width)
;   [esp+24] = ECX (y)
;-----------------------------------------------------------------------
vbe_fill_rect:
        pushad
        mov eax, ecx
        imul eax, [fb_pitch]    ; y * pitch
        mov esi, [fb_addr]
        add esi, eax
        lea esi, [esi + ebx*4]  ; ESI = row-start pointer

        mov ebp, [esp+4]        ; h  (row count)
        mov edx, [esp+20]       ; w  (pixels per row)
        mov eax, [esp+0]        ; colour
.vfr_row:
        test ebp, ebp
        jz   .vfr_done
        mov  edi, esi           ; destination for this row
        mov  ecx, edx           ; pixel count
        rep  stosd              ; fill one row
        add  esi, [fb_pitch]    ; advance to next row
        dec  ebp
        jmp  .vfr_row
.vfr_done:
        popad
        ret

;-----------------------------------------------------------------------
; vbe_draw_hline - Draw a horizontal line
; Input:  EBX = x,  ECX = y,  EDX = width,  ESI = colour
;
; [esp+ 4] = ESI (colour)   [esp+20] = EDX (width)
;-----------------------------------------------------------------------
vbe_draw_hline:
        pushad
        mov eax, ecx
        imul eax, [fb_pitch]
        mov edi, [fb_addr]
        add edi, eax
        lea edi, [edi + ebx*4]
        mov eax, [esp+4]        ; colour
        mov ecx, [esp+20]       ; width
        rep stosd
        popad
        ret

;-----------------------------------------------------------------------
; vbe_draw_vline - Draw a vertical line
; Input:  EBX = x,  ECX = y,  EDX = height,  ESI = colour
;
; [esp+20] = EDX (height)   [esp+ 4] = ESI (colour)
;-----------------------------------------------------------------------
vbe_draw_vline:
        pushad
        mov eax, ecx
        imul eax, [fb_pitch]
        mov edi, [fb_addr]
        add edi, eax
        lea edi, [edi + ebx*4]
        mov ecx, [esp+20]       ; height
        mov eax, [esp+4]        ; colour
.vdv_loop:
        test ecx, ecx
        jz   .vdv_done
        mov  [edi], eax
        add  edi, [fb_pitch]
        dec  ecx
        jmp  .vdv_loop
.vdv_done:
        popad
        ret

;-----------------------------------------------------------------------
; vbe_draw_line - Draw an arbitrary line (Bresenham)
; Input:  EBX = x1,  ECX = y1,  EDX = x2,  ESI = y2,  EDI = colour
;
; Stack frame (EBP-relative, allocated before pushad):
;   [ebp- 4] = |dx|    [ebp- 8] = |dy|
;   [ebp-12] = sx      [ebp-16] = sy
;   [ebp-20] = err     [ebp-24] = cur_x
;   [ebp-28] = cur_y   [ebp-32] = end_x
;   [ebp-36] = end_y   [ebp-40] = colour
;-----------------------------------------------------------------------
vbe_draw_line:
        push ebp
        mov  ebp, esp
        sub  esp, 40
        pushad

        mov [ebp-24], ebx       ; cur_x = x1
        mov [ebp-28], ecx       ; cur_y = y1
        mov [ebp-32], edx       ; end_x = x2
        mov [ebp-36], esi       ; end_y = y2
        mov [ebp-40], edi       ; colour

        ; |dx| and sx
        mov eax, edx
        sub eax, ebx            ; dx = x2 - x1
        jns .vdl_pos_dx
        neg eax
        mov dword [ebp-12], -1  ; sx = -1
        jmp .vdl_done_dx
.vdl_pos_dx:
        mov dword [ebp-12], 1   ; sx = +1
.vdl_done_dx:
        mov [ebp-4], eax        ; |dx|

        ; |dy| and sy
        mov eax, esi
        sub eax, ecx            ; dy = y2 - y1
        jns .vdl_pos_dy
        neg eax
        mov dword [ebp-16], -1  ; sy = -1
        jmp .vdl_done_dy
.vdl_pos_dy:
        mov dword [ebp-16], 1   ; sy = +1
.vdl_done_dy:
        mov [ebp-8], eax        ; |dy|

        ; err = |dx| - |dy|
        mov eax, [ebp-4]
        sub eax, [ebp-8]
        mov [ebp-20], eax

.vdl_loop:
        ; plot (cur_x, cur_y)
        mov ebx, [ebp-24]
        mov ecx, [ebp-28]
        mov edx, [ebp-40]
        call vbe_plot_pixel

        ; stop if we have reached the end point
        mov eax, [ebp-24]
        cmp eax, [ebp-32]
        jne .vdl_step
        mov eax, [ebp-28]
        cmp eax, [ebp-36]
        je  .vdl_exit

.vdl_step:
        mov eax, [ebp-20]
        add eax, eax            ; e2 = 2 * err

        ; if e2 > -|dy|: err -= |dy|; x += sx
        mov ebx, [ebp-8]
        neg ebx                 ; -|dy|
        cmp eax, ebx
        jle .vdl_check_dy
        mov ecx, [ebp-8]
        sub [ebp-20], ecx       ; err -= |dy|
        mov ecx, [ebp-12]
        add [ebp-24], ecx       ; x += sx

.vdl_check_dy:
        ; if e2 < |dx|: err += |dx|; y += sy
        ; Note: EAX still holds the original e2 (both conditions use same e2)
        mov ebx, [ebp-4]        ; |dx|
        cmp eax, ebx
        jge .vdl_loop
        add [ebp-20], ebx       ; err += |dx|
        mov ecx, [ebp-16]
        add [ebp-28], ecx       ; y += sy
        jmp .vdl_loop

.vdl_exit:
        popad
        mov esp, ebp
        pop ebp
        ret

;-----------------------------------------------------------------------
; vbe_draw_circle - Draw a hollow circle (Bresenham midpoint)
; Input:  EBX = cx,  ECX = cy,  EDX = radius,  ESI = colour
;
; Stack frame:
;   [ebp- 4] = cx    [ebp- 8] = cy
;   [ebp-12] = px    [ebp-16] = py
;   [ebp-20] = p     [ebp-24] = colour
;
; Uses local label .vdc_plot8 to draw all 8 symmetric octant points.
; That inner routine expects: EAX=px, ESI=py, EDI=cx; reads [ebp-8] and
; [ebp-24] directly via the still-valid EBP frame pointer.
;-----------------------------------------------------------------------
vbe_draw_circle:
        push ebp
        mov  ebp, esp
        sub  esp, 24
        pushad

        mov [ebp-4],  ebx       ; cx
        mov [ebp-8],  ecx       ; cy
        mov [ebp-12], edx       ; px = radius
        mov dword [ebp-16], 0   ; py = 0
        ; decision parameter p = 1 - radius
        mov eax, 1
        sub eax, edx
        mov [ebp-20], eax
        mov [ebp-24], esi       ; colour

.vdc_loop:
        ; while px >= py
        mov eax, [ebp-16]       ; py
        cmp [ebp-12], eax       ; px >= py ?
        jl  .vdc_exit

        ; plot 8 octants
        mov eax, [ebp-12]       ; px
        mov esi, [ebp-16]       ; py
        mov edi, [ebp-4]        ; cx
        call .vdc_plot8

        ; py++
        inc dword [ebp-16]
        mov esi, [ebp-16]       ; new py

        cmp dword [ebp-20], 0
        jl  .vdc_neg_p

        ; p >= 0: px--, p += 2*(py-px) + 1
        dec dword [ebp-12]
        mov eax, esi            ; py
        sub eax, [ebp-12]       ; py - px
        add eax, eax            ; 2*(py-px)
        inc eax                 ; + 1
        add [ebp-20], eax
        jmp .vdc_loop

.vdc_neg_p:
        ; p < 0: p += 2*py + 1
        mov eax, esi            ; py
        add eax, eax            ; 2*py
        inc eax
        add [ebp-20], eax
        jmp .vdc_loop

.vdc_exit:
        popad
        mov esp, ebp
        pop ebp
        ret

        ; -----------------------------------------------------------------
        ; .vdc_plot8 — plot 8 symmetric octant points
        ; Called with: EAX=px, ESI=py, EDI=cx, EBP→frame ([ebp-8]=cy,
        ;              [ebp-24]=colour).  Preserves all registers via
        ;              vbe_plot_pixel's own pushad/popad contract.
        ; -----------------------------------------------------------------
.vdc_plot8:
        mov edx, [ebp-24]       ; colour — preserved across each call

        ; (cx+px, cy+py)
        mov ebx, edi
        add ebx, eax
        mov ecx, [ebp-8]
        add ecx, esi
        call vbe_plot_pixel

        ; (cx-px, cy+py)
        mov ebx, edi
        sub ebx, eax
        mov ecx, [ebp-8]
        add ecx, esi
        call vbe_plot_pixel

        ; (cx+px, cy-py)
        mov ebx, edi
        add ebx, eax
        mov ecx, [ebp-8]
        sub ecx, esi
        call vbe_plot_pixel

        ; (cx-px, cy-py)
        mov ebx, edi
        sub ebx, eax
        mov ecx, [ebp-8]
        sub ecx, esi
        call vbe_plot_pixel

        ; (cx+py, cy+px)  — px and py swapped for the other 4 octants
        mov ebx, edi
        add ebx, esi
        mov ecx, [ebp-8]
        add ecx, eax
        call vbe_plot_pixel

        ; (cx-py, cy+px)
        mov ebx, edi
        sub ebx, esi
        mov ecx, [ebp-8]
        add ecx, eax
        call vbe_plot_pixel

        ; (cx+py, cy-px)
        mov ebx, edi
        add ebx, esi
        mov ecx, [ebp-8]
        sub ecx, eax
        call vbe_plot_pixel

        ; (cx-py, cy-px)
        mov ebx, edi
        sub ebx, esi
        mov ecx, [ebp-8]
        sub ecx, eax
        call vbe_plot_pixel

        ret

;-----------------------------------------------------------------------
; vbe_present - Blit shadow buffer to the LFB (double-buffer flip)
; Input:  (none)
;-----------------------------------------------------------------------
vbe_present:
        push eax
        push ebx
        mov eax, 37             ; SYS_FRAMEBUF
        mov ebx, 4              ; sub-function 4: present
        int 0x80
        pop ebx
        pop eax
        ret

;-----------------------------------------------------------------------
; BSS variables — declared once via the include guard.
; Programs must populate these before calling any drawing routine:
;
;   mov eax, 37
;   mov ebx, 0          ; query
;   int 0x80
;   mov [fb_addr],   eax
;   mov [fb_width],  ebx
;   mov [fb_height], ecx
;   shl ebx, 2
;   mov [fb_pitch],  ebx
;-----------------------------------------------------------------------
fb_addr:   dd 0         ; shadow buffer virtual address
fb_pitch:  dd 0         ; bytes per row (= width * 4 for 32-bpp)
fb_width:  dd 0         ; framebuffer width  in pixels
fb_height: dd 0         ; framebuffer height in pixels

%endif ; VBE_INC
