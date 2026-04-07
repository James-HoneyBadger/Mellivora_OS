;
; Mellivora OS - 32-bit Protected Mode Kernel
;
; Loaded at 0x00100000 (1MB) by stage 2.
; Flat memory model, full 4GB address space.
;
; Features:
;   - Physical memory manager (bitmap allocator, 4KB pages, up to 4GB)
;   - IDT with full ISR/IRQ handling
;   - PIC remapped to INT 0x20-0x2F
;   - PIT timer at 100 Hz
;   - PS/2 keyboard driver with scancode translation
;   - VGA text mode driver (80x25, 16 colors, scrolling)
;   - ATA PIO disk driver (LBA48, up to 64GB per drive)
;   - Custom filesystem (HBFS - Honey Badger File System)
;   - Syscall interface via INT 0x80
;   - Interactive command shell
;
; Target: i486+
;

[BITS 32]
[ORG 0x00100000]

;=======================================================================
; CONSTANTS
;=======================================================================

; Memory layout
KERNEL_BASE         equ 0x00100000
KERNEL_STACK        equ 0x009FC00       ; Top of conventional memory for kernel stack
PROGRAM_BASE        equ 0x00200000      ; Programs load at 2MB
PROGRAM_MAX_SIZE    equ 0x00100000      ; Max 1MB per program
HEAP_BASE           equ 0x00400000      ; Kernel heap starts at 4MB
PMM_BITMAP          equ 0x00300000      ; Physical memory bitmap at 3MB

; Boot info passed by stage 2 at 0x500
BOOTINFO_DRIVE      equ 0x500
BOOTINFO_MMAP_CNT   equ 0x504
BOOTINFO_MMAP_PTR   equ 0x508

; VGA text mode
VGA_BASE            equ 0xB8000
VGA_WIDTH           equ 80
VGA_HEIGHT          equ 25
VGA_SIZE            equ VGA_WIDTH * VGA_HEIGHT * 2

; Colors
COLOR_DEFAULT       equ 0x07           ; Light gray on black
COLOR_HEADER        equ 0x1F           ; White on blue
COLOR_ERROR         equ 0x4F           ; White on red
COLOR_SUCCESS       equ 0x2F           ; White on green
COLOR_PROMPT        equ 0x0A           ; Light green on black
COLOR_INFO          equ 0x0B           ; Light cyan on black
COLOR_EXEC          equ 0x0E           ; Yellow on black
COLOR_BATCH         equ 0x0D           ; Light magenta on black

; PIC ports
PIC1_CMD            equ 0x20
PIC1_DATA           equ 0x21
PIC2_CMD            equ 0xA0
PIC2_DATA           equ 0xA1

; PIT
PIT_FREQ            equ 1193182
PIT_HZ              equ 100
PIT_DIVISOR         equ PIT_FREQ / PIT_HZ
PIT_CH0             equ 0x40
PIT_CMD             equ 0x43

; Keyboard
KB_DATA             equ 0x60
KB_STATUS           equ 0x64
KB_BUFFER_SIZE      equ 256

; ATA PIO ports (primary)
ATA_DATA            equ 0x1F0
ATA_ERROR           equ 0x1F1
ATA_SECCOUNT        equ 0x1F2
ATA_LBA_LO          equ 0x1F3
ATA_LBA_MID         equ 0x1F4
ATA_LBA_HI          equ 0x1F5
ATA_DRIVE           equ 0x1F6
ATA_CMD             equ 0x1F7
ATA_STATUS          equ 0x1F7
ATA_CONTROL         equ 0x3F6

; ATA commands
ATA_CMD_READ        equ 0x24           ; READ SECTORS EXT (LBA48)
ATA_CMD_WRITE       equ 0x34           ; WRITE SECTORS EXT (LBA48)
ATA_CMD_IDENTIFY    equ 0xEC
ATA_CMD_FLUSH       equ 0xE7

; Arrow key codes (returned by keyboard driver)
KEY_UP              equ 0x80
KEY_DOWN            equ 0x81
KEY_LEFT            equ 0x82
KEY_RIGHT           equ 0x83

; ATA status bits
ATA_SR_BSY          equ 0x80
ATA_SR_DRDY         equ 0x40
ATA_SR_DRQ          equ 0x08
ATA_SR_ERR          equ 0x01

; Filesystem constants (HBFS - Honey Badger File System)
HBFS_MAGIC           equ 0x48424653     ; 'HBFS'
HBFS_BLOCK_SIZE      equ 4096           ; 4KB blocks
HBFS_SECTORS_PER_BLK equ HBFS_BLOCK_SIZE / 512
HBFS_MAX_FILENAME    equ 252            ; 252 chars + null
HBFS_DIR_ENTRY_SIZE  equ 288            ; Filename(253) + type(1) + flags(2) + size(4) +
                                       ; start_block(4) + blocks(4) + created(4) +
                                       ; modified(4) + reserved(12) = 288
HBFS_ROOT_DIR_BLOCKS equ 16            ; Root directory uses 16 blocks (supports 227 entries)
HBFS_ROOT_DIR_SECTS  equ HBFS_ROOT_DIR_BLOCKS * HBFS_SECTORS_PER_BLK ; 128 sectors
HBFS_ROOT_DIR_SIZE   equ HBFS_ROOT_DIR_BLOCKS * HBFS_BLOCK_SIZE      ; 65536 bytes
HBFS_MAX_FILES       equ HBFS_ROOT_DIR_SIZE / HBFS_DIR_ENTRY_SIZE    ; 227 entries
HBFS_SUBDIR_BLOCKS   equ 4            ; Subdirectories get 4 blocks (56 entries each)
HBFS_SUPERBLOCK_LBA  equ 417           ; After kernel area (LBA 33 + 384 sectors)
HBFS_BITMAP_START    equ 418           ; Block allocation bitmap (8 sectors)
HBFS_ROOT_DIR_START  equ 426           ; Root directory (128 sectors = 16 blocks)
HBFS_DATA_START      equ 554           ; Data blocks start here

; File types (stored at byte 253 of directory entry)
FTYPE_FREE          equ 0
FTYPE_TEXT          equ 1              ; Text file
FTYPE_FILE          equ 1              ; Alias for backward compat
FTYPE_DIR           equ 2              ; Directory
FTYPE_EXEC          equ 3              ; Executable (flat binary or ELF)
FTYPE_BATCH         equ 4              ; Batch script

; Syscall numbers (via INT 0x80)
SYS_EXIT            equ 0
SYS_PUTCHAR         equ 1
SYS_GETCHAR         equ 2
SYS_PRINT           equ 3
SYS_READ_KEY        equ 4
SYS_OPEN            equ 5
SYS_READ            equ 6
SYS_WRITE           equ 7
SYS_CLOSE           equ 8
SYS_DELETE          equ 9
SYS_SEEK            equ 10
SYS_STAT            equ 11
SYS_MKDIR           equ 12
SYS_READDIR         equ 13
SYS_SETCURSOR       equ 14
SYS_GETTIME         equ 15
SYS_SLEEP           equ 16
SYS_CLEAR           equ 17
SYS_SETCOLOR        equ 18
SYS_MALLOC          equ 19
SYS_FREE            equ 20
SYS_EXEC            equ 21
SYS_DISK_READ       equ 22
SYS_DISK_WRITE      equ 23

LINE_BUFFER_SIZE    equ 512
BATCH_BUFFER_SIZE   equ 32768           ; 32KB max batch script

; Directory entry field offsets
DIRENT_NAME         equ 0
DIRENT_TYPE         equ 253
DIRENT_FLAGS        equ 254
DIRENT_SIZE         equ 256
DIRENT_START_BLOCK  equ 260
DIRENT_BLOCK_COUNT  equ 264
DIRENT_CREATED      equ 268
DIRENT_MODIFIED     equ 272

; Serial port (COM1)
COM1_PORT           equ 0x3F8

; Directory stack (for multi-level subdirectory navigation)
DIR_STACK_MAX       equ 16             ; Max nesting depth
DIR_STACK_ENTRY_SIZE equ 264           ; lba(4) + sects(4) + name(256)
COM1_LSR            equ 0x3FD

; RTC (CMOS)
RTC_INDEX           equ 0x70
RTC_DATA            equ 0x71

; PC Speaker
PIT_CH2             equ 0x42
SPEAKER_PORT        equ 0x61

; New syscall numbers (extending 0-23)
SYS_BEEP            equ 24
SYS_DATE            equ 25
SYS_CHDIR           equ 26
SYS_GETCWD          equ 27
SYS_SERIAL          equ 28
SYS_GETENV          equ 29
SYS_FREAD           equ 30      ; Read entire file: EBX=name ECX=buf -> EAX=bytes
SYS_FWRITE          equ 31      ; Write entire file: EBX=name ECX=buf EDX=size ESI=type(0=text)
SYS_GETARGS         equ 32      ; Get command-line args: EBX=buf -> EAX=length
SYS_SERIAL_IN       equ 33      ; Read char from serial: -> EAX=char

; File descriptor constants
FD_MAX              equ 8
FD_ENTRY_SIZE       equ 32
FD_FLAG_CLOSED      equ 0
FD_FLAG_READ        equ 1
FD_FLAG_WRITE       equ 2

; ELF constants
ELF_MAGIC           equ 0x464C457F
ELF_PT_LOAD         equ 1
ELF_EHDR_SIZE       equ 52
ELF_PHDR_SIZE       equ 32

; Environment
ENV_MAX             equ 16
ENV_ENTRY_SIZE      equ 128

; Ctrl+C / Tab
CTRL_C_CODE         equ 3
TAB_KEY             equ 0x09

; Ring 3 selectors
USER_CS             equ 0x1B    ; 0x18 | RPL 3
USER_DS             equ 0x23    ; 0x20 | RPL 3
TSS_SEL             equ 0x28

; Program exit trampoline
PROGRAM_EXIT_ADDR   equ PROGRAM_BASE + PROGRAM_MAX_SIZE - 16

;=======================================================================
; KERNEL ENTRY POINT
;=======================================================================
kernel_entry:
        ; Set up 32-bit segments and stack
        mov ax, 0x10            ; Flat data selector
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax
        mov esp, KERNEL_STACK

        ; Initialize BSS (zero out kernel data)
        mov edi, bss_start
        mov ecx, (bss_end - bss_start)
        shr ecx, 2             ; Dword count
        xor eax, eax
        rep stosd

        ; Initialize current directory to root
        mov dword [current_dir_lba], HBFS_ROOT_DIR_START
        mov dword [current_dir_sects], HBFS_ROOT_DIR_SECTS
        mov byte [current_dir_name], '/'
        mov byte [current_dir_name + 1], 0

        ; Initialize subsystems
        call vga_init
        call pic_init
        call idt_init
        call pit_init
        call kb_init
        call pmm_init
        call ata_init
        call serial_init
        call tss_init

        ; Enable interrupts
        sti

        ; Print banner
        mov esi, banner_str
        call vga_print_color
        mov byte [vga_color], COLOR_DEFAULT

        ; Print system info
        call print_sysinfo

        ; Initialize filesystem
        call hbfs_init

        ; Enter the command shell
        jmp shell_main

;=======================================================================
; VGA TEXT MODE DRIVER
;=======================================================================

vga_init:
        ; Set default color and clear screen
        mov byte [vga_color], COLOR_DEFAULT
        mov dword [vga_cursor_x], 0
        mov dword [vga_cursor_y], 0
        call vga_clear
        call vga_update_cursor
        ret

; Clear screen (always uses default color)
vga_clear:
        mov edi, VGA_BASE
        mov byte [vga_color], COLOR_DEFAULT
        mov ah, COLOR_DEFAULT
        mov al, ' '
        mov ecx, VGA_WIDTH * VGA_HEIGHT
        rep stosw
        mov dword [vga_cursor_x], 0
        mov dword [vga_cursor_y], 0
        call vga_update_cursor
        ret

; Print single character in AL
vga_putchar:
        pushad

        cmp al, 0x0A            ; Line feed?
        je .newline
        cmp al, 0x0D            ; Carriage return?
        je .cr
        cmp al, 0x08            ; Backspace?
        je .backspace
        cmp al, 0x09            ; Tab?
        je .tab

        ; Regular character
        mov edi, VGA_BASE
        mov ecx, [vga_cursor_y]
        imul ecx, VGA_WIDTH * 2
        mov edx, [vga_cursor_x]
        shl edx, 1
        add edi, ecx
        add edi, edx

        mov ah, [vga_color]
        mov [edi], ax

        inc dword [vga_cursor_x]
        cmp dword [vga_cursor_x], VGA_WIDTH
        jl .done
        ; Wrap to next line
        mov dword [vga_cursor_x], 0
        inc dword [vga_cursor_y]
        jmp .check_scroll

.newline:
        mov dword [vga_cursor_x], 0
        inc dword [vga_cursor_y]
        jmp .check_scroll

.cr:
        mov dword [vga_cursor_x], 0
        jmp .done

.backspace:
        cmp dword [vga_cursor_x], 0
        je .bs_prevline
        dec dword [vga_cursor_x]
        ; Erase character at cursor
        mov edi, VGA_BASE
        mov ecx, [vga_cursor_y]
        imul ecx, VGA_WIDTH * 2
        mov edx, [vga_cursor_x]
        shl edx, 1
        add edi, ecx
        add edi, edx
        mov byte [edi], ' '
        mov al, [vga_color]
        mov [edi+1], al
        jmp .done

.bs_prevline:
        cmp dword [vga_cursor_y], 0
        je .done
        dec dword [vga_cursor_y]
        mov dword [vga_cursor_x], VGA_WIDTH - 1
        jmp .done

.tab:
        ; Advance to next 8-column boundary
        mov eax, [vga_cursor_x]
        add eax, 8
        and eax, ~7
        cmp eax, VGA_WIDTH
        jl .tab_ok
        mov eax, 0
        inc dword [vga_cursor_y]
.tab_ok:
        mov [vga_cursor_x], eax
        jmp .check_scroll

.check_scroll:
        cmp dword [vga_cursor_y], VGA_HEIGHT
        jl .done
        call vga_scroll
        mov dword [vga_cursor_y], VGA_HEIGHT - 1

.done:
        call vga_update_cursor
        popad
        ret

; Scroll screen up one line
vga_scroll:
        pushad
        ; Copy lines 1..24 to lines 0..23
        mov esi, VGA_BASE + VGA_WIDTH * 2
        mov edi, VGA_BASE
        mov ecx, VGA_WIDTH * (VGA_HEIGHT - 1) * 2 / 4
        rep movsd

        ; Clear last line (always use default color)
        mov edi, VGA_BASE + VGA_WIDTH * (VGA_HEIGHT - 1) * 2
        mov ah, COLOR_DEFAULT
        mov al, ' '
        mov ecx, VGA_WIDTH
        rep stosw
        popad
        ret

; Update hardware cursor position
vga_update_cursor:
        pushad
        mov eax, [vga_cursor_y]
        imul eax, VGA_WIDTH
        add eax, [vga_cursor_x]
        mov ecx, eax

        mov dx, 0x3D4
        mov al, 0x0F            ; Cursor low register
        out dx, al
        mov dx, 0x3D5
        mov al, cl
        out dx, al

        mov dx, 0x3D4
        mov al, 0x0E            ; Cursor high register
        out dx, al
        mov dx, 0x3D5
        mov al, ch
        out dx, al
        popad
        ret

; Print a newline character (0x0A)
vga_newline:
        push eax
        mov al, 0x0A
        call vga_putchar
        pop eax
        ret

; Print null-terminated string at ESI
vga_print:
        pushad
.loop:
        lodsb
        or al, al
        jz .done
        call vga_putchar
        jmp .loop
.done:
        popad
        ret

; Print string at ESI with color prefix byte
vga_print_color:
        lodsb                   ; First byte = color
        mov [vga_color], al
        call vga_print
        ret

; Print 32-bit hex value in EAX
vga_print_hex:
        pushad
        mov ecx, 8              ; 8 hex digits
        mov ebx, eax
.loop:
        rol ebx, 4
        mov al, bl
        and al, 0x0F
        cmp al, 10
        jl .digit
        add al, 'A' - 10
        jmp .print
.digit:
        add al, '0'
.print:
        call vga_putchar
        loop .loop
        popad
        ret

; Print 32-bit decimal value in EAX
vga_print_dec:
        pushad
        mov ecx, 0              ; Digit counter
        mov ebx, 10

        test eax, eax
        jnz .nonzero
        mov al, '0'
        call vga_putchar
        jmp .done

.nonzero:
.push_digits:
        xor edx, edx
        div ebx
        push edx
        inc ecx
        test eax, eax
        jnz .push_digits

.pop_digits:
        pop eax
        add al, '0'
        call vga_putchar
        loop .pop_digits

.done:
        popad
        ret

; Print 32-bit decimal value in EAX, right-aligned in field of EDX chars
; EAX = number, EDX = field width (0 = no padding)
vga_print_dec_width:
        pushad
        mov [.vpdw_width], edx
        ; First count digits
        mov ecx, 0
        mov ebx, 10
        mov [.vpdw_val], eax
        test eax, eax
        jnz .vpdw_count
        mov ecx, 1              ; '0' is 1 digit
        jmp .vpdw_pad
.vpdw_count:
        xor edx, edx
        div ebx
        inc ecx
        test eax, eax
        jnz .vpdw_count
.vpdw_pad:
        ; Print (width - digits) leading spaces
        mov eax, [.vpdw_width]
        sub eax, ecx
        jle .vpdw_print
        mov edx, eax
.vpdw_spc:
        mov al, ' '
        call vga_putchar
        dec edx
        jnz .vpdw_spc
.vpdw_print:
        ; Now print the actual number
        mov eax, [.vpdw_val]
        call vga_print_dec
        popad
        ret
.vpdw_val:   dd 0
.vpdw_width: dd 0

; Set cursor position: EAX=x, EDX=y
vga_set_cursor:
        mov [vga_cursor_x], eax
        mov [vga_cursor_y], edx
        call vga_update_cursor
        ret

;=======================================================================
; PIC (Programmable Interrupt Controller) INITIALIZATION
;=======================================================================

pic_init:
        ; ICW1: begin initialization sequence
        mov al, 0x11
        out PIC1_CMD, al
        out PIC2_CMD, al

        ; ICW2: remap IRQ vectors
        ; PIC1: IRQ 0-7  -> INT 0x20-0x27
        ; PIC2: IRQ 8-15 -> INT 0x28-0x2F
        mov al, 0x20
        out PIC1_DATA, al
        mov al, 0x28
        out PIC2_DATA, al

        ; ICW3: cascade configuration
        mov al, 0x04            ; PIC1: IRQ2 has slave
        out PIC1_DATA, al
        mov al, 0x02            ; PIC2: cascade identity 2
        out PIC2_DATA, al

        ; ICW4: 8086 mode
        mov al, 0x01
        out PIC1_DATA, al
        out PIC2_DATA, al

        ; Mask all IRQs except IRQ0 (timer) and IRQ1 (keyboard)
        mov al, 0xFC            ; Enable IRQ0, IRQ1
        out PIC1_DATA, al
        mov al, 0xFF            ; Mask all on PIC2
        out PIC2_DATA, al

        ret

;=======================================================================
; IDT (Interrupt Descriptor Table)
;=======================================================================

idt_init:
        ; First, fill ALL 256 entries with default handler
        mov ecx, 0
        mov eax, isr_default
.fill_all:
        cmp ecx, 256
        jge .set_exceptions
        call idt_set_gate
        inc ecx
        jmp .fill_all

.set_exceptions:
        ; Set exception handlers (INT 0-31) — no error code
        ; Exceptions 0-7, 9, 15, 16-20, 22-31: no error code
        mov ecx, 0
.exc_noerr:
        cmp ecx, 32
        jge .irq_setup
        ; Skip those that push error codes
        cmp ecx, 8
        je .exc_skip
        cmp ecx, 10
        je .exc_skip
        cmp ecx, 11
        je .exc_skip
        cmp ecx, 12
        je .exc_skip
        cmp ecx, 13
        je .exc_skip
        cmp ecx, 14
        je .exc_skip
        cmp ecx, 17
        je .exc_skip
        cmp ecx, 21
        je .exc_skip
        mov eax, isr_exception_noerr
        call idt_set_gate
.exc_skip:
        inc ecx
        jmp .exc_noerr

.irq_setup:
        ; Set exception handlers that push error codes
        mov ecx, 8
        mov eax, isr_exception_err
        call idt_set_gate
        mov ecx, 10
        call idt_set_gate
        mov ecx, 11
        call idt_set_gate
        mov ecx, 12
        call idt_set_gate
        mov ecx, 13
        call idt_set_gate
        mov ecx, 14
        call idt_set_gate
        mov ecx, 17
        call idt_set_gate
        mov ecx, 21
        call idt_set_gate

        ; IRQ0 = INT 0x20 = Timer
        mov ecx, 0x20
        mov eax, irq_timer
        call idt_set_gate

        ; IRQ1 = INT 0x21 = Keyboard
        mov ecx, 0x21
        mov eax, irq_keyboard
        call idt_set_gate

        ; Fill PIC1 IRQs (INT 0x22-0x27) with PIC1-only EOI stub
        mov ecx, 0x22
.irq_loop_pic1:
        cmp ecx, 0x28
        jge .irq_pic2
        mov eax, irq_stub
        call idt_set_gate
        inc ecx
        jmp .irq_loop_pic1

.irq_pic2:
        ; Fill PIC2 IRQs (INT 0x28-0x2F) with dual-EOI stub
.irq_loop_pic2:
        cmp ecx, 0x30
        jge .syscall
        mov eax, irq_stub_pic2
        call idt_set_gate
        inc ecx
        jmp .irq_loop_pic2

.syscall:
        ; INT 0x80 = Syscall
        mov ecx, 0x80
        mov eax, syscall_handler
        ; Use trap gate with DPL=3 for syscalls
        mov edi, idt_table
        shl ecx, 3             ; ECX * 8 = offset in IDT
        add edi, ecx

        mov word [edi], ax      ; Offset low
        mov word [edi+2], 0x08  ; Code selector
        mov byte [edi+4], 0x00  ; Reserved
        mov byte [edi+5], 0xEF  ; Trap gate, DPL=3, Present
        shr eax, 16
        mov word [edi+6], ax    ; Offset high
        jmp .load

.load:
        lidt [idt_descriptor]
        ret

; Set IDT gate: ECX=vector, EAX=handler address
idt_set_gate:
        push edi
        mov edi, idt_table
        push ecx
        shl ecx, 3             ; ECX * 8 = offset in IDT
        add edi, ecx
        pop ecx

        mov word [edi], ax      ; Offset low
        mov word [edi+2], 0x08  ; Code segment selector
        mov byte [edi+4], 0x00  ; Reserved
        mov byte [edi+5], 0x8E  ; Interrupt gate, DPL=0, Present
        shr eax, 16
        mov word [edi+6], ax    ; Offset high

        pop edi
        ret

;=======================================================================
; ISR / IRQ HANDLERS
;=======================================================================

; Default handler for unregistered interrupts
isr_default:
        iretd

; Exception handler for exceptions WITHOUT error code
isr_exception_noerr:
        ; Stack: [EIP] [CS] [EFLAGS]
        ; Save the faulting EIP for display
        push eax
        mov eax, [esp + 4]      ; EIP is at esp+4 (after our push eax)
        mov [exc_eip], eax
        pop eax

        pushad
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_exception
        call vga_print
        ; Print faulting EIP
        mov esi, msg_exc_eip
        call vga_print
        mov eax, [exc_eip]
        call vga_print_hex
        mov al, 0x0A
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ; Recover to shell
        mov esp, KERNEL_STACK
        sti
        jmp shell_main

; Exception handler for exceptions WITH error code
isr_exception_err:
        ; Stack: [ErrorCode] [EIP] [CS] [EFLAGS]
        push eax
        mov eax, [esp + 8]      ; EIP is at esp+8 (errcode + push eax)
        mov [exc_eip], eax
        mov eax, [esp + 4]      ; Error code at esp+4
        mov [exc_errcode], eax
        pop eax

        pushad
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_exception
        call vga_print
        ; Print faulting EIP
        mov esi, msg_exc_eip
        call vga_print
        mov eax, [exc_eip]
        call vga_print_hex
        ; Print error code
        mov esi, msg_exc_err
        call vga_print
        mov eax, [exc_errcode]
        call vga_print_hex
        mov al, 0x0A
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ; Recover to shell
        mov esp, KERNEL_STACK
        sti
        jmp shell_main

irq_stub:
        pushad
        ; Send EOI to PIC1 only (for IRQs 2-7)
        mov al, 0x20
        out PIC1_CMD, al
        popad
        iretd

; IRQ stub for PIC2 IRQs (8-15) - send EOI to both PICs
irq_stub_pic2:
        pushad
        mov al, 0x20
        out PIC2_CMD, al
        out PIC1_CMD, al
        popad
        iretd

; IRQ0: PIT Timer interrupt (100 Hz)
irq_timer:
        pushad
        inc dword [tick_count]

        ; Send EOI
        mov al, 0x20
        out PIC1_CMD, al
        popad
        iretd

; IRQ1: Keyboard interrupt
irq_keyboard:
        pushad

        ; Read scancode from keyboard controller
        in al, KB_DATA

        ; Check if it's a key release (bit 7 set)
        test al, 0x80
        jnz .key_up

        ; Key press: check for modifier keys first
        movzx ebx, al
        cmp ebx, 128
        jge .eoi

        ; Check for ctrl press
        cmp bl, 0x1D            ; Left ctrl press
        je .ctrl_on

        ; Check for shift press BEFORE ASCII lookup
        cmp bl, 0x2A            ; Left shift press
        je .shift_on
        cmp bl, 0x36            ; Right shift press
        je .shift_on

        ; Translate scancode to ASCII
        mov al, [scancode_table + ebx]
        or al, al
        jz .eoi                 ; Non-printable key

        ; Store in keyboard buffer (ring buffer)
        test byte [kb_shift], 1
        jz .no_shift
        cmp al, 0x20            ; Don't shift-translate control chars
        jl .no_shift
        movzx ebx, al
        mov al, [shift_table + ebx - 0x20]
        cmp al, 0
        je .no_shift_store
.no_shift:
        ; If Ctrl is held, generate control code for letters
        test byte [kb_ctrl], 1
        jz .no_ctrl
        cmp al, 'A'
        jl .no_ctrl
        cmp al, 'z'
        jg .no_ctrl
        and al, 0x1F            ; Ctrl+A=1, Ctrl+C=3, etc.
        ; Check for Ctrl+C program abort
        cmp al, CTRL_C_CODE
        jne .no_ctrl
        cmp byte [program_running], 1
        jne .no_ctrl
        ; Abort the running program
        mov byte [program_running], 0
        mov dword [program_exit_code], 0xFFFFFFFF
        mov byte [ctrl_c_flag], 1
        ; Send EOI to PIC
        mov al, 0x20
        out PIC1_CMD, al
        ; Hard-abort to shell
        mov esp, KERNEL_STACK
        mov byte [vga_color], COLOR_DEFAULT
        sti
        jmp shell_main
.no_ctrl:
        ; Check if buffer is full before writing
        mov ebx, [kb_write_idx]
        lea ecx, [ebx + 1]
        and ecx, KB_BUFFER_SIZE - 1
        cmp ecx, [kb_read_idx]
        je .eoi                 ; Buffer full, drop keystroke
        mov [kb_buffer + ebx], al
        mov [kb_write_idx], ecx
        jmp .eoi

.no_shift_store:
        jmp .eoi

.shift_on:
        or byte [kb_shift], 1
        jmp .eoi

.ctrl_on:
        or byte [kb_ctrl], 1
        jmp .eoi

.key_up:
        and al, 0x7F            ; Remove release bit
        cmp al, 0x1D
        je .ctrl_off
        cmp al, 0x2A
        je .shift_off
        cmp al, 0x36
        je .shift_off
        jmp .eoi

.ctrl_off:
        and byte [kb_ctrl], ~1
        jmp .eoi

.shift_off:
        and byte [kb_shift], ~1

.eoi:
        mov al, 0x20
        out PIC1_CMD, al
        popad
        iretd

;=======================================================================
; PIT (Programmable Interval Timer) - 100 Hz
;=======================================================================

pit_init:
        ; Channel 0, lobyte/hibyte, rate generator
        mov al, 0x36
        out PIT_CMD, al
        mov ax, PIT_DIVISOR
        out PIT_CH0, al
        mov al, ah
        out PIT_CH0, al
        ret

;=======================================================================
; KEYBOARD DRIVER
;=======================================================================

kb_init:
        mov dword [kb_read_idx], 0
        mov dword [kb_write_idx], 0
        mov byte [kb_shift], 0
        ret

; Read a key from buffer (blocking)
; Returns: AL = ASCII character
kb_getchar:
.wait:
        mov eax, [kb_read_idx]
        cmp eax, [kb_write_idx]
        jne .have_key
        sti                     ; Ensure interrupts are enabled
        hlt                     ; Sleep until next interrupt
        jmp .wait
.have_key:

        mov ebx, eax
        mov al, [kb_buffer + ebx]
        inc ebx
        and ebx, KB_BUFFER_SIZE - 1
        mov [kb_read_idx], ebx
        ret

; Non-blocking key check
; Returns: AL = key (0 if none), ZF set if no key
kb_pollchar:
        mov eax, [kb_read_idx]
        cmp eax, [kb_write_idx]
        je .none

        mov ebx, eax
        mov al, [kb_buffer + ebx]
        inc ebx
        and ebx, KB_BUFFER_SIZE - 1
        mov [kb_read_idx], ebx
        or al, al               ; Clear ZF
        ret

.none:
        xor eax, eax            ; AL=0, ZF=1
        ret

;=======================================================================
; PHYSICAL MEMORY MANAGER (Bitmap Allocator)
;=======================================================================

pmm_init:
        pushad

        ; First, mark all memory as used
        mov edi, PMM_BITMAP
        mov ecx, 0x20000 / 4   ; 128KB bitmap = covers 4GB (128K * 8 * 4096)
        mov eax, 0xFFFFFFFF
        rep stosd

        ; Now parse E820 memory map and free usable regions
        mov ecx, [BOOTINFO_MMAP_CNT]
        mov esi, [BOOTINFO_MMAP_PTR]
        or ecx, ecx
        jz .no_map

.map_loop:
        ; E820 entry: base(8), length(8), type(4), [acpi(4)]
        mov eax, [esi + 16]     ; Type
        cmp eax, 1              ; Type 1 = usable
        jne .next

        ; Get base and length (use lower 32 bits only for now)
        mov eax, [esi]          ; Base low
        mov ebx, [esi + 8]     ; Length low

        ; Skip memory below 4MB (reserved for kernel/drivers)
        cmp eax, 0x00400000
        jge .free_region
        ; Adjust if region overlaps 4MB
        mov edx, 0x00400000
        sub edx, eax
        cmp edx, ebx
        jge .next              ; Entire region below 4MB
        sub ebx, edx
        mov eax, 0x00400000

.free_region:
        ; Free pages from EAX with length EBX
        call pmm_free_region

.next:
        add esi, 24
        loop .map_loop

