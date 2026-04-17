;=======================================================================
; COMMAND SHELL
;=======================================================================

shell_main:
        mov rsp, KERNEL_STACK
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
        push rdx
        call vga_print_dec
        mov al, '.'
        call vga_putchar
        pop rax
        ; Convert remainder to centiseconds: rem * 100 / PIT_HZ
        imul eax, 100
        xor edx, edx
        mov ebx, PIT_HZ
        div ebx
        cmp eax, 10
        jge .bt_no_pad
        push rax
        mov al, '0'
        call vga_putchar
        pop rax
.bt_no_pad:
        call vga_print_dec
        mov esi, msg_boot_suf
        call vga_print

        mov byte [vga_color], COLOR_DEFAULT
.no_motd:

.prompt:
        ; Reap any completed background jobs before showing prompt
        call jobs_reap_done

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
        mov dword [shell_cursor_pos], 0

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
        cmp al, KEY_UP          ; Up arrow - history prev
        je .hist_up
        cmp al, KEY_DOWN        ; Down arrow - history next
        je .hist_down
        cmp al, KEY_LEFT        ; Left arrow - cursor left
        je .key_left
        cmp al, KEY_RIGHT       ; Right arrow - cursor right
        je .key_right
        cmp al, KEY_HOME        ; Home - cursor to start
        je .key_home
        cmp al, KEY_END         ; End - cursor to end
        je .key_end
        cmp al, KEY_DELETE      ; Delete - delete at cursor
        je .key_delete
        cmp al, 0x7F            ; Terminal DEL - delete at cursor
        je .key_delete
        cmp al, TAB_KEY         ; Tab - auto-complete
        je .tab_complete
        cmp al, CTRL_C_CODE     ; Ctrl+C - cancel
        je .ctrl_c
        cmp al, 1               ; Ctrl+A - cursor to start
        je .key_home
        cmp al, 5               ; Ctrl+E - cursor to end
        je .key_end
        cmp al, 12              ; Ctrl+L - clear screen, reprint
        je .ctrl_l
        cmp al, 21              ; Ctrl+U - kill to start of line
        je .ctrl_u
        cmp al, 11              ; Ctrl+K - kill to end of line
        je .ctrl_k
        cmp al, 23              ; Ctrl+W - delete previous word
        je .ctrl_w
        cmp al, 18              ; Ctrl+R - reverse history search
        je .ctrl_r
        cmp al, 20              ; Ctrl+T - transpose chars
        je .ctrl_t

        ; Store character if buffer not full
        cmp ecx, LINE_BUFFER_SIZE - 1
        jge .read_loop
        mov edx, [shell_cursor_pos]
        cmp edx, ecx
        je .append_char

        ; --- Mid-line insert: shift buffer right ---
        push rax                ; Save character (in AL)
        mov eax, ecx
.shift_r:
        cmp eax, edx
        jle .shift_r_done
        mov bl, [line_buffer + eax - 1]
        mov [line_buffer + eax], bl
        dec eax
        jmp .shift_r
.shift_r_done:
        pop rax                 ; Restore character
        mov [line_buffer + edx], al
        inc ecx
        inc dword [shell_cursor_pos]
        ; Print inserted char (advances VGA cursor)
        call vga_putchar
        ; Save target cursor position
        mov eax, [vga_cursor_x]
        mov [shell_saved_cx], eax
        mov eax, [vga_cursor_y]
        mov [shell_saved_cy], eax
        ; Redraw rest of line
        mov edx, [shell_cursor_pos]
.redraw_insert:
        cmp edx, ecx
        jge .repos_insert
        mov al, [line_buffer + edx]
        call vga_putchar
        inc edx
        jmp .redraw_insert
.repos_insert:
        mov eax, [shell_saved_cx]
        mov [vga_cursor_x], eax
        mov eax, [shell_saved_cy]
        mov [vga_cursor_y], eax
        call vga_update_cursor
        lea edi, [line_buffer + ecx]
        jmp .read_loop

.append_char:
        mov [line_buffer + ecx], al
        inc ecx
        inc dword [shell_cursor_pos]
        call vga_putchar
        lea edi, [line_buffer + ecx]
        jmp .read_loop

.backspace:
        cmp dword [shell_cursor_pos], 0
        jz .read_loop
        dec dword [shell_cursor_pos]
        dec ecx
        ; Move VGA cursor back (non-destructive)
        cmp dword [vga_cursor_x], 0
        je .bs_wrap
        dec dword [vga_cursor_x]
        jmp .bs_shift
.bs_wrap:
        cmp dword [vga_cursor_y], 0
        je .bs_shift
        dec dword [vga_cursor_y]
        mov dword [vga_cursor_x], VGA_WIDTH - 1
.bs_shift:
        ; Shift buffer left from cursor_pos
        mov edx, [shell_cursor_pos]
        mov eax, edx
.bs_shift_loop:
        cmp eax, ecx
        jge .bs_redraw
        mov bl, [line_buffer + eax + 1]
        mov [line_buffer + eax], bl
        inc eax
        jmp .bs_shift_loop
.bs_redraw:
        ; Save target cursor position
        mov eax, [vga_cursor_x]
        mov [shell_saved_cx], eax
        mov eax, [vga_cursor_y]
        mov [shell_saved_cy], eax
        ; Redraw from cursor to end
        mov edx, [shell_cursor_pos]
.bs_redraw_loop:
        cmp edx, ecx
        jge .bs_erase
        mov al, [line_buffer + edx]
        call vga_putchar
        inc edx
        jmp .bs_redraw_loop
.bs_erase:
        mov al, ' '
        call vga_putchar
        ; Restore cursor
        mov eax, [shell_saved_cx]
        mov [vga_cursor_x], eax
        mov eax, [shell_saved_cy]
        mov [vga_cursor_y], eax
        call vga_update_cursor
        lea edi, [line_buffer + ecx]
        jmp .read_loop

.key_left:
        cmp dword [shell_cursor_pos], 0
        je .read_loop
        dec dword [shell_cursor_pos]
        cmp dword [vga_cursor_x], 0
        je .kl_wrap
        dec dword [vga_cursor_x]
        jmp .kl_done
.kl_wrap:
        cmp dword [vga_cursor_y], 0
        je .kl_done
        dec dword [vga_cursor_y]
        mov dword [vga_cursor_x], VGA_WIDTH - 1
.kl_done:
        call vga_update_cursor
        jmp .read_loop

.key_right:
        mov edx, [shell_cursor_pos]
        cmp edx, ecx
        jge .read_loop
        inc dword [shell_cursor_pos]
        inc dword [vga_cursor_x]
        cmp dword [vga_cursor_x], VGA_WIDTH
        jl .kr_done
        mov dword [vga_cursor_x], 0
        inc dword [vga_cursor_y]
.kr_done:
        call vga_update_cursor
        jmp .read_loop

.key_home:
        mov edx, [shell_cursor_pos]
        or edx, edx
        jz .read_loop
        mov dword [shell_cursor_pos], 0
.kh_back:
        cmp dword [vga_cursor_x], 0
        je .kh_wrap
        dec dword [vga_cursor_x]
        jmp .kh_next
.kh_wrap:
        cmp dword [vga_cursor_y], 0
        je .kh_done
        dec dword [vga_cursor_y]
        mov dword [vga_cursor_x], VGA_WIDTH - 1
.kh_next:
        dec edx
        jnz .kh_back
.kh_done:
        call vga_update_cursor
        jmp .read_loop

.key_end:
        mov edx, [shell_cursor_pos]
        cmp edx, ecx
        jge .read_loop
.ke_fwd:
        mov al, [line_buffer + edx]
        call vga_putchar
        inc edx
        cmp edx, ecx
        jl .ke_fwd
        mov [shell_cursor_pos], ecx
        jmp .read_loop

.key_delete:
        mov edx, [shell_cursor_pos]
        cmp edx, ecx
        jge .read_loop
        dec ecx
        mov eax, edx
.del_shift:
        cmp eax, ecx
        jge .del_redraw
        mov bl, [line_buffer + eax + 1]
        mov [line_buffer + eax], bl
        inc eax
        jmp .del_shift
.del_redraw:
        mov eax, [vga_cursor_x]
        mov [shell_saved_cx], eax
        mov eax, [vga_cursor_y]
        mov [shell_saved_cy], eax
        mov edx, [shell_cursor_pos]
.del_redraw_loop:
        cmp edx, ecx
        jge .del_erase
        mov al, [line_buffer + edx]
        call vga_putchar
        inc edx
        jmp .del_redraw_loop
.del_erase:
        mov al, ' '
        call vga_putchar
        mov eax, [shell_saved_cx]
        mov [vga_cursor_x], eax
        mov eax, [shell_saved_cy]
        mov [vga_cursor_y], eax
        call vga_update_cursor
        lea edi, [line_buffer + ecx]
        jmp .read_loop

.ctrl_l:
        ; Clear screen, reprint prompt and current input
        call vga_clear
        ; Reprint prompt
        mov byte [vga_color], COLOR_PROMPT
        push rsi
        mov esi, shell_prompt_pre
        call vga_print
        push rdi
        mov edi, path_search_buf
        call build_cwd_path
        mov esi, path_search_buf
        call vga_print
        mov esi, shell_prompt_post
        call vga_print
        pop rdi
        pop rsi
        mov byte [vga_color], COLOR_DEFAULT
        ; Reprint current input
        push rcx
        xor edx, edx
.cl_reprint:
        cmp edx, ecx
        jge .cl_cursor
        mov al, [line_buffer + edx]
        call vga_putchar
        inc edx
        jmp .cl_reprint
.cl_cursor:
        pop rcx
        ; Move cursor back to shell_cursor_pos
        mov edx, ecx
        mov eax, [shell_cursor_pos]
.cl_back:
        cmp eax, edx
        jge .cl_done
        cmp dword [vga_cursor_x], 0
        je .cl_bwrap
        dec dword [vga_cursor_x]
        jmp .cl_bnext
.cl_bwrap:
        cmp dword [vga_cursor_y], 0
        je .cl_done
        dec dword [vga_cursor_y]
        mov dword [vga_cursor_x], VGA_WIDTH - 1
.cl_bnext:
        inc eax
        jmp .cl_back
.cl_done:
        call vga_update_cursor
        lea edi, [line_buffer + ecx]
        jmp .read_loop

.ctrl_u:
        ; Kill from start of line to cursor
        mov edx, [shell_cursor_pos]
        or edx, edx
        jz .read_loop
        ; Shift buffer left by cursor_pos chars
        push rcx                ; save length
        mov eax, 0
        mov ebx, edx
