;=======================================================================
; vbe.inc - Burrows Desktop Environment (VBE/Bochs Framebuffer Driver)
;
; Burrows is the graphical desktop environment for Mellivora OS.
; Uses the Bochs VBE extensions (BGA) available in QEMU/Bochs to set
; a high-resolution linear framebuffer mode from protected mode —
; no real-mode INT 10h needed.
;
; The LFB lives at the physical address reported by BGA (typically
; 0xFD000000 on QEMU).  We identity-map the first 8 MB of framebuffer
; via the paging subsystem so pixel writes go directly to VRAM.
;
; A shell command `gui` enters the Burrows desktop that draws colour
; bars, a mouse cursor, and waits for a keypress before switching back
; to VGA text mode.
;
; SYS_FRAMEBUF (37): EBX=0 get info (ret EAX=shadow_buf,EBX=w,ECX=h,EDX=bpp)
;                     EBX=1 set mode  ECX=width EDX=height ESI=bpp
;                     EBX=2 restore text mode
;                     EBX=3 draw text  ECX=x EDX=y ESI=str_ptr EDI=fg_color
;                     EBX=4 present   blit shadow_buf -> LFB (double-buffer flip)
;=======================================================================

; BGA I/O ports
VBE_DISPI_IOPORT_INDEX  equ 0x01CE
VBE_DISPI_IOPORT_DATA   equ 0x01CF

; BGA register indices
VBE_DISPI_INDEX_ID      equ 0
VBE_DISPI_INDEX_XRES    equ 1
VBE_DISPI_INDEX_YRES    equ 2
VBE_DISPI_INDEX_BPP     equ 3
VBE_DISPI_INDEX_ENABLE  equ 4
VBE_DISPI_INDEX_BANK    equ 5
VBE_DISPI_INDEX_VIRT_W  equ 6
VBE_DISPI_INDEX_VIRT_H  equ 7
VBE_DISPI_INDEX_X_OFF   equ 8
VBE_DISPI_INDEX_Y_OFF   equ 9
VBE_DISPI_INDEX_FB_ADDR equ 10         ; Bochs ≥2.7 reports LFB here

; BGA enable flags
VBE_DISPI_DISABLED      equ 0x00
VBE_DISPI_ENABLED       equ 0x01
VBE_DISPI_LFB_ENABLED   equ 0x40

; Default mode
VBE_DEFAULT_WIDTH       equ 1024
VBE_DEFAULT_HEIGHT      equ 768
VBE_DEFAULT_BPP         equ 32

; Hard caps for user-supplied modes (Phase 1.1 hardening).
; The framebuffer mapping is bounded by VBE_MAX_FB_PAGES so a hostile
; or buggy program cannot make us page in arbitrary amounts of LFB.
VBE_MAX_WIDTH           equ 1920
VBE_MAX_HEIGHT          equ 1200
VBE_MAX_FB_PAGES        equ 4096        ; 16 MB cap (covers 1920x1200x32 = 9.0 MB)
VBE_MAX_DRAW_STRING     equ 4096        ; cap for SYS_FRAMEBUF/3 string length

; Fallback LFB address (QEMU/Bochs standard mapping)
VBE_LFB_FALLBACK        equ 0xFD000000

;---------------------------------------
; vbe_init - Detect the Bochs VBE adapter
; Sets vbe_available = 1 if found.
;---------------------------------------
vbe_init:
        pushad

        ; Read BGA ID register
        mov dx, VBE_DISPI_IOPORT_INDEX
        xor ax, ax                      ; INDEX_ID = 0
        out dx, ax
        mov dx, VBE_DISPI_IOPORT_DATA
        in ax, dx

        ; Valid BGA IDs are 0xB0C0 .. 0xB0C5
        cmp ax, 0xB0C0
        jb .no_vbe
        cmp ax, 0xB0C5
        ja .no_vbe

        mov byte [vbe_available], 1

        ; Read LFB base from PCI BAR 0 of VGA device (Bus 0, Dev 2, Fn 0)
        ; PCI config address: 0x80001010 = enable | dev 2 | BAR 0 register
        mov eax, 0x80001010
        mov dx, 0x0CF8
        out dx, eax
        mov dx, 0x0CFC
        in eax, dx
        and eax, 0xFFFFFFF0             ; Mask BAR type bits
        cmp eax, 0x10000                ; Sanity check
        jb .try_bga_fb_addr
        mov [vbe_lfb_addr], eax
        jmp .done

.try_bga_fb_addr:
        ; Fallback: read from BGA INDEX_FB_ADDR (Bochs 2.7+)
        mov dx, VBE_DISPI_IOPORT_INDEX
        mov ax, VBE_DISPI_INDEX_FB_ADDR
        out dx, ax
        mov dx, VBE_DISPI_IOPORT_DATA
        in ax, dx
        movzx eax, ax
        shl eax, 16                     ; Reported as top 16 bits of address
        cmp eax, 0x10000                 ; Sanity check
        jb .use_fallback
        mov [vbe_lfb_addr], eax
        jmp .done

.use_fallback:
        mov dword [vbe_lfb_addr], VBE_LFB_FALLBACK
        jmp .done

.no_vbe:
        mov byte [vbe_available], 0
.done:
        popad
        ret

;---------------------------------------
; vbe_set_mode - Switch to a graphics mode
; EAX = width, EBX = height, ECX = bpp
; Returns: caller's EAX/EBX/ECX preserved via popad; on invalid mode
; the function returns without changing any state.
;---------------------------------------
vbe_set_mode:
        pushad
        ; --- Validate mode parameters (whitelist) ---
        ; Width: 320..VBE_MAX_WIDTH; Height: 200..VBE_MAX_HEIGHT;
        ; Bpp: 16, 24, or 32. Reject anything else to bound the FB mapping
        ; and prevent y*pitch overflow in pixel writers.
        cmp eax, 320
        jl .vsm_bad
        cmp eax, VBE_MAX_WIDTH
        jg .vsm_bad
        cmp ebx, 200
        jl .vsm_bad
        cmp ebx, VBE_MAX_HEIGHT
        jg .vsm_bad
        cmp ecx, 16
        je .vsm_ok
        cmp ecx, 24
        je .vsm_ok
        cmp ecx, 32
        je .vsm_ok
.vsm_bad:
        popad
        ret
.vsm_ok:
        mov [vbe_width], eax
        mov [vbe_height], ebx
        mov [vbe_bpp], ecx

        ; Save VGA text font from plane 2 before switching (once)
        cmp byte [vbe_font_saved], 1
        je .font_already_saved
        call vbe_save_vga_font
        mov byte [vbe_font_saved], 1
