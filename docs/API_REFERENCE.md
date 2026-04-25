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

### Error Handling Patterns

Library functions use two error patterns — check the function table for which one applies:

| Pattern | How to check | Used by |
| --------- | ------------- | --------- |
| **EAX = -1** | `cmp eax, -1` / `je error` | File I/O (`io_file_read`, `io_file_write`, `io_file_size`), number parsing (`str_to_int`, `str_to_hex`) |
| **EAX = 0** (null/false) | `test eax, eax` / `jz error` | Search functions (`str_chr`, `str_str`), `io_get_arg`, `mem_alloc` |
| **Carry flag** | `jc error` | Low-level operations (`mem_pool_alloc`) |

---

## string.inc — String Manipulation

### String Operations

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `str_len` | ESI=string | EAX=length | Get null-terminated string length |
| `str_copy` | ESI=src, EDI=dst | — | Copy string including null |
| `str_ncopy` | ESI=src, EDI=dst, ECX=max | — | Copy up to N chars, null-terminates |
| `str_cat` | ESI=src, EDI=dst | — | Append src to end of dst |
| `str_cmp` | ESI=str1, EDI=str2 | EAX: 0/neg/pos | Case-sensitive compare |
| `str_icmp` | ESI=str1, EDI=str2 | EAX: 0/neg/pos | Case-insensitive compare |
| `str_ncmp` | ESI=str1, EDI=str2, ECX=n | EAX: 0/neg/pos | Compare first N chars |

### String Search

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `str_chr` | ESI=string, AL=char | EAX=ptr or 0 | Find first occurrence of char |
| `str_rchr` | ESI=string, AL=char | EAX=ptr or 0 | Find last occurrence of char |
| `str_str` | ESI=haystack, EDI=needle | EAX=ptr or 0 | Find substring |
| `str_starts_with` | ESI=string, EDI=prefix | EAX=1/0 | Test if string starts with prefix |
| `str_ends_with` | ESI=string, EDI=suffix | EAX=1/0 | Test if string ends with suffix |

### String Transform

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `str_upper` | ESI=string | — | Convert to uppercase in-place |
| `str_lower` | ESI=string | — | Convert to lowercase in-place |
| `str_trim` | ESI=string | — | Trim leading + trailing whitespace |
| `str_ltrim` | ESI=string | — | Trim leading whitespace |
| `str_rtrim` | ESI=string | — | Trim trailing whitespace |
| `str_reverse` | ESI=string | — | Reverse string in-place |
| `str_replace_char` | ESI=string, AL=old, AH=new | — | Replace all occurrences of char |

### String Utilities

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `str_count_char` | ESI=string, AL=char | EAX=count | Count occurrences of char |
| `str_token` | ESI=string (first call), AL=delim | EAX=token ptr or 0 | strtok-style tokenizer |
| `str_split_line` | ESI=buffer, EDI=line_buf, ECX=max | EAX=new pos or 0 | Extract next line from buffer |

### Character Classification

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `str_to_upper_c` | AL=char | AL=upper | Convert char to uppercase |
| `str_to_lower_c` | AL=char | AL=lower | Convert char to lowercase |
| `str_is_alpha` | AL=char | EAX=1/0 | Is alphabetic? |
| `str_is_digit` | AL=char | EAX=1/0 | Is digit (0-9)? |
| `str_is_alnum` | AL=char | EAX=1/0 | Is alphanumeric? |
| `str_is_space` | AL=char | EAX=1/0 | Is whitespace? |

### Memory Operations

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `mem_copy` | ESI=src, EDI=dst, ECX=bytes | — | Copy memory (rep movsb) |
| `mem_set` | EDI=dst, AL=value, ECX=bytes | — | Fill memory (rep stosb) |
| `mem_cmp` | ESI=ptr1, EDI=ptr2, ECX=bytes | EAX: 0/neg/pos | Compare memory blocks |
| `mem_zero` | EDI=dst, ECX=bytes | — | Zero memory block |

---

## io.inc — Input/Output

### Console Input

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `io_read_line` | EDI=buffer, ECX=maxsize | EAX=chars read | Interactive line input with backspace/escape |
| `io_read_num` | ECX=max digits | EAX=number, CF=empty | Read and parse a decimal number |
| `io_read_key` | — | EAX=keycode or 0 | Non-blocking key check |