.cu_shift:
        cmp ebx, [rsp]         ; compare to saved ecx (length)
        jge .cu_shift_done
        mov cl, [line_buffer + ebx]
        mov [line_buffer + eax], cl
        inc eax
        inc ebx
        jmp .cu_shift
.cu_shift_done:
        pop rcx
        sub ecx, edx           ; new length = old_len - cursor_pos
        mov dword [shell_cursor_pos], 0
        ; Move VGA cursor to start of input
        push rcx
.cu_back:
        or edx, edx
        jz .cu_redraw
        cmp dword [vga_cursor_x], 0
        je .cu_bwrap
        dec dword [vga_cursor_x]
        jmp .cu_bnext
.cu_bwrap:
        cmp dword [vga_cursor_y], 0
        je .cu_redraw
        dec dword [vga_cursor_y]
        mov dword [vga_cursor_x], VGA_WIDTH - 1
.cu_bnext:
        dec edx
        jmp .cu_back
.cu_redraw:
        ; Save start cursor pos
        mov eax, [vga_cursor_x]
        mov [shell_saved_cx], eax
        mov eax, [vga_cursor_y]
        mov [shell_saved_cy], eax
        ; Redraw remaining buffer
        pop rcx
        xor edx, edx
.cu_draw:
        cmp edx, ecx
        jge .cu_blank
        mov al, [line_buffer + edx]
        call vga_putchar
        inc edx
        jmp .cu_draw
.cu_blank:
        ; Blank deleted chars (edx = how many we just drew)
        ; We need to blank 'deleted count' more spaces
        mov ebx, [shell_saved_cx]  ; temp: save
        push rcx                   ; save new length
        ; Number of blanks needed = old_cursor_pos (still in the shift count)
        ; Actually edx = new_len at this point. Need old cursor pos blanks.
        ; We saved nothing for that; just blank a generous amount
        mov ecx, edx               ; edx = new chars drawn
        add ecx, 16                ; blank extra to be safe
        sub ecx, edx
.cu_bl:
        or ecx, ecx
        jz .cu_repos
        mov al, ' '
        call vga_putchar
        dec ecx
        jmp .cu_bl
.cu_repos:
        pop rcx
        mov eax, [shell_saved_cx]
        mov [vga_cursor_x], eax
        mov eax, [shell_saved_cy]
        mov [vga_cursor_y], eax
        call vga_update_cursor
        lea edi, [line_buffer + ecx]
        jmp .read_loop

.ctrl_k:
        ; Kill from cursor to end of line
        mov edx, [shell_cursor_pos]
        cmp edx, ecx
        jge .read_loop
        ; Save cursor pos, blank the rest on screen
        mov eax, [vga_cursor_x]
        mov [shell_saved_cx], eax
        mov eax, [vga_cursor_y]
        mov [shell_saved_cy], eax
        push rdx
.ck_blank:
        cmp edx, ecx
        jge .ck_repos
        mov al, ' '
        call vga_putchar
        inc edx
        jmp .ck_blank
.ck_repos:
        pop rdx
        mov ecx, edx            ; new length = cursor_pos
        mov eax, [shell_saved_cx]
        mov [vga_cursor_x], eax
        mov eax, [shell_saved_cy]
        mov [vga_cursor_y], eax
        call vga_update_cursor
        lea edi, [line_buffer + ecx]
        jmp .read_loop

.ctrl_t:
        ; Transpose two characters before cursor
        cmp dword [shell_cursor_pos], 0
        je .read_loop
        ; If at end of line, swap chars at pos-2 and pos-1
        ; Otherwise swap chars at pos-1 and pos, advance cursor
        mov edx, [shell_cursor_pos]
        cmp edx, ecx
        je .ct_at_end
        ; Mid-line: swap [pos-1] and [pos], advance cursor
        mov al, [line_buffer + edx - 1]
        mov bl, [line_buffer + edx]
        mov [line_buffer + edx - 1], bl
        mov [line_buffer + edx], al
        inc dword [shell_cursor_pos]
        ; Redraw: move cursor back 1, print both chars
        cmp dword [vga_cursor_x], 0
        je .ct_mid_wrap
        dec dword [vga_cursor_x]
        jmp .ct_mid_draw
.ct_mid_wrap:
        cmp dword [vga_cursor_y], 0
        je .ct_mid_draw
        dec dword [vga_cursor_y]
        mov dword [vga_cursor_x], VGA_WIDTH - 1
.ct_mid_draw:
        call vga_update_cursor
        mov al, bl
        call vga_putchar
        mov al, [line_buffer + edx]
        call vga_putchar
        jmp .read_loop
.ct_at_end:
        ; At end: swap [pos-2] and [pos-1]
        cmp edx, 2
        jl .read_loop
        mov al, [line_buffer + edx - 2]
        mov bl, [line_buffer + edx - 1]
        mov [line_buffer + edx - 2], bl
        mov [line_buffer + edx - 1], al
        ; Move cursor back 2 positions, redraw both
        push rdx
        mov edx, 2
.ct_end_back:
        cmp dword [vga_cursor_x], 0
        je .ct_end_bwrap
        dec dword [vga_cursor_x]
        jmp .ct_end_bnext
.ct_end_bwrap:
        cmp dword [vga_cursor_y], 0
        je .ct_end_redraw
        dec dword [vga_cursor_y]
        mov dword [vga_cursor_x], VGA_WIDTH - 1
.ct_end_bnext:
        dec edx
        jnz .ct_end_back
.ct_end_redraw:
        pop rdx
        call vga_update_cursor
        mov al, [line_buffer + edx - 2]
        call vga_putchar
        mov al, [line_buffer + edx - 1]
        call vga_putchar
        jmp .read_loop

.ctrl_w:
        ; Delete previous word: skip trailing spaces, then skip word chars
        mov edx, [shell_cursor_pos]
        or edx, edx
        jz .read_loop
        ; Find start of previous word
        mov ebx, edx            ; save original cursor pos
        ; Skip spaces backward
.cw_skip_space:
        or edx, edx
        jz .cw_do_delete
        cmp byte [line_buffer + edx - 1], ' '
        jne .cw_skip_word
        dec edx
        jmp .cw_skip_space
.cw_skip_word:
        or edx, edx
        jz .cw_do_delete
        cmp byte [line_buffer + edx - 1], ' '
        je .cw_do_delete
        dec edx
        jmp .cw_skip_word
.cw_do_delete:
        ; EDX = new cursor pos, EBX = old cursor pos
        ; Chars to delete = EBX - EDX
        mov eax, ebx
        sub eax, edx            ; EAX = chars to delete
        or eax, eax
        jz .read_loop
        ; Move VGA cursor back by (EBX - EDX) chars
        push rcx
        push rdx
        push rax
        mov ecx, eax            ; count of chars to move back
.cw_vga_back:
        or ecx, ecx
        jz .cw_vga_back_done
        cmp dword [vga_cursor_x], 0
        je .cw_vga_bwrap
        dec dword [vga_cursor_x]
        jmp .cw_vga_bnext
.cw_vga_bwrap:
        cmp dword [vga_cursor_y], 0
        je .cw_vga_back_done
        dec dword [vga_cursor_y]
        mov dword [vga_cursor_x], VGA_WIDTH - 1
.cw_vga_bnext:
        dec ecx
        jmp .cw_vga_back
.cw_vga_back_done:
        pop rax                 ; chars deleted
        pop rdx                 ; new cursor pos
        pop rcx                 ; line length
        ; Shift buffer left: move [ebx..ecx) to [edx..)
        push rax                ; save delete count
        mov eax, edx
.cw_shift:
        cmp ebx, ecx
        jge .cw_shift_done
        mov ah, [line_buffer + ebx]
        mov [line_buffer + eax], ah
        inc eax
        inc ebx
        jmp .cw_shift
.cw_shift_done:
        pop rax                 ; delete count
        sub ecx, eax            ; new line length
        mov [shell_cursor_pos], edx
        ; Save VGA pos, redraw from cursor, blank deleted chars
        mov eax, [vga_cursor_x]
        mov [shell_saved_cx], eax
        mov eax, [vga_cursor_y]
        mov [shell_saved_cy], eax
        push rcx
        mov eax, edx
.cw_redraw:
        cmp eax, ecx
        jge .cw_blank
        push rax
        mov al, [line_buffer + eax]
        call vga_putchar
        pop rax
        inc eax
        jmp .cw_redraw
.cw_blank:
        ; Blank leftover chars
        pop rcx
        push rcx
        mov eax, [rsp + 0]      ; ecx (new len)
        ; We need to blank from new_len to old display end
        ; Just blank a safe amount
        mov ebx, 16
.cw_bl:
        or ebx, ebx
        jz .cw_repos
        mov al, ' '
        call vga_putchar
        dec ebx
        jmp .cw_bl
.cw_repos:
        pop rcx
        mov eax, [shell_saved_cx]
        mov [vga_cursor_x], eax
        mov eax, [shell_saved_cy]
        mov [vga_cursor_y], eax
        call vga_update_cursor
        lea edi, [line_buffer + ecx]
        jmp .read_loop

