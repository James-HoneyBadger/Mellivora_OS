# Mellivora OS Style Guide

**Version:** 1.1 (v13.0.0) **Status:** Authoritative

This document is the single source of truth for cross-program conventions in
Mellivora OS user-space programs. Anything new added to `programs/` MUST
follow this guide. Existing programs should be migrated to this guide
incrementally as the v13.x codebase evolves.

For kernel conventions, see [TECHNICAL_REFERENCE.md](TECHNICAL_REFERENCE.md).
For library API details, see [API_REFERENCE.md](API_REFERENCE.md).

---

## 1. Program Skeleton

Every user-space program is a **flat binary** loaded at `0x00200000` and
starts execution at offset 0 with a jump to a labeled entry point.

### 1.1 Header

```nasm
; programname.asm - One-line description.
; <Genre>: <controls summary>.   <One-sentence usage hint.>
%include "syscalls.inc"
%include "lib/vbe_game.inc"        ; VBE games only
%include "lib/font.inc"            ; VBE games only
%include "lib/vbe_ui.inc"          ; if using shared UI widgets

start:
        ...
```

**Do NOT** include `[BITS 32]` or `[ORG ...]` directives in user programs;
the build pipeline handles them. Programs use the standard load address
`0x00200000` implicitly.

### 1.2 The flat-binary BSS rule

NASM `section .bss` in `-f bin` mode places `.bss` labels **past the end of
the on-disk binary** in the program's load image. The Mellivora kernel
loader does NOT zero memory past the binary — it just `rep movsd`'s file
bytes into the program area. Therefore:

* **`section .bss` with `resd`/`resb` is allowed**, but the labels start
  with **whatever was previously in memory** (junk from the prior program,
  or zeros if the area is fresh).
* **If a variable is read before it is written, you MUST initialize it
  explicitly** — use `dd 0` / `times N db 0` in the data section instead
  of `resd`/`resb` in `.bss`.
* **When in doubt, prefer `dd 0` / `times N db 0`.** It guarantees zero
  initial values and adds at most a few hundred bytes to the binary.

Safe (write-before-read pattern):

```nasm
section .bss
fb_addr:    resd 1                  ; OK — written by VBE_GAME_INIT before any read
```

Unsafe (read-before-write pattern):

```nasm
section .bss
score:      resd 1                  ; BUG: a `inc dword [score]` reads junk first
```

Always-safe alternative:

```nasm
fb_addr:    dd 0                    ; guaranteed zero at startup
score:      dd 0
```

### 1.3 Standard exit pattern

Every VBE program (or any program that switches video mode) **must** restore
text mode before exiting:

```nasm
.quit:
        mov eax, SYS_FRAMEBUF
        mov ebx, 2                  ; 2 = restore text mode
        int 0x80
        xor eax, eax                ; SYS_EXIT
        int 0x80
```

CLI programs that didn't change the video mode just need:

```nasm
.exit:
        xor eax, eax
        int 0x80
```

---

## 2. Calling Conventions

### 2.1 Register usage