.font_already_saved:

        ; Calculate pitch = width * (bpp / 8)
        mov eax, [vbe_width]
        mov edx, ecx
        shr edx, 3              ; bpp / 8 = bytes per pixel
        mov [vbe_Bpp], edx
        imul eax, edx
        mov [vbe_pitch], eax

        ; Disable VBE first
        mov dx, VBE_DISPI_IOPORT_INDEX
        mov ax, VBE_DISPI_INDEX_ENABLE
        out dx, ax
        mov dx, VBE_DISPI_IOPORT_DATA
        mov ax, VBE_DISPI_DISABLED
        out dx, ax

        ; Set width
        mov dx, VBE_DISPI_IOPORT_INDEX
        mov ax, VBE_DISPI_INDEX_XRES
        out dx, ax
        mov dx, VBE_DISPI_IOPORT_DATA
        mov ax, [vbe_width]
        out dx, ax

        ; Set height
        mov dx, VBE_DISPI_IOPORT_INDEX
        mov ax, VBE_DISPI_INDEX_YRES
        out dx, ax
        mov dx, VBE_DISPI_IOPORT_DATA
        mov ax, [vbe_height]
        out dx, ax

        ; Set BPP
        mov dx, VBE_DISPI_IOPORT_INDEX
        mov ax, VBE_DISPI_INDEX_BPP
        out dx, ax
        mov dx, VBE_DISPI_IOPORT_DATA
        mov ax, [vbe_bpp]
        out dx, ax

        ; Enable with LFB
        mov dx, VBE_DISPI_IOPORT_INDEX
        mov ax, VBE_DISPI_INDEX_ENABLE
        out dx, ax
        mov dx, VBE_DISPI_IOPORT_DATA
        mov ax, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED
        out dx, ax

        ; Map enough framebuffer pages to cover the validated mode.
        ; pages = ceil(width * height * Bpp / 4096), capped at VBE_MAX_FB_PAGES.
        ; Mode validation above already bounds width/height/bpp so the multiply
        ; cannot overflow 32 bits.
        mov eax, [vbe_lfb_addr]         ; Start virtual = physical (identity map)
        mov ebx, eax                    ; Physical address
        push eax
        push ebx
        mov eax, [vbe_width]
        imul eax, [vbe_height]
        imul eax, [vbe_Bpp]
        add eax, 4095
        shr eax, 12                     ; round up to page count
        cmp eax, VBE_MAX_FB_PAGES
        jbe .map_fb_pages_ok
        mov eax, VBE_MAX_FB_PAGES
.map_fb_pages_ok:
        mov ecx, eax                    ; ECX = page count
        pop ebx
        pop eax
.map_fb_loop:
        push ecx
        mov ecx, 0x07                   ; PG_PRESENT | PG_WRITABLE | PG_USER
        call paging_map_page
        pop ecx
        add eax, 4096
        add ebx, 4096
        dec ecx
        jnz .map_fb_loop

        mov byte [vbe_active], 1

        ; Update mouse bounds to match the new resolution
        mov eax, [vbe_width]
        dec eax
        mov [mouse_max_x], eax
        mov eax, [vbe_height]
        dec eax
        mov [mouse_max_y], eax

        ; Allocate shadow buffer for double buffering.
        ; Reallocate whenever the current mode needs more pages than previously allocated.
        ; pages = ceil(width * height * Bpp / 4096)
        mov eax, [vbe_width]
        imul eax, [vbe_height]
        imul eax, [vbe_Bpp]
        add eax, 4095
        shr eax, 12             ; round up to page count
        cmp eax, [vbe_shadow_pages]
        jle .shadow_ready       ; existing allocation is large enough — reuse it
        ; Need a larger buffer: free the old one first to avoid leaking pages
        ; on repeated mode growth, then allocate fresh.
        push eax                ; save new page count
        mov eax, [vbe_shadow_buf]
        test eax, eax
        jz .shadow_alloc_new
        mov ecx, [vbe_shadow_pages]
        test ecx, ecx
        jz .shadow_alloc_new
        call pmm_free_pages
        mov dword [vbe_shadow_buf], 0
        mov dword [vbe_shadow_pages], 0
.shadow_alloc_new:
        pop eax                 ; restore new page count
        mov ecx, eax
        call pmm_alloc_pages
        test eax, eax
        jz .shadow_ready        ; alloc failed — leave shadow_buf=0
        mov [vbe_shadow_buf], eax
        mov [vbe_shadow_pages], ecx
.shadow_ready:
        popad
        ret

;---------------------------------------
; vbe_restore_text - Switch back to 80x25 text mode
;---------------------------------------
vbe_restore_text:
        pushad

        ; Reset any bank/panning state first so the adapter cleanly
        ; returns from framebuffer mode to legacy VGA text mode.
        mov dx, VBE_DISPI_IOPORT_INDEX
        mov ax, VBE_DISPI_INDEX_BANK
        out dx, ax
        mov dx, VBE_DISPI_IOPORT_DATA
        xor ax, ax
        out dx, ax

        mov dx, VBE_DISPI_IOPORT_INDEX
        mov ax, VBE_DISPI_INDEX_X_OFF
        out dx, ax
        mov dx, VBE_DISPI_IOPORT_DATA
        xor ax, ax
        out dx, ax

        mov dx, VBE_DISPI_IOPORT_INDEX
        mov ax, VBE_DISPI_INDEX_Y_OFF
        out dx, ax
        mov dx, VBE_DISPI_IOPORT_DATA
        xor ax, ax
        out dx, ax

        ; Disable BGA
        mov dx, VBE_DISPI_IOPORT_INDEX
        mov ax, VBE_DISPI_INDEX_ENABLE
        out dx, ax
        mov dx, VBE_DISPI_IOPORT_DATA
        mov ax, VBE_DISPI_DISABLED
        out dx, ax

        ; Reprogram VGA registers for mode 3 (80x25 text)
        ; Misc Output Register
        mov dx, 0x3C2
        mov al, 0x67
        out dx, al

        ; Sequencer: reset, clocking, map mask, char map, memory mode
        mov dx, 0x3C4
        mov al, 0x00
        out dx, al
        mov dx, 0x3C5
        mov al, 0x03
        out dx, al
        mov dx, 0x3C4
        mov al, 0x01
        out dx, al
        mov dx, 0x3C5
        mov al, 0x00
        out dx, al
        mov dx, 0x3C4
        mov al, 0x02
        out dx, al
        mov dx, 0x3C5
        mov al, 0x03
        out dx, al
        mov dx, 0x3C4
        mov al, 0x03
        out dx, al
        mov dx, 0x3C5
        mov al, 0x00
        out dx, al
        mov dx, 0x3C4
        mov al, 0x04
        out dx, al
        mov dx, 0x3C5
        mov al, 0x02
        out dx, al

        ; Fully unlock the CRTC register block before programming mode 3.
        ; Without this, some adapters keep parts of the old graphics timing.
        mov dx, 0x3D4
        mov al, 0x03
        out dx, al
        mov dx, 0x3D5
        in al, dx
        or al, 0x80
        out dx, al

        mov dx, 0x3D4
        mov al, 0x11
        out dx, al
        mov dx, 0x3D5
        in al, dx
        and al, 0x7F
        out dx, al

        ; CRTC registers for 80x25 (standard mode 3 timings)
        mov esi, vbe_crtc_mode3
        xor ecx, ecx
