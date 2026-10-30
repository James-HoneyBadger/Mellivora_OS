;=======================================================================
; gfx.inc - the display engine
;
; Until this file existed the framebuffer had one drawing rule: store 32 bits
; at (y * pitch + x * 4). That is why every mode was 32bpp in practice -
; putpixel wrote a dword whatever the mode said, so a 16bpp or 24bpp mode drew
; garbage - and why nothing could be clipped to anything but the screen.
;
; What a 2000-era display stack gave a program, and what this gives:
;
;   * pixel formats - 8 (3-3-2), 16 (5-6-5), 24 (packed) and 32 (XRGB) - with
;     one packing point, so every primitive is format-correct by construction;
;   * a clip rectangle, set by the shell window or the program, honoured by
;     every primitive - the desktop used to clip by testing every pixel;
;   * colour-key blitting, which is how a 2000-era icon or sprite was drawn;
;   * a gamma/brightness lookup per channel, applied in the packing point, the
;     same place a VGA RAMDAC applied it;
;   * a save-under software cursor - the shape a hardware cursor took on a
;     Matrox or Rage card, done in memory because the emulated adapter has no
;     cursor engine;
;   * the vertical retrace, so a flip can be held to it.
;
; What it does not give, and why: hardware 2D acceleration (the emulated
; adapter has no blitter, so this is a fast software engine with the same API a
; real driver would implement), page flipping by register (the shadow buffer is
; copied), EDID/DDC and DPMS (both are VBE BIOS calls, and this kernel has no
; real-mode thunk to make them from).
;=======================================================================

GFX_MAX_CURSOR_PIXELS equ (32 * 32)

gfx_target      dd 0            ; where pixels go (shadow buffer, or the LFB)
gfx_pitch       dd 0            ; bytes per row
gfx_w           dd 0
gfx_h           dd 0
gfx_bpp         dd 0            ; 8, 16, 24 or 32
gfx_bytepp      dd 0            ; bpp / 8
gfx_clip_x0     dd 0
gfx_clip_y0     dd 0
gfx_clip_x1     dd 0
gfx_clip_y1     dd 0

gfx_gamma_on    db 0
gfx_gamma_r     times 256 db 0
gfx_gamma_g     times 256 db 0
gfx_gamma_b     times 256 db 0

gfx_cursor_on   db 0
gfx_cursor_w    dd 0
gfx_cursor_h    dd 0
gfx_cursor_x    dd 0
gfx_cursor_y    dd 0
gfx_cursor_drawn db 0
gfx_cursor_pixels times GFX_MAX_CURSOR_PIXELS dd 0   ; ARGB, key already stripped
gfx_cursor_under  times GFX_MAX_CURSOR_PIXELS dd 0   ; native pixels saved under it

;-----------------------------------------------------------------------
; gfx_gamma_identity - fill the three tables with 0..255
;-----------------------------------------------------------------------
gfx_gamma_identity:
        pushad
        xor ecx, ecx
.gi_loop:
        mov [gfx_gamma_r + ecx], cl
        mov [gfx_gamma_g + ecx], cl
        mov [gfx_gamma_b + ecx], cl
        inc ecx
        cmp ecx, 256
        jb .gi_loop
        mov byte [gfx_gamma_on], 0
        popad
        ret

;-----------------------------------------------------------------------
; gfx_gamma_brightness - a linear ramp, 100 = unchanged
;   In : EAX = percent (10..300). 0 restores the identity table.
;
; This is a brightness scaler, not a tone curve: a real colour management
; system (ColorSync, ICM) stores a calibrated curve per channel, and this is
; the shape of table they would be loaded into. Saying which one it is matters
; more than pretending to the other.
;-----------------------------------------------------------------------
gfx_gamma_brightness:
        pushad
        test eax, eax
        jz gfx_gamma_identity
        xor ecx, ecx
.gb_loop:
        mov eax, ecx
        imul eax, [esp + 28]            ; * percent (EAX of the pushad frame)
        xor edx, edx
        mov ebx, 100
        div ebx
        cmp eax, 255
        jbe .gb_store
        mov eax, 255
.gb_store:
        mov [gfx_gamma_r + ecx], al
        mov [gfx_gamma_g + ecx], al
        mov [gfx_gamma_b + ecx], al
        inc ecx
        cmp ecx, 256
        jb .gb_loop
        mov byte [gfx_gamma_on], 1
        popad
        ret