.no_map:
        ; Count free pages
        call pmm_count_free
        mov [total_free_pages], eax

        popad
        ret

; Free a region: EAX = base address, EBX = length in bytes
pmm_free_region:
        pushad
        shr eax, 12             ; Convert to page number
        shr ebx, 12             ; Convert to page count
        or ebx, ebx
        jz .done

.loop:
        ; Clear bit in bitmap (page_num / 8 = byte, page_num % 8 = bit)
        mov ecx, eax
        shr ecx, 3              ; Byte offset
        mov edx, eax
        and edx, 7              ; Bit offset

        mov edi, PMM_BITMAP
        add edi, ecx

        ; Create mask to clear the bit
        mov cl, dl
        mov ch, 1
        shl ch, cl
        not ch
        and [edi], ch            ; Clear the bit = free

        inc eax
        dec ebx
        jnz .loop

.done:
        popad
        ret

; Allocate a physical page
; Returns: EAX = physical address (0 = out of memory)
pmm_alloc_page:
        pushad
        mov esi, PMM_BITMAP
        mov ecx, 0x20000        ; Scan 128KB of bitmap

.scan:
        mov al, [esi]
        cmp al, 0xFF            ; All used?
        je .next_byte
        ; Find first zero bit
        mov edx, 0
.find_bit:
        bt eax, edx
        jnc .found
        inc edx
        cmp edx, 8
        jl .find_bit

.next_byte:
        inc esi
        loop .scan

        ; Out of memory
        popad
        xor eax, eax
        ret

.found:
        ; Set the bit (mark as used)
        bts dword [esi], edx

        ; Calculate page address
        sub esi, PMM_BITMAP
        shl esi, 3              ; Byte offset * 8
        add esi, edx            ; + bit offset = page number
        shl esi, 12             ; * 4096 = physical address

        mov [esp + 28], esi     ; Return via EAX in pushad frame
        popad
        ret

; Free a physical page
; EAX = physical address
pmm_free_page:
        pushad
        shr eax, 12             ; Page number
        mov ecx, eax
        shr ecx, 3              ; Byte offset
        and eax, 7              ; Bit offset

        mov edi, PMM_BITMAP
        add edi, ecx
        mov cl, al
        mov ch, 1
        shl ch, cl
        not ch
        and [edi], ch            ; Clear bit = free

        popad
        ret

; Allocate N contiguous physical pages
; ECX = number of pages to allocate
; Returns: EAX = physical address of first page (0 = failure)
pmm_alloc_pages:
        pushad
        cmp ecx, 0
        je .ap_fail
        mov ebp, ecx            ; EBP = pages needed
        mov esi, PMM_BITMAP
        mov ecx, 0x20000        ; Scan 128KB bitmap
        xor ebx, ebx            ; EBX = current page number

.ap_scan:
        movzx eax, byte [esi]
        cmp al, 0xFF
        je .ap_skip_byte

        ; Check each bit in this byte
        xor edx, edx
.ap_check_bit:
        bt eax, edx
        jc .ap_bit_used

        ; This bit is free - check if we can get EBP contiguous from here
        push esi
        push edx
        push ebx
        mov ecx, ebp            ; Need this many contiguous
        ; Current page = (esi - PMM_BITMAP)*8 + edx
        sub esi, PMM_BITMAP
        shl esi, 3
        add esi, edx            ; ESI = starting page number
        mov edi, esi            ; EDI = starting page number

.ap_contig_check:
        cmp ecx, 0
        je .ap_found
        ; Check if page ESI is free
        mov eax, esi
        shr eax, 3              ; Byte index
        push ecx
        mov ecx, esi
        and ecx, 7              ; Bit index
        bt dword [PMM_BITMAP + eax], ecx
        pop ecx
        jc .ap_contig_fail
        inc esi
        dec ecx
        jmp .ap_contig_check

.ap_found:
        ; Mark all pages [EDI..EDI+EBP-1] as used
        mov ecx, ebp
        mov esi, edi
.ap_mark:
        mov eax, esi
        shr eax, 3
        push ecx
        mov ecx, esi
        and ecx, 7
        bts dword [PMM_BITMAP + eax], ecx
        pop ecx
        inc esi
        loop .ap_mark

        ; Return address = EDI * 4096
        shl edi, 12
        pop ebx
        pop edx
        pop esi
        mov [esp + 28], edi     ; Return via EAX in pushad frame
        popad
        ret

.ap_contig_fail:
        pop ebx
        pop edx
        pop esi

.ap_bit_used:
        inc edx
        cmp edx, 8
        jl .ap_check_bit

.ap_skip_byte:
        inc esi
        add ebx, 8
        dec ecx
        jnz .ap_scan

.ap_fail:
        popad
        xor eax, eax
        ret

; Count free pages
; Returns: EAX = number of free pages
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

;=======================================================================
; ATA PIO DISK DRIVER (LBA48 - supports up to 128 PB)
;=======================================================================

ata_init:
        pushad
        ; Identify primary master drive
        mov dx, ATA_DRIVE
        mov al, 0xA0            ; Select master
        out dx, al

        ; Wait a bit
        mov ecx, 4
.wait1:
        mov dx, ATA_STATUS
        in al, dx
        loop .wait1

        ; Send IDENTIFY command
        mov dx, ATA_CMD
        mov al, ATA_CMD_IDENTIFY
        out dx, al

        ; Wait for BSY to clear
        call ata_wait_ready
        jc .no_drive

        ; Check if drive exists
        mov dx, ATA_STATUS
        in al, dx
        test al, al
        jz .no_drive

        ; Read 256 words of identify data
        mov dx, ATA_STATUS
.wait_drq:
        in al, dx
        test al, ATA_SR_ERR
        jnz .no_drive
        test al, ATA_SR_DRQ
        jz .wait_drq

        mov edi, ata_identify_buf
        mov ecx, 256
        mov dx, ATA_DATA
        rep insw

        ; Extract total LBA48 sectors (words 100-103)
        mov eax, [ata_identify_buf + 200]  ; LBA48 low dword
        mov [ata_total_sectors], eax
        mov eax, [ata_identify_buf + 204]  ; LBA48 high dword
        mov [ata_total_sectors + 4], eax

        mov byte [ata_present], 1
        
        mov esi, msg_ata_ok
        call vga_print

        ; Print disk size in MB
        mov eax, [ata_total_sectors]
        shr eax, 11             ; Sectors / 2048 = MB
        call vga_print_dec
        mov esi, msg_mb
        call vga_print

        popad
        ret

.no_drive:
        mov byte [ata_present], 0
        mov esi, msg_ata_none
        call vga_print
        popad
        ret

; Wait for ATA drive ready
; Returns: CF set on timeout
ata_wait_ready:
        push ecx
        mov ecx, 0x100000       ; Timeout counter
.loop:
        mov dx, ATA_STATUS
        in al, dx
        test al, ATA_SR_BSY
        jz .ready
        loop .loop
        stc                     ; Timeout
        pop ecx
        ret
.ready:
        clc
        pop ecx
        ret

; Read sectors using LBA48
; EAX = LBA low 32 bits, ECX = sector count (1-256), EDI = destination buffer
; Returns: CF set on error
ata_read_sectors:
        pushad

        push eax                ; Save LBA
        push ecx                ; Save count

        ; Select drive (master, LBA mode) and wait ready
        mov dx, ATA_DRIVE
        mov al, 0x40            ; Master, LBA
        out dx, al
        call ata_wait_ready
        jc .error_setup         ; Drive not ready

        ; Send high bytes first (LBA48 requires two writes)
        ; Sector count high byte
        pop ecx
        push ecx
        mov dx, ATA_SECCOUNT
        mov al, ch              ; High byte of sector count (LBA48 16-bit)
        out dx, al

        ; LBA48 high bytes: byte 3 = EAX[24:31], bytes 4-5 = 0
        mov eax, [esp + 4]      ; get saved LBA from stack
        shr eax, 24             ; extract bits 24-31
        mov dx, ATA_LBA_LO
        out dx, al              ; LBA byte 3
        xor al, al
        mov dx, ATA_LBA_MID
        out dx, al              ; LBA byte 4 = 0
        mov dx, ATA_LBA_HI
        out dx, al              ; LBA byte 5 = 0

        ; Now send low bytes
        pop ecx
        push ecx
        mov dx, ATA_SECCOUNT
        mov al, cl              ; Sector count low
        out dx, al

        ; LBA bytes 0-2
        pop ecx
        pop eax                 ; Restore LBA

        push ecx                ; Save count again
        mov dx, ATA_LBA_LO
        out dx, al              ; LBA[0:7]
        mov dx, ATA_LBA_MID
        shr eax, 8
        out dx, al              ; LBA[8:15]
        mov dx, ATA_LBA_HI
        shr eax, 8
        out dx, al              ; LBA[16:23]

        ; Send READ SECTORS EXT command
        mov dx, ATA_CMD
        mov al, ATA_CMD_READ
        out dx, al

        pop ecx                 ; Sector count

.read_loop:
        push ecx
        call ata_wait_ready
        jc .error

        ; AL already has status from ata_wait_ready (BSY=0)
        test al, ATA_SR_ERR
        jnz .error
        test al, ATA_SR_DRQ
        jz .error

        ; Read 256 words (512 bytes) for this sector
        mov ecx, 256
        mov dx, ATA_DATA
        rep insw

        pop ecx
        loop .read_loop

        popad
        clc
        ret

.error:
        pop ecx
        popad
        stc
        ret

.error_setup:
        pop ecx                 ; Clean up pushed count
        pop eax                 ; Clean up pushed LBA
        popad
        stc
        ret

; Write sectors using LBA48
; EAX = LBA low 32 bits, ECX = sector count, ESI = source buffer
; Returns: CF set on error
ata_write_sectors:
        pushad

        push eax
        push ecx

        ; Select drive and wait for it to be ready
        mov dx, ATA_DRIVE
        mov al, 0x40
        out dx, al
        call ata_wait_ready
        jc .werror_setup        ; Drive not ready

        ; High bytes - sector count high byte
        pop ecx
        push ecx
        mov dx, ATA_SECCOUNT
        mov al, ch              ; High byte of sector count (LBA48)
        out dx, al
        ; LBA48 high bytes: byte 3 = EAX[24:31], bytes 4-5 = 0
        mov eax, [esp + 4]      ; get saved LBA from stack
        shr eax, 24             ; extract bits 24-31
        mov dx, ATA_LBA_LO
        out dx, al              ; LBA byte 3
        xor al, al
        mov dx, ATA_LBA_MID
        out dx, al              ; LBA byte 4 = 0
        mov dx, ATA_LBA_HI
        out dx, al              ; LBA byte 5 = 0

        ; Low bytes
        pop ecx
        push ecx
        mov dx, ATA_SECCOUNT
        mov al, cl
        out dx, al

        pop ecx
        pop eax
        push ecx

        mov dx, ATA_LBA_LO
        out dx, al
        mov dx, ATA_LBA_MID
        shr eax, 8
        out dx, al
        mov dx, ATA_LBA_HI
        shr eax, 8
        out dx, al

        ; WRITE SECTORS EXT command
        mov dx, ATA_CMD
        mov al, ATA_CMD_WRITE
        out dx, al

        pop ecx

.write_loop:
        push ecx
        call ata_wait_ready
        jc .werror

        ; AL already has status from ata_wait_ready (BSY=0)
        test al, ATA_SR_ERR
        jnz .werror
        test al, ATA_SR_DRQ
        jz .werror

        ; Write 256 words
        mov ecx, 256
        mov dx, ATA_DATA
        rep outsw

        pop ecx
        loop .write_loop

        ; Flush cache once after all sectors written
        mov dx, ATA_CMD
        mov al, ATA_CMD_FLUSH
        out dx, al
        call ata_wait_ready

        popad
        clc
        ret

.werror:
        pop ecx
        popad
        stc
        ret

.werror_setup:
        pop ecx                 ; Clean up pushed count
        pop eax                 ; Clean up pushed LBA
        popad
        stc
        ret

;=======================================================================
; HBFS - HONEY BADGER FILE SYSTEM
;
; Superblock (LBA HBFS_SUPERBLOCK_LBA, 1 sector):
;   [0-3]   Magic: 'HBFS'
;   [4-7]   Version: 1
;   [8-11]  Total blocks
;   [12-15] Free blocks
;   [16-19] Root directory block
;   [20-23] Bitmap start block
;   [24-27] Data start block
;   [28-31] Block size (4096)
;
; Directory Entry (288 bytes):
;   [0-252]   Filename (null-terminated, 252 chars max)
;   [253]     Type (0=free, 1=file, 2=dir)
;   [254-255] Flags
;   [256-259] File size in bytes
;   [260-263] Start block (in data area)
;   [264-267] Block count
;   [268-271] Created timestamp (ticks)
;   [272-275] Modified timestamp (ticks)
;   [276-287] Reserved
;
; Block allocation bitmap:
;   8 sectors (4096 bytes) = 32768 blocks max
;   At 4KB/block = 128MB addressable
;   For 64GB, we'd need more bitmap but this is a solid start
;
;=======================================================================

hbfs_init:
        pushad

        cmp byte [ata_present], 0
        je .no_disk

        ; Try to read superblock
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov edi, hbfs_super_buf
        call ata_read_sectors
        jc .format_new

        ; Check magic
        cmp dword [hbfs_super_buf], HBFS_MAGIC
        jne .format_new

        ; Filesystem exists
        mov esi, msg_hbfs_found
        call vga_print

        ; Load root directory
        call hbfs_load_root_dir

        popad
        ret

.format_new:
        mov esi, msg_hbfs_format
        call vga_print
        call hbfs_format
        popad
        ret

.no_disk:
        mov esi, msg_hbfs_nodisk
        call vga_print
        popad
        ret

; Format: create fresh filesystem
hbfs_format:
        pushad

        ; Build superblock in buffer
        mov edi, hbfs_super_buf
        mov ecx, 512 / 4
        xor eax, eax
        rep stosd               ; Zero buffer

        mov edi, hbfs_super_buf
        mov dword [edi + 0], HBFS_MAGIC
        mov dword [edi + 4], 1                  ; Version
        mov dword [edi + 8], 32768              ; Total blocks
        mov dword [edi + 12], 32768             ; Free blocks
        mov dword [edi + 16], HBFS_ROOT_DIR_START; Root dir LBA
        mov dword [edi + 20], HBFS_BITMAP_START  ; Bitmap start LBA
        mov dword [edi + 24], HBFS_DATA_START    ; Data start LBA
        mov dword [edi + 28], HBFS_BLOCK_SIZE

        ; Write superblock
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov esi, hbfs_super_buf
        call ata_write_sectors

        ; Zero out bitmap (8 sectors)
        mov edi, hbfs_block_buf
        mov ecx, HBFS_BLOCK_SIZE / 4
        xor eax, eax
        rep stosd

        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov esi, hbfs_block_buf
        call ata_write_sectors

        ; Zero out root directory (128 sectors = 16 blocks)
        ; Use a loop to write all blocks
        mov eax, HBFS_ROOT_DIR_START
        mov ebx, HBFS_ROOT_DIR_BLOCKS
.fmt_dir_loop:
        cmp ebx, 0
        je .fmt_dir_done
        mov ecx, HBFS_SECTORS_PER_BLK
        mov esi, hbfs_block_buf
        call ata_write_sectors
        add eax, HBFS_SECTORS_PER_BLK
        dec ebx
        jmp .fmt_dir_loop
.fmt_dir_done:

        mov esi, msg_hbfs_formatted
        call vga_print

        popad
        ret

; Load current directory into memory
hbfs_load_root_dir:
        pushad
        ; Zero entire dir buffer first to clear stale data
        mov edi, hbfs_dir_buf
        mov ecx, HBFS_ROOT_DIR_SIZE / 4
        xor eax, eax
        rep stosd
        ; Now load actual directory data
        mov eax, [current_dir_lba]
        mov ecx, [current_dir_sects]
        mov edi, hbfs_dir_buf
        call ata_read_sectors
        popad
        ret

; Load bitmap from disk into hbfs_bitmap_buf
hbfs_load_bitmap:
        pushad
        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov edi, hbfs_bitmap_buf
        call ata_read_sectors
        popad
        ret

; Save current directory to disk
hbfs_save_root_dir:
        pushad
        mov eax, [current_dir_lba]
        mov ecx, [current_dir_sects]
        mov esi, hbfs_dir_buf
        call ata_write_sectors
        popad
        ret

; Get max directory entries for current directory
; Returns: EAX = number of entries that fit in current directory
hbfs_get_max_entries:
        mov eax, [current_dir_sects]
        shl eax, 9              ; sectors * 512 = bytes
        xor edx, edx
        push ecx
        mov ecx, HBFS_DIR_ENTRY_SIZE
        div ecx                 ; bytes / 288 = max entries
        pop ecx
        ret

; Find file in current directory
; ESI = filename to find
; Returns: EDI = pointer to dir entry (in hbfs_dir_buf), CF set if not found
hbfs_find_file:
        pushad
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax

.search:
        cmp byte [edi + 253], FTYPE_FREE
        je .next

        ; Compare filename
        push esi
        push edi
        push ecx
.cmp_loop:
        mov al, [esi]
        mov ah, [edi]
        cmp al, ah
        jne .no_match
        test al, al
        jz .match
        inc esi
        inc edi
        jmp .cmp_loop

.no_match:
        pop ecx
        pop edi
        pop esi
.next:
        add edi, HBFS_DIR_ENTRY_SIZE
        loop .search

        popad
        stc
        ret

.match:
        pop ecx
        pop edi                 ; EDI = dir entry start
        pop esi
        mov [esp], edi          ; Update EDI in pushad frame (EDI is at offset 0)
        popad
        clc
        ret

; Allocate N contiguous data blocks
; ECX = number of blocks needed
; Returns: EAX = first block number (relative to data start), CF set if full
hbfs_alloc_blocks:
        pushad
        mov [.blocks_needed], ecx

        ; Read bitmap
        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov edi, hbfs_bitmap_buf
        call ata_read_sectors

        ; Scan for N contiguous free bits
        xor edx, edx           ; Current block number
        mov ecx, 32768          ; Total blocks in bitmap

.scan_start:
        cmp edx, ecx
        jge .no_space
        ; Check if block EDX is free
        mov eax, edx
        shr eax, 3              ; byte index
        mov ebx, edx
        and ebx, 7              ; bit index
        bt dword [hbfs_bitmap_buf + eax], ebx
        jc .occupied

        ; This block is free — check if N contiguous blocks from here
        push edx                ; save start candidate
        mov esi, 1              ; count of free found so far
.check_run:
        cmp esi, [.blocks_needed]
        jge .found_run
        push edx
        add edx, esi
        cmp edx, ecx
        pop edx
        jge .run_fail           ; past end of bitmap

        ; Check block (start + esi)
        mov eax, edx
        add eax, esi
        push eax
        shr eax, 3
        mov ebx, edx
        add ebx, esi
        and ebx, 7
        bt dword [hbfs_bitmap_buf + eax], ebx
        pop eax
        jc .run_fail
        inc esi
        jmp .check_run

.run_fail:
        pop edx                 ; restore start candidate
        inc edx
        jmp .scan_start

.found_run:
        pop edx                 ; EDX = first free block

        ; Mark all N blocks as used in bitmap
        mov esi, 0
.mark_loop:
        cmp esi, [.blocks_needed]
        jge .mark_done
        mov eax, edx
        add eax, esi
        push eax
        shr eax, 3
        mov ebx, edx
        add ebx, esi
        and ebx, 7
        bts dword [hbfs_bitmap_buf + eax], ebx
        pop eax
        inc esi
        jmp .mark_loop
.mark_done:

        ; Write bitmap back
        push edx
        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov esi, hbfs_bitmap_buf
        call ata_write_sectors
        pop edx

        ; Update superblock free_blocks counter
        push edx
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov edi, hbfs_super_buf
        call ata_read_sectors
        mov eax, [.blocks_needed]
        sub [hbfs_super_buf + 12], eax   ; free_blocks -= allocated
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov esi, hbfs_super_buf
        call ata_write_sectors
        pop edx

        mov [esp + 28], edx    ; Return first block number via EAX
        popad
        clc
        ret

.occupied:
        inc edx
        jmp .scan_start

.no_space:
        popad
        stc
        ret

.blocks_needed: dd 0

; Allocate a single data block (convenience wrapper)
; Returns: EAX = block number, CF set if full
hbfs_alloc_block:
        push ecx
        mov ecx, 1
        call hbfs_alloc_blocks
        pop ecx
        ret

; Free N contiguous data blocks
; EAX = first block number, ECX = block count
hbfs_free_blocks:
        pushad
        mov [.free_start], eax
        mov [.free_count], ecx

        ; Read bitmap
        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov edi, hbfs_bitmap_buf
        call ata_read_sectors

        ; Clear bits for all blocks
        mov esi, 0
.free_loop:
        cmp esi, [.free_count]
        jge .free_done
        mov eax, [.free_start]
        add eax, esi
        mov ecx, eax
        shr ecx, 3              ; byte index
        and eax, 7              ; bit index
        mov edi, hbfs_bitmap_buf
        add edi, ecx
        mov cl, al
        mov ch, 1
        shl ch, cl
        not ch
        and [edi], ch
        inc esi
        jmp .free_loop
.free_done:

        ; Write back
        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov esi, hbfs_bitmap_buf
        call ata_write_sectors

        ; Update superblock free_blocks counter
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov edi, hbfs_super_buf
        call ata_read_sectors
        mov eax, [.free_count]
        add [hbfs_super_buf + 12], eax   ; free_blocks += freed
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov esi, hbfs_super_buf
        call ata_write_sectors

        popad
        ret

.free_start: dd 0
.free_count: dd 0

; Free a single data block (convenience wrapper)
; EAX = block number
hbfs_free_block:
        push ecx
        mov ecx, 1
        call hbfs_free_blocks
        pop ecx
        ret

; Create a file
; ESI = filename, ECX = size, EDI = data pointer, EDX = file type (FTYPE_*)
; Returns: CF set on error
hbfs_create_file:
        pushad
        ; Save parameters to local variables
        mov [.save_size], ecx
        mov [.save_data], edi
        mov [.save_name], esi
        mov [.save_type], edx

        ; Load directory and check if file already exists
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .not_exists
        call hbfs_delete_file_entry
.not_exists:

        ; Reload directory (may have changed after delete)
        call hbfs_load_root_dir
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax

.find_free:
        cmp byte [edi + 253], FTYPE_FREE
        je .found_slot
        add edi, HBFS_DIR_ENTRY_SIZE
        loop .find_free

        ; Directory full
        popad
        stc
        ret

.found_slot:
        ; Copy filename into directory entry (max 252 chars + null)
        push edi                ; Save entry start
        mov esi, [.save_name]
        mov ecx, HBFS_MAX_FILENAME ; 252 chars max
.copy_name:
        lodsb
        stosb
        test al, al
        jz .name_done
        dec ecx
        jnz .copy_name
        xor al, al              ; Force null terminator if name too long
        stosb
.name_done:
        pop edi                 ; Restore entry start

        ; Set type from caller's EDX
        mov al, [.save_type]
        mov byte [edi + 253], al
        mov word [edi + 254], 0         ; Flags = 0

        ; Calculate blocks needed
        mov ecx, [.save_size]
        add ecx, HBFS_BLOCK_SIZE - 1    ; round up
        shr ecx, 12                     ; / 4096 = blocks needed
        cmp ecx, 0
        jg .has_data
        mov ecx, 1                      ; minimum 1 block
.has_data:
        mov [.save_blocks], ecx

        ; Allocate contiguous blocks
        push edi
        call hbfs_alloc_blocks
        pop edi
        jc .alloc_fail

        ; EAX = first allocated block number
        mov [edi + 260], eax            ; Start block
        mov ecx, [.save_size]
        mov [edi + 256], ecx            ; File size
        mov ecx, [.save_blocks]
        mov [edi + 264], ecx            ; Block count
        mov ecx, [tick_count]
        mov [edi + 268], ecx            ; Created timestamp
        mov [edi + 272], ecx            ; Modified timestamp

        ; Save directory
        call hbfs_save_root_dir

        ; Write file data to disk
        ; LBA = HBFS_DATA_START + block_number * HBFS_SECTORS_PER_BLK
        mov ecx, [.save_blocks]
        shl eax, 3              ; first_block * 8 sectors
        add eax, HBFS_DATA_START ; + data area start = LBA
        shl ecx, 3              ; blocks * 8 = total sectors
        mov esi, [.save_data]
        call ata_write_sectors
        jc .write_fail

        popad
        clc
        ret

.alloc_fail:
.write_fail:
        popad
        stc
        ret

; Local storage for hbfs_create_file
.save_size:   dd 0
.save_data:   dd 0
.save_name:   dd 0
.save_blocks: dd 0
.save_type:   dd 0

; Delete file entry (EDI already points to entry in hbfs_dir_buf)
hbfs_delete_file_entry:
        pushad
        ; Free ALL data blocks (contiguous)
        mov eax, [edi + 260]    ; Start block
        mov ecx, [edi + 264]    ; Block count
        cmp ecx, 0
        jle .skip_free
        call hbfs_free_blocks
.skip_free:

        ; Zero the directory entry
        mov ecx, HBFS_DIR_ENTRY_SIZE / 4
        push edi
        xor eax, eax
        rep stosd
        pop edi

        call hbfs_save_root_dir
        popad
        ret

; Create a subdirectory in the current directory
; ESI = directory name (null-terminated)
; Returns: CF=0 success, CF=1 error
hbfs_mkdir:
        pushad
        mov [.hm_name], esi

        call hbfs_load_root_dir
        call hbfs_find_file
        jnc .hm_fail             ; already exists

        mov ecx, HBFS_SUBDIR_BLOCKS
        call hbfs_alloc_blocks
        jc .hm_fail
        ; EAX = first allocated block
        mov [.hm_block], eax

        ; Zero all allocated blocks on disk
        mov ebx, HBFS_SUBDIR_BLOCKS
.hm_zero:
        cmp ebx, 0
        je .hm_zero_done
        push eax
        push ebx
        mov edi, hbfs_block_buf
        mov ecx, HBFS_BLOCK_SIZE / 4
        xor eax, eax
        rep stosd
        pop ebx
        pop eax
        push eax
        push ebx
        shl eax, 3
        add eax, HBFS_DATA_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov esi, hbfs_block_buf
        call ata_write_sectors
        pop ebx
        pop eax
        inc eax
        dec ebx
        jmp .hm_zero
.hm_zero_done:
        ; Find free directory slot
        call hbfs_load_root_dir
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
.hm_find:
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .hm_slot
        add edi, HBFS_DIR_ENTRY_SIZE
        loop .hm_find
        jmp .hm_fail

.hm_slot:
        push edi
        mov esi, [.hm_name]
.hm_cpn:
        lodsb
        stosb
        test al, al
        jnz .hm_cpn
        pop edi

        mov eax, [.hm_block]
        mov byte [edi + DIRENT_TYPE], FTYPE_DIR
        mov [edi + DIRENT_START_BLOCK], eax
        mov dword [edi + DIRENT_SIZE], 0
        mov dword [edi + DIRENT_BLOCK_COUNT], HBFS_SUBDIR_BLOCKS
        mov eax, [tick_count]
        mov [edi + DIRENT_CREATED], eax
        mov [edi + DIRENT_MODIFIED], eax
        call hbfs_save_root_dir

        popad
        clc
        ret

.hm_fail:
        popad
        stc
        ret

.hm_name:  dd 0
.hm_block: dd 0

;=======================================================================
; GLOBAL FILE SEARCH
; Search current directory first, then root and all subdirectories.
; Makes directories purely organizational — any file found anywhere.
;
; ESI = filename to find (must remain valid, not modified)
; Returns: EDI = pointer to dir entry (in hbfs_dir_buf)
;          CF set if not found anywhere
; Side effect: current_dir_lba/current_dir_sects point to the directory
;              where the file was found (so hbfs_save_root_dir works)
;              .gff_moved is set to 1 if CWD was changed from original.
;              Caller MUST call gff_restore_cwd if .gff_moved == 1.
;=======================================================================
hbfs_find_file_global:
        mov dword [.gff_moved], 0

        ; First, search the current directory
        call hbfs_load_root_dir
        call hbfs_find_file
        jnc .gff_done           ; Found in current dir — done

        ; Not in current dir. Save CWD, search everywhere else.
        call gff_save_cwd

        ; Search root directory
        mov dword [current_dir_lba], HBFS_ROOT_DIR_START
        mov dword [current_dir_sects], HBFS_ROOT_DIR_SECTS
        call hbfs_load_root_dir
        call hbfs_find_file
        jnc .gff_found_elsewhere

        ; Not in root. Iterate all subdirectories in root.
        mov dword [.gff_idx], 0
.gff_scan_next:
        ; Reload root context (subdir search overwrites hbfs_dir_buf)
        mov dword [current_dir_lba], HBFS_ROOT_DIR_START
        mov dword [current_dir_sects], HBFS_ROOT_DIR_SECTS
        call hbfs_load_root_dir

        call hbfs_get_max_entries
        cmp [.gff_idx], eax
        jge .gff_not_found

        mov eax, [.gff_idx]
        imul eax, HBFS_DIR_ENTRY_SIZE
        lea edi, [hbfs_dir_buf + eax]
        inc dword [.gff_idx]

        cmp byte [edi + DIRENT_TYPE], FTYPE_DIR
        jne .gff_scan_next

        ; Switch to this subdirectory
        mov eax, [edi + DIRENT_START_BLOCK]
        shl eax, 3
        add eax, HBFS_DATA_START
        mov [current_dir_lba], eax
        mov eax, [edi + DIRENT_BLOCK_COUNT]
        shl eax, 3
        mov [current_dir_sects], eax

        ; Load subdir and search
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .gff_scan_next       ; Not here, try next subdir

.gff_found_elsewhere:
        ; Found! CWD now points to the directory containing the file.
        ; Do NOT restore CWD — caller needs it for save/delete ops.
        mov dword [.gff_moved], 1
        clc
        ret

.gff_not_found:
        ; Restore original CWD
        call gff_restore_cwd
        stc
        ret

.gff_done:
        clc
        ret

.gff_idx:   dd 0
.gff_moved: dd 0

;-----------------------------------------------------------------------
; GFF-private CWD save/restore (separate slots to avoid conflict with
; file_save_cwd / path_save_cwd used by other subsystems)
;-----------------------------------------------------------------------
gff_save_cwd:
        pushad
        mov eax, [current_dir_lba]
        mov [gff_cwd_lba], eax
        mov eax, [current_dir_sects]
        mov [gff_cwd_sects], eax
        mov eax, [dir_depth]
        mov [gff_cwd_depth], eax
        mov esi, current_dir_name
        mov edi, gff_cwd_name
        call str_copy
        mov esi, dir_stack
        mov edi, gff_cwd_stack
        mov ecx, DIR_STACK_MAX * DIR_STACK_ENTRY_SIZE
        rep movsb
        popad
        ret

