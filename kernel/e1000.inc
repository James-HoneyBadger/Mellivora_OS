;=======================================================================
; e1000.inc - Intel 82540EM (e1000) NIC Driver
;
; Detects the first Intel e1000-family NIC via pci_dev_table.
; If net_state is already NET_STATE_UP (RTL8139 found), does nothing.
;
; Supported PCI IDs (Intel 8254x family / QEMU defaults):
;   8086:100E  82540EM (QEMU default)
;   8086:100F  82545EM
;   8086:1076  82541PI
;   8086:1011  82545EM copper
;
; On detection, fills in net_mac, sets net_state=NET_STATE_UP,
; net_nic_type=2 (NIC_E1000), and stores MMIO base in e1000_mmio.
;
; TX ring: 8 descriptors, 8 × 2048-byte buffers (polling, no IRQ)
; RX ring: 8 descriptors, 8 × 2048-byte buffers
;=======================================================================

; NIC type value stored in net_nic_type
NIC_TYPE_NONE   equ 0
NIC_TYPE_RTL    equ 1
NIC_TYPE_E1000  equ 2

; Supported e1000 PCI device IDs
E1000_100E      equ 0x100E      ; 82540EM (QEMU)
E1000_100F      equ 0x100F      ; 82545EM
E1000_1076      equ 0x1076      ; 82541PI
E1000_1011      equ 0x1011      ; 82545EM copper
E1000_VENDOR    equ 0x8086      ; Intel

; MMIO register offsets
E1000_CTRL      equ 0x0000
E1000_STATUS    equ 0x0008
E1000_EERD      equ 0x0014
E1000_ICR       equ 0x00C0
E1000_IMS       equ 0x00D0
E1000_IMC       equ 0x00D8
E1000_RCTL      equ 0x0100
E1000_TCTL      equ 0x0400
E1000_TIPG      equ 0x0410
E1000_RDBAL     equ 0x2800
E1000_RDBAH     equ 0x2804
E1000_RDLEN     equ 0x2808
E1000_RDH       equ 0x2810
E1000_RDT       equ 0x2818
E1000_TDBAL     equ 0x3800
E1000_TDBAH     equ 0x3804
E1000_TDLEN     equ 0x3808
E1000_TDH       equ 0x3810
E1000_TDT       equ 0x3818
E1000_MTA       equ 0x5200      ; Multicast Table Array (128 dwords)
E1000_RAL0      equ 0x5400      ; Receive Address Low
E1000_RAH0      equ 0x5404      ; Receive Address High (bit 31 = valid)

; CTRL register bits
E1000_CTRL_SLU  equ (1<<6)      ; Set Link Up (force link)
E1000_CTRL_RST  equ (1<<26)     ; Device Reset

; RCTL register bits
E1000_RCTL_EN   equ (1<<1)      ; Receive Enable
E1000_RCTL_SBP  equ (1<<2)      ; Store Bad Packets
E1000_RCTL_UPE  equ (1<<3)      ; Unicast Promisc
E1000_RCTL_MPE  equ (1<<4)      ; Multicast Promisc
E1000_RCTL_BAM  equ (1<<15)     ; Broadcast Accept Mode
E1000_RCTL_SECRC equ (1<<26)    ; Strip Ethernet CRC

; TCTL register bits
E1000_TCTL_EN   equ (1<<1)      ; Transmit Enable
E1000_TCTL_PSP  equ (1<<3)      ; Pad Short Packets

; TX descriptor CMD byte
E1000_TX_EOP    equ (1<<0)      ; End of Packet
E1000_TX_IFCS   equ (1<<1)      ; Insert FCS
E1000_TX_RS     equ (1<<3)      ; Report Status

; TX/RX descriptor status
E1000_STA_DD    equ (1<<0)      ; Descriptor Done

; Descriptor and buffer counts
E1000_TX_COUNT  equ 8
E1000_RX_COUNT  equ 8
E1000_BUF_SIZE  equ 2048

