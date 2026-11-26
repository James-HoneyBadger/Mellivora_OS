# Mellivora: Parallel Universe Workstation

Status: proposed engineering charter and gated roadmap, not a release announcement.
The existing normative specs remain authoritative until a tested replacement is
approved. The Windows 2000 parity scorecard becomes a regression/reference
baseline, not the product ceiling.

## Constitution
- BIOS boot, native 32-bit x86 kernel and user ABI; no mandatory 64-bit execution,
  host service, browser runtime or virtual device. PAE physical addressing is
  compatible with this rule. Native installation and recovery are release gates.
- Current feature floor: PAE, FXSR, SSE, SSE2 (Pentium 4-class); a CPU brand is
  not a capability. Any lower floor must be separately qualified, not promised.
- Local-first, no telemetry, advertising, mandatory cloud or remote accounts.
- Preserve third-party notices and legacy grants; GPL-3.0-only for new first-party
  development subject to dependency compatibility.
- Native identity inspired by 1993 workstations, not a Windows clone.
- No ABI or disk-format break without versioning, migration and rollback.

## Design lineage: Amiga-inspired, independently native

Mellivora is an imagined continuation of the creative workstation tradition,
not an Amiga emulator, clone, compatibility layer or port. Native x86 code,
protected address spaces, Mellivora's own ABI, HBFS and hardware drivers remain
the foundation. No Kickstart ROM, Amiga binaries, proprietary assets, chipset
emulation or Amiga API compatibility is required.

Borrow the principles: responsive multitasking, low overhead, approachable
native programming, scripting as a first-class facility, integrated multimedia,
personal customization and a desktop that invites experimentation.

Burrows should explore independently designed screen-like workspaces, a
configurable command dock, icon-based document launch with per-document
metadata, a visual file manager and keyboard-driven shell integration.
A native application scripting bus, inspired by the spirit of ARexx, should
use bounded typed messages, explicit capabilities and local authorization;
it must not expose arbitrary execution by default. Native logical volume
aliases may be explored over VFS without changing path security semantics.

Creative applications are first-class: a sample-based music tracker, pixel
art and palette editor, sprite and tile workshop, animation tools, MIDI where
hardware permits, and an integrated programming studio. Prefer software
rendering, dirty rectangles, fixed-point DSP where useful and optional
hardware acceleration. No particular legacy chipset is required.

Use original icons, fonts, sounds and visual assets with documented licenses.
Borrow the spirit of Workbench without recreating its trade dress. Prioritize
the feeling of immediacy and control over superficial retro decoration.
All of these are proposed features, subject to the release and hardware gates.

## Audit baseline
The current tree contains a 3-stage BIOS loader, PAE VM, native HBX/ELF32
executables, HBFS, object/handle manager, scheduler, services, registry-like
Sett, Chronicle and Burrows. Specifications contain both target and as-built
sections; capability claims need tests, not inference from symbol names.
Large assembly modules (Burrows, shell, HBFS, networking, scheduler, object
manager) are maintenance and review risks. Do not translate wholesale.
The existing CI fails at the v3 superblock reserved-byte assertion after the
volume label was introduced; fix the assertion to check the label and every
other reserved byte. Nightly failures need independent diagnosis.

## Wave 0: establish truth
Run full CI, debug-bounds and soak. Capture reproducible release image hashes,
toolchain versions, CPU and memory assumptions. Record test IDs and results
against each capability: implemented, emulated-tested, hardware-tested,
experimental, absent. Failures remain visible; no silent skips.
Test BIOS install to sacrificial disk, cold boot, shutdown, reboot, recovery,
no network, missing optional hardware, memory pressure, and corrupted volumes.
Keep stable main and release tags; develop here. Never write to a user's real
disk without explicit selection and confirmation.

## Wave 1: executive and security
Define typed cdecl boundaries and generated offsets for C/assembly. Keep boot,
interrupts, context switches, atomic primitives and register access in asm.
Move pure policy and parsers into separately host-testable freestanding C
modules with fixed-width fields, checked arithmetic and explicit lengths.
Start with syscall argument validation and object lifetime, then VFS path
resolution, scheduler policy, service configuration, network parsers and GUI
policy. Preserve old entry points via adapters until differential tests pass.
Audit user pointer ranges, page ownership, copy-on-write, handle rights,
reference counts, per-process quotas, signal and thread lifetime, cleanup
under partial allocation and interrupt reentrancy. Without hardware NX do not
claim true page-level execute protection. Restrict untrusted applications to
enforceable policies; disclose degraded CPU protection.
Gate: hostile guest test suite, allocation failure matrix, memory leak soak,
cross-session and cross-process negative access tests.

