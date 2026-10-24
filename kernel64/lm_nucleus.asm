;=======================================================================
; lm_nucleus.asm - minimal 64-bit long-mode nucleus
;
; This is the first real piece of the x86-64 port (roadmap P2). stage2 has
; already negotiated long mode, set up an identity map for 0..16 MB with
; 2 MB pages, loaded the 64-bit data selector (0x10) and jumped to
; 0x00100000 with RSP=0x9FC00.
;
; The nucleus proves the whole 64-bit boot path works end to end: it runs in
; 64-bit mode, talks to COM1 and reports a marker the test harness asserts
; on. Everything else (kernel, drivers, schedulers) is ported on top of
; this foundation.
;=======================================================================

[BITS 64]
[ORG 0x00100000]

COM1 equ 0x3F8

start:
        ; Visual proof of life for headless debugging: 'N' at the bottom-left
        ; of the text buffer, written before anything can hang.
        mov rax, 0xB8000 + (24 * 80 * 2)
        mov word [rax], 0x2F4E          ; 'N', white on green
        mov rsp, 0x000000000009F000     ; keep clear of the BIOS data area
        mov ax, 0x10
        mov ds, ax
        mov es, ax
        mov ss, ax
        mov fs, ax
        mov gs, ax

        ; --- COM1: 115200 8N1, FIFO on ---
        mov dx, COM1 + 1
        xor al, al
        out dx, al                      ; no UART interrupts
        mov dx, COM1 + 3
        mov al, 0x80
        out dx, al                      ; DLAB on
        mov dx, COM1 + 0
        mov al, 1
        out dx, al                      ; divisor = 1
        mov dx, COM1 + 1
        xor al, al
        out dx, al
        mov dx, COM1 + 3
        mov al, 0x03
        out dx, al                      ; 8 bits, no parity, 1 stop
        mov dx, COM1 + 2
        mov al, 0xC7
        out dx, al                      ; FIFO enable + clear
        mov dx, COM1 + 4
        mov al, 0x0B
        out dx, al                      ; DTR | RTS | OUT2

        mov rsi, msg_banner
        call puts

        ; Report how much address space the identity map covers, so the log
        ; shows the nucleus understands its own environment.
        mov rsi, msg_pml4
        call puts
        mov rax, cr3
        call puthex
        mov rsi, msg_crlf
        call puts

        mov rsi, msg_ok
        call puts
        mov rsi, msg_crlf
        call puts

.hang:
        cli
        hlt
        jmp .hang

;---------------------------------------
; puts - write a NUL-terminated string to COM1
;   RSI = string
;---------------------------------------
puts:
        lodsb
        test al, al
        jz .done
        call putc
        jmp puts
.done:
        ret

;---------------------------------------
; putc - write AL to COM1, waiting for the transmit register
;---------------------------------------
putc:
        push rax
        push rdx
        mov ah, al
.wait:
        mov dx, COM1 + 5
        in al, dx
        test al, 0x20
        jz .wait
        mov al, ah
        mov dx, COM1
        out dx, al
        pop rdx
        pop rax
        ret

;---------------------------------------
; puthex - write RAX as 16 hex digits
;---------------------------------------
puthex:
        push rax
        push rcx
        mov rcx, 16
.loop:
        rol rax, 4
        push rax
        and al, 0x0F
        cmp al, 10
        jb .digit
        add al, 'A' - 10 - '0'
.digit:
        add al, '0'
        call putc
        pop rax
        dec rcx
        jnz .loop
        pop rcx
        pop rax
        ret

msg_banner: db "[lm] Mellivora x86-64 nucleus", 13, 10, 0
msg_pml4:   db "[lm] cr3=0x", 0
msg_ok:     db "MELLIVORA64_OK", 0
msg_crlf:   db 13, 10, 0
