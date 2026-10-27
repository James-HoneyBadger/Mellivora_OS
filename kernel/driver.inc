;=======================================================================
; driver.inc - the driver model: what is in the machine, and who drives it
;
; Before this, hardware was initialised by a list of calls in the boot path:
; every driver ran, discovered its own device, and said so. That works exactly
; as long as nothing is ever added, removed, or plugged in afterwards, and it
; answers none of the questions a machine actually asks - which device is this,
; who owns it, may two drivers own the same interrupt, and what happens on the
; way down?
;
; Three things live here, in the order they are used:
;
;   1. The device tree. Every device the machine has, one node each, built from
;      the PCI enumeration plus a table of the platform devices a PC has
;      whether or not anything describes them (the two 8259s, the PIT, the PS/2
;      controller, the legacy IDE channels).
;
;   2. Resource arbitration. A driver that binds a device claims that device's
;      I/O ports, memory window and interrupt line. A claim that overlaps
;      another driver's is refused and reported, which is how two drivers
;      fighting over one line is found at boot instead of as a machine that
;      loses interrupts under load.
;
;   3. The drivers themselves. A driver is a record - name, what it matches,
;      an attach routine, a detach routine, a power callback, and an order -
;      registered in a table, not called from the boot path. The manager walks
;      the table in order, matches it against the device tree, claims the
;      resources and calls attach. A driver that finds nothing is recorded as
;      absent, which is a state, not a failure.
;
; The table is registered through driver_module_register, which validates a
; module header first. In-tree drivers are one such module, linked into the
; image; the same header is what a module loaded from elsewhere would carry, so
; the manager has one way in rather than a special case for built-ins.
;
; Locking: none of this takes a lock, and none of it runs after boot on more
; than one processor. Binding is a boot activity; the invariant to preserve
; when a driver gains a runtime path is that the driver owns its device, so the
; device tree is read-only afterwards.
;=======================================================================

DRV_MAX             equ 24
DEV_MAX             equ 48
RES_MAX             equ 64

; Driver record
DR_NAME             equ 0               ; dd -> string
DR_MATCH            equ 4               ; dd: DRV_MATCH_ANY or DRV_MATCH_TABLE
DR_MATCH_TABLE_PTR  equ 8               ; dd -> the match table (see below)
DR_ATTACH           equ 12              ; dd: EAX = device index -> EAX = 0
DR_DETACH           equ 16              ; dd: EAX = device index -> EAX = 0
DR_POWER            equ 20              ; dd: EAX = DRV_POWER_* -> EAX = 0
DR_ORDER            equ 24              ; dd: lower binds first
DR_STATE            equ 28              ; dd: DRV_STATE_*
DR_DEV              equ 32              ; dd: device index, -1 when unbound
DR_NOTE             equ 36              ; dd -> why, for `drivers`
DRV_SIZE            equ 40              ; ten dwords and no padding, so a module
                                        ; written in any language lays it out the
                                        ; same way

; A match table is a list of what a driver serves, terminated by a zero kind:
;
;     dd MT_PCI_ID,  0x8086, 0x100E
;     dd MT_PCI_ID,  0x8086, 0x100F
;     dd MT_PCI_CLASS, 0x0106, 0
;     dd 0
;
; The table is the reason a driver with four device IDs and one with a class
; rule are the same shape: the manager walks the list and stops at the first
; entry that fits, so nothing about matching lives in code.
MT_PCI_ID           equ 1               ; vendor, device
MT_PCI_CLASS        equ 2               ; (class << 8) | subclass, unused
MT_VENDOR           equ 3               ; vendor, unused
MT_PLATFORM         equ 4               ; platform id, unused
MT_SIZE             equ 12
DRV_MATCH_ANY       equ 0               ; binds without a device
DRV_MATCH_TABLE     equ 1               ; binds what its table matches

DRV_STATE_NEW       equ 0
DRV_STATE_BOUND     equ 1
DRV_STATE_ABSENT    equ 2               ; no device matched
DRV_STATE_CLAIMED   equ 3               ; matched, but another driver owns it
DRV_STATE_FAILED    equ 4               ; matched and owned, attach refused
DRV_STATE_SKIPPED   equ 5               ; deliberately not started

DRV_POWER_RESUME    equ 0
DRV_POWER_SUSPEND   equ 1
DRV_POWER_OFF       equ 2

; Device node
DV_KIND             equ 0               ; dd: DV_KIND_*
DV_VENDOR           equ 4
DV_DEVICE           equ 8
DV_CLASS            equ 12              ; dd: (class << 8) | subclass
DV_BDF              equ 16
DV_PLATID           equ 20
DV_IRQ              equ 24              ; dd: 0..15, or 0xFF for none
DV_IOBASE           equ 28              ; dd: first I/O port range, 0 = none
DV_IOLEN            equ 32
DV_MEMBASE          equ 36              ; dd: first memory window, 0 = none
DV_MEMLEN           equ 40
DV_OWNER            equ 44              ; dd: driver index that claimed it
DV_NAME             equ 48              ; dd -> string
DV_SIZE             equ 52

DV_KIND_PCI         equ 1
DV_KIND_PLATFORM    equ 2

; Resource claim
RS_KIND             equ 0               ; dd: RES_*
RS_BASE             equ 4
RS_LEN              equ 8
RS_OWNER            equ 12              ; dd: driver index
RS_SIZE             equ 16

RES_IO              equ 1
RES_MEM             equ 2
RES_IRQ             equ 3               ; an edge line: one owner
RES_IRQ_SHARED      equ 4               ; a level line: many owners, one APIC pin

