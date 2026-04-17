;
; Mellivora OS - Stage 2 Loader (64-bit Long Mode)
;
; Loaded at 0x7E00 by stage 1 boot sector.
; Runs in 16-bit real mode initially. Responsibilities:
;   1. Detect available memory via BIOS int 0x15 E820
;   2. Enable A20 gate and enter unreal mode (flat FS with 4 GB limit)
;   3. Load kernel from disk (LBA 33+) to 0x100000 (1MB) via bounce buffer
;   4. Set up identity-mapped 4-level page tables (2MB pages, first 4GB)
;   5. Set up GDT for 64-bit long mode
;   6. Enable PAE, Long Mode (IA32_EFER), and paging
;   7. Jump to 64-bit kernel entry point
;
; Target: Core 2 Duo+
;

[BITS 16]
[ORG 0x7E00]

; Magic number so stage 1 can verify us
        dd 'BOS2'

;---------------------------------------
; Stage 2 entry point (stage 1 jumps here, past magic)
; DL = boot drive from BIOS
;---------------------------------------
stage2_entry:
        cli
        xor ax, ax
        mov ds, ax
        mov es, ax
        mov ss, ax
        mov sp, 0x7C00
        sti

        mov [boot_drive], dl

        ; Boot splash - set blue background
        mov ax, 0x0003          ; Set 80x25 text mode (clear screen)
        int 0x10
        ; Write a colored title bar
        push es
        mov ax, 0xB800
        mov es, ax
        xor di, di
        mov cx, 80
        mov ax, 0x1F20          ; White on blue, space
        rep stosw
        ; Write title text
        mov di, 20 * 2          ; Column 20
        mov si, splash_title
        mov ah, 0x1F
.splash:
        lodsb
        cmp al, 0
        je .splash_done
        stosw
        jmp .splash
.splash_done:
        pop es

        mov si, msg_stage2
        call print16

        ;---------------------------------------
        ; Detect memory map via BIOS int 0x15, EAX=0xE820
        ;---------------------------------------
detect_memory:
        mov si, msg_mem
        call print16

        mov di, memory_map      ; ES:DI -> buffer for entries
        xor ebx, ebx            ; Continuation value (0 = start)
        xor bp, bp              ; Entry counter
        mov edx, 0x534D4150     ; 'SMAP'

.e820_loop:
        mov eax, 0xE820
        mov ecx, 24             ; Ask for 24 bytes per entry
        int 0x15
        jc .e820_done           ; Carry set = end or error

        cmp eax, 0x534D4150     ; Verify SMAP signature
        jne .e820_done

        cmp ecx, 20             ; Valid entry?
        jl .e820_skip

        inc bp                  ; Count entries
        add di, 24              ; Advance buffer

.e820_skip:
        test ebx, ebx           ; ebx=0 means last entry
        jnz .e820_loop

.e820_done:
        mov [memory_map_count], bp

        ; Print entry count
        mov ax, bp
        add al, '0'
        call putchar16
        mov si, msg_entries
        call print16

        ;---------------------------------------
        ; Enable A20 gate and enter "unreal mode" so that the FS
        ; segment register keeps a 4 GB limit.  This lets us copy
        ; disk chunks directly to extended memory (1 MB+) while
        ; still calling BIOS INT 13h in real mode.
        ;---------------------------------------
setup_unreal:
        cli

        ; Enable A20 via fast gate (port 0x92)
        in al, 0x92
        or al, 2
        and al, 0xFE
        out 0x92, al

        lgdt [gdt_descriptor]           ; Load 4 GB flat GDT

        mov eax, cr0
        or al, 1
        mov cr0, eax                    ; Enter 16-bit protected mode

        mov bx, 0x10                    ; Selector 0x10 = 4 GB data
        mov fs, bx                      ; FS descriptor cache ← 4 GB limit

        and al, 0xFE
        mov cr0, eax                    ; Return to real mode
        jmp 0x0000:.unreal_done         ; Far jump to reload CS
.unreal_done:
        sti                             ; FS now retains 4 GB limit

        ;---------------------------------------
        ; Load kernel from disk using a 16 KB bounce buffer at
        ; KERNEL_TMPBUF (0x20000).  Each chunk is copied to extended
        ; memory at 1 MB+ via unreal-mode FS writes.
        ;---------------------------------------
