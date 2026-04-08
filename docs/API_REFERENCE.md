# Mellivora API Reference

Reusable assembly libraries for Mellivora OS application and systems development.  
All libraries live in `programs/lib/` and are included via NASM `%include` directives.

## Quick Start

```nasm
%include "syscalls.inc"         ; Required: syscall numbers, ORG, BITS
%include "lib/string.inc"       ; String manipulation + memory ops
%include "lib/io.inc"           ; Console I/O, file ops, argument parsing
%include "lib/math.inc"         ; Number parsing/formatting, arithmetic
%include "lib/vga.inc"          ; VGA text mode, cursor, color, UI drawing
%include "lib/mem.inc"          ; Heap allocation, pool/arena allocators
%include "lib/data.inc"         ; Stacks, queues, bitmaps, arrays

start:
        ; Your code here
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80
```

**Include order matters.** Always include `syscalls.inc` first. The `io.inc` library
depends on `string.inc` for `io_print_padded` and `io_print_centered`.

## Calling Convention

- **Arguments:** Passed in registers (ESI, EDI, EAX, EBX, ECX, EDX) as documented per function
- **Return values:** EAX (and sometimes ECX or carry flag)
- **Register preservation:** Functions preserve all registers except documented return values
- **Error signaling:** `-1` return or carry flag set, as documented per function

---

## string.inc — String Manipulation

### String Operations

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `str_len` | ESI=string | EAX=length | Get null-terminated string length |
| `str_copy` | ESI=src, EDI=dst | — | Copy string including null |
| `str_ncopy` | ESI=src, EDI=dst, ECX=max | — | Copy up to N chars, null-terminates |
| `str_cat` | ESI=src, EDI=dst | — | Append src to end of dst |
| `str_cmp` | ESI=str1, EDI=str2 | EAX: 0/neg/pos | Case-sensitive compare |
| `str_icmp` | ESI=str1, EDI=str2 | EAX: 0/neg/pos | Case-insensitive compare |
| `str_ncmp` | ESI=str1, EDI=str2, ECX=n | EAX: 0/neg/pos | Compare first N chars |

### String Search

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `str_chr` | ESI=string, AL=char | EAX=ptr or 0 | Find first occurrence of char |
| `str_rchr` | ESI=string, AL=char | EAX=ptr or 0 | Find last occurrence of char |
| `str_str` | ESI=haystack, EDI=needle | EAX=ptr or 0 | Find substring |
| `str_starts_with` | ESI=string, EDI=prefix | EAX=1/0 | Test if string starts with prefix |
| `str_ends_with` | ESI=string, EDI=suffix | EAX=1/0 | Test if string ends with suffix |

### String Transform

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `str_upper` | ESI=string | — | Convert to uppercase in-place |
| `str_lower` | ESI=string | — | Convert to lowercase in-place |
| `str_trim` | ESI=string | — | Trim leading + trailing whitespace |
| `str_ltrim` | ESI=string | — | Trim leading whitespace |
| `str_rtrim` | ESI=string | — | Trim trailing whitespace |
| `str_reverse` | ESI=string | — | Reverse string in-place |
| `str_replace_char` | ESI=string, AL=old, AH=new | — | Replace all occurrences of char |

### String Utilities

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `str_count_char` | ESI=string, AL=char | EAX=count | Count occurrences of char |
| `str_token` | ESI=string (first call), AL=delim | EAX=token ptr or 0 | strtok-style tokenizer |
| `str_split_line` | ESI=buffer, EDI=line_buf, ECX=max | EAX=new pos or 0 | Extract next line from buffer |

### Character Classification

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `str_to_upper_c` | AL=char | AL=upper | Convert char to uppercase |
| `str_to_lower_c` | AL=char | AL=lower | Convert char to lowercase |
| `str_is_alpha` | AL=char | EAX=1/0 | Is alphabetic? |
| `str_is_digit` | AL=char | EAX=1/0 | Is digit (0-9)? |
| `str_is_alnum` | AL=char | EAX=1/0 | Is alphanumeric? |
| `str_is_space` | AL=char | EAX=1/0 | Is whitespace? |

### Memory Operations

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `mem_copy` | ESI=src, EDI=dst, ECX=bytes | — | Copy memory (rep movsb) |
| `mem_set` | EDI=dst, AL=value, ECX=bytes | — | Fill memory (rep stosb) |
| `mem_cmp` | ESI=ptr1, EDI=ptr2, ECX=bytes | EAX: 0/neg/pos | Compare memory blocks |
| `mem_zero` | EDI=dst, ECX=bytes | — | Zero memory block |