;-----------------------------------------------------------------------
; gfx_set_target - make a memory rectangle the drawing surface
;   In : EAX = base, EBX = pitch (bytes), ECX = width, EDX = height, ESI = bpp
;
; Nothing here assumes the target is the screen: the selftest points it at a
; scratch page and proves every format on it, which is the only way to test a
; 16bpp path on an adapter left in 32bpp.
;-----------------------------------------------------------------------
gfx_set_target:
        push eax
        mov [gfx_target], eax
        mov [gfx_pitch], ebx
        mov [gfx_w], ecx
        mov [gfx_h], edx
        mov [gfx_bpp], esi
        mov eax, esi
        shr eax, 3
        mov [gfx_bytepp], eax
        pop eax
        jmp gfx_clip_full

;-----------------------------------------------------------------------
; gfx_clip_full - clip to the whole target
;-----------------------------------------------------------------------
gfx_clip_full:
        push eax
        mov dword [gfx_clip_x0], 0
        mov dword [gfx_clip_y0], 0
        mov eax, [gfx_w]
        mov [gfx_clip_x1], eax
        mov eax, [gfx_h]
        mov [gfx_clip_y1], eax
        pop eax
        ret

;-----------------------------------------------------------------------
; gfx_set_clip - restrict drawing to a rectangle
;   In : EAX = x, EBX = y, ECX = width, EDX = height
;-----------------------------------------------------------------------
gfx_set_clip:
        push esi
        push edi
        mov [gfx_clip_x0], eax
        mov [gfx_clip_y0], ebx
        lea esi, [eax + ecx]
        lea edi, [ebx + edx]
        test esi, esi
        jg .sc_x_ok
        xor esi, esi
.sc_x_ok:
        cmp esi, [gfx_w]
        jbe .sc_x_in
        mov esi, [gfx_w]
.sc_x_in:
        test edi, edi
        jg .sc_y_ok
        xor edi, edi
.sc_y_ok:
        cmp edi, [gfx_h]
        jbe .sc_y_in
        mov edi, [gfx_h]
.sc_y_in:
        mov [gfx_clip_x1], esi
        mov [gfx_clip_y1], edi
        pop edi
        pop esi
        ret

;-----------------------------------------------------------------------
; gfx_pack - 0x00RRGGBB to the target's own pixel value
;   In : ECX = colour
;   Out: EAX = the value gfx_store writes
;
; One conversion point. Everything that draws goes through it, so a format is
; supported exactly when this switch supports it, and gamma is applied in one
; place - where a VGA's palette did.
;-----------------------------------------------------------------------
gfx_pack:
        push ebx
        push edx
        cmp byte [gfx_gamma_on], 0
        je .gp_no_gamma

        mov eax, ecx
        shr eax, 16
        and eax, 0xFF
        movzx eax, byte [gfx_gamma_r + eax]
        shl eax, 16
        mov ebx, ecx
        shr ebx, 8
        and ebx, 0xFF
        movzx ebx, byte [gfx_gamma_g + ebx]
        shl ebx, 8
        or eax, ebx
        movzx ebx, cl
        movzx ebx, byte [gfx_gamma_b + ebx]
        or eax, ebx
        mov ecx, eax
.gp_no_gamma:
        mov eax, [gfx_bpp]
        cmp eax, 32
        je .gp32
        cmp eax, 24
        je .gp24
        cmp eax, 16
        je .gp16
        ; 8bpp: 3-3-2, the byte a VGA palette was indexed by
        mov eax, ecx
        shr eax, 16
        and eax, 0xE0
        mov ebx, ecx
        shr ebx, 11
        and ebx, 0x1C
        or eax, ebx
        mov ebx, ecx
        shr ebx, 6
        and ebx, 0x03
        or eax, ebx
        jmp .gp_done
.gp32:
        mov eax, ecx
        and eax, 0x00FFFFFF
        jmp .gp_done
.gp24:
        mov eax, ecx
        and eax, 0x00FFFFFF             ; gfx_store writes three bytes
        jmp .gp_done
.gp16:
        mov eax, ecx
        shr eax, 19
        and eax, 0x1F
        shl eax, 11
        mov ebx, ecx
        shr ebx, 10
        and ebx, 0x3F
        shl ebx, 5
        or eax, ebx
        mov ebx, ecx
        shr ebx, 3
        and ebx, 0x1F
        or eax, ebx
.gp_done:
        pop edx
        pop ebx
        ret

;-----------------------------------------------------------------------
; gfx_store - write a packed pixel
;   In : EAX = packed value, EDI = address
;-----------------------------------------------------------------------
gfx_store:
        cmp dword [gfx_bytepp], 4
        je .st4
        cmp dword [gfx_bytepp], 3
        je .st3
        cmp dword [gfx_bytepp], 2
        je .st2
        mov [edi], al
        ret
.st2:
        mov [edi], ax
        ret
.st3:
        mov [edi], ax
        shr eax, 16
        mov [edi + 2], al
        ret
.st4:
        mov [edi], eax
        ret

