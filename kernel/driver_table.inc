;=======================================================================
; driver_table.inc - the drivers this image carries
;
; One module, registered through the same header a loadable module would carry,
; so the manager has a single way in. Each entry is a driver record: what it
; serves, what to call, and when relative to the others.
;
; Orders are phases, not preferences. Under 100 is the hardware the
; architecture guarantees, bound before the frame allocator exists because the
; rest of the boot reports through it. At or above 100 is everything that has
; to wait for PCI enumeration.
;=======================================================================

DRV_ORDER_CORE      equ 10              ; interrupt controller, clock, keyboard
DRV_ORDER_SERIAL    equ 20
DRV_ORDER_STORAGE   equ 100             ; the disk everything else mounts
DRV_ORDER_NET       equ 110
DRV_ORDER_SOUND     equ 120
DRV_ORDER_VIDEO     equ 130
DRV_ORDER_BUS       equ 140             ; AHCI/virtio after the legacy paths

;-----------------------------------------------------------------------
; Attach thunks
;
; A driver's attach routine returns 0 for "bound" and -1 for "I matched the
; device but cannot use it". The existing init routines were written to be
; called unconditionally, so each thunk is exactly that call plus the answer:
; what they found is recorded in their own state, and their own diagnostics
; stay where they were.
;-----------------------------------------------------------------------
drv_attach_pic:
        call pic_init
        xor  eax, eax
        ret

drv_attach_pit:
        call pit_init
        xor  eax, eax
        ret

; The PS/2 controller owns both ports: the keyboard is the main port and the
; mouse the auxiliary one. One driver, because they are one device - two
; drivers would be a resource conflict over the same command byte.
drv_attach_ps2:
        call kb_init
        call mouse_init
        xor  eax, eax
        ret

drv_attach_serial:
        call serial_init
        xor  eax, eax
        ret

drv_attach_ata:
        call ata_init
        xor  eax, eax
        ret

drv_attach_net:
        call net_init
        xor  eax, eax
        ret

drv_attach_e1000:
        call e1000_init
        xor  eax, eax
        ret

drv_attach_sb16:
        call sb16_init
        xor  eax, eax
        ret

drv_attach_vbe:
        call vbe_init
        xor  eax, eax
        ret

drv_attach_ac97:
        call ac97_init
        xor  eax, eax
        ret

drv_attach_atadma:
        call atadma_init
        xor  eax, eax
        ret

drv_attach_ahci:
        call ahci_init
        xor  eax, eax
        ret

drv_attach_virtio:
        call virtio_init
        xor  eax, eax
        ret

;-----------------------------------------------------------------------
; Power callbacks
;
; Only the disk has something to do on the way down right now: its write cache
; and the filesystem's own metadata belong on the platter before the power
; goes. The rest keep no state the kernel would have to save - a PS/2
; controller and a PIT are programmed by the firmware or by us, and neither has
; anything in flight.
;-----------------------------------------------------------------------
drv_power_ata:
        cmp  eax, DRV_POWER_OFF
        jne  .dpa_ok
        call hbfs_sync
        test eax, eax
        jnz  .dpa_ok                    ; a failed sync is reported by shutdown
.dpa_ok:
        xor  eax, eax
        ret

;-----------------------------------------------------------------------
; Match tables
;-----------------------------------------------------------------------
mt_pic:
        dd MT_PLATFORM, PLAT_ID_PIC, 0
        dd 0
mt_pit:
        dd MT_PLATFORM, PLAT_ID_PIT, 0
        dd 0
mt_ps2:
        dd MT_PLATFORM, PLAT_ID_PS2, 0
        dd 0
mt_serial:
        dd MT_PLATFORM, PLAT_ID_SERIAL, 0
        dd 0
mt_ata:
        dd MT_PLATFORM, PLAT_ID_ATA, 0
        dd 0
mt_sb16:
        dd MT_PLATFORM, PLAT_ID_SB16, 0
        dd 0
mt_vbe:
        dd MT_PLATFORM, PLAT_ID_VGA, 0
        dd 0
; The RTL8139 the network stack was written against. net_init scans for it
; itself; the table is what decides whether it is worth calling.
mt_net:
        dd MT_PCI_ID, 0x10EC, 0x8139
        dd 0
; Every e1000 variant this driver knows, which is what e1000_init scans for.
mt_e1000:
        dd MT_PCI_ID, 0x8086, 0x100E
        dd MT_PCI_ID, 0x8086, 0x100F
        dd MT_PCI_ID, 0x8086, 0x1076
        dd MT_PCI_ID, 0x8086, 0x1011
        dd 0
; Audio and storage by class: a card from a vendor nobody wrote a driver for is
; still the class it claims to be, and the class is what the driver serves.
mt_ac97:
        dd MT_PCI_CLASS, 0x0401, 0           ; multimedia / audio
        dd 0
mt_atadma:
        dd MT_PCI_CLASS, 0x0101, 0           ; mass storage / IDE
        dd 0
mt_ahci:
        dd MT_PCI_CLASS, 0x0106, 0           ; mass storage / SATA AHCI
        dd 0
mt_virtio:
        dd MT_VENDOR, 0x1AF4, 0              ; any virtio device
        dd 0

;-----------------------------------------------------------------------
; The driver records
;-----------------------------------------------------------------------
drv_module_drivers:
        ; name, match kind, table, attach, detach, power, order, (state, dev, note)