gff_restore_cwd:
        pushad
        mov eax, [gff_cwd_lba]
        mov [current_dir_lba], eax
        mov eax, [gff_cwd_sects]
        mov [current_dir_sects], eax
        mov eax, [gff_cwd_depth]
        mov [dir_depth], eax
        mov esi, gff_cwd_name
        mov edi, current_dir_name
        call str_copy
        mov esi, gff_cwd_stack
        mov edi, dir_stack
        mov ecx, DIR_STACK_MAX * DIR_STACK_ENTRY_SIZE
        rep movsb
        popad
        ret

; Load file data into buffer
; ESI = filename, EDI = destination buffer
; Returns: ECX = file size, CF set if not found
hbfs_read_file:
        pushad
        mov [.save_dest], edi

        ; === Path resolution: check if filename contains '/' ===
        push esi
        mov edi, esi
.scan_slash:
        cmp byte [edi], 0
        je .no_path_slash
        cmp byte [edi], '/'
        je .has_path_slash
        inc edi
        jmp .scan_slash

.no_path_slash:
        pop esi
        jmp .read_local         ; No slash - read from current directory

.has_path_slash:
        pop esi                 ; ESI = full path string
        ; Find the LAST '/' to split into directory + basename
        push esi
        xor ebx, ebx            ; EBX will hold pointer to last '/'
.find_last_slash:
        cmp byte [esi], 0
        je .split_path
        cmp byte [esi], '/'
        jne .fls_skip
        mov ebx, esi
.fls_skip:
        inc esi
        jmp .find_last_slash

.split_path:
        pop esi                 ; ESI = start of full path
        ; Copy directory part (ESI up to EBX) into path_dir_buf
        mov edi, path_dir_buf
.cp_dir:
        cmp esi, ebx
        je .cp_dir_done
        movsb
        jmp .cp_dir
.cp_dir_done:
        ; If dir part is empty, path was "/filename" - use "/"
        cmp edi, path_dir_buf
        jne .dir_ok
        mov byte [edi], '/'
        inc edi
.dir_ok:
        mov byte [edi], 0

        ; Basename starts after the last '/'
        lea esi, [ebx + 1]
        mov edi, path_base_buf
.cp_base:
        lodsb
        stosb
        test al, al
        jnz .cp_base

        ; If basename is empty (trailing slash), fail
        cmp byte [path_base_buf], 0
        je .path_read_fail_no_restore

        ; Save CWD using file-level save (separate from PATH search save)
        call file_save_cwd

        ; cd into the directory
        mov esi, path_dir_buf
        call cmd_cd_internal
        test eax, eax
        jnz .path_read_fail

        ; Now read from the resolved directory
        call hbfs_load_root_dir
        mov esi, path_base_buf
        call hbfs_find_file
        jc .path_read_fail

        ; Found it - read file data
        movzx ebx, byte [edi + 253]
        mov [last_file_type], bl
        mov ebx, [edi + 256]    ; file size
        mov [.save_fsize], ebx
        mov eax, [edi + 260]    ; start block
        mov ecx, [edi + 264]    ; block count
        shl eax, 3
        add eax, HBFS_DATA_START
        shl ecx, 3
        mov edi, [.save_dest]
        call ata_read_sectors
        jc .path_read_fail

        ; Restore CWD
        call file_restore_cwd

        ; Return file size in ECX
        mov eax, [.save_fsize]
        mov [esp + 24], eax     ; ECX position in pushad frame
        popad
        clc
        ret

.path_read_fail:
        call file_restore_cwd
.path_read_fail_no_restore:
        popad
        stc
        ret

        ; === Local file read (no path - original behavior) ===
.read_local:
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .not_found

        ; EDI now points to the directory entry
        ; Save the file type for callers to inspect
        movzx ebx, byte [edi + 253]
        mov [last_file_type], bl
        ; Save the file size before we use EDI for reading
        mov ebx, [edi + 256]    ; File size
        mov [.save_fsize], ebx

        ; Get block number and block count, then read all data
        mov eax, [edi + 260]    ; Start block
        mov ecx, [edi + 264]    ; Block count
        shl eax, 3              ; * 8 = sector offset within data area
        add eax, HBFS_DATA_START
        shl ecx, 3              ; block_count * 8 = total sectors to read
        mov edi, [.save_dest]
        call ata_read_sectors
        jc .read_error

        ; Return file size via ECX in pushad frame
        mov eax, [.save_fsize]
        mov [esp + 24], eax     ; ECX position in pushad frame

        popad
        clc
        ret

.not_found:
        ; File not found in current directory - search ALL directories
        ; This makes directories purely organizational: any file can be
        ; accessed from any directory without specifying a path.
        mov [.save_name], esi

        call file_save_cwd

        ; Search root directory first
        mov dword [current_dir_lba], HBFS_ROOT_DIR_START
        mov dword [current_dir_sects], HBFS_ROOT_DIR_SECTS
        call hbfs_load_root_dir
        mov esi, [.save_name]
        call hbfs_find_file
        jnc .global_found

        ; Not in root - iterate each subdirectory in root
        mov dword [.global_idx], 0

.global_scan_next:
        ; Return to root context and re-load (subdir search overwrites buf)
        mov dword [current_dir_lba], HBFS_ROOT_DIR_START
        mov dword [current_dir_sects], HBFS_ROOT_DIR_SECTS
        call hbfs_load_root_dir

        ; Check if we've scanned all root entries
        call hbfs_get_max_entries
        cmp [.global_idx], eax
        jge .global_fail

        ; Get the directory entry at current index
        mov eax, [.global_idx]
        imul eax, HBFS_DIR_ENTRY_SIZE
        lea edi, [hbfs_dir_buf + eax]
        inc dword [.global_idx]

        ; Skip non-directory entries
        cmp byte [edi + DIRENT_TYPE], FTYPE_DIR
        jne .global_scan_next

        ; Enter this subdirectory
        mov eax, [edi + DIRENT_START_BLOCK]
        shl eax, 3
        add eax, HBFS_DATA_START
        mov [current_dir_lba], eax
        mov eax, [edi + DIRENT_BLOCK_COUNT]
        shl eax, 3
        mov [current_dir_sects], eax

        ; Load subdirectory and search for file
        call hbfs_load_root_dir
        mov esi, [.save_name]
        call hbfs_find_file
        jc .global_scan_next        ; Not here, try next subdir

.global_found:
        ; EDI = directory entry in hbfs_dir_buf - read the file data
        movzx ebx, byte [edi + 253]
        mov [last_file_type], bl
        mov ebx, [edi + 256]           ; file size
        mov [.save_fsize], ebx
        mov eax, [edi + 260]           ; start block
        mov ecx, [edi + 264]           ; block count
        shl eax, 3
        add eax, HBFS_DATA_START
        shl ecx, 3
        mov edi, [.save_dest]
        call ata_read_sectors
        jc .global_fail

        ; Restore original CWD and return success
        call file_restore_cwd
        mov eax, [.save_fsize]
        mov [esp + 24], eax             ; ECX position in pushad frame
        popad
        clc
        ret

.global_fail:
        call file_restore_cwd
        popad
        stc
        ret

.read_error:
        popad
        stc
        ret

.save_dest:  dd 0
.save_fsize: dd 0
.save_name:  dd 0
.global_idx: dd 0

;=======================================================================
; SYSCALL HANDLER (INT 0x80)
;
; EAX = syscall number
; Arguments in EBX, ECX, EDX, ESI, EDI
; Return value in EAX
;=======================================================================

syscall_handler:
        cmp eax, SYS_EXIT
        je sys_exit
        cmp eax, SYS_PUTCHAR
        je sys_putchar
        cmp eax, SYS_GETCHAR
        je sys_getchar
        cmp eax, SYS_PRINT
        je sys_print
        cmp eax, SYS_READ_KEY
        je sys_read_key
        cmp eax, SYS_DELETE
        je sys_delete
        cmp eax, SYS_STAT
        je sys_stat
        cmp eax, SYS_MKDIR
        je sys_mkdir
        cmp eax, SYS_READDIR
        je sys_readdir
        cmp eax, SYS_SETCURSOR
        je sys_setcursor
        cmp eax, SYS_GETTIME
        je sys_gettime
        cmp eax, SYS_SLEEP
        je sys_sleep
        cmp eax, SYS_CLEAR
        je sys_clear
        cmp eax, SYS_SETCOLOR
        je sys_setcolor
        cmp eax, SYS_MALLOC
        je sys_malloc
        cmp eax, SYS_FREE
        je sys_free
        cmp eax, SYS_EXEC
        je sys_exec_call
        cmp eax, SYS_DISK_READ
        je sys_disk_read
        cmp eax, SYS_DISK_WRITE
        je sys_disk_write
        cmp eax, SYS_BEEP
        je sys_beep
        cmp eax, SYS_DATE
        je sys_date
        cmp eax, SYS_CHDIR
        je sys_chdir
        cmp eax, SYS_GETCWD
        je sys_getcwd
        cmp eax, SYS_SERIAL
        je sys_serial
        cmp eax, SYS_GETENV
        je sys_getenv
        cmp eax, SYS_FREAD
        je sys_fread
        cmp eax, SYS_FWRITE
        je sys_fwrite
        cmp eax, SYS_OPEN
        je sys_open_fd
        cmp eax, SYS_READ
        je sys_read_fd
        cmp eax, SYS_WRITE
        je sys_write_fd
        cmp eax, SYS_CLOSE
        je sys_close_fd
        cmp eax, SYS_SEEK
        je sys_seek_fd
        cmp eax, SYS_GETARGS
        je sys_getargs
        cmp eax, SYS_SERIAL_IN
        je sys_serial_in

        ; Unknown syscall
        mov eax, -1
        iretd

sys_exit:
        ; Save program return code (EBX) for shell inspection
        mov [program_exit_code], ebx
        ; Clear running flag
        mov byte [program_running], 0
        ; Reset color so shell doesn't inherit program's color
        mov byte [vga_color], COLOR_DEFAULT
        ; Ensure interrupts are enabled for the shell
        sti
        ; Return to shell
        mov esp, KERNEL_STACK
        jmp shell_main

sys_putchar:
        push ebx
        mov al, bl
        call vga_putchar
        pop ebx
        xor eax, eax
        iretd

sys_getchar:
        push ebx
        call kb_getchar
        movzx eax, al
        pop ebx
        iretd

sys_print:
        push esi
        mov esi, ebx
        call vga_print
        pop esi
        xor eax, eax
        iretd

sys_read_key:
        push ebx
        call kb_pollchar
        movzx eax, al
        pop ebx
        iretd

sys_clear:
        call vga_clear
        xor eax, eax
        iretd

sys_setcolor:
        mov [vga_color], bl
        xor eax, eax
        iretd

sys_setcursor:
        push edx
        mov eax, ebx            ; X
        mov edx, ecx            ; Y
        call vga_set_cursor
        pop edx
        xor eax, eax
        iretd

sys_gettime:
        mov eax, [tick_count]
        iretd

sys_sleep:
        ; EBX = ticks to sleep (don't modify caller's EBX)
        push ebx
        mov eax, [tick_count]
        add ebx, eax
.wait:
        hlt                     ; Wait for interrupt
        cmp [tick_count], ebx
        jl .wait
        pop ebx
        xor eax, eax
        iretd

sys_exec_call:
        ; EBX = pointer to filename
        ; Note: on success, cmd_exec_program never returns (iretd to ring 3)
        ; On failure (program not found), it returns with CF set
        push esi
        mov esi, ebx
        call cmd_exec_program
        pop esi
        jc .exec_fail
        xor eax, eax
        iretd
.exec_fail:
        mov eax, -1
        iretd

;---------------------------------------
; SYS_DELETE (9): Delete a file
; EBX = pointer to filename string
; Returns: EAX = 0 on success, -1 on error
;---------------------------------------
sys_delete:
        push esi
        push edi
        mov esi, ebx
        call hbfs_find_file_global
        jc .del_not_found
        call hbfs_delete_file_entry
        ; Restore CWD if global search changed it
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .del_no_restore
        call gff_restore_cwd
.del_no_restore:
        pop edi
        pop esi
        xor eax, eax
        iretd
.del_not_found:
        pop edi
        pop esi
        mov eax, -1
        iretd

;---------------------------------------
; SYS_STAT (11): Get file information
; EBX = pointer to filename string
; Returns: EAX = file size (-1 if not found)
;          ECX = block count
;---------------------------------------
sys_stat:
        push esi
        push edi
        mov esi, ebx
        call hbfs_find_file_global
        jc .stat_not_found
        mov eax, [edi + 256]    ; File size
        mov ecx, [edi + 264]    ; Block count
        ; Restore CWD if global search changed it
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .stat_no_restore
        call gff_restore_cwd
.stat_no_restore:
        pop edi
        pop esi
        iretd
.stat_not_found:
        pop edi
        pop esi
        mov eax, -1
        xor ecx, ecx
        iretd

;---------------------------------------
; SYS_MALLOC (19): Allocate 4KB-aligned memory pages
; EBX = size in bytes (rounded up to 4KB pages)
; Returns: EAX = physical address (0 on failure)
;---------------------------------------
sys_malloc:
        push ecx
        ; Round up to pages
        mov eax, ebx
        add eax, 4095
        shr eax, 12             ; / 4096 = pages needed
        mov ecx, eax            ; ECX = page count
        xor eax, eax
        cmp ecx, 0
        je .malloc_done
        ; Allocate pages from PMM
        call pmm_alloc_pages
.malloc_done:
        pop ecx
        iretd

;---------------------------------------
; SYS_FREE (20): Free allocated memory pages
; EBX = physical address (must be page-aligned)
; ECX = size in bytes (rounded up to 4KB pages)
; Returns: EAX = 0
;---------------------------------------
sys_free:
        push ecx
        push eax
        mov eax, ecx
        add eax, 4095
        shr eax, 12             ; pages
        mov ecx, eax
        mov eax, ebx            ; physical address
        and eax, 0xFFFFF000     ; align to page boundary
        cmp ecx, 0
        je .free_done
.free_loop:
        call pmm_free_page
        add eax, 0x1000         ; advance to next page
        loop .free_loop
.free_done:
        pop eax
        pop ecx
        xor eax, eax
        iretd

;---------------------------------------
; SYS_DISK_READ (22): Raw disk read
; EBX = LBA (low 32 bits), ECX = sector count, EDX = dest buffer
; Returns: EAX = 0 on success, -1 on error
;---------------------------------------
sys_disk_read:
        ; Deny raw disk access from user programs (ring 3)
        cmp byte [program_running], 1
        je .dread_denied
        push edi
        mov eax, ebx
        mov edi, edx
        call ata_read_sectors
        pop edi
        jc .dread_err
        xor eax, eax
        iretd
.dread_denied:
.dread_err:
        mov eax, -1
        iretd

;---------------------------------------
; SYS_DISK_WRITE (23): Raw disk write
; EBX = LBA (low 32 bits), ECX = sector count, EDX = source buffer
; Returns: EAX = 0 on success, -1 on error
;---------------------------------------
sys_disk_write:
        ; Deny raw disk access from user programs (ring 3)
        cmp byte [program_running], 1
        je .dwrite_denied
        push esi
        mov eax, ebx
        mov esi, edx
        call ata_write_sectors
        pop esi
        jc .dwrite_err
        xor eax, eax
        iretd
.dwrite_denied:
.dwrite_err:
        mov eax, -1
        iretd

;=======================================================================
; COMMAND SHELL
;=======================================================================

shell_main:
        mov esp, KERNEL_STACK
        mov byte [vga_color], COLOR_DEFAULT

        ; Set default PATH
        mov esi, default_path
        call env_set_str

.prompt:
        ; Print prompt with full current directory path
        mov byte [vga_color], COLOR_PROMPT
        mov esi, shell_prompt_pre
        call vga_print
        ; Build full CWD path
        mov edi, path_search_buf        ; reuse temp buffer
        call build_cwd_path
        mov esi, path_search_buf
        call vga_print
        mov esi, shell_prompt_post
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT

        ; Reset history browsing position
        mov eax, [hist_count]
        mov [hist_browse], eax

        ; Read command line
        mov edi, line_buffer
        xor ecx, ecx           ; Character count

.read_loop:
        call kb_getchar

        cmp al, 0x0D            ; Enter?
        je .execute
        cmp al, 0x0A            ; Linefeed?
        je .execute
        cmp al, 0x08            ; Backspace?
        je .backspace
        cmp al, 0x7F            ; Delete (also backspace on some terms)?
        je .backspace
        cmp al, KEY_UP          ; Up arrow - history prev
        je .hist_up
        cmp al, KEY_DOWN        ; Down arrow - history next
        je .hist_down
        cmp al, TAB_KEY         ; Tab - auto-complete
        je .tab_complete
        cmp al, CTRL_C_CODE     ; Ctrl+C - cancel
        je .ctrl_c

        ; Store character if buffer not full
        cmp ecx, LINE_BUFFER_SIZE - 1
        jge .read_loop
        stosb
        inc ecx
        ; Echo character
        call vga_putchar
        jmp .read_loop

.backspace:
        or ecx, ecx
        jz .read_loop
        dec edi
        dec ecx
        mov al, 0x08
        call vga_putchar
        jmp .read_loop

.hist_up:
        cmp dword [hist_browse], 0
        je .read_loop           ; No more history
        dec dword [hist_browse]
        jmp .hist_recall

.hist_down:
        mov eax, [hist_browse]
        cmp eax, [hist_count]
        jge .read_loop          ; Already at end
        inc dword [hist_browse]
        ; If at end, clear line
        mov eax, [hist_browse]
        cmp eax, [hist_count]
        je .hist_clear_line
        jmp .hist_recall

.tab_complete:
        call shell_tab_complete
        lea edi, [line_buffer + ecx]
        jmp .read_loop

.ctrl_c:
        ; Print ^C and cancel current input
        push esi
        mov esi, msg_ctrl_c
        call vga_print
        pop esi
        mov edi, line_buffer
        xor ecx, ecx
        jmp .prompt

.hist_clear_line:
        ; Erase current input on screen
        call .erase_input
        mov edi, line_buffer
        xor ecx, ecx
        jmp .read_loop

.hist_recall:
        ; Erase current input on screen
        call .erase_input
        ; Copy history entry to line_buffer
        mov eax, [hist_browse]
        shl eax, 8              ; * 256 (HIST_ENTRY_SIZE)
        lea esi, [hist_buf + eax]
        mov edi, line_buffer
        xor ecx, ecx
.hist_copy:
        lodsb
        cmp al, 0
        je .hist_copy_done
        stosb
        inc ecx
        push ecx
        call vga_putchar        ; Echo the char
        pop ecx
        jmp .hist_copy
.hist_copy_done:
        jmp .read_loop

.erase_input:
        ; Backspace over ECX characters
        push ecx
.erase_loop:
        cmp ecx, 0
        je .erase_done
        push ecx
        mov al, 0x08
        call vga_putchar
        mov al, ' '
        call vga_putchar
        mov al, 0x08
        call vga_putchar
        pop ecx
        dec ecx
        jmp .erase_loop
.erase_done:
        pop ecx
        ret

.execute:
        mov byte [edi], 0      ; Null-terminate input
        mov al, 0x0A
        call vga_putchar

        ; Skip empty lines
        cmp ecx, 0
        je .prompt

        ; Save to history
        call shell_save_history

        ; Parse and execute command
        mov esi, line_buffer
        call shell_parse_cmd
        jmp .prompt

;---------------------------------------
; Save current line_buffer to history ring
;---------------------------------------
HIST_MAX        equ 8
HIST_ENTRY_SIZE equ 256

shell_save_history:
        pushad
        ; Shift history up if full
        mov eax, [hist_count]
        cmp eax, HIST_MAX
        jl .hist_not_full
        ; Shift entries 1..7 down to 0..6
        mov esi, hist_buf + HIST_ENTRY_SIZE
        mov edi, hist_buf
        mov ecx, (HIST_MAX - 1) * HIST_ENTRY_SIZE / 4
        rep movsd
        dec dword [hist_count]
.hist_not_full:
        ; Copy line_buffer to hist_buf[hist_count]
        mov eax, [hist_count]
        shl eax, 8              ; * 256
        lea edi, [hist_buf + eax]
        mov esi, line_buffer
        mov ecx, HIST_ENTRY_SIZE - 1
.copy_hist:
        lodsb
        stosb
        cmp al, 0
        je .copy_done
        loop .copy_hist
        mov byte [edi], 0       ; Ensure null termination
.copy_done:
        inc dword [hist_count]
        popad
        ret

;---------------------------------------
; Parse and dispatch command
; ESI = command line
;---------------------------------------
shell_parse_cmd:
        ; Skip leading spaces
        call skip_spaces

        cmp byte [esi], 0
        je .done

        ; --- Alias expansion ---
        ; Extract first word, check alias table
        push esi
        mov ecx, [alias_count]
        cmp ecx, 0
        je .alias_skip
        xor ebx, ebx
        mov edx, alias_table
.alias_lookup:
        cmp ebx, ecx
        jge .alias_skip
        ; Check if command starts with alias name
        push esi
        mov edi, edx
        call str_starts_with
        pop esi
        jc .alias_found
        add edx, ALIAS_ENTRY_SIZE
        inc ebx
        jmp .alias_lookup
.alias_found:
        ; Copy alias command into alias_expand_buf, then append rest of args
        push esi          ; ESI now points past the alias name
        lea esi, [edx + ALIAS_NAME_LEN]
        mov edi, alias_expand_buf
        mov ecx, 255
.aexp_copy_cmd:
        lodsb
        cmp al, 0
        je .aexp_cmd_done
        stosb
        dec ecx
        jnz .aexp_copy_cmd
.aexp_cmd_done:
        pop esi
        ; Append remaining arguments
.aexp_copy_rest:
        lodsb
        stosb
        cmp al, 0
        je .aexp_done
        dec ecx
        jnz .aexp_copy_rest
.aexp_done:
        mov byte [edi], 0
        pop esi            ; discard saved ESI
        mov esi, alias_expand_buf
        jmp shell_parse_cmd  ; Re-parse with expanded command
.alias_skip:
        pop esi

        ; Compare against known commands
        mov edi, cmd_help_str
        call str_starts_with
        jc .cmd_help

        mov edi, cmd_ver_str
        call str_starts_with
        jc .cmd_ver

        mov edi, cmd_clear_str
        call str_starts_with
        jc .cmd_clear

        mov edi, cmd_dir_str
        call str_starts_with
        jc .cmd_dir

        mov edi, cmd_ls_str
        call str_starts_with
        jc .cmd_dir

        mov edi, cmd_del_str
        call str_starts_with
        jc .cmd_del

        mov edi, cmd_rm_str
        call str_starts_with
        jc .cmd_del

        mov edi, cmd_format_str
        call str_starts_with
        jc .cmd_format

        mov edi, cmd_cat_str
        call str_starts_with
        jc .cmd_cat

        mov edi, cmd_write_str
        call str_starts_with
        jc .cmd_write

        mov edi, cmd_hex_str
        call str_starts_with
        jc .cmd_hexdump

        mov edi, cmd_mem_str
        call str_starts_with
        jc .cmd_mem

        mov edi, cmd_time_str
        call str_starts_with
        jc .cmd_time

        mov edi, cmd_disk_str
        call str_starts_with
        jc .cmd_diskinfo

        mov edi, cmd_run_str
        call str_starts_with
        jc .cmd_run

        mov edi, cmd_enter_str
        call str_starts_with
        jc .cmd_enter

        mov edi, cmd_copy_str
        call str_starts_with
        jc .cmd_copy

        mov edi, cmd_ren_str
        call str_starts_with
        jc .cmd_rename

        mov edi, cmd_mv_str
        call str_starts_with
        jc .cmd_rename

        mov edi, cmd_move_str
        call str_starts_with
        jc .cmd_rename

        mov edi, cmd_df_str
        call str_starts_with
        jc .cmd_df

        mov edi, cmd_more_str
        call str_starts_with
        jc .cmd_more

        mov edi, cmd_echo_str
        call str_starts_with
        jc .cmd_echo

        mov edi, cmd_wc_str
        call str_starts_with
        jc .cmd_wc

        mov edi, cmd_find_str
        call str_starts_with
        jc .cmd_find

        mov edi, cmd_append_str
        call str_starts_with
        jc .cmd_append

        mov edi, cmd_date_str
        call str_starts_with
        jc .cmd_date

        mov edi, cmd_beep_str
        call str_starts_with
        jc .cmd_beep

        mov edi, cmd_batch_str
        call str_starts_with
        jc .cmd_batch

        mov edi, cmd_mkdir_str
        call str_starts_with
        jc .cmd_mkdir

        mov edi, cmd_cd_str
        call str_starts_with
        jc .cmd_cd

        mov edi, cmd_pwd_str
        call str_starts_with
        jc .cmd_pwd

        mov edi, cmd_touch_str
        call str_starts_with
        jc .cmd_touch

        mov edi, cmd_set_str
        call str_starts_with
        jc .cmd_set

        mov edi, cmd_unset_str
        call str_starts_with
        jc .cmd_unset

        mov edi, cmd_shutdown_str
        call str_starts_with
        jc .cmd_shutdown

        mov edi, cmd_cls_str
        call str_starts_with
        jc .cmd_clear

        mov edi, cmd_head_str
        call str_starts_with
        jc .cmd_head

        mov edi, cmd_tail_str
        call str_starts_with
        jc .cmd_tail

        mov edi, cmd_type_str
        call str_starts_with
        jc .cmd_cat

        mov edi, cmd_diff_str
        call str_starts_with
        jc .cmd_diff

        mov edi, cmd_uniq_str
        call str_starts_with
        jc .cmd_uniq

        mov edi, cmd_rev_str
        call str_starts_with
        jc .cmd_rev

        mov edi, cmd_tac_str
        call str_starts_with
        jc .cmd_tac

        mov edi, cmd_alias_str
        call str_starts_with
        jc .cmd_alias

        mov edi, cmd_history_str
        call str_starts_with
        jc .cmd_history

        mov edi, cmd_which_str
        call str_starts_with
        jc .cmd_which

        mov edi, cmd_sleep_str
        call str_starts_with
        jc .cmd_sleep

        mov edi, cmd_color_str
        call str_starts_with
        jc .cmd_color

        mov edi, cmd_size_str
        call str_starts_with
        jc .cmd_size

        mov edi, cmd_strings_str
        call str_starts_with
        jc .cmd_strings

        ; Unknown command - try to execute as program
        mov esi, line_buffer
        call cmd_exec_program
        jc .unknown

.done:
        ret

.unknown:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_unknown_cmd
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        ret

;---- Command implementations ----

.cmd_help:
        mov esi, help_text
        call vga_print
        ret

.cmd_ver:
        mov byte [vga_color], COLOR_INFO
        mov esi, version_text
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT

        ; Print memory info
        mov esi, msg_mem_total
        call vga_print
        mov eax, [total_free_pages]
        shl eax, 2              ; Pages * 4 = KB
        push eax
        shr eax, 10             ; KB / 1024 = MB
        call vga_print_dec
        mov esi, msg_mb
        call vga_print
        pop eax
        ret

.cmd_clear:
        call vga_clear
        ret

.cmd_dir:
        call parse_flags
        call cmd_list_dir
        ret

.cmd_del:
        call skip_spaces
        call parse_flags
        call cmd_delete_file
        ret

.cmd_format:
        mov esi, msg_format_confirm
        call vga_print
        call kb_getchar
        or al, 0x20             ; Convert to lowercase
        cmp al, 'y'
        jne .format_cancel
        call hbfs_format
        ret
.format_cancel:
        mov esi, msg_cancelled
        call vga_print
        ret

.cmd_cat:
        call skip_spaces
        call parse_flags
        call cmd_cat_file
        ret

.cmd_write:
        call skip_spaces
        call cmd_write_file
        ret

.cmd_hexdump:
        call skip_spaces
        call parse_flags
        call cmd_hexdump_file
        ret

.cmd_mem:
        call cmd_show_memory
        ret

.cmd_time:
        mov esi, msg_uptime
        call vga_print
        mov eax, [tick_count]
        xor edx, edx
        mov ebx, PIT_HZ
        div ebx                 ; EAX = seconds
        call vga_print_dec
        mov esi, msg_seconds
        call vga_print
        ret

.cmd_diskinfo:
        call cmd_show_disk
        ret

.cmd_run:
        call skip_spaces
        call cmd_exec_program
        ret

.cmd_enter:
        call cmd_enter_hex
        ret

.cmd_copy:
        call skip_spaces
        call parse_flags
        call cmd_copy_file
        ret

.cmd_rename:
        call skip_spaces
        call cmd_rename_file
        ret

.cmd_df:
        call cmd_df_info
        ret

.cmd_more:
        call skip_spaces
        call parse_flags
        call cmd_more_file
        ret

.cmd_echo:
        call parse_flags
        call cmd_echo
        ret

.cmd_wc:
        call skip_spaces
        call parse_flags
        call cmd_wc_file
        ret

.cmd_find:
        call skip_spaces
        call parse_flags
        call cmd_find_file
        ret

.cmd_append:
        call skip_spaces
        call cmd_append_file
        ret

.cmd_date:
        call cmd_date_show
        ret

.cmd_beep:
        call cmd_beep
        ret

.cmd_batch:
        call skip_spaces
        call cmd_exec_batch
        ret

.cmd_mkdir:
        call skip_spaces
        call cmd_mkdir
        ret

.cmd_cd:
        call skip_spaces
        call cmd_cd
        ret

.cmd_pwd:
        call cmd_pwd
        ret

.cmd_touch:
        call skip_spaces
        call cmd_touch_file
        ret

.cmd_set:
        call cmd_set_env
        ret

.cmd_unset:
        call skip_spaces
        call cmd_unset_env
        ret

.cmd_shutdown:
        call cmd_shutdown
        ret

.cmd_head:
        call skip_spaces
        call parse_flags
        call cmd_head_file
        ret

.cmd_tail:
        call skip_spaces
        call parse_flags
        call cmd_tail_file
        ret

.cmd_diff:
        call skip_spaces
        call cmd_diff_files
        ret

.cmd_uniq:
        call skip_spaces
        call parse_flags
        call cmd_uniq_file
        ret

.cmd_rev:
        call skip_spaces
        call cmd_rev_file
        ret

.cmd_tac:
        call skip_spaces
        call cmd_tac_file
        ret

.cmd_alias:
        call skip_spaces
        call cmd_alias
        ret

.cmd_history:
        call cmd_history
        ret

.cmd_which:
        call skip_spaces
        call cmd_which
        ret

.cmd_sleep:
        call skip_spaces
        call cmd_sleep
        ret

.cmd_color:
        call skip_spaces
        call cmd_color
        ret

.cmd_size:
        call skip_spaces
        call cmd_size_file
        ret

.cmd_strings:
        call skip_spaces
        call parse_flags
        call cmd_strings_file
        ret

