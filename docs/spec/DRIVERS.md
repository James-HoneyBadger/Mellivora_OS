# Driver model (normative)

**Normative from:** M2. **Governs:** what a driver is, how it is loaded, how a
device is discovered and given its resources, how an I/O request travels and
completes, how a driver handles power and surprise removal, and how it is
proven before it is trusted.

Today every driver is a `.inc` file compiled into the kernel: `ata.inc`,
`e1000.inc`, `ac97.inc`, twenty more. That works and it is why the system boots,
but it means a driver cannot be replaced, unloaded, power-managed per device, or
written by anyone without the whole tree. This document is the replacement.

## 1. Model

The kernel is monolithic with **loadable modules** ([D6](../ROADMAP.md#architectural-decisions)).
A module is not a microkernel server: it runs in ring 0 with the kernel's
privileges, and its trust boundary is the loader's signature check plus the
developer's discipline. What modularity buys is replacement, unload, per-device
power and a testable interface — not isolation.

Tree of objects ([OBJECTS.md](OBJECTS.md#3-types)):

```text
\Drivers\<name>                     the driver object: module, dispatch table
\Devices\<bus>\<n>                  the physical device (PDO)
\Devices\<bus>\<n>\<function>       the functional device object (FDO), 0..n
\Devices\<bus>\<n>\filter\<k>       filter devices stacked between PDO and FDO
```

An I/O request enters at the top of the stack and travels down; a completion
travels up. A filter may inspect, modify, delay or refuse, which is how a volume
snapshot, an encrypting disk filter or a fault-injecting test device is built
without touching the driver below it.

## 2. Module format

A module is a flat image with a header and three tables:

```text
mv_module_header
  u32 magic          'MVMD'
  u32 version        module ABI version
  u32 size           sizeof(header)
  u32 code_size      bytes of code+rodata
  u32 data_size      bytes of initialised data, then BSS
  u64 load_bias      where it was linked, checked against where it landed
  u32 entry          offset of the load routine
  u32 unload         offset of the unload routine
  u32 exports        offset of the export table
  u32 imports        offset of the import table
  u32 image_hash     CRC-32C over the whole image
```

| Table | Contents |
|---|---|
| **Exports** | `{name, offset, version}` — what other modules may call |
| **Imports** | `{module, name, version}` — resolved at load, refused whole if any is missing |
| **Types** | Object types the module registers ([OBJECTS.md](OBJECTS.md#3-types)) |
| **Devices** | Device names created at start, with their dispatch table |

Load sequence: verify the hash, map the code read-only and the data read-write
in the module window, relocate nothing (the image is linked at the address it is
loaded to, chosen by the loader and reported to the module in its header
check), resolve imports, call `entry`. A failure at any step unwinds everything
already done and reports the step by name.

Unload requires: every device object closed, every IRP completed, no timer or
DPC outstanding, no exported symbol referenced by another loaded module. The
loader waits for a rundown, then frees. `SE_LOAD_DRIVER` gates both.

## 3. Discovery and the device tree

| Bus driver | Enumerates from | Wave |
|---|---|---|
| PCI | Configuration space: vendor/device/class, BARs, capability list | M2 |
| ACPI | `_CRS`, `_STA`, the `DSDT` device tree | M2 |
| USB | Class and interface descriptors | M8 |
| Root | Built-in devices: the PIT, the PIC, the console | M2 |

Each PDO records: its bus id, its compatible ids, its resources (from `_CRS` or
the BARs), its power capabilities, and its list of child devices. A **driver
match** is a table entry `{bus, compatible id, driver name}`; the first match
wins and the loser is recorded so a "why did this device get that driver" answer
exists.

## 4. Resources

| Resource | Description | Arbitration |
|---|---|---|
| `MEM` | A physical range for MMIO, with a cache policy ([PLATFORM.md](PLATFORM.md#7-caching)) | Range must be free and device-owned |
| `PORT` | An I/O port range | Range must be free |
| `IRQ` | A GSI plus trigger mode, polarity and affinity | Claimed from the I/O APIC's table |
| `DMA` | A bus-master address width and a channel | Map registers or an IOMMU when present |
| `BUS` | A bus-number range for a bridge | Range must be free |

Resources are handed to the driver at start, never discovered by it. A driver
that pokes a global register it was not given is a bug the model cannot prevent,
but every global register a driver *can* be given is in the table.

A device that cannot get its resources does not start; it reports which resource
and what else holds it, and the device stays in `FAILED_START` rather than
half-initialised.

## 5. I/O requests

The unit of I/O is an **IRP**:

```text
mv_irp
  u32 header_size, struct_size, version      /* extensible, ABI.md §11 */
  u32 type                                   /* CREATE, CLOSE, READ, WRITE, IOCTL,
                                                FLUSH, PNP, POWER, CLEANUP, SHUTDOWN */
  u32 flags                                  /* synchronous, no-cache, paging-io, ... */
  u32 status
  u32 information                            /* bytes transferred */
  u64 offset
  u32 buffer, length                         /* user or kernel, per flags */
  u32 mdl                                    /* the locked page list, if used */
  u32 completion, completion_context
  u32 cancel_routine
  u32 stack_count
  mv_io_stack stack[]                        /* one per device in the stack */
```

| Rule | Contract |
|---|---|
| Stacking | One `mv_io_stack` entry per device the request passes through; a driver sets the next device's routine and calls it |
| Completion | Runs at `DISPATCH` unless the IRP says `PASSIVE`; it may complete the caller or return the IRP for another pass |
| Pending | A driver that cannot complete returns `MV_PENDING` and owns the IRP until it completes or cancels it |
| Cancellation | One cancel routine per IRP; the cancel path and the completion path are mutually exclusive, and the loser does nothing |
| Queuing | Per-device queues are ordered by type first, then arrival; a read may not be starved by a stream of writes |
| Buffering | `BUFFERED` (kernel copy), `DIRECT` (MDL, page-locked), `NEITHER` (driver maps it) — the driver declares what it supports and the I/O manager refuses what it does not |
| Timeouts | Optional per IRP; expiry runs the cancel routine, and a driver that ignores cancellation is named in the log |

## 6. Interrupts, deferred work and threads

| Stage | Runs at | May do |
|---|---|---|
| ISR | `DIRQL`, interrupts off on that line | Acknowledge, read the status word, queue a DPC, return |
| DPC | `DISPATCH` | Complete IRPs, start the next transfer, signal events |
| Worker | `PASSIVE` | Anything that may block or page: allocate paged pool, touch user memory, take a mutant |

The ISR never searches a list, never logs and never loops over more than 32
completions: a device with more work leaves the rest for the DPC. This is the
difference between a driver that survives an interrupt storm and one that makes
the machine appear dead.

## 7. DMA

| Mode | Requirement |
|---|---|
| Bus-master, no IOMMU | Buffers must be physically contiguous, or described by map registers; a scatter-gather list is built from the MDL |
| Bus-master, with IOMMU | The MDL is mapped; no bounce buffer needed |
| Slave DMA | Channel claimed as a resource; the transfer is programmed by the platform layer |

A 32-bit device must be given memory it can address. The DMA layer owns that
decision: it either allocates below the device's limit or installs a bounce
buffer, and it never hands a driver a page descriptor the device cannot reach.

## 8. Power and PnP

| Callback | When | Contract |
|---|---|---|
| `POWER_QUERY` | Before a system state change | May refuse; refusal cancels the whole transition and names the device |
| `POWER_SET` | Entering `D0`..`D3` or `S0`..`S5` | Must save or restore device state; may not block above `PASSIVE` |
| `PNP_START` | Device resources are assigned | Configure and claim |
| `PNP_STOP` | Resources are being taken away | Quiesce, complete or cancel outstanding IRPs, release |
| `PNP_SURPRISE_REMOVAL` | The device vanished | Fail everything outstanding, do not touch the hardware again |
| `PNP_EJECT` | Before a safe removal | Refuse while a volume is mounted ([STORAGE.md](STORAGE.md)) |

A driver that fails `PNP_STOP` is unloaded forcibly and the failure is recorded
against the module.

## 9. Every driver is a test fixture

| Requirement | Why |
|---|---|
| A driver can be told to fail any I/O with a given status | Fault injection is how the layers above are proven ([RELIABILITY.md](RELIABILITY.md#7-fault-injection)) |
| A driver reports counters: IRPs, bytes, errors, timeouts, storms, power transitions | A driver with no counters cannot be diagnosed |
| A driver can be unloaded after a soak with zero outstanding IRPs | A leak here is a machine that cannot be updated |
| Every driver has a QEMU-reachable device | A driver with no test is not in scope ([D8](../ROADMAP.md#architectural-decisions)) |

## 10. Conformance

M2 does not close until:

1. Every existing driver is a module, loads from the volume, and unloads with no
   outstanding IRP and no leaked handle.
2. `driver_test` starts, stops and restarts every device 1 000 times with no
   leak and no wedge.
3. `pnp_test` attaches and detaches a filter driver at run time, and the volume
   below it keeps serving I/O throughout.
4. A driver that refuses `POWER_QUERY` cancels the sleep, names itself, and the
   machine stays awake and consistent.
5. Surprise removal of a storage device fails outstanding I/O with
   `MV_DEVICE_REMOVED` and does not panic.
6. An injected interrupt storm re-targets and does not lose an interrupt.

## 11. As built (the gap)

The registry, the device tree, the arbitration and the ordering exist. The
device stack, the IRP path and replacement at runtime do not.

| Contract | Tree today |
|---|---|
| A driver is a record, not a call site | `kernel/driver.inc` registers records: name, a match table, attach, detach, power, order. `kernel/driver_table.inc` is the table of the thirteen in-tree drivers. Nothing in `kernel.asm` calls `ata_init`, `net_init`, `e1000_init` … any more |
| Registration goes through a module header | `driver_module_register` validates magic, version, count and table pointer before reading a record. The in-tree drivers are one module with an export table (`irq_enable`, `pci_find_config`, `pmm_alloc_page`, `bootlog_write`, …) |
| Device tree | `dev_table`: one node per PCI function (vendor, device, class, BDF, IRQ, BAR0 with its *real* length measured by the write-all-ones method) plus the eight platform devices a PC has without being asked (8259, PIT, PS/2, IDE, DMA, SB16 ports, COM1, VGA) |
| Match tables | `MT_PCI_ID`, `MT_PCI_CLASS`, `MT_VENDOR`, `MT_PLATFORM`, terminated by a zero entry. A driver with four device IDs and one with a class rule are the same shape, and no matching lives in code |
| Resource arbitration | I/O ranges and memory windows are refused on intersection; interrupt lines are refused on duplicate **unless the device is on PCI**, where one level-triggered pin is shared by design. Claims are recorded, and a claimed memory window is also mapped into the master and active page directories, so a driver that binds after paging is up can touch its registers |
| Binding order | Two phases by `DR_ORDER`: the interrupt controller, clock, keyboard controller and serial before the frame allocator exists; everything else after PCI enumeration. Registration order does not decide binding order |
| States, and why they are states | `bound`, `absent`, `claimed`, `failed`, `skipped`. A driver that found no device has *not* failed, and `drivers` says so, with the reason |
| Boot log | Every bind and every absence is a record in the boot-log ring at INFO or DEBUG, so a machine that did not start a driver says which and why |
| Power callbacks | `driver_power(state)` walks bound drivers. The disk syncs on `OFF`; the others keep no state the kernel would have to save |
| Per-device power objects | None: power is a callback, not an object in a device stack |
| IRP path, device stacks, filters | None: drivers still expose direct calls (`ata_read_sectors`, `net_send`) to their subsystem. This is the seam the VFS and network waves plug into |
| Load and unload at runtime | None: the module header is real and validated, but the image is linked in, so "load" means "register what is already there". Relocatable modules need a symbol table and a base-relative fixup pass — the export table is the half that exists |
| Driver counters | Per-processor job counters (`cpu_blocks`) and the registry's own states; no per-device I/O statistics yet |
| Conformance | `drivers` in `make check-runtime`: 13 registered, 10 bound, 0 conflicts on the profile machine; `rtl8139 bound pci` names the device it took |

The devices work and now have a boundary around them. What that boundary still
lacks is the half that needs a device stack: filters, per-device power, and
replacement without a rebuild.
