;=======================================================================
; usb.inc - USB host controllers
;
; M8 starts here. The milestone's exit line is "plug in a USB stick and copy a
; file, unplug it safely", and the road to that is a host controller stack, hub
; enumeration, and then a mass-storage class driver. This is the first part of
; the first of those: find a UHCI controller, bring it out of reset, install a
; schedule and start it, read its root ports, and enumerate the device on one
; of them - reset the port, read its descriptors and give it an address.
;
; What is here is deliberately small and honest. One controller, one queue
; head, one control transfer at a time, and one device on a root port. A
; device that hangs further devices off a hub, and the mass-storage class that
; makes the milestone's exit line real, are later steps in the same milestone.
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
UHCI_CMD_MAXP   equ 0x0080              ; 64-byte packets rather than 32
UHCI_STS_HCHALT equ 0x0020              ; controller halted
UHCI_PORT_CCS   equ 0x0001              ; current connect status
UHCI_PORT_CSC   equ 0x0002              ; connect status change
UHCI_PORT_PED   equ 0x0004              ; port enabled
UHCI_PORT_PR    equ 0x0200              ; port reset
UHCI_PORT_LS    equ 0x0100              ; low-speed device attached

; Link pointer bits, shared by frame list entries and TD links
UHCI_PTR_TERM   equ 0x00000001          ; terminate
UHCI_PTR_QH     equ 0x00000002          ; this pointer names a queue head
UHCI_PTR_DEPTH  equ 0x00000004          ; walk the element chain, not the
                                        ; horizontal link, when a descriptor
                                        ; completes

; Transfer descriptor status word. The actual length lives in the low eleven
; bits, encoded as length-1, and everything else sits high - a first attempt
; with the spec's bit numbers shifted down by four read a completed transfer as
; a failed one.
TD_ACTIVE       equ 0x00800000          ; controller owns the descriptor
TD_STALLED      equ 0x00400000
TD_DBUFERR      equ 0x00200000
TD_BABBLE       equ 0x00100000
TD_NAK          equ 0x00080000
TD_CRCTIMEO     equ 0x00040000
TD_BITSTUFF     equ 0x00020000
TD_IOC          equ 0x01000000          ; interrupt on completion
TD_SPD          equ 0x20000000          ; short packet detect
TD_EC3          equ 0x18000000          ; error counter: three retries
TD_ERRMASK      equ 0x007E0000          ; stalled, buffer, babble, NAK, CRC, stuff
TD_ACTLEN       equ 0x000007FF          ; actual length, encoded as length-1

; Token word
TD_PID_SETUP    equ 0x2D
TD_PID_IN       equ 0x69
TD_PID_OUT      equ 0xE1

; Offsets inside the page of structures (one page, allocated at usb_mem)
USB_QH          equ 0x000
USB_TD_SETUP    equ 0x010
USB_TD_DATA     equ 0x020
USB_TD_STATUS   equ 0x030
USB_SETUP_BUF   equ 0x040
USB_DESC_BUF    equ 0x080
USB_HID_BUF     equ 0x100
USB_MSC_CBW     equ 0x200               ; 31-byte command block wrapper
USB_MSC_CB      equ 0x220               ; 16-byte SCSI command block
USB_MSC_CSW     equ 0x240               ; 13-byte command status wrapper
USB_MSC_BUF     equ 0x300               ; one 512-byte block of data

USB_DESC_DEVICE  equ 0x01
USB_DESC_CONFIG  equ 0x02
USB_DESC_INTERFACE equ 0x04
USB_DESC_ENDPOINT  equ 0x05
USB_REQ_GET_DESC    equ 0x06
USB_REQ_SET_ADDRESS equ 0x05
USB_REQ_SET_CONFIG  equ 0x09
USB_REQ_SET_IDLE    equ 0x0A
USB_REQ_SET_PROTOCOL equ 0x0B
USB_REQ_MSC_RESET   equ 0xFF            ; bulk-only mass storage reset

USB_CLASS_MASS_STORAGE  equ 0x08
USB_MSC_SUBCLASS_SCSI   equ 0x06        ; SCSI transparent command set
USB_MSC_PROTO_BULKONLY  equ 0x50        ; bulk-only transport

; Bulk-only transport wrappers. Both are read as little-endian dwords, and
; "USBC"/"USBS" are their ASCII spellings on the wire.
USB_CBW_SIGNATURE   equ 0x43425355
USB_CSW_SIGNATURE   equ 0x53425355
USB_CBW_SIZE        equ 31
USB_CSW_SIZE        equ 13
USB_CSW_OK          equ 0

SCSI_INQUIRY        equ 0x12
SCSI_REQUEST_SENSE  equ 0x03
SCSI_READ_CAPACITY  equ 0x25
SCSI_READ10         equ 0x28

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

        ; A schedule, and then the ports. A controller with no frame list is a
        ; bus that answers nothing, so it is started first and the ports are
        ; only worth reading once it runs.
        call usb_start
        jc .report_empty

        cmp dword [usb_connected], 0
        je .report_empty
        LOGBOOT LOG_INFO, msg_usb_connected

        call usb_enumerate
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
; usb_start - install a schedule and start the controller
;   Out: CF=0 if it is running, CF=1 if it would not start
;
; One frame list, one queue head, and nothing on it. That is a real schedule -
; the controller walks it every millisecond and finds nothing to do - and it is
; what every transfer will be hung from: a control transfer is a chain of three
; transfer descriptors linked into the queue head's vertical element.
;
; Starting is the test, not a formality. A controller handed a frame list whose
; pointers are wrong does not run: it raises a host controller process error
; and halts, which is exactly what the caller checks for.
;---------------------------------------
usb_start:
        pushad
        mov dword [usb_running], 0

        call pmm_alloc_page
        test eax, eax
        jz .no_memory
        mov [usb_flbase], eax
        call pmm_alloc_page
        test eax, eax
        jz .no_memory
        mov [usb_mem], eax

        ; Both pages come from the frame allocator, which does not promise
        ; its memory is clear.
        mov edi, [usb_flbase]
        xor eax, eax
        mov ecx, 1024
        cld
        rep stosd
        mov edi, [usb_mem]
        mov ecx, 1024
        rep stosd

        ; The queue head: its horizontal link and its vertical element both
        ; terminate, so the walk stops here until a transfer is hung below it.
        mov edi, [usb_mem]
        mov dword [edi + USB_QH], UHCI_PTR_TERM
        mov dword [edi + USB_QH + 4], UHCI_PTR_TERM

        ; Every frame points at that queue head.
        mov eax, [usb_mem]
        add eax, USB_QH
        or eax, UHCI_PTR_QH
        mov edi, [usb_flbase]
        mov ecx, 1024
        rep stosd

        ; Frame list base. It must be 4 KB aligned, and the allocator's pages
        ; are.
        mov edx, [usb_io_base]
        add edx, UHCI_FLBASE
        mov eax, [usb_flbase]
        out dx, eax

        ; Clear any status the reset left behind, then run.
        mov edx, [usb_io_base]
        add edx, UHCI_STS
        mov ax, 0x003F
        out dx, ax

        mov edx, [usb_io_base]
        mov ax, UHCI_CMD_RUN | UHCI_CMD_MAXP
        out dx, ax

        ; A controller that cannot walk the schedule halts instead of running.
        mov ebp, 200000
.wait:
        mov edx, [usb_io_base]
        add edx, UHCI_STS
        in ax, dx
        test ax, UHCI_STS_HCHALT
        jz .running
        dec ebp
        jnz .wait
        stc
        jmp .out
.running:
        mov dword [usb_running], 1
        LOGBOOT LOG_INFO, msg_usb_running

        clc
        jmp .out
.no_memory:
        stc
.out:
        popad
        ret

;---------------------------------------
; usb_control - one control transfer, setup then data then status
;   [usb_xfer_addr]  device address
;   [usb_xfer_setup] physical address of the 8-byte setup packet
;   [usb_xfer_buf]   physical address of the data buffer (may be 0)
;   [usb_xfer_len]   bytes to move in the data stage (0 = no data stage)
;   Out: CF=0 and EAX = bytes the controller reports it moved, CF=1 on error
;
; Three descriptors, chained in the queue head's element: setup, data, status.
; The status stage is an OUT with no data when there was a data stage and an IN
; when there was not, which is the one asymmetry of a control transfer.
;---------------------------------------
usb_control:
        pushad
        cmp dword [usb_running], 0
        je .fail

        mov ebx, [usb_mem]              ; structures
        mov esi, [usb_xfer_addr]
        shl esi, 8                      ; device address sits at bits 14:8
        mov edi, [usb_xfer_len]
        mov ecx, edi
        test ecx, ecx                   ; the field is length-1, and a
        jz .zero_len                    ; zero-length stage is all ones
        dec ecx
        jmp .len_ready
.zero_len:
        mov ecx, TD_ACTLEN
