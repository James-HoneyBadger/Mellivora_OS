;=======================================================================
; pci.inc - Generic PCI Bus Enumeration Library
;
; Scans PCI buses 0-7 and builds a compact device table.
; Drivers use pci_find_device() to locate hardware by vendor/device ID.
;
; API:
;   pci_init           - Called once at boot to enumerate all devices
;   pci_find_device    - EBX=vendor, ECX=device_id -> EAX=bdf or -1
;   pci_read_config32  - EAX=bdf, EBX=offset -> EAX=value
;   pci_write_config32 - EAX=bdf, EBX=offset, ECX=value
;   pci_read_bar       - EAX=bdf, EBX=bar_idx(0-5) -> EAX=base or 0
;
; BDF packing used here:  bits[23:16]=bus, bits[12:8]=dev, bits[2:0]=fn
; Hardware address format: 0x80000000|bus<<16|dev<<11|fn<<8|offset
;=======================================================================

; PCI entry layout in pci_dev_table (12 bytes each)
PCI_DEV_MAX     equ 64
PCI_ENTRY_SIZE  equ 12

; PCI config space register offsets
PCI_OFF_VENDEV  equ 0x00
PCI_OFF_CLASS   equ 0x08
PCI_OFF_HDR     equ 0x0C
PCI_OFF_BAR0    equ 0x10

; Class codes
PCI_CLASS_STORAGE  equ 0x01
PCI_CLASS_NETWORK  equ 0x02
PCI_CLASS_DISPLAY  equ 0x03
PCI_CLASS_AUDIO    equ 0x04
PCI_SUBCLASS_IDE   equ 0x01

; Vendor/device IDs for new drivers
PCI_VENDOR_INTEL   equ 0x8086
PCI_DEV_ICH_AC97N  equ 0x2415
PCI_DEV_ICH_AC97   equ 0x2425
PCI_DEV_ICH2_AC97  equ 0x2445
PCI_VENDOR_VIRTIO  equ 0x1AF4
PCI_DEV_VIRTIO_NET equ 0x1000
PCI_DEV_VIRTIO_BLK equ 0x1001
PCI_DEV_VIRTIO_NET2 equ 0x1041
PCI_DEV_VIRTIO_BLK2 equ 0x1042

;---------------------------------------
; _pci_cfg_addr - Build PCI config address word
; Uses pci_tmp_bus/dev/fn + EDX=offset
; Returns EAX = config address; destroys EAX, ECX
;---------------------------------------
_pci_cfg_addr:
        movzx eax, byte [pci_tmp_bus]
        shl eax, 16
        movzx ecx, byte [pci_tmp_dev]
        shl ecx, 11                     ; device number goes to bits 13:11
        or  eax, ecx
        movzx ecx, byte [pci_tmp_fn]
        shl ecx, 8
        or  eax, ecx
        and edx, 0xFC                   ; dword-align
        or  eax, edx
        or  eax, 0x80000000
        ret

;---------------------------------------
; _pci_read32 - Read dword using pci_tmp_bus/dev/fn + EDX=offset
; Returns: EAX = value; preserves all regs except EAX
;---------------------------------------
_pci_read32:
        push ecx
        push edx
        call _pci_cfg_addr
        mov dx, PCI_CONFIG_ADDR
        out dx, eax
        mov dx, PCI_CONFIG_DATA
        in  eax, dx
        pop edx
        pop ecx
        ret

;---------------------------------------
; _pci_write32 - Write dword using pci_tmp_bus/dev/fn + EDX=offset + ESI=value
;---------------------------------------
_pci_write32:
        push eax
        push ecx
        push edx
        call _pci_cfg_addr
        mov dx, PCI_CONFIG_ADDR
        out dx, eax
        mov eax, esi
        mov dx, PCI_CONFIG_DATA
        out dx, eax
        pop edx
        pop ecx
        pop eax
        ret

;---------------------------------------
; _pci_unpack_bdf - Unpack our bdf into pci_tmp_bus/dev/fn
; EAX = bdf (bus<<16 | dev<<8 | fn)
;---------------------------------------
_pci_unpack_bdf:
        push eax
        push ecx
        movzx ecx, al
        mov [pci_tmp_fn], cl
        shr eax, 8
        movzx ecx, al
        mov [pci_tmp_dev], cl
        shr eax, 8
        movzx ecx, al
        mov [pci_tmp_bus], cl
        pop ecx
        pop eax
        ret

;---------------------------------------
; pci_read_config32 - EAX=bdf, EBX=offset -> EAX=value
;---------------------------------------
pci_read_config32:
        push edx
        call _pci_unpack_bdf
        mov edx, ebx
        call _pci_read32
        pop edx
        ret

;---------------------------------------
; pci_write_config32 - EAX=bdf, EBX=offset, ECX=value
;---------------------------------------
pci_write_config32:
        push edx
        push esi
        call _pci_unpack_bdf
        mov edx, ebx
        mov esi, ecx
        call _pci_write32
        pop esi
        pop edx
        ret

;---------------------------------------
; pci_read_bar - EAX=bdf, EBX=bar_index(0-5) -> EAX=base or 0
;---------------------------------------
pci_read_bar:
        push ebx
        imul ebx, 4
        add  ebx, PCI_OFF_BAR0
        call pci_read_config32
        test eax, 1
        jnz  .io
        and  eax, 0xFFFFFFF0            ; memory BAR: mask type bits
        jmp  .done
