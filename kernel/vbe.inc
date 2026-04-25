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
; SYS_FRAMEBUF (37): EBX=0 get info (ret EAX=addr,EBX=w,ECX=h,EDX=bpp)
;                     EBX=1 set mode  ECX=width EDX=height ESI=bpp
;                     EBX=2 restore text mode
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
;---------------------------------------
vbe_set_mode:
        pushad
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

        ; Map 8 MB of framebuffer into virtual address space
        ; Map LFB physical pages starting at vbe_lfb_addr
        mov eax, [vbe_lfb_addr]         ; Start virtual = physical (identity map)
        mov ebx, eax                    ; Physical address
        mov ecx, 2048                   ; 2048 pages = 8 MB
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
;---------------------------------------
vbe_putpixel:
        pushad
        ; offset = y * pitch + x * Bpp
        imul ebx, [vbe_pitch]
        imul eax, [vbe_Bpp]
        add ebx, eax
        add ebx, [vbe_lfb_addr]
        mov [ebx], ecx
        popad
        ret

;---------------------------------------
; vbe_fill_rect - Fill a rectangle with colour
; EAX = x, EBX = y, ECX = width, EDX = height, ESI = colour
;---------------------------------------
vbe_fill_rect:
        pushad
        mov [.fr_x], eax
        mov [.fr_w], ecx
        mov [.fr_color], esi
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
;   0 = get info -> EAX=lfb_addr, EBX=width, ECX=height, EDX=bpp
;   1 = set mode: ECX=width, EDX=height, ESI=bpp
;   2 = restore text mode
;   3 = draw string: ECX=x, EDX=y, ESI=str_ptr, EDI=fg_color
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
        xor eax, eax
        dec eax                         ; EAX = -1
        iretd

.fb_info:
        ; Return: EAX=lfb_addr, EBX=width, ECX=height, EDX=bpp
        mov eax, [vbe_lfb_addr]
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

;---------------------------------------
; SYS_FRAMEBUF(3) — draw null-terminated string directly to LFB
; ECX=x, EDX=y, ESI=str_ptr, EDI=fg_color (32-bit RGB)
; Uses the VGA font saved in vbe_font_save_buf (8x16, 32 bytes/glyph)
;---------------------------------------
.fb_drawtext:
        cmp byte [vbe_active], 0
        je .fb_novbe
        pushad
        mov [.fdt_x],   ecx
        mov [.fdt_y],   edx
        mov [.fdt_col], edi
        mov ebp, esi                    ; EBP = string pointer

.fdt_char:
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
        xor eax, eax
        iretd

.fdt_x:   dd 0
.fdt_y:   dd 0
.fdt_col: dd 0

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
        mov byte [vga_color], COLOR_DEFAULT
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
vbe_font_saved: db 0
        align 4
vbe_font_save_buf: times 8192 db 0  ; 256 chars * 32 bytes/char for VGA font
