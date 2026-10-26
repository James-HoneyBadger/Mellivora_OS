;=======================================================================
; cmdtable.inc - the one list of shell built-ins
;
; `help`, tab completion and `which` all read this table, and the lint check
; tools/check_command_table.py fails if the dispatcher and this table disagree.
; An entry is 16 bytes:
;
;   [0]  dd  pointer to the command name ("dir")
;   [4]  dd  pointer to a one-line summary
;   [8]  dd  pointer to the detail block used by `help <cmd>`
;   [12] db  category (CAT_*)
;   [13] db  reserved
;   [14] dw  reserved
;
; The table ends with a 16-byte zero entry.
;=======================================================================

%ifndef CMDTABLE_INC
%define CMDTABLE_INC

CMD_ENTRY_SIZE  equ 16

; Categories, in the order `help` prints them.
CAT_SYSTEM      equ 0
CAT_FILES       equ 1
CAT_TEXT        equ 2
CAT_PROCESS     equ 3
CAT_SCRIPT      equ 4
CAT_DISPLAY     equ 5
CAT_NETWORK     equ 6
CAT_COUNT       equ 7

%macro CMDENT 4
        dd %1
        dd %3
        dd %4
        db %2, 0
        dw 0
%endmacro

cmd_table:
        CMDENT cmd_clear_str, CAT_SYSTEM, sum_clear, det_clear
        CMDENT cmd_cls_str, CAT_SYSTEM, sum_cls, det_cls
        CMDENT cmd_color_str, CAT_SYSTEM, sum_color, det_color
        CMDENT cmd_date_str, CAT_SYSTEM, sum_date, det_date
        CMDENT cmd_disk_str, CAT_SYSTEM, sum_disk, det_disk
        CMDENT cmd_help_str, CAT_SYSTEM, sum_help, det_help
        CMDENT cmd_history_str, CAT_SYSTEM, sum_history, det_history
        CMDENT cmd_mem_str, CAT_SYSTEM, sum_mem, det_mem
        CMDENT cmd_reboot_str, CAT_SYSTEM, sum_reboot, det_reboot
        CMDENT cmd_shutdown_str, CAT_SYSTEM, sum_shutdown, det_shutdown
        CMDENT cmd_sleep_str, CAT_SYSTEM, sum_sleep, det_sleep
        CMDENT cmd_time_str, CAT_SYSTEM, sum_time, det_time
        CMDENT cmd_acpi_str, CAT_SYSTEM, sum_acpi, det_acpi
        CMDENT cmd_locks_str, CAT_SYSTEM, sum_locks, det_locks
        CMDENT cmd_cmod_str, CAT_SYSTEM, sum_cmod, det_cmod
        CMDENT cmd_fpu_str, CAT_SYSTEM, sum_fpu, det_fpu
        CMDENT cmd_ver_str, CAT_SYSTEM, sum_ver, det_ver
        CMDENT cmd_watch_str, CAT_SYSTEM, sum_watch, det_watch
        CMDENT cmd_which_str, CAT_SYSTEM, sum_which, det_which
        CMDENT cmd_whoami_str, CAT_SYSTEM, sum_whoami, det_whoami
        CMDENT cmd_append_str, CAT_FILES, sum_append, det_append
        CMDENT cmd_cat_str, CAT_FILES, sum_cat, det_cat
        CMDENT cmd_cd_str, CAT_FILES, sum_cd, det_cd
        CMDENT cmd_chmod_str, CAT_FILES, sum_chmod, det_chmod
        CMDENT cmd_chown_str, CAT_FILES, sum_chown, det_chown
        CMDENT cmd_copy_str, CAT_FILES, sum_copy, det_copy
        CMDENT cmd_del_str, CAT_FILES, sum_del, det_del
        CMDENT cmd_df_str, CAT_FILES, sum_df, det_df
        CMDENT cmd_dir_str, CAT_FILES, sum_dir, det_dir
        CMDENT cmd_dirs_str, CAT_FILES, sum_dirs, det_dirs
        CMDENT cmd_format_str, CAT_FILES, sum_format, det_format
        CMDENT cmd_fsck_str, CAT_FILES, sum_fsck, det_fsck
        CMDENT cmd_head_str, CAT_FILES, sum_head, det_head
        CMDENT cmd_ln_str, CAT_FILES, sum_ln, det_ln
        CMDENT cmd_ls_str, CAT_FILES, sum_ls, det_ls
        CMDENT cmd_mkdir_str, CAT_FILES, sum_mkdir, det_mkdir
        CMDENT cmd_more_str, CAT_FILES, sum_more, det_more
        CMDENT cmd_move_str, CAT_FILES, sum_move, det_move
        CMDENT cmd_mv_str, CAT_FILES, sum_mv, det_mv
        CMDENT cmd_popd_str, CAT_FILES, sum_popd, det_popd
        CMDENT cmd_pushd_str, CAT_FILES, sum_pushd, det_pushd
        CMDENT cmd_pwd_str, CAT_FILES, sum_pwd, det_pwd
        CMDENT cmd_ren_str, CAT_FILES, sum_ren, det_ren
        CMDENT cmd_rm_str, CAT_FILES, sum_rm, det_rm
        CMDENT cmd_size_str, CAT_FILES, sum_size, det_size
        CMDENT cmd_stat_str, CAT_FILES, sum_stat, det_stat
        CMDENT cmd_tail_str, CAT_FILES, sum_tail, det_tail
        CMDENT cmd_touch_str, CAT_FILES, sum_touch, det_touch
        CMDENT cmd_type_str, CAT_FILES, sum_type, det_type
        CMDENT cmd_write_str, CAT_FILES, sum_write, det_write
        CMDENT cmd_diff_str, CAT_TEXT, sum_diff, det_diff
        CMDENT cmd_find_str, CAT_TEXT, sum_find, det_find
        CMDENT cmd_hex_str, CAT_TEXT, sum_hex, det_hex
        CMDENT cmd_rev_str, CAT_TEXT, sum_rev, det_rev
        CMDENT cmd_strings_str, CAT_TEXT, sum_strings, det_strings
        CMDENT cmd_tac_str, CAT_TEXT, sum_tac, det_tac
        CMDENT cmd_uniq_str, CAT_TEXT, sum_uniq, det_uniq
        CMDENT cmd_wc_str, CAT_TEXT, sum_wc, det_wc
        CMDENT cmd_bg_str, CAT_PROCESS, sum_bg, det_bg
        CMDENT cmd_fg_str, CAT_PROCESS, sum_fg, det_fg
        CMDENT cmd_jobs_str, CAT_PROCESS, sum_jobs, det_jobs
        CMDENT cmd_kill_str, CAT_PROCESS, sum_kill, det_kill
        CMDENT cmd_nice_str, CAT_PROCESS, sum_nice, det_nice
        CMDENT cmd_ps_str, CAT_PROCESS, sum_ps, det_ps
        CMDENT cmd_dotcmd_str, CAT_SCRIPT, sum_dotcmd, det_dotcmd
        CMDENT nm_bracket, CAT_SCRIPT, sum_lbracket, det_lbracket   ; dispatches cmd_lbracket_str
        CMDENT cmd_alias_str, CAT_SCRIPT, sum_alias, det_alias
        CMDENT cmd_batch_str, CAT_SCRIPT, sum_batch, det_batch
        CMDENT cmd_break_str, CAT_SCRIPT, sum_break, det_break
        CMDENT nm_case, CAT_SCRIPT, sum_case, det_case   ; dispatches cmd_case_str
        CMDENT cmd_continue_str, CAT_SCRIPT, sum_continue, det_continue
        CMDENT cmd_echo_str, CAT_SCRIPT, sum_echo, det_echo
        CMDENT cmd_enter_str, CAT_SCRIPT, sum_enter, det_enter
        CMDENT cmd_exit_str, CAT_SCRIPT, sum_exit, det_exit
        CMDENT cmd_export_str, CAT_SCRIPT, sum_export, det_export
        CMDENT nm_for, CAT_SCRIPT, sum_for, det_for   ; dispatches cmd_for_str
        CMDENT nm_function, CAT_SCRIPT, sum_function_def, det_function_def   ; dispatches cmd_function_def_str
        CMDENT nm_if, CAT_SCRIPT, sum_if, det_if   ; dispatches cmd_if_str
        CMDENT cmd_return_str, CAT_SCRIPT, sum_return, det_return
        CMDENT cmd_run_str, CAT_SCRIPT, sum_run, det_run
        CMDENT cmd_set_str, CAT_SCRIPT, sum_set, det_set
        CMDENT cmd_printf_str, CAT_SCRIPT, sum_printf, det_printf
        CMDENT cmd_read_str, CAT_SCRIPT, sum_read, det_read
        CMDENT cmd_shift_str, CAT_SCRIPT, sum_shift, det_shift
        CMDENT cmd_source_str, CAT_SCRIPT, sum_source, det_source
        CMDENT nm_test, CAT_SCRIPT, sum_test, det_test   ; dispatches cmd_test_str
        CMDENT cmd_unset_str, CAT_SCRIPT, sum_unset, det_unset
        CMDENT nm_while, CAT_SCRIPT, sum_while, det_while   ; dispatches cmd_while_str
        CMDENT cmd_beep_str, CAT_DISPLAY, sum_beep, det_beep
        CMDENT cmd_gui_str, CAT_DISPLAY, sum_gui, det_gui
        CMDENT cmd_mouse_str, CAT_DISPLAY, sum_mouse, det_mouse
        CMDENT cmd_scrsaver_str, CAT_DISPLAY, sum_scrsaver, det_scrsaver
        CMDENT cmd_arp_str, CAT_NETWORK, sum_arp, det_arp
        CMDENT cmd_dhcp_str, CAT_NETWORK, sum_dhcp, det_dhcp
        CMDENT cmd_ifconfig_str, CAT_NETWORK, sum_ifconfig, det_ifconfig
        CMDENT cmd_net_str, CAT_NETWORK, sum_net, det_net
        CMDENT cmd_ping_str, CAT_NETWORK, sum_ping, det_ping
        times CMD_ENTRY_SIZE db 0