;=======================================================================
; e1000_init - Detect and initialize Intel e1000 NIC
;=======================================================================
e1000_init:
        pushad

        ; Skip if another NIC is already up
        cmp dword [net_state], NET_STATE_UP
        je  .skip

        ; Search pci_dev_table for Intel e1000 variant
        ; pci_dev_table entry: +0=vendor +2=device +4=bus +5=dev +6=fn
        ;                      +7=class +8=subclass +9=hdr
        mov esi, pci_dev_table
        xor ecx, ecx
.scan:
        cmp ecx, [pci_dev_count]
        jge .not_found

        movzx eax, word [esi + 0]       ; vendor
        cmp ax, E1000_VENDOR
        jne .scan_next

        movzx eax, word [esi + 2]       ; device
        cmp ax, E1000_100E
        je  .found_dev
        cmp ax, E1000_100F
        je  .found_dev
        cmp ax, E1000_1076
        je  .found_dev
        cmp ax, E1000_1011
        je  .found_dev
        jmp .scan_next

.found_dev:
        ; Pack BDF
        movzx eax, byte [esi + 4]
        shl  eax, 16
        movzx edx, byte [esi + 5]
        shl  edx, 8
        or   eax, edx
        movzx edx, byte [esi + 6]
        or   eax, edx
        mov  [e1000_bdf], eax

        ; Read BAR0 (MMIO base)
        xor ebx, ebx            ; bar index 0
        call pci_read_bar
        test eax, eax
        jz  .scan_next
        and eax, 0xFFFFF000
        test eax, eax
        jz  .scan_next
        mov [e1000_mmio], eax
        jmp .configure

.scan_next:
        add esi, PCI_ENTRY_SIZE
        inc ecx
        jmp .scan

.not_found:
        mov esi, msg_e1000_none
        call vga_print
        jmp .skip

.configure:
        ; Enable PCI bus mastering + memory space
        mov eax, [e1000_bdf]
        mov ebx, 0x04
        call pci_read_config32
        or  eax, 0x06
        mov ecx, eax
        mov eax, [e1000_bdf]
        mov ebx, 0x04
        call pci_write_config32

        ; Reset the device
        mov ebx, [e1000_mmio]
        mov eax, E1000_CTRL_RST
        mov [ebx + E1000_CTRL], eax
        ; Delay — spin ~1M iterations to let reset complete
        mov ecx, 1000000
.rst_delay:
        loop .rst_delay
        ; Wait for reset bit to clear
        mov ecx, 1000000
.rst_wait:
        mov eax, [ebx + E1000_CTRL]
        test eax, E1000_CTRL_RST
        jz  .rst_done
        loop .rst_wait
.rst_done:

        ; Set link up (force)
        mov eax, [ebx + E1000_CTRL]
        or  eax, E1000_CTRL_SLU
        mov [ebx + E1000_CTRL], eax

        ; Disable all interrupts
        mov dword [ebx + E1000_IMC], 0xFFFFFFFF

        ; Clear pending interrupts
        mov eax, [ebx + E1000_ICR]

        ; Read MAC from RAL0/RAH0
        mov eax, [ebx + E1000_RAH0]
        test eax, (1<<31)               ; valid bit
        jnz .mac_from_ra

        ; Fall back to EEPROM reads for MAC bytes
        call e1000_read_eeprom_mac
        jmp .mac_done

.mac_from_ra:
        mov eax, [ebx + E1000_RAL0]
        mov [net_mac + 0], al
        shr eax, 8
        mov [net_mac + 1], al
        shr eax, 8
        mov [net_mac + 2], al
        shr eax, 8
        mov [net_mac + 3], al
        mov eax, [ebx + E1000_RAH0]
        mov [net_mac + 4], al
        shr eax, 8
        mov [net_mac + 5], al

.mac_done:
        ; Zero Multicast Table Array (128 × 4 bytes)
        mov ecx, 128
        xor eax, eax
        mov edi, ebx
        add edi, E1000_MTA
