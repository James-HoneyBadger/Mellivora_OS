; fs_meta_test.asm — HBFS metadata-write regression test
;
; Two things are checked that used to be broken:
;
;   1. Changing metadata (chmod) of a file inside a *subdirectory* must not
;      touch the root directory. hbfs_flush_dir_entry added HBFS_ROOT_DIR_START
;      unconditionally, so the modified entry landed in the root directory at
;      the same offset — eating whichever root entry lived there.
;   2. Create / delete / chmod must keep the directory and the block bitmap
;      consistent (all metadata writes are journaled now), so a file created
;      earlier is still readable after a metadata change.
;
; Prints FS_META_OK on success, FS_META_FAIL stage <n> on failure.

%include "syscalls.inc"

start:
        ; --- start from a clean slate ---
        mov al, '1'
        call set_stage
        mov eax, SYS_CHDIR
        mov ebx, path_root
        int 0x80
        cmp eax, 0
        jne fail
        mov eax, SYS_DELETE
        mov ebx, name_inner
        int 0x80                        ; may not exist

        ; --- a root file that must survive ---
        mov al, '2'
        call set_stage
        mov eax, SYS_FWRITE
        mov ebx, name_keep
        mov ecx, payload_keep
        mov edx, payload_keep_len
        xor esi, esi
        int 0x80
        cmp eax, 0
        jne fail

        ; --- a subdirectory with a file in it ---
        mov al, '3'
        call set_stage
        mov eax, SYS_MKDIR
        mov ebx, name_dir
        int 0x80
        ; mkdir may report "exists" — that is fine, we only need the directory
        mov al, '4'
        call set_stage
        mov eax, SYS_CHDIR
        mov ebx, name_dir
        int 0x80
        cmp eax, 0
        jne fail

        mov al, '5'
        call set_stage
        mov eax, SYS_FWRITE
        mov ebx, name_inner
        mov ecx, payload_inner
        mov edx, payload_inner_len
        xor esi, esi
        int 0x80
        cmp eax, 0
        jne fail

        ; --- read it back before touching metadata ---
        mov al, 'P'
        call set_stage
        mov eax, SYS_FREAD
        mov ebx, name_inner
        mov ecx, io_buf
        int 0x80
        cmp eax, payload_inner_len
        jne fail
        mov al, 'Q'
        call set_stage
        mov esi, io_buf
        mov edi, payload_inner
        mov ecx, payload_inner_len
        call compare

        ; --- metadata change inside the subdirectory ---
        ; This is the operation that used to rewrite a root directory sector.
        mov al, '6'
        call set_stage
        mov eax, SYS_CHMOD
        mov ebx, name_inner
        mov ecx, 0644o
        int 0x80
        cmp eax, 0
        jne fail

        ; the file itself must still read back
        mov al, '7'
        call set_stage
        mov eax, SYS_FREAD
        mov ebx, name_inner
        mov ecx, io_buf
        int 0x80
        cmp eax, payload_inner_len
        jne fail
        mov al, '8'
        call set_stage
        mov esi, io_buf
        mov edi, payload_inner
        mov ecx, payload_inner_len
        call compare

        ; --- back to the root: the root file must be untouched ---
        mov al, '9'
        call set_stage
        mov eax, SYS_CHDIR
        mov ebx, path_root
        int 0x80
        cmp eax, 0
        jne fail

        mov al, 'A'
        call set_stage
        mov eax, SYS_FREAD
        mov ebx, name_keep
        mov ecx, io_buf
        int 0x80
        cmp eax, payload_keep_len
        jne fail
        mov al, 'B'
        call set_stage
        mov esi, io_buf
        mov edi, payload_keep
        mov ecx, payload_keep_len
        call compare

        ; --- chmod in the root must not disturb the subdirectory either ---
        mov al, 'C'
        call set_stage
        mov eax, SYS_CHMOD
        mov ebx, name_keep
        mov ecx, 0600o
        int 0x80
        cmp eax, 0
        jne fail

        mov al, 'D'
        call set_stage
        mov eax, SYS_CHDIR
        mov ebx, name_dir
        int 0x80
        cmp eax, 0
        jne fail

        mov al, 'E'
        call set_stage
        mov eax, SYS_FREAD
        mov ebx, name_inner
        mov ecx, io_buf
        int 0x80
        cmp eax, payload_inner_len
        jne fail
        mov al, 'F'
        call set_stage
        mov esi, io_buf
        mov edi, payload_inner
        mov ecx, payload_inner_len
        call compare

        ; --- delete + recreate exercises the block-free path ---
        mov al, 'G'
        call set_stage
        mov eax, SYS_DELETE
        mov ebx, name_inner
        int 0x80
        cmp eax, 0
        jne fail

        mov al, 'H'
        call set_stage
        mov eax, SYS_FWRITE
        mov ebx, name_inner
        mov ecx, payload_inner2
        mov edx, payload_inner2_len
        xor esi, esi
        int 0x80
        cmp eax, 0
        jne fail

        mov al, 'I'
        call set_stage
        mov eax, SYS_FREAD
        mov ebx, name_inner
        mov ecx, io_buf
        int 0x80
        cmp eax, payload_inner2_len
        jne fail
        mov al, 'J'
        call set_stage
        mov esi, io_buf
        mov edi, payload_inner2
        mov ecx, payload_inner2_len
        call compare

        ; --- tidy up ---
        mov al, 'K'
        call set_stage
        mov eax, SYS_CHDIR
        mov ebx, path_root
        int 0x80
        mov eax, SYS_DELETE
        mov ebx, name_keep
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80


; Print the stage character so a hang can be localised on screen.
set_stage:
        mov [stage], al
        mov byte [stage + 1], 10
        mov byte [stage + 2], 0
        push ebx
        mov eax, SYS_PRINT
        mov ebx, stage
        int 0x80
        pop ebx
        ret

; ESI = got, EDI = want, ECX = length. Returns to caller, jumps to fail on mismatch.
compare:
        push esi
        push edi
        push ecx
        cld
        repe cmpsb
        pop ecx
        pop edi
        pop esi
        jne fail
        ret

fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, stage
        int 0x80
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

path_root:        db "/", 0
name_dir:         db "metadir", 0
name_keep:        db "keepme.txt", 0
name_inner:       db "inner.txt", 0

payload_keep:     db "ROOT-DIRECTORY-ENTRY-OK", 0
payload_keep_len  equ 23
payload_inner:    db "SUBDIR-PAYLOAD-OK", 0
payload_inner_len equ 17
payload_inner2:   db "RECREATED-PAYLOAD", 0
payload_inner2_len equ 17

msg_ok:   db "FS_META_OK", 10, 0
msg_fail: db "FS_META_FAIL stage ", 0

stage:    db '0', 10, 0, 0
io_buf:   times 256 db 0
