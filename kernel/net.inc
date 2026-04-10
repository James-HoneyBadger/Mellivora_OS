;=======================================================================
; net.inc - Networking stack stub (RTL8139 NIC driver)
;
; Provides PCI bus scanning, RTL8139 detection & basic initialization,
; TX/RX buffer setup, and a "net" shell command for status display.
; This is the foundation for future TCP/IP support.
;=======================================================================

; PCI Configuration Space I/O ports
PCI_CONFIG_ADDR equ 0xCF8
PCI_CONFIG_DATA equ 0xCFC

; RTL8139 identification
RTL8139_VENDOR  equ 0x10EC
RTL8139_DEVICE  equ 0x8139

; RTL8139 register offsets (from I/O base)
RTL_MAC0        equ 0x00       ; MAC address bytes 0-3
RTL_MAC4        equ 0x04       ; MAC address bytes 4-5
RTL_TXSTATUS0   equ 0x10       ; TX status (4 descriptors: +0, +4, +8, +C)
RTL_TXADDR0     equ 0x20       ; TX buffer address (4 descriptors)
RTL_RXBUF       equ 0x30       ; RX buffer physical address
RTL_CMD         equ 0x37       ; Command register
RTL_CAPR        equ 0x38       ; Current address of packet read
RTL_IMR         equ 0x3C       ; Interrupt mask register
RTL_ISR         equ 0x3E       ; Interrupt status register
RTL_TXCONFIG    equ 0x40       ; TX configuration
RTL_RXCONFIG    equ 0x44       ; RX configuration
RTL_CONFIG1     equ 0x52       ; Configuration register 1

; RTL8139 command bits
RTL_CMD_RESET   equ 0x10
RTL_CMD_RX_EN   equ 0x08
RTL_CMD_TX_EN   equ 0x04

; RX configuration: accept broadcast, multicast, physical match, all
RTL_RX_ACCEPT   equ 0x0F       ; AB + AM + APM + AAP
RTL_RX_WRAP     equ 0x80       ; Wrap bit (bit 7)
RTL_RX_CONFIG   equ (RTL_RX_WRAP | RTL_RX_ACCEPT)

; Buffer sizes
NET_RX_BUF_SIZE equ 8192 + 16  ; 8K + 16 wrap padding
NET_TX_BUF_SIZE equ 1536       ; Max Ethernet frame

; Network state
NET_STATE_NONE  equ 0          ; No NIC detected
NET_STATE_FOUND equ 1          ; NIC found but not initialized
NET_STATE_UP    equ 2          ; NIC initialized and ready

;---------------------------------------
; net_init - Scan PCI bus for RTL8139 NIC
; Called during kernel initialization.
; Sets net_state and stores NIC info.
;---------------------------------------
net_init:
        pushad
        mov dword [net_state], NET_STATE_NONE

        ; Scan PCI bus 0, all 32 devices, all 8 functions
        xor ebx, ebx               ; bus = 0
.scan_dev:
        xor ecx, ecx               ; device = 0
.scan_dev_loop:
        cmp ecx, 32
        jge .scan_done
        xor edx, edx               ; function = 0
.scan_func:
        cmp edx, 8
        jge .next_dev

        ; Read vendor/device ID (PCI config register 0)
        call pci_read_config_word0
        cmp eax, 0xFFFFFFFF
        je .next_func               ; No device present
        cmp eax, 0x0000FFFF
        je .next_func

        ; EAX = (device_id << 16) | vendor_id
        cmp ax, RTL8139_VENDOR      ; Check vendor ID (low 16 bits)
        jne .next_func
        shr eax, 16
        cmp ax, RTL8139_DEVICE      ; Check device ID (high 16 bits)
        jne .next_func

        ; Found RTL8139! Save PCI location
        mov [net_pci_bus], bl
        mov [net_pci_dev], cl
        mov [net_pci_func], dl

        ; Read BAR0 (I/O base address) — PCI config register 4 (offset 0x10)
        push edx
        mov eax, edx
        push ecx
        push ebx
        ; Build config address for register 0x10
        mov eax, 0x80000000
        or eax, ebx                 ; bus (already in low bits since bus=0)
        shl eax, 8                  ; shift bus
        mov al, 0
        ; Rebuild: bus<<16 | dev<<11 | func<<8 | reg
        mov eax, 0x80000000
        movzx esi, bl               ; bus
        shl esi, 16
        or eax, esi
        movzx esi, cl               ; device
        shl esi, 11
        or eax, esi
        pop ebx
        pop ecx
        pop edx
        movzx esi, dl               ; function
        shl esi, 8
        or eax, esi
        or eax, 0x10                ; register offset 0x10 = BAR0

        mov dx, PCI_CONFIG_ADDR     ; (save and restore EDX)
        push edx
        out dx, eax
        mov dx, PCI_CONFIG_DATA
        in eax, dx
        pop edx                     ; restore func in EDX

        ; BAR0: if bit 0 set, it's I/O space — mask low 2 bits
        and eax, 0xFFFFFFFC
        mov [net_io_base], eax

        ; Read MAC address
        call net_read_mac

        mov dword [net_state], NET_STATE_FOUND
        jmp .scan_done

