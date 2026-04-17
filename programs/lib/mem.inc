;=======================================================================
; mem.inc - Mellivora Memory Management API
;
; Wrappers around system heap allocation (SYS_MALLOC/SYS_FREE) plus
; sub-page pool allocator for small fixed-size objects, arena allocator,
; and memory utilities.
;
; NOTE: SYS_MALLOC allocates in 4KB page granularity.
;       For many small allocations, use the pool allocator.
;
; Functions:
;   mem_alloc         - Allocate pages from heap
;   mem_free          - Free pages back to heap
;   mem_realloc       - Resize allocation (alloc + copy + free)
;   mem_pool_init     - Initialize a fixed-size pool allocator
;   mem_pool_alloc    - Allocate one object from pool
;   mem_pool_free     - Return object to pool
;   mem_pool_reset    - Reset pool (free all objects)
;   mem_arena_init    - Initialize bump allocator arena
;   mem_arena_alloc   - Allocate from arena (bump pointer)
;   mem_arena_reset   - Reset arena (free all at once)
;   mem_avail         - Report rough free memory
;=======================================================================

; Pool header structure offsets
POOL_BASE       equ 0          ; dword: base address of pool memory
POOL_OBJ_SIZE   equ 4          ; dword: size per object
POOL_CAPACITY   equ 8          ; dword: max objects
POOL_FREE_HEAD  equ 12         ; dword: pointer to free list head
POOL_USED       equ 16         ; dword: current allocation count
POOL_HDR_SIZE   equ 20

; Arena header structure offsets
ARENA_BASE      equ 0          ; dword: base address
ARENA_SIZE      equ 4          ; dword: total size in bytes
ARENA_OFFSET    equ 8          ; dword: current offset (next free byte)
ARENA_HDR_SIZE  equ 12

;-----------------------------------------------------------------------
; mem_alloc - Allocate memory pages
; Input:  EAX = size in bytes (will be rounded up to 4KB pages)
; Output: EAX = pointer to allocated memory, or 0 on failure
;-----------------------------------------------------------------------
mem_alloc:
        push rbx
        mov ebx, eax
        mov eax, SYS_MALLOC
        int 0x80
        pop rbx
        ret

;-----------------------------------------------------------------------
; mem_free - Free allocated memory
; Input:  EAX = pointer (from mem_alloc), ECX = size in bytes
; Output: none
;-----------------------------------------------------------------------
mem_free:
        push rax
        push rbx
        push rcx
        mov ebx, eax
        mov eax, SYS_FREE
        int 0x80
        pop rcx
        pop rbx
        pop rax
        ret

;-----------------------------------------------------------------------
; mem_realloc - Resize allocation
; Input:  EAX = old pointer, EBX = old size, ECX = new size
; Output: EAX = new pointer (old memory freed), or 0 on failure
; WARNING: Old pointer is invalid after this call
;-----------------------------------------------------------------------
mem_realloc:
        PUSHALL

        mov esi, eax            ; old ptr
        mov edi, ebx            ; old size
        mov ebp, ecx            ; new size

        ; Allocate new block
        mov ebx, ecx
        mov eax, SYS_MALLOC
        int 0x80
        test eax, eax
        jz .mr_fail

        mov [esp + 28], eax     ; overwrite EAX in PUSHALL frame

        ; Copy min(old_size, new_size) bytes
        push rax
        mov ecx, edi
        cmp ecx, ebp
        jbe .mr_copy
        mov ecx, ebp
.mr_copy:
        push rdi
        mov edi, eax
        rep movsb
        pop rdi

        ; Free old block
        mov ebx, [esp]          ; peek at saved new ptr — not needed
        pop rax                 ; new ptr (discard, already in stack frame)
        mov ebx, esi            ; old ptr
        mov ecx, edi            ; old size
        mov eax, SYS_FREE
        int 0x80
        POPALL
        ret

.mr_fail:
        mov dword [esp + 28], 0
        POPALL
        ret

;-----------------------------------------------------------------------
; mem_pool_init - Initialize a fixed-size object pool
; Input:  EDI = pool header (POOL_HDR_SIZE bytes)
;         EAX = object size (bytes, minimum 4)
;         ECX = number of objects
; Output: EAX = 0 on success, -1 on failure
; Notes:  Allocates pool memory via SYS_MALLOC
;-----------------------------------------------------------------------
mem_pool_init:
        PUSHALL

        ; Enforce minimum object size of 4 (for free list pointer)
        cmp eax, 4
        jge .mpi_ok
        mov eax, 4
.mpi_ok:
        mov [edi + POOL_OBJ_SIZE], eax
        mov [edi + POOL_CAPACITY], ecx
        mov dword [edi + POOL_USED], 0

        ; Calculate total memory needed
        push rdi
        imul ebx, eax, 1       ; obj_size in EBX
        mov ebx, eax
        imul eax, ecx           ; total bytes = obj_size * count
        push rbx

        ; Allocate
        mov ebx, eax
        mov eax, SYS_MALLOC
        int 0x80
        pop rbx                 ; obj_size
        pop rdi                 ; pool header

        test eax, eax
        jz .mpi_fail

        mov [edi + POOL_BASE], eax

        ; Build free list: each object's first 4 bytes point to next
        mov esi, eax            ; current object
        mov ecx, [edi + POOL_CAPACITY]
        dec ecx                 ; link count = capacity - 1