.len_ready:
        shl ecx, 21                     ; max length, bits 31:21

        ; --- setup ---
        mov eax, TD_ACTIVE | TD_EC3
        mov [ebx + USB_TD_SETUP + 4], eax
        mov eax, (7 << 21) | TD_PID_SETUP   ; eight bytes, encoded as 8-1
        or eax, esi
        mov [ebx + USB_TD_SETUP + 8], eax
        mov eax, ebx
        add eax, USB_SETUP_BUF
        mov [ebx + USB_TD_SETUP + 12], eax
        mov eax, ebx
        add eax, USB_TD_DATA
        test edi, edi
        jnz .have_data
        mov eax, ebx
        add eax, USB_TD_STATUS
.have_data:
        mov [ebx + USB_TD_SETUP], eax   ; setup runs before the data stage

        ; --- data ---
        test edi, edi
        jz .no_data
        mov eax, TD_ACTIVE | TD_EC3 | TD_SPD | TD_IOC
        mov [ebx + USB_TD_DATA + 4], eax
        mov eax, ecx
        or eax, (1 << 19) | TD_PID_IN   ; data stage is DATA1
        or eax, esi
        mov [ebx + USB_TD_DATA + 8], eax
        mov eax, [usb_xfer_buf]
        mov [ebx + USB_TD_DATA + 12], eax
        mov eax, ebx
        add eax, USB_TD_STATUS
        mov [ebx + USB_TD_DATA], eax
.no_data:

        ; --- status ---
        mov eax, TD_ACTIVE | TD_EC3 | TD_IOC
        mov [ebx + USB_TD_STATUS + 4], eax
        test edi, edi
        jz .status_in
        mov eax, (1 << 19) | TD_PID_OUT  ; after a data stage: OUT, DATA1
        jmp .status_token
.status_in:
        mov eax, (1 << 19) | TD_PID_IN   ; no data stage: IN, DATA1
        or eax, TD_SPD
.status_token:
        or eax, esi
        mov [ebx + USB_TD_STATUS + 8], eax
        mov dword [ebx + USB_TD_STATUS + 12], 0
        mov dword [ebx + USB_TD_STATUS], UHCI_PTR_TERM

        ; Hang the chain and let the controller find it. The depth-first bit is
        ; not decoration: without it the controller walks only the first
        ; descriptor and then leaves by the horizontal link, so the setup stage
        ; runs and the data stage never does.
        mov eax, ebx
        add eax, USB_TD_SETUP
        or eax, UHCI_PTR_DEPTH
        mov [ebx + USB_QH + 4], eax

        mov ebp, 2000000
.poll:
        mov eax, [ebx + USB_TD_STATUS + 4]
        test eax, TD_ACTIVE
        jz .done
        dec ebp
        jnz .poll

        ; Timed out: take the chain off the queue head so a half-finished
        ; transfer cannot be walked again, and report it.
        mov dword [ebx + USB_QH + 4], UHCI_PTR_TERM
        jmp .fail

.done:
        mov dword [ebx + USB_QH + 4], UHCI_PTR_TERM

        mov eax, [ebx + USB_TD_STATUS + 4]
        mov ecx, eax
        and ecx, TD_ERRMASK
        mov eax, [ebx + USB_TD_SETUP + 4]
        and eax, TD_ERRMASK
        or ecx, eax
        mov eax, [ebx + USB_TD_DATA + 4]
        and eax, TD_ERRMASK
        or ecx, eax
        jnz .fail_after
        mov eax, [ebx + USB_TD_DATA + 4]
        and eax, TD_ACTLEN              ; actual length of the data stage,
        inc eax                         ; encoded as length-1
        mov [.result], eax
        popad
        mov eax, [.result]
        clc
        ret
.fail_after:
.fail:
        mov ebx, [usb_mem]
        mov eax, [ebx + USB_TD_SETUP + 4]
        mov [usb_td_status + 0], eax
        mov eax, [ebx + USB_TD_DATA + 4]
        mov [usb_td_status + 4], eax
        mov eax, [ebx + USB_TD_STATUS + 4]
        mov [usb_td_status + 8], eax
        call usb_log_failure
        popad
        stc
        ret
.result: dd 0

;---------------------------------------
; usb_get_descriptor - read a descriptor into the structure page
;   AL = device address, DL = descriptor type, DH = index, ECX = bytes wanted
;   Out: CF=0, [usb_desc_len] = bytes read; CF=1 on error
;---------------------------------------
usb_get_descriptor:
        pushad
        mov [.addr], al
        mov [.type], dl
        mov [.index], dh
        mov [.want], cx

        mov ebx, [usb_mem]
        add ebx, USB_SETUP_BUF
        mov byte [ebx + 0], 0x80        ; device to host, standard, device
        mov byte [ebx + 1], USB_REQ_GET_DESC
        mov byte [ebx + 2], 0           ; wValue low: index
        mov byte [ebx + 3], dl          ; wValue high: descriptor type
        mov eax, [.index]
        mov [ebx + 2], al
        mov byte [ebx + 4], 0           ; wIndex
        mov byte [ebx + 5], 0
        mov word [ebx + 6], cx          ; wLength

        movzx eax, byte [.addr]
        mov [usb_xfer_addr], eax
        mov eax, ebx
        mov [usb_xfer_setup], eax
        mov eax, [usb_mem]
        add eax, USB_DESC_BUF
        mov [usb_xfer_buf], eax
        movzx eax, word [.want]
        mov [usb_xfer_len], eax

        call usb_control
        jc .out
        mov [usb_desc_len], eax
        clc
.out:
        popad
        ret

.addr:  db 0
.type:  db 0
.index: db 0
.want:  dw 0

;---------------------------------------
; usb_set_address - give the device the address it will answer on
;   AL = the address
;---------------------------------------
usb_set_address:
        pushad
        mov [.new], al
        mov ebx, [usb_mem]
        add ebx, USB_SETUP_BUF
        mov byte [ebx + 0], 0x00        ; host to device, standard, device
        mov byte [ebx + 1], USB_REQ_SET_ADDRESS
        mov [ebx + 2], al               ; wValue: the address
        mov byte [ebx + 3], 0
        mov byte [ebx + 4], 0
        mov byte [ebx + 5], 0
        mov word [ebx + 6], 0           ; no data stage

        mov dword [usb_xfer_addr], 0    ; still at address zero
        mov eax, ebx
        mov [usb_xfer_setup], eax
        mov dword [usb_xfer_buf], 0
        mov dword [usb_xfer_len], 0
        call usb_control
        popad
        ret
.new:   db 0

;---------------------------------------
; usb_set_configuration - move the device from addressed to configured
;   AL = configuration value
;   Out: CF=0 if the transfer completed, CF=1 if it did not
;---------------------------------------
usb_set_configuration:
        pushad
        mov [.cfg], al
        mov ebx, [usb_mem]
        add ebx, USB_SETUP_BUF
        mov byte [ebx + 0], 0x00        ; host to device, standard, device
        mov byte [ebx + 1], USB_REQ_SET_CONFIG
        mov [ebx + 2], al               ; wValue: the configuration value
        mov byte [ebx + 3], 0
        mov byte [ebx + 4], 0
        mov byte [ebx + 5], 0
        mov word [ebx + 6], 0           ; no data stage

        mov eax, [usb_dev_addr]
        mov [usb_xfer_addr], eax
        mov eax, ebx
        mov [usb_xfer_setup], eax
        mov dword [usb_xfer_buf], 0
        mov dword [usb_xfer_len], 0
        call usb_control
        popad
        ret
.cfg:   db 0

;---------------------------------------
; usb_hid_request - a no-data class request to the boot interface
;   AL = bRequest, BX = wValue
;   Out: CF=0 if the transfer completed, CF=1 if it did not
;---------------------------------------
usb_hid_request:
        pushad
        mov [.req], al
        mov [.val], bx
        mov ebx, [usb_mem]
        add ebx, USB_SETUP_BUF
        mov byte [ebx + 0], 0x21        ; host to device, class, interface
        mov al, [.req]
        mov [ebx + 1], al
        mov ax, [.val]
        mov [ebx + 2], ax               ; wValue
        mov byte [ebx + 4], 0           ; wIndex: interface 0
        mov byte [ebx + 5], 0
        mov word [ebx + 6], 0           ; no data stage

        mov eax, [usb_dev_addr]
        mov [usb_xfer_addr], eax
        mov eax, ebx
        mov [usb_xfer_setup], eax
        mov dword [usb_xfer_buf], 0
        mov dword [usb_xfer_len], 0
        call usb_control
        popad
        ret
.req:   db 0
.val:   dw 0

;---------------------------------------
; usb_hid_start - find the HID boot keyboard and switch it to boot protocol
;
; The configuration descriptor is already in the structure page. This walks it
; for the boot-keyboard interface and its interrupt IN endpoint, then sends the
; two class requests that put the keyboard in boot protocol. Reading the reports
; is the next step; a device with no HID interface (a stick, a mouse) returns
; without a word because it is not this driver's job yet.
;---------------------------------------
usb_hid_start:
        pushad

        ; Walk the configuration descriptor for the boot keyboard's interrupt
        ; IN endpoint. edx tracks whether the last interface was that keyboard.
        mov esi, [usb_mem]
        add esi, USB_DESC_BUF
        mov ecx, [usb_cfg_len]
        xor edx, edx