load_kernel:
        mov si, msg_load_kern
        call print16

        ; Default: kernel loaded from disk into temp buffer.
        mov dword [kernel_src_lin], KERNEL_TMPBUF_LIN

        ; El Torito ISO path: if BIOS preloaded the kernel (boot-load-size
        ; covers MBR + stage2 + kernel), it sits at PRELOAD_KERNEL_ADDR.
        ; Detect by checking for the kernel's known first 8 bytes.
        cmp dword [PRELOAD_KERNEL_ADDR], 0x0010B866
        jne .load_from_disk
        cmp dword [PRELOAD_KERNEL_ADDR + 4], 0x66D88E66
        jne .load_from_disk
        ; Kernel is preloaded — skip slow disk reads.
        mov dword [kernel_src_lin], PRELOAD_KERNEL_ADDR
        jmp .load_done

.load_from_disk:
        ; Kernel on disk starts at LBA 33 (after boot + stage2)
        mov dword [cur_lba], 33
        mov word [sectors_left], KERNEL_SECTORS
        mov word [load_seg], KERNEL_TMPBUF_SEG  ; Fixed bounce buffer
        mov dword [hi_dest], 0x00100000         ; Destination starts at 1 MB
        mov dword [kernel_src_lin], 0x00100000  ; Will be placed via INT 15h

.load_chunk:
        cmp word [sectors_left], 0
        je .load_done

        ; How many sectors this chunk? Min(sectors_left, 32)
        ; Some BIOS/El Torito paths are unreliable with larger transfers.
        mov ax, [sectors_left]
        cmp ax, 32
        jle .chunk_ok
        mov ax, 32
.chunk_ok:
        mov [chunk_size], ax

        ; Set up DAP for reading into fixed bounce buffer
        mov byte [kern_dap], 16
        mov byte [kern_dap+1], 0
        mov [kern_dap+2], ax            ; sector count
        mov word [kern_dap+4], 0x0000   ; offset always 0
        mov word [kern_dap+6], KERNEL_TMPBUF_SEG  ; Fixed bounce buffer
        mov eax, [cur_lba]
        mov [kern_dap+8], eax
        mov dword [kern_dap+12], 0

        ; Read from disk (prefer LBA, fall back to CHS for legacy BIOS / ISO boot)
        call read_kernel_chunk
        jc .load_fail

        ; Copy chunk from bounce buffer to extended memory via BIOS
        call copy_chunk_to_himem
        jc .load_fail

        ; Advance LBA
        movzx eax, word [chunk_size]
        add [cur_lba], eax

        ; Advance destination in extended memory
        movzx eax, word [chunk_size]
        shl eax, 9              ; * 512 = byte count
        add [hi_dest], eax

        ; Decrease remaining
        mov ax, [chunk_size]
        sub [sectors_left], ax

        ; Print a dot for progress
        mov al, '.'
        call putchar16
        jmp .load_chunk

.load_done:
        mov si, msg_ok
        call print16
        jmp enter_long_mode

.load_fail:
        mov si, msg_load_fail
        call print16
        jmp halt16

        ;---------------------------------------
        ; Enter 64-bit long mode
        ; Step 1: Enter 32-bit protected mode (temp selector 0x38)
        ; Step 2: In PM, set up page tables and enable long mode
        ; Step 3: Far jump to 64-bit code (selector 0x08)
        ;---------------------------------------
enter_long_mode:
        mov si, msg_pmode
        call print16

        cli                             ; No interrupts during switch

        ; Load GDT with 64-bit long mode descriptors
        lgdt [gdt_descriptor]

        ; Set PE bit in CR0 (enter protected mode)
        mov eax, cr0
        or eax, 1
        mov cr0, eax

        ; Far jump to temporary 32-bit code segment (selector 0x38)
        jmp 0x38:pm32_entry

halt16:
        cli
        hlt
        jmp halt16

;---------------------------------------
; Read one kernel chunk, preferring INT 13h extensions but falling back
; to CHS reads when booted from BIOS / El Torito environments that reject AH=42.
;---------------------------------------
read_kernel_chunk:
        mov dl, [boot_drive]
        call read_kernel_chunk_once
        jnc .remember_drive

        mov al, [boot_drive]
        cmp al, 0x80
        je .try_81
        mov dl, 0x80
        call read_kernel_chunk_once
        jnc .remember_drive

.try_81:
        mov al, [boot_drive]
        cmp al, 0x81
        je .try_82
        mov dl, 0x81
        call read_kernel_chunk_once
        jnc .remember_drive

.try_82:
        mov al, [boot_drive]
        cmp al, 0x82
        je .try_floppy
        mov dl, 0x82
        call read_kernel_chunk_once
        jnc .remember_drive

