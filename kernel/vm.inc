;=======================================================================
; VIRTUAL MEMORY - per-process mmap areas
;
; Implements the contract in docs/spec/VMM.md for the 32-bit tree:
;   * user mappings live in the demand zone (128..512 MB)
;   * every mapping is recorded in a per-task vm_area table
;   * anonymous mappings are demand-zero: pages are allocated and zeroed on
;     first touch, with the area's protection, by the page-fault handler
;   * permissions come from PROT_* at map time and are re-applied by
;     mprotect; PROT_EXEC|PROT_WRITE is refused (W^X)
;   * munmap/mprotect split areas so any page-aligned sub-range works
;   * frames are released one at a time through their COW reference count,
;     because a demand-zero mapping has no contiguous physical run
;=======================================================================

; PROT_*, VM_AREA_SIZE/VM_AREAS_PER_TASK and the VMA_* field offsets live in
; kernel.asm: paging.inc and sched.inc need them too.
VM_MAX_PAGES       equ 4096        ; 16 MB per mapping
VM_MAP_JIT         equ 0x40        ; opt-out of W^X (documented, audited)

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
; vm_fault_file_page - fill a demand frame from the file backing an area
;   In : EAX = faulting address, ESI = area record, EDI = zeroed frame
;   Out: EAX = 1 when a block was read, 0 when this page has no file data
;        (the frame keeps the zeros it was allocated with)
;
; The block is read straight into the frame: frames are identity-mapped kernel
; memory, so a PIO read can target one directly. A read failure reports "no
; data" rather than raising a fault inside the fault handler.
;---------------------------------------
vm_fault_file_page:
        pushad
        mov [.vff_rec], esi
        mov [.vff_dst], edi
        mov edx, [esi + VMA_SECTOR]
        test edx, edx
        jz .vff_none                    ; anonymous mapping
        mov ecx, eax
        sub ecx, [esi + VMA_START]
        shr ecx, 12                     ; page index inside the area
        cmp ecx, [esi + VMA_BLOCKS]
        jae .vff_none                   ; past the end of the file: zeros
        shl ecx, 3                      ; 8 sectors per block
        mov eax, [esi + VMA_SECTOR]
        dec eax
        add eax, ecx
        mov ecx, HBFS_SECTORS_PER_BLK
        mov edi, [.vff_dst]
        call blk_cache_read
        jc .vff_none
        mov dword [esp + 28], 1
        popad
        ret
.vff_none:
        mov dword [esp + 28], 0
        popad
        ret
.vff_rec: dd 0
.vff_dst: dd 0

;---------------------------------------
; vm_pte_phys - frame mapped at a virtual address
;   In : EAX = page directory, EBX = virtual address
;   Out: EAX = physical frame, or 0 when the page is not present
;---------------------------------------
vm_pte_phys:
        push ebx
        push ecx
        push edx
        mov edx, ebx
        shr edx, 22
        mov ecx, [eax + edx*4]
        test ecx, PG_PRESENT
        jz .vpp_none
        and ecx, 0xFFFFF000
        mov edx, ebx
        shr edx, 12
        and edx, 0x3FF
        mov ecx, [ecx + edx*4]
        test ecx, PG_PRESENT
        jz .vpp_none
        mov eax, ecx
        and eax, 0xFFFFF000
        jmp .vpp_done
.vpp_none:
        xor eax, eax
.vpp_done:
        pop edx
        pop ecx
        pop ebx
        ret

;---------------------------------------
; vm_clear_pte - drop a mapping from a directory
;   In : EAX = page directory, EBX = virtual address
;---------------------------------------
vm_clear_pte:
        push eax
        push ecx
        push edx
        mov edx, ebx
        shr edx, 22
        mov ecx, [eax + edx*4]
        test ecx, PG_PRESENT
        jz .vcp_done
        and ecx, 0xFFFFF000
        mov edx, ebx
        shr edx, 12
        and edx, 0x3FF
        mov dword [ecx + edx*4], 0
        mov edx, cr3
        cmp edx, [esp + 8]      ; the pushed EAX = directory being edited
        jne .vcp_done
        invlpg [ebx]
.vcp_done:
        pop edx
        pop ecx
        pop eax
        ret

;---------------------------------------
; vm_release_area_frames - unmap pages and drop their frame references
;   In : EAX = page directory, EBX = start, ECX = pages
;
; The frame is read from the page table, not from a stored physical run: a
; demand-zero mapping has no single run (pages are allocated as they are
; touched), and a forked page may be a copy-on-write share whose reference
; count must be dropped rather than freed.
;---------------------------------------
vm_release_area_frames:
        pushad
        mov [.raf_pd], eax
        mov [.raf_va], ebx
        mov [.raf_left], ecx