### Console Output

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
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
| ---------- | ------- | -------- | ------------- |
| `io_get_args` | EDI=buffer(256B) | EAX=length | Get raw command-line argument string |
| `io_parse_args` | ESI=argstr, EDI=argv[], ECX=max | EAX=argc | Parse args into pointer array (modifies string) |

### File Operations

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `io_file_read` | ESI=filename, EDI=buffer | EAX=bytes or -1 | Read entire file into buffer |
| `io_file_write` | ESI=filename, EDI=buf, ECX=size, EDX=type | EAX=0/-1 | Write buffer to file |
| `io_file_exists` | ESI=filename | EAX=1/0 | Check if file exists |
| `io_file_size` | ESI=filename | EAX=size or -1 | Get file size in bytes |
| `io_file_delete` | ESI=filename | EAX=0/-1 | Delete a file |

### Directory Operations

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `io_dir_read` | EDI=namebuf, ECX=index | EAX=type, ECX=size | Read directory entry by index |
| `io_dir_create` | ESI=dirname | EAX=0/-1 | Create a directory |
| `io_dir_change` | ESI=path | EAX=0/-1 | Change current directory |
| `io_dir_getcwd` | EDI=buffer | EAX=0 | Get current working directory |

### System

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `io_beep` | EBX=freq(Hz), ECX=duration | — | Play a tone |
| `io_sleep` | EBX=ticks (100 = 1s) | — | Sleep for N ticks |
| `io_get_time` | — | EAX=ticks | Get system tick count since boot |

---

## math.inc — Math and Number Formatting

### Number Parsing

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `math_parse_int` | ESI=string | EAX=value, ECX=digits | Parse unsigned decimal |
| `math_parse_signed` | ESI=string | EAX=value, ECX=chars | Parse signed decimal (handles `-`/`+`) |
| `math_parse_hex` | ESI=string | EAX=value, ECX=digits | Parse hex (optional `0x` prefix) |

### Number Formatting

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `math_int_to_str` | EAX=value, EDI=buffer | ECX=length | Convert unsigned int to decimal string |
| `math_hex_to_str` | EAX=value, EDI=buffer, ECX=mindigits | — | Convert to hex string (uppercase) |
| `math_bin_to_str` | EAX=value, EDI=buffer, ECX=bits | — | Convert to binary string |

### Arithmetic

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
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
| ---------- | ------- | -------- | ------------- |
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
| ---------- | ------- | -------- | ------------- |
| `vga_set_cursor` | EBX=col, ECX=row | — | Set cursor position |
| `vga_set_color` | BL=color | — | Set text color attribute |
| `vga_make_color` | AL=fg, AH=bg | AL=attr | Create color from fg/bg values |
| `vga_clear` | — | — | Clear the screen |

### Direct VGA Access

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `vga_put_char_at` | AL=char, AH=color, EBX=col, ECX=row | — | Write char+color at position |
| `vga_get_char_at` | EBX=col, ECX=row | AL=char, AH=color | Read char+color from position |
| `vga_write_at` | ESI=str, EBX=col, ECX=row | — | Write string at position |
| `vga_write_color` | ESI=str, EBX=col, ECX=row, DL=color | — | Write string with color at position |

### Drawing Primitives

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `vga_draw_hline` | EBX=col, ECX=row, EDX=len, AL=char, AH=color | — | Horizontal line |
| `vga_draw_vline` | EBX=col, ECX=row, EDX=len, AL=char, AH=color | — | Vertical line |
| `vga_draw_box` | EBX=left, ECX=top, EDX=width, ESI=height, AH=color | — | Single-line border box |
| `vga_draw_filled` | EBX=left, ECX=top, EDX=width, ESI=height, AL=char, AH=color | — | Filled rectangle |
| `vga_clear_region` | EBX=left, ECX=top, EDX=width, ESI=height | — | Clear a region |

### UI Elements

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `vga_status_bar` | ESI=text, ECX=row, DL=color | — | Full-width colored status bar |
| `vga_progress_bar` | EBX=col, ECX=row, EDX=width, ESI=current, EDI=max, AH=color | — | Progress bar |
| `vga_scroll_region` | EBX=left, ECX=top, EDX=width, ESI=height, AH=color | — | Scroll region up 1 line |

---

## mem.inc — Memory Management

