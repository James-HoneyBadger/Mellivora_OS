;
; syscalls.inc - Shared system call definitions for Mellivora OS programs
;
; Include this at the top of every program:
;   %include "syscalls.inc"
;

; Program header
[BITS 32]
[ORG 0x00200000]

; Jump past shared code to program entry point
jmp start

; System call numbers (INT 0x80)
SYS_EXIT        equ 0
SYS_PUTCHAR     equ 1
SYS_GETCHAR     equ 2
SYS_PRINT       equ 3
SYS_READ_KEY    equ 4
SYS_OPEN        equ 5
SYS_READ        equ 6
SYS_WRITE       equ 7
SYS_CLOSE       equ 8
SYS_DELETE      equ 9
SYS_SEEK        equ 10
SYS_STAT        equ 11
SYS_MKDIR       equ 12          ; Create directory: EBX=name -> EAX=0 success
SYS_READDIR     equ 13          ; Read directory entry: EBX=buf ECX=index -> EAX=type(-1=end) ECX=size
SYS_SETCURSOR   equ 14
SYS_GETTIME     equ 15
SYS_SLEEP       equ 16
SYS_CLEAR       equ 17
SYS_SETCOLOR    equ 18
SYS_MALLOC      equ 19
SYS_FREE        equ 20
SYS_EXEC        equ 21
SYS_DISK_READ   equ 22
SYS_DISK_WRITE  equ 23
SYS_BEEP        equ 24
SYS_DATE        equ 25
SYS_CHDIR       equ 26
SYS_GETCWD      equ 27
SYS_SERIAL      equ 28
SYS_GETENV      equ 29
SYS_FREAD       equ 30          ; Read entire file: EBX=name ECX=buf -> EAX=bytes
SYS_FWRITE      equ 31          ; Write file: EBX=name ECX=buf EDX=size ESI=type(0=text)
SYS_GETARGS     equ 32          ; Get command-line args: EBX=buf -> EAX=length
SYS_SERIAL_IN   equ 33          ; Read serial port: -> EAX=char or -1
SYS_STDIN_READ  equ 34          ; Read piped stdin: EBX=buf -> EAX=bytes (-1 if none)
SYS_YIELD       equ 35          ; Cooperative yield: switch to next ready task
SYS_MOUSE       equ 36          ; Read mouse: -> EAX=x, EBX=y, ECX=buttons
SYS_FRAMEBUF    equ 37          ; Framebuffer: EBX=sub (0=info,1=set,2=restore)
SYS_GUI         equ 38          ; Burrows GUI: EBX=sub-function

; SYS_GUI sub-functions (EBX)
GUI_CREATE_WINDOW  equ 0        ; ECX=x|y EDX=w|h ESI=title -> EAX=win_id
GUI_DESTROY_WINDOW equ 1        ; ECX=win_id
GUI_FILL_RECT      equ 2        ; ECX=win_id EDX=x|y ESI=w|h EDI=color
GUI_DRAW_TEXT      equ 3        ; ECX=win_id EDX=x|y ESI=text EDI=color
GUI_POLL_EVENT     equ 4        ; -> EAX=type EBX=param1 ECX=param2
GUI_GET_THEME      equ 5        ; ECX=dest_buf
GUI_SET_THEME      equ 6        ; ECX=src_buf
GUI_DRAW_PIXEL     equ 7        ; ECX=win_id EDX=x|y ESI=color
GUI_DRAW_LINE      equ 8        ; ECX=win_id EDX=x1|y1 ESI=x2|y2 EDI=color
GUI_COMPOSE        equ 9        ; Compose desktop to backbuffer
GUI_FLIP           equ 10       ; Flip backbuffer to screen
GUI_DESKTOP        equ 11       ; Reserved

; GUI event types
EVT_NONE        equ 0
EVT_MOUSE_CLICK equ 1
EVT_MOUSE_MOVE  equ 2
EVT_KEY_PRESS   equ 3
EVT_CLOSE       equ 4

; VGA constants
VGA_BASE        equ 0xB8000
VGA_WIDTH       equ 80
VGA_HEIGHT      equ 25

; Arrow key codes
KEY_UP          equ 0x80
KEY_DOWN        equ 0x81
KEY_LEFT        equ 0x82
KEY_RIGHT       equ 0x83

; Function key codes
KEY_F1          equ 0x84
KEY_F2          equ 0x85
KEY_F3          equ 0x86
KEY_F4          equ 0x87
KEY_F5          equ 0x88
KEY_F6          equ 0x89
KEY_F7          equ 0x8A
KEY_F8          equ 0x8B
KEY_F9          equ 0x8C
KEY_F10         equ 0x8D

; Default color
COLOR_DEFAULT   equ 0x07

; File types (for SYS_FWRITE and SYS_STAT)
FTYPE_TEXT      equ 1
FTYPE_DIR       equ 2
FTYPE_EXEC      equ 3
FTYPE_BATCH     equ 4

;---------------------------------------
; Shared print_dec: print EAX as decimal
; Clobbers no registers (uses pushad/popad)
;---------------------------------------
print_dec:
        pushad
        xor ecx, ecx
        mov ebx, 10
        test eax, eax
        jnz .pd_nonzero
        mov eax, SYS_PUTCHAR
        mov ebx, '0'
        int 0x80
        jmp .pd_done
.pd_nonzero:
.pd_push:
        xor edx, edx
        div ebx
        push edx
        inc ecx
        test eax, eax
        jnz .pd_push
.pd_pop:
        pop ebx
        add ebx, '0'
        mov eax, SYS_PUTCHAR
        int 0x80
        dec ecx
        jnz .pd_pop
.pd_done:
        popad
        ret
