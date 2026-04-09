# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 1.15.x  | :white_check_mark: |
| < 1.15  | :x:                |

Only the latest release on the `main` branch receives security fixes.

## Scope

Mellivora OS is a **bare-metal educational operating system** that runs directly on hardware (or QEMU). It has no networking stack, no user authentication, and no remote attack surface. Security concerns are limited to:

- **Buffer overflows** in kernel or shell code that could corrupt memory or escalate privilege (Ring 3 → Ring 0)
- **Filesystem integrity** issues in HBFS that could cause data loss or corruption
- **Build chain safety** — ensuring the Makefile, Python scripts, and tooling don't introduce vulnerabilities
- **Syscall boundary validation** — ensuring user-mode programs cannot pass invalid pointers or sizes to kernel syscalls

## Reporting a Vulnerability

If you discover a security issue, please report it responsibly:

1. **Do NOT open a public GitHub issue** for security vulnerabilities.
2. Use GitHub's private vulnerability reporting:
   [Report a vulnerability](https://github.com/James-HoneyBadger/Mellivora_OS/security/advisories/new)
3. Include:
   - Description of the vulnerability
   - Steps to reproduce (assembly snippet, shell command, or disk image)
   - Impact assessment (crash, memory corruption, privilege escalation, data loss)
   - Suggested fix if you have one

## Response Timeline

- **Acknowledgment**: Within 72 hours
- **Assessment**: Within 1 week
- **Fix release**: As soon as practical, typically within 2 weeks for confirmed issues

## Security Hardening History

The project actively hardens its codebase. Recent examples:

- **v1.15**: Fixed `build_cwd_path` buffer overflow, added bounded `copy_word_n`, fixed Ctrl+C redirection state leak, wrapped stdout redirection with `cli`/`sti` for interrupt safety
- **v1.12**: ATA retry wrappers with soft reset, HBFS error propagation with carry flag
- **v1.10**: Nested batch execution guard, superblock free_blocks consistency tracking
