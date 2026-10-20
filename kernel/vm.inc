;=======================================================================
; VIRTUAL MEMORY - per-process mmap areas
;
; Implements the contract in docs/spec/VMM.md for the 32-bit tree:
;   * user mappings live in the demand zone (128..512 MB)
;   * every mapping is recorded in a per-task vm_area table
;   * permissions come from PROT_* at map time and are re-applied by
;     mprotect; PROT_EXEC|PROT_WRITE is refused (W^X)
;   * munmap frees the frames and clears the page-table entries
;
; Physical frames come from the PMM as one contiguous run per mapping, which
; keeps munmap a single pmm_free_pages call.
;=======================================================================

; Protection bits (mirror of programs/syscalls.inc)
PROT_NONE          equ 0
PROT_READ          equ 1
PROT_WRITE         equ 2
PROT_EXEC          equ 4

VM_AREA_SIZE       equ 20          ; start, pages, prot, phys, last-page
VM_AREAS_PER_TASK  equ 32
VM_MAX_PAGES       equ 4096        ; 16 MB per mapping
VM_MAP_JIT         equ 0x40        ; opt-out of W^X (documented, audited)

; vm_area fields
VMA_START          equ 0
VMA_PAGES          equ 4
VMA_PROT           equ 8
VMA_PHYS           equ 12
VMA_END            equ 16

;---------------------------------------
; vm_init - seed the mapping randomiser
;---------------------------------------
vm_init:
        pushad
        mov eax, [tick_count]
        xor eax, 0x9E3779B9
        test eax, eax
        jnz .vi_seeded
        mov eax, 0x12345678
.vi_seeded:
        mov [vm_rng], eax
        popad
        ret

;---------------------------------------
; vm_rand - next pseudo-random 32-bit value
;   Out: EAX = value, clobbers EAX only
;---------------------------------------
vm_rand:
        mov eax, [vm_rng]
        imul eax, eax, 1103515245
        add eax, 12345
        mov [vm_rng], eax
        shr eax, 4
        ret

;---------------------------------------
; vm_area_base - area table for a task
;   In : ECX = task id    Out: EAX = table base
;---------------------------------------
vm_area_base:
        mov eax, ecx
        imul eax, VM_AREAS_PER_TASK * VM_AREA_SIZE
        add eax, vm_areas
        ret

;---------------------------------------
; vm_find_area - area containing an address, for the current task
;   In : EBX = address
;   Out: EAX = record pointer, or 0
;---------------------------------------
vm_find_area:
        push ecx
        push edx
        push esi
        call sched_get_current_task
        test eax, eax
        jz .vfa_none
        mov ecx, [eax + TCB_PID]
        call vm_area_base
        mov esi, eax
        mov ecx, VM_AREAS_PER_TASK
.vfa_loop:
        mov eax, [esi + VMA_START]
        test eax, eax
        jz .vfa_next
        cmp ebx, eax
        jb .vfa_next
        cmp ebx, [esi + VMA_END]
        jb .vfa_found
.vfa_next:
        add esi, VM_AREA_SIZE
        dec ecx
        jnz .vfa_loop
.vfa_none:
        xor eax, eax
        pop esi
        pop edx
        pop ecx
        ret
.vfa_found:
        mov eax, esi
        pop esi
        pop edx
        pop ecx
        ret

;---------------------------------------
; vm_free_slot - unused record for the current task
;   Out: EAX = record pointer, or 0
;---------------------------------------
vm_free_slot:
        push ecx
        push edx
        push esi
        call sched_get_current_task
        test eax, eax
        jz .vfs_none
        mov ecx, [eax + TCB_PID]
        call vm_area_base
        mov esi, eax
        mov ecx, VM_AREAS_PER_TASK
.vfs_loop:
        cmp dword [esi + VMA_START], 0
        je .vfs_found
        add esi, VM_AREA_SIZE
        dec ecx
        jnz .vfs_loop
