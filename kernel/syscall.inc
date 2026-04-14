;=======================================================================
; SYSCALL HANDLER (INT 0x80)
;
; EAX = syscall number
; Arguments in EBX, ECX, EDX, ESI, EDI
; Return value in EAX
;
; v3.0: O(1) jump-table dispatch replaces linear if-then-else chain.
;       Table holds 128 entries (room for future syscalls).
;=======================================================================

SYSCALL_MAX     equ 128         ; Table capacity (0..127)

syscall_handler:
        cmp eax, SYSCALL_MAX
        jae .syscall_bad
        jmp [syscall_table + eax * 4]
.syscall_bad:
        mov eax, -1
        iretd

;---------------------------------------
; Syscall jump table — O(1) dispatch
; Index = syscall number, value = handler
; Unused slots point to .syscall_bad
;---------------------------------------
syscall_table:
        dd sys_exit             ;  0  SYS_EXIT
        dd sys_putchar          ;  1  SYS_PUTCHAR
        dd sys_getchar          ;  2  SYS_GETCHAR
        dd sys_print            ;  3  SYS_PRINT
        dd sys_read_key         ;  4  SYS_READ_KEY
        dd sys_open_fd          ;  5  SYS_OPEN
        dd sys_read_fd          ;  6  SYS_READ
        dd sys_write_fd         ;  7  SYS_WRITE
        dd sys_close_fd         ;  8  SYS_CLOSE
        dd sys_delete           ;  9  SYS_DELETE
        dd sys_seek_fd          ; 10  SYS_SEEK
        dd sys_stat             ; 11  SYS_STAT
        dd sys_mkdir            ; 12  SYS_MKDIR
        dd sys_readdir          ; 13  SYS_READDIR
        dd sys_setcursor        ; 14  SYS_SETCURSOR
        dd sys_gettime          ; 15  SYS_GETTIME
        dd sys_sleep            ; 16  SYS_SLEEP
        dd sys_clear            ; 17  SYS_CLEAR
        dd sys_setcolor         ; 18  SYS_SETCOLOR
        dd sys_malloc           ; 19  SYS_MALLOC
        dd sys_free             ; 20  SYS_FREE
        dd sys_exec_call        ; 21  SYS_EXEC
        dd sys_disk_read        ; 22  SYS_DISK_READ
        dd sys_disk_write       ; 23  SYS_DISK_WRITE
        dd sys_beep             ; 24  SYS_BEEP
        dd sys_date             ; 25  SYS_DATE
        dd sys_chdir            ; 26  SYS_CHDIR
        dd sys_getcwd           ; 27  SYS_GETCWD
        dd sys_serial           ; 28  SYS_SERIAL
        dd sys_getenv           ; 29  SYS_GETENV
        dd sys_fread            ; 30  SYS_FREAD
        dd sys_fwrite           ; 31  SYS_FWRITE
        dd sys_getargs          ; 32  SYS_GETARGS
        dd sys_serial_in        ; 33  SYS_SERIAL_IN
        dd sys_stdin_read       ; 34  SYS_STDIN_READ
        dd sys_yield            ; 35  SYS_YIELD
        dd sys_mouse            ; 36  SYS_MOUSE
        dd sys_framebuf         ; 37  SYS_FRAMEBUF
        dd sys_gui              ; 38  SYS_GUI
        dd sys_socket           ; 39  SYS_SOCKET
        dd sys_connect          ; 40  SYS_CONNECT
        dd sys_send             ; 41  SYS_SEND
        dd sys_recv             ; 42  SYS_RECV
        dd sys_bind             ; 43  SYS_BIND
        dd sys_listen           ; 44  SYS_LISTEN
        dd sys_accept           ; 45  SYS_ACCEPT
        dd sys_dns              ; 46  SYS_DNS
        dd sys_sockclose        ; 47  SYS_SOCKCLOSE
        dd sys_ping             ; 48  SYS_PING
        dd sys_setdate          ; 49  SYS_SETDATE
        dd sys_audio_play       ; 50  SYS_AUDIO_PLAY
        dd sys_audio_stop       ; 51  SYS_AUDIO_STOP
        dd sys_audio_status     ; 52  SYS_AUDIO_STATUS
        dd sys_kill             ; 53  SYS_KILL
        dd sys_getpid           ; 54  SYS_GETPID
        dd sys_clipboard_copy   ; 55  SYS_CLIPBOARD_COPY
        dd sys_clipboard_paste  ; 56  SYS_CLIPBOARD_PASTE
        dd sys_notify           ; 57  SYS_NOTIFY
        dd sys_file_open_dlg    ; 58  SYS_FILE_OPEN_DLG
        dd sys_file_save_dlg    ; 59  SYS_FILE_SAVE_DLG
        dd sys_pipe_create      ; 60  SYS_PIPE_CREATE
        dd sys_pipe_write       ; 61  SYS_PIPE_WRITE
        dd sys_pipe_read        ; 62  SYS_PIPE_READ
        dd sys_pipe_close       ; 63  SYS_PIPE_CLOSE
        dd sys_shmget           ; 64  SYS_SHMGET
        dd sys_shmaddr          ; 65  SYS_SHMADDR
        dd sys_proclist         ; 66  SYS_PROCLIST
        dd sys_meminfo          ; 67  SYS_MEMINFO
        ; v3.0 syscalls
        dd sys_chmod            ; 68  SYS_CHMOD
        dd sys_chown            ; 69  SYS_CHOWN
        dd sys_symlink          ; 70  SYS_SYMLINK
        dd sys_readlink         ; 71  SYS_READLINK
        ; Reserved slots (72..127) — point to bad handler
        times (SYSCALL_MAX - 72) dd syscall_handler.syscall_bad

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
        mov esp, KERNEL_STACK
        cmp byte [gui_desktop_active], 1
        je gui_desktop_resume
        jmp shell_main

