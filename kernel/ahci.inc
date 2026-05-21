;=======================================================================
; ahci.inc - AHCI SATA Controller Driver
;
; Supports any AHCI-compliant SATA host controller (PCI class 01h/06h).
; Detected automatically at boot via pci_dev_table.
;
; Public API:
;   ahci_init          - Detect + init; sets ahci_present=1 on success
;   ahci_read_sectors  - EAX=LBA_lo, EDX=LBA_hi, ECX=count, EDI=dest
;   ahci_write_sectors - EAX=LBA_lo, EDX=LBA_hi, ECX=count, ESI=src
;   Returns CF=0 success, CF=1 error
;
; Uses polling (interrupt-free) on command slot 0.
;=======================================================================

; PCI class/subclass for AHCI
AHCI_PCI_CLASS      equ 0x01
AHCI_PCI_SUBCLASS   equ 0x06

; GHC register offsets (from ABAR)
AHCI_GHC_OFF        equ 0x04
AHCI_IS_OFF         equ 0x08
AHCI_PI_OFF         equ 0x0C
AHCI_GHC_AE         equ (1<<31)
AHCI_GHC_HR         equ (1<<0)

; Per-port register offsets (base = ABAR + 0x100 + port*0x80)
AHCI_PORT_STRIDE    equ 0x80
AHCI_PORT_FIRST     equ 0x100

AHCI_P_CLB          equ 0x00
AHCI_P_CLBU         equ 0x04
AHCI_P_FB           equ 0x08
AHCI_P_FBU          equ 0x0C
AHCI_P_IS           equ 0x10
AHCI_P_IE           equ 0x14
AHCI_P_CMD          equ 0x18
AHCI_P_TFD          equ 0x20
AHCI_P_SIG          equ 0x24
AHCI_P_SSTS         equ 0x28
AHCI_P_SERR         equ 0x30
AHCI_P_CI           equ 0x38

AHCI_PCMD_ST        equ (1<<0)
AHCI_PCMD_FRE       equ (1<<4)
AHCI_PCMD_FR        equ (1<<14)
AHCI_PCMD_CR        equ (1<<15)

AHCI_DET_COMM       equ 3
AHCI_SIG_DISK       equ 0x00000101

ATA_READ_DMA_EX     equ 0x25
ATA_WRITE_DMA_EX    equ 0x35
ATA_FLUSH_EXT       equ 0xEA

FIS_H2D             equ 0x27

;=======================================================================
ahci_init:
        pushad
        mov byte [ahci_present], 0

        ; Scan pci_dev_table: +0=vendor +2=device +4=bus +5=dev
        ;                     +6=fn +7=class +8=subclass +9=hdr
        mov esi, pci_dev_table
        xor ecx, ecx
.scan:
        cmp ecx, [pci_dev_count]
        jge .not_found

        movzx eax, byte [esi + 7]
        cmp al, AHCI_PCI_CLASS
        jne .scan_next
        movzx eax, byte [esi + 8]
        cmp al, AHCI_PCI_SUBCLASS
        jne .scan_next

        ; Pack BDF = bus<<16 | dev<<8 | fn
        movzx eax, byte [esi + 4]
        shl  eax, 16
        movzx edx, byte [esi + 5]
        shl  edx, 8
        or   eax, edx
        movzx edx, byte [esi + 6]
        or   eax, edx
        mov  [ahci_bdf], eax

        ; Read BAR5 (ABAR)
        mov ebx, 5
        call pci_read_bar
        test eax, eax
        jz  .scan_next
        and eax, 0xFFFFF000
        test eax, eax
        jz  .scan_next
        mov [ahci_abar], eax
        jmp .found

.scan_next:
        add esi, PCI_ENTRY_SIZE
        inc ecx
        jmp .scan

.not_found:
        mov esi, msg_ahci_none
        call vga_print
        popad
        ret

.found:
        ; Enable bus mastering + memory space
        mov eax, [ahci_bdf]
        mov ebx, 0x04
        call pci_read_config32
        or  eax, 0x06
        mov ecx, eax
        mov eax, [ahci_bdf]
        mov ebx, 0x04
        call pci_write_config32

        ; Enable AHCI mode
        mov ebx, [ahci_abar]
        mov eax, [ebx + AHCI_GHC_OFF]
        or  eax, AHCI_GHC_AE
        mov [ebx + AHCI_GHC_OFF], eax

        mov eax, [ebx + AHCI_PI_OFF]
        mov [ahci_pi], eax

        ; Find first SATA disk port
        xor ecx, ecx
.find_port:
        cmp ecx, 32
        jge .no_disk
        mov eax, 1
        shl eax, cl
        test eax, [ahci_pi]
        jz  .next_port

        mov eax, ecx
        imul eax, AHCI_PORT_STRIDE
        add  eax, AHCI_PORT_FIRST
        add  eax, [ahci_abar]
        mov  [ahci_port_base], eax
        mov  ebx, eax

        mov eax, [ebx + AHCI_P_SSTS]
        and eax, 0x0F
        cmp eax, AHCI_DET_COMM
        jne .next_port

        mov eax, [ebx + AHCI_P_SIG]
        cmp eax, AHCI_SIG_DISK
        je  .port_ok