DRV_MODULE_MAGIC    equ 0x4452564D      ; "MVRD"
DRV_MODULE_VERSION  equ 1

; Module header: the one way in for a table of drivers
MOD_MAGIC           equ 0
MOD_VERSION         equ 4
MOD_DRIVERS         equ 8               ; dd: pointer to the driver records
MOD_COUNT           equ 12              ; dd: how many
MOD_EXPORTS         equ 16              ; dd: { name, address } pairs, 0 = none
MOD_NAME            equ 20              ; dd -> string
MOD_SIZE            equ 24

;---------------------------------------------------------------
; State
;---------------------------------------------------------------
drv_table:          times (DRV_MAX * DRV_SIZE) db 0
drv_count:          dd 0
drv_bound:          dd 0

dev_table:          times DEV_MAX * DV_SIZE db 0
dev_count:          dd 0

res_table:          times RES_MAX * RS_SIZE db 0
res_count:          dd 0
res_conflicts:      dd 0

drv_this:           dd -1               ; driver being bound, for the note

;---------------------------------------------------------------
; Strings
;---------------------------------------------------------------
drv_state_new:      db "new", 0
drv_state_bound:    db "bound", 0
drv_state_absent:   db "absent", 0
drv_state_claimed:  db "claimed", 0
drv_state_failed:   db "failed", 0
drv_state_skipped:  db "skipped", 0
drv_unknown:        db "?", 0

dn_no_device:       db "no device matched", 0
dn_claimed:         db "device is another driver's", 0
dn_no_memory:       db "no room left in the tables", 0
dn_res_conflict:    db "resource conflict", 0
dn_attach_failed:   db "attach refused the device", 0

drv_kind_pci:       db "pci ", 0
drv_kind_platform:  db "isa ", 0

msg_drivers_head:   db 0x0A, "Drivers: ", 0
msg_drivers_reg:    db " registered, ", 0
msg_drivers_bound:  db " bound, ", 0
msg_drivers_conf:   db " conflicts", 0x0A, 0
msg_devices_head:   db "Devices: ", 0
msg_devices_nodes:  db " in the tree", 0x0A, 0x0A, 0
msg_res_head:       db "Resources claimed: ", 0

;-----------------------------------------------------------------------
; driver_module_register - take a driver module's table into the registry
;   In : EAX = module header
;   Out: EAX = drivers registered, -1 refused
;
; The header is validated before a single record is trusted, for the reason the
; C module slot validates its own: a table read from the wrong place produces
; plausible-looking pointers, and the failure then happens somewhere else,
; later, with nothing pointing back here.
;-----------------------------------------------------------------------
driver_module_register:
        pushad
        test eax, eax
        jz .dmr_bad
        mov esi, eax
        cmp dword [esi + MOD_MAGIC], DRV_MODULE_MAGIC
        jne .dmr_bad
        cmp dword [esi + MOD_VERSION], DRV_MODULE_VERSION
        jne .dmr_bad
        mov ecx, [esi + MOD_COUNT]
        test ecx, ecx
        jz .dmr_bad
        mov edi, [esi + MOD_DRIVERS]
        test edi, edi
        jz .dmr_bad

        mov [.dmr_added], ecx
        xor edx, edx                    ; records copied
.dmr_loop:
        cmp edx, ecx
        jae .dmr_done
        mov eax, [drv_count]
        cmp eax, DRV_MAX
        jae .dmr_full

        ; Destination record
        imul eax, DRV_SIZE
        lea ebx, [drv_table + eax]
        push ecx
        push edx
        push edi
        mov ecx, DRV_SIZE / 4
        mov esi, edi
        mov edi, ebx
        cld
        rep movsd
        pop edi
        pop edx
        pop ecx

        ; Every record starts unbound, whoever wrote it.
        mov eax, [drv_count]
        imul eax, DRV_SIZE
        lea ebx, [drv_table + eax]
        mov dword [ebx + DR_STATE], DRV_STATE_NEW
        mov dword [ebx + DR_DEV], -1
        mov dword [ebx + DR_NOTE], 0

        inc dword [drv_count]
        add edi, DRV_SIZE
        inc edx
        jmp .dmr_loop

.dmr_full:
        mov [.dmr_added], edx
.dmr_done:
        mov eax, [.dmr_added]
        mov [esp + 28], eax
        popad
        ret
.dmr_bad:
        mov dword [esp + 28], -1
        popad
        ret
.dmr_added: dd 0

;-----------------------------------------------------------------------
; dev_add_pci - put one PCI function in the device tree
;   In : ESI = pci_dev_table entry
;   Out: EAX = device index, or -1 when the table is full
;
; The BAR and interrupt line are read here, once, from configuration space:
; after this the device tree is the answer to "where is it" and no driver has
; to ask the bus again.
;-----------------------------------------------------------------------
dev_add_pci:
        pushad
        mov eax, [dev_count]
        cmp eax, DEV_MAX
        jae .dap_full
        imul eax, DV_SIZE
        lea edi, [dev_table + eax]

        mov dword [edi + DV_KIND], DV_KIND_PCI
        movzx eax, word [esi + 0]
        mov [edi + DV_VENDOR], eax
        movzx eax, word [esi + 2]
        mov [edi + DV_DEVICE], eax
        movzx eax, byte [esi + 7]
        shl eax, 8
        movzx ecx, byte [esi + 8]
        or eax, ecx
        mov [edi + DV_CLASS], eax
        mov dword [edi + DV_PLATID], 0
        mov dword [edi + DV_OWNER], -1

        ; BDF, packed the way the config-space helpers expect it
        movzx eax, byte [esi + 4]
        shl eax, 16
        movzx ecx, byte [esi + 5]
        shl ecx, 8
        or eax, ecx
        movzx ecx, byte [esi + 6]
        or eax, ecx
        mov [edi + DV_BDF], eax
        mov [.dap_bdf], eax

        ; Interrupt line: config offset 0x3C, low byte
        mov ebx, 0x3C
        call pci_read_config32
        and eax, 0xFF
        cmp eax, 16
        jb .dap_irq_ok
        mov eax, 0xFF                   ; 0 or 255 mean "not routed"
        test eax, eax
        jnz .dap_irq_ok
