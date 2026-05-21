;=======================================================================
; vfs.inc - Virtual File System (VFS) Abstraction Layer
;
; Provides a uniform file-operation interface that dispatches to the
; concrete filesystem backend registered for each mount point.
;
; Supported backends:
;   hbfs  - Honey Badger File System (/, default)
;   procfs - /proc  (process information, read-only)
;   devfs  - /dev   (device nodes: null, zero, random, tty)
;   tmpfs  - /tmp   (RAM-backed ephemeral storage)
;
; Architecture:
;   VFS_MOUNT_TABLE  — up to VFS_MAX_MOUNTS entries
;   Each mount entry stores:
;       path prefix  (null-terminated, 16 bytes)
;       fs_type      (byte: VFS_FS_HBFS / VFS_FS_PROC / VFS_FS_DEV / VFS_FS_TMP)
;       open_fn      (dword: function pointer for open)
;       read_fn      (dword: function pointer for read)
;       write_fn     (dword: function pointer for write)
;       readdir_fn   (dword: function pointer for readdir)
;       stat_fn      (dword: function pointer for stat)
;
; Usage:
;   vfs_init        — call once during kernel init (after hbfs_init)
;   vfs_open        — EBX=path, ECX=flags -> EAX=vfd (virtual FD) or -1
;   vfs_read        — EBX=vfd, ECX=buf, EDX=len -> EAX=bytes or -1
;   vfs_write       — EBX=vfd, ECX=buf, EDX=len -> EAX=bytes or -1
;   vfs_close       — EBX=vfd -> EAX=0
;   vfs_readdir     — EBX=path, ECX=buf, EDX=index -> EAX=type or -1
;   vfs_stat        — EBX=path, ECX=stat_buf -> EAX=0 or -1
;   vfs_mkdir       — EBX=path -> EAX=0 or -1
;   vfs_unlink      — EBX=path -> EAX=0 or -1
;
; v10.0: initial implementation
;=======================================================================

; Filesystem type codes
VFS_FS_NONE  equ 0
VFS_FS_HBFS  equ 1      ; HBFS (Honey Badger File System)
VFS_FS_PROC  equ 2      ; procfs (/proc)
VFS_FS_DEV   equ 3      ; devfs (/dev)
VFS_FS_TMP   equ 4      ; tmpfs (/tmp)

; Max mount points
VFS_MAX_MOUNTS  equ 8

; Mount table entry layout (48 bytes)
VFS_MT_PATH     equ 0    ; path prefix (16 bytes, null-terminated)
VFS_MT_TYPE     equ 16   ; fs_type  (byte)
VFS_MT_OPEN     equ 20   ; open_fn  (dword)
VFS_MT_READ     equ 24   ; read_fn  (dword)
VFS_MT_WRITE    equ 28   ; write_fn (dword)
VFS_MT_READDIR  equ 32   ; readdir_fn (dword)
VFS_MT_STAT     equ 36   ; stat_fn  (dword)
VFS_MT_MKDIR    equ 40   ; mkdir_fn (dword)
VFS_MT_UNLINK   equ 44   ; unlink_fn(dword)
VFS_MT_SIZE     equ 48

; Virtual file descriptor table
VFS_MAX_FDS     equ 64   ; total virtual FDs
VFD_FREE        equ 0
VFD_OPEN        equ 1

; VFD entry layout (32 bytes)
VFD_STATE       equ 0    ; byte: VFD_FREE / VFD_OPEN
VFD_FS_TYPE     equ 4    ; dword: which FS backend owns this fd
VFD_REAL_FD     equ 8    ; dword: underlying fd in the concrete FS (or index)
VFD_POS         equ 12   ; dword: current position / byte offset
VFD_FLAGS       equ 16   ; dword: open flags
VFD_SIZE        equ 32

; procfs special file sub-types (stored in VFD_REAL_FD)
PROC_FD_STATUS  equ 0    ; /proc/<pid>/status
PROC_FD_CMDLINE equ 1    ; /proc/<pid>/cmdline
PROC_FD_MAPS    equ 2    ; /proc/<pid>/maps
PROC_FD_UPTIME  equ 3    ; /proc/uptime
PROC_FD_MEMINFO equ 4    ; /proc/meminfo
PROC_FD_VERSION equ 5    ; /proc/version
PROC_FD_CPUINFO equ 6    ; /proc/cpuinfo

; devfs node IDs
DEV_NULL        equ 0    ; /dev/null
DEV_ZERO        equ 1    ; /dev/zero
DEV_RANDOM      equ 2    ; /dev/random
DEV_URANDOM     equ 3    ; /dev/urandom
DEV_TTY         equ 4    ; /dev/tty  (current console)
DEV_FULL        equ 5    ; /dev/full (always ENOSPC on write)

; tmpfs limits
TMPFS_MAX_FILES equ 64
TMPFS_MAX_BYTES equ (4 * 1024 * 1024)   ; 4 MB total RAM for /tmp
TMPFS_NAME_LEN  equ 64
TMPFS_ENTRY_SIZE equ 96  ; name(64) + state(1) + pad(3) + size(4) + buf_ptr(4) + ctime(4) ...

;=======================================================================
; vfs_init - Register default mount points and initialise sub-FSes.
;=======================================================================
vfs_init:
        pushad

        ; Zero the mount table and VFD table
        mov edi, vfs_mount_table
        xor eax, eax
        mov ecx, (VFS_MAX_MOUNTS * VFS_MT_SIZE) / 4
        rep stosd
        mov edi, vfs_fd_table
        mov ecx, (VFS_MAX_FDS * VFD_SIZE) / 4
        rep stosd

        ; Mount 0: HBFS at "/" (catch-all — matched last)
        call vfs_mount_hbfs

        ; Mount 1: procfs at "/proc"
        call vfs_mount_proc

        ; Mount 2: devfs at "/dev"
        call vfs_mount_dev

        ; Mount 3: tmpfs at "/tmp"
        call vfs_mount_tmp

        mov esi, msg_vfs_ok
        call vga_print

        popad
        ret

;---------------------------------------
; vfs_mount_hbfs — register HBFS at "/"
;---------------------------------------
vfs_mount_hbfs:
        push edi
        mov edi, vfs_mount_table + 0 * VFS_MT_SIZE
        ; Path prefix: "/"
        mov byte [edi + VFS_MT_PATH], '/'
        mov byte [edi + VFS_MT_PATH + 1], 0
        mov byte [edi + VFS_MT_TYPE], VFS_FS_HBFS
        mov dword [edi + VFS_MT_OPEN],    vfs_hbfs_open
        mov dword [edi + VFS_MT_READ],    vfs_hbfs_read
        mov dword [edi + VFS_MT_WRITE],   vfs_hbfs_write
        mov dword [edi + VFS_MT_READDIR], vfs_hbfs_readdir
        mov dword [edi + VFS_MT_STAT],    vfs_hbfs_stat
        mov dword [edi + VFS_MT_MKDIR],   vfs_hbfs_mkdir
        mov dword [edi + VFS_MT_UNLINK],  vfs_hbfs_unlink
        pop edi
        ret

