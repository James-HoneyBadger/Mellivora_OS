;=======================================================================
; SYSCALL HANDLER (INT 0x80) — 64-bit Long Mode
;
; RAX = syscall number
; Arguments in RBX, RCX, RDX, RSI, RDI
; Return value in RAX
;
; v3.0: O(1) jump-table dispatch replaces linear if-then-else chain.
;       Table holds 128 entries (room for future syscalls).
;       v4.0: DQ entries (8-byte pointers) for 64-bit addresses.
;=======================================================================

SYSCALL_MAX     equ 128         ; Table capacity (0..127)

syscall_handler:
        cmp rax, SYSCALL_MAX
        jae .syscall_bad
        ; Strace: log entry if tracing is active (skip SYS_STRACE itself)
        cmp byte [strace_active], 0
        je .no_strace
        cmp eax, 78             ; SYS_STRACE — don't trace ourselves
        je .no_strace
        call strace_log_entry
.no_strace:
        jmp [syscall_table + rax * 8]
.syscall_bad:
        mov eax, -1
        iretq

;---------------------------------------
; strace_log_entry - Log syscall to trace buffer
; Preserves all registers. EAX=num, EBX/ECX/EDX=args
;---------------------------------------
strace_log_entry:
        push rdi
        mov edi, [strace_pos]
        cmp edi, STRACE_BUF_SIZE
        jge .strace_full
        mov [strace_buf + edi], eax
        mov [strace_buf + edi + 4], ebx
        mov [strace_buf + edi + 8], ecx
        mov [strace_buf + edi + 12], edx
        add edi, 16
        mov [strace_pos], edi
        inc dword [strace_count]
.strace_full:
        pop rdi
        ret

;---------------------------------------
; Syscall jump table — O(1) dispatch
; Index = syscall number, value = handler
; DQ = 8-byte pointers for 64-bit long mode
; Unused slots point to .syscall_bad
;---------------------------------------
syscall_table:
        dq sys_exit             ;  0  SYS_EXIT
        dq sys_putchar          ;  1  SYS_PUTCHAR
        dq sys_getchar          ;  2  SYS_GETCHAR
        dq sys_print            ;  3  SYS_PRINT
        dq sys_read_key         ;  4  SYS_READ_KEY
        dq sys_open_fd          ;  5  SYS_OPEN
        dq sys_read_fd          ;  6  SYS_READ
        dq sys_write_fd         ;  7  SYS_WRITE
        dq sys_close_fd         ;  8  SYS_CLOSE
        dq sys_delete           ;  9  SYS_DELETE
        dq sys_seek_fd          ; 10  SYS_SEEK
        dq sys_stat             ; 11  SYS_STAT
        dq sys_mkdir            ; 12  SYS_MKDIR
        dq sys_readdir          ; 13  SYS_READDIR
        dq sys_setcursor        ; 14  SYS_SETCURSOR
        dq sys_gettime          ; 15  SYS_GETTIME
        dq sys_sleep            ; 16  SYS_SLEEP
        dq sys_clear            ; 17  SYS_CLEAR
        dq sys_setcolor         ; 18  SYS_SETCOLOR
        dq sys_malloc           ; 19  SYS_MALLOC
        dq sys_free             ; 20  SYS_FREE
        dq sys_exec_call        ; 21  SYS_EXEC
        dq sys_disk_read        ; 22  SYS_DISK_READ
        dq sys_disk_write       ; 23  SYS_DISK_WRITE
        dq sys_beep             ; 24  SYS_BEEP
        dq sys_date             ; 25  SYS_DATE
        dq sys_chdir            ; 26  SYS_CHDIR
        dq sys_getcwd           ; 27  SYS_GETCWD
        dq sys_serial           ; 28  SYS_SERIAL
        dq sys_getenv           ; 29  SYS_GETENV
        dq sys_fread            ; 30  SYS_FREAD
        dq sys_fwrite           ; 31  SYS_FWRITE
        dq sys_getargs          ; 32  SYS_GETARGS
        dq sys_serial_in        ; 33  SYS_SERIAL_IN
        dq sys_stdin_read       ; 34  SYS_STDIN_READ
        dq sys_yield            ; 35  SYS_YIELD
        dq sys_mouse            ; 36  SYS_MOUSE
        dq sys_framebuf         ; 37  SYS_FRAMEBUF
        dq sys_gui              ; 38  SYS_GUI
        dq sys_socket           ; 39  SYS_SOCKET
        dq sys_connect          ; 40  SYS_CONNECT
        dq sys_send             ; 41  SYS_SEND
        dq sys_recv             ; 42  SYS_RECV
        dq sys_bind             ; 43  SYS_BIND
        dq sys_listen           ; 44  SYS_LISTEN
        dq sys_accept           ; 45  SYS_ACCEPT
        dq sys_dns              ; 46  SYS_DNS
        dq sys_sockclose        ; 47  SYS_SOCKCLOSE
        dq sys_ping             ; 48  SYS_PING
        dq sys_setdate          ; 49  SYS_SETDATE
        dq sys_audio_play       ; 50  SYS_AUDIO_PLAY
        dq sys_audio_stop       ; 51  SYS_AUDIO_STOP
        dq sys_audio_status     ; 52  SYS_AUDIO_STATUS
        dq sys_kill             ; 53  SYS_KILL
        dq sys_getpid           ; 54  SYS_GETPID
        dq sys_clipboard_copy   ; 55  SYS_CLIPBOARD_COPY
        dq sys_clipboard_paste  ; 56  SYS_CLIPBOARD_PASTE
        dq sys_notify           ; 57  SYS_NOTIFY
        dq sys_file_open_dlg    ; 58  SYS_FILE_OPEN_DLG
        dq sys_file_save_dlg    ; 59  SYS_FILE_SAVE_DLG
        dq sys_pipe_create      ; 60  SYS_PIPE_CREATE
        dq sys_pipe_write       ; 61  SYS_PIPE_WRITE
        dq sys_pipe_read        ; 62  SYS_PIPE_READ
        dq sys_pipe_close       ; 63  SYS_PIPE_CLOSE
        dq sys_shmget           ; 64  SYS_SHMGET
        dq sys_shmaddr          ; 65  SYS_SHMADDR
        dq sys_proclist         ; 66  SYS_PROCLIST
        dq sys_meminfo          ; 67  SYS_MEMINFO
        ; v3.0 syscalls
        dq sys_chmod            ; 68  SYS_CHMOD
        dq sys_chown            ; 69  SYS_CHOWN
        dq sys_symlink          ; 70  SYS_SYMLINK
        dq sys_readlink         ; 71  SYS_READLINK
        dq sys_signal           ; 72  SYS_SIGNAL
        dq sys_raise            ; 73  SYS_RAISE
        dq sys_mq_create        ; 74  SYS_MQ_CREATE
        dq sys_mq_send          ; 75  SYS_MQ_SEND
        dq sys_mq_recv          ; 76  SYS_MQ_RECV
        dq sys_mq_close         ; 77  SYS_MQ_CLOSE
        dq sys_strace           ; 78  SYS_STRACE
        dq sys_listenv          ; 79  SYS_LISTENV
        dq sys_rename           ; 80  SYS_RENAME
        dq sys_setenv           ; 81  SYS_SETENV
        dq sys_rmdir            ; 82  SYS_RMDIR
        dq sys_truncate         ; 83  SYS_TRUNCATE
        ; Reserved slots (84..127) — point to bad handler
        times (SYSCALL_MAX - 84) dq syscall_handler.syscall_bad