---

## io.inc — Input/Output

### Console Input

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `io_read_line` | EDI=buffer, ECX=maxsize | EAX=chars read | Interactive line input with backspace/escape |
| `io_read_num` | ECX=max digits | EAX=number, CF=empty | Read and parse a decimal number |
| `io_read_key` | — | EAX=keycode or 0 | Non-blocking key check |

### Console Output

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `io_print` | ESI=string | — | Print null-terminated string |
| `io_println` | ESI=string | — | Print string + newline |
| `io_putchar` | AL=char | — | Output single character |
| `io_newline` | — | — | Output newline (LF) |
| `io_print_repeat` | AL=char, ECX=count | — | Print char N times |
| `io_clear` | — | — | Clear the screen |
| `io_print_padded` | ESI=str, ECX=width, AL=pad, AH=align | — | Print padded (AH: 0=left, 1=right) |
| `io_print_centered` | ESI=string, ECX=row | — | Print string centered on 80-col screen |

### Arguments

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `io_get_args` | EDI=buffer(256B) | EAX=length | Get raw command-line argument string |
| `io_parse_args` | ESI=argstr, EDI=argv[], ECX=max | EAX=argc | Parse args into pointer array (modifies string) |

### File Operations

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `io_file_read` | ESI=filename, EDI=buffer | EAX=bytes or -1 | Read entire file into buffer |
| `io_file_write` | ESI=filename, EDI=buf, ECX=size, EDX=type | EAX=0/-1 | Write buffer to file |
| `io_file_exists` | ESI=filename | EAX=1/0 | Check if file exists |
| `io_file_size` | ESI=filename | EAX=size or -1 | Get file size in bytes |
| `io_file_delete` | ESI=filename | EAX=0/-1 | Delete a file |

### Directory Operations

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `io_dir_read` | EDI=namebuf, ECX=index | EAX=type, ECX=size | Read directory entry by index |
| `io_dir_create` | ESI=dirname | EAX=0/-1 | Create a directory |
| `io_dir_change` | ESI=path | EAX=0/-1 | Change current directory |
| `io_dir_getcwd` | EDI=buffer | EAX=0 | Get current working directory |

### System

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `io_beep` | EBX=freq(Hz), ECX=duration | — | Play a tone |
| `io_sleep` | EBX=ticks (100 = 1s) | — | Sleep for N ticks |
| `io_get_time` | — | EAX=ticks | Get system tick count since boot |

---

## math.inc — Math and Number Formatting

### Number Parsing

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `math_parse_int` | ESI=string | EAX=value, ECX=digits | Parse unsigned decimal |
| `math_parse_signed` | ESI=string | EAX=value, ECX=chars | Parse signed decimal (handles `-`/`+`) |
| `math_parse_hex` | ESI=string | EAX=value, ECX=digits | Parse hex (optional `0x` prefix) |

### Number Formatting

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `math_int_to_str` | EAX=value, EDI=buffer | ECX=length | Convert unsigned int to decimal string |
| `math_hex_to_str` | EAX=value, EDI=buffer, ECX=mindigits | — | Convert to hex string (uppercase) |
| `math_bin_to_str` | EAX=value, EDI=buffer, ECX=bits | — | Convert to binary string |

### Arithmetic

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `math_abs` | EAX=signed | EAX=abs | Absolute value |
| `math_min` | EAX, EBX | EAX=min | Minimum (unsigned) |
| `math_max` | EAX, EBX | EAX=max | Maximum (unsigned) |
| `math_clamp` | EAX=val, EBX=min, ECX=max | EAX=clamped | Clamp to range |
| `math_sign` | EAX=signed | EAX=-1/0/1 | Sign of value |
| `math_div_round` | EAX=dividend, EBX=divisor | EAX=rounded | Divide with rounding |
| `math_mul_safe` | EAX, EBX | EAX=product, CF=overflow | Multiply with overflow check |
| `math_power` | EAX=base, ECX=exp | EAX=result | Integer exponentiation |
| `math_gcd` | EAX=a, EBX=b | EAX=gcd | Greatest common divisor |
| `math_sqrt` | EAX=value | EAX=floor(sqrt) | Integer square root |
| `math_log2` | EAX=value | EAX=floor(log2) | Integer log base 2 |
| `math_digits` | EAX=value | EAX=count | Count decimal digits |

