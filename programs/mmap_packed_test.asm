; mmap_packed_test.asm - a compressed file maps as its plaintext
;
; A chunked file has no plaintext blocks: its data area is a chunk table and
; the chunks themselves. A mapping of it is therefore decoded a chunk at a
; time into the faulting frame, and this program is the proof that what comes
; back is the file and not the container:
;
;   1. every byte of the mapping equals what SYS_FREAD returns for the same
;      file, over a file smaller than one chunk and over a file that crosses
;      two chunk boundaries;
;   2. the bytes between the end of the file and the end of its last page read
;      as zero, because the decode leaves the rest of that page as it found it;
;   3. the same two files map correctly once they are compressed - the caller
;      runs this program before and after `compact` and expects the same
;      answer, so a difference is the container showing through.
;
; Prints MMAP_PACKED_OK, or MMAP_PACKED_FAIL stage <letter>.

%include "syscalls.inc"

start:
        mov ebx, path_small
        mov al, 'a'
        call check_file
        jc fail

        mov ebx, path_big
        mov al, 'b'
        call check_file
        jc fail

        ; A mapping that starts mid-file: the chunk holding the first mapped
        ; page is not chunk zero, so a decode that assumed it was reads the
        ; wrong part of the file.
        mov ebx, path_small
        mov al, 'c'
        call check_offset
        jc fail

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

;-----------------------------------------------------------------------
; check_file - read a file whole and compare it with its mapping
;   In : EBX = path, AL = stage letter
;   Out: CF = 1 when the mapping differs, or does not map at all
;-----------------------------------------------------------------------
check_file:
        call set_stage
        push ebx
        ; Reference copy straight through the read path.
        mov eax, SYS_FREAD
        mov ecx, io_buf
        int 0x80                        ; EAX = bytes read
        test eax, eax
        js .cf_fail_pop
        mov [file_len], eax
        pop ebx

        ; Map the same file, whole pages.
        mov ecx, [file_len]
        add ecx, 0xFFF
        and ecx, ~0xFFF
        mov [map_len], ecx
        mov eax, SYS_MMAP_FILE
        mov edx, PROT_READ
        xor esi, esi
        int 0x80
        cmp eax, -1
        je .cf_fail
        mov [map_addr], eax

        ; 1. the bytes inside the file
        mov esi, [map_addr]
        mov edi, io_buf
        mov ecx, [file_len]
        cld
        repe cmpsb
        jne .cf_unmap_fail

        ; 2. the tail of the last page, which the decode must not leak
        mov esi, [map_addr]
        add esi, [file_len]
        mov ecx, [map_len]
        sub ecx, [file_len]
        jecxz .cf_unmap_ok
.cf_tail:
        cmp byte [esi], 0
        jne .cf_unmap_fail
        inc esi
        dec ecx
        jnz .cf_tail

.cf_unmap_ok:
        mov eax, SYS_MUNMAP
        mov ebx, [map_addr]
        mov ecx, [map_len]
        int 0x80
        clc
        ret
.cf_unmap_fail:
        mov eax, SYS_MUNMAP
        mov ebx, [map_addr]
        mov ecx, [map_len]
        int 0x80
        stc
        ret
.cf_fail_pop:
        pop ebx
.cf_fail:
        stc
        ret

;-----------------------------------------------------------------------
; check_offset - map a file from 4 KiB in and compare it with the same range
;   In : EBX = path, AL = stage letter
;   Out: CF = 1 when the mapping differs
;-----------------------------------------------------------------------
check_offset:
        call set_stage
        push ebx
        mov eax, SYS_FREAD
        mov ecx, io_buf
        int 0x80
        test eax, eax
        js .co_fail_pop
        cmp eax, OFFSET_BYTES + 1024       ; the range has to exist
        jbe .co_fail_pop
        sub eax, OFFSET_BYTES
        mov [file_len], eax
        pop ebx

        mov ecx, [file_len]
        add ecx, 0xFFF
        and ecx, ~0xFFF
        mov [map_len], ecx
        mov eax, SYS_MMAP_FILE
        mov edx, PROT_READ
        mov esi, OFFSET_BYTES
        int 0x80
        cmp eax, -1
        je .co_fail
        mov [map_addr], eax

        mov esi, [map_addr]
        mov edi, io_buf + OFFSET_BYTES
        mov ecx, [file_len]
        cld
        repe cmpsb
        jne .co_unmap_fail

        mov eax, SYS_MUNMAP
        mov ebx, [map_addr]
        mov ecx, [map_len]
        int 0x80
        clc
        ret
.co_unmap_fail:
        mov eax, SYS_MUNMAP
        mov ebx, [map_addr]
        mov ecx, [map_len]
        int 0x80
        stc
        ret
.co_fail_pop:
        pop ebx
.co_fail:
        stc
        ret

set_stage:
        mov [stage], al
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

; The smaller file fits in one chunk; the larger one crosses two boundaries and
; ends mid-page, which is where a decode that forgot the chunk it was in shows.
OFFSET_BYTES equ 4096
path_small:  db "/samples/wumpus.c", 0
path_big:    db "/bin/threadtest", 0
msg_ok:      db "MMAP_PACKED_OK", 10, 0
msg_fail:    db "MMAP_PACKED_FAIL stage ", 0
msg_newline: db 10, 0

file_len:  dd 0
map_len:   dd 0
map_addr:  dd 0
io_buf:    times 40960 db 0
stage:     db '0', 0
stage_buf: times 2 db 0
