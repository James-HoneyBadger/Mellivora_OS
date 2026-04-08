;=======================================================================
; COMMAND SHELL
;=======================================================================

shell_main:
        mov esp, KERNEL_STACK
        mov byte [vga_color], COLOR_DEFAULT

        ; Set default PATH
        mov esi, default_path
        call env_set_str

.prompt:
        ; Print prompt with full current directory path
        mov byte [vga_color], COLOR_PROMPT
        mov esi, shell_prompt_pre
        call vga_print
        ; Build full CWD path
        mov edi, path_search_buf        ; reuse temp buffer
        call build_cwd_path
        mov esi, path_search_buf
        call vga_print
        mov esi, shell_prompt_post
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT

        ; Reset history browsing position
        mov eax, [hist_count]
        mov [hist_browse], eax

        ; Read command line
        mov edi, line_buffer
        xor ecx, ecx           ; Character count

.read_loop:
        call kb_getchar

        cmp al, 0x0D            ; Enter?
        je .execute
        cmp al, 0x0A            ; Linefeed?
        je .execute
        cmp al, 0x08            ; Backspace?
        je .backspace
        cmp al, 0x7F            ; Delete (also backspace on some terms)?
        je .backspace
        cmp al, KEY_UP          ; Up arrow - history prev
        je .hist_up
        cmp al, KEY_DOWN        ; Down arrow - history next
        je .hist_down
        cmp al, TAB_KEY         ; Tab - auto-complete
        je .tab_complete
        cmp al, CTRL_C_CODE     ; Ctrl+C - cancel
        je .ctrl_c

        ; Store character if buffer not full
        cmp ecx, LINE_BUFFER_SIZE - 1
        jge .read_loop
        stosb
        inc ecx
        ; Echo character
        call vga_putchar
        jmp .read_loop

.backspace:
        or ecx, ecx
        jz .read_loop
        dec edi
        dec ecx
        mov al, 0x08
        call vga_putchar
        jmp .read_loop

.hist_up:
        cmp dword [hist_browse], 0
        je .read_loop           ; No more history
        dec dword [hist_browse]
        jmp .hist_recall

.hist_down:
        mov eax, [hist_browse]
        cmp eax, [hist_count]
        jge .read_loop          ; Already at end
        inc dword [hist_browse]
        ; If at end, clear line
        mov eax, [hist_browse]
        cmp eax, [hist_count]
        je .hist_clear_line
        jmp .hist_recall

.tab_complete:
        call shell_tab_complete
        lea edi, [line_buffer + ecx]
        jmp .read_loop

.ctrl_c:
        ; Print ^C and cancel current input
        push esi
        mov esi, msg_ctrl_c
        call vga_print
        pop esi
        mov edi, line_buffer
        xor ecx, ecx
        jmp .prompt

.hist_clear_line:
        ; Erase current input on screen
        call .erase_input
        mov edi, line_buffer
        xor ecx, ecx
        jmp .read_loop

.hist_recall:
        ; Erase current input on screen
        call .erase_input
        ; Copy history entry to line_buffer
        mov eax, [hist_browse]
        shl eax, 8              ; * 256 (HIST_ENTRY_SIZE)
        lea esi, [hist_buf + eax]
        mov edi, line_buffer
        xor ecx, ecx
.hist_copy:
        lodsb
        cmp al, 0
        je .hist_copy_done
        stosb
        inc ecx
        push ecx
        call vga_putchar        ; Echo the char
        pop ecx
        jmp .hist_copy
.hist_copy_done:
        jmp .read_loop

.erase_input:
        ; Backspace over ECX characters
        push ecx
.erase_loop:
        cmp ecx, 0
        je .erase_done
        push ecx
        mov al, 0x08
        call vga_putchar
        mov al, ' '
        call vga_putchar
        mov al, 0x08
        call vga_putchar
        pop ecx
        dec ecx
        jmp .erase_loop
.erase_done:
        pop ecx
        ret

.execute:
        mov byte [edi], 0      ; Null-terminate input
        mov al, 0x0A
        call vga_putchar

        ; Skip empty lines
        cmp ecx, 0
        je .prompt

        ; Save to history
        call shell_save_history

        ; Parse and execute command
        mov esi, line_buffer
        call shell_parse_cmd
        jmp .prompt

;---------------------------------------
; Save current line_buffer to history ring
;---------------------------------------
HIST_MAX        equ 8
HIST_ENTRY_SIZE equ 256

shell_save_history:
        pushad
        ; Shift history up if full
        mov eax, [hist_count]
        cmp eax, HIST_MAX
        jl .hist_not_full
        ; Shift entries 1..7 down to 0..6
        mov esi, hist_buf + HIST_ENTRY_SIZE
        mov edi, hist_buf
        mov ecx, (HIST_MAX - 1) * HIST_ENTRY_SIZE / 4
        rep movsd
        dec dword [hist_count]
.hist_not_full:
        ; Copy line_buffer to hist_buf[hist_count]
        mov eax, [hist_count]
        shl eax, 8              ; * 256
        lea edi, [hist_buf + eax]
        mov esi, line_buffer
        mov ecx, HIST_ENTRY_SIZE - 1
.copy_hist:
        lodsb
        stosb
        cmp al, 0
        je .copy_done
        loop .copy_hist
        mov byte [edi], 0       ; Ensure null termination
.copy_done:
        inc dword [hist_count]
        popad
        ret

;---------------------------------------
; Parse and dispatch command
; ESI = command line
;---------------------------------------
shell_parse_cmd:
        ; Skip leading spaces
        call skip_spaces

        call shell_prepare_redirection
        jc .done

        mov esi, line_buffer
        call skip_spaces

        cmp byte [esi], 0
        je .done

        ; --- Alias expansion ---
        ; Extract first word, check alias table
        push esi
        mov ecx, [alias_count]
        cmp ecx, 0
        je .alias_skip
        xor ebx, ebx
        mov edx, alias_table
.alias_lookup:
        cmp ebx, ecx
        jge .alias_skip
        ; Check if command starts with alias name
        push esi
        mov edi, edx
        call str_starts_with
        pop esi
        jc .alias_found
        add edx, ALIAS_ENTRY_SIZE
        inc ebx
        jmp .alias_lookup
.alias_found:
        ; Copy alias command into alias_expand_buf, then append rest of args
        push esi          ; ESI now points past the alias name
        lea esi, [edx + ALIAS_NAME_LEN]
        mov edi, alias_expand_buf
        mov ecx, 255
.aexp_copy_cmd:
        lodsb
        cmp al, 0
        je .aexp_cmd_done
        stosb
        dec ecx
        jnz .aexp_copy_cmd
.aexp_cmd_done:
        pop esi
        ; Append remaining arguments
.aexp_copy_rest:
        lodsb
        stosb
        cmp al, 0
        je .aexp_done
        dec ecx
        jnz .aexp_copy_rest
.aexp_done:
        mov byte [edi], 0
        pop esi            ; discard saved ESI
        mov esi, alias_expand_buf
        jmp shell_parse_cmd  ; Re-parse with expanded command
.alias_skip:
        pop esi

        ; Compare against known commands
        mov edi, cmd_help_str
        call str_starts_with
        jc .cmd_help

        mov edi, cmd_ver_str
        call str_starts_with
        jc .cmd_ver

        mov edi, cmd_clear_str
        call str_starts_with
        jc .cmd_clear

        mov edi, cmd_dir_str
        call str_starts_with
        jc .cmd_dir

        mov edi, cmd_ls_str
        call str_starts_with
        jc .cmd_dir

        mov edi, cmd_del_str
        call str_starts_with
        jc .cmd_del

        mov edi, cmd_rm_str
        call str_starts_with
        jc .cmd_del

        mov edi, cmd_format_str
        call str_starts_with
        jc .cmd_format

        mov edi, cmd_cat_str
        call str_starts_with
        jc .cmd_cat

        mov edi, cmd_write_str
        call str_starts_with
        jc .cmd_write

        mov edi, cmd_hex_str
        call str_starts_with
        jc .cmd_hexdump

        mov edi, cmd_mem_str
        call str_starts_with
        jc .cmd_mem

        mov edi, cmd_time_str
        call str_starts_with
        jc .cmd_time

        mov edi, cmd_disk_str
        call str_starts_with
        jc .cmd_diskinfo

        mov edi, cmd_run_str
        call str_starts_with
        jc .cmd_run

        mov edi, cmd_enter_str
        call str_starts_with
        jc .cmd_enter

        mov edi, cmd_copy_str
        call str_starts_with
        jc .cmd_copy

        mov edi, cmd_ren_str
        call str_starts_with
        jc .cmd_rename

        mov edi, cmd_mv_str
        call str_starts_with
        jc .cmd_rename

        mov edi, cmd_move_str
        call str_starts_with
        jc .cmd_rename

        mov edi, cmd_df_str
        call str_starts_with
        jc .cmd_df

        mov edi, cmd_more_str
        call str_starts_with
        jc .cmd_more

        mov edi, cmd_echo_str
        call str_starts_with
        jc .cmd_echo

        mov edi, cmd_wc_str
        call str_starts_with
        jc .cmd_wc

        mov edi, cmd_find_str
        call str_starts_with
        jc .cmd_find

        mov edi, cmd_append_str
        call str_starts_with
        jc .cmd_append

        mov edi, cmd_date_str
        call str_starts_with
        jc .cmd_date

        mov edi, cmd_beep_str
        call str_starts_with
        jc .cmd_beep

        mov edi, cmd_batch_str
        call str_starts_with
        jc .cmd_batch

        mov edi, cmd_mkdir_str
        call str_starts_with
        jc .cmd_mkdir

        mov edi, cmd_cd_str
        call str_starts_with
        jc .cmd_cd

        mov edi, cmd_pwd_str
        call str_starts_with
        jc .cmd_pwd

        mov edi, cmd_touch_str
        call str_starts_with
        jc .cmd_touch

        mov edi, cmd_set_str
        call str_starts_with
        jc .cmd_set

        mov edi, cmd_unset_str
        call str_starts_with
        jc .cmd_unset

        mov edi, cmd_shutdown_str
        call str_starts_with
        jc .cmd_shutdown

        mov edi, cmd_cls_str
        call str_starts_with
        jc .cmd_clear

        mov edi, cmd_head_str
        call str_starts_with
        jc .cmd_head

        mov edi, cmd_tail_str
        call str_starts_with
        jc .cmd_tail

        mov edi, cmd_type_str
        call str_starts_with
        jc .cmd_cat

        mov edi, cmd_diff_str
        call str_starts_with
        jc .cmd_diff

        mov edi, cmd_uniq_str
        call str_starts_with
        jc .cmd_uniq

        mov edi, cmd_rev_str
        call str_starts_with
        jc .cmd_rev

        mov edi, cmd_tac_str
        call str_starts_with
        jc .cmd_tac

        mov edi, cmd_alias_str
        call str_starts_with
        jc .cmd_alias

        mov edi, cmd_history_str
        call str_starts_with
        jc .cmd_history

        mov edi, cmd_which_str
        call str_starts_with
        jc .cmd_which

        mov edi, cmd_sleep_str
        call str_starts_with
        jc .cmd_sleep

        mov edi, cmd_color_str
        call str_starts_with
        jc .cmd_color

        mov edi, cmd_size_str
        call str_starts_with
        jc .cmd_size

        mov edi, cmd_strings_str
        call str_starts_with
        jc .cmd_strings

        ; Unknown command - try to execute as program
        mov esi, line_buffer
        call cmd_exec_program
        jc .unknown

.done:
        call shell_finish_redirection
        ret