### Random Numbers

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `math_seed_random` | EAX=seed | — | Seed the PRNG (use `io_get_time`) |
| `math_random` | — | EAX=0..32767 | Generate pseudo-random number |
| `math_random_range` | EAX=min, EBX=max | EAX=random | Random in [min, max] |

---

## vga.inc — VGA Text Mode Graphics

### Constants

**Box Drawing (CP437):**
`BOX_H` (`─`), `BOX_V` (`│`), `BOX_TL` (`┌`), `BOX_TR` (`┐`), `BOX_BL` (`└`), `BOX_BR` (`┘`),
`BOX_DH` (`═`), `BOX_DV` (`║`), `BOX_DTL` (`╔`), `BOX_DTR` (`╗`), `BOX_DBL` (`╚`), `BOX_DBR` (`╝`),
`BOX_FULL` (`█`), `BOX_HALF` (`▌`), `BOX_SHADE_L` (`░`), `BOX_SHADE_M` (`▒`), `BOX_SHADE_H` (`▓`)

**Colors (0-15):**
`VGA_BLACK`, `VGA_BLUE`, `VGA_GREEN`, `VGA_CYAN`, `VGA_RED`, `VGA_MAGENTA`, `VGA_BROWN`, `VGA_LGRAY`,
`VGA_DGRAY`, `VGA_LBLUE`, `VGA_LGREEN`, `VGA_LCYAN`, `VGA_LRED`, `VGA_LMAGENTA`, `VGA_YELLOW`, `VGA_WHITE`

### Cursor and Color

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `vga_set_cursor` | EBX=col, ECX=row | — | Set cursor position |
| `vga_set_color` | BL=color | — | Set text color attribute |
| `vga_make_color` | AL=fg, AH=bg | AL=attr | Create color from fg/bg values |
| `vga_clear` | — | — | Clear the screen |

### Direct VGA Access

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `vga_put_char_at` | AL=char, AH=color, EBX=col, ECX=row | — | Write char+color at position |
| `vga_get_char_at` | EBX=col, ECX=row | AL=char, AH=color | Read char+color from position |
| `vga_write_at` | ESI=str, EBX=col, ECX=row | — | Write string at position |
| `vga_write_color` | ESI=str, EBX=col, ECX=row, DL=color | — | Write string with color at position |

### Drawing Primitives

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `vga_draw_hline` | EBX=col, ECX=row, EDX=len, AL=char, AH=color | — | Horizontal line |
| `vga_draw_vline` | EBX=col, ECX=row, EDX=len, AL=char, AH=color | — | Vertical line |
| `vga_draw_box` | EBX=left, ECX=top, EDX=width, ESI=height, AH=color | — | Single-line border box |
| `vga_draw_filled` | EBX=left, ECX=top, EDX=width, ESI=height, AL=char, AH=color | — | Filled rectangle |
| `vga_clear_region` | EBX=left, ECX=top, EDX=width, ESI=height | — | Clear a region |

### UI Elements

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `vga_status_bar` | ESI=text, ECX=row, DL=color | — | Full-width colored status bar |
| `vga_progress_bar` | EBX=col, ECX=row, EDX=width, ESI=current, EDI=max, AH=color | — | Progress bar |
| `vga_scroll_region` | EBX=left, ECX=top, EDX=width, ESI=height, AH=color | — | Scroll region up 1 line |

---

## mem.inc — Memory Management

### Heap Allocation

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `mem_alloc` | EAX=size (bytes) | EAX=ptr or 0 | Allocate memory (4KB page granularity) |
| `mem_free` | EAX=ptr, ECX=size | — | Free allocated memory |
| `mem_realloc` | EAX=ptr, EBX=oldsize, ECX=newsize | EAX=newptr or 0 | Resize allocation |

### Pool Allocator (Fixed-Size Objects)

For many small allocations of the same size. Uses a free-list internally.

```nasm
section .bss
pool_hdr:   resb POOL_HDR_SIZE     ; 20 bytes

section .text
        ; Initialize pool: 64 objects of 32 bytes each
        mov edi, pool_hdr
        mov eax, 32             ; object size
        mov ecx, 64             ; capacity
        call mem_pool_init

        ; Allocate an object
        mov edi, pool_hdr
        call mem_pool_alloc     ; EAX = ptr

        ; Free it back
        mov edi, pool_hdr
        call mem_pool_free      ; EAX = ptr to free
```

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `mem_pool_init` | EDI=header, EAX=objsize, ECX=count | EAX=0/-1 | Initialize pool |
| `mem_pool_alloc` | EDI=header | EAX=ptr or 0 | Allocate one object |
| `mem_pool_free` | EDI=header, EAX=ptr | — | Return object to pool |
| `mem_pool_reset` | EDI=header | — | Free all objects |

