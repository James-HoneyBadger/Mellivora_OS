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
;
; A page-fault ISR (INT 14) is installed that prints the faulting
; address (CR2) and error code, then recovers to the shell.
;=======================================================================

; Addresses for paging structures — placed after the PMM bitmap (0x300000)
; and below the heap (0x400000).  We'll use 0x380000–0x3A0000.
PAGE_DIR_ADDR    equ 0x00380000     ; Page directory (4 KB)
PAGE_TABLE_BASE  equ 0x00381000     ; First page table (128 KB for 32 tables)
PAGING_MAP_MB    equ 128            ; How many MB to identity-map
PAGING_MAP_TABLES equ PAGING_MAP_MB / 4  ; 32 page tables

; v9.0 Demand-paging extension:
;   Virtual 128MB–512MB (PDEs 32–127) backed by empty page tables.
;   Physical pages are allocated on first access.
DEMAND_MAP_TABLES equ 96           ; Extra page tables: covers 96×4MB = 384MB
DEMAND_TABLE_BASE equ PAGE_TABLE_BASE + PAGING_MAP_TABLES * 4096  ; 0x3A1000
USER_DEMAND_BASE  equ 0x08000000   ; Start of demand-paged zone (128MB)
USER_DEMAND_MAX   equ 0x20000000   ; End of demand-paged zone  (512MB)

; Page entry flags
PG_PRESENT       equ 0x01
PG_WRITABLE      equ 0x02
PG_USER          equ 0x04
PG_WRITE_THROUGH equ 0x08

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
        call paging_map_page     ; EAX=virt, EBX=phys, ECX=flags
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
        pushad

        ; Compute PD index: virt >> 22
        mov edx, eax
        shr edx, 22
        ; Compute PT index: (virt >> 12) & 0x3FF
        mov esi, eax
        shr esi, 12
        and esi, 0x3FF

        ; Check if PDE exists
        mov edi, PAGE_DIR_ADDR
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
        mov edi, PAGE_DIR_ADDR
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

; BSS data for page fault handler
pf_addr:    dd 0
pf_eip:     dd 0
pf_errcode: dd 0

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
        call paging_map_page

.sti_fail:
        popad
        ret