.unknown:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_unknown_cmd
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        jmp .done

;---- Command implementations ----

.cmd_help:
        mov esi, line_buffer
        mov edi, cmd_help_str
        call str_starts_with
        jnc .cmd_help_full
        cmp byte [esi], 0
        je .cmd_help_full

        mov edi, filename_buf
        call copy_word

        mov esi, filename_buf
        mov edi, cmd_help_str
        call str_compare
        jz .cmd_help_help

        mov edi, cmd_ver_str
        call str_compare
        jz .cmd_help_ver

        mov edi, cmd_clear_str
        call str_compare
        jz .cmd_help_clear
        mov edi, cmd_cls_str
        call str_compare
        jz .cmd_help_clear

        mov edi, cmd_dir_str
        call str_compare
        jz .cmd_help_dir
        mov edi, cmd_ls_str
        call str_compare
        jz .cmd_help_dir

        mov edi, cmd_cat_str
        call str_compare
        jz .cmd_help_cat
        mov edi, cmd_type_str
        call str_compare
        jz .cmd_help_cat

        mov edi, cmd_write_str
        call str_compare
        jz .cmd_help_write
        mov edi, cmd_append_str
        call str_compare
        jz .cmd_help_append

        mov edi, cmd_run_str
        call str_compare
        jz .cmd_help_run

        mov edi, cmd_echo_str
        call str_compare
        jz .cmd_help_echo

        mov edi, cmd_cd_str
        call str_compare
        jz .cmd_help_cd
        mov edi, cmd_pwd_str
        call str_compare
        jz .cmd_help_pwd
        mov edi, cmd_mkdir_str
        call str_compare
        jz .cmd_help_mkdir

        mov edi, cmd_set_str
        call str_compare
        jz .cmd_help_set
        mov edi, cmd_unset_str
        call str_compare
        jz .cmd_help_unset

        mov edi, cmd_history_str
        call str_compare
        jz .cmd_help_history
        mov edi, cmd_which_str
        call str_compare
        jz .cmd_help_which

        mov edi, cmd_color_str
        call str_compare
        jz .cmd_help_color
        mov edi, cmd_size_str
        call str_compare
        jz .cmd_help_size
        mov edi, cmd_strings_str
        call str_compare
        jz .cmd_help_strings

        mov esi, msg_help_no_detail
        call vga_print

.cmd_help_full:
        mov esi, help_text
        call vga_print
        ret

.cmd_help_help:
        mov esi, help_help_help
        call vga_print
        ret
.cmd_help_ver:
        mov esi, help_help_ver
        call vga_print
        ret
.cmd_help_clear:
        mov esi, help_help_clear
        call vga_print
        ret
.cmd_help_dir:
        mov esi, help_help_dir
        call vga_print
        ret
.cmd_help_cat:
        mov esi, help_help_cat
        call vga_print
        ret
.cmd_help_write:
        mov esi, help_help_write
        call vga_print
        ret
.cmd_help_append:
        mov esi, help_help_append
        call vga_print
        ret
.cmd_help_run:
        mov esi, help_help_run
        call vga_print
        ret
.cmd_help_echo:
        mov esi, help_help_echo
        call vga_print
        ret
.cmd_help_cd:
        mov esi, help_help_cd
        call vga_print
        ret
.cmd_help_pwd:
        mov esi, help_help_pwd
        call vga_print
        ret
.cmd_help_mkdir:
        mov esi, help_help_mkdir
        call vga_print
        ret
.cmd_help_set:
        mov esi, help_help_set
        call vga_print
        ret
.cmd_help_unset:
        mov esi, help_help_unset
        call vga_print
        ret
.cmd_help_history:
        mov esi, help_help_history
        call vga_print
        ret
.cmd_help_which:
        mov esi, help_help_which
        call vga_print
        ret
.cmd_help_color:
        mov esi, help_help_color
        call vga_print
        ret
.cmd_help_size:
        mov esi, help_help_size
        call vga_print
        ret
.cmd_help_strings:
        mov esi, help_help_strings
        call vga_print
        ret

.cmd_ver:
        mov byte [vga_color], COLOR_INFO
        mov esi, version_text
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT

        ; Print memory info
        mov esi, msg_mem_total
        call vga_print
        mov eax, [total_free_pages]
        shl eax, 2              ; Pages * 4 = KB
        push eax
        shr eax, 10             ; KB / 1024 = MB
        call vga_print_dec
        mov esi, msg_mb
        call vga_print
        pop eax
        ret

.cmd_clear:
        call vga_clear
        ret

.cmd_dir:
        call parse_flags
        call cmd_list_dir
        ret

.cmd_del:
        call skip_spaces
        call parse_flags
        call cmd_delete_file
        ret

.cmd_format:
        mov esi, msg_format_confirm
        call vga_print
        call kb_getchar
        or al, 0x20             ; Convert to lowercase
        cmp al, 'y'
        jne .format_cancel
        call hbfs_format
        ret
.format_cancel:
        mov esi, msg_cancelled
        call vga_print
        ret

.cmd_cat:
        call skip_spaces
        call parse_flags
        call cmd_cat_file
        ret

.cmd_write:
        call skip_spaces
        call cmd_write_file
        ret

.cmd_hexdump:
        call skip_spaces
        call parse_flags
        call cmd_hexdump_file
        ret

.cmd_mem:
        call cmd_show_memory
        ret

.cmd_time:
        mov esi, msg_uptime
        call vga_print
        mov eax, [tick_count]
        xor edx, edx
        mov ebx, PIT_HZ
        div ebx                 ; EAX = seconds
        call vga_print_dec
        mov esi, msg_seconds
        call vga_print
        ret

.cmd_diskinfo:
        call cmd_show_disk
        ret

.cmd_run:
        call skip_spaces
        call cmd_exec_program
        ret

.cmd_enter:
        call cmd_enter_hex
        ret

.cmd_copy:
        call skip_spaces
        call parse_flags
        call cmd_copy_file
        ret

.cmd_rename:
        call skip_spaces
        call cmd_rename_file
        ret

.cmd_df:
        call cmd_df_info
        ret

.cmd_more:
        call skip_spaces
        call parse_flags
        call cmd_more_file
        ret

.cmd_echo:
        call parse_flags
        call cmd_echo
        ret

.cmd_wc:
        call skip_spaces
        call parse_flags
        call cmd_wc_file
        ret

.cmd_find:
        call skip_spaces
        call parse_flags
        call cmd_find_file
        ret

.cmd_append:
        call skip_spaces
        call cmd_append_file
        ret

.cmd_date:
        call cmd_date_show
        ret

.cmd_beep:
        call cmd_beep
        ret

.cmd_batch:
        call skip_spaces
        call cmd_exec_batch
        ret

.cmd_mkdir:
        call skip_spaces
        call cmd_mkdir
        ret

.cmd_cd:
        call skip_spaces
        call cmd_cd
        ret

.cmd_pwd:
        call cmd_pwd
        ret

.cmd_touch:
        call skip_spaces
        call cmd_touch_file
        ret

.cmd_set:
        call cmd_set_env
        ret

.cmd_unset:
        call skip_spaces
        call cmd_unset_env
        ret

.cmd_shutdown:
        call cmd_shutdown
        ret

.cmd_head:
        call skip_spaces
        call parse_flags
        call cmd_head_file
        ret

.cmd_tail:
        call skip_spaces
        call parse_flags
        call cmd_tail_file
        ret

.cmd_diff:
        call skip_spaces
        call cmd_diff_files
        ret

.cmd_uniq:
        call skip_spaces
        call parse_flags
        call cmd_uniq_file
        ret

.cmd_rev:
        call skip_spaces
        call cmd_rev_file
        ret

.cmd_tac:
        call skip_spaces
        call cmd_tac_file
        ret

.cmd_alias:
        call skip_spaces
        call cmd_alias
        ret

.cmd_history:
        call cmd_history
        ret

.cmd_which:
        call skip_spaces
        call cmd_which
        ret

.cmd_sleep:
        call skip_spaces
        call cmd_sleep
        ret

.cmd_color:
        call skip_spaces
        call cmd_color
        ret

.cmd_size:
        call skip_spaces
        call cmd_size_file
        ret

.cmd_strings:
        call skip_spaces
        call parse_flags
        call cmd_strings_file
        ret

;---------------------------------------
; Command: diff - Compare two files line by line
; Usage: diff FILE1 FILE2
;---------------------------------------
cmd_diff_files:
        pushad
        ; Parse two filenames
        mov edi, filename_buf
        call copy_word
        call skip_spaces
        cmp byte [filename_buf], 0
        je .diff_usage
        mov edi, filename_buf2
        call copy_word
        cmp byte [filename_buf2], 0
        je .diff_usage

        ; Read file1 into PROGRAM_BASE
        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .diff_not_found1
        mov [.diff_size1], ecx

        ; Read file2 into PROGRAM_BASE + 64K
        mov esi, filename_buf2
        mov edi, PROGRAM_BASE + 0x10000
        call hbfs_read_file
        jc .diff_not_found2
        mov [.diff_size2], ecx

        ; Null-terminate both
        mov edi, PROGRAM_BASE
        add edi, [.diff_size1]
        mov byte [edi], 0
        mov edi, PROGRAM_BASE + 0x10000
        add edi, [.diff_size2]
        mov byte [edi], 0

        ; Print headers
        mov byte [vga_color], COLOR_INFO
        mov esi, msg_diff_header1
        call vga_print
        mov esi, filename_buf
        call vga_print
        call vga_newline
        mov esi, msg_diff_header2
        call vga_print
        mov esi, filename_buf2
        call vga_print
        call vga_newline
        mov byte [vga_color], COLOR_DEFAULT

        ; Compare line by line
        mov esi, PROGRAM_BASE
        mov edi, PROGRAM_BASE + 0x10000
        mov dword [.diff_line], 1
        mov dword [.diff_diffs], 0

.diff_loop:
        cmp byte [esi], 0
        jne .diff_not_end
        cmp byte [edi], 0
        jne .diff_f2_extra
        jmp .diff_done

.diff_not_end:
        cmp byte [edi], 0
        je .diff_f1_extra

        ; Compare current lines
        push esi
        push edi
        call .diff_cmp_line
        jc .diff_lines_same

        ; Lines differ
        inc dword [.diff_diffs]
        mov byte [vga_color], COLOR_INFO
        mov esi, msg_diff_line
        call vga_print
        mov eax, [.diff_line]
        call vga_print_dec
        call vga_newline
        ; Print file1 line
        mov byte [vga_color], COLOR_ERROR
        pop edi
        pop esi
        push esi
        push edi
        mov al, '<'
        call vga_putchar
        mov al, ' '
        call vga_putchar
        mov ebx, esi
.diff_p1:
        mov al, [ebx]
        cmp al, 0
        je .diff_p1e
        cmp al, 0x0A
        je .diff_p1e
        call vga_putchar
        inc ebx
        jmp .diff_p1
.diff_p1e:
        call vga_newline
        ; Print file2 line
        mov byte [vga_color], COLOR_SUCCESS
        pop edi
        pop esi
        push esi
        push edi
        mov al, '>'
        call vga_putchar
        mov al, ' '
        call vga_putchar
        mov ebx, edi
.diff_p2:
        mov al, [ebx]
        cmp al, 0
        je .diff_p2e
        cmp al, 0x0A
        je .diff_p2e
        call vga_putchar
        inc ebx
        jmp .diff_p2
.diff_p2e:
        call vga_newline
        mov byte [vga_color], COLOR_DEFAULT

.diff_lines_same:
        pop edi
        pop esi
        ; Advance both to next line