;-----------------------------------------------------------------------
; gfx_load - read a packed pixel
;   In : EDI = address
;   Out: EAX = the value the target holds
;-----------------------------------------------------------------------
gfx_load:
        cmp dword [gfx_bytepp], 4
        je .ld4
        cmp dword [gfx_bytepp], 3
        je .ld3
        cmp dword [gfx_bytepp], 2
        je .ld2
        movzx eax, byte [edi]
        ret
.ld2:
        movzx eax, word [edi]
        ret
.ld3:
        movzx eax, word [edi]
        movzx edx, byte [edi + 2]
        shl edx, 16
        or eax, edx
        ret
.ld4:
        mov eax, [edi]
        ret

;-----------------------------------------------------------------------
; gfx_pixel - one pixel, clipped
;   In : EAX = x, EBX = y, ECX = colour
;-----------------------------------------------------------------------
gfx_pixel:
        push esi
        push edi
        push edx
        mov esi, eax                    ; keep x for the address
        cmp eax, [gfx_clip_x0]
        jl .px_done
        cmp eax, [gfx_clip_x1]
        jge .px_done
        cmp ebx, [gfx_clip_y0]
        jl .px_done
        cmp ebx, [gfx_clip_y1]
        jge .px_done

        mov edi, esi
        imul edi, [gfx_bytepp]
        imul ebx, [gfx_pitch]
        add edi, ebx
        add edi, [gfx_target]
        call gfx_pack
        call gfx_store
.px_done:
        pop edx
        pop edi
        pop esi
        ret

;-----------------------------------------------------------------------
; gfx_fill - a filled rectangle, clipped to the clip rect
;   In : EAX = x, EBX = y, ECX = width, EDX = height, ESI = colour
;-----------------------------------------------------------------------
gfx_fill:
        pushad
        test ecx, ecx
        jle .gf_done
        test edx, edx
        jle .gf_done

        mov  edi, eax                   ; left
        mov  ebp, ebx                   ; top
        add  ecx, eax                   ; right
        add  edx, ebx                   ; bottom
        cmp  edi, [gfx_clip_x0]
        jge  .gf_l1
        mov  edi, [gfx_clip_x0]
.gf_l1: cmp  ebp, [gfx_clip_y0]
        jge  .gf_l2
        mov  ebp, [gfx_clip_y0]
.gf_l2: cmp  ecx, [gfx_clip_x1]
        jle  .gf_l3
        mov  ecx, [gfx_clip_x1]
.gf_l3: cmp  edx, [gfx_clip_y1]
        jle  .gf_l4
        mov  edx, [gfx_clip_y1]
.gf_l4:
        sub  ecx, edi                   ; width
        sub  edx, ebp                   ; height
        jle  .gf_done
        test ecx, ecx
        jle  .gf_done

        mov  [.gf_width], ecx
        mov  [.gf_height], edx
        mov  eax, edi
        imul eax, [gfx_bytepp]
        mov  [.gf_xbytes], eax          ; the x offset, in bytes
        mov  eax, esi
        mov  ecx, eax
        call gfx_pack
        mov  [.gf_packed], eax

.gf_row:
        mov  edi, ebp
        imul edi, [gfx_pitch]
        add  edi, [.gf_xbytes]
        add  edi, [gfx_target]
        mov  ecx, [.gf_width]
        mov  eax, [.gf_packed]
        cmp  dword [gfx_bytepp], 4
        je   .gf_row4
        cmp  dword [gfx_bytepp], 2
        je   .gf_row2
.gf_row_byte:
        ; 8 and 24: one pixel at a time. gfx_store eats EAX for 24bpp, so the
        ; value is reloaded rather than kept in a register across the call.
        call gfx_store
        add  edi, [gfx_bytepp]
        mov  eax, [.gf_packed]
        dec  ecx
        jnz  .gf_row_byte
        jmp  .gf_row_end
.gf_row4:
        cld
        rep  stosd
        jmp  .gf_row_end
.gf_row2:
        cld
        rep  stosw
.gf_row_end:
        inc  ebp
        dec  dword [.gf_height]
        jnz  .gf_row
.gf_done:
        popad
        ret

.gf_width:  dd 0
.gf_height: dd 0
.gf_xbytes: dd 0
.gf_packed: dd 0

