;=======================================================================
; ATA DISK DRIVER (PIO + DMA, LBA48 - supports up to 128 PB)
;=======================================================================

ata_init:
        PUSHALL
        mov byte [ata_dma_capable], 0   ; default: no DMA

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

        ; Clear task file registers before IDENTIFY
        mov dx, ATA_SECCOUNT
        xor al, al
        out dx, al
        mov dx, ATA_LBA_LO
        out dx, al
        mov dx, ATA_LBA_MID
        out dx, al
        mov dx, ATA_LBA_HI
        out dx, al

        ; Send IDENTIFY command
        mov dx, ATA_CMD
        mov al, ATA_CMD_IDENTIFY
        out dx, al

        ; Check if drive exists at all
        mov dx, ATA_STATUS
        in al, dx
        test al, al
        jz .no_drive

        ; Wait for BSY to clear
        call ata_wait_ready
        jc .no_drive

        ; Reject non-ATA devices (e.g. ATAPI CD-ROM on ISO boot)
        mov dx, ATA_LBA_MID
        in al, dx
        test al, al
        jnz .no_drive
        mov dx, ATA_LBA_HI
        in al, dx
        test al, al
        jnz .no_drive

        ; Wait for DRQ with timeout so unsupported devices don't hang boot
        mov ecx, 0x100000
.wait_drq:
        mov dx, ATA_STATUS
        in al, dx
        test al, ATA_SR_ERR
        jnz .no_drive
        test al, ATA_SR_DRQ
        jnz .have_drq
        loop .wait_drq
        jmp .no_drive

.have_drq:
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

        ; Probe for Bus Master IDE DMA support via PCI
        call ata_dma_probe

        POPALL
        ret

.no_drive:
        mov byte [ata_present], 0
        mov esi, msg_ata_none
        call vga_print
        POPALL
        ret

; Wait for ATA drive ready
; Returns: CF set on timeout
ata_wait_ready:
        push rcx
        mov ecx, 0x100000       ; Timeout counter
.loop:
        mov dx, ATA_STATUS
        in al, dx
        test al, ATA_SR_BSY
        jz .ready
        loop .loop
        stc                     ; Timeout
        pop rcx
        ret
.ready:
        clc
        pop rcx
        ret

; Read sectors using LBA48 (raw, no retry)
; EAX = LBA low 32 bits, ECX = sector count (1-256), EDI = destination buffer
; Returns: CF set on error
ata_read_sectors_raw:
        PUSHALL

        push rax                ; Save LBA
        push rcx                ; Save count

        ; Select drive (master, LBA mode) and wait ready
        mov dx, ATA_DRIVE
        mov al, 0x40            ; Master, LBA
        out dx, al
        call ata_wait_ready
        jc .error_setup         ; Drive not ready

        ; Send high bytes first (LBA48 requires two writes)
        ; Sector count high byte
        pop rcx
        push rcx
        mov dx, ATA_SECCOUNT
        mov al, ch              ; High byte of sector count (LBA48 16-bit)
        out dx, al

        ; LBA48 high bytes: byte 3 = EAX[24:31], bytes 4-5 = 0
        mov eax, [rsp + 8]      ; get saved LBA from stack
        shr eax, 24             ; extract bits 24-31
        mov dx, ATA_LBA_LO
        out dx, al              ; LBA byte 3
        xor al, al
        mov dx, ATA_LBA_MID
        out dx, al              ; LBA byte 4 = 0
        mov dx, ATA_LBA_HI
        out dx, al              ; LBA byte 5 = 0

        ; Now send low bytes
        pop rcx
        push rcx
        mov dx, ATA_SECCOUNT
        mov al, cl              ; Sector count low
        out dx, al

        ; LBA bytes 0-2
        pop rcx
        pop rax                 ; Restore LBA

        push rcx                ; Save count again
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

        pop rcx                 ; Sector count

.read_loop:
        push rcx
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

        pop rcx
        loop .read_loop

        POPALL
        clc
        ret

