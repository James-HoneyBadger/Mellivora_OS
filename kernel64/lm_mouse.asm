;=======================================================================
; lm_mouse.asm - /mouse, the pointer demo
;
; Proves the PS/2 driver end to end: it draws a block cursor on the text
; screen wherever the pointer is, reports every change of position or button,
; and gives up after five seconds so that `run /mouse` from the shell comes
; back. Moving the pointer while it runs is the whole test - nothing here
; talks to the hardware, it only reads what the timer tick collected.
;
; Lines are assembled in a buffer and written with one syscall, because the
; shell and the other tasks print at the same time.
;=======================================================================

[BITS 64]
[ORG 0x40000000]

SYS_EXIT      equ 3
SYS_TICKS     equ 4
SYS_WRITE     equ 6
SYS_YIELD     equ 7
SYS_CLEAR     equ 23
SYS_PUTCHAR   equ 21
SYS_SETCURSOR equ 36
SYS_MOUSE     equ 40

LINE_BUF   equ 0x40001200       ; the line being assembled
LINE_LIMIT equ 0x40001500       ; where the buffers below start
DEC_BUF    equ 0x40001600

DEADLINE_TICKS equ 500          ; five seconds at 100 Hz

start:
        cld                             ; string instructions run forwards

        mov eax, SYS_CLEAR
        syscall

        call begin_line
        lea rsi, [rel s_head]
        call add_str
        call end_line

        mov eax, SYS_TICKS
        syscall
        add rax, DEADLINE_TICKS
        mov r15, rax                    ; when to stop

        mov r12, -1                     ; nothing drawn yet
        mov r13, -1
        mov r14, -1                     ; and no buttons seen

.loop:
        mov eax, SYS_MOUSE
        syscall                         ; rax=x, rdx=y, rcx=buttons|presses<<8
        mov r8, rax
        mov r9, rdx
        mov r10, rcx

        cmp r8, r12
        jne .moved
        cmp r9, r13
        jne .moved
        cmp r10, r14
        jne .moved
        jmp .next

.moved:
        cmp r12, -1                     ; rub out the old block, if any
        je .draw
        mov rdi, r12
        mov rsi, r13
        mov eax, SYS_SETCURSOR
        syscall
        mov edi, ' '
        mov eax, SYS_PUTCHAR
        syscall

.draw:
        mov rdi, r8
        mov rsi, r9
        mov eax, SYS_SETCURSOR
        syscall
        mov edi, 0xDB                   ; the full block
        mov eax, SYS_PUTCHAR
        syscall

        mov r12, r8
        mov r13, r9
        mov r14, r10

        call begin_line
        lea rsi, [rel s_pos]
        call add_str
        mov rax, r8
        call add_dec
        lea rsi, [rel s_y]
        call add_str
        mov rax, r9
        call add_dec
        lea rsi, [rel s_b]
        call add_str
        mov rax, r10
        and eax, 7
        call add_dec
        lea rsi, [rel s_press]
        call add_str
        mov rax, r10
        shr eax, 8
        and eax, 7
        call add_dec
        call end_line

.next:
        mov eax, SYS_TICKS
        syscall
        cmp rax, r15
        jae .done
        mov eax, SYS_YIELD
        syscall
        jmp .loop

.done:
        cmp r12, -1
        je .exit
        mov rdi, r12                    ; leave the screen as it was found
        mov rsi, r13
        mov eax, SYS_SETCURSOR
        syscall
        mov edi, ' '
        mov eax, SYS_PUTCHAR
        syscall

.exit:
        xor edi, edi
        mov eax, SYS_EXIT
        syscall
.hang:
        jmp .hang

;-----------------------------------------------------------------------
; Line assembly. RBP is the write cursor; nothing here uses it as a frame.
;-----------------------------------------------------------------------
begin_line:
        lea rbp, [LINE_BUF]
        ret

; add_str: rsi = NUL-terminated string.
add_str:
        cld
        lodsb
        test al, al
        jz .done
        cmp rbp, LINE_LIMIT             ; never run into the buffers below
        jae .done
        mov [rbp], al
        inc rbp
        jmp add_str
.done:
        ret

; add_dec: eax = value, in decimal. syscall clobbers rcx and r11, and the
; divide needs ecx, so it is reloaded every digit. The saved state is kept in
; r12-r15 for that reason: r11 does not survive the instruction.
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
        test eax, eax
        jnz .digits
        call add_str
        ret

; end_line: newline, then one write of the whole line.
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

s_head:  db "mouse: move the pointer", 0
s_pos:   db "mouse x=", 0
s_y:     db " y=", 0
s_b:     db " b=", 0
s_press: db " press=", 0
