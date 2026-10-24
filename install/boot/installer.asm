;=======================================================================
; installer.asm - Mellivora OS installer payload
;
; Runs at 0x1000:0000 (physical 0x10000) after stage1.asm loads it from the
; CD. It finds MELLIVORA.IMG in the ISO's /BOOT directory, lists the BIOS hard
; disks, asks which one to use, and writes the image to it sector by sector.
;
; Memory:
;   0x1000:0000  this code, its data and a 2 KB sector buffer
;   0x2000:0000  32 KB transfer buffer
;   0x1000:F000  stack
;=======================================================================
[BITS 16]
[ORG 0x0000]

CHUNK_BYTES     equ 32768               ; 16 CD sectors = 64 disk sectors
CD_SECTS        equ CHUNK_BYTES / 2048
HD_SECTS        equ CHUNK_BYTES / 512
BUF_SEG         equ 0x2000
MAX_DISKS       equ 8

start:
        cli
        mov ax, cs
        mov ds, ax
        mov es, ax
        mov ss, ax
        mov sp, 0xF000
        sti
        cld
        mov [boot_drive], dl

        call clear_screen
        mov si, msg_banner
        call print

        ; ---- Locate the system image on the CD -------------------------
        mov si, msg_find
        call print
        call find_image
        jc .no_image
        mov si, msg_found
        call print
        mov eax, [image_lba]
        call print_dec
        mov si, msg_found2
        call print
        mov eax, [image_size]
        shr eax, 20                     ; bytes -> MiB
        call print_dec
        mov si, msg_mib
        call print

        ; ---- Which disk? ----------------------------------------------
        call list_disks
        cmp word [disk_count], 0
        jne .have_disks
        mov si, msg_no_disk
        call print
        jmp .halt
.have_disks:
.choose:
        mov si, msg_ask
        call print
.ask_key:
        mov ah, 0x00
        int 0x16
        cmp al, 27                      ; Esc quits
        je .halt
        cmp al, '1'
        jb .ask_key
        movzx ebx, byte [disk_count]
        add bl, '0'
        cmp al, bl
        ja .ask_key
        sub al, '1'
        movzx ebx, al
        mov al, [disk_num + ebx]
        mov [target_drive], al
        mov eax, [disk_mb + ebx*4]
        mov [target_mb], eax

        ; The disk has to be at least as large as the image.
        mov eax, [image_size]
        add eax, 0xFFFFF
        shr eax, 20                      ; image size in MiB, rounded up
        mov [image_mb], eax
        mov ebx, [target_mb]
        cmp ebx, eax
        jae .size_ok
        mov si, msg_too_small
        call print
        mov eax, [image_mb]
        call print_dec
        mov si, msg_mib_only
        call print
        jmp .choose

.size_ok:
        call newline
        mov si, msg_target
        call print
        mov al, [target_drive]
        call print_hex_byte
        mov si, msg_target2
        call print
        mov eax, [target_mb]
        call print_dec
        mov si, msg_mib_erase
        call print

        ; ---- Type YES -------------------------------------------------
        mov si, msg_confirm
        call print
        mov di, confirm_buf
        call read_word
        mov si, confirm_buf
        mov di, msg_yes
        call str_ieq
        jnc .halt
        call newline

        ; ---- Copy ------------------------------------------------------
        mov si, msg_copy
        call print
        call copy_image
        jc .copy_failed

        ; ---- Verify ----------------------------------------------------
        mov si, msg_verify
        call print
        call verify_first
        jc .verify_failed
        mov si, msg_ok
        call print
        jmp .done

.copy_failed:
        mov si, msg_err_copy
        call print
        mov al, [last_err]
        call print_hex_byte
        call newline
        jmp .halt
.verify_failed:
        mov si, msg_err_verify
        call print
        jmp .halt
.no_image:
        mov si, msg_err_image
        call print
        jmp .halt

.done:
        mov si, msg_used
        call print
        mov eax, [image_mb]
        call print_dec
        mov si, msg_onto
        call print
        mov eax, [target_mb]
        call print_dec
        mov si, msg_onto2
        call print
        mov si, msg_done
        call print
        mov ah, 0x00
        int 0x16
.halt_quiet:
        cli
.hq:    hlt
        jmp .hq
.halt:
        mov si, msg_halt
        call print
        cli
.h:     hlt
        jmp .h