.mta_loop:
        mov [edi], eax
        add edi, 4
        loop .mta_loop

        ; Initialize TX ring
        call e1000_tx_init

        ; Initialize RX ring
        call e1000_rx_init

        ; Mark NIC active
        mov dword [net_state], NET_STATE_UP
        mov byte  [net_nic_type], NIC_TYPE_E1000

        ; Set default IP (same as RTL8139 path — QEMU user-mode network)
        mov dword [net_ip],      0x0F02000A  ; 10.0.2.15
        mov dword [net_mask],    0x00FFFFFF  ; 255.255.255.0
        mov dword [net_gateway], 0x0202000A  ; 10.0.2.2
        mov dword [net_dns],     0x0302000A  ; 10.0.2.3
        mov word  [net_ephemeral_port], 49152

        mov esi, msg_e1000_ok
        call vga_print

.skip:
        popad
        ret

;=======================================================================
; e1000_read_eeprom_mac - Read MAC from EEPROM via EERD register
;=======================================================================
e1000_read_eeprom_mac:
        push eax
        push ebx
        push ecx
        mov ebx, [e1000_mmio]

        ; Word 0 = MAC bytes 0-1
        call .read_word
        mov [net_mac + 0], al
        shr eax, 8
        mov [net_mac + 1], al

        ; Word 1 = MAC bytes 2-3
        mov ecx, 1
        call .read_word
        mov [net_mac + 2], al
        shr eax, 8
        mov [net_mac + 3], al

        ; Word 2 = MAC bytes 4-5
        mov ecx, 2
        call .read_word
        mov [net_mac + 4], al
        shr eax, 8
        mov [net_mac + 5], al

        pop ecx
        pop ebx
        pop eax
        ret

.read_word:
        ; ECX = EEPROM word address, EBX = MMIO base
        ; Returns EAX = 16-bit data
        push ecx
        shl ecx, 8
        or  ecx, 0x01           ; EERD: address<<8 | START
        mov [ebx + E1000_EERD], ecx
        ; Poll DONE bit (bit 4)
        mov ecx, 0x100000
.eerd_poll:
        mov eax, [ebx + E1000_EERD]
        test eax, (1<<4)
        jnz .eerd_done
        loop .eerd_poll
.eerd_done:
        shr eax, 16             ; data in bits 31:16
        and eax, 0xFFFF
        pop ecx
        ret

;=======================================================================
; e1000_tx_init - Set up TX descriptor ring
;=======================================================================
e1000_tx_init:
        push eax
        push ebx
        push ecx
        push edi

        ; Zero all TX descriptors (16 bytes each, 8 entries)
        mov edi, e1000_tx_ring
        mov ecx, (E1000_TX_COUNT * 16) / 4
        xor eax, eax
        rep stosd

        ; Fill in TX buffer addresses
        mov ecx, 0
.tx_setup:
        cmp ecx, E1000_TX_COUNT
        jge .tx_setup_done
        ; TX descriptor at e1000_tx_ring + ecx*16
        ; QWORD 0 = buffer address (64-bit, high = 0)
        mov eax, ecx
        imul eax, E1000_BUF_SIZE
        add  eax, e1000_tx_buf_area     ; physical address of tx buffer
        mov  ebx, ecx
        imul ebx, 16
        mov  [e1000_tx_ring + ebx + 0], eax
        mov  dword [e1000_tx_ring + ebx + 4], 0
        ; Leave length/cmd/status zero — filled per-packet
        inc  ecx
        jmp  .tx_setup

.tx_setup_done:
        ; Program TX ring into NIC
        mov ebx, [e1000_mmio]
        lea eax, [e1000_tx_ring]
        mov [ebx + E1000_TDBAL], eax
        mov dword [ebx + E1000_TDBAH], 0
        mov dword [ebx + E1000_TDLEN], E1000_TX_COUNT * 16
        mov dword [ebx + E1000_TDH], 0
        mov dword [ebx + E1000_TDT], 0
        mov dword [e1000_tx_tail], 0

        ; Enable TX: EN | PSP | CT=0x0F<<4 | COLD=0x040<<12
        mov dword [ebx + E1000_TCTL], 0x00E03F0B
        ; TX inter-packet gap: recommended value for 82540EM
        mov dword [ebx + E1000_TIPG], 0x00702008

        pop edi
        pop ecx
        pop ebx
        pop eax
        ret

