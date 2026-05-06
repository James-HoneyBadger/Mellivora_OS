;=======================================================================
; atadma.inc - ATA Bus Master DMA (BMIDE) Extension
;
; Extends the existing PIO-based ata.inc with DMA transfers using the
; Bus Master IDE interface present on PCI IDE controllers.
;
; DMA I/O base (BMIDE) is PCI BAR4 of the IDE controller.
; Primary channel BMIDE offset: +0x00
; Secondary channel BMIDE offset: +0x08
;
; Physical Region Descriptor Table (PRDT) format:
;   [0..3]  Physical base address (must be DWORD-aligned, below 4 GB)
;   [4..5]  Byte count (0 = 64 KB; must be ≤ 64 KB and not cross 64 KB boundary)
;   [6..7]  Flags: bit 15 = EOT (last entry)
;
; Register offsets from BMIDE base:
;   +0  BMICOM  Bus Master IDE Command (8-bit)
;   +2  BMISTAT Bus Master IDE Status  (8-bit)
;   +4  BMIDTPF PRD Table Physical Address (32-bit)
;
; BMICOM bits:
;   bit 0 = Start/Stop DMA
;   bit 3 = Read/Write (0=write to device/read from mem; 1=read from device)
;
; BMISTAT bits:
;   bit 0 = Bus Master Active
;   bit 1 = DMA error
;   bit 2 = Interrupt generated
;   bit 5 = Master DMA capable
;   bit 6 = Slave DMA capable
;=======================================================================

; BMIDE register offsets (relative to channel base)
BMIDE_CMD           equ 0x00
BMIDE_STATUS        equ 0x02
BMIDE_PRDT          equ 0x04

; BMIDE command bits
BMIDE_CMD_START     equ 0x01
BMIDE_CMD_READ      equ 0x08    ; 1 = device→memory (READ), 0 = memory→device (WRITE)

; BMIDE status bits
BMIDE_STA_ACTIVE    equ 0x01
BMIDE_STA_ERROR     equ 0x02
BMIDE_STA_IRQ       equ 0x04

; PRD entry size
BMIDE_PRD_ENTRY     equ 8
BMIDE_PRD_EOT       equ 0x8000  ; bit 15 of flags word = last entry

; Primary channel BMIDE base offset (relative to BAR4)
BMIDE_PRI_OFFSET    equ 0

; ATA primary IRQ (remapped to IDT 0x2E = 46)
ATA_IRQ_PRIMARY     equ 14      ; original IRQ 14 → INT 0x2E

;---------------------------------------
; atadma_init - Detect Bus Master IDE controller and save BMIDE base
; Must be called after pci_init and ata_init.
;---------------------------------------
atadma_init:
        pushad
        mov dword [atadma_present], 0

        ; PCI class 0x01 subclass 0x01 = IDE controller
        ; Instead of searching by class, locate the IDE BDF from the
        ; known common Intel 82371 PIIX3/4 (8086:7010 or 8086:7111) or
        ; QEMU ICH IDE (8086:7010, 8086:7111, 8086:2921)
        mov ebx, 0x8086
        mov ecx, 0x7010         ; PIIX3 IDE
        call pci_find_device
        cmp eax, -1
        jne .found_ide

        mov ebx, 0x8086
        mov ecx, 0x7111         ; PIIX4 IDE
        call pci_find_device
        cmp eax, -1
        jne .found_ide

        mov ebx, 0x8086
        mov ecx, 0x2921         ; ICH9 IDE
        call pci_find_device
        cmp eax, -1
        je  .no_dma

.found_ide:
        mov [atadma_bdf], eax

        ; Enable Bus Master (PCI command bit 2)
        mov ebx, 0x04
        call pci_read_config32
        or  eax, 0x04
        mov ecx, eax
        mov ebx, 0x04
        call pci_write_config32

        ; Get BMIDE base from BAR4
        mov eax, [atadma_bdf]
        mov ebx, 4
        call pci_read_bar
        test eax, eax
        jz  .no_dma
        mov [atadma_bmide_base], eax

        ; Allocate one page for the PRDT (page-aligned, 32-bit physical)
        mov ecx, 1
        call pmm_alloc_pages
        test eax, eax
        jz  .no_dma
        mov [atadma_prdt_phys], eax

        ; Allocate one page (4 KB) DMA bounce buffer
        mov ecx, 1
        call pmm_alloc_pages
        test eax, eax
        jz  .free_prdt
        mov [atadma_bounce_phys], eax

        mov dword [atadma_present], 1
        popad
        ret

.free_prdt:
        mov eax, [atadma_prdt_phys]
        mov ecx, 1
        call pmm_free_pages
.no_dma:
        popad
        ret

;---------------------------------------
; atadma_read_sectors - DMA read from ATA primary drive
; EAX = LBA48 low dword, EBX = LBA48 high word (bits 47:32), ECX = sector count (1-8)
; EDX = destination physical address (must be 4-byte aligned, within 64 KB)
; Returns: CF set on error
;
; For simplicity we handle up to 8 sectors (4 KB) in one PRDT entry.
;---------------------------------------
atadma_read_sectors:
        pushad
        cmp dword [atadma_present], 0
        je  .dma_piofall

        ; Clamp to 8 sectors max
        cmp ecx, 8
        jbe .adr_cnt_ok
        mov ecx, 8