.error:
        pop rcx
        POPALL
        stc
        ret

.error_setup:
        pop rcx                 ; Clean up pushed count
        pop rax                 ; Clean up pushed LBA
        POPALL
        stc
        ret

; Write sectors using LBA48 (raw, no retry)
; EAX = LBA low 32 bits, ECX = sector count, ESI = source buffer
; Returns: CF set on error
ata_write_sectors_raw:
        PUSHALL

        push rax
        push rcx

        ; Select drive and wait for it to be ready
        mov dx, ATA_DRIVE
        mov al, 0x40
        out dx, al
        call ata_wait_ready
        jc .werror_setup        ; Drive not ready

        ; High bytes - sector count high byte
        pop rcx
        push rcx
        mov dx, ATA_SECCOUNT
        mov al, ch              ; High byte of sector count (LBA48)
        out dx, al
        ; LBA48 high bytes: byte 3 = EAX[24:31], bytes 4-5 = 0
        mov eax, [rsp + 8]      ; get saved LBA from stack
        shr eax, 24             ; extract bits 24-31
        mov dx, ATA_LBA_LO
        out dx, al              ; LBA byte 3
        xor al, al
        mov dx, ATA_LBA_MID
        out dx, al              ; LBA byte 4 = 0
        mov dx, ATA_LBA_HI
        out dx, al              ; LBA byte 5 = 0

        ; Low bytes
        pop rcx
        push rcx
        mov dx, ATA_SECCOUNT
        mov al, cl
        out dx, al

        pop rcx
        pop rax
        push rcx

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

        pop rcx

.write_loop:
        push rcx
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

        pop rcx
        loop .write_loop

        ; Flush cache once after all sectors written
        mov dx, ATA_CMD
        mov al, ATA_CMD_FLUSH
        out dx, al
        call ata_wait_ready

        POPALL
        clc
        ret

.werror:
        pop rcx
        POPALL
        stc
        ret

.werror_setup:
        pop rcx                 ; Clean up pushed count
        pop rax                 ; Clean up pushed LBA
        POPALL
        stc
        ret

;-----------------------------------------------------------------------
; ATA retry wrappers - retry up to 3 times on error with a soft reset
; between attempts.  Same ABI as ata_read_sectors / ata_write_sectors.
;-----------------------------------------------------------------------

; ata_read_sectors_retry: EAX=LBA, ECX=count, EDI=dest, CF on error
ata_read_sectors:
        ; Try DMA first if available
        cmp byte [ata_dma_capable], 1
        jne .arsr_pio
        call ata_dma_read
        jnc .arsr_ret_ok
.arsr_pio:
        push rcx
        push rax
        push rdi
        mov dword [.arsr_tries], 3
.arsr_loop:
        mov eax, [rsp + 8]      ; LBA
        mov ecx, [rsp + 16]     ; count
        mov edi, [rsp]           ; dest
        call ata_read_sectors_raw
        jnc .arsr_ok
        ; ATA soft reset: pulse SRST then wait
        mov dx, ATA_CONTROL
        mov al, 0x04             ; SRST
        out dx, al
        mov ecx, 5000
.arsr_delay:
        loop .arsr_delay
        xor al, al
        out dx, al
        call ata_wait_ready
        dec dword [.arsr_tries]
        jnz .arsr_loop
        ; All retries failed
        mov esi, msg_ata_read_err
        call vga_print
        pop rdi
        pop rax
        pop rcx
        stc
        ret
.arsr_ok:
        pop rdi
        pop rax
        pop rcx
.arsr_ret_ok:
        clc
        ret
.arsr_tries: dd 0

; ata_write_sectors_retry: EAX=LBA, ECX=count, ESI=src, CF on error
ata_write_sectors:
        ; Try DMA first if available
        cmp byte [ata_dma_capable], 1
        jne .awsr_pio
        call ata_dma_write
        jnc .awsr_ret_ok
.awsr_pio:
        push rcx
        push rax
        push rsi
        mov dword [.awsr_tries], 3
