; fragmake.asm - force the kernel to create an extent file.
;
; The host fills a small v4 volume with one-block files, so no run of two
; blocks is free. This deletes every other filler, leaving isolated one-block
; holes, then writes a two-block file: the contiguous allocation must fail and
; the v4 allocator has to store it as two runs. Reading it back proves the
; mapping; the host checker proves the extent block and bitmap are right.
;
; Prints FRAG_OK or FRAG_FAIL-<stage>.
%include "syscalls.inc"

BUF_SIZE equ 8192

start:
        mov dword [idx], 0
.del_loop:
        cmp dword [idx], 400
        jae .make
        mov eax, [idx]
        test eax, 1
        jnz .next
        call make_name
        mov eax, SYS_DELETE
        mov ebx, name_buf
        int 0x80
.next:
        inc dword [idx]
        jmp .del_loop

.make:
        mov eax, SYS_FWRITE
        mov ebx, target
        mov ecx, payload
        mov edx, BUF_SIZE
        mov esi, 0
        int 0x80
        cmp eax, 0
        jne .fail_write

        mov eax, SYS_FREAD
        mov ebx, target
        mov ecx, rbuf
        int 0x80
        cmp eax, BUF_SIZE
        jne .fail_read

        mov esi, payload
        mov edi, rbuf
        mov ecx, BUF_SIZE
.compare:
        mov al, [esi]
        cmp al, [edi]
        jne .fail_read
        inc esi
        inc edi
        dec ecx
        jnz .compare

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor eax, eax
        int 0x80

.fail_write:
        mov ebx, msg_write
        jmp .fail
.fail_read:
        mov ebx, msg_read
.fail:
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

; make_name - "h" + three decimal digits from [idx]
make_name:
        mov eax, [idx]
        mov byte [name_buf], 'h'
        xor edx, edx
        mov ecx, 100
        div ecx
        add al, '0'
        mov [name_buf + 1], al
        mov eax, edx
        xor edx, edx
        mov ecx, 10
        div ecx
        add al, '0'
        mov [name_buf + 2], al
        add dl, '0'
        mov [name_buf + 3], dl
        mov byte [name_buf + 4], 0
        ret

idx:       dd 0
name_buf:  times 8 db 0
target:    db "two.bin", 0
payload:   times BUF_SIZE db 0x41
rbuf:      times BUF_SIZE db 0
msg_ok:    db 10, "FRAG_OK", 10, 0
msg_write: db 10, "FRAG_FAIL-WRITE", 10, 0
msg_read:  db 10, "FRAG_FAIL-READ", 10, 0