;=======================================================================
; find_image - locate /BOOT/MELLIVORA.IMG in the ISO9660 tree
; Out: [image_lba], [image_size]; CF set when not found
;=======================================================================
find_image:
        ; Primary Volume Descriptor at CD LBA 16
        mov eax, 16
        mov di, sector_buf
        call cd_read_sector
        jc .fi_fail
        cmp byte [sector_buf], 1           ; type 1 = primary volume descriptor
        jne .fi_fail
        cmp byte [sector_buf + 1], 'C'
        jne .fi_fail
        cmp byte [sector_buf + 2], 'D'
        jne .fi_fail
        cmp byte [sector_buf + 3], '0'
        jne .fi_fail

        ; Root directory record lives at offset 156.
        mov eax, [sector_buf + 156 + 2]
        mov [dir_lba], eax
        mov eax, [sector_buf + 156 + 10]
        mov [dir_size], eax

        ; Look for the image in the root directory first.
        mov si, name_image
        call find_entry
        jnc .fi_found

        ; Then try /BOOT in case the image ships next to the boot loader.
        mov si, name_boot
        call find_entry
        jc .fi_fail
        test byte [entry_flags], 0x02
        jz .fi_fail
        mov eax, [entry_lba]
        mov [dir_lba], eax
        mov eax, [entry_size]
        mov [dir_size], eax
        mov si, name_image
        call find_entry
        jc .fi_fail

.fi_found:
        test byte [entry_flags], 0x02      ; must be a file, not a directory
        jnz .fi_fail
        mov eax, [entry_lba]
        mov [image_lba], eax
        mov eax, [entry_size]
        mov [image_size], eax
        test eax, eax
        jz .fi_fail
        clc
        ret
.fi_fail:
        stc
        ret

;-----------------------------------------------------------------------
; find_entry - search the directory at [dir_lba] for the name at DS:SI
; Out: [entry_lba], [entry_size], [entry_flags]; CF set if not found
;-----------------------------------------------------------------------
find_entry:
        push si                           ; cd_read_sector clobbers SI
        mov eax, [dir_lba]
        mov di, sector_buf
        call cd_read_sector
        pop si
        jc .fe_fail
        mov bx, sector_buf
.fe_next:
        movzx eax, byte [bx]              ; record length
        cmp al, 34                        ; shortest possible record
        jb .fe_fail                       ; 0 = end of records, less = corrupt
        movzx ecx, byte [bx + 32]         ; identifier length
        test ecx, ecx
        jz .fe_skip

        push si
        call rr_name_matches              ; Rock Ridge name, if present
        pop si
        jnc .fe_hit

        lea di, [bx + 33]                 ; plain ISO9660 identifier
        movzx ecx, byte [bx + 32]
        push si
        push cx
        call name_matches
        pop cx
        pop si
        jnc .fe_hit
.fe_skip:
        movzx eax, byte [bx]
        add bx, ax
        jmp .fe_next
.fe_hit:
        mov eax, [bx + 2]
        mov [entry_lba], eax
        mov eax, [bx + 10]
        mov [entry_size], eax
        mov al, [bx + 25]
        mov [entry_flags], al
        clc
        ret
.fe_fail:
        stc
        ret

;-----------------------------------------------------------------------
; rr_name_matches - compare the Rock Ridge NM name of the directory record
; at BX with the plain ASCII name at DS:SI
; Out: CF clear on match
;-----------------------------------------------------------------------
rr_name_matches:
        push bx
        push bp
        push di
        movzx eax, byte [bx + 32]
        lea bp, [bx + 33]
        add bp, ax                        ; system use area starts here
        test al, 1
        jz .rr_aligned
        inc bp
.rr_aligned:
        movzx ecx, byte [bx]
        add cx, bx                        ; end of record
.rr_loop:
        mov ax, bp
        add ax, 4
        cmp ax, cx
        ja .rr_no                         ; not enough room for an entry header
        cmp byte [bp], 'N'
        jne .rr_next
        cmp byte [bp + 1], 'M'
        jne .rr_next
        movzx eax, byte [bp + 2]          ; entry length
        cmp al, 5
        jb .rr_next
        test byte [bp + 4], 0x06          ; skip "." and ".." entries
        jnz .rr_next

        movzx ecx, byte [bp + 2]
        sub ecx, 5
        lea di, [bp + 5]
        push si
.rr_cmp:
        test ecx, ecx
        jz .rr_cmp_end
        mov al, [si]
        test al, al
        jz .rr_cmp_fail
        mov dl, [di]
        call to_upper_al
        cmp al, dl
        jne .rr_cmp_fail
        inc si
        inc di
        dec ecx
        jmp .rr_cmp