.raf_loop:
        cmp dword [.raf_left], 0
        jle .raf_done
        mov eax, [.raf_pd]
        mov ebx, [.raf_va]
        call vm_pte_phys
        test eax, eax
        jz .raf_next
        mov [.raf_phys], eax
        mov eax, [.raf_pd]
        mov ebx, [.raf_va]
        call vm_clear_pte
        mov eax, [.raf_phys]
        mov ebx, [.raf_va]
        call cow_release_frame
.raf_next:
        add dword [.raf_va], 0x1000
        dec dword [.raf_left]
        jmp .raf_loop
.raf_done:
        popad
        ret
.raf_pd:   dd 0
.raf_va:   dd 0
.raf_left: dd 0
.raf_phys: dd 0

;---------------------------------------
; vm_clear_table - forget every mapping of a task slot
;   In : ECX = task id
;---------------------------------------
vm_clear_table:
        pushad
        mov eax, ecx
        imul eax, VM_AREAS_PER_TASK * VM_AREA_SIZE
        add eax, vm_areas
        mov edi, eax
        xor eax, eax
        mov ecx, (VM_AREAS_PER_TASK * VM_AREA_SIZE) / 4
        cld
        rep stosd
        mov ecx, [esp + 24]     ; original ECX from the pushad frame
        mov dword [vm_next + ecx*4], 0
        popad
        ret

;---------------------------------------
; vm_split_at - split the area containing an address
;   In : EBX = address, page aligned, strictly inside an area
;   Out: EAX = area that now starts at EBX (new or existing), or 0
;---------------------------------------
vm_split_at:
        push ecx
        push edx
        push esi
        push edi
        mov eax, ebx
        call vm_find_area
        test eax, eax
        jz .vsa_none
        mov esi, eax
        cmp ebx, [esi + VMA_START]
        je .vsa_existing
        cmp ebx, [esi + VMA_END]
        jae .vsa_none
        call vm_free_slot
        test eax, eax
        jz .vsa_none
        mov edi, eax

        ; New right-hand piece: [EBX, old end). A file mapping keeps its file
        ; position: the split point advances the sector by the pages moved out
        ; and shortens the number of blocks the file can still back.
        mov ecx, [esi + VMA_END]
        sub ecx, ebx
        shr ecx, 12
        mov [edi + VMA_START], ebx
        mov [edi + VMA_PAGES], ecx
        mov ecx, [esi + VMA_PROT]
        mov [edi + VMA_PROT], ecx

        mov ecx, ebx
        sub ecx, [esi + VMA_START]
        shr ecx, 12                     ; pages moved out of the left piece
        mov eax, [esi + VMA_SECTOR]
        test eax, eax
        jz .vsa_anon
        mov edx, ecx
        shl edx, 3                      ; sectors per block: 8
        add eax, edx                    ; first sector of the new piece
        mov [edi + VMA_SECTOR], eax
        mov edx, [esi + VMA_BLOCKS]
        sub edx, ecx
        jnc .vsa_blocks_ok
        xor edx, edx
.vsa_blocks_ok:
        mov [edi + VMA_BLOCKS], edx
        cmp dword [esi + VMA_BLOCKS], ecx   ; the left piece ends at the split
        jbe .vsa_end
        mov [esi + VMA_BLOCKS], ecx
        jmp .vsa_end
.vsa_anon:
        mov dword [edi + VMA_SECTOR], 0
        mov dword [edi + VMA_BLOCKS], 0
.vsa_end:
        mov eax, [esi + VMA_END]
        mov [edi + VMA_END], eax

        ; Shrink the left-hand piece to [old start, EBX)
        mov ecx, ebx
        sub ecx, [esi + VMA_START]
        shr ecx, 12
        mov [esi + VMA_PAGES], ecx
        mov [esi + VMA_END], ebx
        mov eax, edi
        jmp .vsa_done
.vsa_existing:
        mov eax, esi
        jmp .vsa_done
.vsa_none:
        xor eax, eax
.vsa_done:
        pop edi
        pop esi
        pop edx
        pop ecx
        ret