;---------------------------------------
; sys_strace - Control syscall tracing
; EBX=0: disable+clear, EBX=1: enable (clears buffer)
; EBX=2: read buffer (ECX=dest, EDX=maxlen) -> EAX=bytes
; EBX=3: query -> EAX=entry count
;---------------------------------------
sys_strace:
        cmp ebx, 0
        je .st_disable
        cmp ebx, 1
        je .st_enable
        cmp ebx, 2
        je .st_read
        cmp ebx, 3
        je .st_query
        mov eax, -1
        iretq
.st_disable:
        mov byte [strace_active], 0
        mov dword [strace_pos], 0
        mov dword [strace_count], 0
        xor eax, eax
        iretq
.st_enable:
        mov dword [strace_pos], 0
        mov dword [strace_count], 0
        mov byte [strace_active], 1
        xor eax, eax
        iretq
.st_read:
        PUSHALL
        mov esi, strace_buf
        mov edi, ecx
        mov ecx, [strace_pos]
        cmp ecx, edx
        jle .st_rd_ok
        mov ecx, edx
.st_rd_ok:
        push rcx
        rep movsb
        pop rcx
        mov [rsp + FRAME_RAX], rcx     ; return bytes copied in RAX
        POPALL
        iretq
.st_query:
        mov eax, [strace_count]
        iretq

sys_exit:
        call shell_finish_redirection
        ; Save program return code (EBX) for shell inspection
        mov [program_exit_code], ebx
        mov [shell_last_status], ebx
        ; Clear running flag
        mov byte [program_running], 0
        ; Reset color so shell doesn't inherit program's color
        mov byte [vga_color], COLOR_DEFAULT
        ; Ensure interrupts are enabled for the shell
        sti
        ; Return to shell (or desktop if GUI active)
        mov rsp, KERNEL_STACK
        cmp byte [gui_desktop_active], 1
        je gui_desktop_resume
        jmp shell_main

sys_putchar:
        push rbx
        mov al, bl
        call vga_putchar
        pop rbx
        xor eax, eax
        iretq

sys_getchar:
        push rbx
        call kb_getchar
        movzx eax, al
        pop rbx
        iretq

sys_print:
        push rsi
        mov esi, ebx
        call vga_print
        pop rsi
        xor eax, eax
        iretq

sys_read_key:
        push rbx
        call kb_pollchar
        movzx eax, al
        pop rbx
        iretq

sys_clear:
        call vga_clear
        xor eax, eax
        iretq

sys_setcolor:
        mov [vga_color], bl
        xor eax, eax
        iretq

