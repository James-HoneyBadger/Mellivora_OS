;
; Mellivora OS - Stage 2 Loader
;
; Loaded at 0x7E00 by stage 1 boot sector.
; Runs in 16-bit real mode initially. Responsibilities:
;   1. Detect available memory via BIOS int 0x15 E820
;   2. Load the 32-bit kernel from disk (LBA 33+) to 0x100000 (1MB)
;   3. Set up GDT for flat 4GB segments
;   4. Switch to 32-bit protected mode
;   5. Jump to 32-bit kernel entry point
;
; Target: i486+
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
        ; Kernel loaded in 64KB chunks: BIOS int 0x13 to 0x10000, then
        ; INT 0x15/0x87 copies each chunk to its destination above 1 MB.
        ;---------------------------------------
load_kernel:
        mov si, msg_load_kern
        call print16

        mov dword [cur_lba],    33
        mov word  [sectors_left], KERNEL_SECTORS
        mov dword [kern_dest],  0x00100000   ; running 32-bit physical destination

.lk_chunk:
        cmp word [sectors_left], 0
        je  .lk_done

        ; sectors this pass = min(sectors_left, 128)  [128 sectors = 64 KB]
        mov ax, [sectors_left]
        cmp ax, 128
        jle .lk_size_ok
        mov ax, 128
.lk_size_ok:
        mov [chunk_size], ax

        ; ---- INT 0x13 read to 0x1000:0x0000 (linear 0x10000) ----
        mov byte [kern_dap], 16
        mov byte [kern_dap+1], 0
        mov [kern_dap+2], ax            ; sector count
        mov word [kern_dap+4], 0x0000   ; buffer offset
        mov word [kern_dap+6], 0x1000   ; buffer segment -> linear 0x10000
        mov eax, [cur_lba]
        mov [kern_dap+8], eax
        mov dword [kern_dap+12], 0
        mov word [load_seg], 0x1000     ; CHS fallback segment
        call read_kernel_chunk
        jc  .lk_fail

        ; Advance LBA counter
        movzx eax, word [chunk_size]
        add   [cur_lba], eax
        mov   ax, [chunk_size]
        sub   [sectors_left], ax

        ; ---- INT 0x15/0x87: copy 0x10000 -> kern_dest ----
        ; Fill destination descriptor (entry 3, bytes 24..31) with kern_dest
        mov eax, [kern_dest]
        mov [xmem_gdt + 24 + 2], ax     ; base bits 0-15
        shr eax, 16
        mov [xmem_gdt + 24 + 4], al     ; base bits 16-23
        mov [xmem_gdt + 24 + 7], ah     ; base bits 24-31

        ; CX = words to move = chunk_size * 256  (512 bytes / 2 per sector)
        mov  ax, [chunk_size]
        shl  ax, 8                      ; * 256 words/sector
        mov  cx, ax
        push es
        xor  bx, bx
        mov  es, bx                     ; ES:SI -> xmem_gdt at segment 0
        mov  si, xmem_gdt
        mov  ah, 0x87
        int  0x15
        pop  es
        jc  .lk_fail

        ; Advance destination
        movzx eax, word [chunk_size]
        shl   eax, 9                    ; * 512 bytes
        add   [kern_dest], eax

        mov al, '.'
        call putchar16
        jmp .lk_chunk

.lk_done:
        mov si, msg_ok
        call print16
        jmp enter_pmode

.lk_fail:
        mov si, msg_load_fail
        call print16
        jmp halt16

        ;---------------------------------------
        ; Enter 32-bit protected mode
        ;---------------------------------------
