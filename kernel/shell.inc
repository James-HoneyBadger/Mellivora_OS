;=======================================================================
; COMMAND SHELL
;=======================================================================

shell_main:
        mov esp, KERNEL_STACK

        ; If control returned from Burrows, force one final console reset
        ; here after the GUI has fully unwound.
        cmp byte [vbe_restore_pending], 1
        jne .shell_console_ready
        mov byte [vbe_restore_pending], 0
        call vbe_restore_text
        call vga_init
.shell_console_ready:

        mov byte [vga_color], COLOR_DEFAULT
        mov eax, [program_exit_code]
        mov [shell_last_status], eax

        ; Print elapsed time if "time <cmd>" was active
        cmp byte [time_active], 1
        jne .no_time_report
        call shell_print_elapsed
.no_time_report:

        ; Re-launch parent program if child was exec'd from user-mode
        cmp byte [parent_active], 0
        je .no_parent_resume
        mov byte [parent_active], 0
        mov esi, parent_prog_name
        call cmd_exec_program
        ; If exec fails, fall through to normal shell prompt
.no_parent_resume:

        ; Resume batch script if interrupted by external program exit
        cmp byte [batch_running], 1
        jne .no_batch_resume
        mov esi, batch_script_buf
        add esi, [batch_resume_pos]
        call batch_run_loop
.no_batch_resume:

        ; Set default PATH
        mov esi, default_path
        call env_set_str

        ; Show MOTD on first boot
        cmp byte [boot_motd_shown], 0
        jne .no_motd
        mov byte [boot_motd_shown], 1
        mov byte [vga_color], COLOR_INFO
        mov esi, motd_str
        call vga_print

        ; Print boot time
        mov esi, msg_boot_time
        call vga_print
        mov eax, [tick_count]
        xor edx, edx
        mov ebx, PIT_HZ
        div ebx                 ; EAX = whole seconds, EDX = remainder
        push edx
        call vga_print_dec
        mov al, '.'
        call vga_putchar
        pop eax
        ; Convert remainder to centiseconds: rem * 100 / PIT_HZ
        imul eax, 100
        xor edx, edx
        mov ebx, PIT_HZ
        div ebx
        cmp eax, 10
        jge .bt_no_pad
        push eax
        mov al, '0'
        call vga_putchar
        pop eax
.bt_no_pad:
        call vga_print_dec
        mov esi, msg_boot_suf
        call vga_print

        mov byte [vga_color], COLOR_DEFAULT
.no_motd:

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
        mov byte [alias_expanding], 0   ; Reset alias expansion guard

        ; Read command line
        mov edi, line_buffer
        xor ecx, ecx           ; Character count
        mov dword [line_len], 0 ; Total chars in buffer (tracks end past cursor)

        ; On first boot, flush stale PS/2 data from the keyboard buffer.
        ; The 8042 controller may deliver delayed bytes after sti that
        ; the IRQ1 handler stores before the shell is ready to read.
        cmp byte [boot_kb_flushed], 0
        jne .read_loop
        mov byte [boot_kb_flushed], 1
        ; Drain any remaining 8042 output buffer bytes
.boot_drain:
        in al, 0x64
        test al, 1
        jz .boot_drain_done
        in al, 0x60
        jmp .boot_drain
.boot_drain_done:
        mov dword [kb_read_idx], 0
        mov dword [kb_write_idx], 0

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

        ; v4.0: Enhanced line editing shortcuts
        cmp al, 0x01            ; Ctrl+A - move to beginning of line
        je .ctrl_a
        cmp al, 0x05            ; Ctrl+E - move to end of line
        je .ctrl_e
        cmp al, 0x0B            ; Ctrl+K - kill from cursor to end of line
        je .ctrl_k
        cmp al, 0x15            ; Ctrl+U - kill from cursor to beginning of line
        je .ctrl_u
        cmp al, 0x17            ; Ctrl+W - delete previous word
        je .ctrl_w
        cmp al, 0x0C            ; Ctrl+L - clear screen, redraw prompt
        je .ctrl_l

        ; Store character if buffer not full
        cmp ecx, LINE_BUFFER_SIZE - 1
        jge .read_loop
        stosb
        inc ecx
        inc dword [line_len]    ; Keep total-length tracker in sync
        ; Echo character
        call vga_putchar
        jmp .read_loop

; v4.0: Ctrl+A - cursor to beginning of line (visually backspace to prompt)
.ctrl_a:
        or ecx, ecx
        jz .read_loop
.ctrl_a_loop:
        cmp ecx, 0
        je .read_loop
        push ecx
        mov al, 0x08
        call vga_putchar
        pop ecx
        dec edi
        dec ecx
        jmp .ctrl_a_loop

; v4.0: Ctrl+E - cursor to end (no-op in simple append mode, but redraws line)
.ctrl_e:
        ; In append-only mode, cursor is always at end; just continue
        jmp .read_loop

; v4.0: Ctrl+K - kill from cursor to end of line
.ctrl_k:
        ; Compute chars after cursor: line_len - ecx
        mov eax, [line_len]
        sub eax, ecx
        jz .read_loop           ; nothing after cursor
        ; Blank them on screen (print space, then backspace)
        push eax
.ctrl_k_blank:
        push eax
        mov al, ' '
        call vga_putchar
        pop eax
        dec eax
        jnz .ctrl_k_blank
        ; Move cursor back to kill point
        pop eax
        mov eax, [line_len]
        sub eax, ecx
.ctrl_k_back:
        push eax
        mov al, 0x08
        call vga_putchar
        pop eax
        dec eax
        jnz .ctrl_k_back
        ; Null-terminate at cursor, update line_len
        mov byte [edi], 0
        mov [line_len], ecx
        jmp .read_loop

; v4.0: Ctrl+U - kill from beginning to cursor (erase whole line)
.ctrl_u:
        ; Erase ECX chars visually, then erase any chars after cursor too
        mov eax, [line_len]
        sub eax, ecx            ; chars after cursor
        push eax
        or ecx, ecx
        jz .ctrl_u_after
        call .erase_input       ; erase before-cursor chars (ECX)
.ctrl_u_after:
        pop eax
        jz .ctrl_u_done
.ctrl_u_after_blank:
        push eax
        mov al, ' '
        call vga_putchar
        pop eax
        dec eax
        jnz .ctrl_u_after_blank
        ; backspace back
        mov eax, [line_len]
        sub eax, ecx
.ctrl_u_back:
        push eax
        mov al, 0x08
        call vga_putchar
        pop eax
        dec eax
        jnz .ctrl_u_back
.ctrl_u_done:
        mov edi, line_buffer
        xor ecx, ecx
        mov dword [line_len], 0
        jmp .read_loop

; v4.0: Ctrl+W - delete previous word
.ctrl_w:
        or ecx, ecx
        jz .read_loop
        ; Skip trailing spaces
.ctrl_w_spaces:
        cmp ecx, 0
        je .read_loop
        cmp byte [edi - 1], ' '
        jne .ctrl_w_word
        dec edi
        dec ecx
        push ecx
        mov al, 0x08
        call vga_putchar
        pop ecx
        jmp .ctrl_w_spaces
.ctrl_w_word:
        ; Delete non-space characters
        cmp ecx, 0
        je .read_loop
        cmp byte [edi - 1], ' '
        je .read_loop
        dec edi
        dec ecx
        push ecx
        mov al, 0x08
        call vga_putchar
        pop ecx
        jmp .ctrl_w_word

; v4.0: Ctrl+L - clear screen and redraw prompt + current input
.ctrl_l:
        push ecx
        push edi
        call vga_clear
        ; Redraw prompt
        mov byte [vga_color], COLOR_PROMPT
        mov esi, shell_prompt_pre
        call vga_print
        mov edi, path_search_buf
        call build_cwd_path
        mov esi, path_search_buf
        call vga_print
        mov esi, shell_prompt_post
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        ; Redraw current input
        pop edi
        pop ecx
        push ecx
        mov esi, line_buffer
        mov edx, ecx
.ctrl_l_redraw:
        cmp edx, 0
        je .ctrl_l_done
        lodsb
        push edx
        call vga_putchar
        pop edx
        dec edx
        jmp .ctrl_l_redraw
.ctrl_l_done:
        pop ecx
        jmp .read_loop

.backspace:
        or ecx, ecx
        jz .read_loop
        dec edi
        dec ecx
        dec dword [line_len]
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
        mov [line_len], ecx     ; cursor at end after tab-complete
        lea edi, [line_buffer + ecx]
        mov [line_len], ecx     ; cursor at end after tab-complete
        jmp .read_loop

.ctrl_c:
        ; Print ^C and cancel current input
        push esi
        mov esi, msg_ctrl_c
        call vga_print
        pop esi
        mov edi, line_buffer
        xor ecx, ecx
        mov dword [line_len], 0
        jmp .prompt

.hist_clear_line:
        ; Erase current input on screen
        call .erase_input
        mov edi, line_buffer
        xor ecx, ecx
        mov dword [line_len], 0
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
        mov [line_len], ecx     ; cursor == end after recall
        jmp .read_loop

.erase_input:
        ; Erase entire visible input line regardless of cursor position.
        ; Step 1: advance to end of line (over after-cursor chars)
        push ecx
        mov eax, [line_len]
        sub eax, ecx            ; chars after cursor
        jz .ei_no_fwd
.ei_fwd:
        push eax
        mov al, ' '
        call vga_putchar        ; overwrite and advance
        pop eax
        dec eax
        jnz .ei_fwd
.ei_no_fwd:
        ; Step 2: cursor is now at end; bs-space-bs over all line_len chars
        mov ecx, [line_len]
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

        ; --- History recall: !! and !n ---
        cmp byte [line_buffer], '!'
        jne .no_hist_recall
        cmp byte [line_buffer+1], '!'
        je .recall_last
        ; Check for !n (digit)
        movzx eax, byte [line_buffer+1]
        cmp al, '0'
        jb .no_hist_recall
        cmp al, '9'
        ja .no_hist_recall
        ; Parse number from line_buffer+1
        lea esi, [line_buffer+1]
        xor eax, eax
.parse_bang_n:
        movzx edx, byte [esi]
        cmp dl, '0'
        jb .bang_n_done
        cmp dl, '9'
        ja .bang_n_done
        imul eax, 10
        sub dl, '0'
        movzx edx, dl
        add eax, edx
        inc esi
        jmp .parse_bang_n
.bang_n_done:
        ; EAX = 1-based history number
        dec eax                 ; convert to 0-based index
        cmp eax, 0
        jl .bad_hist_ref
        cmp eax, [hist_count]
        jge .bad_hist_ref
        jmp .do_recall

.recall_last:
        mov eax, [hist_count]
        dec eax
        cmp eax, 0
        jl .bad_hist_ref

.do_recall:
        ; Copy hist_buf[eax] to line_buffer
        imul eax, HIST_ENTRY_SIZE
        lea esi, [hist_buf + eax]
        mov edi, line_buffer
        xor ecx, ecx