.diff_skip1:
        cmp byte [esi], 0
        je .diff_sk1d
        cmp byte [esi], 0x0A
        je .diff_sk1n
        inc esi
        jmp .diff_skip1
.diff_sk1n:
        inc esi
.diff_sk1d:
.diff_skip2:
        cmp byte [edi], 0
        je .diff_sk2d
        cmp byte [edi], 0x0A
        je .diff_sk2n
        inc edi
        jmp .diff_skip2
.diff_sk2n:
        inc edi
.diff_sk2d:
        inc dword [.diff_line]
        jmp .diff_loop

.diff_f1_extra:
        inc dword [.diff_diffs]
        mov byte [vga_color], COLOR_ERROR
.diff_f1e_loop:
        mov al, [esi]
        cmp al, 0
        je .diff_done
        call vga_putchar
        inc esi
        jmp .diff_f1e_loop

.diff_f2_extra:
        inc dword [.diff_diffs]
        mov byte [vga_color], COLOR_SUCCESS
.diff_f2e_loop:
        mov al, [edi]
        cmp al, 0
        je .diff_done
        call vga_putchar
        inc edi
        jmp .diff_f2e_loop

.diff_done:
        mov byte [vga_color], COLOR_DEFAULT
        cmp dword [.diff_diffs], 0
        jne .diff_ret
        mov esi, msg_diff_same
        call vga_print
.diff_ret:
        popad
        ret

.diff_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_2files
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.diff_not_found1:
        mov byte [vga_color], COLOR_ERROR
        mov esi, filename_buf
        call vga_print
        mov esi, msg_not_found_w
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.diff_not_found2:
        mov byte [vga_color], COLOR_ERROR
        mov esi, filename_buf2
        call vga_print
        mov esi, msg_not_found_w
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

; Compare line at ESI vs line at EDI. CF=1 if same.
.diff_cmp_line:
        push esi
        push edi
.dcl_loop:
        mov al, [esi]
        mov ah, [edi]
        cmp al, 0x0A
        je .dcl_end1
        cmp al, 0
        je .dcl_end1
        cmp ah, 0x0A
        je .dcl_diff
        cmp ah, 0
        je .dcl_diff
        cmp al, ah
        jne .dcl_diff
        inc esi
        inc edi
        jmp .dcl_loop
.dcl_end1:
        cmp ah, 0x0A
        je .dcl_same
        cmp ah, 0
        je .dcl_same
.dcl_diff:
        pop edi
        pop esi
        clc
        ret
.dcl_same:
        pop edi
        pop esi
        stc
        ret

.diff_size1:  dd 0
.diff_size2:  dd 0
.diff_line:   dd 0
.diff_diffs:  dd 0

;---------------------------------------
; Command: uniq - Remove adjacent duplicate lines
; Flags: -c  prefix lines with count
;        -d  only show duplicates
;---------------------------------------
cmd_uniq_file:
        pushad
        mov edi, filename_buf
        call copy_word
        cmp byte [filename_buf], 0
        je .uniq_usage

        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .uniq_nf
        mov edi, PROGRAM_BASE
        add edi, ecx
        mov byte [edi], 0

        mov esi, PROGRAM_BASE
        mov dword [.uniq_prev], 0
        mov dword [.uniq_count], 1

.uniq_loop:
        cmp byte [esi], 0
        je .uniq_flush
        mov [.uniq_cur], esi

        cmp dword [.uniq_prev], 0
        je .uniq_save_prev

        ; Compare current with previous
        push esi
        push edi
        mov edi, [.uniq_prev]
        call cmd_diff_files.diff_cmp_line
        pop edi
        pop esi
        jc .uniq_dup

        ; Lines differ - flush previous
        call .uniq_emit
        mov dword [.uniq_count], 1
        jmp .uniq_save_prev

.uniq_dup:
        inc dword [.uniq_count]
        jmp .uniq_advance

.uniq_save_prev:
        mov eax, [.uniq_cur]
        mov [.uniq_prev], eax
.uniq_advance:
        ; Skip to next line
.uniq_skip:
        cmp byte [esi], 0
        je .uniq_flush
        cmp byte [esi], 0x0A
        je .uniq_skipn
        inc esi
        jmp .uniq_skip
.uniq_skipn:
        inc esi
        jmp .uniq_loop

.uniq_flush:
        cmp dword [.uniq_prev], 0
        je .uniq_done
        call .uniq_emit
.uniq_done:
        popad
        ret

.uniq_emit:
        pushad
        ; Check -d flag (bit 3)
        mov eax, 3
        call test_flag
        jnc .uniq_no_d
        cmp dword [.uniq_count], 1
        je .uniq_emit_skip

.uniq_no_d:
        ; Check -c flag (bit 2)
        mov eax, 2
        call test_flag
        jnc .uniq_no_c
        mov byte [vga_color], COLOR_INFO
        mov eax, [.uniq_count]
        call vga_print_dec
        mov al, ' '
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
.uniq_no_c:
        mov esi, [.uniq_prev]
.uniq_pl:
        mov al, [esi]
        cmp al, 0
        je .uniq_pnl
        cmp al, 0x0A
        je .uniq_pnl
        call vga_putchar
        inc esi
        jmp .uniq_pl
.uniq_pnl:
        call vga_newline
.uniq_emit_skip:
        popad
        ret

.uniq_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.uniq_nf:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_file_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

.uniq_prev:   dd 0
.uniq_cur:    dd 0
.uniq_count:  dd 0

;---------------------------------------
; Command: rev - Reverse each line of a file
;---------------------------------------
cmd_rev_file:
        pushad
        mov edi, filename_buf
        call copy_word
        cmp byte [filename_buf], 0
        je .rev_usage

        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .rev_nf
        mov edi, PROGRAM_BASE
        add edi, ecx
        mov byte [edi], 0

        mov esi, PROGRAM_BASE
.rev_loop:
        cmp byte [esi], 0
        je .rev_done
        ; Find end of line
        mov edi, esi
.rev_find_eol:
        cmp byte [edi], 0
        je .rev_eol
        cmp byte [edi], 0x0A
        je .rev_eol
        inc edi
        jmp .rev_find_eol
.rev_eol:
        mov ebx, edi
        cmp edi, esi
        je .rev_empty_line
        dec edi
.rev_print_rev:
        mov al, [edi]
        call vga_putchar
        cmp edi, esi
        je .rev_next
        dec edi
        jmp .rev_print_rev
.rev_empty_line:
.rev_next:
        call vga_newline
        mov esi, ebx
        cmp byte [esi], 0x0A
        jne .rev_done
        inc esi
        jmp .rev_loop
.rev_done:
        popad
        ret
.rev_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.rev_nf:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_file_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; Command: tac - Print file lines in reverse order
;---------------------------------------
cmd_tac_file:
        pushad
        mov edi, filename_buf
        call copy_word
        cmp byte [filename_buf], 0
        je .tac_usage

        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .tac_nf
        mov edi, PROGRAM_BASE
        add edi, ecx
        mov byte [edi], 0

        ; Build line pointer table at PROGRAM_BASE + 0x10000
        mov esi, PROGRAM_BASE
        mov edi, PROGRAM_BASE + 0x10000
        mov dword [.tac_lines], 0
        mov [edi], esi
        add edi, 4
        inc dword [.tac_lines]

.tac_scan:
        cmp byte [esi], 0
        je .tac_print
        cmp byte [esi], 0x0A
        jne .tac_next_ch
        inc esi
        cmp byte [esi], 0
        je .tac_print
        mov [edi], esi
        add edi, 4
        inc dword [.tac_lines]
        jmp .tac_scan
.tac_next_ch:
        inc esi
        jmp .tac_scan

.tac_print:
        mov ecx, [.tac_lines]
        cmp ecx, 0
        je .tac_done
.tac_print_loop:
        dec ecx
        mov esi, PROGRAM_BASE + 0x10000
        mov esi, [esi + ecx * 4]
.tac_pline:
        mov al, [esi]
        cmp al, 0
        je .tac_pnl
        cmp al, 0x0A
        je .tac_pnl
        call vga_putchar
        inc esi
        jmp .tac_pline
.tac_pnl:
        call vga_newline
        cmp ecx, 0
        jne .tac_print_loop
.tac_done:
        popad
        ret
.tac_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.tac_nf:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_file_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.tac_lines: dd 0

;---------------------------------------
; Command: alias - Define or list command aliases
; Usage: alias              (list all)
;        alias NAME CMD     (define alias)
;---------------------------------------
cmd_alias:
        pushad
        cmp byte [esi], 0
        je .alias_list

        ; Parse alias name
        mov edi, filename_buf
        call copy_word
        call skip_spaces
        cmp byte [esi], 0
        je .alias_show_one

        ; Find existing or empty slot
        mov ecx, [alias_count]
        xor ebx, ebx
        mov edx, alias_table
.alias_find:
        cmp ebx, ecx
        jge .alias_new_slot
        push esi
        mov esi, filename_buf
        mov edi, edx
        call str_compare
        pop esi
        je .alias_update
        add edx, ALIAS_ENTRY_SIZE
        inc ebx
        jmp .alias_find

.alias_new_slot:
        cmp ecx, ALIAS_MAX
        jge .alias_full
        mov eax, ecx
        imul eax, ALIAS_ENTRY_SIZE
        lea edx, [alias_table + eax]
        inc dword [alias_count]

.alias_update:
        push esi
        mov esi, filename_buf
        mov edi, edx
        mov ecx, ALIAS_NAME_LEN - 1
.alias_cpn:
        lodsb
        stosb
        cmp al, 0
        je .alias_nd
        dec ecx
        jnz .alias_cpn
        mov byte [edi], 0
.alias_nd:
        pop esi
        lea edi, [edx + ALIAS_NAME_LEN]
        mov ecx, ALIAS_CMD_LEN - 1
.alias_cpc:
        lodsb
        stosb
        cmp al, 0
        je .alias_cd
        dec ecx
        jnz .alias_cpc
        mov byte [edi], 0
.alias_cd:
        mov esi, msg_alias_set
        call vga_print
        popad
        ret

.alias_list:
        mov ecx, [alias_count]
        cmp ecx, 0
        je .alias_none
        xor ebx, ebx
        mov edx, alias_table
.alias_ll:
        cmp ebx, ecx
        jge .alias_ld
        mov byte [vga_color], COLOR_INFO
        mov esi, edx
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        mov al, '='
        call vga_putchar
        lea esi, [edx + ALIAS_NAME_LEN]
        call vga_print
        call vga_newline
        add edx, ALIAS_ENTRY_SIZE
        inc ebx
        jmp .alias_ll
.alias_ld:
        popad
        ret

.alias_none:
        mov esi, msg_no_aliases
        call vga_print
        popad
        ret

.alias_show_one:
        mov ecx, [alias_count]
        xor ebx, ebx
        mov edx, alias_table
.alias_sf:
        cmp ebx, ecx
        jge .alias_snf
        push esi
        mov esi, filename_buf
        mov edi, edx
        call str_compare
        pop esi
        je .alias_sp
        add edx, ALIAS_ENTRY_SIZE
        inc ebx
        jmp .alias_sf
.alias_sp:
        lea esi, [edx + ALIAS_NAME_LEN]
        call vga_print
        call vga_newline
        popad
        ret
.alias_snf:
        mov esi, filename_buf
        call vga_print
        mov esi, msg_not_found_w
        call vga_print
        popad
        ret

.alias_full:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_table_full
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; Command: history - Show command history
;---------------------------------------
cmd_history:
        pushad
        mov ecx, [hist_count]
        cmp ecx, 0
        je .hist_empty
        xor ebx, ebx
