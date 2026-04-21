;
; Mellivora OS - 32-bit Protected Mode Kernel
;
; Loaded at 0x00100000 (1MB) by stage 2.
; Flat memory model, full 4GB address space.
;
; Components:
;   Mellivora  - Kernel (this file + kernel/*.inc)
;   Ratel      - Init system (hardware & subsystem initialization)
;   HB Lair    - Interactive command shell (Honey Badger Lair)
;   HBFS       - Honey Badger File System
;   HBU        - Honey Badger Utilities (GNU-like user-space tools)
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
BG_PROG_PAGES       equ 256             ; Pages per background program slot (1MB)
BG_STACK_PAGES      equ 16             ; Pages per background task user stack (64KB)

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
HBFS_ROOT_DIR_BLOCKS equ 32            ; Root directory uses 32 blocks (supports 455 entries)
HBFS_ROOT_DIR_SECTS  equ HBFS_ROOT_DIR_BLOCKS * HBFS_SECTORS_PER_BLK ; 256 sectors
HBFS_ROOT_DIR_SIZE   equ HBFS_ROOT_DIR_BLOCKS * HBFS_BLOCK_SIZE      ; 131072 bytes
HBFS_MAX_FILES       equ HBFS_ROOT_DIR_SIZE / HBFS_DIR_ENTRY_SIZE    ; 455 entries
HBFS_SUBDIR_BLOCKS   equ 16           ; Subdirectories get 16 blocks (224 entries each)
HBFS_SUPERBLOCK_LBA  equ 417           ; After kernel area (LBA 33 + 384 sectors)
HBFS_BITMAP_START    equ 418           ; Block allocation bitmap start
HBFS_BITMAP_BLOCKS   equ 16            ; 16 blocks for bitmap (covers 524288 blocks)
HBFS_BITMAP_SECTS    equ HBFS_BITMAP_BLOCKS * HBFS_SECTORS_PER_BLK ; 128 sectors
HBFS_BITMAP_SIZE     equ HBFS_BITMAP_BLOCKS * HBFS_BLOCK_SIZE       ; 65536 bytes
HBFS_TOTAL_BLOCKS    equ 524288        ; Total filesystem blocks (2 GB)
HBFS_ROOT_DIR_START  equ 546           ; Root directory (256 sectors = 32 blocks)
HBFS_DATA_START      equ 802           ; Data blocks start here

; File types (stored at byte 253 of directory entry)
FTYPE_FREE          equ 0
FTYPE_TEXT          equ 1              ; Text file
FTYPE_FILE          equ 1              ; Alias for backward compat
FTYPE_DIR           equ 2              ; Directory
FTYPE_EXEC          equ 3              ; Executable (flat binary or ELF)
FTYPE_BATCH         equ 4              ; Batch script
FTYPE_LINK          equ 5              ; Symbolic link

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
DIRENT_PERMS        equ 276            ; v3.0: Permission bits (2 bytes, rwxrwxrwx = 9 bits)
DIRENT_OWNER        equ 278            ; v3.0: Owner UID (2 bytes)
; Bytes 280-287 still reserved

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
SYS_STDIN_READ      equ 34      ; Read piped stdin: EBX=buf -> EAX=bytes (-1 if none)
SYS_YIELD           equ 35      ; Cooperative yield: switch to next ready task
SYS_MOUSE           equ 36      ; Read mouse state: -> EAX=x, EBX=y, ECX=buttons
SYS_FRAMEBUF        equ 37      ; Framebuffer: EBX=sub (0=info,1=set,2=restore)
SYS_GUI             equ 38      ; Burrows GUI: EBX=sub-function
SYS_SOCKET          equ 39      ; Create socket: EBX=type(1=TCP,2=UDP) -> EAX=fd
SYS_CONNECT         equ 40      ; Connect: EBX=fd ECX=ip EDX=port -> EAX=0/-1
SYS_SEND            equ 41      ; Send: EBX=fd ECX=buf EDX=len -> EAX=bytes
SYS_RECV            equ 42      ; Recv: EBX=fd ECX=buf EDX=maxlen -> EAX=bytes
SYS_BIND            equ 43      ; Bind: EBX=fd ECX=port -> EAX=0/-1
SYS_LISTEN          equ 44      ; Listen: EBX=fd -> EAX=0/-1
SYS_ACCEPT          equ 45      ; Accept: EBX=fd -> EAX=new_fd
SYS_DNS             equ 46      ; DNS resolve: EBX=hostname -> EAX=ip
SYS_SOCKCLOSE       equ 47      ; Close socket: EBX=fd -> EAX=0
SYS_PING            equ 48      ; Ping: EBX=ip -> EAX=rtt_ticks/-1
SYS_SETDATE         equ 49      ; Set RTC: EBX=buf[s,m,h,d,mo,yr] ECX=century
SYS_AUDIO_PLAY      equ 50      ; Play PCM: EBX=buf ECX=len EDX=fmt -> EAX=0/-1
SYS_AUDIO_STOP      equ 51      ; Stop playback: -> EAX=0
SYS_AUDIO_STATUS    equ 52      ; Query audio: -> EAX=state EBX=present
SYS_KILL            equ 53      ; Kill task: EBX=pid -> EAX=0/-1
SYS_GETPID          equ 54      ; Get PID: -> EAX=pid
SYS_CLIPBOARD_COPY  equ 55      ; Copy to clipboard: EBX=buf ECX=len -> EAX=0
SYS_CLIPBOARD_PASTE equ 56      ; Paste clipboard: EBX=buf ECX=maxlen -> EAX=len
SYS_NOTIFY          equ 57      ; Post notification: EBX=text EDX=color -> EAX=0
SYS_FILE_OPEN_DLG   equ 58      ; Open file dialog: EBX=title EDX=filter -> EAX=1/0 ECX=name_buf
SYS_FILE_SAVE_DLG   equ 59      ; Save file dialog: EBX=title EDX=filter -> EAX=1/0 ECX=name_buf
SYS_PIPE_CREATE     equ 60      ; Create pipe: -> EAX=pipe_id (-1=fail)
SYS_PIPE_WRITE      equ 61      ; Write pipe: EBX=pipe_id ECX=buf EDX=len -> EAX=bytes_written
SYS_PIPE_READ       equ 62      ; Read pipe: EBX=pipe_id ECX=buf EDX=maxlen -> EAX=bytes_read
SYS_PIPE_CLOSE      equ 63      ; Close pipe: EBX=pipe_id -> EAX=0
SYS_SHMGET          equ 64      ; Get shared mem: EBX=key ECX=size -> EAX=shm_id
SYS_SHMADDR         equ 65      ; Get shm address: EBX=shm_id -> EAX=pointer
SYS_PROCLIST        equ 66      ; Get task info: EBX=slot(0-15) ECX=buf(16 bytes) -> EAX=0/-1
SYS_MEMINFO         equ 67      ; Get mem info: -> EAX=free_pages, EBX=total_free_pages_at_boot
; v3.0 syscalls
SYS_CHMOD           equ 68      ; Change permissions: EBX=filename ECX=perms -> EAX=0/-1
SYS_CHOWN           equ 69      ; Change owner: EBX=filename ECX=uid -> EAX=0/-1
SYS_SYMLINK         equ 70      ; Create symlink: EBX=linkname ECX=target -> EAX=0/-1
SYS_READLINK        equ 71      ; Read link: EBX=linkname ECX=buf -> EAX=len/-1

; v4.0 syscalls
SYS_SETPRIORITY     equ 72      ; Set priority: EBX=pid(0=self) ECX=prio -> EAX=0/-1
SYS_GETPRIORITY     equ 73      ; Get priority: EBX=pid(0=self) -> EAX=prio/-1
SYS_SIGNAL          equ 74      ; Send signal: EBX=pid ECX=signum -> EAX=0/-1
SYS_SETPGID         equ 75      ; Set PGID: EBX=pid(0=self) ECX=pgid(0=own) -> EAX=0/-1
SYS_GETPGID         equ 76      ; Get PGID: EBX=pid(0=self) -> EAX=pgid/-1
SYS_SIGMASK         equ 77      ; Signal mask: EBX=op ECX=mask -> EAX=old_mask/-1
SYS_TASKNAME        equ 78      ; Set task name: EBX=name_ptr -> EAX=0
SYS_REALLOC         equ 79      ; Realloc: EBX=ptr ECX=new_size -> EAX=new_ptr/0

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
ENV_MAX             equ 32
ENV_ENTRY_SIZE      equ 128

; dmesg ring buffer
DMESG_ENTRIES       equ 64
DMESG_LINE_SIZE     equ 128
DMSG_TOTAL_SIZE     equ (DMESG_ENTRIES * DMESG_LINE_SIZE)

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

        ; Ratel init system — initialize hardware & kernel subsystems
        call vga_init
        call pic_init
        call idt_init
        call pit_init
        call kb_init
        call pmm_init
        call ata_init
        call serial_init
        call tss_init
        call sched_init
        call ipc_init
        call net_init
        call paging_init
        call mouse_init
        call sb16_init
        call vbe_init
        call burrows_init

        ; Drain any stale bytes from the 8042 output buffer so that
        ; enabling interrupts does not deliver a spurious keyboard event.
.drain_8042:
        in al, 0x64
        test al, 1              ; Output buffer full?
        jz .drain_done
        in al, 0x60             ; Read and discard
        jmp .drain_8042
.drain_done:

        ; Enable interrupts
        sti

        ; Print system info
        call print_sysinfo

        ; Initialize filesystem
        call hbfs_init

        ; Enter the command shell
        jmp shell_main

;-----------------------------------------------------------------------
; boot_splash - Animated boot splash with ASCII art
;-----------------------------------------------------------------------
; Subsystem includes – each file corresponds to a logical kernel module.
; The build still produces one flat binary; this split is for readability.
;-----------------------------------------------------------------------

%include "kernel/vga.inc"
%include "kernel/pic.inc"
%include "kernel/idt.inc"
%include "kernel/isr.inc"
%include "kernel/pit.inc"
%include "kernel/pmm.inc"
%include "kernel/ata.inc"
%include "kernel/hbfs.inc"
%include "kernel/filesearch.inc"
%include "kernel/syscall.inc"
%include "kernel/sched.inc"
%include "kernel/ipc.inc"
%include "kernel/net.inc"
%include "kernel/paging.inc"
%include "kernel/mouse.inc"
%include "kernel/sb16.inc"
%include "kernel/vbe.inc"
%include "kernel/burrows.inc"
%include "kernel/screensaver.inc"
%include "kernel/shell.inc"
%include "kernel/util.inc"
%include "kernel/data.inc"