.recall_copy:
        lodsb
        stosb
        cmp al, 0
        je .recall_show
        inc ecx
        jmp .recall_copy
.recall_show:
        ; Show the recalled command
        mov esi, line_buffer
        call vga_print
        call vga_newline
        jmp .no_hist_recall

.bad_hist_ref:
        mov esi, msg_bad_hist
        call vga_print
        jmp .prompt

.no_hist_recall:

        ; Save to history
        call shell_save_history

        ; Parse and execute command
        mov esi, line_buffer
        call shell_parse_cmd
        jmp .prompt

;---------------------------------------
; Save current line_buffer to history ring
;---------------------------------------
HIST_MAX        equ 128
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

        mov dword [shell_last_status], 0
        mov byte [bg_launch_flag], 0    ; Reset background flag

        ; Expand $(...) command substitution and $((...)) arithmetic first
        call shell_expand_cmdsubst

        ; --- Check for trailing '&' (background job request) ---
        call shell_strip_bg_ampersand

        call shell_execute_conditionals
        jc .parse_done

        call shell_execute_pipeline
        jc .parse_done

        call shell_prepare_redirection
        jc .parse_done

        ; Expand wildcards in arguments before dispatching
        call shell_expand_globs

        mov esi, line_buffer
        call skip_spaces

        cmp byte [esi], 0
        je .parse_finish

        call shell_dispatch_core
.parse_finish:
        call shell_finish_redirection
.parse_done:
        ret

shell_status_fail:
        mov dword [shell_last_status], 1
        ret

;---------------------------------------
; shell_print_elapsed - Print elapsed time since time_start_ticks
; Output: "real 1.23s" format (seconds with 2 decimal places)
;---------------------------------------
shell_print_elapsed:
        pushad
        mov byte [time_active], 0

        ; Calculate elapsed ticks
        mov eax, [tick_count]
        sub eax, [time_start_ticks]

        ; Convert ticks to centiseconds: ticks * 100 / PIT_HZ
        ; Since PIT_HZ=100, ticks already ARE centiseconds (10ms each)
        mov ecx, eax            ; ECX = centiseconds total

        ; Print prefix
        mov esi, msg_elapsed
        call vga_print

        ; Print whole seconds: centiseconds / 100
        mov eax, ecx
        xor edx, edx
        mov ebx, 100
        div ebx                 ; EAX = seconds, EDX = remainder
        push edx
        call vga_print_dec

        ; Print decimal point
        mov esi, msg_dot
        call vga_print

        ; Print fractional part (2 digits, zero-padded)
        pop eax                 ; remainder (0-99)
        cmp eax, 10
        jge .no_pad
        push eax
        mov al, '0'
        call vga_putchar
        pop eax
.no_pad:
        call vga_print_dec

        ; Print "s\n"
        mov esi, msg_s_nl
        call vga_print

        popad
        ret

;---------------------------------------
; shell_expand_globs - Pre-process line_buffer to expand
; wildcard arguments (* and ?) against directory entries.
;
; Copies the command name (first token) unchanged, then for
; each subsequent token, if it contains * or ?, expands it by
; matching against the current directory listing.
; Result is written back to line_buffer.
; If a glob matches nothing, the literal pattern is kept (POSIX).
;---------------------------------------
shell_expand_globs:
        pushad

        ; Skip if line_buffer is empty
        mov esi, line_buffer
        cmp byte [esi], 0
        je .seg_done

        ; Quick check: does line_buffer contain any wildcard chars at all?
        push esi
        mov esi, line_buffer
        call str_has_wildcards
        pop esi
        jnc .seg_done           ; No wildcards — nothing to do

        ; Load current directory for matching
        call hbfs_load_root_dir
        jc .seg_done            ; On error, skip expansion

        ; EDI -> output buffer (expand_buf)
        mov edi, expand_buf
        mov esi, line_buffer

        ; Copy command name (first token) as-is
        call skip_spaces
.seg_copy_cmd:
        lodsb
        cmp al, 0
        je .seg_finish
        cmp al, ' '
        je .seg_cmd_done
        stosb
        jmp .seg_copy_cmd
.seg_cmd_done:
        stosb                   ; Copy the space

        ; Process arguments
.seg_next_arg:
        ; Skip spaces
        cmp byte [esi], ' '
        jne .seg_check_arg
        movsb
        jmp .seg_next_arg

.seg_check_arg:
        cmp byte [esi], 0
        je .seg_finish

        ; Extract token into glob_token_buf
        push edi
        mov edi, glob_token_buf
        xor ecx, ecx
.seg_tok:
        mov al, [esi + ecx]
        cmp al, 0
        je .seg_tok_end
        cmp al, ' '
        je .seg_tok_end
        mov [edi + ecx], al
        inc ecx
        cmp ecx, 255
        jb .seg_tok
.seg_tok_end:
        mov byte [edi + ecx], 0
        pop edi                 ; Restore output pointer

        ; Does this token have wildcards?
        push esi
        mov esi, glob_token_buf
        call str_has_wildcards
        pop esi
        jnc .seg_copy_literal   ; No wildcards — copy as-is

        ; Expand against directory entries
        push esi
        mov dword [.seg_match_count], 0
        mov ebp, hbfs_dir_buf
        push edi                ; Save output pointer
        call hbfs_get_max_entries
        mov edx, eax            ; EDX = max entries

.seg_dir_loop:
        cmp edx, 0
        je .seg_dir_done
        cmp byte [ebp + DIRENT_TYPE], FTYPE_FREE
        je .seg_dir_next

        ; Match this entry name against pattern
        push esi
        push edi
        mov esi, glob_token_buf ; pattern
        mov edi, ebp            ; candidate = entry name (at start of dirent)
        call wildcard_match
        pop edi
        pop esi
        jnc .seg_dir_next

        ; Match! Append filename to output
        pop edi                 ; Restore output pointer
        ; Add space separator if not first match
        cmp dword [.seg_match_count], 0
        je .seg_no_sep
        mov al, ' '
        stosb
.seg_no_sep:
        ; Copy entry name
        push esi
        lea esi, [ebp]          ; Entry name at start of dirent
.seg_cp_name:
        lodsb
        cmp al, 0
        je .seg_cp_name_done
        stosb
        jmp .seg_cp_name
.seg_cp_name_done:
        pop esi
        inc dword [.seg_match_count]
        push edi                ; Save output pointer again

.seg_dir_next:
        add ebp, HBFS_DIR_ENTRY_SIZE
        dec edx
        jmp .seg_dir_loop

.seg_dir_done:
        pop edi                 ; Restore output pointer
        pop esi                 ; Restore line_buffer pointer

        ; If no matches, copy the literal glob pattern
        cmp dword [.seg_match_count], 0
        je .seg_copy_literal

        ; Skip past the token in source
        add esi, ecx            ; ECX still = token length
        jmp .seg_next_arg

.seg_copy_literal:
        ; Copy token as-is (no matches or no wildcards)
.seg_lit:
        lodsb
        cmp al, 0
        je .seg_finish
        cmp al, ' '
        je .seg_lit_space
        stosb
        jmp .seg_lit
.seg_lit_space:
        stosb
        jmp .seg_next_arg

.seg_finish:
        mov byte [edi], 0

        ; Copy expanded result back into line_buffer
        mov esi, expand_buf
        mov edi, line_buffer
        mov ecx, LINE_BUFFER_SIZE - 1
.seg_copy_back:
        lodsb
        stosb
        test al, al
        jz .seg_done
        dec ecx
        jnz .seg_copy_back
        mov byte [edi], 0

.seg_done:
        popad
        ret

.seg_match_count: dd 0

;---------------------------------------
; shell_expand_cmdsubst
; Expand $((expr)) arithmetic and $(cmd) command substitution in line_buffer.
; Scans for '$(' patterns, evaluates them, and splices results in-place.
;---------------------------------------
shell_expand_cmdsubst:
        pushad
        ; Copy line_buffer to expand_buf as working copy
        mov esi, line_buffer
        mov edi, expand_buf
        mov ecx, LINE_BUFFER_SIZE
        rep movsb
        ; Write result back to line_buffer using expand_buf as source
        mov esi, expand_buf
        mov edi, line_buffer
        xor ecx, ecx            ; ECX = write position in line_buffer
.ecs_scan:
        mov al, [esi]
        test al, al
        jz .ecs_finish
        cmp al, '$'
        jne .ecs_pass
        cmp byte [esi + 1], '('
        jne .ecs_pass
        ; Found '$(' — check for '$(('
        cmp byte [esi + 2], '('
        je .ecs_arith

        ; ------- Command substitution: $( cmd ) -------
        add esi, 2              ; Skip '$('
        ; Extract inner command up to matching ')'
        push edi
        push ecx
        mov edi, cmd_sub_work
        xor ecx, ecx
.ecs_cs_extract:
        mov al, [esi]
        test al, al
        jz .ecs_cs_unbalanced
        cmp al, ')'
        je .ecs_cs_extracted
        mov [edi + ecx], al
        inc ecx
        inc esi
        cmp ecx, LINE_BUFFER_SIZE - 1
        jb .ecs_cs_extract
.ecs_cs_extracted:
        inc esi                 ; Skip the ')'
        mov byte [edi + ecx], 0
        ; Execute command, capturing stdout to cmd_sub_buf
        push esi
        push edi
        ; Save redirection state
        push dword [stdout_redir_len]
        push eax                ; save EAX
        movzx eax, byte [stdout_redir_active]
        push eax
        ; Enable capture to cmd_sub_buf
        mov byte [stdout_redir_active], 1
        mov dword [stdout_redir_len], 0
        ; Copy inner command to line_buffer and run it
        mov esi, cmd_sub_work
        mov edi, line_buffer
        call str_copy
        ; Run inner command (non-recursive: use batch dispatch)
        call shell_dispatch_core
        call shell_finish_redirection
        ; Grab captured output length
        mov ecx, [stdout_redir_len]
        ; Restore redirection state
        pop eax
        mov [stdout_redir_active], al
        pop eax
        mov [stdout_redir_len], eax
        pop edi                 ; cmd_sub_work pointer (no longer needed)
        pop esi                 ; resume scan ESI
        ; Strip trailing newlines from captured output (cmd_sub_buf)
        push esi
        mov esi, redir_out_buf
        ; ECX = captured length
        test ecx, ecx
        jz .ecs_cs_empty
.ecs_strip_nl:
        cmp ecx, 0
        je .ecs_cs_copy_done
        mov al, [esi + ecx - 1]
        cmp al, 0x0A
        je .ecs_strip_nl_dec
        cmp al, 0x0D
        je .ecs_strip_nl_dec
        jmp .ecs_cs_copy_done
.ecs_strip_nl_dec:
        dec ecx
        jmp .ecs_strip_nl
.ecs_cs_copy_done:
        ; Copy to cmd_sub_buf
        push ecx
        mov edi, cmd_sub_buf
        push esi
        cld
        rep movsb
        mov byte [edi], 0
        pop esi
        pop ecx
