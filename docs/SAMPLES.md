# Mellivora Sample Programs

The `samples/` directory contains 37 teaching examples: 2 NASM programs, 18 C
programs, 11 Perl scripts, and 6 BASIC programs. `make full` copies the source
files into the image under `/samples`; the examples are not assembled into the
normal `/bin` or `/games` program sets.

## Complete Catalog

### NASM (2)

| Sample | Focus | Validation / execution |
| --- | --- | --- |
| `hello_burrows.asm` | Burrows window lifecycle, drawing, events, sleep, Escape handling | `nasm -f bin -Iprograms/ -o hello_burrows.bin samples/hello_burrows.asm`, then install and run in QEMU |
| `notify.asm` | `SYS_NOTIFY`, console output, colors, and clean exit | `nasm -f bin -Iprograms/ -o notify.bin samples/notify.asm`, then install and run in QEMU |

### C and TCC (18)

| Sample | Focus | Runtime |
| --- | --- | --- |
| `bf.c` | Brainfuck interpreter | `tcc /samples/bf.c` |
| `boxes.c` | Console drawing and input | `tcc /samples/boxes.c` |
| `calc.c` | Input parsing and arithmetic | `tcc /samples/calc.c` |
| `echo.c` | Standard input and output | `tcc /samples/echo.c` |
| `echo_server.c` | TCP server and sockets | `tcc /samples/echo_server.c` |
| `fib.c` | Recursion and integer sequences | `tcc /samples/fib.c` |
| `hanoi.c` | Recursion and formatted output | `tcc /samples/hanoi.c` |
| `hello.c` | Minimal output and `putchar` | `tcc /samples/hello.c` |
| `json_parse.c` | Structured text parsing | `tcc /samples/json_parse.c` |
| `mandelbrot_fp.c` | Fixed-point rendering | `tcc /samples/mandelbrot_fp.c` |
| `matrix.c` | Matrix construction and traversal | `tcc /samples/matrix.c` |
| `matrix_mul.c` | Matrix multiplication | `tcc /samples/matrix_mul.c` |
| `primes.c` | Prime-number generation | `tcc /samples/primes.c` |
| `sha256.c` | Hash computation | `tcc /samples/sha256.c` |
| `sort.c` | Arrays and sorting | `tcc /samples/sort.c` |
| `stars.c` | Console patterns and loops | `tcc /samples/stars.c` |
| `word_stats.c` | Character classification and statistics | `tcc /samples/word_stats.c` |
| `wumpus.c` | Interactive text adventure | `tcc /samples/wumpus.c` |

Run these commands from the Mellivora shell after `make full` and `make run`.
Host C syntax checks are useful but do not replace a TCC build inside the OS.

### Perl (11)

| Sample | Focus | Runtime |
| --- | --- | --- |
| `arrays.pl` | Arrays and iteration | `perl /samples/arrays.pl` |
| `csv.pl` | CSV parsing | `perl /samples/csv.pl` |
| `csv_totals.pl` | Validation, hashes, sorting, aggregation | `perl /samples/csv_totals.pl` |
| `factorial.pl` | Recursion and arithmetic | `perl /samples/factorial.pl` |
| `fizzbuzz.pl` | Branching and modulo arithmetic | `perl /samples/fizzbuzz.pl` |
| `guess.pl` | Interactive input and conditionals | `perl /samples/guess.pl` |
| `hello.pl` | Minimal Perl output | `perl /samples/hello.pl` |
| `http_get.pl` | HTTP request handling | `perl /samples/http_get.pl` |
| `prime_report.pl` | Subroutines, lists, and primality testing | `perl /samples/prime_report.pl` |
| `regex_demo.pl` | Regular expressions | `perl /samples/regex_demo.pl` |
| `strings.pl` | String operations | `perl /samples/strings.pl` |

The same scripts can be syntax-checked on the host with `perl -c samples/name.pl`.

### BASIC (6)

| Sample | Focus | Runtime |
| --- | --- | --- |
| `blackjack.bas` | Interactive game logic | `basic /samples/blackjack.bas` |
| `fib.bas` | Iteration and numeric sequences | `basic /samples/fib.bas` |
| `hello.bas` | Minimal `PRINT` program | `basic /samples/hello.bas` |
| `mandelbrot.bas` | Animated rendering | `basic /samples/mandelbrot.bas` |
| `snake.bas` | Interactive keyboard game | `basic /samples/snake.bas` |
| `temperature.bas` | Variables, arithmetic, branches, and `GOTO` | `basic /samples/temperature.bas` |

## Learning Path

1. Start with `hello.c`, `hello.bas`, or `hello.pl`.
2. Read `word_stats.c` for functions, arrays, and loops.
3. Read `notify.asm` for direct syscall usage.
4. Read `hello_burrows.asm` for the GUI lifecycle.
5. Compare `prime_report.pl` and `csv_totals.pl` for two data-processing styles.

## Automated Checks

Run the complete source-level sample check with:

```text
make check-samples
```

It assembles NASM samples, checks Perl syntax, validates BASIC line structure,
and checks C syntax. The host C compiler may warn or reject older examples because
Mellivora's bundled TCC accepts a deliberately different subset. Runtime execution
of C, BASIC, and NASM samples requires QEMU and the VGA keyboard; there is no
headless shell automation path yet. Perl samples can also be run directly on the host.
