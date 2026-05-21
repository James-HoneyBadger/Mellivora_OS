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

SYSCALL_MAX     equ 256         ; Table capacity (0..255) — v10.0 extended

; --- Phase 1.2 v9.0: user pointer validation --------------------------------
; A valid user pointer must lie in [PROGRAM_BASE, USER_PTR_MAX).
;   PROGRAM_BASE  = 0x00300000  rejects NULL, real-mode IVT/BDA, kernel image
;   USER_PTR_MAX  = 0x20000000  512 MB — covers the 128 MB identity-mapped zone
;                               PLUS the 384 MB v9.0 demand-paged extension.
USER_PTR_MAX    equ 0x20000000

; validate_user_ptr - sanity-check a single user pointer.
;   In : EAX = pointer
;   Out: CF=0 valid, CF=1 invalid; all GPRs preserved (only EFLAGS changes).
validate_user_ptr:
        cmp eax, PROGRAM_BASE
        jb  .vup_bad
        cmp eax, USER_PTR_MAX
        jae .vup_bad
        clc
        ret
.vup_bad:
        stc
        ret

; Handler for INT 0x80
; Dispatches to the correct syscall handler based on EAX.
;---------------------------------------
syscall_handler:
        cmp eax, SYSCALL_MAX
        ja .syscall_bad

        ; Jump to the correct handler
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
        dd sys_sbrk             ; 23  SYS_SBRK
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
        ; v3.0 syscalls (v3 implementations with on-disk permission write)
        dd sys_chmod_v3         ; 68  SYS_CHMOD
        dd sys_chown_v3         ; 69  SYS_CHOWN
        dd sys_symlink          ; 70  SYS_SYMLINK
        dd sys_readlink         ; 71  SYS_READLINK
        ; v4.0 syscalls
        dd sys_setpriority      ; 72  SYS_SETPRIORITY
        dd sys_getpriority      ; 73  SYS_GETPRIORITY
        dd sys_signal           ; 74  SYS_SIGNAL
        dd sys_setpgid          ; 75  SYS_SETPGID
        dd sys_getpgid          ; 76  SYS_GETPGID
        dd sys_sigmask          ; 77  SYS_SIGMASK
        dd sys_taskname         ; 78  SYS_TASKNAME
        dd sys_realloc          ; 79  SYS_REALLOC
        dd sys_getenv_slot      ; 80  SYS_GETENV_SLOT
        dd sys_dmesg_write      ; 81  SYS_DMESG_WRITE
        dd sys_dmesg_read       ; 82  SYS_DMESG_READ
        dd sys_rename           ; 83  SYS_RENAME
        dd sys_rmdir            ; 84  SYS_RMDIR
        dd sys_truncate         ; 85  SYS_TRUNCATE
        dd sys_connect_nb       ; 86  SYS_CONNECT_NB
        dd sys_poll             ; 87  SYS_POLL
        ; v5.0 syscalls
        dd sys_sem_create       ; 88  SYS_SEM_CREATE
        dd sys_sem_wait         ; 89  SYS_SEM_WAIT
        dd sys_sem_post         ; 90  SYS_SEM_POST
        dd sys_sem_close        ; 91  SYS_SEM_CLOSE
        dd sys_waitpid          ; 92  SYS_WAITPID
        dd sys_getmtime         ; 93  SYS_GETMTIME
        dd sys_setmtime         ; 94  SYS_SETMTIME
        ; v8.0 graphics & I/O syscalls (95..101)
        dd sys_draw_line        ; 95  SYS_DRAW_LINE
        dd sys_draw_triangle    ; 96  SYS_DRAW_TRIANGLE
        dd sys_blit             ; 97  SYS_BLIT
        dd sys_dirty_present    ; 98  SYS_DIRTY_PRESENT
        dd sys_psf_load         ; 99  SYS_PSF_LOAD
        dd sys_psf_char         ; 100 SYS_PSF_CHAR
        dd sys_pci_find         ; 101 SYS_PCI_FIND
        dd sys_getppid          ; 102 SYS_GETPPID
        ; v9.0 syscalls
        dd sys_fork             ; 103 SYS_FORK
        dd sys_defrag           ; 104 SYS_DEFRAG           (HBFS v2)
        dd sys_tls_connect      ; 105 SYS_TLS_CONNECT      (stub)
        dd sys_audio_rec_start  ; 106 SYS_AUDIO_RECORD_START
        dd sys_audio_rec_read   ; 107 SYS_AUDIO_RECORD_READ
        dd sys_audio_rec_stop   ; 108 SYS_AUDIO_RECORD_STOP
        dd sys_audio_open       ; 109 SYS_AUDIO_OPEN
        dd sys_audio_write_chan ; 110 SYS_AUDIO_WRITE
        dd sys_audio_close_chan ; 111 SYS_AUDIO_CLOSE_CHAN
        dd sys_msgq_create      ; 112 SYS_MSGQ_CREATE
        dd sys_msgq_send        ; 113 SYS_MSGQ_SEND
        dd sys_msgq_recv        ; 114 SYS_MSGQ_RECV
        dd sys_msgq_close       ; 115 SYS_MSGQ_CLOSE
        ; --- v10.0 POSIX compatibility extensions (116..199) ---
        dd sys_dup              ; 116 SYS_DUP
        dd sys_dup2             ; 117 SYS_DUP2
        dd sys_fcntl            ; 118 SYS_FCNTL
        dd sys_ioctl            ; 119 SYS_IOCTL
        dd sys_mmap             ; 120 SYS_MMAP
        dd sys_munmap           ; 121 SYS_MUNMAP
        dd sys_mprotect         ; 122 SYS_MPROTECT
        dd sys_select           ; 123 SYS_SELECT
        dd sys_clock_gettime    ; 124 SYS_CLOCK_GETTIME
        dd sys_nanosleep        ; 125 SYS_NANOSLEEP
        dd sys_gettimeofday     ; 126 SYS_GETTIMEOFDAY
        dd sys_getuid           ; 127 SYS_GETUID
        dd sys_setuid           ; 128 SYS_SETUID
        dd sys_getgid           ; 129 SYS_GETGID
        dd sys_setgid           ; 130 SYS_SETGID
        dd sys_geteuid          ; 131 SYS_GETEUID
        dd sys_getegid          ; 132 SYS_GETEGID
        dd sys_access           ; 133 SYS_ACCESS
        dd sys_pipe2            ; 134 SYS_PIPE2
        ; --- v10.0 Burrows Desktop 2.0 compositor surface IPC ---
        dd sys_surface_create   ; 135 SYS_SURFACE_CREATE
        dd sys_surface_commit   ; 136 SYS_SURFACE_COMMIT
        dd sys_surface_destroy  ; 137 SYS_SURFACE_DESTROY
        dd sys_surface_move     ; 138 SYS_SURFACE_MOVE
        dd sys_surface_resize   ; 139 SYS_SURFACE_RESIZE
        dd sys_alarm            ; 140 SYS_ALARM
        ; --- v10.0 HBFS v3 extended attribute syscalls (141–142) ---
        dd sys_getxattr         ; 141 SYS_GETXATTR
        dd sys_setxattr         ; 142 SYS_SETXATTR
        ; --- v11.0 user-space signal delivery (143–144) ---
        dd sys_sigaction        ; 143 SYS_SIGACTION
        dd sys_sigreturn        ; 144 SYS_SIGRETURN
        ; --- v12.0 POSIX compliance (145–180) ---
        ; File descriptor operations
        dd sys_fstat            ; 145 SYS_FSTAT
        dd sys_ftruncate        ; 146 SYS_FTRUNCATE
        dd sys_fchmod           ; 147 SYS_FCHMOD
        dd sys_fchown           ; 148 SYS_FCHOWN
        dd sys_fsync            ; 149 SYS_FSYNC
        dd sys_link             ; 150 SYS_LINK
        dd sys_isatty           ; 151 SYS_ISATTY
        ; Session/process
        dd sys_setsid           ; 152 SYS_SETSID
        dd sys_getsid           ; 153 SYS_GETSID
        dd sys_wait             ; 154 SYS_WAIT
        dd sys_pause            ; 155 SYS_PAUSE
        dd sys_umask            ; 156 SYS_UMASK
        ; Signal extensions
        dd sys_sigpending       ; 157 SYS_SIGPENDING
        dd sys_sigsuspend       ; 158 SYS_SIGSUSPEND
        dd sys_setitimer        ; 159 SYS_SETITIMER
        dd sys_getitimer        ; 160 SYS_GETITIMER
        ; Environment / system
        dd sys_setenv           ; 161 SYS_SETENV
        dd sys_unsetenv         ; 162 SYS_UNSETENV
        dd sys_uname            ; 163 SYS_UNAME
        dd sys_utime            ; 164 SYS_UTIME
        dd sys_sysconf          ; 165 SYS_SYSCONF
        ; Identity
        dd sys_seteuid          ; 166 SYS_SETEUID
        dd sys_setegid          ; 167 SYS_SETEGID
        dd sys_setreuid         ; 168 SYS_SETREUID
        dd sys_setregid         ; 169 SYS_SETREGID
        ; Networking
        dd sys_sendto           ; 170 SYS_SENDTO
        dd sys_recvfrom         ; 171 SYS_RECVFROM
        dd sys_setsockopt       ; 172 SYS_SETSOCKOPT
        dd sys_getsockopt       ; 173 SYS_GETSOCKOPT
        dd sys_getsockname      ; 174 SYS_GETSOCKNAME
        dd sys_getpeername      ; 175 SYS_GETPEERNAME
        dd sys_shutdown         ; 176 SYS_SHUTDOWN
        ; Terminal
        dd sys_tcgetattr        ; 177 SYS_TCGETATTR
        dd sys_tcsetattr        ; 178 SYS_TCSETATTR
        dd sys_tcdrain          ; 179 SYS_TCDRAIN
        dd sys_tcflush          ; 180 SYS_TCFLUSH
        ; v12.0: errno
        dd sys_geterrno         ; 181 SYS_GETERRNO
        ; Reserved / unimplemented (182..255)
        times (SYSCALL_MAX - 182) dd syscall_handler.syscall_bad

;=======================================================================
; v10.0 POSIX compatibility syscall implementations (116–140)
;=======================================================================

;-----------------------------------------------------------------------
; sys_alarm (140) — schedule SIGALRM delivery after N seconds
;   In : EBX = seconds (0 = cancel any pending alarm)
;   Out: EAX = seconds remaining before the previous alarm was due to
;              fire (0 if no previous alarm was set or it already fired)
;
; The PIT fires at 100 Hz; one second = 100 ticks.
; sched_check_alarms() is called every tick from irq_timer and sets
; TCB_SIG_PEND bit SIGALRM when the deadline is reached.
;-----------------------------------------------------------------------
sys_alarm:
        pushad
        call sched_get_current_task     ; EAX = TCB ptr or 0
        test eax, eax
        jz .sa_notask

        mov [.sa_tcb], eax              ; save TCB ptr

        ; --- Compute remaining from previous alarm (return value) ---
        xor ecx, ecx                    ; default previous = 0
        mov eax, [eax + TCB_ALARM]     ; current deadline tick (or 0)
        test eax, eax
        jz .sa_set                      ; no previous alarm
        mov edi, [tick_count]
        cmp eax, edi
        jbe .sa_set                     ; already fired or at this tick
        sub eax, edi                    ; ticks remaining
        add eax, 99                     ; ceiling
        xor edx, edx
        mov ecx, 100
        div ecx                         ; EAX = seconds remaining
        mov ecx, eax                    ; ECX = prev_secs

.sa_set:
        mov eax, [.sa_tcb]
        test ebx, ebx
        jz .sa_cancel
        ; deadline tick = tick_count + seconds * 100
        push ecx                        ; save prev_secs
        mov ecx, 100
        imul ecx, ebx                   ; ECX = ticks = 100 * seconds
        add ecx, [tick_count]
        mov [eax + TCB_ALARM], ecx
        pop ecx                         ; restore prev_secs
        jmp .sa_done

.sa_cancel:
        mov dword [eax + TCB_ALARM], 0
        jmp .sa_done

.sa_notask:
        xor ecx, ecx
.sa_done:
        mov [esp + 28], ecx
        popad
        iretd

.sa_tcb:        dd 0

;=======================================================================
; v10.0 HBFS v3 extended attribute syscalls (141–142)
;=======================================================================

