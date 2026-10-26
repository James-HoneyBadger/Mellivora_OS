; vmtest2.asm - demand-zero, partial unmap/mprotect and fork isolation
;
; Extends vmtest.asm with the P1 contract from docs/spec/VMM.md:
;   1. mmap reserves virtual space only: free memory is unchanged until a page
;      is touched, and the first read returns zero
;   2. munmap of a middle range keeps the pages on both sides and makes only
;      the removed range fault
;   3. mprotect of a sub-range changes only that range; PROT_NONE denies
;      access but keeps the data for a later mprotect(PROT_WRITE)
;   4. a forked child's writes to an mmap page stay private
;   5. a dead child's mappings are reclaimed (free memory returns)
;
; Prints VM2_OK on success, VMTEST2_FAIL otherwise.

%include "syscalls.inc"

CRASH_STATUS equ 0x80000000

PAGES        equ 64             ; 256 KB mapping

start:
        ; ---------------------------------------------------------------
        ; 0. Find an address this machine will accept as a hint
        ; ---------------------------------------------------------------
        ; The demand zone starts above the highest frame of RAM, so the fixed
        ; 256 MB address this test used to name is user space on a 128 MB
        ; machine and inside physical memory on a 512 MB one. Ask for a page
        ; with no hint, keep the address the allocator chose (4 MB aligned so
        ; it starts a page table, which makes the table accounting below
        ; exact), then hand it back: that is a hint this machine honours.
        mov eax, SYS_MMAP
        mov ebx, 0x400000
        mov ecx, 0x400000
        mov edx, PROT_READ | PROT_WRITE
        int 0x80
        cmp eax, -1
        je fail
        mov ebx, eax                 ; give the probe back exactly as it came
        mov ecx, 0x400000
        mov [map1], eax              ; (map1 is overwritten in case 1)
        mov eax, SYS_MUNMAP
        int 0x80
        test eax, eax
        jnz fail
        mov eax, [map1]
        add eax, 0x3FFFFF
        and eax, 0xFFC00000
        mov [map_hint], eax

        ; ---------------------------------------------------------------
        ; 1. mmap is demand-zero: reserving pages allocates nothing
        ; ---------------------------------------------------------------
        mov eax, SYS_MEMINFO
        int 0x80
        mov [free_base], eax

        mov eax, SYS_MMAP
        mov ebx, [map_hint]
        mov ecx, PAGES * 4096
        mov edx, PROT_READ | PROT_WRITE
        int 0x80
        cmp eax, -1
        je fail
        cmp eax, [map_hint]
        jne fail
        mov [map1], eax

        mov eax, SYS_MEMINFO
        int 0x80
        cmp eax, [free_base]
        jne fail                        ; mmap itself must not allocate

        mov edi, [map1]
        cmp dword [edi], 0              ; first touch: allocate one zero page
        jne fail

        mov eax, SYS_MEMINFO
        int 0x80
        mov ecx, [free_base]
        sub ecx, eax
        cmp ecx, 1
        jb fail
        cmp ecx, 2                      ; frame + at most one page table
        ja fail

        ; Fill every page with a distinct value and verify the writes
        mov edi, [map1]
        mov ecx, PAGES
        mov eax, 4096
.fill:
        mov [edi], eax
        add edi, 4096
        add eax, 4096
        dec ecx
        jnz .fill

        mov edi, [map1]
        xor esi, esi
        mov ecx, PAGES
        call verify_pages
        jc fail

        ; ---------------------------------------------------------------
        ; 2. Partial munmap: the middle range alone is released
        ; ---------------------------------------------------------------
        mov eax, SYS_MUNMAP
        mov ebx, [map1]
        add ebx, 16 * 4096
        mov ecx, 16 * 4096
        int 0x80
        test eax, eax
        jnz fail

        mov edi, [map1]
        xor esi, esi
        mov ecx, 16
        call verify_pages
        jc fail
        mov edi, [map1]
        mov esi, 32
        mov ecx, PAGES - 32
        call verify_pages
        jc fail

        mov eax, [map1]
        add eax, 16 * 4096
        mov [probe_addr], eax
        call check_crash_read
        test eax, eax
        jz fail

        ; Trimming the front must leave the rest intact
        mov eax, SYS_MUNMAP
        mov ebx, [map1]
        mov ecx, 4 * 4096
        int 0x80
        test eax, eax
        jnz fail

        mov edi, [map1]
        mov esi, 4
        mov ecx, 12
        call verify_pages
        jc fail

        mov eax, [map1]
        mov [probe_addr], eax
        call check_crash_read
        test eax, eax
        jz fail

        ; ---------------------------------------------------------------
        ; 3. Partial mprotect: only pages 4..7 become read-only
        ; ---------------------------------------------------------------
        mov eax, SYS_MPROTECT
        mov ebx, [map1]
        add ebx, 4 * 4096
        mov ecx, 4 * 4096
        mov edx, PROT_READ
        int 0x80
        test eax, eax
        jnz fail

        mov edi, [map1]
        mov esi, 4
        mov ecx, 4
        call verify_pages
        jc fail

        ; The neighbouring page kept write access
        mov edi, [map1]
        mov dword [edi + 8 * 4096], 0x12345678

        mov eax, [map1]
        add eax, 4 * 4096
        mov [probe_addr], eax
        call check_crash_write
        test eax, eax
        jz fail

        ; Restoring write access must let the page be written again
        mov eax, SYS_MPROTECT
        mov ebx, [map1]
        add ebx, 4 * 4096
        mov ecx, 4 * 4096
        mov edx, PROT_READ | PROT_WRITE
        int 0x80
        test eax, eax
        jnz fail
        mov edi, [map1]
        mov dword [edi + 4 * 4096], 0xCAFEBABE
        cmp dword [edi + 4 * 4096], 0xCAFEBABE
        jne fail

        ; PROT_NONE denies access without discarding the page
        mov eax, SYS_MPROTECT
        mov ebx, [map1]
        add ebx, 8 * 4096
        mov ecx, 4 * 4096
        xor edx, edx                    ; PROT_NONE
        int 0x80
        test eax, eax
        jnz fail

        mov eax, [map1]
        add eax, 8 * 4096
        mov [probe_addr], eax
        call check_crash_read
        test eax, eax
        jz fail

        mov eax, SYS_MPROTECT
        mov ebx, [map1]
        add ebx, 8 * 4096
        mov ecx, 4 * 4096
        mov edx, PROT_READ | PROT_WRITE
        int 0x80
        test eax, eax
        jnz fail
        mov edi, [map1]
        cmp dword [edi + 8 * 4096], 0x12345678
        jne fail                        ; PROT_NONE preserved the data

        ; ---------------------------------------------------------------
        ; 4. fork: an mmap page is shared copy-on-write, not writable-shared
        ; ---------------------------------------------------------------
        mov eax, SYS_FORK
        int 0x80
        cmp eax, -1
        je fail
        test eax, eax
        jz .iso_child
        mov ebx, eax
        mov eax, SYS_WAITPID
        int 0x80
        test eax, eax
        jnz fail                        ; child must exit 0
        mov edi, [map1]
        cmp dword [edi + 9 * 4096], 4096 * 10
        jne fail                        ; child's write must stay private
        jmp .reclaim

