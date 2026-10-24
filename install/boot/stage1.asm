;=======================================================================
; stage1.asm - El Torito boot stub for the Mellivora installer
;
; The BIOS loads this image (no emulation) at 0x7C00.  A BIOS may honour the
; catalog's sector count or load only the first 2 KB, so stage 1 does the
; minimum: find the boot catalog, learn where this image lives on the CD,
; load the rest of the installer to 0x10000, and jump to it.
;
; installer.asm is appended by build_iso.sh and linked at 0x1000:0000.
;=======================================================================
[BITS 16]
[ORG 0x7C00]

PAYLOAD_SEG     equ 0x1000          ; 0x1000:0000 = physical 0x10000
PAYLOAD_SECTORS equ 32              ; 32 * 2048 = 64 KB (build pads to this)
BVD_BUF         equ 0x8000          ; 2 KB volume descriptor scratch
CAT_BUF         equ 0x8800          ; 2 KB boot catalog

start:
        cli
        xor ax, ax
        mov ds, ax
        mov es, ax
        mov ss, ax
        mov sp, 0x7000                  ; below the boot sector, 3 KB of stack
        sti
        cld
        mov [boot_drive], dl

        mov si, msg_stage1
        call print

        ; ---- Boot Record Volume Descriptor at CD LBA 17 ----------------
        ; This is the El Torito descriptor (type 0); its offset 0x47 holds
        ; the LBA of the boot catalog. The Primary Volume Descriptor at
        ; LBA 16 has the same bytes at that offset but a different meaning.
        mov dword [dap_lba], 17
        mov dword [dap_lba + 4], 0
        mov word [dap_count], 1
        mov word [dap_off], BVD_BUF
        mov word [dap_seg], 0
        call cd_read
        jc .fail_bvd
        cmp byte [BVD_BUF], 0
        jne .fail_bvd
        cmp dword [BVD_BUF + 1], 'CD00'
        jne .fail_bvd

        mov eax, [BVD_BUF + 0x47]          ; boot catalog LBA (LE)
        test eax, eax
        jz .fail_bvd
        mov [dap_lba], eax
        mov dword [dap_lba + 4], 0
        mov word [dap_count], 1
        mov word [dap_off], CAT_BUF
        call cd_read
        jc .fail_cat

        ; Initial/default entry: 0x88 at +32, load RBA at +40.
        cmp byte [CAT_BUF + 32], 0x88
        jne .fail_cat
        mov eax, [CAT_BUF + 40]
        test eax, eax
        jz .fail_cat
        inc eax                            ; skip stage 1's own 2 KB

        ; ---- Load the payload at 0x10000 -------------------------------
        mov [dap_lba], eax
        mov dword [dap_lba + 4], 0
        mov word [dap_count], PAYLOAD_SECTORS
        mov word [dap_off], 0
        mov word [dap_seg], PAYLOAD_SEG
        call cd_read
        jc .fail_pay

        mov dl, [boot_drive]
        jmp PAYLOAD_SEG:0x0000

.fail_bvd:
        mov si, msg_fail_bvd
        jmp .fail
.fail_cat:
        mov si, msg_fail_cat
        jmp .fail
.fail_pay:
        mov si, msg_fail_pay
        jmp .fail
.fail:
        call print
        mov al, [last_err]
        call print_hex
        mov si, msg_nl
        call print
.halt:
        hlt
        jmp .halt

;-----------------------------------------------------------------------
; cd_read - INT 13h extended read, 2048-byte sectors (CD)
; Out: CF set on error, [last_err] = BIOS status
;-----------------------------------------------------------------------
cd_read:
        mov si, dap
        mov dl, [boot_drive]
        mov ah, 0x42
        int 0x13
        jnc .ok
        mov [last_err], ah
        stc
        ret
.ok:
        clc
        ret

;-----------------------------------------------------------------------
; print - teletype output of the NUL-terminated string at DS:SI
;-----------------------------------------------------------------------
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
        mov ah, 0x0E
        mov bx, 0x0007
        int 0x10
        jmp .next
.done:
        pop si
        pop bx
        pop ax
        ret

print_hex:
        push ax
        push cx
        mov cx, 2
.hx:
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
        mov ah, 0x0E
        mov bx, 0x0007
        int 0x10
        pop ax
        loop .hx
        pop cx
        pop ax
        ret

msg_stage1: db "Mellivora installer: loading", 13, 10, 0
msg_fail:     db "Installer stage 1 failure, status 0x", 0
msg_fail_bvd: db "cannot read the El Torito descriptor; ", 0
msg_fail_cat: db "cannot read the boot catalog; ", 0
msg_fail_pay: db "cannot load the installer body; ", 0
msg_nl:     db 13, 10, 0

boot_drive: db 0
last_err:   db 0

align 4
dap:
        db 0x10
        db 0
dap_count: dw 1
dap_off:   dw 0
dap_seg:   dw 0
dap_lba:   dq 0

times 2048 - ($ - $$) db 0