.awsr_loop:
        mov eax, [rsp + 8]      ; LBA
        mov ecx, [rsp + 16]     ; count
        mov esi, [rsp]           ; src
        call ata_write_sectors_raw
        jnc .awsr_ok
        ; ATA soft reset
        mov dx, ATA_CONTROL
        mov al, 0x04
        out dx, al
        mov ecx, 5000
.awsr_delay:
        loop .awsr_delay
        xor al, al
        out dx, al
        call ata_wait_ready
        dec dword [.awsr_tries]
        jnz .awsr_loop
        mov esi, msg_ata_write_err
        call vga_print
        pop rsi
        pop rax
        pop rcx
        stc
        ret
.awsr_ok:
        pop rsi
        pop rax
        pop rcx
.awsr_ret_ok:
        clc
        ret
.awsr_tries: dd 0

;=======================================================================
; ATA DMA SUPPORT
;=======================================================================

;-----------------------------------------------------------------------
; ata_dma_probe — Detect Bus Master IDE via PCI (bus 0, dev 1, fn 1)
;-----------------------------------------------------------------------
ata_dma_probe:
        PUSHALL

        ; Read PCI config: bus 0, device 1, function 1, offset 0x00 (vendor/device)
        ; PCI address = 0x80000000 | (bus << 16) | (dev << 11) | (fn << 8) | (off & 0xFC)
        mov eax, 0x80000900     ; bus=0, dev=1, fn=1, off=0x00
        mov dx, PCI_CONFIG_ADDR
        out dx, eax
        mov dx, PCI_CONFIG_DATA
        in eax, dx

        cmp eax, 0xFFFFFFFF    ; No device
        je .dma_no
        cmp ax, 0              ; Invalid vendor
        je .dma_no

        ; Read class code at offset 0x08 (rev[7:0], progif[15:8], subclass[23:16], class[31:24])
        mov eax, 0x80000908     ; offset 0x08
        mov dx, PCI_CONFIG_ADDR
        out dx, eax
        mov dx, PCI_CONFIG_DATA
        in eax, dx

        ; Check class=0x01 (mass storage), subclass=0x01 (IDE)
        shr eax, 16
        cmp al, 0x01            ; subclass = IDE
        jne .dma_no
        shr eax, 8
        cmp al, 0x01            ; class = mass storage
        jne .dma_no

        ; Read BAR4 at offset 0x20 — Bus Master IDE base address
        mov eax, 0x80000920     ; offset 0x20
        mov dx, PCI_CONFIG_ADDR
        out dx, eax
        mov dx, PCI_CONFIG_DATA
        in eax, dx

        test eax, 1             ; Bit 0 = I/O space indicator
        jz .dma_no
        and eax, 0xFFFC         ; Mask off lower 2 bits
        cmp eax, 0
        je .dma_no
        mov [ata_bm_base], ax

        ; Enable bus mastering in PCI command register (offset 0x04)
        mov eax, 0x80000904     ; offset 0x04
        mov dx, PCI_CONFIG_ADDR
        out dx, eax
        mov dx, PCI_CONFIG_DATA
        in eax, dx
        or eax, 0x04            ; Set Bus Master bit
        push rax
        mov eax, 0x80000904
        mov dx, PCI_CONFIG_ADDR
        out dx, eax
        pop rax
        mov dx, PCI_CONFIG_DATA
        out dx, eax

        mov byte [ata_dma_capable], 1
        mov esi, msg_ata_dma
        call vga_print
        POPALL
        ret

.dma_no:
        POPALL
        ret

;-----------------------------------------------------------------------
; ata_dma_read — Read sectors using DMA
; EAX = LBA, ECX = sector count (max 128), EDI = destination buffer
; Returns: CF set on error
;-----------------------------------------------------------------------
ata_dma_read:
        PUSHALL

        ; Build PRD table: one entry pointing to destination buffer
        ; PRD entry: [base_addr(4)] [byte_count(2)] [0x8000=EOT(2)]
        mov [ata_prd_table], edi            ; physical address (identity mapped)
        mov edx, ecx
        shl edx, 9                          ; sector_count * 512 = byte count
        test edx, edx
        jz .dma_rd_zeros
        mov [ata_prd_table + 4], dx         ; byte count (0 means 64KB)
        jmp .dma_rd_prd_ok