;-----------------------------------------------------------------------
; gfx_blit - copy a 32-bit ARGB rectangle onto the target
;   In : EAX = source (dwords), EBX = source pitch in pixels,
;        ECX = dest x, EDX = dest y, ESI = width | (height << 16),
;        EDI = colour key (0xFFFFFFFF = none)
;
; The colour key is the 2000-era transparency: a sprite carried one colour
; meaning "nothing here" and the blitter skipped it. Alpha is not blended -
; blending was the next generation of card.
;-----------------------------------------------------------------------
gfx_blit:
        pushad
        mov  [.bl_src], eax
        mov  [.bl_spitch], ebx
        mov  [.bl_dx], ecx
        mov  [.bl_dy], edx
        mov  eax, esi
        movzx ebx, ax
        shr  eax, 16
        mov  [.bl_w], ebx
        mov  [.bl_h], eax
        mov  [.bl_key], edi

        xor  ebx, ebx                   ; row
.bl_row:
        cmp  ebx, [.bl_h]
        jge  .bl_done
        mov  edx, [.bl_dy]
        add  edx, ebx
        cmp  edx, [gfx_clip_y0]
        jl   .bl_next_row
        cmp  edx, [gfx_clip_y1]
        jge  .bl_done                   ; and every row below it is clipped too
        mov  edi, edx
        imul edi, [gfx_pitch]
        add  edi, [gfx_target]
        mov  [.bl_rowbase], edi

        xor  esi, esi                   ; column
.bl_col:
        cmp  esi, [.bl_w]
        jge  .bl_next_row
        mov  ecx, [.bl_dx]
        add  ecx, esi
        cmp  ecx, [gfx_clip_x0]
        jl   .bl_next_col
        cmp  ecx, [gfx_clip_x1]
        jge  .bl_next_col

        mov  eax, ebx                   ; source offset
        imul eax, [.bl_spitch]
        add  eax, esi
        mov  edx, [.bl_src]
        mov  eax, [edx + eax * 4]
        and  eax, 0x00FFFFFF
        cmp  dword [.bl_key], -1
        je   .bl_paint
        cmp  eax, [.bl_key]
        je   .bl_next_col

.bl_paint:
        push eax                        ; the colour
        mov  edx, ecx                   ; target x
        imul edx, [gfx_bytepp]
        mov  edi, [.bl_rowbase]
        add  edi, edx
        pop  ecx                        ; colour, for gfx_pack
        call gfx_pack
        call gfx_store
.bl_next_col:
        inc  esi
        jmp  .bl_col
.bl_next_row:
        inc  ebx
        jmp  .bl_row
.bl_done:
        popad
        ret

.bl_src:     dd 0
.bl_spitch:  dd 0
.bl_dx:      dd 0
.bl_dy:      dd 0
.bl_w:       dd 0
.bl_h:       dd 0
.bl_key:     dd 0
.bl_rowbase: dd 0

;-----------------------------------------------------------------------
; gfx_line - a Bresenham line, clipped per pixel
;   In : EAX = x0, EBX = y0, ECX = x1, EDX = y1, ESI = colour
;-----------------------------------------------------------------------
gfx_line:
        pushad
        mov  [.ln_x0], eax
        mov  [.ln_y0], ebx
        mov  [.ln_x1], ecx
        mov  [.ln_y1], edx
        mov  [.ln_col], esi

        mov  eax, ecx
        sub  eax, [.ln_x0]
        mov  ebx, 1
        test eax, eax
        jns  .ln_dx_pos
        neg  eax
        mov  ebx, -1
.ln_dx_pos:
        mov  [.ln_ax], eax
        mov  [.ln_sx], ebx

        mov  eax, edx
        sub  eax, [.ln_y0]
        mov  ebx, 1
        test eax, eax
        jns  .ln_dy_pos
        neg  eax
        mov  ebx, -1
.ln_dy_pos:
        mov  [.ln_ay], eax
        mov  [.ln_sy], ebx

        mov  eax, [.ln_ax]
        sub  eax, [.ln_ay]
        mov  [.ln_err], eax

.ln_loop:
        mov  eax, [.ln_x0]
        mov  ebx, [.ln_y0]
        mov  ecx, [.ln_col]
        call gfx_pixel
        mov  eax, [.ln_x0]
        cmp  eax, [.ln_x1]
        jne  .ln_step
        mov  eax, [.ln_y0]
        cmp  eax, [.ln_y1]
        je   .ln_done
.ln_step:
        mov  eax, [.ln_err]
        add  eax, eax                   ; e2 = 2 * err
        mov  [.ln_e2], eax
        mov  ebx, [.ln_ay]
        neg  ebx
        cmp  eax, ebx
        jl   .ln_skip_x
        mov  ebx, [.ln_err]
        sub  ebx, [.ln_ay]
        mov  [.ln_err], ebx
        mov  ebx, [.ln_sx]
        add  [.ln_x0], ebx
.ln_skip_x:
        mov  eax, [.ln_e2]
        cmp  eax, [.ln_ax]
        jg   .ln_skip_y
        mov  ebx, [.ln_err]
        add  ebx, [.ln_ax]
        mov  [.ln_err], ebx
        mov  ebx, [.ln_sy]
        add  [.ln_y0], ebx