### Heap Allocation

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
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
| ---------- | ------- | -------- | ------------- |
| `mem_pool_init` | EDI=header, EAX=objsize, ECX=count | EAX=0/-1 | Initialize pool |
| `mem_pool_alloc` | EDI=header | EAX=ptr or 0 | Allocate one object |
| `mem_pool_free` | EDI=header, EAX=ptr | — | Return object to pool |
| `mem_pool_reset` | EDI=header | — | Free all objects |

### Arena Allocator (Bump Pointer)

Fast sequential allocation with bulk free. Ideal for per-frame or per-request data.

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
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
| ---------- | ------- | -------- | ------------- |
| `ds_stack_init` | EDI=hdr, ESI=data, ECX=capacity | — | Initialize stack |
| `ds_stack_push` | EDI=hdr, EAX=value | CF=full | Push dword |
| `ds_stack_pop` | EDI=hdr | EAX=value, CF=empty | Pop dword |
| `ds_stack_peek` | EDI=hdr | EAX=value, CF=empty | Peek top |
| `ds_stack_empty` | EDI=hdr | EAX=1/0 | Check if empty |
| `ds_stack_count` | EDI=hdr | EAX=count | Get item count |

### Queue (FIFO, Circular)

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `ds_queue_init` | EDI=hdr, ESI=data, ECX=capacity | — | Initialize queue |
| `ds_queue_push` | EDI=hdr, EAX=value | CF=full | Enqueue dword |
| `ds_queue_pop` | EDI=hdr | EAX=value, CF=empty | Dequeue dword |
| `ds_queue_peek` | EDI=hdr | EAX=value, CF=empty | Peek front |
| `ds_queue_empty` | EDI=hdr | EAX=1/0 | Check if empty |
| `ds_queue_full` | EDI=hdr | EAX=1/0 | Check if full |
| `ds_queue_count` | EDI=hdr | EAX=count | Get item count |

### Bitmap

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
| `ds_bmap_set` | ESI=bitmap, EAX=index | — | Set bit |
| `ds_bmap_clear` | ESI=bitmap, EAX=index | — | Clear bit |
| `ds_bmap_test` | ESI=bitmap, EAX=index | EAX=1/0 | Test bit |
| `ds_bmap_find_free` | ESI=bitmap, ECX=total_bits | EAX=index or -1 | Find first clear bit |
| `ds_bmap_count_set` | ESI=bitmap, ECX=total_bits | EAX=count | Count set bits |

### Array Utilities

| Function | Input | Output | Description |
| ---------- | ------- | -------- | ------------- |
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
| `net_socket` | EAX=type (NET_TCP/NET_UDP) | EAX=fd (-1 error) | Create a socket |
| `net_connect` | EAX=fd, EBX=IP, ECX=port | EAX=0/-1 | Connect to remote host |
| `net_send` | EAX=fd, EBX=buffer, ECX=length | EAX=bytes sent (-1 error) | Send raw data |
| `net_recv` | EAX=fd, EBX=buffer, ECX=max | EAX=bytes (0=none, -1=closed) | Receive data |
| `net_close` | EAX=fd | — | Close socket |
| `net_bind` | EAX=fd, EBX=port | EAX=0/-1 | Bind to local port |
| `net_listen` | EAX=fd | EAX=0/-1 | Start listening for connections |
| `net_accept` | EAX=fd | EAX=new fd (-1 timeout) | Accept incoming connection |

### Line-Oriented I/O

| Function | Input | Output | Description |
| --- | --- | --- | --- |
| `net_send_line` | EAX=fd, ESI=string | — | Send null-terminated string + CRLF |
| `net_recv_line` | EAX=fd, EDI=buffer, ECX=max | EAX=bytes, EDI filled | Receive until LF, null-terminate |

### DNS & ICMP

| Function | Input | Output | Description |
| --- | --- | --- | --- |
| `net_dns` | ESI=hostname | EAX=IP (0=fail) | Resolve hostname to IP address |
| `net_ping` | EAX=IP address | EAX=RTT ticks (-1=timeout) | Send ICMP echo request |
| `net_parse_ip` | ESI=dotted IP string | EAX=IP binary (0=error) | Parse "1.2.3.4" to 32-bit IP |

### Example: Fetch a Web Page

