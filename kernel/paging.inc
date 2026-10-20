;=======================================================================
; paging.inc - Virtual Memory / Paging
;
; Sets up identity-mapped page tables for the first 128MB of physical
; memory, then enables the CR0 paging bit.  With identity mapping,
; virtual addresses == physical addresses so all existing code continues
; to work unchanged — but the MMU is now active, providing the
; foundation for per-process address spaces, COW fork, and guard pages.
;
; Memory consumed:
;   Page directory:  4 KB  (at PAGE_DIR_ADDR)
;   32 page tables:  128 KB total (at PAGE_TABLE_BASE)
;   Each page table maps 4 MB (1024 entries × 4 KB each).
;   32 tables × 4 MB = 128 MB identity-mapped.
;   96 demand tables: 384 KB (at DEMAND_TABLE_BASE) for the 128-512MB zone.
;
; A page-fault ISR (INT 14) is installed that prints the faulting
; address (CR2) and error code, then recovers to the shell.
;=======================================================================

; Addresses for the paging structures.  These MUST NOT overlap the program
; window (PROGRAM_BASE..PROGRAM_END_ADDR = 0x300000-0x400000): an executable
; of up to PROGRAM_MAX_SIZE (1 MB) is loaded there, so a 512 KB+ binary would
; silently overwrite the page directory and every page table.  They sit above
; the program window and below PMM_BITMAP, inside the kernel-reserved region.
;   page directory   0x410000 .. 0x411000  (4 KB, 4 KB-aligned for CR3)
;   32 identity PTs  0x411000 .. 0x431000  (128 KB)
;   96 demand PTs    0x431000 .. 0x491000  (384 KB)
PAGE_DIR_ADDR    equ 0x00410000     ; Page directory (4 KB)
PAGE_TABLE_BASE  equ 0x00411000     ; First page table (128 KB for 32 tables)
PAGING_MAP_MB    equ 128            ; How many MB to identity-map
PAGING_MAP_TABLES equ PAGING_MAP_MB / 4  ; 32 page tables

; v9.0 Demand-paging extension:
;   Virtual 128MB–512MB (PDEs 32–127) backed by empty page tables.
;   Physical pages are allocated on first access.
DEMAND_MAP_TABLES equ 96           ; Extra page tables: covers 96×4MB = 384MB
DEMAND_TABLE_BASE equ PAGE_TABLE_BASE + PAGING_MAP_TABLES * 4096  ; 0x431000
USER_DEMAND_BASE  equ 0x08000000   ; Start of demand-paged zone (128MB)
USER_DEMAND_MAX   equ 0x20000000   ; End of demand-paged zone  (512MB)

; Page entry flags
PG_PRESENT       equ 0x01
PG_WRITABLE      equ 0x02
PG_USER          equ 0x04
PG_WRITE_THROUGH equ 0x08

;---------------------------------------
; paging_new_pd - allocate a page directory for a new address space
;   Out: EAX = page-directory physical address (4 KB aligned), or 0 on OOM
;
; The directory is a copy of the master at PAGE_DIR_ADDR, so a new address
; space initially has exactly the same mappings as the master. That is
; deliberate for the first Phase 1 step: it lets CR3 switching be introduced and
; verified while every task still sees what it saw before. When the user region
; moves to its own high virtual range, only the kernel PDEs will be copied.
;
; The page comes from the PMM, which only hands out memory at or above
; KERNEL_RESERVED_END, and that range is identity-mapped - so the returned
; address is directly usable as a CR3 value.
;---------------------------------------
paging_new_pd:
        pushad
        call pmm_alloc_page
        test eax, eax
        jz   .npd_fail
        mov  edi, eax
        mov  esi, PAGE_DIR_ADDR
        mov  ecx, 1024              ; copy all 1024 PDEs
        rep  movsd
        mov  [esp + 28], eax        ; return value slot in the pushad frame
        popad
        ret
.npd_fail:
        mov  dword [esp + 28], 0
        popad
        ret

;---------------------------------------
; paging_free_pd - release a page directory
;   In : EAX = page-directory address (0 is ignored)
;---------------------------------------
paging_free_pd:
        test eax, eax
        jz   .fpd_done
        call pmm_free_page
.fpd_done:
        ret

;---------------------------------------
; paging_init - Build page tables and
;               enable paging.
;---------------------------------------
paging_init:
        pushad

        ;-----------------------------------
        ; Step 1: Zero the page directory
        ;-----------------------------------
        mov edi, PAGE_DIR_ADDR
        xor eax, eax
        mov ecx, 1024          ; 1024 dword entries
        rep stosd

        ;-----------------------------------
        ; Step 2: Build identity-map page
        ;         tables for 0..128 MB
        ;-----------------------------------
        ; Outer loop: one iteration per 4 MB region
        xor ebx, ebx           ; table index (0..31)
        mov esi, PAGE_TABLE_BASE
.build_tables:
        cmp ebx, PAGING_MAP_TABLES
        jge .fill_dir

        ; Fill 1024 page-table entries for this 4 MB region
        mov edi, esi            ; current page table
        mov eax, ebx
        shl eax, 22             ; base phys address = index × 4 MB
        or  eax, PG_PRESENT | PG_WRITABLE | PG_USER
        mov ecx, 1024
.fill_pt:
        stosd                   ; write PTE
        add eax, 0x1000         ; next 4 KB page
        loop .fill_pt

        ; Advance to next page table
        add esi, 4096
        inc ebx
        jmp .build_tables

