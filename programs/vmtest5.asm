; vmtest5.asm - two shared mappings of one file are coherent
;
; A shared file mapping used to keep private frames, so two mappings of the same
; file disagreed with each other until both were released, and the last release
; won. The page cache gives each file block one frame that every shared mapping
; maps. This checks:
;
;   1. two shared mappings of one file both start out matching the file
;   2. a write through one mapping is visible through the other, both ways
;   3. a forked child writes the same pages (a shared mapping is not COW)
;   4. releasing both mappings writes everything back
;   5. a fresh mapping of the written file reads the new contents
;
; Prints VM5_OK on success, VMTEST5_FAIL stage <letter> otherwise.

%include "syscalls.inc"

FILE_LEN   equ 12288             ; three whole blocks
BLOCK      equ 4096

MARK_A     equ 0x11112222
MARK_B     equ 0x33334444
MARK_C     equ 0x55556666
MARK_CHILD equ 0x77778888

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

        ; --- 1. map the same file shared twice ---
        mov al, '1'
        call set_stage
        mov eax, SYS_MMAP_FILE
        mov ebx, path_data
        mov ecx, FILE_LEN
        mov edx, PROT_READ | PROT_WRITE | MF_SHARED
        xor esi, esi
        int 0x80
        cmp eax, -1
        je fail
        test eax, 0xFFF
        jnz fail
        mov [map_a], eax

        mov al, '2'
        call set_stage
        mov eax, SYS_MMAP_FILE
        mov ebx, path_data
        mov ecx, FILE_LEN
        mov edx, PROT_READ | PROT_WRITE | MF_SHARED
        xor esi, esi
        int 0x80
        cmp eax, -1
        je fail
        test eax, 0xFFF
        jnz fail
        mov [map_b], eax
        cmp eax, [map_a]
        je fail                         ; two mappings must not be one range

        ; both read the file's own bytes
        mov al, '3'
        call set_stage
        mov esi, [map_a]
        mov edi, pattern
        mov ecx, FILE_LEN
        call compare
        mov al, '4'
        call set_stage
        mov esi, [map_b]
        mov edi, pattern
        mov ecx, FILE_LEN
        call compare

        ; --- 2. writes cross between the mappings, in both directions ---
        mov al, '5'
        call set_stage
        mov edi, [map_a]
        mov dword [edi], MARK_A
        mov esi, [map_b]
        cmp dword [esi], MARK_A         ; visible through the other mapping
        jne fail

        mov al, '6'
        call set_stage
        mov edi, [map_b]
        mov dword [edi + BLOCK], MARK_B
        mov esi, [map_a]
        cmp dword [esi + BLOCK], MARK_B
        jne fail

        mov al, '7'
        call set_stage
        mov edi, [map_a]
        mov dword [edi + BLOCK + BLOCK], MARK_C
        mov esi, [map_b]
        cmp dword [esi + BLOCK + BLOCK], MARK_C
        jne fail

        ; --- 3. a forked child writes the same pages ---
        mov al, '8'
        call set_stage
        mov eax, SYS_FORK
        int 0x80
        cmp eax, -1
        je fail
        test eax, eax
        jz .child

        mov ebx, eax
        mov eax, SYS_WAITPID
        int 0x80
        test eax, eax
        jnz fail

        mov al, '9'
        call set_stage
        mov esi, [map_a]
        cmp dword [esi], MARK_CHILD     ; the child wrote the same frame
        jne fail
        mov esi, [map_b]
        cmp dword [esi], MARK_CHILD
        jne fail

        ; --- 4. releasing both mappings writes everything back ---
        mov al, 'A'
        call set_stage
        mov eax, SYS_MUNMAP
        mov ebx, [map_a]
        mov ecx, FILE_LEN
        int 0x80
        test eax, eax
        jnz fail

        mov al, 'B'
        call set_stage
        mov eax, SYS_MUNMAP
        mov ebx, [map_b]
        mov ecx, FILE_LEN
        int 0x80
        test eax, eax
        jnz fail

        mov al, 'C'
        call set_stage
        mov eax, SYS_FREAD
        mov ebx, path_data
        mov ecx, io_buf
        int 0x80
        cmp eax, FILE_LEN               ; length unchanged
        jne fail
        cmp dword [io_buf], MARK_CHILD
        jne fail
        cmp dword [io_buf + BLOCK], MARK_B
        jne fail
        cmp dword [io_buf + BLOCK + BLOCK], MARK_C
        jne fail

        ; --- 5. a new shared mapping reads what was written ---
        mov al, 'D'
        call set_stage
        mov eax, SYS_MMAP_FILE
        mov ebx, path_data
        mov ecx, FILE_LEN
        mov edx, PROT_READ | PROT_WRITE | MF_SHARED
        xor esi, esi
        int 0x80
        cmp eax, -1
        je fail
        mov [map_c], eax
        mov esi, eax
        cmp dword [esi], MARK_CHILD
        jne fail
        cmp dword [esi + BLOCK], MARK_B
        jne fail
        cmp dword [esi + BLOCK + BLOCK], MARK_C
        jne fail

        ; page 1 and page 2 kept the bytes nobody wrote
        mov al, 'E'
        call set_stage
        mov esi, [map_c]
        add esi, 4
        mov edi, pattern + 4
        mov ecx, BLOCK - 4
        call compare
        mov esi, [map_c]
        add esi, BLOCK + 4
        mov edi, pattern + BLOCK + 4
        mov ecx, BLOCK - 4
        call compare

        ; --- 6. mprotect splits the area; each piece keeps its own block ---
        mov al, 'F'
        call set_stage
        mov eax, SYS_MPROTECT
        mov ebx, [map_c]
        add ebx, BLOCK
        mov ecx, BLOCK
        mov edx, PROT_READ
        int 0x80
        test eax, eax
        jnz fail
        mov esi, [map_c]
        cmp dword [esi], MARK_CHILD
        jne fail
        cmp dword [esi + BLOCK], MARK_B
        jne fail
        cmp dword [esi + BLOCK + BLOCK], MARK_C
        jne fail

        ; The split leaves three areas, so release them one at a time.
        mov al, 'G'
        call set_stage
        mov dword [probe_off], 0