.dap_irq_ok:
        mov [edi + DV_IRQ], eax

        ; BAR0: an I/O range or a memory window, whichever it says it is, with
        ; its real length. A BAR does not state a length - the documented way
        ; to ask is to write all ones and read back which bits the device
        ; decodes, then restore the original value.
        mov dword [edi + DV_MEMBASE], 0
        mov dword [edi + DV_MEMLEN], 0
        mov dword [edi + DV_IOBASE], 0
        mov dword [edi + DV_IOLEN], 0

        mov eax, [.dap_bdf]
        mov ebx, 0x10
        call pci_read_config32
        mov [.dap_bar0], eax

        ; Ask the device how much it decodes
        mov eax, [.dap_bdf]
        mov ebx, 0x10
        mov ecx, 0xFFFFFFFF
        call pci_write_config32
        mov eax, [.dap_bdf]
        mov ebx, 0x10
        call pci_read_config32
        mov [.dap_barmask], eax

        ; Put it back before anything else touches the bus
        mov eax, [.dap_bdf]
        mov ebx, 0x10
        mov ecx, [.dap_bar0]
        call pci_write_config32
        mov eax, [.dap_bdf]
        mov ebx, 0x10
        call pci_read_config32

        mov eax, [.dap_barmask]
        test eax, 1
        jz .dap_mem
        ; I/O BAR
        mov eax, [.dap_bar0]
        and eax, 0xFFFFFFFC
        mov [edi + DV_IOBASE], eax
        mov eax, [.dap_barmask]
        and eax, 0xFFFFFFFC
        not eax
        inc eax
        cmp eax, 0x10000
        jbe .dap_io_len_ok
        mov eax, 0x10000
.dap_io_len_ok:
        test eax, eax
        jnz .dap_io_len_set
        mov eax, 256                        ; decode mask all ones: unknown
.dap_io_len_set:
        mov [edi + DV_IOLEN], eax
        jmp .dap_named
.dap_mem:
        mov eax, [.dap_bar0]
        and eax, 0xFFFFFFF0
        test eax, eax
        jz .dap_named
        mov [edi + DV_MEMBASE], eax
        mov eax, [.dap_barmask]
        and eax, 0xFFFFFFF0
        not eax
        inc eax
        test eax, eax
        jnz .dap_mem_len_set
        mov eax, 0x1000
.dap_mem_len_set:
        mov [edi + DV_MEMLEN], eax
.dap_named:
        mov dword [edi + DV_NAME], dn_pci_unnamed

        inc dword [dev_count]
        mov eax, [dev_count]
        dec eax
        mov [esp + 28], eax
        popad
        ret
.dap_full:
        mov dword [esp + 28], -1
        popad
        ret
.dap_bdf: dd 0
.dap_bar0: dd 0
.dap_barmask: dd 0

dn_pci_unnamed: db "pci function", 0

;-----------------------------------------------------------------------
; dev_build_platform - the devices a PC has without being asked
;   Out: EAX = nodes added
;
; Their port ranges are the architectural ones, and they are in the tree for
; one reason: so a PCI device mapped over them is caught by the arbitration
; instead of silently sharing a register. PCI functions are added separately,
; after the bus has been enumerated.
;-----------------------------------------------------------------------
dev_build_platform:
        pushad
        mov esi, dev_platform_list
.db_plat_loop:
        cmp dword [esi], -1                   ; end of the table
        je .db_done
        mov eax, [dev_count]
        cmp eax, DEV_MAX
        jae .db_done
        imul eax, DV_SIZE
        lea edi, [dev_table + eax]

        mov dword [edi + DV_KIND], DV_KIND_PLATFORM
        mov eax, [esi + PLAT_ID]
        mov [edi + DV_PLATID], eax
        mov dword [edi + DV_VENDOR], 0
        mov dword [edi + DV_DEVICE], 0
        mov dword [edi + DV_CLASS], 0
        mov dword [edi + DV_BDF], 0
        mov dword [edi + DV_OWNER], -1
        mov eax, [esi + PLAT_IRQ]
        mov [edi + DV_IRQ], eax
        mov eax, [esi + PLAT_IOBASE]
        mov [edi + DV_IOBASE], eax
        mov eax, [esi + PLAT_IOLEN]
        mov [edi + DV_IOLEN], eax
        mov eax, [esi + PLAT_MEMBASE]
        mov [edi + DV_MEMBASE], eax
        mov eax, [esi + PLAT_MEMLEN]
        mov [edi + DV_MEMLEN], eax
        mov eax, [esi + PLAT_NAME]
        mov [edi + DV_NAME], eax

        inc dword [dev_count]
        add esi, PLAT_SIZE
        jmp .db_plat_loop

