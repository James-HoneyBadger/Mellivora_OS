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
%include "lib/net.inc"          ; TCP/UDP sockets, DNS, ICMP ping
%include "lib/gui.inc"          ; Burrows desktop GUI wrappers

start:
        ; Your code here
        mov rax, SYS_EXIT
        xor rbx, rbx
        int 0x80
```

**Include order matters.** Always include `syscalls.inc` first. The `io.inc` library
depends on `string.inc` for `io_print_padded` and `io_print_centered`.

## Calling Convention

- **Arguments:** Passed in registers (RSI, RDI, RAX, RBX, RCX, RDX) as documented per function
- **Return values:** RAX (and sometimes RCX or carry flag)
- **Register preservation:** Functions preserve all registers except documented return values
- **Error signaling:** `-1` return or carry flag set, as documented per function

### Error Handling Patterns

Library functions use two error patterns — check the function table for which one applies:

| Pattern | How to check | Used by |
| --------- | ------------- | --------- |
| **RAX = -1** | `cmp rax, -1` / `je error` | File I/O (`io_file_read`, `io_file_write`, `io_file_size`), number parsing (`str_to_int`, `str_to_hex`) |
| **RAX = 0** (null/false) | `test rax, rax` / `jz error` | Search functions (`str_chr`, `str_str`), `io_get_arg`, `mem_alloc` |
| **Carry flag** | `jc error` | Low-level operations (`mem_pool_alloc`) |

---

## string.inc — String Manipulation

### String Operations

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `str_len` | RSI=string | RAX=length | Get null-terminated string length |
| `str_copy` | RSI=src, RDI=dst | — | Copy string including null |
| `str_ncopy` | RSI=src, RDI=dst, RCX=max | — | Copy up to N chars, null-terminates |
| `str_cat` | RSI=src, RDI=dst | — | Append src to end of dst |
| `str_cmp` | RSI=str1, RDI=str2 | RAX: 0/neg/pos | Case-sensitive compare |
| `str_icmp` | RSI=str1, RDI=str2 | RAX: 0/neg/pos | Case-insensitive compare |
| `str_ncmp` | RSI=str1, RDI=str2, RCX=n | RAX: 0/neg/pos | Compare first N chars |

### String Search

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `str_chr` | RSI=string, AL=char | RAX=ptr or 0 | Find first occurrence of char |
| `str_rchr` | RSI=string, AL=char | RAX=ptr or 0 | Find last occurrence of char |
| `str_str` | RSI=haystack, RDI=needle | RAX=ptr or 0 | Find substring |
| `str_starts_with` | RSI=string, RDI=prefix | RAX=1/0 | Test if string starts with prefix |
| `str_ends_with` | RSI=string, RDI=suffix | RAX=1/0 | Test if string ends with suffix |

### String Transform

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `str_upper` | RSI=string | — | Convert to uppercase in-place |
| `str_lower` | RSI=string | — | Convert to lowercase in-place |
| `str_trim` | RSI=string | — | Trim leading + trailing whitespace |
| `str_ltrim` | RSI=string | — | Trim leading whitespace |
| `str_rtrim` | RSI=string | — | Trim trailing whitespace |
| `str_reverse` | RSI=string | — | Reverse string in-place |
| `str_replace_char` | RSI=string, AL=old, AH=new | — | Replace all occurrences of char |

### String Utilities

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `str_count_char` | RSI=string, AL=char | RAX=count | Count occurrences of char |
| `str_token` | RSI=string (first call), AL=delim | RAX=token ptr or 0 | strtok-style tokenizer |
| `str_split_line` | RSI=buffer, RDI=line_buf, RCX=max | RAX=new pos or 0 | Extract next line from buffer |

### Character Classification

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `str_to_upper_c` | AL=char | AL=upper | Convert char to uppercase |
| `str_to_lower_c` | AL=char | AL=lower | Convert char to lowercase |
| `str_is_alpha` | AL=char | RAX=1/0 | Is alphabetic? |
| `str_is_digit` | AL=char | RAX=1/0 | Is digit (0-9)? |
| `str_is_alnum` | AL=char | RAX=1/0 | Is alphanumeric? |
| `str_is_space` | AL=char | RAX=1/0 | Is whitespace? |

### Memory Operations

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `mem_copy` | RSI=src, RDI=dst, RCX=bytes | — | Copy memory (rep movsb) |
| `mem_set` | RDI=dst, AL=value, RCX=bytes | — | Fill memory (rep stosb) |
| `mem_cmp` | RSI=ptr1, RDI=ptr2, RCX=bytes | RAX: 0/neg/pos | Compare memory blocks |
| `mem_zero` | RDI=dst, RCX=bytes | — | Zero memory block |

---

## io.inc — Input/Output

### Console Input

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `io_read_line` | RDI=buffer, RCX=maxsize | RAX=chars read | Interactive line input with backspace/escape |
| `io_read_num` | RCX=max digits | RAX=number, CF=empty | Read and parse a decimal number |
| `io_read_key` | — | RAX=keycode or 0 | Non-blocking key check |

### Console Output

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `io_print` | RSI=string | — | Print null-terminated string |
| `io_println` | RSI=string | — | Print string + newline |
| `io_putchar` | AL=char | — | Output single character |
| `io_newline` | — | — | Output newline (LF) |
| `io_print_repeat` | AL=char, RCX=count | — | Print char N times |
| `io_clear` | — | — | Clear the screen |
| `io_print_padded` | RSI=str, RCX=width, AL=pad, AH=align | — | Print padded (AH: 0=left, 1=right) |
| `io_print_centered` | RSI=string, RCX=row | — | Print string centered on 80-col screen |

### Arguments

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `io_get_args` | RDI=buffer(256B) | RAX=length | Get raw command-line argument string |
| `io_parse_args` | RSI=argstr, RDI=argv[], RCX=max | RAX=argc | Parse args into pointer array (modifies string) |

### File Operations

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `io_file_read` | RSI=filename, RDI=buffer | RAX=bytes or -1 | Read entire file into buffer |
| `io_file_write` | RSI=filename, RDI=buf, RCX=size, RDX=type | RAX=0/-1 | Write buffer to file |
| `io_file_exists` | RSI=filename | RAX=1/0 | Check if file exists |
| `io_file_size` | RSI=filename | RAX=size or -1 | Get file size in bytes |
| `io_file_delete` | RSI=filename | RAX=0/-1 | Delete a file |

### Directory Operations

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `io_dir_read` | RDI=namebuf, RCX=index | RAX=type, RCX=size | Read directory entry by index |
| `io_dir_create` | RSI=dirname | RAX=0/-1 | Create a directory |
| `io_dir_change` | RSI=path | RAX=0/-1 | Change current directory |
| `io_dir_getcwd` | RDI=buffer | RAX=0 | Get current working directory |

### System

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `io_beep` | RBX=freq(Hz), RCX=duration | — | Play a tone |
| `io_sleep` | RBX=ticks (100 = 1s) | — | Sleep for N ticks |
| `io_get_time` | — | RAX=ticks | Get system tick count since boot |

---

## math.inc — Math and Number Formatting

### Number Parsing

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `math_parse_int` | RSI=string | RAX=value, RCX=digits | Parse unsigned decimal |
| `math_parse_signed` | RSI=string | RAX=value, RCX=chars | Parse signed decimal (handles `-`/`+`) |
| `math_parse_hex` | RSI=string | RAX=value, RCX=digits | Parse hex (optional `0x` prefix) |

### Number Formatting

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `math_int_to_str` | RAX=value, RDI=buffer | RCX=length | Convert unsigned int to decimal string |
| `math_hex_to_str` | RAX=value, RDI=buffer, RCX=mindigits | — | Convert to hex string (uppercase) |
| `math_bin_to_str` | RAX=value, RDI=buffer, RCX=bits | — | Convert to binary string |

### Arithmetic

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `math_abs` | RAX=signed | RAX=abs | Absolute value |
| `math_min` | RAX, RBX | RAX=min | Minimum (unsigned) |
| `math_max` | RAX, RBX | RAX=max | Maximum (unsigned) |
| `math_clamp` | RAX=val, RBX=min, RCX=max | RAX=clamped | Clamp to range |
| `math_sign` | RAX=signed | RAX=-1/0/1 | Sign of value |
| `math_div_round` | RAX=dividend, RBX=divisor | RAX=rounded | Divide with rounding |
| `math_mul_safe` | RAX, RBX | RAX=product, CF=overflow | Multiply with overflow check |
| `math_power` | RAX=base, RCX=exp | RAX=result | Integer exponentiation |
| `math_gcd` | RAX=a, RBX=b | RAX=gcd | Greatest common divisor |
| `math_sqrt` | RAX=value | RAX=floor(sqrt) | Integer square root |
| `math_log2` | RAX=value | RAX=floor(log2) | Integer log base 2 |
| `math_digits` | RAX=value | RAX=count | Count decimal digits |

### Random Numbers

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `math_seed_random` | RAX=seed | — | Seed the PRNG (use `io_get_time`) |
| `math_random` | — | RAX=0..32767 | Generate pseudo-random number |
| `math_random_range` | RAX=min, RBX=max | RAX=random | Random in [min, max] |

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
| ---------- | ------- | -------- | ------------- |
| `vga_set_cursor` | RBX=col, RCX=row | — | Set cursor position |
| `vga_set_color` | BL=color | — | Set text color attribute |
| `vga_make_color` | AL=fg, AH=bg | AL=attr | Create color from fg/bg values |
| `vga_clear` | — | — | Clear the screen |

### Direct VGA Access

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `vga_put_char_at` | AL=char, AH=color, RBX=col, RCX=row | — | Write char+color at position |
| `vga_get_char_at` | RBX=col, RCX=row | AL=char, AH=color | Read char+color from position |
| `vga_write_at` | RSI=str, RBX=col, RCX=row | — | Write string at position |
| `vga_write_color` | RSI=str, RBX=col, RCX=row, DL=color | — | Write string with color at position |

### Drawing Primitives

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `vga_draw_hline` | RBX=col, RCX=row, RDX=len, AL=char, AH=color | — | Horizontal line |
| `vga_draw_vline` | RBX=col, RCX=row, RDX=len, AL=char, AH=color | — | Vertical line |
| `vga_draw_box` | RBX=left, RCX=top, RDX=width, RSI=height, AH=color | — | Single-line border box |
| `vga_draw_filled` | RBX=left, RCX=top, RDX=width, RSI=height, AL=char, AH=color | — | Filled rectangle |
| `vga_clear_region` | RBX=left, RCX=top, RDX=width, RSI=height | — | Clear a region |

### UI Elements

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `vga_status_bar` | RSI=text, RCX=row, DL=color | — | Full-width colored status bar |
| `vga_progress_bar` | RBX=col, RCX=row, RDX=width, RSI=current, RDI=max, AH=color | — | Progress bar |
| `vga_scroll_region` | RBX=left, RCX=top, RDX=width, RSI=height, AH=color | — | Scroll region up 1 line |

---

## mem.inc — Memory Management

### Heap Allocation

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `mem_alloc` | RAX=size (bytes) | RAX=ptr or 0 | Allocate memory (4KB page granularity) |
| `mem_free` | RAX=ptr, RCX=size | — | Free allocated memory |
| `mem_realloc` | RAX=ptr, RBX=oldsize, RCX=newsize | RAX=newptr or 0 | Resize allocation |

### Pool Allocator (Fixed-Size Objects)

For many small allocations of the same size. Uses a free-list internally.

```nasm
section .bss
pool_hdr:   resb POOL_HDR_SIZE     ; 20 bytes