.fill_dir:
        ;-----------------------------------
        ; Step 3: Point PD entries at the
        ;         page tables we just built
        ;-----------------------------------
        mov edi, PAGE_DIR_ADDR
        mov esi, PAGE_TABLE_BASE
        xor ecx, ecx
.fill_pde:
        cmp ecx, PAGING_MAP_TABLES
        jge .demand_init

        mov eax, esi
        or  eax, PG_PRESENT | PG_WRITABLE | PG_USER
        mov [edi], eax

        add edi, 4
        add esi, 4096
        inc ecx
        jmp .fill_pde

.demand_init:
        ;-----------------------------------
        ; Step 3b: Build demand-paged page
        ;          tables for 128MB–512MB
        ;-----------------------------------
        ; EDI still points to PDE 32 (128MB)
        ; Allocate DEMAND_MAP_TABLES zeroed page tables
        mov esi, DEMAND_TABLE_BASE
        xor ecx, ecx
.demand_pt_loop:
        cmp ecx, DEMAND_MAP_TABLES
        jge .enable

        ; Zero this page table (all PTEs not-present)
        push ecx
        push edi
        mov  edi, esi
        xor  eax, eax
        mov  ecx, 1024
        rep  stosd
        pop  edi
        pop  ecx

        ; Install PDE pointing to this empty page table
        mov  eax, esi
        or   eax, PG_PRESENT | PG_WRITABLE | PG_USER
        mov  [edi], eax

        add  edi, 4
        add  esi, 4096
        inc  ecx
        jmp  .demand_pt_loop

.enable:
        ;-----------------------------------
        ; Step 4: Install page-fault handler
        ;-----------------------------------
        mov ecx, 14             ; INT 14 = Page Fault
        mov eax, isr_page_fault
        call idt_set_gate

        ;-----------------------------------
        ; Step 5: Load CR3 and enable paging
        ;-----------------------------------
        mov eax, PAGE_DIR_ADDR
        mov cr3, eax

        mov eax, cr0
        or  eax, 0x80000000     ; Set PG bit (bit 31)
        mov cr0, eax

        ; A JMP flushes the prefetch queue (good practice)
        jmp .paging_on
.paging_on:
        ; Install read-only sigreturn trampoline page
        call sigreturn_trampoline_init

        popad
        ret

;---------------------------------------
; isr_page_fault - INT 14 handler
;
; Stack on entry (with error code):
;   [ESP+0]  error code
;   [ESP+4]  EIP
;   [ESP+8]  CS
;   [ESP+12] EFLAGS
;---------------------------------------
isr_page_fault:
        ; Save error code and faulting EIP
        push eax
        mov eax, [esp + 4]      ; error code (at ESP+4 after our push)
        mov [pf_errcode], eax
        mov eax, [esp + 8]      ; faulting EIP (at ESP+8)
        mov [pf_eip], eax
        ; Read faulting address from CR2
        mov eax, cr2
        mov [pf_addr], eax
        pop eax

        ;-----------------------------------
        ; v9.0 Demand-paging fast path:
        ;   User-mode, not-present fault in
        ;   the demand-paged zone → alloc.
        ;-----------------------------------
        push eax
        push ecx
        push edx

        ; --- Copy-on-write: write to a PRESENT page that is read-only because
        ; it is shared with another address space. Supervisor writes count too:
        ; the kernel builds signal frames directly on the receiving task's user
        ; stack, which fork() may have left read-only. cow_break_page refuses
        ; pages with no recorded owners, so a genuine protection fault (the
        ; read-only sigreturn trampoline) still crashes.
        mov  eax, [pf_errcode]
        mov  ecx, eax
        and  ecx, 0x03              ; bit0 present, bit1 write
        cmp  ecx, 0x03
        jne  .pf_not_cow
        call cow_break_page
        test eax, eax
        jz   .pf_not_cow
        pop  edx
        pop  ecx
        pop  eax
        add  esp, 4                 ; drop the error code
        iretd
.pf_not_cow:

        ; Check: user-mode fault (error bit 2)
        mov  eax, [pf_errcode]
        test eax, 0x04
        jz   .pf_kernel_fault

        ; Check: not-present fault (error bit 0 clear)
        test eax, 0x01
        jnz  .pf_kernel_fault

        ; Check: faulting address in demand zone [USER_DEMAND_BASE, USER_DEMAND_MAX)
        mov  eax, [pf_addr]
        cmp  eax, USER_DEMAND_BASE
        jb   .pf_kernel_fault
        cmp  eax, USER_DEMAND_MAX
        jae  .pf_kernel_fault

        ; Allocate a physical page
        call pmm_alloc_page      ; EAX = phys addr or 0
        test eax, eax
        jz   .pf_oom

        ; Map virtual fault address → physical page
        ; paging_map_page(EAX=virt, EBX=phys, ECX=flags)
        mov  ecx, eax            ; save phys
        mov  ebx, eax            ; phys
        mov  eax, [pf_addr]
        and  eax, 0xFFFFF000     ; page-align
        push ecx
        mov  ecx, PG_PRESENT | PG_WRITABLE | PG_USER
        call paging_map_page_here       ; extend the faulting task's directory
        pop  ecx

        ; Success — return to faulting instruction
        pop  edx
        pop  ecx
        pop  eax
        add  esp, 4             ; remove error code
        iretd

.pf_oom:
        ; Out of memory — fall through to crash handler
        pop  edx
        pop  ecx
        pop  eax
        jmp  .pf_show_error

.pf_kernel_fault:
        pop  edx
        pop  ecx
        pop  eax