.crtc_loop:
        cmp ecx, 25
        jge .crtc_done
        mov dx, 0x3D4
        mov al, cl
        out dx, al
        mov dx, 0x3D5
        mov al, [esi + ecx]
        out dx, al
        inc ecx
        jmp .crtc_loop
.crtc_done:

        ; GC registers
        mov esi, vbe_gc_mode3
        xor ecx, ecx
.gc_loop:
        cmp ecx, 9
        jge .gc_done
        mov dx, 0x3CE
        mov al, cl
        out dx, al
        mov dx, 0x3CF
        mov al, [esi + ecx]
        out dx, al
        inc ecx
        jmp .gc_loop
.gc_done:

        ; AC registers
        mov dx, 0x3DA
        in al, dx               ; Reset AC flip-flop
        xor ecx, ecx
.ac_loop:
        cmp ecx, 21
        jge .ac_done
        mov dx, 0x3C0
        mov al, cl
        out dx, al
        mov al, [vbe_ac_mode3 + ecx]
        out dx, al
        inc ecx
        jmp .ac_loop
.ac_done:
        ; Re-enable video
        mov dx, 0x3C0
        mov al, 0x20
        out dx, al

        ; Clear VGA text buffer
        mov edi, VGA_BASE
        mov ecx, VGA_SIZE / 4
        mov eax, 0x07200720     ; Two spaces with default attribute
        rep stosd

        mov byte [vbe_active], 0
        mov dword [vbe_width], VGA_WIDTH
        mov dword [vbe_height], VGA_HEIGHT
        mov dword [vbe_bpp], 0
        mov dword [vbe_Bpp], 0
        mov dword [vbe_pitch], VGA_WIDTH * 2

        ; Restore mouse bounds to text-mode range
        mov dword [mouse_max_x], 79
        mov dword [mouse_max_y], 24

        ; Restore VGA font into plane 2
        cmp byte [vbe_font_saved], 1
        jne .skip_font_restore
        call vbe_restore_vga_font
.skip_font_restore:

        ; Reset VGA cursor
        mov dword [vga_cursor_x], 0
        mov dword [vga_cursor_y], 0
        call vga_update_cursor

        popad
        ret

;---------------------------------------
; vbe_save_vga_font - Read font from VGA plane 2
;---------------------------------------
vbe_save_vga_font:
        pushad
        ; Select font plane 2 exactly as VGA expects when accessing the
        ; built-in text font at A0000. The previous code only changed the
        ; read map and produced corrupted saved glyphs.

        ; Sequencer: write plane 2 only
        mov dx, 0x3C4
        mov al, 0x02
        out dx, al
        mov dx, 0x3C5
        mov al, 0x04
        out dx, al

        ; Sequencer: disable odd/even, enable sequential access
        mov dx, 0x3C4
        mov al, 0x04
        out dx, al
        mov dx, 0x3C5
        mov al, 0x06
        out dx, al

        ; Graphics controller: read plane 2 in write mode 0 from A0000
        mov dx, 0x3CE
        mov al, 0x04
        out dx, al
        mov dx, 0x3CF
        mov al, 0x02
        out dx, al

        mov dx, 0x3CE
        mov al, 0x05
        out dx, al
        mov dx, 0x3CF
        mov al, 0x00
        out dx, al

        mov dx, 0x3CE
        mov al, 0x06
        out dx, al
        mov dx, 0x3CF
        mov al, 0x00
        out dx, al

        ; Copy 256 glyphs x 32 bytes each
        mov esi, 0xA0000
        mov edi, vbe_font_save_buf
        mov ecx, 8192 / 4
        cld
        rep movsd

        ; Restore normal text-mode memory access
        mov dx, 0x3C4
        mov al, 0x02
        out dx, al
        mov dx, 0x3C5
        mov al, 0x03
        out dx, al

        mov dx, 0x3C4
        mov al, 0x04
        out dx, al
        mov dx, 0x3C5
        mov al, 0x02
        out dx, al

        mov dx, 0x3CE
        mov al, 0x04
        out dx, al
        mov dx, 0x3CF
        mov al, 0x00
        out dx, al

        mov dx, 0x3CE
        mov al, 0x05
        out dx, al
        mov dx, 0x3CF
        mov al, 0x10
        out dx, al

        mov dx, 0x3CE
        mov al, 0x06
        out dx, al
        mov dx, 0x3CF
        mov al, 0x0E
        out dx, al
        popad
        ret

;---------------------------------------
; vbe_restore_vga_font - Write font back to VGA plane 2
;---------------------------------------
vbe_restore_vga_font:
        pushad
        ; Sequencer: Map Mask = plane 2 only
        mov dx, 0x3C4
        mov al, 0x02
        out dx, al
        mov dx, 0x3C5
        mov al, 0x04            ; plane 2
        out dx, al
        ; Sequencer: sequential access mode
        mov dx, 0x3C4
        mov al, 0x04
        out dx, al
        mov dx, 0x3C5
        mov al, 0x06            ; sequential + extended memory
        out dx, al
        ; GC: disable odd/even, set write mode 0
        mov dx, 0x3CE
        mov al, 0x05
        out dx, al
        mov dx, 0x3CF
        mov al, 0x00
        out dx, al
        mov dx, 0x3CE
        mov al, 0x06
        out dx, al
        mov dx, 0x3CF
        mov al, 0x00            ; no odd/even, no chain, map at A0000
        out dx, al
        ; Copy saved font data to plane 2
        mov esi, vbe_font_save_buf
        mov edi, 0xA0000
        mov ecx, 8192 / 4
        cld
        rep movsd
        ; Restore Sequencer: Map Mask = all planes
        mov dx, 0x3C4
        mov al, 0x02
        out dx, al
        mov dx, 0x3C5
        mov al, 0x03            ; planes 0+1 (text mode normal)
        out dx, al
        ; Restore Sequencer: odd/even mode
        mov dx, 0x3C4
        mov al, 0x04
        out dx, al
        mov dx, 0x3C5
        mov al, 0x02
        out dx, al
        ; Restore GC registers for text mode
        mov dx, 0x3CE
        mov al, 0x05
        out dx, al
        mov dx, 0x3CF
        mov al, 0x10            ; odd/even mode
        out dx, al
        mov dx, 0x3CE
        mov al, 0x06
        out dx, al
        mov dx, 0x3CF
        mov al, 0x0E            ; odd/even + map at B8000
        out dx, al
        popad
        ret

; Standard mode 3 CRTC register values (indices 0-24)
vbe_crtc_mode3:
        db 0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F
        db 0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50
        db 0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3
        db 0xFF
; Standard mode 3 GC register values (indices 0-8)
vbe_gc_mode3:
        db 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF
; Standard mode 3 AC register values (indices 0-20)
vbe_ac_mode3:
        db 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07
        db 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F
        db 0x0C, 0x00, 0x0F, 0x08, 0x00

