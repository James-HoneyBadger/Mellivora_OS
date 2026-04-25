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
        jge .enable

        mov eax, esi
        or  eax, PG_PRESENT | PG_WRITABLE | PG_USER
        mov [edi], eax

        add edi, 4
        add esi, 4096
        inc ecx
        jmp .fill_pde

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

        mov byte [vga_color], COLOR_DEFAULT
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
        jz .map_fail

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

; BSS data for page fault handler
pf_addr:    dd 0
pf_eip:     dd 0
pf_errcode: dd 0