;---------------------------------------
; vfs_mount_proc — register procfs at "/proc"
;---------------------------------------
vfs_mount_proc:
        push edi
        mov edi, vfs_mount_table + 1 * VFS_MT_SIZE
        mov dword [edi + VFS_MT_PATH],   '/pro'
        mov dword [edi + VFS_MT_PATH+4], 'c'
        mov byte  [edi + VFS_MT_PATH+5], 0
        mov byte  [edi + VFS_MT_TYPE], VFS_FS_PROC
        mov dword [edi + VFS_MT_OPEN],    vfs_proc_open
        mov dword [edi + VFS_MT_READ],    vfs_proc_read
        mov dword [edi + VFS_MT_WRITE],   vfs_stub_ro
        mov dword [edi + VFS_MT_READDIR], vfs_proc_readdir
        mov dword [edi + VFS_MT_STAT],    vfs_proc_stat
        mov dword [edi + VFS_MT_MKDIR],   vfs_stub_ro
        mov dword [edi + VFS_MT_UNLINK],  vfs_stub_ro
        pop edi
        ret

;---------------------------------------
; vfs_mount_dev — register devfs at "/dev"
;---------------------------------------
vfs_mount_dev:
        push edi
        mov edi, vfs_mount_table + 2 * VFS_MT_SIZE
        mov dword [edi + VFS_MT_PATH],   '/dev'
        mov byte  [edi + VFS_MT_PATH+4], 0
        mov byte  [edi + VFS_MT_TYPE], VFS_FS_DEV
        mov dword [edi + VFS_MT_OPEN],    vfs_dev_open
        mov dword [edi + VFS_MT_READ],    vfs_dev_read
        mov dword [edi + VFS_MT_WRITE],   vfs_dev_write
        mov dword [edi + VFS_MT_READDIR], vfs_dev_readdir
        mov dword [edi + VFS_MT_STAT],    vfs_dev_stat
        mov dword [edi + VFS_MT_MKDIR],   vfs_stub_ro
        mov dword [edi + VFS_MT_UNLINK],  vfs_stub_ro
        pop edi
        ret

;---------------------------------------
; vfs_mount_tmp — register tmpfs at "/tmp"
;---------------------------------------
vfs_mount_tmp:
        push edi
        mov edi, vfs_mount_table + 3 * VFS_MT_SIZE
        mov dword [edi + VFS_MT_PATH],   '/tmp'
        mov byte  [edi + VFS_MT_PATH+4], 0
        mov byte  [edi + VFS_MT_TYPE], VFS_FS_TMP
        mov dword [edi + VFS_MT_OPEN],    vfs_tmp_open
        mov dword [edi + VFS_MT_READ],    vfs_tmp_read
        mov dword [edi + VFS_MT_WRITE],   vfs_tmp_write
        mov dword [edi + VFS_MT_READDIR], vfs_tmp_readdir
        mov dword [edi + VFS_MT_STAT],    vfs_tmp_stat
        mov dword [edi + VFS_MT_MKDIR],   vfs_stub_ro
        mov dword [edi + VFS_MT_UNLINK],  vfs_tmp_unlink
        ; Init tmpfs state
        call tmpfs_init
        pop edi
        ret

;=======================================================================
; vfs_route — find mount entry for a given path
;   In : EBX = path pointer
;   Out: EAX = pointer to mount table entry, or 0 if none
; Checks "/proc", "/dev", "/tmp" before "/" to ensure specific mounts
; are not shadowed by the HBFS catch-all.
;=======================================================================
vfs_route:
        push esi
        push edi
        push ecx

        ; Check mounts 1..3 first (specific prefixes), then mount 0 (/)
        ; Order: proc, dev, tmp, hbfs
        mov ecx, 1          ; start at index 1

.route_loop:
        cmp ecx, VFS_MAX_MOUNTS
        jge .route_hbfs     ; fall through to HBFS

        mov edi, vfs_mount_table
        mov eax, VFS_MT_SIZE
        mul ecx
        add edi, eax        ; edi -> this mount entry

        cmp byte [edi + VFS_MT_TYPE], VFS_FS_NONE
        je .route_next

        ; Compare path prefix
        mov esi, ebx        ; path from caller
        push edi
        add edi, VFS_MT_PATH
        call str_prefix_match   ; EAX=1 if prefix matches
        pop edi
        test eax, eax
        jnz .route_found

.route_next:
        inc ecx
        jmp .route_loop

.route_hbfs:
        ; Default: HBFS mount at index 0
        mov eax, vfs_mount_table    ; mount 0 = HBFS
        jmp .route_ret

.route_found:
        mov eax, edi

.route_ret:
        pop ecx
        pop edi
        pop esi
        ret

; str_prefix_match: EBX=haystack, EDI=needle -> EAX=1 if needle is prefix
str_prefix_match:
        push esi
        push edi
        push ecx
        mov esi, edi        ; needle in ESI
        mov edi, ebx        ; haystack in EDI
.spm_loop:
        lodsb               ; AL = needle byte
        test al, al
        jz .spm_match       ; reached end of needle = prefix matched
        scasb               ; compare with haystack byte
        jne .spm_no
        jmp .spm_loop
.spm_match:
        mov eax, 1
        jmp .spm_done
.spm_no:
        xor eax, eax
.spm_done:
        pop ecx
        pop edi
        pop esi
        ret

;=======================================================================
; VFS FD allocation helpers
;=======================================================================

; vfs_alloc_fd — returns EAX = index of free VFD slot, or -1
vfs_alloc_fd:
        push ecx
        push edi
        xor ecx, ecx
        mov edi, vfs_fd_table
.vaf_loop:
        cmp ecx, VFS_MAX_FDS
        jge .vaf_fail
        cmp dword [edi + VFD_STATE], VFD_FREE
        je .vaf_found
        add edi, VFD_SIZE
        inc ecx
        jmp .vaf_loop
.vaf_found:
        mov eax, ecx
        pop edi
        pop ecx
        ret
.vaf_fail:
        mov eax, -1
        pop edi
        pop ecx
        ret

; vfs_fd_ptr — EBX = vfd index -> EDI = pointer to VFD entry
vfs_fd_ptr:
        mov edi, vfs_fd_table
        mov eax, VFD_SIZE
        imul eax, ebx
        add edi, eax
        ret

;=======================================================================
; HBFS VFS stubs — delegate to existing hbfs_* functions
;=======================================================================

vfs_hbfs_open:
        ; EBX=path, ECX=flags → EAX=vfd (0..VFS_MAX_FDS-1) or -1
        ; Opens file via legacy fd layer, wraps result in a VFD entry.
        push ebx
        push ecx
        push edx
        push esi

        ; fd_open(ESI=filename, ECX=mode) → EAX=legacy_fd or -1
        mov  esi, ebx
        mov  ecx, FD_FLAG_READ
        call fd_open            ; EAX = legacy fd index, or -1
        cmp  eax, -1
        je  .vho_fail_nofd

        mov  edx, eax           ; EDX = legacy fd index

        ; Allocate a VFD entry
        call vfs_alloc_fd       ; EAX = vfd index or -1
        cmp  eax, -1
        je  .vho_fail_novfd

        ; Fill the VFD
        push eax                ; save vfd index
        push edx                ; save legacy fd
        push ebx
        mov  ebx, eax           ; EBX = vfd index for vfs_fd_ptr
        call vfs_fd_ptr         ; EDI → VFD entry  (clobbers EAX)
        pop  ebx
        pop  edx                ; legacy fd
        mov  dword [edi + VFD_STATE],   VFD_OPEN
        mov  dword [edi + VFD_FS_TYPE], VFS_FS_HBFS
        mov  dword [edi + VFD_POS],     0
        mov  [edi + VFD_REAL_FD], edx   ; store legacy fd
        pop  eax                ; vfd index to return

        pop  esi
        pop  edx
        pop  ecx
        pop  ebx
        ret

