;=======================================================================
; ATA PIO DISK DRIVER (LBA48 - supports up to 128 PB)
;=======================================================================

ata_init:
        pushad
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

        ; Send IDENTIFY command
        mov dx, ATA_CMD
        mov al, ATA_CMD_IDENTIFY
        out dx, al

        ; Wait for BSY to clear
        call ata_wait_ready
        jc .no_drive

        ; Check if drive exists
        mov dx, ATA_STATUS
        in al, dx
        test al, al
        jz .no_drive

        ; Read 256 words of identify data
        mov dx, ATA_STATUS
.wait_drq:
        in al, dx
        test al, ATA_SR_ERR
        jnz .no_drive
        test al, ATA_SR_DRQ
        jz .wait_drq

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

        popad
        ret

.no_drive:
        mov byte [ata_present], 0
        mov esi, msg_ata_none
        call vga_print
        popad
        ret

; Wait for ATA drive ready
; Returns: CF set on timeout
ata_wait_ready:
        push ecx
        mov ecx, 0x100000       ; Timeout counter
.loop:
        mov dx, ATA_STATUS
        in al, dx
        test al, ATA_SR_BSY
        jz .ready
        loop .loop
        stc                     ; Timeout
        pop ecx
        ret
.ready:
        clc
        pop ecx
        ret

; Read sectors using LBA48 (raw, no retry)
; EAX = LBA low 32 bits, ECX = sector count (1-256), EDI = destination buffer
; Returns: CF set on error
ata_read_sectors_raw:
        pushad

        push eax                ; Save LBA
        push ecx                ; Save count

        ; Select drive (master, LBA mode) and wait ready
        mov dx, ATA_DRIVE
        mov al, 0x40            ; Master, LBA
        out dx, al
        call ata_wait_ready
        jc .error_setup         ; Drive not ready

        ; Send high bytes first (LBA48 requires two writes)
        ; Sector count high byte
        pop ecx
        push ecx
        mov dx, ATA_SECCOUNT
        mov al, ch              ; High byte of sector count (LBA48 16-bit)
        out dx, al

        ; LBA48 high bytes: byte 3 = EAX[24:31], bytes 4-5 = 0
        mov eax, [esp + 4]      ; get saved LBA from stack
        shr eax, 24             ; extract bits 24-31
        mov dx, ATA_LBA_LO
        out dx, al              ; LBA byte 3
        xor al, al
        mov dx, ATA_LBA_MID
        out dx, al              ; LBA byte 4 = 0
        mov dx, ATA_LBA_HI
        out dx, al              ; LBA byte 5 = 0

        ; Now send low bytes
        pop ecx
        push ecx
        mov dx, ATA_SECCOUNT
        mov al, cl              ; Sector count low
        out dx, al

        ; LBA bytes 0-2
        pop ecx
        pop eax                 ; Restore LBA

        push ecx                ; Save count again
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

        pop ecx                 ; Sector count

.read_loop:
        push ecx
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

        pop ecx
        loop .read_loop

        popad
        clc
        ret

.error:
        pop ecx
        popad
        stc
        ret

.error_setup:
        pop ecx                 ; Clean up pushed count
        pop eax                 ; Clean up pushed LBA
        popad
        stc
        ret

; Write sectors using LBA48 (raw, no retry)
; EAX = LBA low 32 bits, ECX = sector count, ESI = source buffer
; Returns: CF set on error
ata_write_sectors_raw:
        pushad

        push eax
        push ecx

        ; Select drive and wait for it to be ready
        mov dx, ATA_DRIVE
        mov al, 0x40
        out dx, al
        call ata_wait_ready
        jc .werror_setup        ; Drive not ready

        ; High bytes - sector count high byte
        pop ecx
        push ecx
        mov dx, ATA_SECCOUNT
        mov al, ch              ; High byte of sector count (LBA48)
        out dx, al
        ; LBA48 high bytes: byte 3 = EAX[24:31], bytes 4-5 = 0
        mov eax, [esp + 4]      ; get saved LBA from stack
        shr eax, 24             ; extract bits 24-31
        mov dx, ATA_LBA_LO
        out dx, al              ; LBA byte 3
        xor al, al
        mov dx, ATA_LBA_MID
        out dx, al              ; LBA byte 4 = 0
        mov dx, ATA_LBA_HI
        out dx, al              ; LBA byte 5 = 0

        ; Low bytes
        pop ecx
        push ecx
        mov dx, ATA_SECCOUNT
        mov al, cl
        out dx, al

        pop ecx
        pop eax
        push ecx

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

        pop ecx

.write_loop:
        push ecx
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

        pop ecx
        loop .write_loop

        ; Flush cache once after all sectors written
        mov dx, ATA_CMD
        mov al, ATA_CMD_FLUSH
        out dx, al
        call ata_wait_ready

        popad
        clc
        ret

.werror:
        pop ecx
        popad
        stc
        ret

.werror_setup:
        pop ecx                 ; Clean up pushed count
        pop eax                 ; Clean up pushed LBA
        popad
        stc
        ret

;-----------------------------------------------------------------------
; ATA retry wrappers - retry up to 3 times on error with a soft reset
; between attempts.  Same ABI as ata_read_sectors / ata_write_sectors.
;-----------------------------------------------------------------------

; ata_read_sectors_retry: EAX=LBA, ECX=count, EDI=dest, CF on error
ata_read_sectors:
        push ecx
        push eax
        push edi
        mov dword [.arsr_tries], 3
.arsr_loop:
        mov eax, [esp + 4]      ; LBA
        mov ecx, [esp + 8]      ; count
        mov edi, [esp]           ; dest
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
        pop edi
        pop eax
        pop ecx
        stc
        ret
.arsr_ok:
        pop edi
        pop eax
        pop ecx
        clc
        ret
.arsr_tries: dd 0

; ata_write_sectors_retry: EAX=LBA, ECX=count, ESI=src, CF on error
ata_write_sectors:
        push ecx
        push eax
        push esi
        mov dword [.awsr_tries], 3
.awsr_loop:
        mov eax, [esp + 4]      ; LBA
        mov ecx, [esp + 8]      ; count
        mov esi, [esp]           ; src
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
        pop esi
        pop eax
        pop ecx
        stc
        ret
.awsr_ok:
        pop esi
        pop eax
        pop ecx
        clc
        ret
.awsr_tries: dd 0