;---------------------------------------
; vm_isolate - make [start, start+len) exactly one area
;   In : EBX = start, ECX = length (both page aligned)
;   Out: EAX = the area covering exactly that range, or 0
;
; Returns 0 unless the whole range lies inside a single existing area.
; A failed second split can leave a harmless extra area behind; no frame or
; protection has been changed at that point.
;---------------------------------------
vm_isolate:
        push ebx
        push ecx
        push edx
        push esi
        mov eax, ebx
        call vm_find_area
        test eax, eax
        jz .vis_none
        mov esi, eax
        mov edx, ebx
        add edx, ecx
        jc .vis_none
        cmp ebx, [esi + VMA_START]
        jb .vis_none
        cmp edx, [esi + VMA_END]
        ja .vis_none

        call vm_split_at                ; EBX = start
        test eax, eax
        jz .vis_none
        mov esi, eax

        mov eax, ebx
        add eax, ecx                    ; requested end
        cmp eax, [esi + VMA_END]
        jae .vis_have                   ; already ends here
        push ecx
        mov ebx, eax
        call vm_split_at
        pop ecx
        test eax, eax
        jz .vis_none
.vis_have:
        mov eax, esi
        jmp .vis_done
.vis_none:
        xor eax, eax
.vis_done:
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret

;---------------------------------------
; vm_release_all - release every mapping of a task slot
;   In : EAX = page directory, ECX = task id
;---------------------------------------
vm_release_all:
        pushad
        mov [.vra_pd], eax
        mov [.vra_pid], ecx
        call vm_area_base
        mov [.vra_rec], eax
        mov dword [.vra_i], 0
.vra_loop:
        cmp dword [.vra_i], VM_AREAS_PER_TASK
        jge .vra_done
        mov esi, [.vra_rec]
        mov eax, [.vra_i]
        imul eax, VM_AREA_SIZE
        add esi, eax
        mov ebx, [esi + VMA_START]
        test ebx, ebx
        jz .vra_next
        mov eax, [.vra_pd]
        mov ecx, [esi + VMA_PAGES]
        test ecx, ecx
        jz .vra_clear
        call vm_release_area_frames
.vra_clear:
        xor eax, eax
        mov [esi + VMA_START], eax
        mov [esi + VMA_PAGES], eax
        mov [esi + VMA_PROT], eax
        mov [esi + VMA_SECTOR], eax
        mov [esi + VMA_BLOCKS], eax
        mov [esi + VMA_END], eax
.vra_next:
        inc dword [.vra_i]
        jmp .vra_loop
.vra_done:
        mov ecx, [.vra_pid]
        mov dword [vm_next + ecx*4], 0
        popad
        ret
.vra_pd:  dd 0
.vra_pid: dd 0
.vra_rec: dd 0
.vra_i:   dd 0

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
        jmp .mm_record
.mm_pick:
        mov ecx, [.mm_pages]
        call vm_gap
        test eax, eax
        jz .mm_fail
        mov [.mm_virt], eax

.mm_record:
        ; Demand-zero: nothing is allocated now. The page-fault handler
        ; allocates and zeroes each page the first time the task touches it,
        ; using the area's protection, so an untouched mapping costs no RAM.
        call vm_free_slot
        test eax, eax
        jz .mm_fail
        mov ecx, [.mm_virt]
        mov [eax + VMA_START], ecx
        mov ecx, [.mm_pages]
        mov [eax + VMA_PAGES], ecx
        mov ecx, [.mm_prot]
        mov [eax + VMA_PROT], ecx
        mov dword [eax + VMA_SECTOR], 0     ; anonymous: demand-zero frames
        mov dword [eax + VMA_BLOCKS], 0
        mov edx, [.mm_virt]
        mov ecx, [.mm_pages]
        shl ecx, 12
        add edx, ecx
        mov [eax + VMA_END], edx

        mov eax, [.mm_virt]
        mov [esp + 28], eax
        popad
        iretd
.mm_fail:
        mov dword [esp + 28], -1
        popad
        iretd
.mm_hint:  dd 0
.mm_len:   dd 0
.mm_prot:  dd 0
.mm_pages: dd 0
.mm_virt:  dd 0

;---------------------------------------
; sys_mmap_file (185) - map a file's contents into the address space
;   In : EBX = path (user string), ECX = length, EDX = PROT_*, ESI = offset
;   Out: EAX = address, or -1
;
; The mapping is private to the task and read-write pages are a fresh copy:
; the blocks are read into demand frames on first touch and never written back,
; so a private file mapping cannot corrupt the file. Pages past the end of the
; file (a short last page, or a length longer than the file) read as zero
; rather than faulting.
;
; Only HBFS files can be mapped: the record stores the file's data sectors, so
; a path the fd layer cannot open (tmpfs, procfs, devfs) returns -1.
;---------------------------------------
MF_PATH_MAX equ 200

