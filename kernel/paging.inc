;=======================================================================
; paging.inc - Virtual Memory / Paging (64-bit Long Mode)
;
; Stage2 builds a 4-level page table hierarchy at PML4_BASE (0x70000)
; that identity-maps the first 4 GB using 2 MB pages.  This module
; installs the page-fault handler and provides paging_map_page() to
; add fine-grained 4 KB mappings within the existing hierarchy.
;
; Page table layout (set by stage2):
;   PML4  @ 0x70000  (1 entry  → PDPT)
;   PDPT  @ 0x71000  (4 entries → PD0-PD3)
;   PD0-3 @ 0x72000-0x75000  (512×2 MB pages each = 4 GB)
;
; paging_map_page can split a 2 MB page into a 4 KB page table on
; demand, allocating new page tables from the PMM as needed.
;=======================================================================

; Page entry flags
PG_PRESENT       equ 0x01
PG_WRITABLE      equ 0x02
PG_USER          equ 0x04
PG_WRITE_THROUGH equ 0x08
PG_PAGE_SIZE     equ 0x80       ; PS bit — 2 MB page in PD level

;---------------------------------------
; paging_init - Install page-fault handler.
; Stage2 already built 4-level tables and
; loaded CR3 = PML4_BASE.  We do NOT
; rebuild or overwrite them.
;---------------------------------------
paging_init:
        PUSHALL

        ; Install page-fault handler (INT 14 = Page Fault)
        mov ecx, 14
        mov eax, isr_page_fault
        call idt_set_gate

        POPALL
        ret

;---------------------------------------
; isr_page_fault - INT 14 handler
;
; 64-bit long mode stack on entry (with error code):
;   [RSP+0]  error code  (8 bytes, pushed by CPU)
;   [RSP+8]  RIP         (8 bytes)
;   [RSP+16] CS          (8 bytes)
;   [RSP+24] RFLAGS      (8 bytes)
;   [RSP+32] RSP3        (8 bytes)
;   [RSP+40] SS3         (8 bytes)
;---------------------------------------
isr_page_fault:
        ; Save error code, faulting RIP, and CR2
        push rax
        mov rax, [rsp + 8]      ; error code (at RSP+8 after our push)
        mov [pf_errcode], rax
        mov rax, [rsp + 16]     ; faulting RIP (at RSP+16)
        mov [pf_rip], rax
        ; Read faulting address from CR2
        mov rax, cr2
        mov [pf_addr], rax
        pop rax

        PUSHALL

        ; If we're in GUI mode, restore text mode first so errors are visible
        cmp byte [gui_desktop_active], 0
        je .pf_text_ok
        mov byte [gui_desktop_active], 0
        call vbe_restore_text
.pf_text_ok:

        mov byte [vga_color], COLOR_ERROR

        mov esi, msg_pf
        call vga_print
        mov esi, msg_pf_addr
        call vga_print
        mov eax, [pf_addr]
        call vga_print_hex
        mov esi, msg_pf_rip
        call vga_print
        mov eax, [pf_rip]
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
        POPALL

        ; Remove error code from stack and recover to shell
        add rsp, 8              ; pop 8-byte error code (64-bit)
        mov rsp, KERNEL_STACK
        sti
        jmp shell_main

