;=======================================================================
; SYSCALL HANDLER (INT 0x80)
;
; EAX = syscall number
; Arguments in EBX, ECX, EDX, ESI, EDI
; Return value in EAX
;=======================================================================

syscall_handler:
        cmp eax, SYS_EXIT
        je sys_exit
        cmp eax, SYS_PUTCHAR
        je sys_putchar
        cmp eax, SYS_GETCHAR
        je sys_getchar
        cmp eax, SYS_PRINT
        je sys_print
        cmp eax, SYS_READ_KEY
        je sys_read_key
        cmp eax, SYS_DELETE
        je sys_delete
        cmp eax, SYS_STAT
        je sys_stat
        cmp eax, SYS_MKDIR
        je sys_mkdir
        cmp eax, SYS_READDIR
        je sys_readdir
        cmp eax, SYS_SETCURSOR
        je sys_setcursor
        cmp eax, SYS_GETTIME
        je sys_gettime
        cmp eax, SYS_SLEEP
        je sys_sleep
        cmp eax, SYS_CLEAR
        je sys_clear
        cmp eax, SYS_SETCOLOR
        je sys_setcolor
        cmp eax, SYS_MALLOC
        je sys_malloc
        cmp eax, SYS_FREE
        je sys_free
        cmp eax, SYS_EXEC
        je sys_exec_call
        cmp eax, SYS_DISK_READ
        je sys_disk_read
        cmp eax, SYS_DISK_WRITE
        je sys_disk_write
        cmp eax, SYS_BEEP
        je sys_beep
        cmp eax, SYS_DATE
        je sys_date
        cmp eax, SYS_CHDIR
        je sys_chdir
        cmp eax, SYS_GETCWD
        je sys_getcwd
        cmp eax, SYS_SERIAL
        je sys_serial
        cmp eax, SYS_GETENV
        je sys_getenv
        cmp eax, SYS_FREAD
        je sys_fread
        cmp eax, SYS_FWRITE
        je sys_fwrite
        cmp eax, SYS_OPEN
        je sys_open_fd
        cmp eax, SYS_READ
        je sys_read_fd
        cmp eax, SYS_WRITE
        je sys_write_fd
        cmp eax, SYS_CLOSE
        je sys_close_fd
        cmp eax, SYS_SEEK
        je sys_seek_fd
        cmp eax, SYS_GETARGS
        je sys_getargs
        cmp eax, SYS_SERIAL_IN
        je sys_serial_in

        ; Unknown syscall
        mov eax, -1
        iretd

sys_exit:
        call shell_finish_redirection
        ; Save program return code (EBX) for shell inspection
        mov [program_exit_code], ebx
        ; Clear running flag
        mov byte [program_running], 0
        ; Reset color so shell doesn't inherit program's color
        mov byte [vga_color], COLOR_DEFAULT
        ; Ensure interrupts are enabled for the shell
        sti
        ; Return to shell
        mov esp, KERNEL_STACK
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

sys_sleep:
        ; EBX = ticks to sleep (don't modify caller's EBX)
        push ebx
        mov eax, [tick_count]
        add ebx, eax
.wait:
        hlt                     ; Wait for interrupt
        cmp [tick_count], ebx
        jl .wait
        pop ebx
        xor eax, eax
        iretd

sys_exec_call:
        ; EBX = pointer to filename
        ; Note: on success, cmd_exec_program never returns (iretd to ring 3)
        ; On failure (program not found), it returns with CF set
        push esi
        mov esi, ebx
        call cmd_exec_program
        pop esi
        jc .exec_fail
        xor eax, eax
        iretd
.exec_fail:
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