.next_port:
        inc ecx
        jmp .find_port

.no_disk:
        mov esi, msg_ahci_nodisk
        call vga_print
        popad
        ret

.port_ok:
        call ahci_stop_port

        mov ebx, [ahci_port_base]
        lea eax, [ahci_cmd_list]
        mov [ebx + AHCI_P_CLB],  eax
        mov dword [ebx + AHCI_P_CLBU], 0
        lea eax, [ahci_fis_rx]
        mov [ebx + AHCI_P_FB],  eax
        mov dword [ebx + AHCI_P_FBU], 0

        mov eax, [ebx + AHCI_P_SERR]
        mov [ebx + AHCI_P_SERR], eax
        mov eax, [ebx + AHCI_P_IS]
        mov [ebx + AHCI_P_IS], eax
        mov dword [ebx + AHCI_P_IE], 0

        call ahci_start_port

        mov byte [ahci_present], 1
        mov esi, msg_ahci_ok
        call vga_print
        popad
        ret

;=======================================================================
ahci_stop_port:
        push eax
        push ecx
        push ebx
        mov ebx, [ahci_port_base]
        mov eax, [ebx + AHCI_P_CMD]
        and eax, ~AHCI_PCMD_ST
        mov [ebx + AHCI_P_CMD], eax
        mov ecx, 500000
.cr_wait:
        mov eax, [ebx + AHCI_P_CMD]
        test eax, AHCI_PCMD_CR
        jz  .cr_ok
        loop .cr_wait
.cr_ok:
        mov eax, [ebx + AHCI_P_CMD]
        and eax, ~AHCI_PCMD_FRE
        mov [ebx + AHCI_P_CMD], eax
        mov ecx, 500000
.fr_wait:
        mov eax, [ebx + AHCI_P_CMD]
        test eax, AHCI_PCMD_FR
        jz  .fr_ok
        loop .fr_wait
.fr_ok:
        pop ebx
        pop ecx
        pop eax
        ret

;=======================================================================
ahci_start_port:
        push eax
        push ecx
        push ebx
        mov ebx, [ahci_port_base]
        mov ecx, 500000
.idle_wait:
        mov eax, [ebx + AHCI_P_CMD]
        test eax, AHCI_PCMD_CR
        jz  .idle_ok
        loop .idle_wait
.idle_ok:
        mov eax, [ebx + AHCI_P_CMD]
        or  eax, AHCI_PCMD_FRE
        mov [ebx + AHCI_P_CMD], eax
        or  eax, AHCI_PCMD_ST
        mov [ebx + AHCI_P_CMD], eax
        pop ebx
        pop ecx
        pop eax
        ret

;=======================================================================
; ahci_submit - Build H2D FIS + PRDT, submit slot 0, poll for done.
; All inputs via ahci_tmp_* variables.
; Returns CF=0 success, CF=1 error.
;=======================================================================
ahci_submit:
        push eax
        push ebx
        push ecx
        push edx
        push esi
        push edi

        ; Zero the command table
        mov edi, ahci_cmd_tbl
        mov ecx, 256/4
        xor eax, eax
        rep stosd

        ; H2D Register FIS at offset 0
        mov byte [ahci_cmd_tbl + 0], FIS_H2D
        mov byte [ahci_cmd_tbl + 1], 0x80
        mov al, [ahci_tmp_cmd]
        mov [ahci_cmd_tbl + 2], al
        mov byte [ahci_cmd_tbl + 3], 0x01   ; features=DMA

        mov eax, [ahci_tmp_lba_lo]
        mov [ahci_cmd_tbl + 4], al
        shr eax, 8
        mov [ahci_cmd_tbl + 5], al
        shr eax, 8
        mov [ahci_cmd_tbl + 6], al
        mov byte [ahci_cmd_tbl + 7], 0x40   ; device: LBA/master
        mov eax, [ahci_tmp_lba_lo]
        shr eax, 24
        mov [ahci_cmd_tbl + 8], al
        mov eax, [ahci_tmp_lba_hi]
        mov [ahci_cmd_tbl + 9],  al
        shr eax, 8
        mov [ahci_cmd_tbl + 10], al
        mov byte [ahci_cmd_tbl + 11], 0
        mov byte [ahci_cmd_tbl + 14], 0
        mov byte [ahci_cmd_tbl + 15], 0
        mov ecx, [ahci_tmp_count]
        mov [ahci_cmd_tbl + 12], cl
        mov [ahci_cmd_tbl + 13], ch

        ; PRDT entry at offset 0x80
        mov eax, [ahci_tmp_buf]
        mov [ahci_cmd_tbl + 0x80 + 0], eax
        mov dword [ahci_cmd_tbl + 0x80 + 4], 0
        mov dword [ahci_cmd_tbl + 0x80 + 8], 0
        mov eax, [ahci_tmp_count]
        shl eax, 9
        dec eax
        mov [ahci_cmd_tbl + 0x80 + 12], eax

        ; Command List Header slot 0
        mov eax, 5
        mov ecx, [ahci_tmp_rw]
        shl ecx, 6
        or  eax, ecx
        or  eax, (1 << 16)
        mov [ahci_cmd_list + 0], eax
        mov dword [ahci_cmd_list + 4], 0
        lea eax, [ahci_cmd_tbl]
        mov [ahci_cmd_list + 8], eax
        mov dword [ahci_cmd_list + 12], 0

        ; Issue slot 0
        mov ebx, [ahci_port_base]
        mov eax, [ebx + AHCI_P_SERR]
        mov [ebx + AHCI_P_SERR], eax
        mov eax, [ebx + AHCI_P_IS]
        mov [ebx + AHCI_P_IS], eax
        mov dword [ebx + AHCI_P_CI], 1

        mov ecx, 0x2000000