.hist_loop:
        cmp ebx, ecx
        jge .hist_done
        mov byte [vga_color], COLOR_INFO
        mov eax, ebx
        inc eax
        call vga_print_dec
        mov byte [vga_color], COLOR_DEFAULT
        mov al, ' '
        call vga_putchar
        mov eax, ebx
        imul eax, HIST_ENTRY_SIZE
        lea esi, [hist_buf + eax]
        call vga_print
        call vga_newline
        inc ebx
        jmp .hist_loop
.hist_done:
        popad
        ret
.hist_empty:
        mov esi, msg_no_history
        call vga_print
        popad
        ret

;---------------------------------------
; Command: which - Show if a name is built-in or external
;---------------------------------------
cmd_which:
        pushad
        mov edi, filename_buf
        call copy_word
        cmp byte [filename_buf], 0
        je .which_usage

        ; Check against a table of built-in command names
        mov ebx, .which_builtins
.which_check:
        mov edi, [ebx]
        cmp edi, 0
        je .which_try_fs
        mov esi, filename_buf
        call str_compare
        je .which_builtin
        add ebx, 4
        jmp .which_check

.which_try_fs:
        ; First check current directory
        call hbfs_load_root_dir
        mov esi, filename_buf
        call hbfs_find_file
        jnc .which_found_cwd

        ; Search PATH directories
        call path_save_cwd
        mov esi, path_env_name
        mov edi, path_search_buf
        call env_get_var
        jc .which_path_done

        mov esi, path_search_buf
.which_path_next:
        cmp byte [esi], 0
        je .which_path_done
        mov edi, temp_path_buf
.which_pcopy:
        lodsb
        cmp al, ':'
        je .which_pready
        cmp al, 0
        je .which_plast
        stosb
        jmp .which_pcopy
.which_plast:
        dec esi
.which_pready:
        mov byte [edi], 0
        push esi
        call path_restore_cwd

        mov esi, temp_path_buf
        call cmd_cd_internal
        test eax, eax
        jnz .which_pskip

        call hbfs_load_root_dir
        mov esi, filename_buf
        call hbfs_find_file
        jc .which_pskip

        ; Found in this PATH directory
        call path_restore_cwd
        pop esi
        ; Print: "name is /dir/name (external)"
        mov esi, filename_buf
        call vga_print
        mov esi, msg_is_str
        call vga_print
        mov esi, temp_path_buf
        call vga_print
        mov al, '/'
        call vga_putchar
        mov esi, filename_buf
        call vga_print
        mov esi, msg_external
        call vga_print
        popad
        ret

.which_pskip:
        pop esi
        jmp .which_path_next

.which_path_done:
        call path_restore_cwd
        ; Not found anywhere
        mov esi, filename_buf
        call vga_print
        mov esi, msg_not_found_w
        call vga_print
        popad
        ret

.which_found_cwd:
        ; Show as ./name (external)
        mov esi, filename_buf
        call vga_print
        mov esi, msg_is_str
        call vga_print
        ; Show cwd path
        mov edi, temp_path_buf
        call build_cwd_path
        mov esi, temp_path_buf
        call vga_print
        mov al, '/'
        call vga_putchar
        mov esi, filename_buf
        call vga_print
        mov esi, msg_external
        call vga_print
        popad
        ret

.which_builtin:
        mov esi, filename_buf
        call vga_print
        mov esi, msg_builtin
        call vga_print
        popad
        ret

.which_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

; Table of built-in command name pointers (null-terminated)
.which_builtins:
        dd cmd_help_str, cmd_ver_str, cmd_clear_str, cmd_dir_str
        dd cmd_ls_str, cmd_del_str, cmd_rm_str, cmd_format_str
        dd cmd_cat_str, cmd_write_str, cmd_hex_str, cmd_mem_str
        dd cmd_time_str, cmd_disk_str, cmd_run_str, cmd_enter_str
        dd cmd_copy_str, cmd_ren_str, cmd_mv_str, cmd_move_str
        dd cmd_df_str, cmd_more_str, cmd_echo_str, cmd_wc_str
        dd cmd_find_str, cmd_append_str, cmd_date_str, cmd_beep_str
        dd cmd_batch_str, cmd_mkdir_str, cmd_cd_str, cmd_pwd_str
        dd cmd_touch_str, cmd_set_str, cmd_unset_str, cmd_shutdown_str
        dd cmd_cls_str, cmd_head_str, cmd_tail_str, cmd_type_str
        dd cmd_diff_str, cmd_uniq_str, cmd_rev_str, cmd_tac_str
        dd cmd_alias_str, cmd_history_str, cmd_which_str
        dd cmd_sleep_str, cmd_color_str, cmd_size_str, cmd_strings_str
        dd 0

;---------------------------------------
; Command: sleep - Sleep for N seconds
;---------------------------------------
cmd_sleep:
        pushad
        xor eax, eax
.sleep_parse:
        movzx ecx, byte [esi]
        cmp cl, '0'
        jb .sleep_do
        cmp cl, '9'
        ja .sleep_do
        imul eax, 10
        sub cl, '0'
        add eax, ecx
        inc esi
        jmp .sleep_parse

.sleep_do:
        cmp eax, 0
        je .sleep_usage
        push eax
        mov esi, msg_sleeping
        call vga_print
        mov eax, [esp]
        call vga_print_dec
        mov esi, msg_sleep_sec
        call vga_print
        pop eax
        imul eax, 100
        mov ebx, [tick_count]
        add ebx, eax
.sleep_wait:
        hlt
        cmp byte [ctrl_c_flag], 0
        jne .sleep_abort
        mov eax, [tick_count]
        cmp eax, ebx
        jl .sleep_wait
        popad
        ret

.sleep_abort:
        mov byte [ctrl_c_flag], 0
        popad
        ret

.sleep_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_sleep_usage
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; Command: color - Set terminal foreground/background color
; Usage: color FG [BG]  (hex values 0-F)
;---------------------------------------
cmd_color:
        pushad
        cmp byte [esi], 0
        je .color_usage

        movzx eax, byte [esi]
        call .color_hex_val
        jc .color_usage
        mov ebx, eax
        inc esi

        call skip_spaces
        cmp byte [esi], 0
        je .color_set

        movzx eax, byte [esi]
        call .color_hex_val
        jc .color_usage
        shl eax, 4
        or ebx, eax

.color_set:
        mov [vga_color], bl
        mov esi, msg_color_set
        call vga_print
        popad
        ret

.color_usage:
        mov esi, msg_color_usage
        call vga_print
        popad
        ret

.color_hex_val:
        cmp al, '0'
        jb .chv_fail
        cmp al, '9'
        jbe .chv_digit
        or al, 0x20
        cmp al, 'a'
        jb .chv_fail
        cmp al, 'f'
        ja .chv_fail
        sub al, 'a' - 10
        clc
        ret
.chv_digit:
        sub al, '0'
        clc
        ret
.chv_fail:
        stc
        ret

;---------------------------------------
; Command: size - Show file size and type
;---------------------------------------
cmd_size_file:
        pushad
        mov edi, filename_buf
        call copy_word
        cmp byte [filename_buf], 0
        je .size_usage

        mov esi, filename_buf
        call hbfs_find_file_global
        jc .size_nf

        mov byte [vga_color], COLOR_INFO
        mov esi, filename_buf
        call vga_print
        mov esi, msg_file_colon
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT

        mov eax, [edi + DIRENT_SIZE]
        call vga_print_dec
        mov esi, msg_size_bytes
        call vga_print

        mov eax, [edi + DIRENT_BLOCK_COUNT]
        call vga_print_dec
        mov esi, msg_size_blocks
        call vga_print

        movzx eax, byte [edi + DIRENT_TYPE]
        cmp al, FTYPE_TEXT
        je .size_text
        cmp al, FTYPE_DIR
        je .size_dir
        cmp al, FTYPE_EXEC
        je .size_exec
        cmp al, FTYPE_BATCH
        je .size_batch
        mov esi, msg_type_unknown
        jmp .size_ptype
.size_text:
        mov esi, msg_type_text
        jmp .size_ptype
.size_dir:
        mov esi, msg_type_dir
        jmp .size_ptype
.size_exec:
        mov esi, msg_type_exec
        jmp .size_ptype
.size_batch:
        mov esi, msg_type_batch
.size_ptype:
        call vga_print
        ; Restore CWD if global search changed it
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .size_no_restore
        call gff_restore_cwd
.size_no_restore:
        popad
        ret

.size_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.size_nf:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_file_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; Command: strings - Extract printable strings from a file
; Flags: -n N  minimum string length (default 4)
;---------------------------------------
cmd_strings_file:
        pushad
        mov edi, filename_buf
        call copy_word
        cmp byte [filename_buf], 0
        je .strings_usage

        mov dword [strings_min_len], 4
        mov eax, [cmd_flag_num]
        cmp eax, 0
        je .strings_read
        mov [strings_min_len], eax

.strings_read:
        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .strings_nf
        mov [.strings_total], ecx

        mov esi, PROGRAM_BASE
        xor ebx, ebx            ; run length
        mov edx, esi             ; run start
.strings_scan:
        cmp ecx, 0
        je .strings_flush
        movzx eax, byte [esi]
        cmp al, 0x20
        jb .strings_check_ws
        cmp al, 0x7E
        ja .strings_not_print
        cmp ebx, 0
        jne .strings_cont
        mov edx, esi
.strings_cont:
        inc ebx
        inc esi
        dec ecx
        jmp .strings_scan

.strings_check_ws:
        cmp al, 9
        je .strings_ws
        cmp al, 10
        je .strings_ws
        cmp al, 13
        je .strings_ws
        jmp .strings_not_print
.strings_ws:
        cmp ebx, 0
        jne .strings_cont
        mov edx, esi
        jmp .strings_cont

.strings_not_print:
        call .strings_flush_run
        inc esi
        dec ecx
        jmp .strings_scan

.strings_flush:
        call .strings_flush_run
        popad
        ret

.strings_flush_run:
        cmp ebx, [strings_min_len]
        jb .strings_reset
        push esi
        push ecx
        mov esi, edx
        mov ecx, ebx
.strings_ploop:
        mov al, [esi]
        call vga_putchar
        inc esi
        dec ecx
        jnz .strings_ploop
        call vga_newline
        pop ecx
        pop esi
.strings_reset:
        xor ebx, ebx
        ret

.strings_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.strings_nf:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_file_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

.strings_total: dd 0

;---------------------------------------
; Command: dir/ls - List directory
; Flags: -l  long format (type, size, blocks, name)
;        -s  sort by size (largest first) — not yet, just reserved
;---------------------------------------
cmd_list_dir:
        pushad
        call hbfs_load_root_dir

        ; Check for -l flag (bit 11 = 'l' - 'a')
        mov eax, 11             ; 'l' - 'a'
        call test_flag
        jc .long_format

        ; Short format: just filenames in columns
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
        xor ebx, ebx           ; File counter
        xor edx, edx           ; Column counter

.short_loop:
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .short_next

        inc ebx

        ; Set color by type
        cmp byte [edi + DIRENT_TYPE], FTYPE_DIR
        je .sc_dir
        cmp byte [edi + DIRENT_TYPE], FTYPE_EXEC
        je .sc_exec
        cmp byte [edi + DIRENT_TYPE], FTYPE_BATCH
        je .sc_batch
        mov byte [vga_color], COLOR_DEFAULT
        jmp .sc_print
.sc_dir:
        mov byte [vga_color], COLOR_INFO
        jmp .sc_print
