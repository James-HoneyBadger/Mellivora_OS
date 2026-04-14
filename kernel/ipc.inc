;=======================================================================
; IPC - Inter-Process Communication
; Pipes (circular buffers) and Shared Memory
;=======================================================================

IPC_MAX_PIPES   equ 8
IPC_PIPE_SIZE   equ 4096        ; 4 KB per pipe
IPC_MAX_SHM     equ 4
IPC_SHM_SIZE    equ 4096        ; 4 KB per shared memory region

; Pipe struct offsets
PIPE_ACTIVE     equ 0           ; byte: 0=free, 1=active
PIPE_READ_POS   equ 4           ; dword: read cursor
PIPE_WRITE_POS  equ 8           ; dword: write cursor
PIPE_COUNT      equ 12          ; dword: bytes in buffer
PIPE_BUF        equ 16          ; IPC_PIPE_SIZE bytes of data
PIPE_STRUCT_SIZE equ (16 + IPC_PIPE_SIZE)

; Shared memory struct offsets
SHM_ACTIVE      equ 0           ; byte: 0=free, 1=active
SHM_KEY         equ 4           ; dword: lookup key
SHM_SIZE        equ 8           ; dword: allocated size
SHM_BUF         equ 12          ; IPC_SHM_SIZE bytes
SHM_STRUCT_SIZE equ (12 + IPC_SHM_SIZE)

;---------------------------------------
; ipc_init - Initialize IPC subsystem
;---------------------------------------
ipc_init:
        pushad
        ; Zero all pipe structs
        mov edi, ipc_pipes
        mov ecx, (IPC_MAX_PIPES * PIPE_STRUCT_SIZE) / 4
        xor eax, eax
        rep stosd
        ; Zero all shm structs
        mov edi, ipc_shm
        mov ecx, (IPC_MAX_SHM * SHM_STRUCT_SIZE) / 4
        xor eax, eax
        rep stosd
        popad
        ret

;---------------------------------------
; sys_pipe_create - Create a new pipe
; Returns: EAX = pipe_id (0-based), or -1 on failure
;---------------------------------------
sys_pipe_create:
        push ecx
        push edi
        mov edi, ipc_pipes
        xor ecx, ecx
.spc_find:
        cmp ecx, IPC_MAX_PIPES
        jge .spc_fail
        cmp byte [edi + PIPE_ACTIVE], 0
        je .spc_found
        add edi, PIPE_STRUCT_SIZE
        inc ecx
        jmp .spc_find
.spc_found:
        mov byte [edi + PIPE_ACTIVE], 1
        mov dword [edi + PIPE_READ_POS], 0
        mov dword [edi + PIPE_WRITE_POS], 0
        mov dword [edi + PIPE_COUNT], 0
        mov eax, ecx
        pop edi
        pop ecx
        iretd
.spc_fail:
        mov eax, -1
        pop edi
        pop ecx
        iretd

;---------------------------------------
; sys_pipe_write - Write data to pipe
; EBX = pipe_id, ECX = source buf, EDX = length
; Returns: EAX = bytes written
;
; v3.0: Wakes any tasks waiting on this pipe after writing.
;---------------------------------------
sys_pipe_write:
        pushad
        ; Validate pipe_id
        cmp ebx, IPC_MAX_PIPES
        jge .spw_fail
        mov eax, ebx
        imul eax, PIPE_STRUCT_SIZE
        lea ebp, [ipc_pipes + eax]
        cmp byte [ebp + PIPE_ACTIVE], 1
        jne .spw_fail

        ; Write bytes into circular buffer
        mov esi, ecx            ; source buffer
        xor edi, edi            ; bytes written counter
.spw_loop:
        cmp edi, edx
        jge .spw_done
        cmp dword [ebp + PIPE_COUNT], IPC_PIPE_SIZE
        jge .spw_done           ; buffer full

        ; Write one byte
        mov eax, [ebp + PIPE_WRITE_POS]
        movzx ecx, byte [esi + edi]
        mov [ebp + PIPE_BUF + eax], cl

        ; Advance write pos (circular)
        inc eax
        cmp eax, IPC_PIPE_SIZE
        jl .spw_no_wrap
        xor eax, eax
.spw_no_wrap:
        mov [ebp + PIPE_WRITE_POS], eax
        inc dword [ebp + PIPE_COUNT]
        inc edi
        jmp .spw_loop

.spw_done:
        ; Wake any task waiting on this pipe (v3.0)
        mov eax, ebx            ; pipe_id already in EBX from entry
        call pipe_wake_waiter

        mov [esp + 28], edi     ; return bytes written in EAX
        popad
        iretd
.spw_fail:
        popad
        mov eax, -1
        iretd

;---------------------------------------
; sys_pipe_read - Read data from pipe
; EBX = pipe_id, ECX = dest buf, EDX = max length
; Returns: EAX = bytes read
;
; v3.0: If pipe is empty, yields CPU up to PIPE_READ_RETRIES
;       times before returning 0 (non-blocking fallback).
;       This replaces the old immediate-return-0 that forced
;       callers into 100% CPU polling loops.
;---------------------------------------
PIPE_READ_RETRIES equ 200       ; ~200 yields before giving up

sys_pipe_read:
        pushad
        ; Validate pipe_id
        cmp ebx, IPC_MAX_PIPES
        jge .spr_fail
        mov eax, ebx
        imul eax, PIPE_STRUCT_SIZE
        lea ebp, [ipc_pipes + eax]
        cmp byte [ebp + PIPE_ACTIVE], 1
        jne .spr_fail

        ; If buffer empty, yield and retry up to PIPE_READ_RETRIES times
        mov dword [pipe_retry_count], 0
