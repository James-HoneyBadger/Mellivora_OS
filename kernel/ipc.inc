;=======================================================================
; IPC - Inter-Process Communication
; Pipes (circular buffers) and Shared Memory
;=======================================================================

IPC_MAX_PIPES   equ 16
IPC_PIPE_SIZE   equ 4096        ; 4 KB per pipe
IPC_MAX_SHM     equ 4
IPC_SHM_SIZE    equ 4096        ; 4 KB per shared memory region

; Message queue constants
MQ_MAX_QUEUES   equ 8           ; Maximum message queues
MQ_MAX_MSGS     equ 16          ; Messages per queue
MQ_MSG_SIZE     equ 64          ; Max bytes per message

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

; Message queue struct offsets
MQ_ACTIVE       equ 0           ; byte: 0=free, 1=active
MQ_KEY          equ 4           ; dword: lookup key
MQ_HEAD         equ 8           ; dword: read index (0..MQ_MAX_MSGS-1)
MQ_TAIL         equ 12          ; dword: write index
MQ_COUNT        equ 16          ; dword: messages in queue
MQ_MSGS         equ 20          ; MQ_MAX_MSGS * (4 + MQ_MSG_SIZE) msg array
; Each message: [4 bytes length][MQ_MSG_SIZE bytes data]
MQ_MSG_ENTRY    equ (4 + MQ_MSG_SIZE)              ; 68 bytes per slot
MQ_STRUCT_SIZE  equ (20 + MQ_MAX_MSGS * MQ_MSG_ENTRY) ; 1108 bytes per queue

;---------------------------------------
; ipc_init - Initialize IPC subsystem
;---------------------------------------
ipc_init:
        PUSHALL
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
        ; Zero all message queue structs
        mov edi, ipc_mqueues
        mov ecx, (MQ_MAX_QUEUES * MQ_STRUCT_SIZE) / 4
        xor eax, eax
        rep stosd
        POPALL
        ret

;---------------------------------------
; sys_pipe_create - Create a new pipe
; Returns: EAX = pipe_id (0-based), or -1 on failure
;---------------------------------------
sys_pipe_create:
        push rcx
        push rdi
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
        pop rdi
        pop rcx
        iretq
.spc_fail:
        mov eax, -1
        pop rdi
        pop rcx
        iretq

;---------------------------------------
; sys_pipe_write - Write data to pipe
; EBX = pipe_id, ECX = source buf, EDX = length
; Returns: EAX = bytes written
;
; v3.0: Wakes any tasks waiting on this pipe after writing.
;---------------------------------------
sys_pipe_write:
        PUSHALL
        ; Validate pipe_id
        cmp ebx, IPC_MAX_PIPES
        jge .spw_fail
        mov eax, ebx
        imul rax, PIPE_STRUCT_SIZE
        lea rbp, [ipc_pipes + rax]
        cmp byte [ebp + PIPE_ACTIVE], 1
        jne .spw_fail

        ; Write bytes into circular buffer
        mov rsi, rcx            ; source buffer
        xor edi, edi            ; bytes written counter
.spw_loop:
        cmp edi, edx
        jge .spw_done
        cmp dword [ebp + PIPE_COUNT], IPC_PIPE_SIZE
        jge .spw_done           ; buffer full

        ; Write one byte
        mov eax, [ebp + PIPE_WRITE_POS]
        movzx ecx, byte [rsi + rdi]
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

        mov [rsp + FRAME_RAX], rdi     ; return bytes written in RAX
        POPALL
        iretq
.spw_fail:
        POPALL
        mov eax, -1
        iretq

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
        PUSHALL
        ; Validate pipe_id
        cmp ebx, IPC_MAX_PIPES
        jge .spr_fail
        mov eax, ebx
        imul rax, PIPE_STRUCT_SIZE
        lea rbp, [ipc_pipes + rax]
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
        push rax
        push rbx
        push rcx
        push rdx
        push rbp
        mov eax, SYS_YIELD
        int 0x80
        pop rbp
        pop rdx
        pop rcx
        pop rbx
        pop rax
        jmp .spr_wait

.spr_empty:
        ; No data after retries — return 0
        mov qword [rsp + FRAME_RAX], 0
        POPALL
        iretq

.spr_has_data:
        ; Read bytes from circular buffer
        mov rdi, rcx            ; dest buffer
        xor esi, esi            ; bytes read counter
