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
        mov si, dap             ; DS:SI -> Disk Address Packet
        mov ah, 0x42            ; Extended read
        mov dl, [boot_drive]
        int 0x13
        jc .disk_fail

        ; Verify stage 2 magic number
        cmp dword [STAGE2_ADDR], 'BOS2'
        jne .bad_stage2

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

; Pad and boot signature
        times 510 - ($ - $$) db 0
        dw 0xAA55
