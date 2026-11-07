;=======================================================================
; usb.inc - USB host controllers
;
; M8 starts here. The milestone's exit line is "plug in a USB stick and copy a
; file, unplug it safely", and the road to that is a host controller stack, hub
; enumeration, and then a mass-storage class driver. This is the first step of
; the first of those: find the controller, bring it out of reset, and say what
; is plugged into its root ports.
;
; What is here is deliberately small and honest. The controller is found by its
; PCI class, reset, and read; nothing is transferred, no device is enumerated
; beyond "something is on this port", and the controller is left stopped with
; no frame list. Those are the next steps, not omissions to be discovered
; later: a driver that claims a bus it cannot address is worse than one that
; says where it stopped.
;
; UHCI first, because it is the controller the era's chipsets (and QEMU's
; default machine) actually have. EHCI and OHCI are later steps in the same
; milestone.
;=======================================================================

; UHCI I/O register offsets from the controller's BAR4
UHCI_CMD        equ 0x00                ; command
UHCI_STS        equ 0x02                ; status
UHCI_INTR       equ 0x04                ; interrupt enable
UHCI_FRNUM      equ 0x06                ; frame number
UHCI_FLBASE     equ 0x08                ; frame list base address
UHCI_SOF        equ 0x0C                ; start of frame modify
UHCI_PORTSC1    equ 0x10                ; root hub port 1 status/control
UHCI_PORTSC2    equ 0x12                ; root hub port 2 status/control

UHCI_CMD_HCRESET equ 0x0002             ; host controller reset, self-clearing
UHCI_CMD_RUN    equ 0x0001              ; run
UHCI_PORT_CCS   equ 0x0001              ; current connect status

PCI_CLASS_SERIAL    equ 0x0C
PCI_SUBCLASS_USB    equ 0x03
PCI_PROGIF_UHCI     equ 0x00            ; universal host controller

;---------------------------------------
; usb_init - bind a UHCI controller, if the machine has one
;
; Called by the driver manager through drv_attach_usb. Returns 0 either way: a
; machine without a USB controller is a machine without one, which is a state
; and not a failure.
;---------------------------------------
usb_init:
        pushad
        mov dword [usb_io_base], 0
        mov dword [usb_ports], 0
        mov dword [usb_connected], 0

        xor edx, edx
.next_dev:
        cmp edx, [pci_dev_count]
        jge .no_controller

        mov esi, edx
        imul esi, PCI_ENTRY_SIZE
        add esi, pci_dev_table

        ; bdf, in this tree's packing: bus<<16 | dev<<8 | fn
        movzx eax, byte [esi + 4]       ; bus
        shl eax, 16
        movzx ecx, byte [esi + 5]       ; dev
        shl ecx, 8
        or eax, ecx
        movzx ecx, byte [esi + 6]       ; fn
        or eax, ecx
        mov [.bdf], eax

        ; Class register: prog-if | subclass | class
        mov ebx, PCI_OFF_CLASS
        call pci_read_config32
        mov ecx, eax
        shr ecx, 24                     ; class
        cmp cl, PCI_CLASS_SERIAL
        jne .next
        mov ecx, eax
        shr ecx, 16
        and ecx, 0xFF                   ; subclass
        cmp cl, PCI_SUBCLASS_USB
        jne .next
        mov ecx, eax
        shr ecx, 8
        and ecx, 0xFF                   ; programming interface
        cmp cl, PCI_PROGIF_UHCI
        jne .next

        ; BAR4 (config 0x20) is the I/O space base for a UHCI controller.
        mov eax, [.bdf]
        mov ebx, 0x20
        call pci_read_config32
        test al, 1                      ; must be an I/O space BAR
        jz .next
        and eax, 0xFFFFFFFC
        test eax, eax
        jz .next                        ; unconfigured: PIO to port 0 would
                                        ; corrupt the legacy space
        mov [usb_io_base], eax

        ; Decode I/O space in the command register.
        mov eax, [.bdf]
        mov ebx, 0x04
        call pci_read_config32
        or eax, 1
        mov ecx, eax
        mov eax, [.bdf]
        mov ebx, 0x04
        call pci_write_config32

        call usb_uhci_reset
        jc .failed

        mov dword [usb_ports], 2
        mov edx, [usb_io_base]
        add edx, UHCI_PORTSC1
        in ax, dx
        test ax, UHCI_PORT_CCS
        jz .port2
        inc dword [usb_connected]
.port2:
        mov edx, [usb_io_base]
        add edx, UHCI_PORTSC2
        in ax, dx
        test ax, UHCI_PORT_CCS
        jz .report
        inc dword [usb_connected]

.report:
        LOGBOOT LOG_INFO, msg_usb_uhci
        cmp dword [usb_connected], 0
        je .report_empty
        LOGBOOT LOG_INFO, msg_usb_connected
        jmp .done
.report_empty:
        LOGBOOT LOG_INFO, msg_usb_empty
.done:
        popad
        ret

.failed:
        LOGBOOT LOG_WARN, msg_usb_reset
        mov dword [usb_io_base], 0
        jmp .done

.next:
        inc edx
        jmp .next_dev

.no_controller:
        popad
        ret

.bdf:   dd 0

;---------------------------------------
; usb_uhci_reset - reset the controller and leave it stopped
;   CF=0 on success, CF=1 if it would not come out of reset
;
; The frame list is not programmed and the controller is not started: a frame
; list is what the next step needs, and running a controller with none is a
; bus that answers nothing.
;---------------------------------------
usb_uhci_reset:
        push eax
        push ecx
        push edx

        mov edx, [usb_io_base]
        mov ax, UHCI_CMD_HCRESET
        out dx, ax

        ; The reset bit clears itself when the controller is done. A controller
        ; that never clears it is not one this driver can use.
        mov ecx, 100000
.wait:
        in ax, dx
        test ax, UHCI_CMD_HCRESET
        jz .ok
        dec ecx
        jnz .wait
        stc
        jmp .out
.ok:
        ; Leave it stopped, with no interrupts and a defined frame number.
        xor ax, ax
        out dx, ax                      ; command: not running
        mov edx, [usb_io_base]
        add edx, UHCI_INTR
        xor ax, ax
        out dx, ax
        mov edx, [usb_io_base]
        add edx, UHCI_FRNUM
        xor ax, ax
        out dx, ax
        clc
.out:
        pop edx
        pop ecx
        pop eax
        ret

;---------------------------------------
; usb_state - what the controller stack found, for a caller that has no shell
;   Out: EAX = controller I/O base (0 = none), EBX = ports, ECX = connected
;---------------------------------------
usb_state:
        mov eax, [usb_io_base]
        mov ebx, [usb_ports]
        mov ecx, [usb_connected]
        ret

msg_usb_uhci:      db "usb: UHCI host controller ready", 0
msg_usb_connected: db "usb: a device is plugged into a root port", 0
msg_usb_empty:     db "usb: no device on the root ports", 0
msg_usb_reset:     db "usb: the UHCI controller would not come out of reset", 0