;---------------------------------------
; Command: diff - Compare two files line by line
; Usage: diff FILE1 FILE2
;---------------------------------------
cmd_diff_files:
        pushad
        ; Parse two filenames
        mov edi, filename_buf
        call copy_word
        call skip_spaces
        cmp byte [filename_buf], 0
        je .diff_usage
        mov edi, filename_buf2
        call copy_word
        cmp byte [filename_buf2], 0
        je .diff_usage

        ; Read file1 into PROGRAM_BASE
        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .diff_not_found1
        mov [.diff_size1], ecx

        ; Read file2 into PROGRAM_BASE + 64K
        mov esi, filename_buf2
        mov edi, PROGRAM_BASE + 0x10000
        call hbfs_read_file
        jc .diff_not_found2
        mov [.diff_size2], ecx

        ; Null-terminate both
        mov edi, PROGRAM_BASE
        add edi, [.diff_size1]
        mov byte [edi], 0
        mov edi, PROGRAM_BASE + 0x10000
        add edi, [.diff_size2]
        mov byte [edi], 0

        ; Print headers
        mov byte [vga_color], COLOR_INFO
        mov esi, msg_diff_header1
        call vga_print
        mov esi, filename_buf
        call vga_print
        call vga_newline
        mov esi, msg_diff_header2
        call vga_print
        mov esi, filename_buf2
        call vga_print
        call vga_newline
        mov byte [vga_color], COLOR_DEFAULT

        ; Compare line by line
        mov esi, PROGRAM_BASE
        mov edi, PROGRAM_BASE + 0x10000
        mov dword [.diff_line], 1
        mov dword [.diff_diffs], 0

.diff_loop:
        cmp byte [esi], 0
        jne .diff_not_end
        cmp byte [edi], 0
        jne .diff_f2_extra
        jmp .diff_done

.diff_not_end:
        cmp byte [edi], 0
        je .diff_f1_extra

        ; Compare current lines
        push esi
        push edi
        call .diff_cmp_line
        jc .diff_lines_same

        ; Lines differ
        inc dword [.diff_diffs]
        mov byte [vga_color], COLOR_INFO
        mov esi, msg_diff_line
        call vga_print
        mov eax, [.diff_line]
        call vga_print_dec
        call vga_newline
        ; Print file1 line
        mov byte [vga_color], COLOR_ERROR
        pop edi
        pop esi
        push esi
        push edi
        mov al, '<'
        call vga_putchar
        mov al, ' '
        call vga_putchar
        mov ebx, esi
.diff_p1:
        mov al, [ebx]
        cmp al, 0
        je .diff_p1e
        cmp al, 0x0A
        je .diff_p1e
        call vga_putchar
        inc ebx
        jmp .diff_p1
.diff_p1e:
        call vga_newline
        ; Print file2 line
        mov byte [vga_color], COLOR_SUCCESS
        pop edi
        pop esi
        push esi
        push edi
        mov al, '>'
        call vga_putchar
        mov al, ' '
        call vga_putchar
        mov ebx, edi
.diff_p2:
        mov al, [ebx]
        cmp al, 0
        je .diff_p2e
        cmp al, 0x0A
        je .diff_p2e
        call vga_putchar
        inc ebx
        jmp .diff_p2
.diff_p2e:
        call vga_newline
        mov byte [vga_color], COLOR_DEFAULT

.diff_lines_same:
        pop edi
        pop esi
        ; Advance both to next line
.diff_skip1:
        cmp byte [esi], 0
        je .diff_sk1d
        cmp byte [esi], 0x0A
        je .diff_sk1n
        inc esi
        jmp .diff_skip1
.diff_sk1n:
        inc esi
.diff_sk1d:
.diff_skip2:
        cmp byte [edi], 0
        je .diff_sk2d
        cmp byte [edi], 0x0A
        je .diff_sk2n
        inc edi
        jmp .diff_skip2
.diff_sk2n:
        inc edi
.diff_sk2d:
        inc dword [.diff_line]
        jmp .diff_loop

.diff_f1_extra:
        inc dword [.diff_diffs]
        mov byte [vga_color], COLOR_ERROR
.diff_f1e_loop:
        mov al, [esi]
        cmp al, 0
        je .diff_done
        call vga_putchar
        inc esi
        jmp .diff_f1e_loop

.diff_f2_extra:
        inc dword [.diff_diffs]
        mov byte [vga_color], COLOR_SUCCESS
.diff_f2e_loop:
        mov al, [edi]
        cmp al, 0
        je .diff_done
        call vga_putchar
        inc edi
        jmp .diff_f2e_loop

.diff_done:
        mov byte [vga_color], COLOR_DEFAULT
        cmp dword [.diff_diffs], 0
        jne .diff_ret
        mov esi, msg_diff_same
        call vga_print
.diff_ret:
        popad
        ret

.diff_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_2files
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.diff_not_found1:
        mov byte [vga_color], COLOR_ERROR
        mov esi, filename_buf
        call vga_print
        mov esi, msg_not_found_w
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.diff_not_found2:
        mov byte [vga_color], COLOR_ERROR
        mov esi, filename_buf2
        call vga_print
        mov esi, msg_not_found_w
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

; Compare line at ESI vs line at EDI. CF=1 if same.
.diff_cmp_line:
        push esi
        push edi
.dcl_loop:
        mov al, [esi]
        mov ah, [edi]
        cmp al, 0x0A
        je .dcl_end1
        cmp al, 0
        je .dcl_end1
        cmp ah, 0x0A
        je .dcl_diff
        cmp ah, 0
        je .dcl_diff
        cmp al, ah
        jne .dcl_diff
        inc esi
        inc edi
        jmp .dcl_loop
.dcl_end1:
        cmp ah, 0x0A
        je .dcl_same
        cmp ah, 0
        je .dcl_same
.dcl_diff:
        pop edi
        pop esi
        clc
        ret
.dcl_same:
        pop edi
        pop esi
        stc
        ret

.diff_size1:  dd 0
.diff_size2:  dd 0
.diff_line:   dd 0
.diff_diffs:  dd 0

;---------------------------------------
; Command: uniq - Remove adjacent duplicate lines
; Flags: -c  prefix lines with count
;        -d  only show duplicates
;---------------------------------------
cmd_uniq_file:
        pushad
        mov edi, filename_buf
        call copy_word
        cmp byte [filename_buf], 0
        je .uniq_usage

        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .uniq_nf
        mov edi, PROGRAM_BASE
        add edi, ecx
        mov byte [edi], 0

        mov esi, PROGRAM_BASE
        mov dword [.uniq_prev], 0
        mov dword [.uniq_count], 1

.uniq_loop:
        cmp byte [esi], 0
        je .uniq_flush
        mov [.uniq_cur], esi

        cmp dword [.uniq_prev], 0
        je .uniq_save_prev

        ; Compare current with previous
        push esi
        push edi
        mov edi, [.uniq_prev]
        call cmd_diff_files.diff_cmp_line
        pop edi
        pop esi
        jc .uniq_dup

        ; Lines differ - flush previous
        call .uniq_emit
        mov dword [.uniq_count], 1
        jmp .uniq_save_prev

.uniq_dup:
        inc dword [.uniq_count]
        jmp .uniq_advance

.uniq_save_prev:
        mov eax, [.uniq_cur]
        mov [.uniq_prev], eax
.uniq_advance:
        ; Skip to next line
.uniq_skip:
        cmp byte [esi], 0
        je .uniq_flush
        cmp byte [esi], 0x0A
        je .uniq_skipn
        inc esi
        jmp .uniq_skip
.uniq_skipn:
        inc esi
        jmp .uniq_loop

.uniq_flush:
        cmp dword [.uniq_prev], 0
        je .uniq_done
        call .uniq_emit
.uniq_done:
        popad
        ret

.uniq_emit:
        pushad
        ; Check -d flag (bit 3)
        mov eax, 3
        call test_flag
        jnc .uniq_no_d
        cmp dword [.uniq_count], 1
        je .uniq_emit_skip

.uniq_no_d:
        ; Check -c flag (bit 2)
        mov eax, 2
        call test_flag
        jnc .uniq_no_c
        mov byte [vga_color], COLOR_INFO
        mov eax, [.uniq_count]
        call vga_print_dec
        mov al, ' '
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
.uniq_no_c:
        mov esi, [.uniq_prev]
.uniq_pl:
        mov al, [esi]
        cmp al, 0
        je .uniq_pnl
        cmp al, 0x0A
        je .uniq_pnl
        call vga_putchar
        inc esi
        jmp .uniq_pl
.uniq_pnl:
        call vga_newline
.uniq_emit_skip:
        popad
        ret

.uniq_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.uniq_nf:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_file_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

.uniq_prev:   dd 0
.uniq_cur:    dd 0
.uniq_count:  dd 0

;---------------------------------------
; Command: rev - Reverse each line of a file
;---------------------------------------
cmd_rev_file:
        pushad
        mov edi, filename_buf
        call copy_word
        cmp byte [filename_buf], 0
        je .rev_usage

        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .rev_nf
        mov edi, PROGRAM_BASE
        add edi, ecx
        mov byte [edi], 0

        mov esi, PROGRAM_BASE
.rev_loop:
        cmp byte [esi], 0
        je .rev_done
        ; Find end of line
        mov edi, esi
.rev_find_eol:
        cmp byte [edi], 0
        je .rev_eol
        cmp byte [edi], 0x0A
        je .rev_eol
        inc edi
        jmp .rev_find_eol
.rev_eol:
        mov ebx, edi
        cmp edi, esi
        je .rev_empty_line
        dec edi
.rev_print_rev:
        mov al, [edi]
        call vga_putchar
        cmp edi, esi
        je .rev_next
        dec edi
        jmp .rev_print_rev
.rev_empty_line:
.rev_next:
        call vga_newline
        mov esi, ebx
        cmp byte [esi], 0x0A
        jne .rev_done
        inc esi
        jmp .rev_loop
.rev_done:
        popad
        ret
.rev_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.rev_nf:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_file_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; Command: tac - Print file lines in reverse order
;---------------------------------------
cmd_tac_file:
        pushad
        mov edi, filename_buf
        call copy_word
        cmp byte [filename_buf], 0
        je .tac_usage

        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .tac_nf
        mov edi, PROGRAM_BASE
        add edi, ecx
        mov byte [edi], 0

        ; Build line pointer table at PROGRAM_BASE + 0x10000
        mov esi, PROGRAM_BASE
        mov edi, PROGRAM_BASE + 0x10000
        mov dword [.tac_lines], 0
        mov [edi], esi
        add edi, 4
        inc dword [.tac_lines]

.tac_scan:
        cmp byte [esi], 0
        je .tac_print
        cmp byte [esi], 0x0A
        jne .tac_next_ch
        inc esi
        cmp byte [esi], 0
        je .tac_print
        mov [edi], esi
        add edi, 4
        inc dword [.tac_lines]
        jmp .tac_scan
.tac_next_ch:
        inc esi
        jmp .tac_scan

.tac_print:
        mov ecx, [.tac_lines]
        cmp ecx, 0
        je .tac_done
.tac_print_loop:
        dec ecx
        mov esi, PROGRAM_BASE + 0x10000
        mov esi, [esi + ecx * 4]
.tac_pline:
        mov al, [esi]
        cmp al, 0
        je .tac_pnl
        cmp al, 0x0A
        je .tac_pnl
        call vga_putchar
        inc esi
        jmp .tac_pline
.tac_pnl:
        call vga_newline
        cmp ecx, 0
        jne .tac_print_loop
.tac_done:
        popad
        ret
.tac_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.tac_nf:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_file_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.tac_lines: dd 0

;---------------------------------------
; Command: alias - Define or list command aliases
; Usage: alias              (list all)
;        alias NAME CMD     (define alias)
;---------------------------------------
cmd_alias:
        pushad
        cmp byte [esi], 0
        je .alias_list

        ; Parse alias name
        mov edi, filename_buf
        call copy_word
        call skip_spaces
        cmp byte [esi], 0
        je .alias_show_one

        ; Find existing or empty slot
        mov ecx, [alias_count]
        xor ebx, ebx
        mov edx, alias_table
.alias_find:
        cmp ebx, ecx
        jge .alias_new_slot
        push esi
        mov esi, filename_buf
        mov edi, edx
        call str_compare
        pop esi
        je .alias_update
        add edx, ALIAS_ENTRY_SIZE
        inc ebx
        jmp .alias_find

.alias_new_slot:
        cmp ecx, ALIAS_MAX
        jge .alias_full
        mov eax, ecx
        imul eax, ALIAS_ENTRY_SIZE
        lea edx, [alias_table + eax]
        inc dword [alias_count]

.alias_update:
        push esi
        mov esi, filename_buf
        mov edi, edx
        mov ecx, ALIAS_NAME_LEN - 1
.alias_cpn:
        lodsb
        stosb
        cmp al, 0
        je .alias_nd
        dec ecx
        jnz .alias_cpn
        mov byte [edi], 0
.alias_nd:
        pop esi
        lea edi, [edx + ALIAS_NAME_LEN]
        mov ecx, ALIAS_CMD_LEN - 1
.alias_cpc:
        lodsb
        stosb
        cmp al, 0
        je .alias_cd
        dec ecx
        jnz .alias_cpc
        mov byte [edi], 0
.alias_cd:
        mov esi, msg_alias_set
        call vga_print
        popad
        ret

.alias_list:
        mov ecx, [alias_count]
        cmp ecx, 0
        je .alias_none
        xor ebx, ebx
        mov edx, alias_table
.alias_ll:
        cmp ebx, ecx
        jge .alias_ld
        mov byte [vga_color], COLOR_INFO
        mov esi, edx
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        mov al, '='
        call vga_putchar
        lea esi, [edx + ALIAS_NAME_LEN]
        call vga_print
        call vga_newline
        add edx, ALIAS_ENTRY_SIZE
        inc ebx
        jmp .alias_ll
.alias_ld:
        popad
        ret

.alias_none:
        mov esi, msg_no_aliases
        call vga_print
        popad
        ret

.alias_show_one:
        mov ecx, [alias_count]
        xor ebx, ebx
        mov edx, alias_table
.alias_sf:
        cmp ebx, ecx
        jge .alias_snf
        push esi
        mov esi, filename_buf
        mov edi, edx
        call str_compare
        pop esi
        je .alias_sp
        add edx, ALIAS_ENTRY_SIZE
        inc ebx
        jmp .alias_sf
.alias_sp:
        lea esi, [edx + ALIAS_NAME_LEN]
        call vga_print
        call vga_newline
        popad
        ret
.alias_snf:
        mov esi, filename_buf
        call vga_print
        mov esi, msg_not_found_w
        call vga_print
        popad
        ret

.alias_full:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_table_full
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; Command: history - Show command history
;---------------------------------------
cmd_history:
        pushad
        mov ecx, [hist_count]
        cmp ecx, 0
        je .hist_empty
        xor ebx, ebx
.hist_loop:
        cmp ebx, ecx
        jge .hist_done
        mov byte [vga_color], COLOR_INFO
        mov eax, ebx
        inc eax
        call vga_print_dec
        mov byte [vga_color], COLOR_DEFAULT
        mov al, ' '
        call vga_putchar
        mov eax, ebx
        imul eax, HIST_ENTRY_SIZE
        lea esi, [hist_buf + eax]
        call vga_print
        call vga_newline
        inc ebx
        jmp .hist_loop
.hist_done:
        popad
        ret
.hist_empty:
        mov esi, msg_no_history
        call vga_print
        popad
        ret

;---------------------------------------
; Command: which - Show if a name is built-in or external
;---------------------------------------
cmd_which:
        pushad
        mov edi, filename_buf
        call copy_word
        cmp byte [filename_buf], 0
        je .which_usage

        ; Check against a table of built-in command names
        mov ebx, .which_builtins
.which_check:
        mov edi, [ebx]
        cmp edi, 0
        je .which_try_fs
        mov esi, filename_buf
        call str_compare
        je .which_builtin
        add ebx, 4
        jmp .which_check

.which_try_fs:
        ; First check current directory
        call hbfs_load_root_dir
        mov esi, filename_buf
        call hbfs_find_file
        jnc .which_found_cwd

        ; Search PATH directories
        call path_save_cwd
        mov esi, path_env_name
        mov edi, path_search_buf
        call env_get_var
        jc .which_path_done

        mov esi, path_search_buf
.which_path_next:
        cmp byte [esi], 0
        je .which_path_done
        mov edi, temp_path_buf
.which_pcopy:
        lodsb
        cmp al, ':'
        je .which_pready
        cmp al, 0
        je .which_plast
        stosb
        jmp .which_pcopy
.which_plast:
        dec esi
.which_pready:
        mov byte [edi], 0
        push esi
        call path_restore_cwd

        mov esi, temp_path_buf
        call cmd_cd_internal
        test eax, eax
        jnz .which_pskip

        call hbfs_load_root_dir
        mov esi, filename_buf
        call hbfs_find_file
        jc .which_pskip

        ; Found in this PATH directory
        call path_restore_cwd
        pop esi
        ; Print: "name is /dir/name (external)"
        mov esi, filename_buf
        call vga_print
        mov esi, msg_is_str
        call vga_print
        mov esi, temp_path_buf
        call vga_print
        mov al, '/'
        call vga_putchar
        mov esi, filename_buf
        call vga_print
        mov esi, msg_external
        call vga_print
        popad
        ret

.which_pskip:
        pop esi
        jmp .which_path_next

.which_path_done:
        call path_restore_cwd
        ; Not found anywhere
        mov esi, filename_buf
        call vga_print
        mov esi, msg_not_found_w
        call vga_print
        popad
        ret

.which_found_cwd:
        ; Show as ./name (external)
        mov esi, filename_buf
        call vga_print
        mov esi, msg_is_str
        call vga_print
        ; Show cwd path
        mov edi, temp_path_buf
        call build_cwd_path
        mov esi, temp_path_buf
        call vga_print
        mov al, '/'
        call vga_putchar
        mov esi, filename_buf
        call vga_print
        mov esi, msg_external
        call vga_print
        popad
        ret

.which_builtin:
        mov esi, filename_buf
        call vga_print
        mov esi, msg_builtin
        call vga_print
        popad
        ret

.which_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

; Table of built-in command name pointers (null-terminated)
.which_builtins:
        dd cmd_help_str, cmd_ver_str, cmd_clear_str, cmd_dir_str
        dd cmd_ls_str, cmd_del_str, cmd_rm_str, cmd_format_str
        dd cmd_cat_str, cmd_write_str, cmd_hex_str, cmd_mem_str
        dd cmd_time_str, cmd_disk_str, cmd_run_str, cmd_enter_str
        dd cmd_copy_str, cmd_ren_str, cmd_mv_str, cmd_move_str
        dd cmd_df_str, cmd_more_str, cmd_echo_str, cmd_wc_str
        dd cmd_find_str, cmd_append_str, cmd_date_str, cmd_beep_str
        dd cmd_batch_str, cmd_mkdir_str, cmd_cd_str, cmd_pwd_str
        dd cmd_touch_str, cmd_set_str, cmd_unset_str, cmd_shutdown_str
        dd cmd_cls_str, cmd_head_str, cmd_tail_str, cmd_type_str
        dd cmd_diff_str, cmd_uniq_str, cmd_rev_str, cmd_tac_str
        dd cmd_alias_str, cmd_history_str, cmd_which_str
        dd cmd_sleep_str, cmd_color_str, cmd_size_str, cmd_strings_str
        dd 0

;---------------------------------------
; Command: sleep - Sleep for N seconds
;---------------------------------------
cmd_sleep:
        pushad
        xor eax, eax
.sleep_parse:
        movzx ecx, byte [esi]
        cmp cl, '0'
        jb .sleep_do
        cmp cl, '9'
        ja .sleep_do
        imul eax, 10
        sub cl, '0'
        add eax, ecx
        inc esi
        jmp .sleep_parse

.sleep_do:
        cmp eax, 0
        je .sleep_usage
        push eax
        mov esi, msg_sleeping
        call vga_print
        mov eax, [esp]
        call vga_print_dec
        mov esi, msg_sleep_sec
        call vga_print
        pop eax
        imul eax, 100
        mov ebx, [tick_count]
        add ebx, eax
.sleep_wait:
        hlt
        cmp byte [ctrl_c_flag], 0
        jne .sleep_abort
        mov eax, [tick_count]
        cmp eax, ebx
        jl .sleep_wait
        popad
        ret

.sleep_abort:
        mov byte [ctrl_c_flag], 0
        popad
        ret

.sleep_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_sleep_usage
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; Command: color - Set terminal foreground/background color
; Usage: color FG [BG]  (hex values 0-F)
;---------------------------------------
cmd_color:
        pushad
        cmp byte [esi], 0
        je .color_usage

        movzx eax, byte [esi]
        call .color_hex_val
        jc .color_usage
        mov ebx, eax
        inc esi

        call skip_spaces
        cmp byte [esi], 0
        je .color_set

        movzx eax, byte [esi]
        call .color_hex_val
        jc .color_usage
        shl eax, 4
        or ebx, eax

.color_set:
        mov [vga_color], bl
        mov esi, msg_color_set
        call vga_print
        popad
        ret

.color_usage:
        mov esi, msg_color_usage
        call vga_print
        popad
        ret

.color_hex_val:
        cmp al, '0'
        jb .chv_fail
        cmp al, '9'
        jbe .chv_digit
        or al, 0x20
        cmp al, 'a'
        jb .chv_fail
        cmp al, 'f'
        ja .chv_fail
        sub al, 'a' - 10
        clc
        ret
.chv_digit:
        sub al, '0'
        clc
        ret
.chv_fail:
        stc
        ret

;---------------------------------------
; Command: size - Show file size and type
;---------------------------------------
cmd_size_file:
        pushad
        mov edi, filename_buf
        call copy_word
        cmp byte [filename_buf], 0
        je .size_usage

        mov esi, filename_buf
        call hbfs_find_file_global
        jc .size_nf

        mov byte [vga_color], COLOR_INFO
        mov esi, filename_buf
        call vga_print
        mov esi, msg_file_colon
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT

        mov eax, [edi + DIRENT_SIZE]
        call vga_print_dec
        mov esi, msg_size_bytes
        call vga_print

        mov eax, [edi + DIRENT_BLOCK_COUNT]
        call vga_print_dec
        mov esi, msg_size_blocks
        call vga_print

        movzx eax, byte [edi + DIRENT_TYPE]
        cmp al, FTYPE_TEXT
        je .size_text
        cmp al, FTYPE_DIR
        je .size_dir
        cmp al, FTYPE_EXEC
        je .size_exec
        cmp al, FTYPE_BATCH
        je .size_batch
        mov esi, msg_type_unknown
        jmp .size_ptype
.size_text:
        mov esi, msg_type_text
        jmp .size_ptype
.size_dir:
        mov esi, msg_type_dir
        jmp .size_ptype
.size_exec:
        mov esi, msg_type_exec
        jmp .size_ptype
.size_batch:
        mov esi, msg_type_batch
.size_ptype:
        call vga_print
        ; Restore CWD if global search changed it
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .size_no_restore
        call gff_restore_cwd
.size_no_restore:
        popad
        ret

.size_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.size_nf:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_file_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; Command: strings - Extract printable strings from a file
; Flags: -n N  minimum string length (default 4)
;---------------------------------------
cmd_strings_file:
        pushad
        mov edi, filename_buf
        call copy_word
        cmp byte [filename_buf], 0
        je .strings_usage

        mov dword [strings_min_len], 4
        mov eax, [cmd_flag_num]
        cmp eax, 0
        je .strings_read
        mov [strings_min_len], eax

.strings_read:
        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .strings_nf
        mov [.strings_total], ecx

        mov esi, PROGRAM_BASE
        xor ebx, ebx            ; run length
        mov edx, esi             ; run start
.strings_scan:
        cmp ecx, 0
        je .strings_flush
        movzx eax, byte [esi]
        cmp al, 0x20
        jb .strings_check_ws
        cmp al, 0x7E
        ja .strings_not_print
        cmp ebx, 0
        jne .strings_cont
        mov edx, esi
.strings_cont:
        inc ebx
        inc esi
        dec ecx
        jmp .strings_scan

.strings_check_ws:
        cmp al, 9
        je .strings_ws
        cmp al, 10
        je .strings_ws
        cmp al, 13
        je .strings_ws
        jmp .strings_not_print
.strings_ws:
        cmp ebx, 0
        jne .strings_cont
        mov edx, esi
        jmp .strings_cont

.strings_not_print:
        call .strings_flush_run
        inc esi
        dec ecx
        jmp .strings_scan

.strings_flush:
        call .strings_flush_run
        popad
        ret

.strings_flush_run:
        cmp ebx, [strings_min_len]
        jb .strings_reset
        push esi
        push ecx
        mov esi, edx
        mov ecx, ebx
.strings_ploop:
        mov al, [esi]
        call vga_putchar
        inc esi
        dec ecx
        jnz .strings_ploop
        call vga_newline
        pop ecx
        pop esi
.strings_reset:
        xor ebx, ebx
        ret

.strings_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.strings_nf:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_file_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

.strings_total: dd 0

;---------------------------------------
; Command: dir/ls - List directory
; Flags: -l  long format (type, size, blocks, name)
;        -s  sort by size (largest first) — not yet, just reserved
;---------------------------------------
cmd_list_dir:
        pushad
        call hbfs_load_root_dir

        ; Check for -l flag (bit 11 = 'l' - 'a')
        mov eax, 11             ; 'l' - 'a'
        call test_flag
        jc .long_format

        ; Short format: just filenames in columns
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
        xor ebx, ebx           ; File counter
        xor edx, edx           ; Column counter

.short_loop:
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .short_next

        inc ebx

        ; Set color by type
        cmp byte [edi + DIRENT_TYPE], FTYPE_DIR
        je .sc_dir
        cmp byte [edi + DIRENT_TYPE], FTYPE_EXEC
        je .sc_exec
        cmp byte [edi + DIRENT_TYPE], FTYPE_BATCH
        je .sc_batch
        mov byte [vga_color], COLOR_DEFAULT
        jmp .sc_print
.sc_dir:
        mov byte [vga_color], COLOR_INFO
        jmp .sc_print
.sc_exec:
        mov byte [vga_color], COLOR_EXEC
        jmp .sc_print
.sc_batch:
        mov byte [vga_color], COLOR_BATCH
.sc_print:
        ; Print filename padded to 16 chars
        push esi
        push ecx
        mov esi, edi
        xor ecx, ecx
.sc_name:
        lodsb
        test al, al
        jz .sc_pad
        call vga_putchar
        inc ecx
        jmp .sc_name
.sc_pad:
        cmp ecx, 16
        jge .sc_col
        mov al, ' '
        call vga_putchar
        inc ecx
        jmp .sc_pad
.sc_col:
        pop ecx
        pop esi
        mov byte [vga_color], COLOR_DEFAULT
        inc edx
        cmp edx, 4             ; 4 columns
        jl .short_next
        mov al, 0x0A
        call vga_putchar
        xor edx, edx

.short_next:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jnz .short_loop

        ; Final newline if not already at start of line
        test edx, edx
        jz .short_summary
        mov al, 0x0A
        call vga_putchar
.short_summary:
        mov esi, msg_dir_count
        call vga_print
        mov eax, ebx
        call vga_print_dec
        mov esi, msg_dir_files
        call vga_print
        popad
        ret

.long_format:
        mov byte [vga_color], COLOR_HEADER
        mov esi, dir_header
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        mov esi, dir_separator
        call vga_print

        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
        xor ebx, ebx           ; File counter

.loop:
        cmp byte [edi + 253], FTYPE_FREE
        je .next

        inc ebx

        ; Print type indicator
        cmp byte [edi + 253], FTYPE_DIR
        je .type_dir
        cmp byte [edi + 253], FTYPE_EXEC
        je .type_exec
        cmp byte [edi + 253], FTYPE_BATCH
        je .type_batch
        ; Default: text file
        mov byte [vga_color], COLOR_DEFAULT
        mov esi, type_text
        call vga_print
        jmp .print_size
.type_dir:
        mov byte [vga_color], COLOR_INFO
        mov esi, type_dir
        call vga_print
        jmp .print_size
.type_exec:
        mov byte [vga_color], COLOR_EXEC
        mov esi, type_exec
        call vga_print
        jmp .print_size
.type_batch:
        mov byte [vga_color], COLOR_BATCH
        mov esi, type_batch
        call vga_print

.print_size:
        mov byte [vga_color], COLOR_DEFAULT
        mov al, ' '
        call vga_putchar

        ; Print file size right-aligned in 9-char field
        mov eax, [edi + 256]
        mov edx, 9
        call vga_print_dec_width

        mov al, ' '
        call vga_putchar
        call vga_putchar

        ; Print filename
        push esi
        mov esi, edi
        call vga_print
        pop esi

        mov byte [vga_color], COLOR_DEFAULT
        mov al, 0x0A
        call vga_putchar

.next:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jnz .loop

        ; Print summary
        mov esi, msg_dir_count
        call vga_print
        mov eax, ebx
        call vga_print_dec
        mov esi, msg_dir_files
        call vga_print

        popad
        ret

;---------------------------------------
; Command: del/rm - Delete file
; Flags: -v  verbose (print each deleted filename)
;---------------------------------------
cmd_delete_file:
        pushad
        ; Parse filename/pattern argument
        mov edi, filename_buf
        call copy_word
        cmp byte [filename_buf], 0
        je .not_found

        ; Wildcard mode?
        mov esi, filename_buf
        call str_has_wildcards
        jc .wildcard

        ; Exact filename delete
        mov esi, filename_buf
        call hbfs_find_file_global
        jc .not_found
        ; -v: print name before delete
        mov eax, 21            ; 'v' - 'a'
        call test_flag
        jnc .del_exact_quiet
        push esi
        mov esi, msg_del_prefix
        call vga_print
        mov esi, edi
        call vga_print
        mov al, 0x0A
        call vga_putchar
        pop esi
.del_exact_quiet:
        call hbfs_delete_file_entry
        ; If CWD was changed by global search, restore it
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .del_no_restore
        call gff_restore_cwd
.del_no_restore:
        mov esi, msg_deleted
        call vga_print
        popad
        ret

.wildcard:
        call hbfs_load_root_dir
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
        xor ebx, ebx                    ; match/delete count

.wc_loop:
        cmp ecx, 0
        je .wc_done
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .wc_next

        push ecx
        push edi
        mov esi, filename_buf           ; pattern
        call wildcard_match
        pop edi
        pop ecx
        jnc .wc_next

        ; -v: print name before delete
        mov eax, 21            ; 'v' - 'a'
        call test_flag
        jnc .wc_del_quiet
        push esi
        mov esi, msg_del_prefix
        call vga_print
        mov esi, edi
        call vga_print
        mov al, 0x0A
        call vga_putchar
        pop esi
.wc_del_quiet:
        call hbfs_delete_file_entry
        inc ebx

.wc_next:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jmp .wc_loop

.wc_done:
        cmp ebx, 0
        je .not_found
        mov esi, msg_deleted
        call vga_print
        popad
        ret

.not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; Command: shutdown - Cleanly power off
;---------------------------------------
cmd_shutdown:
        ; Print shutdown banner (consistent with boot banner style)
        mov al, 0x0A
        call vga_putchar
        mov byte [vga_color], COLOR_HEADER
        mov esi, msg_shutdown_bar
        call vga_print
        mov byte [vga_color], COLOR_INFO
        mov esi, msg_shutdown
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT

        ; Small delay so user can see the message
        mov ecx, 0x3FFFFFF
.delay:
        dec ecx
        jnz .delay

        ; Print halt message before attempting shutdown
        mov esi, msg_halt
        call vga_print

        ; Another small delay so user can read the message
        mov ecx, 0x1FFFFFF