```nasm
%include "syscalls.inc"
%include "lib/net.inc"

start:
        ; Resolve hostname
        mov esi, host
        call net_dns
        test eax, eax
        jz .fail
        mov [ip], eax

        ; Open TCP socket and connect
        mov eax, NET_TCP
        call net_socket
        mov [fd], eax
        mov eax, [fd]
        mov ebx, [ip]
        mov ecx, 80
        call net_connect

        ; Send HTTP request
        mov eax, [fd]
        mov esi, request
        call net_send_line
        mov eax, [fd]
        mov esi, blank
        call net_send_line

        ; Receive and print response
.loop:  mov eax, [fd]
        mov ebx, buf
        mov ecx, 512
        call net_recv
        cmp eax, 0
        jle .done
        mov byte [buf + eax], 0
        mov eax, SYS_PRINT
        mov ebx, buf
        int 0x80
        jmp .loop

.done:  mov eax, [fd]
        call net_close
.fail:  mov eax, SYS_EXIT
        xor ebx, ebx
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
| `gui_create_window` | EAX=x, EBX=y, ECX=w, EDX=h, ESI=title | EAX=win_id (0–15, -1=error) | Create a new window |
| `gui_destroy_window` | EAX=win_id | — | Destroy a window |

### Drawing

| Function | Input | Output | Description |
| --- | --- | --- | --- |
| `gui_fill_rect` | EAX=win_id, EBX=x, ECX=y, EDX=w, ESI=h, EDI=color | — | Fill rectangle in window |
| `gui_draw_text` | EAX=win_id, EBX=x, ECX=y, ESI=text, EDI=color | — | Draw text in window |
| `gui_draw_pixel` | EAX=win_id, EBX=x, ECX=y, ESI=color | — | Plot single pixel |

### Events & Compositing

| Function | Input | Output | Description |
| --- | --- | --- | --- |
| `gui_poll_event` | — | EAX=event type, EBX=param1, ECX=param2 | Poll for GUI event |
| `gui_compose` | — | — | Compose desktop to back buffer |
| `gui_flip` | — | — | Draw cursor and flip to screen |

### Themes

| Function | Input | Output | Description |
| --- | --- | --- | --- |
| `gui_get_theme` | EAX=dest buffer (48 bytes) | — | Copy current theme data |
| `gui_set_theme` | EAX=source buffer (48 bytes) | — | Apply theme |

### Event Types

Returned in EAX by `gui_poll_event`:

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
        mov eax, 100
        mov ebx, 80
        mov ecx, 200
        mov edx, 150
        mov esi, title
        call gui_create_window
        mov [win], eax

        ; Fill background
        mov eax, [win]
        xor ebx, ebx
        xor ecx, ecx
        mov edx, 200
        mov esi, 150
        mov edi, 0x404060
        call gui_fill_rect

        ; Draw text
        mov eax, [win]
        mov ebx, 20
        mov ecx, 40
        mov esi, message
        mov edi, 0xFFFFFF
        call gui_draw_text

.loop:
        call gui_compose
        call gui_flip
        call gui_poll_event
        cmp eax, EVT_CLOSE
        jne .loop

        mov eax, [win]
        call gui_destroy_window
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

title:   db "My App", 0
message: db "Hello, Burrows!", 0

section .bss
win:     resd 1
```

---

## sprite.inc — VBE Sprite Drawing

Pixel-art sprite drawing routines for VBE framebuffer programs. Lives at
`programs/sprite.inc` (not inside `lib/`). Include after `syscalls.inc` in any
VBE program that has already called `SYS_FRAMEBUF/0` to obtain `fb_addr` and
`fb_pitch`.

```nasm
%include "syscalls.inc"
%include "sprite.inc"
```

The calling program must expose two dword variables in its BSS section that the
library reads directly:

```nasm
section .bss
fb_addr:  resd 1    ; base address of the shadow buffer (from SYS_FRAMEBUF/0)
fb_pitch: resd 1    ; bytes per row = width * bytes_per_pixel
```

### Sprite Format

Sprites are plain inline data — no external file needed:

```text
dd  width              ; sprite width in pixels
dd  height             ; sprite height in pixels
dd  pixel[0]           ; top-left pixel, 0xAARRGGBB
dd  pixel[1]           ; row-major order
...
dd  pixel[width*height-1]
```

