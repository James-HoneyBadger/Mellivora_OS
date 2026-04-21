;=======================================================================
; net.inc - Full TCP/IP Networking Stack with RTL8139 NIC Driver
;
; Features:
;   - RTL8139 PCI NIC driver with RX/TX interrupt handling
;   - Ethernet II frame handling
;   - ARP (cache, request, reply)
;   - IPv4 (send, receive, header checksum)
;   - ICMP (echo request/reply - ping)
;   - UDP (send, receive, port demux)
;   - TCP (full client state machine, 3-way handshake, data, close)
;   - DHCP client (discover/offer/request/ack)
;   - DNS resolver (A record queries via UDP)
;   - BSD-style socket API (8 simultaneous sockets)
;   - Shell commands: net, ping, ifconfig, dhcp, arp
;=======================================================================

;---------------------------------------------------------------
; CONSTANTS
;---------------------------------------------------------------

; PCI Configuration Space
PCI_CONFIG_ADDR equ 0xCF8
PCI_CONFIG_DATA equ 0xCFC

; RTL8139 identification
RTL8139_VENDOR  equ 0x10EC
RTL8139_DEVICE  equ 0x8139

; RTL8139 register offsets
RTL_MAC0        equ 0x00
RTL_MAC4        equ 0x04
RTL_TXSTATUS0   equ 0x10
RTL_TXADDR0     equ 0x20
RTL_RXBUF       equ 0x30
RTL_CMD         equ 0x37
RTL_CAPR        equ 0x38
RTL_CBR         equ 0x3A
RTL_IMR         equ 0x3C
RTL_ISR         equ 0x3E
RTL_TXCONFIG    equ 0x40
RTL_RXCONFIG    equ 0x44
RTL_CONFIG1     equ 0x52

; RTL8139 command bits
RTL_CMD_RESET   equ 0x10
RTL_CMD_RX_EN   equ 0x08
RTL_CMD_TX_EN   equ 0x04

; RX config: accept broadcast + multicast + physical match + all, wrap
RTL_RX_ACCEPT   equ 0x0F
RTL_RX_WRAP     equ 0x80
RTL_RX_CONFIG   equ (RTL_RX_WRAP | RTL_RX_ACCEPT)

; RTL8139 ISR bits
RTL_ISR_ROK     equ 0x0001
RTL_ISR_TOK     equ 0x0004

; Buffer sizes
NET_RX_BUF_SIZE equ 8192 + 16
NET_TX_BUF_SIZE equ 1536
NET_PKT_BUF_SZ  equ 1536

; Network state
NET_STATE_NONE  equ 0
NET_STATE_FOUND equ 1
NET_STATE_UP    equ 2

; Ethernet constants
ETH_HLEN        equ 14
ETH_TYPE_IP     equ 0x0800
ETH_TYPE_ARP    equ 0x0806

; IP constants
IP_PROTO_ICMP   equ 1
IP_PROTO_TCP    equ 6
IP_PROTO_UDP    equ 17
IP_HLEN         equ 20
IP_TTL          equ 64

; ICMP types
ICMP_ECHO_REPLY equ 0
ICMP_ECHO_REQ   equ 8

; TCP flags
TCP_FIN         equ 0x01
TCP_SYN         equ 0x02
TCP_RST         equ 0x04
TCP_PSH         equ 0x08
TCP_ACK         equ 0x10
TCP_URG         equ 0x20

; TCP states
TCP_CLOSED      equ 0
TCP_LISTEN      equ 1
TCP_SYN_SENT    equ 2
TCP_SYN_RCVD    equ 3
TCP_ESTABLISHED equ 4
TCP_FIN_WAIT_1  equ 5
TCP_FIN_WAIT_2  equ 6
TCP_CLOSE_WAIT  equ 7
TCP_CLOSING     equ 8
TCP_LAST_ACK    equ 9
TCP_TIME_WAIT   equ 10

; Socket constants
MAX_SOCKETS     equ 32
SOCK_SIZE       equ 128
SOCK_TCP        equ 1
SOCK_UDP        equ 2

; Socket structure offsets
SOCK_TYPE       equ 0           ; dword: 0=free, 1=TCP, 2=UDP
SOCK_STATE      equ 4           ; dword: TCP state
SOCK_LOCAL_PORT equ 8           ; word
SOCK_REMOTE_PORT equ 10         ; word
SOCK_REMOTE_IP  equ 12          ; dword (network byte order)
SOCK_SEQ        equ 16          ; dword: our send sequence
SOCK_ACK        equ 20          ; dword: expected receive sequence
SOCK_RECV_BUF   equ 24          ; dword: pointer to 4KB recv buffer
SOCK_RECV_LEN   equ 28          ; dword: bytes available in recv buf
SOCK_SEND_BUF   equ 32          ; dword: pointer to 4KB send buffer
SOCK_SEND_LEN   equ 36          ; dword: bytes pending send
SOCK_FLAGS      equ 40          ; dword: misc flags
SOCK_TIMER      equ 44          ; dword: timeout tick count
SOCK_PENDING    equ 48          ; byte: TCP flags to send from non-ISR context
SOCK_RETX_SEQ   equ 52          ; dword: SEQ of unACKed data
SOCK_RETX_LEN   equ 56          ; dword: length of unACKed data (0 = none)
SOCK_RETX_TIMER equ 60          ; dword: tick count for retransmit timeout
SOCK_RETX_COUNT equ 64          ; dword: retransmissions performed
; v3.0 fields
SOCK_PARENT     equ 68          ; dword: parent listening socket index (-1 = none)

; Socket buffer size
SOCK_BUF_SIZE   equ 8192

; ARP cache
ARP_CACHE_SIZE  equ 16
ARP_ENTRY_SIZE  equ 12
ARP_ENTRY_IP    equ 0           ; dword
ARP_ENTRY_MAC   equ 4           ; 6 bytes
ARP_ENTRY_STATE equ 10          ; byte: 0=free, 1=pending, 2=resolved
ARP_ENTRY_PAD   equ 11          ; byte padding

; ARP opcodes
ARP_OP_REQUEST  equ 1
ARP_OP_REPLY    equ 2

; DHCP constants
DHCP_CLIENT_PORT equ 68
DHCP_SERVER_PORT equ 67
DHCP_DISCOVER   equ 1
DHCP_OFFER      equ 2
DHCP_REQUEST    equ 3
DHCP_ACK        equ 5
DHCP_MAGIC      equ 0x63538263  ; 99.130.83.99 as little-endian dword

; DNS constants
DNS_PORT        equ 53

; Ephemeral port range
EPHEMERAL_START equ 49152

; Broadcast addresses
BROADCAST_MAC:  db 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
ZERO_MAC:       db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
BROADCAST_IP    equ 0xFFFFFFFF  ; 255.255.255.255


;=======================================================================
; SECTION 1: NIC DRIVER (RTL8139)
;=======================================================================

;---------------------------------------
; net_init - Scan PCI bus and initialize RTL8139
;---------------------------------------
net_init:
        pushad
        mov dword [net_state], NET_STATE_NONE

        ; Scan PCI bus 0, all 32 devices, all 8 functions
        xor ebx, ebx
.scan_dev:
        xor ecx, ecx
.scan_dev_loop:
        cmp ecx, 32
        jge .scan_done
        xor edx, edx
.scan_func:
        cmp edx, 8
        jge .next_dev

        call pci_read_config_word0
        cmp eax, 0xFFFFFFFF
        je .next_func
        cmp eax, 0x0000FFFF
        je .next_func

        cmp ax, RTL8139_VENDOR
        jne .next_func
        shr eax, 16
        cmp ax, RTL8139_DEVICE
        jne .next_func

        ; Found RTL8139
        mov [net_pci_bus], bl
        mov [net_pci_dev], cl
        mov [net_pci_func], dl

        ; Read BAR0 (PCI register 0x10)
        push edx
        push ecx
        push ebx
        mov eax, 0x80000000
        movzx esi, bl
        shl esi, 16
        or eax, esi
        movzx esi, cl
        shl esi, 11
        or eax, esi
        pop ebx
        pop ecx
        pop edx
        push edx
        movzx esi, dl
        shl esi, 8
        or eax, esi
        or eax, 0x10
        push edx
        mov dx, PCI_CONFIG_ADDR
        out dx, eax
        mov dx, PCI_CONFIG_DATA
        in eax, dx
        pop edx
        pop edx
        and eax, 0xFFFFFFFC
        mov [net_io_base], eax

        ; Read PCI interrupt line (register 0x3C, byte 0)
        push edx
        push ecx
        push ebx
        mov eax, 0x80000000
        movzx esi, bl
        shl esi, 16
        or eax, esi
        movzx esi, cl
        shl esi, 11
        or eax, esi
        pop ebx
        pop ecx
        pop edx
        push edx
        movzx esi, dl
        shl esi, 8
        or eax, esi
        or eax, 0x3C
        push edx
        mov dx, PCI_CONFIG_ADDR
        out dx, eax
        mov dx, PCI_CONFIG_DATA
        in eax, dx
        pop edx
        pop edx
        and eax, 0xFF
        mov [net_irq], al

        ; Enable PCI bus mastering (register 0x04, set bit 2)
        push edx
        push ecx
        push ebx
        mov eax, 0x80000000
        movzx esi, bl
        shl esi, 16
        or eax, esi
        movzx esi, cl
        shl esi, 11
        or eax, esi
        pop ebx
        pop ecx
        pop edx
        push edx
        movzx esi, dl
        shl esi, 8
        or eax, esi
        or eax, 0x04
        push edx
        push eax
        mov dx, PCI_CONFIG_ADDR
        out dx, eax
        mov dx, PCI_CONFIG_DATA
        in eax, dx
        or eax, 0x04           ; Set bus master bit
        mov ecx, eax
        pop eax
        mov dx, PCI_CONFIG_ADDR
        out dx, eax
        mov dx, PCI_CONFIG_DATA
        mov eax, ecx
        out dx, eax
        pop edx
        pop edx

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
        cmp dword [net_state], NET_STATE_FOUND
        jne .init_done
        call net_rtl8139_init
        call net_install_irq

        ; Set default QEMU user-mode network config
        mov dword [net_ip], 0x0F02000A        ; 10.0.2.15 LE
        mov dword [net_mask], 0x00FFFFFF       ; 255.255.255.0 LE
        mov dword [net_gateway], 0x0202000A    ; 10.0.2.2 LE
        mov dword [net_dns], 0x0302000A        ; 10.0.2.3 LE
        mov word [net_ephemeral_port], EPHEMERAL_START
.init_done:
        popad
        ret

;---------------------------------------
; pci_read_config_word0
;  EBX=bus ECX=device EDX=function -> EAX=config dword 0
;---------------------------------------
pci_read_config_word0:
        push edx
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
        mov dx, PCI_CONFIG_ADDR
        out dx, eax
        mov dx, PCI_CONFIG_DATA
        in eax, dx
        pop edx
        ret

;---------------------------------------
; net_read_mac - Read 6-byte MAC from NIC
;---------------------------------------
net_read_mac:
        pushad
        mov edx, [net_io_base]
        in eax, dx
        mov [net_mac], eax
        add edx, 4
        in eax, dx
        mov [net_mac + 4], ax
        popad
        ret

;---------------------------------------
; net_rtl8139_init - Initialize RTL8139
;---------------------------------------
net_rtl8139_init:
        pushad
        mov ebx, [net_io_base]

        ; Power on
        lea edx, [ebx + RTL_CONFIG1]
        xor al, al
        out dx, al

        ; Software reset
        lea edx, [ebx + RTL_CMD]
        mov al, RTL_CMD_RESET
        out dx, al
        mov ecx, 100000
.wait_reset:
        in al, dx
        test al, RTL_CMD_RESET
        jz .reset_done
        dec ecx
        jnz .wait_reset
        jmp .init_fail
.reset_done:

        ; Allocate RX buffer (3 pages = 12KB, covers 8K+16)
        mov ecx, 3
        call pmm_alloc_pages
        test eax, eax
        jz .init_fail
        mov [net_rx_buf], eax

        ; Tell NIC the RX buffer address
        mov ecx, eax
        lea edx, [ebx + RTL_RXBUF]
        mov eax, ecx
        out dx, eax

        ; Allocate TX buffers (4 pages, one per descriptor)
        mov ecx, 4
.alloc_tx:
        push ecx
        call pmm_alloc_page
        test eax, eax
        jz .init_fail_pop
        pop ecx
        push ecx
        neg ecx
        add ecx, 5             ; descriptor index: 4-ecx
        dec ecx
        mov [net_tx_bufs + ecx*4], eax
        pop ecx
        dec ecx
        jnz .alloc_tx

        ; Allocate packet processing buffer (1 page)
        call pmm_alloc_page
        test eax, eax
        jz .init_fail
        mov [net_pkt_buf], eax

        ; Set IMR: enable ROK and TOK interrupts
        lea edx, [ebx + RTL_IMR]
        mov ax, 0x0005
        out dx, ax

        ; Configure RX
        lea edx, [ebx + RTL_RXCONFIG]
        mov eax, RTL_RX_CONFIG
        out dx, eax

        ; Enable receiver and transmitter
        lea edx, [ebx + RTL_CMD]
        mov al, RTL_CMD_RX_EN | RTL_CMD_TX_EN
        out dx, al

        ; Init CAPR
        lea edx, [ebx + RTL_CAPR]
        mov ax, 0xFFF0
        out dx, ax
        mov dword [net_rx_offset], 0

        call net_read_mac
        mov dword [net_state], NET_STATE_UP
        mov dword [net_tx_desc], 0
        popad
        ret

.init_fail_pop:
        pop ecx
.init_fail:
        popad
        ret

;---------------------------------------
; net_install_irq - Hook NIC IRQ in IDT and unmask PIC
;---------------------------------------
net_install_irq:
        pushad
        movzx eax, byte [net_irq]

        ; Calculate INT vector: IRQ 0-7 -> 0x20-0x27, IRQ 8-15 -> 0x28-0x2F
        cmp eax, 8
        jb .irq_pic1
        ; PIC2 IRQ
        mov ecx, eax
        add ecx, 0x20           ; INT vector
        push eax
        mov eax, irq_net
        call idt_set_gate
        pop eax

        ; Unmask on PIC2
        sub eax, 8
        mov cl, al
        mov ah, 1
        shl ah, cl
        not ah
        in al, PIC2_DATA
        and al, ah
        out PIC2_DATA, al

        ; Ensure cascade (IRQ2) unmasked on PIC1
        in al, PIC1_DATA
        and al, ~(1 << 2)
        out PIC1_DATA, al
        jmp .irq_installed

