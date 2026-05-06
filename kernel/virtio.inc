;=======================================================================
; virtio.inc - VirtIO PCI Legacy Driver (spec 0.9.5)
;
; Implements a minimal virtqueue abstraction for:
;   - virtio-blk  (PCI device 1AF4:1001 or 1AF4:1042 transitional)
;   - virtio-net  (PCI device 1AF4:1000 or 1AF4:1041 transitional)
;
; Only legacy (pre-1.0) PCI interface is implemented (BAR0 = I/O space).
;
; VirtIO PCI Legacy I/O register map (offsets from BAR0):
;   0x00  DEVICE_FEATURES (32-bit R)
;   0x04  DRIVER_FEATURES (32-bit W)
;   0x08  QUEUE_ADDRESS   (32-bit W) — page number of vring (page size=4096)
;   0x0C  QUEUE_SIZE      (16-bit R)
;   0x0E  QUEUE_SELECT    (16-bit W)
;   0x10  QUEUE_NOTIFY    (16-bit W)
;   0x12  DEVICE_STATUS   (8-bit  R/W)
;   0x13  ISR_STATUS      (8-bit  R, clears on read)
;
; DEVICE_STATUS bits:
;   0x01 ACKNOWLEDGE, 0x02 DRIVER, 0x04 DRIVER_OK, 0x40 FEATURES_OK
;
; Virtqueue vring layout (all inside one allocated page group):
;   [descriptor table][avail ring][used ring]
;   desc table  = queue_size × 16 bytes
;   avail ring  = 2 + 2 + queue_size × 2 bytes
;   used ring   = 2 + 2 + queue_size × 8 bytes  (align to 4096)
;=======================================================================

; VirtIO PCI legacy I/O offsets
VIRTIO_DEVICE_FEAT  equ 0x00
VIRTIO_DRIVER_FEAT  equ 0x04
VIRTIO_QUEUE_ADDR   equ 0x08
VIRTIO_QUEUE_SIZE   equ 0x0C
VIRTIO_QUEUE_SEL    equ 0x0E
VIRTIO_QUEUE_NOTIFY equ 0x10
VIRTIO_DEV_STATUS   equ 0x12
VIRTIO_ISR_STATUS   equ 0x13

; Device status bits
VIRTIO_S_ACKNOWLEDGE equ 0x01
VIRTIO_S_DRIVER      equ 0x02
VIRTIO_S_DRIVER_OK   equ 0x04
VIRTIO_S_FAILED      equ 0x80

; Descriptor flags
VIRTQ_DESC_F_NEXT    equ 0x01   ; descriptor chains to next
VIRTQ_DESC_F_WRITE   equ 0x02   ; device writes (otherwise device reads)

; virtio-blk request type
VIRTIO_BLK_T_IN      equ 0     ; read
VIRTIO_BLK_T_OUT     equ 1     ; write

; Queue sizes — use 16 for simplicity
VIRTQ_SIZE           equ 16

; Descriptor table entry (16 bytes)
VIRTQ_DESC_ADDR      equ 0      ; 64-bit buffer address
VIRTQ_DESC_LEN       equ 8      ; 32-bit length
VIRTQ_DESC_FLAGS     equ 12     ; 16-bit flags
VIRTQ_DESC_NEXT      equ 14     ; 16-bit next index
VIRTQ_DESC_SIZE      equ 16

; Avail ring header (2 bytes flags + 2 bytes idx + N × 2-byte ring entries + 2 used_event)
VIRTQ_AVAIL_SIZE     equ (4 + VIRTQ_SIZE * 2)

; Used ring element (8 bytes: id:32, len:32)
VIRTQ_USED_ELEM_SIZE equ 8
VIRTQ_USED_SIZE      equ (4 + VIRTQ_SIZE * VIRTQ_USED_ELEM_SIZE)

; Pages needed for one virtqueue:
;   desc=VIRTQ_SIZE*16=256, avail=36, used=132 → fits in 1 page if aligned
VIRTQ_PAGES          equ 2

; Max sectors per virtio-blk request
VIRTIO_BLK_SECTORS   equ 8