.pf_show_error:
        pushad

        ; If in GUI or VBE mode, restore text so the error is visible
        cmp byte [gui_desktop_active], 0
        je .pf_check_vbe
        mov byte [gui_desktop_active], 0
        call vbe_restore_text
        jmp .pf_text_ok
.pf_check_vbe:
        cmp byte [vbe_active], 0
        je .pf_text_ok
        call vbe_restore_text
.pf_text_ok:

        mov byte [vga_color], COLOR_ERROR

        mov esi, msg_pf
        call vga_print
        mov esi, msg_pf_addr
        call vga_print
        mov eax, [pf_addr]
        call vga_print_hex
        mov esi, msg_pf_eip
        call vga_print
        mov eax, [pf_eip]
        call vga_print_hex
        mov esi, msg_pf_err
        call vga_print
        mov eax, [pf_errcode]
        call vga_print_hex
        mov al, 0x0A
        call vga_putchar

        ; v3.0: Parse error code bits and print human-readable cause
        ; Bit 0: 0 = page not present, 1 = protection violation
        ; Bit 1: 0 = read access, 1 = write access
        ; Bit 2: 0 = supervisor mode, 1 = user mode
        mov esi, msg_pf_cause
        call vga_print

        test dword [pf_errcode], 0x01
        jnz .pf_present
        mov esi, msg_pf_not_present
        call vga_print
        jmp .pf_check_rw
.pf_present:
        mov esi, msg_pf_protection
        call vga_print

.pf_check_rw:
        test dword [pf_errcode], 0x02
        jnz .pf_write
        mov esi, msg_pf_read
        call vga_print
        jmp .pf_check_mode
.pf_write:
        mov esi, msg_pf_write_acc
        call vga_print

.pf_check_mode:
        test dword [pf_errcode], 0x04
        jnz .pf_user
        mov esi, msg_pf_supervisor
        call vga_print
        jmp .pf_cause_done
.pf_user:
        mov esi, msg_pf_usermode
        call vga_print

.pf_cause_done:
        mov al, 0x0A
        call vga_putchar

        call vga_reset_color
        popad

        ; Remove error code from stack and recover to shell
        add esp, 4              ; pop error code
        mov esp, KERNEL_STACK
        sti
        jmp shell_main

;---------------------------------------
; paging_map_page - Map a single virtual
;                   page to a physical page
; EAX = virtual address (page-aligned)
; EBX = physical address (page-aligned)
; ECX = flags (PG_PRESENT | PG_WRITABLE etc.)
;---------------------------------------
paging_map_page:
        mov dword [paging_map_target_pd], PAGE_DIR_ADDR
        jmp paging_map_body

; paging_map_page_at - map a page in a specific directory
;   In : EDI = page-directory physical address, EAX/EBX/ECX as above
paging_map_page_at:
        mov [paging_map_target_pd], edi

paging_map_body:
        pushad

        ; Compute PD index: virt >> 22
        mov edx, eax
        shr edx, 22
        ; Compute PT index: (virt >> 12) & 0x3FF
        mov esi, eax
        shr esi, 12
        and esi, 0x3FF

        ; Check if PDE exists
        mov edi, [paging_map_target_pd]
        mov eax, [edi + edx*4]
        test eax, PG_PRESENT
        jnz .have_pt

        ; Need to allocate a new page table
        push ebx
        push ecx
        push edx
        push esi
        call pmm_alloc_page     ; EAX = new page table phys addr
        test eax, eax
        jz .map_alloc_fail

        ; Zero the new page table
        push eax
        mov edi, eax
        xor eax, eax
        mov ecx, 1024
        rep stosd
        pop eax

        pop esi
        pop edx
        ; Install PDE
        mov edi, [paging_map_target_pd]
        mov ebx, eax
        or  ebx, PG_PRESENT | PG_WRITABLE | PG_USER
        mov [edi + edx*4], ebx
        mov edi, eax             ; EDI = page table base
        pop ecx
        pop ebx
        jmp .write_pte

.have_pt:
        and eax, 0xFFFFF000     ; mask flags to get PT base address
        mov edi, eax

.write_pte:
        ; Write the PTE: physical address | flags
        mov eax, ebx
        or  eax, ecx
        mov [edi + esi*4], eax

        ; Invalidate TLB entry for this virtual address
        mov eax, [esp + 28]      ; original virtual address from pushad EAX slot
        invlpg [eax]

.map_fail:
        popad
        ret

; Phase 1.3: alloc-failure path. We jumped here with 4 extra dwords still
; on the stack from the PT-allocation prologue (ebx/ecx/edx/esi). Drop them
; before popad so the original caller-saved registers are restored cleanly.
.map_alloc_fail:
        add esp, 16             ; drop pushed esi/edx/ecx/ebx
        popad
        ret

;-----------------------------------------------------------------------
; paging_map_page_here - map a page in the ACTIVE directory
;   In : EAX/EBX/ECX as for paging_map_page
;
; Demand paging must extend the faulting task's directory, not the master:
; mapping only the master leaves the task faulting forever.
;-----------------------------------------------------------------------
paging_map_page_here:
        push eax
        mov eax, cr3
        mov edi, eax
        pop eax
        call paging_map_page_at
        ret

;-----------------------------------------------------------------------
; paging_map_page_all - map a page in the master AND the active directory
;   In : EAX = virt, EBX = phys, ECX = flags
;
; Kernel-global mappings (framebuffer, sigreturn trampoline) must be visible to
; the running task as well as inherited by every future address space. A task
; on its own directory would otherwise miss a mapping added while it runs.
;-----------------------------------------------------------------------
paging_map_page_all:
        pushad
        mov [.pma_virt], eax
        mov [.pma_phys], ebx
        mov [.pma_flags], ecx

        call paging_map_page            ; master (and what future tasks copy)

        mov eax, cr3
        cmp eax, PAGE_DIR_ADDR
        je .pma_done                   ; already the master
        mov edi, eax
        mov eax, [.pma_virt]
        mov ebx, [.pma_phys]
        mov ecx, [.pma_flags]
        call paging_map_page_at