; Category titles for the grouped `help` listing.
cmd_category_names:
        dd catname_system
        dd catname_files
        dd catname_text
        dd catname_process
        dd catname_script
        dd catname_display
        dd catname_network
catname_system: db "System", 0
catname_files: db "Files and directories", 0
catname_text: db "Text tools", 0
catname_process: db "Jobs and processes", 0
catname_script: db "Scripting and environment", 0
catname_display: db "Display and desktop", 0
catname_network: db "Network", 0

; Display names for keywords whose dispatcher string ends in a space.
nm_if: db "if", 0
nm_while: db "while", 0
nm_for: db "for", 0
nm_case: db "case", 0
nm_test: db "test", 0
nm_bracket: db "[", 0
nm_function: db "function", 0

; Summaries (one line) and details (`help <command>`).
sum_clear: db "Clear the screen", 0
det_clear:
        db "clear", 0x0A
        db "  Clear the screen and home the cursor.", 0x0A, 0
sum_cls: db "Alias for clear", 0
det_cls:
        db "cls", 0x0A
        db "  Same as clear.", 0x0A, 0
sum_color: db "Set text colours", 0
det_color:
        db "color FG [BG]", 0x0A
        db "  Set the default text colour (hex 0-F), and optionally the background.", 0x0A, 0