.ln_skip_y:
        jmp  .ln_loop
.ln_done:
        popad
        ret

.ln_x0:  dd 0
.ln_y0:  dd 0
.ln_x1:  dd 0
.ln_y1:  dd 0
.ln_col: dd 0
.ln_ax:  dd 0
.ln_ay:  dd 0
.ln_sx:  dd 0
.ln_sy:  dd 0
.ln_err: dd 0
.ln_e2:  dd 0

;-----------------------------------------------------------------------
; gfx_frame - a rectangle outline, one pixel thick
;   In : EAX = x, EBX = y, ECX = width, EDX = height, ESI = colour
;-----------------------------------------------------------------------
gfx_frame:
        pushad
        mov  [.fr_x], eax
        mov  [.fr_y], ebx
        mov  [.fr_w], ecx
        mov  [.fr_h], edx
        mov  [.fr_col], esi

        ; top and bottom
        mov  eax, [.fr_x]
        mov  ebx, [.fr_y]
        mov  ecx, [.fr_w]
        mov  edx, 1
        mov  esi, [.fr_col]
        call gfx_fill
        mov  eax, [.fr_x]
        mov  ebx, [.fr_y]
        add  ebx, [.fr_h]
        dec  ebx
        mov  ecx, [.fr_w]
        mov  edx, 1
        mov  esi, [.fr_col]
        call gfx_fill
        ; left and right, one pixel wide, inside the top and bottom rows
        mov  eax, [.fr_x]
        mov  ebx, [.fr_y]
        inc  ebx
        mov  ecx, 1
        mov  edx, [.fr_h]
        sub  edx, 2
        mov  esi, [.fr_col]
        call gfx_fill
        mov  eax, [.fr_x]
        add  eax, [.fr_w]
        dec  eax
        mov  ebx, [.fr_y]
        inc  ebx
        mov  ecx, 1
        mov  edx, [.fr_h]
        sub  edx, 2
        mov  esi, [.fr_col]
        call gfx_fill
        popad
        ret

.fr_x:   dd 0
.fr_y:   dd 0
.fr_w:   dd 0
.fr_h:   dd 0
.fr_col: dd 0

;-----------------------------------------------------------------------
; gfx_vsync - wait for the start of a vertical retrace
;
; The VGA status port's bit 3 is the retrace, and every 2000-era program that
; animated waited on it. The loop is bounded: a machine with no VGA at that
; port would otherwise hang here rather than draw a frame late.
;-----------------------------------------------------------------------
gfx_vsync:
        pushad
        mov  ecx, 1000000
        mov  dx, 0x3DA
.vs_wait_low:
        in   al, dx
        test al, 8
        jz   .vs_wait_high
        loop .vs_wait_low
        jmp  .vs_done
.vs_wait_high:
        mov  ecx, 1000000
.vs_high_loop:
        in   al, dx
        test al, 8
        jnz  .vs_done
        loop .vs_high_loop
.vs_done:
        popad
        ret

;-----------------------------------------------------------------------
; gfx_cursor_define - load a cursor shape and stop drawing the old one
;   In : EAX = ARGB source, EBX = width | (height << 16), EDI = colour key
;-----------------------------------------------------------------------
gfx_cursor_define:
        pushad
        call gfx_cursor_hide
        mov  [.cd_src], eax
        mov  ecx, ebx
        movzx edx, cx
        shr  ecx, 16
        cmp  edx, 32
        jbe .cd_w_ok
        mov  edx, 32
.cd_w_ok:
        cmp  ecx, 32
        jbe .cd_h_ok
        mov  ecx, 32
.cd_h_ok:
        mov  [gfx_cursor_w], edx
        mov  [gfx_cursor_h], ecx
        mov  [gfx_cursor_key], edi
        mov  esi, [.cd_src]
        mov  edi, gfx_cursor_pixels
        mov  eax, edx
        imul eax, ecx
        mov  ecx, eax
        cld
        rep  movsd
        popad
        ret
.cd_src: dd 0

;-----------------------------------------------------------------------
; gfx_cursor_hide - put back what was under the cursor
;-----------------------------------------------------------------------
gfx_cursor_hide:
        pushad
        cmp  byte [gfx_cursor_drawn], 0
        je   .ch_done
        xor  ebx, ebx
.ch_row:
        cmp  ebx, [gfx_cursor_h]
        jge  .ch_cleared
        xor  esi, esi