.delay2:
        dec ecx
        jnz .delay2

        ; Try ACPI shutdown (works on QEMU and Bochs)
        ; QEMU PIIX4 PM: port 0x604, value 0x2000 = S5 (power off)
        cli
        mov dx, 0x604
        mov ax, 0x2000
        out dx, ax

        ; If ACPI didn't work, halt the CPU
.halt_loop:
        hlt
        jmp .halt_loop

;---------------------------------------
; Command: cat - Display file contents
;---------------------------------------
;---------------------------------------
; Command: head - Show first N lines of a file
; Usage: head [N] filename   or   head -n N filename
;        (default N=10)
;---------------------------------------
cmd_head_file:
        pushad
        ; Check for -n flag first (set by parse_flags)
        mov dword [head_lines], 10
        cmp dword [cmd_flag_num], 0
        je .head_no_flag_num
        mov eax, [cmd_flag_num]
        mov [head_lines], eax
        jmp .head_no_num
.head_no_flag_num:
        cmp byte [esi], 0
        je .head_usage
        ; Check if first arg is a number (legacy positional syntax)
        cmp byte [esi], '0'
        jl .head_no_num
        cmp byte [esi], '9'
        jg .head_no_num
        ; Parse number
        xor eax, eax
.head_parse_num:
        movzx ebx, byte [esi]
        cmp bl, '0'
        jl .head_num_done
        cmp bl, '9'
        jg .head_num_done
        imul eax, 10
        sub bl, '0'
        add eax, ebx
        inc esi
        jmp .head_parse_num
.head_num_done:
        mov [head_lines], eax
        call skip_spaces
.head_no_num:
        ; ESI now points to filename
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .head_not_found

        cmp ecx, PROGRAM_MAX_SIZE - 1
        jle .head_size_ok
        mov ecx, PROGRAM_MAX_SIZE - 1
.head_size_ok:
        mov byte [PROGRAM_BASE + ecx], 0

        mov esi, PROGRAM_BASE
        xor ecx, ecx            ; line counter
.head_loop:
        lodsb
        cmp al, 0
        je .head_done
        call vga_putchar
        cmp al, 0x0A
        jne .head_loop
        inc ecx
        cmp ecx, [head_lines]
        jl .head_loop
.head_done:
        popad
        ret
.head_not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.head_usage:
        mov esi, msg_head_usage
        call vga_print
        popad
        ret

;---------------------------------------
; Command: tail - Show last N lines of a file
; Usage: tail [N] filename   or   tail -n N filename
;        (default N=10)
;---------------------------------------
cmd_tail_file:
        pushad
        ; Check for -n flag first (set by parse_flags)
        mov dword [head_lines], 10
        cmp dword [cmd_flag_num], 0
        je .tail_no_flag_num
        mov eax, [cmd_flag_num]
        mov [head_lines], eax
        jmp .tail_no_num
.tail_no_flag_num:
        cmp byte [esi], 0
        je .tail_usage
        ; Check if first arg is a number (legacy positional syntax)
        cmp byte [esi], '0'
        jl .tail_no_num
        cmp byte [esi], '9'
        jg .tail_no_num
        xor eax, eax
.tail_parse_num:
        movzx ebx, byte [esi]
        cmp bl, '0'
        jl .tail_num_done
        cmp bl, '9'
        jg .tail_num_done
        imul eax, 10
        sub bl, '0'
        add eax, ebx
        inc esi
        jmp .tail_parse_num
.tail_num_done:
        mov [head_lines], eax
        call skip_spaces
.tail_no_num:
        ; Read file
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .tail_not_found

        cmp ecx, PROGRAM_MAX_SIZE - 1
        jle .tail_size_ok
        mov ecx, PROGRAM_MAX_SIZE - 1
.tail_size_ok:
        mov byte [PROGRAM_BASE + ecx], 0

        ; Count total lines first
        mov esi, PROGRAM_BASE
        xor ebx, ebx            ; total line count
.tail_count:
        lodsb
        cmp al, 0
        je .tail_counted
        cmp al, 0x0A
        jne .tail_count
        inc ebx
        jmp .tail_count
.tail_counted:
        ; Calculate skip count: total - N
        mov eax, ebx
        sub eax, [head_lines]
        jle .tail_print_all      ; N >= total, print everything
        mov ecx, eax             ; lines to skip

        ; Skip past first ecx lines
        mov esi, PROGRAM_BASE
.tail_skip:
        lodsb
        cmp al, 0
        je .tail_print_done
        cmp al, 0x0A
        jne .tail_skip
        dec ecx
        jnz .tail_skip
        jmp .tail_print

.tail_print_all:
        mov esi, PROGRAM_BASE
.tail_print:
        lodsb
        cmp al, 0
        je .tail_print_done
        call vga_putchar
        jmp .tail_print
.tail_print_done:
        mov al, 0x0A
        call vga_putchar
        popad
        ret
.tail_not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.tail_usage:
        mov esi, msg_tail_usage
        call vga_print
        popad
        ret

;---------------------------------------
; Command: cat - Display file contents
; Flags: -n  show line numbers
;---------------------------------------
cmd_cat_file:
        pushad
        mov edi, PROGRAM_BASE   ; Use program area as temp buffer
        call hbfs_read_file
        jc .not_found

        ; ECX = file size from hbfs_read_file
        ; Clamp to buffer size to prevent overflow
        cmp ecx, PROGRAM_MAX_SIZE - 1
        jle .cat_size_ok
        mov ecx, PROGRAM_MAX_SIZE - 1
.cat_size_ok:
        ; Null-terminate the content at the file size boundary
        mov byte [PROGRAM_BASE + ecx], 0

        ; Check for -n flag (line numbers)
        mov eax, 13             ; 'n' - 'a'
        call test_flag
        jc .cat_numbered

        ; Plain output
        mov esi, PROGRAM_BASE
        call vga_print
        mov al, 0x0A
        call vga_putchar
        popad
        ret

.cat_numbered:
        mov esi, PROGRAM_BASE
        xor ebx, ebx           ; line counter
.cat_n_line:
        cmp byte [esi], 0
        je .cat_n_done
        inc ebx
        ; Print line number right-aligned in 4-char field
        push esi
        mov byte [vga_color], COLOR_INFO
        mov eax, ebx
        mov edx, 4
        call vga_print_dec_width
        mov al, ' '
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
        pop esi
        ; Print chars until newline or end
.cat_n_char:
        lodsb
        cmp al, 0
        je .cat_n_done
        call vga_putchar
        cmp al, 0x0A
        jne .cat_n_char
        jmp .cat_n_line
.cat_n_done:
        mov al, 0x0A
        call vga_putchar
        popad
        ret

.not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; Command: more - Page-by-page file viewer
; Flags: -n N  lines per page (default 23)
;---------------------------------------
PAGER_LINES equ 23

cmd_more_file:
        pushad
        ; Set page size from -n flag or default
        mov dword [.more_page], PAGER_LINES
        cmp dword [cmd_flag_num], 0
        je .more_default_page
        mov eax, [cmd_flag_num]
        mov [.more_page], eax
.more_default_page:
        mov edi, PROGRAM_BASE   ; Use program area as temp buffer
        call hbfs_read_file
        jc .more_not_found

        ; ECX = file size
        cmp ecx, PROGRAM_MAX_SIZE - 1
        jle .more_size_ok
        mov ecx, PROGRAM_MAX_SIZE - 1
.more_size_ok:
        mov byte [PROGRAM_BASE + ecx], 0

        mov esi, PROGRAM_BASE
        xor ecx, ecx            ; Line counter

.more_loop:
        lodsb
        cmp al, 0
        je .more_done

        call vga_putchar

        cmp al, 0x0A             ; Newline?
        jne .more_loop
        inc ecx
        cmp ecx, [.more_page]
        jl .more_loop

        ; Show pager prompt
        push esi
        mov byte [vga_color], COLOR_INFO
        mov esi, msg_more_prompt
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        pop esi

        ; Wait for keypress
        call kb_getchar
        cmp al, 27              ; ESC
        je .more_done
        cmp al, 'q'
        je .more_done

        ; Erase the prompt line
        mov al, 0x0D             ; CR
        call vga_putchar
        push esi
        mov esi, msg_more_erase
        call vga_print
        pop esi
        mov al, 0x0D
        call vga_putchar

        xor ecx, ecx            ; Reset line counter
        jmp .more_loop

.more_done:
        mov al, 0x0A
        call vga_putchar
        popad
        ret

.more_not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.more_page: dd PAGER_LINES

;---------------------------------------
; Command: write - Write text to file
; Usage: write filename
; Then type content, end with empty line
;---------------------------------------
cmd_write_file:
        pushad

        ; ESI points to filename - copy it
        mov edi, filename_buf
        call copy_word           ; Copy filename to filename_buf

        ; Now read lines of content
        mov edi, PROGRAM_BASE   ; Write content here
        xor ebx, ebx           ; Byte counter

        mov esi, msg_write_prompt
        call vga_print

.read_content:
        ; Read a line
        push edi
        push ebx
        mov edi, temp_line_buf
        xor ecx, ecx

.read_char:
        call kb_getchar
        cmp al, 0x0D
        je .end_line
        cmp al, 0x0A
        je .end_line
        cmp al, 0x08
        je .bs
        mov [edi], al
        inc edi
        inc ecx
        call vga_putchar
        jmp .read_char

.bs:
        or ecx, ecx
        jz .read_char
        dec edi
        dec ecx
        mov al, 0x08
        call vga_putchar
        jmp .read_char

.end_line:
        mov byte [edi], 0
        mov al, 0x0A
        call vga_putchar

        pop ebx
        pop edi

        ; Check for empty line (end of input)
        cmp ecx, 0
        je .save_file

        ; Copy line to content buffer
        mov esi, temp_line_buf
.copy_line:
        lodsb
        mov [edi + ebx], al
        inc ebx
        test al, al
        jnz .copy_line
        ; Replace null with newline
        dec ebx
        mov byte [edi + ebx], 0x0A
        inc ebx
        jmp .read_content

.save_file:
        ; Null-terminate content
        mov byte [PROGRAM_BASE + ebx], 0

        ; Create file - detect type by extension
        mov esi, filename_buf
        mov edx, FTYPE_TEXT     ; Default to text
        call detect_file_type   ; Sets EDX based on filename in ESI
        mov ecx, ebx            ; Size
        mov edi, PROGRAM_BASE   ; Data
        call hbfs_create_file
        jc .write_error

        mov esi, msg_saved
        call vga_print
        popad
        ret

.write_error:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_write_err
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; Command: touch - Create empty file
;---------------------------------------
cmd_touch_file:
        pushad
        mov edi, filename_buf
        call copy_word
        cmp byte [filename_buf], 0
        je .touch_usage

        ; Create empty file
        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        mov ecx, 0              ; Empty file
        mov edx, FTYPE_TEXT     ; Default to text
        call detect_file_type
        call hbfs_create_file
        jc .touch_err
        mov esi, msg_saved
        call vga_print
        jmp .touch_done

.touch_usage:
        mov esi, msg_touch_usage
        call vga_print
        jmp .touch_done

.touch_err:
        mov esi, msg_write_err
        call vga_print

.touch_done:
        popad
        ret

;---------------------------------------
; Command: hex - Hexdump a file
; Flags: -n N  limit to N bytes (default 512)
;---------------------------------------
cmd_hexdump_file:
        pushad
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .not_found

        ; ECX = file size returned by hbfs_read_file
        ; Check for -n flag limit
        mov eax, 512            ; default limit
        cmp dword [cmd_flag_num], 0
        je .hex_default_limit
        mov eax, [cmd_flag_num]
.hex_default_limit:
        cmp ecx, eax
        jle .size_ok
        mov ecx, eax
.size_ok:
        mov esi, PROGRAM_BASE
        mov edx, 0              ; Offset counter

.hex_line:
        cmp edx, ecx
        jge .hex_done

        ; Print offset
        mov eax, edx
        call vga_print_hex
        mov al, ':'
        call vga_putchar
        mov al, ' '
        call vga_putchar

        ; Print 16 hex bytes
        push ecx
        push edx
        mov ebx, 16
.hex_byte:
        cmp edx, ecx
        jge .hex_pad
        movzx eax, byte [esi + edx]
        push eax
        shr eax, 4
        call .print_nibble
        pop eax
        and eax, 0x0F
        call .print_nibble
        mov al, ' '
        call vga_putchar
        inc edx
        dec ebx
        jnz .hex_byte
        jmp .hex_ascii

.hex_pad:
        ; Pad remaining with spaces
        mov al, ' '
        call vga_putchar
        call vga_putchar
        call vga_putchar
        dec ebx
        jnz .hex_pad

.hex_ascii:
        ; Print ASCII representation
        mov al, '|'
        call vga_putchar
        pop edx
        push edx
        mov ebx, 16
.ascii_byte:
        cmp edx, [esp + 4]     ; Compare with ecx on stack
        jge .ascii_done
        movzx eax, byte [esi + edx]
        cmp al, 0x20
        jl .non_printable
        cmp al, 0x7E
        jg .non_printable
        call vga_putchar
        jmp .ascii_next
.non_printable:
        mov al, '.'
        call vga_putchar
.ascii_next:
        inc edx
        dec ebx
        jnz .ascii_byte
.ascii_done:
        mov al, '|'
        call vga_putchar
        mov al, 0x0A
        call vga_putchar

        pop edx
        pop ecx
        jmp .hex_line

.print_nibble:
        cmp al, 10
        jl .pn_digit
        add al, 'A' - 10
        call vga_putchar
        ret
.pn_digit:
        add al, '0'
        call vga_putchar
        ret

.hex_done:
        popad
        ret

.not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; Command: mem - Show memory information
;---------------------------------------
cmd_show_memory:
        pushad
        mov byte [vga_color], COLOR_INFO
        mov esi, msg_mem_header
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT

        call pmm_count_free
        mov [total_free_pages], eax

        mov esi, msg_free_pages
        call vga_print
        mov eax, [total_free_pages]
        call vga_print_dec
        mov esi, msg_pages_suffix
        call vga_print

        mov esi, msg_free_mem
        call vga_print
        mov eax, [total_free_pages]
        shl eax, 2              ; * 4KB
        shr eax, 10             ; / 1024 = MB
        call vga_print_dec
        mov esi, msg_mb
        call vga_print

        mov esi, msg_tick_count
        call vga_print
        mov eax, [tick_count]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar

        popad
        ret

;---------------------------------------
; Command: disk - Show disk information
;---------------------------------------
cmd_show_disk:
        pushad
        cmp byte [ata_present], 0
        je .no_disk

        mov byte [vga_color], COLOR_INFO
        mov esi, msg_disk_header
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT

        mov esi, msg_disk_sectors
        call vga_print
        mov eax, [ata_total_sectors]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar

        mov esi, msg_disk_size
        call vga_print
        mov eax, [ata_total_sectors]
        shr eax, 11             ; / 2048 = MB
        call vga_print_dec
        mov esi, msg_mb
        call vga_print

        popad
        ret

.no_disk:
        mov esi, msg_ata_none
        call vga_print
        popad
        ret

;---------------------------------------
; Command: df - Show HBFS filesystem usage
;---------------------------------------
cmd_df_info:
        pushad
        cmp byte [ata_present], 0
        je .df_no_disk

        mov byte [vga_color], COLOR_INFO
        mov esi, msg_df_header
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT

        ; Count total data blocks available on disk
        ; Total data sectors = ata_total_sectors - HBFS_DATA_START
        mov eax, [ata_total_sectors]
        sub eax, HBFS_DATA_START
        shr eax, 3              ; / 8 sectors per block = total blocks
        mov ebx, eax            ; EBX = total blocks
        push ebx

        ; Count used blocks from bitmap
        call hbfs_load_bitmap
        mov ebp, ebx            ; total blocks
        add ebp, 7
        shr ebp, 3              ; bitmap bytes = ceil(total_blocks / 8)
        xor ecx, ecx            ; ECX = used blocks counter
        xor esi, esi            ; ESI = byte index
        mov edi, hbfs_bitmap_buf

.df_count:
        cmp esi, ebp            ; Scan all bitmap bytes for data blocks
        jge .df_counted
        movzx eax, byte [edi + esi]
        ; Count set bits in AL
        xor edx, edx
.df_bits:
        cmp edx, 8
        jge .df_next_byte
        bt eax, edx
        jnc .df_bit_clear
        inc ecx
.df_bit_clear:
        inc edx
        jmp .df_bits
.df_next_byte:
        inc esi
        jmp .df_count

.df_counted:
        ; ECX = used blocks, [esp] = total blocks
        pop ebx                 ; EBX = total blocks
        ; Clamp used to total
        cmp ecx, ebx
        jle .df_clamp_ok
        mov ecx, ebx
.df_clamp_ok:
        push ecx
        push ebx

        ; Print total blocks
        mov esi, msg_df_total
        call vga_print
        mov eax, ebx
        call vga_print_dec
        mov esi, msg_df_blocks
        call vga_print

        ; Print used blocks
        mov esi, msg_df_used
        call vga_print
        pop ebx                 ; total
        pop ecx                 ; used
        push ecx
        push ebx
        mov eax, ecx
        call vga_print_dec
        mov esi, msg_df_blocks
        call vga_print

        ; Print free blocks
        mov esi, msg_df_free
        call vga_print
        pop ebx                 ; total
        pop ecx                 ; used
        mov eax, ebx
        sub eax, ecx
        push eax
        call vga_print_dec
        mov esi, msg_df_blocks
        call vga_print

        ; Print free space in KB
        mov esi, msg_df_avail
        call vga_print
        pop eax                 ; free blocks
        shl eax, 2              ; * 4 = KB
        call vga_print_dec
        mov esi, msg_df_kb
        call vga_print

        ; Count files across ALL directories
        call path_save_cwd               ; Save user's CWD
        mov dword [.df_total_files], 0
        mov dword [.df_total_slots], 0
        mov dword [.df_dir_count], 0

        ; Count files in root directory
        mov dword [current_dir_lba], HBFS_ROOT_DIR_START
        mov dword [current_dir_sects], HBFS_ROOT_DIR_SECTS
        call hbfs_load_root_dir
        call .df_count_dir               ; ECX = files in root, EAX = max entries
        add [.df_total_files], ecx
        add [.df_total_slots], eax

        ; Iterate root entries looking for subdirectories
        mov dword [.df_scan_idx], 0
.df_scan_subdirs:
        ; Reload root (subdir scan overwrites hbfs_dir_buf)
        mov dword [current_dir_lba], HBFS_ROOT_DIR_START
        mov dword [current_dir_sects], HBFS_ROOT_DIR_SECTS
        call hbfs_load_root_dir
        call hbfs_get_max_entries
        cmp [.df_scan_idx], eax
        jge .df_all_counted

        mov eax, [.df_scan_idx]
        imul eax, HBFS_DIR_ENTRY_SIZE
        lea edi, [hbfs_dir_buf + eax]
        inc dword [.df_scan_idx]

        cmp byte [edi + DIRENT_TYPE], FTYPE_DIR
        jne .df_scan_subdirs

        ; Switch into this subdirectory
        mov eax, [edi + DIRENT_START_BLOCK]
        shl eax, 3
        add eax, HBFS_DATA_START
        mov [current_dir_lba], eax
        mov eax, [edi + DIRENT_BLOCK_COUNT]
        shl eax, 3
        mov [current_dir_sects], eax

        call hbfs_load_root_dir
        call .df_count_dir
        add [.df_total_files], ecx
        add [.df_total_slots], eax
        inc dword [.df_dir_count]
        jmp .df_scan_subdirs

.df_all_counted:
        call path_restore_cwd            ; Restore user's CWD

        mov esi, msg_df_files
        call vga_print
        mov eax, [.df_total_files]
        call vga_print_dec
        mov esi, msg_df_in
        call vga_print
        mov eax, [.df_dir_count]
        inc eax                          ; +1 for root
        call vga_print_dec
        mov esi, msg_df_dirs
        call vga_print

        popad
        ret

; Helper: count files in currently loaded directory buffer
; Returns: ECX = file count, EAX = max entries
.df_count_dir:
        xor ecx, ecx
        xor ebx, ebx
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov edx, eax
.df_cd_loop:
        cmp ebx, edx
        jge .df_cd_done
        cmp byte [edi], 0
        je .df_cd_skip
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .df_cd_skip
        cmp byte [edi + DIRENT_TYPE], FTYPE_DIR
        je .df_cd_skip           ; Don't count directory entries as files
        inc ecx
.df_cd_skip:
        add edi, HBFS_DIR_ENTRY_SIZE
        inc ebx
        jmp .df_cd_loop
.df_cd_done:
        mov eax, edx
        ret

.df_total_files: dd 0
.df_total_slots: dd 0
.df_dir_count:   dd 0
.df_scan_idx:    dd 0

.df_no_disk:
        mov esi, msg_hbfs_nodisk
        call vga_print
        popad
        ret

;---------------------------------------
; Command: run - Execute a program from filesystem
;---------------------------------------
cmd_exec_program:
        pushad

        ; Parse program name (up to first space or null)
        mov edi, prog_name_buf
.parse_pname:
        lodsb
        cmp al, ' '
        je .pname_done
        cmp al, 0
        je .pname_end
        stosb
        jmp .parse_pname
.pname_done:
        mov byte [edi], 0
        call skip_spaces        ; Skip extra spaces before args
        jmp .copy_prog_args
.pname_end:
        mov byte [edi], 0
        dec esi                 ; Point back to null
.copy_prog_args:
        ; Copy remaining args to kernel argument buffer
        mov edi, program_args_buf
        mov ecx, 511
.cpa_loop:
        lodsb
        stosb
        test al, al
        jz .cpa_done
        loop .cpa_loop
        mov byte [edi], 0       ; Force null terminate
.cpa_done:

        ; Load file using parsed program name
        mov esi, prog_name_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .try_path_search

        jmp .found_program

.try_path_search:
        ; File not found in current directory.
        ; Search each directory in PATH (colon-separated, e.g. "/bin:/games")
        ; Save the user's current working directory
        call path_save_cwd

        mov esi, path_env_name
        mov edi, path_search_buf
        call env_get_var
        jc .path_not_found      ; No PATH set

        ; Iterate colon-separated PATH components
        mov esi, path_search_buf
.path_next_dir:
        cmp byte [esi], 0
        je .path_not_found

        ; Extract this path component into temp_path_buf
        mov edi, temp_path_buf
.path_copy_dir:
        lodsb
        cmp al, ':'
        je .path_dir_ready
        cmp al, 0
        je .path_dir_last
        stosb
        jmp .path_copy_dir
.path_dir_last:
        dec esi                 ; point back at null for next iteration
.path_dir_ready:
        mov byte [edi], 0
        push esi                ; save position in PATH string

        ; Restore to user's cwd first (clean slate for cd)
        call path_restore_cwd

        ; cd into this PATH directory
        mov esi, temp_path_buf
        call cmd_cd_internal
        test eax, eax
        jnz .path_cd_fail

        ; Now search for the program in this directory
        call hbfs_load_root_dir
        mov esi, prog_name_buf
        call hbfs_find_file
        jc .path_cd_fail

        ; Found it! Read the file data
        movzx ebx, byte [edi + 253]
        mov [last_file_type], bl
        mov ebx, [edi + 256]    ; file size
        mov [.path_fsize], ebx
        mov eax, [edi + 260]    ; start block
        mov ecx, [edi + 264]    ; block count
        shl eax, 3
        add eax, HBFS_DATA_START
        shl ecx, 3
        mov edi, PROGRAM_BASE
        call ata_read_sectors
        jc .path_cd_fail

        ; Restore user's cwd before execution
        call path_restore_cwd
        pop esi                 ; clean up PATH string pointer
        jmp .found_program

.path_cd_fail:
        pop esi                 ; restore PATH string position
        jmp .path_next_dir

.path_not_found:
        ; Restore user's cwd
        call path_restore_cwd
        jmp .not_found

.found_program:

        ; Set program running flag
        mov byte [program_running], 1
        ; Clear Ctrl+C flag and exit code
        mov byte [ctrl_c_flag], 0
        mov dword [program_exit_code], 0

        ; Check for ELF magic
        cmp dword [PROGRAM_BASE], ELF_MAGIC
        jne .try_flat

        ; ELF binary - parse and load
        call elf_load_program
        jc .not_found           ; ELF parse error
        ; EAX = entry point from ELF
        mov [program_entry], eax
        jmp .exec_program

.try_flat:
        ; Flat binary at PROGRAM_BASE
        mov dword [program_entry], PROGRAM_BASE

.exec_program:
        mov esi, msg_exec
        call vga_print

        ; Write SYS_EXIT trampoline at end of program area
        ; This catches programs that use RET instead of SYS_EXIT
        ; Bytes: B8 00 00 00 00 CD 80 = mov eax, 0 / int 0x80
        mov byte [PROGRAM_EXIT_ADDR], 0xB8
        mov dword [PROGRAM_EXIT_ADDR + 1], 0
        mov byte [PROGRAM_EXIT_ADDR + 5], 0xCD
        mov byte [PROGRAM_EXIT_ADDR + 6], 0x80

        ; Update TSS kernel stack for ring transitions
        mov dword [tss_struct + 4], KERNEL_STACK

        ; Set up ring 3 stack
        mov eax, PROGRAM_EXIT_ADDR - 4
        ; Push return address (trampoline) on ring 3 stack
        mov dword [eax], PROGRAM_EXIT_ADDR

        ; Switch to ring 3 via IRET
        mov ebx, [program_entry]
        push dword USER_DS      ; SS3
        push eax                ; ESP3
        pushfd
        or dword [esp], 0x200   ; Ensure interrupts enabled
        push dword USER_CS      ; CS3
        push ebx                ; EIP
        iretd
        ; SYS_EXIT will jump to shell_main, we never return here

.path_fsize: dd 0               ; Temp storage for file size during PATH search

.not_found:
        popad
        stc
        ret

;---------------------------------------
; Command: enter - Hex entry mode
;---------------------------------------
cmd_enter_hex:
        pushad
        mov edi, PROGRAM_BASE   ; Write hex data here

.hex_prompt:
        push edi
        mov byte [vga_color], COLOR_PROMPT
        mov al, 'h'
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT

        ; Read a line
        mov esi, temp_line_buf
        push esi
        mov edi, temp_line_buf
        xor ecx, ecx

.read_hex_line:
        call kb_getchar
        cmp al, 0x0D
        je .hex_line_done
        cmp al, 0x0A
        je .hex_line_done
        mov [edi], al
        inc edi
        inc ecx
        call vga_putchar
        jmp .read_hex_line

.hex_line_done:
        mov byte [edi], 0
        mov al, 0x0A
        call vga_putchar
        pop esi
        pop edi

        ; Empty line = end of hex input
        or ecx, ecx
        jz .hex_save

        ; Parse hex bytes from line
.parse_hex:
        call parse_hex_byte
        jc .hex_prompt          ; CF=set = end of input, get more
        stosb                   ; CF=clear = valid byte, store it
        jmp .parse_hex

.hex_save:
        ; Calculate size
        mov ecx, edi
        sub ecx, PROGRAM_BASE

        ; Ask for filename
        mov byte [vga_color], COLOR_PROMPT
        mov al, '*'
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT

        push ecx
        mov edi, filename_buf
        xor ecx, ecx
.read_name:
        call kb_getchar
        cmp al, 0x0D
        je .name_done
        cmp al, 0x0A
        je .name_done
        mov [edi], al
        inc edi
        inc ecx
        call vga_putchar
        jmp .read_name

.name_done:
        mov byte [edi], 0
        mov al, 0x0A
        call vga_putchar
        pop ecx

        ; Save file as executable
        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        mov edx, FTYPE_EXEC
        call hbfs_create_file
        jc .save_fail

        mov esi, msg_saved
        call vga_print
        popad
        ret

.save_fail:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_write_err
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; Command: copy - Copy a file
; Flags: -v  verbose (print filenames)
;---------------------------------------
cmd_copy_file:
        pushad

        ; Parse source filename
        mov edi, filename_buf
        call copy_word
        call skip_spaces

        ; Parse dest filename
        mov edi, filename_buf2
        call copy_word

        ; Validate args
        cmp byte [filename_buf], 0
        je .usage_error
        cmp byte [filename_buf2], 0
        je .usage_error

        ; Wildcard source?
        mov esi, filename_buf
        call str_has_wildcards
        jc .copy_wild

        ; Load source file
        push esi
        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .src_not_found

        ; ECX = file size already returned by hbfs_read_file

        ; Create dest file, preserving source type
        mov esi, filename_buf2
        mov edi, PROGRAM_BASE
        movzx edx, byte [last_file_type]
        call hbfs_create_file
        jc .copy_fail

        pop esi
        ; -v: print what was copied
        mov eax, 21            ; 'v' - 'a'
        call test_flag
        jnc .copy_quiet
        push esi
        mov esi, msg_copy_prefix
        call vga_print
        mov esi, filename_buf
        call vga_print
        mov esi, msg_copy_arrow
        call vga_print
        mov esi, filename_buf2
        call vga_print
        mov al, 0x0A
        call vga_putchar
        pop esi
.copy_quiet:
        mov esi, msg_copied
        call vga_print
        popad
        ret

.usage_error:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_copy_usage
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

.copy_wild:
        ; Count matches first
        call hbfs_load_root_dir
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
        xor ebx, ebx                    ; match count

.cw_count_loop:
        cmp ecx, 0
        je .cw_count_done
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .cw_count_next
        cmp byte [edi + DIRENT_TYPE], FTYPE_DIR
        je .cw_count_next

        push ecx
        push edi
        mov esi, filename_buf
        call wildcard_match
        pop edi
        pop ecx
        jnc .cw_count_next
        inc ebx

.cw_count_next:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jmp .cw_count_loop

.cw_count_done:
        cmp ebx, 0
        je .wild_not_found

        ; If multiple matches, destination must contain '*'
        cmp ebx, 1
        jle .cw_copy_begin
        mov esi, filename_buf2
        call str_has_asterisk
        jc .cw_copy_begin
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_wild_needs_star
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

.cw_copy_begin:
        call hbfs_load_root_dir
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
        xor ebx, ebx                    ; copied count

.cw_loop:
        cmp ecx, 0
        je .cw_done
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .cw_next
        cmp byte [edi + DIRENT_TYPE], FTYPE_DIR
        je .cw_next

        push ecx
        push edi
        mov esi, filename_buf
        call wildcard_match
        pop edi
        pop ecx
        jnc .cw_next

        ; source entry name -> wildcard_src_buf
        push ecx
        push edi
        mov esi, edi
        mov edi, wildcard_src_buf
        call str_copy
        pop edi
        pop ecx

        ; load source content
        push ecx
        push edi
        mov esi, wildcard_src_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        pop edi
        pop ecx
        jc .cw_next

        ; build destination name
        mov esi, filename_buf2
        call str_has_asterisk
        jc .cw_expand
        mov esi, filename_buf2
        mov edi, wildcard_dst_buf
        call str_copy
        jmp .cw_create

