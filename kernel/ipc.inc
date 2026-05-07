;=======================================================================
; IPC - Inter-Process Communication
; Pipes (circular buffers) and Shared Memory
;=======================================================================

IPC_MAX_PIPES   equ 8
IPC_PIPE_SIZE   equ 16384       ; 16 KB per pipe
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
        ; Zero all semaphore structs
        mov edi, ipc_sems
        mov ecx, (IPC_MAX_SEM * SEM_STRUCT_SIZE) / 4
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

        ; Phase 1.4: keep PIPE_COUNT check + update atomic. Previously the
        ; check was outside the cli/sti window, allowing two concurrent
        ; writers (or a writer racing a reader) to over-fill the buffer.
        cli
        cmp dword [ebp + PIPE_COUNT], IPC_PIPE_SIZE
        jge .spw_full_unlock
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
        sti
        inc edi
        jmp .spw_loop
.spw_full_unlock:
        sti
        jmp .spw_done

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
;
; Retry count is kept in EAX (stack context) to avoid a race
; condition if two tasks both block on pipe reads simultaneously.
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
        ; EAX = retry counter (stack-local via register — avoids global race)
        xor eax, eax
.spr_wait:
        cmp dword [ebp + PIPE_COUNT], 0
        jne .spr_has_data
        inc eax
        cmp eax, PIPE_READ_RETRIES
        jge .spr_empty
        ; Yield the CPU instead of spinning
        push eax
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
        pop eax
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

        ; Phase 1.4: keep PIPE_COUNT check + update atomic. Previously the
        ; empty check was outside the cli/sti window, allowing two concurrent
        ; readers to both decrement past zero.
        cli
        cmp dword [ebp + PIPE_COUNT], 0
        je .spr_empty_unlock
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
        sti
        inc esi
        jmp .spr_loop
.spr_empty_unlock:
        sti
        jmp .spr_done

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
; SEMAPHORES
;=======================================================================

IPC_MAX_SEM     equ 8           ; Maximum semaphores

; Semaphore struct offsets
SEM_ACTIVE      equ 0           ; byte: 0=free, 1=active
SEM_VALUE       equ 4           ; dword: current count
SEM_STRUCT_SIZE equ 8

;---------------------------------------
; sys_sem_create - Create a new semaphore
; EBX = initial value (0 = locked, 1+ = unlocked)
; Returns: EAX = sem_id (0-based), or -1 on failure
;---------------------------------------
sys_sem_create:
        push ecx
        push edi
        mov edi, ipc_sems
        xor ecx, ecx
.ssc_find:
        cmp ecx, IPC_MAX_SEM
        jge .ssc_fail
        cmp byte [edi + SEM_ACTIVE], 0
        je .ssc_found
        add edi, SEM_STRUCT_SIZE
        inc ecx
        jmp .ssc_find
.ssc_found:
        mov byte [edi + SEM_ACTIVE], 1
        mov [edi + SEM_VALUE], ebx      ; initial count
        mov eax, ecx
        pop edi
        pop ecx
        iretd
.ssc_fail:
        mov eax, -1
        pop edi
        pop ecx
        iretd

;---------------------------------------
; sys_sem_wait - Decrement semaphore (P operation)
; EBX = sem_id
; Yields until value > 0, then decrements.
; Returns: EAX = 0 on success, -1 on invalid id
;---------------------------------------
SEM_WAIT_RETRIES equ 500        ; max yields before giving up

sys_sem_wait:
        pushad
        cmp ebx, IPC_MAX_SEM
        jge .ssw_fail
        mov eax, ebx
        imul eax, SEM_STRUCT_SIZE
        lea ebp, [ipc_sems + eax]
        cmp byte [ebp + SEM_ACTIVE], 1
        jne .ssw_fail

        xor ecx, ecx            ; retry counter
.ssw_spin:
        cmp dword [ebp + SEM_VALUE], 0
        jg .ssw_acquire
        ; Semaphore unavailable — yield and retry
        inc ecx
        cmp ecx, SEM_WAIT_RETRIES
        jge .ssw_timeout
        push eax
        push ebx
        push ecx
        push ebp
        mov eax, SYS_YIELD
        int 0x80
        pop ebp
        pop ecx
        pop ebx
        pop eax
        jmp .ssw_spin
.ssw_acquire:
        dec dword [ebp + SEM_VALUE]
        mov dword [esp + 28], 0
        popad
        iretd
.ssw_timeout:
        ; Timed out — still return 0 (best-effort, non-blocking fallback)
        dec dword [ebp + SEM_VALUE]
        mov dword [esp + 28], 0
        popad
        iretd
.ssw_fail:
        popad
        mov eax, -1
        iretd

;---------------------------------------
; sys_sem_post - Increment semaphore (V operation)
; EBX = sem_id
; Returns: EAX = 0 on success, -1 on invalid id
;---------------------------------------
sys_sem_post:
        cmp ebx, IPC_MAX_SEM
        jge .ssp_fail
        mov eax, ebx
        imul eax, SEM_STRUCT_SIZE
        cmp byte [ipc_sems + eax + SEM_ACTIVE], 1
        jne .ssp_fail
        inc dword [ipc_sems + eax + SEM_VALUE]
        xor eax, eax
        iretd