.vfs_none:
        xor eax, eax
        pop esi
        pop edx
        pop ecx
        ret
.vfs_found:
        mov eax, esi
        pop esi
        pop edx
        pop ecx
        ret

;---------------------------------------
; vm_overlaps - does [EAX, EAX+EBX) hit an existing area?
;   Out: EAX = 1 on overlap, 0 if free. Clobbers EAX/ECX/EDX/ESI.
;---------------------------------------
;---------------------------------------
; vm_overlaps - does [EAX, EAX+EBX) hit an existing area?
;   In : EAX = start, EBX = length
;   Out: EAX = 1 on overlap, 0 if free
;---------------------------------------
vm_overlaps:
        push ebx
        push ecx
        push edx
        push esi
        push edi
        mov [.vo_cstart], eax
        lea edi, [eax + ebx]
        mov [.vo_cend], edi
        call sched_get_current_task
        test eax, eax
        jz .vo_free
        mov ecx, [eax + TCB_PID]
        call vm_area_base
        mov esi, eax
        mov ecx, VM_AREAS_PER_TASK
.vo_loop:
        mov eax, [esi + VMA_START]
        test eax, eax
        jz .vo_next
        cmp [.vo_cend], eax     ; candidate end <= area start -> no overlap
        jbe .vo_next
        mov edx, [esi + VMA_END]
        cmp [.vo_cstart], edx   ; candidate start >= area end -> no overlap
        jae .vo_next
        mov eax, 1
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret
.vo_next:
        add esi, VM_AREA_SIZE
        dec ecx
        jnz .vo_loop
.vo_free:
        xor eax, eax
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret
.vo_cstart: dd 0
.vo_cend:   dd 0

;---------------------------------------
; vm_gap - find a free virtual range for the current task
;   In : ECX = pages    Out: EAX = base address, or 0
;
; Starts from a randomised base in the demand zone, then walks upward past
; whatever is already mapped.
;---------------------------------------
vm_gap:
        pushad
        mov [.vg_pages], ecx
        mov eax, ecx
        shl eax, 12
        mov [.vg_bytes], eax

        ; First candidate: randomised low end of the demand zone
        call sched_get_current_task
        test eax, eax
        jz .vg_fail
        mov eax, [eax + TCB_PID]
        imul eax, VM_AREA_SIZE
        mov eax, [vm_next + eax]
        test eax, eax
        jnz .vg_have_base
        call vm_rand
        and eax, 0x00FFE000     ; up to 16 MB of ASLR offset, page aligned
        add eax, USER_DEMAND_BASE
        mov [.vg_cand], eax
        jmp .vg_try
.vg_have_base:
        mov [.vg_cand], eax

.vg_try:
        mov eax, [.vg_cand]
        mov ebx, [.vg_bytes]
        ; Refuse to cross the top of the demand zone
        mov edx, eax
        add edx, ebx
        cmp edx, USER_DEMAND_MAX
        jae .vg_fail
        call vm_overlaps
        test eax, eax
        jz .vg_ok

        ; Collided: jump past the area that contains the candidate, then retry.
        mov eax, [.vg_cand]
        mov ebx, eax
        call vm_find_area
        test eax, eax
        jz .vg_advance
        mov eax, [eax + VMA_END]
        add eax, 0xFFF
        and eax, ~0xFFF
        mov [.vg_cand], eax
        jmp .vg_try
.vg_advance:
        add dword [.vg_cand], 0x1000
        jmp .vg_try

.vg_ok:
        ; Commit the bump pointer past this mapping (fragmentation is accepted;
        ; freed ranges are reused by the overlap walk below).
        mov eax, [.vg_cand]
        mov [.vg_base], eax
        add eax, [.vg_bytes]
        mov [.vg_end], eax
        call sched_get_current_task
        test eax, eax
        jz .vg_commit
        mov edx, [eax + TCB_PID]
        imul edx, VM_AREA_SIZE
        mov eax, [.vg_end]
        mov [vm_next + edx], eax