;---------------------------------------
; paging_map_page - Map a single 4 KB virtual page
;                   in the 4-level page table hierarchy.
;
; EAX = virtual address (page-aligned)
; EBX = physical address (page-aligned)
; ECX = flags (PG_PRESENT | PG_WRITABLE etc.)
;
; Navigates PML4 → PDPT → PD → PT.
; If the PD entry is a 2 MB large page (PS bit), this function
; splits it into a new 4 KB page table first.
; Allocates new page tables from PMM as needed.
;---------------------------------------
paging_map_page:
        PUSHALL

        ; Save original virtual address for TLB invalidation
        mov r8d, eax            ; R8 = original vaddr

        ; ---- Compute indices at each level ----
        ; PML4 index: bits 39-47 (virt >> 39) & 0x1FF
        ; PDPT index: bits 30-38 (virt >> 30) & 0x1FF
        ; PD index:   bits 21-29 (virt >> 21) & 0x1FF
        ; PT index:   bits 12-20 (virt >> 12) & 0x1FF

        ; We only identity-map < 4 GB, so PML4 index = 0 always
        ; PDPT index
        mov edx, eax
        shr edx, 30
        and edx, 0x1FF          ; EDX = PDPT index (0-3 for first 4GB)

        ; PD index
        mov esi, eax
        shr esi, 21
        and esi, 0x1FF          ; ESI = PD index

        ; PT index
        mov edi, eax
        shr edi, 12
        and edi, 0x1FF          ; EDI = PT index

        ; ---- Walk the hierarchy ----
        ; PML4[0] → PDPT (always exists from stage2)
        mov rax, [PML4_BASE]
        and eax, 0xFFFFF000     ; mask flags → PDPT base
        ; PDPT[edx] → PD
        mov rax, [rax + rdx*8]
        test eax, PG_PRESENT
        jz .map_fail            ; PDPT entry missing — shouldn't happen for < 4GB
        and eax, 0xFFFFF000     ; mask flags → PD base
        mov r9d, eax            ; R9 = PD base

        ; ---- Check PD entry ----
        mov rax, [r9 + rsi*8]
        test eax, PG_PRESENT
        jz .need_new_pt         ; PD entry not present — allocate PT

        ; Check if it's a 2 MB large page (PS bit set)
        test eax, PG_PAGE_SIZE
        jnz .split_large_page

        ; PD entry points to a PT — just write the PTE
        and eax, 0xFFFFF000     ; mask flags → PT base
        jmp .write_pte

.split_large_page:
        ; The PD entry is a 2 MB page.  Split it into 512 × 4 KB entries.
        ; Save the 2 MB-aligned physical base from the PD entry.
        mov r10d, eax
        and r10d, 0xFFE00000    ; 2 MB aligned base address

        ; Allocate a new 4 KB page table
        call pmm_alloc_page     ; EAX = new page
        test eax, eax
        jz .map_fail
        mov r11d, eax           ; R11 = new PT address

        ; Fill 512 PTEs mapping the same 2 MB region at 4 KB granularity
        push rdi
        push rcx
        mov edi, r11d
        mov eax, r10d
        or  eax, PG_PRESENT | PG_WRITABLE | PG_USER
        mov ecx, 512
.split_fill:
        mov [rdi], eax
        mov dword [rdi + 4], 0  ; high 32 bits = 0
        add eax, 0x1000         ; next 4 KB page
        add rdi, 8              ; next PTE (8 bytes)
        dec ecx
        jnz .split_fill
        pop rcx
        pop rdi

        ; Install the new PT in the PD entry (replace 2 MB page)
        mov eax, r11d
        or  eax, PG_PRESENT | PG_WRITABLE | PG_USER
        mov [r9 + rsi*8], eax
        mov dword [r9 + rsi*8 + 4], 0  ; high 32 bits
        mov eax, r11d           ; EAX = PT base
        jmp .write_pte

.need_new_pt:
        ; Allocate a new page table, zero it, install in PD
        call pmm_alloc_page
        test eax, eax
        jz .map_fail

        ; Zero the new page table
        push rdi
        push rcx
        push rax
        mov edi, eax
        xor eax, eax
        mov ecx, 512            ; 512 qword entries
        rep stosq
        pop rax
        pop rcx
        pop rdi

        ; Install PD entry
        mov r11d, eax
        or  eax, PG_PRESENT | PG_WRITABLE | PG_USER
        mov [r9 + rsi*8], eax
        mov dword [r9 + rsi*8 + 4], 0
        mov eax, r11d           ; EAX = PT base
        ; fall through to write_pte

.write_pte:
        ; EAX = PT base, EDI = PT index, EBX = phys addr, ECX = flags
        mov edx, ebx
        or  edx, ecx
        mov [rax + rdi*8], edx
        mov dword [rax + rdi*8 + 4], 0  ; high 32 bits

        ; Invalidate TLB entry for this virtual address
        invlpg [r8]

.map_fail:
        POPALL
        ret

; BSS data for page fault handler (64-bit values)
pf_addr:    dq 0
pf_rip:     dq 0
pf_errcode: dq 0