.irq_pic1:
        mov ecx, eax
        add ecx, 0x20
        push eax
        mov eax, irq_net
        call idt_set_gate
        pop eax

        ; Unmask on PIC1
        mov cl, al
        mov ah, 1
        shl ah, cl
        not ah
        in al, PIC1_DATA
        and al, ah
        out PIC1_DATA, al

.irq_installed:
        popad
        ret

;---------------------------------------
; irq_net - RTL8139 interrupt handler
;---------------------------------------
irq_net:
        pushad

        mov ebx, [net_io_base]

        ; Read ISR
        lea edx, [ebx + RTL_ISR]
        in ax, dx
        test ax, ax
        jz .irq_net_eoi

        ; Acknowledge all bits
        push ax
        out dx, ax
        pop ax

        ; Handle received packets
        test ax, RTL_ISR_ROK
        jz .irq_net_check_tok

        ; Process all pending packets in RX ring
.rx_loop:
        ; Check if buffer empty: CMD bit 0 (BUFE) set means empty
        lea edx, [ebx + RTL_CMD]
        in al, dx
        test al, 0x01           ; BUFE
        jnz .irq_net_eoi

        ; Read packet header at rx_buf + rx_offset
        mov esi, [net_rx_buf]
        add esi, [net_rx_offset]

        ; Header: [status:16] [length:16]
        mov eax, [esi]
        mov ecx, eax
        shr ecx, 16            ; ECX = packet length (includes 4-byte CRC)
        and eax, 0xFFFF        ; EAX = RX status

        ; Check ROK bit in status
        test eax, 1
        jz .rx_skip

        ; Validate length
        cmp ecx, 64
        jb .rx_skip
        cmp ecx, NET_PKT_BUF_SZ
        ja .rx_skip

        ; Copy packet to processing buffer (skip 4-byte header)
        sub ecx, 4             ; Remove CRC from length
        push ecx
        add esi, 4             ; Skip RX header
        mov edi, [net_pkt_buf]
        cld
        rep movsb
        pop ecx

        ; Process the Ethernet frame
        push ebx
        mov esi, [net_pkt_buf]
        call net_process_frame
        pop ebx

.rx_skip:
        ; Advance RX offset: (offset + length + 4 header + 3) & ~3
        mov esi, [net_rx_buf]
        add esi, [net_rx_offset]
        mov eax, [esi]
        shr eax, 16            ; length
        add eax, 4             ; + header
        add eax, 3
        and eax, ~3            ; dword align
        add eax, [net_rx_offset]

        ; Wrap at 8192
        cmp eax, 8192
        jb .rx_no_wrap
        sub eax, 8192
.rx_no_wrap:
        mov [net_rx_offset], eax

        ; Update CAPR (offset - 16)
        sub eax, 16
        and eax, 0xFFFF
        lea edx, [ebx + RTL_CAPR]
        out dx, ax

        jmp .rx_loop

.irq_net_check_tok:

.irq_net_eoi:
        ; Send EOI
        movzx eax, byte [net_irq]
        cmp eax, 8
        jb .eoi_pic1_only
        mov al, 0x20
        out PIC2_CMD, al
.eoi_pic1_only:
        mov al, 0x20
        out PIC1_CMD, al

        popad
        iretd

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

        ; Get current TX descriptor
        mov eax, [net_tx_desc]
        and eax, 3

        ; Wait for descriptor to be available (OWN bit set = NIC done)
        mov ebx, [net_io_base]
        push eax
        shl eax, 2
        lea edx, [ebx + RTL_TXSTATUS0]
        add edx, eax
        mov ebp, 100000
.wait_tx:
        in eax, dx
        test eax, 0x2000       ; OWN bit
        jnz .tx_ready
        ; For first use, OWN=0 is also OK if TOK=0 and size=0
        test eax, eax
        jz .tx_ready
        dec ebp
        jnz .wait_tx
        pop eax
        jmp .send_fail
.tx_ready:
        pop eax

        ; Copy frame to TX buffer
        push ecx
        mov edi, [net_tx_bufs + eax*4]
        cld
        rep movsb
        pop ecx

        ; Pad to minimum 60 bytes (Ethernet minimum)
        cmp ecx, 60
        jge .tx_no_pad
        push ecx
        mov edi, [net_tx_bufs + eax*4]
        add edi, ecx
        mov ebp, 60
        sub ebp, ecx
        xchg ecx, ebp
        xor al, al
        rep stosb
        pop ecx
        mov ecx, 60
.tx_no_pad:

        ; Write TX buffer address
        push ecx
        shl eax, 2
        lea edx, [ebx + RTL_TXADDR0]
        add edx, eax
        shr eax, 2
        mov esi, [net_tx_bufs + eax*4]
        mov eax, esi
        out dx, eax
        pop ecx

        ; Write TX status (length, clear OWN to start TX)
        mov eax, [net_tx_desc]
        and eax, 3
        shl eax, 2
        lea edx, [ebx + RTL_TXSTATUS0]
        add edx, eax
        mov eax, ecx            ; frame size in bits 0-12
        out dx, eax

        ; Advance descriptor
        inc dword [net_tx_desc]

        popad
        clc
        ret
.send_fail:
        popad
        stc
        ret


;=======================================================================
; SECTION 2: ETHERNET LAYER
;=======================================================================

;---------------------------------------
; net_process_frame - Process received Ethernet frame
;  ESI = pointer to frame data (in net_pkt_buf)
;  ECX = frame length (without CRC)
;---------------------------------------
net_process_frame:
        pushad
        cmp ecx, ETH_HLEN
        jb .pf_done

        ; Read EtherType (bytes 12-13, big-endian)
        movzx eax, byte [esi + 12]
        shl eax, 8
        mov al, [esi + 13]

        cmp eax, ETH_TYPE_ARP
        je .pf_arp
        cmp eax, ETH_TYPE_IP
        je .pf_ip
        jmp .pf_done

.pf_arp:
        add esi, ETH_HLEN
        sub ecx, ETH_HLEN
        call arp_handle
        jmp .pf_done

.pf_ip:
        add esi, ETH_HLEN
        sub ecx, ETH_HLEN
        call ip_handle
        jmp .pf_done

.pf_done:
        popad
        ret

;---------------------------------------
; eth_build_frame - Build Ethernet header + send
;  ESI = payload data, ECX = payload length
;  EDI = destination MAC (6 bytes)
;  EBX = EtherType (16-bit, host byte order)
; Uses net_frame_buf for assembly
;---------------------------------------
eth_build_frame:
        pushad

        ; Build frame in net_frame_buf
        push esi
        push ecx

        mov edi, [edi]          ; This won't work - need to pass MAC ptr differently
        ; Actually, let's fix the calling convention:
        ; On entry: stack has dest MAC ptr pushed before call
        ; Let's use a simpler approach - caller pre-builds in net_frame_buf
        pop ecx
        pop esi
        popad
        ret

;---------------------------------------
; net_send_packet - Build Ethernet frame and transmit
;  [net_frame_dst_mac] = destination MAC (6 bytes, caller sets)
;  [net_frame_ethertype] = EtherType (word, network byte order, caller sets)
;  ESI = payload, ECX = payload length
;---------------------------------------
net_send_packet:
        pushad
        mov edi, net_frame_buf

        ; Copy destination MAC
        push esi
        push ecx
        mov esi, net_frame_dst_mac
        mov ecx, 6
        cld
        rep movsb

        ; Copy source MAC (our MAC)
        mov esi, net_mac
        mov ecx, 6
        rep movsb

        ; EtherType (already in network byte order)
        mov ax, [net_frame_ethertype]
        stosw

        ; Copy payload
        pop ecx
        pop esi
        push ecx
        rep movsb
        pop ecx

        ; Total frame length = 14 + payload
        add ecx, ETH_HLEN
        mov esi, net_frame_buf
        call net_send_frame

        popad
        ret


;=======================================================================
; SECTION 3: ARP
;=======================================================================

;---------------------------------------
; arp_handle - Process incoming ARP packet
;  ESI = ARP data (past Ethernet header)
;  ECX = length
;---------------------------------------
arp_handle:
        pushad
        cmp ecx, 28
        jb .arp_h_done

        ; ARP header: htype(2) ptype(2) hlen(1) plen(1) oper(2)
        ;   sha(6) spa(4) tha(6) tpa(4)
        movzx eax, byte [esi + 6]
        shl eax, 8
        mov al, [esi + 7]      ; operation (big-endian)

        cmp eax, ARP_OP_REQUEST
        je .arp_request
        cmp eax, ARP_OP_REPLY
        je .arp_reply
        jmp .arp_h_done

.arp_request:
        ; Is it for our IP?
        mov eax, [esi + 24]    ; target protocol address
        cmp eax, [net_ip]
        jne .arp_h_done

        ; Cache the sender
        mov eax, [esi + 14]    ; sender IP
        lea ebx, [esi + 8]     ; sender MAC
        call arp_cache_add

        ; Send ARP reply
        call arp_send_reply
        jmp .arp_h_done

.arp_reply:
        ; Cache the sender's IP/MAC
        mov eax, [esi + 14]    ; sender IP
        lea ebx, [esi + 8]     ; sender MAC
        call arp_cache_add
        jmp .arp_h_done

.arp_h_done:
        popad
        ret

;---------------------------------------
; arp_send_reply - Send ARP reply
;  ESI = incoming ARP packet (we reply to its sender)
;---------------------------------------
arp_send_reply:
        pushad
        mov edi, net_arp_buf

        ; Hardware type = 1 (Ethernet), big-endian
        mov byte [edi], 0
        mov byte [edi+1], 1
        ; Protocol type = 0x0800 (IPv4), big-endian
        mov byte [edi+2], 0x08
        mov byte [edi+3], 0x00
        ; Hardware size = 6, Protocol size = 4
        mov byte [edi+4], 6
        mov byte [edi+5], 4
        ; Operation = 2 (reply), big-endian
        mov byte [edi+6], 0
        mov byte [edi+7], 2

        ; Sender hardware address = our MAC
        push esi
        lea edi, [edi]
        add edi, 8
        mov esi, net_mac
        mov ecx, 6
        cld
        rep movsb
        pop esi

        ; Sender protocol address = our IP
        mov eax, [net_ip]
        mov [net_arp_buf + 14], eax

        ; Target hardware address = requester's MAC
        push esi
        add esi, 8
        mov edi, net_arp_buf + 18
        mov ecx, 6
        cld
        rep movsb
        pop esi

        ; Target protocol address = requester's IP
        mov eax, [esi + 14]
        mov [net_arp_buf + 24], eax

        ; Send via Ethernet
        ; Set destination MAC to requester's MAC
        push esi
        add esi, 8
        mov edi, net_frame_dst_mac
        mov ecx, 6
        cld
        rep movsb
        pop esi

        mov word [net_frame_ethertype], 0x0608  ; ARP = 0x0806 big-endian
        mov esi, net_arp_buf
        mov ecx, 28
        call net_send_packet
        popad
        ret

;---------------------------------------
; arp_request - Send ARP request for IP
;  EAX = target IP (little-endian)
;---------------------------------------
arp_request:
        pushad
        mov [net_arp_target_ip], eax
        mov edi, net_arp_buf

        ; Hardware type = 1, Protocol type = 0x0800
        mov byte [edi], 0
        mov byte [edi+1], 1
        mov byte [edi+2], 0x08
        mov byte [edi+3], 0x00
        mov byte [edi+4], 6
        mov byte [edi+5], 4
        ; Operation = 1 (request)
        mov byte [edi+6], 0
        mov byte [edi+7], 1

        ; Sender MAC
        mov esi, net_mac
        mov edi, net_arp_buf + 8
        mov ecx, 6
        cld
        rep movsb

        ; Sender IP
        mov eax, [net_ip]
        mov [net_arp_buf + 14], eax

        ; Target MAC = 00:00:00:00:00:00
        mov edi, net_arp_buf + 18
        xor eax, eax
        stosd
        stosw

        ; Target IP
        mov eax, [net_arp_target_ip]
        mov [net_arp_buf + 24], eax

        ; Send broadcast
        mov esi, BROADCAST_MAC
        mov edi, net_frame_dst_mac
        mov ecx, 6
        cld
        rep movsb

        mov word [net_frame_ethertype], 0x0608
        mov esi, net_arp_buf
        mov ecx, 28
        call net_send_packet
        popad
        ret

;---------------------------------------
; arp_cache_add - Add/update ARP entry
;  EAX = IP address (little-endian)
;  EBX = pointer to 6-byte MAC
;---------------------------------------
arp_cache_add:
        pushad
        mov edi, net_arp_cache
        mov ecx, ARP_CACHE_SIZE

        ; Check if already exists
.arp_search:
        cmp [edi + ARP_ENTRY_IP], eax
        je .arp_update
        add edi, ARP_ENTRY_SIZE
        dec ecx
        jnz .arp_search

        ; Find free entry
        mov edi, net_arp_cache
        mov ecx, ARP_CACHE_SIZE
.arp_find_free:
        cmp byte [edi + ARP_ENTRY_STATE], 0
        je .arp_update
        add edi, ARP_ENTRY_SIZE
        dec ecx
        jnz .arp_find_free

        ; Cache full, overwrite first entry
        mov edi, net_arp_cache

.arp_update:
        mov [edi + ARP_ENTRY_IP], eax
        push esi
        mov esi, ebx
        lea edi, [edi + ARP_ENTRY_MAC]
        push edi
        mov ecx, 6
        cld
        rep movsb
        pop edi
        sub edi, ARP_ENTRY_MAC
        mov byte [edi + ARP_ENTRY_STATE], 2  ; resolved
        pop esi
        popad
        ret

;---------------------------------------
; arp_lookup - Look up MAC for IP address
;  EAX = IP address (little-endian)
;  EDI = 6-byte buffer for MAC result
; Returns: CF=0 found, CF=1 not found (ARP request sent)
;---------------------------------------
arp_lookup:
        push ebx
        push ecx
        push esi

        ; Broadcast IP -> broadcast MAC
        cmp eax, BROADCAST_IP
        je .arp_bcast

        ; Check if destination is on our subnet
        mov ebx, eax
        mov ecx, [net_mask]
        and ebx, ecx
        mov esi, [net_ip]
        and esi, ecx
        cmp ebx, esi
        je .arp_direct         ; Same subnet, ARP directly

        ; Different subnet - use gateway
        mov eax, [net_gateway]

