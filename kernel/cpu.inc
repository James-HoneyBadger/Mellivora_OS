;=======================================================================
; cpu.inc - what the processor says about itself
;
; CPUID is not on every 486: the ID bit in EFLAGS only stays set where the
; instruction exists. Everything below runs CPUID only after that check, so an
; old machine reports "no features" instead of faulting inside its own feature
; probe.
;
; The flags recorded here decide how the scheduler saves floating-point and
; vector state (sched.inc): fxsave/fxrstor over the 512-byte image when the CPU
; has FXSR, fnsave/frstor otherwise.
;=======================================================================

%ifndef CPU_INC
%define CPU_INC

CPU_FEAT_FXSR   equ 1 << 24     ; CPUID.01H:EDX - fxsave/fxrstor
CPU_FEAT_SSE    equ 1 << 25     ;                - SSE
CPU_FEAT_SSE2   equ 1 << 26     ;                - SSE2

;---------------------------------------
; cpu_detect - fill cpu_has_fxsr, cpu_has_sse, cpu_has_sse2
;
; Called once from kernel_entry before any task can run vector code. CPUID
; writes EBX, so the whole register set is preserved.
;---------------------------------------
cpu_detect:
        pushad
        mov dword [cpu_has_fxsr], 0
        mov dword [cpu_has_sse], 0
        mov dword [cpu_has_sse2], 0

        ; EFLAGS.ID (bit 21) is writable only where CPUID exists.
        pushfd
        pop eax
        mov ecx, eax
        xor eax, 1 << 21
        push eax
        popfd
        pushfd
        pop eax
        push ecx
        popfd                   ; put the caller's flags back
        xor eax, ecx
        test eax, 1 << 21
        jz .cd_done

        mov eax, 1
        cpuid
        test edx, CPU_FEAT_FXSR
        jz .cd_done
        mov dword [cpu_has_fxsr], 1
        test edx, CPU_FEAT_SSE
        jz .cd_done
        mov dword [cpu_has_sse], 1
        test edx, CPU_FEAT_SSE2
        jz .cd_done
        mov dword [cpu_has_sse2], 1

.cd_done:
        popad
        ret

;---------------------------------------
; cpu_enable_sse - let the CPU keep XMM state on behalf of tasks
;
; CR4.OSFXSR tells the processor the OS saves the extra state with
; fxsave/fxrstor; OSXMMEXCPT routes an SSE fault to #XF rather than #UD. Both
; bits are set only where CPUID advertises FXSR: on a machine without it they
; are reserved, and writing them is a #GP.
;---------------------------------------
cpu_enable_sse:
        cmp dword [cpu_has_fxsr], 0
        je .ces_done
        mov eax, cr4
        or  eax, (1 << 9) | (1 << 10)
        mov cr4, eax
.ces_done:
        ret

%endif ; CPU_INC