.mpi_link:
        cmp ecx, 0
        jle .mpi_last
        lea edx, [esi + ebx]   ; next object
        mov [esi], edx          ; current->next = next
        mov esi, edx
        dec ecx
        jmp .mpi_link

.mpi_last:
        mov dword [esi], 0     ; last->next = null
        mov eax, [edi + POOL_BASE]
        mov [edi + POOL_FREE_HEAD], eax

        ; Return success
        mov dword [esp + 28], 0 ; EAX = 0 in PUSHALL frame
        POPALL
        ret

.mpi_fail:
        mov dword [esp + 28], -1
        POPALL
        ret

;-----------------------------------------------------------------------
; mem_pool_alloc - Allocate one object from pool
; Input:  EDI = pool header
; Output: EAX = pointer to object, or 0 if pool exhausted
;-----------------------------------------------------------------------
mem_pool_alloc:
        push rbx
        mov eax, [edi + POOL_FREE_HEAD]
        test eax, eax
        jz .mpa_empty

        ; Pop from free list
        mov ebx, [eax]          ; next free
        mov [edi + POOL_FREE_HEAD], ebx
        inc dword [edi + POOL_USED]
        pop rbx
        ret

.mpa_empty:
        xor eax, eax
        pop rbx
        ret

;-----------------------------------------------------------------------
; mem_pool_free - Return object to pool
; Input:  EDI = pool header, EAX = object pointer
; Output: none
;-----------------------------------------------------------------------
mem_pool_free:
        push rbx
        mov ebx, [edi + POOL_FREE_HEAD]
        mov [eax], ebx          ; obj->next = free_head
        mov [edi + POOL_FREE_HEAD], eax
        dec dword [edi + POOL_USED]
        pop rbx
        ret

;-----------------------------------------------------------------------
; mem_pool_reset - Reset pool, freeing all objects
; Input:  EDI = pool header
; Output: none
;-----------------------------------------------------------------------
mem_pool_reset:
        PUSHALL
        mov dword [edi + POOL_USED], 0
        mov esi, [edi + POOL_BASE]
        mov ebx, [edi + POOL_OBJ_SIZE]
        mov ecx, [edi + POOL_CAPACITY]
        dec ecx

        ; Rebuild free list
.mpr_link:
        cmp ecx, 0
        jle .mpr_last
        lea edx, [esi + ebx]
        mov [esi], edx
        mov esi, edx
        dec ecx
        jmp .mpr_link

.mpr_last:
        mov dword [esi], 0
        mov eax, [edi + POOL_BASE]
        mov [edi + POOL_FREE_HEAD], eax
        POPALL
        ret

;-----------------------------------------------------------------------
; mem_arena_init - Initialize bump allocator arena
; Input:  EDI = arena header (ARENA_HDR_SIZE bytes)
;         EAX = arena size in bytes
; Output: EAX = 0 on success, -1 on failure
;-----------------------------------------------------------------------
mem_arena_init:
        PUSHALL
        mov [edi + ARENA_SIZE], eax
        mov dword [edi + ARENA_OFFSET], 0

        ; Allocate arena memory
        mov ebx, eax
        mov eax, SYS_MALLOC
        int 0x80
        test eax, eax
        jz .mai_fail

        mov [edi + ARENA_BASE], eax
        mov dword [esp + 28], 0
        POPALL
        ret

.mai_fail:
        mov dword [esp + 28], -1
        POPALL
        ret

;-----------------------------------------------------------------------
; mem_arena_alloc - Allocate from arena (bump pointer)
; Input:  EDI = arena header, EAX = size in bytes
; Output: EAX = pointer, or 0 if arena exhausted
; Note:   Allocations are 4-byte aligned
;-----------------------------------------------------------------------
mem_arena_alloc:
        push rbx
        push rcx

        ; Round up to 4-byte alignment
        add eax, 3
        and eax, ~3

        ; Check space
        mov ebx, [edi + ARENA_OFFSET]
        mov ecx, ebx
        add ecx, eax
        cmp ecx, [edi + ARENA_SIZE]
        ja .maa_fail

        ; Bump pointer
        mov [edi + ARENA_OFFSET], ecx
        mov eax, [edi + ARENA_BASE]
        add eax, ebx            ; return base + old offset

        pop rcx
        pop rbx
        ret

.maa_fail:
        xor eax, eax
        pop rcx
        pop rbx
        ret

;-----------------------------------------------------------------------
; mem_arena_reset - Reset arena (free everything at once)
; Input:  EDI = arena header
; Output: none
;-----------------------------------------------------------------------
mem_arena_reset:
        mov dword [edi + ARENA_OFFSET], 0
        ret