.cw_expand:
        mov esi, filename_buf2          ; template
        mov edi, wildcard_dst_buf       ; output
        mov edx, wildcard_src_buf       ; replacement text
        call wildcard_expand_star

.cw_create:
        push ecx
        push edi
        mov esi, wildcard_dst_buf
        mov edi, PROGRAM_BASE
        movzx edx, byte [last_file_type]
        call hbfs_create_file
        pop edi
        pop ecx
        jc .cw_next
        inc ebx

.cw_next:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jmp .cw_loop

.cw_done:
        cmp ebx, 0
        je .wild_not_found
        mov esi, msg_copied
        call vga_print
        popad
        ret

.wild_not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

.src_not_found:
        pop esi
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

.copy_fail:
        pop esi
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_write_err
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; Command: ren - Rename a file
; Usage: ren oldname newname
;---------------------------------------
cmd_rename_file:
        pushad

        ; Parse old filename
        mov edi, filename_buf
        call copy_word
        call skip_spaces

        ; Parse new filename
        mov edi, filename_buf2
        call copy_word

        ; Validate args
        cmp byte [filename_buf], 0
        je .ren_usage
        cmp byte [filename_buf2], 0
        je .ren_usage

        ; Wildcard source pattern?
        mov esi, filename_buf
        call str_has_wildcards
        jc .ren_wild

        ; Find old file in directory (global search)
        mov esi, filename_buf
        call hbfs_find_file_global
        jc .ren_not_found

        ; Copy new name into entry (max 252 chars)
        push edi
        mov esi, filename_buf2
        xor ecx, ecx
.copy_new_name:
        cmp ecx, HBFS_MAX_FILENAME
        jge .name_truncate
        lodsb
        stosb
        inc ecx
        test al, al
        jnz .copy_new_name
        jmp .name_copied
.name_truncate:
        mov byte [edi], 0      ; Force null-terminate
.name_copied:
        pop edi

        ; Update modified timestamp
        mov eax, [tick_count]
        mov [edi + 272], eax

        call hbfs_save_root_dir

        ; Restore CWD if global search changed it
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .ren_no_restore
        call gff_restore_cwd
.ren_no_restore:
        mov esi, msg_renamed
        call vga_print
        popad
        ret

.ren_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_ren_usage
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

.ren_wild:
        ; Count matches first
        call hbfs_load_root_dir
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
        xor ebx, ebx                    ; match count

.rw_count_loop:
        cmp ecx, 0
        je .rw_count_done
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .rw_count_next

        push ecx
        push edi
        mov esi, filename_buf
        call wildcard_match
        pop edi
        pop ecx
        jnc .rw_count_next
        inc ebx

.rw_count_next:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jmp .rw_count_loop

.rw_count_done:
        cmp ebx, 0
        je .ren_not_found

        ; If multiple matches, destination must contain '*'
        cmp ebx, 1
        jle .rw_do
        mov esi, filename_buf2
        call str_has_asterisk
        jc .rw_do
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_wild_needs_star
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

.rw_do:
        call hbfs_load_root_dir
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
        xor ebx, ebx                    ; renamed count

.rw_loop:
        cmp ecx, 0
        je .rw_done
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .rw_next

        push ecx
        push edi
        mov esi, filename_buf
        call wildcard_match
        pop edi
        pop ecx
        jnc .rw_next

        ; source entry name -> wildcard_src_buf
        push ecx
        push edi
        mov esi, edi
        mov edi, wildcard_src_buf
        call str_copy
        pop edi
        pop ecx

        ; build destination name
        mov esi, filename_buf2
        call str_has_asterisk
        jc .rw_expand
        mov esi, filename_buf2
        mov edi, wildcard_dst_buf
        call str_copy
        jmp .rw_write

.rw_expand:
        mov esi, filename_buf2
        mov edi, wildcard_dst_buf
        mov edx, wildcard_src_buf
        call wildcard_expand_star

.rw_write:
        ; Copy new name into matched entry
        push ecx
        push edi
        mov esi, wildcard_dst_buf
        xor edx, edx
.rw_copy_name:
        cmp edx, HBFS_MAX_FILENAME
        jge .rw_name_trunc
        lodsb
        mov [edi + edx], al
        inc edx
        test al, al
        jnz .rw_copy_name
        jmp .rw_name_done
.rw_name_trunc:
        mov byte [edi + HBFS_MAX_FILENAME], 0
.rw_name_done:
        mov eax, [tick_count]
        mov [edi + DIRENT_MODIFIED], eax
        pop edi
        pop ecx
        inc ebx

.rw_next:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jmp .rw_loop

.rw_done:
        call hbfs_save_root_dir
        mov esi, msg_renamed
        call vga_print
        popad
        ret

.ren_not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;=======================================================================
; UTILITY FUNCTIONS
;=======================================================================

; Detect file type from filename extension
; ESI = filename (preserved)
; Returns: EDX = file type (FTYPE_TEXT, FTYPE_EXEC, FTYPE_BATCH)
; Input EDX is used as default if no extension matched
detect_file_type:
        pushad
        ; Find end of filename and last '.'
        mov edi, esi
        xor ecx, ecx           ; Track position
        xor ebx, ebx           ; Last dot position (0 = none)
.dft_scan:
        mov al, [edi + ecx]
        cmp al, 0
        je .dft_check
        cmp al, '.'
        jne .dft_not_dot
        lea ebx, [edi + ecx + 1] ; EBX = pointer past the dot
.dft_not_dot:
        inc ecx
        jmp .dft_scan
.dft_check:
        cmp ebx, 0
        je .dft_done            ; No dot found, keep default EDX
        ; Compare extension (case insensitive)
        ; Check for .bat
        cmp byte [ebx], 'b'
        jne .dft_try_B
        cmp byte [ebx+1], 'a'
        jne .dft_done
        cmp byte [ebx+2], 't'
        jne .dft_done
        cmp byte [ebx+3], 0
        jne .dft_done
        mov dword [esp + 20], FTYPE_BATCH ; EDX in pushad frame
        jmp .dft_done
.dft_try_B:
        cmp byte [ebx], 'B'
        jne .dft_try_bin
        cmp byte [ebx+1], 'A'
        jne .dft_done
        cmp byte [ebx+2], 'T'
        jne .dft_done
        cmp byte [ebx+3], 0
        jne .dft_done
        mov dword [esp + 20], FTYPE_BATCH
        jmp .dft_done
.dft_try_bin:
        ; Check for .bin
        cmp byte [ebx], 'b'
        jne .dft_try_BIN
        cmp byte [ebx+1], 'i'
        jne .dft_done
        cmp byte [ebx+2], 'n'
        jne .dft_done
        cmp byte [ebx+3], 0
        jne .dft_done
        mov dword [esp + 20], FTYPE_EXEC
        jmp .dft_done
.dft_try_BIN:
        cmp byte [ebx], 'B'
        jne .dft_done
        cmp byte [ebx+1], 'I'
        jne .dft_done
        cmp byte [ebx+2], 'N'
        jne .dft_done
        cmp byte [ebx+3], 0
        jne .dft_done
        mov dword [esp + 20], FTYPE_EXEC
.dft_done:
        popad
        ret

;---------------------------------------
; parse_flags - Parse '-xyz' flag arguments from command line
; Input:  ESI = command line (after command name)
; Output: [cmd_flags] = bitmask of flags found (bits 0-25 = a-z)
;         [cmd_flag_num] = numeric value if -n N was present (0 if not)
;         ESI advanced past all flag arguments
; Flags are single letters after '-'. Multiple flags can be given:
;   -l -n 20    or    -ln 20
; '--' stops flag parsing (everything after is positional)
;---------------------------------------
parse_flags:
        pushad
        mov dword [cmd_flags], 0
        mov dword [cmd_flag_num], 0
.pf_loop:
        cmp byte [esi], '-'
        jne .pf_done
        cmp byte [esi + 1], 0   ; lone '-' is not a flag
        je .pf_done
        cmp byte [esi + 1], ' ' ; '-' followed by space is not a flag
        je .pf_done
        ; Check for '--' (end of flags)
        cmp byte [esi + 1], '-'
        jne .pf_parse
        add esi, 2              ; skip '--'
        call skip_spaces
        jmp .pf_done
.pf_parse:
        inc esi                 ; skip '-'
.pf_chars:
        movzx eax, byte [esi]
        cmp al, 0
        je .pf_next
        cmp al, ' '
        je .pf_next
        ; Is it a letter a-z?
        cmp al, 'a'
        jl .pf_try_upper
        cmp al, 'z'
        jg .pf_next
        sub al, 'a'
        bts [cmd_flags], eax
        inc esi
        ; If flag is 'n', read the next numeric arg
        cmp al, 'n' - 'a'
        je .pf_read_num_after
        jmp .pf_chars
.pf_try_upper:
        cmp al, 'A'
        jl .pf_next
        cmp al, 'Z'
        jg .pf_next
        sub al, 'A'
        bts [cmd_flags], eax    ; map to same bit as lowercase
        inc esi
        cmp al, 'n' - 'a'
        je .pf_read_num_after
        jmp .pf_chars
.pf_next:
        call skip_spaces
        jmp .pf_loop
.pf_read_num_after:
        ; Check if next char (after possible space) is a digit
        cmp byte [esi], ' '
        jne .pf_check_digit
        call skip_spaces
.pf_check_digit:
        cmp byte [esi], '0'
        jl .pf_chars
        cmp byte [esi], '9'
        jg .pf_chars
        ; Parse decimal number
        xor eax, eax
.pf_num:
        movzx ebx, byte [esi]
        sub ebx, '0'
        cmp ebx, 9
        ja .pf_num_done
        imul eax, 10
        add eax, ebx
        inc esi
        jmp .pf_num
.pf_num_done:
        mov [cmd_flag_num], eax
        jmp .pf_next
.pf_done:
        popad
        ret

; Test if flag bit is set. EAX = bit number (0='a', 1='b', etc.)
; Returns: CF set if flag is present
test_flag:
        bt [cmd_flags], eax
        ret

; Skip spaces in string at ESI
skip_spaces:
        cmp byte [esi], ' '
        jne .done
        inc esi
        jmp skip_spaces
.done:
        ret

; Copy word from ESI to EDI (stop at space or null)
; Advances ESI past the word
copy_word:
        lodsb
        cmp al, ' '
        je .done
        cmp al, 0
        je .done_null
        stosb
        jmp copy_word
.done:
        mov byte [edi], 0
        ret
.done_null:
        mov byte [edi], 0
        dec esi                 ; Put back the null
        ret

; Check for wildcard characters '*' or '?' in string at ESI
; Returns: CF set if wildcard exists
str_has_wildcards:
        push esi
.shw_loop:
        mov al, [esi]
        test al, al
        jz .shw_no
        cmp al, '*'
        je .shw_yes
        cmp al, '?'
        je .shw_yes
        inc esi
        jmp .shw_loop
.shw_yes:
        pop esi
        stc
        ret
.shw_no:
        pop esi
        clc
        ret

; Check for '*' in string at ESI
; Returns: CF set if found
; Preserves: ESI
str_has_asterisk:
        push esi
.sha_loop:
        mov al, [esi]
        test al, al
        jz .sha_no
        cmp al, '*'
        je .sha_yes
        inc esi
        jmp .sha_loop
.sha_yes:
        pop esi
        stc
        ret
.sha_no:
        pop esi
        clc
        ret

; Wildcard match
; ESI = pattern (supports '*' and '?')
; EDI = candidate string
; Returns: CF set if match, CF clear if not
wildcard_match:
        push eax
        push ebx
        push edx

        mov dword [wild_star_pat], 0
        mov dword [wild_star_txt], 0

.wm_loop:
        mov al, [esi]
        mov bl, [edi]

        cmp al, '*'
        jne .wm_not_star
        mov [wild_star_pat], esi
        mov [wild_star_txt], edi
        inc esi
        jmp .wm_loop

.wm_not_star:
        test al, al
        jne .wm_pat_not_end
        test bl, bl
        jz .wm_match
        jmp .wm_backtrack

.wm_pat_not_end:
        test bl, bl
        jz .wm_backtrack

        cmp al, '?'
        je .wm_consume
        cmp al, bl
        jne .wm_backtrack

.wm_consume:
        inc esi
        inc edi
        jmp .wm_loop

.wm_backtrack:
        mov edx, [wild_star_pat]
        test edx, edx
        jz .wm_fail

        mov edx, [wild_star_txt]
        cmp byte [edx], 0
        je .wm_fail
        inc edx
        mov [wild_star_txt], edx
        mov edi, edx
        mov esi, [wild_star_pat]
        inc esi
        jmp .wm_loop

.wm_match:
        stc
        jmp .wm_done

.wm_fail:
        clc

.wm_done:
        pop edx
        pop ebx
        pop eax
        ret

; Expand destination template with '*'
; ESI = template
; EDI = output buffer
; EDX = replacement text (usually matched source filename)
wildcard_expand_star:
        push eax
        push ebx
        push ecx
        push esi
        push edx
        xor ecx, ecx

.wes_loop:
        lodsb
        test al, al
        jz .wes_done
        cmp ecx, HBFS_MAX_FILENAME
        jge .wes_done
        cmp al, '*'
        jne .wes_copy_char

        ; splice replacement text
        mov ebx, edx
.wes_rep:
        mov al, [ebx]
        test al, al
        jz .wes_loop
        cmp ecx, HBFS_MAX_FILENAME
        jge .wes_done
        stosb
        inc ebx
        inc ecx
        jmp .wes_rep

.wes_copy_char:
        stosb
        inc ecx
        jmp .wes_loop

.wes_done:
        mov byte [edi], 0
        pop edx
        pop esi
        pop ecx
        pop ebx
        pop eax
        ret

; Compare two null-terminated strings at ESI and EDI
; Returns: ZF set if strings are equal
; Preserves: ESI, EDI
str_compare:
        push esi
        push edi
.sc_loop:
        mov al, [esi]
        cmp al, [edi]
        jne .sc_ne
        test al, al
        jz .sc_eq
        inc esi
        inc edi
        jmp .sc_loop
.sc_eq:
        pop edi
        pop esi
        xor eax, eax          ; ZF=1
        ret
.sc_ne:
        pop edi
        pop esi
        or eax, 1             ; ZF=0
        ret

; Check if string at ESI starts with string at EDI
; Returns: CF set if match, ESI advanced past match
str_starts_with:
        push esi
        push edi
.loop:
        mov al, [edi]
        test al, al
        jz .match               ; End of pattern = match

        cmp al, [esi]
        jne .no_match

        inc esi
        inc edi
        jmp .loop

.match:
        ; Check that next char is space or null (word boundary)
        mov al, [esi]
        cmp al, ' '
        je .yes
        cmp al, 0
        je .yes
        jmp .no_match

.yes:
        pop edi
        add esp, 4              ; Discard saved ESI
        ; Skip spaces after command
        call skip_spaces
        stc
        ret

.no_match:
        pop edi
        pop esi
        clc
        ret

; Parse hex byte from string at ESI
; Returns: AL = byte value, CF clear if valid byte, CF set if end of input
parse_hex_byte:
.skip:
        cmp byte [esi], ' '
        jne .start
        inc esi
        jmp .skip

.start:
        cmp byte [esi], 0
        je .end

        call .nibble
        jc .end                 ; No valid nibble found = end of input
        shl al, 4
        mov ah, al
        call .nibble
        jc .end_one             ; Only got high nibble
        or al, ah
        clc                     ; CF=0 = valid byte
        ret

.end_one:
        mov al, ah              ; Return just the high nibble
        clc                     ; CF=0 = valid byte
        ret

.end:
        stc                     ; CF=1 = no more data
        ret

; Read one hex nibble from [ESI]
; Returns: AL = 0-15, CF clear on success; CF set on end-of-string
.nibble:
        mov al, [esi]
        cmp al, 0
        je .nib_end
        inc esi

        cmp al, '0'
        jl .nib_skip
        cmp al, '9'
        jle .nib_digit
        cmp al, 'A'
        jl .nib_skip
        cmp al, 'F'
        jle .nib_upper
        cmp al, 'a'
        jl .nib_skip
        cmp al, 'f'
        jle .nib_lower
.nib_skip:
        jmp .nibble             ; Skip non-hex chars

.nib_digit:
        sub al, '0'
        clc                     ; CF=0 = success
        ret
.nib_upper:
        sub al, 'A' - 10
        clc
        ret
.nib_lower:
        sub al, 'a' - 10
        clc
        ret
.nib_end:
        stc                     ; CF=1 = end of string
        ret

;---------------------------------------
; Print system information at boot
;---------------------------------------
print_sysinfo:
        pushad
        mov byte [vga_color], COLOR_INFO

        mov esi, msg_cpu
        call vga_print

        mov esi, msg_mem_detect
        call vga_print
        mov eax, [total_free_pages]
        shl eax, 2
        shr eax, 10
        call vga_print_dec
        mov esi, msg_mb
        call vga_print

        mov esi, msg_serial_info
        call vga_print

        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;=======================================================================
; SERIAL PORT (COM1) DRIVER
;=======================================================================

serial_init:
        pushad
        mov dx, COM1_PORT + 1   ; IER - disable interrupts
        xor al, al
        out dx, al
        mov dx, COM1_PORT + 3   ; LCR - enable DLAB
        mov al, 0x80
        out dx, al
        mov dx, COM1_PORT       ; Divisor low (1 = 115200 baud)
        mov al, 1
        out dx, al
        mov dx, COM1_PORT + 1   ; Divisor high
        xor al, al
        out dx, al
        mov dx, COM1_PORT + 3   ; 8N1
        mov al, 0x03
        out dx, al
        mov dx, COM1_PORT + 2   ; Enable FIFO
        mov al, 0xC7
        out dx, al
        mov dx, COM1_PORT + 4   ; RTS/DTR
        mov al, 0x03
        out dx, al
        mov byte [serial_present], 1
        popad
        ret

serial_putchar:
        push edx
        push eax
        mov ah, al
.wait:  mov dx, COM1_LSR
        in al, dx
        test al, 0x20
        jz .wait
        mov al, ah
        mov dx, COM1_PORT
        out dx, al
        pop eax
        pop edx
        ret

serial_print:
        pushad
.loop:  lodsb
        or al, al
        jz .done
        call serial_putchar
        jmp .loop
.done:  popad
        ret

serial_getchar:
        push edx
.wait:  mov dx, COM1_LSR
        in al, dx
        test al, 0x01
        jz .wait
        mov dx, COM1_PORT
        in al, dx
        pop edx
        ret

;=======================================================================
; RTC (Real-Time Clock) via CMOS
;=======================================================================

rtc_read_reg:
        out RTC_INDEX, al
        jmp short $+2
        in al, RTC_DATA
        ret

rtc_bcd_to_bin:
        push ecx
        mov cl, al
        shr al, 4
        mov ah, 10
        mul ah              ; AX = high_nibble * 10
        and cl, 0x0F
        add al, cl
        xor ah, ah
        pop ecx
        ret

rtc_read_time:
        pushad
.wait:  mov al, 0x0A
        call rtc_read_reg
        test al, 0x80
        jnz .wait
        mov al, 0x00
        call rtc_read_reg
        call rtc_bcd_to_bin
        mov [rtc_sec], al
        mov al, 0x02
        call rtc_read_reg
        call rtc_bcd_to_bin
        mov [rtc_min], al
        mov al, 0x04
        call rtc_read_reg
        call rtc_bcd_to_bin
        mov [rtc_hour], al
        mov al, 0x07
        call rtc_read_reg
        call rtc_bcd_to_bin
        mov [rtc_day], al
        mov al, 0x08
        call rtc_read_reg
        call rtc_bcd_to_bin
        mov [rtc_month], al
        mov al, 0x09
        call rtc_read_reg
        call rtc_bcd_to_bin
        mov [rtc_year], al
        mov al, 0x32
        call rtc_read_reg
        cmp al, 0
        je .def_century
        call rtc_bcd_to_bin
        mov [rtc_century], al
        jmp .rtc_done
.def_century:
        mov byte [rtc_century], 20
.rtc_done:
        popad
        ret

;=======================================================================
; PC SPEAKER
;=======================================================================

speaker_beep:
        pushad
        mov eax, PIT_FREQ
        xor edx, edx
        div ebx
        push eax
        mov al, 0xB6
        out PIT_CMD, al
        pop eax
        out PIT_CH2, al
        mov al, ah
        out PIT_CH2, al
        in al, SPEAKER_PORT
        or al, 0x03
        out SPEAKER_PORT, al
        mov eax, [tick_count]
        add ecx, eax
.bwait: hlt
        cmp [tick_count], ecx
        jl .bwait
        call speaker_off
        popad
        ret

speaker_off:
        push eax
        push edx
        in al, SPEAKER_PORT
        and al, 0xFC
        out SPEAKER_PORT, al
        pop edx
        pop eax
        ret

;=======================================================================
; TSS (Task State Segment) for Ring 3
;=======================================================================

tss_init:
        pushad
        mov edi, tss_struct
        mov ecx, 104 / 4
        xor eax, eax
        rep stosd
        mov dword [tss_struct + 4], KERNEL_STACK
        mov dword [tss_struct + 8], 0x10
        mov word [tss_struct + 102], 104

        sub esp, 8
        sgdt [esp]
        mov ebx, [esp + 2]
        add esp, 8
        add ebx, TSS_SEL
        mov eax, tss_struct
        mov [ebx + 2], ax
        shr eax, 16
        mov [ebx + 4], al
        mov [ebx + 7], ah

        mov ax, TSS_SEL
        ltr ax
        popad
        ret

;=======================================================================
; ELF32 LOADER
;=======================================================================

elf_load_program:
        pushad
        cmp byte [PROGRAM_BASE + 4], 1
        jne .elf_bad
        cmp byte [PROGRAM_BASE + 5], 1
        jne .elf_bad

        movzx ecx, word [PROGRAM_BASE + 44]
        mov ebx, [PROGRAM_BASE + 28]
        mov edi, [PROGRAM_BASE + 24]
        mov [.elf_entry], edi

        add ebx, PROGRAM_BASE
.load_seg:
        cmp ecx, 0
        je .elf_ok
        cmp dword [ebx], ELF_PT_LOAD
        jne .next_ph

        push ecx
        mov esi, PROGRAM_BASE
        add esi, [ebx + 4]
        mov edi, [ebx + 8]
        mov ecx, [ebx + 16]
        cmp ecx, 0
        je .zero_bss
        cmp esi, edi
        je .zero_bss
        rep movsb
.zero_bss:
        mov ecx, [ebx + 20]
        sub ecx, [ebx + 16]
        jle .seg_ok
        xor al, al
        rep stosb
.seg_ok:
        pop ecx

.next_ph:
        add ebx, ELF_PHDR_SIZE
        dec ecx
        jmp .load_seg

.elf_ok:
        mov eax, [.elf_entry]
        mov [esp + 28], eax
        popad
        clc
        ret

.elf_bad:
        popad
        stc
        ret

.elf_entry: dd 0

;=======================================================================
; FILE DESCRIPTOR TABLE
;=======================================================================

fd_open:
        pushad
        mov [.fd_mode], ecx
        xor ebx, ebx
.find_fd:
        cmp ebx, FD_MAX
        jge .no_fd
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE
        cmp dword [fd_table + eax], FD_FLAG_CLOSED
        je .got_fd
        inc ebx
        jmp .find_fd
.got_fd:
        push ebx
        call hbfs_find_file_global
        pop ebx
        jc .no_fd
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE
        mov ecx, [.fd_mode]
        mov [fd_table + eax], ecx
        mov ecx, [edi + DIRENT_SIZE]
        mov [fd_table + eax + 4], ecx
        mov ecx, [edi + DIRENT_START_BLOCK]
        mov [fd_table + eax + 8], ecx
        mov dword [fd_table + eax + 12], 0
        mov ecx, [edi + DIRENT_BLOCK_COUNT]
        mov [fd_table + eax + 16], ecx
        ; Record which directory this file lives in (for fd_close)
        mov ecx, [current_dir_lba]
        mov [fd_table + eax + 20], ecx
        mov ecx, [current_dir_sects]
        mov [fd_table + eax + 24], ecx
        ; Restore CWD if global search changed it
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .fd_no_restore
        call gff_restore_cwd
.fd_no_restore:
        mov [esp + 28], ebx
        popad
        ret
.no_fd:
        popad
        mov eax, -1
        ret
.fd_mode: dd 0

fd_read:
        pushad
        cmp ebx, FD_MAX
        jge .frd_err
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE
        cmp dword [fd_table + eax], FD_FLAG_CLOSED
        je .frd_err
        mov esi, [fd_table + eax + 12]
        mov edi, [fd_table + eax + 4]
        sub edi, esi
        jle .frd_eof
        cmp edx, edi
        jle .frd_ok
        mov edx, edi
.frd_ok:
        ; Read only the block containing current position
        push ecx
        push edx
        push eax
        ; Calculate which block the current offset is in
        mov esi, [fd_table + eax + 12]  ; current position
        mov ecx, esi
        shr ecx, 12             ; position / 4096 = block offset within file
        mov eax, [fd_table + eax + 8]   ; start block
        add eax, ecx            ; + block offset = actual block
        shl eax, 3              ; * 8 = sector
        add eax, HBFS_DATA_START
        mov ecx, HBFS_SECTORS_PER_BLK   ; read exactly 1 block (8 sectors)
        mov edi, hbfs_block_buf
        call ata_read_sectors
        pop eax
        pop edx
        pop ecx
        jc .frd_err
        ; Calculate offset within the block
        mov esi, [fd_table + eax + 12]
        and esi, (HBFS_BLOCK_SIZE - 1)  ; position mod 4096
        ; Clamp bytes to read to not exceed block boundary
        push edx
        mov edi, HBFS_BLOCK_SIZE
        sub edi, esi            ; bytes remaining in this block
        cmp edx, edi
        jle .frd_blk_ok
        mov edx, edi
.frd_blk_ok:
        pop edi                 ; restore original edx into edi temporarily
        ; edx = actual bytes to read (may be clamped)
        add esi, hbfs_block_buf ; offset into read buffer
        mov edi, ecx            ; dest buffer (original ECX)
        push eax
        mov ecx, edx
        rep movsb
        pop eax
        add [fd_table + eax + 12], edx
        mov [esp + 28], edx
        popad
        ret
.frd_eof:
        popad
        xor eax, eax
        ret
.frd_err:
        popad
        mov eax, -1
        ret

fd_write:
        pushad
        cmp ebx, FD_MAX
        jge .fwr_err
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE
        cmp dword [fd_table + eax], FD_FLAG_WRITE
        jne .fwr_err

        ; Save fd offset and parameters
        mov [.fw_fdoff], eax
        mov [.fw_src], ecx          ; source buffer
        mov [.fw_count], edx        ; byte count

        ; Check if position is within allocated blocks
        mov esi, [fd_table + eax + 12]  ; current position
        mov ecx, esi
        shr ecx, 12                      ; block index within file
        cmp ecx, [fd_table + eax + 16]  ; block count
        jge .fwr_full

        ; Read the block that contains the current position
        mov eax, [fd_table + eax + 8]   ; start block
        add eax, ecx                     ; + offset = actual block
        mov [.fw_blknum], eax
        shl eax, 3
        add eax, HBFS_DATA_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov edi, hbfs_block_buf
        call ata_read_sectors
        jc .fwr_ioerr

        ; Calculate offset within block and clamp write size
        mov eax, [.fw_fdoff]
        mov esi, [fd_table + eax + 12]  ; current position
        and esi, (HBFS_BLOCK_SIZE - 1)  ; offset within block
        mov ebx, HBFS_BLOCK_SIZE
        sub ebx, esi                     ; space remaining in block
        mov edx, [.fw_count]
        cmp edx, ebx
        jle .fw_cnt_ok
        mov edx, ebx                     ; clamp to block boundary
.fw_cnt_ok:
        mov [.fw_actual], edx

        ; Copy from user buffer to block buffer at offset
        mov ecx, edx
        mov edi, hbfs_block_buf
        add edi, esi                     ; offset within block
        mov esi, [.fw_src]
        rep movsb

        ; Write block back to disk
        mov eax, [.fw_blknum]
        shl eax, 3
        add eax, HBFS_DATA_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov esi, hbfs_block_buf
        call ata_write_sectors
        jc .fwr_ioerr

        ; Update file position and size
        mov eax, [.fw_fdoff]
        mov edx, [.fw_actual]
        add [fd_table + eax + 12], edx   ; advance position
        mov ecx, [fd_table + eax + 12]
        cmp ecx, [fd_table + eax + 4]
        jle .fw_no_grow
        mov [fd_table + eax + 4], ecx     ; extend file size
.fw_no_grow:
        mov [esp + 28], edx              ; return bytes written via EAX
        popad
        ret

.fwr_full:
.fwr_ioerr:
        popad
        xor eax, eax                     ; 0 = nothing written
        ret
.fwr_err:
        popad
        mov eax, -1
        ret

.fw_fdoff:  dd 0
.fw_src:    dd 0
.fw_count:  dd 0
.fw_blknum: dd 0
.fw_actual: dd 0

fd_close:
        cmp ebx, FD_MAX
        jge .fcl_bad
        pushad
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE

        ; If opened for writing, persist updated file size to directory
        cmp dword [fd_table + eax], FD_FLAG_WRITE
        jne .fcl_mark_closed

        ; Save start_block and file_size from FD entry
        mov ebp, [fd_table + eax + 8]    ; start_block
        mov edx, [fd_table + eax + 4]    ; file_size (possibly grown)

        ; Switch to the directory where this file lives (recorded at open time)
        ; Save current CWD first
        push dword [current_dir_lba]
        push dword [current_dir_sects]
        mov ecx, [fd_table + eax + 20]   ; directory LBA from fd entry
        mov [current_dir_lba], ecx
        mov ecx, [fd_table + eax + 24]   ; directory sector count from fd entry
        mov [current_dir_sects], ecx

        ; Find directory entry by matching start_block
        call hbfs_load_root_dir
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
.fcl_scan:
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .fcl_skip
        cmp [edi + DIRENT_START_BLOCK], ebp
        je .fcl_found
.fcl_skip:
        add edi, HBFS_DIR_ENTRY_SIZE
        loop .fcl_scan
        jmp .fcl_restore_cwd             ; entry not found, just close

.fcl_found:
        ; Update file size and modified timestamp in directory entry
        mov [edi + DIRENT_SIZE], edx
        mov eax, [tick_count]
        mov [edi + DIRENT_MODIFIED], eax
        call hbfs_save_root_dir

.fcl_restore_cwd:
        ; Restore original CWD
        pop dword [current_dir_sects]
        pop dword [current_dir_lba]

.fcl_mark_closed:
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE
        mov dword [fd_table + eax], FD_FLAG_CLOSED
        popad
.fcl_bad:
        ret

fd_seek:
        pushad
        cmp ebx, FD_MAX
        jge .fsk_err
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE
        cmp dword [fd_table + eax], FD_FLAG_CLOSED
        je .fsk_err
        cmp edx, 0
        je .fsk_set
        cmp edx, 1
        je .fsk_cur
        cmp edx, 2
        je .fsk_end
        jmp .fsk_err
.fsk_set:
        mov [fd_table + eax + 12], ecx
        jmp .fsk_done
