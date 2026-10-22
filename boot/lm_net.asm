;=======================================================================
; lm_net.asm - /net, the network demo
;
; Asks the network whether anything is out there. The kernel resolves the
; gateway with ARP at boot, so the first call only has to send an echo request
; and wait for the echo reply: three round trips, each timed with the 100 Hz
; tick, printed one line at a time.
;
; Exit code is the number of replies that came back, which is the kind of thing
; a script would check.
;=======================================================================

[BITS 64]
[ORG 0x40000000]

SYS_EXIT    equ 3
SYS_WRITE   equ 6
SYS_NETPING equ 41

GATEWAY equ 0x0A000202          ; 10.0.2.2, the user-mode network's gateway
TIMEOUT equ 500                 ; milliseconds

LINE_BUF equ 0x40001200
LINE_LIMIT equ 0x40001500
DEC_BUF  equ 0x40001600

start:
        cld
        xor r15, r15                    ; replies so far

        call begin_line
        lea rsi, [rel s_head]
        call add_str
        call end_line

        mov r14, 3
.each:
        mov rdi, GATEWAY
        mov esi, TIMEOUT
        mov eax, SYS_NETPING
        syscall

        mov r13, rax                    ; add_str walks over EAX, so keep it

        call begin_line
        lea rsi, [rel s_ping]
        call add_str
        cmp r13, -1
        je .lost
        inc r15
        mov eax, r13d                   ; the round trip, in milliseconds
        call add_dec
        lea rsi, [rel s_ms]
        call add_str
        jmp .line
.lost:
        lea rsi, [rel s_lost]
        call add_str
.line:
        call end_line

        dec r14
        jnz .each

        call begin_line
        lea rsi, [rel s_summary]
        call add_str
        mov rax, r15
        call add_dec
        lea rsi, [rel s_of3]
        call add_str
        call end_line

        mov rdi, r15
        mov eax, SYS_EXIT
        syscall
.hang:
        jmp .hang

;-----------------------------------------------------------------------
; Line assembly. RBP is the write cursor.
;-----------------------------------------------------------------------
begin_line:
        lea rbp, [LINE_BUF]
        ret

add_str:
        cld
        lodsb
        test al, al
        jz .done
        cmp rbp, LINE_LIMIT
        jae .done
        mov [rbp], al
        inc rbp
        jmp add_str
.done:
        ret

; add_dec: rax = value, in decimal. syscall clobbers rcx and r11, and the
; divide needs ecx, so it is reloaded every digit.
add_dec:
        lea rsi, [DEC_BUF + 31]
        mov byte [rsi], 0
.digits:
        mov ecx, 10
        xor edx, edx
        div ecx
        add dl, '0'
        dec rsi
        mov [rsi], dl
        test rax, rax
        jnz .digits
        call add_str
        ret

end_line:
        mov byte [rbp], 10
        inc rbp
        lea rsi, [LINE_BUF]
        mov rdx, rbp
        sub rdx, rsi
        mov edi, 1
        mov eax, SYS_WRITE
        syscall
        ret

s_head:    db "net: ping 10.0.2.2", 0
s_ping:    db "net: reply rtt=", 0
s_ms:      db "ms", 0
s_lost:    db "net: no reply", 0
s_summary: db "net: ", 0
s_of3:     db " of 3 replied", 0