sys_putchar:
        push ebx
        mov al, bl
        call vga_putchar
        pop ebx
        xor eax, eax
        iretd

sys_getchar:
        push ebx
        call kb_getchar
        movzx eax, al
        pop ebx
        iretd

sys_print:
        push esi
        mov esi, ebx
        call vga_print
        pop esi
        xor eax, eax
        iretd

sys_read_key:
        push ebx
        call kb_pollchar
        movzx eax, al
        pop ebx
        iretd

sys_clear:
        call vga_clear
        xor eax, eax
        iretd

sys_setcolor:
        mov [vga_color], bl
        xor eax, eax
        iretd

sys_setcursor:
        push edx
        mov eax, ebx            ; X
        mov edx, ecx            ; Y
        call vga_set_cursor
        pop edx
        xor eax, eax
        iretd

sys_gettime:
        mov eax, [tick_count]
        iretd

; sys_sleep removed — v3.0 blocking implementation is in sched.inc

sys_exec_call:
        ; EBX = pointer to filename
        ; Note: on success, cmd_exec_program never returns (iretd to ring 3)
        ; On failure (program not found), it returns with CF set

        ; Save current program as parent for return-after-child-exit
        push esi
        push edi
        push ecx
        mov esi, prog_name_buf
        mov edi, parent_prog_name
        mov ecx, 256
        rep movsb
        mov byte [parent_active], 1
        pop ecx
        pop edi
        pop esi

        ; Execute the requested program
        push esi
        mov esi, ebx
        call cmd_exec_program
        pop esi
        ; Only reached on failure (success does IRET internally)
        jc .exec_fail
        xor eax, eax
        iretd
.exec_fail:
        mov byte [parent_active], 0
        mov eax, -1
        iretd

;---------------------------------------
; SYS_DELETE (9): Delete a file
; EBX = pointer to filename string
; Returns: EAX = 0 on success, -1 on error
;---------------------------------------
sys_delete:
        push esi
        push edi
        mov esi, ebx
        call hbfs_find_file_global
        jc .del_not_found
        call hbfs_delete_file_entry
        ; Restore CWD if global search changed it
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .del_no_restore
        call gff_restore_cwd
.del_no_restore:
        pop edi
        pop esi
        xor eax, eax
        iretd
.del_not_found:
        pop edi
        pop esi
        mov eax, -1
        iretd

;---------------------------------------
; SYS_STAT (11): Get file information
; EBX = pointer to filename string
; Returns: EAX = file size (-1 if not found)
;          ECX = block count
;---------------------------------------
sys_stat:
        push esi
        push edi
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
        pop edi
        pop esi
        iretd
.stat_not_found:
        pop edi
        pop esi
        mov eax, -1
        xor ecx, ecx
        iretd

;---------------------------------------
; SYS_MALLOC (19): Allocate 4KB-aligned memory pages
; EBX = size in bytes (rounded up to 4KB pages)
; Returns: EAX = physical address (0 on failure)
;---------------------------------------
sys_malloc:
        push ecx
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
        pop ecx
        iretd

;---------------------------------------
; SYS_FREE (20): Free allocated memory pages
; EBX = physical address (must be page-aligned)
; ECX = size in bytes (rounded up to 4KB pages)
; Returns: EAX = 0
;---------------------------------------
sys_free:
        push ecx
        push eax
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
        pop eax
        pop ecx
        xor eax, eax
        iretd

;---------------------------------------
; SYS_DISK_READ (22): Raw disk read
; EBX = LBA (low 32 bits), ECX = sector count, EDX = dest buffer
; Returns: EAX = 0 on success, -1 on error
;---------------------------------------
sys_disk_read:
        ; Deny raw disk access from user programs (ring 3)
        cmp byte [program_running], 1
        je .dread_denied
        push edi
        mov eax, ebx
        mov edi, edx
        call ata_read_sectors
        pop edi
        jc .dread_err
        xor eax, eax
        iretd
.dread_denied:
.dread_err:
        mov eax, -1
        iretd

;---------------------------------------
; SYS_DISK_WRITE (23): Raw disk write
; EBX = LBA (low 32 bits), ECX = sector count, EDX = source buffer
; Returns: EAX = 0 on success, -1 on error
;---------------------------------------
sys_disk_write:
        ; Deny raw disk access from user programs (ring 3)
        cmp byte [program_running], 1
        je .dwrite_denied
        push esi
        mov eax, ebx
        mov esi, edx
        call ata_write_sectors
        pop esi
        jc .dwrite_err
        xor eax, eax
        iretd
.dwrite_denied:
.dwrite_err:
        mov eax, -1
        iretd
