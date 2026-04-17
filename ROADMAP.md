# Mellivora OS Roadmap

This document outlines the planned evolution of Mellivora OS across upcoming versions. Items are prioritized by category and expected completion timeline.

> **Note**: This is a living document. Timelines and priorities may shift based on community feedback and architecture discoveries.

---

## v4.1.0 - Scheduled Q2 2026

### High Priority

- **Performance Optimization**
  - [ ] SIMD (SSE/AVX) support in FPU context switching
  - [ ] Optimize paging: replace page-by-page walk with TLB awareness
  - [ ] Syscall batching: allow multiple syscalls in single INT 0x80
  - Estimated impact: 10-20% syscall throughput improvement

- **Network Stack Hardening**
  - [ ] TCP congestion control (Reno or Cubic)
  - [ ] IPv4 options handling (SACK, window scaling)
  - [ ] UDP/TCP port binding improvements
  - [ ] SYN flood mitigation
  - Tests: RFC compliance suite, stress tests with 1000+ connections

- **Filesystem Resilience**
  - [ ] HBFS journal logging (atomic writes for metadata)
  - [ ] Crash recovery mode (fsck improvements)
  - [ ] Sparse file support (for large disk images)
  - [ ] Hard link support (beyond symlinks)

### Medium Priority

- **Shell Enhancements**
  - [ ] Job control: `fg`, `bg`, `jobs` commands
  - [ ] Command history persistence (`.bash_history` style)
  - [ ] Alias definitions (`alias` command)
  - [ ] Function definitions (shell functions)

- **Program Library Additions**
  - [ ] `awk` implementation (text processing)
  - [ ] `sed` improvements (extended regex)
  - [ ] `jq`-like JSON processor
  - [ ] `cron` scheduler improvements (user crontabs)

### Low Priority

- **Development Tools**
  - [ ] Debugger (gdb-like remote debugging over serial)
  - [ ] Profiler (syscall/CPU time sampling)
  - [ ] Coverage tools for test suite

---

## v4.2.0 - Scheduled Q3 2026

### Multimedia & Graphics

- **GPU Support**
  - [ ] Basic VESA BIOS extensions (VBE 3.0)
  - [ ] Framebuffer scaling/rotation
  - [ ] Hardware cursor acceleration (if VBE supports)
  - [ ] PNG/JPEG image format support

- **Audio Enhancements**
  - [ ] Improved Sound Blaster 16 driver (CD-DA support)
  - [ ] MIDI sequencer and player
  - [ ] OGG Vorbis decoder

### Burrows Desktop Improvements

- **Window Manager Enhancements**
  - [ ] Snap-to-grid window alignment
  - [ ] Workspace switching (virtual desktops)
  - [ ] Compositing improvements (alpha blending, shadows)
  - [ ] Theme editor UI

- **Built-in Applications**
  - [ ] Calendar with event scheduling
  - [ ] Mail client with IMAP support
  - [ ] Remote file browser (SFTP/FTP)

---

## v5.0.0 - Scheduled Q4 2026 (Major Release)

### Kernel Architecture Overhaul

- **Memory Management Advances**
  - [ ] Migrate to 4-level page table per-process (vs. global identity mapping)
  - [ ] Virtual address space randomization (ASLR) for security
  - [ ] Copy-on-write (CoW) for fork() efficiency
  - [ ] Huge pages (2 MB/1 GB pages) support
  - Breaking change: User programs will see `0x0` base, not `0x200000`

- **IPC Expansion**
  - [ ] Unix domain sockets
  - [ ] Semaphores (POSIX-style)
  - [ ] Condition variables
  - [ ] RWLocks (reader-writer locks)

- **Privilege Separation Hardening**
  - [ ] Capabilities system (per-task permissions, not just Ring 3)
  - [ ] Mandatory access control (MAC) for files
  - [ ] Audit logging (syscall arguments and return values)

### Compiler & Runtime

- **TCC Enhancement**
  - [ ] C99 support (designated initializers, variadic macros)
  - [ ] Floating-point library improvements
  - [ ] Inline assembly support
  - [ ] Link-time optimization (LTO)

- **New Scripting Language**
  - [ ] Lua interpreter (lightweight and fast)
  - [ ] Integration with GUI widgets

### Networking Expansion

- **IPv6 Support**
  - [ ] IPv6 addressing and routing
  - [ ] DHCPv6 client
  - [ ] Dual-stack applications

- **TLS/SSL Support**
  - [ ] OpenSSL integration (minimal profile)
  - [ ] HTTPS support in Forager browser
  - [ ] Secure mail client

---

## Future Directions (Post-v5.0)

### Storage

- **Filesystem Alternatives**
  - [ ] Ext4-compatible filesystem (with journaling)
  - [ ] LVM-style volume management
  - [ ] RAID-1 mirroring for redundancy

- **Database Engine**
  - [ ] SQLite integration
  - [ ] In-kernel B-tree indexes

### Virtualization & Containers

- [ ] Lightweight container system (similar to chroot + namespaces)
- [ ] Virtual machine support (nested virtualization for QEMU)

### Advanced Networking

- [ ] VPN client/server
- [ ] DLNA/UPnP support for media streaming
- [ ] Bluetooth stack (if hardware available)

### Mobile & Embedded

- [ ] ARM64 port (Raspberry Pi, QEMU ARM emulation)
- [ ] RISC-V port (educational systems, VirtIO drivers)
- [ ] Device tree format support (modern hardware description)

---

## Community Priorities

The roadmap is flexible and driven by community interest. If you have a strong use case or contribution offer for an unlisted feature, please:

1. Open a discussion in GitHub Issues
2. Propose priority changes with use-case reasoning
3. Offer to implement (with mentor support)

---

## Testing & Stability

Every release includes:

- 1600+ regression tests (existing + new)
- QEMU compatibility verification (6.0+)
- Real hardware testing on Core 2 Duo+ (periodic)
- Fuzzing of syscall boundaries and network stack
- Memory leak detection (valgrind under QEMU)

---

## Known Limitations & Non-Goals

These are unlikely to be addressed:

- **32-bit x86 support**: Architecture is committed to x86-64 only
- **Windows .exe compatibility**: No PE loader; native NASM only
- **Real-time guarantees**: Mellivora is best-effort, not hard-RTOS
- **Hyper-scale networking**: Designed for workstation/hobby use, not data centers
- **Package management**: No standard package manager (keep it minimal)
- **Linux/POSIX API compatibility**: We're building a different OS, not a Linux clone

---

## Versions: Release Frequency & Support

- **Release Cadence**: Major release every 6-12 months; point releases as needed for security
- **Long-term Support (LTS)**: Designated every other major release; supported for 2 years
- **Current LTS**: v4.0.x (2024-2026)

---

## Contributing to the Roadmap

Want to help? Start here:

1. Pick an item and open a GitHub discussion titled "RFC: [Feature]"
2. Gather feedback from maintainers and community
3. If accepted, file an issue and reference the RFC
4. Follow [CONTRIBUTING.md](CONTRIBUTING.md) to open a PR

Questions? Reach out via:
- GitHub Issues
- Discussions tab (for design questions)
- Email: See [SECURITY.md](SECURITY.md) for contact info

---

*Last updated: April 2026*