.try_floppy:
        mov al, [boot_drive]
        cmp al, 0x00
        je .try_cdrom
        mov dl, 0x00
        call read_kernel_chunk_once
        jnc .remember_drive

.try_cdrom:
        mov al, [boot_drive]
        cmp al, 0xE0
        je .fail
        mov dl, 0xE0
        call read_kernel_chunk_once
        jc .fail

.remember_drive:
        mov [boot_drive], dl
.ok:
        clc
        ret

.fail:
        stc
        ret

read_kernel_chunk_once:
        xor ax, ax
        int 0x13                ; Reset disk before attempting read

        mov eax, [cur_lba]
        mov [kern_dap + 8], eax
        mov dword [kern_dap + 12], 0

        mov si, kern_dap
        mov ah, 0x42
        int 0x13
        jnc .ok_once

        ; Query BIOS geometry if possible; otherwise use translated defaults.
        mov byte [chs_spt], 63
        mov byte [chs_heads], 16
        mov ah, 0x08
        int 0x13
        jc .chs_ready
        and cl, 0x3F
        jz .chs_ready
        mov [chs_spt], cl
        inc dh
        jz .chs_ready
        mov [chs_heads], dh

.chs_ready:
        mov ax, [load_seg]
        mov es, ax
        mov si, [cur_lba]
        mov di, [chunk_size]

.chs_loop:
        mov ax, si
        xor dx, dx
        div byte [chs_spt]
        mov cl, ah
        inc cl
        xor ah, ah
        div byte [chs_heads]
        mov ch, al
        mov dh, ah
        xor bx, bx
        mov ax, 0x0201
        int 0x13
        jnc .chs_ok
        xor ax, ax
        int 0x13                ; Reset and retry once
        xor bx, bx
        mov ax, 0x0201
        int 0x13
        jc .fail_once
.chs_ok:
        mov ax, es
        add ax, 0x20            ; Advance 512 bytes = 0x20 paragraphs
        mov es, ax
        inc si
        dec di
        jnz .chs_loop

.ok_once:
        clc
        ret

.fail_once:
        stc
        ret

;---------------------------------------
; 16-bit helper: print string
;---------------------------------------
print16:
        lodsb
        or al, al
        jz .done
        mov ah, 0x0E
        mov bx, 0x0007
        int 0x10
        jmp print16
.done:
        ret

;---------------------------------------
; 16-bit helper: print char in AL
;---------------------------------------
putchar16:
        push bx
        mov ah, 0x0E
        mov bx, 0x0007
        int 0x10
        pop bx
        ret

;---------------------------------------
; Copy chunk from bounce buffer (0x20000) to extended memory
; at [hi_dest] via unreal-mode FS (4 GB limit).
; Preserves all registers.
;---------------------------------------
copy_chunk_to_himem:
        pushad

        mov esi, KERNEL_TMPBUF_LIN      ; Source: bounce buffer
        mov edi, [hi_dest]              ; Destination: extended memory
        movzx ecx, word [chunk_size]
        shl ecx, 7                      ; Dword count = sectors * 512 / 4

.copy:
        mov eax, [fs:esi]               ; Read from bounce buf (FS has 4 GB limit)
        mov [fs:edi], eax               ; Write to extended memory
        add esi, 4
        add edi, 4
        dec ecx
        jnz .copy

        popad
        ret

;=======================================================
; 32-BIT PROTECTED MODE CODE (temporary)
; Sets up boot info, copies kernel, builds page tables,
; enables long mode, then jumps to 64-bit code.
; Uses temporary selector 0x38 (32-bit code segment).
;=======================================================
[BITS 32]

pm32_entry:
        ; Set up 32-bit segment registers
        mov ax, 0x10            ; Data segment selector
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax
        mov esp, KERNEL_STACK_TOP ; 64-bit kernel stack top (aligned, above low memory)

        ; Store boot drive and memory map info at known location
        ; The kernel expects these at 0x500 (BIOS-safe area)
        movzx eax, byte [boot_drive]
        mov [0x500], eax                ; Boot drive number
        movzx eax, word [memory_map_count]
        mov [0x504], eax                ; Memory map entry count
        mov dword [0x508], memory_map   ; Pointer to memory map data

        ; Copy kernel to 1MB from selected source buffer.
        ; Source is BIOS-preloaded 0xBE00 (ISO boot with full preload
        ; via boot-load-size).  Skipped when the unreal-mode loader
        ; already placed the kernel at 1 MB.
        cld
        mov esi, [kernel_src_lin]
        cmp esi, 0x00100000
        je .skip_copy
        mov edi, 0x00100000             ; Dest: 1MB
        mov ecx, (KERNEL_SECTORS * 512) / 4  ; Dword count
        rep movsd