.sc_exec:
        mov byte [vga_color], COLOR_EXEC
        jmp .sc_print
.sc_batch:
        mov byte [vga_color], COLOR_BATCH
.sc_print:
        ; Print filename padded to 16 chars
        push esi
        push ecx
        mov esi, edi
        xor ecx, ecx
.sc_name:
        lodsb
        test al, al
        jz .sc_pad
        call vga_putchar
        inc ecx
        jmp .sc_name
.sc_pad:
        cmp ecx, 16
        jge .sc_col
        mov al, ' '
        call vga_putchar
        inc ecx
        jmp .sc_pad
.sc_col:
        pop ecx
        pop esi
        mov byte [vga_color], COLOR_DEFAULT
        inc edx
        cmp edx, 4             ; 4 columns
        jl .short_next
        mov al, 0x0A
        call vga_putchar
        xor edx, edx

.short_next:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jnz .short_loop

        ; Final newline if not already at start of line
        test edx, edx
        jz .short_summary
        mov al, 0x0A
        call vga_putchar
.short_summary:
        mov esi, msg_dir_count
        call vga_print
        mov eax, ebx
        call vga_print_dec
        mov esi, msg_dir_files
        call vga_print
        popad
        ret

.long_format:
        mov byte [vga_color], COLOR_HEADER
        mov esi, dir_header
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        mov esi, dir_separator
        call vga_print

        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
        xor ebx, ebx           ; File counter

.loop:
        cmp byte [edi + 253], FTYPE_FREE
        je .next

        inc ebx

        ; Print type indicator
        cmp byte [edi + 253], FTYPE_DIR
        je .type_dir
        cmp byte [edi + 253], FTYPE_EXEC
        je .type_exec
        cmp byte [edi + 253], FTYPE_BATCH
        je .type_batch
        ; Default: text file
        mov byte [vga_color], COLOR_DEFAULT
        mov esi, type_text
        call vga_print
        jmp .print_size
.type_dir:
        mov byte [vga_color], COLOR_INFO
        mov esi, type_dir
        call vga_print
        jmp .print_size
.type_exec:
        mov byte [vga_color], COLOR_EXEC
        mov esi, type_exec
        call vga_print
        jmp .print_size
.type_batch:
        mov byte [vga_color], COLOR_BATCH
        mov esi, type_batch
        call vga_print

.print_size:
        mov byte [vga_color], COLOR_DEFAULT
        mov al, ' '
        call vga_putchar

        ; Print file size right-aligned in 9-char field
        mov eax, [edi + 256]
        mov edx, 9
        call vga_print_dec_width

        mov al, ' '
        call vga_putchar
        call vga_putchar

        ; Print filename
        push esi
        mov esi, edi
        call vga_print
        pop esi

        mov byte [vga_color], COLOR_DEFAULT
        mov al, 0x0A
        call vga_putchar

.next:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jnz .loop

        ; Print summary
        mov esi, msg_dir_count
        call vga_print
        mov eax, ebx
        call vga_print_dec
        mov esi, msg_dir_files
        call vga_print

        popad
        ret

;---------------------------------------
; Command: del/rm - Delete file
; Flags: -v  verbose (print each deleted filename)
;---------------------------------------
cmd_delete_file:
        pushad
        ; Parse filename/pattern argument
        mov edi, filename_buf
        call copy_word
        cmp byte [filename_buf], 0
        je .not_found

        ; Wildcard mode?
        mov esi, filename_buf
        call str_has_wildcards
        jc .wildcard

        ; Exact filename delete
        mov esi, filename_buf
        call hbfs_find_file_global
        jc .not_found
        ; -v: print name before delete
        mov eax, 21            ; 'v' - 'a'
        call test_flag
        jnc .del_exact_quiet
        push esi
        mov esi, msg_del_prefix
        call vga_print
        mov esi, edi
        call vga_print
        mov al, 0x0A
        call vga_putchar
        pop esi
.del_exact_quiet:
        call hbfs_delete_file_entry
        ; If CWD was changed by global search, restore it
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .del_no_restore
        call gff_restore_cwd
.del_no_restore:
        mov esi, msg_deleted
        call vga_print
        popad
        ret

.wildcard:
        call hbfs_load_root_dir
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
        xor ebx, ebx                    ; match/delete count

.wc_loop:
        cmp ecx, 0
        je .wc_done
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .wc_next

        push ecx
        push edi
        mov esi, filename_buf           ; pattern
        call wildcard_match
        pop edi
        pop ecx
        jnc .wc_next

        ; -v: print name before delete
        mov eax, 21            ; 'v' - 'a'
        call test_flag
        jnc .wc_del_quiet
        push esi
        mov esi, msg_del_prefix
        call vga_print
        mov esi, edi
        call vga_print
        mov al, 0x0A
        call vga_putchar
        pop esi
.wc_del_quiet:
        call hbfs_delete_file_entry
        inc ebx

.wc_next:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jmp .wc_loop

.wc_done:
        cmp ebx, 0
        je .not_found
        mov esi, msg_deleted
        call vga_print
        popad
        ret

.not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; Command: shutdown - Cleanly power off
;---------------------------------------
cmd_shutdown:
        ; Print shutdown banner (consistent with boot banner style)
        mov al, 0x0A
        call vga_putchar
        mov byte [vga_color], COLOR_HEADER
        mov esi, msg_shutdown_bar
        call vga_print
        mov byte [vga_color], COLOR_INFO
        mov esi, msg_shutdown
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT

        ; Small delay so user can see the message
        mov ecx, 0x3FFFFFF
.delay:
        dec ecx
        jnz .delay

        ; Print halt message before attempting shutdown
        mov esi, msg_halt
        call vga_print

        ; Another small delay so user can read the message
        mov ecx, 0x1FFFFFF
.delay2:
        dec ecx
        jnz .delay2

        ; Try ACPI shutdown (works on QEMU and Bochs)
        ; QEMU PIIX4 PM: port 0x604, value 0x2000 = S5 (power off)
        cli
        mov dx, 0x604
        mov ax, 0x2000
        out dx, ax

        ; If ACPI didn't work, halt the CPU
.halt_loop:
        hlt
        jmp .halt_loop

;---------------------------------------
; Command: cat - Display file contents
;---------------------------------------
;---------------------------------------
; Command: head - Show first N lines of a file
; Usage: head [N] filename   or   head -n N filename
;        (default N=10)
;---------------------------------------
cmd_head_file:
        pushad
        ; Check for -n flag first (set by parse_flags)
        mov dword [head_lines], 10
        cmp dword [cmd_flag_num], 0
        je .head_no_flag_num
        mov eax, [cmd_flag_num]
        mov [head_lines], eax
        jmp .head_no_num
.head_no_flag_num:
        cmp byte [esi], 0
        je .head_usage
        ; Check if first arg is a number (legacy positional syntax)
        cmp byte [esi], '0'
        jl .head_no_num
        cmp byte [esi], '9'
        jg .head_no_num
        ; Parse number
        xor eax, eax
.head_parse_num:
        movzx ebx, byte [esi]
        cmp bl, '0'
        jl .head_num_done
        cmp bl, '9'
        jg .head_num_done
        imul eax, 10
        sub bl, '0'
        add eax, ebx
        inc esi
        jmp .head_parse_num
.head_num_done:
        mov [head_lines], eax
        call skip_spaces
.head_no_num:
        ; ESI now points to filename
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .head_not_found

        cmp ecx, PROGRAM_MAX_SIZE - 1
        jle .head_size_ok
        mov ecx, PROGRAM_MAX_SIZE - 1
.head_size_ok:
        mov byte [PROGRAM_BASE + ecx], 0

        mov esi, PROGRAM_BASE
        xor ecx, ecx            ; line counter
.head_loop:
        lodsb
        cmp al, 0
        je .head_done
        call vga_putchar
        cmp al, 0x0A
        jne .head_loop
        inc ecx
        cmp ecx, [head_lines]
        jl .head_loop
.head_done:
        popad
        ret
.head_not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.head_usage:
        mov esi, msg_head_usage
        call vga_print
        popad
        ret

;---------------------------------------
; Command: tail - Show last N lines of a file
; Usage: tail [N] filename   or   tail -n N filename
;        (default N=10)
;---------------------------------------
cmd_tail_file:
        pushad
        ; Check for -n flag first (set by parse_flags)
        mov dword [head_lines], 10
        cmp dword [cmd_flag_num], 0
        je .tail_no_flag_num
        mov eax, [cmd_flag_num]
        mov [head_lines], eax
        jmp .tail_no_num
.tail_no_flag_num:
        cmp byte [esi], 0
        je .tail_usage
        ; Check if first arg is a number (legacy positional syntax)
        cmp byte [esi], '0'
        jl .tail_no_num
        cmp byte [esi], '9'
        jg .tail_no_num
        xor eax, eax
.tail_parse_num:
        movzx ebx, byte [esi]
        cmp bl, '0'
        jl .tail_num_done
        cmp bl, '9'
        jg .tail_num_done
        imul eax, 10
        sub bl, '0'
        add eax, ebx
        inc esi
        jmp .tail_parse_num
.tail_num_done:
        mov [head_lines], eax
        call skip_spaces
.tail_no_num:
        ; Read file
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .tail_not_found

        cmp ecx, PROGRAM_MAX_SIZE - 1
        jle .tail_size_ok
        mov ecx, PROGRAM_MAX_SIZE - 1
.tail_size_ok:
        mov byte [PROGRAM_BASE + ecx], 0

        ; Count total lines first
        mov esi, PROGRAM_BASE
        xor ebx, ebx            ; total line count
.tail_count:
        lodsb
        cmp al, 0
        je .tail_counted
        cmp al, 0x0A
        jne .tail_count
        inc ebx
        jmp .tail_count
.tail_counted:
        ; Calculate skip count: total - N
        mov eax, ebx
        sub eax, [head_lines]
        jle .tail_print_all      ; N >= total, print everything
        mov ecx, eax             ; lines to skip

        ; Skip past first ecx lines
        mov esi, PROGRAM_BASE
.tail_skip:
        lodsb
        cmp al, 0
        je .tail_print_done
        cmp al, 0x0A
        jne .tail_skip
        dec ecx
        jnz .tail_skip
        jmp .tail_print

.tail_print_all:
        mov esi, PROGRAM_BASE
.tail_print:
        lodsb
        cmp al, 0
        je .tail_print_done
        call vga_putchar
        jmp .tail_print
.tail_print_done:
        mov al, 0x0A
        call vga_putchar
        popad
        ret
.tail_not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.tail_usage:
        mov esi, msg_tail_usage
        call vga_print
        popad
        ret

;---------------------------------------
; Command: cat - Display file contents
; Flags: -n  show line numbers
;---------------------------------------
cmd_cat_file:
        pushad
        mov edi, PROGRAM_BASE   ; Use program area as temp buffer
        call hbfs_read_file
        jc .not_found

        ; ECX = file size from hbfs_read_file
        ; Clamp to buffer size to prevent overflow
        cmp ecx, PROGRAM_MAX_SIZE - 1
        jle .cat_size_ok
        mov ecx, PROGRAM_MAX_SIZE - 1
.cat_size_ok:
        ; Null-terminate the content at the file size boundary
        mov byte [PROGRAM_BASE + ecx], 0

        ; Check for -n flag (line numbers)
        mov eax, 13             ; 'n' - 'a'
        call test_flag
        jc .cat_numbered

        ; Plain output
        mov esi, PROGRAM_BASE
        call vga_print
        mov al, 0x0A
        call vga_putchar
        popad
        ret

