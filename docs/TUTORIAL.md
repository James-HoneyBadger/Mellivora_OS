# Mellivora OS — Tutorial

A step-by-step walkthrough for first-time users. By the end of this tutorial you will
know how to navigate the OS, manage files, run programs, play games, write your first
assembly program, compile C code, and automate tasks with scripts.

---

## Table of Contents

1. [First Boot](#first-boot)
2. [Exploring the Shell](#exploring-the-shell)
3. [Navigating Directories](#navigating-directories)
4. [Working with Files](#working-with-files)
5. [Running Programs](#running-programs)
6. [Playing Games](#playing-games)
7. [Using the Text Editor](#using-the-text-editor)
8. [Writing Your First Assembly Program](#writing-your-first-assembly-program)
9. [Writing and Compiling C Programs](#writing-and-compiling-c-programs)
10. [Batch Scripting](#batch-scripting)
11. [Customizing Your Environment](#customizing-your-environment)
12. [Next Steps](#next-steps)

---

## First Boot

### Building and Launching

If you haven't built the OS yet, run:

```bash
make full
```

This assembles the bootloader, kernel, and all programs, then creates the disk image.
Launch it with:

```bash
make run
```

A QEMU window opens. After a brief splash screen, you see the HB DOS banner and a
prompt:

```text
 ██╗  ██╗██████╗     ██████╗  ██████╗ ███████╗
 ██║  ██║██╔══██╗    ██╔══██╗██╔═══██╗██╔════╝
 ███████║██████╔╝    ██║  ██║██║   ██║███████╗
 ██╔══██║██╔══██╗    ██║  ██║██║   ██║╚════██║
 ██║  ██║██████╔╝    ██████╔╝╚██████╔╝███████║
 ╚═╝  ╚═╝╚═════╝     ╚═════╝  ╚═════╝ ╚══════╝

HBDOS:/>
```

You're in! The `HBDOS:/>` prompt shows your current directory (`/` = root).

### Getting Help

Type `help` and press Enter:

```text
HBDOS:/> help
```

This lists every built-in command with a brief description. Keep this handy as you
explore.

### System Information

```text
HBDOS:/> ver
```

Shows the OS version, detected hardware, and feature list.

---

## Exploring the Shell

### Your First Commands

Try these to get oriented:

```text
HBDOS:/> date          → Shows current date and time
HBDOS:/> time          → Shows uptime (seconds since boot)
HBDOS:/> mem           → Shows available memory
HBDOS:/> disk          → Shows disk size
HBDOS:/> df            → Shows filesystem usage
```

### Listing Files

```text
HBDOS:/> dir
```

You'll see the root directory contents — typically `bin`, `games`, `samples`, `docs`,
and `script.bat`.

For more detail:

```text
HBDOS:/> dir -l
```

This shows file types (DIR, EXEC, FILE, BATCH) and sizes.

---

## Navigating Directories

### Moving Around

```text
HBDOS:/> cd bin         → Enter the bin directory
HBDOS:/bin> dir         → List programs in /bin
HBDOS:/bin> cd ..       → Go back to root
HBDOS:/> cd games       → Enter the games directory
HBDOS:/games> pwd       → Print where you are: /games
HBDOS:/games> cd /      → Jump straight to root
```

### The PATH

Notice that even when you're in `/`, you can run programs from `/bin` and `/games`
without typing the full path:

```text
HBDOS:/> hello          → Runs /bin/hello
HBDOS:/> snake          → Runs /games/snake
```

This works because the PATH is set to `/bin:/games`. The shell automatically searches
these directories.

### Where Is That Program?

```text
HBDOS:/> which hello
hello is /bin/hello (external)

HBDOS:/> which cat
cat is a built-in command

HBDOS:/> which nonexistent
nonexistent: not found
```

---

## Working with Files

### Creating a File

```text
HBDOS:/> write notes.txt
These are my notes.
Second line.
                           ← Press Enter on an empty line to finish
HBDOS:/>
```

### Reading a File

```text
HBDOS:/> cat notes.txt
These are my notes.
Second line.
```

With line numbers:

```text
HBDOS:/> cat -n notes.txt
     1  These are my notes.
     2  Second line.
```

### Other Viewing Commands

```text
HBDOS:/> head notes.txt       → First 10 lines
HBDOS:/> tail notes.txt       → Last 10 lines
HBDOS:/> wc notes.txt         → Line, word, byte count
HBDOS:/> hex notes.txt        → Hex dump
HBDOS:/> more /docs/readme    → Page-by-page viewer
```

### Modifying Files

```text
HBDOS:/> append notes.txt Third line added.
HBDOS:/> copy notes.txt backup.txt
HBDOS:/> ren backup.txt archive.txt
HBDOS:/> del archive.txt
```

### Searching in Files

```text
HBDOS:/> find notes notes.txt
```

Shows every line containing "notes".

### Comparing Files

```text
HBDOS:/> write a.txt
apple
banana
cherry

HBDOS:/> write b.txt
apple
blueberry
cherry

HBDOS:/> diff a.txt b.txt
  apple
< banana
> blueberry
  cherry
```

Lines with `<` are only in the first file (red), `>` only in the second (green).

---

## Running Programs

### By Name

Just type the program name:

```text
HBDOS:/> hello
Hello, World!

HBDOS:/> fibonacci
1 1 2 3 5 8 13 21 34 55 ...

HBDOS:/> primes
2 3 5 7 11 13 17 19 23 29 ...

HBDOS:/> colors
```

### Programs That Take Arguments

```text
HBDOS:/> edit myfile.txt       → Open myfile.txt in the editor
HBDOS:/> grep pattern file     → Search for pattern in file
HBDOS:/> sort data.txt         → Sort lines alphabetically
HBDOS:/> hexdump /bin/hello    → Hex dump of a binary
```

### Aborting a Program

Press **Ctrl+C** at any time to force-quit a running program and return to the shell.

---

## Playing Games

Mellivora comes with several games. Try them!

### Snake

```text
HBDOS:/> snake
```

Use arrow keys to steer the snake. Eat food (★) to grow. Don't hit walls or yourself!
Press ESC to quit.

### Tetris

```text
HBDOS:/> tetris
```

- **←/→** Move piece
- **↑** Rotate
- **↓** Soft drop
- **Space** Hard drop
- **ESC** Quit

### Minesweeper

```text
HBDOS:/> mine
```

- **Arrow keys** Move cursor
- **Space** Reveal cell
- **F** Toggle flag
- **ESC** Quit

### More Games

```text
HBDOS:/> sokoban        → Push boxes onto targets
HBDOS:/> 2048           → Slide number tiles
HBDOS:/> galaga         → Space shooter
HBDOS:/> guess          → Number guessing game
HBDOS:/> life           → Conway's Game of Life
HBDOS:/> maze           → Watch a maze generate and solve itself
HBDOS:/> piano          → Play music with the keyboard
```

---

## Using the Text Editor

The built-in editor lets you create and modify text files with a full-screen interface.

### Opening the Editor

```text
HBDOS:/> edit myfile.txt
```

Or just `edit` to open a scratch file.

### Editor Controls

| Key | Action |
| --- | --- |
| Arrow keys | Move cursor |
| Home / End | Start / end of line |
| Page Up/Down | Scroll by page |
| Backspace | Delete before cursor |
| Delete | Delete at cursor |
| Enter | New line |
| **Ctrl+S** | **Save file** |
| **Ctrl+Q / ESC** | **Quit** |

### Try It

1. `edit todo.txt`
2. Type some text
3. Press **Ctrl+S** to save
4. Press **ESC** to quit
5. `cat todo.txt` to verify

---

## Writing Your First Assembly Program

Let's write a simple program directly on the OS, then a more complete one on the host.

### Method 1: Write on the Host

Create `programs/greet.asm` on your host machine:

```nasm
BITS 32
ORG 0x200000

    ; Clear screen
    mov eax, 17
    int 0x80

    ; Set color to green
    mov eax, 18
    mov ebx, 0x0A
    int 0x80

    ; Print greeting
    mov eax, 3
    mov ebx, msg
    int 0x80

    ; Wait for keypress
    mov eax, 3
    mov ebx, prompt
    int 0x80
    mov eax, 2
    int 0x80

    ; Reset color
    mov eax, 18
    mov ebx, 0x07
    int 0x80

    ; Exit
    mov eax, 0
    xor ebx, ebx
    int 0x80

msg:    db "=== Welcome to Mellivora OS! ===", 10, 10
        db "This is my first program.", 10, 0
prompt: db 10, "Press any key to exit...", 0
```

Build and add to disk:

```bash
nasm -f bin -O0 -o programs/greet programs/greet.asm
```

Add `'greet'` to the `UTILITY_PROGRAMS` list in `populate.py`, then:

```bash
make full
make run
```

```text
HBDOS:/> greet
```

### Method 2: Use the Enter Command

For tiny programs, use `enter` to type raw hex bytes directly:

```text
HBDOS:/> enter
Filename: tiny
Enter hex bytes (empty line to end):
B8 03 00 00 00 BB xx xx xx xx CD 80 B8 00 00 00 00 31 DB CD 80
(hex for: mov eax,3 / mov ebx,msg / int 0x80 / mov eax,0 / xor ebx,ebx / int 0x80)
```

This is mainly useful for testing — for real programs, use NASM on the host.

---

## Writing and Compiling C Programs

### Your First C Program

```text
HBDOS:/> write myapp.c
int main() {
    printf("Hello from C!\n");
    int x = 42;
    printf("The answer is %d\n", x);
    return 0;
}

HBDOS:/> tcc myapp.c
Compiling myapp.c...
Running...
Hello from C!
The answer is 42
```

### Try the Samples

The `/samples` directory has ready-made C programs:

```text
HBDOS:/> tcc /samples/fib.c        → Fibonacci numbers
HBDOS:/> tcc /samples/primes.c     → Prime sieve
HBDOS:/> tcc /samples/hanoi.c      → Tower of Hanoi
HBDOS:/> tcc /samples/wumpus.c     → Hunt the Wumpus game!
HBDOS:/> tcc /samples/matrix.c     → Matrix rain effect
HBDOS:/> tcc /samples/stars.c      → Starfield animation
```

### Writing a Guessing Game in C

```text
HBDOS:/> write numgame.c
int main() {
    int secret = 37;
    int guess = 0;
    int tries = 0;

    printf("I'm thinking of a number 1-100.\n");

    while (guess != secret) {
        printf("Your guess: ");
        guess = 0;
        char c = getchar();
        while (c >= '0' && c <= '9') {
            putchar(c);
            guess = guess * 10 + (c - '0');
            c = getchar();
        }
        putchar('\n');
        tries = tries + 1;

        if (guess < secret) printf("Too low!\n");
        if (guess > secret) printf("Too high!\n");
    }

    printf("Correct! You got it in %d tries.\n", tries);
    return 0;
}

HBDOS:/> tcc numgame.c
```

---

## Batch Scripting

### Creating a Script

Batch scripts are text files that execute commands line by line:

```text
HBDOS:/> write startup.bat
echo ================================
echo   Welcome to Mellivora OS!
echo ================================
date
echo
echo Files in root:
dir
echo
echo Free space:
df
echo ================================

HBDOS:/> batch startup.bat
```

Each line is printed with a `>` prefix before execution.

### Variables in Scripts

```text
HBDOS:/> write demo.bat
set user Mellivora
set version 1.7
echo Hello, $user!
echo Running version $version
echo
echo System status:
mem
unset user
unset version

HBDOS:/> batch demo.bat
```

### A Practical Script

```text
HBDOS:/> write backup.bat
echo Backing up important files...
copy notes.txt notes.bak
copy todo.txt todo.bak
echo Backup complete!
dir

HBDOS:/> batch backup.bat
```

---

## Customizing Your Environment

### Change Text Colors

```text
HBDOS:/> color A 0      → Green text on black background
HBDOS:/> color F 1      → White text on blue background
HBDOS:/> color E 0      → Yellow text on black
HBDOS:/> color 7 0      → Reset to default (gray on black)
```

### Create Aliases

```text
HBDOS:/> alias ll dir -l           → Long directory listing
HBDOS:/> alias cls clear           → DOS-style clear (already built-in!)
HBDOS:/> alias hi echo Hello!      → Quick greeting
HBDOS:/> alias info ver            → Short name for version info

HBDOS:/> ll                        → Runs "dir -l"
HBDOS:/> hi                        → Prints "Hello!"
```

### Set Environment Variables

```text
HBDOS:/> set PATH /bin:/games:/samples    → Add /samples to search path
HBDOS:/> set editor edit                  → Set a custom variable
HBDOS:/> echo My editor is $editor
My editor is edit
```

### View Your Settings

```text
HBDOS:/> alias         → List all aliases
HBDOS:/> set           → List all variables
HBDOS:/> history       → List recent commands
```

---

## Next Steps

Now that you're comfortable with Mellivora OS, here's what to explore next:

### Read the Documentation

```text
HBDOS:/> more /docs/readme
```

Or on the host, read:

- **[USER_GUIDE.md](USER_GUIDE.md)** — Complete command reference
- **[TECHNICAL_REFERENCE.md](TECHNICAL_REFERENCE.md)** — OS internals and architecture
- **[PROGRAMMING_GUIDE.md](PROGRAMMING_GUIDE.md)** — Write your own programs

### Study Existing Programs

Look at the source code in `programs/` for real-world examples:

- `programs/hello.asm` — Minimal "Hello World"
- `programs/snake.asm` — Complete game with game loop, VGA rendering, collision
- `programs/edit.asm` — Full text editor with file I/O
- `programs/tetris.asm` — Game with rotation, scoring, levels

### Build Something

Ideas for your own programs:

- A text adventure game
- A simple drawing program (move cursor, toggle pixels)
- A file encryption tool (XOR cipher)
- A math quiz game
- A clock that updates in real-time
- A simple BASIC interpreter (there's already one — extend it!)

### Contribute

The source is on GitHub. Contributions welcome!

```bash
git clone https://github.com/James-HoneyBadger/Mellivora_OS.git
```