.vho_fail_novfd:
        ; Close the legacy fd we just opened so it doesn't leak
        push ebx
        mov  ebx, edx
        call fd_close
        pop  ebx
.vho_fail_nofd:
        mov  eax, -1
        pop  esi
        pop  edx
        pop  ecx
        pop  ebx
        ret

vfs_hbfs_read:
        ; EBX=vfd, ECX=buf, EDX=len → EAX=bytes  (same signature as other backends)
        push esi
        push ecx
        push ebx

        ; Resolve VFD → legacy fd
        call vfs_fd_ptr         ; EDI → VFD (EBX=vfd, clobbers EAX)
        cmp  dword [edi + VFD_STATE], VFD_OPEN
        jne  .vhr_fail
        mov  ebx, [edi + VFD_REAL_FD]  ; EBX = legacy fd index

        ; fd_read(EBX=fd, ECX=buf, EDX=len) → EAX=bytes
        mov  esi, ecx           ; buf
        mov  ecx, edx           ; len
        call fd_read            ; EAX = bytes read

        pop  ebx
        pop  ecx
        pop  esi
        ret

.vhr_fail:
        pop  ebx
        pop  ecx
        pop  esi
        mov  eax, -1
        ret

vfs_hbfs_write:
        ; EBX=vfd, ECX=buf, EDX=len → EAX=bytes
        push esi
        push ecx
        push ebx

        call vfs_fd_ptr
        cmp  dword [edi + VFD_STATE], VFD_OPEN
        jne  .vhw_fail
        mov  ebx, [edi + VFD_REAL_FD]

        mov  esi, ecx
        mov  ecx, edx
        call fd_write

        pop  ebx
        pop  ecx
        pop  esi
        ret

.vhw_fail:
        pop  ebx
        pop  ecx
        pop  esi
        mov  eax, -1
        ret

vfs_hbfs_close:
        ; EBX=vfd → EAX=0
        push ebx

        call vfs_fd_ptr                 ; EDI → VFD
        mov  ebx, [edi + VFD_REAL_FD]  ; legacy fd
        call fd_close                   ; close legacy fd

        ; Free the VFD
        pop  ebx                        ; restore vfd index
        call vfs_fd_ptr                 ; EDI → VFD
        mov  dword [edi + VFD_STATE], VFD_FREE

        xor  eax, eax
        ret

vfs_hbfs_readdir:
        ; EBX=path (unused), ECX=name_buf, EDX=entry_index -> EAX=type or -1
        pushad
        call hbfs_load_root_dir

        ; Scan hbfs_dir_buf for the EDX-th non-free entry
        mov esi, hbfs_dir_buf
        xor eax, eax            ; running count of non-free entries found
.vhrd_scan:
        cmp esi, hbfs_dir_buf + HBFS_ROOT_DIR_SIZE
        jge .vhrd_eof
        cmp byte [esi + HBFS_DE_TYPE], FTYPE_FREE
        je .vhrd_skip_free
        cmp eax, edx
        je .vhrd_found
        inc eax
.vhrd_skip_free:
        add esi, HBFS_DIR_ENTRY_SIZE
        jmp .vhrd_scan

.vhrd_found:
        ; Copy null-terminated name from dir entry (at HBFS_DE_NAME=0) to ECX buf
        push esi                ; save entry pointer
        mov edi, ecx            ; dest = caller's name buffer
        mov ecx, HBFS_MAX_FILENAME
.vhrd_copy:
        lodsb
        stosb
        test al, al
        jz .vhrd_copy_done
        loop .vhrd_copy
        mov byte [edi], 0       ; force null-terminate if name hit limit
.vhrd_copy_done:
        pop esi                 ; restore entry pointer
        movzx eax, byte [esi + HBFS_DE_TYPE]
        mov [esp + 28], eax     ; place return type in saved EAX of pushad frame
        popad
        ret

.vhrd_eof:
        popad
        mov eax, -1
        ret

vfs_hbfs_stat:
        ; EBX=path, ECX=stat_buf -> EAX=0/-1
        ; Use hbfs_find_file_global: ESI=name -> CF on fail, EDI=dirent_ptr
        push esi
        mov esi, ebx
        call hbfs_find_file_global
        pop esi
        jc .vhbfs_stat_fail
        ; Fill stat buf: [0]=size, [4]=type, [8]=mtime
        mov eax, [edi + DIRENT_SIZE]
        mov [ecx], eax
        movzx eax, byte [edi + DIRENT_TYPE]
        mov [ecx + 4], eax
        mov eax, [edi + HBFS_DE_MTIME]
        mov [ecx + 8], eax
        xor eax, eax
        ret
.vhbfs_stat_fail:
        mov eax, -1
        ret

vfs_hbfs_mkdir:
        ; EBX=path -> EAX=0/-1
        push esi
        mov esi, ebx
        call hbfs_mkdir
        pop esi
        jc .vhm_fail
        xor eax, eax
        ret
.vhm_fail:
        mov eax, -1
        ret

vfs_hbfs_unlink:
        ; EBX=path -> EAX=0/-1
        push esi
        push edi
        mov esi, ebx
        call hbfs_find_file_global  ; CF on fail, EDI=dir_entry_ptr on success
        jc .vhu_fail
        call hbfs_delete_file_entry ; EDI=dir_entry_ptr
        ; Restore CWD if global search changed directories
        cmp dword [hbfs_find_file_global.gff_moved], 1
        jne .vhu_done
        call gff_restore_cwd
.vhu_done:
        pop edi
        pop esi
        xor eax, eax
        ret
.vhu_fail:
        pop edi
        pop esi
        mov eax, -1
        ret

; Stub for read-only backends (proc, dev)
vfs_stub_ro:
        mov eax, -1
        ret

;=======================================================================
; PROCFS backend (/proc)
;
; Supports:
;   /proc/uptime        — system uptime in ticks
;   /proc/meminfo       — free/total pages
;   /proc/version       — kernel version string
;   /proc/cpuinfo       — CPU info (vendor string)
;   /proc/<pid>/status  — task name, state, pid, ppid, priority
;   /proc/<pid>/cmdline — task name (NUL-terminated)
;=======================================================================

; vfs_proc_open  EBX=path -> EAX=vfd or -1
vfs_proc_open:
        push ecx
        push edx
        push esi

        call vfs_alloc_fd
        cmp eax, -1
        je .proc_open_fail

        push eax
        mov ecx, eax
        ; Get pointer to VFD
        push ebx
        mov ebx, ecx
        call vfs_fd_ptr
        pop ebx

        ; Mark as open, record fs type
        mov dword [edi + VFD_STATE],   VFD_OPEN
        mov dword [edi + VFS_MT_TYPE], VFS_FS_PROC
        mov dword [edi + VFD_POS],     0

        ; Decode sub-type from path
        ; Check for /proc/uptime, /proc/meminfo, /proc/version, /proc/cpuinfo
        lea esi, [proc_name_uptime]
        call str_suffix_match_ebx
        jz .proc_is_uptime

        lea esi, [proc_name_meminfo]
        call str_suffix_match_ebx
        jz .proc_is_meminfo

        lea esi, [proc_name_version]
        call str_suffix_match_ebx
        jz .proc_is_version

        lea esi, [proc_name_cpuinfo]
        call str_suffix_match_ebx
        jz .proc_is_cpuinfo

        ; Default: treat as /proc/<pid>/status
        mov dword [edi + VFD_REAL_FD], PROC_FD_STATUS
        jmp .proc_open_ok

.proc_is_uptime:
        mov dword [edi + VFD_REAL_FD], PROC_FD_UPTIME
        jmp .proc_open_ok