.walk:
        test ecx, ecx
        jbe .no_hid
        movzx eax, byte [esi]           ; bLength
        cmp eax, 2
        jb .no_hid
        cmp eax, ecx
        ja .no_hid
        movzx ebx, byte [esi + 1]       ; bDescriptorType
        cmp ebx, USB_DESC_INTERFACE
        jne .not_interface
        cmp byte [esi + 5], 0x03        ; bInterfaceClass: HID
        jne .hid_off
        cmp byte [esi + 6], 0x01        ; bInterfaceSubClass: boot
        jne .hid_off
        cmp byte [esi + 7], 0x01        ; bInterfaceProtocol: keyboard
        jne .hid_off
        mov edx, 1
        jmp .advance
.hid_off:
        xor edx, edx
        jmp .advance
.not_interface:
        cmp ebx, USB_DESC_ENDPOINT
        jne .advance
        test edx, edx
        jz .advance
        movzx ebx, byte [esi + 2]       ; bEndpointAddress
        test bl, 0x80                   ; direction IN
        jz .advance
        movzx ebx, byte [esi + 3]       ; bmAttributes
        and bl, 0x03
        cmp bl, 0x03                    ; transfer type interrupt
        jne .advance
        movzx eax, byte [esi + 2]
        mov [usb_hid_ep], eax
        movzx eax, word [esi + 4]       ; wMaxPacketSize
        mov [usb_hid_maxpkt], eax
        jmp .found
.advance:
        movzx eax, byte [esi]
        add esi, eax
        sub ecx, eax
        jmp .walk

.found:
        ; Boot protocol, then a 4 ms idle rate so the keyboard reports its
        ; state even before a key is pressed - which is also what lets the
        ; pipe be proven here, with a report, rather than with a key.
        mov al, USB_REQ_SET_PROTOCOL
        xor bx, bx                      ; wValue 0: boot protocol
        call usb_hid_request
        jc .fail
        mov al, USB_REQ_SET_IDLE
        mov bx, 0x0100                  ; duration 1 (4 ms), report 0
        call usb_hid_request
        jc .fail
        call usb_intr_in
        jc .fail
        LOGBOOT LOG_INFO, msg_usb_report
        call usb_log_hid
        jmp .out
.fail:
        LOGBOOT LOG_WARN, msg_usb_no_hid
        jmp .out
.no_hid:
        ; Not a boot keyboard: say nothing, it is not this driver's job.
.out:
        popad
        ret

;---------------------------------------
; usb_hid_arm_td - build and hang the interrupt IN descriptor
;   Out: CF=0 if armed, CF=1 if there is no HID keyboard
;   Preserves all registers except flags.
;---------------------------------------
usb_hid_arm_td:
        push eax
        push ecx
        push edx
        push ebx
        cmp dword [usb_hid_ep], 0
        je .fail
        mov ebx, [usb_mem]

        mov eax, [usb_hid_maxpkt]
        test eax, eax
        jz .fail
        cmp eax, 2048                  ; the token's length field is 11 bits
        ja .fail
        dec eax
        shl eax, 21                     ; max length, bits 31:21

        mov ecx, [usb_dev_addr]
        shl ecx, 8
        mov edx, [usb_hid_ep]
        and edx, 0x0F                   ; endpoint number only, bit 7 is the
        shl edx, 15                     ; direction and the PID says it
        or ecx, edx
        or eax, ecx
        or eax, TD_PID_IN

        mov [ebx + USB_TD_DATA + 8], eax                ; token
        mov eax, TD_ACTIVE | TD_IOC | TD_SPD | TD_EC3
        mov [ebx + USB_TD_DATA + 4], eax                ; status
        mov eax, ebx
        add eax, USB_HID_BUF
        mov [ebx + USB_TD_DATA + 12], eax               ; buffer
        mov dword [ebx + USB_TD_DATA], UHCI_PTR_TERM

        mov eax, ebx
        add eax, USB_TD_DATA
        mov [ebx + USB_QH + 4], eax                     ; hang below the queue head
        clc
        jmp .out
.fail:
        stc
.out:
        pop ebx
        pop edx
        pop ecx
        pop eax
        ret

;---------------------------------------
; usb_intr_in - one interrupt IN transfer from the HID endpoint, waited out
;   Out: CF=0 and EAX = bytes read, CF=1 on error
;
; The boot-time proof: one descriptor is armed and waited on. The steady state
; is usb_hid_poll, which arms and checks without waiting.
;---------------------------------------
usb_intr_in:
        pushad
        call usb_hid_arm_td
        jc .fail
        mov ebx, [usb_mem]
        mov ebp, 20                     ; up to ~20 ms for a report
.poll:
        mov eax, [ebx + USB_TD_DATA + 4]
        test eax, TD_ACTIVE
        jz .done
        mov ecx, 1
        call smp_delay_ms
        dec ebp
        jnz .poll
        mov dword [ebx + USB_QH + 4], UHCI_PTR_TERM
        jmp .fail

.done:
        mov dword [ebx + USB_QH + 4], UHCI_PTR_TERM
        mov eax, [ebx + USB_TD_DATA + 4]
        mov ecx, eax
        and ecx, TD_ERRMASK
        jnz .fail_after
        and eax, TD_ACTLEN
        inc eax
        mov [.result], eax
        popad
        mov eax, [.result]
        clc
        ret
.fail_after:
        mov ebx, [usb_mem]
        mov eax, [ebx + USB_TD_DATA + 4]
        mov [usb_td_status + 0], eax
        mov dword [usb_td_status + 4], 0
        mov dword [usb_td_status + 8], 0
        call usb_log_failure
        popad
        stc
        ret
.fail:
        popad
        stc
        ret
.result: dd 0

;---------------------------------------
; usb_hid_poll - poll the HID keyboard's interrupt endpoint, no waiting
;
; Called from the timer interrupt with interrupts off. If the controller is
; still waiting for the device (a NAK), the descriptor stays hung and is
; retried every frame. When a report has completed it is translated and the
; descriptor is re-armed. The drain is bounded so one tick cannot get stuck.
;---------------------------------------
usb_hid_poll:
        pushad
        cmp dword [usb_hid_ep], 0
        je .out
        cmp dword [usb_msc_busy], 0
        jne .out                        ; a bulk transfer owns the queue head
        mov ebp, 8                      ; bound the reports drained per tick
.drain:
        test ebp, ebp
        jz .out
        dec ebp
        mov ebx, [usb_mem]
        cmp dword [usb_hid_armed], 0
        je .arm
        mov eax, [ebx + USB_TD_DATA + 4]
        test eax, TD_ACTIVE
        jnz .out                        ; controller is retrying a NAK
        mov dword [usb_hid_armed], 0
        and eax, TD_ERRMASK
        jnz .arm                        ; errored: drop it and re-arm
        call usb_hid_process_report
        jmp .drain
.arm:
        call usb_hid_arm_td
        jc .out
        mov dword [usb_hid_armed], 1
        jmp .drain
.out:
        popad
        ret

;---------------------------------------
; usb_hid_process_report - turn one HID boot report into key events
;
; The report is at USB_HID_BUF: a modifier byte, a reserved byte, then six
; keycodes. The previous report is diffed against this one so each key that
; went down or up becomes one press or release on the shared keyboard path.
;---------------------------------------
usb_hid_process_report:
        pushad
        mov ebx, [usb_mem]
        add ebx, USB_HID_BUF

        ; --- modifier diff: six bits carry a meaning ---
        mov dl, [ebx]                   ; current modifiers
        mov dh, [usb_hid_prev_mod]      ; previous modifiers
        cmp dl, dh
        je .mods_done
        ; bit 0: Left Ctrl -> 0x1D
        mov cl, dl
        xor cl, dh
        test cl, 0x01
        jz .m1
        mov al, 0x1D
        test dl, 0x01
        jnz .m0_press
        or al, 0x80
.m0_press:
        call kbd_handle_scancode
.m1:
        ; bit 1: Left Shift -> 0x2A
        mov cl, dl
        xor cl, dh
        test cl, 0x02
        jz .m2
        mov al, 0x2A
        test dl, 0x02
        jnz .m1_press
        or al, 0x80
.m1_press:
        call kbd_handle_scancode
.m2:
        ; bit 2: Left Alt -> 0x38
        mov cl, dl
        xor cl, dh
        test cl, 0x04
        jz .m3
        mov al, 0x38
        test dl, 0x04
        jnz .m2_press
        or al, 0x80
.m2_press:
        call kbd_handle_scancode
.m3:
        ; bit 4: Right Ctrl -> 0x1D (the shared path has one ctrl flag)
        mov cl, dl
        xor cl, dh
        test cl, 0x10
        jz .m4
        mov al, 0x1D
        test dl, 0x10
        jnz .m3_press
        or al, 0x80
.m3_press:
        call kbd_handle_scancode