.rr_cmp_end:
        cmp byte [si], 0
        jne .rr_cmp_fail
        pop si
        pop di
        pop bp
        pop bx
        clc
        ret
.rr_cmp_fail:
        pop si
.rr_next:
        movzx eax, byte [bp + 2]
        test al, al
        jz .rr_no
        add bp, ax
        jmp .rr_loop
.rr_no:
        pop di
        pop bp
        pop bx
        stc
        ret

;-----------------------------------------------------------------------
; name_matches - compare DS:SI (plain ASCII, NUL terminated) with the ISO
; name at ES:DI (length ECX, may carry a ";1" version suffix)
; Out: CF clear on match
;-----------------------------------------------------------------------
name_matches:
        push si
        push di
        push cx
.nm_loop:
        test cx, cx
        jz .nm_end
        mov al, [si]
        test al, al
        jz .nm_iso_left
        cmp al, ';'                       ; our name never has a version
        je .nm_end
        mov dl, [di]
        call to_upper_al
        xchg al, dl
        call to_upper_al
        cmp al, dl
        jne .nm_no
        inc si
        inc di
        dec cx
        jmp .nm_loop
.nm_end:
        cmp byte [si], 0
        jne .nm_no
        clc
        jmp .nm_done
.nm_iso_left:
        ; ISO name has a version suffix such as ";1"
        cmp byte [di], ';'
        jne .nm_no
        clc
        jmp .nm_done
.nm_no:
        stc
.nm_done:
        pop cx
        pop di
        pop si
        ret

to_upper_al:
        cmp al, 'a'
        jb .tu_done
        cmp al, 'z'
        ja .tu_done
        sub al, 32
.tu_done:
        ret

;=======================================================================
; list_disks - enumerate BIOS hard disks and print them
;=======================================================================
list_disks:
        mov word [disk_count], 0
        mov byte [cur_drive], 0x80
.ld_loop:
        cmp byte [cur_drive], 0x80 + MAX_DISKS
        jae .ld_print
        ; INT 13 extensions present? (BX comes back 0xAA55 on success)
        mov dl, [cur_drive]
        mov bx, 0x55AA
        mov ah, 0x41
        int 0x13
        jc .ld_next
        cmp bx, 0xAA55
        jne .ld_next

        mov dl, [cur_drive]
        mov si, disk_dap
        mov ah, 0x48
        int 0x13
        jc .ld_next

        mov ecx, [disk_dap + 16]          ; sectors, low dword
        mov eax, [disk_dap + 20]          ; sectors, high dword
        mov edx, ecx
        or edx, eax
        jz .ld_next
        ; MiB = (high << 21) | (low >> 11)
        shr ecx, 11
        shl eax, 21
        or eax, ecx
        test eax, eax
        jz .ld_next

        movzx ecx, word [disk_count]
        mov dl, [cur_drive]
        mov [disk_num + ecx], dl
        mov [disk_mb + ecx*4], eax
        inc word [disk_count]

.ld_next:
        inc byte [cur_drive]
        jmp .ld_loop

.ld_print:
        mov si, msg_disks
        call print
        movzx ecx, word [disk_count]
        test ecx, ecx
        jz .ld_none
        xor ebx, ebx
.ldp:
        mov al, bl
        add al, '1'
        call putchar
        mov si, msg_dot
        call print
        mov eax, [disk_mb + ebx*4]
        call print_dec
        mov si, msg_mib_nl
        call print
        inc ebx
        movzx ecx, word [disk_count]
        cmp ebx, ecx
        jb .ldp
.ld_none:
        ret

;=======================================================================
; copy_image - stream the image from CD to the target disk
; Out: CF set on error
;=======================================================================
copy_image:
        mov eax, [image_lba]
        mov [src_lba], eax
        mov dword [dst_lba], 0
        mov eax, [image_size]
        mov [remaining], eax
        mov dword [progress_mark], 0

.ci_loop:
        mov eax, [remaining]
        test eax, eax
        jz .ci_done

        ; Count this chunk
        mov ebx, CHUNK_BYTES
        cmp eax, ebx
        jae .ci_full
        mov ebx, eax
.ci_full:
        mov [chunk], ebx

        ; CD read
        mov eax, [chunk]
        xor edx, edx
        mov ecx, 2048
        div ecx
        mov [dap_count], ax
        mov eax, [src_lba]
        mov [dap_lba], eax
        mov dword [dap_lba + 4], 0
        mov word [dap_off], 0
        mov word [dap_seg], BUF_SEG
        mov si, dap
        mov dl, [boot_drive]
        mov ah, 0x42
        int 0x13
        jnc .ci_read_ok
        mov [last_err], ah
        stc
        ret
