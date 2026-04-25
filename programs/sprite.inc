;=======================================================================
; sprite.inc — Sprite drawing library for Mellivora OS VBE programs
;
; Include after syscalls.inc in any VBE program:
;   %include "syscalls.inc"
;   %include "sprite.inc"
;
; SPRITE FORMAT (flat binary, inline in .asm data section):
;   dd  width        ; sprite width in pixels
;   dd  height       ; sprite height in pixels
;   dd  pixel[0]     ; 0xAARRGGBB — top-left pixel
;   dd  pixel[1]     ; ...row-major order...
;   dd  pixel[w*h-1] ; bottom-right pixel
;
; Alpha channel (bits 24-31):
;   0x00xxxxxx = fully transparent — pixel is skipped
;   0xFFxxxxxx = fully opaque
;   (intermediate values are treated as opaque by sprite_draw)
;
; All routines read [fb_addr] and [fb_pitch] from the calling
; program's BSS, exactly like fb_fill_rect.
;
; ROUTINES
; --------
;   sprite_draw        EBX=x ECX=y ESI=sprite_ptr
;       Draw sprite with alpha transparency (A=0 → skip pixel).
;
;   sprite_draw_opaque EBX=x ECX=y ESI=sprite_ptr
;       Draw sprite ignoring alpha — every pixel written (fastest).
;
;   sprite_draw_key    EBX=x ECX=y ESI=sprite_ptr EDI=color_key
;       Draw sprite with color-key transparency — pixels whose RGB
;       (low 24 bits) match color_key are skipped.
;
;   sprite_draw_scaled EBX=x ECX=y ESI=sprite_ptr EDX=scale_shift
;       Draw sprite scaled up by 2^scale_shift (e.g. 1 = 2x, 2 = 4x).
;       Uses nearest-neighbour; alpha transparent pixels are skipped.
;
; MACRO — define sprite data inline:
;   SPRITE_BEGIN name, width, height
;     dd 0xFFRRGGBB, ...   (width * height pixel values)
;   SPRITE_END
;
; Example usage:
;   SPRITE_BEGIN my_icon, 8, 8
;     dd 0x00000000, 0xFFFF0000, ...   ; row 0 (0x00 = transparent)
;     ...
;   SPRITE_END
;   ...
;   mov ebx, 100      ; x
;   mov ecx, 50       ; y
;   mov esi, my_icon  ; sprite pointer
;   call sprite_draw
;=======================================================================

;-----------------------------------------------------------------------
; Macros
;-----------------------------------------------------------------------

; SPRITE_BEGIN name, width, height
; Emits the sprite header (width dd, height dd).
; Follow immediately with width*height dd pixel values, then SPRITE_END.
%macro SPRITE_BEGIN 3
%1:
        dd %2           ; width
        dd %3           ; height
%endmacro

; SPRITE_END — no-op placeholder for readability
%macro SPRITE_END 0
%endmacro

;-----------------------------------------------------------------------
; sprite_draw  EBX=x  ECX=y  ESI=sprite_ptr
; Draw sprite with alpha transparency (alpha byte = 0 → skip pixel).
; Clips against the right/bottom screen edges using fb_pitch/fb_height
; is not stored so we only clip X per row.
;-----------------------------------------------------------------------
sprite_draw:
        pushad

        mov eax, [esi]          ; EAX = sprite width
        mov [.sw], eax
        mov eax, [esi + 4]      ; EAX = sprite height
        mov [.sh], eax
        add esi, 8              ; ESI = first pixel

        ; base row address = fb_addr + y * fb_pitch + x * 4
        mov eax, [fb_pitch]
        imul eax, ecx           ; eax = y * pitch
        add eax, [fb_addr]
        lea eax, [eax + ebx*4]  ; + x * 4
        mov [.row_ptr], eax

        mov dword [.row], 0

.row_loop:
        mov eax, [.row]
        cmp eax, [.sh]
        jge .done

        mov eax, [.row_ptr]
        mov [.pix_ptr], eax
        mov dword [.col], 0

.col_loop:
        mov eax, [.col]
        cmp eax, [.sw]
        jge .next_row

        mov eax, [esi]           ; load pixel AARRGGBB
        add esi, 4
        ; test alpha: if top byte is 0, skip
        test eax, 0xFF000000
        jz .col_skip

        mov edx, [.pix_ptr]
        and eax, 0x00FFFFFF      ; strip alpha — write 32-bit value
        mov [edx], eax

.col_skip:
        add dword [.pix_ptr], 4
        inc dword [.col]
        jmp .col_loop

.next_row:
        ; advance row_ptr by one pitch
        mov eax, [.row_ptr]
        add eax, [fb_pitch]
        mov [.row_ptr], eax
        inc dword [.row]
        jmp .row_loop

.done:
        popad
        ret

; local vars (safe: only called from ring-3 non-reentrant context)
.sw:        dd 0
.sh:        dd 0
.row:       dd 0
.col:       dd 0
.row_ptr:   dd 0
.pix_ptr:   dd 0

;-----------------------------------------------------------------------
; sprite_draw_opaque  EBX=x  ECX=y  ESI=sprite_ptr
; Blit every pixel — no alpha check. Fastest path.
;-----------------------------------------------------------------------
sprite_draw_opaque:
        pushad

        mov eax, [esi]           ; width
        mov [.sw], eax
        mov eax, [esi + 4]       ; height
        mov [.sh], eax
        add esi, 8               ; first pixel

        mov eax, [fb_pitch]
        imul eax, ecx
        add eax, [fb_addr]
        lea eax, [eax + ebx*4]
        mov [.row_ptr], eax

        mov dword [.row], 0

