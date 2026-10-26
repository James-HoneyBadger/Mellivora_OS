;=======================================================================
; cpu.inc - what the processor says about itself
;
; Mellivora targets a Pentium 4: the oldest part with the three things this
; kernel is built on - FXSAVE/FXRSTOR (CPUID.01H:EDX bit 24), SSE (bit 25) and
; SSE2 (bit 26). Every task's floating-point image is a 512-byte fxsave, and
; the kernel's C object code is compiled for the Pentium 4, so a processor
; without SSE2 cannot run this kernel at all. cpu_require_pentium4 says so at
; boot instead of letting an SSE2 instruction die inside the first context
; switch, where nothing points back at the reason.
;
; cpu_detect uses CPUID only where CPUID exists (the EFLAGS.ID probe below), so
; an unsupported machine reports what it is rather than faulting while it looks
; for its own feature bits.
;
; The flags recorded here drive the scheduler's save format (sched.inc), the
; CPU line in `sysinfo`, and the boot line a test matches on.
;=======================================================================

%ifndef CPU_INC
%define CPU_INC

CPU_FEAT_FPU    equ 1 <<  0     ; CPUID.01H:EDX - x87 on chip
CPU_FEAT_TSC    equ 1 <<  4     ;                - rdtsc
CPU_FEAT_PAE    equ 1 <<  6     ;                - physical address extension
CPU_FEAT_CMOV   equ 1 << 15     ;                - cmovcc
CPU_FEAT_MMX    equ 1 << 23     ;                - MMX
CPU_FEAT_FXSR   equ 1 << 24     ;                - fxsave/fxrstor
CPU_FEAT_SSE    equ 1 << 25     ;                - SSE
CPU_FEAT_SSE2   equ 1 << 26     ;                - SSE2

; Family numbers as CPUID reports them once the extended rules are applied.
CPU_FAMILY_P6   equ 6           ; Pentium Pro .. Pentium III / Core
CPU_FAMILY_P4   equ 15          ; Netburst: Pentium 4 and Xeon

CPU_MIN_FAMILY  equ CPU_FAMILY_P4

;---------------------------------------
; cpu_detect - fill the cpu_* records from CPUID
;
; Leaves every field zero on a processor without CPUID. Called once from
; kernel_entry before any task can run vector code. CPUID writes EBX, so the
; whole register set is preserved.
;---------------------------------------
cpu_detect:
        pushad
        call cpu_clear_fields

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

        ;--- leaf 0: highest basic leaf, and the vendor string ---
        xor eax, eax
        cpuid
        mov [cpu_max_leaf], eax
        mov [cpu_vendor + 0], ebx
        mov [cpu_vendor + 4], edx
        mov [cpu_vendor + 8], ecx

        ;--- leaf 1: family/model/stepping, then the feature bits ---
        mov eax, 1
        cpuid

        ; Family 15 is spelled "base family 6, extended family 9"; a Netburst
        ; model number likewise needs the extended nibble shifted up by four.
        mov esi, eax
        shr esi, 8
        and esi, 0xF                    ; base family
        mov edi, eax
        shr edi, 16
        and edi, 0xF                    ; extended family
        cmp esi, 0xF
        jne .cd_family_ok
        add esi, edi
.cd_family_ok:
        mov [cpu_family], esi

        mov esi, eax
        shr esi, 4
        and esi, 0xF                    ; base model
        mov edi, eax
        shr edi, 16
        and edi, 0xF                    ; extended model
        shl edi, 4
        mov ecx, eax
        shr ecx, 8
        and ecx, 0xF                    ; base family, for the rule below
        cmp ecx, CPU_FAMILY_P6          ; extended model exists from the P6 on
        jb .cd_model_ok
        or esi, edi
.cd_model_ok:
        mov [cpu_model], esi

        mov esi, eax
        and esi, 0xF
        mov [cpu_stepping], esi

        test edx, CPU_FEAT_FPU
        jz .cd_done                     ; no FPU: nothing below means anything
        test edx, CPU_FEAT_MMX
        jz .cd_no_mmx
        mov dword [cpu_has_mmx], 1
