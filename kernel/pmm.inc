;=======================================================================
; PHYSICAL MEMORY MANAGER — Buddy Allocator (v9.0)
;
; Replaces the v8.x O(N) bitmap scanner with a 12-order buddy system.
; Orders 0–11 map to block sizes 4KB–8MB (2^order pages × 4KB).
;
; Memory layout:
;   PMM_BITMAP     (0x300000, 128KB) — 1 bit per 4KB page; 0=free, 1=used.
;   PMM_BUDDY_TAGS (0x320000,   4KB) — reserved for v9.x coalescing tags.
;
; Free lists (buddy_free_list[12] in data.inc):
;   Per-order singly-linked lists.  Each free block stores the next-block
;   page number in its first four bytes (free pages are unused, so safe).
;   Head = PMM_FREE_LIST_NIL when empty.
;
; Seeding: pmm_free_region uses greedy largest-aligned-block strategy so
;   boot-time memory is placed on the highest-possible-order lists.
;
; Coalescing: v10 adds buddy merging on free (pmm_free_coalesce).
;   When freeing a block, its buddy is looked up on the same-order free
;   list.  If found, the two are merged and the process repeats at the
;   next order, reducing fragmentation over the allocator's lifetime.
;
; Public API (unchanged from v8.x):
;   pmm_init         — build free lists from E820 memory map
;   pmm_alloc_page   — allocate one 4KB page; returns EAX = phys addr
;   pmm_free_page    — free one 4KB page; EAX = phys addr
;   pmm_alloc_pages  — allocate ECX contiguous pages; returns EAX
;   pmm_free_pages   — free ECX pages at EAX (EAX must be page-aligned)
;   pmm_free_region  — (init helper) free raw byte range EAX/EBX
;   pmm_count_free   — walk bitmap, return EAX = free page count
;=======================================================================

BUDDY_ORDERS        equ 12            ; Orders 0..11 (4KB..8MB)
PMM_FREE_LIST_NIL   equ 0xFFFFFFFF    ; Empty free-list sentinel
PMM_BUDDY_TAGS      equ PMM_BITMAP + 0x20000  ; 0x320000 — 4KB reserved

;-----------------------------------------------------------------------
; pmm_init
;-----------------------------------------------------------------------
pmm_init:
        pushad

        ;--- 1. Mark all memory as used ---
        mov edi, PMM_BITMAP
        mov eax, 0xFFFFFFFF
        mov ecx, 0x20000 / 4           ; 128KB bitmap
        rep stosd

        ;--- 2. Initialise all free-list heads to empty ---
        mov edi, buddy_free_list
        mov eax, PMM_FREE_LIST_NIL
        mov ecx, BUDDY_ORDERS
        rep stosd

        ;--- 3. Parse E820 and free usable regions ---
        mov ecx, [BOOTINFO_MMAP_CNT]
        mov esi, [BOOTINFO_MMAP_PTR]
        or  ecx, ecx
        jz  .no_map

.e820_loop:
        mov eax, [esi + 16]             ; E820 entry type (1 = usable)
        cmp eax, 1
        jne .e820_next

        mov eax, [esi]                  ; Base (low 32 bits)
        mov ebx, [esi + 8]             ; Length (low 32 bits)

        ; Trim / skip anything below 4 MB (kernel + driver area)
        cmp eax, 0x00400000
        jge .e820_free
        mov edx, 0x00400000
        sub edx, eax
        cmp edx, ebx
        jge .e820_next                  ; Entire region below 4 MB
        sub ebx, edx
        mov eax, 0x00400000

.e820_free:
        call pmm_free_region            ; EAX = base, EBX = byte length

.e820_next:
        add esi, 24
        loop .e820_loop

.no_map:
        call pmm_count_free
        mov  [total_free_pages], eax

        popad
        ret

;-----------------------------------------------------------------------
; pmm_push_free_block — internal
;   In:  EDX = page number, ECX = order  (ECX < BUDDY_ORDERS)
;   Marks bitmap pages as free and links the block onto free_list[order].
;   All registers preserved.
;-----------------------------------------------------------------------
pmm_push_free_block:
        pushad

        ;--- Clear bitmap bits (0 = free) for pages [EDX .. EDX+(1<<ECX)-1] ---
        mov edi, edx                    ; first page
        mov esi, 1
        shl esi, cl                     ; block_pages = 1 << order

.pfb_clear:
        test esi, esi
        jz   .pfb_cleared
        mov  eax, edi
        shr  eax, 3                     ; byte offset in bitmap
        mov  ebx, edi
        and  ebx, 7                     ; bit offset
        btr  dword [PMM_BITMAP + eax], ebx   ; clear = free
        inc  edi
        dec  esi
        jmp  .pfb_clear