.ch_col:
        cmp  esi, [gfx_cursor_w]
        jge  .ch_next_row
        mov  eax, [gfx_cursor_x]
        add  eax, esi
        mov  edx, [gfx_cursor_y]
        add  edx, ebx
        ; address, then the saved native value
        push eax
        push ebx
        push esi
        mov  edi, eax
        imul edi, [gfx_bytepp]
        mov  ecx, edx
        imul ecx, [gfx_pitch]
        add  edi, ecx
        add  edi, [gfx_target]
        mov  eax, ebx
        imul eax, [gfx_cursor_w]
        add  eax, esi
        mov  eax, [gfx_cursor_under + eax * 4]
        call gfx_store
        pop  esi
        pop  ebx
        pop  eax
        inc  esi
        jmp  .ch_col
.ch_next_row:
        inc  ebx
        jmp  .ch_row
.ch_cleared:
        mov  byte [gfx_cursor_drawn], 0
.ch_done:
        popad
        ret

;-----------------------------------------------------------------------
; gfx_cursor_move - move the cursor: save under, then draw it
;   In : EAX = x, EBX = y
;-----------------------------------------------------------------------
gfx_cursor_move:
        pushad
        call gfx_cursor_hide
        mov  [gfx_cursor_x], eax
        mov  [gfx_cursor_y], ebx
        cmp  dword [gfx_cursor_w], 0
        je   .cm_done

        xor  ebx, ebx                   ; row
.cm_row:
        cmp  ebx, [gfx_cursor_h]
        jge  .cm_drawn
        xor  esi, esi
.cm_col:
        cmp  esi, [gfx_cursor_w]
        jge  .cm_next_row
        mov  eax, [gfx_cursor_x]
        add  eax, esi
        mov  edx, [gfx_cursor_y]
        add  edx, ebx
        cmp  eax, [gfx_clip_x0]
        jl   .cm_next_col
        cmp  eax, [gfx_clip_x1]
        jge  .cm_next_col
        cmp  edx, [gfx_clip_y0]
        jl   .cm_next_col
        cmp  edx, [gfx_clip_y1]
        jge  .cm_next_col

        ; address of this cursor pixel
        push eax
        push ebx
        push esi
        mov  edi, eax
        imul edi, [gfx_bytepp]
        mov  ecx, edx
        imul ecx, [gfx_pitch]
        add  edi, ecx
        add  edi, [gfx_target]

        ; the saved-under copy keeps the native pixel
        mov  eax, ebx
        imul eax, [gfx_cursor_w]
        add  eax, esi
        push eax
        call gfx_load
        pop  ecx
        mov  [gfx_cursor_under + ecx * 4], eax

        ; then the shape: key-coloured pixels are the transparent ones
        mov  eax, ebx
        imul eax, [gfx_cursor_w]
        add  eax, esi
        mov  ecx, [gfx_cursor_pixels + eax * 4]
        and  ecx, 0x00FFFFFF
        cmp  ecx, [gfx_cursor_key]
        je   .cm_keep
        call gfx_pack
        call gfx_store
.cm_keep:
        pop  esi
        pop  ebx
        pop  eax
.cm_next_col:
        inc  esi
        jmp  .cm_col
.cm_next_row:
        inc  ebx
        jmp  .cm_row
.cm_drawn:
        mov  byte [gfx_cursor_drawn], 1
.cm_done:
        popad
        ret

gfx_cursor_key dd -1

gfx_test_page: times 64 * 64 * 4 db 0   ; the selftest's scratch surface

;-----------------------------------------------------------------------
; gfx_selftest - prove every pixel format on a scratch page
;
; The adapter is left in whatever mode it booted in, so the only honest way to
; test 16bpp and 24bpp is to point the engine at a page of RAM with that format
; and check what comes back. A format that packs wrongly would otherwise never
; be noticed until someone chose a mode nobody uses.
;
; Prints GFX_SELFTEST_OK on COM1, or GFX_SELFTEST_FAIL with the format that
; failed.
;-----------------------------------------------------------------------
gfx_selftest:
        pushad
        ; A static page: the test must be runnable from the shell as well as at
        ; boot, and taking the physical-memory lock there would invert the
        ; lock order the shell already holds.
        mov  eax, gfx_test_page
        mov  [.gs_page], eax
        mov  eax, [gfx_target]
        mov  [.gs_saved_target], eax
        mov  eax, [gfx_pitch]
        mov  [.gs_saved_pitch], eax
        mov  eax, [gfx_w]
        mov  [.gs_saved_w], eax
        mov  eax, [gfx_h]
        mov  [.gs_saved_h], eax
        mov  eax, [gfx_bpp]
        mov  [.gs_saved_bpp], eax

        mov  esi, .gs_formats
.gs_next:
        mov  eax, [esi]
        add  esi, 4
        test eax, eax
        jz   .gs_ok
        call .gs_one
        test eax, eax
        jz   .gs_fail
        jmp  .gs_next