;---------------------------------------
; vbe_putpixel - Draw a pixel
; EAX = x, EBX = y, ECX = colour (32-bit ARGB)
; Silently clipped to current framebuffer bounds.
;---------------------------------------
vbe_putpixel:
        pushad
        ; Reject negative coords (signed compare) and coords >= dim
        cmp eax, 0
        jl .pp_clip
        cmp eax, [vbe_width]
        jge .pp_clip
        cmp ebx, 0
        jl .pp_clip
        cmp ebx, [vbe_height]
        jge .pp_clip
        ; offset = y * pitch + x * Bpp
        imul ebx, [vbe_pitch]
        imul eax, [vbe_Bpp]
        add ebx, eax
        ; Write to shadow buffer if one is allocated; otherwise write directly
        ; to the LFB.  This makes all draw primitives (draw_line, fill_triangle,
        ; sprite_blit, etc.) honour double-buffering automatically.
        mov eax, [vbe_shadow_buf]
        test eax, eax
        jnz .pp_use_buf
        mov eax, [vbe_lfb_addr]
.pp_use_buf:
        add ebx, eax
        mov [ebx], ecx
        popad
        ret
.pp_clip:
        popad
        ret

;---------------------------------------
; vbe_fill_rect - Fill a rectangle with colour
; EAX = x, EBX = y, ECX = width, EDX = height, ESI = colour
; Rect is clipped to framebuffer bounds; out-of-bounds rects become no-ops.
; Prevents y*pitch overflow that previously caused page faults.
;---------------------------------------
vbe_fill_rect:
        pushad
        ; --- Clip to framebuffer bounds ---
        ; Reject if rect entirely outside (x>=w or y>=h or x+w<=0 or y+h<=0)
        cmp byte [vbe_active], 0
        je .fr_skip
        cmp dword [vbe_width], 0
        je .fr_skip
        cmp dword [vbe_height], 0
        je .fr_skip
        ; If width/height <= 0 nothing to do (treat as unsigned)
        cmp ecx, 0
        jle .fr_skip
        cmp edx, 0
        jle .fr_skip
        ; Clip left: if eax<0, shift width and set eax=0
        cmp eax, 0
        jge .fr_no_clip_left
        add ecx, eax            ; width += x (x is negative)
        jle .fr_skip
        xor eax, eax
.fr_no_clip_left:
        ; Clip top: if ebx<0, shift height and set ebx=0
        cmp ebx, 0
        jge .fr_no_clip_top
        add edx, ebx            ; height += y (y is negative)
        jle .fr_skip
        xor ebx, ebx
.fr_no_clip_top:
        ; Clip right: if x+w > width, w = width - x
        push eax
        add eax, ecx            ; eax = x + w
        cmp eax, [vbe_width]
        jle .fr_no_clip_right
        mov ecx, [vbe_width]
        sub ecx, [esp]          ; ecx = width - x
        jle .fr_skip_pop
.fr_no_clip_right:
        pop eax
        ; Clip bottom: if y+h > height, h = height - y
        push ebx
        add ebx, edx
        cmp ebx, [vbe_height]
        jle .fr_no_clip_bottom
        mov edx, [vbe_height]
        sub edx, [esp]
        jle .fr_skip_pop2
.fr_no_clip_bottom:
        pop ebx
        ; Final sanity: x in [0,width), y in [0,height), w>0, h>0
        cmp eax, [vbe_width]
        jge .fr_skip
        cmp ebx, [vbe_height]
        jge .fr_skip
        cmp ecx, 0
        jle .fr_skip
        cmp edx, 0
        jle .fr_skip
        mov [.fr_x], eax
        mov [.fr_w], ecx
        mov [.fr_color], esi
        jmp .fr_row
.fr_skip_pop2:
        add esp, 4              ; drop saved ebx
.fr_skip_pop:
        add esp, 4              ; drop saved eax
.fr_skip:
        popad
        ret
.fr_row:
        cmp edx, 0
        je .fr_done
        mov ecx, [.fr_w]
        mov eax, [.fr_x]
.fr_col:
        cmp ecx, 0
        je .fr_next_row
        push ebx
        push ecx
        push edx
        mov ecx, [.fr_color]
        call vbe_putpixel
        pop edx
        pop ecx
        pop ebx
        inc eax
        dec ecx
        jmp .fr_col
.fr_next_row:
        inc ebx
        dec edx
        jmp .fr_row
.fr_done:
        popad
        ret
.fr_x: dd 0
.fr_w: dd 0
.fr_color: dd 0

;---------------------------------------
; vbe_clear - Clear screen to a colour
; EAX = colour (32-bit ARGB)
;---------------------------------------
vbe_clear:
        pushad
        mov edi, [vbe_lfb_addr]
        mov ecx, [vbe_width]
        imul ecx, [vbe_height]
        rep stosd
        popad
        ret

;---------------------------------------
; sys_framebuf - SYS_FRAMEBUF syscall handler
; EBX = sub-function:
;   0 = get info -> EAX=shadow_buf (or lfb if no shadow), EBX=width, ECX=height, EDX=bpp
;   1 = set mode: ECX=width, EDX=height, ESI=bpp
;   2 = restore text mode
;   3 = draw string: ECX=x, EDX=y, ESI=str_ptr, EDI=fg_color
;   4 = present frame: vsync wait + blit shadow buf -> LFB
;---------------------------------------
sys_framebuf:
        cmp ebx, 0
        je .fb_info
        cmp ebx, 1
        je .fb_setmode
        cmp ebx, 2
        je .fb_restore
        cmp ebx, 3
        je .fb_drawtext
        cmp ebx, 4
        je .fb_present
        xor eax, eax
        dec eax                         ; EAX = -1
        iretd

.fb_info:
        ; Return shadow buf addr (programs render there; SYS_FRAMEBUF/4 blits to LFB)
        ; Fall back to real LFB if shadow buf not yet allocated.
        mov eax, [vbe_shadow_buf]
        test eax, eax
        jnz .fb_info_ret
        mov eax, [vbe_lfb_addr]         ; fallback: direct LFB
.fb_info_ret:
        mov ebx, [vbe_width]
        mov ecx, [vbe_height]
        mov edx, [vbe_bpp]
        iretd

.fb_setmode:
        cmp byte [vbe_available], 0
        je .fb_novbe
        ; vbe_set_mode(EAX=width, EBX=height, ECX=bpp)
        mov eax, ecx                    ; width (user ECX)
        mov ecx, esi                    ; bpp (user ESI)
        mov ebx, edx                    ; height (user EDX)
        call vbe_set_mode
        xor eax, eax
        iretd

.fb_restore:
        call vbe_restore_text
        xor eax, eax
        iretd

.fb_novbe:
        xor eax, eax
        dec eax                         ; EAX = -1
        iretd