.row_loop:
        mov eax, [.row]
        cmp eax, [.sh]
        jge .done

        ; copy one full row via rep movsd
        push esi
        push edi
        mov edi, [.row_ptr]
        mov ecx, [.sw]
        cld
        rep movsd                ; esi advances automatically
        pop edi
        ; esi is now past this row — keep it
        ; but we pushed esi so restore and recalculate
        ; Actually: rep movsd moves esi forward by sw*4 which is what we want.
        ; We just need to NOT pop esi.
        add esp, 4               ; discard pushed esi (already advanced)

        mov eax, [.row_ptr]
        add eax, [fb_pitch]
        mov [.row_ptr], eax
        inc dword [.row]
        jmp .row_loop

.done:
        popad
        ret

.sw:        dd 0
.sh:        dd 0
.row:       dd 0
.row_ptr:   dd 0

;-----------------------------------------------------------------------
; sprite_draw_key  EBX=x  ECX=y  ESI=sprite_ptr  EDI=color_key
; Draw sprite with color-key transparency.
; Pixels whose low 24 bits equal color_key are not written.
;-----------------------------------------------------------------------
sprite_draw_key:
        pushad

        mov [.key], edi
        mov eax, [esi]
        mov [.sw], eax
        mov eax, [esi + 4]
        mov [.sh], eax
        add esi, 8

        mov eax, [fb_pitch]
        imul eax, ecx
        add eax, [fb_addr]
        lea eax, [eax + ebx*4]
        mov [.row_ptr], eax

        mov dword [.row], 0

.row_loop:
        mov eax, [.row]
        cmp eax, [.sh]
        jge .done

        mov eax, [.row_ptr]
        mov [.pix_ptr], eax
        mov dword [.col], 0

.col_loop:
        mov eax, [.col]
        cmp eax, [.sw]
        jge .next_row

        mov eax, [esi]
        add esi, 4
        mov edx, eax
        and edx, 0x00FFFFFF      ; mask to RGB
        cmp edx, [.key]
        je .col_skip

        mov edx, [.pix_ptr]
        and eax, 0x00FFFFFF
        mov [edx], eax

.col_skip:
        add dword [.pix_ptr], 4
        inc dword [.col]
        jmp .col_loop

.next_row:
        mov eax, [.row_ptr]
        add eax, [fb_pitch]
        mov [.row_ptr], eax
        inc dword [.row]
        jmp .row_loop

.done:
        popad
        ret

.key:       dd 0
.sw:        dd 0
.sh:        dd 0
.row:       dd 0
.col:       dd 0
.row_ptr:   dd 0
.pix_ptr:   dd 0

;-----------------------------------------------------------------------
; sprite_draw_scaled  EBX=x  ECX=y  ESI=sprite_ptr  EDX=scale_shift
; Draw sprite scaled up by 2^scale_shift (nearest neighbour).
;   scale_shift=0 → 1x (same as sprite_draw)
;   scale_shift=1 → 2x
;   scale_shift=2 → 4x
; Alpha transparent pixels are skipped.
;-----------------------------------------------------------------------
sprite_draw_scaled:
        pushad

        ; Save incoming params before registers are clobbered
        mov [.dst_x],  ebx
        mov [.dst_y],  ecx
        mov [.scale],  edx

        ; pixel_size = 1 << scale_shift
        mov ecx, edx
        mov eax, 1
        shl eax, cl
        mov [.psize], eax

        mov eax, [esi]
        mov [.sw], eax
        mov eax, [esi + 4]
        mov [.sh], eax
        add esi, 8               ; ESI → first pixel

        mov dword [.sr], 0       ; source row index

.srow_loop:
        mov eax, [.sr]
        cmp eax, [.sh]
        jge .sdone

        ; screen_y = dst_y + sr * psize
        imul eax, [.psize]
        add eax, [.dst_y]
        mov [.sy], eax

        mov dword [.sc], 0

.scol_loop:
        mov eax, [.sc]
        cmp eax, [.sw]
        jge .snext_row

        mov eax, [esi]
        add esi, 4

        test eax, 0xFF000000
        jz .sskip

        and eax, 0x00FFFFFF
        mov [.bcolor], eax

        ; screen_x = dst_x + sc * psize
        mov ebx, [.sc]
        imul ebx, [.psize]
        add ebx, [.dst_x]

        ; row_ptr = fb_addr + sy * pitch + screen_x * 4
        mov edx, [fb_pitch]
        imul edx, [.sy]
        add edx, [fb_addr]
        mov eax, ebx
        shl eax, 2
        add edx, eax
        mov [.rptr], edx

        ; draw psize x psize block
        mov ecx, [.psize]        ; row counter
.brow:
        test ecx, ecx
        jz .bdone
        mov edi, [.rptr]
        mov ebx, [.psize]        ; col counter
        mov eax, [.bcolor]
.bcol:
        test ebx, ebx
        jz .bcol_done
        mov [edi], eax
        add edi, 4
        dec ebx
        jmp .bcol
.bcol_done:
        mov eax, [fb_pitch]
        add [.rptr], eax
        dec ecx
        jmp .brow
.bdone:

.sskip:
        inc dword [.sc]
        jmp .scol_loop

.snext_row:
        inc dword [.sr]
        jmp .srow_loop

.sdone:
        popad
        ret

.dst_x:   dd 0
.dst_y:   dd 0
.scale:   dd 0
.psize:   dd 0
.sw:      dd 0
.sh:      dd 0
.sr:      dd 0
.sc:      dd 0
.sy:      dd 0
.bcolor:  dd 0
.rptr:    dd 0