enter_pmode:
        mov si, msg_pmode
        call print16

        cli                             ; No interrupts during switch

        ; Re-enable A20 gate (ensure it's on before entering pmode)
        in al, 0x92
        or al, 2
        and al, 0xFE
        out 0x92, al

        ; Load the real GDT for protected mode
        lgdt [gdt_descriptor]

        ; Set PE bit in CR0
        mov eax, cr0
        or eax, 1
        mov cr0, eax

        ; Far jump to flush pipeline and load CS with 32-bit code selector
        jmp 0x08:pmode_entry

halt16:
        cli
        hlt
        jmp halt16

;=======================================================
; LONG MODE TRANSITION  (32-bit pmode -> x86-64)
;
; Only compiled when KERNEL_64BIT is defined at assemble time:
;   nasm -DKERNEL_64BIT ...
;
; Steps:
;   1. Verify CPUID supports long mode (leaf 0x80000001, bit 29 of EDX).
;   2. Build minimal identity-map page tables at LM_PT_BASE:
;        PML4[0] -> PDPT  (1 entry)
;        PDPT[0] -> PDT   (1 entry, covers 0..1 GB)
;        PDT[0..7] -> 8 x 2-MB large pages covering 0..16 MB
;   3. Load 64-bit GDT.
;   4. CR4.PAE = 1, CR3 = PML4, EFER.LME = 1, CR0.PG = 1.
;   5. Far-jump into the 64-bit code selector.
;   6. Set up 64-bit segment registers, jump to KERNEL_LOAD_ADDR (0x100000).
;=======================================================
%ifdef KERNEL_64BIT
[BITS 32]

; Physical page-table area: 4 pages × 4 KB = 16 KB at 0x70000.
; This range is below the conventional-memory top (0x9FC00) and above
; the real-mode IVT/BDA, so it is safe to write here.
LM_PT_BASE       equ 0x00070000
LM_PML4          equ LM_PT_BASE          ; PML4  (4 KB)
LM_PDPT          equ LM_PT_BASE + 0x1000 ; PDPT  (4 KB)
LM_PDT           equ LM_PT_BASE + 0x2000 ; PDT   (4 KB)
KERNEL_LOAD_ADDR equ 0x00100000

enter_longmode:
        ; --- Step 1: Confirm long-mode support via CPUID ---
        mov eax, 0x80000000
        cpuid
        cmp eax, 0x80000001         ; Must support extended leaves
        jb .lm_no_64bit

        mov eax, 0x80000001
        cpuid
        bt edx, 29                  ; LM bit
        jnc .lm_no_64bit

        ; --- Step 2: Zero all page-table pages ---
        mov edi, LM_PT_BASE
        xor eax, eax
        mov ecx, (3 * 4096) / 4     ; 3 pages
        rep stosd

        ; PML4[0] = PDPT | Present | Writable
        mov dword [LM_PML4 + 0],  (LM_PDPT | 0x03)
        mov dword [LM_PML4 + 4],  0

        ; PDPT[0] = PDT | Present | Writable  (covers 0..512 GB in one entry)
        mov dword [LM_PDPT + 0],  (LM_PDT | 0x03)
        mov dword [LM_PDPT + 4],  0

        ; PDT entries: 2 MB large pages 0..15 MB (8 entries)
        ; Each 2-MB large page PDE: base | PS(bit7) | Present | Writable
        xor ebx, ebx               ; page index 0..7
.lm_fill_pdt:
        mov eax, ebx
        shl eax, 21                ; base = page_idx * 2 MB
        or  eax, 0x83              ; Present | Writable | PS
        mov [LM_PDT + ebx * 8],     eax
        mov dword [LM_PDT + ebx * 8 + 4], 0
        inc ebx
        cmp ebx, 8
        jl  .lm_fill_pdt

        ; --- Step 3: Load 64-bit GDT ---
        lgdt [gdt64_descriptor]

        ; --- Step 4: Switch to long mode ---
        ; CR4: set PAE (bit 5)
        mov eax, cr4
        or  eax, (1 << 5)
        mov cr4, eax

        ; CR3: load PML4 base
        mov eax, LM_PML4
        mov cr3, eax

        ; EFER: set LME (bit 8) via MSR 0xC0000080
        mov ecx, 0xC0000080
        rdmsr
        or  eax, (1 << 8)
        wrmsr

        ; CR0: set PG (bit 31) — EFER.LMA activates automatically
        mov eax, cr0
        or  eax, (1 << 31)
        mov cr0, eax

        ; Far jump into 64-bit code selector (0x08 from gdt64)
        ; This flushes the pipeline and activates 64-bit mode.
        jmp 0x08:.lm_64bit

        ; --- Step 5: 64-bit entry point ---
[BITS 64]
.lm_64bit:
        ; Set all data segments to the 64-bit data selector (0x10)
        mov ax, 0x10
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax

        ; Restore a sane stack in the low 1 MB (kernel will set its own)
        mov rsp, 0x9FC00

        ; Jump to the 64-bit kernel entry point at 0x100000
        mov rax, KERNEL_LOAD_ADDR
        jmp rax

[BITS 32]
.lm_no_64bit:
        ; CPU does not support long mode; print error and halt
        mov esi, msg_no_longmode
        call print32
        cli
        hlt

; 32-bit print helper (we may still be in 32-bit pmode here)
print32:
        lodsb
        test al, al
        jz .done32
        push eax
        push ebx
        push edx
        ; Write directly to VGA text buffer at 0xB8000, row 24
        ; (simple fallback — does not scroll)
        mov ebx, 0xB8000 + (24 * 80 * 2)
        mov ah, 0x4F           ; red background, white text
        mov [ebx], ax
        add ebx, 2
        pop edx
        pop ebx
        pop eax
        jmp print32
.done32:
        ret

msg_no_longmode: db "ERROR: CPU does not support x86-64 long mode.", 0

;---------------------------------------
; 64-bit GDT (3 entries: null, code64, data64)
;---------------------------------------
gdt64_start:
        ; Entry 0: null descriptor
        dq 0

        ; Entry 1: 64-bit code segment (selector 0x08)
        ; Base=0, Limit ignored, L=1 (64-bit), P=1, DPL=0, Code, R/X
        dw 0x0000               ; Limit[0:15]  (ignored in 64-bit)
        dw 0x0000               ; Base[0:15]
        db 0x00                 ; Base[16:23]
        db 10011010b            ; Access: Present | DPL=0 | Code | Exec/Read
        db 00100000b            ; Flags: L=1 (64-bit), D=0; Limit[16:19]=0
        db 0x00                 ; Base[24:31]

        ; Entry 2: 64-bit data segment (selector 0x10)
        dw 0xFFFF               ; Limit[0:15]
        dw 0x0000               ; Base[0:15]
        db 0x00                 ; Base[16:23]
        db 10010010b            ; Access: Present | DPL=0 | Data | R/W
        db 00000000b            ; Flags: G=0, D=0 (data in 64-bit mode)
        db 0x00                 ; Base[24:31]

gdt64_descriptor:
        dw (gdt64_start - $ + 5)  ; Limit = size - 1 (computed at assemble)
        dd gdt64_start            ; Base

%endif  ; KERNEL_64BIT

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

;=======================================================
; 32-BIT PROTECTED MODE CODE
;=======================================================
[BITS 32]

pmode_entry:
        ; Set up 32-bit segment registers
        cld
        mov ax, 0x10            ; flat 4 GB data selector
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax
        mov esp, 0x9FC00        ; Stack at top of conventional memory

        ; Store boot drive and memory map info at known location
        ; The kernel expects these at 0x500 (BIOS-safe area)
        movzx eax, byte [boot_drive]
        mov [0x500], eax
        movzx eax, word [memory_map_count]
        mov [0x504], eax
        mov dword [0x508], memory_map

        ; Kernel already at 0x100000.
        ; When KERNEL_64BIT is defined the build target requires a 64-bit
        ; kernel image.  We transition from 32-bit pmode to x86-64 long mode
        ; before handing off, so the kernel entry point runs in 64-bit mode.
        ; Without KERNEL_64BIT (default legacy build) we jump directly.
%ifdef KERNEL_64BIT
        jmp enter_longmode
%else
        jmp 0x08:0x00100000
%endif

;=======================================================
; DATA (16-bit context)
;=======================================================
[BITS 16]

KERNEL_TMPBUF_SEG equ 0x2000   ; (legacy, unused)
KERNEL_TMPBUF_OFF equ 0x0000
KERNEL_TMPBUF_LIN equ 0x20000
PRELOAD_KERNEL_ADDR equ 0xBE00
BOUNCE_SEG      equ 0x0C00
BOUNCE_LIN      equ 0xC000

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
kern_dest:      dd 0x00100000   ; running 32-bit destination for INT 0x15/0x87 loader

; INT 0x15/0x87 extended-memory descriptor table (8 entries x 8 bytes)
; Entry 2 = source (always 0x10000); entry 3 = dest (filled at runtime)
xmem_gdt:
        times 2*8 db 0          ; entries 0,1: zeros (unused)
        ; Entry 2: source = 0x10000
        dw 0xFFFF               ; limit 0-15
        dw 0x0000               ; base 0-15
        db 0x01                 ; base 16-23  (0x01 0000 = 0x10000)
        db 0x93                 ; access: present, DPL0, data R/W
        db 0x00                 ; G=0, 16-bit, limit 16-19=0
        db 0x00                 ; base 24-31
        ; Entry 3: destination (base filled at runtime)
        dw 0xFFFF               ; limit 0-15
        dw 0x0000               ; base 0-15 (filled)
        db 0x00                 ; base 16-23 (filled)
        db 0x93                 ; access
        db 0x00                 ; flags
        db 0x00                 ; base 24-31 (filled)
        times 4*8 db 0          ; entries 4-7: zeros

kern_dap:       times 16 db 0

msg_stage2:     db "Stage 2 loader", 0x0D, 0x0A, 0
msg_mem:        db "Memory: ", 0
msg_entries:    db " regions", 0x0D, 0x0A, 0
msg_load_kern:  db "Loading kernel", 0
msg_ok:         db " OK", 0x0D, 0x0A, 0
msg_load_fail:  db "Kernel load fail!", 0
msg_pmode:      db "Entering protected mode...", 0x0D, 0x0A, 0
splash_title:   db "Mellivora OS - Booting...", 0

;---------------------------------------
; GDT for 32-bit protected mode
; Flat model: 4GB code and data segments
;---------------------------------------
gdt_start:
        ; Null descriptor (selector 0x00)
        dq 0

        ; Code segment descriptor (selector 0x08)
        ; Base=0, Limit=4GB, 32-bit, ring 0, executable, readable
        dw 0xFFFF               ; Limit 0:15
        dw 0x0000               ; Base 0:15
        db 0x00                 ; Base 16:23
        db 10011010b            ; Access: present, ring 0, code, exec/read
        db 11001111b            ; Flags: 4KB granularity, 32-bit + Limit 16:19
        db 0x00                 ; Base 24:31

        ; Data segment descriptor (selector 0x10)
        ; Base=0, Limit=4GB, 32-bit, ring 0, writable
        dw 0xFFFF               ; Limit 0:15
        dw 0x0000               ; Base 0:15
        db 0x00                 ; Base 16:23
        db 10010010b            ; Access: present, ring 0, data, read/write
        db 11001111b            ; Flags: 4KB granularity, 32-bit + Limit 16:19
        db 0x00                 ; Base 24:31

        ; User code segment descriptor (selector 0x18)
        ; Base=0, Limit=4GB, 32-bit, ring 3, executable, readable
        dw 0xFFFF               ; Limit 0:15
        dw 0x0000               ; Base 0:15
        db 0x00                 ; Base 16:23
        db 11111010b            ; Access: present, ring 3, code, exec/read
        db 11001111b            ; Flags: 4KB granularity, 32-bit + Limit 16:19
        db 0x00                 ; Base 24:31

        ; User data segment descriptor (selector 0x20)
        ; Base=0, Limit=4GB, 32-bit, ring 3, writable
        dw 0xFFFF               ; Limit 0:15
        dw 0x0000               ; Base 0:15
        db 0x00                 ; Base 16:23
        db 11110010b            ; Access: present, ring 3, data, read/write
        db 11001111b            ; Flags: 4KB granularity, 32-bit + Limit 16:19
        db 0x00                 ; Base 24:31

        ; TSS descriptor (selector 0x28)
        ; Base filled by kernel at runtime
        dw 0x0067               ; Limit (104 bytes - 1)
        dw 0x0000               ; Base 0:15 (filled by kernel)
        db 0x00                 ; Base 16:23 (filled by kernel)
        db 10001001b            ; Access: present, ring 0, TSS available
        db 0x00                 ; Flags + Limit 16:19
        db 0x00                 ; Base 24:31 (filled by kernel)
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