.pma_done:
        popad
        ret
.pma_virt:  dd 0
.pma_phys:  dd 0
.pma_flags: dd 0

paging_map_target_pd: dd PAGE_DIR_ADDR

;-----------------------------------------------------------------------
; paging_current_pd — physical address of the active page directory
;   Out: EAX = CR3
;-----------------------------------------------------------------------
paging_current_pd:
        mov eax, cr3
        ret

;-----------------------------------------------------------------------
; paging_clone_pd — copy an existing address space's page directory
;   In : EAX = source page-directory physical address (0 = use master)
;   Out: EAX = new page directory, or 0 on OOM
;
; fork() must copy the PARENT's directory, not the master: a parent that has
; already exec'd owns private page tables, and a child built from the master
; would run a completely different image.
;-----------------------------------------------------------------------
paging_clone_pd:
        pushad
        test eax, eax
        jnz .pcp_have_src
        mov eax, PAGE_DIR_ADDR
.pcp_have_src:
        mov [.pcp_src], eax
        call pmm_alloc_page
        test eax, eax
        jz .pcp_fail
        push eax
        mov edi, eax
        mov esi, [.pcp_src]
        mov ecx, 1024
        cld
        rep movsd
        pop eax
        mov [esp + 28], eax     ; EAX slot of the pushad frame
        popad
        ret
.pcp_fail:
        mov dword [esp + 28], 0
        popad
        ret
.pcp_src: dd 0

;-----------------------------------------------------------------------
; paging_privatize_pde — give one directory its own page table
;   In : EAX = page-directory physical address, EDX = PDE index (0..1023)
;   Out: EAX = that directory's page-table physical address, or 0 on failure
;
; Task directories start as copies of the master, so their PDEs point at the
; SAME page-table pages. Writing a PTE through one directory would therefore
; change every task's view. Copy the table first; only then is it safe for the
; caller to remap entries belonging to just this address space.
;-----------------------------------------------------------------------
paging_privatize_pde:
        pushad
        cmp edx, 1024
        jae .ppd_fail
        mov edi, eax            ; EDI = directory base
        mov ebx, [edi + edx*4]  ; this directory's PDE
        test ebx, PG_PRESENT
        jz .ppd_fail
        mov esi, ebx
        and esi, 0xFFFFF000     ; page table currently used here
        mov ecx, [PAGE_DIR_ADDR + edx*4]
        and ecx, 0xFFFFF000     ; master's page table for the same region
        cmp esi, ecx
        jne .ppd_private        ; already a private copy

        push edx
        push edi
        call pmm_alloc_page     ; EAX = new page table (registers preserved)
        pop edi
        pop edx
        test eax, eax
        jz .ppd_fail

        push eax                ; new page table
        push edi                ; directory base
        mov edi, eax
        mov ecx, 1024
        cld
        rep movsd               ; ESI = old page table, advances
        pop edi
        pop eax

        and ebx, 0xFFF          ; keep the original flag bits
        or  ebx, eax
        mov [edi + edx*4], ebx

        mov esi, eax            ; return the new table
        ; The active directory just changed: flush the TLB.
        mov ecx, cr3
        mov cr3, ecx

.ppd_private:
        mov [esp + 28], esi
        popad
        ret
.ppd_fail:
        mov dword [esp + 28], 0
        popad
        ret

;-----------------------------------------------------------------------
; paging_copy_window_private — remap a virtual window onto private frames
;   In : EAX = page-directory physical address
;        EBX = window start (page aligned)
;        ECX = page count
;   Out: EAX = 0 on success, -1 on OOM (pages already copied stay private)
;
; Each page currently mapped in the window is copied into a fresh frame and the
; directory is repointed at the copy. The PDE must already be private (call
; paging_privatize_pde first) or the PTE write would leak into every task.
;-----------------------------------------------------------------------
paging_copy_window_private:
        pushad
        mov [.pcw_pd], eax
        mov [.pcw_virt], ebx
        mov [.pcw_left], ecx

.pcw_loop:
        cmp dword [.pcw_left], 0
        je .pcw_ok

        mov eax, [.pcw_virt]
        mov edx, eax
        shr edx, 22             ; PDE index
        mov esi, eax
        shr esi, 12
        and esi, 0x3FF          ; PT index

        mov eax, [.pcw_pd]
        mov eax, [eax + edx*4]
        test eax, PG_PRESENT
        jz .pcw_ok              ; nothing mapped — leave the window alone
        and eax, 0xFFFFF000
        lea eax, [eax + esi*4]
        mov [.pcw_pte], eax

        mov ebx, [eax]          ; old PTE
        test ebx, PG_PRESENT
        jz .pcw_next
        and ebx, 0xFFFFF000
        mov [.pcw_src], ebx

        call pmm_alloc_page
        test eax, eax
        jz .pcw_fail
        mov [.pcw_dst], eax

        ; Copy 4 KB. Both frames are identity-mapped physical memory.
        mov esi, [.pcw_src]
        mov edi, eax
        mov ecx, 1024
        cld
        rep movsd

        ; Keep the original flags, point at the copy. Force WRITABLE: the frame
        ; we are replacing may have been marked read-only for copy-on-write, and
        ; the private copy is the shell's/task's own.
        mov edi, [.pcw_pte]
        mov eax, [edi]
        and eax, 0xFFF
        or  eax, PG_WRITABLE
        or  eax, [.pcw_dst]
        mov [edi], eax

        ; We replaced our own mapping, so drop the reference to the old frame.
        ; A frame still shared with another address space survives, and a frame
        ; the master window still maps is never handed back to the allocator.
        mov eax, [.pcw_src]
        mov ebx, [.pcw_virt]
        call cow_release_frame