.proc_is_meminfo:
        mov dword [edi + VFD_REAL_FD], PROC_FD_MEMINFO
        jmp .proc_open_ok
.proc_is_version:
        mov dword [edi + VFD_REAL_FD], PROC_FD_VERSION
        jmp .proc_open_ok
.proc_is_cpuinfo:
        mov dword [edi + VFD_REAL_FD], PROC_FD_CPUINFO

.proc_open_ok:
        pop eax
        pop esi
        pop edx
        pop ecx
        ret

.proc_open_fail:
        mov eax, -1
        pop esi
        pop edx
        pop ecx
        ret

; str_suffix_match_ebx: EBX=path, ESI=suffix -> ZF=1 if match
str_suffix_match_ebx:
        push edi
        push ecx
        ; Find end of EBX string
        mov edi, ebx
        xor ecx, ecx
        not ecx
        xor al, al
        repne scasb
        not ecx
        dec ecx                     ; ecx = strlen(ebx)
        ; Find length of suffix
        push edi
        mov edi, esi
        xor ecx, ecx
        not ecx
        repne scasb
        not ecx
        dec ecx                     ; ecx = strlen(suffix)
        pop edi
        ; Compare last len(suffix) bytes of EBX with ESI
        mov edi, ebx
        ; advance edi to end-of-suffix position
        push eax
        mov eax, ebx
        push ecx
        push edi
        mov edi, eax
        xor ecx, ecx
        not ecx
        xor al, al
        repne scasb
        not ecx
        dec ecx                     ; ecx = strlen(ebx) again
        pop edi
        pop ecx                     ; ecx = strlen(suffix) again
        ; edi = ebx; advance by (strlen(ebx) - strlen(suffix))
        push eax
        mov eax, edi
        xor ecx, ecx
        not ecx
        push edi
        xor al, al
        repne scasb
        not ecx
        dec ecx
        pop edi
        pop eax
        ; simple: compare from end
        ; Actually use straightforward suffix comparison
        ; Restore and just do strstr/endswith
        ; Simplified: scan EBX for last '/' then compare remainder with ESI
        pop eax
        pop ecx
        pop edi
        ; Use simple ends-with: find last / in EBX, compare what follows with ESI
        push edi
        push esi
        push ebx
        ; Find last slash
        mov edi, ebx
.ssm_findslash:
        cmp byte [edi], '/'
        je .ssm_slash
        cmp byte [edi], 0
        je .ssm_noslash
        inc edi
        jmp .ssm_findslash
.ssm_slash:
        inc edi
        mov ebx, edi        ; candidate suffix start
        ; check if another slash
        push edi
        mov edi, ebx
        jmp .ssm_findslash  ; re-scan from here
.ssm_noslash:
        ; ebx now points to the last path component
        ; compare with esi
        push esi
        mov edi, ebx
        pop esi
.ssm_cmp:
        lodsb
        test al, al
        jz .ssm_match
        cmp al, [edi]
        jne .ssm_nomatch
        inc edi
        jmp .ssm_cmp
.ssm_match:
        xor al, al          ; ZF=1
        pop ebx
        pop esi
        pop edi
        ret
.ssm_nomatch:
        or al, 1            ; ZF=0
        pop ebx
        pop esi
        pop edi
        ret

; vfs_proc_read  EBX=vfd, ECX=buf, EDX=len -> EAX=bytes
vfs_proc_read:
        push esi
        push edi
        push ecx
        push edx

        ; Get VFD pointer
        call vfs_fd_ptr         ; edi -> VFD
        cmp dword [edi + VFD_STATE], VFD_OPEN
        jne .procr_eof

        mov eax, [edi + VFD_REAL_FD]
        mov esi, ecx            ; destination buffer
        mov ecx, edx            ; max bytes

        cmp eax, PROC_FD_UPTIME
        je .procr_uptime
        cmp eax, PROC_FD_MEMINFO
        je .procr_meminfo
        cmp eax, PROC_FD_VERSION
        je .procr_version
        cmp eax, PROC_FD_CPUINFO
        je .procr_cpuinfo

.procr_status:
        ; Format: "Name:\t<name>\nState:\t<state>\nPid:\t<pid>\n"
        ; (Minimal implementation; expand as needed)
        mov edi, proc_scratch_buf
        push edi
        mov esi, proc_str_name
        call str_copy
        ; Append task name from current TCB (or "<unknown>" if none)
        push edi
        call sched_get_current_task   ; EAX = TCB ptr or 0
        pop edi
        test eax, eax
        jz .procr_status_noname
        lea esi, [eax + TCB_NAME]
        call str_copy
        jmp .procr_status_nl
.procr_status_noname:
        mov esi, proc_str_unknown
        call str_copy
.procr_status_nl:
        mov byte [edi], 0x0A          ; newline
        inc edi
        mov byte [edi], 0
        pop esi
        jmp .procr_copybuf

.procr_uptime:
        mov edi, proc_scratch_buf
        push edi
        mov eax, [sched_tick]   ; PIT ticks since boot
        xor edx, edx
        mov ecx, 100            ; 100 Hz
        div ecx                 ; EAX = seconds
        call itoa_dec_edi
        mov byte [edi], 0x0A    ; newline
        inc edi
        mov byte [edi], 0
        pop esi
        jmp .procr_copybuf

.procr_meminfo:
        mov edi, proc_scratch_buf
        push edi
        mov esi, proc_str_memtotal
        call str_copy
        call pmm_free_pages     ; EAX = free pages
        push eax
        mov eax, [total_free_pages]  ; v10: use init-time page count
        imul eax, 4             ; KB
        call itoa_dec_edi
        mov esi, proc_str_kb_nl
        call str_copy_append
        pop eax
        imul eax, 4
        call itoa_dec_edi
        mov esi, proc_str_kb_nl
        call str_copy_append
        mov byte [edi], 0
        pop esi
        jmp .procr_copybuf

.procr_version:
        mov esi, proc_str_version
        jmp .procr_copystr

.procr_cpuinfo:
        ; Use CPUID vendor string
        mov edi, proc_scratch_buf
        push edi
        xor eax, eax
        cpuid
        mov [edi], ebx
        mov [edi+4], edx
        mov [edi+8], ecx
        mov byte [edi+12], 0x0A
        mov byte [edi+13], 0
        pop esi
        jmp .procr_copybuf

.procr_copystr:
        ; ESI = static string to copy into caller buffer ECX(original),EDX(original)
        pop edx
        pop ecx
        push ecx
        push edx
        mov edi, ecx            ; destination = caller buf
        call str_copy
        ; return length
        sub edi, ecx
        mov eax, edi
        pop edx
        pop ecx
        pop edi
        pop esi
        ret

.procr_copybuf:
        ; ESI = proc_scratch_buf (filled above), copy to original ECX/EDX
        pop edx
        pop ecx
        push ecx
        push edx
        mov edi, ecx            ; caller buf
        ; copy up to EDX bytes or until null
        xor ecx, ecx
.prc_copy:
        cmp ecx, edx
        jge .prc_copy_done
        lodsb
        test al, al
        jz .prc_copy_done
        stosb
        inc ecx
        jmp .prc_copy
.prc_copy_done:
        mov eax, ecx
        pop edx
        pop ecx
        pop edi
        pop esi
        ret

.procr_eof:
        xor eax, eax
        pop edx
        pop ecx
        pop edi
        pop esi
        ret