.ci_read_ok:

        ; Disk write
        mov eax, [chunk]
        shr eax, 9                         ; bytes -> 512-byte sectors
        mov [wdap_count], ax
        mov eax, [dst_lba]
        mov [wdap_lba], eax
        mov dword [wdap_lba + 4], 0
        mov word [wdap_off], 0
        mov word [wdap_seg], BUF_SEG
        mov si, wdap
        mov dl, [target_drive]
        mov ah, 0x43
        int 0x13
        jnc .ci_write_ok
        mov [last_err], ah
        stc
        ret
.ci_write_ok:

        ; Advance
        mov eax, [chunk]
        mov ebx, eax
        shr ebx, 11                        ; CD sectors
        add [src_lba], ebx
        shr eax, 9                         ; disk sectors
        add [dst_lba], eax
        mov eax, [chunk]
        sub [remaining], eax

        ; Progress dots: one per MiB written
        mov eax, [dst_lba]
        shr eax, 11                        ; MiB
        mov ebx, [progress_mark]
        cmp eax, ebx
        jbe .ci_loop
        mov [progress_mark], eax
        mov al, '.'
        call putchar
        jmp .ci_loop

.ci_done:
        call newline
        clc
        ret

;=======================================================================
; verify_first - read sector 0 back from the target and compare
;=======================================================================
verify_first:
        mov word [wdap_count], 1
        mov dword [wdap_lba], 0
        mov dword [wdap_lba + 4], 0
        mov word [wdap_off], 0
        mov word [wdap_seg], BUF_SEG
        mov si, wdap
        mov dl, [target_drive]
        mov ah, 0x42
        int 0x13
        jc .vf_fail

        ; Compare with the CD's sector 0 of the image
        mov eax, [image_lba]
        mov di, sector_buf
        call cd_read_sector
        jc .vf_fail
        mov si, sector_buf
        mov ax, BUF_SEG
        mov es, ax
        xor di, di
        mov cx, 256
        push ds
        push cs
        pop ds
        repe cmpsb
        pop ds
        mov ax, cs
        mov es, ax
        jne .vf_fail
        clc
        ret
.vf_fail:
        stc
        ret

;=======================================================================
; I/O helpers
;=======================================================================
cd_read_sector:
        ; In: EAX = LBA, DI = destination offset in this segment
        mov [dap_lba], eax
        mov dword [dap_lba + 4], 0
        mov word [dap_count], 1
        mov [dap_off], di
        mov ax, cs
        mov [dap_seg], ax
        push ds
        push cs
        pop ds
        mov si, dap
        mov dl, [boot_drive]
        mov ah, 0x42
        int 0x13
        pop ds
        jnc .crs_ok
        mov [last_err], ah
        stc
        ret
.crs_ok:
        clc
        ret

putchar:
        push bx
        mov ah, 0x0E
        mov bx, 0x0007
        int 0x10
        pop bx
        ret

newline:
        mov al, 13
        call putchar
        mov al, 10
        jmp putchar

print:
        push ax
        push bx
        push si
.next:
        lodsb
        test al, al
        jz .done
        cmp al, 10
        jne .put
        mov ah, 0x0E
        mov al, 13
        mov bx, 0x0007
        int 0x10
        mov al, 10
.put:
        call putchar
        jmp .next
.done:
        pop si
        pop bx
        pop ax
        ret

print_hex_byte:
        push ax
        push cx
        mov cx, 2
.nyb:
        rol al, 4
        push ax
        and al, 0x0F
        cmp al, 10
        jb .dig
        add al, 'A' - 10
        jmp .out
.dig:
        add al, '0'
.out:
        call putchar
        pop ax
        loop .nyb
        pop cx
        pop ax
        ret

print_dec:
        push eax
        push ebx
        push ecx
        push edx
        mov ebx, 10
        xor ecx, ecx
.pd_loop:
        xor edx, edx
        div ebx
        push edx
        inc ecx
        test eax, eax
        jnz .pd_loop
.pd_out:
        pop eax
        add al, '0'
        call putchar
        loop .pd_out
        pop edx
        pop ecx
        pop ebx
        pop eax
        ret

; Read a line (terminated by Enter) into ES:DI, at most 8 characters.
read_word:
        push ax
        push di
        xor cx, cx