.m4:
        ; bit 5: Right Shift -> 0x36
        mov cl, dl
        xor cl, dh
        test cl, 0x20
        jz .m5
        mov al, 0x36
        test dl, 0x20
        jnz .m4_press
        or al, 0x80
.m4_press:
        call kbd_handle_scancode
.m5:
        ; bit 6: Right Alt -> 0x38 (the shared path has one alt flag)
        mov cl, dl
        xor cl, dh
        test cl, 0x40
        jz .mods_done
        mov al, 0x38
        test dl, 0x40
        jnz .m5_press
        or al, 0x80
.m5_press:
        call kbd_handle_scancode
.mods_done:
        mov [usb_hid_prev_mod], dl

        ; --- keycode diff: presses first, then releases ---
        xor edi, edi
.key_press:
        movzx eax, byte [ebx + 2 + edi] ; current keycode
        test eax, eax
        jz .kp_next
        call usb_hid_key_in_prev
        jc .kp_next                     ; already down in the last report
        xor ch, ch                      ; press
        call usb_hid_send_key
.kp_next:
        inc edi
        cmp edi, 6
        jb .key_press

        xor edi, edi
.key_release:
        movzx eax, byte [usb_hid_prev_keys + edi]
        test eax, eax
        jz .kr_next
        call usb_hid_key_in_cur
        jc .kr_next                     ; still down in this report
        mov ch, 1                       ; release
        call usb_hid_send_key
.kr_next:
        inc edi
        cmp edi, 6
        jb .key_release

        ; Save this report as the previous one.
        cld
        mov esi, ebx
        add esi, 2
        mov edi, usb_hid_prev_keys
        mov ecx, 6
        rep movsb
        popad
        ret

; usb_hid_key_in_prev - is AL among the six previous keycodes?
;   Out: CF=1 if present. Preserves all registers except flags.
usb_hid_key_in_prev:
        push eax
        push ecx
        push edi
        mov edi, usb_hid_prev_keys
        mov ecx, 6
.loop:
        cmp al, [edi]
        je .yes
        inc edi
        dec ecx
        jnz .loop
        clc
        jmp .out
.yes:
        stc
.out:
        pop edi
        pop ecx
        pop eax
        ret

; usb_hid_key_in_cur - is AL among the six current keycodes?
;   Out: CF=1 if present. Preserves all registers except flags.
usb_hid_key_in_cur:
        push eax
        push ecx
        push edi
        push ebx
        mov ebx, [usb_mem]
        add ebx, USB_HID_BUF + 2
        mov ecx, 6
.loop:
        cmp al, [ebx]
        je .yes
        inc ebx
        dec ecx
        jnz .loop
        clc
        jmp .out
.yes:
        stc
.out:
        pop ebx
        pop edi
        pop ecx
        pop eax
        ret

;---------------------------------------
; usb_hid_send_key - translate a HID usage (AL) and send one key event
;   AL = usage, CH = 0 press / 1 release
;   Preserves all registers.
;
; Values in hid_scancode_table feed the same path the PS/2 keyboard uses. The
; six extended navigation keys are sent as an E0 prefix and a make code; their
; releases are ignored by that path, so they send nothing on release.
;---------------------------------------
usb_hid_send_key:
        push eax
        push ebx
        push ecx
        movzx ebx, al
        cmp ebx, 0x66
        jae .done                      ; outside the boot-keyboard range
        cmp bl, 0x52                   ; Up
        je .ext
        cmp bl, 0x51                   ; Down
        je .ext
        cmp bl, 0x50                   ; Left
        je .ext
        cmp bl, 0x4F                   ; Right
        je .ext
        cmp bl, 0x4B                   ; PageUp
        je .ext
        cmp bl, 0x4E                   ; PageDown
        je .ext
        mov al, [hid_scancode_table + ebx]
        test al, al
        jz .done                       ; not mapped (keypad and friends)
        test ch, ch
        jz .send
        or al, 0x80                    ; release bit
.send:
        call kbd_handle_scancode
        jmp .done
.ext:
        test ch, ch
        jnz .done                      ; extended releases are ignored
        mov al, 0xE0
        call kbd_handle_scancode
        cmp bl, 0x52
        jne .x1
        mov al, 0x48                   ; Up
        jmp .send
.x1:
        cmp bl, 0x51
        jne .x2
        mov al, 0x50                   ; Down
        jmp .send
.x2:
        cmp bl, 0x50
        jne .x3
        mov al, 0x4B                   ; Left
        jmp .send
.x3:
        cmp bl, 0x4F
        jne .x4
        mov al, 0x4D                   ; Right
        jmp .send
.x4:
        cmp bl, 0x4B
        jne .x5
        mov al, 0x49                   ; PageUp
        jmp .send
.x5:
        mov al, 0x51                   ; PageDown
        jmp .send
.done:
        pop ecx
        pop ebx
        pop eax
        ret

;---------------------------------------
; usb_msc_start - find a bulk-only mass-storage interface and prove it
;
; The configuration descriptor is walked for the interface whose class is mass
; storage, subclass SCSI transparent and protocol bulk-only, and for the bulk
; IN and OUT endpoints under it. What follows is the introduction the bulk-only
; transport prescribes: a class reset, INQUIRY to make the device describe
; itself, READ CAPACITY to learn how big it is, and one READ(10) of block zero -
; so "a block was read" is a block that crossed the wire, not a descriptor that
; was parsed.
;
; A device that is not mass storage walks past silently: it is not this
; driver's job.
;---------------------------------------
usb_msc_start:
        pushad
        mov dword [usb_msc_ep_in], 0
        mov dword [usb_msc_ep_out], 0
        mov dword [usb_msc_ready], 0

        ; edx tracks whether the last interface was the mass-storage one, the
        ; same walk usb_hid_start does for the keyboard.
        mov esi, [usb_mem]
        add esi, USB_DESC_BUF
        mov ecx, [usb_cfg_len]
        xor edx, edx
.walk:
        test ecx, ecx
        jbe .found
        movzx eax, byte [esi]           ; bLength
        cmp eax, 2
        jb .out
        cmp eax, ecx
        ja .out
        movzx ebx, byte [esi + 1]       ; bDescriptorType
        cmp ebx, USB_DESC_INTERFACE
        jne .not_interface
        cmp byte [esi + 5], USB_CLASS_MASS_STORAGE
        jne .msc_off
        cmp byte [esi + 6], USB_MSC_SUBCLASS_SCSI
        jne .msc_off
        cmp byte [esi + 7], USB_MSC_PROTO_BULKONLY
        jne .msc_off
        movzx eax, byte [esi + 2]       ; bInterfaceNumber: the reset names it
        mov [usb_msc_iface], eax
        mov edx, 1
        jmp .advance
.msc_off:
        xor edx, edx
        jmp .advance
.not_interface:
        cmp ebx, USB_DESC_ENDPOINT
        jne .advance
        test edx, edx
        jz .advance
        movzx eax, byte [esi + 3]       ; bmAttributes
        and eax, 0x03
        cmp eax, 2                      ; transfer type bulk
        jne .advance
        movzx ebx, byte [esi + 2]       ; bEndpointAddress
        test bl, 0x80                   ; direction IN
        jz .out_ep
        mov [usb_msc_ep_in], ebx
        movzx eax, word [esi + 4]       ; wMaxPacketSize
        mov [usb_msc_maxpkt_in], eax
        jmp .advance
.out_ep:
        mov [usb_msc_ep_out], ebx
        movzx eax, word [esi + 4]
        mov [usb_msc_maxpkt_out], eax
.advance:
        movzx eax, byte [esi]
        add esi, eax
        sub ecx, eax
        jmp .walk

.found:
        ; A bulk-only device needs both directions. One endpoint without the
        ; other cannot carry a command and its answer, so it is left alone.
        cmp dword [usb_msc_ep_in], 0
        je .out
        cmp dword [usb_msc_ep_out], 0
        je .out
        call usb_log_msc_endpoints
        call usb_msc_probe
.out:
        popad
        ret

;---------------------------------------
; usb_msc_reset - the class reset, and DATA0 on both bulk pipes
;   Out: CF=0 if the device accepted it, CF=1 if it did not
;
; The reset is what makes a device that was left mid-command usable again; it
; also puts both data toggles back to DATA0, which is where the driver's own
; copies have to go with it.
;---------------------------------------
usb_msc_reset:
        pushad
        mov ebx, [usb_mem]
        add ebx, USB_SETUP_BUF
        mov byte [ebx + 0], 0x21        ; host to device, class, interface
        mov byte [ebx + 1], USB_REQ_MSC_RESET
        mov byte [ebx + 2], 0
        mov byte [ebx + 3], 0
        mov eax, [usb_msc_iface]
        mov [ebx + 4], al               ; wIndex: the interface number
        mov byte [ebx + 5], 0
        mov word [ebx + 6], 0           ; no data stage

        mov eax, [usb_dev_addr]
        mov [usb_xfer_addr], eax
        mov eax, ebx
        mov [usb_xfer_setup], eax
        mov dword [usb_xfer_buf], 0
        mov dword [usb_xfer_len], 0
        call usb_control
        jc .out
        mov dword [usb_msc_in_toggle], 0
        mov dword [usb_msc_out_toggle], 0
        clc