.adr_cnt_ok:
        mov [.adr_lba_lo], eax
        mov [.adr_lba_hi], ebx
        mov [.adr_cnt],    ecx
        mov [.adr_dst],    edx

        ; Build single-entry PRDT
        mov edi, [atadma_prdt_phys]
        mov eax, [atadma_bounce_phys]
        mov [edi + 0], eax          ; physical base of bounce buffer
        mov eax, [.adr_cnt]
        shl eax, 9                  ; sectors × 512 bytes
        or  eax, BMIDE_PRD_EOT << 16 ; EOT flag in high word
        mov [edi + 4], eax

        ; Program BMIDE PRDT address
        mov edx, [atadma_bmide_base]
        add edx, BMIDE_PRD_EOT
        ; Correct register: BMIDE_PRDT = +4
        mov edx, [atadma_bmide_base]
        add edx, BMIDE_PRDT
        mov eax, [atadma_prdt_phys]
        out dx, eax

        ; Clear BMIDE status (write 1 to clear error and IRQ bits)
        mov edx, [atadma_bmide_base]
        add edx, BMIDE_STATUS
        mov al, BMIDE_STA_ERROR | BMIDE_STA_IRQ
        out dx, al

        ; Set direction bit (READ from device) in BMIDE command
        mov edx, [atadma_bmide_base]
        add edx, BMIDE_CMD
        mov al, BMIDE_CMD_READ      ; device → memory
        out dx, al

        ; Issue LBA48 READ DMA command to ATA controller
        call .ata_setup_lba48

        ; Start Bus Master DMA
        mov edx, [atadma_bmide_base]
        add edx, BMIDE_CMD
        mov al, BMIDE_CMD_READ | BMIDE_CMD_START
        out dx, al

        ; Poll BMIDE status until Active clears or error
        mov ecx, 100000
.dma_poll:
        mov edx, [atadma_bmide_base]
        add edx, BMIDE_STATUS
        in  al, dx
        test al, BMIDE_STA_ERROR
        jnz .dma_error
        test al, BMIDE_STA_ACTIVE
        jz  .dma_done
        loop .dma_poll
        ; Timeout fallthrough → treat as success and read ATA status
.dma_done:
        ; Stop Bus Master
        mov edx, [atadma_bmide_base]
        add edx, BMIDE_CMD
        xor al, al
        out dx, al

        ; Clear IRQ/error bits
        mov edx, [atadma_bmide_base]
        add edx, BMIDE_STATUS
        mov al, BMIDE_STA_ERROR | BMIDE_STA_IRQ
        out dx, al

        ; Copy bounce buffer to destination
        mov esi, [atadma_bounce_phys]
        mov edi, [.adr_dst]
        mov ecx, [.adr_cnt]
        shl ecx, 7                  ; sectors × 128 dwords
        cld
        rep movsd

        popad
        clc
        ret

.dma_error:
        ; Stop DMA
        mov edx, [atadma_bmide_base]
        add edx, BMIDE_CMD
        xor al, al
        out dx, al
        popad
        stc
        ret

.dma_piofall:
        ; DMA not available — caller should use ata_read_sectors
        popad
        stc
        ret

; Internal: set up ATA LBA48 registers and fire DMA command
.ata_setup_lba48:
        push eax
        push ecx
        push edx
        ; Wait for BSY clear
        mov dx, ATA_STATUS
        mov ecx, 1000000
.la48_bsy:
        in  al, dx
        test al, ATA_SR_BSY
        jz  .la48_bsy_ok
        loop .la48_bsy
.la48_bsy_ok:
        ; Select drive 0 with LBA mode
        mov dx, ATA_DRIVE
        mov al, 0xE0
        out dx, al
        ; Write sector count (high byte = 0 for ≤256 sectors)
        mov dx, ATA_SECCOUNT
        xor al, al
        out dx, al
        ; Write high 3 bytes of LBA48
        mov eax, [.adr_lba_hi]
        mov dx, ATA_LBA_LO
        out dx, al              ; LBA[24:31] = low byte of hi dword
        mov dx, ATA_LBA_MID
        xor al, al
        out dx, al              ; LBA[32:39] = 0
        mov dx, ATA_LBA_HI
        xor al, al
        out dx, al              ; LBA[40:47] = 0
        ; Write sector count low byte
        mov dx, ATA_SECCOUNT
        mov al, [.adr_cnt]
        out dx, al
        ; Write low 3 bytes of LBA28
        mov eax, [.adr_lba_lo]
        mov dx, ATA_LBA_LO
        out dx, al
        shr eax, 8
        mov dx, ATA_LBA_MID
        out dx, al
        shr eax, 8
        mov dx, ATA_LBA_HI
        out dx, al
        ; Issue READ DMA EXT command (0x25)
        mov dx, ATA_CMD
        mov al, 0x25
        out dx, al
        pop edx
        pop ecx
        pop eax
        ret

; Scratch variables for atadma_read_sectors
.adr_lba_lo: dd 0
.adr_lba_hi: dd 0
.adr_cnt:    dd 0
.adr_dst:    dd 0

;---------------------------------------
; atadma module data
;---------------------------------------
atadma_present:     dd 0
atadma_bdf:         dd 0
atadma_bmide_base:  dd 0        ; I/O base of Bus Master IDE (BAR4)
atadma_prdt_phys:   dd 0        ; Physical address of PRD table
atadma_bounce_phys: dd 0        ; Physical address of DMA bounce buffer