.cat_numbered:
        mov esi, PROGRAM_BASE
        xor ebx, ebx           ; line counter
.cat_n_line:
        cmp byte [esi], 0
        je .cat_n_done
        inc ebx
        ; Print line number right-aligned in 4-char field
        push esi
        mov byte [vga_color], COLOR_INFO
        mov eax, ebx
        mov edx, 4
        call vga_print_dec_width
        mov al, ' '
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
        pop esi
        ; Print chars until newline or end
.cat_n_char:
        lodsb
        cmp al, 0
        je .cat_n_done
        call vga_putchar
        cmp al, 0x0A
        jne .cat_n_char
        jmp .cat_n_line
.cat_n_done:
        mov al, 0x0A
        call vga_putchar
        popad
        ret

.not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; Command: more - Page-by-page file viewer
; Flags: -n N  lines per page (default 23)
;---------------------------------------
PAGER_LINES equ 23

cmd_more_file:
        pushad
        ; Set page size from -n flag or default
        mov dword [.more_page], PAGER_LINES
        cmp dword [cmd_flag_num], 0
        je .more_default_page
        mov eax, [cmd_flag_num]
        mov [.more_page], eax
.more_default_page:
        mov edi, PROGRAM_BASE   ; Use program area as temp buffer
        call hbfs_read_file
        jc .more_not_found

        ; ECX = file size
        cmp ecx, PROGRAM_MAX_SIZE - 1
        jle .more_size_ok
        mov ecx, PROGRAM_MAX_SIZE - 1
.more_size_ok:
        mov byte [PROGRAM_BASE + ecx], 0

        mov esi, PROGRAM_BASE
        xor ecx, ecx            ; Line counter

.more_loop:
        lodsb
        cmp al, 0
        je .more_done

        call vga_putchar

        cmp al, 0x0A             ; Newline?
        jne .more_loop
        inc ecx
        cmp ecx, [.more_page]
        jl .more_loop

        ; Show pager prompt
        push esi
        mov byte [vga_color], COLOR_INFO
        mov esi, msg_more_prompt
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        pop esi

        ; Wait for keypress
        call kb_getchar
        cmp al, 27              ; ESC
        je .more_done
        cmp al, 'q'
        je .more_done

        ; Erase the prompt line
        mov al, 0x0D             ; CR
        call vga_putchar
        push esi
        mov esi, msg_more_erase
        call vga_print
        pop esi
        mov al, 0x0D
        call vga_putchar

        xor ecx, ecx            ; Reset line counter
        jmp .more_loop

.more_done:
        mov al, 0x0A
        call vga_putchar
        popad
        ret

.more_not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.more_page: dd PAGER_LINES

;---------------------------------------
; Command: write - Write text to file
; Usage: write filename
; Then type content, end with empty line
;---------------------------------------
cmd_write_file:
        pushad

        ; ESI points to filename - copy it
        mov edi, filename_buf
        call copy_word           ; Copy filename to filename_buf

        ; Now read lines of content
        mov edi, PROGRAM_BASE   ; Write content here
        xor ebx, ebx           ; Byte counter

        mov esi, msg_write_prompt
        call vga_print

.read_content:
        ; Read a line
        push edi
        push ebx
        mov edi, temp_line_buf
        xor ecx, ecx

.read_char:
        call kb_getchar
        cmp al, 0x0D
        je .end_line
        cmp al, 0x0A
        je .end_line
        cmp al, 0x08
        je .bs
        mov [edi], al
        inc edi
        inc ecx
        call vga_putchar
        jmp .read_char

.bs:
        or ecx, ecx
        jz .read_char
        dec edi
        dec ecx
        mov al, 0x08
        call vga_putchar
        jmp .read_char

.end_line:
        mov byte [edi], 0
        mov al, 0x0A
        call vga_putchar

        pop ebx
        pop edi

        ; Check for empty line (end of input)
        cmp ecx, 0
        je .save_file

        ; Copy line to content buffer
        mov esi, temp_line_buf
.copy_line:
        lodsb
        mov [edi + ebx], al
        inc ebx
        test al, al
        jnz .copy_line
        ; Replace null with newline
        dec ebx
        mov byte [edi + ebx], 0x0A
        inc ebx
        jmp .read_content

.save_file:
        ; Null-terminate content
        mov byte [PROGRAM_BASE + ebx], 0

        ; Create file - detect type by extension
        mov esi, filename_buf
        mov edx, FTYPE_TEXT     ; Default to text
        call detect_file_type   ; Sets EDX based on filename in ESI
        mov ecx, ebx            ; Size
        mov edi, PROGRAM_BASE   ; Data
        call hbfs_create_file
        jc .write_error

        mov esi, msg_saved
        call vga_print
        popad
        ret

.write_error:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_write_err
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; Command: touch - Create empty file
;---------------------------------------
cmd_touch_file:
        pushad
        mov edi, filename_buf
        call copy_word
        cmp byte [filename_buf], 0
        je .touch_usage

        ; Create empty file
        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        mov ecx, 0              ; Empty file
        mov edx, FTYPE_TEXT     ; Default to text
        call detect_file_type
        call hbfs_create_file
        jc .touch_err
        mov esi, msg_saved
        call vga_print
        jmp .touch_done

.touch_usage:
        mov esi, msg_touch_usage
        call vga_print
        jmp .touch_done

.touch_err:
        mov esi, msg_write_err
        call vga_print

.touch_done:
        popad
        ret

;---------------------------------------
; Command: hex - Hexdump a file
; Flags: -n N  limit to N bytes (default 512)
;---------------------------------------
cmd_hexdump_file:
        pushad
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .not_found

        ; ECX = file size returned by hbfs_read_file
        ; Check for -n flag limit
        mov eax, 512            ; default limit
        cmp dword [cmd_flag_num], 0
        je .hex_default_limit
        mov eax, [cmd_flag_num]
.hex_default_limit:
        cmp ecx, eax
        jle .size_ok
        mov ecx, eax
.size_ok:
        mov esi, PROGRAM_BASE
        mov edx, 0              ; Offset counter

.hex_line:
        cmp edx, ecx
        jge .hex_done

        ; Print offset
        mov eax, edx
        call vga_print_hex
        mov al, ':'
        call vga_putchar
        mov al, ' '
        call vga_putchar

        ; Print 16 hex bytes
        push ecx
        push edx
        mov ebx, 16
.hex_byte:
        cmp edx, ecx
        jge .hex_pad
        movzx eax, byte [esi + edx]
        push eax
        shr eax, 4
        call .print_nibble
        pop eax
        and eax, 0x0F
        call .print_nibble
        mov al, ' '
        call vga_putchar
        inc edx
        dec ebx
        jnz .hex_byte
        jmp .hex_ascii

.hex_pad:
        ; Pad remaining with spaces
        mov al, ' '
        call vga_putchar
        call vga_putchar
        call vga_putchar
        dec ebx
        jnz .hex_pad

.hex_ascii:
        ; Print ASCII representation
        mov al, '|'
        call vga_putchar
        pop edx
        push edx
        mov ebx, 16
.ascii_byte:
        cmp edx, [esp + 4]     ; Compare with ecx on stack
        jge .ascii_done
        movzx eax, byte [esi + edx]
        cmp al, 0x20
        jl .non_printable
        cmp al, 0x7E
        jg .non_printable
        call vga_putchar
        jmp .ascii_next
.non_printable:
        mov al, '.'
        call vga_putchar
.ascii_next:
        inc edx
        dec ebx
        jnz .ascii_byte
.ascii_done:
        mov al, '|'
        call vga_putchar
        mov al, 0x0A
        call vga_putchar

        pop edx
        pop ecx
        jmp .hex_line

.print_nibble:
        cmp al, 10
        jl .pn_digit
        add al, 'A' - 10
        call vga_putchar
        ret
.pn_digit:
        add al, '0'
        call vga_putchar
        ret

.hex_done:
        popad
        ret

.not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; Command: mem - Show memory information
;---------------------------------------
cmd_show_memory:
        pushad
        mov byte [vga_color], COLOR_INFO
        mov esi, msg_mem_header
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT

        call pmm_count_free
        mov [total_free_pages], eax

        mov esi, msg_free_pages
        call vga_print
        mov eax, [total_free_pages]
        call vga_print_dec
        mov esi, msg_pages_suffix
        call vga_print

        mov esi, msg_free_mem
        call vga_print
        mov eax, [total_free_pages]
        shl eax, 2              ; * 4KB
        shr eax, 10             ; / 1024 = MB
        call vga_print_dec
        mov esi, msg_mb
        call vga_print

        mov esi, msg_tick_count
        call vga_print
        mov eax, [tick_count]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar

        popad
        ret

;---------------------------------------
; Command: disk - Show disk information
;---------------------------------------
cmd_show_disk:
        pushad
        cmp byte [ata_present], 0
        je .no_disk

        mov byte [vga_color], COLOR_INFO
        mov esi, msg_disk_header
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT

        mov esi, msg_disk_sectors
        call vga_print
        mov eax, [ata_total_sectors]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar

        mov esi, msg_disk_size
        call vga_print
        mov eax, [ata_total_sectors]
        shr eax, 11             ; / 2048 = MB
        call vga_print_dec
        mov esi, msg_mb
        call vga_print

        popad
        ret

.no_disk:
        mov esi, msg_ata_none
        call vga_print
        popad
        ret

;---------------------------------------
; Command: df - Show HBFS filesystem usage
;---------------------------------------
cmd_df_info:
        pushad
        cmp byte [ata_present], 0
        je .df_no_disk

        mov byte [vga_color], COLOR_INFO
        mov esi, msg_df_header
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT

        ; Count total data blocks available on disk
        ; Total data sectors = ata_total_sectors - HBFS_DATA_START
        mov eax, [ata_total_sectors]
        sub eax, HBFS_DATA_START
        shr eax, 3              ; / 8 sectors per block = total blocks
        mov ebx, eax            ; EBX = total blocks
        push ebx

        ; Count used blocks from bitmap
        call hbfs_load_bitmap
        mov ebp, ebx            ; total blocks
        add ebp, 7
        shr ebp, 3              ; bitmap bytes = ceil(total_blocks / 8)
        xor ecx, ecx            ; ECX = used blocks counter
        xor esi, esi            ; ESI = byte index
        mov edi, hbfs_bitmap_buf

.df_count:
        cmp esi, ebp            ; Scan all bitmap bytes for data blocks
        jge .df_counted
        movzx eax, byte [edi + esi]
        ; Count set bits in AL
        xor edx, edx
.df_bits:
        cmp edx, 8
        jge .df_next_byte
        bt eax, edx
        jnc .df_bit_clear
        inc ecx
.df_bit_clear:
        inc edx
        jmp .df_bits
.df_next_byte:
        inc esi
        jmp .df_count

.df_counted:
        ; ECX = used blocks, [esp] = total blocks
        pop ebx                 ; EBX = total blocks
        ; Clamp used to total
        cmp ecx, ebx
        jle .df_clamp_ok
        mov ecx, ebx