.arp_direct:
        ; Search ARP cache
        push edi
        mov edi, net_arp_cache
        mov ecx, ARP_CACHE_SIZE
.arp_lk:
        cmp byte [edi + ARP_ENTRY_STATE], 2
        jne .arp_lk_next
        cmp [edi + ARP_ENTRY_IP], eax
        je .arp_lk_found
.arp_lk_next:
        add edi, ARP_ENTRY_SIZE
        dec ecx
        jnz .arp_lk
        pop edi

        ; Not found - send ARP request
        call arp_request
        pop esi
        pop ecx
        pop ebx
        stc
        ret

.arp_lk_found:
        lea esi, [edi + ARP_ENTRY_MAC]
        pop edi
        push edi
        mov ecx, 6
        cld
        rep movsb
        pop edi
        pop esi
        pop ecx
        pop ebx
        clc
        ret

.arp_bcast:
        push edi
        mov esi, BROADCAST_MAC
        mov ecx, 6
        cld
        rep movsb
        pop edi
        pop esi
        pop ecx
        pop ebx
        clc
        ret


;=======================================================================
; SECTION 4: IP LAYER
;=======================================================================

;---------------------------------------
; ip_handle - Process incoming IP packet
;  ESI = IP header, ECX = length
;---------------------------------------
ip_handle:
        pushad
        cmp ecx, IP_HLEN
        jb .ip_h_done

        ; Verify IP version = 4 and IHL >= 5
        movzx eax, byte [esi]
        mov ebx, eax
        shr eax, 4
        cmp eax, 4
        jne .ip_h_done
        and ebx, 0x0F
        cmp ebx, 5
        jb .ip_h_done

        ; Check destination is us or broadcast
        mov eax, [esi + 16]    ; destination IP
        cmp eax, [net_ip]
        je .ip_for_us
        cmp eax, BROADCAST_IP
        je .ip_for_us
        ; Check subnet broadcast
        mov edx, [net_mask]
        not edx
        mov ecx, [net_ip]
        or ecx, edx
        cmp eax, ecx
        jne .ip_h_done

.ip_for_us:
        ; Save source IP for response building
        mov eax, [esi + 12]
        mov [net_recv_src_ip], eax

        ; Get protocol
        movzx eax, byte [esi + 9]
        ; Get header length in bytes
        movzx ecx, byte [esi]
        and ecx, 0x0F
        shl ecx, 2
        ; Get total length
        movzx edx, byte [esi + 2]
        shl edx, 8
        mov dl, [esi + 3]      ; total length (big-endian)

        ; Payload = ESI + header_len, length = total - header_len
        push edx
        sub edx, ecx
        add esi, ecx
        mov ecx, edx
        pop edx

        cmp eax, IP_PROTO_ICMP
        je .ip_icmp
        cmp eax, IP_PROTO_UDP
        je .ip_udp
        cmp eax, IP_PROTO_TCP
        je .ip_tcp
        jmp .ip_h_done

.ip_icmp:
        call icmp_handle
        jmp .ip_h_done
.ip_udp:
        call udp_handle
        jmp .ip_h_done
.ip_tcp:
        call tcp_handle
        jmp .ip_h_done
.ip_h_done:
        popad
        ret