sys_mmap_file:
        pushad
        mov [.mf_len], ecx
        mov [.mf_prot], edx
        mov [.mf_off], esi

        ; Protection mask and W^X: identical rules to SYS_MMAP.
        mov eax, edx
        and eax, ~(PROT_READ | PROT_WRITE | PROT_EXEC | VM_MAP_JIT)
        jnz .mf_fail
        mov eax, edx
        and eax, PROT_WRITE | PROT_EXEC
        cmp eax, PROT_WRITE | PROT_EXEC
        jne .mf_wx_ok
        test edx, VM_MAP_JIT
        jz .mf_fail
.mf_wx_ok:
        and dword [.mf_prot], PROT_READ | PROT_WRITE | PROT_EXEC

        cmp dword [.mf_len], 0
        je .mf_fail
        cmp dword [.mf_len], USER_PTR_MAX
        ja .mf_fail
        test dword [.mf_off], 0xFFF     ; offset must be page aligned
        jnz .mf_fail

        ; Copy the path into kernel memory.
        mov eax, ebx
        mov ecx, MF_PATH_MAX
        call user_range_ok
        jc .mf_fail
        mov esi, ebx
        mov edi, vm_file_path
        xor ecx, ecx
.mf_copy:
        cmp ecx, MF_PATH_MAX - 1
        jae .mf_fail
        mov al, [esi + ecx]
        mov [edi + ecx], al
        test al, al
        jz .mf_copied
        inc ecx
        jmp .mf_copy
.mf_copied:

        ; Open it: the legacy fd entry carries the file's geometry.
        mov esi, vm_file_path
        mov ecx, FD_FLAG_READ
        call fd_open
        cmp eax, -1
        je .mf_fail
        mov [.mf_fd], eax
        imul eax, FD_ENTRY_SIZE

        mov ecx, [.mf_off]
        shr ecx, 12                     ; first block of the mapping
        mov edx, [fd_table + eax + 16]  ; blocks the file occupies
        cmp ecx, edx
        jae .mf_close_fail              ; offset at or past the end of the file
        sub edx, ecx                    ; blocks still available from here
        mov [.mf_avail], edx

        mov edx, [fd_table + eax + 8]   ; file's first data block
        add edx, ecx
        shl edx, 3                      ; sector LBA
        add edx, HBFS_DATA_START
        inc edx                         ; 0 means "anonymous"
        mov [.mf_sector], edx

        ; Pages needed
        mov eax, [.mf_len]
        add eax, 0xFFF
        jc .mf_close_fail
        shr eax, 12
        test eax, eax
        jz .mf_close_fail
        cmp eax, VM_MAX_PAGES
        ja .mf_close_fail
        mov [.mf_pages], eax

        ; The descriptor is not needed any more: the record keeps the sectors.
        mov ebx, [.mf_fd]
        call fd_close

        mov ecx, [.mf_pages]
        call vm_gap
        test eax, eax
        jz .mf_fail
        mov [.mf_virt], eax

        call vm_free_slot
        test eax, eax
        jz .mf_fail
        mov ecx, [.mf_virt]
        mov [eax + VMA_START], ecx
        mov ecx, [.mf_pages]
        mov [eax + VMA_PAGES], ecx
        mov ecx, [.mf_prot]
        mov [eax + VMA_PROT], ecx
        mov ecx, [.mf_sector]
        mov [eax + VMA_SECTOR], ecx
        mov ecx, [.mf_avail]
        cmp ecx, [.mf_pages]
        jbe .mf_clamp_ok
        mov ecx, [.mf_pages]
.mf_clamp_ok:
        mov [eax + VMA_BLOCKS], ecx
        mov edx, [.mf_virt]
        mov ecx, [.mf_pages]
        shl ecx, 12
        add edx, ecx
        mov [eax + VMA_END], edx

        mov eax, [.mf_virt]
        mov [esp + 28], eax
        popad
        iretd

.mf_close_fail:
        mov ebx, [.mf_fd]
        call fd_close
.mf_fail:
        mov dword [esp + 28], -1
        popad
        iretd
.mf_len:    dd 0
.mf_prot:   dd 0
.mf_off:    dd 0
.mf_fd:     dd 0
.mf_avail:  dd 0
.mf_sector: dd 0
.mf_pages:  dd 0
.mf_virt:   dd 0

