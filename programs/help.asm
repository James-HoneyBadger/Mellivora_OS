; help.asm - Built-in help / manual page viewer for Mellivora OS
; Usage: help [command]
; Without arguments, lists all available commands.
; With a command name, shows detailed help for that command.

%include "syscalls.inc"

COL_TITLE       equ 0x0E        ; yellow
COL_HEADING     equ 0x0B        ; cyan
COL_TEXT         equ 0x07        ; grey
COL_CMD         equ 0x0F        ; white
COL_EXAMPLE     equ 0x0A        ; green

start:
        ; Get arguments
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80

        ; Skip leading spaces
        mov esi, arg_buf
.skip_sp:
        cmp byte [esi], ' '
        jne .sp_done
        inc esi
        jmp .skip_sp
.sp_done:
        cmp byte [esi], 0
        je .show_index

        ; Look up command in help table
        mov edi, help_table
.lookup:
        cmp dword [edi], 0       ; end of table
        je .not_found

        ; Compare argument with command name
        push esi
        mov ebx, [edi]           ; command name pointer
.cmp_loop:
        mov al, [ebx]
        mov cl, [esi]
        ; Case-insensitive
        cmp al, 'A'
        jb .cmp_no_lower_a
        cmp al, 'Z'
        ja .cmp_no_lower_a
        add al, 32
.cmp_no_lower_a:
        cmp cl, 'A'
        jb .cmp_no_lower_b
        cmp cl, 'Z'
        ja .cmp_no_lower_b
        add cl, 32
.cmp_no_lower_b:
        cmp al, cl
        jne .cmp_mismatch
        test al, al
        jz .cmp_match
        inc ebx
        inc esi
        jmp .cmp_loop

.cmp_mismatch:
        pop esi
        add edi, 8              ; next entry (name_ptr, help_ptr)
        jmp .lookup

.cmp_match:
        pop esi
        ; Found — display help text
        mov esi, [edi + 4]       ; help text pointer
        call display_help
        jmp .exit

.not_found:
        mov eax, SYS_SETCOLOR
        mov ebx, COL_TITLE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_not_found1
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, esi
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_not_found2
        int 0x80
        jmp .exit

.show_index:
        call display_index

.exit:
        mov eax, SYS_EXIT
        int 0x80


; ─── display_index ───────────────────────────────────────────
; Show list of all available commands
display_index:
        pushad
        mov eax, SYS_SETCOLOR
        mov ebx, COL_TITLE
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_index_header
        int 0x80

        mov eax, SYS_SETCOLOR
        mov ebx, COL_TEXT
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_index_intro
        int 0x80

        ; Print commands in columns
        mov edi, help_table
        xor ecx, ecx            ; column counter
.idx_loop:
        cmp dword [edi], 0
        je .idx_done

        ; Print command name
        mov eax, SYS_SETCOLOR
        mov ebx, COL_CMD
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, [edi]
        int 0x80

        ; Pad to column width (14 chars)
        mov esi, [edi]
        xor edx, edx
.idx_len:
        cmp byte [esi + edx], 0
        je .idx_pad
        inc edx
        jmp .idx_len
.idx_pad:
        cmp edx, 14
        jge .idx_col_done
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80
        inc edx
        jmp .idx_pad

.idx_col_done:
        inc ecx
        cmp ecx, 5
        jl .idx_next
        ; New line
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        xor ecx, ecx

.idx_next:
        add edi, 8
        jmp .idx_loop

.idx_done:
        ; Final newline
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80
        mov eax, SYS_SETCOLOR
        mov ebx, COL_TEXT
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, str_index_footer
        int 0x80

        popad
        ret


; ─── display_help ────────────────────────────────────────────
; Display formatted help text
; ESI = help text with format codes:
;   \1 = title color, \2 = heading color, \3 = text color,
;   \4 = cmd color, \5 = example color
display_help:
        pushad