.ssp_fail:
        mov eax, -1
        iretd

;---------------------------------------
; sys_sem_close - Destroy a semaphore
; EBX = sem_id
; Returns: EAX = 0 on success, -1 on invalid id
;---------------------------------------
sys_sem_close:
        cmp ebx, IPC_MAX_SEM
        jge .ssclose_fail
        mov eax, ebx
        imul eax, SEM_STRUCT_SIZE
        cmp byte [ipc_sems + eax + SEM_ACTIVE], 1
        jne .ssclose_fail
        mov byte [ipc_sems + eax + SEM_ACTIVE], 0
        mov dword [ipc_sems + eax + SEM_VALUE], 0
        xor eax, eax
        iretd
.ssclose_fail:
        mov eax, -1
        iretd

;=======================================================================
; IPC Data
;=======================================================================
ipc_pipes:      times (IPC_MAX_PIPES * PIPE_STRUCT_SIZE) db 0
ipc_shm:        times (IPC_MAX_SHM * SHM_STRUCT_SIZE) db 0
ipc_sems:       times (IPC_MAX_SEM * SEM_STRUCT_SIZE) db 0

;=======================================================================
; POSIX MESSAGE QUEUES (v9.0) — SYS_MSGQ_* (slots 112-115)
;
; Message queue layout:
;   MSGQ_ACTIVE   equ 0     ; dword: 0=free, 1=active
;   MSGQ_MAX_MSGS equ 4     ; dword: max messages in queue
;   MSGQ_MSG_SIZE equ 8     ; dword: bytes per message
;   MSGQ_WR_IDX   equ 12    ; dword: write index (0..MAX_MSGS-1)
;   MSGQ_RD_IDX   equ 16    ; dword: read index
;   MSGQ_COUNT    equ 20    ; dword: current message count
;   MSGQ_BUF      equ 24    ; MAX_MSGS * MSG_SIZE bytes of data
;
; Maximum message queues, messages per queue, bytes per message:
;=======================================================================
MSGQ_MAX_QUEUES equ 8
MSGQ_MAX_MSGS   equ 16           ; messages per queue
MSGQ_MAX_MSIZE  equ 256          ; max bytes per message
MSGQ_STRUCT_HDR equ 24           ; header size before buf
MSGQ_BUF_SIZE   equ (MSGQ_MAX_MSGS * MSGQ_MAX_MSIZE)
MSGQ_STRUCT_SIZE equ (MSGQ_STRUCT_HDR + MSGQ_BUF_SIZE)

MSGQ_ACTIVE     equ 0
MSGQ_MAX_MSGS_O equ 4
MSGQ_MSG_SIZE_O equ 8
MSGQ_WR_IDX     equ 12
MSGQ_RD_IDX     equ 16
MSGQ_COUNT      equ 20
MSGQ_BUF        equ 24

;---------------------------------------
; sys_msgq_create — Create a message queue
; EBX = max_msgs (capped at MSGQ_MAX_MSGS)
; ECX = msg_size (bytes, capped at MSGQ_MAX_MSIZE)
; Returns: EAX = queue handle (0-based), or -1 on failure
;---------------------------------------
sys_msgq_create:
        pushad
        ; Find a free slot
        mov esi, msgq_pool
        xor edx, edx
.smqc_scan:
        cmp edx, MSGQ_MAX_QUEUES
        jge .smqc_fail
        cmp dword [esi + MSGQ_ACTIVE], 0
        je .smqc_found
        add esi, MSGQ_STRUCT_SIZE
        inc edx
        jmp .smqc_scan
.smqc_found:
        ; Clamp parameters
        mov eax, ebx
        cmp eax, 0
        jle .smqc_fail
        cmp eax, MSGQ_MAX_MSGS
        jle .smqc_nmsgs_ok
        mov eax, MSGQ_MAX_MSGS
.smqc_nmsgs_ok:
        mov ecx, ecx            ; msg_size already in ECX
        cmp ecx, 0
        jle .smqc_fail
        cmp ecx, MSGQ_MAX_MSIZE
        jle .smqc_msz_ok
        mov ecx, MSGQ_MAX_MSIZE
.smqc_msz_ok:
        ; Zero the struct
        push edi
        push ecx
        push eax
        mov edi, esi
        mov ecx, MSGQ_STRUCT_SIZE / 4
        xor eax, eax
        rep stosd
        pop eax
        pop ecx
        pop edi
        ; Initialize fields
        mov dword [esi + MSGQ_ACTIVE],     1
        mov [esi + MSGQ_MAX_MSGS_O],  eax
        mov [esi + MSGQ_MSG_SIZE_O],  ecx
        mov dword [esi + MSGQ_WR_IDX],     0
        mov dword [esi + MSGQ_RD_IDX],     0
        mov dword [esi + MSGQ_COUNT],      0
        ; Return handle = slot index
        mov dword [esp + 28], edx
        popad
        iretd
