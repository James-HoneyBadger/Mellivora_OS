; vmtest3.asm - file-backed demand-paged mappings (SYS_MMAP_FILE)
;
; Checks the file-mapping contract added to docs/spec/VMM.md:
;   1. mapping a file allocates no memory; the first touch reads its block
;   2. the mapped bytes equal the file's contents
;   3. a byte past the end of the file reads as zero instead of faulting
;   4. a non-zero file offset maps the matching part of the file
;   5. the mapping is private: writing through it never reaches the file
;   6. a write to a read-only file mapping faults (only that task dies)
;   6b. splitting the mapping keeps each piece bound to its own file block
;   7. after munmap the range faults again
;   8. rewriting a file is visible to the next mapping (no stale cache)
;
; Prints VM3_OK on success, VMTEST3_FAIL stage <letter> on failure.

%include "syscalls.inc"

CRASH_STATUS equ 0x80000000

FILE_LEN   equ 5000              ; two full blocks and a 904-byte tail
MAP_LEN    equ 8192              ; map two pages: the second is half past EOF
READ_LEN   equ 4096

start:
        ; --- 0. start from a clean file ---
        mov al, '0'
        call set_stage
        mov eax, SYS_DELETE
        mov ebx, path_data
        int 0x80                        ; may not exist

        ; Build a recognisable pattern: byte i = i & 0xFF
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
        mov al, '1'
        call set_stage
        mov eax, SYS_FWRITE
        mov ebx, path_data
        mov ecx, pattern
        mov edx, FILE_LEN
        xor esi, esi
        int 0x80
        cmp eax, 0
        jne fail

        ; --- 1. mmap_file reserves, it does not allocate ---
        mov al, '2'
        call set_stage
        mov eax, SYS_MEMINFO
        int 0x80
        mov [free_base], eax

        mov eax, SYS_MMAP_FILE
        mov ebx, path_data
        mov ecx, MAP_LEN
        mov edx, PROT_READ
        xor esi, esi
        int 0x80
        cmp eax, -1
        je fail
        test eax, 0xFFF
        jnz fail
        mov [map_ro], eax

        mov eax, SYS_MEMINFO
        int 0x80
        cmp eax, [free_base]
        jne fail                        ; nothing was read or allocated yet

        ; --- 2. the mapped bytes are the file's bytes ---
        mov al, '3'
        call set_stage
        mov esi, [map_ro]
        mov edi, pattern
        mov ecx, READ_LEN
        call compare

        mov al, '4'
        call set_stage
        mov esi, [map_ro]
        add esi, READ_LEN
        mov edi, pattern + READ_LEN
        mov ecx, FILE_LEN - READ_LEN    ; tail of the file
        call compare

        ; --- 3. past the end of the file the page reads as zero ---
        mov al, '5'
        call set_stage
        mov esi, [map_ro]
        add esi, FILE_LEN
        mov ecx, MAP_LEN - FILE_LEN
        call check_zero

        ; --- 4. a non-zero offset maps the matching part of the file ---
        mov al, '6'
        call set_stage
        mov eax, SYS_MMAP_FILE
        mov ebx, path_data
        mov ecx, READ_LEN
        mov edx, PROT_READ
        mov esi, READ_LEN
        int 0x80
        cmp eax, -1
        je fail
        mov [map_off], eax
        mov esi, eax
        mov edi, pattern + READ_LEN
        mov ecx, FILE_LEN - READ_LEN
        call compare

        ; --- 5. writes through a private mapping never reach the file ---
        mov al, '7'
        call set_stage
        mov eax, SYS_MMAP_FILE
        mov ebx, path_data
        mov ecx, READ_LEN
        mov edx, PROT_READ | PROT_WRITE
        xor esi, esi
        int 0x80
        cmp eax, -1
        je fail
        mov [map_rw], eax

        mov edi, eax
        mov dword [edi], 0xDEADBEEF     ; must not touch the disk
        mov dword [edi + 4092], 0xFEEDFACE

        mov al, '8'
        call set_stage
        mov eax, SYS_FREAD
        mov ebx, path_data
        mov ecx, io_buf
        int 0x80
        cmp eax, FILE_LEN
        jne fail
        mov al, '9'
        call set_stage
        mov esi, io_buf
        mov edi, pattern
        mov ecx, READ_LEN               ; the first page the mapping changed
        call compare

        ; --- 6. writing through a read-only mapping faults ---
        mov al, 'A'
        call set_stage
        mov eax, [map_ro]
        mov [probe_addr], eax
        call check_crash_write
        test eax, eax
        jz fail

        ; --- 6b. splitting a file mapping keeps its file position ---
        ; Only page 0 of the new mapping is touched, so page 1 is still
        ; demand-zero when the area is split; reading it afterwards must read
        ; the file's second block, not the first one again.
        mov al, 'E'
        call set_stage
        mov eax, SYS_MMAP_FILE
        mov ebx, path_data
        mov ecx, MAP_LEN
        mov edx, PROT_READ
        xor esi, esi
        int 0x80
        cmp eax, -1
        je fail
        mov [map_split], eax
        mov edi, eax
        mov al, [edi]                   ; touch page 0 only

        mov eax, SYS_MPROTECT
        mov ebx, [map_split]
        mov ecx, READ_LEN
        xor edx, edx                    ; PROT_NONE for page 0
        int 0x80
        test eax, eax
        jnz fail

        mov al, 'F'
        call set_stage
        mov esi, [map_split]
        add esi, READ_LEN               ; page 1: the right-hand split piece
        mov edi, pattern + READ_LEN
        mov ecx, FILE_LEN - READ_LEN
        call compare

        mov al, 'G'
        call set_stage
        mov eax, [map_split]
        mov [probe_addr], eax           ; page 0 is PROT_NONE now
        call check_crash_read
        test eax, eax
        jz fail

        mov eax, SYS_MUNMAP
        mov ebx, [map_split]
        mov ecx, MAP_LEN
        int 0x80

        ; --- 7. after munmap the range faults ---
        mov al, 'B'
        call set_stage
        mov eax, SYS_MUNMAP
        mov ebx, [map_ro]
        mov ecx, MAP_LEN
        int 0x80
        test eax, eax
        jnz fail
        mov eax, [map_ro]
        mov [probe_addr], eax
        call check_crash_read
        test eax, eax
        jz fail

        ; --- 8. a rewritten file is visible to the next mapping ---
        ; The read warms the block cache; the new content has to reach a new
        ; mapping rather than the block that was current before the write.
        mov al, 'H'
        call set_stage
        mov eax, SYS_MMAP_FILE
        mov ebx, path_data
        mov ecx, READ_LEN
        mov edx, PROT_READ
        xor esi, esi
        int 0x80
        cmp eax, -1
        je fail
        mov [map_old], eax
        mov esi, eax
        mov edi, pattern
        mov ecx, READ_LEN
        call compare

        ; Invert every byte and write the file back.
        mov edi, pattern
        mov ecx, FILE_LEN
