---
name: Bug report
about: Report a bug or unexpected behavior
title: "[BUG] "
labels: bug
assignees: ""
---

## Description

<!-- Clearly describe the bug. What did you expect to happen? What actually happened? -->

## Steps to reproduce

<!-- Provide exact steps to reproduce the issue: -->

1. <!-- Step 1 -->
2. <!-- Step 2 -->
3. <!-- Step 3 -->

## Expected behavior

<!-- What should happen? -->

## Actual behavior

<!-- What actually happens? Include error messages, crashes, or corrupted output. -->

## Environment

- **Host OS**: <!-- e.g., Linux (Ubuntu 22.04), macOS 13, Windows WSL2 -->
- **QEMU version**: <!-- e.g., 6.2, 7.0, from `qemu-system-x86_64 --version` -->
- **NASM version**: <!-- e.g., 2.15.05, from `nasm -version` -->
- **Build type**: <!-- e.g., `make full`, `make run`, specific program -->
- **Mellivora commit**: <!-- e.g., `522a78a` or "main branch" -->

## To Reproduce (Assembly or shell example)

<!-- If possible, provide a minimal assembly snippet or shell command that triggers the bug. -->

```nasm
; Example assembly code that causes the issue
mov rax, SYS_READ
int 0x80
```

Or shell command:

```bash
# Example command sequence
./mellivora
# > some command that causes the bug
```

## Relevant logs or output

<!-- Include terminal output, error messages, or screenshot. Use code blocks for readability. -->

```
error: page fault at address 0x00001234
kernel panic: unhandled exception
```

## Additional context

<!-- Add any other context about the problem here (e.g., recent commit that broke this, related to a specific feature). -->

---

## Security Note

<!-- If this is a security-related bug, please report it privately instead: -->

**If this is a security issue**, please do NOT open a public issue. Instead, use GitHub's private vulnerability reporting:
→ [Report a vulnerability](https://github.com/James-HoneyBadger/Mellivora_OS/security/advisories/new)