.ctrl_r:
        ; Reverse incremental history search
        ; Show (reverse-i-search)`': prompt, accept chars, search
        push rcx                ; save current line length
        ; Save current input in temp_line_buf
        push rcx
        mov esi, line_buffer
        mov edi, temp_line_buf
        rep movsb
        mov byte [edi], 0
        pop rcx
        mov [cr_saved_len], ecx
        ; Init search
        mov dword [cr_query_len], 0
        mov dword [cr_match_idx], -1
        ; Erase current input and show search prompt
        call .erase_input
        mov esi, cr_prompt_pre
        call vga_print
        mov esi, cr_prompt_mid
        call vga_print
.cr_loop:
        call kb_getchar
        cmp al, 0x0D            ; Enter - accept match
        je .cr_accept
        cmp al, 0x0A
        je .cr_accept
        cmp al, CTRL_C_CODE     ; Ctrl+C - cancel
        je .cr_cancel
        cmp al, 27              ; Escape - cancel
        je .cr_cancel
        cmp al, 0x08            ; Backspace - remove last query char
        je .cr_backspace
        cmp al, 18              ; Ctrl+R again - search further back
        je .cr_search_next
        ; Filter non-printable
        cmp al, 32
        jb .cr_loop
        cmp al, 126
        ja .cr_loop
        ; Add char to search query
        mov edx, [cr_query_len]
        cmp edx, 63
        jge .cr_loop            ; query full
        mov [cr_query_buf + edx], al
        inc dword [cr_query_len]
        mov byte [cr_query_buf + edx + 1], 0
        ; Reset search index
        mov eax, [hist_count]
        mov [cr_match_idx], eax
        jmp .cr_do_search
.cr_backspace:
        cmp dword [cr_query_len], 0
        je .cr_loop
        dec dword [cr_query_len]
        mov edx, [cr_query_len]
        mov byte [cr_query_buf + edx], 0
        ; Reset search index
        mov eax, [hist_count]
        mov [cr_match_idx], eax
        jmp .cr_do_search
.cr_search_next:
        ; Search further back from current match
        jmp .cr_do_search
.cr_do_search:
        ; Search backward from cr_match_idx - 1
        mov eax, [cr_match_idx]
        dec eax
        cmp eax, 0
        jl .cr_no_match
.cr_search_loop:
        ; Compare query against hist_buf[eax]
        push rax
        shl eax, 8             ; * HIST_ENTRY_SIZE
        lea esi, [hist_buf + eax]
        mov edi, cr_query_buf
        call .cr_strstr
        pop rax
        jc .cr_found
        dec eax
        cmp eax, 0
        jge .cr_search_loop
.cr_no_match:
        mov dword [cr_match_idx], -1
        jmp .cr_redraw
.cr_found:
        mov [cr_match_idx], eax
.cr_redraw:
        ; Clear line and redraw search prompt with match
        ; Move cursor to col 0
        mov eax, [vga_cursor_x]
        push rax
        mov dword [vga_cursor_x], 0
        ; Blank the entire line
        push rcx
        mov ecx, VGA_WIDTH
.cr_blank_line:
        mov al, ' '
        call vga_putchar
        loop .cr_blank_line
        pop rcx
        mov dword [vga_cursor_x], 0
        pop rax                 ; discard saved cursor_x
        ; Print search prompt
        mov esi, cr_prompt_pre
        call vga_print
        mov esi, cr_query_buf
        call vga_print
        mov esi, cr_prompt_mid
        call vga_print
        ; Print match if found
        cmp dword [cr_match_idx], -1
        je .cr_loop
        mov eax, [cr_match_idx]
        shl eax, 8
        lea esi, [hist_buf + eax]
        call vga_print
        jmp .cr_loop
.cr_accept:
        ; Accept match, copy to line_buffer
        pop rcx                 ; discard saved len
        cmp dword [cr_match_idx], -1
        je .cr_accept_empty
        ; Clear line
        mov dword [vga_cursor_x], 0
        push rcx
        mov ecx, VGA_WIDTH
.cr_acc_blank:
        mov al, ' '
        call vga_putchar
        loop .cr_acc_blank
        pop rcx
        mov dword [vga_cursor_x], 0
        ; Reprint prompt
        mov byte [vga_color], COLOR_PROMPT
        mov esi, shell_prompt_pre
        call vga_print
        push rdi
        mov edi, path_search_buf
        call build_cwd_path
        mov esi, path_search_buf
        call vga_print
        mov esi, shell_prompt_post
        call vga_print
        pop rdi
        mov byte [vga_color], COLOR_DEFAULT
        ; Copy match to line_buffer
        mov eax, [cr_match_idx]
        shl eax, 8
        lea esi, [hist_buf + eax]
        mov edi, line_buffer
        xor ecx, ecx
.cr_acc_copy:
        lodsb
        stosb
        cmp al, 0
        je .cr_acc_done
        push rcx
        call vga_putchar
        pop rcx
        inc ecx
        jmp .cr_acc_copy
.cr_acc_done:
        mov [shell_cursor_pos], ecx
        lea edi, [line_buffer + ecx]
        jmp .read_loop
.cr_accept_empty:
        ; No match - restore original input
        jmp .cr_restore
.cr_cancel:
        pop rcx                 ; discard saved len
.cr_restore:
        ; Restore original input from temp_line_buf
        ; Clear line
        mov dword [vga_cursor_x], 0
        push rcx
        mov ecx, VGA_WIDTH
.cr_can_blank:
        mov al, ' '
        call vga_putchar
        loop .cr_can_blank
        pop rcx
        mov dword [vga_cursor_x], 0
        ; Reprint prompt
        mov byte [vga_color], COLOR_PROMPT
        mov esi, shell_prompt_pre
        call vga_print
        push rdi
        mov edi, path_search_buf
        call build_cwd_path
        mov esi, path_search_buf
        call vga_print
        mov esi, shell_prompt_post
        call vga_print
        pop rdi
        mov byte [vga_color], COLOR_DEFAULT
        ; Restore original line
        mov esi, temp_line_buf
        mov edi, line_buffer
        mov ecx, [cr_saved_len]
        push rcx
        rep movsb
        pop rcx
        mov byte [line_buffer + ecx], 0
        ; Reprint it
        push rcx
        xor edx, edx
.cr_reprint:
        cmp edx, ecx
        jge .cr_reprint_done
        mov al, [line_buffer + edx]
        call vga_putchar
        inc edx
        jmp .cr_reprint
.cr_reprint_done:
        pop rcx
        mov [shell_cursor_pos], ecx
        lea edi, [line_buffer + ecx]
        jmp .read_loop

; Substring search: does null-terminated ESI contain null-terminated EDI?
; Returns CF=1 if found, CF=0 if not
.cr_strstr:
        push rax
        push rbx
        push rsi
        push rdi
        cmp byte [edi], 0      ; empty query matches everything
        je .crss_found
.crss_outer:
        cmp byte [esi], 0
        je .crss_notfound
        push rsi
        push rdi
.crss_inner:
        cmp byte [edi], 0
        je .crss_inner_match
        mov al, [esi]
        cmp al, 0
        je .crss_inner_fail
        cmp al, [edi]
        jne .crss_inner_fail
        inc esi
        inc edi
        jmp .crss_inner
.crss_inner_match:
        pop rdi
        pop rsi
        jmp .crss_found
.crss_inner_fail:
        pop rdi
        pop rsi
        inc esi
        jmp .crss_outer
.crss_notfound:
        pop rdi
        pop rsi
        pop rbx
        pop rax
        clc
        ret
.crss_found:
        pop rdi
        pop rsi
        pop rbx
        pop rax
        stc
        ret

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
        mov [shell_cursor_pos], ecx
        jmp .read_loop

.ctrl_c:
        ; Print ^C and cancel current input
        push rsi
        mov esi, msg_ctrl_c
        call vga_print
        pop rsi
        mov edi, line_buffer
        xor ecx, ecx
        mov dword [shell_cursor_pos], 0
        jmp .prompt

.hist_clear_line:
        ; Erase current input on screen
        call .erase_input
        mov edi, line_buffer
        xor ecx, ecx
        mov dword [shell_cursor_pos], 0
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
        push rcx
        call vga_putchar        ; Echo the char
        pop rcx
        jmp .hist_copy
.hist_copy_done:
        mov [shell_cursor_pos], ecx
        jmp .read_loop

.erase_input:
        ; Move cursor to end of line first
        push rcx
        mov edx, [shell_cursor_pos]
.erase_to_end:
        cmp edx, ecx
        jge .erase_at_end
        mov al, [line_buffer + edx]
        call vga_putchar
        inc edx
        jmp .erase_to_end
.erase_at_end:
        pop rcx
        ; Erase ECX characters going backward
        push rcx
.erase_loop:
        cmp ecx, 0
        je .erase_done
        mov al, 0x08
        call vga_putchar
        dec ecx
        jmp .erase_loop
.erase_done:
        pop rcx
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
HIST_MAX        equ 64
HIST_ENTRY_SIZE equ 256

shell_save_history:
        PUSHALL
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
        POPALL
        ret

;---------------------------------------
; Parse and dispatch command
; ESI = command line
;---------------------------------------
shell_parse_cmd:
        ; Skip leading spaces
        call skip_spaces

        mov dword [shell_last_status], 0

        ; --- Expand $VAR references in line_buffer ---
        call shell_expand_vars

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
; shell_expand_vars - Expand $VARIABLE references in line_buffer
; Scans for '$' — if found, runs env_expand into env_temp_buf
; then copies result back to line_buffer.
;---------------------------------------
shell_expand_vars:
        PUSHALL
        ; Quick check: does line_buffer contain any '$'?
        mov esi, line_buffer
.sev_scan:
        lodsb
        cmp al, 0
        je .sev_done            ; No '$' found — nothing to do
        cmp al, '$'
        jne .sev_scan
        ; Found '$' — do full expansion
        mov esi, line_buffer
        mov edi, env_temp_buf
        call env_expand
        ; Copy expanded result back to line_buffer
        mov esi, env_temp_buf
        mov edi, line_buffer
        mov ecx, LINE_BUFFER_SIZE - 1
.sev_copy:
        lodsb
        stosb
        test al, al
        jz .sev_done
        dec ecx
        jnz .sev_copy
        mov byte [edi], 0      ; Ensure null-termination
.sev_done:
        POPALL
        ret

;---------------------------------------
; shell_print_elapsed - Print elapsed time since time_start_ticks
; Output: "real 1.23s" format (seconds with 2 decimal places)
;---------------------------------------
shell_print_elapsed:
        PUSHALL
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
        push rdx
        call vga_print_dec

        ; Print decimal point
        mov esi, msg_dot
        call vga_print

        ; Print fractional part (2 digits, zero-padded)
        pop rax                 ; remainder (0-99)
        cmp eax, 10
        jge .no_pad
        push rax
        mov al, '0'
        call vga_putchar
        pop rax
.no_pad:
        call vga_print_dec

        ; Print "s\n"
        mov esi, msg_s_nl
        call vga_print

        POPALL
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
        PUSHALL

        ; Skip if line_buffer is empty
        mov esi, line_buffer
        cmp byte [esi], 0
        je .seg_done

        ; Quick check: does line_buffer contain any wildcard chars at all?
        push rsi
        mov esi, line_buffer
        call str_has_wildcards
        pop rsi
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
        push rdi
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
        pop rdi                 ; Restore output pointer

        ; Does this token have wildcards?
        push rsi
        mov esi, glob_token_buf
        call str_has_wildcards
        pop rsi
        jnc .seg_copy_literal   ; No wildcards — copy as-is

        ; Expand against directory entries
        push rsi
        mov dword [.seg_match_count], 0
        mov ebp, hbfs_dir_buf
        push rdi                ; Save output pointer
        call hbfs_get_max_entries
        mov edx, eax            ; EDX = max entries

.seg_dir_loop:
        cmp edx, 0
        je .seg_dir_done
        cmp byte [ebp + DIRENT_TYPE], FTYPE_FREE
        je .seg_dir_next

        ; Match this entry name against pattern
        push rsi
        push rdi
        mov esi, glob_token_buf ; pattern
        mov edi, ebp            ; candidate = entry name (at start of dirent)
        call wildcard_match
        pop rdi
        pop rsi
        jnc .seg_dir_next

        ; Match! Append filename to output
        pop rdi                 ; Restore output pointer
        ; Add space separator if not first match
        cmp dword [.seg_match_count], 0
        je .seg_no_sep
        mov al, ' '
        stosb
.seg_no_sep:
        ; Copy entry name
        push rsi
        lea esi, [ebp]          ; Entry name at start of dirent
.seg_cp_name:
        lodsb
        cmp al, 0
        je .seg_cp_name_done
        stosb
        jmp .seg_cp_name
.seg_cp_name_done:
        pop rsi
        inc dword [.seg_match_count]
        push rdi                ; Save output pointer again

.seg_dir_next:
        add ebp, HBFS_DIR_ENTRY_SIZE
        dec edx
        jmp .seg_dir_loop

.seg_dir_done:
        pop rdi                 ; Restore output pointer
        pop rsi                 ; Restore line_buffer pointer

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
        POPALL
        ret

.seg_match_count: dd 0

;---------------------------------------
; Check for trailing '&' (not '&&') in line_buffer.
; If found, strip it and set bg_request flag for background execution.
;---------------------------------------
shell_strip_bg_ampersand:
        PUSHALL
        mov byte [bg_request], 0    ; Default: foreground
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
        ; Strip the '&' (null-terminate there)
        mov byte [esi + ecx], 0
        ; Trim trailing spaces after removing '&'
.sba_trim:
        dec ecx
        js .sba_set_bg
        cmp byte [esi + ecx], ' '
        jne .sba_set_bg
        mov byte [esi + ecx], 0
        jmp .sba_trim
.sba_set_bg:
        mov byte [bg_request], 1    ; Request background execution
.sba_done:
        POPALL
        ret

shell_execute_conditionals:
        PUSHALL
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
        POPALL
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
        POPALL
        stc
        ret

.sec_done:
        POPALL
        stc
        ret

shell_dispatch_core:
        ; --- Alias expansion ---
        ; Extract first word, check alias table
        ; Guard: only expand once per command to prevent infinite recursion
        cmp byte [alias_expanding], 1
        je .alias_skip_nofix
        push rsi
        mov ecx, [alias_count]
        cmp ecx, 0
        je .alias_skip
        xor ebx, ebx
        mov edx, alias_table
.alias_lookup:
        cmp ebx, ecx
        jge .alias_skip
        ; Check if command starts with alias name
        push rsi
        mov edi, edx
        call str_starts_with
        pop rsi
        jc .alias_found
        add edx, ALIAS_ENTRY_SIZE
        inc ebx
        jmp .alias_lookup
.alias_found:
        ; Copy alias command into alias_expand_buf, then append rest of args
        push rsi          ; ESI now points past the alias name
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
        pop rsi
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
        pop rsi            ; discard saved ESI
        mov esi, alias_expand_buf
        mov byte [alias_expanding], 1  ; Prevent re-expansion
        jmp shell_parse_cmd  ; Re-parse with expanded command
.alias_skip:
        pop rsi
.alias_skip_nofix:
        mov byte [alias_expanding], 0  ; Reset for next command

        ; --- Shell function expansion ---
        cmp byte [func_expanding], 1
        je .func_skip_nofix
        push rsi
        mov ecx, [func_count]
        cmp ecx, 0
        je .func_skip
        xor ebx, ebx
        mov edx, func_table
.func_lookup:
        cmp ebx, ecx
        jge .func_skip
        push rsi
        mov edi, edx
        call str_starts_with
        pop rsi
        jc .func_found
        add edx, FUNC_ENTRY_SIZE
        inc ebx
        jmp .func_lookup
.func_found:
        ; Execute function body: split on ';' and execute each command
        pop rsi                        ; discard saved ESI
        mov byte [func_expanding], 1   ; Prevent recursion
        lea esi, [edx + FUNC_NAME_LEN] ; ESI = function body
        ; Execute semicolon-separated commands
.func_exec_loop:
        cmp byte [esi], 0
        je .func_exec_done
        cmp byte [esi], ' '
        jne .func_exec_start
        inc esi
        jmp .func_exec_loop
.func_exec_start:
        ; Copy next command (up to ';' or null) into line_buffer
        mov edi, line_buffer
.func_exec_copy:
        lodsb
        cmp al, ';'
        je .func_exec_run
        cmp al, 0
        je .func_exec_run_last
        stosb
        jmp .func_exec_copy
.func_exec_run:
        mov byte [edi], 0
        push rsi
        call shell_parse_cmd
        pop rsi
        jmp .func_exec_loop
.func_exec_run_last:
        mov byte [edi], 0
        call shell_parse_cmd
.func_exec_done:
        mov byte [func_expanding], 0
        ret                            ; Return from shell_parse_cmd
.func_skip:
        pop rsi
.func_skip_nofix:
        mov byte [func_expanding], 0

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

        mov edi, cmd_rmdir_str
        call str_starts_with
        jc .cmd_rmdir

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

        mov edi, cmd_read_str
        call str_starts_with
        jc .cmd_read

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

        mov edi, cmd_functions_str
        call str_starts_with
        jc .cmd_functions

        mov edi, cmd_function_str
        call str_starts_with
        jc .cmd_function

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

        mov edi, cmd_jobs_str
        call str_starts_with
        jc .cmd_jobs

        mov edi, cmd_fg_str
        call str_starts_with
        jc .cmd_fg

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
        push rax
        shr eax, 10             ; KB / 1024 = MB
        call vga_print_dec
        mov esi, msg_mb
        call vga_print
        pop rax
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

.cmd_rmdir:
        call skip_spaces
        call cmd_rmdir_impl
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

.cmd_read:
        call skip_spaces
        call cmd_read_input
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

.cmd_function:
        call skip_spaces
        call cmd_function
        ret

.cmd_functions:
        call cmd_functions
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

.cmd_jobs:
        call cmd_jobs
        ret

.cmd_fg:
        call skip_spaces
        call cmd_fg
        ret

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
        mov rsi, [scr_mode_names + rax*8]
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
        mov rsi, [scr_mode_names + rax*8]
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
        PUSHALL
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
        push rsi
        push rdi
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
        pop rdi
        pop rsi
        push rsi
        push rdi
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
        pop rdi
        pop rsi
        push rsi
        push rdi
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
        pop rdi
        pop rsi
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
        POPALL
        ret

.diff_usage:
        call shell_status_fail
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_2files
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret
.diff_not_found1:
        call shell_status_fail
        mov byte [vga_color], COLOR_ERROR
        mov esi, filename_buf
        call vga_print
        mov esi, msg_not_found_w
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret
.diff_not_found2:
        call shell_status_fail
        mov byte [vga_color], COLOR_ERROR
        mov esi, filename_buf2
        call vga_print
        mov esi, msg_not_found_w
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

; Compare line at ESI vs line at EDI. CF=1 if same.
.diff_cmp_line:
        push rsi
        push rdi
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
        pop rdi
        pop rsi
        clc
        ret
.dcl_same:
        pop rdi
        pop rsi
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
        PUSHALL
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
        mov qword [.uniq_prev], 0
        mov dword [.uniq_count], 1

.uniq_loop:
        cmp byte [esi], 0
        je .uniq_flush
        mov [.uniq_cur], rsi

        cmp qword [.uniq_prev], 0
        je .uniq_save_prev

        ; Compare current with previous
        push rsi
        push rdi
        mov rdi, [.uniq_prev]
        call cmd_diff_files.diff_cmp_line
        pop rdi
        pop rsi
        jc .uniq_dup

        ; Lines differ - flush previous
        call .uniq_emit
        mov dword [.uniq_count], 1
        jmp .uniq_save_prev

.uniq_dup:
        inc dword [.uniq_count]
        jmp .uniq_advance

.uniq_save_prev:
        mov rax, [.uniq_cur]
        mov [.uniq_prev], rax
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
        cmp qword [.uniq_prev], 0
        je .uniq_done
        call .uniq_emit
.uniq_done:
        POPALL
        ret

.uniq_emit:
        PUSHALL
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
        mov rsi, [.uniq_prev]
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
        POPALL
        ret

.uniq_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret
.uniq_nf:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_file_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

.uniq_prev:   dq 0
.uniq_cur:    dq 0
.uniq_count:  dd 0

;---------------------------------------
; Command: rev - Reverse each line of a file
;---------------------------------------
cmd_rev_file:
        PUSHALL
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
        POPALL
        ret
.rev_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret
.rev_nf:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_file_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

;---------------------------------------
; Command: tac - Print file lines in reverse order
;---------------------------------------
cmd_tac_file:
        PUSHALL
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
        mov [rdi], rsi
        add rdi, 8
        inc dword [.tac_lines]

.tac_scan:
        cmp byte [rsi], 0
        je .tac_print
        cmp byte [rsi], 0x0A
        jne .tac_next_ch
        inc rsi
        cmp byte [rsi], 0
        je .tac_print
        mov [rdi], rsi
        add rdi, 8
        inc dword [.tac_lines]
        jmp .tac_scan
.tac_next_ch:
        inc rsi
        jmp .tac_scan

.tac_print:
        mov ecx, [.tac_lines]
        cmp ecx, 0
        je .tac_done
.tac_print_loop:
        dec ecx
        mov rsi, PROGRAM_BASE + 0x10000
        mov rsi, [rsi + rcx * 8]
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
        POPALL
        ret
.tac_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret
.tac_nf:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_file_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret
.tac_lines: dd 0

;---------------------------------------
; Command: alias - Define or list command aliases
; Usage: alias              (list all)
;        alias NAME CMD     (define alias)
;---------------------------------------
cmd_alias:
        PUSHALL
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
        push rsi
        mov esi, filename_buf
        mov edi, edx
        call str_compare
        pop rsi
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
        push rsi
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
        pop rsi
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
        POPALL
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
        POPALL
        ret

.alias_none:
        mov esi, msg_no_aliases
        call vga_print
        POPALL
        ret

.alias_show_one:
        mov ecx, [alias_count]
        xor ebx, ebx
        mov edx, alias_table
.alias_sf:
        cmp ebx, ecx
        jge .alias_snf
        push rsi
        mov esi, filename_buf
        mov edi, edx
        call str_compare
        pop rsi
        je .alias_sp
        add edx, ALIAS_ENTRY_SIZE
        inc ebx
        jmp .alias_sf
.alias_sp:
        lea esi, [edx + ALIAS_NAME_LEN]
        call vga_print
        call vga_newline
        POPALL
        ret
.alias_snf:
        mov esi, filename_buf
        call vga_print
        mov esi, msg_not_found_w
        call vga_print
        POPALL
        ret

.alias_full:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_table_full
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

;---------------------------------------
; Command: function name { cmd1; cmd2 } - Define shell function
;---------------------------------------
cmd_function:
        PUSHALL
        ; Parse function name
        cmp byte [esi], 0
        je .func_usage

        mov edi, filename_buf
        call copy_word
        call skip_spaces

        ; Expect '{'
        cmp byte [esi], '{'
        jne .func_usage
        inc esi
        call skip_spaces_fn

        ; Find the closing '}'
        ; Copy body (everything between { and }) to temp buffer
        mov edi, cond_work_buf
        xor ecx, ecx
.func_copy_body:
        cmp byte [esi], 0
        je .func_usage          ; No closing brace
        cmp byte [esi], '}'
        je .func_body_done
        lodsb
        stosb
        inc ecx
        cmp ecx, FUNC_BODY_LEN - 1
        jb .func_copy_body
.func_body_done:
        ; Trim trailing spaces from body
.func_trim_trail:
        cmp ecx, 0
        je .func_body_empty
        cmp byte [edi - 1], ' '
        jne .func_body_trimmed
        dec edi
        dec ecx
        jmp .func_trim_trail
.func_body_trimmed:
        mov byte [edi], 0

        ; Check for existing function with same name (update it)
        mov ecx, [func_count]
        xor ebx, ebx
        mov edx, func_table
.func_find:
        cmp ebx, ecx
        jge .func_new_slot
        push rsi
        mov esi, filename_buf
        mov edi, edx
        call str_compare
        pop rsi
        je .func_update
        add edx, FUNC_ENTRY_SIZE
        inc ebx
        jmp .func_find

.func_new_slot:
        cmp ecx, FUNC_MAX
        jge .func_full
        mov eax, ecx
        imul eax, FUNC_ENTRY_SIZE
        lea edx, [func_table + eax]
        inc dword [func_count]

.func_update:
        ; Copy name
        push rsi
        mov esi, filename_buf
        mov edi, edx
        mov ecx, FUNC_NAME_LEN - 1
.func_cpn:
        lodsb
        stosb
        cmp al, 0
        je .func_cpn_done
        dec ecx
        jnz .func_cpn
        mov byte [edi], 0
.func_cpn_done:
        pop rsi

        ; Copy body from cond_work_buf
        push rsi
        mov esi, cond_work_buf
        lea edi, [edx + FUNC_NAME_LEN]
        mov ecx, FUNC_BODY_LEN - 1
.func_cpb:
        lodsb
        stosb
        cmp al, 0
        je .func_cpb_done
        dec ecx
        jnz .func_cpb
        mov byte [edi], 0
.func_cpb_done:
        pop rsi

        mov byte [vga_color], COLOR_INFO
        mov esi, msg_func_defined
        call vga_print
        mov esi, filename_buf
        call vga_print
        call vga_newline
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

.func_body_empty:
.func_usage:
        mov esi, msg_func_usage
        call vga_print
        POPALL
        ret

.func_full:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_func_full
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

; skip_spaces_fn - Skip spaces in ESI (local helper)
skip_spaces_fn:
        cmp byte [esi], ' '
        jne .ssf_done
        inc esi
        jmp skip_spaces_fn
.ssf_done:
        ret

;---------------------------------------
; Command: functions - List all defined shell functions
;---------------------------------------
cmd_functions:
        PUSHALL
        mov ecx, [func_count]
        cmp ecx, 0
        je .funcs_none
        xor ebx, ebx
        mov edx, func_table
.funcs_loop:
        cmp ebx, ecx
        jge .funcs_done
        mov byte [vga_color], COLOR_INFO
        mov esi, edx
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        mov esi, msg_func_sep
        call vga_print
        lea esi, [edx + FUNC_NAME_LEN]
        call vga_print
        mov esi, msg_func_end
        call vga_print
        add edx, FUNC_ENTRY_SIZE
        inc ebx
        jmp .funcs_loop
.funcs_done:
        POPALL
        ret
.funcs_none:
        mov esi, msg_func_none
        call vga_print
        POPALL
        ret

;---------------------------------------
; Command: history - Show command history
;---------------------------------------
cmd_history:
        PUSHALL
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
        POPALL
        ret
.hist_empty:
        mov esi, msg_no_history
        call vga_print
        POPALL
        ret

;---------------------------------------
; Command: which - Show if a name is built-in or external
;---------------------------------------
cmd_which:
        PUSHALL
        mov edi, filename_buf
        call copy_word
        cmp byte [filename_buf], 0
        je .which_usage

        ; Check against a table of built-in command names
        mov rbx, .which_builtins
.which_check:
        mov rdi, [rbx]
        cmp rdi, 0
        je .which_try_fs
        mov esi, filename_buf
        call str_compare
        je .which_builtin
        add rbx, 8
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
        push rsi
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
        pop rsi
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
        POPALL
        ret

.which_pskip:
        pop rsi
        jmp .which_path_next

.which_path_done:
        call path_restore_cwd
        ; Not found anywhere
        mov esi, filename_buf
        call vga_print
        mov esi, msg_not_found_w
        call vga_print
        POPALL
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
        POPALL
        ret

.which_builtin:
        mov esi, filename_buf
        call vga_print
        mov esi, msg_builtin
        call vga_print
        POPALL
        ret

.which_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

; Table of built-in command name pointers (null-terminated)
.which_builtins:
        dq cmd_help_str, cmd_ver_str, cmd_clear_str, cmd_dir_str
        dq cmd_ls_str, cmd_del_str, cmd_rm_str, cmd_format_str
        dq cmd_cat_str, cmd_write_str, cmd_hex_str, cmd_mem_str
        dq cmd_time_str, cmd_disk_str, cmd_run_str, cmd_enter_str
        dq cmd_copy_str, cmd_ren_str, cmd_mv_str, cmd_move_str
        dq cmd_df_str, cmd_more_str, cmd_echo_str, cmd_wc_str
        dq cmd_find_str, cmd_append_str, cmd_date_str, cmd_beep_str
        dq cmd_batch_str, cmd_mkdir_str, cmd_cd_str, cmd_pwd_str
        dq cmd_pushd_str, cmd_popd_str, cmd_dirs_str
        dq cmd_touch_str, cmd_set_str, cmd_unset_str, cmd_read_str, cmd_shutdown_str
        dq cmd_reboot_str
        dq cmd_cls_str, cmd_head_str, cmd_tail_str, cmd_type_str
        dq cmd_diff_str, cmd_uniq_str, cmd_rev_str, cmd_tac_str
        dq cmd_alias_str, cmd_history_str, cmd_which_str
        dq cmd_sleep_str, cmd_color_str, cmd_size_str, cmd_strings_str
        dq cmd_mouse_str
        dq cmd_gui_str
        dq cmd_scrsaver_str
        dq cmd_whoami_str
        dq cmd_stat_str
        dq cmd_fsck_str
        dq cmd_ln_str
        dq cmd_chmod_str
        dq cmd_chown_str
        dq cmd_rmdir_str
        dq 0

;---------------------------------------
; Command: sleep - Sleep for N seconds
;---------------------------------------
cmd_sleep:
        PUSHALL
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
        push rax
        mov esi, msg_sleeping
        call vga_print
        mov eax, [rsp]
        call vga_print_dec
        mov esi, msg_sleep_sec
        call vga_print
        pop rax
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
        POPALL
        ret

.sleep_abort:
        mov byte [ctrl_c_flag], 0
        POPALL
        ret

.sleep_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_sleep_usage
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

;---------------------------------------
; Command: color - Set terminal foreground/background color
; Usage: color FG [BG]  (hex values 0-F)
;---------------------------------------
cmd_color:
        PUSHALL
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
        POPALL
        ret

.color_usage:
        mov esi, msg_color_usage
        call vga_print
        POPALL
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
        PUSHALL
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
        POPALL
        ret

.size_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret
.size_nf:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_file_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

;---------------------------------------
; Command: strings - Extract printable strings from a file
; Flags: -n N  minimum string length (default 4)
;---------------------------------------
cmd_strings_file:
        PUSHALL
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
        POPALL
        ret

.strings_flush_run:
        cmp ebx, [strings_min_len]
        jb .strings_reset
        push rsi
        push rcx
        mov esi, edx
        mov ecx, ebx
.strings_ploop:
        mov al, [esi]
        call vga_putchar
        inc esi
        dec ecx
        jnz .strings_ploop
        call vga_newline
        pop rcx
        pop rsi
.strings_reset:
        xor ebx, ebx
        ret

.strings_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret
.strings_nf:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_file_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

.strings_total: dd 0

;---------------------------------------
; Command: mouse - Show PS/2 mouse status
;---------------------------------------
cmd_mouse_status:
        PUSHALL
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
        POPALL
        ret

.mouse_not_present:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_mouse_absent
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

;---------------------------------------
; Command: dir/ls - List directory
; Flags: -l  long format (type, size, blocks, name)
;        -s  sort by size (largest first) — not yet, just reserved
;---------------------------------------
cmd_list_dir:
        PUSHALL
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
        push rsi
        push rcx
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
        pop rcx
        pop rsi
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
        POPALL
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
        push rbx
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
        push rax
        mov al, '0'
        call vga_putchar
        pop rax
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
        push rax
        mov al, '0'
        call vga_putchar
        pop rax
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
        push rax
        mov al, '0'
        call vga_putchar
        pop rax
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
        push rax
        mov al, '0'
        call vga_putchar
        pop rax
.tmin_ok:
        call vga_print_dec
        pop rbx

        mov al, ' '
        call vga_putchar

        ; Print filename
        push rsi
        mov esi, edi
        call vga_print
        pop rsi

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

        POPALL
        ret

;---------------------------------------
; Command: del/rm - Delete file
; Flags: -v  verbose (print each deleted filename)
;---------------------------------------
cmd_delete_file:
        PUSHALL
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
        push rsi
        mov esi, msg_del_prefix
        call vga_print
        mov esi, edi
        call vga_print
        mov al, 0x0A
        call vga_putchar
        pop rsi
.del_exact_quiet:
        call hbfs_delete_file_entry
        ; If CWD was changed by global search, restore it
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .del_no_restore
        call gff_restore_cwd
.del_no_restore:
        mov esi, msg_deleted
        call vga_print
        POPALL
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

        push rcx
        push rdi
        mov esi, filename_buf           ; pattern
        call wildcard_match
        pop rdi
        pop rcx
        jnc .wc_next

        ; -v: print name before delete
        mov eax, 21            ; 'v' - 'a'
        call test_flag
        jnc .wc_del_quiet
        push rsi
        mov esi, msg_del_prefix
        call vga_print
        mov esi, edi
        call vga_print
        mov al, 0x0A
        call vga_putchar
        pop rsi
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
        POPALL
        ret

.not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
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
        PUSHALL
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
        POPALL
        ret
.head_not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret
.head_usage:
        mov esi, msg_head_usage
        call vga_print
        POPALL
        ret

;---------------------------------------
; Command: tail - Show last N lines of a file
; Usage: tail [N] filename   or   tail -n N filename
;        (default N=10)
;---------------------------------------
cmd_tail_file:
        PUSHALL
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
        POPALL
        ret
.tail_not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret
.tail_usage:
        mov esi, msg_tail_usage
        call vga_print
        POPALL
        ret

;---------------------------------------
; Command: cat - Display file contents
; Flags: -n  show line numbers
;---------------------------------------
cmd_cat_file:
        PUSHALL
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
        POPALL
        ret

.cat_numbered:
        mov esi, PROGRAM_BASE
        xor ebx, ebx           ; line counter
.cat_n_line:
        cmp byte [esi], 0
        je .cat_n_done
        inc ebx
        ; Print line number right-aligned in 4-char field
        push rsi
        mov byte [vga_color], COLOR_INFO
        mov eax, ebx
        mov edx, 4
        call vga_print_dec_width
        mov al, ' '
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
        pop rsi
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
        POPALL
        ret

.cat_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_need_filename
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

.not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

;---------------------------------------
; Command: more - Page-by-page file viewer
; Flags: -n N  lines per page (default 23)
;---------------------------------------
PAGER_LINES equ 23

cmd_more_file:
        PUSHALL
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
        push rsi
        mov byte [vga_color], COLOR_INFO
        mov esi, msg_more_prompt
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        pop rsi

        ; Wait for keypress
        call kb_getchar
        cmp al, 27              ; ESC
        je .more_done
        cmp al, 'q'
        je .more_done

        ; Erase the prompt line
        mov al, 0x0D             ; CR
        call vga_putchar
        push rsi
        mov esi, msg_more_erase
        call vga_print
        pop rsi
        mov al, 0x0D
        call vga_putchar

        xor ecx, ecx            ; Reset line counter
        jmp .more_loop

.more_done:
        mov al, 0x0A
        call vga_putchar
        POPALL
        ret

.more_not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret
.more_page: dd PAGER_LINES

;---------------------------------------
; Command: write - Write text to file
; Usage: write filename
; Then type content, end with empty line
;---------------------------------------
cmd_write_file:
        PUSHALL

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
        push rdi
        push rbx
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

        pop rbx
        pop rdi

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
        POPALL
        ret

.write_error:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_write_err
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

;---------------------------------------
; Command: touch - Create empty file
;---------------------------------------
cmd_touch_file:
        PUSHALL
        mov edi, filename_buf
        call copy_word
        cmp byte [filename_buf], 0
        je .touch_usage

        ; Check if file already exists — if so, just update timestamp
        mov esi, filename_buf
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .touch_create

        ; File exists: update modified timestamp only
        call rtc_get_timestamp
        mov [edi + DIRENT_MODIFIED], eax
        call hbfs_save_root_dir
        mov esi, .touch_updated_msg
        call vga_print
        jmp .touch_done

.touch_create:
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
        POPALL
        ret

.touch_updated_msg: db "Timestamp updated", 0x0A, 0

;---------------------------------------
; Command: hex - Hexdump a file
; Flags: -n N  limit to N bytes (default 512)
;---------------------------------------
cmd_hexdump_file:
        PUSHALL
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
        push rcx
        push rdx
        mov ebx, 16
.hex_byte:
        cmp edx, ecx
        jge .hex_pad
        movzx eax, byte [esi + edx]
        push rax
        shr eax, 4
        call .print_nibble
        pop rax
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
        pop rdx
        push rdx
        mov ebx, 16
.ascii_byte:
        cmp edx, [rsp + 8]     ; Compare with ecx on stack
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

        pop rdx
        pop rcx
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
        POPALL
        ret

.not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

;---------------------------------------
; Command: mem - Show memory information
;---------------------------------------
cmd_show_memory:
        PUSHALL
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

        POPALL
        ret

;---------------------------------------
; Command: disk - Show disk information
;---------------------------------------
cmd_show_disk:
        PUSHALL
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

        POPALL
        ret

.no_disk:
        mov esi, msg_ata_none
        call vga_print
        POPALL
        ret

;---------------------------------------
; Command: df - Show HBFS filesystem usage
;---------------------------------------
cmd_df_info:
        PUSHALL
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
        push rbx

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
        ; ECX = used blocks, [rsp] = total blocks
        pop rbx                 ; EBX = total blocks
        ; Clamp used to total
        cmp ecx, ebx
        jle .df_clamp_ok
        mov ecx, ebx
.df_clamp_ok:
        push rcx
        push rbx

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
        pop rbx                 ; total
        pop rcx                 ; used
        push rcx
        push rbx
        mov eax, ecx
        call vga_print_dec
        mov esi, msg_df_blocks
        call vga_print

        ; Print free blocks
        mov esi, msg_df_free
        call vga_print
        pop rbx                 ; total
        pop rcx                 ; used
        mov eax, ebx
        sub eax, ecx
        push rax
        call vga_print_dec
        mov esi, msg_df_blocks
        call vga_print

        ; Print free space in KB
        mov esi, msg_df_avail
        call vga_print
        pop rax                 ; free blocks
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

        POPALL
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
        POPALL
        ret

;---------------------------------------
; Command: jobs - List background jobs
;---------------------------------------
cmd_jobs:
        PUSHALL
        ; First, reap any completed background tasks
        call jobs_reap_done

        mov edx, 0             ; found any?
        mov ecx, JOB_MAX
        mov edi, job_table
.jobs_loop:
        cmp dword [edi + JOB_ACTIVE], 1
        jne .jobs_next
        ; Check if the task is still running
        mov eax, [edi + JOB_TASK_ID]
        call sched_task_alive
        test eax, eax
        jz .jobs_mark_done
        ; Print: [N] PID  Name
        mov edx, 1
        mov esi, msg_bg_launched    ; "["
        call vga_print
        ; Job number = JOB_MAX - ECX
        mov eax, JOB_MAX
        sub eax, ecx
        inc eax
        call vga_print_dec
        mov esi, msg_bg_launched2   ; "] "
        call vga_print
        ; PID
        mov eax, [edi + JOB_PID]
        call vga_print_dec
        mov al, ' '
        call vga_putchar
        mov al, ' '
        call vga_putchar
        ; Name
        lea esi, [edi + JOB_NAME]
        call vga_print
        mov al, 0x0A
        call vga_putchar
        jmp .jobs_next
.jobs_mark_done:
        mov dword [edi + JOB_ACTIVE], 0
        dec dword [job_count]
.jobs_next:
        add edi, JOB_ENTRY_SIZE
        dec ecx
        jnz .jobs_loop

        test edx, edx
        jnz .jobs_end
        mov esi, msg_no_jobs
        call vga_print
.jobs_end:
        POPALL
        ret

;---------------------------------------
; cmd_fg - Bring background job to foreground
; ESI = argument (job number)
;---------------------------------------
cmd_fg:
        PUSHALL
        cmp byte [esi], 0
        je .fg_usage

        ; Parse job number
        xor eax, eax
.fg_parse:
        movzx edx, byte [esi]
        cmp dl, '0'
        jb .fg_parsed
        cmp dl, '9'
        ja .fg_parsed
        imul eax, 10
        sub dl, '0'
        add eax, edx
        inc esi
        jmp .fg_parse
.fg_parsed:
        ; EAX = 1-based job number → 0-based index
        dec eax
        cmp eax, JOB_MAX
        jge .fg_no_job
        imul eax, JOB_ENTRY_SIZE
        lea edi, [job_table + eax]
        cmp dword [edi + JOB_ACTIVE], 1
        jne .fg_no_job

        ; Check task is still alive
        mov eax, [edi + JOB_TASK_ID]
        call sched_task_alive
        test eax, eax
        jz .fg_no_job

        ; Wait for the task to finish
        mov byte [vga_color], COLOR_INFO
        lea esi, [edi + JOB_NAME]
        call vga_print
        mov al, 0x0A
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
.fg_wait:
        hlt
        mov eax, [edi + JOB_TASK_ID]
        call sched_task_alive
        test eax, eax
        jnz .fg_wait
        ; Task finished — clean up job entry
        mov dword [edi + JOB_ACTIVE], 0
        dec dword [job_count]
        POPALL
        ret

.fg_usage:
        mov esi, msg_fg_usage
        call vga_print
        POPALL
        ret
.fg_no_job:
        mov esi, msg_fg_no_job
        call vga_print
        POPALL
        ret

;---------------------------------------
; jobs_reap_done - Clean up completed background jobs
; Prints "[N] Done" for each completed job.
;---------------------------------------
jobs_reap_done:
        PUSHALL
        mov ecx, JOB_MAX
        mov edi, job_table
.jrd_loop:
        cmp dword [edi + JOB_ACTIVE], 1
        jne .jrd_next
        mov eax, [edi + JOB_TASK_ID]
        call sched_task_alive
        test eax, eax
        jnz .jrd_next           ; Still running
        ; Task is done — print notification
        mov esi, msg_bg_done    ; "["
        call vga_print
        mov eax, JOB_MAX
        sub eax, ecx
        inc eax
        call vga_print_dec
        mov esi, msg_bg_done2   ; "] Done\n"
        call vga_print
        ; Clean up
        mov dword [edi + JOB_ACTIVE], 0
        dec dword [job_count]
.jrd_next:
        add edi, JOB_ENTRY_SIZE
        dec ecx
        jnz .jrd_loop
        POPALL
        ret

;---------------------------------------
; jobs_register - Register a new background job
; EAX = task ID from sched_create_task
; ESI = program name (null-terminated)
; Returns: job number in EAX (1-based), or 0 on failure
;---------------------------------------
jobs_register:
        PUSHALL
        mov ebx, eax            ; Save task ID
        ; Find free slot
        mov ecx, JOB_MAX
        mov edi, job_table
.jr_find:
        cmp dword [edi + JOB_ACTIVE], 0
        je .jr_found
        add edi, JOB_ENTRY_SIZE
        dec ecx
        jnz .jr_find
        ; Table full
        mov qword [rsp + FRAME_RAX], 0
        POPALL
        ret
.jr_found:
        mov dword [edi + JOB_ACTIVE], 1
        mov [edi + JOB_TASK_ID], ebx
        mov [edi + JOB_PID], ebx
        ; Copy program name
        push rdi
        lea edi, [edi + JOB_NAME]
        mov ecx, 51             ; max name length
.jr_cpname:
        lodsb
        stosb
        test al, al
        jz .jr_name_done
        dec ecx
        jnz .jr_cpname
        mov byte [edi], 0
.jr_name_done:
        pop rdi
        inc dword [job_count]
        ; Return 1-based job number
        mov eax, edi
        sub eax, job_table
        xor edx, edx
        push rbx
        mov ebx, JOB_ENTRY_SIZE
        div ebx
        pop rbx
        inc eax
        mov [rsp + FRAME_RAX], rax     ; Return in RAX
        POPALL
        ret

;---------------------------------------
; Command: run - Execute a program from filesystem
;---------------------------------------
cmd_exec_program:
        PUSHALL

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
        push rsi                ; save position in PATH string

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
        pop rsi                 ; clean up PATH string pointer
        jmp .found_program

.path_cd_fail:
        pop rsi                 ; restore PATH string position
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
        mov [program_entry], rax
        jmp .exec_program

.try_flat:
        ; Flat binary at PROGRAM_BASE
        mov qword [program_entry], PROGRAM_BASE

.exec_program:
        ; Check for background execution request
        cmp byte [bg_request], 1
        je .exec_background

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
        mov qword [tss_struct + 4], KERNEL_STACK

        ; Set up ring 3 stack
        mov rax, PROGRAM_EXIT_ADDR - 8
        ; Push return address (trampoline) on ring 3 stack
        mov qword [rax], PROGRAM_EXIT_ADDR

        ; Switch to ring 3 via IRET
        mov rbx, [program_entry]
        push qword USER_DS      ; SS3
        push rax                ; RSP3
        pushfq
        or dword [rsp], 0x200   ; Ensure interrupts enabled
        push qword USER_CS      ; CS3
        push rbx                ; RIP
        iretq
        ; SYS_EXIT will jump to shell_main, we never return here

.exec_background:
        ; Launch program as a background task via scheduler
        mov rbx, [program_entry]         ; Entry point
        mov ecx, PROGRAM_EXIT_ADDR - 8   ; User stack top
        ; Write SYS_EXIT trampoline for background task too
        mov byte [PROGRAM_EXIT_ADDR], 0xB8
        mov dword [PROGRAM_EXIT_ADDR + 1], 0
        mov byte [PROGRAM_EXIT_ADDR + 5], 0xCD
        mov byte [PROGRAM_EXIT_ADDR + 6], 0x80
        mov qword [ecx], PROGRAM_EXIT_ADDR  ; Return address on user stack

        call sched_create_task
        cmp eax, -1
        je .bg_fail

        ; Register in job table
        push rax                ; Save task ID
        mov esi, prog_name_buf
        call jobs_register
        mov ebx, eax            ; EBX = job number
        pop rax                 ; EAX = task ID

        ; Print "[N] PID"
        push rax
        mov esi, msg_bg_launched    ; "["
        call vga_print
        mov eax, ebx
        call vga_print_dec
        mov esi, msg_bg_launched2   ; "] "
        call vga_print
        pop rax
        call vga_print_dec
        mov al, 0x0A
        call vga_putchar

        mov byte [bg_request], 0
        mov byte [program_running], 0
        POPALL
        clc
        ret

.bg_fail:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_bg_unsupported
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        mov byte [bg_request], 0
        POPALL
        clc
        ret

.path_fsize: dd 0               ; Temp storage for file size during PATH search

.not_found:
        call shell_status_fail
        POPALL
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
        PUSHALL
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
        push rsi
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
        pop rsi
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
        pop rsi
.spr_in_err:
        call shell_status_fail
        call shell_redir_reset
        mov esi, msg_redir_in_err
        call vga_print
        POPALL
        stc
        ret

.spr_in_big_pop:
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .spr_in_big_pop_done
        call gff_restore_cwd
.spr_in_big_pop_done:
        pop rsi
        call shell_status_fail
        call shell_redir_reset
        mov esi, msg_redir_too_big
        call vga_print
        POPALL
        stc
        ret

.spr_syntax_err:
        call shell_status_fail
        call shell_redir_reset
        mov esi, msg_redir_syntax
        call vga_print
        POPALL
        stc
        ret

.spr_ok:
        POPALL
        clc
        ret

shell_finish_redirection:
        PUSHALL
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
        POPALL
        ret

.sfr_reset:
        call shell_redir_reset
        POPALL
        ret

shell_use_stdin_as_input:
        cmp byte [stdin_redir_active], 1
        jne .susi_no_input
        push rdi
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
        pop rdi
        clc
        ret
.susi_no_input:
        stc
        ret

shell_execute_pipeline:
        PUSHALL
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
        POPALL
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
        POPALL
        stc
        ret

.pipe_syntax:
        call shell_status_fail
        call shell_redir_reset
        mov esi, msg_redir_syntax
        call vga_print
        POPALL
        stc
        ret

.pipe_fail:
        call shell_status_fail
        call shell_redir_reset
        POPALL
        stc
        ret

;---------------------------------------
; Command: enter - Hex entry mode
;---------------------------------------
cmd_enter_hex:
        PUSHALL
        mov edi, PROGRAM_BASE   ; Write hex data here

.hex_prompt:
        push rdi
        mov byte [vga_color], COLOR_PROMPT
        mov al, 'h'
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT

        ; Read a line
        mov esi, temp_line_buf
        push rsi
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
        pop rsi
        pop rdi

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

        push rcx
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
        pop rcx

        ; Save file as executable
        mov esi, filename_buf
        mov edi, PROGRAM_BASE
        mov edx, FTYPE_EXEC
        call hbfs_create_file
        jc .save_fail

        mov esi, msg_saved
        call vga_print
        POPALL
        ret

.save_fail:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_write_err
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

;---------------------------------------
; Command: copy - Copy a file
; Flags: -v  verbose (print filenames)
;---------------------------------------
cmd_copy_file:
        PUSHALL

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
        push rsi
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

        pop rsi
        ; -v: print what was copied
        mov eax, 21            ; 'v' - 'a'
        call test_flag
        jnc .copy_quiet
        push rsi
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
        pop rsi
.copy_quiet:
        mov esi, msg_copied
        call vga_print
        POPALL
        ret

.usage_error:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_copy_usage
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
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

        push rcx
        push rdi
        mov esi, filename_buf
        call wildcard_match
        pop rdi
        pop rcx
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
        POPALL
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

        push rcx
        push rdi
        mov esi, filename_buf
        call wildcard_match
        pop rdi
        pop rcx
        jnc .cw_next

        ; source entry name -> wildcard_src_buf
        push rcx
        push rdi
        mov esi, edi
        mov edi, wildcard_src_buf
        call str_copy
        pop rdi
        pop rcx

        ; load source content
        push rcx
        push rdi
        mov esi, wildcard_src_buf
        mov edi, PROGRAM_BASE
        call hbfs_read_file
        pop rdi
        pop rcx
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
        push rcx
        push rdi
        mov esi, wildcard_dst_buf
        mov edi, PROGRAM_BASE
        movzx edx, byte [last_file_type]
        call hbfs_create_file
        pop rdi
        pop rcx
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
        POPALL
        ret

.wild_not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

.src_not_found:
        pop rsi
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

.copy_fail:
        pop rsi
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_write_err
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

;---------------------------------------
; Command: ren - Rename a file
; Usage: ren oldname newname
;---------------------------------------
cmd_rename_file:
        PUSHALL

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
        push rdi
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
        pop rdi

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
        POPALL
        ret

.ren_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_ren_usage
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
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

        push rcx
        push rdi
        mov esi, filename_buf
        call wildcard_match
        pop rdi
        pop rcx
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
        POPALL
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

        push rcx
        push rdi
        mov esi, filename_buf
        call wildcard_match
        pop rdi
        pop rcx
        jnc .rw_next

        ; source entry name -> wildcard_src_buf
        push rcx
        push rdi
        mov esi, edi
        mov edi, wildcard_src_buf
        call str_copy
        pop rdi
        pop rcx

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
        push rcx
        push rdi
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
        pop rdi
        pop rcx
        inc ebx

.rw_next:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jmp .rw_loop

.rw_done:
        call hbfs_save_root_dir
        mov esi, msg_renamed
        call vga_print
        POPALL
        ret

.ren_not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

;---------------------------------------
; Command: rmdir <dirname>
; Remove an empty directory
;---------------------------------------
cmd_rmdir_impl:
        PUSHALL
        cmp byte [esi], 0
        je .rmdir_usage

        mov [.rmdir_name_ptr], rsi

        ; Find in current directory
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .rmdir_not_found

        ; Check it's a directory
        cmp byte [edi + DIRENT_TYPE], FTYPE_DIR
        jne .rmdir_not_dir

        ; Check directory is empty: read its data blocks, scan for entries
        mov eax, [edi + DIRENT_START_BLOCK]
        mov [.rmdir_entry], edi
        ; Load directory data from disk
        shl eax, 3              ; block * 8 = LBA offset
        add eax, HBFS_DATA_START
        mov ecx, [edi + DIRENT_BLOCK_COUNT]
        shl ecx, 3              ; blocks * 8 = sector count
        mov esi, hbfs_block_buf
        ; Can't read into hbfs_block_buf if it's too small; use dir_buf trick
        ; Actually subdirs are 16 blocks = 64KB. Use a sector-by-sector scan.
        ; We scan the first sector of each block for non-free entries.
        mov edi, [.rmdir_entry]
        mov eax, [edi + DIRENT_START_BLOCK]
        mov ebx, [edi + DIRENT_BLOCK_COUNT]
        ; Scan all sectors of the subdir for any non-free entry
        mov [.rmdir_blk], eax
        mov [.rmdir_cnt], ebx

.rmdir_scan_blk:
        cmp dword [.rmdir_cnt], 0
        je .rmdir_empty
        ; Read one block (8 sectors) into hbfs_block_buf
        mov eax, [.rmdir_blk]
        shl eax, 3
        add eax, HBFS_DATA_START
        mov ecx, HBFS_SECTORS_PER_BLK
        mov edi, hbfs_block_buf
        call ata_read_sectors
        ; Scan entries in this block
        mov edi, hbfs_block_buf
        mov ecx, HBFS_BLOCK_SIZE / HBFS_DIR_ENTRY_SIZE
.rmdir_scan_entry:
        cmp byte [edi + DIRENT_TYPE], FTYPE_FREE
        jne .rmdir_not_empty
        add edi, HBFS_DIR_ENTRY_SIZE
        loop .rmdir_scan_entry
        inc dword [.rmdir_blk]
        dec dword [.rmdir_cnt]
        jmp .rmdir_scan_blk

.rmdir_empty:
        ; Directory is empty — delete it
        ; Re-find the entry (hbfs_block_buf was overwritten)
        call hbfs_load_root_dir
        mov rsi, [.rmdir_name_ptr]
        call hbfs_find_file
        jc .rmdir_not_found     ; shouldn't happen
        call hbfs_delete_file_entry
        mov esi, .rmdir_ok_msg
        call vga_print
        POPALL
        ret

.rmdir_not_empty:
        mov byte [vga_color], COLOR_ERROR
        mov esi, .rmdir_not_empty_msg
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

.rmdir_not_dir:
        mov byte [vga_color], COLOR_ERROR
        mov esi, .rmdir_not_dir_msg
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

.rmdir_not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

.rmdir_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, .rmdir_usage_msg
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

.rmdir_entry:       dd 0
.rmdir_name_ptr:    dq 0
.rmdir_blk:         dd 0
.rmdir_cnt:         dd 0
.rmdir_ok_msg:      db "Directory removed", 0x0A, 0
.rmdir_not_empty_msg: db "rmdir: directory not empty", 0x0A, 0
.rmdir_not_dir_msg: db "rmdir: not a directory", 0x0A, 0
.rmdir_usage_msg:   db "Usage: rmdir <directory>", 0x0A, 0

;---------------------------------------
; Command: read [-p PROMPT] VARNAME
; Read a line of input into an environment variable
; Usage:
;   read VAR        - Read line into $VAR
;   read -p "text" VAR - Print prompt first, then read
;---------------------------------------
cmd_read_input:
        PUSHALL
        ; ESI points to arguments (spaces already skipped)
        cmp byte [esi], 0
        je .ri_usage

        ; Check for -p flag
        cmp byte [esi], '-'
        jne .ri_no_prompt
        cmp byte [esi + 1], 'p'
        jne .ri_no_prompt
        cmp byte [esi + 2], ' '
        jne .ri_no_prompt
        add esi, 3
        ; Skip spaces to prompt text
.ri_skip_p:
        cmp byte [esi], ' '
        jne .ri_get_prompt
        inc esi
        jmp .ri_skip_p
.ri_get_prompt:
        ; Check for quoted prompt
        cmp byte [esi], '"'
        je .ri_quoted
        ; Unquoted: single word prompt
        push rsi
        call vga_print
        ; Find end of prompt word
        pop rsi
.ri_skip_prompt:
        cmp byte [esi], 0
        je .ri_usage
        cmp byte [esi], ' '
        je .ri_after_prompt
        inc esi
        jmp .ri_skip_prompt

.ri_quoted:
        inc esi                 ; skip opening quote
        push rsi
        call vga_print_until_quote
        pop rsi
        ; Advance past closing quote
.ri_find_close:
        cmp byte [esi], 0
        je .ri_usage
        cmp byte [esi], '"'
        je .ri_past_quote
        inc esi
        jmp .ri_find_close
.ri_past_quote:
        inc esi                 ; skip closing quote

.ri_after_prompt:
        ; Skip spaces to varname
.ri_skip_to_var:
        cmp byte [esi], ' '
        jne .ri_have_var
        inc esi
        jmp .ri_skip_to_var
.ri_have_var:
        cmp byte [esi], 0
        je .ri_usage
        jmp .ri_read_line

.ri_no_prompt:
        ; ESI points directly at variable name

.ri_read_line:
        ; Save variable name pointer
        mov [.ri_varname], rsi

        ; Read a line of input using kb_getchar + echo
        mov edi, .ri_input_buf
        xor ecx, ecx           ; length
.ri_key_loop:
        call kb_getchar
        cmp al, 0x0D            ; Enter
        je .ri_got_line
        cmp al, 0x0A
        je .ri_got_line
        cmp al, 0x08            ; Backspace
        je .ri_backspace
        cmp al, 3               ; Ctrl+C
        je .ri_cancel

        ; Ignore non-printable
        cmp al, 32
        jb .ri_key_loop

        ; Store and echo if room
        cmp ecx, 250
        jge .ri_key_loop
        mov [edi + ecx], al
        inc ecx
        call vga_putchar
        jmp .ri_key_loop

.ri_backspace:
        test ecx, ecx
        jz .ri_key_loop
        dec ecx
        ; Erase on screen
        mov al, 0x08
        call vga_putchar
        mov al, ' '
        call vga_putchar
        mov al, 0x08
        call vga_putchar
        jmp .ri_key_loop

.ri_cancel:
        ; Ctrl+C: abort with empty result
        mov al, 0x0A
        call vga_putchar
        POPALL
        ret

.ri_got_line:
        mov byte [edi + ecx], 0 ; null terminate input
        mov al, 0x0A
        call vga_putchar

        ; Build "VARNAME=VALUE" in env_temp_buf
        mov rsi, [.ri_varname]
        mov edi, env_temp_buf
        xor edx, edx           ; length counter
.ri_copy_name:
        lodsb
        cmp al, 0
        je .ri_name_done
        cmp al, ' '
        je .ri_name_done
        stosb
        inc edx
        cmp edx, 60
        jb .ri_copy_name
.ri_name_done:
        mov al, '='
        stosb
        ; Copy input value
        mov esi, .ri_input_buf
.ri_copy_val:
        lodsb
        stosb
        test al, al
        jnz .ri_copy_val

        ; Set the env variable
        mov esi, env_temp_buf
        call env_set_str

        POPALL
        ret

.ri_usage:
        mov byte [vga_color], COLOR_ERROR
        mov esi, .ri_usage_msg
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

.ri_varname:    dq 0
.ri_input_buf:  times 256 db 0
.ri_usage_msg:  db "Usage: read [-p prompt] VARNAME", 0x0A, 0

; Print string at ESI until '"' or null (no newline)
vga_print_until_quote:
        push rsi
.vpuq_loop:
        lodsb
        cmp al, 0
        je .vpuq_done
        cmp al, '"'
        je .vpuq_done
        call vga_putchar
        jmp .vpuq_loop
.vpuq_done:
        pop rsi
        ret

;---------------------------------------
; Command: stat <filename>
; Show detailed file information
;---------------------------------------
cmd_stat:
        PUSHALL
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
        push rdi
        mov esi, edi            ; filename at start of entry
        call vga_print
        pop rdi
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
        push rdi
        push rax
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
        push rcx
        mov ecx, 3
        div ecx
        pop rcx
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
        pop rax
        pop rdi
        mov esi, .stat_rwx_buf
        call vga_print
        mov al, ' '
        call vga_putchar
        mov al, '('
        call vga_putchar
        movzx eax, word [edi + DIRENT_PERMS]
        ; Print octal: 3 digits (each 3 bits)
        push rax
        shr eax, 6
        and eax, 7
        add al, '0'
        call vga_putchar
        pop rax
        push rax
        shr eax, 3
        and eax, 7
        add al, '0'
        call vga_putchar
        pop rax
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
        push rdi
        mov edi, .ln_read_buf
        call ata_read_sectors
        pop rdi
        jc .stat_done
        mov esi, .ln_read_buf
        call vga_print
        mov al, 0x0A
        call vga_putchar

.stat_done:
        POPALL
        ret

.stat_usage:
        mov esi, .stat_usage_s
        call vga_print
        POPALL
        ret

.stat_not_found:
        mov byte [vga_color], COLOR_ERROR
        mov esi, msg_not_found
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

; Print packed RTC timestamp in EBX as YYYY-MM-DD HH:MM:SS
.stat_print_ts:
        push rbx
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
        push rax
        mov al, '0'
        call vga_putchar
        pop rax
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
        push rax
        mov al, '0'
        call vga_putchar
        pop rax
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
        push rax
        mov al, '0'
        call vga_putchar
        pop rax
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
        push rax
        mov al, '0'
        call vga_putchar
        pop rax
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
        push rax
        mov al, '0'
        call vga_putchar
        pop rax
.spt_sec:
        call vga_print_dec
        pop rbx
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
        PUSHALL
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
        POPALL
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
        push rcx
        mov ecx, 8
.fsck_bit_loop:
        test al, 1
        jz .fsck_no_bit
        inc edx
.fsck_no_bit:
        shr al, 1
        loop .fsck_bit_loop
        pop rcx
.fsck_next_byte:
        loop .fsck_count_bits
        mov [.fsck_bmp_alloc], edx
        mov esi, .fsck_ok
        call vga_print
        jmp .fsck_step3
.fsck_bmp_fail:
        mov esi, .fsck_fail
        call vga_print
        POPALL
        ret

.fsck_step3:
        ; Step 3: Walk root directory entries, count files and block usage
        mov esi, .fsck_check_dir
        call vga_print
        ; Save current dir state
        push qword [current_dir_lba]
        push qword [current_dir_sects]
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
        push rax
        push rdx
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
        push rsi
        push rbx
        mov byte [vga_color], COLOR_ERROR
        mov esi, .fsck_warn_size
        call vga_print
        mov esi, edi
        call vga_print         ; filename
        mov al, 0x0A
        call vga_putchar
        mov byte [vga_color], COLOR_DEFAULT
        pop rbx
        pop rsi
        inc dword [.fsck_errors]
.fsck_size_ok:
        pop rdx
        pop rax
.fsck_dir_next:
        add edi, HBFS_DIR_ENTRY_SIZE
        dec ecx
        jnz .fsck_dir_loop

        mov [.fsck_file_count], ebx
        mov [.fsck_dir_blocks], edx

        ; Restore directory state
        pop qword [current_dir_sects]
        pop qword [current_dir_lba]
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
        POPALL
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
        PUSHALL
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
        mov [.ln_target], rsi
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
        push rsi               ; save linkname
        mov rsi, [.ln_target]
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
        mov rsi, [.ln_target]
        mov edi, .ln_buf
        rep movsb

        pop rsi                ; ESI = linkname
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
        POPALL
        ret

.ln_fail:
        mov byte [vga_color], COLOR_ERROR
        mov esi, .ln_err
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret

.ln_usage:
        mov esi, .ln_usage_s
        call vga_print
        POPALL
        ret

.ln_target:    dq 0
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
        PUSHALL
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
        POPALL
        ret
.chmod_fail:
        mov byte [vga_color], COLOR_ERROR
        mov esi, .chmod_err
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret
.chmod_usage:
        mov esi, .chmod_usage_s
        call vga_print
        POPALL
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
        PUSHALL
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
        POPALL
        ret
.chown_fail:
        mov byte [vga_color], COLOR_ERROR
        mov esi, .chown_err
        call vga_print
        mov byte [vga_color], COLOR_DEFAULT
        POPALL
        ret
.chown_usage:
        mov esi, .chown_usage_s
        call vga_print
        POPALL
        ret
.chown_done:   db "Owner updated.", 0x0A, 0
.chown_err:    db "chown: file not found.", 0x0A, 0
.chown_usage_s: db "Usage: chown <uid> <filename>", 0x0A, 0