section .text
        ; Initialize pool: 64 objects of 32 bytes each
        mov rdi, pool_hdr
        mov rax, 32             ; object size
        mov rcx, 64             ; capacity
        call mem_pool_init

        ; Allocate an object
        mov rdi, pool_hdr
        call mem_pool_alloc     ; RAX = ptr

        ; Free it back
        mov rdi, pool_hdr
        call mem_pool_free      ; RAX = ptr to free
```

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `mem_pool_init` | RDI=header, RAX=objsize, RCX=count | RAX=0/-1 | Initialize pool |
| `mem_pool_alloc` | RDI=header | RAX=ptr or 0 | Allocate one object |
| `mem_pool_free` | RDI=header, RAX=ptr | — | Return object to pool |
| `mem_pool_reset` | RDI=header | — | Free all objects |

### Arena Allocator (Bump Pointer)

Fast sequential allocation with bulk free. Ideal for per-frame or per-request data.

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `mem_arena_init` | RDI=header, RAX=size | RAX=0/-1 | Initialize arena |
| `mem_arena_alloc` | RDI=header, RAX=size | RAX=ptr or 0 | Allocate (4-byte aligned) |
| `mem_arena_reset` | RDI=header | — | Free all arena memory at once |

---

## data.inc — Data Structures

### Stack (LIFO)

```nasm
section .bss
stk_hdr:    resb STK_HDR_SIZE      ; 12 bytes
stk_data:   resd 256               ; 256 dwords max