;---------------------------------------
; virtio_init - Detect and initialise virtio-blk and virtio-net
;---------------------------------------
virtio_init:
        pushad
        mov dword [virtio_blk_present], 0
        mov dword [virtio_net_present], 0

        ; --- virtio-blk ---
        mov ebx, PCI_VENDOR_VIRTIO
        mov ecx, PCI_DEV_VIRTIO_BLK
        call pci_find_device
        cmp eax, -1
        je  .try_blk2
        call .init_blk
        jmp .try_net
.try_blk2:
        mov ebx, PCI_VENDOR_VIRTIO
        mov ecx, PCI_DEV_VIRTIO_BLK2
        call pci_find_device
        cmp eax, -1
        je  .try_net
        call .init_blk

        ; --- virtio-net ---
.try_net:
        mov ebx, PCI_VENDOR_VIRTIO
        mov ecx, PCI_DEV_VIRTIO_NET
        call pci_find_device
        cmp eax, -1
        je  .try_net2
        call .init_net
        jmp .vio_done
.try_net2:
        mov ebx, PCI_VENDOR_VIRTIO
        mov ecx, PCI_DEV_VIRTIO_NET2
        call pci_find_device
        cmp eax, -1
        je  .vio_done
        call .init_net

.vio_done:
        popad
        ret

; ---------- inner: init virtio-blk ----------
.init_blk:
        push eax
        ; Enable PCI bus master + I/O space
        push eax
        mov ebx, 0x04
        call pci_read_config32
        or  eax, 0x05
        mov ecx, eax
        mov ebx, 0x04
        call pci_write_config32
        pop eax
        ; Get BAR0 (I/O base)
        push eax
        mov ebx, 0
        call pci_read_bar
        mov [virtio_blk_io], eax
        pop eax
        ; Negotiate (acknowledge + driver features)
        call .vio_reset_dev_blk
        ; Allocate and set up virtqueue 0 (request queue)
        call .vio_setup_blk_vq
        mov dword [virtio_blk_present], 1
        pop eax
        ret

; ---------- inner: init virtio-net ----------
.init_net:
        push eax
        ; Enable PCI bus master + I/O space
        push eax
        mov ebx, 0x04
        call pci_read_config32
        or  eax, 0x05
        mov ecx, eax
        mov ebx, 0x04
        call pci_write_config32
        pop eax
        mov ebx, 0
        call pci_read_bar
        mov [virtio_net_io], eax
        call .vio_reset_dev_net
        ; Read MAC address (at device-specific config = BAR0 + 0x14 for net)
        push edx
        mov edx, [virtio_net_io]
        add edx, 0x14           ; device-specific config start for net
        in  al, dx
        mov [virtio_mac], al
        inc edx
        in  al, dx
        mov [virtio_mac + 1], al
        inc edx
        in  al, dx
        mov [virtio_mac + 2], al
        inc edx
        in  al, dx
        mov [virtio_mac + 3], al
        inc edx
        in  al, dx
        mov [virtio_mac + 4], al
        inc edx
        in  al, dx
        mov [virtio_mac + 5], al
        pop edx
        mov dword [virtio_net_present], 1
        pop eax
        ret

; ---------- reset and negotiate virtio-blk ----------
.vio_reset_dev_blk:
        push eax
        push edx
        mov edx, [virtio_blk_io]
        add edx, VIRTIO_DEV_STATUS
        xor al, al
        out dx, al              ; Reset
        mov al, VIRTIO_S_ACKNOWLEDGE | VIRTIO_S_DRIVER
        out dx, al
        ; Accept all features (no feature negotiation for simplicity)
        mov edx, [virtio_blk_io]
        add edx, VIRTIO_DEVICE_FEAT
        in  eax, dx
        mov edx, [virtio_blk_io]
        add edx, VIRTIO_DRIVER_FEAT
        out dx, eax
        ; DRIVER_OK
        mov edx, [virtio_blk_io]
        add edx, VIRTIO_DEV_STATUS
        mov al, VIRTIO_S_ACKNOWLEDGE | VIRTIO_S_DRIVER | VIRTIO_S_DRIVER_OK
        out dx, al
        pop edx
        pop eax
        ret