.invert:
        mov al, [edi]
        xor al, 0xFF
        mov [edi], al
        inc edi
        dec ecx
        jnz .invert

        mov eax, SYS_DELETE
        mov ebx, path_data
        int 0x80
        mov eax, SYS_FWRITE
        mov ebx, path_data
        mov ecx, pattern
        mov edx, FILE_LEN
        xor esi, esi
        int 0x80
        cmp eax, 0
        jne fail

        mov al, 'I'
        call set_stage
        mov eax, SYS_MMAP_FILE
        mov ebx, path_data
        mov ecx, READ_LEN
        mov edx, PROT_READ
        xor esi, esi
        int 0x80
        cmp eax, -1
        je fail
        mov [map_new], eax
        mov esi, eax
        mov edi, pattern
        mov ecx, READ_LEN
        call compare

        mov eax, SYS_MUNMAP
        mov ebx, [map_old]
        mov ecx, READ_LEN
        int 0x80
        mov eax, SYS_MUNMAP
        mov ebx, [map_new]
        mov ecx, READ_LEN
        int 0x80

        ; --- clean up ---
        mov eax, SYS_MUNMAP
        mov ebx, [map_off]
        mov ecx, READ_LEN
        int 0x80
        mov eax, SYS_MUNMAP
        mov ebx, [map_rw]
        mov ecx, READ_LEN
        int 0x80
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
; compare - byte-compare two buffers
;   In : ESI = a, EDI = b, ECX = length   Out: falls through to fail on
;        mismatch (the caller's stage letter is still in AL)
;-----------------------------------------------------------------------
compare:
        push eax
.comp_loop:
        test ecx, ecx
        jz .comp_ok
        mov al, [esi]
        cmp al, [edi]
        jne .comp_bad
        inc esi
        inc edi
        dec ecx
        jmp .comp_loop
.comp_bad:
        pop eax
        jmp fail
.comp_ok:
        pop eax
        ret

;-----------------------------------------------------------------------
; check_zero - every byte in the range must be zero
;   In : ESI = start, ECX = length
;-----------------------------------------------------------------------
check_zero:
        push eax
.zero_loop:
        test ecx, ecx
        jz .zero_ok
        cmp byte [esi], 0
        jne .zero_bad
        inc esi
        dec ecx
        jmp .zero_loop
.zero_bad:
        pop eax
        jmp fail
.zero_ok:
        pop eax
        ret

;-----------------------------------------------------------------------
; check_crash_read / check_crash_write - the probe must kill only the child
;   Out: EAX = 1 when the child exited with CRASH_STATUS
;-----------------------------------------------------------------------
check_crash_read:
        mov eax, SYS_FORK
        int 0x80
        cmp eax, -1
        je .ccr_no
        test eax, eax
        jz .ccr_child
        mov ebx, eax
        mov eax, SYS_WAITPID
        int 0x80
        cmp eax, CRASH_STATUS
        jne .ccr_no
        mov eax, 1
        ret
.ccr_no:
        xor eax, eax
        ret
.ccr_child:
        mov esi, [probe_addr]
        mov ebx, [esi]                  ; must fault
        mov eax, SYS_PRINT
        mov ebx, msg_no_fault
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

check_crash_write:
        mov eax, SYS_FORK
        int 0x80
        cmp eax, -1
        je .ccw_no
        test eax, eax
        jz .ccw_child
        mov ebx, eax
        mov eax, SYS_WAITPID
        int 0x80
        cmp eax, CRASH_STATUS
        jne .ccw_no
        mov eax, 1
        ret
.ccw_no:
        xor eax, eax
        ret
.ccw_child:
        mov esi, [probe_addr]
        mov dword [esi], 0x0BADF00D     ; must fault
        mov eax, SYS_PRINT
        mov ebx, msg_no_fault
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

;-----------------------------------------------------------------------
; set_stage / fail - report which step failed
;   In : AL = stage letter
;-----------------------------------------------------------------------
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

path_data:     db "vm3data.txt", 0
msg_ok:        db "VM3_OK", 10, 0
msg_fail:      db "VMTEST3_FAIL stage ", 0
msg_no_fault:  db "VMTEST3_FAIL_NO_FAULT", 10, 0
msg_newline:   db 10, 0

; Buffers live in the loaded image and in the default section: a `section`
; directive here would restart the address counter at zero, so every buffer
; would alias the program's own code instead of following it.
pattern:     times FILE_LEN db 0
io_buf:      times FILE_LEN + 32 db 0
map_ro:      dd 0
map_off:     dd 0
map_rw:      dd 0
map_split:   dd 0
map_old:     dd 0
map_new:     dd 0
probe_addr:  dd 0
free_base:   dd 0
stage:       db 0
stage_buf:   db 0, 0
