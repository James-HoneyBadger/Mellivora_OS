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
11. [Networking](#networking)
12. [The Burrows Desktop](#the-burrows-desktop)
13. [Customizing Your Environment](#customizing-your-environment)
14. [Next Steps](#next-steps)

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

A QEMU window opens. After a brief splash screen, you see the HB Lair banner and a
prompt:

```text
 ██╗  ██╗██████╗     ██████╗  ██████╗ ███████╗
 ██║  ██║██╔══██╗    ██╔══██╗██╔═══██╗██╔════╝
 ███████║██████╔╝    ██║  ██║██║   ██║███████╗
 ██╔══██║██╔══██╗    ██║  ██║██║   ██║╚════██║
 ██║  ██║██████╔╝    ██████╔╝╚██████╔╝███████║
 ╚═╝  ╚═╝╚═════╝     ╚═════╝  ╚═════╝ ╚══════╝

Lair:/>
```

You're in! The `Lair:/>` prompt shows your current directory (`/` = root).

### Getting Help

Type `help` and press Enter:

```text
Lair:/> help
```

This lists every built-in command with a brief description. Keep this handy as you
explore.

### System Information

```text
Lair:/> ver
```

Shows the OS version, detected hardware, and feature list.

---

## Exploring the Shell

### Your First Commands

Try these to get oriented:

```text
Lair:/> date          → Shows current date and time
Lair:/> time          → Shows uptime (seconds since boot)
Lair:/> mem           → Shows available memory
Lair:/> disk          → Shows disk size
Lair:/> df            → Shows filesystem usage
```

### Listing Files

```text
Lair:/> dir
```

You'll see the root directory contents — typically `bin`, `games`, `samples`, `docs`,
and `script.bat`.

For more detail:

```text
Lair:/> dir -l
```

This shows file types (DIR, EXEC, FILE, BATCH) and sizes.

---

## Navigating Directories

### Moving Around

```text
Lair:/> cd bin         → Enter the bin directory
Lair:/bin> dir         → List programs in /bin
Lair:/bin> cd ..       → Go back to root
Lair:/> cd games       → Enter the games directory
Lair:/games> pwd       → Print where you are: /games
Lair:/games> cd /      → Jump straight to root
```

### The PATH

Notice that even when you're in `/`, you can run programs from `/bin` and `/games`
without typing the full path:

```text
Lair:/> hello          → Runs /bin/hello
Lair:/> snake          → Runs /games/snake
```

This works because the PATH is set to `/bin:/games`. The shell automatically searches
these directories.

### Where Is That Program?

```text
Lair:/> which hello
hello is /bin/hello (external)

Lair:/> which cat
cat is a built-in command

Lair:/> which nonexistent
nonexistent: not found
```

---

## Working with Files

### Creating a File

```text
Lair:/> write notes.txt
These are my notes.
Second line.
                           ← Press Enter on an empty line to finish
Lair:/>
```

### Reading a File

```text
Lair:/> cat notes.txt
These are my notes.
Second line.
```

With line numbers:

```text
Lair:/> cat -n notes.txt
     1  These are my notes.
     2  Second line.
```

### Other Viewing Commands

```text
Lair:/> head notes.txt       → First 10 lines
Lair:/> tail notes.txt       → Last 10 lines
Lair:/> wc notes.txt         → Line, word, byte count
Lair:/> hex notes.txt        → Hex dump
Lair:/> more /docs/readme    → Page-by-page viewer
```

### Modifying Files

```text
Lair:/> append notes.txt Third line added.
Lair:/> copy notes.txt backup.txt
Lair:/> ren backup.txt archive.txt
Lair:/> del archive.txt
```

### Searching in Files

```text
Lair:/> find notes notes.txt
```

Shows every line containing "notes".

### Comparing Files

```text
Lair:/> write a.txt
apple
banana
cherry

Lair:/> write b.txt
apple
blueberry
cherry

Lair:/> diff a.txt b.txt
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
Lair:/> hello
Hello, World!

Lair:/> fibonacci
1 1 2 3 5 8 13 21 34 55 ...

Lair:/> primes
2 3 5 7 11 13 17 19 23 29 ...

Lair:/> colors
```

### Programs That Take Arguments

```text
Lair:/> edit myfile.txt       → Open myfile.txt in the editor
Lair:/> grep pattern file     → Search for pattern in file
Lair:/> sort data.txt         → Sort lines alphabetically
Lair:/> hexdump /bin/hello    → Hex dump of a binary
```

### Aborting a Program

Press **Ctrl+C** at any time to force-quit a running program and return to the shell.

---

## Playing Games

Mellivora comes with several games. Try them!

### Snake

```text
Lair:/> snake
```

Use arrow keys to steer the snake. Eat food (★) to grow. Don't hit walls or yourself!
Press ESC to quit.

### Tetris

```text
Lair:/> tetris
```

- **←/→** Move piece
- **↑** Rotate
- **↓** Soft drop
- **Space** Hard drop
- **ESC** Quit

### Minesweeper

```text
Lair:/> mine
```

- **Arrow keys** Move cursor
- **Space** Reveal cell
- **F** Toggle flag
- **ESC** Quit

### More Games

```text
Lair:/> sokoban        → Push boxes onto targets
Lair:/> 2048           → Slide number tiles
Lair:/> galaga         → Space shooter
Lair:/> guess          → Number guessing game
Lair:/> life           → Conway's Game of Life
Lair:/> maze           → Watch a maze generate and solve itself
Lair:/> piano          → Play music with the keyboard
```

---

## Using the Text Editor

The built-in editor lets you create and modify text files with a full-screen interface.

### Opening the Editor

```text
Lair:/> edit myfile.txt
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
Lair:/> greet
```

### Method 2: Use the Enter Command

For tiny programs, use `enter` to type raw hex bytes directly:

```text
Lair:/> enter
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
Lair:/> write myapp.c
int main() {
    printf("Hello from C!\n");
    int x = 42;
    printf("The answer is %d\n", x);
    return 0;
}

Lair:/> tcc myapp.c
Compiling myapp.c...
Running...
Hello from C!
The answer is 42
```

### Try the Samples

The `/samples` directory has ready-made C programs:

```text
Lair:/> tcc /samples/fib.c        → Fibonacci numbers
Lair:/> tcc /samples/primes.c     → Prime sieve
Lair:/> tcc /samples/hanoi.c      → Tower of Hanoi
Lair:/> tcc /samples/wumpus.c     → Hunt the Wumpus game!
Lair:/> tcc /samples/matrix.c     → Matrix rain effect
Lair:/> tcc /samples/stars.c      → Starfield animation
```

### Writing a Guessing Game in C

```text
Lair:/> write numgame.c
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

Lair:/> tcc numgame.c
```

---

## Batch Scripting

### Creating a Script

Batch scripts are text files that execute commands line by line:

```text
Lair:/> write startup.bat
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

Lair:/> batch startup.bat
```

Each line is printed with a `>` prefix before execution.

### Variables in Scripts

```text
Lair:/> write demo.bat
set user Mellivora
set version 1.7
echo Hello, $user!
echo Running version $version
echo
echo System status:
mem
unset user
unset version

Lair:/> batch demo.bat
```

### A Practical Script

```text
Lair:/> write backup.bat
echo Backing up important files...
copy notes.txt notes.bak
copy todo.txt todo.bak
echo Backup complete!
dir

Lair:/> batch backup.bat
```

---

## Networking

Mellivora includes a full TCP/IP networking stack. When running under QEMU with default
settings, the network is ready to use.

### Getting Connected

The OS automatically configures networking via DHCP at boot. Verify your connection:

```text
Lair:/> ifconfig
IP Address:    10.0.2.15
Subnet Mask:   255.255.255.0
Gateway:       10.0.2.2
DNS Server:    10.0.2.3
MAC Address:   52:54:00:12:34:56
```

If DHCP didn't run automatically:

```text
Lair:/> dhcp
Sending DHCP DISCOVER...
Received DHCP OFFER: 10.0.2.15
Sending DHCP REQUEST...
Received DHCP ACK
Network configured successfully.
```

### Ping a Host

```text
Lair:/> ping 10.0.2.2
Reply from 10.0.2.2: time=1ms
```

### View the ARP Table

```text
Lair:/> arp
IP Address       MAC Address        State
10.0.2.2         52:55:0a:00:02:02  Resolved
```

### Network Programs

Several programs use the network. Make sure DHCP has completed first:

```text
Lair:/> forager example.com        → Fetch a web page via HTTP
Lair:/> dns example.com            → Resolve a hostname
Lair:/> nslookup google.com        → DNS lookup
Lair:/> netstat                    → Show open sockets
Lair:/> wget http://example.com/   → Download a page
```

### Writing Network Programs

See the [Programming Guide](PROGRAMMING_GUIDE.md#networking) for how to write your own
TCP/UDP clients and servers using the `lib/net.inc` library.

---

## The Burrows Desktop

Mellivora includes a graphical desktop environment called Burrows, with windows, a
mouse cursor, taskbar and application menu.

### Launching the Desktop

```text
Lair:/> burrow
```

The screen switches to 640×480 graphical mode. You'll see a desktop background, a
taskbar at the bottom, and a "Menu" button.

### Using the Desktop

- **Click "Menu"** to open the application menu
- **Click an app** to launch it in a window
- **Click a window's title bar** to focus it
- **Drag a window's title bar** to move it
- **Click ×** in the title bar to close a window
- **Click a window button in the taskbar** to bring it to front

### Built-in Desktop Applications

| App | Description |
| --- | --- |
| About | Shows OS version information |
| Clock | Displays the current time |
| Settings | Change the desktop theme |
| BCalc (bcalc) | GUI calculator |
| BEdit (bedit) | Graphical text editor |
| BHive (bhive) | Browse and manage files |
| BForager (bforager) | GUI web browser |
| BPaint (bpaint) | Pixel art drawing tool |
| BSysMon (bsysmon) | Real-time CPU and memory stats |
| BTerm (bterm) | Terminal emulator inside the desktop |

### Changing Themes

Open **Settings** from the menu (or run `bsysmon` and use its theme button) to switch
between three themes:

- **Blue** — Default, professional look
- **Dark** — Dark mode with green accents
- **Light** — Light background theme

### Exiting the Desktop

Press **ESC** or close all windows to return to the text-mode shell.

---

## Customizing Your Environment

### Change Text Colors

```text
Lair:/> color A 0      → Green text on black background
Lair:/> color F 1      → White text on blue background
Lair:/> color E 0      → Yellow text on black
Lair:/> color 7 0      → Reset to default (gray on black)
```

### Create Aliases

```text
Lair:/> alias ll dir -l           → Long directory listing
Lair:/> alias cls clear           → DOS-style clear (already built-in!)
Lair:/> alias hi echo Hello!      → Quick greeting
Lair:/> alias info ver            → Short name for version info

Lair:/> ll                        → Runs "dir -l"
Lair:/> hi                        → Prints "Hello!"
```

### Set Environment Variables

```text
Lair:/> set PATH /bin:/games:/samples    → Add /samples to search path
Lair:/> set editor edit                  → Set a custom variable
Lair:/> echo My editor is $editor
My editor is edit
```

### View Your Settings

```text
Lair:/> alias         → List all aliases
Lair:/> set           → List all variables
Lair:/> history       → List recent commands
```

---

## Next Steps

Now that you're comfortable with Mellivora OS, here's what to explore next:

### Read the Documentation

```text
Lair:/> more /docs/readme
```

Or on the host, read:

- **[USER_GUIDE.md](USER_GUIDE.md)** — Complete command reference
- **[TECHNICAL_REFERENCE.md](TECHNICAL_REFERENCE.md)** — OS internals and architecture
- **[PROGRAMMING_GUIDE.md](PROGRAMMING_GUIDE.md)** — Write your own programs
- **[API_REFERENCE.md](API_REFERENCE.md)** — Library function reference
- **[NETWORKING_GUIDE.md](NETWORKING_GUIDE.md)** — Networking architecture and usage

### Study Existing Programs

Look at the source code in `programs/` for real-world examples:

- `programs/hello.asm` — Minimal "Hello World"
- `programs/snake.asm` — Complete game with game loop, VGA rendering, collision
- `programs/edit.asm` — Full text editor with file I/O
- `programs/tetris.asm` — Game with rotation, scoring, levels

### Build Something

Ideas for your own programs:

- A text adventure game
- A network chat client (use `lib/net.inc`)
- A GUI drawing program (use `lib/gui.inc`)
- A file encryption tool (XOR cipher)
- A math quiz game
- A clock that updates in real-time
- A simple BASIC interpreter (there's already one — extend it!)
- An HTTP server that serves files from HBFS

### Contribute

The source is on GitHub. Contributions welcome!

```bash
git clone https://github.com/James-HoneyBadger/Mellivora_OS.git
```