.out:
        popad
        ret

;---------------------------------------
; usb_bulk - one bulk transfer on an endpoint
;   EAX = endpoint address (bit 7 set for IN), ECX = bytes, EDI = buffer
;   Out: CF=0 and EAX = bytes moved, CF=1 on error or timeout
;
; The data toggle is not the controller's to flip. A bulk endpoint alternates
; DATA0 and DATA1 across transfers, and a controller handed the wrong one has
; its packet ignored, so the driver keeps the toggle per direction and flips it
; only after a transfer completes - a NAKed descriptor that the controller
; retries is still the same transfer, with the same toggle.
;---------------------------------------
usb_bulk:
        pushad
        mov [.ep], al
        mov [.len], ecx
        mov [.buf], edi
        mov dword [usb_msc_busy], 1

        test al, 0x80
        jz .out_dir
        mov eax, [usb_msc_in_toggle]
        jmp .toggle
.out_dir:
        mov eax, [usb_msc_out_toggle]
.toggle:
        mov [.tog], eax

        mov ebx, [usb_mem]
        mov ecx, [.len]
        dec ecx
        shl ecx, 21                     ; max length, bits 31:21, encoded as n-1
        movzx eax, byte [.ep]
        and eax, 0x0F                   ; endpoint number only
        shl eax, 15
        or ecx, eax
        mov eax, [usb_dev_addr]
        shl eax, 8
        or ecx, eax
        mov eax, [.tog]
        shl eax, 19                     ; DATA1 when set, DATA0 when clear
        or ecx, eax
        movzx eax, byte [.ep]
        test al, 0x80
        jz .pid_out
        or ecx, TD_PID_IN
        jmp .pid
.pid_out:
        or ecx, TD_PID_OUT
.pid:
        mov [ebx + USB_TD_DATA + 8], ecx
        mov eax, TD_ACTIVE | TD_EC3 | TD_IOC | TD_SPD
        mov [ebx + USB_TD_DATA + 4], eax
        mov eax, [.buf]
        mov [ebx + USB_TD_DATA + 12], eax
        mov dword [ebx + USB_TD_DATA], UHCI_PTR_TERM
        mov eax, ebx
        add eax, USB_TD_DATA
        mov [ebx + USB_QH + 4], eax

        ; A block read is slower than a control transfer, and the first one
        ; after a reset makes the device spin up; two seconds is generous and
        ; bounded.
        mov ebp, 2000
.poll:
        mov eax, [ebx + USB_TD_DATA + 4]
        test eax, TD_ACTIVE
        jz .done
        mov ecx, 1
        call smp_delay_ms
        dec ebp
        jnz .poll
        mov dword [ebx + USB_QH + 4], UHCI_PTR_TERM
        jmp .fail
.done:
        mov dword [ebx + USB_QH + 4], UHCI_PTR_TERM
        mov eax, [ebx + USB_TD_DATA + 4]
        test eax, TD_ERRMASK
        jnz .fail

        mov eax, [.tog]
        xor eax, 1
        test byte [.ep], 0x80
        jz .save_out
        mov [usb_msc_in_toggle], eax
        jmp .moved
.save_out:
        mov [usb_msc_out_toggle], eax
.moved:
        mov dword [usb_msc_busy], 0
        mov eax, [ebx + USB_TD_DATA + 4]
        and eax, TD_ACTLEN
        inc eax
        mov [.result], eax
        popad
        mov eax, [.result]
        clc
        ret
.fail:
        mov dword [usb_msc_busy], 0
        mov ebx, [usb_mem]
        mov eax, [ebx + USB_TD_DATA + 4]
        mov [usb_td_status + 0], eax
        mov dword [usb_td_status + 4], 0
        mov dword [usb_td_status + 8], 0
        call usb_log_failure
        popad
        stc
        ret
.ep:    db 0
        times 3 db 0
.len:   dd 0
.buf:   dd 0
.tog:   dd 0
.result: dd 0

;---------------------------------------
; usb_msc_command - one bulk-only transport command
;   ESI = command block, AL = its length, EDX = data length,
;   EBX = 1 for a data-IN stage and 0 for data-OUT, EDI = data buffer
;   Out: CF=0 if the device reported success, CF=1 otherwise
;
; 31 bytes of command block wrapper out, the data stage, 13 bytes of command
; status wrapper in. The wrapper carries a tag and the status repeats it, so a
; status left over from an earlier command is not mistaken for this one.
;---------------------------------------
usb_msc_command:
        pushad
        mov [.cb], esi
        mov [.cblen], al
        mov [.dlen], edx
        mov [.din], bl
        mov [.dbuf], edi

        mov ebx, [usb_mem]
        add ebx, USB_MSC_CBW
        mov eax, USB_CBW_SIGNATURE
        mov [ebx + 0], eax
        mov eax, [usb_msc_tag]
        inc eax
        mov [usb_msc_tag], eax
        mov [.tag], eax
        mov [ebx + 4], eax
        mov eax, [.dlen]
        mov [ebx + 8], eax
        mov al, [.din]
        shl al, 7                       ; 0x80 for IN, 0x00 for OUT
        mov [ebx + 12], al
        mov byte [ebx + 13], 0          ; LUN 0
        mov al, [.cblen]
        mov [ebx + 14], al
        mov esi, [.cb]
        lea edi, [ebx + 15]
        movzx ecx, byte [.cblen]
        cld
        rep movsb

        mov eax, [usb_msc_ep_out]
        mov ecx, USB_CBW_SIZE
        mov edi, ebx
        call usb_bulk
        jc .fail

        mov ecx, [.dlen]
        test ecx, ecx
        jz .status
        mov edi, [.dbuf]
        cmp byte [.din], 0
        je .data_out
        mov eax, [usb_msc_ep_in]
        call usb_bulk
        jc .fail
        jmp .status
.data_out:
        mov eax, [usb_msc_ep_out]
        call usb_bulk
        jc .fail

.status:
        mov eax, [usb_msc_ep_in]
        mov ecx, USB_CSW_SIZE
        mov edi, [usb_mem]
        add edi, USB_MSC_CSW
        call usb_bulk
        jc .fail

        mov ebx, [usb_mem]
        add ebx, USB_MSC_CSW
        cmp dword [ebx + 0], USB_CSW_SIGNATURE
        jne .fail
        mov eax, [.tag]
        cmp [ebx + 4], eax
        jne .bad_csw
        cmp dword [ebx + 8], 0          ; data residue: every byte was moved
        jne .bad_csw
        cmp byte [ebx + 12], USB_CSW_OK
        jne .bad_csw
        clc
        jmp .out
.bad_csw:
        call usb_log_msc_csw
.fail:
        stc
.out:
        popad
        ret
.cb:    dd 0
.cblen: db 0
.din:   db 0
        times 2 db 0
.dlen:  dd 0
.dbuf:  dd 0
.tag:   dd 0

;---------------------------------------
; usb_msc_sense - read and clear the device's sense data
;   Out: CF=0 if the sense transfer completed, CF=1 if it did not
;
; A SCSI device reports a condition once: the command that hit it fails with
; status 1, and the next REQUEST SENSE carries the reason and clears it. The
; sense data is logged at DEBUG, because a device that reports UNIT ATTENTION
; the first time it is seen is normal and a console line for it is noise.
;---------------------------------------
usb_msc_sense:
        pushad
        mov esi, msc_cb_sense
        mov al, 6
        mov edx, 18
        mov ebx, 1
        mov edi, [usb_mem]
        add edi, USB_MSC_BUF
        call usb_msc_command
        jc .out
        call usb_log_msc_sense
        clc
.out:
        popad
        ret

;---------------------------------------
; usb_msc_command_retry - a command, and one more try after clearing a condition
;   In : as usb_msc_command; every register is preserved
;   Out: CF=0 on success, CF=1 if both attempts failed
;
; This is what a stick that was just plugged in needs: the first command that
; is not INQUIRY comes back CHECK CONDITION with UNIT ATTENTION, which is the
; device saying "something changed, look once". The sense read clears it and
; the command is repeated. One retry, not a loop: a device that keeps failing
; is failing.
;---------------------------------------
usb_msc_command_retry:
        pushad
        call usb_msc_command
        jnc .ok
        call usb_msc_sense
        call usb_msc_command
        jc .fail
.ok:
        popad
        clc
        ret
.fail:
        popad
        stc
        ret