.fb_present:
        ; Blit shadow buffer -> real LFB.
        ; Called once per game frame after all rendering is done.
        ; Note: vsync wait removed — port 0x3DA bit 3 may never toggle
        ; in QEMU BGA mode causing an indefinite hang.
        cmp byte [vbe_active], 0
        je .fb_present_ret
        cmp dword [vbe_shadow_buf], 0
        je .fb_present_ret              ; no shadow buf — nothing to blit
        pushad
        mov esi, [vbe_shadow_buf]
        mov edi, [vbe_lfb_addr]
        mov eax, [vbe_width]
        imul eax, [vbe_height]
        imul eax, [vbe_Bpp]             ; total bytes
        shr eax, 2                      ; convert to dwords
        mov ecx, eax
        cld
        rep movsd
        popad
.fb_present_ret:
        xor eax, eax
        iretd

;---------------------------------------
; SYS_FRAMEBUF(3) — draw null-terminated string directly to LFB
; ECX=x, EDX=y, ESI=str_ptr, EDI=fg_color (32-bit RGB)
; Uses the VGA font saved in vbe_font_save_buf (8x16, 32 bytes/glyph)
;---------------------------------------
.fb_drawtext:
        cmp byte [vbe_active], 0
        je .fb_novbe
        ; Redirect pixel writes to shadow buffer (if allocated) so that
        ; text is included in the double-buffered frame.
        mov eax, [vbe_shadow_buf]
        test eax, eax
        jz .fdt_start
        xchg eax, [vbe_lfb_addr]       ; temporarily swap shadow into lfb_addr
        mov [.fdt_saved_lfb], eax       ; save real LFB addr for restore
.fdt_start:
        pushad
        mov [.fdt_x],   ecx
        mov [.fdt_y],   edx
        mov [.fdt_col], edi
        mov ebp, esi                    ; EBP = string pointer
        mov dword [.fdt_remaining], VBE_MAX_DRAW_STRING

.fdt_char:
        cmp dword [.fdt_remaining], 0
        jle .fdt_done
        dec dword [.fdt_remaining]
        movzx eax, byte [ebp]
        test al, al
        jz .fdt_done
        ; glyph base = vbe_font_save_buf + char*32
        imul eax, 32
        add eax, vbe_font_save_buf      ; EAX = glyph row data pointer
        ; draw 16 rows
        xor edx, edx                    ; row counter

.fdt_row:
        cmp edx, 16
        jge .fdt_next_char
        movzx ecx, byte [eax + edx]    ; row bitmask (8 pixels)
        ; y for this row
        push eax
        push edx
        push ecx
        mov ebx, [.fdt_y]
        add ebx, edx                    ; EBX = screen_y + row
        mov edx, [.fdt_x]              ; EDX = screen_x start
        ; draw 8 bits MSB-first
        xor esi, esi                    ; bit index 0..7

.fdt_bit:
        cmp esi, 8
        jge .fdt_row_done
        test ecx, 0x80
        jz .fdt_skip_pixel
        ; vbe_putpixel(EAX=x, EBX=y, ECX=color)
        push eax
        push ebx
        push ecx
        mov eax, edx
        add eax, esi                    ; x = screen_x + bit
        mov ecx, [.fdt_col]
        call vbe_putpixel
        pop ecx
        pop ebx
        pop eax

.fdt_skip_pixel:
        shl ecx, 1
        inc esi
        jmp .fdt_bit

.fdt_row_done:
        pop ecx
        pop edx
        pop eax
        inc edx
        jmp .fdt_row

.fdt_next_char:
        add dword [.fdt_x], 8          ; advance x by one char width
        inc ebp
        jmp .fdt_char

.fdt_done:
        popad
        ; Restore real LFB addr if we swapped it for shadow buf
        mov eax, [.fdt_saved_lfb]
        test eax, eax
        jz .fdt_ret
        mov [vbe_lfb_addr], eax
        mov dword [.fdt_saved_lfb], 0
.fdt_ret:
        xor eax, eax
        iretd

.fdt_x:         dd 0
.fdt_y:         dd 0
.fdt_col:       dd 0
.fdt_saved_lfb: dd 0
.fdt_remaining: dd 0

;---------------------------------------
; cmd_gui_demo - Shell command `gui`:
; Enter 640x480x32 mode, draw colour bars,
; show mouse cursor, exit on keypress.
;---------------------------------------
cmd_gui_demo:
        pushad
        cmp byte [vbe_available], 0
        je .gui_no_vbe

        ; Switch to 640x480x32
        mov eax, VBE_DEFAULT_WIDTH
        mov ebx, VBE_DEFAULT_HEIGHT
        mov ecx, VBE_DEFAULT_BPP
        call vbe_set_mode

        ; Draw colour bars (8 bars across the screen)
        xor esi, esi            ; bar index
.gui_bar:
        cmp esi, 8
        jge .gui_bars_done

        ; Calculate x = bar_index * 80
        mov eax, esi
        shl eax, 6              ; * 64
        add eax, esi
        shl eax, 1              ; * 80 total (64+16=80, shifted by 1 = mismath)

        ; Simpler: x = esi * 80
        mov eax, esi
        imul eax, 80

        ; Pick colour from table
        mov ecx, [.gui_colors + esi * 4]

        ; Draw bar: x, y=0, w=80, h=480
        push esi
        mov ebx, 0
        mov edx, 480
        mov esi, ecx
        mov ecx, 80
        call vbe_fill_rect
        pop esi

        inc esi
        jmp .gui_bar

.gui_bars_done:
        ; Draw a white banner area at top
        mov eax, 160
        mov ebx, 20
        mov ecx, 320
        mov edx, 40
        mov esi, 0x00FFFFFF
        call vbe_fill_rect

        ; Draw mouse cursor (white 8x8 block at mouse position)
.gui_loop:
        ; Get mouse state
        mov eax, [mouse_x]
        mov ebx, [mouse_y]

        ; Scale mouse coords: mouse is 0-79/0-24, screen is 640x480
        shl eax, 3              ; * 8
        imul ebx, 20            ; * 20 (24 * 20 = 480)

        ; Draw cursor (small white cross)
        mov ecx, 8
        mov edx, 8
        push esi
        mov esi, 0x00FFFFFF
        call vbe_fill_rect
        pop esi

        ; Check for keypress
        call kb_pollchar
        cmp al, 0
        je .gui_loop

        ; Restore text mode
        call vbe_restore_text

        popad
        ret

.gui_no_vbe:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_vbe_no
        call vga_print
        call vga_reset_color
        popad
        ret

; Colour bar table (8 colours)
.gui_colors:
        dd 0x00000000           ; Black
        dd 0x00FF0000           ; Red
        dd 0x0000FF00           ; Green
        dd 0x000000FF           ; Blue
        dd 0x00FFFF00           ; Yellow
        dd 0x00FF00FF           ; Magenta
        dd 0x0000FFFF           ; Cyan
        dd 0x00FFFFFF           ; White

; VBE BSS-style data (initialized to 0 on kernel BSS clear)
vbe_available:      db 0
vbe_active:         db 0
vbe_restore_pending: db 0
                    db 0        ; padding
