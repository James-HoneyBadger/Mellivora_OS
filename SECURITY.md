# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| Latest published release | :white_check_mark: |
| Older releases | :x: |

The latest published release and `main` receive security fixes. Older releases
are unsupported.

## Scope

Mellivora OS is an **educational operating system**, not a hardened production
system. Ring 3 execution, per-process page directories, and syscall validation
do not imply a complete multi-user security model. The normative target model
and its as-built status are documented separately in
[docs/spec/SECURITY.md](docs/spec/SECURITY.md) and
[docs/ROADMAP.md](docs/ROADMAP.md). Security concerns include:

- **Buffer overflows** in kernel or shell code that could corrupt memory or escalate privilege (Ring 3 → Ring 0)
- **Network stack vulnerabilities** — malformed packets, integer overflows in protocol parsers, or boundary errors in the socket API
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

See [CHANGELOG.md](CHANGELOG.md) for the versioned history of security and
reliability fixes.
