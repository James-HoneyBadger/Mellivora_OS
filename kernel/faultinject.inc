;=======================================================================
; FAULT INJECTION - deterministic failure hooks for reliability testing
;
; Roadmap P10: "OOM/disk-full/network-chaos injection". A program arms one of
; the counters below, performs the operation it wants to see fail, and checks
; that the kernel reported an error instead of panicking, corrupting state or
; leaking. Everything here is a test facility: the counters are global and any
; task may arm them, which is fine for a single-user system and is what makes
; the tests deterministic.
;
; Three failure points, each at the narrowest choke point that every path goes
; through, so one hook covers malloc/fork/fault-in as well as a single write:
;
;   FI_ALLOC  pmm_alloc_buddy_order  - page allocation returns 0 (OOM)
;   FI_IO     ata_write_sectors      - a disk write fails (CF=1)
;   FI_NET    net_send_frame         - an outgoing frame is dropped
;
; Each gate preserves every register and returns its verdict in CF, so the
; hook is a `call`/`jc` and nothing else in the caller changes.
;=======================================================================

FI_OP_CLEAR  equ 0              ; SYS_FAULTINJECT: clear the armed counters
FI_OP_ALLOC  equ 1              ; fail the next ECX page allocations
FI_OP_IO     equ 2              ; fail the next ECX disk writes
FI_OP_NET    equ 3              ; drop the next ECX outgoing frames
FI_OP_STATE  equ 4              ; report the armed counters
FI_OP_TOTAL  equ 5              ; report how many faults were injected

FI_ARM_ALLOC equ 1              ; STATE mask bits
FI_ARM_IO    equ 2
FI_ARM_NET   equ 4

;---------------------------------------
; fi_init - start with every counter clear
;---------------------------------------
fi_init:
        pushad
        mov dword [fi_alloc_left], 0
        mov dword [fi_io_left], 0
        mov dword [fi_net_left], 0
        mov dword [fi_alloc_hits], 0
        mov dword [fi_io_hits], 0
        mov dword [fi_net_hits], 0
        popad
        ret

;---------------------------------------
; fi_alloc_gate - may this page allocation proceed?
;   Out: CF=1 = fail it (one injection consumed), CF=0 = allocate normally
;   Preserves all registers.
;---------------------------------------
fi_alloc_gate:
        push eax
        mov eax, [fi_alloc_left]
        test eax, eax
        jz .fag_pass
        dec eax
        mov [fi_alloc_left], eax
        inc dword [fi_alloc_hits]
        pop eax
        stc
        ret
.fag_pass:
        pop eax
        clc
        ret

;---------------------------------------
; fi_io_gate - may this disk write proceed?
;   Out: CF=1 = fail it (one injection consumed), CF=0 = write normally
;   Preserves all registers.
;---------------------------------------
fi_io_gate:
        push eax
        mov eax, [fi_io_left]
        test eax, eax
        jz .fig_pass
        dec eax
        mov [fi_io_left], eax
        inc dword [fi_io_hits]
        pop eax
        stc
        ret
.fig_pass:
        pop eax
        clc
        ret

;---------------------------------------
; fi_net_gate - may this frame be transmitted?
;   Out: CF=1 = drop it (one injection consumed), CF=0 = send normally
;   Preserves all registers.
;---------------------------------------
fi_net_gate:
        push eax
        mov eax, [fi_net_left]
        test eax, eax
        jz .fng_pass
        dec eax
        mov [fi_net_left], eax
        inc dword [fi_net_hits]
        pop eax
        stc
        ret
.fng_pass:
        pop eax
        clc
        ret

;-----------------------------------------------------------------------
; sys_faultinject (186) - arm, clear and query fault injection
;   In : EBX = operation, ECX = count for the arming operations
;   Out: EAX = 0 on success, -1 for an unknown operation
;        STATE: EAX = armed mask (FI_ARM_*), EBX = allocations left,
;               ECX = writes left, EDX = frames left
;        TOTAL: EAX = allocations failed, EBX = writes failed,
;               ECX = frames dropped
;
; Arming replaces the previous count for that failure point; the others are
; left alone, so a test can arm two faults without ordering them.
;-----------------------------------------------------------------------
sys_faultinject:
        push ebp
        push edi
        push esi
        push edx

        cmp ebx, FI_OP_CLEAR
        je  .sfi_clear
        cmp ebx, FI_OP_ALLOC
        je  .sfi_alloc
        cmp ebx, FI_OP_IO
        je  .sfi_io
        cmp ebx, FI_OP_NET
        je  .sfi_net
        cmp ebx, FI_OP_STATE
        je  .sfi_state
        cmp ebx, FI_OP_TOTAL
        je  .sfi_total
        jmp .sfi_bad

.sfi_clear:
        ; Clear what is still armed. The hit counters are cumulative statistics
        ; (cleared only by fi_init at boot), so a test can clear its budget and
        ; still ask afterwards whether the gate fired.
        mov dword [fi_alloc_left], 0
        mov dword [fi_io_left], 0
        mov dword [fi_net_left], 0
        xor eax, eax
        jmp .sfi_done

.sfi_alloc:
        mov [fi_alloc_left], ecx
        xor eax, eax
        jmp .sfi_done

.sfi_io:
        mov [fi_io_left], ecx
        xor eax, eax
        jmp .sfi_done

.sfi_net:
        mov [fi_net_left], ecx
        xor eax, eax
        jmp .sfi_done

.sfi_state:
        xor eax, eax
        cmp dword [fi_alloc_left], 0
        je  .sfi_state_io
        or  eax, FI_ARM_ALLOC
.sfi_state_io:
        cmp dword [fi_io_left], 0
        je  .sfi_state_net
        or  eax, FI_ARM_IO
.sfi_state_net:
        cmp dword [fi_net_left], 0
        je  .sfi_state_regs
        or  eax, FI_ARM_NET
.sfi_state_regs:
        mov ebx, [fi_alloc_left]
        mov ecx, [fi_io_left]
        mov edx, [fi_net_left]
        jmp .sfi_done

.sfi_total:
        mov eax, [fi_alloc_hits]
        mov ebx, [fi_io_hits]
        mov ecx, [fi_net_hits]
        jmp .sfi_done

.sfi_bad:
        mov eax, -1
.sfi_done:
        pop edx
        pop esi
        pop edi
        pop ebp
        iretd