sys_setcursor:
        push rdx
        mov eax, ebx            ; X
        mov edx, ecx            ; Y
        call vga_set_cursor
        pop rdx
        xor eax, eax
        iretq

sys_gettime:
        mov eax, [tick_count]
        iretq

; sys_sleep removed — v3.0 blocking implementation is in sched.inc

sys_exec_call:
        ; EBX = pointer to filename
        ; Note: on success, cmd_exec_program never returns (iretq to ring 3)
        ; On failure (program not found), it returns with CF set

        ; Save current program as parent for return-after-child-exit
        push rsi
        push rdi
        push rcx
        mov esi, prog_name_buf
        mov edi, parent_prog_name
        mov ecx, 256
        rep movsb
        mov byte [parent_active], 1
        pop rcx
        pop rdi
        pop rsi

        ; Execute the requested program
        push rsi
        mov esi, ebx
        call cmd_exec_program
        pop rsi
        ; Only reached on failure (success does IRET internally)
        jc .exec_fail
        xor eax, eax
        iretq
.exec_fail:
        mov byte [parent_active], 0
        mov eax, -1
        iretq

;---------------------------------------
; SYS_DELETE (9): Delete a file
; EBX = pointer to filename string
; Returns: EAX = 0 on success, -1 on error
;---------------------------------------
sys_delete:
        push rsi
        push rdi
        mov esi, ebx
        call hbfs_find_file_global
        jc .del_not_found
        call hbfs_delete_file_entry
        ; Restore CWD if global search changed it
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .del_no_restore
        call gff_restore_cwd
.del_no_restore:
        pop rdi
        pop rsi
        xor eax, eax
        iretq
.del_not_found:
        pop rdi
        pop rsi
        mov eax, -1
        iretq

;---------------------------------------
; SYS_STAT (11): Get file information
; EBX = pointer to filename string
; Returns: EAX = file size (-1 if not found)
;          ECX = block count
;---------------------------------------
sys_stat:
        push rsi
        push rdi
        mov esi, ebx
        call hbfs_find_file_global
        jc .stat_not_found
        mov eax, [edi + 256]    ; File size
        mov ecx, [edi + 264]    ; Block count
        ; Restore CWD if global search changed it
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .stat_no_restore
        call gff_restore_cwd
.stat_no_restore:
        pop rdi
        pop rsi
        iretq
.stat_not_found:
        pop rdi
        pop rsi
        mov eax, -1
        xor ecx, ecx
        iretq

;---------------------------------------
; SYS_MALLOC (19): Allocate 4KB-aligned memory pages
; EBX = size in bytes (rounded up to 4KB pages)
; Returns: EAX = physical address (0 on failure)
;---------------------------------------
sys_malloc:
        push rcx
        ; Round up to pages
        mov eax, ebx
        add eax, 4095
        shr eax, 12             ; / 4096 = pages needed
        mov ecx, eax            ; ECX = page count
        xor eax, eax
        cmp ecx, 0
        je .malloc_done
        ; Allocate pages from PMM
        call pmm_alloc_pages
.malloc_done:
        pop rcx
        iretq

;---------------------------------------
; SYS_FREE (20): Free allocated memory pages
; EBX = physical address (must be page-aligned)
; ECX = size in bytes (rounded up to 4KB pages)
; Returns: EAX = 0
;---------------------------------------
sys_free:
        push rcx
        push rax
        mov eax, ecx
        add eax, 4095
        shr eax, 12             ; pages
        mov ecx, eax
        mov eax, ebx            ; physical address
        and eax, 0xFFFFF000     ; align to page boundary
        cmp ecx, 0
        je .free_done
.free_loop:
        call pmm_free_page
        add eax, 0x1000         ; advance to next page
        loop .free_loop
.free_done:
        pop rax
        pop rcx
        xor eax, eax
        iretq

;---------------------------------------
; SYS_DISK_READ (22): Raw disk read
; EBX = LBA (low 32 bits), ECX = sector count, EDX = dest buffer
; Returns: EAX = 0 on success, -1 on error
;---------------------------------------
sys_disk_read:
        ; Deny raw disk access from user programs (ring 3)
        cmp byte [program_running], 1
        je .dread_denied
        push rdi
        mov eax, ebx
        mov edi, edx
        call ata_read_sectors
        pop rdi
        jc .dread_err
        xor eax, eax
        iretq
.dread_denied:
.dread_err:
        mov eax, -1
        iretq

;---------------------------------------
; SYS_DISK_WRITE (23): Raw disk write
; EBX = LBA (low 32 bits), ECX = sector count, EDX = source buffer
; Returns: EAX = 0 on success, -1 on error
;---------------------------------------
sys_disk_write:
        ; Deny raw disk access from user programs (ring 3)
        cmp byte [program_running], 1
        je .dwrite_denied
        push rsi
        mov eax, ebx
        mov esi, edx
        call ata_write_sectors
        pop rsi
        jc .dwrite_err
        xor eax, eax
        iretq
.dwrite_denied:
.dwrite_err:
        mov eax, -1
        iretq