| Reg | Role |
|-----|------|
| `EAX` | Syscall number on input; return value on output |
| `EBX, ECX, EDX, ESI, EDI` | Argument registers and secondary returns |
| `EBP` | Free for use; not implicitly preserved |
| `ESP` | Stack pointer (don't desync) |

### 2.2 Function preservation rule

**All shared-library functions in `programs/lib/*.inc` MUST preserve every
register via `pushad` / `popad`** unless they are intentionally returning
multiple values. Exceptions must be documented in the function header
comment.

```nasm
my_helper:
        pushad
        ; ... do work ...
        popad
        ret
```

If a helper returns a value via `EAX`, it should still preserve everything
else; stash the result before `popad` and reload after:

```nasm
get_count:
        pushad
        ; ... compute, leaving result in EAX ...
        mov  [.tmp], eax
        popad
        mov  eax, [.tmp]
        ret
.tmp:   dd 0
```

### 2.3 Parameter ordering

Unless overridden by an existing established API, parameters go in this
order: `EBX, ECX, EDX, ESI, EDI`. Pointers prefer `ESI` (source) and `EDI`
(destination); coordinates use `EBX=x, ECX=y`; sizes use `EDX=w, ESI=h`;
colors go in the last register (often `EDI`).

This matches `vbe_fill_rect EBX=x ECX=y EDX=w ESI=h EDI=color` and the
established `lib/vbe.inc` style.

---

## 3. Syscall Conventions

| Property | Rule |
|----------|------|
| Invocation | `mov eax, SYS_*` then `int 0x80` |
| Numbers | See [`programs/syscalls.inc`](../programs/syscalls.inc); never hard-code |
| `SYS_BEEP` | **Zeroes EAX after returning** — always reload `EAX` before the next `int 0x80` in a sequence |
| `SYS_READ_KEY` (4) | Non-blocking; returns 0 if no key |
| `SYS_GETCHAR` (2) | Blocking |
| `SYS_FRAMEBUF` (37) | sub 1=set mode, sub 2=restore text, sub 4=present |

---

## 4. VBE Game Conventions

### 4.1 Required includes

```nasm
%include "syscalls.inc"
%include "lib/vbe_game.inc"   ; VBE_GAME_INIT, VBE_GAME_POLL_KEY, VBE_GAME_PRESENT, KEY_*
%include "lib/font.inc"       ; vbe_draw_str / vbe_draw_num / vbe_draw_char / vbe_fill_circle
%include "lib/vbe_ui.inc"     ; vbe_ui_header_bar, vbe_ui_status_bar, vbe_ui_modal, vbe_ui_input_line
%include "lib/palette.inc"    ; (auto-included by vbe_ui.inc) MV_* color constants
```

### 4.2 Standard color palette

Use constants from [`programs/lib/palette.inc`](../programs/lib/palette.inc).
Do **not** hard-code hex literals like `0x00111111` — define a per-program
alias if needed:

```nasm
COL_BG    equ MV_BG_DARK          ; 0x00121212 (standard background)
COL_TEXT  equ MV_FG_BRIGHT        ; 0x00EEEEEE (standard text)
```

Key palette tones: `MV_BG_DARK`, `MV_BG_BAND`, `MV_FG_BRIGHT`, `MV_FG_DIM`,
`MV_ACCENT_YELLOW`, `MV_STATUS_OK`, `MV_STATUS_ERR`, `MV_CURSOR`. See
`palette.inc` for the full list.

### 4.3 Standard layout zones (1024 × 768)

| Zone | Y range | Notes |
|------|---------|-------|
| Header band | `0..22` | `vbe_ui_header_bar` (title left, status right) |
| Play area | `30..720` | Game-specific; center board horizontally |
| Status bar | `750..768` | `vbe_ui_status_bar` (key hints, dim text) |

### 4.4 Standard key bindings

These bindings are **mandatory** unless a game has a deliberate, documented
reason to override them.

| Key | Action |
|-----|--------|
| `Q` and `KEY_ESC` | Quit (both must work) |
| Arrow keys | Primary movement / cursor navigation |
| `W A S D` | Secondary movement (where arrows make sense) |
| `H J K L` | Tertiary (rogue-likes only) |
| `KEY_ENTER` | Confirm / select / place |
| `KEY_SPACE` | Action / shoot / fire |
| `R` | Restart current level / new game |
| `P` | Pause toggle (where applicable) |
| `?` or `H` | Help overlay |
| `N` | New game (where distinct from restart) |

Always handle key input **case-insensitively** for letter commands:

```nasm
        cmp al, 'q'
        je  .quit
        cmp al, 'Q'
        je  .quit
        cmp al, KEY_ESC
        je  .quit
```

### 4.5 Standard main loop

```nasm
start:
        VBE_GAME_INIT
        call init_state
        call draw_all

.main_loop:
        VBE_GAME_POLL_KEY
        cmp eax, -1
        je  .no_key

        ; Universal quit
        cmp al, 'q'
        je  .quit
        cmp al, 'Q'
        je  .quit
        cmp al, KEY_ESC
        je  .quit

        ; ... game-specific keys ...

.no_key:
        ; Frame pacing (~10 ms = 1 tick @ 100 Hz)
        mov eax, SYS_SLEEP
        mov ebx, 1
        int 0x80
        jmp .main_loop

.quit:
        mov eax, SYS_FRAMEBUF
        mov ebx, 2
        int 0x80
        xor eax, eax
        int 0x80
```

### 4.6 Strings: uppercase only

The 5×7 bitmap font in `lib/font.inc` only supports glyphs `0x20..0x5F`
(printable ASCII without lowercase letters). All strings drawn via
`vbe_draw_str` / `vbe_draw_char` **must be uppercase**.

```nasm
str_title:   db "ROGUE - HELP", 0       ; OK
str_bad:     db "rogue - help", 0       ; renders as garbage
```

### 4.7 Frame presentation

Always call `VBE_GAME_PRESENT` once per frame after all drawing is done.
Drawing happens to a shadow buffer; `VBE_GAME_PRESENT` blits it to the
framebuffer and draws the soft mouse cursor.

---

## 5. CLI Program Conventions

CLI utilities (cat, ls, grep, etc.) do **not** use VBE.

### 5.1 Required includes

```nasm
%include "syscalls.inc"
%include "lib/io.inc"       ; if using io_print / io_println / file helpers
%include "lib/string.inc"   ; if doing string work
```

### 5.2 Output

Use `SYS_PRINT` for NUL-terminated text. Use `SYS_PUTCHAR` only for single
characters. Use `io_println` (auto-newline) when convenient.

### 5.3 Exit codes

`0` = success. Non-zero values follow Unix convention: `1` for general
error, `2` for usage error, `>2` for tool-specific failures.

### 5.4 Argument handling

```nasm
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        ; EAX = length; args_buf is NUL-terminated
```

---

## 6. Burrows GUI Conventions

Burrows apps use `lib/gui.inc` and run inside windows on the desktop
compositor.

### 6.1 Required includes

```nasm
%include "syscalls.inc"
%include "lib/gui.inc"
%include "lib/widgets.inc"   ; if using buttons/textboxes/etc.
```

### 6.2 Lifecycle

1. Create window with `gui_create_window`.
2. Event loop using `gui_poll_event`; handle close events.
3. Always call `gui_destroy_window` before `SYS_EXIT`.

### 6.3 Conventional window sizes

* Tiny dialog: 320 × 200
* Small tool: 480 × 360
* Standard app: 640 × 480 or 800 × 600
* Full-canvas (paint, browser): 800 × 600

Do not exceed 960 × 720 (leave room for taskbar).

---

## 7. Memory & File Safety

### 7.1 Buffer sizing

Always declare buffers at fixed maximum size and check input length before
writes:

```nasm
INPUT_MAX  equ 256
input_buf: times INPUT_MAX db 0
input_len: dd 0

; Before appending a byte:
mov ecx, [input_len]
cmp ecx, INPUT_MAX - 1
jge .full
mov [input_buf + ecx], al
inc dword [input_len]
.full:
```

### 7.2 File I/O error handling

Every `SYS_OPEN`, `SYS_FREAD`, `SYS_FWRITE`, etc. returns `-1` on failure.
Always check before continuing.

### 7.3 No `mem_free` after `mem_alloc` of zero-size

`mem_alloc(0)` is undefined; check sizes before allocating.

---

## 8. Code Quality Checklist

Before committing changes to a program, verify:

- [ ] Builds: `nasm -f bin -Iprograms/ -o /tmp/x.bin programs/<name>.asm`
- [ ] Variables read before being written are initialized via `dd 0` / `times N db 0` (not `resd`/`resb` in `.bss`)
- [ ] All VBE strings are uppercase
- [ ] Exit pattern restores text mode (VBE programs)
- [ ] `Q` and `ESC` both quit (VBE games)
- [ ] No hard-coded color hex literals (use `MV_*` constants)
- [ ] Loop counters that conflict with `vbe_*` calls live in memory, not registers
- [ ] `SYS_BEEP` followed by another syscall reloads `EAX` first
- [ ] All shared-lib calls preserve registers (verify if writing new ones)
- [ ] Game tested through one full play to win/lose/quit

---

## 9. Documentation Conventions

* Per-file header comment is required: one line summary + brief usage.
* Section headers use `;===...` for major sections, `;---...` for sub-sections.
* Function headers state inputs, outputs, and clobbers explicitly.
* Use 8-column tabs displayed as spaces (NASM tradition).
* Capitalize NASM directives and instructions in mixed style: `mov`, `EAX`.

---

## 10. References

- [API_REFERENCE.md](API_REFERENCE.md) — Library function signatures
- [PROGRAMMING_GUIDE.md](PROGRAMMING_GUIDE.md) — How-to guide
- [TECHNICAL_REFERENCE.md](TECHNICAL_REFERENCE.md) — Kernel internals
- [`programs/lib/palette.inc`](../programs/lib/palette.inc) — Color constants
- [`programs/lib/vbe_ui.inc`](../programs/lib/vbe_ui.inc) — UI widgets
- [`programs/syscalls.inc`](../programs/syscalls.inc) — Syscall numbers

---

*This style guide will evolve as the v13.x codebase progresses. Changes are
recorded in `CHANGELOG.md`.*