.probe:
        mov eax, SYS_MUNMAP
        mov ebx, [map_c]
        add ebx, [probe_off]
        mov ecx, BLOCK
        int 0x80
        test eax, eax
        jnz fail
        add dword [probe_off], BLOCK
        cmp dword [probe_off], FILE_LEN
        jb .probe

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

.child:
        mov edi, [map_b]
        mov dword [edi], MARK_CHILD
        mov esi, [map_a]
        cmp dword [esi], MARK_CHILD     ; the parent's mapping sees it too
        jne .child_fail
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80
.child_fail:
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

;-----------------------------------------------------------------------
; compare - byte-compare two buffers, fail stage <letter> on mismatch
;   In : ESI = a, EDI = b, ECX = length
;-----------------------------------------------------------------------
compare:
        push eax
        mov [cmp_base], esi
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
        mov eax, esi
        sub eax, [cmp_base]
        mov [cmp_off], eax
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
        mov ebx, msg_at
        int 0x80
        mov edx, [cmp_off]
        mov ecx, 8
.hx:
        rol edx, 4
        mov ebx, edx
        and ebx, 0x0F
        cmp ebx, 10
        jb .hx_digit
        add ebx, 'a' - 10 - '0'
.hx_digit:
        add ebx, '0'
        mov eax, SYS_PUTCHAR
        int 0x80
        dec ecx
        jnz .hx
        mov eax, SYS_PRINT
        mov ebx, msg_newline
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

path_data:   db "vm5data.txt", 0
msg_ok:      db "VM5_OK", 10, 0
msg_fail:    db "VMTEST5_FAIL stage ", 0
msg_at:      db " offset 0x", 0
msg_newline: db 10, 0

; Variables live in the loaded image: a `section` directive here would restart
; the address counter at zero and alias the program's own code.
map_a:       dd 0
map_b:       dd 0
map_c:       dd 0
stage_buf:   db 0, 0
cmp_base:    dd 0
cmp_off:     dd 0
probe_off:   dd 0
pattern:     times FILE_LEN db 0
io_buf:      times FILE_LEN + 64 db 0