.skip_copy:

        ;---------------------------------------
        ; Set up 4-level page tables at 0x70000
        ; Identity-map first 4GB using 2MB pages
        ;   PML4  @ 0x70000  (1 entry  -> PDPT)
        ;   PDPT  @ 0x71000  (4 entries -> PD0-PD3)
        ;   PD0-3 @ 0x72000-0x75000  (512 entries each)
        ;---------------------------------------
        mov edi, 0x70000
        xor eax, eax
        mov ecx, 6 * 1024              ; 6 pages * 4KB / 4 = 6144 dwords
        rep stosd                       ; Zero entire page table area

        ; PML4[0] -> PDPT at 0x71000 (Present + Read/Write + User)
        mov dword [0x70000], 0x71007

        ; PDPT[0..3] -> PD0..PD3
        mov dword [0x71000], 0x72007
        mov dword [0x71008], 0x73007
        mov dword [0x71010], 0x74007
        mov dword [0x71018], 0x75007

        ; Fill 2048 PD entries: each maps a 2MB page
        ; Flags: PS (bit 7) | US (bit 2) | RW (bit 1) | P (bit 0) = 0x87
        mov edi, 0x72000
        xor eax, eax                    ; Physical address starts at 0
        mov ecx, 2048                   ; 4 PDs * 512 entries
.fill_pd:
        mov edx, eax
        or edx, 0x87                    ; Present + RW + User + Page Size (2MB)
        mov [edi], edx                  ; Low 32 bits of PD entry
        mov dword [edi + 4], 0          ; High 32 bits = 0
        add eax, 0x200000              ; Next 2MB
        add edi, 8                      ; Next PD entry (8 bytes)
        dec ecx
        jnz .fill_pd

        ;---------------------------------------
        ; Enable PAE (bit 5), OSFXSR (bit 9), OSXMMEXCPT (bit 10)
        ;---------------------------------------
        mov eax, cr4
        or eax, (1 << 5) | (1 << 9) | (1 << 10)
        mov cr4, eax

        ;---------------------------------------
        ; Load PML4 base into CR3
        ;---------------------------------------
        mov eax, 0x70000
        mov cr3, eax

        ;---------------------------------------
        ; Enable Long Mode via IA32_EFER MSR
        ;---------------------------------------
        mov ecx, 0xC0000080             ; IA32_EFER
        rdmsr
        or eax, (1 << 8)               ; Set LME (Long Mode Enable)
        wrmsr

        ;---------------------------------------
        ; Enable paging — activates long mode
        ;---------------------------------------
        mov eax, cr0
        or eax, (1 << 31)              ; CR0.PG
        mov cr0, eax

        ; Far jump to 64-bit kernel code segment (selector 0x08)
        jmp 0x08:lmode_entry

;=======================================================
; 64-BIT LONG MODE CODE
;=======================================================
[BITS 64]

lmode_entry:
        ; Set up 64-bit data segment registers
        mov ax, 0x10
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax
        mov rsp, KERNEL_STACK_TOP

        ; Jump to 64-bit kernel at 1MB
        mov rax, 0x00100000
        jmp rax

;=======================================================
; DATA (16-bit context)
;=======================================================
[BITS 16]

KERNEL_TMPBUF_SEG equ 0x2000   ; Segment for temp buffer (linear 0x20000)
KERNEL_TMPBUF_OFF equ 0x0000   ; Offset for temp buffer
KERNEL_TMPBUF_LIN equ 0x20000  ; Linear address of temp buffer (seg*16+off)
PRELOAD_KERNEL_ADDR equ 0xBE00 ; Kernel location when preloaded by BIOS
                               ; (0x7C00 + 33 * 512)
KERNEL_STACK_TOP    equ 0x003FF000 ; 4KB-aligned stack top below heap (legacy 0x9FC00)

; KERNEL_SECTORS is generated by the build system in kernel_sectors.inc
; It equals ceil(kernel.bin size / 512), ensuring we always load the
; exact kernel binary without hard-coding a stale value.
%include "kernel_sectors.inc"

boot_drive:     db 0
chs_spt:        db 0
chs_heads:      db 0
cur_lba:        dd 0
load_seg:       dw 0
sectors_left:   dw 0
chunk_size:     dw 0
memory_map_count: dw 0
kernel_src_lin: dd KERNEL_TMPBUF_LIN

kern_dap:       times 16 db 0

hi_dest:        dd 0                    ; Destination address in extended memory