.db_done:
        mov eax, [dev_count]
        mov [esp + 28], eax
        popad
        ret

;-----------------------------------------------------------------------
; res_claim - may this driver own this range?
;   In : EAX = RES_*, EBX = base, ECX = length, EDX = driver index
;   Out: EAX = 0 claimed, -1 refused (overlaps something already claimed)
;
; Interrupts are a single number, so a claim is a duplicate check. I/O ports
; and memory windows can overlap partially, so the check is a range
; intersection - the case a duplicate check misses and the one that actually
; happens when a card is left mapped over legacy ports.
;-----------------------------------------------------------------------
res_claim:
        push ebx
        push ecx
        push edx
        push esi
        push edi
        mov [.rc_kind], eax
        mov [.rc_base], ebx
        mov [.rc_len], ecx
        mov [.rc_owner], edx

        cmp eax, RES_IRQ_SHARED
        je .rc_store                    ; a shared line is claimed, not held
        cmp eax, RES_IRQ
        je .rc_irq

        ; I/O and memory: refuse any intersection with the same kind
        xor esi, esi
.rc_scan:
        cmp esi, [res_count]
        jae .rc_store
        mov edi, esi
        imul edi, RS_SIZE
        lea edi, [res_table + edi]
        mov eax, [edi + RS_KIND]
        cmp eax, [.rc_kind]
        jne .rc_next
        ; [base, base+len) against [other, other+len)
        mov eax, [.rc_base]
        cmp eax, [edi + RS_BASE]
        jb .rc_below
        ; this base is at or above the other's: overlap iff base < other_end
        mov ecx, [edi + RS_BASE]
        add ecx, [edi + RS_LEN]
        cmp eax, ecx
        jb .rc_conflict
        jmp .rc_next
.rc_below:
        mov ecx, [.rc_base]
        add ecx, [.rc_len]
        cmp ecx, [edi + RS_BASE]
        ja .rc_conflict
.rc_next:
        inc esi
        jmp .rc_scan

.rc_irq:
        xor esi, esi
.rc_irq_scan:
        cmp esi, [res_count]
        jae .rc_store
        mov edi, esi
        imul edi, RS_SIZE
        lea edi, [res_table + edi]
        cmp dword [edi + RS_KIND], RES_IRQ
        jne .rc_irq_next                ; only an edge line refuses a second owner
        mov eax, [edi + RS_BASE]
        cmp eax, [.rc_base]
        je .rc_conflict
.rc_irq_next:
        inc esi
        jmp .rc_irq_scan

.rc_store:
        mov eax, [res_count]
        cmp eax, RES_MAX
        jae .rc_conflict
        imul eax, RS_SIZE
        lea edi, [res_table + eax]
        mov eax, [.rc_kind]
        mov [edi + RS_KIND], eax
        mov eax, [.rc_base]
        mov [edi + RS_BASE], eax
        mov eax, [.rc_len]
        mov [edi + RS_LEN], eax
        mov eax, [.rc_owner]
        mov [edi + RS_OWNER], eax
        inc dword [res_count]
        xor eax, eax
        jmp .rc_out

.rc_conflict:
        inc dword [res_conflicts]
        mov eax, -1
.rc_out:
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret
.rc_kind:  dd 0
.rc_base:  dd 0
.rc_len:   dd 0
.rc_owner: dd 0

;-----------------------------------------------------------------------
; dev_map_window - make a device's memory window readable and writable
;   In : ESI = device record
;
; Identity-mapped, page at a time, in the master directory and the active one.
; Skipped before paging is on, when the address is physical anyway.
;-----------------------------------------------------------------------
dev_map_window:
        pushad
        mov eax, [esi + DV_MEMBASE]
        test eax, eax
        jz .dmw_done
        mov ecx, [esi + DV_MEMLEN]
        test ecx, ecx
        jnz .dmw_have_len
        mov ecx, 0x1000
.dmw_have_len:
        ; Paging up? (CR0.PG)
        mov edx, cr0
        test edx, 0x80000000
        jz .dmw_done
        mov [.dmw_base], eax
        mov [.dmw_len], ecx
        mov ebx, eax
        add ecx, 0xFFF
        and ecx, 0xFFFFF000
        shr ecx, 12
        mov [.dmw_pages], ecx
.dmw_loop:
        cmp dword [.dmw_pages], 0
        je .dmw_done
        mov eax, [.dmw_base]
        mov ebx, eax
        mov ecx, PG_PRESENT | PG_WRITABLE
        call paging_map_page_all
        add dword [.dmw_base], 0x1000
        dec dword [.dmw_pages]
        jmp .dmw_loop
.dmw_done:
        popad
        ret
.dmw_base:  dd 0
.dmw_len:   dd 0
.dmw_pages: dd 0

;-----------------------------------------------------------------------
; dev_claim - hand a device's resources to a driver
;   In : EAX = device index, EDX = driver index
;   Out: EAX = 0 claimed, -1 conflict
;-----------------------------------------------------------------------
dev_claim:
        push ebx
        push ecx
        push edx
        push esi
        push edi
        mov [.dc_dev], eax
        mov [.dc_driver], edx
        imul eax, DV_SIZE
        lea esi, [dev_table + eax]

        ; Memory window first: a device with both is usually a memory device.
        mov eax, [esi + DV_MEMBASE]
        test eax, eax
        jz .dc_io
        mov eax, RES_MEM
        mov ebx, [esi + DV_MEMBASE]
        mov ecx, [esi + DV_MEMLEN]
        test ecx, ecx
        jnz .dc_mem_len
        mov ecx, 0x1000