.df_clamp_ok:
        push ecx
        push ebx

        ; Print total blocks
        mov esi, msg_df_total
        call vga_print
        mov eax, ebx
        call vga_print_dec
        mov esi, msg_df_blocks
        call vga_print

        ; Print used blocks
        mov esi, msg_df_used
        call vga_print
        pop ebx                 ; total
        pop ecx                 ; used
        push ecx
        push ebx
        mov eax, ecx
        call vga_print_dec
        mov esi, msg_df_blocks
        call vga_print

        ; Print free blocks
        mov esi, msg_df_free
        call vga_print
        pop ebx                 ; total
        pop ecx                 ; used
        mov eax, ebx
        sub eax, ecx
        push eax
        call vga_print_dec
        mov esi, msg_df_blocks
        call vga_print

        ; Print free space in KB
        mov esi, msg_df_avail
        call vga_print
        pop eax                 ; free blocks
        shl eax, 2              ; * 4 = KB
        call vga_print_dec
        mov esi, msg_df_kb
        call vga_print

        ; Count files across ALL directories
        call path_save_cwd               ; Save user's CWD
        mov dword [.df_total_files], 0
        mov dword [.df_total_slots], 0
        mov dword [.df_dir_count], 0

        ; Count files in root directory
        mov dword [current_dir_lba], HBFS_ROOT_DIR_START
        mov dword [current_dir_sects], HBFS_ROOT_DIR_SECTS
        call hbfs_load_root_dir
        call .df_count_dir               ; ECX = files in root, EAX = max entries
        add [.df_total_files], ecx
        add [.df_total_slots], eax

        ; Iterate root entries looking for subdirectories
        mov dword [.df_scan_idx], 0
.df_scan_subdirs:
        ; Reload root (subdir scan overwrites hbfs_dir_buf)
        mov dword [current_dir_lba], HBFS_ROOT_DIR_START
        mov dword [current_dir_sects], HBFS_ROOT_DIR_SECTS
        call hbfs_load_root_dir
        call hbfs_get_max_entries
        cmp [.df_scan_idx], eax
        jge .df_all_counted

        mov eax, [.df_scan_idx]
        imul eax, HBFS_DIR_ENTRY_SIZE
        lea edi, [hbfs_dir_buf + eax]
        inc dword [.df_scan_idx]

        cmp byte [edi + DIRENT_TYPE], FTYPE_DIR
        jne .df_scan_subdirs

        ; Switch into this subdirectory
        mov eax, [edi + DIRENT_START_BLOCK]
        shl eax, 3
        add eax, HBFS_DATA_START
        mov [current_dir_lba], eax
        mov eax, [edi + DIRENT_BLOCK_COUNT]
        shl eax, 3
        mov [current_dir_sects], eax

        call hbfs_load_root_dir
        call .df_count_dir
        add [.df_total_files], ecx
        add [.df_total_slots], eax
        inc dword [.df_dir_count]
        jmp .df_scan_subdirs

.df_all_counted:
        call path_restore_cwd            ; Restore user's CWD

        mov esi, msg_df_files
        call vga_print
        mov eax, [.df_total_files]
        call vga_print_dec
        mov esi, msg_df_in
        call vga_print
        mov eax, [.df_dir_count]
        inc eax                          ; +1 for root
        call vga_print_dec
        mov esi, msg_df_dirs
        call vga_print

        popad
        ret

; Helper: count files in currently loaded directory buffer
; Returns: ECX = file count, EAX = max entries
.df_count_dir:
        xor ecx, ecx
        xor ebx, ebx
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov edx, eax
.df_cd_loop:
        cmp ebx, edx
        jge .df_cd_done
        cmp byte [edi], 0
        je .df_cd_skip
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .df_cd_skip
        cmp byte [edi + DIRENT_TYPE], FTYPE_DIR
        je .df_cd_skip           ; Don't count directory entries as files
        inc ecx
.df_cd_skip:
        add edi, HBFS_DIR_ENTRY_SIZE
        inc ebx
        jmp .df_cd_loop
.df_cd_done:
        mov eax, edx
        ret

.df_total_files: dd 0
.df_total_slots: dd 0
.df_dir_count:   dd 0
.df_scan_idx:    dd 0

.df_no_disk:
        mov esi, msg_hbfs_nodisk
        call vga_print
        popad
        ret

;---------------------------------------
; Command: run - Execute a program from filesystem
;---------------------------------------
cmd_exec_program:
        pushad

        ; Parse program name (up to first space or null)
        mov edi, prog_name_buf
.parse_pname:
        lodsb
        cmp al, ' '
        je .pname_done
        cmp al, 0
        je .pname_end
        stosb
        jmp .parse_pname
.pname_done:
        mov byte [edi], 0
        call skip_spaces        ; Skip extra spaces before args
        jmp .copy_prog_args
.pname_end:
        mov byte [edi], 0
        dec esi                 ; Point back to null
.copy_prog_args:
        ; Copy remaining args to kernel argument buffer
        mov edi, program_args_buf
        mov ecx, 511
.cpa_loop:
        lodsb
        stosb
        test al, al
        jz .cpa_done
        loop .cpa_loop
        mov byte [edi], 0       ; Force null terminate
.cpa_done:

        ; Load file using parsed program name
        mov esi, prog_name_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .try_path_search

        jmp .found_program

.try_path_search:
        ; File not found in current directory.
        ; Search each directory in PATH (colon-separated, e.g. "/bin:/games")
        ; Save the user's current working directory
        call path_save_cwd

        mov esi, path_env_name
        mov edi, path_search_buf
        call env_get_var
        jc .path_not_found      ; No PATH set

        ; Iterate colon-separated PATH components
        mov esi, path_search_buf
.path_next_dir:
        cmp byte [esi], 0
        je .path_not_found

        ; Extract this path component into temp_path_buf
        mov edi, temp_path_buf
.path_copy_dir:
        lodsb
        cmp al, ':'
        je .path_dir_ready
        cmp al, 0
        je .path_dir_last
        stosb
        jmp .path_copy_dir
.path_dir_last:
        dec esi                 ; point back at null for next iteration
.path_dir_ready:
        mov byte [edi], 0
        push esi                ; save position in PATH string

        ; Restore to user's cwd first (clean slate for cd)
        call path_restore_cwd

        ; cd into this PATH directory
        mov esi, temp_path_buf
        call cmd_cd_internal
        test eax, eax
        jnz .path_cd_fail

        ; Now search for the program in this directory
        call hbfs_load_root_dir
        mov esi, prog_name_buf
        call hbfs_find_file
        jc .path_cd_fail

        ; Found it! Read the file data
        movzx ebx, byte [edi + 253]
        mov [last_file_type], bl
        mov ebx, [edi + 256]    ; file size
        mov [.path_fsize], ebx
        mov eax, [edi + 260]    ; start block
        mov ecx, [edi + 264]    ; block count
        shl eax, 3
        add eax, HBFS_DATA_START
        shl ecx, 3
        mov edi, PROGRAM_BASE
        call ata_read_sectors
        jc .path_cd_fail

        ; Restore user's cwd before execution
        call path_restore_cwd
        pop esi                 ; clean up PATH string pointer
        jmp .found_program

.path_cd_fail:
        pop esi                 ; restore PATH string position
        jmp .path_next_dir

.path_not_found:
        ; Restore user's cwd
        call path_restore_cwd
        jmp .not_found

.found_program:

        ; Set program running flag
        mov byte [program_running], 1
        ; Clear Ctrl+C flag and exit code
        mov byte [ctrl_c_flag], 0
        mov dword [program_exit_code], 0

        ; Check for ELF magic
        cmp dword [PROGRAM_BASE], ELF_MAGIC
        jne .try_flat

        ; ELF binary - parse and load
        call elf_load_program
        jc .not_found           ; ELF parse error
        ; EAX = entry point from ELF
        mov [program_entry], eax
        jmp .exec_program

.try_flat:
        ; Flat binary at PROGRAM_BASE
        mov dword [program_entry], PROGRAM_BASE

.exec_program:
        mov esi, msg_exec
        call vga_print

        ; Write SYS_EXIT trampoline at end of program area
        ; This catches programs that use RET instead of SYS_EXIT
        ; Bytes: B8 00 00 00 00 CD 80 = mov eax, 0 / int 0x80
        mov byte [PROGRAM_EXIT_ADDR], 0xB8
        mov dword [PROGRAM_EXIT_ADDR + 1], 0
        mov byte [PROGRAM_EXIT_ADDR + 5], 0xCD
        mov byte [PROGRAM_EXIT_ADDR + 6], 0x80

        ; Update TSS kernel stack for ring transitions
        mov dword [tss_struct + 4], KERNEL_STACK

        ; Set up ring 3 stack
        mov eax, PROGRAM_EXIT_ADDR - 4
        ; Push return address (trampoline) on ring 3 stack
        mov dword [eax], PROGRAM_EXIT_ADDR

        ; Switch to ring 3 via IRET
        mov ebx, [program_entry]
        push dword USER_DS      ; SS3
        push eax                ; ESP3
        pushfd
        or dword [esp], 0x200   ; Ensure interrupts enabled
        push dword USER_CS      ; CS3
        push ebx                ; EIP
        iretd
        ; SYS_EXIT will jump to shell_main, we never return here

.path_fsize: dd 0               ; Temp storage for file size during PATH search

.not_found:
        popad
        stc
        ret

;---------------------------------------
; Redirection helpers
;---------------------------------------
shell_redir_reset:
        mov byte [stdout_redir_active], 0
        mov byte [stdout_redir_append], 0
        mov byte [stdin_redir_active], 0
        mov dword [stdout_redir_len], 0
        mov dword [stdin_redir_len], 0
        mov dword [stdin_redir_pos], 0
        mov byte [redir_out_name], 0
        mov byte [redir_in_name], 0
        mov byte [redir_out_buf], 0
        ret

shell_prepare_redirection:
        pushad
        call shell_redir_reset
        mov esi, line_buffer
.spr_scan:
        mov al, [esi]
        test al, al
        jz .spr_ok
        cmp al, '>'
        je .spr_stdout
        cmp al, '<'
        je .spr_stdin
        inc esi
        jmp .spr_scan

.spr_stdout:
        mov byte [esi], 0
        inc esi
        mov byte [stdout_redir_append], 0
        cmp byte [esi], '>'
        jne .spr_stdout_name
        mov byte [stdout_redir_append], 1
        inc esi
.spr_stdout_name:
        call skip_spaces
        mov edi, redir_out_name
        call copy_word
        cmp byte [redir_out_name], 0
        je .spr_syntax_err
        mov byte [stdout_redir_active], 1
        mov dword [stdout_redir_len], 0
        mov byte [redir_out_buf], 0
        jmp .spr_scan

.spr_stdin:
        mov byte [esi], 0
        inc esi
        call skip_spaces
        mov edi, redir_in_name
        call copy_word
        cmp byte [redir_in_name], 0
        je .spr_syntax_err
        push esi
        mov esi, redir_in_name
        call hbfs_find_file_global
        jc .spr_in_err_pop
        mov eax, [edi + 256]
        cmp eax, BATCH_BUFFER_SIZE - 1
        ja .spr_in_big_pop
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .spr_in_size_ok
        call gff_restore_cwd
.spr_in_size_ok:
        pop esi
        mov esi, redir_in_name
        mov edi, redir_in_buf
        call hbfs_read_file
        jc .spr_in_err
        mov [stdin_redir_len], ecx
        mov dword [stdin_redir_pos], 0
        mov byte [stdin_redir_active], 1
        jmp .spr_scan

.spr_in_err_pop:
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .spr_in_err_pop_done
        call gff_restore_cwd
.spr_in_err_pop_done:
        pop esi
.spr_in_err:
        call shell_redir_reset
        mov esi, msg_redir_in_err
        call vga_print
        popad
        stc
        ret

.spr_in_big_pop:
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .spr_in_big_pop_done
        call gff_restore_cwd
.spr_in_big_pop_done:
        pop esi
        call shell_redir_reset
        mov esi, msg_redir_too_big
        call vga_print
        popad
        stc
        ret

.spr_syntax_err:
        call shell_redir_reset
        mov esi, msg_redir_syntax
        call vga_print
        popad
        stc
        ret

