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

; The shipping image is a flat binary with a fixed load address. The same
; source also assembles as ELF32 (make symbols) so GDB has a symbol table:
; without ORG the symbols are section-relative, and the debugger adds the
; 1 MB load base with `add-symbol-file kernel.elf 0x00100000`.
%ifdef KERNEL_ELF
        ; no ORG: ELF symbols are offsets from the load base
%else
[ORG 0x00100000]
%endif

;=======================================================================
; CONSTANTS
;=======================================================================

; Memory layout — everything from KERNEL_BASE up to KERNEL_RESERVED_END is
; claimed by the kernel: image, program window, page tables, PMM bitmap/tags
; and the heap window.  pmm_init frees only memory at or above
; KERNEL_RESERVED_END; anything lower must never reach the buddy allocator or
; it will hand out the kernel's own metadata.
; Keep in sync with docs/TECHNICAL_REFERENCE.md and populate.py.
KERNEL_BASE         equ 0x00100000
KERNEL_STACK        equ 0x009FC00       ; Top of conventional memory for kernel stack
PROGRAM_BASE        equ 0x00300000      ; Programs load at 3MB
PROGRAM_END_ADDR    equ 0x00400000      ; End of static program area (for sbrk)
PROGRAM_MAX_SIZE    equ 0x00100000      ; Max 1MB per program = END - BASE
PMM_BITMAP          equ 0x00500000      ; Physical memory bitmap at 5MB (128KB)
HEAP_BASE           equ 0x00700000      ; Kernel heap window (currently unused)
KERNEL_RESERVED_END equ 0x00800000      ; 8MB — lower bound of PMM-managed memory
BG_PROG_PAGES       equ 256             ; Pages per background program slot (1MB)
BG_STACK_PAGES      equ 16             ; Pages per background task user stack (64KB)

; User stack. It lives at the top of the program window, below the exit
; trampoline. Only the top USER_STACK_INIT_PAGES pages are mapped when a task
; starts; the pages below them are left unmapped so that
;   * a stack overflow faults instead of silently overwriting the heap, and
;   * the fault handler can grow the stack on demand (up to the hard cap).
; USER_STACK_BASE itself is a permanent guard page: it is never mapped, so the
; task that runs into it is killed while the rest of the system keeps running.
USER_STACK_MAX_PAGES  equ 64                                  ; 256 KB hard cap
USER_STACK_INIT_PAGES equ 16                                  ; 64 KB mapped at exec
USER_STACK_BASE       equ PROGRAM_END_ADDR - USER_STACK_MAX_PAGES * 4096
; Stack ASLR: the stack top starts up to this many pages below the window top,
; and the trampoline and initial ESP move with it. It has to stay well under
; USER_STACK_MAX_PAGES - USER_STACK_INIT_PAGES so the stack can still grow.
STACK_ASLR_PAGES      equ 16
STACK_GROWTH_SLACK    equ 16                                  ; pages one fault may skip

; mmap protection bits (mirror of programs/syscalls.inc)
PROT_NONE           equ 0
PROT_READ           equ 1
PROT_WRITE          equ 2
PROT_EXEC           equ 4