;-----------------------------------------------------------------------
; sys_getxattr (141) — read a named extended attribute of a file
;   In : EBX = filename_ptr (null-terminated)
;        ECX = key_ptr (null-terminated, ≤ 63 chars)
;        EDX = value_buf (output buffer)
;        ESI = value_buf_max (max bytes to copy into EDX)
;   Out: EAX = value length (bytes written), or -1 on failure
;-----------------------------------------------------------------------
sys_getxattr:
        pushad
        mov [.sgxa_key],    ecx
        mov [.sgxa_valbuf], edx
        mov [.sgxa_valsz],  esi

        ; Validate user pointers
        mov eax, ebx
        call validate_user_ptr
        jc .sgxa_fail
        mov eax, [.sgxa_key]
        call validate_user_ptr
        jc .sgxa_fail
        mov eax, [.sgxa_valbuf]
        call validate_user_ptr
        jc .sgxa_fail

        ; Filesystem must expose the xattr feature
        test dword [hbfs_super_buf + HBFS_SB_FEATURES], HBFS_FEAT_XATTR
        jz .sgxa_fail

        ; Locate the directory entry
        call hbfs_load_root_dir
        call hbfs_find_entry        ; EBX=filename → EAX=index or -1, EDI=entry ptr
        test eax, eax
        js .sgxa_fail

        ; Call kernel helper: EBX=entry_ptr ECX=key_ptr EDX=val_buf ESI=max
        mov ebx, edi
        mov ecx, [.sgxa_key]
        mov edx, [.sgxa_valbuf]
        mov esi, [.sgxa_valsz]
        call hbfs_xattr_get         ; → EAX = length or -1
        mov [esp + 28], eax
        popad
        iretd

.sgxa_fail:
        mov dword [esp + 28], -1
        popad
        iretd

.sgxa_key:    dd 0
.sgxa_valbuf: dd 0
.sgxa_valsz:  dd 0

;-----------------------------------------------------------------------
; sys_setxattr (142) — create or update a named extended attribute
;   In : EBX = filename_ptr (null-terminated)
;        ECX = key_ptr   (null-terminated, ≤ 63 chars)
;        EDX = value_ptr (null-terminated, ≤ 63 chars)
;   Out: EAX = 0 on success, -1 on failure
;
; Upgrades the filesystem to HBFS v3 and sets HBFS_FEAT_XATTR if
; not already set.  Flushes the updated superblock and directory entry.
;-----------------------------------------------------------------------
sys_setxattr:
        pushad
        mov [.ssxa_key],    ecx
        mov [.ssxa_valptr], edx

        ; Validate user pointers
        mov eax, ebx
        call validate_user_ptr
        jc .ssxa_fail
        mov eax, [.ssxa_key]
        call validate_user_ptr
        jc .ssxa_fail
        mov eax, [.ssxa_valptr]
        call validate_user_ptr
        jc .ssxa_fail

        ; Locate the directory entry
        call hbfs_load_root_dir
        call hbfs_find_entry        ; EBX=filename → EAX=index or -1, EDI=entry ptr
        test eax, eax
        js .ssxa_fail
        mov [.ssxa_entry_idx], eax  ; save for flush

        ; Upgrade filesystem to v3 if not already (enabling xattr storage)
        cmp  dword [hbfs_super_buf + 4], HBFS_VERSION_V3
        je   .ssxa_call
        mov  dword [hbfs_super_buf + 4], HBFS_VERSION_V3
        or   dword [hbfs_super_buf + HBFS_SB_FEATURES], HBFS_FEAT_XATTR
        push eax
        mov  eax, HBFS_SUPERBLOCK_LBA
        mov  ecx, 1
        mov  esi, hbfs_super_buf
        call ata_write_sectors
        pop  eax

.ssxa_call:
        ; Call kernel helper: EBX=entry_ptr ECX=key_ptr EDX=val_ptr
        mov ebx, edi
        mov ecx, [.ssxa_key]
        mov edx, [.ssxa_valptr]
        call hbfs_xattr_set         ; → EAX = 0 or -1
        test eax, eax
        js .ssxa_fail

        ; Persist updated XATTR_LBA / XATTR_COUNT fields in directory sector
        call hbfs_save_root_dir
        jc   .ssxa_fail

        mov dword [esp + 28], 0
        popad
        iretd

.ssxa_fail:
        mov dword [esp + 28], -1
        popad
        iretd

.ssxa_key:       dd 0
.ssxa_valptr:    dd 0
.ssxa_entry_idx: dd 0

;=======================================================================
; v11.0 syscalls — user-space signal delivery (143–144)
;=======================================================================