.pcw_no_release:

        mov eax, [.pcw_virt]
        invlpg [eax]

.pcw_next:
        add dword [.pcw_virt], 4096
        dec dword [.pcw_left]
        jmp .pcw_loop

.pcw_ok:
        mov dword [esp + 28], 0
        popad
        ret
.pcw_fail:
        mov dword [esp + 28], -1
        popad
        ret

.pcw_pd:   dd 0
.pcw_virt: dd 0
.pcw_left: dd 0
.pcw_pte:  dd 0
.pcw_src:  dd 0
.pcw_dst:  dd 0

;-----------------------------------------------------------------------
; paging_free_window_private — release frames private to a dying directory
;   In : EAX = page-directory physical address
;        EBX = window start (page aligned)
;        ECX = page count
;
; A page is released only when its PTE differs from the master's mapping for the
; same address, so shared identity frames (and the shell's own program image)
; are never freed by mistake. The master PTE is restored afterwards.
;-----------------------------------------------------------------------
paging_free_window_private:
        pushad
        test eax, eax
        jz .pfw_done
        mov [.pfw_pd], eax
        mov [.pfw_virt], ebx
        mov [.pfw_left], ecx

.pfw_loop:
        cmp dword [.pfw_left], 0
        je .pfw_done

        mov eax, [.pfw_virt]
        mov edx, eax
        shr edx, 22
        mov esi, eax
        shr esi, 12
        and esi, 0x3FF

        mov eax, [.pfw_pd]
        mov eax, [eax + edx*4]
        test eax, PG_PRESENT
        jz .pfw_done
        and eax, 0xFFFFF000
        lea eax, [eax + esi*4]
        mov [.pfw_pte], eax

        mov ecx, [PAGE_DIR_ADDR + edx*4]
        and ecx, 0xFFFFF000
        lea ecx, [ecx + esi*4]   ; master PTE address
        mov [.pfw_mpte], ecx

        mov ebx, [eax]           ; this directory's PTE
        mov edx, [ecx]           ; master PTE

        ; Compare FRAMES, not whole PTEs: a copy-on-write share points at the
        ; same frame as the master with different flags, and freeing it would
        ; take the frame away from the address space that still uses it.
        mov esi, ebx
        and esi, 0xFFFFF000
        mov edi, edx
        and edi, 0xFFFFF000
        cmp esi, edi
        je  .pfw_shared

        ; Restore the shared mapping first, then release our frame.
        mov [eax], edx
        mov eax, esi
        test eax, eax
        jz  .pfw_next
        mov ebx, [.pfw_virt]
        call cow_release_frame
        jmp .pfw_next
.pfw_shared:
        ; Same frame as the master — only undo the copy-on-write write barrier.
        mov [eax], edx

.pfw_next:
        add dword [.pfw_virt], 4096
        dec dword [.pfw_left]
        jmp .pfw_loop

.pfw_done:
        popad
        ret

.pfw_pd:   dd 0
.pfw_virt: dd 0
.pfw_left: dd 0
.pfw_pte:  dd 0
.pfw_mpte: dd 0

;-----------------------------------------------------------------------
; paging_isolate_program_window — give the running task a private program area
;   Out: EAX = 0 on success, -1 on failure
;
; execve() loads the new image at PROGRAM_BASE, which every directory maps to
; the same physical frames. Without this step an exec inside a forked child
; would overwrite the parent's code while the parent is still running (fork
; without copy-on-write shares the frame, not just the page table).
;-----------------------------------------------------------------------
paging_isolate_program_window:
        pushad
        call paging_current_pd
        mov edx, PROGRAM_BASE >> 22
        call paging_privatize_pde
        test eax, eax
        jz .piw_fail

        call paging_current_pd
        mov ebx, PROGRAM_BASE
        mov ecx, PROGRAM_MAX_SIZE / 4096
        call paging_copy_window_private
        jmp .piw_done
.piw_fail:
        mov eax, -1
.piw_done:
        mov [esp + 28], eax
        popad
        ret

; BSS data for page fault handler
pf_addr:    dd 0
pf_eip:     dd 0
pf_errcode: dd 0

;=======================================================================
; COPY-ON-WRITE FORK
;
; fork() shares the parent's user frames instead of copying them. Both
; address spaces are marked read-only for the shared range; the first write in
; either one takes a #PF, copies the frame into a private page and drops the
; old reference. cow_ref (data.inc) holds one byte per frame of
; identity-mapped RAM; 0 means "not shared — the single owner may free it".
;
; The page tables must be private to each address space before their PTEs are
; modified (paging_privatize_pde), or a write would leak into every task: task
; directories start as copies of the master.
;=======================================================================

;-----------------------------------------------------------------------
; cow_get — reference count of a frame
;   In : EAX = physical address   Out: EAX = count (0 = not shared)
;   Clobbers: EAX
;-----------------------------------------------------------------------
cow_get:
        shr eax, 12
        cmp eax, COW_MAX_PAGES
        jae .cg_zero
        movzx eax, byte [cow_ref + eax]
        ret