sum_date: db "Show the clock, or set it", 0
det_date:
        db "date [YYYY-MM-DD HH:MM:SS]", 0x0A
        db "  With no argument, print the current date and time.", 0x0A
        db "  With a full timestamp, set the CMOS clock.", 0x0A, 0
sum_disk: db "Disk geometry and size", 0
det_disk:
        db "disk", 0x0A
        db "  Show the ATA disk size and sector counts.", 0x0A, 0
sum_help: db "Show commands by category or one command", 0
det_help:
        db "help [COMMAND | -a]", 0x0A
        db "  Without an argument, lists built-ins grouped by category.", 0x0A
        db "  help <command> shows usage for one; help -a prints every name.", 0x0A, 0
sum_history: db "Recent command history", 0
det_history:
        db "history", 0x0A
        db "  List the last 128 commands. !N re-runs command N; !! re-runs the last one.", 0x0A, 0
sum_mem: db "Memory information", 0
det_mem:
        db "mem", 0x0A
        db "  Show free pages, megabytes free and tick count.", 0x0A, 0
sum_reboot: db "Restart the machine", 0
det_reboot:
        db "reboot", 0x0A
        db "  Flush the filesystem and reboot.", 0x0A, 0
sum_shutdown: db "Sync the disk and power off", 0
det_shutdown:
        db "shutdown", 0x0A
        db "  Flush the filesystem, then power the machine off.", 0x0A, 0
