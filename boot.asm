;
; Mellivora OS - Stage 1 Boot Sector
;
; 512-byte boot sector (MBR) that:
;   1. Saves BIOS boot drive
;   2. Enables A20 gate
;   3. Loads stage 2 (32 sectors = 16KB) from LBA 1-32
;   4. Jumps to stage 2 at 0x7E00
;
; Target: i486+
; Assembled with: nasm -f bin boot.asm -o boot.bin
;

[BITS 16]
[ORG 0x7C00]

STAGE2_ADDR     equ 0x7E00     ; Where we load stage 2
STAGE2_SECTORS  equ 32         ; 32 sectors = 16KB for stage 2
STACK_TOP       equ 0x7C00     ; Stack grows down from here

start:
        cli
        xor ax, ax
        mov ds, ax
        mov es, ax
        mov ss, ax
        mov sp, STACK_TOP
        sti
        cld

        ; Save boot drive number from BIOS (DL)
        mov [boot_drive], dl

        ; Print loading message
        mov si, msg_boot
        call print16

        ;---------------------------------------
        ; Enable A20 gate (fast method via port 0x92)
        ;---------------------------------------
enable_a20:
        in al, 0x92
        test al, 2
        jnz .a20_done           ; Already enabled
        or al, 2
        and al, 0xFE            ; Don't trigger reset
        out 0x92, al
.a20_done:

        ;---------------------------------------
        ; Load stage 2 from disk using LBA
        ; Uses int 0x13 AH=0x42 (Extended Read)
        ;---------------------------------------
load_stage2:
        ; On El Torito boots, BIOS may preload stage 2 already. Keep it intact.
        ; If BIOS reported a CD-style drive number, prefer the emulated HDD (0x80)
        ; for later kernel reads.
        cmp dword [STAGE2_ADDR], 'BOS2'
        jne .load_from_disk
        cmp byte [boot_drive], 0x80
        je .stage2_ready
        cmp byte [boot_drive], 0xE0
        jne .stage2_ready
        mov byte [boot_drive], 0x80
        jmp .stage2_ready

.load_from_disk:
        call read_stage2
        jc .disk_fail

        ; Verify stage 2 magic number
        cmp dword [STAGE2_ADDR], 'BOS2'
        jne .bad_stage2

.stage2_ready:

        ; Pass boot drive in DL, jump to stage 2
        mov dl, [boot_drive]
        jmp 0x0000:STAGE2_ADDR + 4  ; Skip magic, enter stage 2

.disk_fail:
        mov si, msg_disk_err
        call print16
        jmp halt

.bad_stage2:
        mov si, msg_bad_s2
        call print16

halt:
        cli
        hlt
        jmp halt

;---------------------------------------
; Read stage 2, prefer INT 13h extensions and fall back to CHS.
; This improves compatibility with BIOS / El Torito hard-disk emulation.
;---------------------------------------
read_stage2:
        mov dl, [boot_drive]
        call try_stage2_drive
        jnc .remember_drive

        mov al, [boot_drive]
        cmp al, 0x80
        je .try_81
        mov dl, 0x80
        call try_stage2_drive
        jnc .remember_drive

.try_81:
        mov al, [boot_drive]
        cmp al, 0x81
        je .try_82
        mov dl, 0x81
        call try_stage2_drive
        jnc .remember_drive

.try_82:
        mov al, [boot_drive]
        cmp al, 0x82
        je .try_floppy
        mov dl, 0x82
        call try_stage2_drive
        jnc .remember_drive

.try_floppy:
        mov al, [boot_drive]
        cmp al, 0x00
        je .try_cdrom
        mov dl, 0x00
        call try_stage2_drive
        jnc .remember_drive

.try_cdrom:
        mov al, [boot_drive]
        cmp al, 0xE0
        je .fail
        mov dl, 0xE0
        call try_stage2_drive
        jc .fail

.remember_drive:
        mov [boot_drive], dl
        clc
        ret

.fail:
        stc
        ret

try_stage2_drive:
        call read_stage2_once
        jc .fail_try
        cmp dword [STAGE2_ADDR], 'BOS2'
        jne .fail_try
        clc
        ret

.fail_try:
        stc
        ret

read_stage2_once:
        ; Reset first; some BIOSes are picky immediately after El Torito handoff.
        xor ax, ax
        int 0x13

        mov dword [dap + 8], 1
        mov dword [dap + 12], 0

        mov si, dap             ; DS:SI -> Disk Address Packet
        mov ah, 0x42            ; Extended read
        int 0x13
        jnc .ok_once

        ; Fallback for BIOSes that do not support AH=42 on optical/El Torito boot.
        ; If geometry query fails, assume a common translated HDD geometry.
        mov byte [chs_spt], 63
        mov byte [chs_heads], 16
        mov ah, 0x08            ; Query drive geometry
        int 0x13
        jc .chs_ready
        and cl, 0x3F            ; Sectors per track
        jz .chs_ready
        mov [chs_spt], cl
        inc dh                  ; Heads are returned zero-based
        jz .chs_ready
        mov [chs_heads], dh

.chs_ready:
        mov bx, STAGE2_ADDR
        mov si, 1               ; Starting LBA of stage 2
        mov bp, STAGE2_SECTORS

.chs_loop:
        mov ax, si
        xor dx, dx
        div byte [chs_spt]      ; AL = track index, AH = sector remainder
        mov cl, ah
        inc cl                  ; Sectors are 1-based
        xor ah, ah
        div byte [chs_heads]    ; AL = cylinder, AH = head
        mov ch, al
        mov dh, ah
        mov ax, 0x0201          ; Read 1 sector via CHS
        int 0x13
        jnc .chs_ok
        xor ax, ax
        int 0x13                ; Reset and retry once
        mov ax, 0x0201
        int 0x13
        jc .fail_once
.chs_ok:
        add bx, 512
        inc si
        dec bp
        jnz .chs_loop

.ok_once:
        clc
        ret

.fail_once:
        stc
        ret

;---------------------------------------
; Print null-terminated string (16-bit real mode)
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
; Data
;---------------------------------------
boot_drive:     db 0
chs_spt:        db 0
chs_heads:      db 0

; Disk Address Packet for int 0x13 AH=0x42
dap:
        db 16                   ; Size of DAP
        db 0                    ; Reserved
        dw STAGE2_SECTORS       ; Number of sectors to read
        dw STAGE2_ADDR          ; Offset to load to
        dw 0x0000               ; Segment to load to
        dq 1                    ; Starting LBA (sector 1 = second sector)

msg_boot:       db "Mellivora", 0x0D, 0x0A, 0
msg_disk_err:   db "Disk!", 0
msg_bad_s2:     db "Stage2!", 0

; Standard MBR partition table area.
; A simple bootable partition entry improves compatibility with BIOS
; hard-disk emulation used by El Torito CD boot.
        times 446 - ($ - $$) db 0

        ; Partition 1: spans the disk after the MBR sector.
        db 0x80                 ; bootable
        db 0x00                 ; start head
        db 0x02                 ; start sector
        db 0x00                 ; start cylinder
        db 0x83                 ; partition type (generic Linux/custom)
        db 0xFE                 ; end head
        db 0xFF                 ; end sector
        db 0xFF                 ; end cylinder
        dd 1                    ; starting LBA
        dd 131071               ; sector count (64 MiB disk - 1 sector)

        times 16 * 3 db 0       ; remaining partition entries unused
        dw 0xAA55