.io:
        and  eax, 0xFFFFFFFC            ; I/O BAR: mask type bits
.done:
        pop  ebx
        ret

;---------------------------------------
; pci_init - Enumerate all PCI buses/devices/functions.
; Fills pci_dev_table and pci_dev_count.
;---------------------------------------
pci_init:
        pushad
        mov dword [pci_dev_count], 0
        mov byte [pci_loop_bus], 0

.bus_loop:
        cmp byte [pci_loop_bus], 8
        jge .done
        mov byte [pci_loop_dev], 0

.dev_loop:
        cmp byte [pci_loop_dev], 32
        jge .next_bus
        mov byte [pci_loop_fn], 0

.fn_loop:
        cmp byte [pci_loop_fn], 8
        jge .next_dev

        ; Set up pci_tmp_* for _pci_read32
        mov al, [pci_loop_bus]
        mov [pci_tmp_bus], al
        mov al, [pci_loop_dev]
        mov [pci_tmp_dev], al
        mov al, [pci_loop_fn]
        mov [pci_tmp_fn], al

        ; Read vendor:device
        mov edx, PCI_OFF_VENDEV
        call _pci_read32
        cmp eax, 0xFFFFFFFF
        je  .skip_fn

        ; Table full?
        mov edi, [pci_dev_count]
        cmp edi, PCI_DEV_MAX
        jge .skip_fn

        ; Store entry in table
        imul edi, PCI_ENTRY_SIZE
        add  edi, pci_dev_table
        mov [edi + 0], ax               ; vendor_id (low word)
        push eax
        shr eax, 16
        mov [edi + 2], ax               ; device_id (high word)
        pop eax

        mov al, [pci_loop_bus]
        mov [edi + 4], al
        mov al, [pci_loop_dev]
        mov [edi + 5], al
        mov al, [pci_loop_fn]
        mov [edi + 6], al

        ; Class code
        mov edx, PCI_OFF_CLASS
        call _pci_read32
        push eax
        shr eax, 24
        mov [edi + 7], al               ; class
        pop eax
        shr eax, 16
        and eax, 0xFF
        mov [edi + 8], al               ; subclass

        ; Header type
        mov edx, PCI_OFF_HDR
        call _pci_read32
        shr eax, 16
        and eax, 0xFF
        mov [edi + 9], al               ; header_type
        mov word [edi + 10], 0

        inc dword [pci_dev_count]

        ; If this is fn 0 and NOT multifunction, skip fn 1-7
        cmp byte [pci_loop_fn], 0
        jne .skip_mf_check
        ; al still has header_type (bits 15:8 of HDR dword, we extracted it above)
        ; Re-read to be safe
        mov edx, PCI_OFF_HDR
        call _pci_read32
        test eax, 0x00800000            ; bit 23 = multifunction flag
        jnz .skip_mf_check              ; multifunction — scan all fns
        ; Single-function — advance to next device
        inc byte [pci_loop_dev]
        jmp .dev_loop

.skip_mf_check:
.skip_fn:
        inc byte [pci_loop_fn]
        jmp .fn_loop

.next_dev:
        inc byte [pci_loop_dev]
        jmp .dev_loop

.next_bus:
        inc byte [pci_loop_bus]
        jmp .bus_loop

.done:
        popad
        ret

;---------------------------------------
; pci_find_device - EBX=vendor_id(16-bit), ECX=device_id(16-bit)
; Returns: EAX = bdf (bus<<16|dev<<8|fn) or -1 if not found
;---------------------------------------
pci_find_device:
        push esi
        push ecx
        push edx
        xor edx, edx
.loop:
        cmp edx, [pci_dev_count]
        jge .not_found
        mov esi, edx
        imul esi, PCI_ENTRY_SIZE
        add  esi, pci_dev_table
        movzx eax, word [esi + 0]       ; vendor_id
        cmp ax, bx
        jne .next
        movzx eax, word [esi + 2]       ; device_id
        cmp eax, ecx
        jne .next
        ; Match — pack bdf
        movzx eax, byte [esi + 4]       ; bus
        shl eax, 16
        movzx ecx, byte [esi + 5]       ; dev
        shl ecx, 8
        or  eax, ecx
        movzx ecx, byte [esi + 6]       ; fn
        or  eax, ecx
        pop edx
        pop ecx
        pop esi
        ret
.next:
        inc edx
        jmp .loop
.not_found:
        mov eax, -1
        pop edx
        pop ecx
        pop esi
        ret

;---------------------------------------
; sys_pci_find - SYS_PCI_FIND syscall handler
; EBX=vendor_id, ECX=device_id -> EAX=bdf or -1
;---------------------------------------
sys_pci_find:
        call pci_find_device
        iretd

;---------------------------------------
; PCI module data (zeroed by kernel BSS clear)
;---------------------------------------
pci_tmp_bus:    db 0
pci_tmp_dev:    db 0
pci_tmp_fn:     db 0
                db 0
pci_loop_bus:   db 0
pci_loop_dev:   db 0
pci_loop_fn:    db 0
                db 0
pci_dev_count:  dd 0
pci_dev_table:  times PCI_DEV_MAX * PCI_ENTRY_SIZE db 0