**Alpha channel** (bits 24–31): `0x00xxxxxx` = fully transparent (pixel is skipped);
any non-zero alpha is treated as fully opaque.

### Macros

| Macro | Arguments | Description |
| --------- | ---------- | ------------- |
| `SPRITE_BEGIN` | `name, width, height` | Emit label + `dd width, height` header |
| `SPRITE_END` | — | No-op placeholder for readability |

```nasm
; Define a 4×4 white square with a transparent centre
SPRITE_BEGIN my_spr, 4, 4
  dd 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
  dd 0xFFFFFFFF, 0x00000000, 0x00000000, 0xFFFFFFFF
  dd 0xFFFFFFFF, 0x00000000, 0x00000000, 0xFFFFFFFF
  dd 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
SPRITE_END
```

### Drawing Routines

| Routine | Input | Description |
| ----------- | ------- | ------------- |
| `sprite_draw` | EBX=x, ECX=y, ESI=sprite_ptr | Draw with per-pixel alpha (alpha=0 → skip) |
| `sprite_draw_opaque` | EBX=x, ECX=y, ESI=sprite_ptr | Draw every pixel, ignore alpha (fastest) |
| `sprite_draw_key` | EBX=x, ECX=y, ESI=sprite_ptr, EDI=color_key | Color-key: skip pixels whose low 24 bits == color_key |
| `sprite_draw_scaled` | EBX=x, ECX=y, ESI=sprite_ptr, EDX=scale_shift | Nearest-neighbour scale by 2^scale_shift (1=2×, 2=4×) |

All routines preserve all registers except flags (via `pushad`/`popad`).

### Example: Sprite in a VBE Game Loop

```nasm
%include "syscalls.inc"
%include "sprite.inc"

SPRITE_BEGIN spr_ship, 8, 8
  dd 0x00000000, 0x00000000, 0xFF00FF00, 0x00000000, 0x00000000, 0x00000000, 0xFF00FF00, 0x00000000
  dd 0x00000000, 0xFF00FF00, 0xFF00FFFF, 0xFF00FF00, 0xFF00FF00, 0xFF00FF00, 0xFF00FFFF, 0xFF00FF00
  dd 0xFF00FF00, 0xFF00FFFF, 0xFFFFFFFF, 0xFF00FFFF, 0xFF00FFFF, 0xFFFFFFFF, 0xFF00FFFF, 0xFF00FF00
  dd 0xFF00FF00, 0xFF00FFFF, 0xFFFFFFFF, 0xFF00FFFF, 0xFF00FFFF, 0xFFFFFFFF, 0xFF00FFFF, 0xFF00FF00
  dd 0x00000000, 0xFF00FF00, 0xFF00FFFF, 0xFF00FF00, 0xFF00FF00, 0xFF00FFFF, 0xFF00FF00, 0x00000000
  dd 0x00000000, 0x00000000, 0xFF00FF00, 0x00000000, 0x00000000, 0xFF00FF00, 0x00000000, 0x00000000
  dd 0x00000000, 0x00000000, 0xFF808000, 0xFF808000, 0xFF808000, 0xFF808000, 0x00000000, 0x00000000
  dd 0x00000000, 0xFF808000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xFF808000, 0x00000000
SPRITE_END

start:
    ; Enter 640x480x32 mode
    mov eax, SYS_FRAMEBUF
    mov ebx, 1
    mov ecx, 640
    mov edx, 480
    mov esi, 32
    int 0x80

    ; Get shadow buffer address
    mov eax, SYS_FRAMEBUF
    xor ebx, ebx
    int 0x80
    mov [fb_addr], eax      ; shadow buffer pointer
    mov eax, ebx
    imul eax, 4             ; pitch = width * 4
    mov [fb_pitch], eax

game_loop:
    ; ... clear shadow buffer, update game state ...

    ; Draw ship at (x, y)
    mov ebx, [ship_x]
    mov ecx, [ship_y]
    mov esi, spr_ship
    call sprite_draw

    ; Present: blit shadow -> real LFB
    mov eax, SYS_FRAMEBUF
    mov ebx, 4
    int 0x80

    mov eax, SYS_SLEEP
    mov ebx, 2              ; ~20 ms frame cap
    int 0x80
    jmp game_loop

section .bss
fb_addr:  resd 1
fb_pitch: resd 1
ship_x:   resd 1
ship_y:   resd 1
```