.poll:
        mov eax, [ebx + AHCI_P_CI]
        test eax, 1
        jz  .done
        mov eax, [ebx + AHCI_P_TFD]
        test eax, 1
        jnz .fail
        loop .poll

.fail:
        call ahci_stop_port
        call ahci_start_port
        mov eax, [ebx + AHCI_P_SERR]
        mov [ebx + AHCI_P_SERR], eax
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
        stc
        ret

.done:
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        pop eax
        clc
        ret

;=======================================================================
; ahci_read_sectors  EAX=LBA_lo  EDX=LBA_hi  ECX=count  EDI=dest
;=======================================================================
ahci_read_sectors:
        cmp byte [ahci_present], 0
        je  .absent
        mov [ahci_tmp_lba_lo], eax
        mov [ahci_tmp_lba_hi], edx
        mov [ahci_tmp_count],  ecx
        mov [ahci_tmp_buf],    edi
        mov byte [ahci_tmp_cmd], ATA_READ_DMA_EX
        mov dword [ahci_tmp_rw], 0
        call ahci_submit
        ret
.absent:
        stc
        ret

;=======================================================================
; ahci_write_sectors  EAX=LBA_lo  EDX=LBA_hi  ECX=count  ESI=src
;=======================================================================
ahci_write_sectors:
        cmp byte [ahci_present], 0
        je  .absent
        mov [ahci_tmp_lba_lo], eax
        mov [ahci_tmp_lba_hi], edx
        mov [ahci_tmp_count],  ecx
        mov [ahci_tmp_buf],    esi
        mov byte [ahci_tmp_cmd], ATA_WRITE_DMA_EX
        mov dword [ahci_tmp_rw], 1
        call ahci_submit
        ret
.absent:
        stc
        ret

;=======================================================================
; ahci_flush - FLUSH CACHE EXT (no data transfer)
;=======================================================================
ahci_flush:
        cmp byte [ahci_present], 0
        je  .skip
        push eax
        push ebx
        push ecx
        push edi

        mov edi, ahci_cmd_tbl
        mov ecx, 256/4
        xor eax, eax
        rep stosd

        mov byte [ahci_cmd_tbl + 0], FIS_H2D
        mov byte [ahci_cmd_tbl + 1], 0x80
        mov byte [ahci_cmd_tbl + 2], ATA_FLUSH_EXT
        mov byte [ahci_cmd_tbl + 7], 0x40

        mov dword [ahci_cmd_list + 0], 5
        mov dword [ahci_cmd_list + 4], 0
        lea eax, [ahci_cmd_tbl]
        mov [ahci_cmd_list + 8], eax
        mov dword [ahci_cmd_list + 12], 0

        mov ebx, [ahci_port_base]
        mov eax, [ebx + AHCI_P_SERR]
        mov [ebx + AHCI_P_SERR], eax
        mov dword [ebx + AHCI_P_CI], 1

        mov ecx, 0x2000000
.fpoll:
        mov eax, [ebx + AHCI_P_CI]
        test eax, 1
        jz  .fdone
        loop .fpoll
.fdone:
        pop edi
        pop ecx
        pop ebx
        pop eax
.skip:
        ret

;-----------------------------------------------------------------------
; BSS data
;-----------------------------------------------------------------------
ahci_present:       db 0
ahci_bdf:           dd 0
ahci_abar:          dd 0
ahci_pi:            dd 0
ahci_port_base:     dd 0

ahci_tmp_lba_lo:    dd 0
ahci_tmp_lba_hi:    dd 0
ahci_tmp_count:     dd 0
ahci_tmp_buf:       dd 0
ahci_tmp_cmd:       db 0
                    db 0, 0, 0
ahci_tmp_rw:        dd 0

align 1024
ahci_cmd_list:      times 1024 db 0

align 256
ahci_fis_rx:        times 256  db 0

align 256
ahci_cmd_tbl:       times 256  db 0

msg_ahci_none:      db "AHCI: no controller", 0x0A, 0
msg_ahci_nodisk:    db "AHCI: no SATA disk", 0x0A, 0
msg_ahci_ok:        db "AHCI: SATA disk ready", 0x0A, 0