.spr_ok:
        popad
        clc
        ret

shell_finish_redirection:
        pushad
        cmp byte [stdout_redir_active], 1
        jne .sfr_reset

        mov eax, [stdout_redir_len]
        mov byte [redir_out_buf + eax], 0

        cmp byte [stdout_redir_append], 1
        jne .sfr_write_new

        mov esi, redir_out_name
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .sfr_append_new
        mov ebx, ecx
        mov eax, ebx
        add eax, [stdout_redir_len]
        cmp eax, PROGRAM_MAX_SIZE - 1
        ja .sfr_fail
        mov esi, redir_out_buf
        mov edi, PROGRAM_BASE
        add edi, ebx
        mov ecx, [stdout_redir_len]
        rep movsb
        mov esi, redir_out_name
        mov ecx, ebx
        add ecx, [stdout_redir_len]
        mov edi, PROGRAM_BASE
        movzx edx, byte [last_file_type]
        cmp edx, FTYPE_FREE
        jne .sfr_append_type_ok
        mov edx, FTYPE_TEXT
.sfr_append_type_ok:
        call hbfs_create_file
        jc .sfr_fail
        jmp .sfr_reset

.sfr_append_new:
.sfr_write_new:
        mov esi, redir_out_name
        mov ecx, [stdout_redir_len]
        mov edi, redir_out_buf
        mov edx, FTYPE_TEXT
        call hbfs_create_file
        jc .sfr_fail
        jmp .sfr_reset

.sfr_fail:
        call shell_redir_reset
        mov esi, msg_write_err
        call vga_print
        popad
        ret

.sfr_reset:
        call shell_redir_reset
        popad
        ret

;---------------------------------------
; Command: enter - Hex entry mode
;---------------------------------------
cmd_enter_hex:
        pushad
        mov edi, PROGRAM_BASE   ; Write hex data here

.hex_prompt:
        push edi
        mov byte [vga_color], COLOR_PROMPT
        mov al, 'h'
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT

        ; Read a line
        mov esi, temp_line_buf
        push esi
        mov edi, temp_line_buf
        xor ecx, ecx

.read_hex_line:
        call kb_getchar
        cmp al, 0x0D
        je .hex_line_done
        cmp al, 0x0A
        je .hex_line_done
        mov [edi], al
        inc edi
        inc ecx
        call vga_putchar
        jmp .read_hex_line

.hex_line_done:
        mov byte [edi], 0
        mov al, 0x0A
        call vga_putchar
        pop esi
        pop edi

        ; Empty line = end of hex input
        or ecx, ecx
        jz .hex_save

        ; Parse hex bytes from line
.parse_hex:
        call parse_hex_byte
        jc .hex_prompt          ; CF=set = end of input, get more
        stosb                   ; CF=clear = valid byte, store it
        jmp .parse_hex

.hex_save:
        ; Calculate size
        mov ecx, edi
        sub ecx, PROGRAM_BASE

        ; Ask for filename
        mov byte [vga_color], COLOR_PROMPT
        mov al, '*'
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT

        push ecx
        mov edi, filename_buf
        xor ecx, ecx
.read_name:
        call kb_getchar
        cmp al, 0x0D
        je .name_done
        cmp al, 0x0A
        je .name_done
        mov [edi], al
        inc edi
        inc ecx
        call vga_putchar
        jmp .read_name

.name_done:
        mov byte [edi], 0
        mov al, 0x0A
        call vga_putchar
        pop ecx

        ; Save file as executable
        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        mov edx, FTYPE_EXEC
        call hbfs_create_file
        jc .save_fail

        mov esi, msg_saved
        call vga_print
        popad
        ret

.save_fail:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_write_err
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; Command: copy - Copy a file
; Flags: -v  verbose (print filenames)
;---------------------------------------
cmd_copy_file:
        pushad

        ; Parse source filename
        mov edi, filename_buf
        call copy_word
        call skip_spaces

        ; Parse dest filename
        mov edi, filename_buf2
        call copy_word

        ; Validate args
        cmp byte [filename_buf], 0
        je .usage_error
        cmp byte [filename_buf2], 0
        je .usage_error

        ; Wildcard source?
        mov esi, filename_buf
        call str_has_wildcards
        jc .copy_wild

        ; Load source file
        push esi
        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .src_not_found

        ; ECX = file size already returned by hbfs_read_file

        ; Create dest file, preserving source type
        mov esi, filename_buf2
        mov edi, PROGRAM_BASE
        movzx edx, byte [last_file_type]
        call hbfs_create_file
        jc .copy_fail

        pop esi
        ; -v: print what was copied
        mov eax, 21            ; 'v' - 'a'
        call test_flag
        jnc .copy_quiet
        push esi
        mov esi, msg_copy_prefix
        call vga_print
        mov esi, filename_buf
        call vga_print
        mov esi, msg_copy_arrow
        call vga_print
        mov esi, filename_buf2
        call vga_print
        mov al, 0x0A
        call vga_putchar
        pop esi
.copy_quiet:
        mov esi, msg_copied
        call vga_print
        popad
        ret

.usage_error:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_copy_usage
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

.copy_wild:
        ; Count matches first
        call hbfs_load_root_dir
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
        xor ebx, ebx                    ; match count

.cw_count_loop:
        cmp ecx, 0
        je .cw_count_done
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .cw_count_next
        cmp byte [edi + DIRENT_TYPE], FTYPE_DIR
        je .cw_count_next

        push ecx
        push edi
        mov esi, filename_buf
        call wildcard_match
        pop edi
        pop ecx
        jnc .cw_count_next
        inc ebx

.cw_count_next:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jmp .cw_count_loop

.cw_count_done:
        cmp ebx, 0
        je .wild_not_found

        ; If multiple matches, destination must contain '*'
        cmp ebx, 1
        jle .cw_copy_begin
        mov esi, filename_buf2
        call str_has_asterisk
        jc .cw_copy_begin
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_wild_needs_star
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

.cw_copy_begin:
        call hbfs_load_root_dir
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
        xor ebx, ebx                    ; copied count

.cw_loop:
        cmp ecx, 0
        je .cw_done
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .cw_next
        cmp byte [edi + DIRENT_TYPE], FTYPE_DIR
        je .cw_next

        push ecx
        push edi
        mov esi, filename_buf
        call wildcard_match
        pop edi
        pop ecx
        jnc .cw_next

        ; source entry name -> wildcard_src_buf
        push ecx
        push edi
        mov esi, edi
        mov edi, wildcard_src_buf
        call str_copy
        pop edi
        pop ecx

        ; load source content
        push ecx
        push edi
        mov esi, wildcard_src_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        pop edi
        pop ecx
        jc .cw_next

        ; build destination name
        mov esi, filename_buf2
        call str_has_asterisk
        jc .cw_expand
        mov esi, filename_buf2
        mov edi, wildcard_dst_buf
        call str_copy
        jmp .cw_create

.cw_expand:
        mov esi, filename_buf2          ; template
        mov edi, wildcard_dst_buf       ; output
        mov edx, wildcard_src_buf       ; replacement text
        call wildcard_expand_star

.cw_create:
        push ecx
        push edi
        mov esi, wildcard_dst_buf
        mov edi, PROGRAM_BASE
        movzx edx, byte [last_file_type]
        call hbfs_create_file
        pop edi
        pop ecx
        jc .cw_next
        inc ebx

.cw_next:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jmp .cw_loop

.cw_done:
        cmp ebx, 0
        je .wild_not_found
        mov esi, msg_copied
        call vga_print
        popad
        ret

.wild_not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

.src_not_found:
        pop esi
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

.copy_fail:
        pop esi
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_write_err
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

;---------------------------------------
; Command: ren - Rename a file
; Usage: ren oldname newname
;---------------------------------------
cmd_rename_file:
        pushad

        ; Parse old filename
        mov edi, filename_buf
        call copy_word
        call skip_spaces

        ; Parse new filename
        mov edi, filename_buf2
        call copy_word

        ; Validate args
        cmp byte [filename_buf], 0
        je .ren_usage
        cmp byte [filename_buf2], 0
        je .ren_usage

        ; Wildcard source pattern?
        mov esi, filename_buf
        call str_has_wildcards
        jc .ren_wild

        ; Find old file in directory (global search)
        mov esi, filename_buf
        call hbfs_find_file_global
        jc .ren_not_found

        ; Copy new name into entry (max 252 chars)
        push edi
        mov esi, filename_buf2
        xor ecx, ecx
.copy_new_name:
        cmp ecx, HBFS_MAX_FILENAME
        jge .name_truncate
        lodsb
        stosb
        inc ecx
        test al, al
        jnz .copy_new_name
        jmp .name_copied
.name_truncate:
        mov byte [edi], 0      ; Force null-terminate
.name_copied:
        pop edi

        ; Update modified timestamp
        mov eax, [tick_count]
        mov [edi + 272], eax

        call hbfs_save_root_dir

        ; Restore CWD if global search changed it
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .ren_no_restore
        call gff_restore_cwd
.ren_no_restore:
        mov esi, msg_renamed
        call vga_print
        popad
        ret

.ren_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_ren_usage
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

.ren_wild:
        ; Count matches first
        call hbfs_load_root_dir
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
        xor ebx, ebx                    ; match count

.rw_count_loop:
        cmp ecx, 0
        je .rw_count_done
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .rw_count_next

        push ecx
        push edi
        mov esi, filename_buf
        call wildcard_match
        pop edi
        pop ecx
        jnc .rw_count_next
        inc ebx

.rw_count_next:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jmp .rw_count_loop

.rw_count_done:
        cmp ebx, 0
        je .ren_not_found

        ; If multiple matches, destination must contain '*'
        cmp ebx, 1
        jle .rw_do
        mov esi, filename_buf2
        call str_has_asterisk
        jc .rw_do
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_wild_needs_star
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

.rw_do:
        call hbfs_load_root_dir
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
        xor ebx, ebx                    ; renamed count

.rw_loop:
        cmp ecx, 0
        je .rw_done
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        je .rw_next

        push ecx
        push edi
        mov esi, filename_buf
        call wildcard_match
        pop edi
        pop ecx
        jnc .rw_next

        ; source entry name -> wildcard_src_buf
        push ecx
        push edi
        mov esi, edi
        mov edi, wildcard_src_buf
        call str_copy
        pop edi
        pop ecx

        ; build destination name
        mov esi, filename_buf2
        call str_has_asterisk
        jc .rw_expand
        mov esi, filename_buf2
        mov edi, wildcard_dst_buf
        call str_copy
        jmp .rw_write

.rw_expand:
        mov esi, filename_buf2
        mov edi, wildcard_dst_buf
        mov edx, wildcard_src_buf
        call wildcard_expand_star

.rw_write:
        ; Copy new name into matched entry
        push ecx
        push edi
        mov esi, wildcard_dst_buf
        xor edx, edx
.rw_copy_name:
        cmp edx, HBFS_MAX_FILENAME
        jge .rw_name_trunc
        lodsb
        mov [edi + edx], al
        inc edx
        test al, al
        jnz .rw_copy_name
        jmp .rw_name_done
.rw_name_trunc:
        mov byte [edi + HBFS_MAX_FILENAME], 0
.rw_name_done:
        mov eax, [tick_count]
        mov [edi + DIRENT_MODIFIED], eax
        pop edi
        pop ecx
        inc ebx

.rw_next:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jmp .rw_loop

.rw_done:
        call hbfs_save_root_dir
        mov esi, msg_renamed
        call vga_print
        popad
        ret

.ren_not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