vbe_lfb_addr:   dd 0
vbe_width:      dd 0
vbe_height:     dd 0
vbe_bpp:        dd 0
vbe_Bpp:        dd 0            ; bytes per pixel
vbe_pitch:      dd 0
vbe_shadow_buf: dd 0            ; shadow/back buffer for double buffering (0 = not allocated)
vbe_shadow_pages: dd 0          ; allocated size of shadow buffer in pages
vbe_font_saved: db 0
        align 4
vbe_font_save_buf: times 8192 db 0  ; 256 chars * 32 bytes/char for VGA font

; -----------------------------------------------------------------------
; Dirty-rectangle tracking (smallest enclosing rectangle of modified area)
; Reset to "nothing dirty" after each present (x0=MAX,y0=MAX,x1=0,y1=0).
; -----------------------------------------------------------------------
vbe_dirty_x0:   dd 0xFFFFFFFF
vbe_dirty_y0:   dd 0xFFFFFFFF
vbe_dirty_x1:   dd 0
vbe_dirty_y1:   dd 0

; PSF2 font support
PSF2_MAGIC          equ 0x864AB572
psf_loaded:         db 0
                    db 0, 0, 0
psf_buf:            dd 0        ; pointer to font data (PMM-allocated)
psf_glyph_size:     dd 0        ; bytes per glyph
psf_num_glyphs:     dd 0
psf_glyph_w:        dd 0
psf_glyph_h:        dd 0
psf_bytes_per_row:  dd 0        ; ceil(width/8)

;=======================================================================
; DRAWING PRIMITIVES — added in v8.0
;=======================================================================

;---------------------------------------
; _vbe_dirty_expand - expand dirty rect to include (EAX=x, EDX=y)
; Preserves all registers.
;---------------------------------------
_vbe_dirty_expand:
        push eax
        push edx
        cmp eax, [vbe_dirty_x0]
        jge .de_x0ok
        mov [vbe_dirty_x0], eax
.de_x0ok:
        cmp eax, [vbe_dirty_x1]
        jle .de_x1ok
        mov [vbe_dirty_x1], eax
.de_x1ok:
        cmp edx, [vbe_dirty_y0]
        jge .de_y0ok
        mov [vbe_dirty_y0], edx
.de_y0ok:
        cmp edx, [vbe_dirty_y1]
        jle .de_y1ok
        mov [vbe_dirty_y1], edx
.de_y1ok:
        pop edx
        pop eax
        ret

;---------------------------------------
; vbe_dirty_reset - mark entire dirty rect as clean
;---------------------------------------
vbe_dirty_reset:
        mov dword [vbe_dirty_x0], 0xFFFFFFFF
        mov dword [vbe_dirty_y0], 0xFFFFFFFF
        mov dword [vbe_dirty_x1], 0
        mov dword [vbe_dirty_y1], 0
        ret

;---------------------------------------
; vbe_draw_line - Bresenham integer line
; EAX=x0, EBX=y0, ECX=x1, EDX=y1, ESI=color
;---------------------------------------
vbe_draw_line:
        pushad
        mov [.x0], eax
        mov [.y0], ebx
        mov [.x1], ecx
        mov [.y1], edx
        mov [.color], esi
        ; adx = abs(x1-x0), sx = sign
        sub ecx, eax
        jge .l_sxp
        neg ecx
        mov dword [.sx], -1
        jmp .l_sxd
.l_sxp: mov dword [.sx], 1
.l_sxd: mov [.adx], ecx
        ; ady = abs(y1-y0), sy = sign
        mov edx, [.y1]
        sub edx, ebx
        jge .l_syp
        neg edx
        mov dword [.sy], -1
        jmp .l_syd
.l_syp: mov dword [.sy], 1
.l_syd: mov [.ady], edx
        ; err = adx - ady
        mov eax, [.adx]
        sub eax, [.ady]
        mov [.err], eax
        mov eax, [.x0]
        mov ebx, [.y0]
.l_loop:
        mov ecx, [.color]
        call vbe_putpixel
        ; expand dirty rect
        mov edx, ebx
        call _vbe_dirty_expand
        cmp eax, [.x1]
        jne .l_cont
        cmp ebx, [.y1]
        je  .l_done
.l_cont:
        mov esi, [.err]
        lea edi, [esi * 2]
        mov ecx, [.ady]
        neg ecx
        cmp edi, ecx
        jle .l_skx
        sub esi, [.ady]
        add eax, [.sx]
.l_skx:
        cmp edi, [.adx]
        jge .l_sky
        add esi, [.adx]
        add ebx, [.sy]
.l_sky:
        mov [.err], esi
        jmp .l_loop
.l_done:
        popad
        ret
.x0: dd 0
.y0: dd 0
.x1: dd 0
.y1: dd 0
.sx: dd 0
.sy: dd 0
.adx: dd 0
.ady: dd 0
.err: dd 0
.color: dd 0

;---------------------------------------
; vbe_fill_triangle - Filled triangle (scanline rasterizer)
; EAX=x0,EBX=y0, ECX=x1,EDX=y1, ESI=x2,EDI=y2
; color is passed on stack: push color before call, then call
;---------------------------------------
vbe_fill_triangle:
        pushad
        mov [.v + 0],  eax
        mov [.v + 4],  ebx
        mov [.v + 8],  ecx
        mov [.v + 12], edx
        mov [.v + 16], esi
        mov [.v + 20], edi
        mov eax, [esp + 36]     ; color arg (pushad=32b + retaddr=4 = 36 from ESP)
        mov [.tc], eax
        call .sort3y
        ; Check degenerate (all same Y)
        mov eax, [.v + 4]
        cmp eax, [.v + 20]
        je  .ft_done
        ; total height = y2 - y0
        mov ecx, [.v + 20]
        sub ecx, [.v + 4]   ; ecx = total_h
        mov ebx, [.v + 4]   ; ebx = current y (row)
.ft_scan:
        cmp ebx, [.v + 20]
        jg  .ft_done
        ; Long edge x: v0.x + (v2.x-v0.x)*(y-v0.y)/total_h
        mov eax, ebx
        sub eax, [.v + 4]       ; eax = y - y0
        mov esi, [.v + 16]
        sub esi, [.v + 0]       ; esi = x2 - x0
        imul esi, eax           ; esi = (x2-x0)*(y-y0)
        mov eax, esi
        cdq
        idiv ecx                ; eax = (x2-x0)*(y-y0)/total_h
        add eax, [.v + 0]       ; eax = long_x
        mov [.lx], eax
        ; Short edge: upper half (y0..y1) or lower half (y1..y2)
        cmp ebx, [.v + 12]
        jge .ft_lower
        ; Upper half: v0 -> v1
        mov edx, [.v + 12]
        sub edx, [.v + 4]       ; edx = seg_h = y1 - y0
        test edx, edx
        jz  .ft_skip_horiz
        mov eax, ebx
        sub eax, [.v + 4]       ; eax = y - y0
        mov esi, [.v + 8]
        sub esi, [.v + 0]       ; esi = x1 - x0
        imul esi, eax
        mov eax, esi
        cdq
        idiv edx
        add eax, [.v + 0]       ; eax = short_x
        jmp .ft_draw_span