; Per-task mmap area records (kernel/vm.inc, storage in kernel/data.inc).
; Defined here because sched.inc and paging.inc are included before vm.inc.
;
;   start    first virtual address (page aligned)
;   pages    page count
;   prot     PROT_* mask
;   sector   first file sector (LBA + 1) backing the area; 0 = anonymous
;   end      exclusive end address
;   blocks   file blocks present from `sector`; later pages read as zero
;            (a file mapping whose last page is short, or which runs past EOF)
;   flags    VM_MAP_* bits that are not a protection (currently VM_MAP_SHARED)
VM_AREA_SIZE        equ 28
VM_AREAS_PER_TASK   equ 32
VMA_START           equ 0
VMA_PAGES           equ 4
VMA_PROT            equ 8
VMA_SECTOR          equ 12
VMA_END             equ 16
VMA_BLOCKS          equ 20
VMA_FLAGS           equ 24
; COW_MAX_PAGES (the copy-on-write refcount table size) is derived from
; USER_DEMAND_BASE_MAX in kernel/paging.inc; cow_ref in data.inc is that big.

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
KEY_PGUP            equ 0x84        ; v9.0: Page Up
KEY_PGDN            equ 0x85        ; v9.0: Page Down

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
; HBFS on-disk layout — all LBAs derived from HBFS_SUPERBLOCK_LBA.
; HBFS_SUPERBLOCK_LBA must be > (33 + KERNEL_SECTORS).
; KERNEL_SECTORS is generated into kernel_sectors.inc by the Makefile from the
; assembled binary, and that rule hard-fails the build above 4062 sectors (the
; headroom below this LBA). Do NOT hard-code a sector count here — a number in
; a comment silently drifts out of date. If the kernel ever really does exceed
; 4062 sectors, raise this value AND the matching constant in populate.py.
HBFS_SUPERBLOCK_LBA  equ 4096          ; First LBA of HBFS (well past kernel end)
HBFS_BITMAP_START    equ HBFS_SUPERBLOCK_LBA + 1
HBFS_BITMAP_BLOCKS   equ 16            ; 16 blocks for bitmap (covers 524288 blocks)
HBFS_BITMAP_SECTS    equ HBFS_BITMAP_BLOCKS * HBFS_SECTORS_PER_BLK ; 128 sectors
HBFS_BITMAP_SIZE     equ HBFS_BITMAP_BLOCKS * HBFS_BLOCK_SIZE       ; 65536 bytes
HBFS_TOTAL_BLOCKS    equ 523727        ; Data blocks in a 2048 MiB image
                                       ; (523728 blocks would run 512 bytes past
                                       ; the end); must match populate.py
HBFS_ROOT_DIR_START  equ HBFS_BITMAP_START + HBFS_BITMAP_SECTS      ; = HBFS_SUPERBLOCK_LBA + 129
HBFS_DATA_START      equ HBFS_ROOT_DIR_START + HBFS_ROOT_DIR_SECTS  ; = HBFS_SUPERBLOCK_LBA + 385

; v9.0 HBFS v2 — Write-Ahead Journal constants
; The journal occupies the first HBFS_JOURNAL_BLOCKS data-area blocks.
; File allocation is forced to start at block HBFS_JOURNAL_BLOCKS, so
; journal sectors are never overwritten by file data.
HBFS_VERSION_V2      equ 2
HBFS_JOURNAL_BLOCKS  equ 4            ; 4 blocks × 4KB = 16KB journal area
HBFS_JOURNAL_SECTS   equ HBFS_JOURNAL_BLOCKS * HBFS_SECTORS_PER_BLK ; 32 sectors
HBFS_JOURNAL_LBA     equ HBFS_DATA_START  ; Journal starts at first data LBA
HBFS_JRNL_MAGIC      equ 0x4A524E4C   ; 'JRNL'
HBFS_JRNL_T_PRE      equ 1            ; Journal entry type: pre-image
HBFS_JRNL_T_COMMIT   equ 2            ; Journal entry type: commit
; Offsets within the 512-byte superblock for journal fields
HBFS_SB_JRNL_STATE   equ 40          ; dword: 0=clean 1=transaction-in-progress
HBFS_SB_JRNL_PTR     equ 44          ; dword: next journal sector index (0 = first)
; Every transaction increments this counter; each journal record is stamped with
; it. Recovery only replays records carrying the current epoch, so stale records
; from an older transaction (still sitting in the circular log) can never be
; applied again.
HBFS_SB_JRNL_EPOCH   equ 56          ; dword: journal transaction epoch