;=======================================================================
; e1000_rx_init - Set up RX descriptor ring
;=======================================================================
e1000_rx_init:
        push eax
        push ebx
        push ecx
        push edi

        ; Zero all RX descriptors
        mov edi, e1000_rx_ring
        mov ecx, (E1000_RX_COUNT * 16) / 4
        xor eax, eax
        rep stosd

        ; Fill in RX buffer addresses
        mov ecx, 0
.rx_setup:
        cmp ecx, E1000_RX_COUNT
        jge .rx_setup_done
        mov eax, ecx
        imul eax, E1000_BUF_SIZE
        add  eax, e1000_rx_buf_area
        mov  ebx, ecx
        imul ebx, 16
        mov  [e1000_rx_ring + ebx + 0], eax
        mov  dword [e1000_rx_ring + ebx + 4], 0
        ; Status = 0 (hardware will set DD on receive)
        inc  ecx
        jmp  .rx_setup

.rx_setup_done:
        ; Program RX ring into NIC
        mov ebx, [e1000_mmio]
        lea eax, [e1000_rx_ring]
        mov [ebx + E1000_RDBAL], eax
        mov dword [ebx + E1000_RDBAH], 0
        mov dword [ebx + E1000_RDLEN], E1000_RX_COUNT * 16
        mov dword [ebx + E1000_RDH], 0
        ; Set tail to last descriptor - 1 (give all descriptors to hardware)
        mov dword [ebx + E1000_RDT], E1000_RX_COUNT - 1
        mov dword [e1000_rx_head], 0

        ; Enable RX: EN | BAM | SECRC | BSIZE=2048 (bits 17:16=00)
        mov dword [ebx + E1000_RCTL], 0x04008002  ; EN|BAM|SECRC

        pop edi
        pop ecx
        pop ebx
        pop eax
        ret

;=======================================================================
; e1000_hw_send - Transmit one Ethernet frame
;   ESI = frame data pointer
;   ECX = frame length (bytes)
;   Returns: CF=0 success, CF=1 error
;=======================================================================
e1000_hw_send:
        push eax
        push ebx
        push ecx
        push edx
        push edi

        cmp ecx, E1000_BUF_SIZE
        ja  .fail

        ; Get current TX tail slot
        mov eax, [e1000_tx_tail]
        and eax, (E1000_TX_COUNT - 1)

        ; Copy frame into the TX buffer for this slot
        mov edx, eax
        imul edx, E1000_BUF_SIZE
        add  edx, e1000_tx_buf_area     ; destination buffer address
        push esi
        push ecx
        mov  edi, edx
        cld
        rep  movsb                      ; copy ECX bytes from ESI to EDI
        pop  ecx
        pop  esi

        ; Build TX descriptor
        ; [0-3] buf addr low  (already set in init, but re-confirm)
        mov ebx, eax
        imul ebx, 16
        mov  [e1000_tx_ring + ebx + 0], edx     ; buffer address
        mov  dword [e1000_tx_ring + ebx + 4], 0 ; high = 0

        ; [8-9] length
        mov  [e1000_tx_ring + ebx + 8], cx
        ; [10] CSO = 0
        mov  byte [e1000_tx_ring + ebx + 10], 0
        ; [11] CMD = EOP | IFCS | RS
        mov  byte [e1000_tx_ring + ebx + 11], (E1000_TX_EOP | E1000_TX_IFCS | E1000_TX_RS)
        ; [12] STA = 0 (hardware fills DD)
        mov  byte [e1000_tx_ring + ebx + 12], 0
        ; [13] CSS = 0
        mov  byte [e1000_tx_ring + ebx + 13], 0
        ; [14-15] special = 0
        mov  word [e1000_tx_ring + ebx + 14], 0

        ; Advance tail and write to TDT
        inc  dword [e1000_tx_tail]
        mov  eax, [e1000_tx_tail]
        and  eax, (E1000_TX_COUNT - 1)
        mov  ebx, [e1000_mmio]
        mov  [ebx + E1000_TDT], eax

        ; Poll for DD (descriptor done) — wait for TX to complete
        ; (we reuse the slot we just wrote; tail is now advanced past it)
        mov  eax, [e1000_tx_tail]
        dec  eax
        and  eax, (E1000_TX_COUNT - 1)
        imul eax, 16

        mov  ecx, 0x200000
