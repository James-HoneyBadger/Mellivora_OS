; stale_probe.asm - look for what the previous program left in the window
;
; Run right after stale_write.asm, which fills the window (image tail, heap and
; the whole user stack) with MARKER. Neither of those addresses belongs to this
; program, so a match means the launcher handed a task a copy of memory another
; program had used.
;
; Run as:  stale_probe
; Output:  STALE_CLEAN           (nothing found)
;          STALE_LEAK <addr>    (the first marker found)

%include "syscalls.inc"

MARKER   equ 0x4D415453         ; 'STAM'
FILL_BASE equ 0x540000          ; where stale_write.asm started filling

start:
        cmp dword [FILL_BASE], MARKER
        je .leak_fixed

        ; The mapped stack window is the 16 pages ending at the launch top, and
        ; ESP is inside the last of them, so 15 pages below ESP are all mapped
        ; and all fair game for a scan.
        mov esi, esp
        sub esi, 0xF000
        mov ecx, 0xF000 / 4
.scan:
        cmp dword [esi], MARKER
        je .leak

        add esi, 4
        dec ecx
        jnz .scan

        mov eax, SYS_PRINT
        mov ebx, msg_clean
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

.leak_fixed:
        mov esi, FILL_BASE
.leak:
        mov eax, esi
        call print_hex

        mov eax, SYS_PRINT
        mov ebx, msg_leak
        int 0x80
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

;---------------------------------------
; print_hex - print EAX as 8 lowercase hex digits
;---------------------------------------
print_hex:
        pushad
        mov edi, hexbuf
        mov ecx, 8
.digit:
        rol eax, 4
        push eax
        and al, 0x0F
        cmp al, 10
        jb .decimal
        add al, 'a' - 10
        jmp .store
.decimal:
        add al, '0'
.store:
        mov [edi], al
        inc edi
        pop eax
        dec ecx
        jnz .digit
        mov byte [edi], 0

        mov eax, SYS_PRINT
        mov ebx, hexbuf
        int 0x80
        popad
        ret

msg_clean: db 'STALE_CLEAN', 13, 10, 0
msg_leak:  db 'STALE_LEAK', 13, 10, 0
hexbuf:    times 9 db 0
