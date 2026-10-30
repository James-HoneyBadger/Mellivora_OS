; pe_payload.inc - the body every PE32 loader fixture carries.
;
; The same reasoning as payload_ok.inc: a fixture that is expected to be refused
; never runs, so the payload only has to be a program. The positive fixtures use
; it to prove that a load which should succeed did, and the message names the
; format so a mixed-up fixture cannot pass for the wrong one.
start:
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A                   ; light green
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_pe_ok
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, 0x07
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

msg_pe_ok: db "PE_OK", 0x0A, 0