.dc_mem_len:
        mov edx, [.dc_driver]
        call res_claim
        test eax, eax
        jnz .dc_conflict
        ; A claimed window is also a mapped window: the driver binds after
        ; paging is on, and a register write to an unmapped physical address is
        ; a kernel fault in whichever driver happened to touch it first.
        call dev_map_window

.dc_io:
        mov eax, [esi + DV_IOBASE]
        test eax, eax
        jz .dc_irq
        mov eax, RES_IO
        mov ebx, [esi + DV_IOBASE]
        mov ecx, [esi + DV_IOLEN]
        test ecx, ecx
        jnz .dc_io_len
        mov ecx, 8
.dc_io_len:
        mov edx, [.dc_driver]
        call res_claim
        test eax, eax
        jnz .dc_conflict

.dc_irq:
        mov eax, [esi + DV_IRQ]
        cmp eax, 0xFF
        je .dc_ok
        cmp eax, 0
        je .dc_ok                        ; 0 means unrouted, not "line zero"
        mov ebx, eax
        ; PCI lines are shared by design - four slots, one level-triggered pin,
        ; every driver acknowledging its own device. Refusing the second claim
        ; would refuse two of the four. An ISA line is an edge with one owner.
        mov eax, RES_IRQ_SHARED
        cmp dword [esi + DV_KIND], DV_KIND_PCI
        je .dc_irq_kind
        mov eax, RES_IRQ
.dc_irq_kind:
        mov ecx, 1
        mov edx, [.dc_driver]
        call res_claim
        test eax, eax
        jnz .dc_conflict

.dc_ok:
        mov eax, [.dc_dev]
        imul eax, DV_SIZE
        mov edx, [.dc_driver]
        mov [dev_table + eax + DV_OWNER], edx
        xor eax, eax
        jmp .dc_out
.dc_conflict:
        mov eax, -1
.dc_out:
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret
.dc_dev:    dd 0
.dc_driver: dd 0

;-----------------------------------------------------------------------
; drv_match - does this driver serve this device?
;   In : ESI = driver record, EDI = device record
;   Out: EAX = 1 match, 0 no match
;
; Walks the driver's match table. A table entry that names the device's kind
; wrong for this device is not a mismatch of the driver - the next entry may
; fit - so the walk only stops on a positive answer.
;-----------------------------------------------------------------------
drv_match:
        push ebx
        push ecx
        push esi
        push edi
        mov ecx, [esi + DR_MATCH_TABLE_PTR]
        test ecx, ecx
        jz .dm_no
        mov ebx, [edi + DV_KIND]
.dm_loop:
        mov eax, [ecx + 0]
        test eax, eax
        jz .dm_no

        cmp eax, MT_PCI_ID
        je .dm_pci_id
        cmp eax, MT_PCI_CLASS
        je .dm_pci_class
        cmp eax, MT_VENDOR
        je .dm_vendor
        cmp eax, MT_PLATFORM
        je .dm_platform
        jmp .dm_next

.dm_pci_id:
        cmp ebx, DV_KIND_PCI
        jne .dm_next
        mov eax, [edi + DV_VENDOR]
        cmp eax, [ecx + 4]
        jne .dm_next
        mov eax, [edi + DV_DEVICE]
        cmp eax, [ecx + 8]
        jne .dm_next
        jmp .dm_yes
.dm_pci_class:
        cmp ebx, DV_KIND_PCI
        jne .dm_next
        mov eax, [edi + DV_CLASS]
        cmp eax, [ecx + 4]
        jne .dm_next
        jmp .dm_yes
.dm_vendor:
        cmp ebx, DV_KIND_PCI
        jne .dm_next
        mov eax, [edi + DV_VENDOR]
        cmp eax, [ecx + 4]
        jne .dm_next
        jmp .dm_yes
.dm_platform:
        cmp ebx, DV_KIND_PLATFORM
        jne .dm_next
        mov eax, [edi + DV_PLATID]
        cmp eax, [ecx + 4]
        jne .dm_next
        jmp .dm_yes
.dm_next:
        add ecx, MT_SIZE
        jmp .dm_loop
.dm_yes:
        mov eax, 1
        jmp .dm_out
.dm_no:
        xor eax, eax
.dm_out:
        pop edi
        pop esi
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; drv_bind_one - match, claim and attach one driver
;   In : EAX = driver index
;
; Order of operations matters: the device is claimed *before* attach, so an
; attach that fails has already been recorded against the device it wanted and
; a retry cannot hand the same device to a driver that will fail the same way.
;-----------------------------------------------------------------------
drv_bind_one:
        pushad
        mov [drv_this], eax
        imul eax, DRV_SIZE
        lea esi, [drv_table + eax]

        ; A driver that needs no device binds without one: there is exactly one
        ; platform, and a driver that says so is describing the machine's own
        ; circuitry rather than a device that could be missing.
        cmp dword [esi + DR_MATCH], DRV_MATCH_ANY
        je .dbo_any

        xor ecx, ecx
.dbo_scan:
        cmp ecx, [dev_count]
        jae .dbo_absent
        mov edi, ecx
        imul edi, DV_SIZE
        lea edi, [dev_table + edi]

        cmp dword [edi + DV_OWNER], -1
        jne .dbo_next
        push ecx
        push edi
        push esi
        call drv_match
        pop esi
        pop edi
        pop ecx
        test eax, eax
        jz .dbo_next

        ; Matched: claim it.
        mov eax, ecx
        mov edx, [drv_this]
        push ecx
        push esi
        call dev_claim
        pop esi
        pop ecx
        test eax, eax
        jnz .dbo_conflict

        mov eax, [drv_this]
        imul eax, DRV_SIZE
        lea esi, [drv_table + eax]
        mov [esi + DR_DEV], ecx
        mov dword [esi + DR_STATE], DRV_STATE_BOUND
        jmp .dbo_attach