sum_sleep: db "Pause for N seconds", 0
det_sleep:
        db "sleep N", 0x0A
        db "  Sleep for N seconds; Ctrl+C aborts the wait.", 0x0A, 0
sum_time: db "Uptime, or time one command", 0
det_time:
        db "time [COMMAND]", 0x0A
        db "  Without an argument, print seconds since boot.", 0x0A
        db "  With a command, run it and report how long it took.", 0x0A, 0
sum_ver: db "Version, features and free memory", 0
det_ver:
        db "ver", 0x0A
        db "  Print the OS and shell version, the feature summary and free memory.", 0x0A, 0
sum_watch: db "Re-run a command on a timer", 0
det_watch:
        db "watch SECONDS COMMAND", 0x0A
        db "  Run COMMAND every SECONDS until a key is pressed.", 0x0A, 0
sum_which: db "Locate a command", 0
det_which:
        db "which NAME", 0x0A
        db "  Say whether NAME is a shell built-in, or where it is found on PATH.", 0x0A, 0
sum_whoami: db "Current user and uid", 0
det_whoami:
        db "whoami", 0x0A
        db "  Print the name and uid the shell is running as.", 0x0A, 0
sum_locks: db "Exercise the lock-order gate", 0
det_locks:
        db "locks", 0x0A
        db "  Run the spinlock self-test: take two locks in the wrong order and", 0x0A
        db "  require the gate to refuse, then in the right order and require it", 0x0A
        db "  to grant. Prints LOCK_SELFTEST_OK or LOCK_SELFTEST_FAIL.", 0x0A, 0
sum_fpu: db "Prove the per-task x87 and SSE image round-trips", 0
det_fpu:
        db "fpu", 0x0A
        db "  Save two different x87+SSE states, disturb the live registers and", 0x0A
        db "  load each back. Prints FPU_SELFTEST_OK, FPU_SELFTEST_FAIL, or", 0x0A
        db "  FPU_SELFTEST_SKIP on a CPU with no FXSR to carry.", 0x0A, 0
sum_cmod: db "Run the C-built kernel module's self-test", 0
det_cmod:
        db "cmod", 0x0A
        db "  Call the kernel module written in C: its header is checked, its init", 0x0A
        db "  is handed the host table, and it checks the freestanding runtime and", 0x0A
        db "  the C-to-assembly call before printing CMOD_OK. An image built", 0x0A
        db "  without a C toolchain says CMOD_ABSENT instead.", 0x0A, 0