.fsk_cur:
        add [fd_table + eax + 12], ecx
        jmp .fsk_done
.fsk_end:
        mov edx, [fd_table + eax + 4]
        add edx, ecx
        mov [fd_table + eax + 12], edx
.fsk_done:
        mov edx, [fd_table + eax + 12]
        mov [esp + 28], edx
        popad
        ret
.fsk_err:
        popad
        mov eax, -1
        ret

;=======================================================================
; ENVIRONMENT VARIABLES
;=======================================================================

env_get:
        pushad
        mov edi, env_table
        mov ecx, ENV_MAX
.eg_search:
        cmp byte [edi], 0
        je .eg_skip
        push esi
        push edi
.eg_cmp:
        mov al, [esi]
        cmp al, 0
        jne .eg_cont
        cmp byte [edi], '='
        je .eg_found
        jmp .eg_no
.eg_cont:
        cmp al, [edi]
        jne .eg_no
        inc esi
        inc edi
        jmp .eg_cmp
.eg_found:
        inc edi
        pop eax
        pop eax
        mov [esp + 28], edi
        popad
        ret
.eg_no:
        pop edi
        pop esi
.eg_skip:
        add edi, ENV_ENTRY_SIZE
        loop .eg_search
        popad
        xor eax, eax
        ret

;---------------------------------------
; env_get_var - Get environment variable value
; ESI = variable name (null-terminated)
; EDI = destination buffer for value
; Returns: CF=0 if found, CF=1 if not found
;---------------------------------------
env_get_var:
        push eax
        push esi
        push edi
        push ecx
        mov ecx, edi            ; save dest buffer
        call env_get
        ; env_get: on success EDI = value ptr, on failure EDI unchanged
        cmp edi, ecx            ; EDI unchanged = not found
        je .egv_not_found
        ; Copy value from EDI to our dest buffer
        mov esi, edi
        mov edi, ecx
.egv_copy:
        lodsb
        stosb
        cmp al, 0
        jne .egv_copy
        pop ecx
        pop edi
        pop esi
        pop eax
        clc
        ret
.egv_not_found:
        pop ecx
        pop edi
        pop esi
        pop eax
        stc
        ret

env_set_str:
        pushad
        mov edi, env_table
        mov ecx, ENV_MAX
        xor ebx, ebx
.es_scan:
        cmp byte [edi], 0
        jne .es_check
        cmp ebx, 0
        jne .es_next
        mov ebx, edi
        jmp .es_next
.es_check:
        push esi
        push edi
.es_cmp:
        mov al, [esi]
        cmp al, '='
        je .es_eq
        cmp al, 0
        je .es_eq
        cmp al, [edi]
        jne .es_nomatch
        inc esi
        inc edi
        jmp .es_cmp
.es_eq:
        cmp byte [edi], '='
        je .es_overwrite
.es_nomatch:
        pop edi
        pop esi
.es_next:
        add edi, ENV_ENTRY_SIZE
        loop .es_scan
        cmp ebx, 0
        je .es_full
        mov edi, ebx
        jmp .es_write
.es_overwrite:
        pop edi
        pop esi
.es_write:
        mov ecx, ENV_ENTRY_SIZE - 1
.es_copy:
        lodsb
        stosb
        cmp al, 0
        je .es_pad
        loop .es_copy
        mov byte [edi], 0
        jmp .es_done
.es_pad:
        xor al, al
        rep stosb
.es_done:
        popad
        ret
.es_full:
        popad
        ret

env_unset:
        pushad
        mov edi, env_table
        mov ecx, ENV_MAX
.eu_scan:
        cmp byte [edi], 0
        je .eu_skip
        push esi
        push edi
.eu_cmp:
        mov al, [esi]
        cmp al, 0
        jne .eu_cont
        cmp byte [edi], '='
        je .eu_found
        jmp .eu_no
.eu_cont:
        cmp al, [edi]
        jne .eu_no
        inc esi
        inc edi
        jmp .eu_cmp
.eu_found:
        pop edi
        pop esi
        push ecx
        mov ecx, ENV_ENTRY_SIZE / 4
        xor eax, eax
        rep stosd
        pop ecx
        popad
        ret
.eu_no:
        pop edi
        pop esi
.eu_skip:
        add edi, ENV_ENTRY_SIZE
        loop .eu_scan
        popad
        ret

env_expand:
        pushad
.ee_loop:
        lodsb
        cmp al, 0
        je .ee_done
        cmp al, '$'
        je .ee_var
        stosb
        jmp .ee_loop
.ee_var:
        push edi
        mov edi, env_name_buf
.ee_rname:
        mov al, [esi]
        cmp al, 'A'
        jl .ee_name_end
        cmp al, 'z'
        jg .ee_name_end
        cmp al, '['
        jge .ee_chklow
        jmp .ee_namechar
.ee_chklow:
        cmp al, 'a'
        jl .ee_name_end
.ee_namechar:
        stosb
        inc esi
        jmp .ee_rname
.ee_name_end:
        mov byte [edi], 0
        pop edi
        push esi
        mov esi, env_name_buf
        call env_get
        pop esi
        cmp eax, 0
        je .ee_loop
        push esi
        mov esi, eax
.ee_cpval:
        lodsb
        cmp al, 0
        je .ee_cpvdone
        stosb
        jmp .ee_cpval
.ee_cpvdone:
        pop esi
        jmp .ee_loop
.ee_done:
        mov byte [edi], 0
        popad
        ret

;=======================================================================
; SUBDIRECTORY SUPPORT
;=======================================================================

; cd_internal: Change directory
; ESI = path string
; Returns: [esp+28] = 0 on success, -1 on failure
; Supports: "..", "/", single name, multi-component "a/b/c"
cmd_cd_internal:
        pushad

        ; Check for empty argument
        cmp byte [esi], 0
        je .cd_fail

        ; Check for "cd /" (absolute root)
        cmp byte [esi], '/'
        jne .cd_check_dotdot
        cmp byte [esi + 1], 0
        je .cd_goto_root
        ; Absolute path like /a/b/c: go to root first, then resolve rest
        inc esi
        call .cd_reset_to_root
        jmp .cd_resolve_path

.cd_check_dotdot:
        ; Check for ".."
        cmp byte [esi], '.'
        jne .cd_resolve_path
        cmp byte [esi + 1], '.'
        jne .cd_resolve_path
        ; Could be ".." or "../something"
        cmp byte [esi + 2], 0
        je .cd_parent
        cmp byte [esi + 2], '/'
        je .cd_parent_continue
        jmp .cd_resolve_path    ; Not "..", just a name starting with ".."

.cd_parent:
        ; cd .. - go up one level
        call .cd_pop_stack
        mov dword [esp + 28], 0
        popad
        ret

.cd_parent_continue:
        ; "../something" - go up, then resolve rest
        call .cd_pop_stack
        add esi, 3              ; skip "../"
        jmp .cd_resolve_path

.cd_goto_root:
        call .cd_reset_to_root
        mov dword [esp + 28], 0
        popad
        ret

; Resolve a (possibly multi-component) relative path
; ESI = remaining path components like "a/b/c"
.cd_resolve_path:
        ; Extract next path component (up to '/' or null)
        mov edi, filename_buf
        xor ecx, ecx
.cd_copy_component:
        lodsb
        cmp al, '/'
        je .cd_component_end
        cmp al, 0
        je .cd_component_last
        stosb
        inc ecx
        cmp ecx, HBFS_MAX_FILENAME
        jl .cd_copy_component
.cd_component_end:
        mov byte [edi], 0
        cmp ecx, 0
        je .cd_resolve_path     ; empty component (double slash), skip it
        ; Check for ".." as intermediate component
        cmp byte [filename_buf], '.'
        jne .cd_comp_enter
        cmp byte [filename_buf + 1], 0
        je .cd_resolve_path     ; "." = current dir, skip it
        cmp byte [filename_buf + 1], '.'
        jne .cd_comp_enter
        cmp byte [filename_buf + 2], 0
        jne .cd_comp_enter
        ; ".." component - go up one level
        call .cd_pop_stack
        jmp .cd_resolve_path
.cd_comp_enter:
        ; Named directory component - enter it
        push esi                ; save remaining path
        call .cd_enter_subdir
        pop esi
        jc .cd_fail             ; subdirectory not found
        jmp .cd_resolve_path    ; process next component

.cd_component_last:
        mov byte [edi], 0
        cmp ecx, 0
        je .cd_success          ; trailing slash, already done
        ; Check for "." or ".." as final component
        cmp byte [filename_buf], '.'
        jne .cd_enter_last
        cmp byte [filename_buf + 1], 0
        je .cd_success          ; "." = current dir, done
        cmp byte [filename_buf + 1], '.'
        jne .cd_enter_last
        cmp byte [filename_buf + 2], 0
        jne .cd_enter_last
        call .cd_pop_stack
        jmp .cd_success

.cd_enter_last:
        call .cd_enter_subdir
        jc .cd_fail

.cd_success:
        mov dword [esp + 28], 0
        popad
        ret

.cd_fail:
        mov dword [esp + 28], -1
        popad
        ret

; Enter a subdirectory by name (filename_buf)
; Pushes current dir onto stack, switches to new dir
; Returns CF set on failure
.cd_enter_subdir:
        ; Load current directory and find the named entry
        call hbfs_load_root_dir
        push esi
        mov esi, filename_buf
        call hbfs_find_file
        pop esi
        jc .cd_sub_fail
        cmp byte [edi + DIRENT_TYPE], FTYPE_DIR
        jne .cd_sub_fail

        ; Push current directory onto stack
        mov eax, [dir_depth]
        cmp eax, DIR_STACK_MAX
        jge .cd_sub_fail        ; stack full

        ; Calculate stack entry address: dir_stack + depth * DIR_STACK_ENTRY_SIZE
        push edi                ; save dir entry pointer
        imul eax, DIR_STACK_ENTRY_SIZE
        lea ebx, [dir_stack + eax]
        ; Save current state
        mov eax, [current_dir_lba]
        mov [ebx], eax
        mov eax, [current_dir_sects]
        mov [ebx + 4], eax
        ; Copy current_dir_name
        push esi
        lea edi, [ebx + 8]
        mov esi, current_dir_name
.cd_push_name:
        lodsb
        stosb
        cmp al, 0
        jne .cd_push_name
        pop esi

        inc dword [dir_depth]
        pop edi                 ; restore dir entry pointer

        ; Switch to new directory
        mov eax, [edi + DIRENT_START_BLOCK]
        shl eax, 3              ; blocks to sectors
        add eax, HBFS_DATA_START
        mov [current_dir_lba], eax
        mov eax, [edi + DIRENT_BLOCK_COUNT]
        shl eax, 3
        mov [current_dir_sects], eax

        ; Copy directory name
        push esi
        mov esi, edi            ; entry name
        mov edi, current_dir_name
.cd_ent_name:
        lodsb
        stosb
        cmp al, 0
        jne .cd_ent_name
        pop esi

        ; Load the new directory contents
        mov eax, [current_dir_lba]
        mov ecx, [current_dir_sects]
        mov edi, hbfs_dir_buf
        call ata_read_sectors

        clc
        ret

.cd_sub_fail:
        stc
        ret

; Pop one level from the directory stack (cd ..)
; If already at root, do nothing
.cd_pop_stack:
        mov eax, [dir_depth]
        cmp eax, 0
        je .cd_pop_done         ; already at root

        dec eax
        mov [dir_depth], eax

        ; Calculate stack entry address
        imul eax, DIR_STACK_ENTRY_SIZE
        lea ebx, [dir_stack + eax]

        ; Restore parent directory state
        mov eax, [ebx]
        mov [current_dir_lba], eax
        mov eax, [ebx + 4]
        mov [current_dir_sects], eax

        ; Copy parent name back
        push esi
        lea esi, [ebx + 8]
        mov edi, current_dir_name
.cd_pop_name:
        lodsb
        stosb
        cmp al, 0
        jne .cd_pop_name
        pop esi

.cd_pop_done:
        ret

; Reset to root directory and clear stack
.cd_reset_to_root:
        mov dword [current_dir_lba], HBFS_ROOT_DIR_START
        mov dword [current_dir_sects], HBFS_ROOT_DIR_SECTS
        mov byte [current_dir_name], '/'
        mov byte [current_dir_name + 1], 0
        mov dword [dir_depth], 0
        ret

;=======================================================================
; NEW SYSCALL HANDLERS
;=======================================================================

sys_beep:
        cmp ebx, 0
        je .beep_off
        call speaker_beep
        xor eax, eax
        iretd
.beep_off:
        call speaker_off
        xor eax, eax
        iretd

sys_date:
        push esi
        call rtc_read_time
        mov al, [rtc_sec]
        mov [ebx], al
        mov al, [rtc_min]
        mov [ebx + 1], al
        mov al, [rtc_hour]
        mov [ebx + 2], al
        mov al, [rtc_day]
        mov [ebx + 3], al
        mov al, [rtc_month]
        mov [ebx + 4], al
        mov al, [rtc_year]
        mov [ebx + 5], al
        movzx eax, byte [rtc_century]
        imul eax, 100
        movzx ecx, byte [rtc_year]
        add eax, ecx
        pop esi
        iretd

sys_chdir:
        push esi
        push edi
        mov esi, ebx
        call cmd_cd_internal
        pop edi
        pop esi
        iretd

sys_getcwd:
        push edi
        mov edi, ebx            ; destination buffer
        call build_cwd_path
        pop edi
        xor eax, eax
        iretd

sys_serial:
        push esi
        mov esi, ebx
        call serial_print
        pop esi
        xor eax, eax
        iretd

sys_serial_in:
        call serial_getchar
        xor ah, ah          ; Clear high byte
        iretd

sys_getenv:
        push esi
        push edi
        mov esi, ebx
        call env_get
        pop edi
        pop esi
        iretd

;---------------------------------------
; SYS_GETARGS (32): Get command-line arguments
; EBX = pointer to destination buffer (max 512 bytes)
; Returns: EAX = length of argument string (0 if none)
;---------------------------------------
sys_getargs:
        push esi
        push edi
        push ecx
        mov esi, program_args_buf
        mov edi, ebx
        xor ecx, ecx
.ga_copy:
        lodsb
        stosb
        inc ecx
        test al, al
        jnz .ga_copy
        dec ecx                 ; Don't count null terminator
        mov eax, ecx
        pop ecx
        pop edi
        pop esi
        iretd

;--- Read entire file into buffer ---
; EBX = filename, ECX = buffer pointer
; Returns EAX = bytes read (0 if not found)
sys_fread:
        pushad
        mov esi, ebx            ; filename
        mov edi, [esp + 24]     ; recover original ECX (dest buffer) from pushad frame
        call hbfs_read_file     ; ESI=filename, EDI=dest buffer; returns ECX=size, CF on error
        jc .fread_fail
        ; ECX = file size returned by hbfs_read_file
        mov [esp + 28], ecx     ; set EAX in pushad frame to file size
        popad
        iretd
.fread_fail:
        popad
        xor eax, eax
        iretd

;--- Write entire file (create/overwrite) ---
; EBX = filename, ECX = buffer pointer, EDX = size
; Returns EAX = 0 on success
sys_fwrite:
        pushad
        ; Save parameters before hbfs_find_file clobbers them
        mov [.fw_name], ebx
        mov [.fw_buf], ecx
        mov [.fw_size], edx
        ; ESI = file type (0 = default to FTYPE_TEXT)
        cmp esi, FTYPE_TEXT
        jb .fw_default_type
        cmp esi, FTYPE_BATCH
        ja .fw_default_type
        mov [.fw_type], esi
        jmp .fw_type_set
.fw_default_type:
        mov dword [.fw_type], FTYPE_TEXT
.fw_type_set:
        ; Delete old file if it exists
        mov esi, ebx
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .fwrite_new
        ; EDI = dir entry pointer from hbfs_find_file
        call hbfs_delete_file_entry
.fwrite_new:
        ; Create new file with specified type
        mov esi, [.fw_name]     ; filename
        mov edi, [.fw_buf]      ; data buffer
        mov ecx, [.fw_size]     ; size
        mov edx, [.fw_type]
        call hbfs_create_file
        jc .fwrite_err
        mov dword [esp + 28], 0 ; set EAX = 0 in pushad frame
        popad
        iretd
.fwrite_err:
        popad
        mov eax, -1
        iretd
.fw_name: dd 0
.fw_buf:  dd 0
.fw_size: dd 0
.fw_type: dd 0

sys_open_fd:
        push esi
        push edi
        mov esi, ebx
        call fd_open
        pop edi
        pop esi
        iretd

sys_read_fd:
        push esi
        push edi
        call fd_read
        pop edi
        pop esi
        iretd

sys_write_fd:
        push esi
        push edi
        call fd_write
        pop edi
        pop esi
        iretd

sys_close_fd:
        push ebx
        call fd_close
        pop ebx
        xor eax, eax
        iretd

sys_seek_fd:
        push esi
        call fd_seek
        pop esi
        iretd

;---------------------------------------
; SYS_MKDIR (12): Create a subdirectory
; EBX = pointer to directory name
; Returns: EAX = 0 on success, -1 on error
;---------------------------------------
sys_mkdir:
        push esi
        mov esi, ebx
        call hbfs_mkdir
        pop esi
        jc .smk_err
        xor eax, eax
        iretd
.smk_err:
        mov eax, -1
        iretd

;---------------------------------------
; SYS_READDIR (13): Read directory entry
; EBX = buffer for filename, ECX = entry index (0-based)
; Returns: EAX = file type (0=free, -1=end), ECX = file size
;---------------------------------------
sys_readdir:
        push esi
        push edi
        push edx
        push ebp

        call hbfs_load_root_dir
        call hbfs_get_max_entries
        cmp ecx, eax
        jge .srd_end             ; index >= max entries

        ; Point to entry at index ECX
        mov edi, hbfs_dir_buf
        imul edx, ecx, HBFS_DIR_ENTRY_SIZE
        add edi, edx
        mov ebp, edi             ; save entry pointer

        cmp byte [ebp + DIRENT_TYPE], FTYPE_FREE
        je .srd_free

        ; Copy filename to user buffer (EBX)
        mov esi, ebp
        mov edi, ebx
.srd_cpn:
        lodsb
        stosb
        test al, al
        jnz .srd_cpn

        ; Return type in EAX and size in ECX
        movzx eax, byte [ebp + DIRENT_TYPE]
        mov ecx, [ebp + DIRENT_SIZE]

        pop ebp
        pop edx
        pop edi
        pop esi
        iretd

.srd_free:
        ; Empty slot - return type 0
        mov byte [ebx], 0
        xor eax, eax
        xor ecx, ecx
        pop ebp
        pop edx
        pop edi
        pop esi
        iretd

.srd_end:
        ; Past end of directory
        mov byte [ebx], 0
        mov eax, -1
        xor ecx, ecx
        pop ebp
        pop edx
        pop edi
        pop esi
        iretd

;=======================================================================
; NEW SHELL COMMANDS
;=======================================================================

;---------------------------------------
; Command: echo - Print text with $VAR expansion
; Flags: -n  no trailing newline
;---------------------------------------
cmd_echo:
        pushad
        mov edi, expand_buf
        call env_expand
        mov esi, expand_buf
        call vga_print
        ; Check for -n flag (suppress newline)
        mov eax, 13             ; 'n' - 'a'
        call test_flag
        jc .echo_done
        mov al, 0x0A
        call vga_putchar
.echo_done:
        popad
        ret

;---------------------------------------
; Command: wc - Count lines, words, bytes
; Flags: -l  show only line count
;        -w  show only word count
;        -c  show only byte count
;        (no flags = show all three)
;---------------------------------------
cmd_wc_file:
        pushad
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .wc_nf
        mov [.wc_bytes], ecx
        mov esi, PROGRAM_BASE
        xor ebx, ebx
        xor edx, edx
        xor ebp, ebp
.wc_loop:
        cmp ecx, 0
        je .wc_print
        dec ecx
        lodsb
        cmp al, 0x0A
        jne .wc_nnl
        inc ebx
.wc_nnl:
        cmp al, ' '
        je .wc_sp
        cmp al, 0x09
        je .wc_sp
        cmp al, 0x0A
        je .wc_sp
        cmp al, 0x0D
        je .wc_sp
        cmp ebp, 0
        jne .wc_loop
        mov ebp, 1
        inc edx
        jmp .wc_loop
.wc_sp:
        xor ebp, ebp
        jmp .wc_loop
.wc_print:
        ; Check if any specific flag is set
        mov eax, 11             ; 'l' - 'a'
        call test_flag
        jc .wc_only_lines
        mov eax, 22             ; 'w' - 'a'
        call test_flag
        jc .wc_only_words
        mov eax, 2              ; 'c' - 'a'
        call test_flag
        jc .wc_only_bytes
        ; No flags - print all
        push edx
        mov eax, ebx
        call vga_print_dec
        mov al, ' '
        call vga_putchar
        pop eax
        call vga_print_dec
        mov al, ' '
        call vga_putchar
        mov eax, [.wc_bytes]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar
        popad
        ret
.wc_only_lines:
        mov eax, ebx
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar
        popad
        ret
.wc_only_words:
        mov eax, edx
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar
        popad
        ret
.wc_only_bytes:
        mov eax, [.wc_bytes]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar
        popad
        ret
.wc_nf:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.wc_bytes: dd 0