; vfs_proc_readdir  EBX=path, ECX=buf, EDX=index -> EAX=type
vfs_proc_readdir:
        ; Return proc entries: uptime, meminfo, version, cpuinfo
        cmp edx, 4
        jge .prd_eof
        ; Copy name into ECX
        mov esi, [proc_dirent_names + edx * 4]
        mov edi, ecx
        call str_copy
        mov eax, 1          ; type = file
        ret
.prd_eof:
        mov eax, -1
        ret

; vfs_proc_stat  EBX=path, ECX=stat_buf -> EAX=0
vfs_proc_stat:
        ; Minimal stat: always succeed, size=0, type=file
        xor eax, eax
        ret

;=======================================================================
; DEVFS backend (/dev)
;
; Devices: null (drain), zero (supply zeros), random (LFSR noise),
;          urandom (same as random), tty (console I/O)
;=======================================================================

vfs_dev_open:
        push ecx
        push esi

        call vfs_alloc_fd
        cmp eax, -1
        je .dev_open_fail

        push eax
        mov ecx, eax
        push ebx
        mov ebx, ecx
        call vfs_fd_ptr
        pop ebx

        mov dword [edi + VFD_STATE],   VFD_OPEN
        mov dword [edi + VFD_FS_TYPE], VFS_FS_DEV
        mov dword [edi + VFD_POS],     0

        ; Map path tail to device ID
        mov esi, dev_name_null
        call str_suffix_match_ebx
        jz .dev_is_null

        mov esi, dev_name_zero
        call str_suffix_match_ebx
        jz .dev_is_zero

        mov esi, dev_name_urandom
        call str_suffix_match_ebx
        jz .dev_is_random       ; urandom == random here

        mov esi, dev_name_random
        call str_suffix_match_ebx
        jz .dev_is_random

        mov esi, dev_name_tty
        call str_suffix_match_ebx
        jz .dev_is_tty

        mov esi, dev_name_full
        call str_suffix_match_ebx
        jz .dev_is_full

        ; Unknown device
        mov dword [edi + VFD_REAL_FD], DEV_NULL
        jmp .dev_open_ok

.dev_is_null:   mov dword [edi + VFD_REAL_FD], DEV_NULL   ; jmp .dev_open_ok
                jmp .dev_open_ok
.dev_is_zero:   mov dword [edi + VFD_REAL_FD], DEV_ZERO   ; jmp .dev_open_ok
                jmp .dev_open_ok
.dev_is_random: mov dword [edi + VFD_REAL_FD], DEV_RANDOM
                jmp .dev_open_ok
.dev_is_tty:    mov dword [edi + VFD_REAL_FD], DEV_TTY
                jmp .dev_open_ok
.dev_is_full:   mov dword [edi + VFD_REAL_FD], DEV_FULL

.dev_open_ok:
        pop eax
        pop esi
        pop ecx
        ret
.dev_open_fail:
        mov eax, -1
        pop esi
        pop ecx
        ret

; vfs_dev_read  EBX=vfd, ECX=buf, EDX=len -> EAX=bytes
vfs_dev_read:
        push edi
        push ecx
        push edx
        call vfs_fd_ptr
        cmp dword [edi + VFD_STATE], VFD_OPEN
        jne .devr_fail

        mov eax, [edi + VFD_REAL_FD]
        pop edx
        pop ecx

        cmp eax, DEV_NULL
        je .devr_null

        cmp eax, DEV_ZERO
        je .devr_zero

        cmp eax, DEV_RANDOM
        je .devr_random

        cmp eax, DEV_TTY
        je .devr_tty

        cmp eax, DEV_FULL
        je .devr_full

        xor eax, eax
        pop edi
        ret

.devr_null:
        xor eax, eax
        pop edi
        ret

.devr_zero:
        push ecx
        push edi
        mov edi, ecx
        mov ecx, edx
        xor eax, eax
        rep stosb
        pop edi
        pop ecx
        mov eax, edx
        pop edi
        ret

.devr_random:
        push ecx
        push edi
        mov edi, ecx
        xor ecx, ecx
.devr_rng_loop:
        cmp ecx, edx
        jge .devr_rng_done
        ; Simple 32-bit Xorshift LFSR
        mov eax, [dev_rng_state]
        mov ebx, eax
        shl ebx, 13
        xor eax, ebx
        mov ebx, eax
        shr ebx, 17
        xor eax, ebx
        mov ebx, eax
        shl ebx, 5
        xor eax, ebx
        mov [dev_rng_state], eax
        stosb
        inc ecx
        jmp .devr_rng_loop
.devr_rng_done:
        pop edi
        pop ecx
        mov eax, edx
        pop edi
        ret

.devr_tty:
        ; Read one character from keyboard
        push ecx
        push edx
        mov edi, ecx
        xor ecx, ecx
.devr_tty_loop:
        cmp ecx, edx
        jge .devr_tty_done
        call getchar_blocking        ; EAX = character
        stosb
        cmp al, 0x0A                ; newline = end of line
        je .devr_tty_done
        inc ecx
        jmp .devr_tty_loop
.devr_tty_done:
        mov eax, ecx
        pop edx
        pop ecx
        pop edi
        ret

.devr_full:
        ; /dev/full reads as zeros (like /dev/zero)
        push ecx
        push edi
        mov edi, ecx
        mov ecx, edx
        xor eax, eax
        rep stosb
        pop edi
        pop ecx
        mov eax, edx
        pop edi
        ret

.devr_fail:
        mov eax, -1
        pop edx
        pop ecx
        pop edi
        ret

; vfs_dev_write  EBX=vfd, ECX=buf, EDX=len -> EAX=bytes
vfs_dev_write:
        push edi
        call vfs_fd_ptr
        mov eax, [edi + VFD_REAL_FD]
        pop edi
        cmp eax, DEV_NULL
        je .devw_null
        cmp eax, DEV_TTY
        je .devw_tty
        cmp eax, DEV_FULL
        je .devw_full
        xor eax, eax
        ret
.devw_null:
        mov eax, edx        ; silently accept all bytes
        ret
.devw_tty:
        ; Print ECX..ECX+EDX to console
        push esi
        push ecx
        mov esi, ecx
        xor ecx, ecx
.devw_tty_loop:
        cmp ecx, edx
        jge .devw_tty_done
        lodsb
        call putchar_vga
        inc ecx
        jmp .devw_tty_loop
.devw_tty_done:
        mov eax, edx
        pop ecx
        pop esi
        ret

.devw_full:
        ; /dev/full: always fail (simulate ENOSPC)
        mov eax, -1
        ret

vfs_dev_readdir:
        cmp edx, 6
        jge .drd_eof
        mov esi, [dev_dirent_names + edx * 4]
        mov edi, ecx
        call str_copy
        mov eax, 1
        ret
.drd_eof:
        mov eax, -1
        ret

vfs_dev_stat:
        xor eax, eax
        ret

;=======================================================================
; TMPFS backend (/tmp)
;
; In-memory filesystem backed by kernel heap allocations.
; Files stored in tmpfs_table[]. Max TMPFS_MAX_FILES entries.
;=======================================================================

; tmpfs entry structure (in-memory):
;   [0]    state   (byte: 0=free, 1=used)
;   [4]    size    (dword: bytes written)
;   [8]    buf_ptr (dword: pointer to allocated buffer)
;   [12]   name    (TMPFS_NAME_LEN bytes)
TMPF_STATE  equ 0
TMPF_SIZE   equ 4
TMPF_PTR    equ 8
TMPF_NAME   equ 12
TMPF_ESIZE  equ (12 + TMPFS_NAME_LEN)