sum_acpi: db "The machine's own description: CPUs, APICs, table of contents", 0
det_acpi:
        db "acpi", 0x0A
        db "  Find the RSDP, walk the RSDT/XSDT and read the MADT, then report how", 0x0A
        db "  many processors the firmware describes, where the local APIC is and", 0x0A
        db "  which I/O APICs exist. Read-only: nothing is programmed.", 0x0A, 0
sum_append: db "Append text to a file", 0
det_append:
        db "append FILE TEXT", 0x0A
        db "  Add one line of text to the end of FILE.", 0x0A, 0
sum_cat: db "Print a file or standard input", 0
det_cat:
        db "cat [-n] [FILE]", 0x0A
        db "  Print FILE, or whatever is piped in. -n numbers the lines.", 0x0A, 0
sum_cd: db "Change directory", 0
det_cd:
        db "cd [DIR]", 0x0A
        db "  Change to DIR (absolute or relative); cd .. goes up, cd - goes back.", 0x0A, 0
sum_chmod: db "Change permission bits", 0
det_chmod:
        db "chmod MODE FILE", 0x0A
        db "  MODE is octal (644) or rwxrwxrwx. Checked by SYS_ACCESS.", 0x0A, 0
sum_chown: db "Change file owner", 0
det_chown:
        db "chown UID[:GID] FILE", 0x0A
        db "  Set the owner, and optionally the group, of FILE.", 0x0A, 0
sum_copy: db "Copy a file", 0
det_copy:
        db "copy [-v] SRC DEST", 0x0A
        db "  Copy SRC to DEST; wildcards are allowed in SRC.", 0x0A, 0
sum_del: db "Delete files", 0
det_del:
        db "del [-v] FILE...", 0x0A
        db "  Delete files; wildcards (* ?) are allowed. -v names each one.", 0x0A, 0
sum_df: db "Filesystem usage", 0
det_df:
        db "df", 0x0A
        db "  Show total, used and free blocks and the file count.", 0x0A, 0
sum_dir: db "List a directory", 0
det_dir:
        db "dir [-l] [-s] [PATH]", 0x0A
        db "  -l long format (type, size, timestamp), -s largest first.", 0x0A, 0
sum_dirs: db "Show the directory stack", 0
det_dirs:
        db "dirs", 0x0A
        db "  List the directories saved by pushd.", 0x0A, 0
sum_format: db "Format the filesystem", 0
det_format:
        db "format", 0x0A
        db "  Erase and re-create the HBFS volume. Asks for confirmation.", 0x0A, 0
sum_fsck: db "Check the filesystem", 0
det_fsck:
        db "fsck", 0x0A
        db "  Walk the HBFS volume and report inconsistencies.", 0x0A, 0
sum_head: db "First lines of a file", 0
det_head:
        db "head [-n N] [FILE]", 0x0A
        db "  Print the first N lines (default 10).", 0x0A, 0
sum_ln: db "Create a link entry", 0
det_ln:
        db "ln TARGET LINK", 0x0A
        db "  Create a link entry named LINK that points at TARGET.", 0x0A, 0
sum_ls: db "Alias for dir", 0
det_ls:
        db "ls [-l] [-s] [PATH]", 0x0A
        db "  Same as dir.", 0x0A, 0
sum_mkdir: db "Create a subdirectory", 0
det_mkdir:
        db "mkdir NAME", 0x0A
        db "  Create a directory under the current one (16 levels max).", 0x0A, 0
sum_more: db "Page through a file", 0
det_more:
        db "more [-n N] FILE", 0x0A
        db "  Space advances a page, q quits, -n sets the page size.", 0x0A, 0
sum_move: db "Alias for ren", 0
det_move:
        db "move OLD NEW", 0x0A
        db "  Same as ren.", 0x0A, 0
sum_mv: db "Alias for ren", 0
det_mv:
        db "mv OLD NEW", 0x0A
        db "  Same as ren.", 0x0A, 0