.poll_dd:
        movzx edx, byte [e1000_tx_ring + eax + 12]
        test dl, E1000_STA_DD
        jnz  .done
        loop .poll_dd

.fail:
        pop  edi
        pop  edx
        pop  ecx
        pop  ebx
        pop  eax
        stc
        ret

.done:
        pop  edi
        pop  edx
        pop  ecx
        pop  ebx
        pop  eax
        clc
        ret

;=======================================================================
; e1000_poll_rx - Check RX ring for received frames, process each one.
; Call periodically from the scheduler tick or RX handler.
;=======================================================================
e1000_poll_rx:
        cmp byte [net_nic_type], NIC_TYPE_E1000
        jne .done

        push eax
        push ebx
        push ecx
        push esi
        push edi

        mov ebx, [e1000_mmio]

.rx_loop:
        mov eax, [e1000_rx_head]
        and eax, (E1000_RX_COUNT - 1)
        imul ecx, eax, 16

        ; Check DD bit in descriptor status
        movzx edx, byte [e1000_rx_ring + ecx + 12]
        test dl, E1000_STA_DD
        jz  .rx_no_more

        ; Get packet length
        movzx ecx, word [e1000_rx_ring + ecx + 8]

        ; Point ESI at the receive buffer for this slot
        mov esi, eax
        imul esi, E1000_BUF_SIZE
        add  esi, e1000_rx_buf_area

        ; Copy into net_pkt_buf for processing
        cmp ecx, 1536
        jg  .rx_skip_frame
        push ecx
        mov  edi, [net_pkt_buf]
        test edi, edi
        jz  .rx_skip_frame_pop
        cld
        rep  movsb
        pop  ecx

        ; Hand off to Ethernet layer
        mov  esi, [net_pkt_buf]
        call net_process_frame

        jmp .rx_recycle

.rx_skip_frame_pop:
        pop ecx
.rx_skip_frame:

.rx_recycle:
        ; Clear DD and give descriptor back to hardware
        mov  eax, [e1000_rx_head]
        and  eax, (E1000_RX_COUNT - 1)
        imul eax, 16
        mov  byte [e1000_rx_ring + eax + 12], 0

        ; Advance RDT (give this slot back to hardware)
        mov  eax, [e1000_rx_head]
        and  eax, (E1000_RX_COUNT - 1)
        mov  [ebx + E1000_RDT], eax

        ; Advance head
        inc  dword [e1000_rx_head]

        jmp .rx_loop

.rx_no_more:
        pop edi
        pop esi
        pop ecx
        pop ebx
        pop eax
.done:
        ret

;-----------------------------------------------------------------------
; BSS data
;-----------------------------------------------------------------------
e1000_mmio:         dd 0
e1000_bdf:          dd 0
e1000_tx_tail:      dd 0
e1000_rx_head:      dd 0

; TX and RX descriptor rings (128-byte aligned, 16 bytes per entry)
align 128
e1000_tx_ring:      times E1000_TX_COUNT * 16 db 0

align 128
e1000_rx_ring:      times E1000_RX_COUNT * 16 db 0

; TX packet buffers (E1000_TX_COUNT * 2048 = 16KB)
align 4096
e1000_tx_buf_area:  times E1000_TX_COUNT * E1000_BUF_SIZE db 0

; RX packet buffers (E1000_RX_COUNT * 2048 = 16KB)
align 4096
e1000_rx_buf_area:  times E1000_RX_COUNT * E1000_BUF_SIZE db 0

msg_e1000_none:     db "e1000: not found", 0x0A, 0
msg_e1000_ok:       db "e1000: NIC ready", 0x0A, 0