.spr_loop:
        cmp esi, edx
        jge .spr_done
        cmp dword [ebp + PIPE_COUNT], 0
        je .spr_done            ; buffer empty

        ; Read one byte
        mov eax, [ebp + PIPE_READ_POS]
        movzx ecx, byte [ebp + PIPE_BUF + eax]
        mov [rdi + rsi], cl

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
        mov [rsp + FRAME_RAX], rsi     ; return bytes read in RAX
        POPALL
        iretq
.spr_fail:
        POPALL
        mov eax, -1
        iretq

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
        imul rax, PIPE_STRUCT_SIZE
        mov byte [ipc_pipes + rax + PIPE_ACTIVE], 0
        xor eax, eax
        iretq
.spclose_fail:
        mov eax, -1
        iretq

;---------------------------------------
; sys_shmget - Get or create shared memory region
; EBX = key (nonzero identifier), ECX = size (max IPC_SHM_SIZE)
; Returns: EAX = shm_id (0-based), -1 on failure
;---------------------------------------
sys_shmget:
        push rcx
        push rdi
        push rdx
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
        pop rdx
        pop rdi
        pop rcx
        iretq

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
        push rcx
        push rdi
        lea rdi, [rdi + SHM_BUF]
        shr ecx, 2
        inc ecx
        xor eax, eax
        rep stosd
        pop rdi
        pop rcx
        mov eax, edx
        pop rdx
        pop rdi
        pop rcx
        iretq
.ssg_fail:
        mov eax, -1
        pop rdx
        pop rdi
        pop rcx
        iretq

;---------------------------------------
; sys_shmaddr - Get pointer to shared memory buffer
; EBX = shm_id
; Returns: EAX = pointer to buffer, -1 on failure
;---------------------------------------
sys_shmaddr:
        cmp ebx, IPC_MAX_SHM
        jge .ssa_fail
        mov eax, ebx
        imul rax, SHM_STRUCT_SIZE
        lea rax, [ipc_shm + rax]
        cmp byte [rax + SHM_ACTIVE], 1
        jne .ssa_fail
        add rax, SHM_BUF
        iretq
.ssa_fail:
        mov eax, -1
        iretq

;=======================================================================
; IPC Data
;=======================================================================
ipc_pipes:      times (IPC_MAX_PIPES * PIPE_STRUCT_SIZE) db 0
ipc_shm:        times (IPC_MAX_SHM * SHM_STRUCT_SIZE) db 0
ipc_mqueues:    times (MQ_MAX_QUEUES * MQ_STRUCT_SIZE) db 0

;---------------------------------------
; sys_mq_create - Create/open a message queue by key
; EBX = key (nonzero)
; Returns: EAX = mq_id (0-based), -1 on failure
;---------------------------------------
sys_mq_create:
        PUSHALL
        ; Look for existing queue with this key
        mov edi, ipc_mqueues
        xor edx, edx
.mqc_find:
        cmp edx, MQ_MAX_QUEUES
        jge .mqc_new
        cmp byte [edi + MQ_ACTIVE], 1
        jne .mqc_nextf
        cmp [edi + MQ_KEY], ebx
        je .mqc_found
.mqc_nextf:
        add edi, MQ_STRUCT_SIZE
        inc edx
        jmp .mqc_find
.mqc_found:
        mov [rsp + FRAME_RAX], rdx          ; return mq_id in RAX
        POPALL
        iretq

.mqc_new:
        ; Find free slot
        mov edi, ipc_mqueues
        xor edx, edx
.mqc_alloc:
        cmp edx, MQ_MAX_QUEUES
        jge .mqc_fail
        cmp byte [edi + MQ_ACTIVE], 0
        je .mqc_create
        add edi, MQ_STRUCT_SIZE
        inc edx
        jmp .mqc_alloc
.mqc_create:
        mov byte [edi + MQ_ACTIVE], 1
        mov [edi + MQ_KEY], ebx
        mov dword [edi + MQ_HEAD], 0
        mov dword [edi + MQ_TAIL], 0
        mov dword [edi + MQ_COUNT], 0
        mov [rsp + FRAME_RAX], rdx          ; return mq_id
        POPALL
        iretq
.mqc_fail:
        mov qword [rsp + FRAME_RAX], -1
        POPALL
        iretq

