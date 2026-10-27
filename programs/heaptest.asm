; heaptest.asm - SYS_MALLOC / SYS_FREE / SYS_REALLOC on top of the VMM
;
; The heap used to be raw PMM pages published at their physical addresses, so
; a block held whatever the previous owner left in it and free() left the
; mapping in place. The heap is now anonymous demand-zero memory, which this
; test checks:
;
;   1. a fresh block reads as zero
;   2. a block bigger than the program window works (it lives in the demand
;      zone, not below the stack region)
;   3. realloc keeps the bytes the caller wrote
;   4. a freed block is gone from the address space (accessing it faults)
;   5. a re-allocated block is zeroed again: no stale bytes from the previous
;      owner
;   6. free() of a bogus pointer is refused instead of unmapping something else
;   7. a demand-zone buffer can still be played (the SB16 path stages it)
;
; Prints HEAP_OK on success, HEAPTEST_FAIL stage <letter> otherwise.

%include "syscalls.inc"

; Variables sit immediately after the entry jump so their offsets are known
; before the code that uses them is assembled.
blk:         dd 0
big:         dd 0
grown:       dd 0
again:       dd 0
probe_addr:  dd 0
pcm:         dd 0
stage_buf:   db 0, 0


CRASH_STATUS equ 0x80000000
PROGRAM_BASE equ 0x00500000      ; kernel.asm PROGRAM_BASE
WINDOW_END   equ 0x00600000      ; kernel.asm PROGRAM_END_ADDR

SMALL_LEN  equ 3000              ; one page
BIG_LEN    equ 4194304           ; 4 MB: far larger than the 1 MB window
GROW_LEN   equ 8192

start:
        ; --- 1. a fresh block is zero-filled ---
        mov al, '1'
        call set_stage
        mov eax, SYS_MALLOC
        mov ebx, SMALL_LEN
        int 0x80
        test eax, eax
        jz fail
        mov [blk], eax
        mov esi, eax
        mov ecx, SMALL_LEN
        call check_zero

        ; ...and writable
        mov al, '2'
        call set_stage
        mov edi, [blk]
        mov dword [edi], 0x11223344
        cmp dword [edi], 0x11223344
        jne fail
        mov dword [edi + SMALL_LEN - 4], 0x55667788
        cmp dword [edi + SMALL_LEN - 4], 0x55667788
        jne fail

        ; --- 2. a block larger than the window ---
        mov al, '3'
        call set_stage
        mov eax, SYS_MALLOC
        mov ebx, BIG_LEN
        int 0x80
        test eax, eax
        jz fail
        mov [big], eax
        cmp eax, WINDOW_END
        jb fail                         ; must not be inside the window

        ; every 4 KB page of it reads as zero and is writable
        mov edi, [big]
        mov ecx, BIG_LEN / 4096
.big_pages:
        cmp dword [edi], 0
        jne fail
        mov dword [edi], ecx            ; page index as a marker
        add edi, 4096
        dec ecx
        jnz .big_pages

        mov edi, [big]
        mov ecx, BIG_LEN / 4096
.big_verify:
        cmp dword [edi], ecx
        jne fail
        add edi, 4096
        dec ecx
        jnz .big_verify

        ; --- 3. realloc keeps the data ---
        mov al, '4'
        call set_stage
        mov eax, SYS_REALLOC
        mov ebx, [blk]
        mov ecx, GROW_LEN
        mov edx, SMALL_LEN
        int 0x80
        test eax, eax
        jz fail
        mov [grown], eax
        cmp dword [eax], 0x11223344
        jne fail
        cmp dword [eax + SMALL_LEN - 4], 0x55667788
        jne fail
        ; the tail is new memory and must still be zero
        mov esi, [grown]
        add esi, SMALL_LEN
        mov ecx, GROW_LEN - SMALL_LEN
        call check_zero

        ; --- 4. a freed block leaves the address space ---
        mov al, '5'
        call set_stage
        mov eax, SYS_FREE
        mov ebx, [grown]
        int 0x80
        cmp eax, 0
        jne fail
        mov eax, [grown]
        mov [probe_addr], eax
        call check_crash_read
        test eax, eax
        jz fail

        ; --- 5. the reclaimed range comes back zeroed ---
        mov al, '6'
        call set_stage
        mov eax, SYS_MALLOC
        mov ebx, GROW_LEN
        int 0x80
        test eax, eax
        jz fail
        mov [again], eax
        mov esi, eax
        mov ecx, GROW_LEN
        call check_zero

        ; --- 6. free() rejects a pointer that is not a block base ---
        mov al, '7'
        call set_stage
        mov eax, [again]
        add eax, 4096
        mov ebx, eax
        mov eax, SYS_FREE
        int 0x80
        cmp eax, -1
        jne fail
        ; the block itself is still there
        mov eax, SYS_FREE
        mov ebx, [again]
        int 0x80
        cmp eax, 0
        jne fail

        mov eax, SYS_FREE
        mov ebx, [big]
        int 0x80
        mov eax, SYS_FREE
        mov ebx, [blk]
        int 0x80                        ; already realloc'd away: refused

        ; --- 8. a demand-zone buffer can still be handed to the audio path ---
        ; The SB16 reads memory by physical address, so SYS_AUDIO_PLAY copies
        ; such buffers into its staging range first. A missing card reports -1
        ; and is not a failure.
        mov al, '8'
        call set_stage
        mov eax, SYS_MALLOC
        mov ebx, 4096
        int 0x80
        test eax, eax
        jz fail
        mov [pcm], eax
        mov edi, eax
        mov ecx, 1024
        xor eax, eax
        cld
        rep stosd                       ; silence
        mov eax, SYS_AUDIO_PLAY
        mov ebx, [pcm]
        mov ecx, 64
        mov edx, 8000                   ; 8-bit mono 8 kHz
        int 0x80
        test eax, eax
        jz .pcm_stop
        cmp eax, -1
        jne fail
.pcm_stop:
        mov eax, SYS_AUDIO_STOP
        int 0x80
        mov eax, SYS_FREE
        mov ebx, [pcm]
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

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
        jmp near fail                   ; fixed size: keeps passes stable
.zero_ok:
        pop eax
        ret

;-----------------------------------------------------------------------
; check_crash_read - reading the probe address must kill only the child
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

msg_ok:        db "HEAP_OK", 10, 0
msg_fail:      db "HEAPTEST_FAIL stage ", 0
msg_no_fault:  db "HEAPTEST_FAIL_NO_FAULT", 10, 0
msg_newline:   db 10, 0
