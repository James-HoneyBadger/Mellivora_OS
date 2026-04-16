; whatis.asm - Display one-line descriptions for commands [HBU]
; Usage: whatis <command> [command2 ...]
;        whatis -a            (list all entries)
;
%include "syscalls.inc"

start:
        mov eax, SYS_GETARGS
        mov ebx, args_buf
        int 0x80
        cmp eax, 0
        jle show_usage

        mov esi, args_buf

        ; Check for -a flag
        cmp byte [esi], '-'
        jne .lookup_loop
        cmp byte [esi+1], 'a'
        jne .lookup_loop
        call list_all
        jmp .done

.lookup_loop:
        cmp byte [esi], 0
        je .done
        cmp byte [esi], ' '
        jne .got_word
        inc esi
        jmp .lookup_loop
.got_word:
        mov [cur_word], esi
        ; Find end of word
.scan_word:
        cmp byte [esi], 0
        je .do_lookup
        cmp byte [esi], ' '
        je .term_word
        inc esi
        jmp .scan_word
.term_word:
        mov byte [esi], 0
        inc esi
.do_lookup:
        push rsi
        mov esi, [cur_word]
        call lookup_cmd
        pop rsi
        jmp .lookup_loop

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; lookup_cmd: look up ESI in the database, print result
;---------------------------------------
lookup_cmd:
        PUSHALL
        mov [.lk_name], esi
        mov edi, whatis_db
.lk_loop:
        cmp byte [edi], 0      ; end of database
        je .lk_not_found
        ; EDI points to "name\0description\0"
        mov esi, [.lk_name]
        push rdi
        ; Compare name
.lk_cmp:
        mov al, [esi]
        mov bl, [edi]
        cmp bl, 0              ; end of db name
        jne .lk_cmp_cont
        cmp al, 0              ; end of query
        je .lk_match
        jmp .lk_nomatch
.lk_cmp_cont:
        cmp al, bl
        jne .lk_nomatch
        inc esi
        inc edi
        jmp .lk_cmp

.lk_match:
        pop rdi
        ; Print "name - description\n"
        mov eax, SYS_PRINT
        mov ebx, [.lk_name]
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, .lk_sep
        int 0x80
        ; Skip past name to description
        push rdi
.lk_skip_name:
        cmp byte [edi], 0
        je .lk_at_desc
        inc edi
        jmp .lk_skip_name
.lk_at_desc:
        inc edi                 ; skip null after name
        mov eax, SYS_PRINT
        mov ebx, edi
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        pop rdi
        POPALL
        ret

.lk_nomatch:
        pop rdi
        ; Skip past this entry (name\0desc\0)
.lk_skip1:
        cmp byte [edi], 0
        je .lk_skip2
        inc edi
        jmp .lk_skip1
.lk_skip2:
        inc edi                 ; past name null
.lk_skip3:
        cmp byte [edi], 0
        je .lk_skip4
        inc edi
        jmp .lk_skip3
.lk_skip4:
        inc edi                 ; past desc null
        jmp .lk_loop

.lk_not_found:
        mov eax, SYS_PRINT
        mov ebx, [.lk_name]
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, .lk_nf_msg
        int 0x80
        POPALL
        ret

.lk_name:   dd 0
.lk_sep:    db " - ", 0
.lk_nf_msg: db ": nothing appropriate", 0x0A, 0

;---------------------------------------
; list_all: print all entries
;---------------------------------------
list_all:
        PUSHALL
        mov edi, whatis_db
.la_loop:
        cmp byte [edi], 0
        je .la_done
        ; Print name
        mov eax, SYS_PRINT
        mov ebx, edi
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, .la_sep
        int 0x80
        ; Skip name
.la_skip:
        cmp byte [edi], 0
        je .la_desc
        inc edi
        jmp .la_skip
.la_desc:
        inc edi
        ; Print description
        mov eax, SYS_PRINT
        mov ebx, edi
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80
        ; Skip desc
.la_skip2:
        cmp byte [edi], 0
        je .la_next
        inc edi
        jmp .la_skip2
