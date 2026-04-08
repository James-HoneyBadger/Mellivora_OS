;=======================================================================
; PATH SEARCH: Save and restore current directory state
; Used when searching PATH directories so we can temporarily cd
; into each path component without losing the user's cwd.
;=======================================================================

path_save_cwd:
        pushad
        ; Save scalar state
        mov eax, [current_dir_lba]
        mov [path_save_lba], eax
        mov eax, [current_dir_sects]
        mov [path_save_sects], eax
        mov eax, [dir_depth]
        mov [path_save_depth], eax
        ; Copy current_dir_name
        mov esi, current_dir_name
        mov edi, path_save_name
        call str_copy
        ; Copy dir_stack
        mov esi, dir_stack
        mov edi, path_save_stack
        mov ecx, DIR_STACK_MAX * DIR_STACK_ENTRY_SIZE
        rep movsb
        popad
        ret

path_restore_cwd:
        pushad
        ; Restore scalar state
        mov eax, [path_save_lba]
        mov [current_dir_lba], eax
        mov eax, [path_save_sects]
        mov [current_dir_sects], eax
        mov eax, [path_save_depth]
        mov [dir_depth], eax
        ; Copy name back
        mov esi, path_save_name
        mov edi, current_dir_name
        call str_copy
        ; Copy dir_stack back
        mov esi, path_save_stack
        mov edi, dir_stack
        mov ecx, DIR_STACK_MAX * DIR_STACK_ENTRY_SIZE
        rep movsb
        popad
        ret

; File-level CWD save/restore (separate from path_save_cwd)
; Used by hbfs_read_file for path resolution without conflicting
; with the PATH search in cmd_exec_program
file_save_cwd:
        pushad
        mov eax, [current_dir_lba]
        mov [file_save_lba], eax
        mov eax, [current_dir_sects]
        mov [file_save_sects], eax
        mov eax, [dir_depth]
        mov [file_save_depth], eax
        mov esi, current_dir_name
        mov edi, file_save_name
        call str_copy
        mov esi, dir_stack
        mov edi, file_save_stack
        mov ecx, DIR_STACK_MAX * DIR_STACK_ENTRY_SIZE
        rep movsb
        popad
        ret

file_restore_cwd:
        pushad
        mov eax, [file_save_lba]
        mov [current_dir_lba], eax
        mov eax, [file_save_sects]
        mov [current_dir_sects], eax
        mov eax, [file_save_depth]
        mov [dir_depth], eax
        mov esi, file_save_name
        mov edi, current_dir_name
        call str_copy
        mov esi, file_save_stack
        mov edi, dir_stack
        mov ecx, DIR_STACK_MAX * DIR_STACK_ENTRY_SIZE
        rep movsb
        popad
        ret

;=======================================================================
; UTILITY: String copy ESI -> EDI
;=======================================================================

str_copy:
        lodsb
        stosb
        cmp al, 0
        jne str_copy
        ret

;=======================================================================
; STRING DATA
;=======================================================================

banner_str:
        db COLOR_HEADER
        db "          Mellivora OS - 32-bit Protected Mode Operating System          ", 0x0A
        db "       HB Lair v1.4 - Honey Badger Lair / i486+                        ", 0x0A, 0

version_text:
        db "Mellivora OS v1.4", 0x0A
        db "  Kernel:        Mellivora (32-bit protected mode, ring 0/3)", 0x0A
        db "  Init System:   Ratel (hardware & subsystem initialization)", 0x0A
        db "  Shell:         HB Lair (Honey Badger Lair)", 0x0A
        db "  Utilities:     HBU (Honey Badger Utilities)", 0x0A
        db "  Address Space: 4 GB flat memory model", 0x0A
        db "  Disk:          ATA PIO, LBA48 (up to 128 PB)", 0x0A
        db "  Filesystem:    HBFS (Honey Badger File System)", 0x0A
        db "    Block size:  4096 bytes", 0x0A
        db "    Max files:   227 root / 56 per subdirectory", 0x0A
        db "    Max name:    252 characters", 0x0A
        db "    Multi-block: Files can span multiple blocks", 0x0A
        db "    Subdirs:     mkdir / cd / pwd", 0x0A
        db "  Timer:         PIT at 100 Hz", 0x0A
        db "  Keyboard:      PS/2 with shift/ctrl support", 0x0A
        db "  Display:       VGA 80x25 text, 16 colors", 0x0A
        db "  Serial:        COM1 at 115200 baud", 0x0A
        db "  Speaker:       PC speaker (beep)", 0x0A
        db "  Syscalls:      INT 0x80 (34 syscalls)", 0x0A
        db "  Shell:         Tab completion, history, Ctrl+C abort", 0x0A
        db "  Programs:      Flat binary + ELF32 (ring 3)", 0x0A
        db "  Arguments:     Command-line args via SYS_GETARGS", 0x0A
        db "  Env vars:      set/unset with $VAR expansion", 0x0A
        db "  File I/O:      open/read/write/close/seek (fd)", 0x0A
        db "  Scripts:       Batch file execution", 0x0A
        db "  Security:      Raw disk access denied to ring 3", 0x0A, 0