tmpfs_init:
        pushad
        mov edi, tmpfs_table
        xor eax, eax
        mov ecx, (TMPFS_MAX_FILES * TMPF_ESIZE) / 4
        rep stosd
        popad
        ret

; vfs_tmp_open  EBX=path, ECX=flags -> EAX=vfd
vfs_tmp_open:
        push esi
        push edx

        ; Extract filename from path (last component after /tmp/)
        mov esi, ebx
        ; skip "/tmp/" prefix
        add esi, 5          ; "/tmp/" = 5 chars

        ; Find or create entry in tmpfs_table
        call tmpfs_find_or_create   ; ESI=name -> EAX=entry_index or -1
        cmp eax, -1
        je .tmpo_fail

        push eax            ; save entry index

        call vfs_alloc_fd
        cmp eax, -1
        je .tmpo_fail2

        push eax
        mov edx, eax
        push ebx
        mov ebx, edx
        call vfs_fd_ptr     ; EDI -> VFD
        pop ebx

        mov dword [edi + VFD_STATE],   VFD_OPEN
        mov dword [edi + VFD_FS_TYPE], VFS_FS_TMP
        mov dword [edi + VFD_POS],     0

        pop eax             ; entry index
        pop edx             ; (discard save)
        push eax
        mov [edi + VFD_REAL_FD], eax    ; entry index in tmpfs_table
        pop eax
        pop edx
        pop esi
        ret

.tmpo_fail2:
        pop eax             ; discard entry index
.tmpo_fail:
        mov eax, -1
        pop edx
        pop esi
        ret

; tmpfs_find_or_create  ESI=name -> EAX=entry_index or -1
tmpfs_find_or_create:
        push ecx
        push edi
        xor ecx, ecx
        mov edi, tmpfs_table

.tfc_loop:
        cmp ecx, TMPFS_MAX_FILES
        jge .tfc_fail

        cmp byte [edi + TMPF_STATE], 0
        je .tfc_create          ; free slot — create here

        ; Check if name matches
        push esi
        push edi
        add edi, TMPF_NAME
        call str_cmp_esi_edi    ; ESI=name, EDI=entry name -> EAX=0 if equal
        pop edi
        pop esi
        test eax, eax
        jz .tfc_found

        add edi, TMPF_ESIZE
        inc ecx
        jmp .tfc_loop

.tfc_create:
        ; Allocate buffer for this file
        push ecx
        push edi
        push esi
        mov ebx, 4096
        call pmm_alloc_page    ; EAX = buffer address or 0
        pop esi
        pop edi
        pop ecx
        test eax, eax
        jz .tfc_fail

        mov byte [edi + TMPF_STATE], 1
        mov dword [edi + TMPF_SIZE], 0
        mov [edi + TMPF_PTR], eax

        ; Copy name (up to TMPFS_NAME_LEN - 1 chars)
        push esi
        push edi
        push ecx
        add edi, TMPF_NAME
        mov ecx, TMPFS_NAME_LEN - 1
.tfc_name_copy:
        lodsb
        stosb
        test al, al
        jz .tfc_name_done
        loop .tfc_name_copy
        mov byte [edi], 0
.tfc_name_done:
        pop ecx
        pop edi
        pop esi

.tfc_found:
        mov eax, ecx
        pop edi
        pop ecx
        ret

.tfc_fail:
        mov eax, -1
        pop edi
        pop ecx
        ret

; str_cmp_esi_edi: ESI=a, EDI=b -> EAX=0 if equal
str_cmp_esi_edi:
        push esi
        push edi
.scse_loop:
        lodsb
        mov ah, [edi]
        inc edi
        cmp al, ah
        jne .scse_ne
        test al, al
        jnz .scse_loop
        xor eax, eax
        pop edi
        pop esi
        ret
.scse_ne:
        mov eax, 1
        pop edi
        pop esi
        ret

; vfs_tmp_read  EBX=vfd, ECX=buf, EDX=len -> EAX=bytes
vfs_tmp_read:
        push esi
        push edi
        push ecx
        push edx
        call vfs_fd_ptr             ; EDI -> VFD
        cmp dword [edi + VFD_STATE], VFD_OPEN
        jne .tmpr_fail

        mov eax, [edi + VFD_REAL_FD]   ; entry index
        ; Get tmpfs entry pointer
        push eax
        mov eax, TMPF_ESIZE
        imul eax, [edi + VFD_REAL_FD]
        mov esi, tmpfs_table
        add esi, eax

        mov eax, [esi + TMPF_SIZE]
        mov ecx, [edi + VFD_POS]        ; current position
        pop eax                         ; (unused; esi is set)

        ; bytes_remaining = size - pos
        sub eax, ecx
        js .tmpr_eof
        cmp eax, edx
        jle .tmpr_copy
        mov eax, edx                    ; clamp to max requested

.tmpr_copy:
        push eax                        ; save return count
        mov edx, eax
        mov esi, [esi + TMPF_PTR]
        add esi, ecx                    ; src = buf + pos
        pop edx
        push edx
        pop ecx                         ; count
        push ecx
        pop edx
        ; edi needs to be caller buf
        pop edx                         ; orig len
        pop ecx                         ; caller buf
        push ecx
        push edx
        mov edi, ecx                    ; dest = caller buf

        ; final copy (eax = bytes to copy already in EAX before we overwrote)
        ; Simpler: just use the stored values
        pop edx
        pop ecx
        push ecx
        push edx

        ; Re-derive via edi (VFD ptr) which is still valid from call above
        ; Note: call vfs_fd_ptr clobbers EDI, ECX
        push ebx
        call vfs_fd_ptr
        pop ebx
        mov esi, [edi + VFD_REAL_FD]
        mov eax, TMPF_ESIZE
        imul eax, esi
        add eax, tmpfs_table
        mov esi, eax                    ; esi -> tmpfs entry
        mov eax, [esi + TMPF_SIZE]
        mov ecx, [edi + VFD_POS]
        sub eax, ecx
        js .tmpr_eof2
        pop edx
        pop ecx
        cmp eax, edx
        jle .tmpr_do_copy
        mov eax, edx
.tmpr_do_copy:
        push eax
        mov edx, eax
        ; src = tmpfs_buf + pos
        push esi
        push edi
        ; get edi back (re-call needed)
        push ebx
        call vfs_fd_ptr                 ; edi -> VFD
        pop ebx
        mov ebx, [edi + VFD_POS]
        pop edi
        pop esi
        mov esi, [esi + TMPF_PTR]
        add esi, ebx                    ; src = buf + current_pos
        mov edi, ecx                    ; dest = caller buf
        mov ecx, edx
        rep movsb
        ; Advance position
        push ebx
        call vfs_fd_ptr
        pop ebx
        add [edi + VFD_POS], edx
        pop eax                         ; return count
        pop edx
        pop ecx
        pop edi
        pop esi
        ret
.tmpr_eof:
.tmpr_eof2:
        xor eax, eax
        pop edx
        pop ecx
        pop edi
        pop esi
        ret
.tmpr_fail:
        mov eax, -1
        pop edx
        pop ecx
        pop edi
        pop esi
        ret