.la_next:
        inc edi
        jmp .la_loop
.la_done:
        POPALL
        ret
.la_sep:    db " - ", 0

show_usage:
        mov eax, SYS_PRINT
        mov ebx, usage_str
        int 0x80
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

section .data
usage_str:  db "Usage: whatis <command> [...]", 0x0A
            db "       whatis -a  (list all)", 0x0A, 0

; Database: pairs of null-terminated name, null-terminated description
; End with a single null byte
whatis_db:
    ; --- Shell built-ins ---
    db "help", 0, "display help information", 0
    db "ver", 0, "show OS version", 0
    db "clear", 0, "clear the terminal screen", 0
    db "cls", 0, "clear the terminal screen", 0
    db "dir", 0, "list directory contents", 0
    db "ls", 0, "list directory contents", 0
    db "del", 0, "delete files", 0
    db "rm", 0, "delete files", 0
    db "format", 0, "format the filesystem", 0
    db "cat", 0, "concatenate and display files", 0
    db "type", 0, "display file contents", 0
    db "write", 0, "write data to a file", 0
    db "hex", 0, "display file in hexadecimal", 0
    db "mem", 0, "display memory information", 0
    db "time", 0, "measure command execution time", 0
    db "net", 0, "network configuration summary", 0
    db "disk", 0, "display disk information", 0
    db "run", 0, "execute a binary at address", 0
    db "enter", 0, "hex entry mode for code input", 0
    db "copy", 0, "copy a file", 0
    db "ren", 0, "rename a file", 0
    db "mv", 0, "rename or move a file", 0
    db "move", 0, "rename or move a file", 0
    db "df", 0, "display filesystem usage", 0
    db "more", 0, "page through file contents", 0
    db "echo", 0, "display a line of text", 0
    db "wc", 0, "count lines, words, and bytes", 0
    db "find", 0, "search for files by name", 0
    db "append", 0, "append text to a file", 0
    db "date", 0, "display or set date and time", 0
    db "beep", 0, "play a tone through PC speaker", 0
    db "batch", 0, "execute a batch script file", 0
    db "mkdir", 0, "create a directory", 0
    db "rmdir", 0, "remove an empty directory", 0
    db "cd", 0, "change working directory", 0
    db "pwd", 0, "print working directory", 0
    db "pushd", 0, "push directory onto stack", 0
    db "popd", 0, "pop directory from stack", 0
    db "dirs", 0, "display directory stack", 0
    db "touch", 0, "create an empty file", 0
    db "set", 0, "set an environment variable", 0
    db "unset", 0, "remove an environment variable", 0
    db "read", 0, "read a line of input into a variable", 0
    db "shutdown", 0, "halt the system", 0
    db "reboot", 0, "restart the system", 0
    db "head", 0, "display first lines of a file", 0
    db "tail", 0, "display last lines of a file", 0
    db "diff", 0, "compare two files line by line", 0
    db "uniq", 0, "filter duplicate adjacent lines", 0
    db "rev", 0, "reverse characters in each line", 0
    db "tac", 0, "print file in reverse line order", 0
    db "alias", 0, "create command aliases", 0
    db "history", 0, "display command history", 0
    db "which", 0, "locate a command", 0
    db "sleep", 0, "delay for a specified time", 0
    db "color", 0, "set terminal text color", 0
    db "size", 0, "display file size in bytes", 0
    db "strings", 0, "extract printable strings from file", 0
    db "mouse", 0, "toggle mouse cursor visibility", 0
    db "burrows", 0, "launch Burrows graphical desktop", 0
    db "ping", 0, "send ICMP echo requests", 0
    db "ifconfig", 0, "display network interface config", 0
    db "dhcp", 0, "request IP address via DHCP", 0
    db "arp", 0, "display ARP cache", 0
    db "scrsaver", 0, "toggle screensaver", 0
    db "whoami", 0, "print current username", 0
    db "stat", 0, "display file status information", 0
    db "fsck", 0, "filesystem consistency check", 0
    db "ln", 0, "create symbolic links", 0
    db "chmod", 0, "change file permissions", 0
    db "chown", 0, "change file ownership", 0
    db "jobs", 0, "list background jobs", 0
    db "fg", 0, "bring background job to foreground", 0
    db "function", 0, "define a shell function", 0
    ; --- Userspace programs ---
    db "2048", 0, "2048 sliding tile puzzle game", 0
    db "adventure", 0, "text adventure game", 0
    db "asm", 0, "mini assembler for x86 code", 0
    db "banner", 0, "print large text banner", 0
    db "base64", 0, "base64 encode/decode", 0
    db "basename", 0, "strip directory from filename", 0
    db "basic", 0, "BASIC language interpreter", 0
    db "bcalc", 0, "GUI calculator application", 0
    db "bedit", 0, "GUI text editor", 0
    db "bhive", 0, "GUI file manager", 0
    db "blackjack", 0, "blackjack card game", 0
    db "bnotes", 0, "GUI sticky notes", 0
    db "bpaint", 0, "GUI paint application", 0
    db "bplayer", 0, "GUI audio player", 0
    db "breakout", 0, "breakout brick game", 0
    db "bsettings", 0, "GUI system settings", 0
    db "bsheet", 0, "GUI spreadsheet application", 0
    db "bsysmon", 0, "GUI system monitor", 0
    db "bterm", 0, "GUI terminal emulator", 0
    db "bview", 0, "GUI image viewer", 0
    db "cal", 0, "display a calendar", 0
    db "calc", 0, "command-line calculator", 0
    db "chess", 0, "chess game with AI", 0
    db "clock", 0, "display a clock", 0
    db "cmp", 0, "compare two files byte by byte", 0
    db "colors", 0, "display terminal color palette", 0
    db "comm", 0, "compare two sorted files", 0
    db "connect4", 0, "Connect Four game", 0
    db "cowsay", 0, "speaking cow ASCII art", 0
    db "cron", 0, "scheduled task runner", 0
    db "csv", 0, "CSV file viewer/formatter", 0
    db "cut", 0, "extract fields from lines", 0
    db "debug", 0, "interactive debugger", 0
    db "defrag", 0, "filesystem defragmenter", 0
    db "dirname", 0, "strip filename from path", 0
    db "doom", 0, "Doom-style raycaster game", 0
    db "doomfire", 0, "Doom fire effect demo", 0
    db "du", 0, "estimate file space usage", 0
    db "edit", 0, "full-screen text editor", 0
    db "encrypt", 0, "XOR file encryption tool", 0
    db "expand", 0, "convert tabs to spaces", 0
    db "expr", 0, "evaluate arithmetic expressions", 0
    db "factor", 0, "print prime factors", 0
    db "false", 0, "exit with failure status", 0
    db "fibonacci", 0, "print Fibonacci sequence", 0
    db "figlet", 0, "display large ASCII text", 0
    db "file", 0, "determine file type", 0
    db "fold", 0, "wrap lines to a given width", 0
    db "forager", 0, "foraging survival game", 0
    db "forth", 0, "Forth language interpreter", 0
    db "fortune", 0, "print a random fortune", 0
    db "free", 0, "display memory usage", 0
    db "freecell", 0, "FreeCell solitaire card game", 0
    db "ftp", 0, "FTP client", 0
    db "galaga", 0, "Galaga-style space shooter", 0
    db "gopher", 0, "Gopher protocol browser", 0
    db "grep", 0, "search files for patterns", 0
    db "guess", 0, "number guessing game", 0
    db "gzip", 0, "compress/decompress files", 0
    db "hangman", 0, "hangman word game", 0
    db "hanoi", 0, "Tower of Hanoi puzzle", 0
    db "hello", 0, "Hello World demo program", 0
    db "hexdump", 0, "display file as hex dump", 0
    db "hexedit", 0, "interactive hex file viewer", 0
    db "httpd", 0, "HTTP/1.1 web server", 0
    db "id", 0, "display user identity", 0
    db "irc", 0, "IRC chat client", 0
    db "join", 0, "join lines on a common field", 0
    db "json", 0, "JSON file viewer/formatter", 0
    db "kingdom", 0, "kingdom management game", 0
    db "life", 0, "Conway's Game of Life", 0
    db "lolcat", 0, "rainbow text coloring", 0
    db "man", 0, "display manual pages", 0
    db "mandel", 0, "Mandelbrot fractal renderer", 0
    db "markdown", 0, "Markdown document renderer", 0
    db "mastermind", 0, "Mastermind code-breaking game", 0
    db "matrix", 0, "Matrix rain effect", 0
    db "maze", 0, "maze generator and solver", 0
    db "md5sum", 0, "compute MD5 checksums", 0
    db "mine", 0, "text-mode minesweeper", 0
    db "mines", 0, "GUI minesweeper game", 0
    db "neofetch", 0, "system information display", 0
    db "nl", 0, "number lines of a file", 0
    db "nslookup", 0, "DNS hostname lookup", 0
    db "od", 0, "octal dump of file contents", 0
    db "pager", 0, "file pager (less-like viewer)", 0
    db "paste", 0, "merge lines of files side by side", 0
    db "periodic", 0, "periodic table display", 0
    db "perl", 0, "Perl language interpreter", 0
    db "piano", 0, "virtual piano keyboard", 0
    db "pipes", 0, "animated pipes screensaver", 0
    db "pkg", 0, "package manager", 0
    db "pong", 0, "Pong video game", 0
    db "primes", 0, "list prime numbers", 0
    db "printenv", 0, "print environment variables", 0
    db "printf", 0, "format and print data", 0
    db "ps", 0, "list running processes", 0
    db "rain", 0, "rain animation effect", 0
    db "rename", 0, "rename files using SYS_RENAME", 0
    db "rmdir", 0, "remove empty directories", 0
    db "rot13", 0, "ROT13 encode/decode text", 0
    db "sed", 0, "stream editor for text", 0
    db "seq", 0, "print a sequence of numbers", 0
    db "sha256", 0, "compute SHA-256 checksums", 0
    db "snake", 0, "snake arcade game", 0
    db "sokoban", 0, "Sokoban box-pushing puzzle", 0
    db "solitaire", 0, "Klondike solitaire card game", 0
    db "sort", 0, "sort lines of a file", 0
    db "split", 0, "split a file into pieces", 0
    db "strace", 0, "trace system calls", 0
    db "sudoku", 0, "Sudoku puzzle game", 0
    db "sysinfo", 0, "display system information", 0
    db "syslog", 0, "view or write system log", 0
    db "tar", 0, "tape archive utility", 0
    db "tcc", 0, "Tiny C Compiler", 0
    db "tee", 0, "read stdin, write to file and stdout", 0
    db "test", 0, "evaluate conditional expressions", 0
    db "telnet", 0, "Telnet protocol client", 0
    db "tetris", 0, "Tetris block puzzle game", 0
    db "top", 0, "interactive process viewer", 0
    db "tput", 0, "terminal control utility", 0
    db "tr", 0, "translate or delete characters", 0
    db "tree", 0, "display directory tree", 0
    db "true", 0, "exit with success status", 0
    db "truncate", 0, "shrink a file to a given size", 0
    db "uname", 0, "print system information", 0
    db "unexpand", 0, "convert spaces to tabs", 0
    db "uptime", 0, "show system uptime", 0
    db "watch", 0, "monitor a file for changes", 0
    db "weather", 0, "display weather information", 0
    db "wget", 0, "download files from HTTP servers", 0
    db "whatis", 0, "display command descriptions", 0
    db "wordle", 0, "Wordle word guessing game", 0
    db "worm", 0, "Worm arcade game", 0
    db "xargs", 0, "build commands from stdin", 0
    db "xxd", 0, "hex dump / reverse hex dump", 0
    db "yes", 0, "repeatedly output a string", 0
    db 0    ; end of database

section .bss
args_buf:   resb 512
cur_word:   resd 1