; ---------- reset and negotiate virtio-net ----------
.vio_reset_dev_net:
        push eax
        push edx
        mov edx, [virtio_net_io]
        add edx, VIRTIO_DEV_STATUS
        xor al, al
        out dx, al
        mov al, VIRTIO_S_ACKNOWLEDGE | VIRTIO_S_DRIVER
        out dx, al
        mov edx, [virtio_net_io]
        add edx, VIRTIO_DEVICE_FEAT
        in  eax, dx
        mov edx, [virtio_net_io]
        add edx, VIRTIO_DRIVER_FEAT
        out dx, eax
        mov edx, [virtio_net_io]
        add edx, VIRTIO_DEV_STATUS
        mov al, VIRTIO_S_ACKNOWLEDGE | VIRTIO_S_DRIVER | VIRTIO_S_DRIVER_OK
        out dx, al
        pop edx
        pop eax
        ret

; ---------- set up virtio-blk virtqueue 0 ----------
.vio_setup_blk_vq:
        push eax
        push ecx
        push edx
        ; Allocate VIRTQ_PAGES pages for the vring
        mov ecx, VIRTQ_PAGES
        call pmm_alloc_pages
        test eax, eax
        jz  .vvq_fail
        mov [virtio_blk_vring], eax

        ; Zero the vring
        push eax
        mov edi, eax
        mov ecx, VIRTQ_PAGES * 4096 / 4
        xor eax, eax
        cld
        rep stosd
        pop eax

        ; Program QUEUE_SELECT = 0
        mov edx, [virtio_blk_io]
        add edx, VIRTIO_QUEUE_SEL
        xor ax, ax
        out dx, ax

        ; Read queue size (should be ≥ VIRTQ_SIZE)
        mov edx, [virtio_blk_io]
        add edx, VIRTIO_QUEUE_SIZE
        in  ax, dx
        movzx eax, ax
        mov [virtio_blk_qsz], eax

        ; Program QUEUE_ADDRESS = physical page number
        mov edx, [virtio_blk_io]
        add edx, VIRTIO_QUEUE_ADDR
        mov eax, [virtio_blk_vring]
        shr eax, 12             ; convert to page number (4KB pages)
        out dx, eax

.vvq_fail:
        pop edx
        pop ecx
        pop eax
        ret