section .text
        mov rdi, stk_hdr
        mov rsi, stk_data
        mov rcx, 256
        call ds_stack_init

        mov rax, 42
        call ds_stack_push      ; CF clear = success

        call ds_stack_pop       ; RAX = 42, CF clear = success
```

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `ds_stack_init` | RDI=hdr, RSI=data, RCX=capacity | — | Initialize stack |
| `ds_stack_push` | RDI=hdr, RAX=value | CF=full | Push dword |
| `ds_stack_pop` | RDI=hdr | RAX=value, CF=empty | Pop dword |
| `ds_stack_peek` | RDI=hdr | RAX=value, CF=empty | Peek top |
| `ds_stack_empty` | RDI=hdr | RAX=1/0 | Check if empty |
| `ds_stack_count` | RDI=hdr | RAX=count | Get item count |

### Queue (FIFO, Circular)

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `ds_queue_init` | RDI=hdr, RSI=data, RCX=capacity | — | Initialize queue |
| `ds_queue_push` | RDI=hdr, RAX=value | CF=full | Enqueue dword |
| `ds_queue_pop` | RDI=hdr | RAX=value, CF=empty | Dequeue dword |
| `ds_queue_peek` | RDI=hdr | RAX=value, CF=empty | Peek front |
| `ds_queue_empty` | RDI=hdr | RAX=1/0 | Check if empty |
| `ds_queue_full` | RDI=hdr | RAX=1/0 | Check if full |
| `ds_queue_count` | RDI=hdr | RAX=count | Get item count |

### Bitmap

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `ds_bmap_set` | RSI=bitmap, RAX=index | — | Set bit |
| `ds_bmap_clear` | RSI=bitmap, RAX=index | — | Clear bit |
| `ds_bmap_test` | RSI=bitmap, RAX=index | RAX=1/0 | Test bit |
| `ds_bmap_find_free` | RSI=bitmap, RCX=total_bits | RAX=index or -1 | Find first clear bit |
| `ds_bmap_count_set` | RSI=bitmap, RCX=total_bits | RAX=count | Count set bits |

### Array Utilities

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `ds_sort_insert` | RSI=array, RCX=count | — | Insertion sort (unsigned dwords) |
| `ds_binary_search` | RSI=sorted_array, RCX=count, RAX=value | RAX=index/-1, CF | Binary search |
| `ds_array_swap` | RSI=array, RAX=idx1, RBX=idx2 | — | Swap two elements |
| `ds_array_reverse` | RSI=array, RCX=count | — | Reverse array in place |
| `ds_array_min` | RSI=array, RCX=count | RAX=min, RBX=index | Find minimum |
| `ds_array_max` | RSI=array, RCX=count | RAX=max, RBX=index | Find maximum |
| `ds_array_sum` | RSI=array, RCX=count | RAX=sum | Sum all elements |

---

## Example: File Reader Utility

```nasm
%include "syscalls.inc"
%include "lib/string.inc"
%include "lib/io.inc"

