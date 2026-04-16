;=======================================================================
; PHYSICAL MEMORY MANAGER (Buddy Allocator)
;=======================================================================
; O(1) single-page alloc, O(log n) multi-page, automatic buddy merging.
; Free-list node links stored in the free pages themselves (identity-
; mapped), order tracking at BUDDY_ORDER (128KB array, 1 byte/page).
; Bitmap at PMM_BITMAP retained for pmm_count_free compatibility.
;
; Data structures:
;   PMM_BITMAP      - 128KB bitmap (1 bit per page), 0=free, 1=used
;   BUDDY_ORDER     - 128KB array (1 byte per page), order of free block
;                     or 0xFF if page is allocated / not tracked
;   buddy_free_list - 11 qwords: head pointers for orders 0..10
;                     Each free block stores [next, prev] at its phys addr
;=======================================================================

;-----------------------------------------------------------------------
; pmm_init — Initialise PMM from E820 memory map
;-----------------------------------------------------------------------
pmm_init:
        PUSHALL

        ; Mark all memory as used in bitmap
        mov edi, PMM_BITMAP
        mov ecx, 0x20000 / 4           ; 128KB bitmap
        mov eax, 0xFFFFFFFF
        rep stosd

        ; Clear buddy order array (0xFF = not free)
        mov edi, BUDDY_ORDER
        mov ecx, 0x40000 / 4           ; 256KB (1 byte/page, covers 1GB)
        mov eax, 0xFFFFFFFF
        rep stosd

        ; Clear buddy free list heads
        mov edi, buddy_free_list
        mov ecx, BUDDY_MAX_ORDER + 1
        xor eax, eax
        rep stosq

        ; Parse E820 memory map and free usable regions
        mov ecx, [BOOTINFO_MMAP_CNT]
        mov esi, [BOOTINFO_MMAP_PTR]
        or ecx, ecx
        jz .no_map

.map_loop:
        mov eax, [esi + 16]            ; Type
        cmp eax, 1                     ; Type 1 = usable
        jne .next

        mov eax, [esi]                 ; Base low
        mov ebx, [esi + 8]            ; Length low

        ; Skip memory below 4MB (reserved for kernel)
        cmp eax, 0x00400000
        jge .free_region
        mov edx, 0x00400000
        sub edx, eax
        cmp edx, ebx
        jge .next
        sub ebx, edx
        mov eax, 0x00400000

.free_region:
        call buddy_free_region

.next:
        add esi, 24
        loop .map_loop

.no_map:
        call pmm_count_free
        mov [total_free_pages], eax

        POPALL
        ret

;-----------------------------------------------------------------------
; buddy_free_region — Break a region into buddy-aligned blocks
; EAX = base address (page-aligned), EBX = length in bytes
;-----------------------------------------------------------------------
buddy_free_region:
        PUSHALL

        ; Align base up and length down to page boundaries
        add eax, 0xFFF
        and eax, 0xFFFFF000
        and ebx, 0xFFFFF000
        or ebx, ebx
        jz .bfr_done

.bfr_loop:
        cmp ebx, 0x1000
        jb .bfr_done

        ; Find largest order that fits and is properly aligned
        mov ecx, BUDDY_MAX_ORDER
.bfr_find:
        mov edx, 1
        shl edx, cl                    ; 2^order pages
        shl edx, 12                    ; block size in bytes
        cmp edx, ebx
        ja .bfr_smaller
        ; Alignment check: addr & (block_size - 1) == 0
        mov edi, edx
        dec edi
        test eax, edi
        jz .bfr_add
.bfr_smaller:
        dec ecx
        jns .bfr_find
        jmp .bfr_done                  ; shouldn't reach here

.bfr_add:
        ; ECX = order, EAX = block base, EDX = block size
        push rax
        push rbx
        push rcx
        push rdx

        ; Clear bitmap bits for all pages in this block
        mov ebp, 1
        shl ebp, cl                    ; page count
        push rax
        shr eax, 12                    ; start page number