.spr_wait:
        cmp dword [ebp + PIPE_COUNT], 0
        jne .spr_has_data
        inc dword [pipe_retry_count]
        cmp dword [pipe_retry_count], PIPE_READ_RETRIES
        jge .spr_empty
        ; Yield the CPU instead of spinning
        push ebx
        push ecx
        push edx
        push ebp
        mov eax, SYS_YIELD
        int 0x80
        pop ebp
        pop edx
        pop ecx
        pop ebx
        jmp .spr_wait

.spr_empty:
        ; No data after retries — return 0
        mov dword [esp + 28], 0
        popad
        iretd

.spr_has_data:
        ; Read bytes from circular buffer
        mov edi, ecx            ; dest buffer
        xor esi, esi            ; bytes read counter
.spr_loop:
        cmp esi, edx
        jge .spr_done
        cmp dword [ebp + PIPE_COUNT], 0
        je .spr_done            ; buffer empty

        ; Read one byte
        mov eax, [ebp + PIPE_READ_POS]
        movzx ecx, byte [ebp + PIPE_BUF + eax]
        mov [edi + esi], cl

        ; Advance read pos (circular)
        inc eax
        cmp eax, IPC_PIPE_SIZE
        jl .spr_no_wrap
        xor eax, eax
.spr_no_wrap:
        mov [ebp + PIPE_READ_POS], eax
        dec dword [ebp + PIPE_COUNT]
        inc esi
        jmp .spr_loop

.spr_done:
        mov [esp + 28], esi     ; return bytes read in EAX
        popad
        iretd
.spr_fail:
        popad
        mov eax, -1
        iretd

;---------------------------------------
; pipe_wake_waiter - Stub for future
; wait-queue integration. Currently a NOP
; since yield-based retry handles wakeup.
; EAX = pipe_id (preserved)
;---------------------------------------
pipe_wake_waiter:
        ret

;---------------------------------------
; sys_pipe_close - Close/free a pipe
; EBX = pipe_id
; Returns: EAX = 0
;---------------------------------------
sys_pipe_close:
        cmp ebx, IPC_MAX_PIPES
        jge .spclose_fail
        mov eax, ebx
        imul eax, PIPE_STRUCT_SIZE
        mov byte [ipc_pipes + eax + PIPE_ACTIVE], 0
        xor eax, eax
        iretd
.spclose_fail:
        mov eax, -1
        iretd

;---------------------------------------
; sys_shmget - Get or create shared memory region
; EBX = key (nonzero identifier), ECX = size (max IPC_SHM_SIZE)
; Returns: EAX = shm_id (0-based), -1 on failure
;---------------------------------------
sys_shmget:
        push ecx
        push edi
        push edx
        ; First, check if key already exists
        mov edi, ipc_shm
        xor edx, edx
.ssg_find:
        cmp edx, IPC_MAX_SHM
        jge .ssg_new
        cmp byte [edi + SHM_ACTIVE], 1
        jne .ssg_nextf
        cmp [edi + SHM_KEY], ebx
        je .ssg_found
.ssg_nextf:
        add edi, SHM_STRUCT_SIZE
        inc edx
        jmp .ssg_find
.ssg_found:
        mov eax, edx
        pop edx
        pop edi
        pop ecx
        iretd

.ssg_new:
        ; Find free slot
        mov edi, ipc_shm
        xor edx, edx
.ssg_alloc:
        cmp edx, IPC_MAX_SHM
        jge .ssg_fail
        cmp byte [edi + SHM_ACTIVE], 0
        je .ssg_create
        add edi, SHM_STRUCT_SIZE
        inc edx
        jmp .ssg_alloc
.ssg_create:
        mov byte [edi + SHM_ACTIVE], 1
        mov [edi + SHM_KEY], ebx
        ; Clamp size
        cmp ecx, IPC_SHM_SIZE
        jle .ssg_sz_ok
        mov ecx, IPC_SHM_SIZE
.ssg_sz_ok:
        mov [edi + SHM_SIZE], ecx
        ; Zero the buffer
        push ecx
        push edi
        lea edi, [edi + SHM_BUF]
        shr ecx, 2
        inc ecx
        xor eax, eax
        rep stosd
        pop edi
        pop ecx
        mov eax, edx
        pop edx
        pop edi
        pop ecx
        iretd
.ssg_fail:
        mov eax, -1
        pop edx
        pop edi
        pop ecx
        iretd

;---------------------------------------
; sys_shmaddr - Get pointer to shared memory buffer
; EBX = shm_id
; Returns: EAX = pointer to buffer, -1 on failure
;---------------------------------------
sys_shmaddr:
        cmp ebx, IPC_MAX_SHM
        jge .ssa_fail
        mov eax, ebx
        imul eax, SHM_STRUCT_SIZE
        lea eax, [ipc_shm + eax]
        cmp byte [eax + SHM_ACTIVE], 1
        jne .ssa_fail
        add eax, SHM_BUF
        iretd
.ssa_fail:
        mov eax, -1
        iretd

;=======================================================================
; IPC Data
;=======================================================================
ipc_pipes:      times (IPC_MAX_PIPES * PIPE_STRUCT_SIZE) db 0
ipc_shm:        times (IPC_MAX_SHM * SHM_STRUCT_SIZE) db 0
pipe_retry_count: dd 0