; vfs_tmp_write  EBX=vfd, ECX=buf, EDX=len -> EAX=bytes
vfs_tmp_write:
        push esi
        push edi
        push ecx
        push edx
        call vfs_fd_ptr
        cmp dword [edi + VFD_STATE], VFD_OPEN
        jne .tmpw_fail

        ; Get tmpfs entry
        mov eax, [edi + VFD_REAL_FD]
        imul eax, TMPF_ESIZE
        add eax, tmpfs_table
        mov esi, eax

        ; Check space
        mov eax, [esi + TMPF_SIZE]
        cmp eax, 4096               ; 4 KB per file limit
        jge .tmpw_full

        ; Copy bytes
        mov edi, [esi + TMPF_PTR]
        add edi, [esi + TMPF_SIZE]  ; append position
        pop edx
        pop ecx
        push ecx
        push edx
        mov esi, ecx                ; src = caller buf
        mov ecx, edx
        ; clamp to avoid overflow
        push edi
        call vfs_fd_ptr
        pop edi
        ; re-derive esi
        push ebx
        call vfs_fd_ptr
        pop ebx
        mov eax, [edi + VFD_REAL_FD]
        imul eax, TMPF_ESIZE
        add eax, tmpfs_table
        push eax
        mov edi, [eax + TMPF_PTR]
        add edi, [eax + TMPF_SIZE]
        pop eax
        pop edx
        pop ecx
        push ecx
        push edx
        mov esi, ecx
        mov ecx, edx
        rep movsb

        ; Update size
        push ebx
        call vfs_fd_ptr
        pop ebx
        mov eax, [edi + VFD_REAL_FD]
        imul eax, TMPF_ESIZE
        add eax, tmpfs_table
        pop edx
        pop ecx
        add [eax + TMPF_SIZE], edx
        mov eax, edx
        pop edi
        pop esi
        ret
.tmpw_full:
        xor eax, eax
        pop edx
        pop ecx
        pop edi
        pop esi
        ret
.tmpw_fail:
        mov eax, -1
        pop edx
        pop ecx
        pop edi
        pop esi
        ret

vfs_tmp_readdir:
        ; Return names of all active tmpfs files
        push ecx
        push edi
        xor ecx, ecx
        xor edi, edi        ; found count
        mov esi, tmpfs_table
.trd_loop:
        cmp ecx, TMPFS_MAX_FILES
        jge .trd_eof
        cmp byte [esi + TMPF_STATE], 0
        je .trd_skip
        cmp edi, edx        ; found_count == requested index?
        jne .trd_skip
        ; Copy name to buf
        push esi
        add esi, TMPF_NAME
        mov edi, ecx        ; wait, buf is in ECX param... re-read
        ; This param layout: EBX=path, ECX=buf, EDX=index
        ; We used ECX/EDX above — restore
        pop esi
        push ecx
        pop edi             ; edi = buf
        push esi
        add esi, TMPF_NAME
        call str_copy
        pop esi
        mov eax, 1
        pop edi
        pop ecx
        ret
.trd_skip:
        cmp byte [esi + TMPF_STATE], 1
        je .trd_inc_found
        jmp .trd_next
.trd_inc_found:
        inc edi
.trd_next:
        add esi, TMPF_ESIZE
        inc ecx
        jmp .trd_loop
.trd_eof:
        mov eax, -1
        pop edi
        pop ecx
        ret

vfs_tmp_stat:
        xor eax, eax
        ret

vfs_tmp_unlink:
        ; Find the entry, free its buffer, zero entry
        push esi
        push ecx
        mov esi, ebx
        add esi, 5                  ; skip "/tmp/"
        xor ecx, ecx
        mov edi, tmpfs_table
.tmu_loop:
        cmp ecx, TMPFS_MAX_FILES
        jge .tmu_notfound
        cmp byte [edi + TMPF_STATE], 0
        je .tmu_next
        push edi
        add edi, TMPF_NAME
        push esi
        call str_cmp_esi_edi
        pop esi
        pop edi
        test eax, eax
        jnz .tmu_next
        ; Found — free buffer and zero entry
        push ecx
        push edi
        mov ebx, [edi + TMPF_PTR]
        call pmm_free_page
        pop edi
        pop ecx
        push edi
        mov ecx, TMPF_ESIZE / 4
        xor eax, eax
        rep stosd
        pop edi
        xor eax, eax
        pop ecx
        pop esi
        ret
.tmu_next:
        add edi, TMPF_ESIZE
        inc ecx
        jmp .tmu_loop
.tmu_notfound:
        mov eax, -1
        pop ecx
        pop esi
        ret

;=======================================================================
; Helpers (stubs for functions that may be defined elsewhere)
;=======================================================================

; itoa_dec_edi: EAX = value to write as decimal at [EDI]; advances EDI
itoa_dec_edi:
        push eax
        push ecx
        push edx
        ; Convert EAX to decimal string at EDI
        mov ecx, 10
        push 0              ; sentinel
.iad_div:
        xor edx, edx
        div ecx
        add edx, '0'
        push edx
        test eax, eax
        jnz .iad_div
.iad_write:
        pop eax
        test eax, eax
        jz .iad_done
        stosb
        jmp .iad_write
.iad_done:
        pop edx
        pop ecx
        pop eax
        ret

; str_copy_append: ESI=src -> copy to EDI, advance EDI to new null
str_copy_append:
        ; Back up EDI past current null
        dec edi
.sca_loop:
        lodsb
        stosb
        test al, al
        jnz .sca_loop
        ret

; getchar_blocking: waits for a key, returns ASCII in EAX
getchar_blocking:
        call kb_getchar
        ret

; putchar_vga: AL = char to print to VGA text mode
putchar_vga:
        push eax
        push ebx
        mov ah, al
        mov ebx, 0x0F       ; white on black
        call vga_putchar
        pop ebx
        pop eax
        ret

;=======================================================================
; Static data for VFS
;=======================================================================
msg_vfs_ok:         db "VFS: Mounted hbfs, procfs, devfs, tmpfs", 0x0A, 0

; procfs
proc_name_uptime:   db "uptime", 0
proc_name_meminfo:  db "meminfo", 0
proc_name_version:  db "version", 0
proc_name_cpuinfo:  db "cpuinfo", 0
proc_str_name:      db "Name:", 0x09, 0
proc_str_unknown:   db "<unknown>", 0
proc_str_memtotal:  db "MemTotal:", 0x09, 0
proc_str_memfree:   db "MemFree:", 0x09, 0
proc_str_kb_nl:     db " kB", 0x0A, 0
proc_str_version:   db "Mellivora v10.0 (x86 protected mode)", 0x0A, 0
proc_dirent_names:
        dd proc_name_uptime
        dd proc_name_meminfo
        dd proc_name_version
        dd proc_name_cpuinfo

; devfs
dev_name_null:      db "null", 0
dev_name_zero:      db "zero", 0
dev_name_random:    db "random", 0
dev_name_urandom:   db "urandom", 0
dev_name_tty:       db "tty", 0
dev_name_full:      db "full", 0
dev_dirent_names:
        dd dev_name_null
        dd dev_name_zero
        dd dev_name_random
        dd dev_name_urandom
        dd dev_name_tty
        dd dev_name_full

dev_rng_state:      dd 0xDEADBEEF   ; Xorshift PRNG seed

;=======================================================================
; Per-backend close helpers for proc / dev / tmp
; (HBFS has vfs_hbfs_close above — it also frees the legacy fd.)
; For the other backends the VFD just needs to be freed.
;=======================================================================
vfs_proc_close:
vfs_dev_close:
vfs_tmp_close:
        ; EBX = vfd → free the VFD entry, return EAX=0
        call vfs_fd_ptr                 ; EDI → VFD
        mov  dword [edi + VFD_STATE], VFD_FREE
        xor  eax, eax
        ret