shell_prompt_pre:  db "Lair:", 0
shell_prompt_post: db "> ", 0
default_path:      db "PATH=/bin:/games", 0
path_env_name:     db "PATH", 0

; Command strings
cmd_help_str:   db "help", 0
cmd_ver_str:    db "ver", 0
cmd_clear_str:  db "clear", 0
cmd_dir_str:    db "dir", 0
cmd_ls_str:     db "ls", 0
cmd_del_str:    db "del", 0
cmd_rm_str:     db "rm", 0
cmd_format_str: db "format", 0
cmd_cat_str:    db "cat", 0
cmd_write_str:  db "write", 0
cmd_hex_str:    db "hex", 0
cmd_mem_str:    db "mem", 0
cmd_time_str:   db "time", 0
cmd_disk_str:   db "disk", 0
cmd_run_str:    db "run", 0
cmd_enter_str:  db "enter", 0
cmd_copy_str:   db "copy", 0
cmd_ren_str:    db "ren", 0
cmd_mv_str:     db "mv", 0
cmd_move_str:   db "move", 0
cmd_df_str:     db "df", 0
cmd_more_str:   db "more", 0
cmd_echo_str:   db "echo", 0
cmd_wc_str:     db "wc", 0
cmd_find_str:   db "find", 0
cmd_append_str: db "append", 0
cmd_date_str:   db "date", 0
cmd_beep_str:   db "beep", 0
cmd_batch_str:  db "batch", 0
cmd_mkdir_str:  db "mkdir", 0
cmd_cd_str:     db "cd", 0
cmd_pwd_str:    db "pwd", 0
cmd_touch_str:  db "touch", 0
cmd_set_str:    db "set", 0
cmd_unset_str:  db "unset", 0
cmd_shutdown_str: db "shutdown", 0
cmd_cls_str:    db "cls", 0
cmd_head_str:   db "head", 0
cmd_tail_str:   db "tail", 0
cmd_type_str:   db "type", 0
cmd_diff_str:   db "diff", 0
cmd_uniq_str:   db "uniq", 0
cmd_rev_str:    db "rev", 0
cmd_tac_str:    db "tac", 0
cmd_alias_str:  db "alias", 0
cmd_history_str: db "history", 0
cmd_which_str:  db "which", 0
cmd_sleep_str:  db "sleep", 0
cmd_color_str:  db "color", 0
cmd_size_str:   db "size", 0
cmd_strings_str: db "strings", 0

; Help text
msg_help_no_detail:
        db "No specific help for that command. Showing full help:", 0x0A, 0

help_help_help:
        db "help [command]", 0x0A
        db "Show full command list, or detailed help for one command.", 0x0A, 0
help_help_ver:
        db "ver", 0x0A
        db "Show Mellivora OS version and memory summary.", 0x0A, 0
help_help_clear:
        db "clear | cls", 0x0A
        db "Clear the screen and reset cursor position.", 0x0A, 0
help_help_dir:
        db "dir | ls [-l]", 0x0A
        db "List files in current directory. Use -l for long format.", 0x0A, 0
help_help_cat:
        db "cat | type [-n] FILE", 0x0A
        db "Display a file. Use -n to show line numbers.", 0x0A, 0
help_help_write:
        db "write FILE", 0x0A
        db "Create/overwrite FILE with typed lines (empty line ends).", 0x0A, 0
help_help_append:
        db "append FILE", 0x0A
        db "Append typed lines to an existing file.", 0x0A, 0
help_help_run:
        db "run FILE", 0x0A
        db "Load and execute a binary program.", 0x0A, 0