; v10.0 HBFS v3 — Permissions, UID/GID, xattr, extended filename
; Directory entry layout is unchanged in byte count (288 bytes) but the
; previously-reserved bytes [276-287] now carry permission metadata:
;
;   [276-277]  mode     word  : Unix-style permission bits (12 low bits used)
;                               bit 11 = setuid, bit 10 = setgid, bit 9 = sticky
;                               bits 8-6 = owner rwx, bits 5-3 = group rwx,
;                               bits 2-0 = world rwx
;   [278-279]  uid      word  : owner user  ID  (0 = root)
;   [280-281]  gid      word  : owner group ID  (0 = root)
;   [282-283]  xattr_count  word  : number of xattr entries (0 = none)
;   [284-287]  xattr_lba    dword : LBA of first xattr block (0 = none)
;
; xattr block layout (one 4 KB block per file, up to 16 xattr entries):
;   Each entry: key(64 bytes) + value(64 bytes) = 128 bytes; 32 entries / block
;
HBFS_VERSION_V3      equ 3

; Directory entry v3 field offsets (relative to entry start)
HBFS_DE_NAME         equ 0            ; filename  (253 bytes, null-terminated)
HBFS_DE_TYPE         equ 253          ; file type (byte)
HBFS_DE_FLAGS        equ 254          ; flags     (word)
HBFS_DE_SIZE         equ 256          ; file size (dword)
HBFS_DE_BLOCK        equ 260          ; start block (dword)
HBFS_DE_BLKCNT       equ 264          ; block count (dword)
HBFS_DE_CTIME        equ 268          ; created timestamp (dword)
HBFS_DE_MTIME        equ 272          ; modified timestamp (dword)
; Previously reserved [276-287] — now v3 permission fields:
HBFS_DE_MODE         equ 276          ; Unix permission bits (word)
HBFS_DE_UID          equ 278          ; owner UID (word)
HBFS_DE_GID          equ 280          ; owner GID (word)
HBFS_DE_XATTR_COUNT  equ 282          ; xattr count (word)
HBFS_DE_XATTR_LBA    equ 284          ; xattr block LBA (dword)

; Default permission modes
HBFS_MODE_FILE_DEF   equ 0o644        ; rw-r--r-- (default file)
HBFS_MODE_DIR_DEF    equ 0o755        ; rwxr-xr-x (default directory)
HBFS_MODE_EXEC_DEF   equ 0o755        ; rwxr-xr-x (executable)

; xattr block constants
HBFS_XATTR_KEY_LEN   equ 64
HBFS_XATTR_VAL_LEN   equ 64
HBFS_XATTR_ENTRY_SIZE equ (HBFS_XATTR_KEY_LEN + HBFS_XATTR_VAL_LEN)  ; 128 bytes
HBFS_XATTR_PER_BLOCK equ (HBFS_BLOCK_SIZE / HBFS_XATTR_ENTRY_SIZE)    ; 32 entries

; v10.0: UEFI boot detection (set by uefi_loader.c at 0x528)
BOOTINFO_UEFI_MAGIC  equ 0x528
UEFI_BOOT_MAGIC      equ 0xEF100000

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
SYS_SBRK            equ 23      ; Adjust program break: EBX=increment -> EAX=old_brk/-1

LINE_BUFFER_SIZE    equ 1024
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

; New syscall numbers (extending 0-35)
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

; v8.0 graphics/IO syscalls (slots 80-101)
SYS_GETENV_SLOT     equ 80
SYS_DMESG_WRITE     equ 81
SYS_DMESG_READ      equ 82
SYS_RENAME          equ 83
SYS_RMDIR           equ 84
SYS_TRUNCATE        equ 85
SYS_CONNECT_NB      equ 86
SYS_POLL            equ 87
SYS_SEM_CREATE      equ 88
SYS_SEM_WAIT        equ 89
SYS_SEM_POST        equ 90
SYS_SEM_CLOSE       equ 91
SYS_WAITPID         equ 92
SYS_GETMTIME        equ 93
SYS_SETMTIME        equ 94
SYS_DRAW_LINE       equ 95      ; Draw Bresenham line: EBX=x0 ECX=y0 EDX=x1 ESI=y1 EDI=color
SYS_DRAW_TRIANGLE   equ 96      ; Fill triangle: EBX=x0|(y0<<16) ECX=x1|(y1<<16) EDX=x2|(y2<<16) ESI=color
SYS_BLIT            equ 97      ; Sprite blit: EBX=src ECX=dx EDX=dy ESI=w|(h<<16) EDI=colorkey
SYS_DIRTY_PRESENT   equ 98      ; Present dirty rect to LFB
SYS_PSF_LOAD        equ 99      ; Load PSF2 font: EBX=filename -> EAX=0/-1
SYS_PSF_CHAR        equ 100     ; Draw PSF2 char: EBX=x ECX=y EDX=codepoint ESI=fg_color
SYS_PCI_FIND        equ 101     ; Find PCI device: EBX=vendor ECX=device_id -> EAX=bdf/-1
SYS_GETPPID         equ 102     ; Get parent PID -> EAX=ppid

