# Contributing to Mellivora OS

Thank you for your interest in contributing to Mellivora OS! This document outlines guidelines and procedures for submitting contributions.

## Code of Conduct

Please read our [Code of Conduct](CODE_OF_CONDUCT.md) before participating. We are committed to providing a welcoming and inclusive environment for all contributors.

## Getting Started

### Prerequisites

- **NASM** 2.15+ — Netwide Assembler for x86-64 assembly
- **GNU Make** 4.0+ — Build orchestration
- **QEMU** 6.0+ — `qemu-system-x86_64` for testing
- **Python 3** 3.6+ — Filesystem population and utilities
- **git** — Version control

See [Installation Guide](docs/INSTALL.md) for detailed setup instructions.

### Building Locally

```bash
git clone https://github.com/James-HoneyBadger/Mellivora_OS.git
cd Mellivora_OS
make full        # Full build: bootloader + kernel + programs + filesystem
make run         # Launch in QEMU
make test        # Run test suite
```

### Project Structure

```
.
├── boot.asm              # MBR boot sector (512 bytes)
├── stage2.asm            # Stage 2 loader (16 KB) — paging, long mode setup
├── kernel.asm            # Kernel entry point and dispatcher
├── kernel/               # Kernel includes (architecture, drivers, syscalls)
├── programs/             # User-space programs (140 assemblies + libraries)
├── docs/                 # Documentation (guides, API reference, tutorials)
├── tests/                # Test suite
├── populate.py           # Filesystem population script
└── Makefile              # Build automation
```

## Types of Contributions

### Bug Reports

Found a bug? We appreciate detailed reports. Please:

1. **Check existing issues** to avoid duplicates
2. **Use the bug report template** when opening an issue
3. **Include**:
   - Clear description of the problem
   - Steps to reproduce (assembly snippet, shell command, QEMU flags)
   - Expected vs. actual behavior
   - Environment (host OS, QEMU version, NASM version)
   - Relevant logs or error messages

**Security bugs**: Please report privately via [GitHub Security Advisories](https://github.com/James-HoneyBadger/Mellivora_OS/security/advisories/new) — do not open public issues.

### Feature Requests

Have an idea? We'd love to hear it:

1. **Check existing issues** to see if it's already been discussed
2. **Use the feature request template** when opening an issue
3. **Describe**:
   - What problem does this solve?
   - Proposed implementation (high level OK)
   - Example usage or mockup
   - Why this should be in Mellivora OS?

### Code Changes

Want to fix a bug or implement a feature? Follow these steps:

#### 1. Fork and Branch

```bash
# Fork the repo on GitHub (click "Fork" button)
git clone https://github.com/YOUR_USERNAME/Mellivora_OS.git
cd Mellivora_OS
git checkout -b feature/your-feature-name
# or: git checkout -b fix/issue-number
```

#### 2. Make Changes

- **Assembly code**: Follow the existing style:
  - Use lowercase mnemonics: `mov`, `add`, `ret`
  - Comment non-obvious sections
  - Use meaningful labels (e.g., `sys_read` not `s1`)
  - Preserve alignment: 4-space indentation, wrap at ~100 chars
  
- **Include files** (kernel/):
  - Document syscall numbers and purposes
  - Add comments for state machines (e.g., TCP handshake)
  - Mark kernel data with origin and alignment
  
- **Python scripts**:
  - Follow PEP 8
  - Add docstrings to functions
  - Handle errors gracefully

- **Documentation**:
  - Use Markdown
  - Use clear, conversational tone
  - Include code examples where helpful
  - Update table of contents if adding sections

#### 3. Test Your Changes

```bash
# Build everything
make clean
make full

# Run the build test
make test

# Test interactively in QEMU
make run
# Inside QEMU: test your feature manually

# For kernel changes: verify no new errors
make all 2>&1 | grep -i error
```

#### 4. Commit with Clear Messages

```bash
# Commits should be atomic and well-messaged
git add file1.asm file2.inc
git commit -m "feat: Add support for new feature

- First implementation detail
- Second implementation detail
- Fixes #123 (if applicable)"
```

**Commit message style**:
- Use imperative mood: "add", "fix", "optimize" (not "added", "fixes", "optimizes")
- First line: type and summary (<50 chars)
  - Types: `feat`, `fix`, `docs`, `refactor`, `perf`, `test`, `chore`
- Blank line after summary
- Body: explain *why*, not *what* (the code shows what)
- Mention related issues: `Fixes #123` or `Related to #456`

#### 5. Push and Open a Pull Request

```bash
git push origin feature/your-feature-name
```

Then open a PR on GitHub:
- **Title**: Start with type: `feat: Add X`, `fix: Resolve Y`
- **Description**: 
  - Briefly describe what changed and why
  - Link related issues
  - Include testing steps if non-obvious
  - Highlight any breaking changes

#### 6. Address Review Feedback

- Maintainers will review your PR
- Make requested changes in new commits (don't force-push)
- Respond to code review comments
- Once approved, your PR will be merged

## Development Guidelines

### Assembly Code Quality

- **Comments**: Explain the *why*, not just *what*
  ```nasm
  ; Disable interrupts during context switch to prevent race condition
  ; between TCB updates and ISR references
  cli
  ```

- **Register usage**: Follow x86-64 calling conventions
  - `rax`, `rcx`, `rdx`, `rsi`, `rdi`: scratch/arg registers
  - `rbx`, `rbp`, `r12-r15`: preserved across calls
  - Use 32-bit operands when values fit (zero-extends to 64-bit)

- **Labels**: Use meaningful names
  ```nasm
  .sys_not_found:    ; Clear label → what state?
  .syscall_dispatch_end:  ; Better: describes location
  ```

- **Macros**: Keep them simple and well-documented
  ```nasm
  ; PUSHALL: Save all general-purpose registers
  ; Used in ISR stubs to preserve user context
  %macro PUSHALL 0
    push rax
    ; ... etc
  %endmacro
  ```

### Documentation

- Keep docs up-to-date with code changes
- API changes? Update [docs/API_REFERENCE.md](docs/API_REFERENCE.md)
- New syscall? Document in [docs/TECHNICAL_REFERENCE.md](docs/TECHNICAL_REFERENCE.md)
- User-facing feature? Add to [docs/USER_GUIDE.md](docs/USER_GUIDE.md)

### Testing

Before opening a PR:

```bash
# Full rebuild (catches assembly errors early)
make clean full

# Run test suite
make test

# Manual testing in QEMU
make run
# Try your feature thoroughly:
# - Expected happy path
# - Error conditions
# - Edge cases (full disk, OOM, network timeout, etc.)
```

### Performance Considerations

- **Kernel code**: Minimize interrupt time (ISRs, syscalls < 1 ms ideal)
- **Memory**: Be mindful of kernel stack (4 KB per task) and heap fragmentation
- **Network**: Avoid busy-waiting; use event-driven design where possible

## Licensing

By contributing, you agree that your code will be licensed under the [MIT License](LICENSE). All contributions must have a valid author and can include an optional copyright notice.

## Getting Help

- **Questions about the codebase?** Open a discussion or ask in an issue
- **Need architecture overview?** Start with [docs/TECHNICAL_REFERENCE.md](docs/TECHNICAL_REFERENCE.md)
- **Want to contribute but unsure what to work on?** Look for issues tagged `good-first-issue` or `help-wanted`

## Recognition

Contributors are recognized in:
- The [CHANGELOG.md](CHANGELOG.md) for major features/fixes
- GitHub contributor graph
- README.md (for substantial contributions)

Thank you for contributing to Mellivora OS! 🦡