help_help_echo:
        db "echo [-n] TEXT", 0x0A
        db "Print text. -n suppresses trailing newline.", 0x0A, 0
help_help_cd:
        db "cd DIR", 0x0A
        db "Change current working directory.", 0x0A, 0
help_help_pwd:
        db "pwd", 0x0A
        db "Print current working directory.", 0x0A, 0
help_help_mkdir:
        db "mkdir DIR", 0x0A
        db "Create a new directory.", 0x0A, 0
help_help_set:
        db "set NAME VALUE", 0x0A
        db "Set an environment variable.", 0x0A, 0
help_help_unset:
        db "unset NAME", 0x0A
        db "Remove an environment variable.", 0x0A, 0
help_help_history:
        db "history", 0x0A
        db "Show recent command history.", 0x0A, 0
help_help_which:
        db "which NAME", 0x0A
        db "Show whether NAME is built-in or found as a program.", 0x0A, 0
help_help_color:
        db "color FG [BG]", 0x0A
        db "Set VGA text colors using hex digits 0-F.", 0x0A, 0
help_help_size:
        db "size FILE", 0x0A
        db "Show file size and type information.", 0x0A, 0
help_help_strings:
        db "strings FILE", 0x0A
        db "Extract printable strings from a file.", 0x0A, 0

help_text:
        db "HB Lair Commands:", 0x0A
        db "  help       - Show this help", 0x0A
        db "  ver        - System information", 0x0A
        db "  clear/cls  - Clear screen", 0x0A
        db "  dir/ls [-l]- List files (-l for long format)", 0x0A
        db "  cat [-n] F - Display file (-n for line numbers)", 0x0A
        db "  type FILE  - Alias for cat", 0x0A
        db "  head [-n N] F  - Show first N lines (default 10)", 0x0A
        db "  tail [-n N] F  - Show last N lines (default 10)", 0x0A
        db "  more [-n N] F  - Page through file (-n N page size)", 0x0A
        db "  wc [-lwc] F    - Line/word/byte count", 0x0A
        db "  find [-ic] P F - Search pattern P in file F", 0x0A
        db "             (-i case-insensitive, -c count only)", 0x0A
        db "  write F    - Write text to file (end with empty line)", 0x0A
        db "  append F   - Append text to existing file", 0x0A
        db "  touch F    - Create empty file", 0x0A
        db "  hex [-n N] F   - Hexdump a file (-n N byte limit)", 0x0A
        db "  del/rm [-v] F  - Delete a file (-v verbose)", 0x0A
        db "             (wildcards: * and ? supported)", 0x0A
        db "  copy [-v] S D  - Copy file (-v verbose)", 0x0A
        db "             (wildcards in S; use * in D for multi-match)", 0x0A
        db "  ren O N    - Rename file O to N", 0x0A
        db "  mv/move    - Alias for ren", 0x0A
        db "  run FILE   - Execute a program", 0x0A
        db "  enter      - Enter hex bytes as program", 0x0A
        db "  echo [-n] T    - Print text (-n no newline, $VAR)", 0x0A
        db "  date       - Show current date and time", 0x0A
        db "  beep       - Play a beep", 0x0A
        db "  batch F    - Execute script file", 0x0A
        db "  mkdir D    - Create directory", 0x0A
        db "  cd DIR     - Change directory", 0x0A
        db "  pwd        - Print working directory", 0x0A
        db "  set N V    - Set environment variable", 0x0A
        db "  unset N    - Remove environment variable", 0x0A
        db "  shutdown   - Shut down the system", 0x0A
        db "  format     - Format filesystem (WARNING: erases all data)", 0x0A
        db "  mem        - Memory information", 0x0A
        db "  disk       - Disk information", 0x0A
        db "  df         - Filesystem usage", 0x0A
        db "  time       - Uptime", 0x0A
        db "  diff F1 F2 - Compare two files line by line", 0x0A
        db "  uniq [-cd] F   - Remove adjacent duplicates", 0x0A
        db "             (-c count, -d duplicates only)", 0x0A
        db "  rev F      - Reverse each line", 0x0A
        db "  tac F      - Print file in reverse line order", 0x0A
        db "  alias [N C]- Define alias N for command C", 0x0A
        db "  history    - Show command history", 0x0A
        db "  which N    - Show if N is built-in or program", 0x0A
        db "  sleep N    - Sleep N seconds", 0x0A
        db "  color FG [BG] - Set text color (hex 0-F)", 0x0A
        db "  size F     - Show file size and type", 0x0A
        db "  strings F  - Extract printable strings from file", 0x0A
        db "  <name>     - Run program (searches PATH)", 0x0A
        db "  Tab        - Auto-complete filename", 0x0A
        db "  Ctrl+C     - Cancel current input", 0x0A
        db 0x0A
        db "PATH: set PATH /bin:/games to search directories.", 0x0A
        db "      Programs in PATH dirs run from any directory.", 0x0A, 0