.pfb_cleared:
        ;--- Link block at head of free_list[order] ---
        ; Store current head in first 4 bytes of the free page itself
        mov eax, edx
        shl eax, 12                     ; page → physical address
        mov esi, [buddy_free_list + ecx*4]
        mov [eax], esi                  ; next = old head

        mov [buddy_free_list + ecx*4], edx  ; head = this page

        popad
        ret

;-----------------------------------------------------------------------
; pmm_alloc_buddy_order — allocate a 2^order-page block
;   In:  ECX = requested order (0..BUDDY_ORDERS-1)
;   Out: EAX = physical address of block, or 0 on OOM
;   Clobbers: EAX only (all others preserved).
;-----------------------------------------------------------------------
pmm_alloc_buddy_order:
        push ecx
        push edx
        push ebx
        push esi
        push edi

        mov  edi, ecx                   ; EDI = requested order

        ;--- Find lowest order >= EDI that has a free block ---
        mov  ecx, edi
.abo_search:
        cmp  ecx, BUDDY_ORDERS
        jge  .abo_oom
        cmp  dword [buddy_free_list + ecx*4], PMM_FREE_LIST_NIL
        jne  .abo_found
        inc  ecx
        jmp  .abo_search

.abo_found:
        ;--- Pop block from free_list[ecx] ---
        mov  edx, [buddy_free_list + ecx*4]   ; EDX = page number
        mov  eax, edx
        shl  eax, 12                           ; physical address
        mov  esi, [eax]                        ; next ptr stored in page
        mov  [buddy_free_list + ecx*4], esi    ; update list head

        ;--- Mark pages used in bitmap ---
        push ecx
        push edx
        mov  edi, edx                          ; first page
        mov  esi, 1
        shl  esi, cl                           ; block_pages = 1 << found_order
.abo_mark:
        test esi, esi
        jz   .abo_marked
        mov  eax, edi
        shr  eax, 3
        mov  ebx, edi
        and  ebx, 7
        bts  dword [PMM_BITMAP + eax], ebx    ; set = used
        inc  edi
        dec  esi
        jmp  .abo_mark
.abo_marked:
        pop  edx                               ; EDX = page
        pop  ecx                               ; ECX = found_order

        ;--- Restore EDI = requested order from the initial push frame ---
        ; Entry pushes were: ecx edx ebx esi edi → edi at [esp+0]
        ; After the two inner push/pops above we're back to 5-push frame:
        ;   [esp+0]=edi [esp+4]=esi [esp+8]=ebx [esp+12]=edx [esp+16]=ecx
        mov  edi, [esp + 16]                   ; requested order

        ;--- Split from found order (ECX) down to requested order (EDI) ---
.abo_split:
        cmp  ecx, edi                          ; already at requested order?
        je   .abo_done

        dec  ecx                               ; one order lower

        ; Second half page = EDX + (1 << ecx)
        push ecx
        mov  esi, 1
        shl  esi, cl                           ; 1 << new_order
        pop  ecx
        add  esi, edx                          ; esi = second_half_page

        ; Push second half onto free_list[ecx]
        push ecx
        push edx
        push edi
        mov  edx, esi                          ; page = second half
        call pmm_push_free_block               ; edx=page, ecx=order
        pop  edi
        pop  edx
        pop  ecx

        jmp  .abo_split

.abo_done:
        ; EAX = return value = first page phys addr
        mov  eax, edx
        shl  eax, 12
        jmp  .abo_ret

.abo_oom:
        xor  eax, eax

.abo_ret:
        pop  edi
        pop  esi
        pop  ebx
        pop  edx
        pop  ecx
        ret

;-----------------------------------------------------------------------
; pmm_alloc_page — allocate one 4KB page (order 0)
;   Returns: EAX = physical address (0 = OOM)
;   All other registers preserved.
;-----------------------------------------------------------------------
pmm_alloc_page:
        push ecx
        xor  ecx, ecx               ; order 0
        call pmm_alloc_buddy_order
        pop  ecx
        ret

;-----------------------------------------------------------------------
; pmm_free_page — free one 4KB page
;   In: EAX = physical address (page-aligned)
;   All registers preserved.
;-----------------------------------------------------------------------
pmm_free_page:
        pushad
        shr  eax, 12                ; page number
        mov  edx, eax
        xor  ecx, ecx               ; order 0
        call pmm_free_coalesce      ; merge with buddy if available
        popad
        ret