;---------------------------------------
; usb_msc_probe - reset, describe, size and read the mass-storage device
;
; Each step is a precondition of the next: INQUIRY says the command transport
; works, READ CAPACITY says how to ask for a block, and READ(10) says the data
; path carries blocks and not just wrappers.
;---------------------------------------
usb_msc_probe:
        pushad
        call usb_msc_reset
        jc .fail

        mov esi, msc_cb_inquiry
        mov al, 6
        mov edx, 36
        mov ebx, 1                      ; data stage IN
        mov edi, [usb_mem]
        add edi, USB_MSC_BUF
        call usb_msc_command_retry
        jc .fail
        call usb_log_msc_inquiry

        mov esi, msc_cb_capacity
        mov al, 10
        mov edx, 8
        mov ebx, 1
        mov edi, [usb_mem]
        add edi, USB_MSC_BUF
        call usb_msc_command_retry
        jc .fail

        ; Last block number and block length, both big-endian on the wire.
        mov ebx, [usb_mem]
        add ebx, USB_MSC_BUF
        mov eax, [ebx]
        bswap eax
        inc eax                         ; last LBA is zero-based
        mov [usb_msc_blocks], eax
        mov eax, [ebx + 4]
        bswap eax
        test eax, eax
        jz .fail
        mov [usb_msc_blocksize], eax
        call usb_log_msc_capacity

        ; One block, block zero. The buffer is one 512-byte sector, so a
        ; device that reports a larger block is not read into it.
        cmp eax, 512
        ja .fail
        mov edx, eax
        mov esi, msc_cb_read
        mov al, 10
        mov ebx, 1
        mov edi, [usb_mem]
        add edi, USB_MSC_BUF
        call usb_msc_command_retry
        jc .fail
        call usb_log_msc_block0
        mov dword [usb_msc_ready], 1
        jmp .out
.fail:
        LOGBOOT LOG_WARN, msg_usb_msc_fail
.out:
        popad
        ret

;---------------------------------------
; usb_msc_read_block - read one block through the bulk transport
;   EAX = LBA, EDI = destination (512 bytes)
;   Out: CF=0 on success, CF=1 on error
;
; The block-device entry point in miniature. The command block is rebuilt each
; call because the LBA lives in it, big-endian at bytes 2-5, and the length at
; bytes 7-8 is one block.
;
; One command at a time, by construction: the shell is the only caller and
; there is one shell. The interrupt path does not touch these structures while
; usb_msc_busy is set.
;---------------------------------------
usb_msc_read_block:
        pushad
        mov [.lba], eax
        mov [.dst], edi

        mov ebx, [usb_mem]
        add ebx, USB_MSC_CB
        mov byte [ebx + 0], SCSI_READ10
        mov byte [ebx + 1], 0
        mov eax, [.lba]
        bswap eax
        mov [ebx + 2], eax              ; LBA, most significant byte first
        mov byte [ebx + 6], 0
        mov byte [ebx + 7], 0
        mov byte [ebx + 8], 1           ; one block
        mov byte [ebx + 9], 0

        mov esi, ebx
        mov al, 10
        mov edx, [usb_msc_blocksize]
        mov ebx, 1                      ; data stage IN
        mov edi, [.dst]
        call usb_msc_command_retry
        popad
        ret
.lba:   dd 0
.dst:   dd 0

;---------------------------------------
; usb_log_msc_endpoints - one line: both bulk endpoints and their packet sizes
;---------------------------------------
usb_log_msc_endpoints:
        pushad
        mov edi, usb_msg_buf
        mov esi, msg_usb_msc_endpoints
.copy:
        lodsb
        test al, al
        jz .in
        stosb
        jmp .copy
.in:
        mov ebx, [usb_msc_ep_in]
        call usb_hex_byte
        mov al, '/'
        stosb
        mov ebx, [usb_msc_ep_out]
        call usb_hex_byte
        mov esi, msg_usb_msc_sep
.copy2:
        lodsb
        test al, al
        jz .inpkt
        stosb
        jmp .copy2
.inpkt:
        mov ebx, [usb_msc_maxpkt_in]
        call usb_hex_word
        mov al, '/'
        stosb
        mov ebx, [usb_msc_maxpkt_out]
        call usb_hex_word
        mov esi, msg_usb_msc_suffix
.copy3:
        lodsb
        test al, al
        jz .done
        stosb
        jmp .copy3
.done:
        xor al, al
        stosb
        mov al, LOG_INFO
        mov esi, usb_msg_buf
        call bootlog_write
        popad
        ret

;---------------------------------------
; usb_log_msc_inquiry - one line: vendor and product, as the device spells them
;---------------------------------------
usb_log_msc_inquiry:
        pushad
        mov edi, usb_msg_buf
        mov esi, msg_usb_msc_inquiry
.copy:
        lodsb
        test al, al
        jz .vendor
        stosb
        jmp .copy
.vendor:
        mov esi, [usb_mem]
        add esi, USB_MSC_BUF + 8        ; t8 vendor identification
        mov ecx, 8
        call .copy_trim
        mov al, ' '
        stosb
.product:
        mov esi, [usb_mem]
        add esi, USB_MSC_BUF + 16       ; t16 product identification
        mov ecx, 16
        call .copy_trim
        xor al, al
        stosb
        mov al, LOG_INFO
        mov esi, usb_msg_buf
        call bootlog_write
        popad
        ret
; ECX bytes from ESI to EDI, trailing spaces dropped. EDI moves with the copy.
.copy_trim:
        push eax
        push ecx
        push esi
        mov ebx, ecx
        lea edx, [esi + ecx]
.trim_back:
        test ebx, ebx
        jz .ct_none
        cmp byte [edx - 1], ' '
        jne .ct_copy
        dec edx
        dec ebx
        jmp .trim_back
.ct_copy:
        mov ecx, ebx
        test ecx, ecx
        jz .ct_done
        cld
        rep movsb
        jmp .ct_done
.ct_none:
        mov al, '?'
        stosb
.ct_done:
        pop esi
        pop ecx
        pop eax
        ret

;---------------------------------------
; usb_log_msc_capacity - one line: blocks and block size, in hex
;---------------------------------------
usb_log_msc_capacity:
        pushad
        mov edi, usb_msg_buf
        mov esi, msg_usb_msc_capacity
.copy:
        lodsb
        test al, al
        jz .blocks
        stosb
        jmp .copy
.blocks:
        mov ebx, [usb_msc_blocks]
        call usb_hex_dword
        mov esi, msg_usb_msc_blocks
.copy2:
        lodsb
        test al, al
        jz .size
        stosb
        jmp .copy2
.size:
        mov ebx, [usb_msc_blocksize]
        call usb_hex_dword
        mov esi, msg_usb_msc_bytes
.copy3:
        lodsb
        test al, al
        jz .done
        stosb
        jmp .copy3
.done:
        xor al, al
        stosb
        mov al, LOG_INFO
        mov esi, usb_msg_buf
        call bootlog_write
        popad
        ret

;---------------------------------------
; usb_log_msc_block0 - one line: the last two bytes of block zero
;
; A signature is what the caller gave the block, so logging it proves the
; bytes came back from the device rather than from a zeroed buffer.
;---------------------------------------
usb_log_msc_block0:
        pushad
        mov edi, usb_msg_buf
        mov esi, msg_usb_msc_block0
.copy:
        lodsb
        test al, al
        jz .sig
        stosb
        jmp .copy
.sig:
        mov esi, [usb_mem]
        add esi, USB_MSC_BUF
        add esi, [usb_msc_blocksize]
        mov bl, [esi - 2]
        call usb_hex_byte
        mov bl, [esi - 1]
        call usb_hex_byte
        xor al, al
        stosb
        mov al, LOG_INFO
        mov esi, usb_msg_buf
        call bootlog_write
        popad
        ret

;---------------------------------------
; usb_log_msc_csw - one line: the status wrapper that was rejected
;
; A rejection with no numbers is a bug report that says "it did not work". The
; four fields say which check failed: a signature of zero means nothing came
; back, a tag that is not the one just sent means an answer from an earlier
; command, a residue that is not zero means the data stage was short, and a
; status of 1 means the device rejected the command itself.
;---------------------------------------
usb_log_msc_csw:
        pushad
        mov ebx, [usb_mem]
        add ebx, USB_MSC_CSW
        mov edi, usb_msg_buf
        mov esi, msg_usb_msc_csw
.copy:
        lodsb
        test al, al
        jz .sig
        stosb
        jmp .copy
.sig:
        mov ebx, [ebx + 0]
        call usb_hex_dword
        mov esi, msg_usb_msc_csw_tag
.copy2:
        lodsb
        test al, al
        jz .tag
        stosb
        jmp .copy2
.tag:
        mov ebx, [usb_mem]
        mov ebx, [ebx + USB_MSC_CSW + 4]
        call usb_hex_dword
        mov esi, msg_usb_msc_csw_res
.copy3:
        lodsb
        test al, al
        jz .res
        stosb
        jmp .copy3
.res:
        mov ebx, [usb_mem]
        mov ebx, [ebx + USB_MSC_CSW + 8]
        call usb_hex_dword
        mov esi, msg_usb_msc_csw_st
.copy4:
        lodsb
        test al, al
        jz .st
        stosb
        jmp .copy4
.st:
        mov ebx, [usb_mem]
        movzx ebx, byte [ebx + USB_MSC_CSW + 12]
        call usb_hex_byte
        xor al, al
        stosb
        mov al, LOG_WARN
        mov esi, usb_msg_buf
        call bootlog_write
        popad
        ret