.gs_ok:
        call gfx_gamma_identity
        call .gs_restore
        mov  esi, .gs_msg_ok
        call serial_print
        mov  esi, .gs_msg_ok
        call vga_print
        popad
        ret
.gs_fail:
        call gfx_gamma_identity
        call .gs_restore
        mov  esi, .gs_msg_fail
        call serial_print
        mov  esi, .gs_msg_fail
        call vga_print
        popad
        ret
.gs_skip:
        mov  esi, .gs_msg_skip
        call serial_print
        mov  esi, .gs_msg_skip
        call vga_print
        popad
        ret

;--- .gs_one: EAX = bpp. Out: EAX = 1 pass, 0 fail ---------------------
.gs_one:
        pushad
        mov  [.gs_bpp], eax

        mov  eax, [.gs_page]
        mov  ebx, 64 * 4                ; room for a 32bpp row of 64
        mov  ecx, 64
        mov  edx, 64
        mov  esi, [.gs_bpp]
        call gfx_set_target
        call gfx_clip_full

        ; 1. a whole-target fill reaches both corners
        mov  eax, 0
        mov  ebx, 0
        mov  ecx, 64
        mov  edx, 64
        mov  esi, 0x112233
        call gfx_fill
        mov  eax, 0
        mov  ebx, 0
        mov  edx, 0x112233
        call .gs_expect
        test eax, eax
        jz   .gs_one_bad
        mov  eax, 63
        mov  ebx, 63
        mov  edx, 0x112233
        call .gs_expect
        test eax, eax
        jz   .gs_one_bad

        ; 2. a rectangle changes only what it covers
        mov  eax, 10
        mov  ebx, 10
        mov  ecx, 5
        mov  edx, 5
        mov  esi, 0xFF0000
        call gfx_fill
        mov  eax, 14
        mov  ebx, 14
        mov  edx, 0xFF0000
        call .gs_expect
        test eax, eax
        jz   .gs_one_bad
        mov  eax, 9
        mov  ebx, 10
        mov  edx, 0x112233
        call .gs_expect
        test eax, eax
        jz   .gs_one_bad

        ; 3. a colour-keyed blit skips its key and paints the rest
        mov  eax, .gs_sprite
        mov  ebx, 4
        mov  ecx, 20
        mov  edx, 20
        mov  esi, 4 | (4 << 16)
        mov  edi, 0xFF00FF
        call gfx_blit
        mov  eax, 20
        mov  ebx, 20
        mov  edx, 0x00FF00
        call .gs_expect
        test eax, eax
        jz   .gs_one_bad
        mov  eax, 21
        mov  ebx, 20
        mov  edx, 0x112233
        call .gs_expect
        test eax, eax
        jz   .gs_one_bad

        ; 4. the clip rectangle holds the fill in
        mov  eax, 0
        mov  ebx, 0
        mov  ecx, 8
        mov  edx, 8
        call gfx_set_clip
        mov  eax, 0
        mov  ebx, 0
        mov  ecx, 64
        mov  edx, 64
        mov  esi, 0x0000FF
        call gfx_fill
        call gfx_clip_full
        mov  eax, 41
        mov  ebx, 41
        mov  edx, 0x112233
        call .gs_expect
        test eax, eax
        jz   .gs_one_bad
        mov  eax, 2
        mov  ebx, 2
        mov  edx, 0x0000FF
        call .gs_expect
        test eax, eax
        jz   .gs_one_bad

        ; 5. gamma is applied where colours are packed: 0x80 at 50% is 0x40
        mov  eax, 50
        call gfx_gamma_brightness
        mov  eax, 50
        mov  ebx, 50
        mov  ecx, 1
        mov  edx, 1
        mov  esi, 0x808080
        call gfx_fill
        call gfx_gamma_identity
        mov  eax, 50
        mov  ebx, 50
        mov  edx, 0x404040
        call .gs_expect
        test eax, eax
        jz   .gs_one_bad

        ; 6. a line reaches both ends
        mov  eax, 30
        mov  ebx, 30
        mov  ecx, 33
        mov  edx, 30
        mov  esi, 0x00FFFF
        call gfx_line
        mov  eax, 33
        mov  ebx, 30
        mov  edx, 0x00FFFF
        call .gs_expect
        test eax, eax
        jz   .gs_one_bad

        popad
        mov  eax, 1
        ret
.gs_one_bad:
        popad
        xor  eax, eax
        ret