;-----------------------------------------------------------------------
; pmm_free_coalesce — free one buddy block, merging with its buddy
;                     if it is on the same-order free list.
;   In:  EDX = page number, ECX = order
;   Clears bitmap bits, then repeatedly tries to merge with buddy
;   up to BUDDY_ORDERS-1, finally links the (possibly enlarged) block.
;   All registers preserved (uses pushad/popad).
;-----------------------------------------------------------------------
pmm_free_coalesce:
        pushad

        ;--- Clear bitmap bits for [EDX .. EDX+(1<<ECX)-1] ---
        mov edi, edx
        mov esi, 1
        shl esi, cl                 ; block_pages = 1 << order
.pfc_bm_clear:
        test esi, esi
        jz   .pfc_bm_done
        mov  eax, edi
        shr  eax, 3                 ; byte offset in bitmap
        mov  ebx, edi
        and  ebx, 7                 ; bit offset
        btr  dword [PMM_BITMAP + eax], ebx
        inc  edi
        dec  esi
        jmp  .pfc_bm_clear
.pfc_bm_done:

        ;--- Coalesce loop: try to merge with buddy at each order ---
.pfc_coalesce_loop:
        cmp  ecx, BUDDY_ORDERS - 1  ; at max order?
        jl   .pfc_can_coalesce
        jmp  .pfc_link_done         ; yes — just link
.pfc_can_coalesce:
        ; Compute buddy page = page XOR (1 << order)
        push ecx
        mov  eax, 1
        shl  eax, cl                ; 1 << order
        pop  ecx
        mov  esi, edx
        xor  esi, eax               ; ESI = buddy page number

        ; Scan buddy_free_list[ECX] for ESI
        ; EDI = ptr to the 'next' field of the previous list node
        ;       (initially = address of list head)
        lea  edi, [buddy_free_list + ecx*4]
        mov  ebx, [edi]             ; EBX = first page in list
.pfc_scan:
        cmp  ebx, PMM_FREE_LIST_NIL
        je   .pfc_not_found
        cmp  ebx, esi               ; is this our buddy?
        je   .pfc_found
        ; Advance: EDI = addr of EBX's next field; EBX = EBX.next
        mov  eax, ebx
        shl  eax, 12                ; page → physical address
        mov  edi, eax               ; EDI = &(EBX.next)
        mov  ebx, [edi]             ; EBX = next page
        jmp  .pfc_scan

.pfc_found:
        ; Unlink buddy from free list: [EDI] = buddy.next
        mov  eax, ebx               ; EBX = buddy page
        shl  eax, 12
        mov  eax, [eax]             ; buddy.next
        mov  [edi], eax             ; prev.next = buddy.next
        ; Merged block starts at lower of the two page addresses
        cmp  edx, esi
        jbe  .pfc_keep_page
        mov  edx, esi               ; use buddy's (lower) page
.pfc_keep_page:
        inc  ecx                    ; raise order
        jmp  .pfc_coalesce_loop

.pfc_not_found:
.pfc_link_done:
        ; Link the (possibly merged) block onto buddy_free_list[ECX]
        mov  eax, edx
        shl  eax, 12                ; merged block physical address
        mov  esi, [buddy_free_list + ecx*4]
        mov  [eax], esi             ; merged.next = old head
        mov  [buddy_free_list + ecx*4], edx  ; head = merged page
        popad
        ret

;-----------------------------------------------------------------------
; pmm_alloc_pages — allocate ECX contiguous pages
;   Returns: EAX = physical address of first page (0 = failure)
;   All other registers preserved.
;-----------------------------------------------------------------------
pmm_alloc_pages:
        push ecx
        push edx

        ; Compute minimum order s.t. (1 << order) >= ECX
        xor  edx, edx               ; EDX = order
        push ecx                    ; save page count
        mov  ecx, 1
.ap_order:
        cmp  ecx, [esp]             ; compare (1<<order) with count
        jge  .ap_order_done
        cmp  edx, BUDDY_ORDERS - 1
        jge  .ap_fail_pop           ; request too large
        inc  edx
        shl  ecx, 1
        jmp  .ap_order
.ap_order_done:
        pop  ecx                    ; discard saved count

        mov  ecx, edx               ; order
        call pmm_alloc_buddy_order  ; EAX = phys addr or 0

        pop  edx
        pop  ecx
        ret

.ap_fail_pop:
        pop  ecx                    ; discard saved count
        xor  eax, eax
        pop  edx
        pop  ecx
        ret

