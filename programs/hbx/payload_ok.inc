; payload_ok.inc - the body every HBX loader fixture carries.
;
; A fixture that is expected to be refused never runs, so the payload only has
; to be a program: it says what it is and exits cleanly. The positive fixtures
; use it to prove that a load which should succeed did.

start:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A                   ; light green
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

msg_ok: db "HBX_OK", 0x0A, 0