; ---------------------------------------------------------------------
; v9.0 syscalls (103-115)
; Declared here as well as in programs/syscalls.inc so that
; tools/validate_syscalls.py can check the two headers against each other AND
; against the positional dispatch table in kernel/syscall.inc. These numbers
; used to exist only in the user header, which meant the whole v9.0/v10.0/v11.0
; range (103-180) was never cross-checked.
; ---------------------------------------------------------------------
SYS_FORK            equ 103     ; -> EAX=child_pid (parent) / 0 (child) / -1
SYS_DEFRAG          equ 104     ; Defragment HBFS: -> EAX=0/-1
SYS_TLS_CONNECT     equ 105     ; TLS 1.2 stub: EBX=fd -> EAX=0/-1
SYS_AUDIO_REC_START equ 106     ; Start AC97 recording: EBX=buf ECX=max -> EAX=0/-1
SYS_AUDIO_REC_READ  equ 107     ; Read recorded PCM: EBX=buf ECX=max -> EAX=bytes/-1
SYS_AUDIO_REC_STOP  equ 108     ; Stop recording -> EAX=0
SYS_AUDIO_OPEN      equ 109     ; Open mixer channel: EBX=fmt ECX=rate -> EAX=ch/-1
SYS_AUDIO_WRITE     equ 110     ; Write to mixer: EBX=ch ECX=buf EDX=len -> EAX=bytes
SYS_AUDIO_CLOSE_CHAN equ 111    ; Close mixer channel: EBX=ch -> EAX=0
SYS_MSGQ_CREATE     equ 112     ; Create message queue: EBX=key ECX=max_msgs -> EAX=qid/-1
SYS_MSGQ_SEND       equ 113     ; Send: EBX=qid ECX=buf EDX=len -> EAX=0/-1
SYS_MSGQ_RECV       equ 114     ; Recv: EBX=qid ECX=buf EDX=max -> EAX=bytes/-1
SYS_MSGQ_CLOSE      equ 115     ; Destroy queue: EBX=qid -> EAX=0/-1

; ---------------------------------------------------------------------
; v10.0 POSIX compatibility extensions + compositor surfaces (116-142)
; ---------------------------------------------------------------------
SYS_DUP             equ 116     ; dup(fd) -> EAX=new_fd/-1
SYS_DUP2            equ 117     ; dup2(src,dst) -> EAX=dst/-1
SYS_FCNTL           equ 118     ; fcntl(fd,cmd,arg) -> EAX=result/-1
SYS_IOCTL           equ 119     ; ioctl(fd,req,arg) -> EAX=0/-1
SYS_MMAP            equ 120     ; mmap(addr,len,prot) -> EAX=addr/-1
SYS_MUNMAP          equ 121     ; munmap(addr,len) -> EAX=0/-1
SYS_MPROTECT        equ 122     ; mprotect(addr,len,prot) -> EAX=0/-1
SYS_SELECT          equ 123     ; select(nfds,rfds,wfds,efds,tv) -> EAX=count
SYS_CLOCK_GETTIME   equ 124     ; clock_gettime(clk,timespec*) -> EAX=0/-1
SYS_NANOSLEEP       equ 125     ; nanosleep(rqtp,rmtp) -> EAX=0/-1
SYS_GETTIMEOFDAY    equ 126     ; gettimeofday(timeval*,tz*) -> EAX=0
SYS_GETUID          equ 127     ; -> EAX=uid
SYS_SETUID          equ 128     ; setuid(uid) -> EAX=0/-1
SYS_GETGID          equ 129     ; -> EAX=gid
SYS_SETGID          equ 130     ; setgid(gid) -> EAX=0/-1
SYS_GETEUID         equ 131     ; -> EAX=euid
SYS_GETEGID         equ 132     ; -> EAX=egid
SYS_ACCESS          equ 133     ; access(path,mode) -> EAX=0/-1
SYS_PIPE2           equ 134     ; pipe2(pipefd[2],flags) -> EAX=0/-1
SYS_SURFACE_CREATE  equ 135     ; Create surface: EBX=w ECX=h EDX=x ESI=y -> EAX=id/-1
SYS_SURFACE_COMMIT  equ 136     ; Commit dirty rect: EBX=id ECX=dx EDX=dy ESI=dw EDI=dh
SYS_SURFACE_DESTROY equ 137     ; Destroy surface: EBX=id -> EAX=0/-1
SYS_SURFACE_MOVE    equ 138     ; Move surface: EBX=id ECX=x EDX=y -> EAX=0/-1
SYS_SURFACE_RESIZE  equ 139     ; Resize surface: EBX=id ECX=w EDX=h -> EAX=0/-1
SYS_ALARM           equ 140     ; alarm(seconds; 0=cancel) -> EAX=prev_secs
SYS_GETXATTR        equ 141     ; getxattr(file,key,val,max) -> EAX=len/-1
SYS_SETXATTR        equ 142     ; setxattr(file,key,val) -> EAX=0/-1

