; hivepoke.asm - put four bytes over the front of a hive image on purpose.
;
; CONFIG.md section 10 item 5 asks that a hive with a corrupted bin is refused
; by name and offset, and that the last-known-good generation is used when it
; is there. Nothing in the shell writes raw bytes to a file, so the test needs a
; program whose whole purpose is to damage one: it opens hive.hiv, seeks to
; zero and writes "XXXX" over the magic. The hive loader then has something
; corrupt to refuse.
;
; The strings come before the code so that no instruction names a label the
; assembler has not placed yet; the jump over them is the entry point.
%include "syscalls.inc"

O_WRONLY    equ 1

        jmp start

vfd:        dd 0
hive_file:  db "hive.hiv", 0
junk:       db "XXXX"
msg_ok:     db "HIVEPOKE_OK", 0x0A, 0
msg_fail:   db "HIVEPOKE_FAIL", 0x0A, 0

start:
        mov eax, SYS_OPEN
        mov ebx, hive_file
        mov ecx, O_WRONLY
        int 0x80
        inc eax                         ; -1 becomes zero
        jz fail
        dec eax
        mov [vfd], eax

        mov eax, SYS_SEEK
        mov ebx, [vfd]
        xor ecx, ecx                    ; offset zero
        xor edx, edx                    ; from the start
        int 0x80

        mov eax, SYS_WRITE
        mov ebx, [vfd]
        mov ecx, junk
        mov edx, 4
        int 0x80
        sub eax, 4
        jnz fail

        mov eax, SYS_CLOSE
        mov ebx, [vfd]
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80
fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80