msg_stage2:     db "Stage 2 loader", 0x0D, 0x0A, 0
msg_mem:        db "Memory: ", 0
msg_entries:    db " regions", 0x0D, 0x0A, 0
msg_load_kern:  db "Loading kernel", 0
msg_ok:         db " OK", 0x0D, 0x0A, 0
msg_load_fail:  db "Kernel load fail!", 0
msg_pmode:      db "Entering long mode...", 0x0D, 0x0A, 0
splash_title:   db "Mellivora OS - Booting...", 0

;---------------------------------------
; GDT for 64-bit long mode
;
; Selector layout:
;   0x00 - Null descriptor
;   0x08 - Kernel code (64-bit, DPL=0, L=1 D=0)
;   0x10 - Kernel data (flat 4GB, DPL=0)
;   0x18 - User code   (64-bit, DPL=3, L=1 D=0)
;   0x20 - User data   (flat 4GB, DPL=3)
;   0x28 - TSS low     (16-byte descriptor in long mode)
;   0x30 - TSS high
;   0x38 - 32-bit code (temporary, used only during boot)
;---------------------------------------
gdt_start:
        ; Null descriptor (selector 0x00)
        dq 0

        ; Kernel code segment (selector 0x08)
        ; 64-bit long mode: L=1, D=0, ring 0
        dw 0xFFFF               ; Limit 0:15
        dw 0x0000               ; Base 0:15
        db 0x00                 ; Base 16:23
        db 10011010b            ; Access: P=1, DPL=0, S=1, Type=1010
        db 10101111b            ; G=1, L=1, D=0, Limit 16:19=0xF
        db 0x00                 ; Base 24:31

        ; Kernel data segment (selector 0x10)
        ; Flat 4GB (DB=1 needed for unreal mode bootstrap)
        dw 0xFFFF               ; Limit 0:15
        dw 0x0000               ; Base 0:15
        db 0x00                 ; Base 16:23
        db 10010010b            ; Access: P=1, DPL=0, S=1, Type=0010
        db 11001111b            ; G=1, DB=1, Limit 16:19=0xF
        db 0x00                 ; Base 24:31

        ; User code segment (selector 0x18)
        ; 64-bit long mode: L=1, D=0, ring 3
        dw 0xFFFF               ; Limit 0:15
        dw 0x0000               ; Base 0:15
        db 0x00                 ; Base 16:23
        db 11111010b            ; Access: P=1, DPL=3, S=1, Type=1010
        db 10101111b            ; G=1, L=1, D=0
        db 0x00                 ; Base 24:31

        ; User data segment (selector 0x20)
        ; Flat 4GB, ring 3
        dw 0xFFFF               ; Limit 0:15
        dw 0x0000               ; Base 0:15
        db 0x00                 ; Base 16:23
        db 11110010b            ; Access: P=1, DPL=3, S=1, Type=0010
        db 11001111b            ; G=1, DB=1
        db 0x00                 ; Base 24:31

        ; TSS descriptor (selector 0x28) — 16 bytes in long mode
        ; Low 8 bytes
        dw 0x0067               ; Limit (104 bytes - 1)
        dw 0x0000               ; Base 0:15 (filled by kernel)
        db 0x00                 ; Base 16:23
        db 10001001b            ; Access: P=1, DPL=0, Type=1001 (64-bit TSS)
        db 0x00                 ; Flags + Limit 16:19
        db 0x00                 ; Base 24:31
        ; High 8 bytes (occupies selector 0x30 — not usable as segment)
        dd 0x00000000           ; Base 32:63
        dd 0x00000000           ; Reserved, must be zero

        ; Temporary 32-bit code segment (selector 0x38)
        ; Used only during real-mode to long-mode transition
        dw 0xFFFF               ; Limit 0:15
        dw 0x0000               ; Base 0:15
        db 0x00                 ; Base 16:23
        db 10011010b            ; Access: P=1, DPL=0, S=1, Type=1010
        db 11001111b            ; G=1, DB=1 (32-bit), Limit 16:19=0xF
        db 0x00                 ; Base 24:31
gdt_end:

gdt_descriptor:
        dw gdt_end - gdt_start - 1     ; GDT size
        dd gdt_start                    ; GDT base address

;---------------------------------------
; E820 memory map buffer (at end of stage2)
; Room for 32 entries × 24 bytes = 768 bytes
;---------------------------------------
memory_map:
        times 32 * 24 db 0

; Pad stage2 to exactly 16KB (32 sectors)
        times (32 * 512) - ($ - $$) db 0