.vg_commit:
        mov eax, [.vg_base]
        mov [esp + 28], eax
        popad
        ret

.vg_fail:
        mov dword [esp + 28], 0
        popad
        ret
.vg_pages: dd 0
.vg_base:  dd 0
.vg_end:   dd 0
.vg_bytes: dd 0
.vg_cand:  dd 0

;---------------------------------------
; vm_flags_for_prot - PTE flags for a PROT_* mask
;   In : EDX = prot    Out: ECX = flags
;---------------------------------------
vm_flags_for_prot:
        mov ecx, PG_PRESENT | PG_USER
        test edx, PROT_WRITE
        jz .vfp_no_write
        or ecx, PG_WRITABLE
.vfp_no_write:
        ; PROT_NONE leaves only... nothing: unmapped is the same as not present
        test edx, PROT_READ | PROT_WRITE | PROT_EXEC
        jnz .vfp_done
        xor ecx, ecx
.vfp_done:
        ret

;---------------------------------------
; vm_map_run - map a physical run into the current address space
;   In : EAX = virt, EBX = pages, ECX = phys, EDX = prot
;   Out: EAX = 0 on success, -1 on failure
;---------------------------------------
vm_map_run:
        pushad
        mov [.vmr_virt], eax
        mov [.vmr_pages], ebx
        mov [.vmr_phys], ecx
        mov [.vmr_prot], edx
        mov ebx, edx
        mov edx, ebx
        call vm_flags_for_prot
        mov [.vmr_flags], ecx

.vmr_loop:
        cmp dword [.vmr_pages], 0
        jle .vmr_ok
        ; Make the 4 MB region's page table private before touching it.
        mov eax, [.vmr_virt]
        shr eax, 22
        mov edx, eax            ; PDE index
        mov eax, cr3
        call paging_privatize_pde
        test eax, eax
        jz .vmr_fail

        mov eax, [.vmr_virt]
        mov ebx, [.vmr_phys]
        mov ecx, [.vmr_flags]
        call paging_map_page_here

        add dword [.vmr_virt], 0x1000
        add dword [.vmr_phys], 0x1000
        dec dword [.vmr_pages]
        jmp .vmr_loop
.vmr_ok:
        mov dword [esp + 28], 0
        popad
        ret
.vmr_fail:
        mov dword [esp + 28], -1
        popad
        ret
.vmr_virt:  dd 0
.vmr_pages: dd 0
.vmr_phys:  dd 0
.vmr_prot:  dd 0
.vmr_flags: dd 0