.dma_rd_zeros:
        mov word [ata_prd_table + 4], 0     ; 0 = 64KB
.dma_rd_prd_ok:
        mov word [ata_prd_table + 6], 0x8000 ; EOT flag

        ; Stop any prior DMA
        movzx edx, word [ata_bm_base]
        add edx, BM_CMD_REG
        mov al, 0
        out dx, al

        ; Clear status (write 1 to clear IRQ and error bits)
        movzx edx, word [ata_bm_base]
        add edx, BM_STATUS_REG
        mov al, BM_STATUS_IRQ | BM_STATUS_ERR
        out dx, al

        ; Set PRD table address
        movzx edx, word [ata_bm_base]
        add edx, BM_PRD_REG
        mov eax, ata_prd_table
        out dx, eax

        ; Setup ATA command (LBA48 READ DMA EXT)
        mov eax, [rsp + FRAME_RAX]             ; original EAX (LBA) from PUSHALL
        mov ecx, [rsp + FRAME_RCX]      ; original ECX (count) from PUSHALL

        push rax
        push rcx

        mov dx, ATA_DRIVE
        mov al, 0x40
        out dx, al
        call ata_wait_ready
        jc .dma_rd_fail

        ; High bytes
        pop rcx
        push rcx
        mov dx, ATA_SECCOUNT
        mov al, ch
        out dx, al
        mov eax, [rsp + 8]             ; saved LBA
        shr eax, 24
        mov dx, ATA_LBA_LO
        out dx, al
        xor al, al
        mov dx, ATA_LBA_MID
        out dx, al
        mov dx, ATA_LBA_HI
        out dx, al

        ; Low bytes
        pop rcx
        push rcx
        mov dx, ATA_SECCOUNT
        mov al, cl
        out dx, al
        pop rcx
        pop rax
        mov dx, ATA_LBA_LO
        out dx, al
        shr eax, 8
        mov dx, ATA_LBA_MID
        out dx, al
        shr eax, 8
        mov dx, ATA_LBA_HI
        out dx, al

        ; Send READ DMA EXT command
        mov dx, ATA_CMD
        mov al, ATA_CMD_READ_DMA
        out dx, al

        ; Start DMA (direction = read = device-to-memory, bit 3 = 1)
        movzx edx, word [ata_bm_base]
        add edx, BM_CMD_REG
        mov al, BM_CMD_START | BM_CMD_WRITE
        out dx, al

        ; Poll for completion
        mov ecx, 0x200000               ; timeout
.dma_rd_poll:
        movzx edx, word [ata_bm_base]
        add edx, BM_STATUS_REG
        in al, dx
        test al, BM_STATUS_ERR
        jnz .dma_rd_err
        test al, BM_STATUS_IRQ
        jnz .dma_rd_ok
        loop .dma_rd_poll
        jmp .dma_rd_err

.dma_rd_ok:
        ; Stop DMA
        movzx edx, word [ata_bm_base]
        add edx, BM_CMD_REG
        mov al, 0
        out dx, al
        ; Clear status
        movzx edx, word [ata_bm_base]
        add edx, BM_STATUS_REG
        mov al, BM_STATUS_IRQ | BM_STATUS_ERR
        out dx, al
        POPALL
        clc
        ret

.dma_rd_fail:
        pop rcx
        pop rax
.dma_rd_err:
        ; Stop DMA
        movzx edx, word [ata_bm_base]
        add edx, BM_CMD_REG
        mov al, 0
        out dx, al
        movzx edx, word [ata_bm_base]
        add edx, BM_STATUS_REG
        mov al, BM_STATUS_IRQ | BM_STATUS_ERR
        out dx, al
        POPALL
        stc
        ret