; Messages
msg_exception:  db "*** CPU EXCEPTION ***", 0x0A, 0
msg_exc_eip:    db "  EIP: 0x", 0
msg_exc_err:    db "  Error code: 0x", 0
msg_ata_ok:     db "  ATA disk:      ", 0
msg_ata_none:   db "  ATA disk:      not detected", 0x0A, 0
msg_shutdown_bar: db "===========================================================", 0x0A, 0
msg_shutdown:   db "  Shutting down Mellivora OS...", 0x0A, 0
msg_halt:       db "  System halted. You may power off now.", 0x0A, 0
msg_mb:         db " MB", 0x0A, 0
msg_cpu:        db "  CPU:           i486+ (32-bit protected mode)", 0x0A, 0
msg_mem_detect: db "  RAM:           ", 0
msg_mem_total:  db "  Total memory:  ", 0
msg_mem_header: db "=== Memory Information ===", 0x0A, 0
msg_free_pages: db "  Free pages:    ", 0
msg_pages_suffix: db " (4KB each)", 0x0A, 0
msg_free_mem:   db "  Free memory:   ", 0
msg_tick_count: db "  Timer ticks:   ", 0
msg_df_header:  db "=== HBFS Filesystem Usage ===", 0x0A, 0
msg_df_total:   db "  Total blocks:  ", 0
msg_df_used:    db "  Used blocks:   ", 0
msg_df_free:    db "  Free blocks:   ", 0
msg_df_avail:   db "  Free space:    ", 0
msg_df_blocks:  db " (4KB each)", 0x0A, 0
msg_df_kb:      db " KB", 0x0A, 0
msg_df_files:   db "  Files:         ", 0
msg_df_in:      db " in ", 0
msg_df_dirs:    db " directories", 0x0A, 0
msg_more_prompt: db "-- MORE -- (Space/Enter=next, q/ESC=quit)", 0
msg_more_erase: db "                                          ", 0
msg_disk_header: db "=== Disk Information ===", 0x0A, 0
msg_disk_sectors: db "  Total sectors: ", 0
msg_disk_size:  db "  Disk size:     ", 0
msg_hbfs_found:  db "  Filesystem:    HBFS detected", 0x0A, 0
msg_hbfs_format: db "  Filesystem:    Formatting new HBFS...", 0x0A, 0
msg_hbfs_formatted: db "  Filesystem formatted.", 0x0A, 0
msg_hbfs_nodisk: db "  Filesystem:    No disk available", 0x0A, 0
msg_hbfs_dir_read_err:  db "HBFS: directory read failed", 0x0A, 0
msg_hbfs_dir_write_err: db "HBFS: directory write failed", 0x0A, 0
msg_hbfs_bitmap_err:    db "HBFS: bitmap read failed", 0x0A, 0
msg_ata_read_err:       db "ATA: read failed after retries", 0x0A, 0
msg_ata_write_err:      db "ATA: write failed after retries", 0x0A, 0
dir_header:     db "Type       Size  Name", 0x0A, 0
dir_separator:  db "----- --------- ----------------", 0x0A, 0
type_text:      db "text ", 0
type_batch:     db "batch", 0
type_exec:      db "exec ", 0
type_dir:       db "dir  ", 0
msg_dir_count:  db "  ", 0
msg_dir_files:  db " file(s)", 0x0A, 0
msg_not_found:  db "File not found", 0x0A, 0
msg_deleted:    db "Deleted.", 0x0A, 0
msg_saved:      db "Saved.", 0x0A, 0
msg_copied:     db "Copied.", 0x0A, 0
msg_renamed:    db "Renamed.", 0x0A, 0
msg_copy_usage: db "Usage: copy SRC DEST", 0x0A, 0
msg_ren_usage:  db "Usage: ren|mv OLD NEW", 0x0A, 0
msg_wild_needs_star: db "For multiple wildcard matches, destination must include '*'.", 0x0A, 0
msg_write_prompt: db "Type content (empty line to end):", 0x0A, 0
msg_write_err:  db "Write error!", 0x0A, 0
msg_unknown_cmd: db "Unknown command. Type 'help'.", 0x0A, 0
msg_redir_syntax: db "Redirection syntax error.", 0x0A, 0
msg_redir_in_err: db "Redirection input file error.", 0x0A, 0
msg_redir_too_big: db "Redirection file too large.", 0x0A, 0
msg_touch_usage: db "Usage: touch <filename>", 0x0A, 0
msg_head_usage: db "Usage: head [N] <filename>", 0x0A, 0
msg_tail_usage: db "Usage: tail [N] <filename>", 0x0A, 0
msg_format_confirm: db "Format will erase all data. Continue? (y/N) ", 0
msg_cancelled:  db "Cancelled.", 0x0A, 0
msg_uptime:     db "Uptime: ", 0
msg_seconds:    db " seconds", 0x0A, 0
msg_serial_info: db "  Serial:        COM1 @ 115200 baud", 0x0A, 0
msg_exec:       db "Executing...", 0x0A, 0
msg_exec_done:  db "Program exited.", 0x0A, 0
msg_ctrl_c:     db "^C", 0x0A, 0
msg_batch_prefix: db "> ", 0
msg_batch_nested: db "Error: nested batch execution not supported", 0x0A, 0
msg_dir_created: db "Directory created.", 0x0A, 0
msg_already_exists: db "Already exists.", 0x0A, 0
msg_env_set:    db "", 0
msg_del_prefix: db "  deleted: ", 0
msg_copy_prefix: db "  ", 0
msg_copy_arrow: db " -> ", 0
msg_diff_header1: db "--- ", 0
msg_diff_header2: db "+++ ", 0
msg_diff_line:  db "@@ line ", 0
msg_diff_same:  db "Files are identical.", 0x0A, 0
msg_builtin:    db " is a shell built-in command", 0x0A, 0
msg_external:   db " (external)", 0x0A, 0
msg_not_found_w: db ": not found", 0x0A, 0
msg_is_str:     db " is ", 0
msg_alias_set:  db "Alias set.", 0x0A, 0
msg_size_bytes: db " bytes, ", 0
msg_size_blocks: db " blocks, type: ", 0
msg_type_text:  db "text", 0x0A, 0
msg_type_dir:   db "directory", 0x0A, 0
msg_type_exec:  db "executable", 0x0A, 0
msg_type_batch: db "batch script", 0x0A, 0
msg_type_unknown: db "unknown", 0x0A, 0
msg_sleeping:   db "Sleeping ", 0
msg_sleep_sec:  db " seconds...", 0x0A, 0
msg_color_set:  db "Color set.", 0x0A, 0
msg_color_usage: db "Usage: color FG [BG] (hex 0-F)", 0x0A, 0
msg_alias_usage: db "Usage: alias NAME COMMAND", 0x0A, 0
msg_no_aliases: db "No aliases defined.", 0x0A, 0
msg_need_filename: db "Error: filename required", 0x0A, 0
msg_need_2files: db "Usage: diff FILE1 FILE2", 0x0A, 0
msg_file_not_found: db "File not found", 0x0A, 0
msg_no_history: db "No command history.", 0x0A, 0
msg_table_full: db "Alias table full.", 0x0A, 0
msg_file_colon: db ": ", 0
msg_sleep_usage: db "Usage: sleep SECONDS", 0x0A, 0