; ---------------------------------------------------------------------
; v11.0 user-space signal delivery (143-144)
; ---------------------------------------------------------------------
SYS_SIGACTION       equ 143     ; sigaction(signum,handler,old) -> EAX=0/-1
SYS_SIGRETURN       equ 144     ; sigreturn() — return from a signal handler

; ---------------------------------------------------------------------
; v12.0 POSIX compliance (145-181)
; ---------------------------------------------------------------------
SYS_FSTAT           equ 145     ; fstat(fd,stat_buf) -> EAX=0/-1
SYS_FTRUNCATE       equ 146     ; ftruncate(fd,size) -> EAX=0/-1
SYS_FCHMOD          equ 147     ; fchmod(fd,mode) -> EAX=0/-1
SYS_FCHOWN          equ 148     ; fchown(fd,uid,gid) -> EAX=0/-1
SYS_FSYNC           equ 149     ; fsync(fd) -> EAX=0/-1
SYS_LINK            equ 150     ; link(old,new) -> EAX=0/-1
SYS_ISATTY          equ 151     ; isatty(fd) -> EAX=1/0
SYS_SETSID          equ 152     ; setsid() -> EAX=sid/-1
SYS_GETSID          equ 153     ; getsid(pid) -> EAX=sid/-1
SYS_WAIT            equ 154     ; wait(status*) -> EAX=pid/-1
SYS_PAUSE           equ 155     ; pause() -> EAX=-1
SYS_UMASK           equ 156     ; umask(mask) -> EAX=old_mask
SYS_SIGPENDING      equ 157     ; sigpending(set*) -> EAX=0
SYS_SIGSUSPEND      equ 158     ; sigsuspend(mask*) -> EAX=-1
SYS_SETITIMER       equ 159     ; setitimer(which,new*,old*) -> EAX=0/-1
SYS_GETITIMER       equ 160     ; getitimer(which,cur*) -> EAX=0/-1
SYS_SETENV          equ 161     ; setenv(name,val,over) -> EAX=0/-1
SYS_UNSETENV        equ 162     ; unsetenv(name) -> EAX=0
SYS_UNAME           equ 163     ; uname(utsname*) -> EAX=0/-1
SYS_UTIME           equ 164     ; utime(path,utimbuf*) -> EAX=0/-1
SYS_SYSCONF         equ 165     ; sysconf(name) -> EAX=value/-1
SYS_SETEUID         equ 166     ; seteuid(euid) -> EAX=0/-1
SYS_SETEGID         equ 167     ; setegid(egid) -> EAX=0/-1
SYS_SETREUID        equ 168     ; setreuid(ruid,euid) -> EAX=0/-1
SYS_SETREGID        equ 169     ; setregid(rgid,egid) -> EAX=0/-1
SYS_SENDTO          equ 170     ; sendto(fd,buf,len,fl,addr) -> EAX=bytes/-1
SYS_RECVFROM        equ 171     ; recvfrom(fd,buf,len,fl,from) -> EAX=bytes/-1
SYS_SETSOCKOPT      equ 172     ; setsockopt(fd,lvl,opt,val,len) -> EAX=0/-1
SYS_GETSOCKOPT      equ 173     ; getsockopt(fd,lvl,opt,val*,len*) -> EAX=0/-1
SYS_GETSOCKNAME     equ 174     ; getsockname(fd,addr,len*) -> EAX=0/-1
SYS_GETPEERNAME     equ 175     ; getpeername(fd,addr,len*) -> EAX=0/-1
SYS_SHUTDOWN        equ 176     ; shutdown(fd,how) -> EAX=0/-1
SYS_TCGETATTR       equ 177     ; tcgetattr(fd,termios*) -> EAX=0/-1
SYS_TCSETATTR       equ 178     ; tcsetattr(fd,action,termios*) -> EAX=0/-1
SYS_TCDRAIN         equ 179     ; tcdrain(fd) -> EAX=0/-1
SYS_TCFLUSH         equ 180     ; tcflush(fd,queue) -> EAX=0/-1
; SYS_GETERRNO — retrieve per-task errno
SYS_GETERRNO        equ 181     ; Get errno: -> EAX=errno
SYS_EXECVE          equ 182     ; execve(path, argv, envp) -> no return / -1
SYS_TCSETPGRP       equ 183     ; Set terminal foreground pgid: EBX=pgid -> 0/-1
SYS_TCGETPGRP       equ 184     ; Get terminal foreground pgid: -> EAX=pgid
SYS_MMAP_FILE       equ 185     ; Map a file: EBX=path ECX=len EDX=prot ESI=off -> addr/-1