.cd_no_mmx:
        test edx, CPU_FEAT_CMOV
        jz .cd_no_cmov
        mov dword [cpu_has_cmov], 1
.cd_no_cmov:
        test edx, CPU_FEAT_TSC
        jz .cd_no_tsc
        mov dword [cpu_has_tsc], 1
.cd_no_tsc:
        test edx, CPU_FEAT_PAE
        jz .cd_no_pae
        mov dword [cpu_has_pae], 1
.cd_no_pae:
        test edx, CPU_FEAT_FXSR
        jz .cd_done
        mov dword [cpu_has_fxsr], 1
        test edx, CPU_FEAT_SSE
        jz .cd_done
        mov dword [cpu_has_sse], 1
        test edx, CPU_FEAT_SSE2
        jz .cd_done
        mov dword [cpu_has_sse2], 1

        call cpu_read_brand

.cd_done:
        popad
        ret

;---------------------------------------
; cpu_clear_fields - zero the whole record
;---------------------------------------
cpu_clear_fields:
        mov dword [cpu_max_leaf], 0
        mov dword [cpu_family], 0
        mov dword [cpu_model], 0
        mov dword [cpu_stepping], 0
        mov dword [cpu_has_fxsr], 0
        mov dword [cpu_has_sse], 0
        mov dword [cpu_has_sse2], 0
        mov dword [cpu_has_mmx], 0
        mov dword [cpu_has_cmov], 0
        mov dword [cpu_has_tsc], 0
        mov dword [cpu_has_pae], 0
        mov dword [cpu_brand_len], 0
        mov byte [cpu_brand], 0
        mov byte [cpu_vendor], 0
        ret