.cg_zero:
        xor eax, eax
        ret

;-----------------------------------------------------------------------
; cow_share — account one additional owner
;   In : EAX = physical address
;   Clobbers: EAX, EDX   (a first share turns 0 into 2: owner + sharer)
;-----------------------------------------------------------------------
cow_share:
        shr eax, 12
        cmp eax, COW_MAX_PAGES
        jae .cs_done
        mov dl, [cow_ref + eax]
        cmp dl, 255
        jae .cs_done                    ; saturated — never wraps to "not shared"
        test dl, dl
        jz .cs_first
        inc dl
        jmp .cs_store
.cs_first:
        mov dl, 2
.cs_store:
        mov [cow_ref + eax], dl
.cs_done:
        ret

;-----------------------------------------------------------------------
; cow_put — drop one reference
;   In : EAX = physical address
;   Out: EAX = 1 if the caller should pmm_free_page it, 0 if still shared
;   Clobbers: EAX, EDX
;-----------------------------------------------------------------------
cow_put:
        shr eax, 12
        cmp eax, COW_MAX_PAGES
        jae .cp_owner
        mov dl, [cow_ref + eax]
        test dl, dl
        jz .cp_owner                    ; untracked frame: we are the only owner
        dec dl
        mov [cow_ref + eax], dl
        test dl, dl
        jnz .cp_shared
.cp_owner:
        mov eax, 1
        ret
.cp_shared:
        xor eax, eax
        ret

;-----------------------------------------------------------------------
; cow_privatize_range — give a directory its own page tables for a range
;   In : EAX = page directory, EBX = start (page aligned), ECX = page count
;   Out: EAX = 0 on success, -1 on OOM
;-----------------------------------------------------------------------
cow_privatize_range:
        pushad
        mov [.cpr_pd], eax
        mov [.cpr_va], ebx
        mov [.cpr_left], ecx
        mov dword [.cpr_last], 0xFFFFFFFF
.cpr_loop:
        cmp dword [.cpr_left], 0
        je .cpr_ok
        mov eax, [.cpr_va]
        shr eax, 22
        cmp eax, [.cpr_last]
        je .cpr_next                    ; same 4 MB region — already done
        mov [.cpr_last], eax
        mov edx, eax
        mov eax, [.cpr_pd]
        call paging_privatize_pde
        test eax, eax
        jz .cpr_fail
.cpr_next:
        add dword [.cpr_va], 4096
        dec dword [.cpr_left]
        jmp .cpr_loop
.cpr_ok:
        mov dword [esp + 28], 0
        popad
        ret
.cpr_fail:
        mov dword [esp + 28], -1
        popad
        ret
.cpr_pd:   dd 0
.cpr_va:   dd 0
.cpr_left: dd 0
.cpr_last: dd 0

;-----------------------------------------------------------------------
; cow_share_range  — clear PG_WRITABLE and count one extra owner per page
; cow_mark_ro_range — clear PG_WRITABLE only (the second directory of a pair)
;   In : EAX = page directory, EBX = start (page aligned), ECX = page count
;
; Call cow_share_range for the first directory and cow_mark_ro_range for the
; other: the owner count is per frame, not per page table.
;-----------------------------------------------------------------------
cow_share_range:
        mov edx, 1
        jmp cow_range_body
cow_mark_ro_range:
        xor edx, edx
cow_range_body:
        pushad
        mov eax, [esp + 28]             ; EAX = page directory (saved by pushad)
        mov [.csr_pd], eax
        mov [.csr_va], ebx
        mov [.csr_left], ecx
        mov [.csr_ref], edx
.csr_loop:
        cmp dword [.csr_left], 0
        je .csr_done
        mov eax, [.csr_va]
        mov edx, eax
        shr edx, 22                     ; PDE index
        mov esi, eax
        shr esi, 12
        and esi, 0x3FF                  ; PT index
        mov eax, [.csr_pd]
        mov eax, [eax + edx*4]
        test eax, PG_PRESENT
        jz .csr_next
        and eax, 0xFFFFF000
        lea eax, [eax + esi*4]          ; PTE address
        mov ebx, [eax]
        test ebx, PG_PRESENT
        jz .csr_next
        test ebx, PG_WRITABLE
        jz .csr_next                    ; already read-only
        mov edx, ebx
        and edx, ~PG_WRITABLE
        mov [eax], edx
        cmp dword [.csr_ref], 0
        je .csr_next
        mov eax, ebx
        and eax, 0xFFFFF000
        call cow_share
.csr_next:
        add dword [.csr_va], 4096
        dec dword [.csr_left]
        jmp .csr_loop
.csr_done:
        ; The active directory may hold stale writable TLB entries.
        mov eax, cr3
        mov cr3, eax
        popad
        ret
.csr_pd:   dd 0
.csr_va:   dd 0
.csr_left: dd 0
.csr_ref:  dd 0