.dh_loop:
        movzx eax, byte [esi]
        test al, al
        jz .dh_done

        cmp al, 1
        je .dh_col_title
        cmp al, 2
        je .dh_col_heading
        cmp al, 3
        je .dh_col_text
        cmp al, 4
        je .dh_col_cmd
        cmp al, 5
        je .dh_col_example

        ; Regular character
        mov ebx, eax
        mov eax, SYS_PUTCHAR
        int 0x80
        inc esi
        jmp .dh_loop

.dh_col_title:
        mov eax, SYS_SETCOLOR
        mov ebx, COL_TITLE
        int 0x80
        inc esi
        jmp .dh_loop
.dh_col_heading:
        mov eax, SYS_SETCOLOR
        mov ebx, COL_HEADING
        int 0x80
        inc esi
        jmp .dh_loop
.dh_col_text:
        mov eax, SYS_SETCOLOR
        mov ebx, COL_TEXT
        int 0x80
        inc esi
        jmp .dh_loop
.dh_col_cmd:
        mov eax, SYS_SETCOLOR
        mov ebx, COL_CMD
        int 0x80
        inc esi
        jmp .dh_loop
.dh_col_example:
        mov eax, SYS_SETCOLOR
        mov ebx, COL_EXAMPLE
        int 0x80
        inc esi
        jmp .dh_loop

.dh_done:
        ; Reset color
        mov eax, SYS_SETCOLOR
        mov ebx, COL_TEXT
        int 0x80
        popad
        ret


; ═════════════════════════════════════════════════════════════
; DATA
; ═════════════════════════════════════════════════════════════

str_not_found1: db 10, "No help found for '", 0
str_not_found2: db "'. Type 'help' for a list.", 10, 0

str_index_header: db 10
        db '  Mellivora OS - Command Reference', 10
        db '  ================================', 10, 10, 0

str_index_intro: db '  Available commands:', 10, 10, '  ', 0
str_index_footer: db '  Type "help <command>" for details.', 10, 0

; Help table: pairs of (name_ptr, help_ptr), terminated by (0, 0)
help_table:
        dd cmd_ls,      help_ls
        dd cmd_cd,      help_cd
        dd cmd_cat,     help_cat
        dd cmd_cp,      help_cp
        dd cmd_rm,      help_rm
        dd cmd_mv,      help_mv
        dd cmd_mkdir,   help_mkdir
        dd cmd_pwd,     help_pwd
        dd cmd_echo,    help_echo
        dd cmd_cls,     help_cls
        dd cmd_date,    help_date
        dd cmd_time,    help_time
        dd cmd_env,     help_env
        dd cmd_set,     help_set
        dd cmd_ping,    help_ping
        dd cmd_net,     help_net
        dd cmd_dhcp,    help_dhcp
        dd cmd_arp,     help_arp
        dd cmd_edit,    help_edit
        dd cmd_hex,     help_hex
        dd cmd_head,    help_head
        dd cmd_grep,    help_grep
        dd cmd_find,    help_find
        dd cmd_calc,    help_calc
        dd cmd_help,    help_help
        dd cmd_shutdown, help_shutdown
        dd cmd_reboot,  help_reboot
        dd cmd_burrows, help_burrows
        dd 0, 0

; Command name strings
cmd_ls:       db 'ls', 0
cmd_cd:       db 'cd', 0
cmd_cat:      db 'cat', 0
cmd_cp:       db 'cp', 0
cmd_rm:       db 'rm', 0
cmd_mv:       db 'mv', 0
cmd_mkdir:    db 'mkdir', 0
cmd_pwd:      db 'pwd', 0
cmd_echo:     db 'echo', 0
cmd_cls:      db 'cls', 0
cmd_date:     db 'date', 0
cmd_time:     db 'time', 0
cmd_env:      db 'env', 0
cmd_set:      db 'set', 0
cmd_ping:     db 'ping', 0
cmd_net:      db 'net', 0
cmd_dhcp:     db 'dhcp', 0
cmd_arp:      db 'arp', 0
cmd_edit:     db 'edit', 0
cmd_hex:      db 'hexdump', 0
cmd_head:     db 'head', 0
cmd_grep:     db 'grep', 0
cmd_find:     db 'find', 0
cmd_calc:     db 'calc', 0
cmd_help:     db 'help', 0
cmd_shutdown: db 'shutdown', 0
cmd_reboot:   db 'reboot', 0
cmd_burrows:  db 'burrows', 0