;---------------------------------------
; sys_munmap (121) - release part or all of a mapping
;   In : EBX = addr, ECX = length
;   Out: EAX = 0, or -1
;
; The range is page-aligned outwards, must lie inside a single area, and is
; split out of that area before its PTEs are dropped. Frames are released one
; by one through their reference count, so a page shared with a fork parent
; survives until the last owner unmaps it.
;---------------------------------------
sys_munmap:
        pushad
        test ebx, ebx
        jz .mu_fail
        test ecx, ecx
        jz .mu_fail

        mov eax, ebx
        and eax, 0xFFFFF000             ; start, rounded down
        mov edx, ebx
        add edx, ecx                    ; addr + length
        jc .mu_fail
        add edx, 0xFFF
        jc .mu_fail
        and edx, 0xFFFFF000             ; end, rounded up
        mov ecx, edx
        sub ecx, eax                    ; length to unmap
        jz .mu_fail

        mov ebx, eax
        call vm_isolate
        test eax, eax
        jz .mu_fail
        mov esi, eax

        mov eax, cr3
        mov ebx, [esi + VMA_START]
        mov ecx, [esi + VMA_PAGES]
        call vm_release_area_frames

        xor eax, eax
        mov [esi + VMA_START], eax
        mov [esi + VMA_PAGES], eax
        mov [esi + VMA_PROT], eax
        mov [esi + VMA_SECTOR], eax
        mov [esi + VMA_BLOCKS], eax
        mov [esi + VMA_END], eax

        mov dword [esp + 28], 0
        popad
        iretd
.mu_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;---------------------------------------
; sys_mprotect (122) - change protection of part or all of a mapping
;   In : EBX = addr, ECX = length, EDX = PROT_*
;   Out: EAX = 0, or -1
;
; The range must lie inside a single area and is split out of it first, so a
; partial change leaves neighbouring pages with their old protection. PTEs
; that are not present stay absent: the demand fault will map them with the
; area's current protection.
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
        test ecx, ecx
        jz .mp_fail

        mov eax, ebx
        and eax, 0xFFFFF000
        mov edx, ebx
        add edx, ecx
        jc .mp_fail
        add edx, 0xFFF
        jc .mp_fail
        and edx, 0xFFFFF000
        mov ecx, edx
        sub ecx, eax
        jz .mp_fail

        mov ebx, eax
        call vm_isolate
        test eax, eax
        jz .mp_fail
        mov esi, eax
        mov ecx, [.mp_prot]
        mov [esi + VMA_PROT], ecx

        mov eax, [esi + VMA_START]
        mov [.mp_virt], eax
        mov eax, [esi + VMA_PAGES]
        mov [.mp_pages], eax
.mp_loop:
        cmp dword [.mp_pages], 0
        jle .mp_ok
        mov eax, cr3
        mov ebx, [.mp_virt]
        call vm_pte_phys
        test eax, eax
        jz .mp_next                    ; not touched yet: fault uses new prot

        mov [.mp_phys], eax
        mov edx, [.mp_prot]
        call vm_flags_for_prot
        mov [.mp_flags], ecx

        ; PROT_NONE keeps the frame but takes PG_USER away, so the data
        ; survives and ring 3 still faults on every access.
        test dword [.mp_prot], PROT_READ | PROT_WRITE | PROT_EXEC
        jnz .mp_check_shared
        mov dword [.mp_flags], PG_PRESENT
        jmp .mp_map

.mp_check_shared:
        ; A protection change detaches this address space from a fork share:
        ; copy the frame first, so neither mprotect(PROT_READ) here nor a
        ; later mprotect(PROT_WRITE) can change what the other process sees.
        ; A COW-marked frame counts even when its reference count has dropped
        ; to one: the mark is what would let the next write fault break it,
        ; and an explicit read-only protection must outrank that.
        mov eax, [.mp_phys]
        call cow_flag
        test eax, eax
        jnz .mp_detach
        mov eax, [.mp_phys]
        call cow_get
        cmp eax, 1
        jbe .mp_map                    ; not shared

.mp_detach:
        call pmm_alloc_page
        test eax, eax
        jz .mp_fail                    ; out of memory
        mov [.mp_new], eax
        mov esi, [.mp_phys]
        mov edi, eax
        mov ecx, 1024
        cld
        rep movsd
        mov eax, [.mp_phys]
        mov [.mp_old], eax
        mov eax, [.mp_new]
        mov [.mp_phys], eax
        mov eax, [.mp_old]
        mov ebx, [.mp_virt]
        call cow_release_frame

.mp_map:
        mov eax, [.mp_virt]
        mov ebx, [.mp_phys]
        mov ecx, [.mp_flags]
        call paging_map_page_here
.mp_next:
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
.mp_phys:  dd 0
.mp_flags: dd 0
.mp_old:   dd 0
.mp_new:   dd 0