.ft_lower:
        ; Lower half: v1 -> v2
        mov edx, [.v + 20]
        sub edx, [.v + 12]      ; edx = seg_h = y2 - y1
        test edx, edx
        jz  .ft_skip_horiz
        mov eax, ebx
        sub eax, [.v + 12]      ; eax = y - y1
        mov esi, [.v + 16]
        sub esi, [.v + 8]       ; esi = x2 - x1
        imul esi, eax
        mov eax, esi
        cdq
        idiv edx
        add eax, [.v + 8]       ; eax = short_x
.ft_draw_span:
        ; EAX = short_x, [.lx] = long_x, EBX = row
        ; Ensure ECX=left_x <= EAX=right_x
        mov ecx, [.lx]
        cmp ecx, eax
        jle .ft_span_ok
        xchg eax, ecx
.ft_span_ok:
        push eax                ; save right_x (loop bound)
.ft_hloop:
        cmp ecx, [esp]          ; left_x <= right_x?
        jg  .ft_hdone
        mov  eax, ecx           ; EAX = current column x
        ; EBX = row y
        mov  [.ft_col], ecx     ; save column
        mov  ecx, [.tc]         ; ECX = fill color
        call vbe_putpixel       ; draw pixel at (eax, ebx) color ecx
        mov  ecx, [.ft_col]     ; restore column
        inc  ecx
        jmp  .ft_hloop
.ft_hdone:
        pop eax                 ; balance push eax
.ft_skip_horiz:
        ; Expand dirty rect
        mov eax, [.lx]
        mov edx, ebx
        call _vbe_dirty_expand
        inc ebx
        jmp .ft_scan
.ft_done:
        popad
        ret

; Sort .v[0..2] by Y ascending (3-pass bubble)
.sort3y:
        ; Pass 1: compare v0.y vs v1.y
        mov eax, [.v + 4]
        cmp eax, [.v + 12]
        jle .s01
        mov ecx, [.v + 0]
        xchg ecx, [.v + 8]
        mov [.v + 0], ecx
        mov ecx, [.v + 4]
        xchg ecx, [.v + 12]
        mov [.v + 4], ecx
.s01:
        ; Pass 2: compare v0.y vs v2.y
        mov eax, [.v + 4]
        cmp eax, [.v + 20]
        jle .s02
        mov ecx, [.v + 0]
        xchg ecx, [.v + 16]
        mov [.v + 0], ecx
        mov ecx, [.v + 4]
        xchg ecx, [.v + 20]
        mov [.v + 4], ecx
.s02:
        ; Pass 3: compare v1.y vs v2.y
        mov eax, [.v + 12]
        cmp eax, [.v + 20]
        jle .s12
        mov ecx, [.v + 8]
        xchg ecx, [.v + 16]
        mov [.v + 8], ecx
        mov ecx, [.v + 12]
        xchg ecx, [.v + 20]
        mov [.v + 12], ecx
.s12:
        ret

; Vertex buffer and scratch vars
.v:      dd 0, 0, 0, 0, 0, 0   ; x0,y0, x1,y1, x2,y2
.lx:     dd 0
.tc:     dd 0
.ft_col: dd 0

;---------------------------------------
; vbe_sprite_blit - Color-key transparent blit
; EBX=src_ptr (32bpp rows), ECX=dst_x, EDX=dst_y, ESI=width, EDI=height
; Color key is read from [sys_blit.bl_key] (set by sys_blit before calling)
; color_key = -1 to disable transparency
;---------------------------------------
vbe_sprite_blit:
        pushad
        cmp byte [vbe_active], 0
        je  .vsb_done
        mov [.sb_src], ebx
        mov [.sb_dx],  ecx
        mov [.sb_dy],  edx
        mov [.sb_w],   esi
        mov [.sb_h],   edi
        ; Row loop
        xor edi, edi            ; row counter
.vsb_row:
        cmp edi, [.sb_h]
        jge .vsb_done
        xor esi, esi            ; col counter
.vsb_col:
        cmp esi, [.sb_w]
        jge .vsb_next_row
        ; Load source pixel (32bpp ARGB or XRGB)
        mov eax, [.sb_src]
        mov ecx, edi
        imul ecx, [.sb_w]
        add  ecx, esi
        mov  eax, [eax + ecx * 4]
        and  eax, 0x00FFFFFF    ; strip alpha
        ; Color key check
        cmp dword [sys_blit.bl_key], -1
        je  .vsb_draw
        cmp eax, [sys_blit.bl_key]
        je  .vsb_skip_px
.vsb_draw:
        push eax
        mov eax, [.sb_dx]
        add eax, esi
        mov ebx, [.sb_dy]
        add ebx, edi
        cmp eax, 0
        jl  .vsb_skip2
        cmp eax, [vbe_width]
        jge .vsb_skip2
        cmp ebx, 0
        jl  .vsb_skip2
        cmp ebx, [vbe_height]
        jge .vsb_skip2
        mov ecx, [esp]          ; restore pixel color
        call vbe_putpixel
        mov edx, ebx
        call _vbe_dirty_expand
.vsb_skip2:
        pop eax
.vsb_skip_px:
        inc esi
        jmp .vsb_col
.vsb_next_row:
        inc edi
        jmp .vsb_row
.vsb_done:
        popad
        ret

.sb_src: dd 0
.sb_dx:  dd 0
.sb_dy:  dd 0
.sb_w:   dd 0
.sb_h:   dd 0

;---------------------------------------
; vbe_dirty_present - Blit shadow -> LFB (full blit, resets dirty rect)
;---------------------------------------
vbe_dirty_present:
        pushad
        cmp byte [vbe_active], 0
        je  .vdp_done
        cmp dword [vbe_shadow_buf], 0
        je  .vdp_done
        cmp dword [vbe_dirty_x0], 0xFFFFFFFF
        je  .vdp_done           ; nothing dirty
        ; Full shadow blit (simple, correct)
        mov esi, [vbe_shadow_buf]
        mov edi, [vbe_lfb_addr]
        mov eax, [vbe_width]
        imul eax, [vbe_height]
        imul eax, [vbe_Bpp]
        shr eax, 2
        mov ecx, eax
        cld
        rep movsd
        call vbe_dirty_reset
.vdp_done:
        popad
        ret

;---------------------------------------
; Syscall handlers for new primitives
;---------------------------------------

; SYS_DRAW_LINE (95): EBX=x0, ECX=y0, EDX=x1, ESI=y1, EDI=color
sys_draw_line:
        cmp byte [vbe_active], 0
        je  .done
        mov eax, ebx
        mov ebx, ecx
        mov ecx, edx
        mov edx, esi
        mov esi, edi
        call vbe_draw_line
.done:  xor eax, eax
        iretd