sum_popd: db "Return to the saved directory", 0
det_popd:
        db "popd", 0x0A
        db "  Pop the directory stack and change back to it.", 0x0A, 0
sum_pushd: db "Save the directory and cd", 0
det_pushd:
        db "pushd DIR", 0x0A
        db "  Push the current directory and change to DIR.", 0x0A, 0
sum_pwd: db "Print working directory", 0
det_pwd:
        db "pwd", 0x0A
        db "  Print the full path of the current directory.", 0x0A, 0
sum_ren: db "Rename a file", 0
det_ren:
        db "ren OLD NEW", 0x0A
        db "  Rename OLD to NEW.", 0x0A, 0
sum_rm: db "Alias for del", 0
det_rm:
        db "rm [-v] FILE...", 0x0A
        db "  Same as del.", 0x0A, 0
sum_size: db "File size and type", 0
det_size:
        db "size FILE", 0x0A
        db "  Show size in bytes and blocks, plus the entry type.", 0x0A, 0
sum_stat: db "File metadata", 0
det_stat:
        db "stat FILE", 0x0A
        db "  Show size, blocks, type, mode, owner and timestamps.", 0x0A, 0
sum_tail: db "Last lines of a file", 0
det_tail:
        db "tail [-n N] [FILE]", 0x0A
        db "  Print the last N lines (default 10).", 0x0A, 0
sum_touch: db "Create an empty file", 0
det_touch:
        db "touch FILE", 0x0A
        db "  Create FILE if it does not exist.", 0x0A, 0
sum_type: db "Alias for cat", 0
det_type:
        db "type [-n] [FILE]", 0x0A
        db "  Same as cat.", 0x0A, 0
sum_write: db "Create or overwrite a file", 0
det_write:
        db "write FILE", 0x0A
        db "  Type the contents; a blank line ends the file.", 0x0A, 0
sum_diff: db "Compare two files", 0
det_diff:
        db "diff FILE1 FILE2", 0x0A
        db "  Show lines that differ (< left, > right).", 0x0A, 0
sum_find: db "Search file contents", 0
det_find:
        db "find [-i] [-c] PATTERN FILE", 0x0A
        db "  -i ignores case, -c prints only the number of matching lines.", 0x0A
        db "  For file *names*, use dir/tree with a wildcard.", 0x0A, 0
sum_hex: db "Hexdump a file", 0
det_hex:
        db "hex [-n N] FILE", 0x0A
        db "  Dump bytes as hex; -n limits how many.", 0x0A, 0
sum_rev: db "Reverse each line", 0
det_rev:
        db "rev [FILE]", 0x0A
        db "  Reverse the characters of every line.", 0x0A, 0
sum_strings: db "Extract printable strings", 0
det_strings:
        db "strings FILE", 0x0A
        db "  Print runs of 4 or more printable characters.", 0x0A, 0
sum_tac: db "Reverse the line order", 0
det_tac:
        db "tac [FILE]", 0x0A
        db "  Print the file from the last line to the first.", 0x0A, 0
sum_uniq: db "Drop adjacent duplicates", 0
det_uniq:
        db "uniq [-c] [-d] [FILE]", 0x0A
        db "  -c prefixes counts, -d shows only duplicated lines.", 0x0A, 0
sum_wc: db "Count lines, words and bytes", 0
det_wc:
        db "wc [-l] [-w] [-c] [FILE]", 0x0A
        db "  Without flags, print all three counts.", 0x0A, 0
sum_bg: db "Resume a job in the background", 0
det_bg:
        db "bg PID", 0x0A
        db "  Resume a stopped task without taking the console.", 0x0A, 0
sum_fg: db "Resume a job in the foreground", 0
det_fg:
        db "fg PID", 0x0A
        db "  Resume a stopped task on the console.", 0x0A, 0
sum_jobs: db "List background jobs", 0
det_jobs:
        db "jobs", 0x0A
        db "  List tasks started with &.", 0x0A, 0