.dbo_next:
        inc ecx
        jmp .dbo_scan

.dbo_any:
        mov dword [esi + DR_DEV], -1
        mov dword [esi + DR_STATE], DRV_STATE_BOUND
        xor ecx, ecx

.dbo_attach:
        mov eax, [drv_this]
        imul eax, DRV_SIZE
        lea esi, [drv_table + eax]
        mov ecx, [esi + DR_DEV]
        mov eax, [esi + DR_ATTACH]
        test eax, eax
        jz .dbo_out                      ; nothing to call: bound is enough
        mov eax, ecx                     ; EAX = device index (-1 for platform)
        call [esi + DR_ATTACH]
        test eax, eax
        jz .dbo_out
        ; Attached driver said no.
        mov eax, [drv_this]
        imul eax, DRV_SIZE
        lea esi, [drv_table + eax]
        mov dword [esi + DR_STATE], DRV_STATE_FAILED
        mov [esi + DR_NOTE], dword dn_attach_failed
        jmp .dbo_out

.dbo_conflict:
        mov eax, [drv_this]
        imul eax, DRV_SIZE
        lea esi, [drv_table + eax]
        mov dword [esi + DR_STATE], DRV_STATE_CLAIMED
        mov [esi + DR_NOTE], dword dn_res_conflict
        jmp .dbo_out

.dbo_absent:
        mov dword [esi + DR_STATE], DRV_STATE_ABSENT
        mov [esi + DR_NOTE], dword dn_no_device
.dbo_out:
        ; Say what happened. A bound driver is news; one that matched nothing is
        ; the answer to "why is there no sound on this machine", which is what
        ; the ring is for - so it is a debug record rather than an info one.
        mov eax, [drv_this]
        call drv_log_outcome
        popad
        ret

;-----------------------------------------------------------------------
; drv_log_outcome - one boot-log record for what a driver did
;   In : EAX = driver index
;-----------------------------------------------------------------------
drv_log_outcome:
        pushad
        mov esi, eax
        imul esi, DRV_SIZE
        lea esi, [drv_table + esi]

        mov edi, drv_log_buf
        mov eax, "drv "                  ; four bytes, little endian
        stosd
        mov eax, esi
        push esi
        mov esi, [esi + DR_NAME]
.dlo_name:
        lodsb
        test al, al
        jz .dlo_name_done
        stosb
        jmp .dlo_name
.dlo_name_done:
        mov al, ':'
        stosb
        mov al, ' '
        stosb
        pop esi
        mov eax, [esi + DR_STATE]
        call drv_state_name
        push esi
        mov esi, eax
.dlo_state:
        lodsb
        test al, al
        jz .dlo_state_done
        stosb
        jmp .dlo_state
.dlo_state_done:
        pop esi
        mov byte [edi], 0

        mov al, LOG_DEBUG
        cmp dword [esi + DR_STATE], DRV_STATE_BOUND
        jne .dlo_level
        mov al, LOG_INFO
.dlo_level:
        mov esi, drv_log_buf
        call bootlog_write
        popad
        ret

drv_log_buf: times 64 db 0

;-----------------------------------------------------------------------
; driver_init_phase - bind the drivers whose order falls in this phase
;   In : EAX = highest DR_ORDER to bind (drivers already bound are skipped)
;   Out: EAX = how many are bound now
;
; Two phases, because the machine does not appear all at once. The first binds
; the hardware the architecture guarantees - interrupt controller, clock,
; keyboard controller, serial - before the frame allocator exists, since that
; is what the rest of the boot reports through. The second runs after PCI
; enumeration, when the device tree finally knows what is plugged in.
;
; Registration order is not binding order: DR_ORDER decides, so a bus driver and
; a function driver can be declared in the table in whatever order reads
; best. The sort is a selection sort over indices because the tables are tiny
; and this runs twice.
;-----------------------------------------------------------------------
driver_init_phase:
        pushad
        mov [.dip_limit], eax
        cmp dword [.dip_platform_done], 0
        jne .dip_platform_ok
        mov dword [.dip_platform_done], 1
        call dev_build_platform
.dip_platform_ok:
        call dev_add_pci_all

        ; Order the drivers. A separate index array keeps the records - which a
        ; module owns - in the order they were given.
        xor ecx, ecx
        mov edi, drv_order_buf
.dia_index:
        cmp ecx, [drv_count]
        jae .dia_sort
        mov [edi + ecx*4], ecx
        inc ecx
        jmp .dia_index

.dia_sort:
        ; selection sort by DR_ORDER, ascending
        xor ecx, ecx
.dia_outer:
        mov eax, [drv_count]
        dec eax
        cmp ecx, eax
        jae .dia_bind
        mov edx, ecx
        inc edx
.dia_inner:
        cmp edx, [drv_count]
        jae .dia_next_outer
        ; compare order(buf[edx]) < order(buf[ecx])
        mov eax, [edi + edx*4]
        imul eax, DRV_SIZE
        mov ebx, [drv_table + eax + DR_ORDER]
        mov eax, [edi + ecx*4]
        imul eax, DRV_SIZE
        mov esi, [drv_table + eax + DR_ORDER]
        cmp ebx, esi
        jae .dia_no_swap
        ; swap the two index slots
        mov eax, [edi + edx*4]
        mov esi, [edi + ecx*4]
        mov [edi + edx*4], esi
        mov [edi + ecx*4], eax
