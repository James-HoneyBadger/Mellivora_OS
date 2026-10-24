;=======================================================================
; lm_beep.asm - /beep, three notes on the PC speaker
;
; The shortest program that proves the sound path from ring 3: ask for a tone,
; get it, ask for silence. What it cannot prove is that anybody heard it, so it
; prints what it did and leaves the listening to the person in front of the
; machine.
;=======================================================================

[BITS 64]
[ORG 0x40000000]

SYS_EXIT  equ 3
SYS_WRITE equ 6
SYS_BEEP  equ 42

LINE_BUF   equ 0x40001200
LINE_LIMIT equ 0x40001500
DEC_BUF    equ 0x40001600

NOTE_MS equ 120

start:
        cld

        call begin_line
        lea rsi, [rel s_head]
        call add_str
        call end_line

        ; A, C#, E: an arpeggio short enough to be pleasant once.
        lea rbx, [rel notes]
        mov r14, 3
.note:
        mov edi, [rbx]                  ; the frequency
        mov esi, NOTE_MS
        mov eax, SYS_BEEP
        syscall

        call begin_line
        lea rsi, [rel s_note]
        call add_str
        mov eax, [rbx]
        call add_dec
        lea rsi, [rel s_hz]
        call add_str
        call end_line

        add rbx, 4
        dec r14
        jnz .note

        ; Silence at the end, in case the last tone was cut short.
        xor edi, edi
        xor esi, esi
        mov eax, SYS_BEEP
        syscall

        call begin_line
        lea rsi, [rel s_done]
        call add_str
        call end_line

        xor edi, edi
        mov eax, SYS_EXIT
        syscall
.hang:
        jmp .hang

;-----------------------------------------------------------------------
; Line assembly. RBP is the write cursor; R14 and R15 belong to the caller.
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

; add_dec: eax = value, in decimal. syscall clobbers rcx and r11, and the
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
        test eax, eax
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

notes:     dd 440, 554, 659          ; A4, C#5, E5
s_head:    db "beep: three notes on the pc speaker", 0
s_note:    db "beep: tone ", 0
s_hz:      db " Hz", 0
s_done:    db "beep: silent", 0