start:
        ; Get filename from command line
        mov rdi, arg_buf
        call io_get_args
        test rax, rax
        jz .no_args

        ; Read the file
        mov rsi, arg_buf
        mov rdi, file_buf
        call io_file_read
        cmp rax, -1
        je .not_found

        ; Print contents
        mov rsi, file_buf
        call io_println
        jmp .exit

.no_args:
        mov rsi, msg_usage
        call io_println
        jmp .exit

.not_found:
        mov rsi, msg_notfound
        call io_println

.exit:
        mov rax, SYS_EXIT
        xor rbx, rbx
        int 0x80

msg_usage:      db "Usage: reader <filename>", 0
msg_notfound:   db "Error: File not found", 0

section .bss
arg_buf:        resb 256
file_buf:       resb 65536
```

---

## net.inc — Networking

TCP/UDP socket operations, DNS resolution, and ICMP ping. Requires `syscalls.inc`.

### Constants

| Name | Value | Description |
| --- | --- | --- |
| `NET_TCP` | 1 | TCP socket type |
| `NET_UDP` | 2 | UDP socket type |

### Socket Operations

| Function | Input | Output | Description |
| --- | --- | --- | --- |
| `net_socket` | RAX=type (NET_TCP/NET_UDP) | RAX=fd (-1 error) | Create a socket |
| `net_connect` | RAX=fd, RBX=IP, RCX=port | RAX=0/-1 | Connect to remote host |
| `net_send` | RAX=fd, RBX=buffer, RCX=length | RAX=bytes sent (-1 error) | Send raw data |
| `net_recv` | RAX=fd, RBX=buffer, RCX=max | RAX=bytes (0=none, -1=closed) | Receive data |
| `net_close` | RAX=fd | — | Close socket |
| `net_bind` | RAX=fd, RBX=port | RAX=0/-1 | Bind to local port |
| `net_listen` | RAX=fd | RAX=0/-1 | Start listening for connections |
| `net_accept` | RAX=fd | RAX=new fd (-1 timeout) | Accept incoming connection |

### Line-Oriented I/O

| Function | Input | Output | Description |
| --- | --- | --- | --- |
| `net_send_line` | RAX=fd, RSI=string | — | Send null-terminated string + CRLF |
| `net_recv_line` | RAX=fd, RDI=buffer, RCX=max | RAX=bytes, RDI filled | Receive until LF, null-terminate |

### DNS & ICMP

| Function | Input | Output | Description |
| --- | --- | --- | --- |
| `net_dns` | RSI=hostname | RAX=IP (0=fail) | Resolve hostname to IP address |
| `net_ping` | RAX=IP address | RAX=RTT ticks (-1=timeout) | Send ICMP echo request |
| `net_parse_ip` | RSI=dotted IP string | RAX=IP binary (0=error) | Parse "1.2.3.4" to 32-bit IP |

### Example: Fetch a Web Page

```nasm
%include "syscalls.inc"
%include "lib/net.inc"