;---------------------------------------
; virtio_blk_read - Read sectors from virtio-blk device
; EAX = sector number (32-bit), ECX = sector count (1-8), EDI = destination ptr
; Returns CF on error
;---------------------------------------
virtio_blk_read:
        pushad
        cmp dword [virtio_blk_present], 0
        je  .vbr_fail

        ; Build a 3-descriptor chain:
        ;  desc[0] → blk request header (read-only)  [type:32, reserved:32, sector:64]
        ;  desc[1] → data buffer (write-only by device)
        ;  desc[2] → status byte (write-only by device)

        ; Store args
        mov [.vbr_sector], eax
        mov [.vbr_count], ecx
        mov [.vbr_dst], edi

        ; Build request header in scratch buffer
        mov dword [virtio_blk_req + 0],  VIRTIO_BLK_T_IN
        mov dword [virtio_blk_req + 4],  0          ; reserved
        mov eax, [.vbr_sector]
        mov [virtio_blk_req + 8],  eax              ; sector lo
        mov dword [virtio_blk_req + 12], 0           ; sector hi (64-bit extension)
        mov byte [virtio_blk_status], 0xFF           ; status byte placeholder

        ; Get descriptor table base
        mov edi, [virtio_blk_vring]                  ; desc table at offset 0

        ; Descriptor 0: request header (device reads)
        mov eax, virtio_blk_req                      ; physical addr = virtual (identity map)
        mov [edi + VIRTQ_DESC_ADDR + 0], eax
        mov dword [edi + VIRTQ_DESC_ADDR + 4], 0
        mov dword [edi + VIRTQ_DESC_LEN],  16        ; sizeof request header
        mov word  [edi + VIRTQ_DESC_FLAGS], VIRTQ_DESC_F_NEXT
        mov word  [edi + VIRTQ_DESC_NEXT],  1

        ; Descriptor 1: data buffer (device writes)
        add edi, VIRTQ_DESC_SIZE
        mov eax, [.vbr_dst]
        mov [edi + VIRTQ_DESC_ADDR + 0], eax
        mov dword [edi + VIRTQ_DESC_ADDR + 4], 0
        mov eax, [.vbr_count]
        shl eax, 9                              ; count × 512
        mov [edi + VIRTQ_DESC_LEN], eax
        mov word  [edi + VIRTQ_DESC_FLAGS], VIRTQ_DESC_F_WRITE | VIRTQ_DESC_F_NEXT
        mov word  [edi + VIRTQ_DESC_NEXT],  2

        ; Descriptor 2: status byte (device writes)
        add edi, VIRTQ_DESC_SIZE
        mov eax, virtio_blk_status
        mov [edi + VIRTQ_DESC_ADDR + 0], eax
        mov dword [edi + VIRTQ_DESC_ADDR + 4], 0
        mov dword [edi + VIRTQ_DESC_LEN], 1
        mov word  [edi + VIRTQ_DESC_FLAGS], VIRTQ_DESC_F_WRITE
        mov word  [edi + VIRTQ_DESC_NEXT],  0

        ; Add descriptor 0 to avail ring
        ; Avail ring offset = VIRTQ_SIZE * VIRTQ_DESC_SIZE = 16*16 = 256
        mov edi, [virtio_blk_vring]
        add edi, VIRTQ_SIZE * VIRTQ_DESC_SIZE       ; avail ring base
        ; avail.flags at +0, avail.idx at +2, avail.ring[0..N] at +4
        movzx eax, word [edi + 2]                   ; avail.idx
        mov [.vbr_avail_idx], eax
        ; ring[(idx) mod VIRTQ_SIZE] = descriptor 0
        and eax, VIRTQ_SIZE - 1
        lea ecx, [edi + 4 + eax * 2]
        mov word [ecx], 0                           ; put descriptor head = 0

        ; Increment avail.idx
        movzx eax, word [edi + 2]
        inc eax
        mov [edi + 2], ax

        ; Notify device: QUEUE_NOTIFY queue 0
        push edx
        mov edx, [virtio_blk_io]
        add edx, VIRTIO_QUEUE_NOTIFY
        xor ax, ax
        out dx, ax
        pop edx

        ; Poll used ring until used.idx advances
        mov edi, [virtio_blk_vring]
        ; Used ring starts at aligned offset after avail ring
        ; Offset = align(desc + avail, 4096) — for simplicity at page 1
        add edi, 4096
        mov ecx, 100000
.vbr_poll:
        movzx eax, word [edi + 2]               ; used.idx
        cmp eax, [.vbr_avail_idx]
        jg  .vbr_ok
        loop .vbr_poll
        ; Timeout
        popad
        stc
        ret

.vbr_ok:
        ; Check status byte
        cmp byte [virtio_blk_status], 0
        jne .vbr_fail
        popad
        clc
        ret

.vbr_fail:
        popad
        stc
        ret

.vbr_sector:    dd 0
.vbr_count:     dd 0
.vbr_dst:       dd 0
.vbr_avail_idx: dd 0

;---------------------------------------
; virtio module data
;---------------------------------------
virtio_blk_present: dd 0
virtio_blk_io:      dd 0        ; I/O base (BAR0)
virtio_blk_vring:   dd 0        ; physical addr of vring memory
virtio_blk_qsz:     dd 0        ; queue size reported by device
virtio_blk_req:     times 16 db 0  ; request header scratch (type+reserved+sector_lo+sector_hi)
virtio_blk_status:  db 0
                    times 3 db 0    ; padding

virtio_net_present: dd 0
virtio_net_io:      dd 0
virtio_mac:         times 6 db 0
                    db 0, 0         ; padding