.iso_child:
        mov edi, [map1]
        mov dword [edi + 9 * 4096], 0xDEADBEEF
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

        ; ---------------------------------------------------------------
        ; 5. A child that mmaps and dies must give its frames back
        ; ---------------------------------------------------------------
.reclaim:
        mov eax, SYS_MEMINFO
        int 0x80
        mov [free_before], eax

        mov eax, SYS_FORK
        int 0x80
        cmp eax, -1
        je fail
        test eax, eax
        jz .reclaim_child
        mov ebx, eax
        mov eax, SYS_WAITPID
        int 0x80
        test eax, eax
        jnz fail

        mov eax, SYS_MEMINFO
        int 0x80
        mov ecx, [free_before]
        sub ecx, eax
        cmp ecx, 8                      ; page tables cloned by fork may leak
        ja fail
        jmp .done

.reclaim_child:
        mov eax, SYS_MMAP
        mov ebx, [map_hint]
        add ebx, 0x800000            ; two page tables up: still this machine's zone
        mov ecx, 32 * 4096
        mov edx, PROT_READ | PROT_WRITE
        int 0x80
        cmp eax, -1
        je .reclaim_child_fail
        mov edi, eax
        mov ecx, 32
.fill_child:
        mov dword [edi], 0x5A5A5A5A
        add edi, 4096
        dec ecx
        jnz .fill_child
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80
.reclaim_child_fail:
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

        ; ---------------------------------------------------------------
        ; 6. The kernel touching a demand-zero syscall buffer must fault the
        ;    user page in with user access, not identity-map over it.
        ; ---------------------------------------------------------------
        mov eax, SYS_MMAP
        mov ebx, [map_hint]
        add ebx, 0x1000000
        mov ecx, 4096
        mov edx, PROT_READ | PROT_WRITE
        int 0x80
        cmp eax, -1
        je fail
        mov [kern_buf], eax

        mov eax, SYS_GETARGS
        mov ebx, [kern_buf]
        int 0x80
        cmp eax, 0
        jl fail

        mov edi, [kern_buf]
        mov dword [edi], 0x12345678
        cmp dword [edi], 0x12345678
        jne fail                      ; still a user-accessible page

.done:
        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

;-----------------------------------------------------------------------
; verify_pages - compare PAGES' worth of fill values
;   In : EDI = base, ESI = first page index, ECX = count
;   Out: CF set on mismatch
; Page N holds 4096*(N+1), which is what the fill loops write.
;-----------------------------------------------------------------------
verify_pages:
        push eax
        push edx
        push ecx
        mov edx, esi
        shl edx, 12
        add edx, edi
        mov eax, esi
        inc eax
        shl eax, 12
.vp_loop:
        cmp ecx, 0
        je .vp_ok
        cmp dword [edx], eax
        jne .vp_bad
        add edx, 4096
        add eax, 4096
        dec ecx
        jmp .vp_loop
.vp_ok:
        clc
        jmp .vp_out
.vp_bad:
        stc
.vp_out:
        pop ecx
        pop edx
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

fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

msg_ok:       db "VM2_OK", 10, 0
msg_fail:     db "VMTEST2_FAIL", 10, 0
msg_no_fault: db "VMTEST2_FAIL_NO_FAULT", 10, 0

; Buffers live in the loaded image and in the default section: a `section`
; directive here would restart the address counter at zero, so every buffer
; would alias the program's own code instead of following it.
map1:        dd 0
map_hint:    dd 0
probe_addr:  dd 0
free_base:   dd 0
free_before: dd 0
kern_buf:    dd 0