.ecs_cs_empty:
        pop esi                 ; resume scan ESI (already past ')')
        pop ecx                 ; restore write position
        pop edi                 ; restore write pointer
        ; Splice captured output into output stream
        push esi
        mov esi, cmd_sub_buf
.ecs_cs_splice:
        lodsb
        test al, al
        jz .ecs_cs_splice_done
        mov [edi + ecx], al
        inc ecx
        jmp .ecs_cs_splice
.ecs_cs_splice_done:
        pop esi
        jmp .ecs_scan

.ecs_cs_unbalanced:
        ; Unbalanced $(, just pass through
        pop ecx
        pop edi
        sub esi, 2              ; back to '$'
        jmp .ecs_pass

        ; ------- Arithmetic expansion: $(( expr )) -------
.ecs_arith:
        add esi, 3              ; Skip '$(('
        ; Find closing '))'
        push edi
        push ecx
        mov edi, cmd_sub_work
        xor ecx, ecx
.ecs_ae_extract:
        mov al, [esi]
        test al, al
        jz .ecs_ae_unbalanced
        cmp al, ')'
        jne .ecs_ae_more
        cmp byte [esi + 1], ')'
        je .ecs_ae_extracted
.ecs_ae_more:
        mov [edi + ecx], al
        inc ecx
        inc esi
        cmp ecx, LINE_BUFFER_SIZE - 1
        jb .ecs_ae_extract
.ecs_ae_extracted:
        add esi, 2              ; Skip '))'
        mov byte [edi + ecx], 0
        ; Evaluate arithmetic expression from cmd_sub_work
        push esi
        mov esi, cmd_sub_work
        call shell_eval_arith   ; EAX = result
        pop esi
        pop ecx
        pop edi
        ; Convert EAX to decimal string and write into output
        push esi
        push eax
        call vga_dec_to_str     ; ESI = scratch, result in dec_str_buf
        mov esi, dec_str_buf
.ecs_ae_splice:
        lodsb
        test al, al
        jz .ecs_ae_splice_done
        mov [edi + ecx], al
        inc ecx
        jmp .ecs_ae_splice
.ecs_ae_splice_done:
        pop eax
        pop esi
        jmp .ecs_scan

.ecs_ae_unbalanced:
        pop ecx
        pop edi
        sub esi, 3
        jmp .ecs_pass

.ecs_pass:
        mov al, [esi]
        mov [edi + ecx], al
        inc ecx
        inc esi
        jmp .ecs_scan

.ecs_finish:
        mov byte [edi + ecx], 0
        popad
        ret

;---------------------------------------
; shell_eval_arith - Evaluate a simple arithmetic expression
; ESI = pointer to null-terminated expression (with whitespace allowed)
; Returns: EAX = integer result
; Supports: integers, +, -, *, /, % with proper precedence
;---------------------------------------
shell_eval_arith:
        push esi
        push ebx
        push ecx
        push edx
        call .sea_expr
        pop edx
        pop ecx
        pop ebx
        pop esi
        ret

.sea_skip_ws:
        cmp byte [esi], ' '
        je .sea_skip_ws_do
        cmp byte [esi], 9
        je .sea_skip_ws_do
        ret
.sea_skip_ws_do:
        inc esi
        jmp .sea_skip_ws

; parse_expr: parse addition / subtraction
.sea_expr:
        call .sea_term          ; EAX = left
.sea_expr_loop:
        call .sea_skip_ws
        mov bl, [esi]
        cmp bl, '+'
        je .sea_add
        cmp bl, '-'
        je .sea_sub
        ret
.sea_add:
        inc esi
        push eax
        call .sea_term
        pop ecx
        add eax, ecx
        jmp .sea_expr_loop
.sea_sub:
        inc esi
        push eax
        call .sea_term
        xchg eax, [esp]         ; swap: stack=term result, eax=left
        sub eax, [esp]
        add esp, 4
        jmp .sea_expr_loop

; parse_term: parse multiplication / division / modulo
.sea_term:
        call .sea_primary       ; EAX = left
.sea_term_loop:
        call .sea_skip_ws
        mov bl, [esi]
        cmp bl, '*'
        je .sea_mul
        cmp bl, '/'
        je .sea_div
        cmp bl, '%'
        je .sea_mod
        ret
.sea_mul:
        inc esi
        push eax
        call .sea_primary
        pop ecx
        imul eax, ecx
        jmp .sea_term_loop
.sea_div:
        inc esi
        push eax
        call .sea_primary
        mov ecx, eax            ; divisor
        pop eax                 ; dividend
        test ecx, ecx
        jz .sea_div_zero
        cdq
        idiv ecx
        jmp .sea_term_loop
.sea_div_zero:
        xor eax, eax
        jmp .sea_term_loop
.sea_mod:
        inc esi
        push eax
        call .sea_primary
        mov ecx, eax
        pop eax
        test ecx, ecx
        jz .sea_mod_zero
        cdq
        idiv ecx
        mov eax, edx            ; remainder
        jmp .sea_term_loop
.sea_mod_zero:
        xor eax, eax
        jmp .sea_term_loop

; parse_primary: parse number or parenthesized expression
.sea_primary:
        call .sea_skip_ws
        mov bl, [esi]
        cmp bl, '('
        je .sea_paren
        cmp bl, '-'
        je .sea_unary_neg
        ; Parse decimal number
        xor eax, eax
.sea_digits:
        mov bl, [esi]
        cmp bl, '0'
        jb .sea_prim_done
        cmp bl, '9'
        ja .sea_prim_done
        imul eax, 10
        sub bl, '0'
        movzx ecx, bl
        add eax, ecx
        inc esi
        jmp .sea_digits
.sea_prim_done:
        ret
.sea_paren:
        inc esi                 ; skip '('
        call .sea_expr
        call .sea_skip_ws
        cmp byte [esi], ')'
        jne .sea_paren_done
        inc esi                 ; skip ')'
.sea_paren_done:
        ret
.sea_unary_neg:
        inc esi
        call .sea_primary
        neg eax
        ret

;---------------------------------------
; vga_dec_to_str - Convert EAX to decimal string in dec_str_buf
; EAX = value to convert
;---------------------------------------
vga_dec_to_str:
        pushad
        mov edi, dec_str_buf + 15
        mov byte [edi], 0
        dec edi
        xor ecx, ecx
        test eax, eax
        jns .dts_positive
        neg eax
        inc ecx                 ; ECX = 1 means negative
.dts_positive:
        test eax, eax
        jnz .dts_loop
        ; Special case: 0
        mov byte [edi], '0'
        dec edi
        jmp .dts_done
.dts_loop:
        test eax, eax
        jz .dts_sign
        mov edx, 0
        push ecx
        mov ecx, 10
        div ecx
        pop ecx
        add dl, '0'
        mov [edi], dl
        dec edi
        jmp .dts_loop
.dts_sign:
        test ecx, ecx
        jz .dts_done
        mov byte [edi], '-'
        dec edi
.dts_done:
        inc edi
        ; Copy result to start of dec_str_buf for easy access
        push esi
        mov esi, edi
        mov edi, dec_str_buf
        call str_copy
        pop esi
        popad
        ret

;---------------------------------------
; Check for trailing '&' (not '&&') in line_buffer.
; If found, strip it and set bg_launch_flag = 1.
;---------------------------------------
shell_strip_bg_ampersand:
        pushad
        mov esi, line_buffer
        ; Find end of input
        xor ecx, ecx
.sba_len:
        cmp byte [esi + ecx], 0
        je .sba_got_len
        inc ecx
        jmp .sba_len
.sba_got_len:
        test ecx, ecx
        jz .sba_done                ; empty string
        ; Scan backward past trailing spaces
.sba_skip_sp:
        dec ecx
        js .sba_done                ; all spaces
        cmp byte [esi + ecx], ' '
        je .sba_skip_sp
        ; ECX = index of last non-space char
        cmp byte [esi + ecx], '&'
        jne .sba_done               ; no trailing &
        ; Check it's not '&&': preceding char must not be '&'
        test ecx, ecx
        jz .sba_is_bg               ; single '&' at very start → bg
        cmp byte [esi + ecx - 1], '&'
        je .sba_done                ; it's '&&', leave alone
.sba_is_bg:
        ; Strip the '&' (null-terminate there) and set bg launch flag
        mov byte [esi + ecx], 0
        mov byte [bg_launch_flag], 1
.sba_done:
        popad
        ret

shell_execute_conditionals:
        pushad
        mov esi, line_buffer
.sec_scan:
        mov al, [esi]
        test al, al
        jz .sec_none
        cmp al, '&'
        jne .sec_try_or
        cmp byte [esi + 1], '&'
        je .sec_found
.sec_try_or:
        cmp al, '|'
        jne .sec_scan_next
        cmp byte [esi + 1], '|'
        je .sec_found
.sec_scan_next:
        inc esi
        jmp .sec_scan

.sec_none:
        popad
        clc
        ret

.sec_found:
        mov esi, line_buffer
        mov edi, cond_work_buf
        call str_copy
        mov byte [cond_prev_op], 0
        mov esi, cond_work_buf

.sec_stage:
        call skip_spaces
        cmp byte [esi], 0
        je .sec_done

        mov edi, esi
.sec_find_end:
        mov al, [esi]
        test al, al
        jz .sec_last
        cmp al, '&'
        jne .sec_check_or
        cmp byte [esi + 1], '&'
        je .sec_split_and
.sec_check_or:
        cmp al, '|'
        jne .sec_advance
        cmp byte [esi + 1], '|'
        je .sec_split_or
.sec_advance:
        inc esi
        jmp .sec_find_end

.sec_split_and:
        mov byte [esi], 0
        lea ebx, [esi + 2]
        mov byte [cond_next_op], '&'
        jmp .sec_maybe_exec

.sec_split_or:
        mov byte [esi], 0
        lea ebx, [esi + 2]
        mov byte [cond_next_op], '|'
        jmp .sec_maybe_exec

.sec_last:
        xor ebx, ebx
        mov byte [cond_next_op], 0

.sec_maybe_exec:
        cmp byte [cond_prev_op], '&'
        jne .sec_check_prev_or
        cmp dword [shell_last_status], 0
        jne .sec_continue
        jmp .sec_exec
.sec_check_prev_or:
        cmp byte [cond_prev_op], '|'
        jne .sec_exec
        cmp dword [shell_last_status], 0
        je .sec_continue

.sec_exec:
        mov esi, edi
        call skip_spaces
        cmp byte [esi], 0
        je .sec_syntax
        mov edi, line_buffer
        call str_copy
        mov esi, line_buffer
        call shell_parse_cmd

.sec_continue:
        mov al, [cond_next_op]
        mov [cond_prev_op], al
        cmp ebx, 0
        je .sec_done
        mov esi, ebx
        jmp .sec_stage

.sec_syntax:
        call shell_status_fail
        mov esi, msg_redir_syntax
        call vga_print
        popad
        stc
        ret

.sec_done:
        popad
        stc
        ret

shell_dispatch_core:
        ; --- Alias expansion ---
        ; Extract first word, check alias table
        ; Guard: only expand once per command to prevent infinite recursion
        cmp byte [alias_expanding], 1
        je .alias_skip_nofix
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
        mov byte [alias_expanding], 1  ; Prevent re-expansion
        jmp shell_parse_cmd  ; Re-parse with expanded command
.alias_skip:
        pop esi
.alias_skip_nofix:
        mov byte [alias_expanding], 0  ; Reset for next command

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

        mov edi, cmd_net_str
        call str_starts_with
        jc .cmd_net

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

        mov edi, cmd_pushd_str
        call str_starts_with
        jc .cmd_pushd

        mov edi, cmd_popd_str
        call str_starts_with
        jc .cmd_popd

        mov edi, cmd_dirs_str
        call str_starts_with
        jc .cmd_dirs

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

        mov edi, cmd_reboot_str
        call str_starts_with
        jc .cmd_reboot

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

        mov edi, cmd_mouse_str
        call str_starts_with
        jc .cmd_mouse

        mov edi, cmd_gui_str
        call str_starts_with
        jc .cmd_gui

        mov edi, cmd_ping_str
        call str_starts_with
        jc .cmd_ping

        mov edi, cmd_ifconfig_str
        call str_starts_with
        jc .cmd_ifconfig

        mov edi, cmd_dhcp_str
        call str_starts_with
        jc .cmd_dhcp

        mov edi, cmd_arp_str
        call str_starts_with
        jc .cmd_arp

        mov edi, cmd_scrsaver_str
        call str_starts_with
        jc .cmd_scrsaver

        mov edi, cmd_whoami_str
        call str_starts_with
        jc .cmd_whoami

        mov edi, cmd_stat_str
        call str_starts_with
        jc .cmd_stat

        mov edi, cmd_fsck_str
        call str_starts_with
        jc .cmd_fsck

        mov edi, cmd_ln_str
        call str_starts_with
        jc .cmd_ln

        mov edi, cmd_chmod_str
        call str_starts_with
        jc .cmd_chmod

        mov edi, cmd_chown_str
        call str_starts_with
        jc .cmd_chown

        ; v4.0 commands
        mov edi, cmd_jobs_str
        call str_starts_with
        jc .cmd_jobs

        mov edi, cmd_kill_str
        call str_starts_with
        jc .cmd_kill

        mov edi, cmd_ps_str
        call str_starts_with
        jc .cmd_ps

        mov edi, cmd_bg_str
        call str_starts_with
        jc .cmd_bg

        mov edi, cmd_fg_str
        call str_starts_with
        jc .cmd_fg

        mov edi, cmd_nice_str
        call str_starts_with
        jc .cmd_nice

        mov edi, cmd_export_str
        call str_starts_with
        jc .cmd_export

        mov edi, cmd_source_str
        call str_starts_with
        jc .cmd_source

        mov edi, cmd_dotcmd_str
        call str_starts_with
        jc .cmd_source

        ; Unknown command - try to execute as program
        mov esi, line_buffer
        call cmd_exec_program
        jc .unknown
        ret

.unknown:
        call shell_status_fail
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_unknown_cmd
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        ret

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

        mov edi, cmd_mouse_str
        call str_compare
        jz .cmd_help_mouse

        mov edi, cmd_gui_str
        call str_compare
        jz .cmd_help_gui

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
.cmd_help_mouse:
        mov esi, help_help_mouse
        call vga_print
        ret
.cmd_help_gui:
        mov esi, help_help_gui
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
        call vbe_restore_text
        call vga_init
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
        call shell_status_fail
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
        ; If no arguments, show uptime
        cmp byte [esi], 0
        jne .cmd_time_cmd

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

.cmd_time_cmd:
        ; "time <command>" — measure execution duration
        ; Copy remaining args into line_buffer for re-dispatch
        mov edi, line_buffer
        mov ecx, 255
.tc_copy:
        lodsb
        stosb
        test al, al
        jz .tc_go
        dec ecx
        jnz .tc_copy
.tc_go:
        ; Record start ticks and set flag
        mov eax, [tick_count]
        mov [time_start_ticks], eax
        mov byte [time_active], 1

        ; Re-dispatch the command in line_buffer
        mov esi, line_buffer
        call skip_spaces
        call shell_dispatch_core

        ; If we get here, it was a built-in (externals don't return)
        cmp byte [time_active], 1
        jne .tc_done
        call shell_print_elapsed
.tc_done:
        ret

.cmd_net:
        call cmd_net_status
        ret

.cmd_diskinfo:
        call cmd_show_disk
        ret

.cmd_run:
        call skip_spaces
        call cmd_exec_program
        jnc .cmd_run_done
        call shell_status_fail
.cmd_run_done:
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
        call skip_spaces
        cmp byte [esi], 0
        je .cmd_date_show_only
        call cmd_date_set
        ret
.cmd_date_show_only:
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

.cmd_pushd:
        call skip_spaces
        call cmd_pushd
        ret

.cmd_popd:
        call cmd_popd
        ret

.cmd_dirs:
        call cmd_dirs
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

.cmd_reboot:
        call cmd_reboot
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

.cmd_mouse:
        call cmd_mouse_status
        ret

.cmd_gui:
        call gui_desktop_main
        ; If Burrows returns directly instead of tail-jumping back into
        ; the shell, force the text console restore here as well.
        call vbe_restore_text
        call vga_init
        mov byte [vbe_restore_pending], 0
        ret

.cmd_whoami:
        cmp byte [login_authenticated], 1
        je .whoami_show
        mov esi, .whoami_none
        call vga_print
        ret
.whoami_show:
        mov esi, login_current_user
        call vga_print
        mov al, 0x0A
        call vga_putchar
        ret
.whoami_none: db "Not logged in", 0x0A, 0

.cmd_stat:
        call cmd_stat
        ret

.cmd_fsck:
        call cmd_fsck
        ret

.cmd_ln:
        call cmd_ln
        ret

.cmd_chmod:
        call cmd_chmod
        ret

.cmd_chown:
        call cmd_chown
        ret

;---- v4.0 command implementations ----

.cmd_ps:
        call cmd_ps_impl
        ret

.cmd_jobs:
        call cmd_ps_impl           ; jobs = ps for now (same view)
        ret

.cmd_kill:
        ; Parse: kill <pid> [signal]
        mov esi, line_buffer
        mov edi, cmd_kill_str
        call str_starts_with
        call skip_spaces
        cmp byte [esi], 0
        je .kill_usage
        ; Parse PID
        call parse_decimal
        test eax, eax
        jz .kill_usage
        mov ebx, eax            ; EBX = PID
        ; Check for optional signal number
        call skip_spaces
        cmp byte [esi], 0
        je .kill_default
        call parse_decimal
        mov ecx, eax            ; ECX = signal number
        jmp .kill_send
.kill_default:
        mov ecx, SIGTERM         ; Default signal = SIGTERM
.kill_send:
        mov eax, SYS_SIGNAL
        int 0x80
        cmp eax, 0
        jne .kill_failed
        mov esi, msg_kill_ok
        call vga_print
        ret
.kill_failed:
        mov esi, msg_kill_fail
        call vga_print
        call shell_status_fail
        ret
.kill_usage:
        mov esi, msg_kill_usage
        call vga_print
        ret

.cmd_bg:
        ; bg <pid> - resume a stopped task in background
        mov esi, line_buffer
        mov edi, cmd_bg_str
        call str_starts_with
        call skip_spaces
        cmp byte [esi], 0
        je .bg_usage
        call parse_decimal
        mov ebx, eax
        mov ecx, SIGCONT
        mov eax, SYS_SIGNAL
        int 0x80
        ret
.bg_usage:
        mov esi, msg_kill_usage
        call vga_print
        ret

.cmd_fg:
        ; fg <pid> - resume a stopped task (same as bg for now)
        mov esi, line_buffer
        mov edi, cmd_fg_str
        call str_starts_with
        call skip_spaces
        cmp byte [esi], 0
        je .bg_usage
        call parse_decimal
        mov ebx, eax
        mov ecx, SIGCONT
        mov eax, SYS_SIGNAL
        int 0x80
        ret

.cmd_nice:
        ; nice <priority> <command> - run command with given priority
        mov esi, line_buffer
        mov edi, cmd_nice_str
        call str_starts_with
        call skip_spaces
        cmp byte [esi], 0
        je .nice_usage
        ; Parse priority (0-3)
        call parse_decimal
        cmp eax, 3
        ja .nice_usage
        mov [nice_priority], eax
        ; Rest of line is the command to run
        call skip_spaces
        cmp byte [esi], 0
        je .nice_usage
        ; Copy rest to line_buffer and re-dispatch
        push esi
        mov edi, line_buffer
        call str_copy
        pop esi
        ; The program will inherit this priority via task_create
        mov esi, line_buffer
        call cmd_exec_program
        ret
.nice_usage:
        mov esi, msg_nice_usage
        call vga_print
        ret

.cmd_export:
        ; export NAME=VALUE (synonym for set)
        mov esi, line_buffer
        mov edi, cmd_export_str
        call str_starts_with
        call skip_spaces
        cmp byte [esi], 0
        je .export_usage
        ; Treat as env_set_str
        call env_set_str
        ret
.export_usage:
        mov esi, msg_export_usage
        call vga_print
        ret

.cmd_source:
        ; source <file> or . <file> - run batch script
        mov esi, line_buffer
        ; Skip command name
        cmp byte [esi], '.'
        je .source_dot
        mov edi, cmd_source_str
        call str_starts_with
        jmp .source_run
.source_dot:
        inc esi
.source_run:
        call skip_spaces
        cmp byte [esi], 0
        je .source_usage
        ; Copy filename to line_buffer so cmd_exec_batch can find it
        mov edi, line_buffer
        call str_copy
        mov esi, line_buffer
        call cmd_exec_batch
        ret
.source_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, .source_usage_str
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        ret
.source_usage_str: db "Usage: source <script>", 0x0A, 0

.cmd_ping:
        call cmd_ping
        ret

.cmd_ifconfig:
        call cmd_ifconfig
        ret

.cmd_dhcp:
        call cmd_dhcp_shell
        ret

.cmd_arp:
        call cmd_arp_show
        ret

.cmd_scrsaver:
        mov esi, line_buffer
        mov edi, cmd_scrsaver_str
        call str_starts_with
        call skip_spaces
        cmp byte [esi], 0
        je .scrsaver_show

        ; Parse mode number (0-4)
        movzx eax, byte [esi]
        sub eax, '0'
        cmp eax, 4
        ja .scrsaver_usage
        mov [scr_mode], al
        mov esi, msg_scrsaver_set
        call vga_print
        movzx eax, byte [scr_mode]
        add al, '0'
        call vga_putchar
        mov al, ' '
        call vga_putchar
        mov al, '('
        call vga_putchar
        movzx eax, byte [scr_mode]
        mov esi, [scr_mode_names + eax*4]
        call vga_print
        mov al, ')'
        call vga_putchar
        call vga_newline
        ret

.scrsaver_show:
        mov esi, msg_scrsaver_cur
        call vga_print
        movzx eax, byte [scr_mode]
        add al, '0'
        call vga_putchar
        mov al, ' '
        call vga_putchar
        mov al, '('
        call vga_putchar
        movzx eax, byte [scr_mode]
        mov esi, [scr_mode_names + eax*4]
        call vga_print
        mov al, ')'
        call vga_putchar
        call vga_newline
        mov esi, msg_scrsaver_help
        call vga_print
        ret

.scrsaver_usage:
        mov esi, msg_scrsaver_help
        call vga_print
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
        cmp dword [.diff_diffs], 0
        je .diff_ok
        call shell_status_fail
.diff_ok:
        popad
        ret

.diff_usage:
        call shell_status_fail
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_2files
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.diff_not_found1:
        call shell_status_fail
        mov byte [vga_color], COLOR_ERROR
        mov esi, filename_buf
        call vga_print
        mov esi, msg_not_found_w
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.diff_not_found2:
        call shell_status_fail
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
        jne .uniq_from_file
        call shell_use_stdin_as_input
        jc .uniq_usage
        jmp .uniq_loaded

.uniq_from_file:
        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .uniq_nf
        mov edi, PROGRAM_BASE
        add edi, ecx
        mov byte [edi], 0
.uniq_loaded:

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
        jne .rev_from_file
        call shell_use_stdin_as_input
        jc .rev_usage
        jmp .rev_loaded

.rev_from_file:
        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .rev_nf
        mov edi, PROGRAM_BASE
        add edi, ecx
        mov byte [edi], 0
.rev_loaded:

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
        jne .tac_from_file
        call shell_use_stdin_as_input
        jc .tac_usage
        jmp .tac_loaded

.tac_from_file:
        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .tac_nf
        mov edi, PROGRAM_BASE
        add edi, ecx
        mov byte [edi], 0
.tac_loaded:

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
        dd cmd_pushd_str, cmd_popd_str, cmd_dirs_str
        dd cmd_touch_str, cmd_set_str, cmd_unset_str, cmd_shutdown_str
        dd cmd_reboot_str
        dd cmd_cls_str, cmd_head_str, cmd_tail_str, cmd_type_str
        dd cmd_diff_str, cmd_uniq_str, cmd_rev_str, cmd_tac_str
        dd cmd_alias_str, cmd_history_str, cmd_which_str
        dd cmd_sleep_str, cmd_color_str, cmd_size_str, cmd_strings_str
        dd cmd_mouse_str
        dd cmd_gui_str
        dd cmd_scrsaver_str
        dd cmd_whoami_str
        dd cmd_stat_str
        dd cmd_fsck_str
        dd cmd_ln_str
        dd cmd_chmod_str
        dd cmd_chown_str
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
; Command: mouse - Show PS/2 mouse status
;---------------------------------------
cmd_mouse_status:
        pushad
        cmp byte [mouse_present], 0
        je .mouse_not_present

        mov byte [vga_color], COLOR_INFO
        mov esi, msg_mouse_hdr
        call vga_print

        ; Position
        mov esi, msg_mouse_pos
        call vga_print
        mov eax, [mouse_x]
        call vga_print_dec
        mov al, ','
        call vga_putchar
        mov al, ' '
        call vga_putchar
        mov eax, [mouse_y]
        call vga_print_dec
        call vga_newline

        ; Buttons
        mov esi, msg_mouse_btn
        call vga_print
        movzx eax, byte [mouse_buttons]
        test al, 0x01
        jz .no_left
        mov esi, msg_mouse_left
        call vga_print
.no_left:
        test byte [mouse_buttons], 0x02
        jz .no_right
        mov esi, msg_mouse_right
        call vga_print
.no_right:
        test byte [mouse_buttons], 0x04
        jz .no_middle
        mov esi, msg_mouse_mid
        call vga_print
.no_middle:
        ; If no buttons, print "none"
        cmp byte [mouse_buttons], 0
        jne .btn_done
        mov esi, msg_mouse_none
        call vga_print
.btn_done:
        call vga_newline
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

.mouse_not_present:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_mouse_absent
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

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
        cmp byte [edi + DIRENT_TYPE], FTYPE_LINK
        je .sc_link
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
        jmp .sc_print
.sc_link:
        mov byte [vga_color], COLOR_INFO
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
        cmp byte [edi + 253], FTYPE_LINK
        je .type_link
        ; Default: text file
        mov byte [vga_color], COLOR_DEFAULT
        mov esi, type_text
        call vga_print
        jmp .print_size
.type_link:
        mov byte [vga_color], COLOR_INFO
        mov esi, type_link
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

        ; Print modified timestamp as YYYY-MM-DD HH:MM
        push ebx
        mov ebx, [edi + 272]        ; DIRENT_MODIFIED (packed RTC)
        ; Extract year: bits 31-25 + 2000
        mov eax, ebx
        shr eax, 25
        and eax, 0x7F
        add eax, 2000
        call vga_print_dec
        mov al, '-'
        call vga_putchar
        ; Extract month: bits 24-21
        mov eax, ebx
        shr eax, 21
        and eax, 0x0F
        cmp eax, 10
        jge .mon_ok
        push eax
        mov al, '0'
        call vga_putchar
        pop eax
.mon_ok:
        call vga_print_dec
        mov al, '-'
        call vga_putchar
        ; Extract day: bits 20-16
        mov eax, ebx
        shr eax, 16
        and eax, 0x1F
        cmp eax, 10
        jge .day_ok
        push eax
        mov al, '0'
        call vga_putchar
        pop eax
.day_ok:
        call vga_print_dec
        mov al, ' '
        call vga_putchar
        ; Extract hour: bits 15-11
        mov eax, ebx
        shr eax, 11
        and eax, 0x1F
        cmp eax, 10
        jge .hour_ok
        push eax
        mov al, '0'
        call vga_putchar
        pop eax
.hour_ok:
        call vga_print_dec
        mov al, ':'
        call vga_putchar
        ; Extract minute: bits 10-5
        mov eax, ebx
        shr eax, 5
        and eax, 0x3F
        cmp eax, 10
        jge .tmin_ok
        push eax
        mov al, '0'
        call vga_putchar
        pop eax
.tmin_ok:
        call vga_print_dec
        pop ebx

        mov al, ' '
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

        ; Flush filesystem (write bitmap back to disk)
        mov esi, msg_shutdown_sync
        call vga_print
        mov eax, HBFS_BITMAP_START
        mov ecx, HBFS_BITMAP_SECTS
        mov esi, hbfs_bitmap_buf
        call ata_write_sectors

        ; Flush ATA write cache
        mov dx, ATA_CMD
        mov al, 0xE7            ; FLUSH CACHE command
        out dx, al
        mov ecx, 0xFFFFF
.flush_wait:
        in al, dx
        test al, 0x80           ; BSY?
        jz .flush_ok
        dec ecx
        jnz .flush_wait
.flush_ok:

        mov esi, msg_shutdown_done
        call vga_print

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
; Command: reboot - Warm reboot the system
;---------------------------------------
cmd_reboot:
        ; Print reboot banner
        mov al, 0x0A
        call vga_putchar
        mov byte [vga_color], COLOR_HEADER
        mov esi, msg_shutdown_bar
        call vga_print
        mov byte [vga_color], COLOR_INFO
        mov esi, msg_reboot
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT

        ; Small delay so user can see the message without feeling stalled
        mov ecx, 0x0800000
.reboot_delay:
        dec ecx
        jnz .reboot_delay

        cli

        ; Method 1: 8042 keyboard controller pulse reset.
        ; Wait until input buffer is clear before writing command 0xFE.
        mov ecx, 0x20000
.wait_kbc_ready:
        in al, 0x64
        test al, 0x02
        jz .kbc_ready
        dec ecx
        jnz .wait_kbc_ready
        jmp .try_cf9

.kbc_ready:
        mov al, 0xFE
        out 0x64, al

        ; Give hardware a short window to react.
        mov ecx, 0x200000
.wait_kbc_reset:
        dec ecx
        jnz .wait_kbc_reset

        ; Method 2: QEMU/PC chipset hard reset control register.
.try_cf9:
        mov dx, 0xCF9
        mov al, 0x02
        out dx, al
        mov al, 0x06
        out dx, al

        ; Method 3: Fast A20/reset register.
        in al, 0x92
        or al, 0x01
        out 0x92, al

        ; Method 4: Triple fault fallback.
        lidt [.reboot_null_idt]
        int 0

        ; Should never reach here
.reboot_halt:
        hlt
        jmp .reboot_halt

.reboot_null_idt:
        dw 0
        dd 0

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
        je .head_no_num
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
        cmp byte [esi], 0
        jne .head_from_file
        call shell_use_stdin_as_input
        jc .head_usage
        jmp .head_have_buf
.head_from_file:
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .head_not_found

        cmp ecx, PROGRAM_MAX_SIZE - 1
        jle .head_size_ok
        mov ecx, PROGRAM_MAX_SIZE - 1
.head_size_ok:
        mov byte [PROGRAM_BASE + ecx], 0
.head_have_buf:
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
        je .tail_no_num
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
        cmp byte [esi], 0
        jne .tail_from_file
        call shell_use_stdin_as_input
        jc .tail_usage
        jmp .tail_have_buf
.tail_from_file:
        ; Read file
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        jc .tail_not_found

        cmp ecx, PROGRAM_MAX_SIZE - 1
        jle .tail_size_ok
        mov ecx, PROGRAM_MAX_SIZE - 1
.tail_size_ok:
        mov byte [PROGRAM_BASE + ecx], 0
.tail_have_buf:

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
        cmp byte [esi], 0
        jne .cat_from_file
        call shell_use_stdin_as_input
        jc .cat_usage
        jmp .cat_loaded
.cat_from_file:
        mov edi, PROGRAM_BASE   ; Use program area as temp buffer
        call hbfs_read_file
        jc .not_found
.cat_loaded:

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

.cat_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
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
        ; Check if this should be a background job
        cmp byte [bg_launch_flag], 1
        je .exec_bg

        mov esi, msg_exec
        call vga_print

        ; Write SYS_EXIT trampoline at end of program area
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

.exec_bg:
        ; Launch program as a background scheduler task
        ; Clear foreground program_running flag (bg tasks don't block the shell)
        mov byte [program_running], 0
        ; 1. Allocate memory for a copy of the program (1MB = 256 pages)
        mov ecx, BG_PROG_PAGES
        call pmm_alloc_pages
        test eax, eax
        jz .bg_nomem
        mov [.bg_prog_addr], eax

        ; 2. Copy program from PROGRAM_BASE to allocated area
        push eax
        mov edi, eax
        mov esi, PROGRAM_BASE
        mov ecx, (BG_PROG_PAGES * 4096) / 4
        rep movsd
        pop eax                         ; EAX = bg_prog_addr again

        ; 3. Fix up entry point (relative to new base)
        mov ebx, [program_entry]
        sub ebx, PROGRAM_BASE
        add ebx, eax                    ; EBX = entry point in bg copy

        ; 4. Write SYS_EXIT trampoline at end of bg slot
        mov edi, eax
        add edi, (BG_PROG_PAGES * 4096) - 16
        mov [.bg_tramp_addr], edi
        mov byte  [edi],     0xB8       ; mov eax, SYS_EXIT(0)
        mov dword [edi + 1], 0
        mov byte  [edi + 5], 0xCD       ; int 0x80
        mov byte  [edi + 6], 0x80

        ; 5. Allocate user stack for bg task (64KB = 16 pages)
        push ebx
        mov ecx, BG_STACK_PAGES
        call pmm_alloc_pages
        test eax, eax
        jz .bg_nomem_pop
        mov [.bg_stack_addr], eax
        add eax, (BG_STACK_PAGES * 4096) - 4   ; Stack top - 4 for trampoline
        ; Push trampoline address as "return address" on ring-3 stack
        mov edx, [.bg_tramp_addr]
        mov [eax], edx
        ; ECX = ring-3 ESP (pointing at trampoline)
        mov ecx, eax
        pop ebx                         ; EBX = bg entry point

        ; 6. Create the task
        mov esi, prog_name_buf
        mov edi, task_create_name
        push ecx
        mov ecx, 16
        rep movsb
        pop ecx
        call sched_create_task          ; EBX=entry, ECX=stack → EAX=pid (or -1)
        test eax, eax
        js .bg_sched_fail

        ; 7. Mark the task as background in TCB (bit 31 of TCB_EXIT_CODE)
        ;    Store bg_prog_addr page number so sys_exit can free it.
        push eax                        ; save PID (= slot index)
        imul eax, TCB_SIZE
        lea edi, [task_table + eax]
        mov eax, [.bg_prog_addr]
        shr eax, 12                     ; Convert to page number
        or  eax, 0x80000000             ; Set bg_task flag (bit 31)
        mov [edi + TCB_EXIT_CODE], eax
        mov dword [edi + TCB_PRIORITY], PRIO_LOW  ; Run at low priority
        pop eax                         ; EAX = PID

        ; 8. Print "[1] <pid>" to confirm bg launch
        mov byte [bg_launch_flag], 0
        mov byte [vga_color], COLOR_INFO
        mov esi, msg_bg_launched
        call vga_print
        push eax
        call vga_print_dec
        mov al, 10
        call vga_putchar
        pop eax
        mov byte [vga_color], COLOR_DEFAULT

        popad
        clc
        ret

.bg_nomem_pop:
        pop ebx
.bg_nomem:
        mov byte [bg_launch_flag], 0
        mov esi, msg_bg_nomem
        call vga_print
        jmp .not_found

.bg_sched_fail:
        ; Free allocated pages before failing
        mov eax, [.bg_prog_addr]
        mov ecx, BG_PROG_PAGES
        call pmm_free_pages
        mov eax, [.bg_stack_addr]
        mov ecx, BG_STACK_PAGES
        call pmm_free_pages
        mov byte [bg_launch_flag], 0
        jmp .not_found

.bg_prog_addr:   dd 0
.bg_stack_addr:  dd 0
.bg_tramp_addr:  dd 0

.path_fsize: dd 0               ; Temp storage for file size during PATH search

.not_found:
        call shell_status_fail
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
        call shell_status_fail
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
        call shell_status_fail
        call shell_redir_reset
        mov esi, msg_redir_too_big
        call vga_print
        popad
        stc
        ret

.spr_syntax_err:
        call shell_status_fail
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
        call shell_status_fail
        call shell_redir_reset
        mov esi, msg_write_err
        call vga_print
        popad
        ret

.sfr_reset:
        call shell_redir_reset
        popad
        ret

shell_use_stdin_as_input:
        cmp byte [stdin_redir_active], 1
        jne .susi_no_input
        push edi
        mov ecx, [stdin_redir_len]
        cmp ecx, PROGRAM_MAX_SIZE - 1
        jle .susi_size_ok
        mov ecx, PROGRAM_MAX_SIZE - 1
.susi_size_ok:
        mov edx, ecx
        mov esi, redir_in_buf
        mov edi, PROGRAM_BASE
        rep movsb
        mov byte [PROGRAM_BASE + edx], 0
        mov [stdin_redir_pos], edx
        mov byte [stdin_redir_active], 0
        mov esi, PROGRAM_BASE
        mov ecx, edx
        pop edi
        clc
        ret
.susi_no_input:
        stc
        ret

shell_execute_pipeline:
        pushad
        mov esi, line_buffer
.sep_scan:
        mov al, [esi]
        test al, al
        jz .sep_none
        cmp al, '|'
        je .sep_found
        inc esi
        jmp .sep_scan

.sep_none:
        popad
        clc
        ret

.sep_found:
        mov esi, line_buffer
        mov edi, pipe_work_buf
        call str_copy
        mov byte [pipe_has_input], 0
        mov dword [pipe_input_len], 0
        mov esi, pipe_work_buf

.pipe_next_stage:
        call skip_spaces
        cmp byte [esi], 0
        je .pipe_done

        mov edi, esi
.pipe_find_end:
        mov al, [esi]
        test al, al
        jz .pipe_last_stage
        cmp al, '|'
        je .pipe_mid_stage
        inc esi
        jmp .pipe_find_end

.pipe_mid_stage:
        mov byte [esi], 0
        lea ebx, [esi + 1]
        mov esi, edi
        call skip_spaces
        cmp byte [esi], 0
        je .pipe_syntax
        mov edi, line_buffer
        call str_copy

        mov dl, [pipe_has_input]
        mov eax, [pipe_input_len]
        call shell_prepare_redirection
        jc .pipe_fail
        cmp dl, 0
        je .pipe_input_ready
        cmp byte [stdin_redir_active], 1
        je .pipe_input_ready
        mov byte [stdin_redir_active], 1
        mov dword [stdin_redir_len], eax
        mov dword [stdin_redir_pos], 0
.pipe_input_ready:
        mov byte [stdout_redir_active], 1
        mov byte [stdout_redir_append], 0
        mov dword [stdout_redir_len], 0
        mov byte [redir_out_name], 0
        mov byte [redir_out_buf], 0

        mov esi, line_buffer
        call skip_spaces
        cmp byte [esi], 0
        je .pipe_syntax
        call shell_dispatch_core

        mov eax, [stdout_redir_len]
        cmp eax, BATCH_BUFFER_SIZE - 1
        jbe .pipe_len_ok
        mov eax, BATCH_BUFFER_SIZE - 1
.pipe_len_ok:
        mov [pipe_input_len], eax
        mov byte [redir_out_buf + eax], 0
        mov esi, redir_out_buf
        mov edi, redir_in_buf
        mov ecx, eax
        rep movsb
        mov byte [redir_in_buf + eax], 0
        mov byte [pipe_has_input], 1
        mov byte [stdout_redir_active], 0
        mov byte [stdout_redir_append], 0
        mov dword [stdout_redir_len], 0

        mov esi, ebx
        jmp .pipe_next_stage

.pipe_last_stage:
        mov esi, edi
        call skip_spaces
        cmp byte [esi], 0
        je .pipe_syntax
        mov edi, line_buffer
        call str_copy

        mov dl, [pipe_has_input]
        mov eax, [pipe_input_len]
        call shell_prepare_redirection
        jc .pipe_fail
        cmp dl, 0
        je .pipe_last_input_ready
        cmp byte [stdin_redir_active], 1
        je .pipe_last_input_ready
        mov byte [stdin_redir_active], 1
        mov dword [stdin_redir_len], eax
        mov dword [stdin_redir_pos], 0
.pipe_last_input_ready:
        mov esi, line_buffer
        call skip_spaces
        cmp byte [esi], 0
        je .pipe_syntax
        call shell_dispatch_core
        call shell_finish_redirection

.pipe_done:
        call shell_redir_reset
        popad
        stc
        ret

.pipe_syntax:
        call shell_status_fail
        call shell_redir_reset
        mov esi, msg_redir_syntax
        call vga_print
        popad
        stc
        ret

.pipe_fail:
        call shell_status_fail
        call shell_redir_reset
        popad
        stc
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
        call rtc_get_timestamp
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

;---------------------------------------
; Command: stat <filename>
; Show detailed file information
;---------------------------------------
cmd_stat:
        pushad
        ; Skip leading spaces
.stat_skip:
        cmp byte [esi], ' '
        jne .stat_go
        inc esi
        jmp .stat_skip
.stat_go:
        cmp byte [esi], 0
        je .stat_usage

        ; Find file in current directory
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .stat_not_found
        ; EDI = pointer to directory entry

        ; Print "  File: <name>"
        mov esi, .stat_lbl_file
        call vga_print
        push edi
        mov esi, edi            ; filename at start of entry
        call vga_print
        pop edi
        mov al, 0x0A
        call vga_putchar

        ; Print "  Type: <type>"
        mov esi, .stat_lbl_type
        call vga_print
        movzx eax, byte [edi + 253]
        cmp eax, FTYPE_FILE
        je .stat_t_file
        cmp eax, FTYPE_DIR
        je .stat_t_dir
        cmp eax, FTYPE_EXEC
        je .stat_t_exec
        cmp eax, FTYPE_BATCH
        je .stat_t_batch
        cmp eax, FTYPE_LINK
        je .stat_t_link
        mov esi, .stat_t_unk
        jmp .stat_t_done
.stat_t_file:
        mov esi, .stat_t_file_s
        jmp .stat_t_done
.stat_t_dir:
        mov esi, .stat_t_dir_s
        jmp .stat_t_done
.stat_t_exec:
        mov esi, .stat_t_exec_s
        jmp .stat_t_done
.stat_t_batch:
        mov esi, .stat_t_batch_s
        jmp .stat_t_done
.stat_t_link:
        mov esi, .stat_t_link_s
        jmp .stat_t_done
.stat_t_done:
        call vga_print
        mov al, 0x0A
        call vga_putchar

        ; Print "  Size: <bytes> bytes"
        mov esi, .stat_lbl_size
        call vga_print
        mov eax, [edi + DIRENT_SIZE]
        call vga_print_dec
        mov esi, .stat_bytes
        call vga_print

        ; Print " (<N> blocks)"
        mov esi, .stat_lbl_paren
        call vga_print
        mov eax, [edi + DIRENT_BLOCK_COUNT]
        call vga_print_dec
        mov esi, .stat_blocks
        call vga_print

        ; Print "  Block: <start_block>"
        mov esi, .stat_lbl_block
        call vga_print
        mov eax, [edi + DIRENT_START_BLOCK]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar

        ; Print "  Created:  YYYY-MM-DD HH:MM:SS"
        mov esi, .stat_lbl_created
        call vga_print
        mov ebx, [edi + DIRENT_CREATED]
        call .stat_print_ts
        mov al, 0x0A
        call vga_putchar

        ; Print "  Modified: YYYY-MM-DD HH:MM:SS"
        mov esi, .stat_lbl_modified
        call vga_print
        mov ebx, [edi + DIRENT_MODIFIED]
        call .stat_print_ts
        mov al, 0x0A
        call vga_putchar

        ; Print "  Permissions: rwxrwxrwx (octal)"
        mov esi, .stat_lbl_perms
        call vga_print
        movzx eax, word [edi + DIRENT_PERMS]
        push edi
        push eax
        ; Print symbolic rwxrwxrwx
        mov edi, .stat_rwx_buf
        mov ecx, 9
        mov ebx, eax
.stat_rwx_loop:
        mov eax, 9
        sub eax, ecx            ; bit index 8..0
        bt ebx, eax
        jnc .stat_rwx_dash
        ; map bit position to r/w/x char: mod 3: 0=r,1=w,2=x
        mov eax, 9
        sub eax, ecx
        mov edx, eax
        mov eax, edx
        xor edx, edx
        push ecx
        mov ecx, 3
        div ecx
        pop ecx
        ; EDX = remainder: 0=r,1=w,2=x
        cmp edx, 0
        je .stat_rwx_r
        cmp edx, 1
        je .stat_rwx_w
        mov byte [edi], 'x'
        inc edi
        dec ecx
        jnz .stat_rwx_loop
        jmp .stat_rwx_done
.stat_rwx_r:
        mov byte [edi], 'r'
        inc edi
        dec ecx
        jnz .stat_rwx_loop
        jmp .stat_rwx_done
.stat_rwx_w:
        mov byte [edi], 'w'
        inc edi
        dec ecx
        jnz .stat_rwx_loop
        jmp .stat_rwx_done
.stat_rwx_dash:
        mov byte [edi], '-'
        inc edi
        dec ecx
        jnz .stat_rwx_loop
.stat_rwx_done:
        mov byte [edi], 0
        pop eax
        pop edi
        mov esi, .stat_rwx_buf
        call vga_print
        mov al, ' '
        call vga_putchar
        mov al, '('
        call vga_putchar
        movzx eax, word [edi + DIRENT_PERMS]
        ; Print octal: 3 digits (each 3 bits)
        push eax
        shr eax, 6
        and eax, 7
        add al, '0'
        call vga_putchar
        pop eax
        push eax
        shr eax, 3
        and eax, 7
        add al, '0'
        call vga_putchar
        pop eax
        and eax, 7
        add al, '0'
        call vga_putchar
        mov al, ')'
        call vga_putchar
        mov al, 0x0A
        call vga_putchar

        ; Print "  Owner:  <uid>"
        mov esi, .stat_lbl_owner
        call vga_print
        movzx eax, word [edi + DIRENT_OWNER]
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar

        ; If symlink, show target
        cmp byte [edi + 253], FTYPE_LINK
        jne .stat_done
        mov esi, .stat_lbl_target
        call vga_print
        ; Read symlink target from disk
        mov eax, [edi + DIRENT_START_BLOCK]
        shl eax, 3
        add eax, HBFS_DATA_START
        mov ecx, 1             ; 1 sector is enough for a path
        push edi
        mov edi, .ln_read_buf
        call ata_read_sectors
        pop edi
        jc .stat_done
        mov esi, .ln_read_buf
        call vga_print
        mov al, 0x0A
        call vga_putchar

.stat_done:
        popad
        ret

.stat_usage:
        mov esi, .stat_usage_s
        call vga_print
        popad
        ret

.stat_not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

; Print packed RTC timestamp in EBX as YYYY-MM-DD HH:MM:SS
.stat_print_ts:
        push ebx
        ; Year
        mov eax, ebx
        shr eax, 25
        and eax, 0x7F
        add eax, 2000
        call vga_print_dec
        mov al, '-'
        call vga_putchar
        ; Month
        mov eax, ebx
        shr eax, 21
        and eax, 0x0F
        cmp eax, 10
        jge .spt_mon
        push eax
        mov al, '0'
        call vga_putchar
        pop eax
.spt_mon:
        call vga_print_dec
        mov al, '-'
        call vga_putchar
        ; Day
        mov eax, ebx
        shr eax, 16
        and eax, 0x1F
        cmp eax, 10
        jge .spt_day
        push eax
        mov al, '0'
        call vga_putchar
        pop eax
.spt_day:
        call vga_print_dec
        mov al, ' '
        call vga_putchar
        ; Hour
        mov eax, ebx
        shr eax, 11
        and eax, 0x1F
        cmp eax, 10
        jge .spt_hr
        push eax
        mov al, '0'
        call vga_putchar
        pop eax
.spt_hr:
        call vga_print_dec
        mov al, ':'
        call vga_putchar
        ; Minute
        mov eax, ebx
        shr eax, 5
        and eax, 0x3F
        cmp eax, 10
        jge .spt_min
        push eax
        mov al, '0'
        call vga_putchar
        pop eax
.spt_min:
        call vga_print_dec
        mov al, ':'
        call vga_putchar
        ; Second (*2)
        mov eax, ebx
        and eax, 0x1F
        shl eax, 1
        cmp eax, 10
        jge .spt_sec
        push eax
        mov al, '0'
        call vga_putchar
        pop eax
.spt_sec:
        call vga_print_dec
        pop ebx
        ret

.stat_lbl_file:     db "  File: ", 0
.stat_lbl_type:     db "  Type: ", 0
.stat_lbl_size:     db "  Size: ", 0
.stat_bytes:        db " bytes ", 0
.stat_lbl_paren:    db "(", 0
.stat_blocks:       db " blocks)", 0x0A, 0
.stat_lbl_block:    db " Block: ", 0
.stat_lbl_created:  db "  Created:  ", 0
.stat_lbl_modified: db "  Modified: ", 0
.stat_lbl_perms:    db "  Perms:    ", 0
.stat_lbl_owner:    db "  Owner:    ", 0
.stat_t_file_s:     db "regular file", 0
.stat_t_dir_s:      db "directory", 0
.stat_t_exec_s:     db "executable", 0
.stat_t_batch_s:    db "batch script", 0
.stat_t_link_s:     db "symbolic link", 0
.stat_t_unk:        db "unknown", 0
.stat_usage_s:      db "Usage: stat <filename>", 0x0A, 0
.stat_lbl_target:   db "  Target: ", 0
.stat_rwx_buf:      times 10 db 0
.ln_read_buf:       times 256 db 0

;---------------------------------------
; Command: fsck
; Filesystem consistency check
;---------------------------------------
cmd_fsck:
        pushad
        mov esi, .fsck_banner
        call vga_print

        ; Step 1: Validate superblock magic
        mov esi, .fsck_check_sb
        call vga_print
        mov eax, HBFS_SUPERBLOCK_LBA
        mov ecx, 1
        mov edi, hbfs_block_buf
        call ata_read_sectors
        jc .fsck_sb_fail
        cmp dword [hbfs_block_buf], 'HBFS'
        jne .fsck_sb_fail
        mov esi, .fsck_ok
        call vga_print
        jmp .fsck_step2
.fsck_sb_fail:
        mov esi, .fsck_fail
        call vga_print
        mov esi, .fsck_sb_corrupt
        call vga_print
        popad
        ret

.fsck_step2:
        ; Step 2: Count allocated bits in bitmap
        mov esi, .fsck_check_bmp
        call vga_print
        call hbfs_load_bitmap
        jc .fsck_bmp_fail
        xor edx, edx           ; EDX = allocated bit count
        mov esi, hbfs_bitmap_buf
        mov ecx, HBFS_BITMAP_SIZE
.fsck_count_bits:
        lodsb
        test al, al
        jz .fsck_next_byte
        ; Count set bits in AL
        push ecx
        mov ecx, 8
.fsck_bit_loop:
        test al, 1
        jz .fsck_no_bit
        inc edx
.fsck_no_bit:
        shr al, 1
        loop .fsck_bit_loop
        pop ecx
.fsck_next_byte:
        loop .fsck_count_bits
        mov [.fsck_bmp_alloc], edx
        mov esi, .fsck_ok
        call vga_print
        jmp .fsck_step3
.fsck_bmp_fail:
        mov esi, .fsck_fail
        call vga_print
        popad
        ret

.fsck_step3:
        ; Step 3: Walk root directory entries, count files and block usage
        mov esi, .fsck_check_dir
        call vga_print
        ; Save current dir state
        push dword [current_dir_lba]
        push dword [current_dir_sects]
        mov dword [current_dir_lba], HBFS_ROOT_DIR_START
        mov dword [current_dir_sects], HBFS_ROOT_DIR_SECTS
        ; Invalidate dir cache to force a fresh load
        mov dword [dir_cache_lba], 0
        call hbfs_load_root_dir
        mov edi, hbfs_dir_buf
        call hbfs_get_max_entries
        mov ecx, eax
        xor ebx, ebx           ; EBX = file count
        xor edx, edx           ; EDX = total blocks from dir entries
        xor eax, eax           ; EAX = error count
        mov [.fsck_errors], eax

.fsck_dir_loop:
        cmp byte [edi + 253], FTYPE_FREE
        je .fsck_dir_next
        ; Active entry
        inc ebx
        ; Sum block count
        add edx, [edi + DIRENT_BLOCK_COUNT]
        ; Validate: block count should match ceil(size/4096)
        push eax
        push edx
        mov eax, [edi + DIRENT_SIZE]
        add eax, HBFS_BLOCK_SIZE - 1
        shr eax, 12                    ; ceil(size/4096)
        cmp eax, 0
        jg .fsck_has_size
        mov eax, 1                     ; min 1 block
.fsck_has_size:
        cmp eax, [edi + DIRENT_BLOCK_COUNT]
        je .fsck_size_ok
        ; Size mismatch
        push esi
        push ebx
        mov byte [vga_color], COLOR_ERROR
        mov esi, .fsck_warn_size
        call vga_print
        mov esi, edi
        call vga_print         ; filename
        mov al, 0x0A
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
        pop ebx
        pop esi
        inc dword [.fsck_errors]
.fsck_size_ok:
        pop edx
        pop eax
.fsck_dir_next:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jnz .fsck_dir_loop

        mov [.fsck_file_count], ebx
        mov [.fsck_dir_blocks], edx

        ; Restore directory state
        pop dword [current_dir_sects]
        pop dword [current_dir_lba]
        mov dword [dir_cache_lba], 0   ; invalidate cache
        mov esi, .fsck_ok
        call vga_print

        ; Step 4: Compare bitmap vs directory block counts
        mov esi, .fsck_check_alloc
        call vga_print
        mov eax, [.fsck_bmp_alloc]
        cmp eax, [.fsck_dir_blocks]
        je .fsck_alloc_ok
        mov esi, .fsck_warn_alloc
        call vga_print
        inc dword [.fsck_errors]
        jmp .fsck_report
.fsck_alloc_ok:
        mov esi, .fsck_ok
        call vga_print

.fsck_report:
        ; Print summary
        mov al, 0x0A
        call vga_putchar
        mov esi, .fsck_summary
        call vga_print
        mov eax, [.fsck_file_count]
        call vga_print_dec
        mov esi, .fsck_sum_files
        call vga_print
        mov eax, [.fsck_dir_blocks]
        call vga_print_dec
        mov esi, .fsck_sum_blocks
        call vga_print
        mov eax, [.fsck_bmp_alloc]
        call vga_print_dec
        mov esi, .fsck_sum_bmp
        call vga_print
        mov eax, [.fsck_errors]
        cmp eax, 0
        je .fsck_clean
        call vga_print_dec
        mov esi, .fsck_sum_err
        call vga_print
        jmp .fsck_end
.fsck_clean:
        mov byte [vga_color], COLOR_SUCCESS
        mov esi, .fsck_sum_clean
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
.fsck_end:
        popad
        ret

.fsck_bmp_alloc:   dd 0
.fsck_file_count:  dd 0
.fsck_dir_blocks:  dd 0
.fsck_errors:      dd 0

.fsck_banner:       db "fsck: HBFS filesystem check", 0x0A, 0
.fsck_check_sb:     db "  Checking superblock...      ", 0
.fsck_check_bmp:    db "  Checking bitmap...          ", 0
.fsck_check_dir:    db "  Checking directory...       ", 0
.fsck_check_alloc:  db "  Checking allocation...      ", 0
.fsck_ok:           db "[OK]", 0x0A, 0
.fsck_fail:         db "[FAIL]", 0x0A, 0
.fsck_sb_corrupt:   db "  FATAL: Superblock magic invalid!", 0x0A, 0
.fsck_warn_size:    db "  WARN: Block/size mismatch: ", 0
.fsck_warn_alloc:   db "[MISMATCH]", 0x0A, 0
.fsck_summary:      db "  Summary:", 0x0A, "    Files:           ", 0
.fsck_sum_files:    db 0x0A, "    Blocks (dir):    ", 0
.fsck_sum_blocks:   db 0x0A, "    Blocks (bitmap): ", 0
.fsck_sum_bmp:      db 0x0A, "    ", 0
.fsck_sum_err:      db " error(s) found", 0x0A, 0
.fsck_sum_clean:    db "Filesystem clean.", 0x0A, 0

;---------------------------------------
; Command: ln -s <target> <linkname>
; Create a symbolic link
;---------------------------------------
cmd_ln:
        pushad
        ; Expect "-s" flag
        cmp byte [esi], '-'
        jne .ln_usage
        cmp byte [esi + 1], 's'
        jne .ln_usage
        add esi, 2
        ; Skip spaces
.ln_skip1:
        cmp byte [esi], ' '
        jne .ln_get_target
        inc esi
        jmp .ln_skip1
.ln_get_target:
        cmp byte [esi], 0
        je .ln_usage
        ; ESI points to target path
        mov [.ln_target], esi
        ; Find end of target (next space)
.ln_scan_target:
        cmp byte [esi], 0
        je .ln_usage           ; no link name
        cmp byte [esi], ' '
        je .ln_got_target
        inc esi
        jmp .ln_scan_target
.ln_got_target:
        mov byte [esi], 0     ; null-terminate target
        inc esi
        ; Skip spaces before linkname
.ln_skip2:
        cmp byte [esi], ' '
        jne .ln_get_name
        inc esi
        jmp .ln_skip2
.ln_get_name:
        cmp byte [esi], 0
        je .ln_usage
        ; ESI = linkname, [.ln_target] = target path
        ; Compute target path length
        push esi               ; save linkname
        mov esi, [.ln_target]
        xor ecx, ecx
.ln_len:
        cmp byte [esi + ecx], 0
        je .ln_len_done
        inc ecx
        jmp .ln_len
.ln_len_done:
        inc ecx                ; include null terminator
        mov [.ln_tgt_len], ecx

        ; Copy target path to a temp buffer for hbfs_create_file
        mov esi, [.ln_target]
        mov edi, .ln_buf
        rep movsb

        pop esi                ; ESI = linkname
        ; Create symlink entry: name=linkname, data=target path, type=FTYPE_LINK
        mov ecx, [.ln_tgt_len]
        mov edi, .ln_buf       ; data = target path
        mov edx, FTYPE_LINK
        call hbfs_create_file
        jc .ln_fail
        ; Invalidate dir cache
        mov dword [dir_cache_lba], 0

        mov esi, .ln_done
        call vga_print
        popad
        ret

.ln_fail:
        mov byte [vga_color], COLOR_ERROR
        mov esi, .ln_err
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret

.ln_usage:
        mov esi, .ln_usage_s
        call vga_print
        popad
        ret

.ln_target:    dd 0
.ln_tgt_len:   dd 0
.ln_done:      db "Link created.", 0x0A, 0
.ln_err:       db "Failed to create link.", 0x0A, 0
.ln_usage_s:   db "Usage: ln -s <target> <linkname>", 0x0A, 0
.ln_buf:       times 256 db 0

;---------------------------------------
; Command: chmod <octal> <filename>
; Change file permission bits
; Example: chmod 755 myprog
;---------------------------------------
cmd_chmod:
        pushad
        call skip_spaces
        cmp byte [esi], 0
        je .chmod_usage

        ; Parse octal permission value (e.g. 755 -> 0x1ED)
        xor ecx, ecx
.chmod_parse_oct:
        movzx eax, byte [esi]
        cmp al, '0'
        jb .chmod_got_perms
        cmp al, '7'
        ja .chmod_got_perms
        imul ecx, 8
        sub al, '0'
        add ecx, eax
        inc esi
        jmp .chmod_parse_oct
.chmod_got_perms:
        ; ECX = permission bits; skip space to filename
        call skip_spaces
        cmp byte [esi], 0
        je .chmod_usage

        ; Make syscall: EBX = filename, ECX = perms
        mov ebx, esi
        mov eax, SYS_CHMOD
        int 0x80
        test eax, eax
        jnz .chmod_fail

        mov byte [vga_color], COLOR_SUCCESS
        mov esi, .chmod_done
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.chmod_fail:
        mov byte [vga_color], COLOR_ERROR
        mov esi, .chmod_err
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.chmod_usage:
        mov esi, .chmod_usage_s
        call vga_print
        popad
        ret
.chmod_done:   db "Permissions updated.", 0x0A, 0
.chmod_err:    db "chmod: file not found.", 0x0A, 0
.chmod_usage_s: db "Usage: chmod <octal> <filename>", 0x0A
                db "  e.g. chmod 755 myprog", 0x0A, 0

;---------------------------------------
; Command: chown <uid> <filename>
; Change file owner UID
; Example: chown 1000 myfile
;---------------------------------------
cmd_chown:
        pushad
        call skip_spaces
        cmp byte [esi], 0
        je .chown_usage

        ; Parse decimal uid
        xor ecx, ecx
.chown_parse_uid:
        movzx eax, byte [esi]
        cmp al, '0'
        jb .chown_got_uid
        cmp al, '9'
        ja .chown_got_uid
        imul ecx, 10
        sub al, '0'
        add ecx, eax
        inc esi
        jmp .chown_parse_uid
.chown_got_uid:
        call skip_spaces
        cmp byte [esi], 0
        je .chown_usage

        mov ebx, esi
        mov eax, SYS_CHOWN
        int 0x80
        test eax, eax
        jnz .chown_fail

        mov byte [vga_color], COLOR_SUCCESS
        mov esi, .chown_done
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.chown_fail:
        mov byte [vga_color], COLOR_ERROR
        mov esi, .chown_err
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        popad
        ret
.chown_usage:
        mov esi, .chown_usage_s
        call vga_print
        popad
        ret
.chown_done:   db "Owner updated.", 0x0A, 0
.chown_err:    db "chown: file not found.", 0x0A, 0
.chown_usage_s: db "Usage: chown <uid> <filename>", 0x0A, 0

;=======================================================================
; v4.0: cmd_ps_impl - List all tasks (ps / jobs command)
;=======================================================================
cmd_ps_impl:
        pushad
        mov esi, msg_job_hdr
        call vga_print

        xor ebp, ebp            ; slot index
.ps_loop:
        cmp ebp, MAX_TASKS
        jge .ps_done

        ; Calculate TCB address
        mov eax, ebp
        imul eax, TCB_SIZE
        lea edi, [task_table + eax]

        ; Skip free slots
        cmp dword [edi + TCB_STATE], TASK_FREE
        je .ps_next

        ; Print "  " indent
        mov al, ' '
        call vga_putchar
        call vga_putchar

        ; Print PID (decimal, right-padded to 5)
        mov eax, [edi + TCB_PID]
        call vga_print_dec
        mov al, ' '
        call vga_putchar
        call vga_putchar

        ; Print state
        push edi
        mov eax, [edi + TCB_STATE]
        cmp eax, TASK_RUNNING
        je .ps_st_running
        cmp eax, TASK_READY
        je .ps_st_ready
        cmp eax, TASK_BLOCKED
        je .ps_st_blocked
        cmp eax, TASK_STOPPED
        je .ps_st_stopped
        mov esi, msg_free
        jmp .ps_st_print
.ps_st_running:
        mov esi, msg_running
        jmp .ps_st_print
.ps_st_ready:
        mov esi, msg_ready
        jmp .ps_st_print
.ps_st_blocked:
        mov esi, msg_blocked
        jmp .ps_st_print
.ps_st_stopped:
        mov esi, msg_stopped
.ps_st_print:
        call vga_print
        pop edi

        ; Print priority
        push edi
        mov eax, [edi + TCB_PRIORITY]
        cmp eax, PRIO_HIGH
        je .ps_pr_high
        cmp eax, PRIO_NORMAL
        je .ps_pr_norm
        cmp eax, PRIO_LOW
        je .ps_pr_low
        mov esi, msg_prio_idle
        jmp .ps_pr_print
.ps_pr_high:
        mov esi, msg_prio_high
        jmp .ps_pr_print
.ps_pr_norm:
        mov esi, msg_prio_norm
        jmp .ps_pr_print
.ps_pr_low:
        mov esi, msg_prio_low
.ps_pr_print:
        call vga_print
        pop edi

        ; Print task name
        push edi
        lea esi, [edi + TCB_NAME]
        cmp byte [esi], 0
        je .ps_no_name
        call vga_print
        jmp .ps_name_done
.ps_no_name:
        mov al, '-'
        call vga_putchar
.ps_name_done:
        pop edi

        call vga_newline
.ps_next:
        inc ebp
        jmp .ps_loop
.ps_done:
        ; If no tasks, print message
        cmp dword [task_count], 0
        jne .ps_ret
        mov esi, msg_no_jobs
        call vga_print
.ps_ret:
        popad
        ret

;---------------------------------------
; parse_decimal - Parse decimal number from string at ESI
;   ESI = pointer to string (advanced past digits)
;   Returns: EAX = parsed value
;---------------------------------------
parse_decimal:
        xor eax, eax
.pd_loop:
        movzx edx, byte [esi]
        cmp dl, '0'
        jb .pd_done
        cmp dl, '9'
        ja .pd_done
        imul eax, 10
        sub dl, '0'
        movzx edx, dl
        add eax, edx
        inc esi
        jmp .pd_loop
.pd_done:
        ret
