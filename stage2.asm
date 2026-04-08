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
        ; Load kernel from disk into LOW MEMORY at KERNEL_TMPBUF
        ; (0x20000). 192KB kernel fits easily below 640KB.
        ; We will copy it to 1MB after entering protected mode.
        ;---------------------------------------
load_kernel:
        mov si, msg_load_kern
        call print16

        ; Kernel on disk starts at LBA 33 (after boot + stage2)
        mov dword [cur_lba], 33
        mov word [sectors_left], KERNEL_SECTORS
        mov word [load_seg], KERNEL_TMPBUF_SEG  ; Start segment = 0x2000

.load_chunk:
        cmp word [sectors_left], 0
        je .load_done

        ; How many sectors this chunk? Min(sectors_left, 64)
        mov ax, [sectors_left]
        cmp ax, 64
        jle .chunk_ok
        mov ax, 64
.chunk_ok:
        mov [chunk_size], ax

        ; Set up DAP for reading into [load_seg]:0x0000
        mov byte [kern_dap], 16
        mov byte [kern_dap+1], 0
        mov [kern_dap+2], ax            ; sector count
        mov word [kern_dap+4], 0x0000   ; offset always 0
        mov bx, [load_seg]
        mov [kern_dap+6], bx            ; segment advances each chunk
        mov eax, [cur_lba]
        mov [kern_dap+8], eax
        mov dword [kern_dap+12], 0

        ; Read from disk
        mov si, kern_dap
        mov ah, 0x42
        mov dl, [boot_drive]
        int 0x13
        jc .load_fail

        ; Advance LBA
        movzx eax, word [chunk_size]
        add [cur_lba], eax

        ; Advance load segment: seg += chunk_size * 512 / 16 = chunk_size * 32
        mov ax, [chunk_size]
        shl ax, 5               ; * 32 = paragraphs per chunk
        add [load_seg], ax

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
        jmp enter_pmode

.load_fail:
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
        mov ax, 0x10            ; Data segment selector
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax
        mov esp, 0x9FC00        ; Stack at top of conventional memory

        ; Store boot drive and memory map info at known location
        ; The kernel expects these at 0x500 (BIOS-safe area)
        movzx eax, byte [boot_drive]
        mov [0x500], eax                ; Boot drive number
        movzx eax, word [memory_map_count]
        mov [0x504], eax                ; Memory map entry count
        mov dword [0x508], memory_map   ; Pointer to memory map data

        ; Copy kernel from low memory (0x20000) to 1MB (0x100000)
        ; Kernel is KERNEL_SECTORS * 512 bytes
        cld
        mov esi, KERNEL_TMPBUF_LIN      ; Source: 0x20000
        mov edi, 0x00100000             ; Dest: 1MB
        mov ecx, (KERNEL_SECTORS * 512) / 4  ; Dword count
        rep movsd

        ; Jump to kernel at 1MB
        jmp 0x08:0x00100000

;=======================================================
; DATA (16-bit context)
;=======================================================
[BITS 16]

KERNEL_TMPBUF_SEG equ 0x2000   ; Segment for temp buffer (linear 0x20000)
KERNEL_TMPBUF_OFF equ 0x0000   ; Offset for temp buffer
KERNEL_TMPBUF_LIN equ 0x20000  ; Linear address of temp buffer (seg*16+off)

; KERNEL_SECTORS is generated by the build system in kernel_sectors.inc
; It equals ceil(kernel.bin size / 512), ensuring we always load the
; exact kernel binary without hard-coding a stale value.
%include "kernel_sectors.inc"

boot_drive:     db 0
cur_lba:        dd 0
load_seg:       dw 0
sectors_left:   dw 0
chunk_size:     dw 0
memory_map_count: dw 0

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
