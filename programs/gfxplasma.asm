; gfxplasma.asm — VBE animated plasma effect for Mellivora OS
;
; Classic plasma: sum of sine waves evaluated per pixel, mapped to
; a rotating rainbow palette.  Renders 320×240 → 2×2 blocks → 640×480.
; ~60 frames of smooth animation then loops.  Press any key to exit.

%include "syscalls.inc"

SCR_W    equ 640
SCR_H    equ 480
PITCH    equ SCR_W * 4
GRID_W   equ 320
GRID_H   equ 240

start:
        ; ---- VBE mode ----
        mov eax, SYS_FRAMEBUF
        mov ebx, 1
        mov ecx, SCR_W
        mov edx, SCR_H
        mov esi, 32
        int 0x80
        cmp eax, -1
        je .novbe

        mov eax, SYS_FRAMEBUF
        xor ebx, ebx
        int 0x80
        mov [fb], eax

        mov dword [tick], 0

.frame:
        ; Non-blocking key check
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jnz .exit

        ; Render
        xor esi, esi            ; y = 0
.row:
        cmp esi, GRID_H
        jge .row_done

        xor edi, edi            ; x = 0
.col:
        cmp edi, GRID_W
        jge .col_done

        ; plasma value = sin(x/4 + t) + sin(y/4 + t) + sin((x+y)/8 + t*2)
        ; Use LUT: sin256[i] = sin(i/256 * 2π) * 127 + 128  → 0..255
        ; Component 1: sin(x/4 + tick)
        mov eax, edi
        shr eax, 2
        add eax, [tick]
        and eax, 0xFF
        movzx eax, byte [sin256 + eax]   ; 0..255

        ; Component 2: sin(y/4 + tick)
        mov ebx, esi
        shr ebx, 2
        add ebx, [tick]
        and ebx, 0xFF
        movzx ebx, byte [sin256 + ebx]
        add eax, ebx

        ; Component 3: sin((x+y)/8 + tick*2)
        mov ebx, edi
        add ebx, esi
        shr ebx, 3
        mov ecx, [tick]
        shl ecx, 1
        add ebx, ecx
        and ebx, 0xFF
        movzx ebx, byte [sin256 + ebx]
        add eax, ebx

        ; Sum in range 0..765; normalize to 0..255
        ; Simple: divide by 3 (shift right + approximation: (sum * 86) >> 8)
        imul eax, 86
        shr eax, 8              ; eax ≈ sum/3  (0..255)
        and eax, 0xFF

        ; Palette rotation: add tick to hue
        add eax, [tick]
        and eax, 0xFF

        ; Map hue to RGB
        push esi
        push edi
        call hue_to_rgb
        pop edi
        pop esi

        ; Plot 2×2 block at (edi*2, esi*2)
        mov ebx, esi
        shl ebx, 1              ; py = y * 2
        imul ebx, ebx, PITCH
        add ebx, [fb]
        mov ecx, edi
        shl ecx, 3              ; px offset = x * 2 * 4
        add ebx, ecx

        mov [ebx], eax
        mov [ebx + 4], eax
        mov [ebx + PITCH], eax
        mov [ebx + PITCH + 4], eax

        inc edi
        jmp .col

.col_done:
        inc esi
        jmp .row

.row_done:
        ; blit
        mov eax, SYS_FRAMEBUF
        mov ebx, 4
        int 0x80

        inc dword [tick]

        mov eax, SYS_SLEEP
        mov ebx, 1
        int 0x80

        jmp .frame

.exit:
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80
        jmp .done

.novbe:
        mov eax, SYS_PRINT
        mov ebx, msg_novbe
        int 0x80
.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================================================
; hue_to_rgb  EAX=hue(0-255) → EAX=0x00RRGGBB  (clobbers ECX,EDX,EBX)
;=======================================================================
hue_to_rgb:
        cmp eax, 85
        jl  .sec0
        cmp eax, 170
        jl  .sec1
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
.sec0:
        imul ecx, eax, 3
        and ecx, 0xFF
        imul edx, eax, 3
        mov eax, 255
        sub eax, edx
        and eax, 0xFF
        shl eax, 16
        or  eax, ecx
        ret
.sec1:
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
; sin256 LUT — 256 entries, sin(i/256 * 2π) * 127 + 128 → 0..255
;=======================================================================
sin256:
        db 128,131,134,137,140,143,146,149,152,155,158,161,164,167,170,173
        db 176,178,181,184,186,189,191,194,196,198,201,203,205,207,209,211
        db 213,215,216,218,220,221,222,224,225,226,227,228,229,230,231,231
        db 232,232,233,233,234,234,234,234,234,234,234,234,233,233,232,232
        db 231,231,230,229,228,227,226,225,224,222,221,220,218,216,215,213
        db 211,209,207,205,203,201,198,196,194,191,189,186,184,181,178,176
        db 173,170,167,164,161,158,155,152,149,146,143,140,137,134,131,128
        db 125,122,119,116,113,110,107,104,101, 98, 95, 92, 89, 86, 83, 80
        db  77, 75, 72, 69, 67, 64, 62, 59, 57, 55, 52, 50, 48, 46, 44, 42
        db  40, 38, 37, 35, 33, 32, 31, 29, 28, 27, 26, 25, 24, 23, 22, 22
        db  21, 21, 20, 20, 19, 19, 19, 19, 19, 19, 19, 19, 20, 20, 21, 21
        db  22, 22, 23, 24, 25, 26, 27, 28, 29, 31, 32, 33, 35, 37, 38, 40
        db  42, 44, 46, 48, 50, 52, 55, 57, 59, 62, 64, 67, 69, 72, 75, 77
        db  80, 83, 86, 89, 92, 95, 98,101,104,107,110,113,116,119,122,125

;=======================================================================
fb:    dd 0
tick:  dd 0
msg_novbe: db "gfxplasma: VBE not available", 0x0A, 0