## Wave 2: storage and recovery
HBFS v4 first; no new format until power-loss, flush/barrier semantics and
recovery are proven. Explicit metadata transaction order; checksums, allocation
bounds, extent overlap and cycle checks; directory permissions and atomic
rename. A/B system generations and boot rollback; separate user data from
system upgrade. Offline read-only fsck by default, repair only by request,
backup/export before destructive repair. Removable devices safely mount/eject,
with read-only FAT interoperability before enabling writes.
Gate: deterministic cut-power-at-every-write tests, file-content hashes,
exhaustion tests, migration of old images, recovery on BIOS hardware.

## Wave 3: real machines
Baseline P4-class PC with IDE/ATA, PS/2, VGA/VBE and PIC/PIT fallback. Validate
AHCI, AC97/SB16, e1000/RTL8139, UHCI/OHCI/EHCI as separately qualified drivers,
not all assumed. Enumerate PCI across bridges; resource ownership, DMA bounds,
timeouts, removal and reset. Document real chipset/controller IDs and firmware
versions, and exact device qualification. Distinguish emulated CPU CPUID
branding from real CPU execution. Extend VM matrix with low memory, no APIC,
no optional devices, SATA and alternative NICs; optional later Pentium III
(non-SSE2), then 486 research profile only after alternate VM/context-save
design. Do not introduce a second public ABI.
Gate: at least one independently recorded physical 32-bit machine install
and boot before declaring bare-metal verified; broader support only with evidence.

## Wave 4: Burrows becomes a native user-space workstation
Retain thin kernel surface/event primitives and move window policy, launcher,
widgets, menus, clipboard, file dialogs and theme rendering into a restartable
user-mode desktop service. Define bounded versioned IPC with per-owner buffers,
focus/clipboard rights and damage tracking. Native retained widget tree,
keyboard navigation, scalable bitmap fonts, theme editor, workspaces and
document-centric launch. Make every action usable without mouse. Keep the
text console usable if graphics fail. Original visual identity: restrained
industrial chrome, warm paper, deep teal, high contrast, optional copper
accents; no copied vendor assets.
Gate: kill/restart desktop, malicious surface requests, window and input
isolation, screenshot and interaction regressions, measured memory and latency.

## Wave 5: native application and development ecosystem
Mellivora Studio: editor, project templates, assembler/C/BASIC/Forth, build
output, debugger, breakpoints, symbols, documentation and package creation.
Native SDK: versioned C headers, assembler includes, stable interfaces,
resource formats, application manifest, examples and conformance tests.
Daily-driver flagship suite: Forager file manager, document editor,
spreadsheet with verified arithmetic and CSV, drawing/paint, audio tracker,
terminal, system monitor and recovery tools. Time Warp Universe-inspired
planner, notes, calendar and time tracking with local export/import; share
data formats/logic when feasible, never require host services. Do not claim
the existing Time Warp app runs natively until a port passes tests.
Gate: create, build, debug, package, install and uninstall a native application
inside Mellivora, with persistent documents surviving reboot.

## Wave 6: networking and packages
Offline default; explicit user-enabled networking and services. Separate
network protocol parsing from kernel policy where practical; no plaintext
credentials, no unsupported TLS security claims, bound all protocol messages
and queues. Local package repository/import, manifest dependencies and hashes,
signed index/package verification when a vetted crypto implementation exists,
atomic installation and rollback. Old protocols isolated and labeled legacy.
Gate: malformed packet tests, no-network UX, failed package transaction recovery,
authenticated update and rollback tests.

## Performance budgets (provisional, not measurements)
Reference real P4 512 MiB and comparable VM; also test 128 MiB as an exploratory
low-memory profile. Target idle desktop <=64 MiB committed, shell <=24 MiB,
desktop interaction p95 <=50 ms, boot to usable shell <=10 seconds excluding
firmware POST, no monotonic leak during 8-hour soak. Rebaseline only with
recorded hardware and rationale. Measure before optimizing; publish raw
results and percentile methodology. Avoid pretending QEMU timing predicts
physical hardware. Targets are hypotheses until instrumented.

## Release policy
A feature counts when it works end-to-end, survives errors, has a documented
contract and passes tests. No new release from this branch until full CI,
storage recovery, executable ABI and install/boot matrix are green; record
physical testing or label it unverified. No compatibility promise from
emulator-only results. Keep release artifacts reproducible and recovery media
available. A smaller reliable workstation beats a giant unqualified demo.