.dia_no_swap:
        inc edx
        jmp .dia_inner
.dia_next_outer:
        inc ecx
        jmp .dia_outer

.dia_bind:
        xor ecx, ecx
.dia_bind_loop:
        cmp ecx, [drv_count]
        jae .dia_done
        mov eax, [edi + ecx*4]
        imul eax, DRV_SIZE
        ; A driver bound in an earlier phase is not bound twice.
        cmp dword [drv_table + eax + DR_STATE], DRV_STATE_NEW
        jne .dia_bind_next
        mov eax, [drv_table + eax + DR_ORDER]
        cmp eax, [.dip_limit]
        ja .dia_bind_next
        mov eax, [edi + ecx*4]
        push ecx
        call drv_bind_one
        pop ecx
.dia_bind_next:
        inc ecx
        jmp .dia_bind_loop

.dia_done:
        ; Count what is bound, from scratch: the count is the state of the
        ; machine, not a running total of how many phases have run.
        mov dword [drv_bound], 0
        xor ecx, ecx
.dia_count:
        cmp ecx, [drv_count]
        jae .dia_out
        mov eax, ecx
        imul eax, DRV_SIZE
        cmp dword [drv_table + eax + DR_STATE], DRV_STATE_BOUND
        jne .dia_count_next
        inc dword [drv_bound]
.dia_count_next:
        inc ecx
        jmp .dia_count
.dia_out:
        mov eax, [drv_bound]
        mov [esp + 28], eax
        popad
        ret
.dip_limit: dd 0
.dip_platform_done: dd 0

drv_order_buf:      times DRV_MAX dd 0

;-----------------------------------------------------------------------
; dev_add_pci_all - put every enumerated PCI function in the device tree
;
; Called at the start of each phase: the first call adds nothing, because PCI
; has not been enumerated yet, and the second adds what it found. Adding twice
; would double every node, so the count is remembered.
;-----------------------------------------------------------------------
dev_add_pci_all:
        pushad
        cmp dword [.dapa_done], 0
        jne .dapa_out
        ; Nothing to add before the bus has been enumerated, and marking the
        ; work done here would leave the tree without a single card in it.
        cmp dword [pci_dev_count], 0
        je .dapa_out
        mov dword [.dapa_done], 1
        xor ecx, ecx
.dapa_loop:
        cmp ecx, [pci_dev_count]
        jae .dapa_out
        mov esi, ecx
        imul esi, PCI_ENTRY_SIZE
        add esi, pci_dev_table
        push ecx
        call dev_add_pci
        pop ecx
        inc ecx
        jmp .dapa_loop
.dapa_out:
        popad
        ret
.dapa_done: dd 0

;-----------------------------------------------------------------------
; driver_power - tell every bound driver what is about to happen
;   In : EAX = DRV_POWER_RESUME / SUSPEND / OFF
;   Out: EAX = drivers that refused
;
; Called on the way down and, later, on the way back up from a sleep state.
; A driver with no callback is assumed to have nothing to do, which is true of
; a driver whose device keeps no state the kernel cares about.
;-----------------------------------------------------------------------
driver_power:
        pushad
        mov [.dp_state], eax
        mov dword [.dp_refused], 0
        xor ecx, ecx
.dp_loop:
        cmp ecx, [drv_count]
        jae .dp_done
        mov eax, ecx
        imul eax, DRV_SIZE
        lea esi, [drv_table + eax]
        cmp dword [esi + DR_STATE], DRV_STATE_BOUND
        jne .dp_next
        mov eax, [esi + DR_POWER]
        test eax, eax
        jz .dp_next
        mov eax, [.dp_state]
        push ecx
        push esi
        call [esi + DR_POWER]
        pop esi
        pop ecx
        test eax, eax
        jz .dp_next
        inc dword [.dp_refused]
.dp_next:
        inc ecx
        jmp .dp_loop
.dp_done:
        mov eax, [.dp_refused]
        mov [esp + 28], eax
        popad
        ret
.dp_state:   dd 0
.dp_refused: dd 0

;-----------------------------------------------------------------------
; drv_state_name - EAX = state -> EAX = string
;-----------------------------------------------------------------------
drv_state_name:
        cmp eax, DRV_STATE_BOUND
        je .dsn_bound
        cmp eax, DRV_STATE_ABSENT
        je .dsn_absent
        cmp eax, DRV_STATE_CLAIMED
        je .dsn_claimed
        cmp eax, DRV_STATE_FAILED
        je .dsn_failed
        cmp eax, DRV_STATE_SKIPPED
        je .dsn_skipped
        mov eax, drv_state_new
        ret
.dsn_bound:   mov eax, drv_state_bound
        ret
.dsn_absent:  mov eax, drv_state_absent
        ret
.dsn_claimed: mov eax, drv_state_claimed
        ret
.dsn_failed:  mov eax, drv_state_failed
        ret
.dsn_skipped: mov eax, drv_state_skipped
        ret