start:
        ; Resolve hostname
        mov rsi, host
        call net_dns
        test rax, rax
        jz .fail
        mov [ip], rax

        ; Open TCP socket and connect
        mov rax, NET_TCP
        call net_socket
        mov [fd], rax
        mov rax, [fd]
        mov rbx, [ip]
        mov rcx, 80
        call net_connect

        ; Send HTTP request
        mov rax, [fd]
        mov rsi, request
        call net_send_line
        mov rax, [fd]
        mov rsi, blank
        call net_send_line

        ; Receive and print response
.loop:  mov rax, [fd]
        mov rbx, buf
        mov rcx, 512
        call net_recv
        cmp rax, 0
        jle .done
        mov byte [buf + rax], 0
        mov rax, SYS_PRINT
        mov rbx, buf
        int 0x80
        jmp .loop

.done:  mov rax, [fd]
        call net_close
.fail:  mov rax, SYS_EXIT
        xor rbx, rbx
        int 0x80

host:    db "example.com", 0
request: db "GET / HTTP/1.0", 0
blank:   db "", 0

section .bss
ip:      resd 1
fd:      resd 1
buf:     resb 513
```

---

## gui.inc — Burrows Desktop GUI

Wrapper functions for the `SYS_GUI` syscall (38) sub-functions. Provides a clean
calling convention for creating and managing windows in the Burrows desktop environment.
Requires `syscalls.inc`.

**Coordinate packing:** Many SYS_GUI sub-functions use `hi16:lo16` packed registers.
The `gui.inc` wrappers handle this packing automatically — you pass x, y, w, h as
separate registers.

### Window Management

| Function | Input | Output | Description |
| --- | --- | --- | --- |
| `gui_create_window` | RAX=x, RBX=y, RCX=w, RDX=h, RSI=title | RAX=win_id (0–15, -1=error) | Create a new window |
| `gui_destroy_window` | RAX=win_id | — | Destroy a window |

### Drawing

| Function | Input | Output | Description |
| --- | --- | --- | --- |
| `gui_fill_rect` | RAX=win_id, RBX=x, RCX=y, RDX=w, RSI=h, RDI=color | — | Fill rectangle in window |
| `gui_draw_text` | RAX=win_id, RBX=x, RCX=y, RSI=text, RDI=color | — | Draw text in window |
| `gui_draw_pixel` | RAX=win_id, RBX=x, RCX=y, RSI=color | — | Plot single pixel |

### Events & Compositing

| Function | Input | Output | Description |
| --- | --- | --- | --- |
| `gui_poll_event` | — | RAX=event type, RBX=param1, RCX=param2 | Poll for GUI event |
| `gui_compose` | — | — | Compose desktop to back buffer |
| `gui_flip` | — | — | Draw cursor and flip to screen |

### Themes

| Function | Input | Output | Description |
| --- | --- | --- | --- |
| `gui_get_theme` | RAX=dest buffer (48 bytes) | — | Copy current theme data |
| `gui_set_theme` | RAX=source buffer (48 bytes) | — | Apply theme |

### Event Types

Returned in RAX by `gui_poll_event`:

| Constant | Value | Description |
| --- | --- | --- |
| `EVT_NONE` | 0 | No event pending |
| `EVT_MOUSE_CLICK` | 1 | Mouse button pressed |
| `EVT_MOUSE_MOVE` | 2 | Mouse position changed |
| `EVT_KEY_PRESS` | 3 | Keyboard key pressed |
| `EVT_CLOSE` | 4 | Window close requested |

### Example: Simple GUI Application

```nasm
%include "syscalls.inc"
%include "lib/gui.inc"

start:
        ; Create a window at (100, 80), 200x150
        mov rax, 100
        mov rbx, 80
        mov rcx, 200
        mov rdx, 150
        mov rsi, title
        call gui_create_window
        mov [win], rax

        ; Fill background
        mov rax, [win]
        xor rbx, rbx
        xor rcx, rcx
        mov rdx, 200
        mov rsi, 150
        mov rdi, 0x404060
        call gui_fill_rect

        ; Draw text
        mov rax, [win]
        mov rbx, 20
        mov rcx, 40
        mov rsi, message
        mov rdi, 0xFFFFFF
        call gui_draw_text

.loop:
        call gui_compose
        call gui_flip
        call gui_poll_event
        cmp rax, EVT_CLOSE
        jne .loop

        mov rax, [win]
        call gui_destroy_window
        mov rax, SYS_EXIT
        xor rbx, rbx
        int 0x80

title:   db "My App", 0
message: db "Hello, Burrows!", 0

section .bss
win:     resd 1
```