; Help text with inline color codes (1=title,2=heading,3=text,4=cmd,5=example)
help_ls:
        db 1, 'LS', 3, ' - List directory contents', 10, 10
        db 2, 'USAGE', 10, 3
        db '  ls [directory]', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Lists files and directories in the current or specified directory.', 10
        db '  Directories are shown with a trailing /.', 10, 10
        db 2, 'EXAMPLES', 10, 5
        db '  ls              List current directory', 10
        db '  ls /bin         List /bin directory', 10, 0

help_cd:
        db 1, 'CD', 3, ' - Change directory', 10, 10
        db 2, 'USAGE', 10, 3
        db '  cd <directory>', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Changes the current working directory.', 10
        db '  Use "cd /" to go to root, "cd .." to go up.', 10, 10
        db 2, 'EXAMPLES', 10, 5
        db '  cd /bin         Go to /bin', 10
        db '  cd ..           Go up one level', 10, 0

help_cat:
        db 1, 'CAT', 3, ' - Display file contents', 10, 10
        db 2, 'USAGE', 10, 3
        db '  cat <filename>', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Reads and displays the entire contents of a file.', 10, 0

help_cp:
        db 1, 'CP', 3, ' - Copy a file', 10, 10
        db 2, 'USAGE', 10, 3
        db '  cp <source> <dest>', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Copies a file from source to destination.', 10, 0

help_rm:
        db 1, 'RM', 3, ' - Remove a file', 10, 10
        db 2, 'USAGE', 10, 3
        db '  rm <filename>', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Deletes the specified file permanently.', 10, 0

help_mv:
        db 1, 'MV', 3, ' - Move/rename a file', 10, 10
        db 2, 'USAGE', 10, 3
        db '  mv <source> <dest>', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Moves or renames a file.', 10, 0

help_mkdir:
        db 1, 'MKDIR', 3, ' - Create directory', 10, 10
        db 2, 'USAGE', 10, 3
        db '  mkdir <name>', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Creates a new directory with the given name.', 10, 0

help_pwd:
        db 1, 'PWD', 3, ' - Print working directory', 10, 10
        db 2, 'USAGE', 10, 3
        db '  pwd', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Displays the current working directory path.', 10, 0

help_echo:
        db 1, 'ECHO', 3, ' - Print text', 10, 10
        db 2, 'USAGE', 10, 3
        db '  echo <text>', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Prints the given text to the console.', 10
        db '  Supports $VAR environment variable expansion.', 10, 0

help_cls:
        db 1, 'CLS', 3, ' - Clear screen', 10, 10
        db 2, 'USAGE', 10, 3
        db '  cls', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Clears the terminal screen.', 10, 0

help_date:
        db 1, 'DATE', 3, ' - Show or set date/time', 10, 10
        db 2, 'USAGE', 10, 3
        db '  date', 10
        db '  date -s YYYY-MM-DD HH:MM:SS', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Without arguments, shows current date and time.', 10
        db '  With -s, sets the real-time clock.', 10, 0

help_time:
        db 1, 'TIME', 3, ' - Show elapsed time', 10, 10
        db 2, 'USAGE', 10, 3
        db '  time <command>', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Shows system uptime in ticks (100 ticks/sec).', 10, 0

help_env:
        db 1, 'ENV', 3, ' - Show environment variables', 10, 10
        db 2, 'USAGE', 10, 3
        db '  env', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Lists all set environment variables.', 10, 0

help_set:
        db 1, 'SET', 3, ' - Set environment variable', 10, 10
        db 2, 'USAGE', 10, 3
        db '  set NAME=VALUE', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Sets an environment variable to the given value.', 10, 0

