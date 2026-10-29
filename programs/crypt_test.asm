; crypt_test.asm - an encrypted volume round-trips a file
;
; Runs on whatever volume is mounted. The first run creates cryptfile.bin with
; a 3-block pattern and reads it back; a later run finds the file and requires
; the same bytes, which is the round trip across a reboot, and therefore across
; the key being dropped from the store and put back.
;
; The first four bytes of the pattern are ASCII, so the offline half of the
; gate can search the raw image for them: finding them means the blocks were
; written in the clear.
;
; Prints CRYPT_CREATED_OK, CRYPT_PERSIST_OK, or CRYPT_FAIL-<stage>.
%include "syscalls.inc"

O_RDONLY equ 0
LEN      equ 3 * 4096
PAT_SEED equ 0x5AA5C33C

start:
        mov eax, SYS_OPEN
        mov ebx, path
        mov ecx, O_RDONLY
        int 0x80
        cmp eax, -1
        je do_create

        ; The file is there: read it and require the pattern back.
        mov [fd], eax
        mov ebx, [fd]
        mov edi, buf
        mov edx, LEN
        call read_all
        jc fail_read_
        mov eax, SYS_CLOSE
        mov ebx, [fd]
        int 0x80

        mov edi, want
        call fill
        mov esi, buf
        mov edi, want
        mov ecx, LEN
        call memcmp
        jc fail_compare_
        mov esi, msg_persist
        call print
        jmp finish_ok

do_create:
        mov edi, buf
        call fill

        mov eax, SYS_FWRITE
        mov ebx, path
        mov ecx, buf
        mov edx, LEN
        xor esi, esi                 ; text file
        int 0x80
        test eax, eax
        jnz fail_write_

        mov eax, SYS_OPEN
        mov ebx, path
        mov ecx, O_RDONLY
        int 0x80
        cmp eax, -1
        je fail_open_
        mov [fd], eax
        mov ebx, [fd]
        mov edi, got
        mov edx, LEN
        call read_all
        jc fail_read_
        mov eax, SYS_CLOSE
        mov ebx, [fd]
        int 0x80

        mov edi, want
        call fill
        mov esi, got
        mov edi, want
        mov ecx, LEN
        call memcmp
        jc fail_compare_
        mov esi, msg_created
        call print
        jmp finish_ok

; read_all: EBX = fd, EDI = buffer, EDX = bytes wanted
;   CF set when the file ended early or the read failed.
; A single SYS_READ returns at most one 4 KiB block, so a file larger than a
; block is read one block at a time and the short read is the normal case.
read_all:
        pushad
        mov [.ra_fd], ebx
        mov [.ra_dst], edi
        mov [.ra_left], edx
.ra_loop:
        cmp dword [.ra_left], 0
        je .ra_ok
        mov eax, SYS_READ
        mov ebx, [.ra_fd]
        mov ecx, [.ra_dst]
        mov edx, [.ra_left]
        int 0x80
        cmp eax, 0
        jle .ra_short
        add [.ra_dst], eax
        sub [.ra_left], eax
        jmp .ra_loop
.ra_ok:
        popad
        clc
        ret
.ra_short:
        popad
        stc
        ret
.ra_fd:   dd 0
.ra_dst:  dd 0
.ra_left: dd 0

; fill: EDI = buffer, filled with the pattern
fill:
        pushad
        mov [.f_dst], edi
        mov edi, [.f_dst]
        mov dword [edi], 'CRYP'
        mov ecx, 4
        mov eax, PAT_SEED
.f_loop:
        imul eax, eax, 1103515245
        add eax, 12345
        mov [edi + ecx], al
        inc ecx
        cmp ecx, LEN
        jb .f_loop
        popad
        ret
.f_dst: dd 0

; memcmp: CF clear when the two buffers are equal
memcmp:
        cld
.mc_loop:
        test ecx, ecx
        jz .mc_equal
        mov al, [esi]
        cmp al, [edi]
        jne .mc_diff
        inc esi
        inc edi
        dec ecx
        jmp .mc_loop
.mc_equal:
        clc
        ret
.mc_diff:
        stc
        ret

print:
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        ret

finish_ok:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

fail_open_:
        mov esi, msg_fail_open
        jmp fail_print_
fail_read_:
        mov esi, msg_fail_read
        jmp fail_print_
fail_write_:
        mov esi, msg_fail_write
        jmp fail_print_
fail_compare_:
        mov esi, msg_fail_compare
fail_print_:
        call print
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

path:        db "cryptfile.bin", 0
msg_created: db "CRYPT_CREATED_OK", 10, 0
msg_persist: db "CRYPT_PERSIST_OK", 10, 0
msg_fail_open:    db "CRYPT_FAIL-open", 10, 0
msg_fail_read:    db "CRYPT_FAIL-read", 10, 0
msg_fail_write:   db "CRYPT_FAIL-write", 10, 0
msg_fail_compare: db "CRYPT_FAIL-compare", 10, 0

fd:   dd 0
buf:  times LEN db 0
got:  times LEN db 0
want: times LEN db 0
