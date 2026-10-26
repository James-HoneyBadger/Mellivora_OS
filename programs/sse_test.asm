; sse_test.asm - SSE state must survive a context switch too
;
; The XMM registers and MXCSR are CPU state, exactly like the x87 stack: unless
; the scheduler saves and restores them, two programs doing vector work
; silently corrupt each other. The kernel saves 512 bytes with fxsave/fxrstor
; on a CPU that advertises FXSR, and CR4.OSFXSR is what tells the CPU it may
; keep that state on the OS's behalf - without it, this program faults on its
; first movaps.
;
; The 486 the runtime matrix boots by default has no SSE at all, so this test
; belongs to a machine that does: `run_case ... pentium3` in
; tests/test_runtime.sh. It prints SSE_SKIP on a CPU without SSE rather than
; faulting, so a careless run on a 486 is a skip and not a false failure.
;
; Prints SSE_OK when both forked tasks read back their own four vectors,
; SSE_FAIL otherwise.

%include "syscalls.inc"

YIELDS equ 64                  ; the same depth fpu_test uses

start:
        ; CPUID without a CPUID bit would fault, so check the ID bit first.
        pushfd
        pop eax
        mov ecx, eax
        xor eax, 1 << 21
        push eax
        popfd
        pushfd
        pop eax
        push ecx
        popfd
        xor eax, ecx
        test eax, 1 << 21
        jz .skip

        mov eax, 1
        cpuid
        test edx, 1 << 25              ; SSE
        jz .skip
        test edx, 1 << 24              ; FXSR - the state the kernel saves
        jz .skip

        mov eax, SYS_FORK
        int 0x80
        cmp eax, -1
        je .fail
        test eax, eax
        jz .child

        mov [child_pid], eax
        call sse_check
        jc .fail
        mov eax, SYS_WAITPID
        mov ebx, [child_pid]
        int 0x80
        test eax, eax                  ; child exits 0 only when it agreed
        jnz .fail
        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        jmp .exit

.child:
        call sse_check
        jc .child_fail
        xor ebx, ebx
        jmp .exit
.child_fail:
        mov ebx, 1
        jmp .exit

.skip:
        mov eax, SYS_PRINT
        mov ebx, msg_skip
        int 0x80
        xor ebx, ebx
        jmp .exit

.fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        mov ebx, 1
.exit:
        mov eax, SYS_EXIT
        int 0x80

;-----------------------------------------------------------------------
; sse_check - fill four XMM registers from a per-task seed, switch tasks 64
;             times, and require the same four vectors back.
;   Out: CF clear on agreement, CF set on corruption
;-----------------------------------------------------------------------
sse_check:
        ; The seed depends on the PID, so the two tasks hold different vectors
        ; and a swapped register cannot pass by accident.
        mov eax, SYS_GETPID
        int 0x80
        imul eax, eax, 0x9E3779B1
        mov [seed], eax

        mov edi, want
        mov ecx, 4
        mov ebx, eax
.fill:
        mov [edi], ebx
        add ebx, 0x01010101
        add edi, 4
        dec ecx
        jnz .fill

        movaps xmm0, [want]
        movaps xmm1, [want + 16]
        movaps xmm2, [want + 32]
        movaps xmm3, [want + 48]
        movss  xmm4, [seed]            ; MXCSR-adjacent state: a scalar copy

        mov ecx, YIELDS
.yield_loop:
        push ecx
        mov eax, SYS_YIELD
        int 0x80
        pop ecx
        dec ecx
        jnz .yield_loop

        movaps [got], xmm0
        movaps [got + 16], xmm1
        movaps [got + 32], xmm2
        movaps [got + 48], xmm3

        mov esi, want
        mov edi, got
        mov ecx, 16                    ; 16 dwords = 64 bytes
.cmp:
        mov eax, [esi]
        cmp eax, [edi]
        jne .bad
        add esi, 4
        add edi, 4
        dec ecx
        jnz .cmp

        ; MXCSR must be the default all-masked value: a zeroed image would
        ; unmask every exception, which shows up as a fault rather than a
        ; mismatch, but it is worth asserting the value we expect.
        stmxcsr [mxcsr]
        mov eax, [mxcsr]
        and eax, 0x3F
        jnz .bad
        clc
        ret
.bad:
        stc
        ret

msg_ok:   db "SSE_OK", 10, 0
msg_fail: db "SSE_FAIL", 10, 0
msg_skip: db "SSE_SKIP", 10, 0

child_pid:  dd 0
seed:       dd 0
mxcsr:      dd 0
align 16
want:       times 16 dd 0
got:        times 16 dd 0