;--- .gs_expect: EAX = x, EBX = y, EDX = colour -> EAX = 1 equal, 0 not ---
.gs_expect:
        pushad
        mov  [.gs_ex], eax
        mov  [.gs_ey], ebx
        mov  ecx, edx
        call gfx_pack
        mov  [.gs_ewant], eax
        mov  eax, [.gs_ex]
        mov  ebx, [.gs_ey]
        mov  edi, eax
        imul edi, [gfx_bytepp]
        imul ebx, [gfx_pitch]
        add  edi, ebx
        add  edi, [gfx_target]
        call gfx_load
        cmp  eax, [.gs_ewant]
        jne  .gs_ex_no
        popad
        mov  eax, 1
        ret
.gs_ex_no:
        popad
        xor  eax, eax
        ret

.gs_restore:
        mov  eax, [.gs_saved_target]
        mov  [gfx_target], eax
        mov  eax, [.gs_saved_pitch]
        mov  [gfx_pitch], eax
        mov  eax, [.gs_saved_w]
        mov  [gfx_w], eax
        mov  eax, [.gs_saved_h]
        mov  [gfx_h], eax
        mov  eax, [.gs_saved_bpp]
        mov  [gfx_bpp], eax
        push eax
        shr  eax, 3
        mov  [gfx_bytepp], eax
        pop  eax
        jmp  gfx_clip_full

.gs_page:         dd 0
.gs_bpp:          dd 0
.gs_ex:           dd 0
.gs_ey:           dd 0
.gs_ewant:        dd 0
.gs_saved_target: dd 0
.gs_saved_pitch:  dd 0
.gs_saved_w:      dd 0
.gs_saved_h:      dd 0
.gs_saved_bpp:    dd 0
.gs_formats:      dd 32, 24, 16, 8, 0

; One transparent pixel (the 0xFF00FF at row 0, column 1) is what proves the
; key; the rest is what proves the paint.
.gs_sprite:
        dd 0x00FF00, 0xFF00FF, 0x00FF00, 0x00FF00
        dd 0x00FF00, 0x00FF00, 0x00FF00, 0x00FF00
        dd 0x00FF00, 0x00FF00, 0x00FF00, 0x00FF00
        dd 0x00FF00, 0x00FF00, 0x00FF00, 0x00FF00

.gs_msg_ok:   db "GFX_SELFTEST_OK", 13, 10, 0
.gs_msg_fail: db "GFX_SELFTEST_FAIL", 13, 10, 0
.gs_msg_skip: db "GFX_SELFTEST_SKIP", 13, 10, 0

;-----------------------------------------------------------------------
; gfx_report - the `gfx` command: what the engine is pointed at, then proof it
; works
;-----------------------------------------------------------------------
gfx_report:
        pushad
        mov  esi, .gr_head
        call vga_print

        cmp  dword [gfx_target], 0
        jne  .gr_have_target
        mov  esi, .gr_none
        call vga_print
        jmp  .gr_test
.gr_have_target:
        mov  eax, [gfx_w]
        call vga_print_dec
        mov  esi, .gr_x
        call vga_print
        mov  eax, [gfx_h]
        call vga_print_dec
        mov  esi, .gr_at
        call vga_print
        mov  eax, [gfx_bpp]
        call vga_print_dec
        mov  esi, .gr_bpp
        call vga_print

        mov  esi, .gr_clip
        call vga_print
        mov  eax, [gfx_clip_x0]
        call vga_print_dec
        mov  esi, .gr_comma
        call vga_print
        mov  eax, [gfx_clip_y0]
        call vga_print_dec
        mov  esi, .gr_dash
        call vga_print
        mov  eax, [gfx_clip_x1]
        call vga_print_dec
        mov  esi, .gr_comma
        call vga_print
        mov  eax, [gfx_clip_y1]
        call vga_print_dec
        mov  esi, .gr_nl
        call vga_print

        mov  esi, .gr_gamma
        call vga_print
        movzx eax, byte [gfx_gamma_on]
        call vga_print_dec
        mov  esi, .gr_cursor
        call vga_print
        mov  eax, [gfx_cursor_w]
        call vga_print_dec
        mov  esi, .gr_x
        call vga_print
        mov  eax, [gfx_cursor_h]
        call vga_print_dec
        mov  esi, .gr_nl
        call vga_print
.gr_test:
        mov  esi, .gr_running
        call vga_print
        call gfx_selftest
        popad
        ret

.gr_head:    db "display engine", 13, 10, 0
.gr_none:    db "  no surface: no graphics mode is set", 13, 10, 0
.gr_x:       db "x", 0
.gr_at:      db " at ", 0
.gr_bpp:     db " bpp", 13, 10, 0
.gr_clip:    db "  clip ", 0
.gr_comma:   db ",", 0
.gr_dash:    db " - ", 0
.gr_nl:      db 13, 10, 0
.gr_gamma:   db "  gamma ", 0
.gr_cursor:  db "  cursor ", 0
.gr_running: db "  testing every pixel format on a scratch page...", 13, 10, 0