;---------------------------------------
; usb_log_msc_sense - one line at DEBUG: the first eight sense bytes
;
; UNIT ATTENTION on a device that was just plugged in is expected, so it is
; kept out of the console and left for `bootlog`.
;---------------------------------------
usb_log_msc_sense:
        pushad
        mov edi, usb_msg_buf
        mov esi, msg_usb_msc_sense
.copy:
        lodsb
        test al, al
        jz .bytes
        stosb
        jmp .copy
.bytes:
        mov esi, [usb_mem]
        add esi, USB_MSC_BUF
        mov ebp, 8
.byte:
        mov bl, [esi]
        call usb_hex_byte
        mov al, ' '
        stosb
        inc esi
        dec ebp
        jnz .byte
        xor al, al
        stosb
        mov al, LOG_DEBUG
        mov esi, usb_msg_buf
        call bootlog_write
        popad
        ret

;---------------------------------------
; usb_port_reset - reset one root port and wait for it to be enabled
;   AL = port index (0 or 1)
;   Out: CF=0 if the port came back enabled, CF=1 if it did not
;---------------------------------------
usb_port_reset:
        pushad
        movzx eax, al
        shl eax, 1                      ; PORTSC1 is 0x10, PORTSC2 is 0x12
        add eax, UHCI_PORTSC1
        mov [.off], eax

        mov edx, [usb_io_base]
        add edx, [.off]
        in ax, dx
        or ax, UHCI_PORT_PR
        out dx, ax

        mov ecx, 50                     ; the specification asks for 10 ms; a
        call smp_delay_ms               ; device that needs longer is not one
                                        ; this step is meant to catch
        ; Clearing the reset is not enough: the port is enabled by software
        ; writing the enable bit, and a port that is never enabled answers
        ; nothing. The change bit we caused is cleared at the same time, since
        ; a PORTSC write replaces the read/write bits rather than updating them.
        mov edx, [usb_io_base]
        add edx, [.off]
        in ax, dx
        and ax, ~UHCI_PORT_PR
        or ax, UHCI_PORT_PED | UHCI_PORT_CSC
        out dx, ax

        mov ecx, 10
        call smp_delay_ms

        mov edx, [usb_io_base]
        add edx, [.off]
        in ax, dx
        test ax, UHCI_PORT_PED
        jz .no
        clc
        jmp .out
.no:
        stc
.out:
        popad
        ret
.off:   dd 0

;---------------------------------------
; usb_enumerate - give the device on a root port an address and read it
;
; The order is the one the specification lays down: reset the port, ask for the
; first eight bytes of the device descriptor at address zero (which is how the
; packet size for the rest is learned), set an address, then read the whole
; descriptor at that address.
;---------------------------------------
usb_enumerate:
        pushad
        mov dword [usb_dev_present], 0

        ; Which port has the device? QEMU's default machine puts the first
        ; plugged device on the second root port, so assuming port one is how
        ; this spent an afternoon resetting an empty port.
        xor ebp, ebp                    ; port index
.find_port:
        mov eax, ebp
        and eax, 0xFF
        shl eax, 1
        add eax, UHCI_PORTSC1
        mov edx, [usb_io_base]
        add edx, eax
        in ax, dx
        test ax, UHCI_PORT_CCS
        jnz .reset_port
        inc ebp
        cmp ebp, 2
        jb .find_port
        jmp .no_device
.reset_port:
        mov [usb_dev_port], ebp         ; the scan has to watch this one
        mov eax, ebp
        call usb_port_reset
        jc .no_reset

        ; Eight bytes first: bMaxPacketSize0 is byte 7.
        xor eax, eax
        mov dl, USB_DESC_DEVICE
        xor dh, dh
        mov ecx, 8
        call usb_get_descriptor
        jc .no_device
        mov ebx, [usb_mem]
        movzx eax, byte [ebx + USB_DESC_BUF + 7]
        test eax, eax
        jz .no_device
        mov [usb_dev_maxpkt], eax

        mov al, USB_DEV_ADDRESS
        call usb_set_address
        mov ecx, 2                      ; the device needs a moment to move
        call smp_delay_ms

        mov al, USB_DEV_ADDRESS
        mov dl, USB_DESC_DEVICE
        xor dh, dh
        mov ecx, 18
        call usb_get_descriptor
        jc .no_device

        mov ebx, [usb_mem]
        add ebx, USB_DESC_BUF
        movzx eax, word [ebx + 8]       ; idVendor
        mov [usb_dev_vendor], eax
        movzx eax, word [ebx + 10]      ; idProduct
        mov [usb_dev_product], eax
        movzx eax, byte [ebx + 4]       ; bDeviceClass
        mov [usb_dev_class], eax
        mov dword [usb_dev_addr], USB_DEV_ADDRESS

        LOGBOOT LOG_INFO, msg_usb_enum
        call usb_log_device

        ; The configuration descriptor says what the device can do, so it is
        ; read next and the device is configured. Nine bytes first: the total
        ; length is bytes 2-3 and the interface count byte 4.
        mov al, USB_DEV_ADDRESS
        mov dl, USB_DESC_CONFIG
        xor dh, dh
        mov ecx, 9
        call usb_get_descriptor
        jc .no_config

        mov ebx, [usb_mem]
        movzx eax, word [ebx + USB_DESC_BUF + 2]   ; wTotalLength
        test eax, eax
        jz .no_config
        mov [usb_cfg_len], eax
        movzx eax, byte [ebx + USB_DESC_BUF + 4]   ; bNumInterfaces
        mov [usb_cfg_interfaces], eax

        mov al, USB_DEV_ADDRESS
        mov dl, USB_DESC_CONFIG
        xor dh, dh
        mov ecx, [usb_cfg_len]
        call usb_get_descriptor
        jc .no_config

        mov al, 1                       ; configuration value 1
        call usb_set_configuration
        jc .no_configure

        LOGBOOT LOG_INFO, msg_usb_configured
        call usb_log_config

        ; From here the device is answering on its address, so a scan has
        ; something to watch. Class-driver success is a separate question: a
        ; mass-storage device that fails its probe is still a device on a port.
        mov dword [usb_dev_present], 1

        ; The class drivers run in this order on purpose: the mass-storage
        ; probe moves bulk data, and the HID driver leaves an interrupt
        ; descriptor hung on the one queue head, which a bulk transfer would
        ; overwrite. A device that is both has one of them until the queue
        ; head learns to hold two chains.
        call usb_msc_start
        call usb_hid_start
        jmp .out
.no_configure:
        LOGBOOT LOG_WARN, msg_usb_no_configure
        jmp .out
.no_config:
        LOGBOOT LOG_WARN, msg_usb_no_config
        jmp .out
.no_reset:
        LOGBOOT LOG_WARN, msg_usb_no_reset
        jmp .out
.no_device:
        LOGBOOT LOG_WARN, msg_usb_no_enum
.out:
        popad
        ret

;---------------------------------------
; usb_scan - look at the root ports, and follow a device in or out
;
; Called from the shell. The boot path enumerates once; this makes the same
; decision runnable later, which is what "plug in" has to mean. The driver
; holds one device at a time, so a second device plugged in beside a working
; one is left alone and logged only by its absence - the port it is on is
; still there when the first one leaves, and the next scan picks it up.
;---------------------------------------
usb_scan:
        pushad
        cmp dword [usb_running], 0
        je .out

        cmp dword [usb_dev_present], 0
        je .look
        mov eax, [usb_dev_port]
        shl eax, 1                      ; PORTSC1 is 0x10, PORTSC2 is 0x12
        add eax, UHCI_PORTSC1
        mov edx, [usb_io_base]
        add edx, eax
        in ax, dx
        test ax, UHCI_PORT_CCS
        jnz .out                        ; still there, nothing to do
        call usb_detach

.look:
        xor ebp, ebp
.port:
        mov eax, ebp
        shl eax, 1
        add eax, UHCI_PORTSC1
        mov edx, [usb_io_base]
        add edx, eax
        in ax, dx
        test ax, UHCI_PORT_CCS
        jnz .found
        inc ebp
        cmp ebp, 2
        jb .port
        jmp .out
.found:
        call usb_enumerate
.out:
        popad
        ret

;---------------------------------------
; usb_detach - forget the device that was on a root port
;
; The HID poll is disarmed before anything else: an interrupt descriptor
; refers to a device that is no longer there, and the timer would go on
; checking it. Every class driver's state goes with the device, so a stick
; that is unplugged stops being a capacity the shell can report.
;---------------------------------------
usb_detach:
        pushad
        mov dword [usb_hid_armed], 0
        mov dword [usb_hid_ep], 0
        mov dword [usb_hid_maxpkt], 0
        mov byte [usb_hid_prev_mod], 0
        mov dword [usb_hid_prev_keys], 0
        mov word [usb_hid_prev_keys + 4], 0

        mov dword [usb_msc_ready], 0
        mov dword [usb_msc_blocks], 0
        mov dword [usb_msc_blocksize], 0
        mov dword [usb_msc_ep_in], 0
        mov dword [usb_msc_ep_out], 0

        mov dword [usb_dev_present], 0
        mov dword [usb_dev_addr], 0

        ; Anything still hung on the queue head names the device that left.
        mov ebx, [usb_mem]
        mov dword [ebx + USB_QH + 4], UHCI_PTR_TERM

        LOGBOOT LOG_INFO, msg_usb_removed
        popad
        ret

