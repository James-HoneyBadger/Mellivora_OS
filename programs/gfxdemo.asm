; gfxdemo.asm  —  VBE graphics demo for Mellivora OS
;
; Uses ONLY direct pixel writes to the shadow buffer (same pattern as
; doomfire.asm — the proven working method).  No kernel draw syscalls.
;
; Renders:
;   - Animated scrolling rainbow-gradient background
;   - Bouncing 40x40 bright-white square sprite
;   - Title text overlay via SYS_FRAMEBUF(3)
;   - SYS_FRAMEBUF(4) flip each frame
;
; Press any key to exit.

%include "syscalls.inc"

SCR_W   equ 640
SCR_H   equ 480
PITCH   equ SCR_W * 4

SPR_W   equ 40
SPR_H   equ 40

start:
        ; ---- Set VBE mode 640x480x32 ----
        mov eax, SYS_FRAMEBUF
        mov ebx, 1
        mov ecx, SCR_W
        mov edx, SCR_H
        mov esi, 32
        int 0x80
        cmp eax, -1
        je  .novbe

        ; ---- Get shadow buffer address ----
        mov eax, SYS_FRAMEBUF
        xor ebx, ebx
        int 0x80
        mov [fb_addr],  eax
        mov dword [fb_pitch], PITCH

        ; ---- Initialise sprite state ----
        mov dword [sx],  80
        mov dword [sy],  60
        mov dword [sdx],  5
        mov dword [sdy],  3

.frame:
        ; Non-blocking key check
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jnz .exit

        ; --- Phase 1: animated rainbow-gradient background ---
        ; Each row colour = hue_to_rgb((row*2 + tick) & 0xFF)
        xor ebx, ebx
.bg_row:
        cmp ebx, SCR_H
        jge .bg_done
        mov eax, ebx
        shl eax, 1
        add eax, [tick]
        and eax, 0xFF
        push ebx               ; save row counter — hue_to_rgb clobbers EBX
        call hue_to_rgb
        pop ebx                ; restore row counter
        mov ecx, ebx
        imul ecx, PITCH
        add ecx, [fb_addr]
        mov edx, SCR_W
.bg_px:
        mov [ecx], eax
        add ecx, 4
        dec edx
        jnz .bg_px
        inc ebx
        jmp .bg_row
.bg_done:

        ; --- Phase 2: bouncing 40x40 white square ---
        xor esi, esi
.spr_row:
        cmp esi, SPR_H
        jge .spr_done
        mov eax, [sy]
        add eax, esi
        cmp eax, SCR_H
        jge .spr_done
        imul eax, PITCH
        add eax, [fb_addr]
        mov ecx, [sx]
        cmp ecx, SCR_W
        jge .spr_done
        shl ecx, 2
        add eax, ecx
        mov ecx, SPR_W
.spr_px:
        mov dword [eax], 0x00FFFFFF
        add eax, 4
        dec ecx
        jnz .spr_px
        inc esi
        jmp .spr_row
.spr_done:

        ; --- Phase 3: title text ---
        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 160
        mov edx, 8
        mov esi, title_str
        mov edi, 0x00FFFFFF
        int 0x80

        mov eax, SYS_FRAMEBUF
        mov ebx, 3
        mov ecx, 184
        mov edx, 20
        mov esi, sub_str
        mov edi, 0x00FFFF00
        int 0x80

        ; --- Phase 4: flip shadow -> LFB ---
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80

        ; --- Phase 5: update sprite position ---
        mov eax, [sx]
        add eax, [sdx]
        cmp eax, 0
        jge .sx_lo_ok
        xor eax, eax
        neg dword [sdx]
        jmp .sx_store
.sx_lo_ok:
        cmp eax, SCR_W - SPR_W
        jle .sx_store
        mov eax, SCR_W - SPR_W
        neg dword [sdx]
.sx_store:
        mov [sx], eax
        mov eax, [sy]
        add eax, [sdy]
        cmp eax, 0
        jge .sy_lo_ok
        xor eax, eax
        neg dword [sdy]
        jmp .sy_store
.sy_lo_ok:
        cmp eax, SCR_H - SPR_H
        jle .sy_store
        mov eax, SCR_H - SPR_H
        neg dword [sdy]
.sy_store:
        mov [sy], eax

        inc dword [tick]
        mov eax, SYS_SLEEP
        mov ebx, 1
        int 0x80
        jmp .frame

.exit:
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80
.novbe:
        mov eax, SYS_PRINT
        mov ebx, msg_exit
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================================================
; hue_to_rgb  —  EAX=hue (0..255) -> EAX=0x00RRGGBB
;=======================================================================
hue_to_rgb:
        cmp eax, 85
        jl  .sec0
        cmp eax, 170
        jl  .sec1
        ; Sector 2 (170..254): R rises, G falls, B=0
        sub eax, 170
        imul ecx, eax, 3
        and ecx, 0xFF
        imul edx, eax, 3
        mov ebx, 255
        sub ebx, edx
        and ebx, 0xFF
        shl ecx, 16
        shl ebx, 8
        mov eax, ecx
        or  eax, ebx
        ret
.sec0:  ; Sector 0 (0..84): R falls, G=0, B rises
        imul ecx, eax, 3
        and ecx, 0xFF
        imul edx, eax, 3
        mov eax, 255
        sub eax, edx
        and eax, 0xFF
        shl eax, 16
        or  eax, ecx
        ret
.sec1:  ; Sector 1 (85..169): R=0, G rises, B falls
        sub eax, 85
        imul ecx, eax, 3
        and ecx, 0xFF
        imul edx, eax, 3
        mov eax, 255
        sub eax, edx
        and eax, 0xFF
        shl ecx, 8
        or  eax, ecx
        ret

;=======================================================================
fb_addr:  dd 0
fb_pitch: dd 0
tick:     dd 0
sx:       dd 0
sy:       dd 0
sdx:      dd 0
sdy:      dd 0

title_str: db "MELLIVORA OS  -  VBE GRAPHICS DEMO", 0
sub_str:   db "press any key to exit", 0
msg_exit:  db "gfxdemo: done.", 0x0A, 0