; SYS_DRAW_TRIANGLE (96): EBX=x0|(y0<<16), ECX=x1|(y1<<16), EDX=x2|(y2<<16), ESI=color
sys_draw_triangle:
        cmp byte [vbe_active], 0
        je  .sdt_done
        ; Unpack packed coords into scratch vars using EAX/EDI as temp
        movzx eax, bx
        mov [.t_x0], eax
        mov eax, ebx
        shr eax, 16
        mov [.t_y0], eax
        movzx eax, cx
        mov [.t_x1], eax
        mov eax, ecx
        shr eax, 16
        mov [.t_y1], eax
        movzx eax, dx
        mov [.t_x2], eax
        mov eax, edx
        shr eax, 16
        mov [.t_y2], eax
        mov [.t_col], esi
        ; Set up all six register args then push color and call
        mov eax, [.t_x0]
        mov ebx, [.t_y0]
        mov ecx, [.t_x1]
        mov edx, [.t_y1]
        mov esi, [.t_x2]
        mov edi, [.t_y2]
        push dword [.t_col]
        call vbe_fill_triangle
        add esp, 4
.sdt_done:
        xor eax, eax
        iretd
.t_x0: dd 0
.t_y0: dd 0
.t_x1: dd 0
.t_y1: dd 0
.t_x2: dd 0
.t_y2: dd 0
.t_col: dd 0

; SYS_BLIT (97): EBX=src_ptr, ECX=dst_x, EDX=dst_y, ESI=width, EDI=height
;               color_key passed as dword at [ESP+4] before syscall (user pushes it)
;               Simplified: treat EDI>>16 as height, low 16 of EDI as color_key flag
;               Actual: EBX=src, ECX=dx, EDX=dy, ESI=w|(h<<16), EDI=colorkey
sys_blit:
        cmp byte [vbe_active], 0
        je  .sbl_done
        mov eax, ebx
        call validate_user_ptr
        jc  .sbl_done
        ; Unpack width/height from ESI
        movzx eax, si
        mov [.bl_src], ebx
        mov [.bl_dx],  ecx
        mov [.bl_dy],  edx
        mov [.bl_w],   eax
        mov eax, esi
        shr eax, 16
        mov [.bl_h],   eax
        mov [.bl_key], edi
        ; Set up register args for vbe_sprite_blit
        mov ebx, [.bl_src]
        mov ecx, [.bl_dx]
        mov edx, [.bl_dy]
        mov esi, [.bl_w]
        mov edi, [.bl_h]
        call vbe_sprite_blit
.sbl_done:
        xor eax, eax
        iretd
.bl_src: dd 0
.bl_dx:  dd 0
.bl_dy:  dd 0
.bl_w:   dd 0
.bl_h:   dd 0
.bl_key: dd 0

; SYS_DIRTY_PRESENT (98): present dirty rect, no args
sys_dirty_present:
        call vbe_dirty_present
        xor eax, eax
        iretd

;=======================================================================
; PSF2 FONT SUPPORT — v8.0
;=======================================================================

; SYS_PSF_LOAD (99): EBX=filename_ptr -> EAX=0/-1
sys_psf_load:
        pushad
        mov eax, ebx
        call validate_user_ptr
        jc  .psf_l_fail
        ; Allocate 256 KB for font data
        mov ecx, 64
        call pmm_alloc_pages
        test eax, eax
        jz  .psf_l_fail
        mov [psf_buf], eax
        ; Load file
        mov esi, ebx
        mov edi, [psf_buf]
        call hbfs_read_file
        jc  .psf_l_free
        ; Verify magic
        mov edi, [psf_buf]
        cmp dword [edi], PSF2_MAGIC
        jne .psf_l_free
        ; Parse header: header_size at +8, num_glyphs at +16, glyph_size at +20,
        ;               height at +24, width at +28
        mov eax, [edi + 16]
        mov [psf_num_glyphs], eax
        mov eax, [edi + 20]
        mov [psf_glyph_size], eax
        mov eax, [edi + 24]
        mov [psf_glyph_h], eax
        mov eax, [edi + 28]
        mov [psf_glyph_w], eax
        add eax, 7
        shr eax, 3
        mov [psf_bytes_per_row], eax
        mov byte [psf_loaded], 1
        popad
        xor eax, eax
        iretd
.psf_l_free:
        mov eax, [psf_buf]
        mov ecx, 64
        call pmm_free_pages
        mov dword [psf_buf], 0
.psf_l_fail:
        popad
        mov eax, -1
        iretd

; SYS_PSF_CHAR (100): EBX=x, ECX=y, EDX=codepoint, ESI=fg_color
sys_psf_char:
        cmp byte [vbe_active], 0
        je  .spc_done
        cmp byte [psf_loaded], 0
        je  .spc_vga_fb          ; fall back to SYS_FRAMEBUF text draw
        ; Bounds-check codepoint
        cmp edx, [psf_num_glyphs]
        jae .spc_done
        ; Glyph pointer
        push esi
        mov esi, [psf_buf]
        mov eax, [esi + 8]      ; header_size
        add esi, eax            ; skip header
        mov eax, [psf_glyph_size]
        imul eax, edx           ; codepoint * bytes_per_glyph
        add esi, eax            ; esi = glyph data
        ; Save args
        mov [.spc_x], ebx
        mov [.spc_y], ecx
        pop eax
        mov [.spc_fg], eax
        ; Render glyph rows
        xor edi, edi            ; row counter
.spc_row:
        cmp edi, [psf_glyph_h]
        jge .spc_done
        xor ecx, ecx            ; col counter
        push esi
.spc_byte_loop:
        mov eax, ecx
        shr eax, 3              ; byte index = col/8
        cmp eax, [psf_bytes_per_row]
        jge .spc_next_row
        movzx edx, byte [esi + eax]
        ; bit within byte: 7 - (col mod 8) for MSB-first
        mov eax, ecx
        and eax, 7
        mov ebx, 7
        sub ebx, eax
        bt  edx, ebx            ; test bit
        jnc .spc_no_px
        ; Check within glyph width
        cmp ecx, [psf_glyph_w]
        jge .spc_no_px
        push ecx
        push edi
        mov eax, [.spc_x]
        add eax, ecx
        mov ebx, [.spc_y]
        add ebx, edi
        mov ecx, [.spc_fg]
        call vbe_putpixel
        mov edx, ebx
        call _vbe_dirty_expand
        pop edi
        pop ecx
.spc_no_px:
        inc ecx
        cmp ecx, [psf_glyph_w]
        jl  .spc_byte_loop
.spc_next_row:
        pop esi
        add esi, [psf_bytes_per_row]
        inc edi
        jmp .spc_row
.spc_vga_fb:
        ; Reuse vbe_draw_text path: store single char string and call sys_framebuf
        ; Simple: just use SYS_FRAMEBUF draw text call path
        ; (psf not loaded — silently do nothing; caller should use SYS_FRAMEBUF/3)
.spc_done:
        xor eax, eax
        iretd

.spc_x:  dd 0
.spc_y:  dd 0
.spc_fg: dd 0
