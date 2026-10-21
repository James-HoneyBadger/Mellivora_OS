; heapbig.asm - 100 MB heap workload (the P1 exit gate)
;
; The roadmap closes P1 only when a 100 MB heap workload passes. A single
; SYS_MALLOC is capped at VM_MAX_PAGES (16 MB), so the workload is twenty
; 5 MB blocks - which also exercises the per-task area table.
;
;   1. twenty 5 MB blocks are allocated (100 MB in total)
;   2. every page of every block is written with a block- and page-specific
;      marker (demand paging, so untouched pages would read as zero)
;   3. every marker is read back: the blocks are private and do not alias
;   4. every block is freed and the free-page count is back where it started
;   5. a single large block can be allocated again afterwards
;
; Prints HEAPBIG_OK on success, HEAPBIG_FAIL stage <letter> otherwise.
; Run it with at least 256 MB of guest RAM (make check-heap does).

%include "syscalls.inc"

BLOCKS     equ 20
BLOCK_MB   equ 5
BLOCK_LEN  equ BLOCK_MB * 1024 * 1024
TOTAL_MB   equ BLOCKS * BLOCK_MB          ; 100 MB

start:
        ; --- 1. allocate the blocks ---
        mov al, '1'
        call set_stage
        mov eax, SYS_MEMINFO
        int 0x80
        mov [free_base], eax

        xor edi, edi
.alloc:
        cmp edi, BLOCKS
        jae .alloc_done
        mov [idx], edi
        mov eax, SYS_MALLOC
        mov ebx, BLOCK_LEN
        int 0x80
        test eax, eax
        jz fail
        mov edi, [idx]
        mov [blocks + edi*4], eax
        ; blocks must not overlap each other
        mov esi, eax
        xor ecx, ecx
.overlap_check:
        cmp ecx, edi
        jae .overlap_ok
        mov edx, [blocks + ecx*4]
        mov eax, esi
        sub eax, edx
        cmp eax, BLOCK_LEN
        jb fail                         ; inside an earlier block
        mov eax, edx
        sub eax, esi
        cmp eax, BLOCK_LEN
        jb fail
        inc ecx
        jmp .overlap_check
.overlap_ok:
        mov edi, [idx]
        inc edi
        jmp .alloc
.alloc_done:

        ; --- 2. write a marker into every page ---
        mov al, '2'
        call set_stage
        xor edi, edi
.fill_block:
        cmp edi, BLOCKS
        jae .fill_done
        mov esi, [blocks + edi*4]
        mov ebx, edi
        inc ebx                         ; block number, 1-based
        mov ecx, BLOCK_LEN / 4096
.fill_page:
        mov eax, ebx
        shl eax, 16
        xor eax, ecx                    ; block << 16 | page
        mov dword [esi], eax
        add esi, 4096
        dec ecx
        jnz .fill_page
        inc edi
        jmp .fill_block
.fill_done:

        ; --- 3. read every marker back ---
        mov al, '3'
        call set_stage
        xor edi, edi
.verify_block:
        cmp edi, BLOCKS
        jae .verify_done
        mov esi, [blocks + edi*4]
        mov ebx, edi
        inc ebx
        mov ecx, BLOCK_LEN / 4096
.verify_page:
        mov eax, ebx
        shl eax, 16
        xor eax, ecx
        cmp dword [esi], eax
        jne fail
        add esi, 4096
        dec ecx
        jnz .verify_page
        inc edi
        jmp .verify_block
.verify_done:

        ; --- 4. free everything; the pages must come back ---
        mov al, '4'
        call set_stage
        xor edi, edi
.free_block:
        cmp edi, BLOCKS
        jae .free_done
        mov eax, SYS_FREE
        mov ebx, [blocks + edi*4]
        int 0x80
        cmp eax, 0
        jne fail
        inc edi
        jmp .free_block
.free_done:
        mov eax, SYS_MEMINFO
        int 0x80
        mov ecx, [free_base]
        sub ecx, eax
        cmp ecx, 64                     ; page tables cloned by demands may stay
        ja fail

        ; --- 5. a large block still allocates afterwards ---
        mov al, '5'
        call set_stage
        mov eax, SYS_MALLOC
        mov ebx, 16 * 1024 * 1024       ; the per-mapping maximum
        int 0x80
        test eax, eax
        jz fail
        mov [big], eax
        mov edi, eax
        mov dword [edi], 0xC0FFEE00
        mov dword [edi + 16 * 1024 * 1024 - 4], 0xDECAF000
        cmp dword [edi], 0xC0FFEE00
        jne fail
        cmp dword [edi + 16 * 1024 * 1024 - 4], 0xDECAF000
        jne fail
        mov eax, SYS_FREE
        mov ebx, [big]
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

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

msg_ok:      db "HEAPBIG_OK", 10, 0
msg_fail:    db "HEAPBIG_FAIL stage ", 0
msg_newline: db 10, 0

; Variables live in the loaded image and in the default section: a `section`
; directive here would restart the address counter at zero, so every variable
; would alias the program's own code instead of following it.
idx:         dd 0
big:         dd 0
free_base:   dd 0
stage_buf:   db 0, 0
blocks:      times BLOCKS dd 0