;---------------------------------------
; usb_log_device - one line: vendor, product and class, in hex
;
; The boot log takes a string, so the numbers are formatted here rather than
; leaving the reader with "a device answered".
;---------------------------------------
usb_log_failure:
        pushad
        mov edi, usb_msg_buf
        mov esi, msg_usb_xfer_fail
.copy:
        lodsb
        test al, al
        jz .words
        stosb
        jmp .copy
.words:
        xor ecx, ecx
.next:
        mov ebx, [usb_td_status + ecx*4]
        call usb_hex_dword
        mov al, ' '
        stosb
        inc ecx
        cmp ecx, 3
        jb .next
        mov al, '/'
        stosb
        mov ebx, [usb_flbase]
        call usb_hex_dword
        mov al, '/'
        stosb
        mov ebx, [usb_flbase]
        mov ebx, [ebx]
        call usb_hex_dword
        mov al, '/'
        stosb
        mov ebx, [usb_mem]
        call usb_hex_dword
        xor al, al
        stosb
        mov al, LOG_INFO
        mov esi, usb_msg_buf
        call bootlog_write
        popad
        ret

usb_log_device:
        pushad
        mov edi, usb_msg_buf
        mov esi, msg_usb_dev_prefix
.copy:
        lodsb
        test al, al
        jz .hex
        stosb
        jmp .copy
.hex:
        mov ebx, [usb_dev_vendor]
        call usb_hex_word
        mov al, ':'
        stosb
        mov ebx, [usb_dev_product]
        call usb_hex_word
        mov al, ','
        stosb
        mov al, ' '
        stosb
        mov esi, msg_usb_class_prefix
.copy2:
        lodsb
        test al, al
        jz .cls
        stosb
        jmp .copy2
.cls:
        mov ebx, [usb_dev_class]
        call usb_hex_byte
        xor al, al
        stosb
        mov al, LOG_INFO
        mov esi, usb_msg_buf
        call bootlog_write
        popad
        ret

; usb_log_config - one line: interface count and total length, in hex
usb_log_config:
        pushad
        mov edi, usb_msg_buf
        mov esi, msg_usb_config_prefix
.copy:
        lodsb
        test al, al
        jz .interfaces
        stosb
        jmp .copy
.interfaces:
        mov ebx, [usb_cfg_interfaces]
        call usb_hex_byte
        mov esi, msg_usb_interfaces
.copy2:
        lodsb
        test al, al
        jz .bytes
        stosb
        jmp .copy2
.bytes:
        mov ebx, [usb_cfg_len]
        call usb_hex_word
        mov esi, msg_usb_bytes
.copy3:
        lodsb
        test al, al
        jz .done
        stosb
        jmp .copy3
.done:
        xor al, al
        stosb
        mov al, LOG_INFO
        mov esi, usb_msg_buf
        call bootlog_write
        popad
        ret

; usb_log_hid - one line: the interrupt endpoint and its packet size, in hex
usb_log_hid:
        pushad
        mov edi, usb_msg_buf
        mov esi, msg_usb_hid_prefix
.copy:
        lodsb
        test al, al
        jz .ep
        stosb
        jmp .copy
.ep:
        mov ebx, [usb_hid_ep]
        call usb_hex_byte
        mov esi, msg_usb_hid_sep
.copy2:
        lodsb
        test al, al
        jz .maxpkt
        stosb
        jmp .copy2
.maxpkt:
        mov ebx, [usb_hid_maxpkt]
        call usb_hex_word
        mov esi, msg_usb_hid_suffix
.copy3:
        lodsb
        test al, al
        jz .done
        stosb
        jmp .copy3
.done:
        xor al, al
        stosb
        mov al, LOG_INFO
        mov esi, usb_msg_buf
        call bootlog_write
        popad
        ret

; BX as four hex digits at EDI
usb_hex_word:
        push eax
        push ecx
        push edx
        movzx edx, bx
        mov ecx, 12                     ; shift for the top nibble
.hw_loop:
        mov eax, edx
        shr eax, cl
        and al, 0x0F
        cmp al, 10
        jb .hw_digit
        add al, 'a' - 10
        jmp .hw_store
.hw_digit:
        add al, '0'
.hw_store:
        stosb
        sub ecx, 4
        jns .hw_loop
        pop edx
        pop ecx
        pop eax
        ret

; BL as two hex digits at EDI
usb_hex_byte:
        push eax
        push ecx
        push edx
        movzx edx, bl
        mov ecx, 4
.hb_loop:
        mov eax, edx
        shr eax, cl
        and al, 0x0F
        cmp al, 10
        jb .hb_digit
        add al, 'a' - 10
        jmp .hb_store
.hb_digit:
        add al, '0'
.hb_store:
        stosb
        sub ecx, 4
        jns .hb_loop
        pop edx
        pop ecx
        pop eax
        ret

; EBX as eight hex digits at EDI
usb_hex_dword:
        push eax
        push ecx
        push edx
        mov edx, ebx
        mov ecx, 28                     ; shift for the top nibble
.hd_loop:
        mov eax, edx
        shr eax, cl
        and al, 0x0F
        cmp al, 10
        jb .hd_digit
        add al, 'a' - 10
        jmp .hd_store
.hd_digit:
        add al, '0'
.hd_store:
        stosb
        sub ecx, 4
        jns .hd_loop
        pop edx
        pop ecx
        pop eax
        ret

USB_DEV_ADDRESS equ 1

msg_usb_uhci:         db "usb: UHCI host controller ready", 0
msg_usb_running:      db "usb: frame list installed, controller running", 0
msg_usb_connected:    db "usb: a device is plugged into a root port", 0
msg_usb_empty:        db "usb: no device on the root ports", 0
msg_usb_reset:        db "usb: the UHCI controller would not come out of reset", 0
msg_usb_enum:         db "usb: device enumerated", 0
msg_usb_configured:   db "usb: configured", 0
msg_usb_no_enum:      db "usb: the device on a root port would not enumerate", 0
msg_usb_no_config:    db "usb: the configuration descriptor would not read", 0
msg_usb_no_configure: db "usb: the device would not configure", 0
msg_usb_no_reset:     db "usb: the root port would not reset", 0
msg_usb_xfer_fail:    db "usb: a control transfer did not complete, status=", 0
msg_usb_regs:         db "usb: regs cmd=", 0
msg_usb_dev_prefix:   db "usb: device ", 0
msg_usb_class_prefix: db "class ", 0
msg_usb_config_prefix: db "usb: config ", 0
msg_usb_interfaces:   db " interfaces, ", 0
msg_usb_bytes:        db " bytes", 0
msg_usb_hid_prefix:   db "usb: hid endpoint ", 0
msg_usb_hid_sep:      db ", ", 0
msg_usb_hid_suffix:   db " bytes, ready", 0
msg_usb_report:       db "usb: hid report read", 0
msg_usb_no_hid:       db "usb: the HID boot keyboard would not start", 0
msg_usb_msc_endpoints: db "usb: mass storage endpoints ", 0
msg_usb_msc_sep:      db ", ", 0
msg_usb_msc_suffix:   db " bytes, ready", 0
msg_usb_msc_inquiry:  db "usb: mass storage: ", 0
msg_usb_msc_capacity: db "usb: mass storage ", 0
msg_usb_msc_blocks:   db " blocks of ", 0
msg_usb_msc_bytes:    db " bytes", 0
msg_usb_msc_block0:   db "usb: mass storage block 0 reads ", 0
msg_usb_msc_fail:     db "usb: the mass-storage device did not answer", 0
msg_usb_removed:      db "usb: the device was removed", 0
msg_usb_msc_csw:      db "usb: msc csw sig=", 0
msg_usb_msc_csw_tag:  db " tag=", 0
msg_usb_msc_csw_res:  db " res=", 0
msg_usb_msc_csw_st:   db " st=", 0
msg_usb_msc_sense:    db "usb: mass storage sense ", 0

; SCSI command blocks, little conventions of their own: LBA and length are
; big-endian inside them even though the wrappers around them are not.
msc_cb_inquiry:       db SCSI_INQUIRY, 0, 0, 0, 36, 0
msc_cb_sense:         db SCSI_REQUEST_SENSE, 0, 0, 0, 18, 0
msc_cb_capacity:      db SCSI_READ_CAPACITY, 0, 0, 0, 0, 0, 0, 0, 0, 0
msc_cb_read:          db SCSI_READ10, 0, 0, 0, 0, 0, 0, 0, 1, 0

usb_msg_buf:          times 96 db 0