;-----------------------------------------------------------------------
; ata_dma_write — Write sectors using DMA
; EAX = LBA, ECX = sector count, ESI = source buffer
; Returns: CF set on error
;-----------------------------------------------------------------------
ata_dma_write:
        PUSHALL

        ; Build PRD table
        mov [ata_prd_table], esi
        mov edx, ecx
        shl edx, 9
        test edx, edx
        jz .dma_wr_zeros
        mov [ata_prd_table + 4], dx
        jmp .dma_wr_prd_ok
.dma_wr_zeros:
        mov word [ata_prd_table + 4], 0
.dma_wr_prd_ok:
        mov word [ata_prd_table + 6], 0x8000

        ; Stop prior DMA, clear status
        movzx edx, word [ata_bm_base]
        add edx, BM_CMD_REG
        mov al, 0
        out dx, al
        movzx edx, word [ata_bm_base]
        add edx, BM_STATUS_REG
        mov al, BM_STATUS_IRQ | BM_STATUS_ERR
        out dx, al

        ; Set PRD table
        movzx edx, word [ata_bm_base]
        add edx, BM_PRD_REG
        mov eax, ata_prd_table
        out dx, eax

        ; Setup ATA command
        mov eax, [rsp + FRAME_RAX]
        mov ecx, [rsp + FRAME_RCX]

        push rax
        push rcx

        mov dx, ATA_DRIVE
        mov al, 0x40
        out dx, al
        call ata_wait_ready
        jc .dma_wr_fail

        pop rcx
        push rcx
        mov dx, ATA_SECCOUNT
        mov al, ch
        out dx, al
        mov eax, [rsp + 8]
        shr eax, 24
        mov dx, ATA_LBA_LO
        out dx, al
        xor al, al
        mov dx, ATA_LBA_MID
        out dx, al
        mov dx, ATA_LBA_HI
        out dx, al

        pop rcx
        push rcx
        mov dx, ATA_SECCOUNT
        mov al, cl
        out dx, al
        pop rcx
        pop rax
        mov dx, ATA_LBA_LO
        out dx, al
        shr eax, 8
        mov dx, ATA_LBA_MID
        out dx, al
        shr eax, 8
        mov dx, ATA_LBA_HI
        out dx, al

        ; Send WRITE DMA EXT
        mov dx, ATA_CMD
        mov al, ATA_CMD_WRITE_DMA
        out dx, al

        ; Start DMA (direction = write = memory-to-device, bit 3 = 0)
        movzx edx, word [ata_bm_base]
        add edx, BM_CMD_REG
        mov al, BM_CMD_START
        out dx, al

        ; Poll for completion
        mov ecx, 0x200000
.dma_wr_poll:
        movzx edx, word [ata_bm_base]
        add edx, BM_STATUS_REG
        in al, dx
        test al, BM_STATUS_ERR
        jnz .dma_wr_err
        test al, BM_STATUS_IRQ
        jnz .dma_wr_ok
        loop .dma_wr_poll
        jmp .dma_wr_err

.dma_wr_ok:
        movzx edx, word [ata_bm_base]
        add edx, BM_CMD_REG
        mov al, 0
        out dx, al
        movzx edx, word [ata_bm_base]
        add edx, BM_STATUS_REG
        mov al, BM_STATUS_IRQ | BM_STATUS_ERR
        out dx, al
        ; Flush cache
        mov dx, ATA_CMD
        mov al, ATA_CMD_FLUSH
        out dx, al
        call ata_wait_ready
        POPALL
        clc
        ret

.dma_wr_fail:
        pop rcx
        pop rax
.dma_wr_err:
        movzx edx, word [ata_bm_base]
        add edx, BM_CMD_REG
        mov al, 0
        out dx, al
        movzx edx, word [ata_bm_base]
        add edx, BM_STATUS_REG
        mov al, BM_STATUS_IRQ | BM_STATUS_ERR
        out dx, al
        POPALL
        stc
        ret