.bfr_clear:
        push rax
        mov ecx, eax
        shr ecx, 3                     ; byte offset
        and eax, 7                     ; bit offset
        mov dl, 1
        push rcx
        mov cl, al
        shl dl, cl
        pop rcx
        not dl
        and [PMM_BITMAP + ecx], dl     ; clear bit = free
        pop rax
        inc eax
        dec ebp
        jnz .bfr_clear
        pop rax

        ; Set buddy_order for the base page of this block
        pop rdx
        pop rcx
        push rcx
        push rdx
        push rax
        shr eax, 12
        mov [BUDDY_ORDER + eax], cl
        pop rax

        ; Add to free list at order ECX
        call buddy_list_add

        pop rdx
        pop rcx
        pop rbx
        pop rax

        ; Advance past this block
        add eax, edx
        sub ebx, edx
        jmp .bfr_loop

.bfr_done:
        POPALL
        ret

;-----------------------------------------------------------------------
; buddy_list_add — Insert block at head of free list
; EAX = physical address, ECX = order
;-----------------------------------------------------------------------
buddy_list_add:
        PUSHALL
        mov ebx, [buddy_free_list + rcx * 8]
        mov dword [eax], ebx           ; next = old head
        mov dword [eax + 4], 0         ; prev = NULL
        or ebx, ebx
        jz .bla_skip
        mov [ebx + 4], eax             ; old_head->prev = us
.bla_skip:
        mov [buddy_free_list + rcx * 8], eax
        POPALL
        ret

;-----------------------------------------------------------------------
; buddy_list_remove — Remove block from its free list
; EAX = physical address, ECX = order
;-----------------------------------------------------------------------
buddy_list_remove:
        PUSHALL
        mov ebx, [eax]                 ; next
        mov edx, [eax + 4]            ; prev
        or edx, edx
        jz .blr_head
        mov [edx], ebx                ; prev->next = next
        jmp .blr_fix
.blr_head:
        mov [buddy_free_list + rcx * 8], ebx
.blr_fix:
        or ebx, ebx
        jz .blr_done
        mov [ebx + 4], edx            ; next->prev = prev
.blr_done:
        POPALL
        ret

;-----------------------------------------------------------------------
; pmm_alloc_page — Allocate one physical page
; Returns: EAX = physical address (0 = out of memory)
;-----------------------------------------------------------------------
pmm_alloc_page:
        PUSHALL
        xor ecx, ecx                  ; start at order 0

.ap_find:
        cmp ecx, BUDDY_MAX_ORDER
        ja .ap_fail
        mov eax, [buddy_free_list + rcx * 8]
        or eax, eax
        jnz .ap_found
        inc ecx
        jmp .ap_find

.ap_found:
        ; Remove from free list
        call buddy_list_remove

        ; Split down to order 0
.ap_split:
        cmp ecx, 0
        je .ap_done
        dec ecx

        ; Upper half becomes a free buddy at order ECX
        mov edx, 1
        shl edx, cl
        shl edx, 12                   ; half-block size in bytes
        lea rbx, [rax + rdx]          ; upper half address

        ; Set order for upper half
        push rax
        mov eax, ebx
        shr eax, 12
        mov [BUDDY_ORDER + eax], cl
        pop rax

        ; Add upper half to free list
        push rax
        push rcx
        mov eax, ebx
        call buddy_list_add
        pop rcx
        pop rax
        jmp .ap_split

.ap_done:
        ; Mark page used in bitmap
        push rax
        shr eax, 12
        mov byte [BUDDY_ORDER + eax], 0xFF
        mov ecx, eax
        shr ecx, 3
        and eax, 7
        mov edx, 1
        push rcx
        mov cl, al
        shl edx, cl
        pop rcx
        or [PMM_BITMAP + ecx], dl      ; set bit = used
        pop rax

        mov [rsp + FRAME_RAX], rax            ; return via RAX in PUSHALL frame
        POPALL
        ret

.ap_fail:
        POPALL
        xor eax, eax
        ret

;-----------------------------------------------------------------------
; pmm_free_page — Free one physical page, merge with buddy if possible
; EAX = physical address
;-----------------------------------------------------------------------
pmm_free_page:
        PUSHALL

        ; Clear bitmap bit
        push rax
        shr eax, 12
        mov ecx, eax
        shr ecx, 3
        and eax, 7
        mov edx, 1
        push rcx
        mov cl, al
        shl edx, cl
        pop rcx
        not dl
        and [PMM_BITMAP + ecx], dl     ; clear bit = free
        pop rax

        ; Try buddy merge from order 0 upward
        xor ecx, ecx                  ; current order