;-----------------------------------------------------------------------
; driver_show - the `drivers` command
;-----------------------------------------------------------------------
driver_show:
        pushad
        mov esi, msg_drivers_head
        call vga_print
        mov eax, [drv_count]
        call vga_print_dec
        mov esi, msg_drivers_reg
        call vga_print
        mov eax, [drv_bound]
        call vga_print_dec
        mov esi, msg_drivers_bound
        call vga_print
        mov eax, [res_conflicts]
        call vga_print_dec
        mov esi, msg_drivers_conf
        call vga_print

        mov esi, msg_devices_head
        call vga_print
        mov eax, [dev_count]
        call vga_print_dec
        mov esi, msg_devices_nodes
        call vga_print

        xor ecx, ecx
.dsh_loop:
        cmp ecx, [drv_count]
        jae .dsh_resources
        mov eax, ecx
        imul eax, DRV_SIZE
        lea ebx, [drv_table + eax]

        mov al, ' '
        call vga_putchar
        mov al, ' '
        call vga_putchar

        ; name, padded to 12 columns so the states line up
        mov esi, [ebx + DR_NAME]
        call vga_print
        mov esi, [ebx + DR_NAME]
        xor edx, edx
.dsh_len:
        cmp byte [esi + edx], 0
        je .dsh_pad
        inc edx
        jmp .dsh_len
.dsh_pad:
        cmp edx, 12
        jae .dsh_state
        mov al, ' '
        call vga_putchar
        inc edx
        jmp .dsh_pad

.dsh_state:
        mov eax, [ebx + DR_STATE]
        call drv_state_name
        mov esi, eax
        call vga_print

        ; Bound drivers say where; the rest say why not.
        cmp dword [ebx + DR_STATE], DRV_STATE_BOUND
        jne .dsh_note
        mov al, ' '
        call vga_putchar
        mov eax, [ebx + DR_DEV]
        cmp eax, -1
        je .dsh_platform
        imul eax, DV_SIZE
        lea esi, [dev_table + eax]
        mov eax, [esi + DV_KIND]
        cmp eax, DV_KIND_PCI
        mov esi, drv_kind_platform
        jne .dsh_kind
        mov esi, drv_kind_pci
.dsh_kind:
        call vga_print
        mov eax, [ebx + DR_DEV]
        imul eax, DV_SIZE
        lea esi, [dev_table + eax]
        cmp dword [esi + DV_KIND], DV_KIND_PLATFORM
        je .dsh_named
        mov eax, [esi + DV_VENDOR]
        call vga_print_hex
        mov al, ':'
        call vga_putchar
        mov eax, [esi + DV_DEVICE]
        call vga_print_hex
        jmp .dsh_nl
.dsh_named:
        mov esi, [esi + DV_NAME]
        call vga_print
        jmp .dsh_nl
.dsh_platform:
        mov esi, drv_kind_platform
        call vga_print
        mov esi, [ebx + DR_NAME]
        call vga_print
        jmp .dsh_nl

.dsh_note:
        mov esi, [ebx + DR_NOTE]
        test esi, esi
        jz .dsh_nl
        mov al, ' '
        call vga_putchar
        call vga_print
.dsh_nl:
        mov al, 0x0A
        call vga_putchar
        inc ecx
        jmp .dsh_loop

.dsh_resources:
        mov esi, msg_res_head
        call vga_print
        mov eax, [res_count]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar
        popad
        ret

;-----------------------------------------------------------------------
; The platform devices: the hardware a PC has because it is a PC
;
; Their port ranges are the architectural ones, and they are in the tree for
; exactly one reason: so a PCI device that has been mapped over them is caught
; by the arbitration instead of silently sharing a register.
;-----------------------------------------------------------------------
PLAT_ID             equ 0
PLAT_IRQ            equ 4
PLAT_IOBASE         equ 8
PLAT_IOLEN          equ 12
PLAT_MEMBASE        equ 16
PLAT_MEMLEN         equ 20
PLAT_NAME           equ 24
PLAT_SIZE           equ 28

PLAT_ID_PIT         equ 1
PLAT_ID_PIC         equ 2
PLAT_ID_PS2         equ 3
PLAT_ID_ATA         equ 4
PLAT_ID_SB16        equ 5
PLAT_ID_VGA         equ 6
PLAT_ID_SERIAL      equ 7
PLAT_ID_APIC        equ 8
PLAT_ID_DMA         equ 9

dev_platform_list:
        dd PLAT_ID_PIT,   0xFF, 0x40,  4, 0, 0, dn_plat_pit
        dd PLAT_ID_PIC,   0xFF, 0x20,  2, 0, 0, dn_plat_pic
        dd PLAT_ID_PS2,   0x01, 0x60,  5, 0, 0, dn_plat_ps2
        dd PLAT_ID_ATA,   0x0E, 0x1F0, 8, 0, 0, dn_plat_ata
        dd PLAT_ID_DMA,   0x00, 0x00, 16, 0, 0, dn_plat_dma
        dd PLAT_ID_SB16,  0x05, 0x220, 16, 0, 0, dn_plat_sb16
        dd PLAT_ID_SERIAL,0x04, 0x3F8, 8, 0, 0, dn_plat_serial
        dd PLAT_ID_VGA,   0xFF, 0x1CE, 2, 0, 0, dn_plat_vga
        dd -1, 0, 0, 0, 0, 0, 0

dn_plat_pit:    db "8254 PIT", 0
dn_plat_pic:    db "8259 PIC", 0
dn_plat_ps2:    db "PS/2 controller", 0
dn_plat_ata:    db "legacy IDE channel 0", 0
dn_plat_dma:    db "8237 DMA controller", 0
dn_plat_sb16:   db "Sound Blaster ports", 0
dn_plat_serial: db "COM1", 0
dn_plat_vga:    db "VGA adapter", 0