;-----------------------------------------------------------------------
; pmm_free_pages — free N contiguous pages
;   In:  EAX = base physical address (page-aligned)
;        ECX = number of pages
;   Computes order from ECX; puts block back onto buddy free list.
;   All registers preserved.
;-----------------------------------------------------------------------
pmm_free_pages:
        push eax
        push ecx
        push edx
        push ebx
        push esi

        ; Compute smallest order N s.t. (1 << N) >= ECX
        xor  ebx, ebx           ; EBX = order
        mov  esi, 1             ; ESI = 1 << order
.fp_order:
        cmp  esi, ecx
        jge  .fp_order_done
        cmp  ebx, BUDDY_ORDERS - 1
        jge  .fp_order_done     ; cap at max order
        inc  ebx
        shl  esi, 1
        jmp  .fp_order
.fp_order_done:
        ; EBX = order, EAX = base phys addr
        shr  eax, 12            ; page number
        mov  edx, eax           ; EDX = page
        mov  ecx, ebx           ; ECX = order
        call pmm_free_coalesce  ; coalesce with buddy if available

        pop  esi
        pop  ebx
        pop  edx
        pop  ecx
        pop  eax
        ret

;-----------------------------------------------------------------------
; pmm_free_region — free a raw byte range into buddy lists (init helper)
;   In:  EAX = base address, EBX = byte length
;   Greedy: uses largest aligned order that fits at each step.
;   All registers preserved.
;-----------------------------------------------------------------------
pmm_free_region:
        pushad

        ; Align base up to page boundary
        add  eax, 0xFFF
        and  eax, 0xFFFFF000
        ; Align length down to page boundary
        and  ebx, 0xFFFFF000
        jz   .pfr_done

.pfr_loop:
        test ebx, ebx
        jz   .pfr_done

        ; Page number
        mov  ecx, eax
        shr  ecx, 12                    ; ECX = page number

        ; Determine max alignment order from trailing zeros of page number
        cmp  ecx, 0
        je   .pfr_use0                  ; page 0 → order 0 (BSF undefined on 0)
        bsf  edx, ecx                   ; EDX = trailing zeros = alignment order
        cmp  edx, BUDDY_ORDERS - 1
        jle  .pfr_align_ok
        mov  edx, BUDDY_ORDERS - 1
.pfr_align_ok:

        ; Cap by available page count
        push eax
        push ecx
        mov  ecx, edx
        mov  eax, 1
        shl  eax, cl                    ; block_pages = 1 << order
        pop  ecx
        shl  eax, 12                    ; block_bytes
.pfr_fit:
        cmp  eax, ebx
        jle  .pfr_fit_ok
        test edx, edx
        jz   .pfr_fit_order0
        dec  edx
        shr  eax, 1
        jmp  .pfr_fit
.pfr_fit_order0:
        mov  edx, 0
        mov  eax, 4096
.pfr_fit_ok:
        pop  eax                        ; restore base addr

        ; Free (page = eax>>12, order = edx)
        push eax
        push ebx
        push edx
        mov  edx, eax
        shr  edx, 12                    ; page number
        mov  ecx, [esp]                 ; order (edx before push)
        ; edx=page, ecx=order — but wait, we just clobbered edx.
        ; Let me redo: save order in ecx before push
        pop  ecx                        ; ecx = order (was edx)
        push ecx                        ; re-save for later
        mov  edx, eax
        shr  edx, 12
        call pmm_push_free_block        ; edx=page, ecx=order
        pop  ecx                        ; ecx = order
        pop  ebx
        pop  eax

        ; Advance by block size
        mov  esi, 1
        shl  esi, cl
        shl  esi, 12                    ; block bytes = (1<<order)*4096
        add  eax, esi
        sub  ebx, esi
        jmp  .pfr_loop

.pfr_use0:
        mov  edx, 0
        jmp  .pfr_align_ok

.pfr_done:
        popad
        ret

;-----------------------------------------------------------------------
; pmm_count_free — walk bitmap, return EAX = number of free pages
;-----------------------------------------------------------------------
pmm_count_free:
        push ecx
        push edx
        push esi
        xor eax, eax
        mov esi, PMM_BITMAP
        mov ecx, 0x20000        ; 128KB bitmap

.loop:
        movzx edx, byte [esi]
        not dl
        ; Count set bits in DL (inverted = free pages)
.count:
        test dl, dl
        jz .next
        mov dh, dl
        dec dh
        and dl, dh              ; Clear lowest set bit
        inc eax
        jmp .count

.next:
        inc esi
        loop .loop

        pop esi
        pop edx
        pop ecx
        ret