; Signal trampoline — read-only page mapped at top of demand zone
SIGRETURN_TRAMPOLINE_VADDR equ 0x1FFFF000   ; Last page of demand zone (128–512MB)

; errno constants (POSIX E-values, Linux/ia32 numbering so programs can compare
; against the real values). Recorded per task via errno_set() and read back with
; SYS_GETERRNO.
;
; NOTE: storage is per-TCB, so it only covers code running as a scheduler task
; (background jobs). A foreground program entered via cmd_exec_program has no
; TCB yet, so its errno reads back as 0 until the Phase 2 process model gives
; every user program a real task. Do not export a global errno as a shortcut:
; background tasks would race on it.
EPERM               equ 1       ; Operation not permitted
ENOENT              equ 2       ; No such file or directory
ESRCH               equ 3       ; No such process
EINTR               equ 4       ; Interrupted system call
EIO                 equ 5       ; I/O error
ENXIO               equ 6       ; No such device or address
ENOEXEC             equ 8       ; Exec format error
EBADF               equ 9       ; Bad file descriptor
ECHILD              equ 10      ; No child processes
EAGAIN              equ 11      ; Try again / would block
EWOULDBLOCK         equ EAGAIN  ; POSIX alias for EAGAIN
ENOMEM              equ 12      ; Out of memory
EACCES              equ 13      ; Permission denied
EFAULT              equ 14      ; Bad address
EBUSY               equ 16      ; Device or resource busy
EEXIST              equ 17      ; File exists
EXDEV               equ 18      ; Cross-device link
ENODEV              equ 19      ; No such device
ENOTDIR             equ 20      ; Not a directory
EISDIR              equ 21      ; Is a directory
EINVAL              equ 22      ; Invalid argument
ENFILE              equ 23      ; File table overflow
EMFILE              equ 24      ; Too many open files
ENOTTY              equ 25      ; Inappropriate ioctl for device
EFBIG               equ 27      ; File too large
ENOSPC              equ 28      ; No space left on device
ESPIPE              equ 29      ; Illegal seek
EROFS               equ 30      ; Read-only file system
EMLINK              equ 31      ; Too many links
EPIPE               equ 32      ; Broken pipe
ERANGE              equ 34      ; Numerical result out of range
ENAMETOOLONG        equ 36      ; File name too long
ENOSYS              equ 38      ; Function not implemented
ENOTEMPTY           equ 39      ; Directory not empty
ELOOP               equ 40      ; Too many levels of symbolic links