;---------------------------------------
; vm_unmap_run - clear mappings (identity of the frames is the caller's job)
;   In : EAX = virt, EBX = pages
;---------------------------------------
vm_unmap_run:
        pushad
        mov [.vur_virt], eax
        mov [.vur_pages], ebx
.vur_loop:
        cmp dword [.vur_pages], 0
        jle .vur_done
        mov eax, [.vur_virt]
        xor ebx, ebx            ; phys 0
        xor ecx, ecx            ; flags 0 -> PTE becomes non-present
        call paging_map_page_here
        add dword [.vur_virt], 0x1000
        dec dword [.vur_pages]
        jmp .vur_loop
.vur_done:
        popad
        ret
.vur_virt:  dd 0
.vur_pages: dd 0

;---------------------------------------
; sys_mmap (120) - map anonymous memory
;   In : EBX = hint (0 = anywhere), ECX = length, EDX = PROT_*
;   Out: EAX = address, or -1
;---------------------------------------
sys_mmap:
        pushad
        mov [.mm_hint], ebx
        mov [.mm_len], ecx
        mov [.mm_prot], edx

        ; Reject nonsense lengths and bad protection masks.
        cmp ecx, USER_PTR_MAX
        ja .mm_fail
        test ecx, ecx
        jz .mm_fail
        mov eax, edx
        and eax, ~(PROT_READ | PROT_WRITE | PROT_EXEC | VM_MAP_JIT)
        jnz .mm_fail

        ; W^X: writable and executable never co-exist unless the caller asks
        ; for MAP_JIT inside the protection mask (audited by the loader).
        mov eax, edx
        and eax, PROT_WRITE | PROT_EXEC
        cmp eax, PROT_WRITE | PROT_EXEC
        jne .mm_wx_ok
        test edx, VM_MAP_JIT
        jz .mm_fail
.mm_wx_ok:
        and dword [.mm_prot], PROT_READ | PROT_WRITE | PROT_EXEC

        ; Pages needed
        mov eax, [.mm_len]
        add eax, 0xFFF
        jc .mm_fail
        shr eax, 12
        test eax, eax
        jz .mm_fail
        cmp eax, VM_MAX_PAGES
        ja .mm_fail
        mov [.mm_pages], eax
        mov ecx, eax

        ; Choose the virtual range: honour a page-aligned hint inside the
        ; demand zone if it is free, otherwise allocate one.
        cmp dword [.mm_hint], 0
        je .mm_pick
        mov eax, [.mm_hint]
        test eax, 0xFFF
        jnz .mm_fail
        cmp eax, USER_DEMAND_BASE
        jb .mm_fail
        mov edx, eax
        mov ecx, [.mm_pages]
        shl ecx, 12
        add edx, ecx
        cmp edx, USER_DEMAND_MAX
        ja .mm_fail
        mov ebx, ecx
        call vm_overlaps
        test eax, eax
        jnz .mm_fail
        mov eax, [.mm_hint]
        mov [.mm_virt], eax
        jmp .mm_alloc_phys
.mm_pick:
        mov ecx, [.mm_pages]
        call vm_gap
        test eax, eax
        jz .mm_fail
        mov [.mm_virt], eax

.mm_alloc_phys:
        mov ecx, [.mm_pages]
        call pmm_alloc_pages
        test eax, eax
        jz .mm_fail
        mov [.mm_phys], eax

        ; Record the area before mapping so a partial failure can be undone.
        call vm_free_slot
        test eax, eax
        jz .mm_free_phys
        mov [.mm_rec], eax
        mov ecx, [.mm_virt]
        mov [eax + VMA_START], ecx
        mov ecx, [.mm_pages]
        mov [eax + VMA_PAGES], ecx
        mov ecx, [.mm_prot]
        mov [eax + VMA_PROT], ecx
        mov ecx, [.mm_phys]
        mov [eax + VMA_PHYS], ecx
        mov edx, [.mm_virt]
        mov ecx, [.mm_pages]
        shl ecx, 12
        add edx, ecx
        mov [eax + VMA_END], edx

        ; Zero the frames: a fresh mapping must never expose stale data.
        mov edi, [.mm_phys]
        mov ecx, [.mm_pages]
        shl ecx, 10             ; pages * 1024 dwords
        xor eax, eax
        cld
        rep stosd

        ; Map the run into this process
        mov eax, [.mm_virt]
        mov ebx, [.mm_pages]
        mov ecx, [.mm_phys]
        mov edx, [.mm_prot]
        call vm_map_run
        test eax, eax
        js .mm_undo

        mov eax, [.mm_virt]
        mov [esp + 28], eax
        popad
        iretd

.mm_undo:
        mov eax, [.mm_rec]
        mov dword [eax + VMA_START], 0
.mm_free_phys:
        mov eax, [.mm_phys]
        mov ecx, [.mm_pages]
        call pmm_free_pages
.mm_fail:
        mov dword [esp + 28], -1
        popad
        iretd
.mm_hint:  dd 0
.mm_len:   dd 0
.mm_prot:  dd 0
.mm_pages: dd 0
.mm_virt:  dd 0
.mm_phys:  dd 0
.mm_rec:   dd 0

;---------------------------------------
; sys_munmap (121) - release a mapping
;   In : EBX = addr, ECX = length
;   Out: EAX = 0, or -1
;
; The address must be the base of a mapping and the length must cover it
; (partial unmapping is not implemented yet; VMM.md records that).
;---------------------------------------
sys_munmap:
        pushad
        test ebx, ebx
        jz .mu_fail
        test ecx, ecx
        jz .mu_fail
        mov eax, ebx
        call vm_find_area
        test eax, eax
        jz .mu_fail
        cmp ebx, [eax + VMA_START]
        jne .mu_fail
        mov edx, [eax + VMA_END]
        sub edx, [eax + VMA_START]
        ; Accept the exact size or a length that covers the mapping.
        add ecx, 0xFFF
        shr ecx, 12
        mov edx, [eax + VMA_PAGES]
        cmp ecx, edx
        jb .mu_fail

        mov esi, eax            ; record
        ; Unmap the range, then release the frames.
        mov eax, [esi + VMA_START]
        mov ebx, [esi + VMA_PAGES]
        call vm_unmap_run
        mov eax, [esi + VMA_PHYS]
        mov ecx, [esi + VMA_PAGES]
        call pmm_free_pages
        mov dword [esi + VMA_START], 0
        mov dword [esi + VMA_PAGES], 0
        mov dword [esi + VMA_PHYS], 0
        mov dword [esp + 28], 0
        popad
        iretd
.mu_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;---------------------------------------
; sys_mprotect (122) - change protection of a mapping
;   In : EBX = addr, ECX = length, EDX = PROT_*
;   Out: EAX = 0, or -1
;---------------------------------------
sys_mprotect:
        pushad
        mov [.mp_prot], edx
        mov eax, edx
        and eax, ~(PROT_READ | PROT_WRITE | PROT_EXEC)
        jnz .mp_fail
        ; W^X also applies when protection is changed after the fact.
        mov eax, edx
        and eax, PROT_WRITE | PROT_EXEC
        cmp eax, PROT_WRITE | PROT_EXEC
        je .mp_fail
        test ebx, ebx
        jz .mp_fail
        mov eax, ebx
        call vm_find_area
        test eax, eax
        jz .mp_fail
        mov esi, eax
        ; The whole requested range must sit inside one mapping.
        mov eax, ecx
        add eax, 0xFFF
        shr eax, 12
        mov edx, ebx
        shl eax, 12
        add edx, eax
        cmp edx, [esi + VMA_END]
        ja .mp_fail
        mov eax, [esi + VMA_PROT]
        mov eax, [.mp_prot]
        mov [esi + VMA_PROT], eax

        ; Rewrite the PTEs for the affected pages.
        mov eax, ebx
        and eax, ~0xFFF
        mov [.mp_virt], eax
        mov eax, ecx
        add eax, 0xFFF
        shr eax, 12
        mov [.mp_pages], eax
.mp_loop:
        cmp dword [.mp_pages], 0
        jle .mp_ok
        mov edx, [.mp_prot]
        call vm_flags_for_prot
        ; Find the frame backing this page from the area's physical run.
        mov eax, [.mp_virt]
        sub eax, [esi + VMA_START]
        shr eax, 12
        mov ebx, [esi + VMA_PHYS]
        shl eax, 12
        add ebx, eax            ; phys for this page
        mov eax, [.mp_virt]
        call paging_map_page_here
        add dword [.mp_virt], 0x1000
        dec dword [.mp_pages]
        jmp .mp_loop
.mp_ok:
        mov dword [esp + 28], 0
        popad
        iretd
.mp_fail:
        mov dword [esp + 28], -1
        popad
        iretd
.mp_prot:  dd 0
.mp_virt:  dd 0
.mp_pages: dd 0