.next_func:
        inc edx
        jmp .scan_func
.next_dev:
        inc ecx
        jmp .scan_dev_loop
.scan_done:
        ; If NIC found, attempt initialization
        cmp dword [net_state], NET_STATE_FOUND
        jne .init_done
        call net_rtl8139_init
.init_done:
        popad
        ret

;---------------------------------------
; pci_read_config_word0 - Read PCI config register 0
;  EBX = bus (low 8 bits)
;  ECX = device (low 5 bits)
;  EDX = function (low 3 bits)
; Returns: EAX = dword from config register 0
;---------------------------------------
pci_read_config_word0:
        push edx
        ; Build PCI config address: 0x80000000 | bus<<16 | dev<<11 | func<<8 | reg=0
        mov eax, 0x80000000
        movzx esi, bl
        shl esi, 16
        or eax, esi
        movzx esi, cl
        shl esi, 11
        or eax, esi
        movzx esi, dl
        shl esi, 8
        or eax, esi
        ; register offset = 0 (vendor/device ID)

        mov dx, PCI_CONFIG_ADDR
        out dx, eax
        mov dx, PCI_CONFIG_DATA
        in eax, dx
        pop edx
        ret

;---------------------------------------
; net_read_mac - Read 6-byte MAC address from NIC
; Uses net_io_base
;---------------------------------------
net_read_mac:
        pushad
        mov edx, [net_io_base]
        ; Read bytes 0-3
        in eax, dx
        mov [net_mac], eax
        ; Read bytes 4-5
        add edx, 4
        in eax, dx
        mov [net_mac + 4], ax
        popad
        ret

;---------------------------------------
; net_rtl8139_init - Initialize RTL8139 NIC
; Performs software reset, sets up TX/RX,
; enables the receiver and transmitter.
;---------------------------------------
net_rtl8139_init:
        pushad
        mov ebx, [net_io_base]

        ; 1. Power on: write 0x00 to CONFIG1
        lea edx, [ebx + RTL_CONFIG1]
        xor al, al
        out dx, al

        ; 2. Software reset
        lea edx, [ebx + RTL_CMD]
        mov al, RTL_CMD_RESET
        out dx, al

        ; Wait for reset to complete (bit 4 clears)
        mov ecx, 100000
.wait_reset:
        in al, dx
        test al, RTL_CMD_RESET
        jz .reset_done
        dec ecx
        jnz .wait_reset
        ; Timeout — mark as failed
        jmp .init_fail
.reset_done:

        ; 3. Allocate RX buffer (8K+16 from PMM — 3 pages)
        mov ecx, 3
        call pmm_alloc_pages
        test eax, eax
        jz .init_fail
        mov [net_rx_buf], eax

        ; Tell NIC where RX buffer is
        mov ecx, eax
        lea edx, [ebx + RTL_RXBUF]
        mov eax, ecx
        out dx, eax

        ; 4. Allocate TX buffer (1 page = 4KB, enough for 1 frame)
        call pmm_alloc_page
        test eax, eax
        jz .init_fail
        mov [net_tx_buf], eax

        ; 5. Set IMR — enable RX OK and TX OK interrupts (for future use)
        lea edx, [ebx + RTL_IMR]
        mov ax, 0x0005              ; ROK | TOK
        out dx, ax

        ; 6. Configure RX: accept broadcast + physical match, wrap
        lea edx, [ebx + RTL_RXCONFIG]
        mov eax, RTL_RX_CONFIG
        out dx, eax

        ; 7. Enable receiver and transmitter
        lea edx, [ebx + RTL_CMD]
        mov al, RTL_CMD_RX_EN | RTL_CMD_TX_EN
        out dx, al

        ; Re-read MAC after init (some NICs need this)
        call net_read_mac

        mov dword [net_state], NET_STATE_UP
        mov dword [net_tx_desc], 0    ; Start with TX descriptor 0
        popad
        ret