;---------------------------------------
; cpu_read_brand - copy the 48-byte brand string from leaf 80000004H
;
; The brand string is what the part calls itself ("Intel(R) Pentium(R) 4 CPU
; 2.40GHz"). It exists on a Pentium III and later; where it is missing the
; record stays an empty string and the reporter prints the family instead.
;---------------------------------------
cpu_read_brand:
        pushad
        mov eax, 0x80000000
        cpuid
        cmp eax, 0x80000004
        jb .crb_done

        mov edi, cpu_brand
        mov eax, 0x80000002
        cpuid
        mov [edi + 0], eax
        mov [edi + 4], ebx
        mov [edi + 8], ecx
        mov [edi + 12], edx
        mov eax, 0x80000003
        cpuid
        mov [edi + 16], eax
        mov [edi + 20], ebx
        mov [edi + 24], ecx
        mov [edi + 28], edx
        mov eax, 0x80000004
        cpuid
        mov [edi + 32], eax
        mov [edi + 36], ebx
        mov [edi + 40], ecx
        mov [edi + 44], edx

        ; Real parts pad the 48 bytes with spaces; QEMU hands back the
        ; left-aligned string a spoofed model-id carries. Trim both ends and
        ; terminate, so one clean line comes out either way.
        mov ecx, 48
        xor esi, esi                    ; ESI = first non-space
.crb_lead:
        cmp esi, ecx
        jae .crb_blank
        cmp byte [edi + esi], ' '
        jne .crb_trim
        inc esi
        jmp .crb_lead
.crb_blank:
        mov byte [cpu_brand], 0
        jmp .crb_done
.crb_trim:
        mov edx, ecx                    ; EDX = end, exclusive
.crb_trail:
        cmp edx, esi
        jbe .crb_copy
        cmp byte [edi + edx - 1], ' '
        jne .crb_copy
        dec edx
        jmp .crb_trail
.crb_copy:
        sub edx, esi                    ; EDX = trimmed length
        mov ecx, edx
        mov byte [cpu_brand + ecx], 0
        test ecx, ecx
        jz .crb_done
        mov [cpu_brand_len], ecx
        add edi, esi
        mov esi, cpu_brand
.crb_move:
        mov al, [edi]
        mov [esi], al
        inc edi
        inc esi
        dec ecx
        jnz .crb_move
.crb_done:
        popad
        ret

;---------------------------------------
; cpu_require_pentium4 - refuse to run below the kernel's floor
;
; Prints one line naming the part it found and stops the machine. There is no
; degraded mode to fall back to: the scheduler saves SSE state on every switch
; and the C object code assumes cmov/sse2, so carrying on would corrupt task
; state a few instructions later instead of failing where it can be explained.
;
; Runs after serial_init, before any subsystem that saves task state.
;---------------------------------------
cpu_require_pentium4:
        pushad
        cmp dword [cpu_has_fxsr], 0
        je .crp_unsupported
        cmp dword [cpu_has_sse], 0
        je .crp_unsupported
        cmp dword [cpu_has_sse2], 0
        je .crp_unsupported
        cmp dword [cpu_family], CPU_MIN_FAMILY
        jb .crp_unsupported
        popad
        ret

.crp_unsupported:
        mov esi, cpu_unsupported_msg
        call serial_print
        mov eax, [cpu_family]
        call serial_print_dec
        mov esi, cpu_unsupported_dot
        call serial_print
        mov eax, [cpu_model]
        call serial_print_dec
        mov esi, cpu_unsupported_tail
        call serial_print
.crp_halt:
        cli
        hlt
        jmp .crp_halt

;---------------------------------------
; cpu_report_boot - one serial line describing the processor
;
; "CPU_P4 <brand> family=.. model=.. step=..". The CPU_P4 marker is the
; contract with the test suite: it is printed only after cpu_require_pentium4
; accepted the part, so seeing it means the floor was really met.
;---------------------------------------
cpu_report_boot:
        pushad
        mov  esi, cpu_boot_msg
        call serial_print
        cmp  dword [cpu_brand_len], 0
        je   .crb_no_brand
        mov  esi, cpu_brand
        call serial_print
        jmp  .crb_family
.crb_no_brand:
        mov  esi, cpu_unknown_brand
        call serial_print
.crb_family:
        mov  esi, cpu_boot_family
        call serial_print
        mov  eax, [cpu_family]
        call serial_print_dec
        mov  esi, cpu_boot_model
        call serial_print
        mov  eax, [cpu_model]
        call serial_print_dec
        mov  esi, cpu_boot_step
        call serial_print
        mov  eax, [cpu_stepping]
        call serial_print_dec
        mov  esi, cpu_boot_fxsr
        call serial_print
        mov  eax, [cpu_has_fxsr]
        call serial_print_dec
        mov  esi, cpu_boot_sse
        call serial_print
        mov  eax, [cpu_has_sse]
        call serial_print_dec
        mov  esi, cpu_boot_sse2
        call serial_print
        mov  eax, [cpu_has_sse2]
        call serial_print_dec
        mov  esi, cpu_boot_newline
        call serial_print
        popad
        ret

;---------------------------------------
; cpu_enable_sse - let the CPU keep XMM state on behalf of tasks
;
; CR4.OSFXSR tells the processor the OS saves the extra state with
; fxsave/fxrstor; OSXMMEXCPT routes an SSE fault to #XF rather than #UD. Both
; are unconditional: the kernel does not run on a part that lacks FXSR, because
; cpu_require_pentium4 has already stopped on one. They are also set before
; paging, which is where CR4 gets rewritten (PAE stays off), so this cannot be
; moved later without losing them.
;---------------------------------------
cpu_enable_sse:
        mov eax, cr4
        or  eax, (1 << 9) | (1 << 10)
        mov cr4, eax
        ret

; Strings live in the code section: they are read-only and are printed before
; the data section is meaningful on the failure path.
cpu_unsupported_msg:  db "CPU_UNSUPPORTED: Mellivora needs a Pentium 4 or later (fxsr+sse+sse2); found family ", 0
cpu_unsupported_dot:  db " model ", 0
cpu_unsupported_tail: db " - halting", 0x0A, 0
cpu_boot_msg:         db "CPU_P4 ", 0
cpu_unknown_brand:    db "(no brand string)", 0
cpu_boot_family:      db " family=", 0
cpu_boot_model:       db " model=", 0
cpu_boot_step:        db " step=", 0
cpu_boot_fxsr:        db " fxsr=", 0
cpu_boot_sse:         db " sse=", 0
cpu_boot_sse2:        db " sse2=", 0
cpu_boot_newline:     db 0x0A, 0

%endif ; CPU_INC