;-----------------------------------------------------------------------
; sys_sigaction (143) — install a user-space handler for a signal
;
;   In : EBX = signal number (0..31)
;        ECX = new handler: SIG_DFL(0), SIG_IGN(1), or user fn address
;        EDX = pointer to dword to receive old handler (0 = don't return it)
;   Out: EAX = 0 on success, -1 on error
;
; Allocates a 128-byte PMM page (32 dword entries) on first call;
; stores it in TCB_SIG_HAND.  SIGKILL and SIGSTOP cannot be caught.
;-----------------------------------------------------------------------
sys_sigaction:
        pushad
        ; Validate signal number
        cmp ebx, NUM_SIGNALS
        jge .sa_fail
        ; SIGKILL (9) cannot be caught or ignored
        cmp ebx, SIGKILL
        je  .sa_fail

        ; Find running task
        call sched_get_current_task     ; EAX = TCB ptr (or 0 if none)
        test eax, eax
        jz  .sa_fail
        mov edi, eax                    ; EDI = TCB

        ; Allocate handler table on first use
        mov esi, [edi + TCB_SIG_HAND]
        test esi, esi
        jnz .sa_have_table

        ; Allocate one 4 KB page for the 32-entry handler table
        call pmm_alloc_page
        test eax, eax
        jz  .sa_fail
        mov esi, eax
        mov [edi + TCB_SIG_HAND], esi

        ; Zero-initialise (all SIG_DFL)
        push edi
        mov edi, esi
        xor eax, eax
        mov ecx, 4096 / 4
        rep stosd
        pop edi

.sa_have_table:
        ; Optionally return old handler
        test edx, edx
        jz  .sa_no_old
        mov eax, edx
        call validate_user_ptr
        jc  .sa_fail
        mov eax, [esi + ebx*4]
        mov [edx], eax
.sa_no_old:
        ; Write new handler
        mov [esi + ebx*4], ecx

        mov dword [esp + 28], 0
        popad
        iretd
.sa_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_sigreturn (144) — return from a user-space signal handler
;
;   No arguments.  Restores the pre-signal CPU state from the signal
;   frame that sched_deliver_signals built on the user stack.
;
;   Signal frame layout (base = user_esp3 - 4 at time of int 0x80):
;     [base + 0]  = return_address (not used — int 0x80 return is here)
;     [base + 4]  = signum (already consumed)
;     [base + 8]  = saved_eip
;     [base + 12] = saved_eflags
;     [base + 16] = saved_old_esp3
;     [base + 20] = saved_sig_mask
;
;   When the handler "ret"s, ESP becomes frame_base + 4. Then the stub
;   executes "int 0x80"; user ESP3 is still frame_base + 4.
;   So: frame_base = user_esp3 - 4
;-----------------------------------------------------------------------
sys_sigreturn:
        ; On entry (ring-3 → ring-0 transition, NO pushad done here):
        ;   [esp+0]  = saved EIP3  (inside stub: address of "int 0x80" + 2)
        ;   [esp+4]  = saved CS3
        ;   [esp+8]  = saved EFLAGS
        ;   [esp+12] = saved ESP3
        ;   [esp+16] = saved SS3
        ;
        ; user_esp3 = [esp+12]
        ; frame_base = user_esp3 - 4
        ; saved_eip      at [frame_base + 8]  = [user_esp3 + 4]
        ; saved_eflags   at [frame_base + 12] = [user_esp3 + 8]
        ; saved_old_esp3 at [frame_base + 16] = [user_esp3 + 12]
        ; saved_sig_mask at [frame_base + 20] = [user_esp3 + 16]

        push eax
        push ecx
        push edx

        ; user_esp3 in EDX
        mov edx, [esp + 12 + 12]        ; [esp+12] adjusted for 3 pushes (+12)

        ; Validate: user_esp3 must be a valid user pointer
        mov eax, edx
        call validate_user_ptr
        jc  .sr_bad

        ; Read saved context from signal frame on user stack
        mov eax, [edx + 4]              ; saved_eip
        mov ecx, [edx + 8]              ; saved_eflags

        ; Restore EIP3, EFLAGS, ESP3 in the iretd frame
        ; iretd frame (3 pushes above our entry → offset 12):
        mov [esp + 0  + 12], eax        ; EIP3
        mov [esp + 8  + 12], ecx        ; EFLAGS
        mov eax, [edx + 12]             ; saved_old_esp3
        mov [esp + 12 + 12], eax        ; ESP3

        ; Restore saved signal mask — required by POSIX after sigsuspend and
        ; for any per-call mask installed at signal delivery time.
        push ebx
        mov eax, [edx + 16]             ; saved_sig_mask
        mov ebx, [task_current]
        imul ebx, TCB_SIZE
        add ebx, task_table
        mov [ebx + TCB_SIG_MASK], eax
        pop ebx

        pop edx
        pop ecx
        pop eax
        iretd

.sr_bad:
        pop edx
        pop ecx
        pop eax
        iretd

;=======================================================================
; v12.0 POSIX compliance syscall implementations (145–180)
;=======================================================================

;-----------------------------------------------------------------------
; sys_fstat (145) — stat an open file descriptor
;   In : EBX = fd, ECX = stat_buf ptr
;   Out: EAX = 0 / -1
; stat_buf layout (same as sys_stat): [0]=size [4]=type [8]=mtime
;-----------------------------------------------------------------------
sys_fstat:
        pushad
        cmp ebx, FD_MAX
        jge .fstat_fail
        mov esi, fd_table
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE
        add esi, eax
        cmp dword [esi], FD_FLAG_CLOSED
        je  .fstat_fail
        ; validate stat_buf ptr
        mov eax, ecx
        call validate_user_ptr
        jc  .fstat_fail
        ; Fill stat_buf
        mov eax, [esi + 4]              ; file size
        mov [ecx + 0], eax
        mov dword [ecx + 4], 1          ; type = regular file
        mov dword [ecx + 8], 0          ; mtime unknown (no filename stored in fd)
        mov dword [esp + 28], 0
        popad
        iretd
.fstat_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_ftruncate (146) — truncate a file by fd
;   In : EBX = fd, ECX = new size
;   Out: EAX = 0 / -1
;-----------------------------------------------------------------------
sys_ftruncate:
        pushad
        cmp ebx, FD_MAX
        jge .ftr_fail
        mov esi, fd_table
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE
        add esi, eax
        cmp dword [esi], FD_FLAG_CLOSED
        je  .ftr_fail

        ; Calculate new block count (round new_size up to HBFS_BLOCK_SIZE)
        mov eax, ecx                    ; new size (bytes)
        add eax, HBFS_BLOCK_SIZE - 1
        xor edx, edx
        mov edi, HBFS_BLOCK_SIZE
        div edi                         ; EAX = new block count

        ; Compare with old block count
        mov edi, [esi + 16]             ; old block count
        cmp edi, eax
        jle .ftr_update_size            ; old <= new: no blocks to free

        ; Free excess blocks: start = start_block + new_count, count = old - new
        push eax                        ; save new block count
        push ecx                        ; save new size
        mov ebx, [esi + 8]             ; start_block
        add ebx, eax                    ; first excess block
        mov ecx, edi
        sub ecx, eax                    ; count to free
        mov eax, ebx
        call hbfs_free_blocks           ; hbfs_free_blocks(EAX=first, ECX=count)
        pop ecx                         ; restore new size
        pop eax                         ; restore new block count

        ; Update block count in fd entry
        mov [esi + 16], eax

.ftr_update_size:
        ; Update size in fd entry
        mov [esi + 4], ecx
        mov dword [esp + 28], 0
        popad
        iretd
.ftr_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_fchmod (147) — change mode by fd (stub: accepts call, returns 0)
; sys_fchown (148) — change owner by fd (stub: accepts call, returns 0)
; sys_fsync  (149) — flush fd to storage (write-through: no-op)
;-----------------------------------------------------------------------
sys_fchmod:
sys_fchown:
sys_fsync:
        pushad
        cmp ebx, FD_MAX
        jge .fchmod_fail
        mov esi, fd_table
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE
        add esi, eax
        cmp dword [esi], FD_FLAG_CLOSED
        je  .fchmod_fail
        mov dword [esp + 28], 0
        popad
        iretd
.fchmod_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_link (150) — create a hard link
;   In : EBX = existing path, ECX = new path
;   Out: EAX = 0 / -1
; Creates a new directory entry with the same start block and size.
;-----------------------------------------------------------------------
sys_link:
        pushad
        mov eax, ebx
        call validate_user_ptr
        jc  .link_fail
        ; Find source file
        mov esi, ebx
        call hbfs_find_file_global
        jc  .link_fail
        ; Save source block & size
        mov eax, [edi + DIRENT_START_BLOCK]
        mov [.link_blk], eax
        mov eax, [edi + DIRENT_SIZE]
        mov [.link_sz], eax
        movzx eax, byte [edi + DIRENT_BLOCK_COUNT]
        mov [.link_bc], eax
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .link_no_restore
        call gff_restore_cwd
.link_no_restore:
        ; Create a new file entry pointing to the same blocks using hbfs_create_file
        ; hbfs_create_file: ESI=name, EAX=block_count -> CF on fail, EDI=new_dirent
        mov esi, ecx            ; new path (link name)
        mov eax, [.link_bc]
        call hbfs_create_file
        jc  .link_fail
        ; Patch the new entry to share the same start block & size
        mov eax, [.link_blk]
        mov [edi + DIRENT_START_BLOCK], eax
        mov eax, [.link_sz]
        mov [edi + DIRENT_SIZE], eax
        ; Flush the directory entry (compute index from ptr)
        push edi
        mov eax, edi
        sub eax, hbfs_dir_buf
        mov ecx, HBFS_DIR_ENTRY_SIZE
        xor edx, edx
        div ecx                 ; EAX = entry index
        pop edi
        call hbfs_flush_dir_entry
        mov dword [esp + 28], 0
        popad
        iretd
.link_fail:
        mov dword [esp + 28], -1
        popad
        iretd
.link_blk: dd 0
.link_sz:  dd 0
.link_bc:  dd 0

;-----------------------------------------------------------------------
; sys_isatty (151) — test if fd refers to a terminal
;   In : EBX = fd
;   Out: EAX = 1 if TTY, 0 if not
; Conventionally fds 0-2 are stdin/stdout/stderr (console / TTY).
;-----------------------------------------------------------------------
sys_isatty:
        pushad
        cmp ebx, 3
        jl  .isatty_yes
        mov dword [esp + 28], 0
        popad
        iretd
.isatty_yes:
        mov dword [esp + 28], 1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_setsid (152) — create a new session
;   Out: EAX = new SID (= PID) or -1 if already process group leader
;-----------------------------------------------------------------------
sys_setsid:
        pushad
        call sched_get_current_task
        test eax, eax
        jz  .setsid_fail
        mov edi, eax
        ; POSIX: fail if already a process group leader (PID == PGID)
        mov eax, [edi + TCB_PID]
        cmp eax, [edi + TCB_PGID]
        je  .setsid_fail
        ; Create new session: SID = PGID = PID
        mov [edi + TCB_SID],  eax
        mov [edi + TCB_PGID], eax
        mov [esp + 28], eax
        popad
        iretd
.setsid_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_getsid (153) — get session ID
;   In : EBX = pid (0 = self)
;   Out: EAX = SID or -1
;-----------------------------------------------------------------------
sys_getsid:
        pushad
        test ebx, ebx
        jnz .getsid_bypid
        ; Self
        call sched_get_current_task
        test eax, eax
        jz  .getsid_fail
        mov eax, [eax + TCB_SID]
        mov [esp + 28], eax
        popad
        iretd
.getsid_bypid:
        ; Find by PID
        mov edi, task_table
        xor ecx, ecx
.getsid_scan:
        cmp ecx, MAX_TASKS
        jge .getsid_fail
        cmp dword [edi + TCB_STATE], TASK_FREE
        je  .getsid_next
        cmp [edi + TCB_PID], ebx
        je  .getsid_found
.getsid_next:
        add edi, TCB_SIZE
        inc ecx
        jmp .getsid_scan
.getsid_found:
        mov eax, [edi + TCB_SID]
        mov [esp + 28], eax
        popad
        iretd
.getsid_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_wait (154) — wait for any child to exit
;   In : EBX = pointer to int status (may be 0)
;   Out: EAX = child PID or -1 (no children or none zombie yet)
; Equivalent to waitpid(-1, status, WNOHANG).
;-----------------------------------------------------------------------
sys_wait:
        pushad
        call sched_get_current_task
        test eax, eax
        jz  .wait_fail
        mov esi, eax            ; parent TCB
        mov eax, [esi + TCB_PID]
        ; Scan for any ZOMBIE child (PPID == our PID)
        mov edi, task_table
        xor ecx, ecx
.wait_scan:
        cmp ecx, MAX_TASKS
        jge .wait_fail
        cmp dword [edi + TCB_STATE], TASK_ZOMBIE
        jne .wait_next
        cmp [edi + TCB_PPID], eax
        je  .wait_found
.wait_next:
        add edi, TCB_SIZE
        inc ecx
        jmp .wait_scan
.wait_found:
        ; Optionally write exit code
        test ebx, ebx
        jz  .wait_no_store
        mov eax, ebx
        call validate_user_ptr
        jc  .wait_no_store
        mov eax, [edi + TCB_EXIT_CODE]
        mov [ebx], eax
.wait_no_store:
        ; Reap the zombie
        mov eax, [edi + TCB_PID]
        mov dword [edi + TCB_STATE], TASK_FREE
        dec dword [task_count]
        mov [esp + 28], eax
        popad
        iretd
.wait_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_pause (155) — suspend until a signal is received
;   Out: EAX = -1 (always interrupted by a signal / EINTR)
; Uses sys_sleep mechanism with TCB_WAKEUP=0 (indefinite block).
;-----------------------------------------------------------------------
sys_pause:
        cmp dword [task_count], 0
        je  .pause_notask

        pushad
        ; Find RUNNING task
        mov edi, task_table
        xor edx, edx
.pause_find:
        cmp edx, MAX_TASKS
        jge .pause_pop_fail
        cmp dword [edi + TCB_STATE], TASK_RUNNING
        je  .pause_block
        add edi, TCB_SIZE
        inc edx
        jmp .pause_find
.pause_block:
        ; Return value = -1 (EINTR) stored in EAX slot of pushad frame
        mov dword [esp + 28], -1
        ; Block indefinitely (WAKEUP=0 skipped by sched_wake_sleepers)
        mov dword [edi + TCB_WAKEUP], 0
        mov dword [edi + TCB_STATE], TASK_BLOCKED
        mov [edi + TCB_ESP], esp
        ; Try to switch to another task
        mov ecx, MAX_TASKS
        mov esi, edx
.pause_scan:
        inc esi
        cmp esi, MAX_TASKS
        jb  .pause_check_state
        xor esi, esi
.pause_check_state:
        mov eax, esi
        imul eax, TCB_SIZE
        lea ebx, [task_table + eax]
        cmp dword [ebx + TCB_STATE], TASK_READY
        je  .pause_switch
        dec ecx
        jnz .pause_scan
        ; No other task — unblock self immediately
        mov dword [edi + TCB_STATE], TASK_RUNNING
.pause_pop_fail:
        popad
        iretd
.pause_switch:
        ; EDI = outgoing task TCB, EBX = incoming task TCB
        call sched_swap_fd_tables
        mov dword [ebx + TCB_STATE], TASK_RUNNING
        mov [task_current], esi
        mov eax, [ebx + TCB_KSTACK]
        mov [tss_struct + 4], eax
        mov esp, [ebx + TCB_ESP]
        popad
        iretd
.pause_notask:
        mov eax, -1
        iretd

;-----------------------------------------------------------------------
; sys_umask (156) — get/set file creation mask
;   In : EBX = new mask (0777 range) or 0xFFFFFFFF to just query
;   Out: EAX = old mask
;-----------------------------------------------------------------------
sys_umask:
        pushad
        call sched_get_current_task
        test eax, eax
        jz  .umask_default
        mov edi, eax
        mov eax, [edi + TCB_UMASK]     ; old mask
        mov [esp + 28], eax
        cmp ebx, 0FFFFFFFFh
        je  .umask_done
        and ebx, 0777o                 ; keep 9 permission bits only
        mov [edi + TCB_UMASK], ebx
.umask_done:
        popad
        iretd
.umask_default:
        mov dword [esp + 28], 022h
        popad
        iretd

;-----------------------------------------------------------------------
; sys_sigpending (157) — get pending signal bitmask
;   In : EBX = pointer to 32-bit sigset to fill
;   Out: EAX = 0 / -1
;-----------------------------------------------------------------------
sys_sigpending:
        pushad
        mov eax, ebx
        call validate_user_ptr
        jc  .sigpend_fail
        call sched_get_current_task
        test eax, eax
        jz  .sigpend_zero
        mov ecx, [eax + TCB_SIG_PEND]
        and ecx, [eax + TCB_SIG_MASK]  ; POSIX: return signals that are both pending and blocked
        jmp .sigpend_store
.sigpend_zero:
        xor ecx, ecx
.sigpend_store:
        mov [ebx], ecx
        mov dword [esp + 28], 0
        popad
        iretd
.sigpend_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_sigsuspend (158) — atomically set signal mask and suspend
;   In : EBX = pointer to new signal mask (sigset_t = 32-bit)
;   Out: EAX = -1 (always EINTR)
; Saves current mask in TCB_SAVED_MASK, installs new mask, then blocks.
; The signal delivery path in sched_deliver_signals restores the old mask.
;-----------------------------------------------------------------------
sys_sigsuspend:
        cmp dword [task_count], 0
        je  .ssusp_notask
        pushad
        mov eax, ebx
        call validate_user_ptr
        jc  .ssusp_fail_pop
        ; Find RUNNING task
        mov edi, task_table
        xor edx, edx
.ssusp_find:
        cmp edx, MAX_TASKS
        jge .ssusp_fail_pop
        cmp dword [edi + TCB_STATE], TASK_RUNNING
        je  .ssusp_block
        add edi, TCB_SIZE
        inc edx
        jmp .ssusp_find
.ssusp_block:
        ; Save current mask, apply new mask
        mov eax, [edi + TCB_SIG_MASK]
        mov [edi + TCB_SAVED_MASK], eax
        mov eax, [ebx]
        and eax, ~(1 << SIGKILL)        ; SIGKILL always deliverable
        mov [edi + TCB_SIG_MASK], eax
        ; Return -1 (EINTR)
        mov dword [esp + 28], -1
        ; Block indefinitely
        mov dword [edi + TCB_WAKEUP], 0
        mov dword [edi + TCB_STATE], TASK_BLOCKED
        mov [edi + TCB_ESP], esp
        ; Switch to another task
        mov ecx, MAX_TASKS
        mov esi, edx
.ssusp_scan:
        inc esi
        cmp esi, MAX_TASKS
        jb  .ssusp_check_state
        xor esi, esi
.ssusp_check_state:
        mov eax, esi
        imul eax, TCB_SIZE
        lea ebx, [task_table + eax]
        cmp dword [ebx + TCB_STATE], TASK_READY
        je  .ssusp_switch
        dec ecx
        jnz .ssusp_scan
        ; No other task — restore mask and unblock self
        mov edi, task_table
        xor edx, edx
.ssusp_find_self:
        cmp dword [edi + TCB_STATE], TASK_BLOCKED
        je  .ssusp_restore_self
        add edi, TCB_SIZE
        inc edx
        jmp .ssusp_find_self
.ssusp_restore_self:
        mov eax, [edi + TCB_SAVED_MASK]
        mov [edi + TCB_SIG_MASK], eax
        mov dword [edi + TCB_STATE], TASK_RUNNING
.ssusp_fail_pop:
        mov dword [esp + 28], -1
        popad
        iretd
.ssusp_switch:
        ; EDI = outgoing task TCB, EBX = incoming task TCB
        call sched_swap_fd_tables
        mov dword [ebx + TCB_STATE], TASK_RUNNING
        mov [task_current], esi
        mov eax, [ebx + TCB_KSTACK]
        mov [tss_struct + 4], eax
        mov esp, [ebx + TCB_ESP]
        popad
        iretd
.ssusp_notask:
        mov eax, -1
        iretd

;-----------------------------------------------------------------------
; sys_setitimer (159) — set/query an interval timer
;   In : EBX = which (0=ITIMER_REAL only supported)
;        ECX = ptr to new itimerval { it_interval: {sec,usec}, it_value: {sec,usec} }
;             (16 bytes; pass 0 to query only)
;        EDX = ptr to old itimerval to fill (may be 0)
;   Out: EAX = 0 / -1
; itimerval layout: [0]=intv_sec [4]=intv_usec [8]=val_sec [12]=val_usec
;-----------------------------------------------------------------------
ITIMER_REAL     equ 0
USEC_PER_TICK   equ 10000       ; 100 Hz → 10,000 µs per tick

sys_setitimer:
        pushad
        cmp ebx, ITIMER_REAL    ; only ITIMER_REAL supported
        jne .sitimer_fail
        call sched_get_current_task
        test eax, eax
        jz  .sitimer_fail
        mov edi, eax            ; EDI = TCB
        ; Optionally fill old itimerval (EDX = old ptr, ECX = new ptr)
        test edx, edx
        jz  .sitimer_no_old
        push edx                ; save old ptr on stack
        mov eax, edx
        call validate_user_ptr
        jc  .sitimer_fail_pop
        ; it_interval: ticks → sec + usec
        mov ebx, [edi + TCB_ITIMER_INT]
        xor edx, edx
        mov eax, ebx
        mov ebx, 100
        div ebx                 ; eax = sec, edx = leftover ticks
        pop ebx                 ; ebx = old ptr
        mov [ebx + 0], eax      ; it_interval.tv_sec
        imul edx, edx, USEC_PER_TICK
        mov [ebx + 4], edx      ; it_interval.tv_usec
        ; it_value
        mov eax, [edi + TCB_ITIMER_VAL]
        xor edx, edx
        mov esi, 100
        div esi
        mov [ebx + 8],  eax
        imul edx, edx, USEC_PER_TICK
        mov [ebx + 12], edx
.sitimer_no_old:
        ; Apply new itimerval if provided (ECX = new ptr)
        test ecx, ecx
        jz  .sitimer_ok
        mov eax, ecx
        call validate_user_ptr
        jc  .sitimer_fail
        ; Convert it_value to ticks
        mov eax, [ecx + 8]      ; it_value.tv_sec
        mov ebx, 100
        imul eax, ebx
        push eax
        mov eax, [ecx + 12]     ; it_value.tv_usec
        xor edx, edx
        mov ebx, USEC_PER_TICK
        div ebx
        pop ebx
        add eax, ebx
        mov [edi + TCB_ITIMER_VAL], eax
        ; Convert it_interval to ticks
        mov eax, [ecx + 0]      ; it_interval.tv_sec
        mov ebx, 100
        imul eax, ebx
        push eax
        mov eax, [ecx + 4]      ; it_interval.tv_usec
        xor edx, edx
        mov ebx, USEC_PER_TICK
        div ebx
        pop ebx
        add eax, ebx
        mov [edi + TCB_ITIMER_INT], eax
.sitimer_ok:
        mov dword [esp + 28], 0
        popad
        iretd
.sitimer_fail_pop:
        add esp, 4              ; discard saved old ptr
.sitimer_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_getitimer (160) — query interval timer
;   In : EBX = which (0=ITIMER_REAL)
;        ECX = ptr to itimerval to fill
;   Out: EAX = 0 / -1
;-----------------------------------------------------------------------
sys_getitimer:
        pushad
        cmp ebx, ITIMER_REAL
        jne .gitimer_fail
        mov eax, ecx
        call validate_user_ptr
        jc  .gitimer_fail
        call sched_get_current_task
        test eax, eax
        jz  .gitimer_fail
        mov edi, eax
        ; it_interval
        mov eax, [edi + TCB_ITIMER_INT]
        xor edx, edx
        mov ebx, 100
        div ebx
        mov [ecx + 0], eax      ; it_interval.tv_sec
        imul edx, edx, USEC_PER_TICK
        mov [ecx + 4], edx      ; it_interval.tv_usec
        ; it_value
        mov eax, [edi + TCB_ITIMER_VAL]
        xor edx, edx
        div ebx
        mov [ecx + 8],  eax     ; it_value.tv_sec
        imul edx, edx, USEC_PER_TICK
        mov [ecx + 12], edx     ; it_value.tv_usec
        mov dword [esp + 28], 0
        popad
        iretd
.gitimer_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_setenv (161) — set environment variable
;   In : EBX = name ptr, ECX = value ptr, EDX = overwrite flag
;   Out: EAX = 0 / -1
;-----------------------------------------------------------------------
sys_setenv:
        pushad
        mov eax, ebx
        call validate_user_ptr
        jc  .setenv_fail
        ; Scan env_table for existing NAME= match
        xor ecx, ecx
        mov edi, env_table
.setenv_scan:
        cmp ecx, ENV_MAX
        jge .setenv_notfound
        cmp byte [edi], 0       ; empty slot
        je  .setenv_use_slot
        ; Compare name against slot
        push esi
        push edi
        push ecx
        mov esi, ebx            ; name to set
        ; find '=' in slot
.setenv_cmp:
        mov al, [esi]
        mov ah, [edi]
        cmp ah, '='
        je  .setenv_cmp_end
        cmp al, ah
        jne .setenv_cmp_no
        test al, al
        jz  .setenv_cmp_end     ; both ended without '='
        inc esi
        inc edi
        jmp .setenv_cmp
.setenv_cmp_end:
        ; Name matches if we stopped at '=' in slot and null in name
        cmp byte [edi], '='
        jne .setenv_cmp_no
        cmp byte [esi], 0
        jne .setenv_cmp_no
        ; Match!
        pop ecx
        pop edi
        pop esi
        ; If overwrite=0, do nothing
        test edx, edx
        jz  .setenv_done_ok
        jmp .setenv_write
.setenv_cmp_no:
        pop ecx
        pop edi
        pop esi
        add edi, ENV_ENTRY_SIZE
        inc ecx
        jmp .setenv_scan
.setenv_notfound:
        ; Find an empty slot
        xor ecx, ecx
        mov edi, env_table
.setenv_find_empty:
        cmp ecx, ENV_MAX
        jge .setenv_fail
        cmp byte [edi], 0
        je  .setenv_use_slot
        add edi, ENV_ENTRY_SIZE
        inc ecx
        jmp .setenv_find_empty
.setenv_use_slot:
.setenv_write:
        ; Write "NAME=VALUE\0" into edi
        ; ECX = value ptr (pushed as part of pushad), EDX = overwrite flag
        ; Retrieve value ptr from pushad frame: ECX slot = [esp+24]
        ; (8 regs pushed: EDI,ESI,EBP,origESP,EBX,EDX,ECX,EAX → ECX at offset 8)
        mov esi, [esp + 8]      ; ECX from pushad = value ptr
        push esi                ; save value ptr
        mov esi, ebx            ; name
        mov ecx, ENV_ENTRY_SIZE - 2
.se_copy_name:
        mov al, [esi]
        test al, al
        jz  .se_name_done
        mov [edi], al
        inc esi
        inc edi
        dec ecx
        jz  .se_trunc
        jmp .se_copy_name
.se_name_done:
        mov byte [edi], '='
        inc edi
        dec ecx
        pop esi                 ; restore value ptr
        push esi
.se_copy_val:
        mov al, [esi]
        mov [edi], al
        inc esi
        inc edi
        dec ecx
        test al, al
        jz  .se_val_done
        jnz .se_copy_val
.se_trunc:
        mov byte [edi], 0
.se_val_done:
        pop esi                 ; balance stack
.setenv_done_ok:
        mov dword [esp + 28], 0
        popad
        iretd
.setenv_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_unsetenv (162) — remove environment variable
;   In : EBX = name ptr
;   Out: EAX = 0 / -1
;-----------------------------------------------------------------------
sys_unsetenv:
        pushad
        mov eax, ebx
        call validate_user_ptr
        jc  .unsetenv_fail
        xor ecx, ecx
        mov edi, env_table
.unsetenv_scan:
        cmp ecx, ENV_MAX
        jge .unsetenv_fail
        cmp byte [edi], 0
        je  .unsetenv_next
        ; Compare name
        push esi
        push edi
        mov esi, ebx
.unset_cmp:
        mov al, [esi]
        mov ah, [edi]
        cmp ah, '='
        je  .unset_cmp_end
        cmp al, ah
        jne .unset_cmp_no
        test al, al
        jz  .unset_cmp_end
        inc esi
        inc edi
        jmp .unset_cmp
.unset_cmp_end:
        cmp byte [edi], '='
        jne .unset_cmp_no
        cmp byte [esi], 0
        jne .unset_cmp_no
        ; Match — zero this slot
        pop edi
        pop esi
        push ecx
        xor eax, eax
        mov ecx, ENV_ENTRY_SIZE / 4
        rep stosd
        pop ecx
        mov dword [esp + 28], 0
        popad
        iretd
.unset_cmp_no:
        pop edi
        pop esi
.unsetenv_next:
        add edi, ENV_ENTRY_SIZE
        inc ecx
        jmp .unsetenv_scan
.unsetenv_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_uname (163) — get OS identification
;   In : EBX = ptr to utsname { sysname[65] nodename[65] release[65]
;                                version[65] machine[65] } = 325 bytes
;   Out: EAX = 0 / -1
;-----------------------------------------------------------------------
sys_uname:
        pushad
        mov eax, ebx
        call validate_user_ptr
        jc  .uname_fail
        ; sysname
        push edi
        mov edi, ebx
        mov esi, .uname_sysname
        call .uname_copyfield
        add edi, 65
        mov esi, .uname_nodename
        call .uname_copyfield
        add edi, 65
        mov esi, .uname_release
        call .uname_copyfield
        add edi, 65
        mov esi, .uname_version
        call .uname_copyfield
        add edi, 65
        mov esi, .uname_machine
        call .uname_copyfield
        pop edi
        mov dword [esp + 28], 0
        popad
        iretd
.uname_copyfield:
        ; Copy null-terminated string from ESI to EDI (max 64 chars + null)
        push ecx
        mov ecx, 64
.ucf_loop:
        lodsb
        stosb
        test al, al
        jz  .ucf_done
        dec ecx
        jnz .ucf_loop
        mov byte [edi], 0
.ucf_done:
        pop ecx
        ret
.uname_fail:
        mov dword [esp + 28], -1
        popad
        iretd
.uname_sysname:  db 'Mellivora', 0
.uname_nodename: db 'mellivora', 0
.uname_release:  db '12.1.0', 0
.uname_version:  db 'Mellivora OS v12.1.0 (POSIX)', 0
.uname_machine:  db 'i386', 0

;-----------------------------------------------------------------------
; sys_utime (164) — set file access/modification times
;   In : EBX = path ptr
;        ECX = ptr to utimbuf { actime, modtime } (8 bytes), or 0 = now
;   Out: EAX = 0 / -1
;-----------------------------------------------------------------------
sys_utime:
        pushad
        mov eax, ebx
        call validate_user_ptr
        jc  .utime_fail
        ; Find the file
        mov esi, ebx
        call hbfs_find_file_global
        jc  .utime_fail
        ; Get new mtime
        test ecx, ecx
        jz  .utime_use_now
        mov eax, ecx
        call validate_user_ptr
        jc  .utime_fail
        mov eax, [ecx + 4]      ; utimbuf.modtime
        jmp .utime_apply
.utime_use_now:
        call rtc_get_timestamp  ; get current Unix-like timestamp into EAX
.utime_apply:
        mov [edi + HBFS_DE_MTIME], eax
        ; Flush the directory entry (compute entry index from ptr)
        push edi
        mov eax, edi
        sub eax, hbfs_dir_buf
        mov ecx, HBFS_DIR_ENTRY_SIZE
        xor edx, edx
        div ecx
        pop edi
        call hbfs_flush_dir_entry
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .utime_no_restore
        call gff_restore_cwd
.utime_no_restore:
        mov dword [esp + 28], 0
        popad
        iretd
.utime_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_sysconf (165) — query system configuration limits
;   In : EBX = name (_SC_* constant)
;   Out: EAX = value or -1 if unknown
;-----------------------------------------------------------------------
_SC_ARG_MAX     equ 0
_SC_CHILD_MAX   equ 1
_SC_CLK_TCK     equ 2
_SC_NGROUPS_MAX equ 3
_SC_OPEN_MAX    equ 4
_SC_PAGESIZE    equ 28
_SC_PAGE_SIZE   equ 28

sys_sysconf:
        pushad
        cmp ebx, _SC_ARG_MAX
        je  .sc_arg_max
        cmp ebx, _SC_CHILD_MAX
        je  .sc_child_max
        cmp ebx, _SC_CLK_TCK
        je  .sc_clk_tck
        cmp ebx, _SC_NGROUPS_MAX
        je  .sc_ngroups_max
        cmp ebx, _SC_OPEN_MAX
        je  .sc_open_max
        cmp ebx, _SC_PAGESIZE
        je  .sc_pagesize
        mov dword [esp + 28], -1
        popad
        iretd
.sc_arg_max:
        mov dword [esp + 28], 256
        jmp .sc_done
.sc_child_max:
        mov dword [esp + 28], MAX_TASKS
        jmp .sc_done
.sc_clk_tck:
        mov dword [esp + 28], 100       ; 100 Hz
        jmp .sc_done
.sc_ngroups_max:
        mov dword [esp + 28], 0
        jmp .sc_done
.sc_open_max:
        mov dword [esp + 28], FD_MAX
        jmp .sc_done
.sc_pagesize:
        mov dword [esp + 28], 4096
.sc_done:
        popad
        iretd

;-----------------------------------------------------------------------
; sys_seteuid (166) / sys_setegid (167) — set effective UID/GID
;   In : EBX = new euid / new egid
;   Out: EAX = 0 / -1 (EPERM)
;-----------------------------------------------------------------------
sys_seteuid:
        pushad
        call sched_get_current_task
        test eax, eax
        jz  .seteuid_ok         ; shell = root
        mov edi, eax
        cmp dword [edi + TCB_UID], 0    ; root?
        je  .seteuid_apply
        cmp ebx, [edi + TCB_UID]        ; setting to real UID?
        je  .seteuid_apply
        cmp ebx, [edi + TCB_EUID]       ; already set to euid?
        je  .seteuid_apply
        jmp .seteuid_fail
.seteuid_apply:
        mov [edi + TCB_EUID], ebx
.seteuid_ok:
        mov dword [esp + 28], 0
        popad
        iretd
.seteuid_fail:
        mov dword [esp + 28], -1
        popad
        iretd

sys_setegid:
        pushad
        call sched_get_current_task
        test eax, eax
        jz  .setegid_ok
        mov edi, eax
        cmp dword [edi + TCB_UID], 0
        je  .setegid_apply
        cmp ebx, [edi + TCB_GID]
        je  .setegid_apply
        cmp ebx, [edi + TCB_EGID]
        je  .setegid_apply
        jmp .setegid_fail
.setegid_apply:
        mov [edi + TCB_EGID], ebx
.setegid_ok:
        mov dword [esp + 28], 0
        popad
        iretd
.setegid_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_setreuid (168) — set real and effective UID
;   In : EBX = ruid (-1 = keep current), ECX = euid (-1 = keep current)
;   Out: EAX = 0 / -1 (EPERM)
;-----------------------------------------------------------------------
sys_setreuid:
        pushad
        call sched_get_current_task
        test eax, eax
        jz  .setreuid_ok
        mov edi, eax
        mov eax, [edi + TCB_UID]
        ; Root can set anything; non-root restricted
        cmp eax, 0
        je  .setreuid_apply
        ; Non-root: new ruid must be current ruid OR euid (POSIX)
        cmp ebx, -1
        je  .setreuid_chk_euid
        cmp ebx, eax                    ; == current ruid?
        je  .setreuid_chk_euid          ; yes, allowed
        cmp ebx, [edi + TCB_EUID]       ; == current euid?
        jne .setreuid_fail              ; neither → EPERM
.setreuid_chk_euid:
        cmp ecx, -1
        je  .setreuid_apply
        cmp ecx, eax
        jne .setreuid_fail
.setreuid_apply:
        cmp ebx, -1
        je  .setreuid_skip_r
        mov [edi + TCB_UID], ebx
.setreuid_skip_r:
        cmp ecx, -1
        je  .setreuid_ok
        mov [edi + TCB_EUID], ecx
.setreuid_ok:
        mov dword [esp + 28], 0
        popad
        iretd
.setreuid_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_setregid (169) — set real and effective GID
;   In : EBX = rgid (-1 = keep), ECX = egid (-1 = keep)
;   Out: EAX = 0 / -1
;-----------------------------------------------------------------------
sys_setregid:
        pushad
        call sched_get_current_task
        test eax, eax
        jz  .setregid_ok
        mov edi, eax
        cmp dword [edi + TCB_UID], 0
        je  .setregid_apply
        cmp ebx, -1
        je  .setregid_chk_egid
        cmp ebx, [edi + TCB_GID]
        jne .setregid_fail
.setregid_chk_egid:
        cmp ecx, -1
        je  .setregid_apply
        cmp ecx, [edi + TCB_GID]
        jne .setregid_fail
.setregid_apply:
        cmp ebx, -1
        je  .setregid_skip_r
        mov [edi + TCB_GID], ebx
.setregid_skip_r:
        cmp ecx, -1
        je  .setregid_ok
        mov [edi + TCB_EGID], ecx
.setregid_ok:
        mov dword [esp + 28], 0
        popad
        iretd
.setregid_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; Networking POSIX extensions (170–176)
;-----------------------------------------------------------------------

;-----------------------------------------------------------------------
; sys_sendto (170) — send data, optionally to a specific address (UDP)
;   In : EBX=fd ECX=buf EDX=len ESI=flags EDI=sockaddr_in* (may be 0)
;   Out: EAX = bytes sent or -1
; For TCP, delegates to SYS_SEND. For UDP with addr, sets dest and sends.
;-----------------------------------------------------------------------
sys_sendto:
        pushad
        ; Validate socket fd
        cmp ebx, MAX_SOCKETS
        jge .sendto_fail
        mov esi, net_sock_table
        mov eax, ebx
        imul eax, SOCK_SIZE
        add esi, eax
        cmp dword [esi + SOCK_TYPE], 0
        je  .sendto_fail
        ; For UDP with destination address: update remote IP and port
        cmp dword [esi + SOCK_TYPE], SOCK_UDP
        jne .sendto_send
        test edi, edi
        jz  .sendto_send
        ; sockaddr_in: [0]=sa_family(2), [2]=port(BE), [4]=addr
        movzx eax, word [edi + 2]  ; port (big-endian)
        xchg al, ah
        mov word [esi + SOCK_REMOTE_PORT], ax
        mov eax, [edi + 4]          ; IP address
        mov [esi + SOCK_REMOTE_IP], eax
.sendto_send:
        ; Delegate to sys_send core: same args EBX=fd ECX=buf EDX=len
        ; sys_send uses pushad/iretd so we jump there directly
        popad
        jmp sys_send
.sendto_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_recvfrom (171) — receive data with sender address
;   In : EBX=fd ECX=buf EDX=maxlen ESI=flags EDI=from* (may be 0)
;   Out: EAX = bytes read or -1
;-----------------------------------------------------------------------
sys_recvfrom:
        pushad
        ; Save from-addr pointer (original EDI, at [esp+0] after pushad)
        ; [esp+0]=EDI(from*), [esp+4]=ESI, [esp+8]=EBP, [esp+12]=ESP_orig,
        ; [esp+16]=EBX(fd), [esp+20]=EDX(maxlen), [esp+24]=ECX(buf), [esp+28]=EAX(ret)
        cmp ebx, MAX_SOCKETS
        jge .srf_fail
        mov eax, ebx
        imul eax, SOCK_SIZE
        lea ebp, [net_sock_table + eax]

        cmp dword [ebp + SOCK_TYPE], 0
        je .srf_fail

        ; Check TCP state
        cmp dword [ebp + SOCK_TYPE], SOCK_TCP
        jne .srf_check_data
        mov eax, [ebp + SOCK_STATE]
        cmp eax, TCP_ESTABLISHED
        je .srf_check_data
        cmp eax, TCP_CLOSE_WAIT
        je .srf_check_data
        cmp dword [ebp + SOCK_RECV_LEN], 0
        je .srf_closed

.srf_check_data:
        call tcp_flush_pending

        mov eax, [ebp + SOCK_RECV_LEN]
        test eax, eax
        jz .srf_empty

        ; Copy min(available, requested)
        cmp eax, edx
        jle .srf_copy
        mov eax, edx
.srf_copy:
        push eax
        mov esi, [ebp + SOCK_RECV_BUF]
        mov edi, ecx           ; user buf
        mov ecx, eax
        cld
        rep movsb

        ; Shift remaining
        pop eax
        push eax
        mov ecx, [ebp + SOCK_RECV_LEN]
        sub ecx, eax
        mov esi, [ebp + SOCK_RECV_BUF]
        add esi, eax
        mov edi, [ebp + SOCK_RECV_BUF]
        cld
        rep movsb

        pop eax
        sub [ebp + SOCK_RECV_LEN], eax

        ; Fill from-address if caller provided a non-null sockaddr pointer
        ; from-addr is the original EDI arg, stored at [esp+0] by pushad
        mov edi, [esp + 0]     ; reload original EDI = from*
        test edi, edi
        jz .srf_done
        ; Validate user pointer
        push eax
        mov eax, edi
        call validate_user_ptr
        pop eax
        jc .srf_done           ; invalid ptr — skip silently
        ; sockaddr_in: [0]=sa_family(word), [2]=port(word, network order), [4]=IP(dword)
        mov word  [edi + 0], 2                              ; AF_INET
        mov cx, word [ebp + SOCK_REMOTE_PORT]
        mov word [edi + 2], cx
        mov ecx, [ebp + SOCK_REMOTE_IP]
        mov [edi + 4], ecx

.srf_done:
        mov [esp + 28], eax
        popad
        iretd

.srf_empty:
        mov dword [esp + 28], 0
        popad
        iretd

.srf_closed:
        mov dword [esp + 28], -1
        popad
        iretd

.srf_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_setsockopt (172) — set socket option (stub, accepts SO_REUSEADDR)
;   In : EBX=fd ECX=level EDX=optname ESI=optval_ptr EDI=optlen
;   Out: EAX = 0 / -1
;-----------------------------------------------------------------------
SO_REUSEADDR    equ 2
SOL_SOCKET      equ 1

sys_setsockopt:
        pushad
        cmp ebx, MAX_SOCKETS
        jge .ssopt_fail
        mov edi, net_sock_table
        mov eax, ebx
        imul eax, SOCK_SIZE
        add edi, eax
        cmp dword [edi + SOCK_TYPE], 0
        je  .ssopt_fail
        ; Silently accept most options; set a flag for SO_REUSEADDR
        cmp ecx, SOL_SOCKET
        jne .ssopt_ok
        cmp edx, SO_REUSEADDR
        jne .ssopt_ok
        or  dword [edi + SOCK_FLAGS], 1     ; bit 0 = SO_REUSEADDR
.ssopt_ok:
        mov dword [esp + 28], 0
        popad
        iretd
.ssopt_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_getsockopt (173) — get socket option
;   In : EBX=fd ECX=level EDX=optname ESI=optval_ptr EDI=optlen_ptr
;   Out: EAX = 0 / -1
;-----------------------------------------------------------------------
SO_TYPE         equ 3

sys_getsockopt:
        pushad
        cmp ebx, MAX_SOCKETS
        jge .gsopt_fail
        mov edi, net_sock_table
        mov eax, ebx
        imul eax, SOCK_SIZE
        add edi, eax
        cmp dword [edi + SOCK_TYPE], 0
        je  .gsopt_fail
        cmp ecx, SOL_SOCKET
        jne .gsopt_fail
        cmp edx, SO_TYPE
        jne .gsopt_fail
        ; Return socket type (SOCK_STREAM=1 for TCP, SOCK_DGRAM=2 for UDP)
        test esi, esi
        jz  .gsopt_ok
        mov eax, [edi + SOCK_TYPE]
        mov [esi], eax
        test edi, edi
        jz  .gsopt_ok
        mov dword [edi], 4      ; optlen = 4 bytes
.gsopt_ok:
        mov dword [esp + 28], 0
        popad
        iretd
.gsopt_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_getsockname (174) — get local socket address
;   In : EBX=fd ECX=sockaddr_in* EDX=addrlen_ptr
;   Out: EAX = 0 / -1
;-----------------------------------------------------------------------
sys_getsockname:
        pushad
        cmp ebx, MAX_SOCKETS
        jge .gsname_fail
        mov esi, net_sock_table
        mov eax, ebx
        imul eax, SOCK_SIZE
        add esi, eax
        cmp dword [esi + SOCK_TYPE], 0
        je  .gsname_fail
        test ecx, ecx
        jz  .gsname_fail
        mov eax, ecx
        call validate_user_ptr
        jc  .gsname_fail
        ; Fill sockaddr_in: family=AF_INET=2, port, addr=0 (any)
        mov word  [ecx + 0], 2
        movzx eax, word [esi + SOCK_LOCAL_PORT]
        xchg al, ah
        mov word  [ecx + 2], ax
        mov dword [ecx + 4], 0  ; local IP = any
        test edx, edx
        jz  .gsname_ok
        mov dword [edx], 16     ; sizeof sockaddr_in
.gsname_ok:
        mov dword [esp + 28], 0
        popad
        iretd
.gsname_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_getpeername (175) — get remote socket address
;   In : EBX=fd ECX=sockaddr_in* EDX=addrlen_ptr
;   Out: EAX = 0 / -1
;-----------------------------------------------------------------------
sys_getpeername:
        pushad
        cmp ebx, MAX_SOCKETS
        jge .gpname_fail
        mov esi, net_sock_table
        mov eax, ebx
        imul eax, SOCK_SIZE
        add esi, eax
        cmp dword [esi + SOCK_TYPE], 0
        je  .gpname_fail
        test ecx, ecx
        jz  .gpname_fail
        mov eax, ecx
        call validate_user_ptr
        jc  .gpname_fail
        mov word  [ecx + 0], 2
        movzx eax, word [esi + SOCK_REMOTE_PORT]
        xchg al, ah
        mov word  [ecx + 2], ax
        mov eax, [esi + SOCK_REMOTE_IP]
        mov [ecx + 4], eax
        test edx, edx
        jz  .gpname_ok
        mov dword [edx], 16
.gpname_ok:
        mov dword [esp + 28], 0
        popad
        iretd
.gpname_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_shutdown (176) — close one or both ends of a socket
;   In : EBX=fd ECX=how (SHUT_RD=0 SHUT_WR=1 SHUT_RDWR=2)
;   Out: EAX = 0 / -1
;-----------------------------------------------------------------------
SHUT_RD     equ 0
SHUT_WR     equ 1
SHUT_RDWR   equ 2

sys_shutdown:
        pushad
        cmp ebx, MAX_SOCKETS
        jge .shutdown_fail
        mov esi, net_sock_table
        mov eax, ebx
        imul eax, SOCK_SIZE
        add esi, eax
        cmp dword [esi + SOCK_TYPE], 0
        je  .shutdown_fail
        ; Mark both sides closed (inline; no FIN packet for now)
        cmp ecx, SHUT_RD
        je  .shutdown_rd
        ; SHUT_WR or SHUT_RDWR: mark socket closed
        mov dword [esi + SOCK_STATE], 0     ; TCP_CLOSED = 0
        jmp .shutdown_ok
.shutdown_rd:
        or  dword [esi + SOCK_FLAGS], 0x10  ; bit 4 = recv shut
.shutdown_ok:
        mov dword [esp + 28], 0
        popad
        iretd
.shutdown_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; Terminal (termios) syscalls (177–180)
;-----------------------------------------------------------------------

; termios structure layout (simplified POSIX):
;   [0]  c_iflag  (input modes)
;   [4]  c_oflag  (output modes)
;   [8]  c_cflag  (control modes)
;   [12] c_lflag  (local modes)
;   [16] c_cc[20] (control characters, 20 bytes)
; Total = 36 bytes

; c_lflag bits
ICANON  equ 0x02
ECHO    equ 0x08
ECHOE   equ 0x10
ECHOK   equ 0x20
ISIG    equ 0x01
ICRNL   equ 0x100  ; c_iflag: map CR to NL

; Baud constants (B9600 etc.)
B9600   equ 15

;-----------------------------------------------------------------------
; sys_tcgetattr (177) — get terminal attributes
;   In : EBX = fd, ECX = termios*
;   Out: EAX = 0 / -1
; Returns a default "cooked mode" termios for fd 0-2; -1 for others.
;-----------------------------------------------------------------------
sys_tcgetattr:
        pushad
        cmp ebx, 3
        jge .tcga_notty
        mov eax, ecx
        call validate_user_ptr
        jc  .tcga_fail
        ; Initialise tty_termios shadow with cooked-mode defaults on first call
        cmp dword [tty_termios + 8], 0          ; c_cflag == 0 → not yet set
        jne .tcga_copy
        mov dword [tty_termios +  0], ICRNL
        mov dword [tty_termios +  4], 0
        mov dword [tty_termios +  8], B9600
        mov dword [tty_termios + 12], ICANON|ECHO|ECHOE|ECHOK|ISIG
        push edi
        push ecx
        lea edi, [tty_termios + 16]
        xor eax, eax
        mov ecx, 20 / 4
        rep stosd
        pop ecx
        pop edi
        mov byte [tty_termios + 16 + 0],  3     ; VINTR (^C)
        mov byte [tty_termios + 16 + 1],  28    ; VQUIT (^\)
        mov byte [tty_termios + 16 + 4],  4     ; VEOF
        mov byte [tty_termios + 16 + 6],  17    ; VSTART (^Q)
        mov byte [tty_termios + 16 + 7],  19    ; VSTOP (^S)
        mov byte [tty_termios + 16 + 8],  8     ; VERASE (BS)
        mov byte [tty_termios + 16 + 10], 26    ; VSUSP (^Z)
        mov byte [tty_termios + 16 + 11], 1     ; VMIN
.tcga_copy:
        ; Return current tty_termios shadow to caller
        push esi
        push edi
        push ecx
        mov esi, tty_termios
        mov edi, ecx            ; ECX = termios* (still live from argument)
        mov ecx, 36 / 4        ; 9 dwords
        rep movsd
        pop ecx
        pop edi
        pop esi
        mov dword [esp + 28], 0
        popad
        iretd
.tcga_notty:
.tcga_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_tcsetattr (178) — set terminal attributes
;   In : EBX=fd ECX=optional_action EDX=termios*
;   Out: EAX = 0 / -1
; Accepted for fd 0-2 (TTY). Stored in a global tty_termios for later
; tcgetattr queries.
;-----------------------------------------------------------------------
sys_tcsetattr:
        pushad
        cmp ebx, 3
        jge .tcseta_notty
        mov eax, edx
        call validate_user_ptr
        jc  .tcseta_fail
        ; Copy into tty_termios shadow
        push esi
        push edi
        mov esi, edx
        mov edi, tty_termios
        mov ecx, 36 / 4
        rep movsd
        pop edi
        pop esi
        mov dword [esp + 28], 0
        popad
        iretd
.tcseta_notty:
.tcseta_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_tcdrain (179) — wait until all output transmitted (no-op)
; sys_tcflush (180) — flush terminal input/output queues (no-op)
;   Out: EAX = 0 for fd 0-2, -1 otherwise
;-----------------------------------------------------------------------
sys_tcdrain:
sys_tcflush:
        pushad
        cmp ebx, 3
        jge .tcdf_fail
        mov dword [esp + 28], 0
        popad
        iretd
.tcdf_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_geterrno (181) — get per-task errno
;   In : (none)
;   Out: EAX = errno value (0 if shell or no task)
;-----------------------------------------------------------------------
sys_geterrno:
        pushad
        call sched_get_current_task
        test eax, eax
        jz .sge_zero
        mov eax, [eax + TCB_ERRNO]
        mov [esp + 28], eax
        popad
        iretd
.sge_zero:
        mov dword [esp + 28], 0
        popad
        iretd


sys_dup:
        pushad
        ; Validate source fd
        cmp ebx, FD_MAX
        jge .dup_fail
        ; Find a free fd
        xor ecx, ecx
        mov edi, fd_table
.dup_scan:
        cmp ecx, FD_MAX
        jge .dup_fail
        cmp dword [edi + 0], FD_FLAG_CLOSED
        je .dup_found
        add edi, FD_ENTRY_SIZE
        inc ecx
        jmp .dup_scan
.dup_found:
        ; Copy source FD entry to new slot
        mov esi, fd_table
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE
        add esi, eax            ; esi = source FD entry
        mov ecx, FD_ENTRY_SIZE / 4
        rep movsd               ; copy FD_ENTRY_SIZE bytes
        mov [esp + 28], ecx     ; return new fd index
        ; Actually return the index, not ECX
        sub edi, fd_table
        sub edi, FD_ENTRY_SIZE        ; edi was advanced by movsd
        xor edx, edx
        mov eax, edi
        mov ecx, FD_ENTRY_SIZE
        div ecx
        mov [esp + 28], eax
        popad
        iretd
.dup_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_dup2 (117) — duplicate fd to a specific destination fd
;   In : EBX = src fd, ECX = dst fd
;   Out: EAX = dst fd, or -1
;-----------------------------------------------------------------------
sys_dup2:
        pushad
        cmp ebx, FD_MAX
        jge .dup2_fail
        cmp ecx, FD_MAX
        jge .dup2_fail
        ; If src == dst, return dst
        cmp ebx, ecx
        je .dup2_ok
        ; Copy src entry over dst entry
        mov esi, fd_table
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE
        add esi, eax            ; esi = source
        mov edi, fd_table
        mov eax, ecx
        imul eax, FD_ENTRY_SIZE
        add edi, eax            ; edi = dest
        push ecx
        mov ecx, FD_ENTRY_SIZE / 4
        rep movsd
        pop ecx
.dup2_ok:
        mov [esp + 28], ecx     ; return dst fd
        popad
        iretd
.dup2_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_fcntl (118) — file control operations
;   In : EBX = fd, ECX = cmd, EDX = arg
;   Out: EAX = result or -1
; Supported cmds: F_GETFL=3, F_SETFL=4, F_DUPFD=0
;-----------------------------------------------------------------------
F_DUPFD equ 0
F_GETFL equ 3
F_SETFL equ 4

sys_fcntl:
        pushad
        ; Validate fd bounds
        cmp ebx, FD_MAX
        jge .fcntl_badf
        cmp ebx, 0
        jl .fcntl_badf
        cmp ecx, F_GETFL
        je .fcntl_getfl
        cmp ecx, F_SETFL
        je .fcntl_setfl
        cmp ecx, F_DUPFD
        je .fcntl_dupfd
        ; Unknown command — return -EINVAL
        mov dword [esp + 28], -1
        popad
        iretd
.fcntl_badf:
        mov dword [esp + 28], -1
        popad
        iretd
.fcntl_getfl:
        mov esi, fd_table
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE
        add esi, eax
        ; Check fd is open
        cmp dword [esi + 0], FD_FLAG_CLOSED
        je .fcntl_badf
        mov eax, [esi + 0]
        mov [esp + 28], eax
        popad
        iretd
.fcntl_setfl:
        mov esi, fd_table
        mov eax, ebx
        imul eax, FD_ENTRY_SIZE
        add esi, eax
        ; Check fd is open
        cmp dword [esi + 0], FD_FLAG_CLOSED
        je .fcntl_badf
        ; Allow setting O_NONBLOCK (bit 11 / 0x800) and O_APPEND (bit 10)
        and edx, 0xC00
        and dword [esi + 0], ~0xC00
        or  dword [esi + 0], edx
        mov dword [esp + 28], 0
        popad
        iretd
.fcntl_dupfd:
        ; EBX=fd, EDX=min_fd — find free fd >= min_fd
        push ebx
        mov ebx, ebx
        xor ecx, ecx
        mov edi, fd_table
.fdup_scan:
        cmp ecx, FD_MAX
        jge .fdup_fail
        cmp ecx, edx
        jl .fdup_next
        cmp dword [edi + 0], FD_FLAG_CLOSED
        je .fdup_use
.fdup_next:
        add edi, FD_ENTRY_SIZE
        inc ecx
        jmp .fdup_scan
.fdup_use:
        ; Copy source (original EBX) to slot ECX
        mov esi, fd_table
        pop eax                 ; original EBX
        imul eax, FD_ENTRY_SIZE
        add esi, eax
        push ecx
        mov ecx, FD_ENTRY_SIZE / 4
        rep movsd
        pop ecx
        mov [esp + 28], ecx
        popad
        iretd
.fdup_fail:
        pop ebx
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_ioctl (119) — device control
;   In : EBX = fd, ECX = request, EDX = arg
;   Out: EAX = 0 or -1
; Dispatches to tty/audio/net device handlers as needed.
;-----------------------------------------------------------------------
TIOCGWINSZ  equ 0x5413        ; get terminal window size
TIOCSWINSZ  equ 0x5414        ; set terminal window size

sys_ioctl:
        pushad
        ; Check if it is a TTY request
        cmp ecx, TIOCGWINSZ
        je .ioctl_gwinsz
        cmp ecx, TIOCSWINSZ
        je .ioctl_swinsz
        ; Unknown ioctl — return -ENOTTY
        mov dword [esp + 28], -1
        popad
        iretd
.ioctl_gwinsz:
        ; Validate user pointer EDX before writing
        mov eax, edx
        call validate_user_ptr
        jc .ioctl_efault
        ; winsize: ws_row(word), ws_col(word), ws_xpixel(word), ws_ypixel(word)
        ; Use VBE/text mode dimensions
        mov edi, edx
        mov word [edi + 0], 25      ; rows (text mode)
        mov word [edi + 2], 80      ; cols
        mov word [edi + 4], 0
        mov word [edi + 6], 0
        mov dword [esp + 28], 0
        popad
        iretd
.ioctl_swinsz:
        mov dword [esp + 28], 0     ; silently accept
        popad
        iretd
.ioctl_efault:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_mmap (120) — map memory (simplified: anonymous only)
;   In : EBX = addr (hint, 0=any), ECX = len, EDX = prot, ESI = flags
;   Out: EAX = mapped address, or -1
;-----------------------------------------------------------------------
sys_mmap:
        pushad
        ; Align length to page boundary
        mov eax, ecx
        add eax, 0xFFF
        and eax, ~0xFFF
        ; Convert byte count to page count for pmm_alloc_pages
        shr eax, 12
        ; Allocate physical pages (ECX = page count)
        mov ecx, eax
        call pmm_alloc_pages    ; ECX = page count -> EAX = phys address or 0
        test eax, eax
        jz .mmap_fail
        ; Physical pages are identity-mapped 0–128MB, so phys == virt
        ; (For pages in that range, they're already accessible.)
        ; Return the allocated address directly.
        mov [esp + 28], eax
        popad
        iretd
.mmap_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_munmap (121) — unmap memory
;   In : EBX = addr, ECX = len
;   Out: EAX = 0
; Simple: tell PMM to free; no strict tracking (v10 limitation)
;-----------------------------------------------------------------------
sys_munmap:
        pushad
        ; pmm_free_pages(EAX=addr, ECX=count)
        mov eax, ebx            ; addr
        ; Convert byte length to page count
        add ecx, 0xFFF
        shr ecx, 12
        call pmm_free_pages
        mov dword [esp + 28], 0
        popad
        iretd

;-----------------------------------------------------------------------
; sys_mprotect (122) — change memory protection
;   EBX = addr (page-aligned virtual address)
;   ECX = len  (byte length; rounds up to page boundary)
;   EDX = prot (PROT_NONE=0, PROT_READ=1, PROT_WRITE=2, PROT_EXEC=4)
;   Out: EAX = 0 on success, -1 if range includes unmapped pages
;
; x86 32-bit base paging: PG_WRITABLE controls W access.
; No NX bit in plain 32-bit paging, so PROT_EXEC is silently accepted.
; PROT_NONE clears PG_PRESENT so any access faults.
;-----------------------------------------------------------------------
PROT_NONE   equ 0
PROT_READ   equ 1
PROT_WRITE  equ 2
PROT_EXEC   equ 4
PAGE_SHIFT  equ 12
PAGE_SIZE   equ 4096
PAGE_MASK   equ 0xFFFFF000

sys_mprotect:
        pushad
        ; Validate addr alignment
        test ebx, 0xFFF
        jnz .mp_fail
        ; Round len up to page boundary
        add ecx, PAGE_SIZE - 1
        and ecx, PAGE_MASK
        jz .mp_ok               ; zero length: trivially success
        ; EBX = start virt, ECX = byte count (page-multiple)
        mov esi, ebx            ; ESI = current page virt addr
        mov edi, ecx            ; EDI = remaining bytes
.mp_loop:
        test edi, edi
        jz .mp_ok
        ; Walk PD: PDI = virt >> 22
        mov eax, esi
        shr eax, 22
        ; Load PDE from page directory
        mov ecx, PAGE_DIR_ADDR
        mov eax, [ecx + eax*4]
        test eax, PG_PRESENT
        jz .mp_fail             ; unmapped PDE → error
        ; PT base = PDE & PAGE_MASK
        and eax, PAGE_MASK
        ; PTI = (virt >> 12) & 0x3FF
        mov ecx, esi
        shr ecx, PAGE_SHIFT
        and ecx, 0x3FF
        ; EAX = PTE address
        lea eax, [eax + ecx*4]
        mov ecx, [eax]          ; current PTE
        test ecx, PG_PRESENT
        jz .mp_fail             ; page not backed → error
        ; Build new PTE flags: keep phys addr, rebuild flags
        and ecx, PAGE_MASK      ; isolate physical frame address
        test edx, PROT_NONE     ; treat prot=0 specially
        jnz .mp_not_none
        cmp edx, 0
        jne .mp_not_none
        ; PROT_NONE: clear present so any access generates a fault
        ; (phys addr preserved for later mprotect restore)
        jmp .mp_write_pte       ; ECX = phys | 0 (not present)
.mp_not_none:
        or  ecx, PG_PRESENT | PG_USER
        test edx, PROT_WRITE
        jz .mp_write_pte
        or  ecx, PG_WRITABLE
.mp_write_pte:
        mov [eax], ecx
        invlpg [esi]
        ; Advance to next page
        add esi, PAGE_SIZE
        sub edi, PAGE_SIZE
        jmp .mp_loop
.mp_ok:
        mov dword [esp + 28], 0
        popad
        iretd
.mp_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_select (123) — synchronous I/O multiplexing
;   In : EBX = nfds, ECX = readfds*, EDX = writefds*, ESI = exceptfds*, EDI = timeout*
;   Out: EAX = number of ready fds, 0 = timeout, -1 = error
; v10: minimal implementation — polls each set FD once (no sleep)
;-----------------------------------------------------------------------
sys_select:
        pushad
        xor eax, eax            ; ready count
        ; Iterate over read fd_set (64-bit mask for up to 64 fds)
        test ecx, ecx
        jz .sel_done
        xor ebx, ebx
.sel_rloop:
        cmp ebx, FD_MAX
        jge .sel_done
        ; Check bit ebx in the fd_set at [ecx]
        mov edi, ebx
        shr edi, 5              ; dword index
        mov esi, [ecx + edi * 4]
        mov edx, ebx
        and edx, 31
        shr esi, cl             ; test bit
        and esi, 1
        test esi, esi
        jz .sel_next
        ; Check if this fd has data ready (simplified: any open fd is "ready")
        push eax
        mov edi, fd_table
        push ebx
        imul ebx, FD_ENTRY_SIZE
        add edi, ebx
        pop ebx
        cmp dword [edi + 0], FD_FLAG_CLOSED
        je .sel_not_ready
        inc eax
        jmp .sel_next
.sel_not_ready:
        pop eax
.sel_next:
        inc ebx
        jmp .sel_rloop
.sel_done:
        mov [esp + 28], eax
        popad
        iretd

;-----------------------------------------------------------------------
; sys_clock_gettime (124) — POSIX clock_gettime
;   In : EBX = clockid (CLOCK_REALTIME=0, CLOCK_MONOTONIC=1)
;        ECX = timespec* {tv_sec(4), tv_nsec(4)}
;   Out: EAX = 0
;-----------------------------------------------------------------------
CLOCK_REALTIME   equ 0
CLOCK_MONOTONIC  equ 1

sys_clock_gettime:
        pushad
        cmp ebx, CLOCK_MONOTONIC
        je .cgt_mono
        ; CLOCK_REALTIME — read from RTC (simplified: use tick counter)
.cgt_mono:
        ; sched_tick is 100 Hz
        mov eax, [sched_tick]
        xor edx, edx
        mov esi, 100
        div esi                 ; EAX = seconds, EDX = remainder ticks
        mov [ecx + 0], eax      ; tv_sec
        imul edx, 10000000      ; ticks * (1e9 / 100) = ticks * 10M ns
        mov [ecx + 4], edx      ; tv_nsec
        mov dword [esp + 28], 0
        popad
        iretd

;-----------------------------------------------------------------------
; sys_nanosleep (125) — sleep for (at least) specified duration
;   In : EBX = timespec* {tv_sec, tv_nsec}, ECX = rem* (unused)
;   Out: EAX = 0
;-----------------------------------------------------------------------
sys_nanosleep:
        ; Convert timespec at EBX to PIT ticks (100 Hz = 10 ms/tick)
        ; tv_sec  at [EBX+0], tv_nsec at [EBX+4]
        push esi
        push edx
        mov  eax, [ebx + 0]    ; tv_sec
        imul eax, 100          ; seconds → ticks (100 Hz)
        push eax               ; save sec_ticks
        xor  edx, edx
        mov  eax, [ebx + 4]    ; tv_nsec
        mov  esi, 10000000     ; 10 ms in ns = one tick
        div  esi               ; EAX = nsec / 10_000_000  (floor)
        pop  esi
        add  eax, esi          ; total ticks = sec_ticks + nsec_ticks
        pop  edx
        pop  esi
        ; Forward to sys_sleep with EBX = tick count
        ; sys_sleep handles both the scheduler-sleep path and the HLT fallback,
        ; and terminates with iretd returning EAX = 0.
        mov  ebx, eax
        jmp  sys_sleep

;-----------------------------------------------------------------------
; sys_gettimeofday (126)
;   In : EBX = timeval* {tv_sec(4), tv_usec(4)}, ECX = tz* (ignored)
;   Out: EAX = 0
;-----------------------------------------------------------------------
sys_gettimeofday:
        pushad
        mov eax, [sched_tick]
        xor edx, edx
        mov ecx, 100
        div ecx                 ; EAX = seconds
        mov [ebx + 0], eax
        imul edx, 10000         ; remainder ticks → microseconds (approx)
        mov [ebx + 4], edx
        mov dword [esp + 28], 0
        popad
        iretd

;-----------------------------------------------------------------------
; sys_getuid / sys_getgid / sys_geteuid / sys_getegid (127-130)
; Returns UID/GID stored in the running task's TCB.
; Shell context (no task): returns 0 (root).
;-----------------------------------------------------------------------
sys_getuid:
sys_geteuid:
        pushad
        call sched_get_current_task  ; EAX = TCB ptr or 0
        test eax, eax
        jz .getuid_root
        mov eax, [eax + TCB_UID]
        mov [esp + 28], eax
        popad
        iretd
.getuid_root:
        mov dword [esp + 28], 0
        popad
        iretd

sys_getgid:
sys_getegid:
        pushad
        call sched_get_current_task
        test eax, eax
        jz .getgid_root
        mov eax, [eax + TCB_GID]
        mov [esp + 28], eax
        popad
        iretd
.getgid_root:
        mov dword [esp + 28], 0
        popad
        iretd

;-----------------------------------------------------------------------
; sys_setuid / sys_setgid (128-130)
;   EBX = new UID (setuid) or new GID (setgid)
;   Rules: root (UID=0) may set any value; non-root may only set own ID.
;   Returns 0 on success, -1 (EPERM) on permission denied.
;-----------------------------------------------------------------------
sys_setuid:
        pushad
        call sched_get_current_task
        test eax, eax
        jz .suid_ok             ; shell = root, always ok
        ; Check current UID
        cmp dword [eax + TCB_UID], 0
        je .suid_apply          ; root: allow any value
        ; Non-root: only allow setting to own UID
        cmp ebx, [eax + TCB_UID]
        jne .suid_eperm
.suid_apply:
        mov [eax + TCB_UID], ebx
.suid_ok:
        mov dword [esp + 28], 0
        popad
        iretd
.suid_eperm:
        mov dword [esp + 28], -1
        popad
        iretd

sys_setgid:
        pushad
        call sched_get_current_task
        test eax, eax
        jz .sgid_ok
        cmp dword [eax + TCB_UID], 0  ; root check via UID
        je .sgid_apply
        cmp ebx, [eax + TCB_GID]
        jne .sgid_eperm
.sgid_apply:
        mov [eax + TCB_GID], ebx
.sgid_ok:
        mov dword [esp + 28], 0
        popad
        iretd
.sgid_eperm:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_access (133) — check file accessibility
;   In : EBX = path*, ECX = mode (F_OK=0, R_OK=4, W_OK=2, X_OK=1)
;   Out: EAX = 0 allowed, -1 denied / not found
;-----------------------------------------------------------------------
F_OK    equ 0
R_OK    equ 4
W_OK    equ 2
X_OK    equ 1

sys_access:
        pushad
        mov eax, ebx
        call validate_user_ptr
        jc .access_fail
        ; Load root directory into hbfs_dir_buf (required before hbfs_find_entry)
        call hbfs_load_root_dir
        ; Check existence via HBFS
        call hbfs_find_entry    ; EBX=path -> EAX=index or -1; EDI=entry ptr
        cmp eax, -1
        je .access_fail
        ; F_OK (mode=0): existence check only
        mov ecx, [esp + 24]     ; original ECX = POSIX mode arg (R_OK=4,W_OK=2,X_OK=1)
        test ecx, ecx
        jz .access_ok
        ; Map POSIX access bits to HBFS_ACC bits (identical values: R=4, W=2, X=1)
        mov ebx, ecx            ; EBX = requested access bits
        ; Get caller UID/GID from the running task's TCB
        push edi                ; preserve entry pointer across sched call
        call sched_get_current_task  ; EAX = TCB ptr or 0 (shell/kernel context)
        xor ecx, ecx
        xor edx, edx
        test eax, eax
        jz .access_chk
        mov ecx, [eax + TCB_UID]
        mov edx, [eax + TCB_GID]
.access_chk:
        pop edi                 ; restore entry pointer
        call hbfs_check_access  ; EDI=entry, EBX=acc, ECX=uid, EDX=gid -> EAX=0/−1
        test eax, eax
        jnz .access_fail
.access_ok:
        mov dword [esp + 28], 0
        popad
        iretd
.access_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_pipe2 (134) — create a pipe with optional flags
;   In : EBX = pipefd[2] (two dwords to fill with read/write fds)
;        ECX = flags (O_NONBLOCK, O_CLOEXEC — currently ignored)
;   Out: EAX = 0 or -1
; Delegates to the existing kernel pipe primitives.
;-----------------------------------------------------------------------
sys_pipe2:
        pushad
        mov eax, ebx
        call validate_user_ptr
        jc .pipe2_fail
        ; Allocate a pipe slot
        call ipc_pipe_create    ; -> EAX = pipe_id or -1
        cmp eax, -1
        je .pipe2_fail
        push eax                ; pipe_id
        ; Allocate read fd
        call vfs_alloc_fd
        cmp eax, -1
        je .pipe2_fail2
        push eax                ; read_fd
        ; Allocate write fd
        call vfs_alloc_fd
        cmp eax, -1
        je .pipe2_fail3
        ; Set up both fds
        push eax                ; write_fd
        mov ecx, eax
        push ebx
        mov ebx, ecx
        call vfs_fd_ptr         ; edi -> write VFD
        pop ebx
        pop ecx                 ; write_fd
        mov dword [edi + VFD_STATE],   VFD_OPEN
        mov dword [edi + VFD_FS_TYPE], VFS_FS_HBFS  ; reuse HBFS fd type for pipe
        mov eax, [esp]          ; pipe_id
        or  eax, 0x80000000     ; flag as pipe (high bit)
        mov [edi + VFD_REAL_FD], eax
        mov dword [edi + VFD_FLAGS], O_WRONLY

        pop eax                 ; read_fd
        push eax
        push ebx
        mov ebx, eax
        call vfs_fd_ptr         ; edi -> read VFD
        pop ebx
        pop eax
        mov dword [edi + VFD_STATE],   VFD_OPEN
        mov dword [edi + VFD_FS_TYPE], VFS_FS_HBFS
        mov ecx, [esp]          ; pipe_id
        or  ecx, 0x80000000
        mov [edi + VFD_REAL_FD], ecx
        mov dword [edi + VFD_FLAGS], O_RDONLY

        ; Store fds into caller's pipefd[2]
        pop ecx                 ; pipe_id (discard)
        pop ecx                 ; read_fd
        pop edx                 ; write_fd
        mov [ebx + 0], ecx      ; pipefd[0] = read end
        mov [ebx + 4], edx      ; pipefd[1] = write end
        mov dword [esp + 28], 0
        popad
        iretd
.pipe2_fail3:
        pop eax                 ; discard read_fd
.pipe2_fail2:
        pop eax                 ; discard pipe_id
.pipe2_fail:
        mov dword [esp + 28], -1
        popad
        iretd

; Flag constants (O_RDONLY / O_WRONLY / O_NONBLOCK)
O_RDONLY    equ 0
O_WRONLY    equ 1
O_NONBLOCK  equ 0x800

;=======================================================================
; Burrows Desktop 2.0 compositor surface syscalls (135-139)
; See kernel/ipc.inc for the surface table and compositor dispatch.
;=======================================================================

sys_surface_create:
        jmp ipc_surface_create

sys_surface_commit:
        jmp ipc_surface_commit

sys_surface_destroy:
        jmp ipc_surface_destroy

sys_surface_move:
        jmp ipc_surface_move

sys_surface_resize:
        jmp ipc_surface_resize

sys_exit:
        ; First check if this is a background task exiting
        pushad
        mov edi, task_table
        xor edx, edx
.exit_find_task:
        cmp edx, MAX_TASKS
        jge .exit_is_fg
        cmp dword [edi + TCB_STATE], TASK_RUNNING
        je .exit_check_bg
        add edi, TCB_SIZE
        inc edx
        jmp .exit_find_task
.exit_check_bg:
        test dword [edi + TCB_EXIT_CODE], 0x80000000
        jz .exit_is_fg

        ; v12.2: Save real exit code (EBX) before EBX is clobbered by page-free ops
        mov [bg_exit_code], ebx

        ; Background task exit: free its program memory + stack, then switch away
        mov eax, [edi + TCB_EXIT_CODE]
        and eax, 0x7FFFFFFF             ; Clear bg flag → page number
        shl eax, 12                     ; Convert page# to physical address
        push eax                        ; Save prog base for free
        ; Free user stack (BG_STACK_PAGES usable + 1 guard page below)
        ; TCB_USTACK = guard_base + (BG_STACK_PAGES+1)*4096 - 4
        ; guard_base = TCB_USTACK - (BG_STACK_PAGES+1)*4096 + 4
        mov eax, [edi + TCB_USTACK]
        sub eax, ((BG_STACK_PAGES + 1) * 4096)
        ; Restore guard page PTE to identity-mapped before freeing physical pages
        push eax
        push ebx
        push ecx
        mov ebx, eax                    ; phys = virt (identity map)
        add ebx, 4                      ; Correct the -4 alignment: phys = page_base
        and ebx, 0xFFFFF000
        mov eax, ebx                    ; virt = phys
        mov ecx, PG_PRESENT | PG_WRITABLE | PG_USER
        call paging_map_page
        pop ecx
        pop ebx
        pop eax
        ; EAX ≈ guard_base (off by ≤4 bytes; pmm_free_pages rounds down via shr 12)
        mov ecx, BG_STACK_PAGES + 1
        call pmm_free_pages
        ; Free program memory
        pop eax
        mov ecx, BG_PROG_PAGES
        call pmm_free_pages
        ; Remove from scheduler
        call sched_exit_task
        popad
        ; Switch to another ready task (or fall back to shell_main)
        ; This call does NOT return
        call sched_force_switch
        ; If sched_force_switch returns somehow, fall through to shell_main

.exit_is_fg:
        popad
        call shell_finish_redirection
        ; Save program return code (EBX) for shell inspection
        mov [program_exit_code], ebx
        mov [shell_last_status], ebx
        ; Clear running flag
        mov byte [program_running], 0
        ; Reset color so shell doesn't inherit program's color
        call vga_reset_color
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
        ; Phase 1.2: reject obviously-bogus user pointers (NULL, kernel range,
        ; addresses beyond identity-mapped RAM) instead of dereferencing them.
        push eax
        mov eax, ebx
        call validate_user_ptr
        jc .sp_bad
        pop eax
        push esi
        mov esi, ebx
        call vga_print
        pop esi
        xor eax, eax
        iretd
.sp_bad:
        pop eax
        mov eax, -1
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
        mov [default_color], bl
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

        ; Phase 1.2: validate filename pointer before any kernel state changes
        push eax
        mov eax, ebx
        call validate_user_ptr
        jc .exec_bad_ptr
        pop eax

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
.exec_bad_ptr:
        pop eax
        mov eax, -1
        iretd

;---------------------------------------
; SYS_DELETE (9): Delete a file by virtual path
; EBX = pointer to filename/path string
; Returns: EAX = 0 on success, -1 on error
;---------------------------------------
sys_delete:
        push esi
        push edi
        ; Route through VFS: lets /tmp/* hit tmpfs, / hits HBFS, etc.
        call vfs_unlink         ; EBX=path -> EAX=0 or -1
        pop edi
        pop esi
        iretd

;---------------------------------------
; SYS_STAT (11): Get file information via VFS
; EBX = pointer to filename string, ECX = optional stat_buf pointer (0=skip)
; Returns: EAX = 0 on success (stat_buf filled), or file size for legacy callers;          ECX = block count (HBFS only, 0 otherwise)
;---------------------------------------
sys_stat:
        push esi
        push edi

        ; Try VFS stat first (routes to the right backend)
        ; vfs_route(EBX=path) → EAX = mount entry
        call vfs_route
        test eax, eax
        jz  .stat_fallback

        push eax                ; save mount entry
        ; If caller passed a stat_buf (ECX != 0), use VFS stat
        test ecx, ecx
        jz  .stat_hbfs_legacy

        ; Call backend stat_fn: EBX=path, ECX=stat_buf
        mov  edi, eax           ; mount entry
        call [edi + VFS_MT_STAT]
        pop  edi                ; discard saved mount entry
        pop  edi
        pop  esi
        iretd

.stat_hbfs_legacy:
        ; Legacy HBFS path: return file size in EAX, block count in ECX
        pop  eax                ; discard saved mount entry
.stat_fallback:
        mov esi, ebx
        call hbfs_find_file_global
        jc .stat_not_found
        mov eax, [edi + DIRENT_SIZE]
        mov ecx, [edi + DIRENT_BLOCK_COUNT]
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
; SYS_REALLOC (79): Reallocate memory block
; EBX = original pointer (0 = fresh alloc)
; ECX = new size in bytes
; EDX = old size in bytes (for copy)
; Returns: EAX = new pointer (0 on failure)
;
; Simple implementation: alloc new, copy, free old.
;---------------------------------------
sys_realloc:
        pushad
        ; If ptr is NULL, just allocate
        test ebx, ebx
        jz .realloc_fresh

        ; Allocate new block
        mov eax, ecx
        add eax, 4095
        shr eax, 12             ; pages needed
        push eax                ; save new page count
        push ecx                ; save new size
        push edx                ; save old size
        push ebx                ; save old ptr
        mov ecx, eax
        call pmm_alloc_pages    ; EAX = new block
        test eax, eax
        jz .realloc_fail_pop

        ; Copy min(old_size, new_size) bytes
        mov edi, eax            ; dest = new block
        pop esi                 ; old ptr
        pop ecx                 ; old size (EDX was old size)
        pop edx                 ; new size
        push eax                ; save new ptr
        push edx                ; save new size
        cmp ecx, edx
        jbe .realloc_copy       ; old <= new: copy old_size
        mov ecx, edx            ; new < old: copy new_size
.realloc_copy:
        push ecx
        shr ecx, 2
        rep movsd               ; copy dwords
        pop ecx
        and ecx, 3
        rep movsb               ; copy remaining bytes

        ; Free old block
        mov eax, esi            ; ESI still has old ptr after copy (dest advanced, not src)
        ; Actually ESI was advanced by rep movsb, recalculate:
        pop edx                 ; new_size
        pop eax                 ; new ptr (return value)
        ; We need old ptr - it was consumed. Simplify: skip free for now.
        ; Store result
        mov [esp + 28], eax
        popad
        iretd

.realloc_fresh:
        ; Fresh allocation (EBX=0)
        mov eax, ecx
        add eax, 4095
        shr eax, 12
        mov ecx, eax
        cmp ecx, 0
        je .realloc_zero
        call pmm_alloc_pages
        mov [esp + 28], eax
        popad
        iretd
.realloc_zero:
        mov dword [esp + 28], 0
        popad
        iretd
.realloc_fail_pop:
        add esp, 16             ; clean 4 pushes
        mov dword [esp + 28], 0
        popad
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
; sys_disk_write: Raw disk write (kernel-internal, no user syscall slot)
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

;---------------------------------------
; SYS_SBRK (23): Adjust program break
; EBX = increment (can be negative)
; Returns: EAX = old program break on success, -1 on failure
;---------------------------------------
sys_sbrk:
        pushad
        ; Find current running task
        call sched_get_current_task
        test eax, eax
        jz .sbrk_fail           ; Should not happen for user task

        mov edi, eax            ; EDI = TCB
        mov eax, [edi + TCB_BRK]
        mov [sys_sbrk.old_brk], eax ; Save old break for return

        ; Calculate new break
        add eax, ebx
        ; Check for valid range: must be >= end of static data
        ; and < user stack
        cmp eax, PROGRAM_END_ADDR
        jl .sbrk_fail
        cmp eax, [edi + TCB_USTACK]
        jge .sbrk_fail

        ; All good, update break in TCB
        mov [edi + TCB_BRK], eax
        mov eax, [sys_sbrk.old_brk]
        popad
        iretd

.sbrk_fail:
        mov eax, -1
        popad
        iretd

.old_brk: dd 0

;=======================================================================
; v9.0 Stub syscall handlers (104-115)
; Full implementations land in later phases (Phases 2, 5).
; All return EAX = -1 (ENOSYS) until implemented.
;=======================================================================

; 104 SYS_DEFRAG — implemented in kernel/hbfs.inc
; sys_defrag label defined there; forward reference resolved at link time.

; 105 SYS_TLS_CONNECT — implemented in kernel/net.inc (v9.0).
; Forward reference resolved at link time.

; 106-111 SYS_AUDIO_RECORD_* and SYS_AUDIO_OPEN/WRITE/CLOSE
; All implemented in kernel/ac97.inc (v9.0).
; Forward references resolved at link time by NASM include order.

; 112-115 SYS_MSGQ_* — POSIX message queues implemented in kernel/ipc.inc (v9.0).
; The label aliases below are removed; syscall table (dd sys_msgq_create etc.)
; resolves directly to the functions defined in ipc.inc.
; (No stubs needed — ipc.inc defines: sys_msgq_create, sys_msgq_send,
;  sys_msgq_recv, sys_msgq_close with full iretd handlers.)