;---------------------------------------
; Command: find - Search for pattern in file
; Flags: -i  case-insensitive matching
;        -c  count matches only (don't print lines)
;        -n  print line numbers (default: on, this is for compat)
;---------------------------------------
cmd_find_file:
        pushad
        mov edi, filename_buf
        call copy_word
        call skip_spaces
        push esi
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        pop esi
        jc .find_nf
        mov byte [PROGRAM_BASE + ecx], 0
        mov esi, PROGRAM_BASE
        xor ebx, ebx           ; line counter
        xor ebp, ebp           ; match counter
.find_line:
        inc ebx
        mov edi, esi
.find_eol:
        cmp byte [esi], 0
        je .find_end
        cmp byte [esi], 0x0A
        je .found_eol
        inc esi
        jmp .find_eol
.found_eol:
        mov byte [esi], 0
        push esi
        mov esi, edi
        ; Check for -i flag (case-insensitive)
        mov eax, 8              ; 'i' - 'a'
        call test_flag
        jc .find_ci
        call str_find_substr
        jmp .find_checked
.find_ci:
        call str_find_substr_ci
.find_checked:
        pop esi
        jc .find_nxt
        inc ebp
        ; Check for -c flag (count only)
        mov eax, 2              ; 'c' - 'a'
        call test_flag
        jc .find_nxt
        push esi
        push edi
        mov byte [vga_color], COLOR_INFO
        mov eax, ebx
        call vga_print_dec
        mov al, ':'
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
        mov esi, edi
        call vga_print
        mov al, 0x0A
        call vga_putchar
        pop edi
        pop esi
.find_nxt:
        mov byte [esi], 0x0A
        inc esi
        jmp .find_line
.find_end:
        cmp edi, esi
        je .find_done2
        push esi
        mov esi, edi
        ; Check for -i flag
        mov eax, 8
        call test_flag
        jc .find_end_ci
        call str_find_substr
        jmp .find_end_checked
.find_end_ci:
        call str_find_substr_ci
.find_end_checked:
        pop esi
        jc .find_done2
        inc ebp
        mov eax, 2              ; -c
        call test_flag
        jc .find_done2
        mov byte [vga_color], COLOR_INFO
        mov eax, ebx
        call vga_print_dec
        mov al, ':'
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
        mov esi, edi
        call vga_print
        mov al, 0x0A
        call vga_putchar
.find_done2:
        ; If -c flag, print match count
        mov eax, 2
        call test_flag
        jnc .find_exit
        mov eax, ebp
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar
.find_exit:
        popad
        ret
.find_nf:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

; Substring search: find filename_buf in string at ESI
; Returns: CF clear=found, CF set=not found
str_find_substr:
        push esi
        push edi
.sfs_loop:
        cmp byte [esi], 0
        je .sfs_no
        push esi
        mov edi, filename_buf
.sfs_cmp:
        cmp byte [edi], 0
        je .sfs_yes
        mov al, [esi]
        cmp al, 0
        je .sfs_cmpfail
        cmp al, [edi]
        jne .sfs_cmpfail
        inc esi
        inc edi
        jmp .sfs_cmp
.sfs_yes:
        pop esi
        pop edi
        pop esi
        clc
        ret
.sfs_cmpfail:
        pop esi
        inc esi
        jmp .sfs_loop
.sfs_no:
        pop edi
        pop esi
        stc
        ret

; Case-insensitive substring search: find filename_buf in string at ESI
; Returns: CF clear=found, CF set=not found
str_find_substr_ci:
        push esi
        push edi
.sfci_loop:
        cmp byte [esi], 0
        je .sfci_no
        push esi
        mov edi, filename_buf
.sfci_cmp:
        cmp byte [edi], 0
        je .sfci_yes
        mov al, [esi]
        cmp al, 0
        je .sfci_fail
        mov ah, [edi]
        ; Lowercase both
        cmp al, 'A'
        jl .sfci_a_ok
        cmp al, 'Z'
        jg .sfci_a_ok
        or al, 0x20
.sfci_a_ok:
        cmp ah, 'A'
        jl .sfci_b_ok
        cmp ah, 'Z'
        jg .sfci_b_ok
        or ah, 0x20
.sfci_b_ok:
        cmp al, ah
        jne .sfci_fail
        inc esi
        inc edi
        jmp .sfci_cmp
.sfci_yes:
        pop esi
        pop edi
        pop esi
        clc
        ret
.sfci_fail:
        pop esi
        inc esi
        jmp .sfci_loop
.sfci_no:
        pop edi
        pop esi
        stc
        ret

cmd_append_file:
        pushad
        mov edi, filename_buf
        call copy_word
        push esi
        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .app_new
        mov [.app_sz], ecx
        jmp .app_prompt
.app_new:
        mov dword [.app_sz], 0
        mov byte [last_file_type], FTYPE_FREE
.app_prompt:
        pop esi
        mov esi, msg_write_prompt
        call vga_print
.app_rdln:
        mov edi, temp_line_buf
        xor ecx, ecx
.app_ch:
        call kb_getchar
        cmp al, 0x0D
        je .app_eol
        cmp al, 0x0A
        je .app_eol
        cmp al, 0x08
        je .app_bs
        mov [edi], al
        inc edi
        inc ecx
        call vga_putchar
        jmp .app_ch
.app_bs:
        or ecx, ecx
        jz .app_ch
        dec edi
        dec ecx
        mov al, 0x08
        call vga_putchar
        jmp .app_ch
.app_eol:
        mov byte [edi], 0
        mov al, 0x0A
        call vga_putchar
        cmp ecx, 0
        je .app_save
        mov esi, temp_line_buf
        mov edi, PROGRAM_BASE
        add edi, [.app_sz]
.app_cp:
        lodsb
        stosb
        cmp al, 0
        jne .app_cp
        dec edi
        mov byte [edi], 0x0A
        inc edi
        sub edi, PROGRAM_BASE
        mov [.app_sz], edi
        jmp .app_rdln
.app_save:
        mov esi, filename_buf
        mov ecx, [.app_sz]
        mov edi, PROGRAM_BASE
        ; Preserve file type if existing, else default to text
        movzx edx, byte [last_file_type]
        cmp edx, FTYPE_FREE
        jne .app_type_ok
        mov edx, FTYPE_TEXT
.app_type_ok:
        call hbfs_create_file
        jc .app_err
        mov esi, msg_saved
        call vga_print
        popad
        ret
.app_err:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_write_err
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.app_sz: dd 0

cmd_date_show:
        pushad
        call rtc_read_time
        movzx eax, byte [rtc_century]
        imul eax, 100
        movzx ecx, byte [rtc_year]
        add eax, ecx
        call vga_print_dec
        mov al, '-'
        call vga_putchar
        movzx eax, byte [rtc_month]
        call .p2d
        mov al, '-'
        call vga_putchar
        movzx eax, byte [rtc_day]
        call .p2d
        mov al, ' '
        call vga_putchar
        movzx eax, byte [rtc_hour]
        call .p2d
        mov al, ':'
        call vga_putchar
        movzx eax, byte [rtc_min]
        call .p2d
        mov al, ':'
        call vga_putchar
        movzx eax, byte [rtc_sec]
        call .p2d
        mov al, 0x0A
        call vga_putchar
        popad
        ret
.p2d:
        cmp eax, 10
        jge .p2d_ok
        push eax
        mov al, '0'
        call vga_putchar
        pop eax
.p2d_ok:
        call vga_print_dec
        ret

cmd_beep:
        pushad
        mov ebx, 1000
        mov ecx, 20
        call speaker_beep
        popad
        ret

cmd_exec_batch:
        pushad

        ; Guard against nested batch execution (shared buffers would corrupt)
        cmp byte [batch_running], 1
        jne .bat_not_nested
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_batch_nested
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.bat_not_nested:
        mov byte [batch_running], 1

        mov edi, batch_script_buf
        call hbfs_read_file
        jc .bat_nf
        ; Clamp script to buffer size
        cmp ecx, BATCH_BUFFER_SIZE - 1
        jle .bat_size_ok
        mov ecx, BATCH_BUFFER_SIZE - 1
.bat_size_ok:
        mov byte [batch_script_buf + ecx], 0
        mov esi, batch_script_buf
.bat_line:
        cmp byte [esi], 0
        je .bat_done
        mov edi, batch_line_buf
        xor ecx, ecx
.bat_cpln:
        lodsb
        cmp al, 0x0A
        je .bat_exec
        cmp al, 0x0D
        je .bat_cpln
        cmp al, 0
        je .bat_exec_last
        stosb
        inc ecx
        jmp .bat_cpln
.bat_exec_last:
        dec esi
.bat_exec:
        mov byte [edi], 0
        cmp ecx, 0
        je .bat_line
        push esi
        mov byte [vga_color], COLOR_INFO
        mov esi, msg_batch_prefix
        call vga_print
        mov esi, batch_line_buf
        call vga_print
        mov al, 0x0A
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
        mov esi, batch_line_buf
        call shell_parse_cmd
        pop esi
        jmp .bat_line
.bat_done:
        mov byte [batch_running], 0
        popad
        ret
.bat_nf:
        mov byte [batch_running], 0
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

cmd_mkdir:
        pushad
        mov edi, filename_buf
        call copy_word
        mov esi, filename_buf
        cmp byte [esi], 0
        je .mk_nospace
        call hbfs_mkdir
        jc .mk_fail
        mov esi, msg_dir_created
        call vga_print
        popad
        ret
.mk_fail:
        ; Check if it already exists
        mov esi, filename_buf
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .mk_nospace
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_already_exists
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.mk_nospace:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_write_err
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

cmd_cd:
        pushad
        call cmd_cd_internal
        cmp eax, -1             ; cmd_cd_internal returns 0 or -1 in EAX
        jne .cd_ok
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
.cd_ok:
        popad
        ret

cmd_pwd:
        pushad
        mov edi, path_search_buf
        call build_cwd_path
        mov esi, path_search_buf
        call vga_print
        mov al, 0x0A
        call vga_putchar
        popad
        ret

cmd_set_env:
        pushad
        cmp byte [esi], 0
        je .set_show
        mov edi, env_temp_buf
.set_cp:
        lodsb
        cmp al, ' '
        je .set_space
        stosb
        cmp al, 0
        jne .set_cp
        jmp .set_check
.set_space:
        mov byte [edi], '='
        inc edi
.set_rest:
        lodsb
        stosb
        cmp al, 0
        jne .set_rest
.set_check:
        mov esi, env_temp_buf
        call env_set_str
        popad
        ret
.set_show:
        mov edi, env_table
        mov ecx, ENV_MAX
.set_loop:
        cmp byte [edi], 0
        je .set_skip
        push esi
        mov esi, edi
        call vga_print
        mov al, 0x0A
        call vga_putchar
        pop esi
.set_skip:
        add edi, ENV_ENTRY_SIZE
        loop .set_loop
        popad
        ret

cmd_unset_env:
        pushad
        call env_unset
        popad
        ret

;=======================================================================
; TAB COMPLETION
;=======================================================================

shell_tab_complete:
        push eax
        push ebx
        push edx
        push esi
        push edi
        push ebp
        mov byte [line_buffer + ecx], 0
        mov [.tc_count], ecx
        call hbfs_load_root_dir
        mov edi, hbfs_dir_buf
        xor ebx, ebx
        xor ebp, ebp
        call hbfs_get_max_entries
        mov edx, eax
.tc_scan:
        cmp edx, 0
        je .tc_result
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .tc_next
        push esi
        push edi
        mov esi, line_buffer
.tc_cmp:
        mov al, [esi]
        cmp al, 0
        je .tc_match
        cmp al, [edi]
        jne .tc_nomatch
        inc esi
        inc edi
        jmp .tc_cmp
.tc_match:
        pop edi
        pop esi
        inc ebx
        mov ebp, edi
        jmp .tc_next
.tc_nomatch:
        pop edi
        pop esi
.tc_next:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec edx
        jmp .tc_scan
.tc_result:
        cmp ebx, 0
        je .tc_done
        cmp ebx, 1
        je .tc_single
        mov al, 0x0A
        call vga_putchar
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov edx, eax
.tc_show:
        cmp edx, 0
        je .tc_reprint
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .tc_snext
        push esi
        push edi
        mov esi, line_buffer
.tc_scmp:
        mov al, [esi]
        cmp al, 0
        je .tc_syes
        cmp al, [edi]
        jne .tc_sno
        inc esi
        inc edi
        jmp .tc_scmp
.tc_syes:
        pop edi
        pop esi
        push esi
        mov esi, edi
        call vga_print
        mov al, ' '
        call vga_putchar
        call vga_putchar
        pop esi
        jmp .tc_snext
.tc_sno:
        pop edi
        pop esi
.tc_snext:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec edx
        jmp .tc_show
.tc_reprint:
        mov al, 0x0A
        call vga_putchar
        push esi
        mov byte [vga_color], COLOR_PROMPT
        mov esi, shell_prompt_pre
        call vga_print
        ; Build and print full CWD path
        mov edi, path_search_buf
        call build_cwd_path
        mov esi, path_search_buf
        call vga_print
        mov esi, shell_prompt_post
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        mov esi, line_buffer
        call vga_print
        pop esi
        jmp .tc_done
.tc_single:
        mov esi, ebp
        mov ecx, [.tc_count]
        add esi, ecx
.tc_type:
        lodsb
        cmp al, 0
        je .tc_space
        mov [line_buffer + ecx], al
        inc ecx
        call vga_putchar
        jmp .tc_type
.tc_space:
        mov byte [line_buffer + ecx], ' '
        inc ecx
        mov al, ' '
        call vga_putchar
        mov [.tc_count], ecx
.tc_done:
        mov ecx, [.tc_count]
        pop ebp
        pop edi
        pop esi
        pop edx
        pop ebx
        pop eax
        ret
.tc_count: dd 0

;=======================================================================
; UTILITY: String copy ESI -> EDI
;=======================================================================

;=======================================================================
; UTILITY: Build full CWD path into buffer
; EDI = destination buffer (must be >= 256 bytes)
; Writes null-terminated full path like "/a/b/c" or "/"
;=======================================================================
build_cwd_path:
        push esi
        push ecx
        push eax
        push ebx

        ; If at root, just write "/"
        cmp dword [dir_depth], 0
        jne .bcp_build
        mov byte [edi], '/'
        mov byte [edi + 1], 0
        jmp .bcp_done

.bcp_build:
        xor ecx, ecx           ; stack index
.bcp_loop:
        cmp ecx, [dir_depth]
        jge .bcp_current
        mov eax, ecx
        imul eax, DIR_STACK_ENTRY_SIZE
        lea esi, [dir_stack + eax + 8]
        ; Skip root "/" entry
        cmp byte [esi], '/'
        jne .bcp_add_comp
        inc ecx
        jmp .bcp_loop
.bcp_add_comp:
        mov byte [edi], '/'
        inc edi
.bcp_cp_stack:
        lodsb
        cmp al, 0
        je .bcp_stack_done
        stosb
        jmp .bcp_cp_stack
.bcp_stack_done:
        inc ecx
        jmp .bcp_loop

.bcp_current:
        ; Append current directory
        mov byte [edi], '/'
        inc edi
        mov esi, current_dir_name
.bcp_cp_cur:
        lodsb
        stosb
        cmp al, 0
        jne .bcp_cp_cur
        ; Null terminator already written

.bcp_done:
        pop ebx
        pop eax
        pop ecx
        pop esi
        ret

;=======================================================================
; PATH SEARCH: Save and restore current directory state
; Used when searching PATH directories so we can temporarily cd
; into each path component without losing the user's cwd.
;=======================================================================

path_save_cwd:
        pushad
        ; Save scalar state
        mov eax, [current_dir_lba]
        mov [path_save_lba], eax
        mov eax, [current_dir_sects]
        mov [path_save_sects], eax
        mov eax, [dir_depth]
        mov [path_save_depth], eax
        ; Copy current_dir_name
        mov esi, current_dir_name
        mov edi, path_save_name
        call str_copy
        ; Copy dir_stack
        mov esi, dir_stack
        mov edi, path_save_stack
        mov ecx, DIR_STACK_MAX * DIR_STACK_ENTRY_SIZE
        rep movsb
        popad
        ret

path_restore_cwd:
        pushad
        ; Restore scalar state
        mov eax, [path_save_lba]
        mov [current_dir_lba], eax
        mov eax, [path_save_sects]
        mov [current_dir_sects], eax
        mov eax, [path_save_depth]
        mov [dir_depth], eax
        ; Copy name back
        mov esi, path_save_name
        mov edi, current_dir_name
        call str_copy
        ; Copy dir_stack back
        mov esi, path_save_stack
        mov edi, dir_stack
        mov ecx, DIR_STACK_MAX * DIR_STACK_ENTRY_SIZE
        rep movsb
        popad
        ret

; File-level CWD save/restore (separate from path_save_cwd)
; Used by hbfs_read_file for path resolution without conflicting
; with the PATH search in cmd_exec_program
file_save_cwd:
        pushad
        mov eax, [current_dir_lba]
        mov [file_save_lba], eax
        mov eax, [current_dir_sects]
        mov [file_save_sects], eax
        mov eax, [dir_depth]
        mov [file_save_depth], eax
        mov esi, current_dir_name
        mov edi, file_save_name
        call str_copy
        mov esi, dir_stack
        mov edi, file_save_stack
        mov ecx, DIR_STACK_MAX * DIR_STACK_ENTRY_SIZE
        rep movsb
        popad
        ret

file_restore_cwd:
        pushad
        mov eax, [file_save_lba]
        mov [current_dir_lba], eax
        mov eax, [file_save_sects]
        mov [current_dir_sects], eax
        mov eax, [file_save_depth]
        mov [dir_depth], eax
        mov esi, file_save_name
        mov edi, current_dir_name
        call str_copy
        mov esi, file_save_stack
        mov edi, dir_stack
        mov ecx, DIR_STACK_MAX * DIR_STACK_ENTRY_SIZE
        rep movsb
        popad
        ret

;=======================================================================
; UTILITY: String copy ESI -> EDI
;=======================================================================

str_copy:
        lodsb
        stosb
        cmp al, 0
        jne str_copy
        ret

;=======================================================================
; STRING DATA
;=======================================================================

banner_str:
        db COLOR_HEADER
        db "          Mellivora OS - 32-bit Protected Mode Operating System          ", 0x0A
        db "       HB DOS v1.4 - Honey Badger Disk Operating System / i486+          ", 0x0A, 0

version_text:
        db "Mellivora OS v1.4", 0x0A
        db "  Kernel:        Mellivora (32-bit protected mode, ring 0/3)", 0x0A
        db "  Shell:         HB DOS (Honey Badger Disk Operating System)", 0x0A
        db "  Address Space: 4 GB flat memory model", 0x0A
        db "  Disk:          ATA PIO, LBA48 (up to 128 PB)", 0x0A
        db "  Filesystem:    HBFS (Honey Badger File System)", 0x0A
        db "    Block size:  4096 bytes", 0x0A
        db "    Max files:   227 root / 56 per subdirectory", 0x0A
        db "    Max name:    252 characters", 0x0A
        db "    Multi-block: Files can span multiple blocks", 0x0A
        db "    Subdirs:     mkdir / cd / pwd", 0x0A
        db "  Timer:         PIT at 100 Hz", 0x0A
        db "  Keyboard:      PS/2 with shift/ctrl support", 0x0A
        db "  Display:       VGA 80x25 text, 16 colors", 0x0A
        db "  Serial:        COM1 at 115200 baud", 0x0A
        db "  Speaker:       PC speaker (beep)", 0x0A
        db "  Syscalls:      INT 0x80 (34 syscalls)", 0x0A
        db "  Shell:         Tab completion, history, Ctrl+C abort", 0x0A
        db "  Programs:      Flat binary + ELF32 (ring 3)", 0x0A
        db "  Arguments:     Command-line args via SYS_GETARGS", 0x0A
        db "  Env vars:      set/unset with $VAR expansion", 0x0A
        db "  File I/O:      open/read/write/close/seek (fd)", 0x0A
        db "  Scripts:       Batch file execution", 0x0A
        db "  Security:      Raw disk access denied to ring 3", 0x0A, 0

shell_prompt_pre:  db "HBDOS:", 0
shell_prompt_post: db "> ", 0
default_path:      db "PATH=/bin:/games", 0
path_env_name:     db "PATH", 0

; Command strings
cmd_help_str:   db "help", 0
cmd_ver_str:    db "ver", 0
cmd_clear_str:  db "clear", 0
cmd_dir_str:    db "dir", 0
cmd_ls_str:     db "ls", 0
cmd_del_str:    db "del", 0
cmd_rm_str:     db "rm", 0
cmd_format_str: db "format", 0
cmd_cat_str:    db "cat", 0
cmd_write_str:  db "write", 0
cmd_hex_str:    db "hex", 0
cmd_mem_str:    db "mem", 0
cmd_time_str:   db "time", 0
cmd_disk_str:   db "disk", 0
cmd_run_str:    db "run", 0
cmd_enter_str:  db "enter", 0
cmd_copy_str:   db "copy", 0
cmd_ren_str:    db "ren", 0
cmd_mv_str:     db "mv", 0
cmd_move_str:   db "move", 0
cmd_df_str:     db "df", 0
cmd_more_str:   db "more", 0
cmd_echo_str:   db "echo", 0
cmd_wc_str:     db "wc", 0
cmd_find_str:   db "find", 0
cmd_append_str: db "append", 0
cmd_date_str:   db "date", 0
cmd_beep_str:   db "beep", 0
cmd_batch_str:  db "batch", 0
cmd_mkdir_str:  db "mkdir", 0
cmd_cd_str:     db "cd", 0
cmd_pwd_str:    db "pwd", 0
cmd_touch_str:  db "touch", 0
cmd_set_str:    db "set", 0
cmd_unset_str:  db "unset", 0
cmd_shutdown_str: db "shutdown", 0
cmd_cls_str:    db "cls", 0
cmd_head_str:   db "head", 0
cmd_tail_str:   db "tail", 0
cmd_type_str:   db "type", 0
cmd_diff_str:   db "diff", 0
cmd_uniq_str:   db "uniq", 0
cmd_rev_str:    db "rev", 0
cmd_tac_str:    db "tac", 0
cmd_alias_str:  db "alias", 0
cmd_history_str: db "history", 0
cmd_which_str:  db "which", 0
cmd_sleep_str:  db "sleep", 0
cmd_color_str:  db "color", 0
cmd_size_str:   db "size", 0
cmd_strings_str: db "strings", 0

; Help text
help_text:
        db "HB DOS Commands:", 0x0A
        db "  help       - Show this help", 0x0A
        db "  ver        - System information", 0x0A
        db "  clear/cls  - Clear screen", 0x0A
        db "  dir/ls [-l]- List files (-l for long format)", 0x0A
        db "  cat [-n] F - Display file (-n for line numbers)", 0x0A
        db "  type FILE  - Alias for cat", 0x0A
        db "  head [-n N] F  - Show first N lines (default 10)", 0x0A
        db "  tail [-n N] F  - Show last N lines (default 10)", 0x0A
        db "  more [-n N] F  - Page through file (-n N page size)", 0x0A
        db "  wc [-lwc] F    - Line/word/byte count", 0x0A
        db "  find [-ic] P F - Search pattern P in file F", 0x0A
        db "             (-i case-insensitive, -c count only)", 0x0A
        db "  write F    - Write text to file (end with empty line)", 0x0A
        db "  append F   - Append text to existing file", 0x0A
        db "  touch F    - Create empty file", 0x0A
        db "  hex [-n N] F   - Hexdump a file (-n N byte limit)", 0x0A
        db "  del/rm [-v] F  - Delete a file (-v verbose)", 0x0A
        db "             (wildcards: * and ? supported)", 0x0A
        db "  copy [-v] S D  - Copy file (-v verbose)", 0x0A
        db "             (wildcards in S; use * in D for multi-match)", 0x0A
        db "  ren O N    - Rename file O to N", 0x0A
        db "  mv/move    - Alias for ren", 0x0A
        db "  run FILE   - Execute a program", 0x0A
        db "  enter      - Enter hex bytes as program", 0x0A
        db "  echo [-n] T    - Print text (-n no newline, $VAR)", 0x0A
        db "  date       - Show current date and time", 0x0A
        db "  beep       - Play a beep", 0x0A
        db "  batch F    - Execute script file", 0x0A
        db "  mkdir D    - Create directory", 0x0A
        db "  cd DIR     - Change directory", 0x0A
        db "  pwd        - Print working directory", 0x0A
        db "  set N V    - Set environment variable", 0x0A
        db "  unset N    - Remove environment variable", 0x0A
        db "  shutdown   - Shut down the system", 0x0A
        db "  format     - Format filesystem (WARNING: erases all data)", 0x0A
        db "  mem        - Memory information", 0x0A
        db "  disk       - Disk information", 0x0A
        db "  df         - Filesystem usage", 0x0A
        db "  time       - Uptime", 0x0A
        db "  diff F1 F2 - Compare two files line by line", 0x0A
        db "  uniq [-cd] F   - Remove adjacent duplicates", 0x0A
        db "             (-c count, -d duplicates only)", 0x0A
        db "  rev F      - Reverse each line", 0x0A
        db "  tac F      - Print file in reverse line order", 0x0A
        db "  alias [N C]- Define alias N for command C", 0x0A
        db "  history    - Show command history", 0x0A
        db "  which N    - Show if N is built-in or program", 0x0A
        db "  sleep N    - Sleep N seconds", 0x0A
        db "  color FG [BG] - Set text color (hex 0-F)", 0x0A
        db "  size F     - Show file size and type", 0x0A
        db "  strings F  - Extract printable strings from file", 0x0A
        db "  <name>     - Run program (searches PATH)", 0x0A
        db "  Tab        - Auto-complete filename", 0x0A
        db "  Ctrl+C     - Cancel current input", 0x0A
        db 0x0A
        db "PATH: set PATH /bin:/games to search directories.", 0x0A
        db "      Programs in PATH dirs run from any directory.", 0x0A, 0

; Messages
msg_exception:  db "*** CPU EXCEPTION ***", 0x0A, 0
msg_exc_eip:    db "  EIP: 0x", 0
msg_exc_err:    db "  Error code: 0x", 0
msg_ata_ok:     db "  ATA disk:      ", 0
msg_ata_none:   db "  ATA disk:      not detected", 0x0A, 0
msg_shutdown_bar: db "===========================================================", 0x0A, 0
msg_shutdown:   db "  Shutting down Mellivora OS...", 0x0A, 0
msg_halt:       db "  System halted. You may power off now.", 0x0A, 0
msg_mb:         db " MB", 0x0A, 0
msg_cpu:        db "  CPU:           i486+ (32-bit protected mode)", 0x0A, 0
msg_mem_detect: db "  RAM:           ", 0
msg_mem_total:  db "  Total memory:  ", 0
msg_mem_header: db "=== Memory Information ===", 0x0A, 0
msg_free_pages: db "  Free pages:    ", 0
msg_pages_suffix: db " (4KB each)", 0x0A, 0
msg_free_mem:   db "  Free memory:   ", 0
msg_tick_count: db "  Timer ticks:   ", 0
msg_df_header:  db "=== HBFS Filesystem Usage ===", 0x0A, 0
msg_df_total:   db "  Total blocks:  ", 0
msg_df_used:    db "  Used blocks:   ", 0
msg_df_free:    db "  Free blocks:   ", 0
msg_df_avail:   db "  Free space:    ", 0
msg_df_blocks:  db " (4KB each)", 0x0A, 0
msg_df_kb:      db " KB", 0x0A, 0
msg_df_files:   db "  Files:         ", 0
msg_df_in:      db " in ", 0
msg_df_dirs:    db " directories", 0x0A, 0
msg_more_prompt: db "-- MORE -- (Space/Enter=next, q/ESC=quit)", 0
msg_more_erase: db "                                          ", 0
msg_disk_header: db "=== Disk Information ===", 0x0A, 0
msg_disk_sectors: db "  Total sectors: ", 0
msg_disk_size:  db "  Disk size:     ", 0
msg_hbfs_found:  db "  Filesystem:    HBFS detected", 0x0A, 0
msg_hbfs_format: db "  Filesystem:    Formatting new HBFS...", 0x0A, 0
msg_hbfs_formatted: db "  Filesystem formatted.", 0x0A, 0
msg_hbfs_nodisk: db "  Filesystem:    No disk available", 0x0A, 0
dir_header:     db "Type       Size  Name", 0x0A, 0
dir_separator:  db "----- --------- ----------------", 0x0A, 0
type_text:      db "text ", 0
type_batch:     db "batch", 0
type_exec:      db "exec ", 0
type_dir:       db "dir  ", 0
msg_dir_count:  db "  ", 0
msg_dir_files:  db " file(s)", 0x0A, 0
msg_not_found:  db "File not found", 0x0A, 0
msg_deleted:    db "Deleted.", 0x0A, 0
msg_saved:      db "Saved.", 0x0A, 0
msg_copied:     db "Copied.", 0x0A, 0
msg_renamed:    db "Renamed.", 0x0A, 0
msg_copy_usage: db "Usage: copy SRC DEST", 0x0A, 0
msg_ren_usage:  db "Usage: ren|mv OLD NEW", 0x0A, 0
msg_wild_needs_star: db "For multiple wildcard matches, destination must include '*'.", 0x0A, 0
msg_write_prompt: db "Type content (empty line to end):", 0x0A, 0
msg_write_err:  db "Write error!", 0x0A, 0
msg_unknown_cmd: db "Unknown command. Type 'help'.", 0x0A, 0
msg_touch_usage: db "Usage: touch <filename>", 0x0A, 0
msg_head_usage: db "Usage: head [N] <filename>", 0x0A, 0
msg_tail_usage: db "Usage: tail [N] <filename>", 0x0A, 0
msg_format_confirm: db "Format will erase all data. Continue? (y/N) ", 0
msg_cancelled:  db "Cancelled.", 0x0A, 0
msg_uptime:     db "Uptime: ", 0
msg_seconds:    db " seconds", 0x0A, 0
msg_serial_info: db "  Serial:        COM1 @ 115200 baud", 0x0A, 0
msg_exec:       db "Executing...", 0x0A, 0
msg_exec_done:  db "Program exited.", 0x0A, 0
msg_ctrl_c:     db "^C", 0x0A, 0
msg_batch_prefix: db "> ", 0
msg_batch_nested: db "Error: nested batch execution not supported", 0x0A, 0
msg_dir_created: db "Directory created.", 0x0A, 0
msg_already_exists: db "Already exists.", 0x0A, 0
msg_env_set:    db "", 0
msg_del_prefix: db "  deleted: ", 0
msg_copy_prefix: db "  ", 0
msg_copy_arrow: db " -> ", 0
msg_diff_header1: db "--- ", 0
msg_diff_header2: db "+++ ", 0
msg_diff_line:  db "@@ line ", 0
msg_diff_same:  db "Files are identical.", 0x0A, 0
msg_builtin:    db " is a shell built-in command", 0x0A, 0
msg_external:   db " (external)", 0x0A, 0
msg_not_found_w: db ": not found", 0x0A, 0
msg_is_str:     db " is ", 0
msg_alias_set:  db "Alias set.", 0x0A, 0
msg_size_bytes: db " bytes, ", 0
msg_size_blocks: db " blocks, type: ", 0
msg_type_text:  db "text", 0x0A, 0
msg_type_dir:   db "directory", 0x0A, 0
msg_type_exec:  db "executable", 0x0A, 0
msg_type_batch: db "batch script", 0x0A, 0
msg_type_unknown: db "unknown", 0x0A, 0
msg_sleeping:   db "Sleeping ", 0
msg_sleep_sec:  db " seconds...", 0x0A, 0
msg_color_set:  db "Color set.", 0x0A, 0
msg_color_usage: db "Usage: color FG [BG] (hex 0-F)", 0x0A, 0
msg_alias_usage: db "Usage: alias NAME COMMAND", 0x0A, 0
msg_no_aliases: db "No aliases defined.", 0x0A, 0
msg_need_filename: db "Error: filename required", 0x0A, 0
msg_need_2files: db "Usage: diff FILE1 FILE2", 0x0A, 0
msg_file_not_found: db "File not found", 0x0A, 0
msg_no_history: db "No command history.", 0x0A, 0
msg_table_full: db "Alias table full.", 0x0A, 0
msg_file_colon: db ": ", 0
msg_sleep_usage: db "Usage: sleep SECONDS", 0x0A, 0

;=======================================================================
; SCANCODE TRANSLATION TABLES
;=======================================================================

; US keyboard scancode -> ASCII (unshifted)
scancode_table:
        ;       0     1     2     3     4     5     6     7
        db   0,  27, '1', '2', '3', '4', '5', '6'     ; 0x00-0x07
        db '7', '8', '9', '0', '-', '=',0x08,0x09     ; 0x08-0x0F (BS, TAB)
        db 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i'    ; 0x10-0x17
        db 'o', 'p', '[', ']',0x0D,  0,  'a', 's'     ; 0x18-0x1F (Enter, LCtrl)
        db 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';'    ; 0x20-0x27
        db  39, '`',  0, '\', 'z', 'x', 'c', 'v'      ; 0x28-0x2F (', `, LShift, \)
        db 'b', 'n', 'm', ',', '.', '/',  0,  '*'     ; 0x30-0x37 (RShift, KP*)
        db   0, ' ',  0,   0,   0,   0,   0,   0      ; 0x38-0x3F (LAlt, Space, Caps, F1-F5)
        db   0,   0,   0,   0,   0,   0,   0,  '7'    ; 0x40-0x47 (F6-F10, NumLock, ScrLock, KP7)
        db 0x80,'9', '-',0x82,'5',0x83,'+', '1'        ; 0x48-0x4F (Up,KP9,-,Left,KP5,Right,+,KP1)
        db 0x81,'3', '0', '.',  0,   0,   0,   0       ; 0x50-0x57 (Down,KP3,KP0,KP.)
        times 128 - ($ - scancode_table) db 0

; Shifted ASCII table (indexed by ASCII value of unshifted char, offset by 0x20)
shift_table:
        db ' ', '!', '"', '#', '$', '%', '&', '"'      ; 0x20-0x27
        db '(', ')', '*', '+', '<', '_', '>', '?'      ; 0x28-0x2F
        db ')', '!', '@', '#', '$', '%', '^', '&'      ; 0x30-0x37
        db '*', '(', ':', ':', '<', '+', '>', '?'      ; 0x38-0x3F
        db '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G'    ; 0x40-0x47
        db 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O'    ; 0x48-0x4F
        db 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W'    ; 0x50-0x57
        db 'X', 'Y', 'Z', '{', '|', '}', '^', '_'     ; 0x58-0x5F
        db '~', 'A', 'B', 'C', 'D', 'E', 'F', 'G'    ; 0x60-0x67
        db 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O'    ; 0x68-0x6F
        db 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W'    ; 0x70-0x77
        db 'X', 'Y', 'Z', '{', '|', '}', '~',  0      ; 0x78-0x7F

;=======================================================================
; IDT DESCRIPTOR
;=======================================================================
idt_descriptor:
        dw 256 * 8 - 1         ; IDT size (256 entries * 8 bytes)
        dd idt_table            ; IDT base address

;=======================================================================
; BSS (Uninitialized Data) - zeroed at boot
;=======================================================================

bss_start:

vga_cursor_x:   resd 1
vga_cursor_y:   resd 1
vga_color:      resb 1

tick_count:     resd 1
total_free_pages: resd 1

kb_buffer:      resb KB_BUFFER_SIZE
kb_read_idx:    resd 1
kb_write_idx:   resd 1
kb_shift:       resb 1

ata_present:    resb 1
ata_total_sectors: resq 1      ; 64-bit LBA48 sector count
ata_identify_buf: resb 512

hbfs_super_buf:  resb 512       ; Superblock buffer
hbfs_dir_buf:    resb HBFS_ROOT_DIR_SIZE  ; Directory buffer (64KB)
hbfs_block_buf:  resb HBFS_BLOCK_SIZE     ; General block buffer (4KB)
hbfs_bitmap_buf: resb HBFS_BLOCK_SIZE     ; Bitmap buffer (4KB)

line_buffer:    resb LINE_BUFFER_SIZE
temp_line_buf:  resb LINE_BUFFER_SIZE
filename_buf:   resb 256
filename_buf2:  resb 256
wildcard_src_buf: resb 256
wildcard_dst_buf: resb 256

exc_eip:        resd 1          ; Saved EIP from exception
exc_errcode:    resd 1          ; Saved error code from exception

hist_buf:       resb HIST_MAX * HIST_ENTRY_SIZE  ; Command history buffer (8 * 256 = 2KB)
hist_count:     resd 1          ; Number of entries in history
hist_browse:    resd 1          ; Current browse position

idt_table:      resb 256 * 8   ; 256 IDT entries * 8 bytes each = 2KB

; Serial port
serial_present: resb 1

; Keyboard ctrl state
kb_ctrl:        resb 1

; RTC time fields
rtc_sec:        resb 1
rtc_min:        resb 1
rtc_hour:       resb 1
rtc_day:        resb 1
rtc_month:      resb 1
rtc_year:       resb 1
rtc_century:    resb 1

; Ctrl+C flag
ctrl_c_flag:    resb 1

; Program execution
program_entry:  resd 1
program_exit_code: resd 1

; TSS structure (104 bytes)
tss_struct:     resb 104

; File descriptor table (8 fds * 32 bytes)
fd_table:       resb FD_MAX * FD_ENTRY_SIZE

; Environment variables (16 * 128 = 2KB)
env_table:      resb ENV_MAX * ENV_ENTRY_SIZE
env_name_buf:   resb 64
env_temp_buf:   resb LINE_BUFFER_SIZE
expand_buf:     resb LINE_BUFFER_SIZE

; Subdirectory tracking
current_dir_lba:  resd 1
current_dir_sects: resd 1
current_dir_name: resb 256

; Directory stack for multi-level subdirectory navigation
dir_depth:        resd 1                             ; Current nesting depth (0=root)
dir_stack:        resb DIR_STACK_MAX * DIR_STACK_ENTRY_SIZE  ; Stack of parent dirs

; Last file type read (set by hbfs_read_file)
last_file_type: resb 1
wild_star_pat:  resd 1
wild_star_txt:  resd 1

; Batch execution buffers
batch_line_buf:   resb LINE_BUFFER_SIZE
batch_script_buf: resb BATCH_BUFFER_SIZE  ; 32KB batch script storage
batch_running:    resb 1                  ; Guard against nested batch calls

; Program execution state
program_running: resb 1          ; 1 = user program is executing
prog_name_buf:   resb 256        ; Parsed program name from command line
program_args_buf: resb 512       ; Command-line arguments for programs
head_lines:      resd 1          ; Line count for head/tail commands
cmd_flags:       resd 1          ; Bitmask of parsed flags (bit 0='a', bit 13='n', etc.)
cmd_flag_num:    resd 1          ; Numeric argument from -n N
path_search_buf: resb 256        ; Buffer for PATH-based program search
temp_path_buf:   resb 256        ; Buffer for building PATH/progname paths

; Alias table (16 entries: 32 byte name + 224 byte command = 256 per entry)
ALIAS_MAX        equ 16
ALIAS_NAME_LEN   equ 32
ALIAS_CMD_LEN    equ 224
ALIAS_ENTRY_SIZE equ ALIAS_NAME_LEN + ALIAS_CMD_LEN
alias_table:     resb ALIAS_MAX * ALIAS_ENTRY_SIZE  ; 4KB alias table
alias_count:     resd 1

; Diff command buffers
diff_buf1:       resd 1          ; Pointer (uses malloc)
diff_buf2:       resd 1          ; Pointer (uses malloc)

; Strings command
strings_min_len: resd 1

; Alias expansion buffer
alias_expand_buf: resb 256

; PATH search: saved directory state for restore after search
path_save_lba:    resd 1          ; Saved current_dir_lba
path_save_sects:  resd 1          ; Saved current_dir_sects
path_save_depth:  resd 1          ; Saved dir_depth
path_save_name:   resb 256        ; Saved current_dir_name
path_save_stack:  resb DIR_STACK_MAX * DIR_STACK_ENTRY_SIZE  ; Saved dir_stack

; File-level CWD save (used by hbfs_read_file path resolution)
file_save_lba:    resd 1
file_save_sects:  resd 1
file_save_depth:  resd 1
file_save_name:   resb 256
file_save_stack:  resb DIR_STACK_MAX * DIR_STACK_ENTRY_SIZE

; Path resolution buffers for hbfs_read_file
path_dir_buf:     resb 256        ; Directory part of path (e.g. "/bin")
path_base_buf:    resb 256        ; Basename part of path (e.g. "hello")

; GFF (global file find) CWD save slots
gff_cwd_lba:      resd 1
gff_cwd_sects:    resd 1
gff_cwd_depth:    resd 1
gff_cwd_name:     resb 256
gff_cwd_stack:    resb DIR_STACK_MAX * DIR_STACK_ENTRY_SIZE

bss_end:
