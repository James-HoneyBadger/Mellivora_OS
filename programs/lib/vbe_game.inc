;=======================================================================
; vbe_game.inc - Mellivora VBE Game-Loop Helper (user-space)
;
; High-level macros that automate the boilerplate needed for a VBE
; 32-bpp pixel-graphics game:
;   VBE_GAME_INIT      Enter 640x480x32 mode and populate fb_* vars.
;   VBE_GAME_PRESENT   Blit shadow buffer to the LFB.
;   VBE_GAME_POLL_KEY  Non-blocking key read (result in EAX; -1=none).
;
; Standard scancode constants for the most common game controls:
;   KEY_LEFT  KEY_RIGHT  KEY_UP  KEY_DOWN
;   KEY_ESC   KEY_SPACE  KEY_ENTER
;   KEY_W  KEY_A  KEY_S  KEY_D
;   KEY_P  KEY_R  KEY_Q
;
; This file auto-includes lib/vbe.inc (which declares fb_addr, fb_pitch,
; fb_width, fb_height and all drawing routines).
;
; Typical usage:
;
;   %include "lib/vbe_game.inc"   ; also pulls in lib/vbe.inc
;
;   _start:
;       VBE_GAME_INIT             ; enter 640x480x32, fill fb_* vars
;
;   .game_loop:
;       VBE_GAME_POLL_KEY         ; EAX = scancode or -1
;       cmp eax, KEY_ESC
;       je  .quit
;       ; ... update state, draw frame ...
;       VBE_GAME_PRESENT          ; flip to screen
;       jmp .game_loop
;
;   .quit:
;       mov eax, 2                ; SYS_FRAMEBUF restore text mode
;       mov ebx, 2
;       int 0x80
;       xor eax, eax
;       int 0x80                  ; SYS_EXIT
;
; Requires: syscalls.inc (or that EAX/EBX/ECX/EDX/ESI conventions hold).
;=======================================================================

%ifndef VBE_GAME_INC
%define VBE_GAME_INC

%include "lib/vbe.inc"          ; drawing primitives + fb_* BSS vars

; -----------------------------------------------------------------------
; Standard game-key scancodes (raw scancodes from SYS_READ_KEY / SYS_GETCHAR).
; -----------------------------------------------------------------------
KEY_ESC   equ 0x01
KEY_ENTER equ 0x1C
KEY_SPACE equ 0x39

KEY_UP    equ 0x48              ; arrow keys (extended, high byte 0xE0)
KEY_DOWN  equ 0x50
KEY_LEFT  equ 0x4B
KEY_RIGHT equ 0x4D

KEY_W     equ 0x11
KEY_A     equ 0x1E
KEY_S     equ 0x1F
KEY_D     equ 0x20

KEY_P     equ 0x19              ; pause
KEY_R     equ 0x13              ; restart
KEY_Q     equ 0x10              ; quit

; -----------------------------------------------------------------------
; VBE_GAME_INIT - Enter 640x480x32 VBE mode and fill fb_* variables.
;
; Clobbers: EAX, EBX, ECX, EDX, ESI.
; After the macro returns, [fb_addr], [fb_width], [fb_height], [fb_pitch]
; are all populated and the shadow buffer is ready for drawing.
; -----------------------------------------------------------------------
%macro VBE_GAME_INIT 0
        ; Enter 640 x 480 x 32-bpp mode
        mov eax, 37             ; SYS_FRAMEBUF
        mov ebx, 1              ; sub-function 1: set mode
        mov ecx, 640            ; width
        mov edx, 480            ; height
        mov esi, 32             ; bpp
        int 0x80

        ; Query framebuffer info
        ; Returns: EAX=shadow_buf, EBX=width, ECX=height, EDX=bpp
        mov eax, 37
        mov ebx, 0
        int 0x80

        mov [fb_addr],   eax    ; shadow buffer address
        mov [fb_width],  ebx    ; width in pixels
        mov [fb_height], ecx    ; height in pixels
        ; pitch = width * 4  (32-bpp, no padding)
        shl ebx, 2
        mov [fb_pitch],  ebx
%endmacro

; -----------------------------------------------------------------------
; VBE_GAME_PRESENT - Blit shadow buffer to the LFB (double-buffer flip).
; Clobbers: EAX, EBX.
; -----------------------------------------------------------------------
%macro VBE_GAME_PRESENT 0
        mov eax, 37
        mov ebx, 4              ; sub-function 4: present
        int 0x80
%endmacro

; -----------------------------------------------------------------------
; VBE_GAME_POLL_KEY - Non-blocking keyboard check.
; Result: EAX = scancode (positive), or -1 if no key is pending.
; Clobbers: EAX only.
; -----------------------------------------------------------------------
%macro VBE_GAME_POLL_KEY 0
        mov eax, 4              ; SYS_READ_KEY  (non-blocking)
        int 0x80
        ; EAX = scancode or -1 (0xFFFFFFFF) if no key
%endmacro

%endif ; VBE_GAME_INC