pic_driver:
        dd drv_name_pic,   DRV_MATCH_TABLE, mt_pic,    drv_attach_pic,    0, 0, DRV_ORDER_CORE,     0, 0, 0
pit_driver:
        dd drv_name_pit,   DRV_MATCH_TABLE, mt_pit,    drv_attach_pit,    0, 0, DRV_ORDER_CORE,     0, 0, 0
ps2_driver:
        dd drv_name_ps2,   DRV_MATCH_TABLE, mt_ps2,    drv_attach_ps2,    0, 0, DRV_ORDER_CORE,     0, 0, 0
serial_driver:
        dd drv_name_serial,DRV_MATCH_TABLE, mt_serial, drv_attach_serial, 0, 0, DRV_ORDER_SERIAL,   0, 0, 0
ata_driver:
        dd drv_name_ata,   DRV_MATCH_TABLE, mt_ata,    drv_attach_ata,    0, drv_power_ata, DRV_ORDER_STORAGE, 0, 0, 0
net_driver:
        dd drv_name_net,   DRV_MATCH_TABLE, mt_net,    drv_attach_net,    0, 0, DRV_ORDER_NET,      0, 0, 0
e1000_driver:
        dd drv_name_e1000, DRV_MATCH_TABLE, mt_e1000,  drv_attach_e1000,  0, 0, DRV_ORDER_NET + 1,  0, 0, 0
sb16_driver:
        dd drv_name_sb16,  DRV_MATCH_TABLE, mt_sb16,   drv_attach_sb16,   0, 0, DRV_ORDER_SOUND,    0, 0, 0
ac97_driver:
        dd drv_name_ac97,  DRV_MATCH_TABLE, mt_ac97,   drv_attach_ac97,   0, 0, DRV_ORDER_SOUND + 1, 0, 0, 0
vbe_driver:
        dd drv_name_vbe,   DRV_MATCH_TABLE, mt_vbe,    drv_attach_vbe,    0, 0, DRV_ORDER_VIDEO,    0, 0, 0
virtio_driver:
        dd drv_name_virtio,DRV_MATCH_TABLE, mt_virtio, drv_attach_virtio, 0, 0, DRV_ORDER_BUS,      0, 0, 0
atadma_driver:
        dd drv_name_atadma,DRV_MATCH_TABLE, mt_atadma, drv_attach_atadma, 0, 0, DRV_ORDER_BUS + 1,  0, 0, 0
ahci_driver:
        dd drv_name_ahci,  DRV_MATCH_TABLE, mt_ahci,   drv_attach_ahci,   0, 0, DRV_ORDER_BUS + 2,  0, 0, 0
drv_module_drivers_end:

DRV_MODULE_COUNT equ (drv_module_drivers_end - drv_module_drivers) / DRV_SIZE

;-----------------------------------------------------------------------
; The module header. The C module slot uses the same idea for the same reason:
; a table is validated before any pointer in it is trusted.
;-----------------------------------------------------------------------
drv_builtin_module:
        dd DRV_MODULE_MAGIC
        dd DRV_MODULE_VERSION
        dd drv_module_drivers
        dd DRV_MODULE_COUNT
        dd drv_exports
        dd drv_module_name

drv_module_name:  db "mellivora built-in drivers", 0

; Export table: the kernel entry points a driver may call, by name. Built-in
; drivers are linked and need none of it; it is what a module loaded later
; would search, and what `drivers` counts to show the interface has content.
drv_exports:
        dd drv_exp_1,  irq_enable
        dd drv_exp_2,  irq_disable
        dd drv_exp_3,  pci_find_device
        dd drv_exp_4,  pci_read_config32
        dd drv_exp_5,  pmm_alloc_page
        dd drv_exp_6,  pmm_free_page
        dd drv_exp_7,  kmem_alloc
        dd drv_exp_8,  kmem_free
        dd drv_exp_9,  vga_print
        dd drv_exp_10, bootlog_write
        dd drv_exp_11, serial_print
        dd drv_exp_12, irq_route
        dd 0, 0

drv_exp_1:  db "irq_enable", 0
drv_exp_2:  db "irq_disable", 0
drv_exp_3:  db "pci_find_device", 0
drv_exp_4:  db "pci_read_config32", 0
drv_exp_5:  db "pmm_alloc_page", 0
drv_exp_6:  db "pmm_free_page", 0
drv_exp_7:  db "kmem_alloc", 0
drv_exp_8:  db "kmem_free", 0
drv_exp_9:  db "vga_print", 0
drv_exp_10: db "bootlog_write", 0
drv_exp_11: db "serial_print", 0
drv_exp_12: db "irq_route", 0

drv_name_pic:     db "pic", 0
drv_name_pit:     db "pit", 0
drv_name_ps2:     db "ps2", 0
drv_name_serial:  db "serial", 0
drv_name_ata:     db "ata", 0
drv_name_net:     db "rtl8139", 0
drv_name_e1000:   db "e1000", 0
drv_name_sb16:    db "sb16", 0
drv_name_ac97:    db "ac97", 0
drv_name_vbe:     db "vbe", 0
drv_name_virtio:  db "virtio", 0
drv_name_atadma:  db "atadma", 0
drv_name_ahci:    db "ahci", 0

msg_drv_register:  db "DRV_REGISTER count=", 0
msg_drv_bound:     db "DRV_BOUND phase=", 0