.rw_key:
        mov ah, 0x00
        int 0x16
        cmp al, 0x0D
        je .rw_done
        cmp al, 0x08
        je .rw_back
        cmp cx, 7
        jae .rw_key
        stosb
        inc cx
        call putchar
        jmp .rw_key
.rw_back:
        test cx, cx
        jz .rw_key
        dec cx
        dec di
        mov al, 8
        call putchar
        mov al, ' '
        call putchar
        mov al, 8
        call putchar
        jmp .rw_key
.rw_done:
        mov byte [di], 0
        pop di
        pop ax
        ret

;-----------------------------------------------------------------------
; str_ieq - case-insensitive compare DS:SI with DS:DI, both NUL terminated
; Out: CF set when equal
;-----------------------------------------------------------------------
str_ieq:
        push si
        push di
.se_loop:
        mov al, [si]
        mov dl, [di]
        call to_upper_al
        xchg al, dl
        call to_upper_al
        cmp al, dl
        jne .se_no
        test al, al
        jz .se_yes
        inc si
        inc di
        jmp .se_loop
.se_yes:
        stc
        jmp .se_done
.se_no:
        clc
.se_done:
        pop di
        pop si
        ret

clear_screen:
        mov ax, 0x0003
        int 0x10
        ret

;=======================================================================
; Data
;=======================================================================
msg_banner:   db "Mellivora OS installer", 10
              db "=====================", 10, 10, 0
msg_find:     db "Looking for MELLIVORA.IMG on the CD...", 10, 0
msg_found:    db "Found the system image at CD sector ", 0
msg_found2:   db ", ", 0
msg_mib:      db " MiB.", 10, 0
msg_disks:    db 10, "Disks found:", 10, 0
msg_dot:      db ". ", 0
msg_mib_nl:   db " MiB", 10, 0
msg_no_disk:  db "No BIOS hard disk was found.", 10
              db "Attach a disk (IDE/SATA in legacy mode) and try again.", 10, 0
msg_ask:      db "Install to which disk? ", 0
msg_too_small: db 10, "That disk is too small - this image needs ", 0
msg_mib_only: db " MiB.", 10, 0
msg_target:   db "Target: BIOS drive 0x", 0
msg_target2:  db ", ", 0
msg_mib_erase: db " MiB. EVERYTHING ON IT WILL BE ERASED.", 10, 0
msg_confirm:  db "Type YES to continue: ", 0
msg_yes:      db "YES", 0
msg_copy:     db 10, "Writing the image (one dot per MiB, this takes a few minutes):", 10, 0
msg_verify:   db "Verifying the first sector... ", 0
msg_ok:       db "OK", 10, 0
msg_used:     db "Installed ", 0
msg_onto:     db " MiB onto a ", 0
msg_onto2:    db " MiB disk; the rest is left untouched.", 10
msg_done:     db 10, "Installation complete.", 10
              db "Remove the CD, then boot from the disk.", 10
              db "Press any key.", 10, 0
msg_halt:     db 10, "Installer stopped.", 10, 0
msg_err_image:  db "MELLIVORA.IMG was not found on this CD.", 10, 0
msg_err_copy:   db "Write failed, BIOS status 0x", 0
msg_err_verify: db "The first sector did not read back correctly.", 10, 0

name_boot:    db "BOOT", 0
name_image:   db "MELLIVORA.IMG", 0

boot_drive:   db 0
target_drive: db 0
last_err:     db 0

disk_count:   dw 0
cur_drive:    db 0
disk_num:     times MAX_DISKS db 0
disk_mb:      times MAX_DISKS dd 0
confirm_buf:  times 16 db 0

image_lba:    dd 0
image_size:   dd 0
dir_lba:      dd 0
dir_size:     dd 0
entry_lba:    dd 0
entry_size:   dd 0
entry_flags:  db 0
src_lba:      dd 0
dst_lba:      dd 0
remaining:    dd 0
chunk:        dd 0
progress_mark: dd 0
target_mb:    dd 0
image_mb:     dd 0

align 4
dap:
        db 0x10
        db 0
dap_count: dw 1
dap_off:   dw 0
dap_seg:   dw 0
dap_lba:   dq 0

wdap:
        db 0x10
        db 0
wdap_count: dw 1
wdap_off:   dw 0
wdap_seg:   dw 0
wdap_lba:   dq 0

disk_dap:
        dw 0x1A
        dw 0
        dd 0
        dd 0
        dd 0
        dq 0
        dw 0

sector_buf: times 2048 db 0