;-----------------------------------------------------------------------
; cow_release_frame — drop one reference and free the frame if it was ours
;   In : EAX = frame address, EBX = virtual address it was mapped at
;
; A frame that the MASTER directory maps at the same virtual address inside the
; program window belongs to the mirror image the shell loaded; a task that
; unmaps it must not return it to the allocator, or the next `exec` that
; rewrites the shared window would scribble over pages now used elsewhere.
; Identity-mapped ranges (stacks, background images) are outside the window,
; so they are always released through their own reference count.
;-----------------------------------------------------------------------
cow_release_frame:
        pushad
        mov [.crf_frame], eax
        cmp ebx, PROGRAM_BASE
        jb  .crf_put
        mov ecx, PROGRAM_BASE + PROGRAM_MAX_SIZE
        cmp ebx, ecx
        jae .crf_put
        ; Compare with what the master maps at this virtual address
        mov edx, ebx
        shr edx, 22
        mov esi, ebx
        shr esi, 12
        and esi, 0x3FF
        mov eax, [PAGE_DIR_ADDR + edx*4]
        test eax, PG_PRESENT
        jz  .crf_put
        and eax, 0xFFFFF000
        lea eax, [eax + esi*4]
        mov eax, [eax]
        test eax, PG_PRESENT
        jz  .crf_put
        and eax, 0xFFFFF000
        cmp eax, [.crf_frame]
        jne .crf_put
        jmp .crf_done                   ; the master still owns this frame
.crf_put:
        mov eax, [.crf_frame]
        call cow_put
        test eax, eax
        jz  .crf_done
        mov eax, [.crf_frame]
        call pmm_free_page
.crf_done:
        popad
        ret
.crf_frame: dd 0

;-----------------------------------------------------------------------
; cow_release_range — release one reference per page of a range
;   In : EAX = start (page aligned), ECX = page count
; A frame that is still shared by another address space is left alone.
;-----------------------------------------------------------------------
cow_release_range:
        pushad
        mov [.crl_va], eax
        mov [.crl_left], ecx
.crl_loop:
        cmp dword [.crl_left], 0
        je .crl_done
        mov eax, [.crl_va]
        mov ebx, eax                    ; identity range: va == frame
        call cow_release_frame
.crl_next:
        add dword [.crl_va], 4096
        dec dword [.crl_left]
        jmp .crl_loop
.crl_done:
        popad
        ret
.crl_va:   dd 0
.crl_left: dd 0

;-----------------------------------------------------------------------
; cow_break_page — handle a write fault on a shared frame
;   In : pf_addr / pf_errcode set by isr_page_fault
;   Out: EAX = 1 handled (retry the instruction), 0 not a COW page
;
; Only frames whose refcount is at least 2 are broken: a read-only page that
; was never shared (the sigreturn trampoline) is a genuine protection fault
; and must keep failing.
;-----------------------------------------------------------------------
cow_break_page:
        pushad
        mov eax, [pf_addr]
        mov edx, eax
        shr edx, 22
        mov esi, eax
        shr esi, 12
        and esi, 0x3FF
        call paging_current_pd
        mov eax, [eax + edx*4]
        test eax, PG_PRESENT
        jz .cbp_fail
        and eax, 0xFFFFF000
        lea eax, [eax + esi*4]
        mov [.cbp_pte], eax
        mov ebx, [eax]
        test ebx, PG_PRESENT
        jz .cbp_fail
        test ebx, PG_WRITABLE
        jnz .cbp_fail                   ; already writable: not a COW break
        mov eax, ebx
        and eax, 0xFFFFF000
        mov [.cbp_old], eax
        call cow_get
        test eax, eax
        jz .cbp_fail                    ; untracked: real protection violation
        cmp eax, 1
        je .cbp_solo

        ; --- Two or more owners: take a private copy ---
        call pmm_alloc_page
        test eax, eax
        jz .cbp_fail                    ; OOM — report it rather than loop
        mov [.cbp_new], eax

        mov esi, [.cbp_old]
        mov edi, eax
        mov ecx, 1024
        cld
        rep movsd                       ; copy the shared frame

        mov edi, [.cbp_pte]
        mov eax, [edi]                  ; keep PG_USER and friends
        and eax, 0xFFF
        or  eax, PG_PRESENT | PG_WRITABLE
        or  eax, [.cbp_new]
        mov [edi], eax

        ; Drop our reference to the shared frame (never frees an image frame the
        ; master directory still maps)
        mov eax, [.cbp_old]
        mov ebx, [pf_addr]
        call cow_release_frame
.cbp_no_free:
        mov eax, [pf_addr]
        invlpg [eax]
        mov dword [.cbp_ok], 1
        jmp .cbp_out

.cbp_solo:
        ; --- Sole remaining owner: the frame is already ours ---
        ; The other side took its copy first, so there is nothing to copy: just
        ; re-enable writing and drop the bookkeeping back to "not shared".
        mov edi, [.cbp_pte]
        mov eax, [edi]
        or  eax, PG_WRITABLE
        mov [edi], eax
        mov eax, [.cbp_old]
        call cow_put                    ; 1 -> 0
        mov eax, [pf_addr]
        invlpg [eax]
        mov dword [.cbp_ok], 1
        jmp .cbp_out

.cbp_fail:
        mov dword [.cbp_ok], 0
.cbp_out:
        mov eax, [.cbp_ok]
        mov [esp + 28], eax
        popad
        ret
.cbp_pte: dd 0
.cbp_old: dd 0
.cbp_new: dd 0
.cbp_ok:  dd 0