;=======================================================================
; vfs_open — route and open a file
;   In : EBX = path (null-terminated), ECX = open flags
;   Out: EAX = vfd (0..VFS_MAX_FDS-1) or -1 on failure
;=======================================================================
vfs_open:
        push edi
        call vfs_route          ; EAX = mount table entry pointer
        test eax, eax
        jz  .vo_fail
        mov  edi, eax
        call [edi + VFS_MT_OPEN]        ; EBX=path, ECX=flags → EAX=vfd
        pop  edi
        ret
.vo_fail:
        pop  edi
        mov  eax, -1
        ret

;=======================================================================
; vfs_read — dispatch read to the correct backend
;   In : EBX = vfd, ECX = user buffer, EDX = byte count
;   Out: EAX = bytes read, or -1 on error / closed fd
;=======================================================================
vfs_read:
        push edi
        push ebx

        call vfs_fd_ptr                 ; EDI → VFD  (EAX clobbered)
        cmp  dword [edi + VFD_STATE], VFD_OPEN
        jne  .vrd_fail

        mov  eax, [edi + VFD_FS_TYPE]
        pop  ebx                        ; restore vfd index

        cmp  eax, VFS_FS_HBFS
        je  .vrd_hbfs
        cmp  eax, VFS_FS_PROC
        je  .vrd_proc
        cmp  eax, VFS_FS_DEV
        je  .vrd_dev
        cmp  eax, VFS_FS_TMP
        je  .vrd_tmp
        jmp  .vrd_fail2

.vrd_hbfs: call vfs_hbfs_read   ; EBX=vfd, ECX=buf, EDX=len → EAX
           jmp  .vrd_ret
.vrd_proc: call vfs_proc_read   ; same convention
           jmp  .vrd_ret
.vrd_dev:  call vfs_dev_read
           jmp  .vrd_ret
.vrd_tmp:  call vfs_tmp_read
.vrd_ret:
        pop  edi
        ret

.vrd_fail:
        pop  ebx
.vrd_fail2:
        pop  edi
        mov  eax, -1
        ret

;=======================================================================
; vfs_write — dispatch write to the correct backend
;   In : EBX = vfd, ECX = user buffer, EDX = byte count
;   Out: EAX = bytes written, or -1
;=======================================================================
vfs_write:
        push edi
        push ebx

        call vfs_fd_ptr
        cmp  dword [edi + VFD_STATE], VFD_OPEN
        jne  .vwr_fail

        mov  eax, [edi + VFD_FS_TYPE]
        pop  ebx

        cmp  eax, VFS_FS_HBFS
        je  .vwr_hbfs
        cmp  eax, VFS_FS_DEV
        je  .vwr_dev
        cmp  eax, VFS_FS_TMP
        je  .vwr_tmp
        ; proc / unknown: read-only
        jmp  .vwr_fail2

.vwr_hbfs: call vfs_hbfs_write  ; EBX=vfd, ECX=buf, EDX=len → EAX
           jmp  .vwr_ret
.vwr_dev:  call vfs_dev_write
           jmp  .vwr_ret
.vwr_tmp:  call vfs_tmp_write
.vwr_ret:
        pop  edi
        ret

.vwr_fail:
        pop  ebx
.vwr_fail2:
        pop  edi
        mov  eax, -1
        ret

;=======================================================================
; vfs_close — close a virtual fd
;   In : EBX = vfd
;   Out: EAX = 0 on success, -1 on bad fd
;=======================================================================
vfs_close:
        push edi
        push ebx

        call vfs_fd_ptr
        cmp  dword [edi + VFD_STATE], VFD_OPEN
        jne  .vcl_bad

        mov  eax, [edi + VFD_FS_TYPE]
        pop  ebx

        cmp  eax, VFS_FS_HBFS
        je  .vcl_hbfs
        ; proc / dev / tmp: just free the VFD
        call vfs_proc_close             ; same body for all three
        pop  edi
        ret

.vcl_hbfs:
        call vfs_hbfs_close
        pop  edi
        ret

.vcl_bad:
        pop  ebx
        pop  edi
        mov  eax, -1
        ret

;=======================================================================
; vfs_seek — seek within a virtual fd
;   In : EBX = vfd, ECX = offset, EDX = whence (0=SET, 1=CUR, 2=END)
;   Out: EAX = new position, or -1
;=======================================================================
vfs_seek:
        push edi
        push ebx

        call vfs_fd_ptr
        cmp  dword [edi + VFD_STATE], VFD_OPEN
        jne  .vsk_fail

        mov  eax, [edi + VFD_FS_TYPE]
        pop  ebx

        cmp  eax, VFS_FS_HBFS
        jne  .vsk_vfd           ; proc/dev/tmp: update VFD_POS directly

        ; HBFS: delegate to legacy fd_seek (EBX=real_fd, ECX=offset, EDX=whence)
        push ebx
        call vfs_fd_ptr         ; EDI → VFD
        mov  ebx, [edi + VFD_REAL_FD]
        pop  eax                ; discard saved vfd (ebx is now real_fd)
        call fd_seek            ; EAX = new pos
        pop  edi
        ret

.vsk_vfd:
        ; For proc/dev/tmp track position directly in VFD_POS
        ; Re-get VFD pointer (vfs_fd_ptr needs EBX=vfd which is still correct)
        push ebx
        call vfs_fd_ptr
        pop  ebx
        mov  eax, [edi + VFD_POS]

        cmp  edx, 0             ; SEEK_SET
        je  .vsk_set
        cmp  edx, 1             ; SEEK_CUR
        je  .vsk_cur
        ; SEEK_END: position = size (size unknown for these backends, treat as 0)
        xor  eax, eax
        jmp  .vsk_store

.vsk_set:
        mov  eax, ecx
        jmp  .vsk_store
.vsk_cur:
        add  eax, ecx
.vsk_store:
        mov  [edi + VFD_POS], eax
        pop  edi
        ret

.vsk_fail:
        pop  ebx
        pop  edi
        mov  eax, -1
        ret

;=======================================================================
; vfs_readdir — enumerate a directory by virtual path
;   In : EBX = path (used to select mount), ECX = name_buf, EDX = entry_index
;   Out: EAX = entry type, or -1 if no more entries
;=======================================================================
vfs_readdir:
        push edi
        call vfs_route          ; EAX = mount entry for EBX path
        test eax, eax
        jz   .vrdir_fail
        mov  edi, eax           ; mount entry pointer
        call [edi + VFS_MT_READDIR]   ; EBX/ECX/EDX pass through unchanged
        pop  edi
        ret
.vrdir_fail:
        pop  edi
        mov  eax, -1
        ret

;=======================================================================
; vfs_unlink — delete a file/entry by virtual path
;   In : EBX = path
;   Out: EAX = 0 on success, -1 on error
;=======================================================================
vfs_unlink:
        push edi
        call vfs_route
        test eax, eax
        jz   .vunl_fail
        mov  edi, eax
        call [edi + VFS_MT_UNLINK]    ; EBX = path passes through
        pop  edi
        ret
.vunl_fail:
        pop  edi
        mov  eax, -1
        ret

;=======================================================================
; vfs_mkdir — create a directory by virtual path
;   In : EBX = path
;   Out: EAX = 0 on success, -1 on error
;=======================================================================
vfs_mkdir:
        push edi
        call vfs_route
        test eax, eax
        jz   .vmkd_fail
        mov  edi, eax
        call [edi + VFS_MT_MKDIR]     ; EBX = path passes through
        pop  edi
        ret
.vmkd_fail:
        pop  edi
        mov  eax, -1
        ret

