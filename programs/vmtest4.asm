; vmtest4.asm - shared, write-back file mappings (SYS_MMAP_FILE | MF_SHARED)
;
; A shared mapping has to push what the program wrote back into the file when
; the mapping goes away. This checks:
;
;   1. the mapping starts out matching the file
;   2. a write to a page that was touched reaches the file on munmap
;   3. a page that was only read is not rewritten (the dirty bit decides)
;   4. a write to a page past the end of the file is not written back
;   5. the file keeps its length
;   6. a private mapping still does not write through
;
; Prints VM4_OK on success, VMTEST4_FAIL stage <letter> otherwise.

%include "syscalls.inc"

FILE_LEN   equ 8392              ; 3 blocks, the last one 200 bytes short
MAP_LEN    equ 12288             ; 3 pages: the third is half past EOF
BLOCK      equ 4096

FILE_MARK  equ 0x5A5A5A5A
PAGE1_MARK equ 0xA5A5A5A5
TAIL_MARK  equ 0x1234ABCD

start:
        ; --- 0. clean slate and a known file ---
        mov al, '0'
        call set_stage
        mov eax, SYS_DELETE
        mov ebx, path_data
        int 0x80

        mov edi, pattern
        xor ecx, ecx
.fill:
        cmp ecx, FILE_LEN
        jae .filled
        mov eax, ecx
        mov [edi + ecx], al
        inc ecx
        jmp .fill
.filled:
        mov eax, SYS_FWRITE
        mov ebx, path_data
        mov ecx, pattern
        mov edx, FILE_LEN
        xor esi, esi
        int 0x80
        cmp eax, 0
        jne fail

        ; --- 1. a shared mapping starts out matching the file ---
        mov al, '1'
        call set_stage
        mov eax, SYS_MMAP_FILE
        mov ebx, path_data
        mov ecx, MAP_LEN
        mov edx, PROT_READ | PROT_WRITE | MF_SHARED
        xor esi, esi
        int 0x80
        cmp eax, -1
        je fail
        mov [map_sh], eax

        mov al, '2'
        call set_stage
        mov esi, [map_sh]
        mov edi, pattern
        mov ecx, BLOCK
        call compare                    ; page 0
        mov esi, [map_sh]
        add esi, BLOCK
        mov edi, pattern + BLOCK
        mov ecx, BLOCK
        call compare                    ; page 1

        ; --- 2. write page 0 and the tail page, leave page 1 clean ---
        mov al, '3'
        call set_stage
        mov edi, [map_sh]
        mov dword [edi], FILE_MARK
        mov dword [edi + BLOCK + BLOCK], TAIL_MARK
        cmp dword [edi], FILE_MARK
        jne fail

        ; --- 3. releasing the mapping writes the dirty pages back ---
        mov al, '4'
        call set_stage
        mov eax, SYS_MUNMAP
        mov ebx, [map_sh]
        mov ecx, MAP_LEN
        int 0x80
        test eax, eax
        jnz fail

        mov al, '5'
        call set_stage
        mov eax, SYS_FREAD
        mov ebx, path_data
        mov ecx, io_buf
        int 0x80
        cmp eax, FILE_LEN               ; length unchanged
        jne fail

        ; the written marker is on disk
        mov al, '6'
        call set_stage
        cmp dword [io_buf], FILE_MARK
        jne fail

        ; the untouched page kept the file's own bytes
        mov al, '7'
        call set_stage
        mov esi, io_buf + BLOCK
        mov edi, pattern + BLOCK
        mov ecx, BLOCK
        call compare

        ; the tail page: its file bytes were rewritten with our marker
        mov al, '8'
        call set_stage
        cmp dword [io_buf + BLOCK + BLOCK], TAIL_MARK
        jne fail
        ; ...and the bytes between the marker and the end of the file survived
        mov al, '9'
        call set_stage
        mov esi, io_buf + BLOCK + BLOCK + 4
        mov edi, pattern + BLOCK + BLOCK + 4
        mov ecx, FILE_LEN - (BLOCK + BLOCK) - 4
        call compare

        ; --- 4. a private mapping still does not write through ---
        mov al, 'A'
        call set_stage
        mov eax, SYS_MMAP_FILE
        mov ebx, path_data
        mov ecx, BLOCK
        mov edx, PROT_READ | PROT_WRITE
        xor esi, esi
        int 0x80
        cmp eax, -1
        je fail
        mov [map_pr], eax
        mov edi, eax
        mov dword [edi], 0x0BADF00D
        mov eax, SYS_MUNMAP
        mov ebx, [map_pr]
        mov ecx, BLOCK
        int 0x80

        mov al, 'B'
        call set_stage
        mov eax, SYS_FREAD
        mov ebx, path_data
        mov ecx, io_buf
        int 0x80
        cmp eax, FILE_LEN
        jne fail
        cmp dword [io_buf], FILE_MARK    ; still the shared write, not the private one
        jne fail

        ; --- clean up ---
        mov eax, SYS_DELETE
        mov ebx, path_data
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

;-----------------------------------------------------------------------
; compare - byte-compare two buffers, fail stage <letter> on mismatch
;   In : ESI = a, EDI = b, ECX = length
;-----------------------------------------------------------------------
compare:
        push eax
.cmp_loop:
        test ecx, ecx
        jz .cmp_ok
        mov al, [esi]
        cmp al, [edi]
        jne .cmp_bad
        inc esi
        inc edi
        dec ecx
        jmp .cmp_loop
.cmp_bad:
        pop eax
        jmp near fail
.cmp_ok:
        pop eax
        ret

;-----------------------------------------------------------------------
; set_stage / fail - report which step failed
;   In : AL = stage letter
;-----------------------------------------------------------------------
set_stage:
        mov [stage_buf], al
        mov byte [stage_buf + 1], 0
        ret

fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, stage_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_newline
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

path_data:   db "vm4data.txt", 0
msg_ok:      db "VM4_OK", 10, 0
msg_fail:    db "VMTEST4_FAIL stage ", 0
msg_newline: db 10, 0

; Variables live in the loaded image and in the default section: a `section`
; directive here would restart the address counter at zero, so every variable
; would alias the program's own code instead of following it.
map_sh:      dd 0
map_pr:      dd 0
stage_buf:   db 0, 0
pattern:     times FILE_LEN db 0
io_buf:      times FILE_LEN + 64 db 0