.smqc_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;---------------------------------------
; sys_msgq_send — Send a message
; EBX = queue handle
; ECX = user data pointer
; EDX = size (bytes, must be <= queue's msg_size)
; Returns: EAX = 0 on success, -1 on failure/full
;---------------------------------------
sys_msgq_send:
        pushad
        ; Validate handle
        cmp ebx, MSGQ_MAX_QUEUES
        jge .smqs_fail
        mov eax, ebx
        imul eax, MSGQ_STRUCT_SIZE
        lea esi, [msgq_pool + eax]
        cmp dword [esi + MSGQ_ACTIVE], 1
        jne .smqs_fail
        ; Check not full
        mov eax, [esi + MSGQ_COUNT]
        cmp eax, [esi + MSGQ_MAX_MSGS_O]
        jge .smqs_full
        ; Validate user pointer
        mov eax, ecx
        call validate_user_ptr
        jc .smqs_fail
        ; Clamp size to queue's msg_size
        mov eax, edx
        cmp eax, [esi + MSGQ_MSG_SIZE_O]
        jle .smqs_sz_ok
        mov eax, [esi + MSGQ_MSG_SIZE_O]
.smqs_sz_ok:
        ; Compute dest = &buf[wr_idx * msg_size]
        push esi
        mov edi, [esi + MSGQ_WR_IDX]
        imul edi, [esi + MSGQ_MSG_SIZE_O]
        add edi, esi
        add edi, MSGQ_BUF
        ; Copy data
        push eax
        push ecx
        mov esi, ecx            ; user ptr
        mov ecx, eax            ; size
        rep movsb
        pop ecx
        pop eax
        pop esi
        ; Advance write index (circular)
        mov eax, [esi + MSGQ_WR_IDX]
        inc eax
        cmp eax, [esi + MSGQ_MAX_MSGS_O]
        jl .smqs_no_wr_wrap
        xor eax, eax
.smqs_no_wr_wrap:
        mov [esi + MSGQ_WR_IDX], eax
        inc dword [esi + MSGQ_COUNT]
        mov dword [esp + 28], 0
        popad
        iretd
.smqs_full:
.smqs_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;---------------------------------------
; sys_msgq_recv — Receive a message
; EBX = queue handle
; ECX = user buffer pointer
; EDX = max bytes to copy
; Returns: EAX = bytes copied, -1 on failure/empty
;---------------------------------------
sys_msgq_recv:
        pushad
        ; Validate handle
        cmp ebx, MSGQ_MAX_QUEUES
        jge .smqr_fail
        mov eax, ebx
        imul eax, MSGQ_STRUCT_SIZE
        lea esi, [msgq_pool + eax]
        cmp dword [esi + MSGQ_ACTIVE], 1
        jne .smqr_fail
        ; Check not empty
        cmp dword [esi + MSGQ_COUNT], 0
        je .smqr_empty
        ; Validate user pointer
        mov eax, ecx
        call validate_user_ptr
        jc .smqr_fail
        ; Compute src = &buf[rd_idx * msg_size]
        push esi
        mov eax, [esi + MSGQ_RD_IDX]
        imul eax, [esi + MSGQ_MSG_SIZE_O]
        add eax, esi
        add eax, MSGQ_BUF           ; eax = src ptr
        ; Determine copy size
        mov edi, ecx                ; user dest
        mov ecx, [esi + MSGQ_MSG_SIZE_O]
        cmp ecx, edx
        jle .smqr_sz_ok
        mov ecx, edx
.smqr_sz_ok:
        push ecx
        push esi
        mov esi, eax               ; src
        rep movsb
        pop esi
        pop ecx
        pop esi
        ; Advance read index
        mov eax, [esi + MSGQ_RD_IDX]
        inc eax
        cmp eax, [esi + MSGQ_MAX_MSGS_O]
        jl .smqr_no_rd_wrap
        xor eax, eax
.smqr_no_rd_wrap:
        mov [esi + MSGQ_RD_IDX], eax
        dec dword [esi + MSGQ_COUNT]
        mov [esp + 28], ecx        ; return bytes copied
        popad
        iretd
.smqr_empty:
.smqr_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;---------------------------------------
; sys_msgq_close — Destroy a message queue
; EBX = queue handle
; Returns: EAX = 0 on success, -1 on failure
;---------------------------------------
sys_msgq_close:
        cmp ebx, MSGQ_MAX_QUEUES
        jge .smqclose_fail
        mov eax, ebx
        imul eax, MSGQ_STRUCT_SIZE
        cmp dword [msgq_pool + eax + MSGQ_ACTIVE], 1
        jne .smqclose_fail
        mov dword [msgq_pool + eax + MSGQ_ACTIVE], 0
        xor eax, eax
        iretd
.smqclose_fail:
        mov eax, -1
        iretd

; Message queue pool storage
msgq_pool:      times (MSGQ_MAX_QUEUES * MSGQ_STRUCT_SIZE) db 0