sum_kill: db "Send a signal to a task", 0
det_kill:
        db "kill PID [SIGNAL]", 0x0A
        db "  Default signal is TERM; names or numbers are accepted.", 0x0A, 0
sum_nice: db "Run a command at a priority", 0
det_nice:
        db "nice PRIORITY COMMAND", 0x0A
        db "  Priority 0=HIGH, 1=NORMAL, 2=LOW, 3=IDLE.", 0x0A, 0
sum_ps: db "List tasks", 0
det_ps:
        db "ps", 0x0A
        db "  List every task with state, priority and name.", 0x0A, 0
sum_dotcmd: db "Alias for source", 0
det_dotcmd:
        db ". FILE", 0x0A
        db "  Same as source.", 0x0A, 0
sum_lbracket: db "Alias for test", 0
det_lbracket:
        db "[ EXPR ]", 0x0A
        db "  Same as test, but requires the closing ].", 0x0A, 0
sum_alias: db "Define or list aliases", 0
det_alias:
        db "alias [NAME COMMAND]", 0x0A
        db "  Without arguments, list aliases.", 0x0A, 0
sum_batch: db "Run a script file", 0
det_batch:
        db "batch FILE", 0x0A
        db "  Run FILE as a batch script (labels, goto, REM, if).", 0x0A, 0
sum_break: db "Leave the innermost loop", 0
det_break:
        db "break", 0x0A
        db "  Exit the enclosing for, while or until loop.", 0x0A, 0
sum_case: db "Match a word against patterns", 0
det_case:
        db "case WORD in PATTERN) BODY;; esac", 0x0A
        db "  Each arm ends with ;;.", 0x0A, 0
sum_continue: db "Next loop iteration", 0
det_continue:
        db "continue", 0x0A
        db "  Skip the rest of the loop body and re-test the condition.", 0x0A, 0
sum_echo: db "Print text", 0
det_echo:
        db "echo [-n] TEXT", 0x0A
        db "  Expand $VAR and print TEXT. -n omits the trailing newline.", 0x0A, 0
sum_enter: db "Enter hex bytes as a program", 0
det_enter:
        db "enter", 0x0A
        db "  Type hex bytes; they are saved and run as a program.", 0x0A, 0
sum_export: db "Set NAME=VALUE", 0
det_export:
        db "export NAME=VALUE", 0x0A
        db "  Set and export a variable in one step.", 0x0A, 0
sum_for: db "Loop over words", 0
det_for:
        db "for VAR in WORD...; do BODY; done", 0x0A
        db "  Sets VAR to each word in turn.", 0x0A, 0
sum_function_def: db "Define a shell function", 0
det_function_def:
        db "function NAME { BODY }", 0x0A
        db "  Or function NAME() { BODY }. Call it by name afterwards.", 0x0A, 0
sum_if: db "Run commands when a test passes", 0
det_if:
        db "if COND; then BODY; fi", 0x0A
        db "  elif and else are supported. Keep it on one line.", 0x0A, 0
sum_return: db "Leave a function or script", 0
det_return:
        db "return [N]", 0x0A
        db "  Return from the current function or sourced file with status N.", 0x0A, 0
sum_run: db "Run a program file", 0
det_run:
        db "run FILE", 0x0A
        db "  Execute FILE as a ring-3 program.", 0x0A, 0
sum_set: db "Set an environment variable", 0
det_set:
        db "set NAME VALUE", 0x0A
        db "  Store VALUE under NAME for $NAME expansion.", 0x0A, 0
sum_source: db "Run a script in this shell", 0
det_source:
        db "source FILE", 0x0A
        db "  Run FILE in the current shell, so variables and functions persist.", 0x0A, 0
sum_test: db "Evaluate a test expression", 0
det_test:
        db "test EXPR", 0x0A
        db "  File tests (-f -d -e), string tests (= !=) and numeric tests (-eq -lt ...).", 0x0A, 0