.init_fail:
        popad
        ret

;---------------------------------------
; net_send_frame - Transmit an Ethernet frame
;  ESI = pointer to frame data
;  ECX = frame length in bytes
; Returns: CF=0 success, CF=1 failure
;---------------------------------------
net_send_frame:
        pushad
        cmp dword [net_state], NET_STATE_UP
        jne .send_fail

        cmp ecx, NET_TX_BUF_SIZE
        ja .send_fail

        ; Copy frame data to TX buffer
        mov edi, [net_tx_buf]
        push ecx
        rep movsb
        pop ecx

        ; Get current TX descriptor (0-3, round-robin)
        mov eax, [net_tx_desc]
        and eax, 3

        ; Write TX buffer address to TxAddr[n]
        mov ebx, [net_io_base]
        lea edx, [ebx + RTL_TXADDR0]
        shl eax, 2                   ; descriptor * 4
        add edx, eax
        mov eax, [net_tx_buf]
        out dx, eax

        ; Write TX status (size | OWN bit clear) to TxStatus[n]
        mov eax, [net_tx_desc]
        and eax, 3
        shl eax, 2
        lea edx, [ebx + RTL_TXSTATUS0]
        add edx, eax
        mov eax, ecx                 ; frame size
        ; Bit 13=OWN is already 0 (we own it), bits 0-12 = size
        out dx, eax

        ; Advance to next descriptor
        inc dword [net_tx_desc]

        popad
        clc
        ret
.send_fail:
        popad
        stc
        ret

;---------------------------------------
; cmd_net_status - "net" shell command
; Displays NIC status and MAC address
;---------------------------------------
cmd_net_status:
        pushad

        mov byte [vga_color], COLOR_HEADER
        mov esi, msg_net_header
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT

        ; Show state
        mov esi, msg_net_state
        call vga_print
        mov eax, [net_state]
        cmp eax, NET_STATE_UP
        je .state_up
        cmp eax, NET_STATE_FOUND
        je .state_found
        mov esi, msg_net_none
        call vga_print
        jmp .net_end
.state_found:
        mov esi, msg_net_found
        call vga_print
        jmp .show_details
.state_up:
        mov esi, msg_net_up
        call vga_print

.show_details:
        ; Show PCI location
        mov esi, msg_net_pci
        call vga_print
        movzx eax, byte [net_pci_bus]
        call vga_print_dec
        mov al, ':'
        call vga_putchar
        movzx eax, byte [net_pci_dev]
        call vga_print_dec
        mov al, '.'
        call vga_putchar
        movzx eax, byte [net_pci_func]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar

        ; Show I/O base
        mov esi, msg_net_iobase
        call vga_print
        mov eax, [net_io_base]
        call vga_print_hex
        mov al, 0x0A
        call vga_putchar

        ; Show MAC address
        mov esi, msg_net_mac
        call vga_print
        mov esi, net_mac
        mov ecx, 6
        xor ebx, ebx
.mac_loop:
        movzx eax, byte [esi + ebx]
        call vga_print_hex_byte
        inc ebx
        cmp ebx, ecx
        jge .mac_done
        mov al, ':'
        call vga_putchar
        jmp .mac_loop
.mac_done:
        mov al, 0x0A
        call vga_putchar

.net_end:
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; vga_print_hex_byte - Print AL as 2 hex digits
;---------------------------------------
vga_print_hex_byte:
        push eax
        push ecx
        mov cl, al
        shr al, 4
        call .hex_nibble
        mov al, cl
        and al, 0x0F
        call .hex_nibble
        pop ecx
        pop eax
        ret
.hex_nibble:
        cmp al, 10
        jb .hex_digit
        add al, 'A' - 10
        jmp .hex_print
.hex_digit:
        add al, '0'
.hex_print:
        call vga_putchar
        ret