; File descriptor constants
FD_MAX              equ 8
FD_ENTRY_SIZE       equ 32
FD_FLAG_CLOSED      equ 0
FD_FLAG_READ        equ 1
FD_FLAG_WRITE       equ 2

; A task's private fd page (4 KB) holds BOTH fd views:
;   offset 0    legacy HBFS fd_table  (FD_MAX * FD_ENTRY_SIZE = 256 bytes)
;   offset 256  VFS virtual-fd table  (VFS_MAX_FDS * VFD_SIZE = 2048 bytes)
; Both are copied in/out of the live globals on every context switch, so one
; task closing a descriptor cannot affect another task's view of it.
FD_PAGE_VFD_OFS     equ (FD_MAX * FD_ENTRY_SIZE)
FD_PAGE_VFD_BYTES   equ 2048

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

; Ctrl+C / Ctrl+Z / Tab
CTRL_C_CODE         equ 3
CTRL_Z_CODE         equ 0x1A
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

        ; Initialize x87 FPU: clear CR0.EM (emulation), set CR0.MP (coprocessor
        ; present).  Without this, any user program that executes a float
        ; instruction will raise a #NM (Device Not Available) fault.
        mov eax, cr0
        and eax, ~(1 << 2)     ; Clear EM bit (bit 2)
        or  eax,  (1 << 1)     ; Set  MP bit (bit 1)
        mov cr0, eax
        fninit                  ; Initialize FPU to known state

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
        call fi_init
        call kmem_init
        call ata_init
        call serial_init
        call tss_init
        call sched_init
        call ipc_init
        call net_init
        call e1000_init
        call paging_init
        call vm_init
        call mouse_init
        call sb16_init
        call vbe_init
        call pci_init
        call ac97_init
        call atadma_init
        call ahci_init
        call virtio_init
        call burrows_init
        call gdbstub_init

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

        ; Initialize filesystem (and the block cache in front of it)
        call blk_cache_init
        call pc_init
        call hbfs_init
        call vfs_init

        ; Enter the command shell
        jmp shell_main

;-----------------------------------------------------------------------
; boot_splash - Animated boot splash with ASCII art
;-----------------------------------------------------------------------

; Subsystem includes – each file corresponds to a logical kernel module.
; The build still produces one flat binary; this split is for readability.
;-----------------------------------------------------------------------
%include "kernel/sched.inc"
%include "kernel/vga.inc"
%include "kernel/pic.inc"
%include "kernel/idt.inc"
%include "kernel/pit.inc"
%include "kernel/pmm.inc"
%include "kernel/faultinject.inc"
%include "kernel/kmem.inc"
%include "kernel/ata.inc"
%include "kernel/blkcache.inc"
%include "kernel/pagecache.inc"
%include "kernel/hbfs.inc"
%include "kernel/vfs.inc"
%include "kernel/filesearch.inc"
%include "kernel/isr.inc"
%include "kernel/ipc.inc"
%include "kernel/net.inc"
%include "kernel/paging.inc"
%include "kernel/vm.inc"
%include "kernel/mouse.inc"
%include "kernel/sb16.inc"
%include "kernel/vbe.inc"
%include "kernel/pci.inc"
%include "kernel/ac97.inc"
%include "kernel/atadma.inc"
%include "kernel/ahci.inc"
%include "kernel/e1000.inc"
%include "kernel/virtio.inc"
%include "kernel/burrows.inc"
%include "kernel/gdbstub.inc"
%include "kernel/screensaver.inc"
%include "kernel/shell.inc"
%include "kernel/util.inc"
%include "kernel/syscall.inc"
%include "kernel/cmdtable.inc"
%include "kernel/data.inc"
