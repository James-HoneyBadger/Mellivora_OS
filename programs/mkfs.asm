; mkfs.asm - Format a disk region as HBFS
; Usage: mkfs <device>
; WARNING: This will destroy all data on the specified device!
; Requires confirmation: type YES
;
; NOTE: Raw disk write (SYS_DISK_WRITE) is a kernel-only operation and is
; not accessible from user-space programs. This tool is provided as a
; reference/template only. Disk formatting must be done at boot time or
; via the kernel's built-in HBFS initialization routines.

%include "syscalls.inc"

; HBFS superblock layout (first sector = LBA 0 of partition)
; Offset 0:   magic  "HBFS" (4 bytes)
; Offset 4:   version dword = 1
; Offset 8:   total_sectors dword
; Offset 12:  root_dir_lba dword = 1
; Offset 16:  block_size dword = 512
; Offset 20:  label: 16 bytes

SECTOR_SIZE     equ 512
HBFS_MAGIC      equ 0x53464248    ; 'HBFS' little-endian
HBFS_VERSION    equ 1
DEFAULT_SECTORS equ 2048          ; 1 MB default

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz .usage

        mov esi, arg_buf
        call skip_spaces
        cmp byte [esi], 0
        je .usage

        ; Copy device/label argument
        mov edi, dev_label
        xor ecx, ecx
.copy_dev:
        mov al, [esi]
        cmp al, ' '
        je .dev_done
        cmp al, 0
        je .dev_done
        mov [edi + ecx], al
        inc ecx
        inc esi
        jmp .copy_dev
.dev_done:
        mov byte [edi + ecx], 0

        ; Confirmation prompt
        mov eax, SYS_PRINT
        mov ebx, msg_warn1
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, dev_label
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_warn2
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_confirm
        int 0x80

        mov eax, SYS_STDIN_READ
        mov ebx, confirm_buf
        mov ecx, 8
        int 0x80
        ; Check for "YES"
        cmp byte [confirm_buf + 0], 'Y'
        jne .cancelled
        cmp byte [confirm_buf + 1], 'E'
        jne .cancelled
        cmp byte [confirm_buf + 2], 'S'
        jne .cancelled

        ; Build superblock
        mov eax, SYS_PRINT
        mov ebx, msg_formatting
        int 0x80

        ; Fill sector buffer with zeros
        mov edi, sector_buf
        mov ecx, SECTOR_SIZE
        xor eax, eax
        rep stosb

        ; Write HBFS magic and fields
        mov dword [sector_buf + 0], HBFS_MAGIC
        mov dword [sector_buf + 4], HBFS_VERSION
        mov dword [sector_buf + 8], DEFAULT_SECTORS
        mov dword [sector_buf + 12], 1          ; root dir at LBA 1
        mov dword [sector_buf + 16], SECTOR_SIZE

        ; Copy label (truncate to 15 chars)
        mov esi, dev_label
        mov edi, sector_buf + 20
        mov ecx, 0
.copy_label:
        cmp ecx, 15
        jge .label_done
        mov al, [esi]
        test al, al
        jz .label_done
        mov [edi + ecx], al
        inc ecx
        inc esi
        jmp .copy_label
.label_done:

        ; Raw disk write is a kernel-only operation — always denied from
        ; user-space programs. Print an explanatory error and exit.
        mov eax, SYS_PRINT
        mov ebx, msg_no_raw_write
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.cancelled:
        mov eax, SYS_PRINT
        mov ebx, msg_cancelled
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.write_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

skip_spaces:
        cmp byte [esi], ' '
        je .s
        cmp byte [esi], 9
        je .s
        ret
.s:     inc esi
        jmp skip_spaces

msg_usage:      db "Usage: mkfs <label>", 10
                db "Formats the disk as HBFS with the given label.", 10, 0
msg_warn1:      db "WARNING: About to format disk with label '", 0
msg_warn2:      db "'.", 10
                db "This will DESTROY all data!", 10, 0
msg_confirm:    db "Type YES (uppercase) to proceed: ", 0
msg_cancelled:  db "Cancelled.", 10, 0
msg_formatting: db "Formatting... ", 0
msg_done:       db "Done. HBFS filesystem created.", 10, 0
msg_fail:       db "mkfs: disk write failed", 10, 0
msg_no_raw_write: db "mkfs: raw disk write is not available from user programs.", 10
                  db "      Disk formatting must be performed by the kernel.", 10, 0

dev_label:      times 64 db 0
arg_buf:        times 256 db 0
confirm_buf:    times 8 db 0
sector_buf:     times SECTOR_SIZE db 0