### Arena Allocator (Bump Pointer)

Fast sequential allocation with bulk free. Ideal for per-frame or per-request data.

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `mem_arena_init` | EDI=header, EAX=size | EAX=0/-1 | Initialize arena |
| `mem_arena_alloc` | EDI=header, EAX=size | EAX=ptr or 0 | Allocate (4-byte aligned) |
| `mem_arena_reset` | EDI=header | — | Free all arena memory at once |

---

## data.inc — Data Structures

### Stack (LIFO)

```nasm
section .bss
stk_hdr:    resb STK_HDR_SIZE      ; 12 bytes
stk_data:   resd 256               ; 256 dwords max

section .text
        mov edi, stk_hdr
        mov esi, stk_data
        mov ecx, 256
        call ds_stack_init

        mov eax, 42
        call ds_stack_push      ; CF clear = success

        call ds_stack_pop       ; EAX = 42, CF clear = success
```

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `ds_stack_init` | EDI=hdr, ESI=data, ECX=capacity | — | Initialize stack |
| `ds_stack_push` | EDI=hdr, EAX=value | CF=full | Push dword |
| `ds_stack_pop` | EDI=hdr | EAX=value, CF=empty | Pop dword |
| `ds_stack_peek` | EDI=hdr | EAX=value, CF=empty | Peek top |
| `ds_stack_empty` | EDI=hdr | EAX=1/0 | Check if empty |
| `ds_stack_count` | EDI=hdr | EAX=count | Get item count |

### Queue (FIFO, Circular)

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `ds_queue_init` | EDI=hdr, ESI=data, ECX=capacity | — | Initialize queue |
| `ds_queue_push` | EDI=hdr, EAX=value | CF=full | Enqueue dword |
| `ds_queue_pop` | EDI=hdr | EAX=value, CF=empty | Dequeue dword |
| `ds_queue_peek` | EDI=hdr | EAX=value, CF=empty | Peek front |
| `ds_queue_empty` | EDI=hdr | EAX=1/0 | Check if empty |
| `ds_queue_full` | EDI=hdr | EAX=1/0 | Check if full |
| `ds_queue_count` | EDI=hdr | EAX=count | Get item count |

### Bitmap

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `ds_bmap_set` | ESI=bitmap, EAX=index | — | Set bit |
| `ds_bmap_clear` | ESI=bitmap, EAX=index | — | Clear bit |
| `ds_bmap_test` | ESI=bitmap, EAX=index | EAX=1/0 | Test bit |
| `ds_bmap_find_free` | ESI=bitmap, ECX=total_bits | EAX=index or -1 | Find first clear bit |
| `ds_bmap_count_set` | ESI=bitmap, ECX=total_bits | EAX=count | Count set bits |

### Array Utilities

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `ds_sort_insert` | ESI=array, ECX=count | — | Insertion sort (unsigned dwords) |
| `ds_binary_search` | ESI=sorted_array, ECX=count, EAX=value | EAX=index/-1, CF | Binary search |
| `ds_array_swap` | ESI=array, EAX=idx1, EBX=idx2 | — | Swap two elements |
| `ds_array_reverse` | ESI=array, ECX=count | — | Reverse array in place |
| `ds_array_min` | ESI=array, ECX=count | EAX=min, EBX=index | Find minimum |
| `ds_array_max` | ESI=array, ECX=count | EAX=max, EBX=index | Find maximum |
| `ds_array_sum` | ESI=array, ECX=count | EAX=sum | Sum all elements |

---

## Example: File Reader Utility

```nasm
%include "syscalls.inc"
%include "lib/string.inc"
%include "lib/io.inc"

start:
        ; Get filename from command line
        mov edi, arg_buf
        call io_get_args
        test eax, eax
        jz .no_args

        ; Read the file
        mov esi, arg_buf
        mov edi, file_buf
        call io_file_read
        cmp eax, -1
        je .not_found

        ; Print contents
        mov esi, file_buf
        call io_println
        jmp .exit

.no_args:
        mov esi, msg_usage
        call io_println
        jmp .exit

.not_found:
        mov esi, msg_notfound
        call io_println

.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

msg_usage:      db "Usage: reader <filename>", 0
msg_notfound:   db "Error: File not found", 0

section .bss
arg_buf:        resb 256
file_buf:       resb 65536
```