;---------------------------------------
; ip_send - Send IP packet
;  ESI = payload, ECX = payload length
;  EAX = destination IP (little-endian)
;  DL  = protocol (ICMP=1, TCP=6, UDP=17)
;---------------------------------------
ip_send:
        pushad
        mov [net_ip_dst], eax
        mov [net_ip_proto], dl
        mov [net_ip_payload], esi
        mov [net_ip_payload_len], ecx

        ; Build IP header in net_ip_buf
        mov edi, net_ip_buf

        ; Version=4, IHL=5 -> byte 0x45
        mov byte [edi], 0x45
        ; DSCP/ECN = 0
        mov byte [edi+1], 0
        ; Total length = 20 + payload (big-endian)
        mov eax, ecx
        add eax, IP_HLEN
        mov byte [edi+2], ah
        mov byte [edi+3], al
        ; Identification
        movzx eax, word [net_ip_id]
        inc word [net_ip_id]
        mov byte [edi+4], ah
        mov byte [edi+5], al
        ; Flags + Fragment offset = 0x4000 (Don't Fragment)
        mov byte [edi+6], 0x40
        mov byte [edi+7], 0x00
        ; TTL
        mov byte [edi+8], IP_TTL
        ; Protocol
        mov al, [net_ip_proto]
        mov [edi+9], al
        ; Header checksum (zero for now)
        mov word [edi+10], 0
        ; Source IP
        mov eax, [net_ip]
        mov [edi+12], eax
        ; Destination IP
        mov eax, [net_ip_dst]
        mov [edi+16], eax

        ; Compute IP header checksum
        push edi
        mov esi, edi
        mov ecx, IP_HLEN
        call inet_checksum
        pop edi
        mov [edi+10], ax

        ; Append payload after IP header
        mov esi, [net_ip_payload]
        mov ecx, [net_ip_payload_len]
        push ecx
        lea edi, [edi + IP_HLEN]
        cld
        rep movsb
        pop ecx

        ; Resolve destination MAC via ARP
        mov eax, [net_ip_dst]
        mov edi, net_frame_dst_mac
        call arp_lookup
        jc .ip_send_arp_wait

        ; Set EtherType to IP (0x0800 big-endian = 0x0008 LE)
        mov word [net_frame_ethertype], 0x0008

        ; Send
        mov esi, net_ip_buf
        mov ecx, [net_ip_payload_len]
        add ecx, IP_HLEN
        call net_send_packet
        popad
        ret

.ip_send_arp_wait:
        ; ARP request sent, wait briefly then retry once
        mov ecx, 100           ; Wait ~1 second
.arp_wait_loop:
        hlt
        dec ecx
        jz .ip_send_fail

        mov eax, [net_ip_dst]
        ; Check if on different subnet, use gateway
        mov ebx, eax
        push ecx
        mov ecx, [net_mask]
        and ebx, ecx
        mov edx, [net_ip]
        and edx, ecx
        pop ecx
        cmp ebx, edx
        je .arp_check_direct
        mov eax, [net_gateway]
.arp_check_direct:
        push edi
        mov edi, net_frame_dst_mac
        call arp_lookup
        pop edi
        jc .arp_wait_loop

        ; ARP resolved, send the packet
        mov word [net_frame_ethertype], 0x0008
        mov esi, net_ip_buf
        mov ecx, [net_ip_payload_len]
        add ecx, IP_HLEN
        call net_send_packet
        popad
        ret

.ip_send_fail:
        popad
        ret

;---------------------------------------
; inet_checksum - Compute Internet checksum
;  ESI = data, ECX = length in bytes
; Returns: AX = checksum (one's complement)
;---------------------------------------
inet_checksum:
        push ebx
        push ecx
        push edx
        push esi
        xor edx, edx
        mov ebx, ecx
        shr ecx, 1
        jz .ics_odd_check
.ics_loop:
        movzx eax, word [esi]
        add edx, eax
        add esi, 2
        dec ecx
        jnz .ics_loop
.ics_odd_check:
        test ebx, 1
        jz .ics_fold
        movzx eax, byte [esi]
        add edx, eax
.ics_fold:
        mov eax, edx
        shr eax, 16
        and edx, 0xFFFF
        add edx, eax
        mov eax, edx
        shr eax, 16
        add edx, eax
        not dx
        mov ax, dx
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret


;=======================================================================
; SECTION 5: ICMP
;=======================================================================

;---------------------------------------
; icmp_handle - Handle incoming ICMP packet
;  ESI = ICMP data, ECX = length
;  [net_recv_src_ip] = source IP
;---------------------------------------
icmp_handle:
        pushad
        cmp ecx, 8
        jb .icmp_done

        movzx eax, byte [esi]  ; ICMP type
        cmp eax, ICMP_ECHO_REQ
        je .icmp_echo_reply

        cmp eax, ICMP_ECHO_REPLY
        je .icmp_echo_recv
        jmp .icmp_done

.icmp_echo_reply:
        ; Send echo reply: swap src/dst, change type to 0
        mov edi, net_icmp_buf
        mov byte [edi], ICMP_ECHO_REPLY
        mov byte [edi+1], 0    ; code
        mov word [edi+2], 0    ; checksum placeholder
        ; Copy rest of ICMP data (ID, seq, data)
        push ecx
        sub ecx, 4
        lea esi, [esi + 4]
        lea edi, [edi + 4]
        cld
        rep movsb
        pop ecx

        ; Compute ICMP checksum
        mov esi, net_icmp_buf
        push ecx
        call inet_checksum
        mov [net_icmp_buf + 2], ax
        pop ecx

        ; Send via IP
        mov esi, net_icmp_buf
        mov eax, [net_recv_src_ip]
        mov dl, IP_PROTO_ICMP
        call ip_send
        jmp .icmp_done

.icmp_echo_recv:
        ; Store ping reply info for the ping command
        movzx eax, word [esi + 4]  ; ID (network byte order)
        cmp ax, [net_ping_id]
        jne .icmp_done
        mov byte [net_ping_received], 1
        ; Store tick for RTT calculation
        mov eax, [tick_count]
        mov [net_ping_recv_tick], eax
        jmp .icmp_done

.icmp_done:
        popad
        ret

;---------------------------------------
; icmp_send_echo - Send ICMP echo request (ping)
;  EAX = destination IP
;---------------------------------------
icmp_send_echo:
        pushad
        mov [net_ping_dst_ip], eax

        mov edi, net_icmp_buf
        mov byte [edi], ICMP_ECHO_REQ
        mov byte [edi+1], 0           ; code
        mov word [edi+2], 0           ; checksum placeholder
        ; ID
        mov ax, [net_ping_id]
        mov [edi+4], ax
        ; Sequence number
        inc word [net_ping_seq]
        mov ax, [net_ping_seq]
        mov [edi+6], ax
        ; Data (8 bytes of timestamp)
        mov eax, [tick_count]
        mov [net_ping_send_tick], eax
        mov [edi+8], eax
        mov dword [edi+12], 0x12345678

        ; Compute checksum
        mov esi, net_icmp_buf
        mov ecx, 16
        call inet_checksum
        mov [net_icmp_buf + 2], ax

        ; Send
        mov byte [net_ping_received], 0
        mov esi, net_icmp_buf
        mov ecx, 16
        mov eax, [net_ping_dst_ip]
        mov dl, IP_PROTO_ICMP
        call ip_send
        popad
        ret


;=======================================================================
; SECTION 6: UDP
;=======================================================================

;---------------------------------------
; udp_handle - Handle incoming UDP packet
;  ESI = UDP header, ECX = length
;  [net_recv_src_ip] = source IP
;---------------------------------------
udp_handle:
        pushad
        cmp ecx, 8
        jb .udp_h_done

        ; UDP header: src_port(2) dst_port(2) length(2) checksum(2)
        movzx eax, byte [esi]
        shl eax, 8
        mov al, [esi+1]        ; source port (big-endian -> host)
        mov [net_udp_src_port], ax

        movzx ebx, byte [esi+2]
        shl ebx, 8
        mov bl, [esi+3]        ; dest port (big-endian -> host)

        ; Payload starts at offset 8
        add esi, 8
        sub ecx, 8

        ; Check for DHCP response (port 68)
        cmp bx, DHCP_CLIENT_PORT
        je .udp_dhcp

        ; Check for DNS response (source port 53)
        cmp word [net_udp_src_port], DNS_PORT
        je .udp_dns

        ; Find matching socket
        push ecx
        push esi
        mov eax, ebx           ; destination port
        mov dl, SOCK_UDP
        call sock_find_by_port
        pop esi
        pop ecx
        cmp eax, -1
        je .udp_h_done

        ; Copy data to socket recv buffer
        mov edi, eax
        imul edi, SOCK_SIZE
        add edi, net_sock_table

        mov ebx, [edi + SOCK_RECV_BUF]
        test ebx, ebx
        jz .udp_h_done

        ; Append to recv buffer
        mov edx, [edi + SOCK_RECV_LEN]
        add ebx, edx
        push ecx
        push edi
        mov edi, ebx
        cld
        rep movsb
        pop edi
        pop ecx
        add [edi + SOCK_RECV_LEN], ecx

        ; Store sender info for recvfrom
        mov eax, [net_recv_src_ip]
        mov [edi + SOCK_REMOTE_IP], eax
        mov ax, [net_udp_src_port]
        mov [edi + SOCK_REMOTE_PORT], ax
        jmp .udp_h_done

.udp_dhcp:
        call dhcp_handle
        jmp .udp_h_done
.udp_dns:
        call dns_handle_response
        jmp .udp_h_done
.udp_h_done:
        popad
        ret

;---------------------------------------
; udp_send - Send UDP packet
;  ESI = payload, ECX = payload length
;  EAX = destination IP (little-endian)
;  BX = destination port (host byte order)
;  DX = source port (host byte order)
;---------------------------------------
udp_send:
        pushad
        mov [net_udp_dst_ip], eax
        mov [net_udp_dst_port], bx
        mov [net_udp_local_port], dx
        mov [net_udp_payload], esi
        mov [net_udp_payload_len], ecx

        ; Build UDP header in net_udp_buf
        mov edi, net_udp_buf

        ; Source port (big-endian)
        mov ax, [net_udp_local_port]
        xchg al, ah
        stosw
        ; Destination port (big-endian)
        mov ax, [net_udp_dst_port]
        xchg al, ah
        stosw
        ; Length = 8 + payload (big-endian)
        mov eax, [net_udp_payload_len]
        add eax, 8
        xchg al, ah
        stosw
        ; v3.0: Checksum placeholder (computed below)
        xor ax, ax
        stosw

        ; Copy payload
        mov esi, [net_udp_payload]
        mov ecx, [net_udp_payload_len]
        cld
        rep movsb

        ; v3.0: Compute UDP checksum over pseudo-header + UDP packet
        ; Pseudo-header: src_ip(4) + dst_ip(4) + zero(1) + proto(1) + udp_len(2)
        call udp_compute_checksum
        mov [net_udp_buf + 6], ax       ; Store checksum at offset 6

        ; Send via IP
        mov esi, net_udp_buf
        mov ecx, [net_udp_payload_len]
        add ecx, 8
        mov eax, [net_udp_dst_ip]
        mov dl, IP_PROTO_UDP
        call ip_send
        popad
        ret

;---------------------------------------
; udp_compute_checksum - v3.0
; Computes RFC 768 UDP checksum over
; pseudo-header + UDP header + payload.
; Uses net_udp_buf (complete UDP packet),
; net_ip (our IP), net_udp_dst_ip.
; Returns: AX = checksum (network byte order)
;---------------------------------------
udp_compute_checksum:
        pushad
        xor edx, edx           ; 32-bit accumulator

        ; Add pseudo-header fields
        ; Source IP (in network byte order)
        movzx eax, word [net_ip]
        add edx, eax
        movzx eax, word [net_ip + 2]
        add edx, eax
        ; Destination IP
        movzx eax, word [net_udp_dst_ip]
        add edx, eax
        movzx eax, word [net_udp_dst_ip + 2]
        add edx, eax
        ; Zero + Protocol (0x0011 = UDP in big-endian: 0x1100)
        add edx, 0x1100
        ; UDP length (already in buffer at offset 4, big-endian)
        movzx eax, word [net_udp_buf + 4]
        add edx, eax

        ; Sum UDP header + payload (16-bit words)
        mov esi, net_udp_buf
        mov ecx, [net_udp_payload_len]
        add ecx, 8             ; total UDP length
        shr ecx, 1             ; word count
        jz .udp_cksum_odd
.udp_cksum_loop:
        movzx eax, word [esi]
        add edx, eax
        add esi, 2
        dec ecx
        jnz .udp_cksum_loop
.udp_cksum_odd:
        ; Handle odd byte (RFC 1071: on little-endian, add byte as-is)
        mov ecx, [net_udp_payload_len]
        add ecx, 8
        test ecx, 1
        jz .udp_cksum_fold
        movzx eax, byte [esi]
        add edx, eax

.udp_cksum_fold:
        ; Fold 32-bit sum to 16 bits
        mov eax, edx
        shr eax, 16
        and edx, 0xFFFF
        add edx, eax
        ; Fold again in case of carry
        mov eax, edx
        shr eax, 16
        add edx, eax
        and edx, 0xFFFF

        ; One's complement
        not dx
        ; If checksum is 0, set to 0xFFFF (UDP convention)
        test dx, dx
        jnz .udp_cksum_ok
        mov dx, 0xFFFF
.udp_cksum_ok:
        mov [esp + 28], edx    ; Return in EAX via pushad frame
        popad
        ret


;=======================================================================
; SECTION 7: TCP
;=======================================================================

;---------------------------------------
; tcp_handle - Handle incoming TCP segment
;  ESI = TCP header, ECX = payload length (IP payload size)
;  [net_recv_src_ip] = source IP
;---------------------------------------
tcp_handle:
        pushad
        cmp ecx, 20
        jb .tcp_h_done

        ; Parse TCP header
        movzx eax, byte [esi]
        shl eax, 8
        mov al, [esi+1]
        mov [net_tcp_src_port], ax     ; source port (host order)

        movzx eax, byte [esi+2]
        shl eax, 8
        mov al, [esi+3]
        mov [net_tcp_dst_port], ax     ; dest port (host order)

        ; Sequence number (big-endian -> host)
        mov eax, [esi+4]
        bswap eax
        mov [net_tcp_their_seq], eax

        ; ACK number (big-endian -> host)
        mov eax, [esi+8]
        bswap eax
        mov [net_tcp_their_ack], eax

        ; Data offset (high nibble of byte 12) * 4 = header length
        movzx eax, byte [esi+12]
        shr eax, 4
        shl eax, 2
        mov [net_tcp_hdr_len], eax

        ; Flags (byte 13)
        movzx eax, byte [esi+13]
        mov [net_tcp_flags], al

        ; Find socket by dest port
        push esi
        push ecx
        movzx eax, word [net_tcp_dst_port]
        mov dl, SOCK_TCP
        call sock_find_by_port
        pop ecx
        pop esi
        cmp eax, -1
        je .tcp_h_done

        ; EAX = socket index
        mov ebp, eax
        imul ebp, SOCK_SIZE
        add ebp, net_sock_table

        ; Verify remote IP and port match (for connected sockets)
        cmp dword [ebp + SOCK_STATE], TCP_LISTEN
        je .tcp_try_child       ; v3.0: Try child socket first

        mov eax, [net_recv_src_ip]
        cmp [ebp + SOCK_REMOTE_IP], eax
        jne .tcp_h_done
        movzx eax, word [net_tcp_src_port]
        cmp [ebp + SOCK_REMOTE_PORT], ax
        jne .tcp_h_done

.tcp_dispatch_state:
        ; Dispatch by state
        mov eax, [ebp + SOCK_STATE]
        cmp eax, TCP_SYN_RCVD
        je .tcp_state_syn_rcvd
        cmp eax, TCP_SYN_SENT
        je .tcp_state_syn_sent
        cmp eax, TCP_ESTABLISHED
        je .tcp_state_established
        cmp eax, TCP_FIN_WAIT_1
        je .tcp_state_fin_wait_1
        cmp eax, TCP_FIN_WAIT_2
        je .tcp_state_fin_wait_2
        cmp eax, TCP_CLOSE_WAIT
        je .tcp_state_close_wait
        cmp eax, TCP_LAST_ACK
        je .tcp_state_last_ack
        jmp .tcp_h_done

; v3.0: When the found socket is LISTEN, first check if there's a
; child socket (SYN_RCVD/ESTABLISHED) matching the remote IP/port.
; This handles the ACK half of the 3-way handshake for spawned children.
.tcp_try_child:
        test byte [net_tcp_flags], TCP_SYN
        jnz .tcp_state_listen   ; New SYN → handle in LISTEN state
        ; Non-SYN packet → search for child socket matching src IP/port
        push esi
        push ecx
        mov edi, net_sock_table
        xor edx, edx
.tcp_child_scan:
        cmp edx, MAX_SOCKETS
        jge .tcp_child_not_found
        cmp dword [edi + SOCK_TYPE], SOCK_TCP
        jne .tcp_child_next
        ; Check if this socket's remote matches the packet's source
        mov eax, [net_recv_src_ip]
        cmp [edi + SOCK_REMOTE_IP], eax
        jne .tcp_child_next
        movzx eax, word [net_tcp_src_port]
        cmp [edi + SOCK_REMOTE_PORT], ax
        jne .tcp_child_next
        ; Check state is SYN_RCVD or ESTABLISHED (not LISTEN/CLOSED)
        cmp dword [edi + SOCK_STATE], TCP_SYN_RCVD
        je .tcp_child_found
        cmp dword [edi + SOCK_STATE], TCP_ESTABLISHED
        je .tcp_child_found
        cmp dword [edi + SOCK_STATE], TCP_FIN_WAIT_1
        je .tcp_child_found
        cmp dword [edi + SOCK_STATE], TCP_FIN_WAIT_2
        je .tcp_child_found
        cmp dword [edi + SOCK_STATE], TCP_CLOSE_WAIT
        je .tcp_child_found
        cmp dword [edi + SOCK_STATE], TCP_LAST_ACK
        je .tcp_child_found
.tcp_child_next:
        add edi, SOCK_SIZE
        inc edx
        jmp .tcp_child_scan
.tcp_child_found:
        ; Use child socket instead
        mov ebp, edi
        pop ecx
        pop esi
        jmp .tcp_dispatch_state
.tcp_child_not_found:
        pop ecx
        pop esi
        jmp .tcp_h_done

;---------------------------------------
; v3.0: SYN_RCVD state — awaiting final ACK of 3-way handshake.
; When ACK is received, transition to ESTABLISHED.
;---------------------------------------
.tcp_state_syn_rcvd:
        ; Check for RST
        test byte [net_tcp_flags], TCP_RST
        jnz .tcp_rst_close
        ; Check for ACK (completing 3-way handshake)
        test byte [net_tcp_flags], TCP_ACK
        jz .tcp_h_done
        ; Verify ACK number matches our SEQ
        mov eax, [net_tcp_their_ack]
        cmp eax, [ebp + SOCK_SEQ]
        jb .tcp_h_done          ; Stale ACK
        ; Transition to ESTABLISHED
        mov dword [ebp + SOCK_STATE], TCP_ESTABLISHED
        ; Update sequence tracking
        mov eax, [net_tcp_their_seq]
        mov [ebp + SOCK_ACK], eax
        jmp .tcp_h_done

.tcp_state_listen:
        ; Received SYN?
        test byte [net_tcp_flags], TCP_SYN
        jz .tcp_h_done

        ; v3.0: Allocate a child socket for this connection
        ; so the parent stays in LISTEN for more connections.
        push esi
        push ecx

        ; Save parent socket index
        mov eax, ebp
        sub eax, net_sock_table
        push eax                ; save parent offset

        ; Allocate new TCP socket
        mov eax, SOCK_TCP
        call sock_alloc
        cmp eax, -1
        je .tcp_listen_no_slot

        ; EAX = child socket index
        mov ecx, eax
        imul eax, SOCK_SIZE
        lea edi, [net_sock_table + eax]

        ; Copy local port from parent
        mov ax, [ebp + SOCK_LOCAL_PORT]
        mov [edi + SOCK_LOCAL_PORT], ax

        ; Store remote info in child socket
        mov eax, [net_recv_src_ip]
        mov [edi + SOCK_REMOTE_IP], eax
        movzx eax, word [net_tcp_src_port]
        mov [edi + SOCK_REMOTE_PORT], ax

        ; Set initial ACK = their seq + 1
        mov eax, [net_tcp_their_seq]
        inc eax
        mov [edi + SOCK_ACK], eax

        ; Initialize sequence number
        mov eax, [tick_count]
        mov [edi + SOCK_SEQ], eax

        ; Store parent reference
        pop eax                 ; parent offset
        push eax
        ; Convert offset to index
        xor edx, edx
        push ecx
        mov ecx, SOCK_SIZE
        div ecx
        pop ecx
        mov [edi + SOCK_PARENT], eax

        ; Child enters SYN_RCVD and defers SYN-ACK
        mov dword [edi + SOCK_STATE], TCP_SYN_RCVD
        mov byte [edi + SOCK_PENDING], TCP_SYN | TCP_ACK

        ; Parent stays in LISTEN
        pop eax                 ; discard parent offset
        pop ecx
        pop esi
        jmp .tcp_h_done

.tcp_listen_no_slot:
        ; No free socket — drop the SYN
        pop eax
        pop ecx
        pop esi
        jmp .tcp_h_done

.tcp_state_syn_sent:
        ; Expect SYN-ACK
        mov al, [net_tcp_flags]
        and al, TCP_SYN | TCP_ACK
        cmp al, TCP_SYN | TCP_ACK
        jne .tcp_check_rst
        ; Verify ACK matches our SEQ+1
        mov eax, [net_tcp_their_ack]
        mov ebx, [ebp + SOCK_SEQ]
        cmp eax, ebx
        jne .tcp_h_done
        ; Set our ACK = their seq + 1
        mov eax, [net_tcp_their_seq]
        inc eax
        mov [ebp + SOCK_ACK], eax
        ; Move to ESTABLISHED
        mov dword [ebp + SOCK_STATE], TCP_ESTABLISHED
        ; Defer ACK to non-ISR context (TX re-entrance fix)
        mov byte [ebp + SOCK_PENDING], TCP_ACK
        jmp .tcp_h_done

.tcp_check_rst:
        test byte [net_tcp_flags], TCP_RST
        jz .tcp_h_done
        mov dword [ebp + SOCK_STATE], TCP_CLOSED
        jmp .tcp_h_done

.tcp_state_established:
        ; Check for RST
        test byte [net_tcp_flags], TCP_RST
        jnz .tcp_rst_close
        ; Check for FIN
        test byte [net_tcp_flags], TCP_FIN
        jnz .tcp_recv_fin

        ; Handle incoming data
        test byte [net_tcp_flags], TCP_ACK
        jz .tcp_h_done

        ; Update our SEQ based on their ACK (data they acknowledged)
        mov eax, [net_tcp_their_ack]
        mov [ebp + SOCK_SEQ], eax

        ; Clear retransmission state if they ACKed our data
        cmp dword [ebp + SOCK_RETX_LEN], 0
        je .tcp_retx_clear_done
        mov edx, [ebp + SOCK_RETX_SEQ]
        add edx, [ebp + SOCK_RETX_LEN]
        cmp eax, edx
        jb .tcp_retx_clear_done
        ; All data ACKed, cancel retransmit
        mov dword [ebp + SOCK_RETX_LEN], 0
.tcp_retx_clear_done:

        ; Calculate payload: total TCP segment - TCP header
        mov eax, [net_tcp_hdr_len]
        sub ecx, eax
        jle .tcp_h_done         ; No data

        ; Copy payload to recv buffer
        mov edx, [ebp + SOCK_RECV_BUF]
        test edx, edx
        jz .tcp_h_done
        mov ebx, [ebp + SOCK_RECV_LEN]
        lea edi, [edx + ebx]

        ; Check space
        mov edx, SOCK_BUF_SIZE
        sub edx, ebx
        cmp ecx, edx
        jle .tcp_copy_ok
        mov ecx, edx            ; Truncate to available space
.tcp_copy_ok:
        push ecx
        push esi
        add esi, [net_tcp_hdr_len]
        cld
        rep movsb
        pop esi
        pop ecx
        add [ebp + SOCK_RECV_LEN], ecx

        ; Update our ACK
        add [ebp + SOCK_ACK], ecx

        ; Defer ACK to non-ISR context (TX re-entrance fix)
        mov byte [ebp + SOCK_PENDING], TCP_ACK
        jmp .tcp_h_done

.tcp_recv_fin:
        ; Advance ACK past payload + FIN
        mov eax, [net_tcp_hdr_len]
        sub ecx, eax
        jl .tcp_fin_no_data
        add [ebp + SOCK_ACK], ecx
.tcp_fin_no_data:
        inc dword [ebp + SOCK_ACK]    ; ACK the FIN
        mov dword [ebp + SOCK_STATE], TCP_CLOSE_WAIT
        ; Defer ACK for FIN to non-ISR context (TX re-entrance fix)
        mov byte [ebp + SOCK_PENDING], TCP_ACK
        jmp .tcp_h_done

.tcp_rst_close:
        mov dword [ebp + SOCK_STATE], TCP_CLOSED
        jmp .tcp_h_done

.tcp_state_fin_wait_1:
        test byte [net_tcp_flags], TCP_ACK
        jz .tcp_h_done
        test byte [net_tcp_flags], TCP_FIN
        jnz .tcp_simul_close
        ; Got ACK of our FIN
        mov dword [ebp + SOCK_STATE], TCP_FIN_WAIT_2
        jmp .tcp_h_done

.tcp_simul_close:
        ; FIN+ACK: simultaneous close
        inc dword [ebp + SOCK_ACK]
        mov dword [ebp + SOCK_STATE], TCP_TIME_WAIT
        ; Defer ACK to non-ISR context (TX re-entrance fix)
        mov byte [ebp + SOCK_PENDING], TCP_ACK
        ; Set timer for TIME_WAIT (2 seconds)
        mov eax, [tick_count]
        add eax, 200
        mov [ebp + SOCK_TIMER], eax
        jmp .tcp_h_done

.tcp_state_fin_wait_2:
        test byte [net_tcp_flags], TCP_FIN
        jz .tcp_h_done
        inc dword [ebp + SOCK_ACK]
        mov dword [ebp + SOCK_STATE], TCP_TIME_WAIT
        ; Defer ACK to non-ISR context (TX re-entrance fix)
        mov byte [ebp + SOCK_PENDING], TCP_ACK
        mov eax, [tick_count]
        add eax, 200
        mov [ebp + SOCK_TIMER], eax
        jmp .tcp_h_done

.tcp_state_close_wait:
        ; User hasn't called close yet, just ACK
        jmp .tcp_h_done

.tcp_state_last_ack:
        test byte [net_tcp_flags], TCP_ACK
        jz .tcp_h_done
        mov dword [ebp + SOCK_STATE], TCP_CLOSED
        jmp .tcp_h_done

.tcp_h_done:
        popad
        ret

;---------------------------------------
; tcp_flush_pending - Send deferred TCP flags from non-ISR context
;  EBP = pointer to socket in net_sock_table
; Must only be called from normal (non-ISR) context.
;---------------------------------------
tcp_flush_pending:
        mov al, [ebp + SOCK_PENDING]
        test al, al
        jz .tfp_no_pending
        mov byte [ebp + SOCK_PENDING], 0
        call tcp_send_flags
.tfp_no_pending:
        ; Check retransmission timer
        cmp dword [ebp + SOCK_RETX_LEN], 0
        je .tfp_none
        cmp dword [ebp + SOCK_STATE], TCP_ESTABLISHED
        jne .tfp_none
        mov eax, [tick_count]
        cmp eax, [ebp + SOCK_RETX_TIMER]
        jb .tfp_none
        ; Timer expired — retransmit (max 3 attempts)
        cmp dword [ebp + SOCK_RETX_COUNT], 3
        jge .tfp_give_up
        inc dword [ebp + SOCK_RETX_COUNT]
        ; Reset SEQ to retransmit position
        mov eax, [ebp + SOCK_RETX_SEQ]
        mov [ebp + SOCK_SEQ], eax
        ; Resend from send buffer
        mov esi, [ebp + SOCK_SEND_BUF]
        test esi, esi
        jz .tfp_none
        mov ecx, [ebp + SOCK_RETX_LEN]
        call tcp_send_data
        ; Double the timeout (exponential backoff: 3s, 6s, 12s)
        mov eax, [tick_count]
        mov ecx, [ebp + SOCK_RETX_COUNT]
        mov edx, 300              ; base timeout
        shl edx, cl               ; 300 << count
        add eax, edx
        mov [ebp + SOCK_RETX_TIMER], eax
        jmp .tfp_none
.tfp_give_up:
        ; Too many retransmissions — clear state (connection likely dead)
        mov dword [ebp + SOCK_RETX_LEN], 0
.tfp_none:
        ret

;---------------------------------------
; tcp_send_flags - Send TCP segment with flags
;  EBP = pointer to socket in net_sock_table
;  AL = TCP flags to send
;  Sends with current SEQ/ACK, no data
;---------------------------------------
tcp_send_flags:
        pushad
        mov [net_tcp_send_flags], al

        mov edi, net_tcp_buf

        ; Source port (big-endian)
        movzx eax, word [ebp + SOCK_LOCAL_PORT]
        xchg al, ah
        mov [edi], ax
        ; Dest port (big-endian)
        movzx eax, word [ebp + SOCK_REMOTE_PORT]
        xchg al, ah
        mov [edi+2], ax
        ; Sequence number (big-endian)
        mov eax, [ebp + SOCK_SEQ]
        bswap eax
        mov [edi+4], eax
        ; ACK number
        mov eax, [ebp + SOCK_ACK]
        bswap eax
        mov [edi+8], eax
        ; Data offset = 5 words (20 bytes), reserved = 0
        mov byte [edi+12], 0x50
        ; Flags
        mov al, [net_tcp_send_flags]
        mov [edi+13], al
        ; Window size = 4096 (big-endian)
        mov word [edi+14], 0x0010    ; 4096 = 0x1000, BE = 0x0010
        ; Checksum placeholder
        mov word [edi+16], 0
        ; Urgent pointer
        mov word [edi+18], 0

        ; Compute TCP checksum with pseudo-header
        mov esi, edi
        mov ecx, 20            ; TCP header length
        mov eax, [ebp + SOCK_REMOTE_IP]
        call tcp_checksum
        mov [edi+16], ax

        ; If SYN flag set, increment SEQ for SYN
        test byte [net_tcp_send_flags], TCP_SYN
        jz .tcp_sf_no_syn
        inc dword [ebp + SOCK_SEQ]
.tcp_sf_no_syn:
        ; If FIN flag set, increment SEQ for FIN
        test byte [net_tcp_send_flags], TCP_FIN
        jz .tcp_sf_no_fin
        inc dword [ebp + SOCK_SEQ]
.tcp_sf_no_fin:

        ; Send via IP
        mov esi, net_tcp_buf
        mov ecx, 20
        mov eax, [ebp + SOCK_REMOTE_IP]
        mov dl, IP_PROTO_TCP
        call ip_send
        popad
        ret

;---------------------------------------
; tcp_send_data - Send TCP data segment
;  EBP = socket pointer
;  ESI = data, ECX = data length
;---------------------------------------
tcp_send_data:
        pushad
        mov [net_tcp_data_ptr], esi
        mov [net_tcp_data_len], ecx

        ; Save data to send buffer for retransmission
        mov edi, [ebp + SOCK_SEND_BUF]
        test edi, edi
        jz .tsd_no_save
        push ecx
        push esi
        cmp ecx, SOCK_BUF_SIZE
        jle .tsd_save_ok
        mov ecx, SOCK_BUF_SIZE
.tsd_save_ok:
        cld
        rep movsb
        pop esi
        pop ecx
        ; Record retransmission state
        mov eax, [ebp + SOCK_SEQ]
        mov [ebp + SOCK_RETX_SEQ], eax
        mov [ebp + SOCK_RETX_LEN], ecx
        mov dword [ebp + SOCK_RETX_COUNT], 0
        ; Set retransmit timer: 3 seconds (300 ticks)
        mov eax, [tick_count]
        add eax, 300
        mov [ebp + SOCK_RETX_TIMER], eax
.tsd_no_save:

        mov edi, net_tcp_buf

        ; Source port
        movzx eax, word [ebp + SOCK_LOCAL_PORT]
        xchg al, ah
        mov [edi], ax
        ; Dest port
        movzx eax, word [ebp + SOCK_REMOTE_PORT]
        xchg al, ah
        mov [edi+2], ax
        ; Sequence number
        mov eax, [ebp + SOCK_SEQ]
        bswap eax
        mov [edi+4], eax
        ; ACK number
        mov eax, [ebp + SOCK_ACK]
        bswap eax
        mov [edi+8], eax
        ; Data offset = 5 (20 bytes)
        mov byte [edi+12], 0x50
        ; Flags = PSH|ACK
        mov byte [edi+13], TCP_PSH | TCP_ACK
        ; Window
        mov word [edi+14], 0x0010
        ; Checksum placeholder
        mov word [edi+16], 0
        ; Urgent
        mov word [edi+18], 0

        ; Copy data after header
        push edi
        add edi, 20
        mov esi, [net_tcp_data_ptr]
        mov ecx, [net_tcp_data_len]
        cld
        rep movsb
        pop edi

        ; Compute TCP checksum (header + data)
        mov esi, edi
        mov ecx, [net_tcp_data_len]
        add ecx, 20
        mov eax, [ebp + SOCK_REMOTE_IP]
        call tcp_checksum
        mov [edi+16], ax

        ; Advance our SEQ by data length
        mov eax, [net_tcp_data_len]
        add [ebp + SOCK_SEQ], eax

        ; Send via IP
        mov esi, net_tcp_buf
        mov ecx, [net_tcp_data_len]
        add ecx, 20
        mov eax, [ebp + SOCK_REMOTE_IP]
        mov dl, IP_PROTO_TCP
        call ip_send
        popad
        ret

;---------------------------------------
; tcp_checksum - Compute TCP checksum with pseudo-header
;  ESI = TCP segment (header + data)
;  ECX = total TCP length
;  EAX = destination IP (little-endian)
; Returns: AX = checksum
;---------------------------------------
tcp_checksum:
        push ebx
        push ecx
        push edx
        push esi
        push edi

        mov [net_tcp_ck_dst], eax
        mov [net_tcp_ck_len], ecx

        ; Build pseudo-header + TCP in net_tcp_ck_buf
        mov edi, net_tcp_ck_buf
        ; Source IP
        mov eax, [net_ip]
        stosd
        ; Dest IP
        mov eax, [net_tcp_ck_dst]
        stosd
        ; Zero + Protocol(6)
        xor al, al
        stosb
        mov al, IP_PROTO_TCP
        stosb
        ; TCP length (big-endian)
        mov eax, [net_tcp_ck_len]
        xchg al, ah
        stosw

        ; Copy TCP segment
        mov ecx, [net_tcp_ck_len]
        cld
        rep movsb

        ; Compute checksum over pseudo-header + TCP
        mov esi, net_tcp_ck_buf
        mov ecx, [net_tcp_ck_len]
        add ecx, 12             ; pseudo-header is 12 bytes
        call inet_checksum

        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret


;=======================================================================
; SECTION 8: DHCP CLIENT
;=======================================================================

;---------------------------------------
; dhcp_discover - Send DHCPDISCOVER
;---------------------------------------
dhcp_discover:
        pushad
        mov edi, net_dhcp_buf

        ; Zero 300 bytes for basic DHCP packet
        push edi
        mov ecx, 75             ; 75 dwords = 300 bytes
        xor eax, eax
        cld
        rep stosd
        pop edi

        ; op=1 (BOOTREQUEST), htype=1 (Ethernet), hlen=6, hops=0
        mov byte [edi], 1
        mov byte [edi+1], 1
        mov byte [edi+2], 6
        mov byte [edi+3], 0

        ; XID (transaction ID) - use tick count
        mov eax, [tick_count]
        mov [edi+4], eax
        mov [net_dhcp_xid], eax

        ; Flags = 0x8000 (broadcast)
        mov byte [edi+10], 0x80
        mov byte [edi+11], 0x00

        ; chaddr = our MAC (at offset 28, 16 bytes padded)
        mov esi, net_mac
        lea edi, [edi + 28]
        mov ecx, 6
        rep movsb

        ; Move to options (offset 236)
        mov edi, net_dhcp_buf
        add edi, 236

        ; Magic cookie: 99.130.83.99
        mov byte [edi], 99
        mov byte [edi+1], 130
        mov byte [edi+2], 83
        mov byte [edi+3], 99
        add edi, 4

        ; Option 53: DHCP Message Type = 1 (Discover)
        mov byte [edi], 53
        mov byte [edi+1], 1
        mov byte [edi+2], DHCP_DISCOVER
        add edi, 3

        ; Option 55: Parameter Request List
        mov byte [edi], 55
        mov byte [edi+1], 3
        mov byte [edi+2], 1      ; Subnet mask
        mov byte [edi+3], 3      ; Router
        mov byte [edi+4], 6      ; DNS
        add edi, 5

        ; End option
        mov byte [edi], 255
        inc edi

        ; Calculate packet size
        mov ecx, edi
        sub ecx, net_dhcp_buf

        ; Pad to minimum 300 bytes
        cmp ecx, 300
        jge .dhcp_send
        mov ecx, 300

.dhcp_send:
        ; Send via UDP: src=0.0.0.0:68 dst=255.255.255.255:67
        mov esi, net_dhcp_buf
        mov eax, BROADCAST_IP
        mov bx, DHCP_SERVER_PORT
        mov dx, DHCP_CLIENT_PORT

        ; For DHCP discover, we need to send with source IP 0.0.0.0
        ; Temporarily set our IP to 0
        ; Set state BEFORE sending to avoid race: QEMU may deliver the
        ; OFFER response (and fire the RX interrupt) before udp_send returns.
        mov byte [net_dhcp_state], 1  ; Discover sent
        push dword [net_ip]
        mov dword [net_ip], 0
        call udp_send
        pop dword [net_ip]

        popad
        ret

;---------------------------------------
; dhcp_handle - Handle incoming DHCP packet
;  ESI = DHCP payload (UDP data)
;  ECX = length
;---------------------------------------
dhcp_handle:
        pushad
        cmp ecx, 240
        jb .dhcp_done

        ; Check op=2 (reply) and our XID
        cmp byte [esi], 2
        jne .dhcp_done
        mov eax, [esi+4]
        cmp eax, [net_dhcp_xid]
        jne .dhcp_done

        ; Get offered IP (yiaddr at offset 16)
        mov eax, [esi + 16]
        mov [net_dhcp_offered_ip], eax

        ; Get server IP (siaddr at offset 20)
        mov eax, [esi + 20]
        mov [net_dhcp_server_ip], eax

        ; Parse options (start at offset 240, after magic cookie at 236)
        lea edi, [esi + 240]
        mov ebx, ecx
        sub ebx, 240

.dhcp_parse_opts:
        cmp ebx, 0
        jle .dhcp_opts_done
        movzx eax, byte [edi]
        cmp eax, 255            ; End
        je .dhcp_opts_done
        cmp eax, 0              ; Pad
        je .dhcp_opt_pad

        ; Option: type(1) length(1) data(length)
        movzx ecx, byte [edi+1]

        cmp al, 53              ; DHCP message type
        je .dhcp_opt_type
        cmp al, 1               ; Subnet mask
        je .dhcp_opt_mask
        cmp al, 3               ; Router
        je .dhcp_opt_router
        cmp al, 6               ; DNS server
        je .dhcp_opt_dns
        cmp al, 54              ; Server identifier
        je .dhcp_opt_server_id

        ; Skip unknown option
        add edi, ecx
        add edi, 2
        sub ebx, ecx
        sub ebx, 2
        jmp .dhcp_parse_opts

.dhcp_opt_pad:
        inc edi
        dec ebx
        jmp .dhcp_parse_opts

.dhcp_opt_type:
        mov al, [edi+2]
        mov [net_dhcp_msg_type], al
        add edi, ecx
        add edi, 2
        sub ebx, ecx
        sub ebx, 2
        jmp .dhcp_parse_opts

.dhcp_opt_mask:
        mov eax, [edi+2]
        mov [net_dhcp_mask], eax
        add edi, ecx
        add edi, 2
        sub ebx, ecx
        sub ebx, 2
        jmp .dhcp_parse_opts

.dhcp_opt_router:
        mov eax, [edi+2]
        mov [net_dhcp_gateway], eax
        add edi, ecx
        add edi, 2
        sub ebx, ecx
        sub ebx, 2
        jmp .dhcp_parse_opts

.dhcp_opt_dns:
        mov eax, [edi+2]
        mov [net_dhcp_dns], eax
        add edi, ecx
        add edi, 2
        sub ebx, ecx
        sub ebx, 2
        jmp .dhcp_parse_opts

.dhcp_opt_server_id:
        mov eax, [edi+2]
        mov [net_dhcp_server_ip], eax
        add edi, ecx
        add edi, 2
        sub ebx, ecx
        sub ebx, 2
        jmp .dhcp_parse_opts

.dhcp_opts_done:
        ; Dispatch based on message type
        cmp byte [net_dhcp_msg_type], DHCP_OFFER
        je .dhcp_got_offer
        cmp byte [net_dhcp_msg_type], DHCP_ACK
        je .dhcp_got_ack
        jmp .dhcp_done

.dhcp_got_offer:
        cmp byte [net_dhcp_state], 1   ; Only if we sent Discover
        jne .dhcp_done
        ; Signal that we got an offer; the polling loop will send the REQUEST
        ; (We must NOT send from inside the ISR — TX re-entrance on the same
        ;  descriptor doesn't work under QEMU's RTL8139 emulation.)
        mov byte [net_dhcp_state], 2   ; Offer received
        jmp .dhcp_done

.dhcp_got_ack:
        cmp byte [net_dhcp_state], 3   ; Only if we sent Request
        jne .dhcp_done
        ; Apply configuration
        mov eax, [net_dhcp_offered_ip]
        mov [net_ip], eax
        mov eax, [net_dhcp_mask]
        test eax, eax
        jz .dhcp_no_mask
        mov [net_mask], eax
.dhcp_no_mask:
        mov eax, [net_dhcp_gateway]
        test eax, eax
        jz .dhcp_no_gw
        mov [net_gateway], eax
.dhcp_no_gw:
        mov eax, [net_dhcp_dns]
        test eax, eax
        jz .dhcp_no_dns
        mov [net_dns], eax
.dhcp_no_dns:
        mov byte [net_dhcp_state], 4   ; Configured
        jmp .dhcp_done

.dhcp_done:
        popad
        ret

;---------------------------------------
; dhcp_request - Send DHCPREQUEST
;---------------------------------------
dhcp_request:
        pushad
        mov edi, net_dhcp_buf

        ; Zero 300 bytes for DHCP packet
        push edi
        mov ecx, 75             ; 75 dwords = 300 bytes
        xor eax, eax
        cld
        rep stosd
        pop edi

        ; op=1, htype=1, hlen=6
        mov byte [edi], 1
        mov byte [edi+1], 1
        mov byte [edi+2], 6
        ; XID
        mov eax, [net_dhcp_xid]
        mov [edi+4], eax
        ; Flags = broadcast
        mov byte [edi+10], 0x80
        ; chaddr
        mov esi, net_mac
        lea edi, [edi + 28]
        mov ecx, 6
        rep movsb

        ; Options at offset 236
        mov edi, net_dhcp_buf
        add edi, 236
        ; Magic cookie
        mov byte [edi], 99
        mov byte [edi+1], 130
        mov byte [edi+2], 83
        mov byte [edi+3], 99
        add edi, 4

        ; Option 53: DHCP Message Type = 3 (Request)
        mov byte [edi], 53
        mov byte [edi+1], 1
        mov byte [edi+2], DHCP_REQUEST
        add edi, 3

        ; Option 50: Requested IP Address
        mov byte [edi], 50
        mov byte [edi+1], 4
        mov eax, [net_dhcp_offered_ip]
        mov [edi+2], eax
        add edi, 6

        ; Option 54: Server Identifier
        mov byte [edi], 54
        mov byte [edi+1], 4
        mov eax, [net_dhcp_server_ip]
        mov [edi+2], eax
        add edi, 6

        ; End
        mov byte [edi], 255

        ; Send
        ; Set state BEFORE sending to avoid race with fast ACK response
        mov byte [net_dhcp_state], 3   ; Request sent
        mov esi, net_dhcp_buf
        mov ecx, 300
        mov eax, BROADCAST_IP
        mov bx, DHCP_SERVER_PORT
        mov dx, DHCP_CLIENT_PORT
        push dword [net_ip]
        mov dword [net_ip], 0
        call udp_send
        pop dword [net_ip]

        popad
        ret


;=======================================================================
; SECTION 9: DNS RESOLVER
;=======================================================================

;---------------------------------------
; dns_resolve - Resolve hostname to IP via DNS
;  ESI = null-terminated hostname
; Returns: EAX = IP address (0 on failure)
; Blocks up to ~5 seconds (two 2.5s attempts with retry)
;---------------------------------------
dns_resolve:
        push ebx
        push ecx
        push edx
        push esi
        push edi

        ; Check DNS cache first
        call dns_cache_lookup
        test eax, eax
        jnz .dns_cached

        ; Fail immediately if NIC is not initialised
        cmp dword [net_state], NET_STATE_UP
        jne .dns_fail

        ; Build DNS query
        mov [net_dns_query_name], esi
        mov edi, net_dns_buf

        ; Transaction ID
        mov ax, [net_dns_id]
        inc word [net_dns_id]
        xchg al, ah
        mov [edi], ax
        ; Flags: standard query, recursion desired
        mov byte [edi+2], 0x01
        mov byte [edi+3], 0x00
        ; Questions = 1
        mov byte [edi+4], 0
        mov byte [edi+5], 1
        ; Answers = 0, Authority = 0, Additional = 0
        mov dword [edi+6], 0
        mov word [edi+10], 0
        add edi, 12

        ; Encode hostname as DNS name (labels)
        mov esi, [net_dns_query_name]
.dns_encode:
        mov ebx, edi            ; save label length position
        inc edi                 ; skip length byte
        xor ecx, ecx
.dns_char:
        lodsb
        cmp al, '.'
        je .dns_label_end
        cmp al, 0
        je .dns_name_end
        stosb
        inc ecx
        jmp .dns_char
.dns_label_end:
        mov [ebx], cl           ; write label length
        jmp .dns_encode
.dns_name_end:
        mov [ebx], cl
        mov byte [edi], 0       ; root label
        inc edi
        ; Type = A (1), big-endian
        mov byte [edi], 0
        mov byte [edi+1], 1
        ; Class = IN (1), big-endian
        mov byte [edi+2], 0
        mov byte [edi+3], 1
        add edi, 4

        ; Calculate and save query length for retry
        mov ecx, edi
        sub ecx, net_dns_buf
        mov [net_dns_query_len], cx

        ; --- ATTEMPT 1 ---
        mov byte [net_dns_resolved], 0
        mov dword [net_dns_result_ip], 0
        mov esi, net_dns_buf
        mov eax, [net_dns]
        mov bx, DNS_PORT
        mov dx, [net_ephemeral_port]
        inc word [net_ephemeral_port]
        movzx ecx, word [net_dns_query_len]
        call udp_send

        ; Wait up to 2.5 seconds for first response
        mov ecx, 250
.dns_wait1:
        hlt
        cmp byte [net_dns_resolved], 1
        je .dns_got_response
        dec ecx
        jnz .dns_wait1

        ; --- ATTEMPT 2: retry with a fresh transaction ID ---
        mov ax, [net_dns_id]
        inc word [net_dns_id]
        xchg al, ah
        mov word [net_dns_buf], ax    ; patch TxID in already-built query

        mov byte [net_dns_resolved], 0
        mov dword [net_dns_result_ip], 0
        mov esi, net_dns_buf
        mov eax, [net_dns]
        mov bx, DNS_PORT
        mov dx, [net_ephemeral_port]
        inc word [net_ephemeral_port]
        movzx ecx, word [net_dns_query_len]
        call udp_send

        ; Wait up to 2.5 seconds for retry response
        mov ecx, 250
.dns_wait2:
        hlt
        cmp byte [net_dns_resolved], 1
        je .dns_got_response
        dec ecx
        jnz .dns_wait2

.dns_fail:
        ; All attempts timed out (or NIC not up)
        xor eax, eax
        jmp .dns_return

.dns_got_response:
        mov eax, [net_dns_result_ip]
        ; Cache the result
        push eax
        mov esi, [net_dns_query_name]
        call dns_cache_add
        pop eax

.dns_return:
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret

.dns_cached:
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret

;---------------------------------------
; dns_handle_response - Handle DNS response
;  ESI = DNS response data, ECX = length
;---------------------------------------
dns_handle_response:
        pushad
        cmp ecx, 12
        jb .dns_hr_done

        ; Check it's a response (QR bit set in flags)
        test byte [esi+2], 0x80
        jz .dns_hr_done

        ; Get answer count
        movzx eax, byte [esi+6]
        shl eax, 8
        mov al, [esi+7]
        test eax, eax
        jz .dns_hr_done

        ; Skip header (12 bytes) and question section
        lea edi, [esi + 12]
        mov ebx, ecx
        sub ebx, 12

        ; Skip question name
.dns_skip_name:
        cmp ebx, 0
        jle .dns_hr_done
        movzx eax, byte [edi]
        test eax, eax
        jz .dns_name_skipped
        test eax, 0xC0          ; Pointer?
        jnz .dns_name_ptr
        add edi, eax
        inc edi
        sub ebx, eax
        dec ebx
        jmp .dns_skip_name
.dns_name_ptr:
        add edi, 2
        sub ebx, 2
        jmp .dns_skip_qtype
.dns_name_skipped:
        inc edi
        dec ebx
.dns_skip_qtype:
        ; Skip QTYPE(2) + QCLASS(2)
        add edi, 4
        sub ebx, 4

        ; Parse answer records
.dns_parse_answer:
        cmp ebx, 12
        jb .dns_hr_done

        ; Skip answer name (may be pointer)
        movzx eax, byte [edi]
        test eax, 0xC0
        jnz .dns_ans_ptr
        ; Walk labels
.dns_ans_walk:
        movzx eax, byte [edi]
        test eax, eax
        jz .dns_ans_name_done
        add edi, eax
        inc edi
        jmp .dns_ans_walk
.dns_ans_name_done:
        inc edi
        jmp .dns_ans_fields
.dns_ans_ptr:
        add edi, 2

.dns_ans_fields:
        ; Type(2) Class(2) TTL(4) RDLength(2) RData(RDLength)
        movzx eax, byte [edi]
        shl eax, 8
        mov al, [edi+1]        ; Type
        cmp eax, 1              ; A record?
        jne .dns_skip_rr

        ; Read RDLength
        movzx ecx, byte [edi+8]
        shl ecx, 8
        mov cl, [edi+9]
        cmp ecx, 4
        jne .dns_skip_rr

        ; Got an A record! Read IP
        mov eax, [edi+10]
        mov [net_dns_result_ip], eax
        mov byte [net_dns_resolved], 1
        jmp .dns_hr_done

.dns_skip_rr:
        movzx ecx, byte [edi+8]
        shl ecx, 8
        mov cl, [edi+9]
        add edi, 10
        add edi, ecx
        sub ebx, 10
        sub ebx, ecx
        jmp .dns_parse_answer

.dns_hr_done:
        popad
        ret

;---------------------------------------
; dns_cache_lookup - Check DNS cache
;  ESI = hostname
; Returns: EAX = IP (0 if not found)
;---------------------------------------
dns_cache_lookup:
        pushad
        mov edi, net_dns_cache
        mov ecx, 8              ; DNS_CACHE_SIZE
.dns_cl:
        cmp dword [edi + 28], 0 ; IP field
        je .dns_cl_next
        ; Compare hostname
        push esi
        push edi
        push ecx
        mov ecx, 28
.dns_cl_cmp:
        mov al, [esi]
        mov bl, [edi]
        cmp al, bl
        jne .dns_cl_mismatch
        test al, al
        jz .dns_cl_match
        inc esi
        inc edi
        dec ecx
        jnz .dns_cl_cmp
.dns_cl_match:
        pop ecx
        pop edi
        pop esi
        mov eax, [edi + 28]
        mov [esp + 28], eax     ; return in EAX
        popad
        ret
.dns_cl_mismatch:
        pop ecx
        pop edi
        pop esi
.dns_cl_next:
        add edi, 32             ; DNS_ENTRY_SIZE
        dec ecx
        jnz .dns_cl
        ; Not found
        xor eax, eax
        mov [esp + 28], eax
        popad
        ret

;---------------------------------------
; dns_cache_add - Add entry to DNS cache
;  ESI = hostname, EAX = IP
;---------------------------------------
dns_cache_add:
        pushad
        mov ebx, eax
        ; Find free slot or overwrite oldest
        mov edi, net_dns_cache
        mov ecx, 8
.dns_ca_find:
        cmp dword [edi + 28], 0
        je .dns_ca_write
        add edi, 32
        dec ecx
        jnz .dns_ca_find
        ; Overwrite first entry
        mov edi, net_dns_cache
.dns_ca_write:
        ; Copy hostname (up to 27 chars + null)
        push ebx
        mov ecx, 27
.dns_ca_copy:
        lodsb
        stosb
        test al, al
        jz .dns_ca_pad
        dec ecx
        jnz .dns_ca_copy
        mov byte [edi], 0
        inc edi
.dns_ca_pad:
        ; Pad remaining with zeros
        test ecx, ecx
        jz .dns_ca_ip
        xor al, al
        rep stosb
.dns_ca_ip:
        pop ebx
        mov [edi], ebx          ; IP address
        popad
        ret


;=======================================================================
; SECTION 10: SOCKET API
;=======================================================================

;---------------------------------------
; sock_alloc - Allocate a socket
;  EAX = type (SOCK_TCP or SOCK_UDP)
; Returns: EAX = socket index (0-7), or -1 on failure
;---------------------------------------
sock_alloc:
        push ebx
        push ecx
        push edi
        mov ebx, eax

        mov edi, net_sock_table
        xor ecx, ecx
.sa_find:
        cmp ecx, MAX_SOCKETS
        jge .sa_fail
        cmp dword [edi + SOCK_TYPE], 0
        je .sa_found
        add edi, SOCK_SIZE
        inc ecx
        jmp .sa_find

.sa_found:
        ; Clear the socket structure
        push ecx
        push edi
        push ebx
        mov ecx, SOCK_SIZE / 4
        xor eax, eax
        cld
        rep stosd
        pop ebx
        pop edi
        pop ecx

        ; Set type
        mov [edi + SOCK_TYPE], ebx

        ; Allocate recv buffer
        push ecx
        call pmm_alloc_page
        pop ecx
        test eax, eax
        jz .sa_fail
        mov [edi + SOCK_RECV_BUF], eax

        ; Allocate send buffer (TCP only)
        cmp ebx, SOCK_TCP
        jne .sa_no_send_buf
        push ecx
        call pmm_alloc_page
        pop ecx
        test eax, eax
        jz .sa_fail
        mov [edi + SOCK_SEND_BUF], eax
.sa_no_send_buf:

        mov eax, ecx
        pop edi
        pop ecx
        pop ebx
        ret

.sa_fail:
        mov eax, -1
        pop edi
        pop ecx
        pop ebx
        ret

;---------------------------------------
; sock_free - Free a socket
;  EAX = socket index
;---------------------------------------
sock_free:
        pushad
        cmp eax, MAX_SOCKETS
        jge .sf_done
        imul eax, SOCK_SIZE
        lea edi, [net_sock_table + eax]

        ; Free recv buffer
        mov eax, [edi + SOCK_RECV_BUF]
        test eax, eax
        jz .sf_no_recv
        call pmm_free_page
.sf_no_recv:
        ; Free send buffer
        mov eax, [edi + SOCK_SEND_BUF]
        test eax, eax
        jz .sf_no_send
        call pmm_free_page
.sf_no_send:

        ; Zero the structure
        mov ecx, SOCK_SIZE / 4
        xor eax, eax
        cld
        rep stosd

.sf_done:
        popad
        ret

;---------------------------------------
; sock_find_by_port - Find socket by local port and type
;  EAX = port (host byte order)
;  DL  = type (SOCK_TCP or SOCK_UDP)
; Returns: EAX = socket index, or -1 if not found
;---------------------------------------
sock_find_by_port:
        push ebx
        push ecx
        push edi
        mov ebx, eax
        movzx ecx, dl

        mov edi, net_sock_table
        xor eax, eax
.sfp_loop:
        cmp eax, MAX_SOCKETS
        jge .sfp_fail
        cmp [edi + SOCK_TYPE], ecx
        jne .sfp_next
        cmp [edi + SOCK_LOCAL_PORT], bx
        je .sfp_found
.sfp_next:
        add edi, SOCK_SIZE
        inc eax
        jmp .sfp_loop

.sfp_found:
        pop edi
        pop ecx
        pop ebx
        ret

.sfp_fail:
        mov eax, -1
        pop edi
        pop ecx
        pop ebx
        ret

;---------------------------------------
; net_alloc_port - Allocate ephemeral port
; Returns: AX = port number
;---------------------------------------
net_alloc_port:
        mov ax, [net_ephemeral_port]
        inc word [net_ephemeral_port]
        cmp word [net_ephemeral_port], 0
        jne .nap_ok
        mov word [net_ephemeral_port], EPHEMERAL_START
.nap_ok:
        ret


;=======================================================================
; SECTION 11: SYSCALL HANDLERS
;=======================================================================

;---------------------------------------
; sys_socket - Create socket
;  EBX = type (1=TCP, 2=UDP) -> EAX = fd (-1 error)
;---------------------------------------
sys_socket:
        mov eax, ebx
        call sock_alloc
        iretd

;---------------------------------------
; sys_connect - Connect to remote host
;  EBX = socket fd, ECX = IP (LE), EDX = port (host order)
; Returns: EAX = 0 success, -1 error
;---------------------------------------
sys_connect:
        pushad
        cmp ebx, MAX_SOCKETS
        jge .sconn_fail
        mov eax, ebx
        imul eax, SOCK_SIZE
        lea ebp, [net_sock_table + eax]

        cmp dword [ebp + SOCK_TYPE], SOCK_TCP
        jne .sconn_udp

        ; TCP connect
        mov [ebp + SOCK_REMOTE_IP], ecx
        mov [ebp + SOCK_REMOTE_PORT], dx

        ; Assign local port
        call net_alloc_port
        mov [ebp + SOCK_LOCAL_PORT], ax

        ; Generate initial sequence number from tick count
        mov eax, [tick_count]
        imul eax, 1103515245
        add eax, 12345
        mov [ebp + SOCK_SEQ], eax

        ; Send SYN
        mov dword [ebp + SOCK_STATE], TCP_SYN_SENT
        mov al, TCP_SYN
        call tcp_send_flags

        ; Wait for ESTABLISHED (up to 5 seconds)
        mov ecx, 500
.sconn_wait:
        hlt
        call tcp_flush_pending    ; Send deferred ACK from non-ISR context
        cmp dword [ebp + SOCK_STATE], TCP_ESTABLISHED
        je .sconn_ok
        cmp dword [ebp + SOCK_STATE], TCP_CLOSED
        je .sconn_fail_pop
        dec ecx
        jnz .sconn_wait
        ; Timeout - reset
        mov dword [ebp + SOCK_STATE], TCP_CLOSED
        jmp .sconn_fail_pop

.sconn_ok:
        popad
        xor eax, eax
        iretd

.sconn_udp:
        ; UDP "connect" - just save remote info
        mov [ebp + SOCK_REMOTE_IP], ecx
        mov [ebp + SOCK_REMOTE_PORT], dx
        call net_alloc_port
        mov [ebp + SOCK_LOCAL_PORT], ax
        popad
        xor eax, eax
        iretd

.sconn_fail_pop:
.sconn_fail:
        popad
        mov eax, -1
        iretd

;---------------------------------------
; sys_send - Send data on socket
;  EBX = fd, ECX = buffer, EDX = length
; Returns: EAX = bytes sent, -1 error
;---------------------------------------
sys_send:
        pushad
        cmp ebx, MAX_SOCKETS
        jge .ssend_fail
        mov eax, ebx
        imul eax, SOCK_SIZE
        lea ebp, [net_sock_table + eax]

        cmp dword [ebp + SOCK_TYPE], SOCK_TCP
        je .ssend_tcp
        cmp dword [ebp + SOCK_TYPE], SOCK_UDP
        je .ssend_udp
        jmp .ssend_fail

.ssend_tcp:
        cmp dword [ebp + SOCK_STATE], TCP_ESTABLISHED
        jne .ssend_fail

        call tcp_flush_pending    ; Flush any pending ACK before sending data

        ; Limit to 1400 bytes (MSS)
        cmp edx, 1400
        jle .ssend_tcp_ok
        mov edx, 1400
.ssend_tcp_ok:
        mov esi, ecx
        mov ecx, edx
        call tcp_send_data

        ; Wait briefly for ACK
        push edx
        mov ecx, 50
.ssend_wait_ack:
        hlt
        dec ecx
        jnz .ssend_wait_ack
        pop edx

        popad
        mov eax, edx            ; return bytes sent
        iretd

.ssend_udp:
        mov esi, ecx
        mov ecx, edx
        mov eax, [ebp + SOCK_REMOTE_IP]
        movzx ebx, word [ebp + SOCK_REMOTE_PORT]
        movzx edx, word [ebp + SOCK_LOCAL_PORT]
        push edx
        mov bx, bx
        pop edx
        xchg ebx, edx          ; ebx=src_port(dx), edx=dst_port(bx)
        ; Actually: udp_send wants EAX=dst_ip, BX=dst_port, DX=src_port
        mov eax, [ebp + SOCK_REMOTE_IP]
        mov bx, [ebp + SOCK_REMOTE_PORT]
        mov dx, [ebp + SOCK_LOCAL_PORT]
        push dword [esp + 8]    ; save original EDX (length)
        call udp_send
        pop eax                 ; recover length for return
        popad
        mov eax, edx            ; return bytes "sent"
        iretd

.ssend_fail:
        popad
        mov eax, -1
        iretd

;---------------------------------------
; sys_recv - Receive data from socket
;  EBX = fd, ECX = buffer, EDX = max length
; Returns: EAX = bytes received (0=no data, -1=error/closed)
;---------------------------------------
sys_recv:
        pushad
        cmp ebx, MAX_SOCKETS
        jge .srecv_fail
        mov eax, ebx
        imul eax, SOCK_SIZE
        lea ebp, [net_sock_table + eax]

        cmp dword [ebp + SOCK_TYPE], 0
        je .srecv_fail

        ; For TCP, check if connection is still alive
        cmp dword [ebp + SOCK_TYPE], SOCK_TCP
        jne .srecv_check_data
        mov eax, [ebp + SOCK_STATE]
        cmp eax, TCP_ESTABLISHED
        je .srecv_check_data
        cmp eax, TCP_CLOSE_WAIT
        je .srecv_check_data
        ; Connection not usable
        cmp dword [ebp + SOCK_RECV_LEN], 0
        je .srecv_closed
        ; Still have buffered data, deliver it first

.srecv_check_data:
        call tcp_flush_pending    ; Send deferred ACK from non-ISR context

        mov eax, [ebp + SOCK_RECV_LEN]
        test eax, eax
        jz .srecv_empty

        ; Copy min(available, requested) to user buffer
        cmp eax, edx
        jle .srecv_copy
        mov eax, edx
.srecv_copy:
        push eax               ; save count
        mov esi, [ebp + SOCK_RECV_BUF]
        mov edi, ecx
        mov ecx, eax
        cld
        rep movsb

        ; Shift remaining data forward
        pop eax
        push eax
        mov ecx, [ebp + SOCK_RECV_LEN]
        sub ecx, eax
        mov esi, [ebp + SOCK_RECV_BUF]
        add esi, eax
        mov edi, [ebp + SOCK_RECV_BUF]
        cld
        rep movsb

        ; Update recv len
        pop eax
        sub [ebp + SOCK_RECV_LEN], eax

        mov [esp + 28], eax    ; return byte count in EAX
        popad
        iretd

.srecv_empty:
        popad
        xor eax, eax
        iretd

.srecv_closed:
        popad
        mov eax, -1
        iretd

.srecv_fail:
        popad
        mov eax, -1
        iretd

;---------------------------------------
; sys_bind - Bind socket to local port
;  EBX = fd, ECX = port
; Returns: EAX = 0 success, -1 error
;---------------------------------------
sys_bind:
        pushad
        cmp ebx, MAX_SOCKETS
        jge .sbind_fail
        mov eax, ebx
        imul eax, SOCK_SIZE
        lea edi, [net_sock_table + eax]
        mov [edi + SOCK_LOCAL_PORT], cx
        popad
        xor eax, eax
        iretd
.sbind_fail:
        popad
        mov eax, -1
        iretd

;---------------------------------------
; sys_listen - Set socket to listening state
;  EBX = fd
;---------------------------------------
sys_listen:
        pushad
        cmp ebx, MAX_SOCKETS
        jge .slisten_fail
        mov eax, ebx
        imul eax, SOCK_SIZE
        lea edi, [net_sock_table + eax]
        cmp dword [edi + SOCK_TYPE], SOCK_TCP
        jne .slisten_fail
        mov dword [edi + SOCK_STATE], TCP_LISTEN
        popad
        xor eax, eax
        iretd
.slisten_fail:
        popad
        mov eax, -1
        iretd

;---------------------------------------
; sys_accept - Accept incoming connection
;  EBX = listening socket fd
; Returns: EAX = new socket fd, -1 on timeout
;
; v3.0: Scans for a child socket spawned from
; this listener (matching SOCK_PARENT). The
; listener stays in TCP_LISTEN for more
; connections.
;---------------------------------------
sys_accept:
        pushad
        cmp ebx, MAX_SOCKETS
        jge .saccept_fail
        mov eax, ebx
        imul eax, SOCK_SIZE
        lea ebp, [net_sock_table + eax]
        cmp dword [ebp + SOCK_STATE], TCP_LISTEN
        jne .saccept_fail

        ; Wait for a child socket to reach ESTABLISHED (up to 30 seconds)
        mov ecx, 3000
.saccept_wait:
        hlt
        call tcp_flush_pending    ; Send deferred SYN-ACK from non-ISR context

        ; Scan socket table for child socket that has our index as parent
        push ecx
        mov edi, net_sock_table
        xor edx, edx
.saccept_scan:
        cmp edx, MAX_SOCKETS
        jge .saccept_scan_done
        cmp dword [edi + SOCK_PARENT], ebx
        jne .saccept_scan_next
        ; Found child — check if ESTABLISHED
        cmp dword [edi + SOCK_STATE], TCP_ESTABLISHED
        je .saccept_found
        ; If SYN_RCVD, keep waiting
        cmp dword [edi + SOCK_STATE], TCP_SYN_RCVD
        je .saccept_scan_done   ; child is handshaking, keep waiting
.saccept_scan_next:
        add edi, SOCK_SIZE
        inc edx
        jmp .saccept_scan

.saccept_scan_done:
        pop ecx
        dec ecx
        jnz .saccept_wait
        jmp .saccept_fail

.saccept_found:
        ; EDX = child socket index
        ; Clear parent reference (connection is now independent)
        mov dword [edi + SOCK_PARENT], -1
        pop ecx                 ; balance push ecx
        mov [esp + 28], edx     ; return child socket fd in EAX
        popad
        iretd

.saccept_fail:
        popad
        mov eax, -1
        iretd

;---------------------------------------
; sys_dns - Resolve hostname
;  EBX = hostname pointer
; Returns: EAX = IP address (0 on failure)
;---------------------------------------
sys_dns:
        mov esi, ebx
        call dns_resolve
        iretd

;---------------------------------------
; sys_sockclose - Close socket
;  EBX = fd
;---------------------------------------
sys_sockclose:
        pushad
        cmp ebx, MAX_SOCKETS
        jge .sclose_done
        mov eax, ebx
        imul eax, SOCK_SIZE
        lea ebp, [net_sock_table + eax]

        cmp dword [ebp + SOCK_TYPE], SOCK_TCP
        jne .sclose_free

        ; TCP graceful close
        cmp dword [ebp + SOCK_STATE], TCP_ESTABLISHED
        je .sclose_fin
        cmp dword [ebp + SOCK_STATE], TCP_CLOSE_WAIT
        je .sclose_last_ack
        jmp .sclose_free

.sclose_fin:
        mov dword [ebp + SOCK_STATE], TCP_FIN_WAIT_1
        mov al, TCP_FIN | TCP_ACK
        call tcp_send_flags
        ; Wait for close to complete (up to 3 seconds)
        mov ecx, 300
.sclose_wait:
        hlt
        call tcp_flush_pending    ; Send deferred ACK from non-ISR context
        mov eax, [ebp + SOCK_STATE]
        cmp eax, TCP_CLOSED
        je .sclose_free
        cmp eax, TCP_TIME_WAIT
        je .sclose_free
        dec ecx
        jnz .sclose_wait
        jmp .sclose_free

.sclose_last_ack:
        call tcp_flush_pending    ; Flush pending ACK for received FIN
        mov dword [ebp + SOCK_STATE], TCP_LAST_ACK
        mov al, TCP_FIN | TCP_ACK
        call tcp_send_flags
        mov ecx, 300
.sclose_la_wait:
        hlt
        call tcp_flush_pending    ; Send deferred ACK from non-ISR context
        cmp dword [ebp + SOCK_STATE], TCP_CLOSED
        je .sclose_free
        dec ecx
        jnz .sclose_la_wait

.sclose_free:
        mov eax, ebx
        call sock_free
.sclose_done:
        popad
        xor eax, eax
        iretd

;---------------------------------------
; sys_ping - Send ICMP ping and wait for reply
;  EBX = destination IP (LE)
; Returns: EAX = RTT in ticks (-1 on timeout)
;---------------------------------------
sys_ping:
        push ebx
        push ecx
        mov eax, ebx
        call icmp_send_echo

        ; Wait up to 5 seconds for reply
        mov ecx, 500
.sp_wait:
        hlt
        cmp byte [net_ping_received], 1
        je .sp_got_reply
        dec ecx
        jnz .sp_wait

        ; Timeout
        mov eax, -1
        pop ecx
        pop ebx
        iretd

.sp_got_reply:
        mov eax, [net_ping_recv_tick]
        sub eax, [net_ping_send_tick]
        pop ecx
        pop ebx
        iretd


;=======================================================================
; SECTION 12: SHELL COMMANDS
;=======================================================================

;---------------------------------------
; cmd_net_status - Enhanced "net" command
;---------------------------------------
cmd_net_status:
        pushad

        mov byte [vga_color], COLOR_HEADER
        mov esi, msg_net_header
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT

        ; State
        mov esi, msg_net_state
        call vga_print
        mov eax, [net_state]
        cmp eax, NET_STATE_UP
        je .ns_up
        cmp eax, NET_STATE_FOUND
        je .ns_found
        mov esi, msg_net_none
        call vga_print
        jmp .ns_end
.ns_found:
        mov esi, msg_net_found
        call vga_print
        jmp .ns_show
.ns_up:
        mov esi, msg_net_up
        call vga_print

.ns_show:
        ; PCI location
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
        mov esi, msg_net_irq_str
        call vga_print
        movzx eax, byte [net_irq]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar

        ; I/O base
        mov esi, msg_net_iobase
        call vga_print
        mov eax, [net_io_base]
        call vga_print_hex
        mov al, 0x0A
        call vga_putchar

        ; MAC
        mov esi, msg_net_mac
        call vga_print
        call print_mac_address
        mov al, 0x0A
        call vga_putchar

        ; IP config
        mov esi, msg_net_ip_str
        call vga_print
        mov eax, [net_ip]
        call vga_print_ip
        mov al, 0x0A
        call vga_putchar

        mov esi, msg_net_mask_str
        call vga_print
        mov eax, [net_mask]
        call vga_print_ip
        mov al, 0x0A
        call vga_putchar

        mov esi, msg_net_gw_str
        call vga_print
        mov eax, [net_gateway]
        call vga_print_ip
        mov al, 0x0A
        call vga_putchar

        mov esi, msg_net_dns_str
        call vga_print
        mov eax, [net_dns]
        call vga_print_ip
        mov al, 0x0A
        call vga_putchar

.ns_end:
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; cmd_ping - "ping" shell command
;  ESI = rest of command line (hostname or IP)
;---------------------------------------
cmd_ping:
        pushad
        call skip_spaces
        cmp byte [esi], 0
        je .ping_usage

        ; Try to parse as IP address first
        call parse_ip
        test eax, eax
        jnz .ping_have_ip

        ; Try DNS resolution
        push esi
        call dns_resolve
        pop esi
        test eax, eax
        jz .ping_dns_fail

.ping_have_ip:
        mov [net_ping_target], eax

        ; Print "Pinging x.x.x.x..."
        push esi
        mov esi, msg_ping_start
        call vga_print
        mov eax, [net_ping_target]
        call vga_print_ip
        mov esi, msg_ping_dots
        call vga_print
        pop esi

        ; Send 4 pings
        mov dword [net_ping_count], 0
        mov dword [net_ping_success], 0

.ping_loop:
        cmp dword [net_ping_count], 4
        jge .ping_stats

        inc word [net_ping_id]
        mov eax, [net_ping_target]
        call icmp_send_echo

        ; Wait up to 2 seconds
        mov ecx, 200
.ping_wait:
        hlt
        cmp byte [net_ping_received], 1
        je .ping_got
        dec ecx
        jnz .ping_wait

        ; Timeout
        mov esi, msg_ping_timeout
        call vga_print
        jmp .ping_next

.ping_got:
        inc dword [net_ping_success]
        mov esi, msg_ping_reply
        call vga_print
        mov eax, [net_ping_target]
        call vga_print_ip
        mov esi, msg_ping_time
        call vga_print
        mov eax, [net_ping_recv_tick]
        sub eax, [net_ping_send_tick]
        ; Convert ticks to ms (1 tick = 10ms at 100Hz)
        imul eax, 10
        call vga_print_dec
        mov esi, msg_ping_ms
        call vga_print

.ping_next:
        inc dword [net_ping_count]
        ; Brief delay between pings
        mov ecx, 100
.ping_delay:
        hlt
        dec ecx
        jnz .ping_delay
        jmp .ping_loop

.ping_stats:
        ; Print summary
        mov esi, msg_ping_stats
        call vga_print
        mov eax, [net_ping_success]
        call vga_print_dec
        mov esi, msg_ping_of
        call vga_print
        mov eax, [net_ping_count]
        call vga_print_dec
        mov esi, msg_ping_recv_str
        call vga_print
        jmp .ping_done

.ping_usage:
        mov esi, msg_ping_usage
        call vga_print
        jmp .ping_done
.ping_dns_fail:
        mov esi, msg_ping_dns_fail
        call vga_print
.ping_done:
        popad
        ret

;---------------------------------------
; cmd_ifconfig - "ifconfig" shell command
;---------------------------------------
cmd_ifconfig:
        pushad
        call skip_spaces
        cmp byte [esi], 0
        je .ifc_show

        ; Parse: ifconfig ip mask gateway dns
        ; For simplicity, just set IP if one arg given
        call parse_ip
        test eax, eax
        jz .ifc_show
        mov [net_ip], eax
        mov esi, msg_ifc_set
        call vga_print
        mov eax, [net_ip]
        call vga_print_ip
        mov al, 0x0A
        call vga_putchar
        jmp .ifc_done

.ifc_show:
        ; Just call net status (which shows IP info)
        call cmd_net_status
.ifc_done:
        popad
        ret

;---------------------------------------
; cmd_dhcp_shell - "dhcp" shell command
;---------------------------------------
cmd_dhcp_shell:
        pushad
        cmp dword [net_state], NET_STATE_UP
        jne .dhcp_no_nic

        mov esi, msg_dhcp_start
        call vga_print

        mov byte [net_dhcp_state], 0
        call dhcp_discover

        ; Wait for DHCP to complete (up to 10 seconds)
        ; States: 1=discover sent, 2=offer received, 3=request sent, 4=done
        mov ecx, 1000
.dhcp_wait:
        hlt

        ; If we got an offer (state 2), send the REQUEST from here
        ; (not from the ISR, to avoid TX descriptor re-entrance)
        cmp byte [net_dhcp_state], 2
        je .dhcp_send_request

        cmp byte [net_dhcp_state], 4
        je .dhcp_ok
        dec ecx
        jnz .dhcp_wait

        mov esi, msg_dhcp_fail
        call vga_print
        jmp .dhcp_sh_done

.dhcp_send_request:
        call dhcp_request
        jmp .dhcp_wait

.dhcp_ok:
        mov esi, msg_dhcp_ok
        call vga_print
        mov eax, [net_ip]
        call vga_print_ip
        mov al, 0x0A
        call vga_putchar
        jmp .dhcp_sh_done

.dhcp_no_nic:
        mov esi, msg_dhcp_no_nic
        call vga_print
.dhcp_sh_done:
        popad
        ret

;---------------------------------------
; cmd_arp_show - "arp" shell command
;---------------------------------------
cmd_arp_show:
        pushad
        mov byte [vga_color], COLOR_HEADER
        mov esi, msg_arp_header
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT

        mov edi, net_arp_cache
        mov ecx, ARP_CACHE_SIZE
.arp_show_loop:
        cmp byte [edi + ARP_ENTRY_STATE], 2
        jne .arp_show_next

        ; Print IP
        mov eax, [edi + ARP_ENTRY_IP]
        call vga_print_ip

        mov esi, msg_arp_at
        call vga_print

        ; Print MAC
        push ecx
        push edi
        lea esi, [edi + ARP_ENTRY_MAC]
        xor ebx, ebx
.arp_mac_loop:
        movzx eax, byte [esi + ebx]
        call vga_print_hex_byte
        inc ebx
        cmp ebx, 6
        jge .arp_mac_done
        mov al, ':'
        call vga_putchar
        jmp .arp_mac_loop
.arp_mac_done:
        mov al, 0x0A
        call vga_putchar
        pop edi
        pop ecx

.arp_show_next:
        add edi, ARP_ENTRY_SIZE
        dec ecx
        jnz .arp_show_loop

        popad
        ret


;=======================================================================
; SECTION 13: UTILITY FUNCTIONS
;=======================================================================

;---------------------------------------
; vga_print_ip - Print IP address from EAX (little-endian)
;---------------------------------------
vga_print_ip:
        push eax
        push ecx
        mov ecx, eax

        movzx eax, cl
        call vga_print_dec
        mov al, '.'
        call vga_putchar
        movzx eax, ch
        call vga_print_dec
        mov al, '.'
        call vga_putchar
        shr ecx, 16
        movzx eax, cl
        call vga_print_dec
        mov al, '.'
        call vga_putchar
        movzx eax, ch
        call vga_print_dec

        pop ecx
        pop eax
        ret

;---------------------------------------
; print_mac_address - Print MAC from net_mac
;---------------------------------------
print_mac_address:
        pushad
        mov esi, net_mac
        xor ebx, ebx
.pma_loop:
        movzx eax, byte [esi + ebx]
        call vga_print_hex_byte
        inc ebx
        cmp ebx, 6
        jge .pma_done
        mov al, ':'
        call vga_putchar
        jmp .pma_loop
.pma_done:
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

;---------------------------------------
; parse_ip - Parse dotted-decimal IP string
;  ESI = string (e.g., "10.0.2.15")
; Returns: EAX = IP (little-endian), 0 on error
; ESI advanced past IP
;---------------------------------------
parse_ip:
        push ebx
        push ecx
        push edx
        xor eax, eax
        xor ebx, ebx            ; result IP
        xor ecx, ecx            ; octet count
        xor edx, edx            ; current octet value

.pip_loop:
        movzx eax, byte [esi]
        cmp al, '0'
        jb .pip_dot_or_end
        cmp al, '9'
        ja .pip_dot_or_end
        sub al, '0'
        imul edx, 10
        add edx, eax
        inc esi
        jmp .pip_loop

.pip_dot_or_end:
        cmp edx, 255
        ja .pip_fail
        ; Store octet
        shl ebx, 8
        or ebx, edx
        inc ecx
        xor edx, edx

        cmp al, '.'
        jne .pip_finish
        cmp ecx, 4
        jge .pip_fail
        inc esi
        jmp .pip_loop

.pip_finish:
        cmp ecx, 4
        jne .pip_fail
        ; Convert from big-endian construction to little-endian
        bswap ebx
        mov eax, ebx
        pop edx
        pop ecx
        pop ebx
        ret

.pip_fail:
        xor eax, eax
        pop edx
        pop ecx
        pop ebx
        ret


;=======================================================================
; SECTION 14: NET STRINGS
;=======================================================================

msg_net_header: db "=== Network Status ===", 0x0A, 0
msg_net_state:  db "  State:    ", 0
msg_net_none:   db "No NIC detected", 0x0A, 0
msg_net_found:  db "NIC found (not initialized)", 0x0A, 0
msg_net_up:     db "UP (RTL8139)", 0x0A, 0
msg_net_pci:    db "  PCI:      ", 0
msg_net_irq_str: db "  IRQ ", 0
msg_net_iobase: db "  I/O:      0x", 0
msg_net_mac:    db "  MAC:      ", 0
msg_net_ip_str: db "  IP:       ", 0
msg_net_mask_str: db "  Netmask:  ", 0
msg_net_gw_str: db "  Gateway:  ", 0
msg_net_dns_str: db "  DNS:      ", 0

msg_ping_start: db "Pinging ", 0
msg_ping_dots:  db "...", 0x0A, 0
msg_ping_timeout: db "  Request timed out", 0x0A, 0
msg_ping_reply: db "  Reply from ", 0
msg_ping_time:  db " time=", 0
msg_ping_ms:    db "ms", 0x0A, 0
msg_ping_stats: db "  --- ", 0
msg_ping_of:    db "/", 0
msg_ping_recv_str: db " packets received", 0x0A, 0
msg_ping_usage: db "Usage: ping <host>", 0x0A, 0
msg_ping_dns_fail: db "DNS resolution failed", 0x0A, 0

msg_ifc_set:    db "IP set to ", 0

msg_dhcp_start: db "Sending DHCP Discover...", 0x0A, 0
msg_dhcp_ok:    db "DHCP configured: ", 0
msg_dhcp_fail:  db "DHCP timeout", 0x0A, 0
msg_dhcp_no_nic: db "No NIC available", 0x0A, 0

msg_arp_header: db "ARP Cache:", 0x0A, 0
msg_arp_at:     db " -> ", 0