;---------------------------------------
; sys_mq_send - Send a message to a queue
; EBX = mq_id, ECX = data pointer, EDX = length (max MQ_MSG_SIZE)
; Returns: EAX = 0 on success, -1 on failure (queue full or invalid)
;---------------------------------------
sys_mq_send:
        PUSHALL
        ; Validate mq_id
        cmp ebx, MQ_MAX_QUEUES
        jge .mqs_fail
        mov eax, ebx
        imul rax, MQ_STRUCT_SIZE
        lea rdi, [ipc_mqueues + rax]
        cmp byte [rdi + MQ_ACTIVE], 1
        jne .mqs_fail
        ; Check if full
        cmp dword [rdi + MQ_COUNT], MQ_MAX_MSGS
        jge .mqs_fail
        ; Clamp length
        cmp edx, MQ_MSG_SIZE
        jle .mqs_sz_ok
        mov edx, MQ_MSG_SIZE
.mqs_sz_ok:
        ; Calculate write slot: MQ_MSGS + tail * MQ_MSG_ENTRY
        mov eax, [rdi + MQ_TAIL]
        imul rax, MQ_MSG_ENTRY
        lea rbx, [rdi + MQ_MSGS + rax]
        ; Store message length
        mov [rbx], edx
        ; Copy data
        push rdi
        lea rdi, [rbx + 4]
        mov rsi, rcx             ; source = user data pointer
        mov ecx, edx             ; length
        rep movsb
        pop rdi
        ; Advance tail
        mov eax, [rdi + MQ_TAIL]
        inc eax
        cmp eax, MQ_MAX_MSGS
        jb .mqs_no_wrap
        xor eax, eax
.mqs_no_wrap:
        mov [rdi + MQ_TAIL], eax
        inc dword [rdi + MQ_COUNT]
        mov qword [rsp + FRAME_RAX], 0       ; success
        POPALL
        iretq
.mqs_fail:
        mov qword [rsp + FRAME_RAX], -1
        POPALL
        iretq

;---------------------------------------
; sys_mq_recv - Receive a message from a queue
; EBX = mq_id, ECX = dest buffer, EDX = buffer size
; Returns: EAX = bytes read, -1 on failure, 0 if empty
;---------------------------------------
sys_mq_recv:
        PUSHALL
        ; Validate mq_id
        cmp ebx, MQ_MAX_QUEUES
        jge .mqr_fail
        mov eax, ebx
        imul rax, MQ_STRUCT_SIZE
        lea rdi, [ipc_mqueues + rax]
        cmp byte [rdi + MQ_ACTIVE], 1
        jne .mqr_fail
        ; Check if empty
        cmp dword [rdi + MQ_COUNT], 0
        je .mqr_empty
        ; Calculate read slot: MQ_MSGS + head * MQ_MSG_ENTRY
        mov eax, [rdi + MQ_HEAD]
        imul rax, MQ_MSG_ENTRY
        lea rbx, [rdi + MQ_MSGS + rax]
        ; Get message length
        mov esi, [rbx]           ; message length
        ; Clamp to dest buffer size
        cmp esi, edx
        jle .mqr_sz_ok
        mov esi, edx
.mqr_sz_ok:
        ; Copy data to user buffer
        ; ESI = clamped length, RBX = slot ptr, RCX = user dest, RDI = queue ptr
        push rdi                 ; save queue ptr
        push rsi                 ; save length for return
        lea rax, [rbx + 4]      ; RAX = source data address
        mov rsi, rax             ; RSI = source
        mov rdi, rcx             ; RDI = user dest buffer
        pop rcx                  ; ECX = byte count
        push rcx                 ; save again for return value
        rep movsb
        pop rcx                  ; ECX = bytes copied
        pop rdi                  ; restore queue ptr
        ; Advance head
        mov eax, [rdi + MQ_HEAD]
        inc eax
        cmp eax, MQ_MAX_MSGS
        jb .mqr_no_wrap
        xor eax, eax
.mqr_no_wrap:
        mov [rdi + MQ_HEAD], eax
        dec dword [rdi + MQ_COUNT]
        mov [rsp + FRAME_RAX], rcx          ; return bytes read
        POPALL
        iretq
.mqr_empty:
        mov qword [rsp + FRAME_RAX], 0
        POPALL
        iretq
.mqr_fail:
        mov qword [rsp + FRAME_RAX], -1
        POPALL
        iretq

;---------------------------------------
; sys_mq_close - Destroy a message queue
; EBX = mq_id
; Returns: EAX = 0 on success, -1 on failure
;---------------------------------------
sys_mq_close:
        cmp ebx, MQ_MAX_QUEUES
        jge .mqcl_fail
        mov eax, ebx
        imul rax, MQ_STRUCT_SIZE
        mov byte [ipc_mqueues + rax + MQ_ACTIVE], 0
        xor eax, eax
        iretq
.mqcl_fail:
        mov eax, -1
        iretq