sum_unset: db "Remove an environment variable", 0
det_unset:
        db "unset NAME", 0x0A
        db "  Delete NAME from the environment.", 0x0A, 0
sum_while: db "Loop while a test passes", 0
det_while:
        db "while COND; do BODY; done", 0x0A
        db "  Re-tests COND before each iteration.", 0x0A, 0
sum_beep: db "Beep the PC speaker", 0
det_beep:
        db "beep", 0x0A
        db "  Sound the PC speaker briefly.", 0x0A, 0
sum_gui: db "Enter the graphical desktop", 0
det_gui:
        db "burrows", 0x0A
        db "  Start the Burrows desktop. Ctrl+C returns to the text shell.", 0x0A, 0
sum_mouse: db "Mouse status", 0
det_mouse:
        db "mouse", 0x0A
        db "  Show the pointer position and button state.", 0x0A, 0
sum_scrsaver: db "Start the screensaver", 0
det_scrsaver:
        db "scrsaver", 0x0A
        db "  Run the screensaver until a key is pressed.", 0x0A, 0
sum_arp: db "ARP table", 0
det_arp:
        db "arp", 0x0A
        db "  Show learned IPv4 to MAC address mappings.", 0x0A, 0
sum_dhcp: db "Request a DHCP lease", 0
det_dhcp:
        db "dhcp", 0x0A
        db "  Ask for an address, gateway and DNS servers.", 0x0A, 0
sum_ifconfig: db "Interface configuration", 0
det_ifconfig:
        db "ifconfig", 0x0A
        db "  Show the configuration of the network interfaces.", 0x0A, 0
sum_net: db "Interface status", 0
det_net:
        db "net", 0x0A
        db "  Show link status and addresses for each interface.", 0x0A, 0
sum_ping: db "ICMP echo", 0
det_ping:
        db "ping HOST", 0x0A
        db "  Send echo requests and report replies.", 0x0A, 0


sum_exit: db "Leave the current script or function", 0
det_exit:
        db "exit [STATUS]", 0x0A
        db "  Stop the running script or function and set its status. At the", 0x0A
        db "  prompt, use shutdown or reboot to leave the system.", 0x0A, 0
sum_printf: db "Formatted output", 0
det_printf:
        db "printf FORMAT [ARG...]", 0x0A
        db "  Conversions: %s %d %u %x %c %%; escapes: \n \t \\ \r.", 0x0A
        db "  Variables and positional parameters are expanded first.", 0x0A, 0
sum_read: db "Read a line into a variable", 0
det_read:
        db "read [VAR]", 0x0A
        db "  Read one line from the keyboard into VAR (default REPLY).", 0x0A, 0
sum_shift: db "Drop positional parameters", 0
det_shift:
        db "shift [N]", 0x0A
        db "  Remove the first N parameters (default 1) from $1..$9.", 0x0A, 0
; Messages used by help and by the unknown-command path.
help_all_word: db "all", 0
read_default_name: db "REPLY", 0
msg_exit_hint: db "  exit ends a script or function; use shutdown or reboot to leave Mellivora.", 0x0A, 0
help_title: db "HB Lair v13.0 commands:", 0x0A, 0
msg_did_you_mean: db "  Did you mean '", 0
msg_quote_end: db "'?", 0x0A, 0
msg_unknown_pre: db "Unknown command '", 0
msg_unknown_post: db "'.", 0x0A, 0
msg_try_help: db "  Type 'help' for the command list, or 'dir /bin' to browse programs.", 0x0A, 0
msg_help_unknown_pre: db "  No built-in command named '", 0
msg_help_unknown_post: db "'.", 0x0A, 0
msg_help_usage: db "  Usage: help [COMMAND | -a]", 0x0A, 0
msg_help_hint: db "  help <command> for one command; PgUp/PgDn scrolls this output.", 0x0A, 0

%endif ; CMDTABLE_INC