help_ping:
        db 1, 'PING', 3, ' - Send ICMP echo request', 10, 10
        db 2, 'USAGE', 10, 3
        db '  ping <ip_address>', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Sends an ICMP echo request to the given IP address', 10
        db '  and displays the round-trip time.', 10, 10
        db 2, 'EXAMPLES', 10, 5
        db '  ping 10.0.2.2', 10, 0

help_net:
        db 1, 'NET', 3, ' - Network status', 10, 10
        db 2, 'USAGE', 10, 3
        db '  net', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Shows network interface status, IP address,', 10
        db '  gateway, subnet mask, and DNS server.', 10, 0

help_dhcp:
        db 1, 'DHCP', 3, ' - Obtain IP address', 10, 10
        db 2, 'USAGE', 10, 3
        db '  dhcp', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Runs DHCP client to obtain IP configuration', 10
        db '  from the network (IP, gateway, DNS).', 10, 0

help_arp:
        db 1, 'ARP', 3, ' - Show ARP cache', 10, 10
        db 2, 'USAGE', 10, 3
        db '  arp', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Displays the ARP cache showing IP-to-MAC', 10
        db '  address mappings.', 10, 0

help_edit:
        db 1, 'EDIT', 3, ' - Text editor', 10, 10
        db 2, 'USAGE', 10, 3
        db '  edit <filename>', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Opens a full-screen text editor.', 10
        db '  Ctrl+S = Save, Ctrl+Q = Quit, Ctrl+F = Find.', 10, 0

help_hex:
        db 1, 'HEXDUMP', 3, ' - Hex file viewer', 10, 10
        db 2, 'USAGE', 10, 3
        db '  hexdump <filename>', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Displays file contents in hex+ASCII format.', 10, 0

help_head:
        db 1, 'HEAD', 3, ' - Show first lines', 10, 10
        db 2, 'USAGE', 10, 3
        db '  head [-n count] <filename>', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Shows the first N lines of a file (default 10).', 10, 0

help_grep:
        db 1, 'GREP', 3, ' - Search text in files', 10, 10
        db 2, 'USAGE', 10, 3
        db '  grep <pattern> <filename>', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Searches for a pattern in a file and prints', 10
        db '  matching lines.', 10, 0

help_find:
        db 1, 'FIND', 3, ' - Find files', 10, 10
        db 2, 'USAGE', 10, 3
        db '  find <pattern>', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Searches for files matching the given pattern.', 10, 0

help_calc:
        db 1, 'CALC', 3, ' - Calculator', 10, 10
        db 2, 'USAGE', 10, 3
        db '  calc', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Interactive calculator supporting +, -, *, /,', 10
        db '  parentheses, and hex (0x) numbers.', 10, 0

help_help:
        db 1, 'HELP', 3, ' - Show help', 10, 10
        db 2, 'USAGE', 10, 3
        db '  help [command]', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Without arguments, lists all commands.', 10
        db '  With a command name, shows detailed help.', 10, 0

help_shutdown:
        db 1, 'SHUTDOWN', 3, ' - Power off system', 10, 10
        db 2, 'USAGE', 10, 3
        db '  shutdown', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Syncs filesystem, flushes disk caches, and', 10
        db '  powers off via ACPI.', 10, 0

help_reboot:
        db 1, 'REBOOT', 3, ' - Restart system', 10, 10
        db 2, 'USAGE', 10, 3
        db '  reboot', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Restarts the system via keyboard controller reset.', 10, 0

help_burrows:
        db 1, 'BURROWS', 3, ' - Start desktop environment', 10, 10
        db 2, 'USAGE', 10, 3
        db '  burrows', 10, 10
        db 2, 'DESCRIPTION', 10, 3
        db '  Launches the Burrows graphical desktop.', 10
        db '  Alt+Tab cycles windows, click to focus.', 10
        db '  Taskbar at bottom with app menu and clock.', 10, 0


section .bss

arg_buf:        resb 256