;-----------------------------------------------------------------------
; cow_fork_share — mark the parent's user memory copy-on-write for fork
;   In : EAX = child page directory (already cloned from the parent's)
;   Out: EAX = 0 on success, -1 on OOM (no PTE has been touched)
;
; Regions a child inherits, and how each is found:
;   1. the program window [PROGRAM_BASE, +PROGRAM_MAX_SIZE) — the task's image
;      and, for foreground programs, its stack (it runs at the top of the
;      window);
;   2. a background task's separately allocated program image (page number in
;      TCB_EXIT_CODE, bit 31 marks a background task);
;   3. a background task's separately allocated stack.
; Foreground stacks live inside (1), which is why (3) is skipped when the stack
; range falls in the window — sharing a range twice would count the frames
; twice and leak them.
;-----------------------------------------------------------------------
cow_fork_share:
        pushad
        mov [.cfs_child], eax
        mov esi, [fork_parent_tcb]
        mov eax, [esi + TCB_CR3]
        mov [.cfs_parent], eax

        ;--- 1. Program window ---
        mov eax, [.cfs_parent]
        mov ebx, PROGRAM_BASE
        mov ecx, PROGRAM_MAX_SIZE / 4096
        call cow_privatize_range
        test eax, eax
        js .cfs_fail
        mov eax, [.cfs_child]
        mov ebx, PROGRAM_BASE
        mov ecx, PROGRAM_MAX_SIZE / 4096
        call cow_privatize_range
        test eax, eax
        js .cfs_fail

        mov eax, [.cfs_parent]
        mov ebx, PROGRAM_BASE
        mov ecx, PROGRAM_MAX_SIZE / 4096
        call cow_share_range            ; count the share, clear write (parent)
        mov eax, [.cfs_child]
        mov ebx, PROGRAM_BASE
        mov ecx, PROGRAM_MAX_SIZE / 4096
        call cow_mark_ro_range          ; clear write only (child)

        ;--- 2. Background program image, if this task has one ---
        mov esi, [fork_parent_tcb]
        mov eax, [esi + TCB_EXIT_CODE]
        test eax, 0x80000000
        jz .cfs_stack
        and eax, 0x7FFFFFFF
        shl eax, 12
        jz .cfs_stack                   ; malformed — nothing to share
        mov [.cfs_bg], eax

        mov eax, [.cfs_parent]
        mov ebx, [.cfs_bg]
        mov ecx, BG_PROG_PAGES
        call cow_privatize_range
        test eax, eax
        js .cfs_fail
        mov eax, [.cfs_child]
        mov ebx, [.cfs_bg]
        mov ecx, BG_PROG_PAGES
        call cow_privatize_range
        test eax, eax
        js .cfs_fail

        mov eax, [.cfs_parent]
        mov ebx, [.cfs_bg]
        mov ecx, BG_PROG_PAGES
        call cow_share_range
        mov eax, [.cfs_child]
        mov ebx, [.cfs_bg]
        mov ecx, BG_PROG_PAGES
        call cow_mark_ro_range

.cfs_stack:
        ;--- 3. User stack, when it is not part of the program window ---
        ; TCB_USTACK is the stack top minus 4 (the exit trampoline sits there),
        ; so the allocation starts at USTACK + 4 - size.
        mov esi, [fork_parent_tcb]
        mov ebx, [esi + TCB_USTACK]
        test ebx, ebx
        jz .cfs_ok
        add ebx, 4
        sub ebx, BG_STACK_PAGES * 4096  ; first page of the allocation
        cmp ebx, PROGRAM_BASE
        jb  .cfs_stack_do
        mov eax, PROGRAM_BASE + PROGRAM_MAX_SIZE
        cmp ebx, eax
        jb  .cfs_ok                     ; inside the window: already shared

.cfs_stack_do:
        mov eax, [.cfs_parent]
        mov ecx, BG_STACK_PAGES
        call cow_privatize_range
        test eax, eax
        js .cfs_fail
        mov eax, [.cfs_child]
        mov ecx, BG_STACK_PAGES
        call cow_privatize_range
        test eax, eax
        js .cfs_fail

        mov eax, [.cfs_parent]
        mov ecx, BG_STACK_PAGES
        call cow_share_range
        mov eax, [.cfs_child]
        mov ecx, BG_STACK_PAGES
        call cow_mark_ro_range

.cfs_ok:
        mov dword [esp + 28], 0
        popad
        ret
.cfs_fail:
        mov dword [esp + 28], -1
        popad
        ret
.cfs_child:  dd 0
.cfs_parent: dd 0
.cfs_bg:     dd 0



;-----------------------------------------------------------------------
; sigreturn_trampoline_init  — map a read-only sigreturn trampoline page
;
; Allocates one physical page, writes the 8-byte sigreturn stub at its
; start, then maps it read-only (PG_PRESENT|PG_USER, no PG_WRITABLE) at
; SIGRETURN_TRAMPOLINE_VADDR.  Saves the physical address in
; sigreturn_trampoline_phys for future reference.
;
; Must be called after paging is enabled and PMM is available.
; Preserves all registers.
;-----------------------------------------------------------------------
sigreturn_trampoline_init:
        pushad
        call pmm_alloc_page          ; EAX = physical page (identity-mapped)
        test eax, eax
        jz .sti_fail

        ; Save physical address
        mov [sigreturn_trampoline_phys], eax

        ; Write sigreturn stub bytes at the start of the page
        ; (page is identity-mapped, so phys == virt here)
        ;   mov eax, 144      ; 0xB8 0x90 0x00 0x00 0x00
        ;   int 0x80           ; 0xCD 0x80
        ;   nop                ; 0x90
        mov byte  [eax + 0], 0xB8
        mov dword [eax + 1], 144
        mov byte  [eax + 5], 0xCD
        mov byte  [eax + 6], 0x80
        mov byte  [eax + 7], 0x90

        ; Map the page read-only (PG_PRESENT|PG_USER, no PG_WRITABLE)
        ; paging_map_page(EAX=virt, EBX=phys, ECX=flags)
        mov ebx, [sigreturn_trampoline_phys]
        mov eax, SIGRETURN_TRAMPOLINE_VADDR
        mov ecx, PG_PRESENT | PG_USER
        call paging_map_page_all

.sti_fail:
        popad
        ret
