# Platform (normative)

**Normative from:** M2. **Governs:** how the machine describes itself (ACPI),
how interrupts reach a processor, how time is measured, how processors are
started, how the machine sleeps and switches off, and which CPU features the
kernel is allowed to assume.

The rule behind all of it: **the firmware describes, the kernel verifies, and
nothing is assumed because a table said so.** QEMU's MADT advertises a local
APIC even on a model that has none — the `p4-noapic` profile is that machine,
and it is in the test matrix precisely because a table describes a machine
without guaranteeing it. Every fact below is probed and can be refused.

## 1. Firmware hand-off

| Source | Used for | Trust |
|---|---|---|
| BIOS `INT 0x15/E820` | A first look at physical memory, before ACPI is parsed | Cross-checked against the ACPI tables; a disagreement is reported and ACPI wins |
| BIOS `INT 0x15/E801` | Memory above 64 MB for very old machines | Fallback only |
| ACPI RSDP | The root of every other ACPI table | Checksum verified before anything is read |
| MP tables | Not used | The kernel does not read them; ACPI replaced them in 1999 |

UEFI is out of scope for the parity target. The boot path stays BIOS-first, and
the loader passes a **boot information block** at `0x00000500` whose layout is
versioned and asserted ([EXEC.md](EXEC.md#9-boot-information)).

## 2. ACPI

### 2.1 Tables the kernel reads

| Table | Provides | Required |
|---|---|---|
| `RSDT` / `XSDT` | The table directory | One of them |
| `FADT` | Power management registers, the `DSDT` pointer, the SCI interrupt, reset register | Yes |
| `MADT` | Processor list, LAPIC address, I/O APICs, interrupt source overrides, NMI sources | Yes for SMP |
| `HPET` | The high-precision timer's base and comparators | No |
| `SRAT` / `SLIT` | Processor and memory topology, NUMA distances | No, but used when present |
| `MCFG` | PCIe enhanced configuration access | No |
| `ECDT` | The embedded controller, when the `DSDT` needs it early | No |
| `DSDT` | `_PRT` (PCI interrupt routing), `_Sx` sleep methods, the power button, thermal zones | Yes for power management |

The `XSDT` is preferred when the `FADT` revision is 2 or later; a 64-bit entry
whose high half is not zero is **refused** rather than truncated, because a table
above 4 GiB is a machine this kernel does not claim to support and silently using
the low half would describe the wrong memory.

### 2.2 The AML interpreter

A deliberately small subset, because a full interpreter is a large attack surface
and the plan needs six methods, not six hundred:

| Method | Used for |
|---|---|
| `_PRT` | PCI interrupt routing |
| `_CRS` | Device resources for PnP ([DRIVERS.md](DRIVERS.md#4-resources)) |
| `_STA` | Device presence |
| `_Sx` / `_S0` | Sleep-state entry and wake |
| `_TMP` / `_PSV` | Thermal zone temperature and the passive trip point |
| `_BIF` / `_BST` | Battery information and state |
| `_PIC` | Selecting interrupt model |

Anything else is not evaluated. The interpreter is bounded: a maximum of
**65 536 opcodes per method**, a nesting depth of **32**, and a failure is
reported by name (`_PRT refused: …`) and treated as "not described", never as
"assume the default".

## 3. Interrupts

| Element | Contract |
|---|---|
| IDT | 256 gates, `DPL 0` except `int 0x80` (user-callable), each with an IST for `#DF`, `NMI` and `#MC` |
| Controller selection | Probed, not trusted: enable the local APIC, read it back; only if the write sticks are the 8259s masked and the I/O APIC programmed. [As built.] |
| Routing | Redirection entries from the `MADT` interrupt source overrides; `_PRT` for PCI links; a line the firmware does not describe is taken as ISA edge/high except the two PCI link lines |
| Message-signalled | MSI and MSI-X preferred over INTx when the device and the `MCFG` allow it; per-vector affinity |
| Affinity | Every line has a target processor and a fallback; an interrupt storm is detected by a per-line rate counter and re-targeted |
| Acknowledgment | Local APIC EOI; a spurious interrupt is counted, not fatal |
| 8259 fallback | Required: a machine with no usable APIC must behave exactly as it does today |
| Interrupt-to-thread | The ISR acknowledges and defers; work runs at `DISPATCH` then `PASSIVE` ([SCHED.md](SCHED.md#4-waiting-and-wait-reasons)) |

## 4. Timers

| Clock | Use | Fallback when absent |
|---|---|---|
| HPET | Clock of record: monotonic, 1 ns resolution | LAPIC timer |
| LAPIC timer | Per-processor deadlines, calibrated against the clock of record | PIT |
| PIT | Boot-time calibration and the last resort tick | — |
| TSC | Fast timestamp reads, only when `CPUID` says invariant | HPET read |
| RTC / CMOS | Wall clock at boot and at resume | Manual set |

The invariant TSC is used only when the CPU reports it as invariant **and**
`CPUID` reports a non-stop TSC; otherwise a TSC read is a hint and the clock of
record is authoritative. A calibration that disagrees with the earlier one by
more than 1 % discards the fast clock and says so — a wrong clock that is fast is
worse than a slow clock that is right.

## 5. Processors

### 5.1 Start-up

1. The bootstrap processor parses the `MADT` and builds the processor table.
2. For every other APIC id: `INIT`, wait 10 ms, `SIPI` twice with a 200 µs gap,
   addressed to the id from the table. A processor that does not appear within
   100 ms is marked absent and the boot line says how many of how many came up.
3. The trampoline at a 4 KiB-aligned page below 1 MiB switches to protected
   mode with PAE, loads the kernel's `CR3`, and enters `ap_entry`.
4. Every processor gets: a per-CPU block, a kernel stack, a GDT/TSS/IDT, its
   `MTRR`/`PAT` programming copied from the BSP, and its idle thread.
5. Application processors are **not parked** ([S1](README.md#decisions-this-set-records)):
   they take interrupts, run the scheduler and execute work. Parking was the
   right first step and is now the bug.

### 5.2 Ordering and coherence

- Every lock is `lock cmpxchg` based; a store-release/load-acquire pair is used
  where a lock would be disproportionate.
- A mapping change sends a TLB-shootdown IPI to every other processor and waits
  for acknowledgment ([VMM.md](VMM.md#31-three-levels-pae)).
- Inter-processor interrupts: reschedule, halt, flush-TLB, call-function. Each is
  counted per processor and reported by `acpi`.

## 6. Power

| State | Behaviour |
|---|---|
| `S0` working | Normal |
| `C1`/`C2`/`C3` | Idle-loop halt depth, chosen from the `FADT` and `_CST`; entered only when the next deadline allows |
| `S1`–`S3` sleep | Device power callbacks, processor context saved, wake from the `FADT`'s wake GPE or the power button |
| `S4` hibernate | Out of scope for M2; needs the storage stack's free-space guarantee ([STORAGE.md](STORAGE.md)) |
| `S5` soft-off | `FADT` reset/`PM1_CNT` write; the machine powers off rather than halting |
| Reset | `FADT` reset register first, then the keyboard controller, then a triple fault as the last resort |

Sleep entry requires every driver to accept the transition; a driver that says no
cancels the sleep and names itself. This is why `DRIVERS.md` defines the power
callbacks before this document claims `S3` works.

Thermal: a passive trip point reduces the CPU rate of the offending job rather
than the whole machine; a critical trip point initiates `S5` after an audit
record and a dump if one is configured.

## 7. Caching

| Range | Policy | Set by |
|---|---|---|
| RAM | Write-back | `MTRR`/`PAT` at boot, identical on every processor |
| Framebuffer | Write-combining | The graphics driver, through the physical window |
| MMIO | Uncacheable | The driver, per mapping |
| Page tables | Write-back, never cached in the TLB as user-accessible | The VMM |

`PAT` is used when present, `MTRR` otherwise; the two are programmed from one
table so a processor started later gets the same view. A driver that maps MMIO
without naming its cache policy gets uncacheable, which is the safe direction.

## 8. CPU features

| Feature | Use | Refusal |
|---|---|---|
| PAE | Always (the address space needs it) | A CPU without PAE is not supported; the boot says so |
| NX | Per-page no-execute | A CPU without NX has W^X enforced by mapping only |
| FXSR / SSE / SSE2 | Per-task vector state ([ABI.md](ABI.md#7-floating-point-and-vector-state)) | **Required**: a part without all three is refused at boot |
| SMEP / SMAP | Keep ring 0 from executing or reading user pages | Where absent, user-copy goes through an explicit path |
| Invariant TSC | Fast time | Falls back to HPET |
| APIC / x2APIC | Interrupt delivery | xAPIC only; x2APIC's 32-bit register interface is not used |
| MTRR / PAT | Cache policy | Uncacheable everywhere, which is slower and correct |
| CPUID leaf 7 | Feature bits | Absence means "not offered" |

### 8.1 The CPU floor is a Pentium 4

SSE2 is not optional here. Every task image is a 512-byte `fxsave`
([ABI.md](ABI.md#7-floating-point-and-vector-state)), the scheduler writes it on
every switch, and the kernel's C is compiled for the Pentium 4. A processor
without FXSR, SSE and SSE2 therefore cannot run this kernel, and
`cpu_require_pentium4` stops the boot with one line naming what it found:

```
CPU_UNSUPPORTED: Mellivora needs a Pentium 4 or later (fxsr+sse+sse2); found family 4 model 8 - halting
```

The alternative — a degraded save path chosen at run time — was the old
behaviour and it is gone: two formats that must agree is a class of bug, not a
feature. Conformance (§11) covers it: booting the `i486` machine profile must
produce exactly that line and stay halted.

What is *not* required is Netburst itself being present: the check is on the
feature bits and a family of 15 or above, so a newer processor that still
reports a compatible CPUID runs, and `p4-noapic` (a Pentium 4 with the local
APIC removed from CPUID) is a supported machine.

### 8.2 The emulated machine

The machine is defined once, in `tools/machine_profiles.py`, and every tool
reads it from there: the `Makefile`, `tests/test_boot.sh`,
`tests/test_runtime.sh`, `tests/test_desktop.py`, `tools/qemu_drive.py`,
`tools/crash_inject.py`, `tools/gdb_kernel.sh` and the installers. Shell callers
source `tools/machine_profile.sh`.

| Profile | Machine | Use |
|---|---|---|
| `p4` (default) | Pentium 4 (CPUID family 15, model 2, stepping 7, brand `Intel(R) Pentium(R) 4 CPU 2.40GHz`), SSE2, 512 MB, i440FX, AC'97, USB 1.1, RTL8139 | Everything, unless a case says otherwise |
| `p4-noapic` | The same part with `APIC` removed from CPUID | The 8259 interrupt path |
| `i486` | 486, 128 MB, SB16 | Legacy: must be **refused** by the boot |

`MV_PROFILE` selects one; `make run` and `make check` agree because they read
the same file.

## 9. Error reporting

| Event | Handling |
|---|---|
| Machine check (`#MC`) | IST stack, record the bank, mark the frame bad, kill the task if the error is recoverable and confined, panic otherwise |
| NMI | Watchdog ping, or a panic with the symbolised EIP of the interrupted context |
| PCIe AER | Logged to the event log when the `MCFG`/ECAM is usable ([DRIVERS.md](DRIVERS.md)) |
| Thermal critical | Audit record, `S5` |
| Firmware bug | Reported once, by table and offset, and never worked around silently |

## 10. Hardware support policy

A device is in scope only if a test can exercise it
([D8](../ROADMAP.md#architectural-decisions)). The QEMU-first list, extended for
the waves that need them:

| Bus / device | Wave | Test |
|---|---|---|
| 8259, local APIC, I/O APIC, HPET, PIT | M2 | `acpi`, `locks`, soak |
| PCI and PCIe ECAM | M2 | `pci` |
| ATA PIO, ATA DMA, AHCI, virtio-blk | M3 | storage suite |
| e1000, rtl8139, virtio-net | M7 | net suite |
| AC'97, SB16, virtio-sound | M8 | media suite |
| UHCI/OHCI/EHCI, virtio-input | M8 | USB suite |
| VBE/BGA framebuffer | M6 | desktop suite |
| PS/2 keyboard and pointer | as built | input cases |

Real-hardware certification stays out of scope; a machine that is not QEMU is
supported to the extent it describes itself correctly.

## 11. Conformance

M2 does not close until:

1. `acpi` reports the processor count, APIC addresses, I/O APICs, interrupt
   controller in use, clock of record and resolution — and reports them
   correctly on `p4-noapic` (no local APIC), on a 2-processor `p4` and on a
   4-processor `p4`.
2. Four processors run the mixed-load soak for 24 hours with no panic and no
   lost wakeup.
3. Sleep and soft-off work from the shell and from the desktop; a driver that
   refuses sleep names itself and cancels the transition.
4. The interrupt storm detector fires on an injected storm and re-targets the
   line without losing an interrupt.
5. A `_PRT` refusal and a bad `RSDP` checksum are both reported and do not stop
   the boot.
6. Every processor started reports the same cache policy and the same clock
   frequency as the bootstrap processor.

## 12. As built (the gap)

| Contract | Tree today |
|---|---|
| 64 processors scheduling | APs start and park; 8 slots |
| MSI/MSI-X, per-vector affinity | INTx through the I/O APIC only |
| `_PRT`, `_CRS`, `_Sx`, thermal, battery | `RSDT`/`MADT` inventory only |
| Sleep, soft-off | The `apic`/`acpi` inventory exists; power states are not programmed |
| HPET as clock of record | APIC timer calibrated against the PIT, `tick=lapic` printed |
| NX, SMEP, SMAP | NX and SMEP/SMAP enabled where offered |
| Machine-check and NMI handling | Fault handler only |
| Cache policy per mapping | Not programmable |
| CPU floor (FXSR+SSE+SSE2, family 15) | Enforced at boot; `i486` profile refused with `CPU_UNSUPPORTED` |
| One machine definition for build, tests and installers | `tools/machine_profiles.py` (`p4`, `p4-noapic`, `i486`) |

The ACPI inventory, the APIC selection, the AP start-up, the lock-rank gate and
the calibrated APIC timer are real and tested; the table above is what they grow
into.