.fp_merge:
        cmp ecx, BUDDY_MAX_ORDER
        jge .fp_add

        ; Calculate buddy address: addr XOR block_size
        mov edx, 1
        shl edx, cl
        shl edx, 12                   ; block_size in bytes
        mov ebx, eax
        xor ebx, edx                  ; buddy address

        ; Check buddy is within addressable range
        push rax
        mov eax, ebx
        shr eax, 12
        cmp eax, 0x40000              ; 256K page limit (1GB)
        jge .fp_no_merge_pop

        ; Check if buddy is free at the same order
        cmp byte [BUDDY_ORDER + eax], cl
        pop rax
        jne .fp_add

        ; Buddy is free and same order — merge!
        push rax
        push rcx
        mov eax, ebx
        call buddy_list_remove
        pop rcx
        pop rax

        ; Use the lower address as the merged block
        cmp eax, ebx
        jb .fp_lower
        mov eax, ebx
.fp_lower:
        inc ecx
        jmp .fp_merge

.fp_no_merge_pop:
        pop rax

.fp_add:
        ; Set order and add to free list
        push rax
        shr eax, 12
        mov [BUDDY_ORDER + eax], cl
        pop rax
        call buddy_list_add

        POPALL
        ret

;-----------------------------------------------------------------------
; pmm_alloc_pages — Allocate N contiguous pages (power-of-2 rounding)
; ECX = number of pages needed
; Returns: EAX = physical address of first page (0 = failure)
;-----------------------------------------------------------------------
pmm_alloc_pages:
        PUSHALL
        or ecx, ecx
        jz .aps_fail

        ; Find order = ceil(log2(ecx))
        mov ebx, ecx
        xor edx, edx                  ; order
        mov eax, 1
.aps_order:
        cmp eax, ebx
        jge .aps_found
        shl eax, 1
        inc edx
        cmp edx, BUDDY_MAX_ORDER
        jbe .aps_order
        jmp .aps_fail                  ; request too large

.aps_found:
        mov ecx, edx                  ; required order
        mov ebp, edx                  ; save it

        ; Find a block of this order or higher
.aps_scan:
        cmp ecx, BUDDY_MAX_ORDER
        ja .aps_fail
        mov eax, [buddy_free_list + rcx * 8]
        or eax, eax
        jnz .aps_got
        inc ecx
        jmp .aps_scan

.aps_got:
        call buddy_list_remove

        ; Split down to required order
.aps_split:
        cmp ecx, ebp
        je .aps_mark
        dec ecx
        mov edx, 1
        shl edx, cl
        shl edx, 12                   ; half-block bytes
        lea rbx, [rax + rdx]

        push rax
        push rcx
        mov eax, ebx
        push rax
        shr eax, 12
        mov [BUDDY_ORDER + eax], cl
        pop rax
        call buddy_list_add
        pop rcx
        pop rax
        jmp .aps_split

.aps_mark:
        ; Mark all pages as used
        push rax
        mov ecx, ebp
        mov ebx, 1
        shl ebx, cl                   ; page count
        mov ecx, ebx
        shr eax, 12
.aps_mark_loop:
        mov byte [BUDDY_ORDER + eax], 0xFF
        push rax
        push rcx
        mov edx, eax
        shr edx, 3
        and eax, 7
        mov ecx, eax
        mov ebx, 1
        shl ebx, cl
        or [PMM_BITMAP + edx], bl      ; set bit = used
        pop rcx
        pop rax
        inc eax
        loop .aps_mark_loop
        pop rax

        mov [rsp + FRAME_RAX], rax
        POPALL
        ret

.aps_fail:
        POPALL
        xor eax, eax
        ret

;-----------------------------------------------------------------------
; pmm_free_region — Legacy wrapper (called during pmm_init)
;-----------------------------------------------------------------------
pmm_free_region:
        jmp buddy_free_region

;-----------------------------------------------------------------------
; pmm_count_free — Count free pages via bitmap
; Returns: EAX = number of free pages
;-----------------------------------------------------------------------
pmm_count_free:
        push rcx
        push rdx
        push rsi
        xor eax, eax
        mov esi, PMM_BITMAP
        mov ecx, 0x20000               ; 128KB bitmap

.loop:
        movzx edx, byte [esi]
        not dl
.count:
        test dl, dl
        jz .next
        mov dh, dl
        dec dh
        and dl, dh                     ; clear lowest set bit
        inc eax
        jmp .count

.next:
        inc esi
        loop .loop

        pop rsi
        pop rdx
        pop rcx
        ret