;=======================================================================
; SCANCODE TRANSLATION TABLES
;=======================================================================

; US keyboard scancode -> ASCII (unshifted)
scancode_table:
        ;       0     1     2     3     4     5     6     7
        db   0,  27, '1', '2', '3', '4', '5', '6'     ; 0x00-0x07
        db '7', '8', '9', '0', '-', '=',0x08,0x09     ; 0x08-0x0F (BS, TAB)
        db 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i'    ; 0x10-0x17
        db 'o', 'p', '[', ']',0x0D,  0,  'a', 's'     ; 0x18-0x1F (Enter, LCtrl)
        db 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';'    ; 0x20-0x27
        db  39, '`',  0, '\', 'z', 'x', 'c', 'v'      ; 0x28-0x2F (', `, LShift, \)
        db 'b', 'n', 'm', ',', '.', '/',  0,  '*'     ; 0x30-0x37 (RShift, KP*)
        db   0, ' ',  0,   0,   0,   0,   0,   0      ; 0x38-0x3F (LAlt, Space, Caps, F1-F5)
        db   0,   0,   0,   0,   0,   0,   0,  '7'    ; 0x40-0x47 (F6-F10, NumLock, ScrLock, KP7)
        db 0x80,'9', '-',0x82,'5',0x83,'+', '1'        ; 0x48-0x4F (Up,KP9,-,Left,KP5,Right,+,KP1)
        db 0x81,'3', '0', '.',  0,   0,   0,   0       ; 0x50-0x57 (Down,KP3,KP0,KP.)
        times 128 - ($ - scancode_table) db 0

; Shifted ASCII table (indexed by ASCII value of unshifted char, offset by 0x20)
shift_table:
        db ' ', '!', '"', '#', '$', '%', '&', '"'      ; 0x20-0x27
        db '(', ')', '*', '+', '<', '_', '>', '?'      ; 0x28-0x2F
        db ')', '!', '@', '#', '$', '%', '^', '&'      ; 0x30-0x37
        db '*', '(', ':', ':', '<', '+', '>', '?'      ; 0x38-0x3F
        db '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G'    ; 0x40-0x47
        db 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O'    ; 0x48-0x4F
        db 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W'    ; 0x50-0x57
        db 'X', 'Y', 'Z', '{', '|', '}', '^', '_'     ; 0x58-0x5F
        db '~', 'A', 'B', 'C', 'D', 'E', 'F', 'G'    ; 0x60-0x67
        db 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O'    ; 0x68-0x6F
        db 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W'    ; 0x70-0x77
        db 'X', 'Y', 'Z', '{', '|', '}', '~',  0      ; 0x78-0x7F

;=======================================================================
; IDT DESCRIPTOR
;=======================================================================
idt_descriptor:
        dw 256 * 8 - 1         ; IDT size (256 entries * 8 bytes)
        dd idt_table            ; IDT base address

;=======================================================================
; BSS (Uninitialized Data) - zeroed at boot
;=======================================================================

bss_start:

vga_cursor_x:   resd 1
vga_cursor_y:   resd 1
vga_color:      resb 1

tick_count:     resd 1
total_free_pages: resd 1

kb_buffer:      resb KB_BUFFER_SIZE
kb_read_idx:    resd 1
kb_write_idx:   resd 1
kb_shift:       resb 1

ata_present:    resb 1
ata_total_sectors: resq 1      ; 64-bit LBA48 sector count
ata_identify_buf: resb 512

hbfs_super_buf:  resb 512       ; Superblock buffer
hbfs_dir_buf:    resb HBFS_ROOT_DIR_SIZE  ; Directory buffer (64KB)
hbfs_block_buf:  resb HBFS_BLOCK_SIZE     ; General block buffer (4KB)
hbfs_bitmap_buf: resb HBFS_BLOCK_SIZE     ; Bitmap buffer (4KB)

line_buffer:    resb LINE_BUFFER_SIZE
temp_line_buf:  resb LINE_BUFFER_SIZE
filename_buf:   resb 256
filename_buf2:  resb 256
wildcard_src_buf: resb 256
wildcard_dst_buf: resb 256
redir_out_name: resb 256
redir_in_name:  resb 256
redir_out_buf:  resb BATCH_BUFFER_SIZE
redir_in_buf:   resb BATCH_BUFFER_SIZE

exc_eip:        resd 1          ; Saved EIP from exception
exc_errcode:    resd 1          ; Saved error code from exception

hist_buf:       resb HIST_MAX * HIST_ENTRY_SIZE  ; Command history buffer (8 * 256 = 2KB)
hist_count:     resd 1          ; Number of entries in history
hist_browse:    resd 1          ; Current browse position

idt_table:      resb 256 * 8   ; 256 IDT entries * 8 bytes each = 2KB

; Serial port
serial_present: resb 1

; Keyboard ctrl state
kb_ctrl:        resb 1

; RTC time fields
rtc_sec:        resb 1
rtc_min:        resb 1
rtc_hour:       resb 1
rtc_day:        resb 1
rtc_month:      resb 1
rtc_year:       resb 1
rtc_century:    resb 1

; Ctrl+C flag
ctrl_c_flag:    resb 1

; Program execution
program_entry:  resd 1
program_exit_code: resd 1

; TSS structure (104 bytes)
tss_struct:     resb 104

; File descriptor table (8 fds * 32 bytes)
fd_table:       resb FD_MAX * FD_ENTRY_SIZE

; Environment variables (16 * 128 = 2KB)
env_table:      resb ENV_MAX * ENV_ENTRY_SIZE
env_name_buf:   resb 64
env_temp_buf:   resb LINE_BUFFER_SIZE
expand_buf:     resb LINE_BUFFER_SIZE

; Subdirectory tracking
current_dir_lba:  resd 1
current_dir_sects: resd 1
current_dir_name: resb 256

; Directory stack for multi-level subdirectory navigation
dir_depth:        resd 1                             ; Current nesting depth (0=root)
dir_stack:        resb DIR_STACK_MAX * DIR_STACK_ENTRY_SIZE  ; Stack of parent dirs

; Last file type read (set by hbfs_read_file)
last_file_type: resb 1
wild_star_pat:  resd 1
wild_star_txt:  resd 1

; Batch execution buffers
batch_line_buf:   resb LINE_BUFFER_SIZE
batch_script_buf: resb BATCH_BUFFER_SIZE  ; 32KB batch script storage
batch_running:    resb 1                  ; Guard against nested batch calls
batch_resume_pos: resd 1                  ; Resume offset into batch_script_buf

; Program execution state
program_running: resb 1          ; 1 = user program is executing
prog_name_buf:   resb 256        ; Parsed program name from command line
program_args_buf: resb 512       ; Command-line arguments for programs
head_lines:      resd 1          ; Line count for head/tail commands
cmd_flags:       resd 1          ; Bitmask of parsed flags (bit 0='a', bit 13='n', etc.)
cmd_flag_num:    resd 1          ; Numeric argument from -n N
path_search_buf: resb 256        ; Buffer for PATH-based program search
temp_path_buf:   resb 256        ; Buffer for building PATH/progname paths
stdout_redir_active: resb 1
stdout_redir_append: resb 1
stdin_redir_active:  resb 1
stdout_redir_len:    resd 1
stdin_redir_len:     resd 1
stdin_redir_pos:     resd 1

; Alias table (16 entries: 32 byte name + 224 byte command = 256 per entry)
ALIAS_MAX        equ 16
ALIAS_NAME_LEN   equ 32
ALIAS_CMD_LEN    equ 224
ALIAS_ENTRY_SIZE equ ALIAS_NAME_LEN + ALIAS_CMD_LEN
alias_table:     resb ALIAS_MAX * ALIAS_ENTRY_SIZE  ; 4KB alias table
alias_count:     resd 1

; Diff command buffers
diff_buf1:       resd 1          ; Pointer (uses malloc)
diff_buf2:       resd 1          ; Pointer (uses malloc)

; Strings command
strings_min_len: resd 1

; Alias expansion buffer
alias_expand_buf: resb 256
alias_expanding:  resb 1          ; Guard: 1 = already expanded, prevents recursion

; PATH search: saved directory state for restore after search
path_save_lba:    resd 1          ; Saved current_dir_lba
path_save_sects:  resd 1          ; Saved current_dir_sects
path_save_depth:  resd 1          ; Saved dir_depth
path_save_name:   resb 256        ; Saved current_dir_name
path_save_stack:  resb DIR_STACK_MAX * DIR_STACK_ENTRY_SIZE  ; Saved dir_stack

; File-level CWD save (used by hbfs_read_file path resolution)
file_save_lba:    resd 1
file_save_sects:  resd 1
file_save_depth:  resd 1
file_save_name:   resb 256
file_save_stack:  resb DIR_STACK_MAX * DIR_STACK_ENTRY_SIZE

; Path resolution buffers for hbfs_read_file
path_dir_buf:     resb 256        ; Directory part of path (e.g. "/bin")
path_base_buf:    resb 256        ; Basename part of path (e.g. "hello")

; GFF (global file find) CWD save slots
gff_cwd_lba:      resd 1
gff_cwd_sects:    resd 1
gff_cwd_depth:    resd 1
gff_cwd_name:     resb 256
gff_cwd_stack:    resb DIR_STACK_MAX * DIR_STACK_ENTRY_SIZE

bss_end:
