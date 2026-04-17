; doomfire.asm - Doom fire effect demo
; Usage: doomfire
; Press any key to exit.

%include "syscalls.inc"

FIRE_W          equ 80
FIRE_H          equ 25
FIRE_SIZE       equ FIRE_W * FIRE_H
TICK_DELAY      equ 2           ; ~50ms per frame

; Fire intensity palette: character + color attribute
; Intensity 0-7: black to white-hot
NUM_SHADES      equ 8

start:
        ; Clear screen
        mov eax, SYS_CLEAR
        int 0x80

        ; Seed random state from clock ticks
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_state], eax

        ; Initialize fire buffer to zero (cold)
        mov edi, fire_buf
        mov ecx, FIRE_SIZE
        xor eax, eax
        rep stosb

        ; Set bottom row to max intensity
        mov edi, fire_buf + (FIRE_H - 1) * FIRE_W
        mov ecx, FIRE_W
        mov al, NUM_SHADES - 1
        rep stosb

.main_loop:
        ; Check for keypress — exit on any key
        mov eax, SYS_READ_KEY
        int 0x80
        test eax, eax
        jnz .exit

        ; Propagate fire upward
        call fire_spread

        ; Render fire to screen
        call fire_render

        ; Delay
        mov eax, SYS_SLEEP
        mov ebx, TICK_DELAY
        int 0x80

        jmp .main_loop

.exit:
        ; Reset color and clear
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_CLEAR
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; fire_spread - Propagate fire from bottom up
; Each pixel samples the pixel below (with random decay)
;---------------------------------------
fire_spread:
        PUSHALL
        ; Process rows 0..(FIRE_H-2), each pixel looks at row below
        mov edx, 0              ; Y = 0
.fs_row:
        cmp edx, FIRE_H - 1
        jge .fs_done
        xor ecx, ecx           ; X = 0
.fs_col:
        cmp ecx, FIRE_W
        jge .fs_next_row

        ; Source = fire_buf[(Y+1)*FIRE_W + X]
        mov eax, edx
        inc eax
        imul eax, FIRE_W
        add eax, ecx
        movzx ebx, byte [fire_buf + eax]  ; source intensity

        ; Random decay (0-1)
        call rand_small
        sub ebx, eax
        jns .fs_clamp_ok
        xor ebx, ebx
.fs_clamp_ok:

        ; Random horizontal wind (-1, 0, or +1)
        call rand_small
        mov esi, ecx
        sub esi, eax
        ; Clamp X to [0, FIRE_W-1]
        jns .fs_xok
        xor esi, esi
.fs_xok:
        cmp esi, FIRE_W
        jb .fs_xok2
        mov esi, FIRE_W - 1
.fs_xok2:

        ; Destination = fire_buf[Y*FIRE_W + adjusted_X]
        mov eax, edx
        imul eax, FIRE_W
        add eax, esi
        mov byte [fire_buf + eax], bl

        inc ecx
        jmp .fs_col
.fs_next_row:
        inc edx
        jmp .fs_row
.fs_done:
        POPALL
        ret

;---------------------------------------
; fire_render - Draw fire buffer to screen
;---------------------------------------
fire_render:
        PUSHALL
        xor edx, edx           ; Y = 0
.fr_row:
        cmp edx, FIRE_H
        jge .fr_done
        ; Set cursor to start of row
        mov eax, SYS_SETCURSOR
        xor ebx, ebx
        mov ecx, edx
        int 0x80

        xor ecx, ecx           ; X = 0
.fr_col:
        cmp ecx, FIRE_W
        jge .fr_next_row

        ; Get intensity
        mov eax, edx
        imul eax, FIRE_W
        add eax, ecx
        movzx eax, byte [fire_buf + eax]

        ; Look up color and character
        push rcx
        push rdx
        movzx ebx, byte [palette_color + eax]
        mov eax, SYS_SETCOLOR
        int 0x80
        pop rdx
        pop rcx

        push rcx
        push rdx
        mov eax, edx
        imul eax, FIRE_W
        add eax, ecx
        movzx eax, byte [fire_buf + eax]
        movzx ebx, byte [palette_char + eax]
        mov eax, SYS_PUTCHAR
        int 0x80
        pop rdx
        pop rcx

        inc ecx
        jmp .fr_col
.fr_next_row:
        inc edx
        jmp .fr_row
.fr_done:
        POPALL
        ret

;---------------------------------------
; rand_small - Return 0 or 1 in EAX
; Simple LCG: state = state * 1103515245 + 12345
;---------------------------------------
rand_small:
        push rbx
        mov eax, [rand_state]
        imul eax, 1103515245
        add eax, 12345
        mov [rand_state], eax
        shr eax, 16
        and eax, 1
        pop rbx
        ret

; Fire intensity palette
; Shade 0=cold (black), 7=hottest (white)
palette_color:
        db 0x00         ; 0: black on black
        db 0x04         ; 1: dark red on black
        db 0x0C         ; 2: bright red on black
        db 0x06         ; 3: brown/orange on black
        db 0x0E         ; 4: yellow on black
        db 0x0E         ; 5: yellow on black
        db 0x0F         ; 6: white on black
        db 0x0F         ; 7: bright white on black

palette_char:
        db ' '          ; 0: empty
        db 0xB0         ; 1: light shade
        db 0xB1         ; 2: medium shade
        db 0xB1         ; 3: medium shade
        db 0xB2         ; 4: dark shade
        db 0xDB         ; 5: full block
        db 0xDB         ; 6: full block
        db 0xDB         ; 7: full block

; Data
rand_state:     dd 0
fire_buf:       times FIRE_SIZE db 0
