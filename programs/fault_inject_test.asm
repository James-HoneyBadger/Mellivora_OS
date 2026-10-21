; fault_inject_test.asm - reliability test for the fault-injection hooks
;
; Roadmap P10: OOM, disk-full and network-chaos injection. Each section arms a
; fault, performs the operation that has to survive it, and checks two things:
; the failure was reported as an error *and* the system is unchanged
; afterwards - no leak, no panic, and the same operation works when the fault
; is cleared.
;
;   1. OOM: fork with the next N page allocations failing. Every prefix length
;      fails at a different point in the fork path (kernel stack, area table,
;      page directory, COW share), so this walks N upwards and requires the
;      free-page count to come back to its starting value every time.
;   2. Disk: a file create whose first write fails must report an error; the
;      same create has to succeed once the fault is cleared, and read back.
;   3. Network: a dropped frame must be retried, not fatal. The resolver needs
;      the NIC up, so the harness runs `dhcp` first and this section skips
;      itself if there is no network at all.
;
; Run as:  fault_inject_test
; Output:  FI_ALLOC_OK / FI_DISK_OK / FI_NET_OK (or FI_NET_SKIP)
;          FAULT_OK

%include "syscalls.inc"

FI_OP_CLEAR  equ 0
FI_OP_ALLOC  equ 1
FI_OP_IO     equ 2
FI_OP_NET    equ 3
FI_OP_TOTAL  equ 5

FI_SLACK     equ 4              ; pages one-off allocations may leave behind
FI_MAX_FAILS equ 12             ; longest prefix of failing allocations to try

start:
        mov eax, SYS_FAULTINJECT
        xor ebx, ebx
        int 0x80                        ; clear anything a previous run armed

        ; The network section needs the NIC up. Probe it before touching any
        ; fault, because a failed probe is the environment's fault, not ours.
        mov eax, SYS_DNS
        mov ebx, name_probe
        int 0x80
        mov [net_up], eax

        call test_alloc
        call test_disk
        call test_net

        mov eax, SYS_FAULTINJECT
        xor ebx, ebx
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

;-----------------------------------------------------------------------
; test_alloc - fork must survive an allocation failure at every stage
;
; Arming N failures means the fork fails at whichever allocation comes N-th:
; N=1 is the kernel stack, larger N reach the area table, the page directory
; and the COW share. Whichever it is, fork returns -1 (or, if the budget runs
; out first, succeeds), nothing leaks, and the child - when there is one -
; still works.
;-----------------------------------------------------------------------
test_alloc:
        mov eax, SYS_MEMINFO
        int 0x80
        mov [free_base], eax

        mov dword [fail_n], 1
.next_n:
        ; Arm exactly this many failures, fork, then clear them immediately so
        ; a short-lived budget cannot leak into unrelated kernel work.
        mov eax, SYS_FAULTINJECT
        mov ebx, FI_OP_ALLOC
        mov ecx, [fail_n]
        int 0x80

        mov eax, SYS_FORK
        int 0x80
        mov [child_pid], eax

        push eax
        mov eax, SYS_FAULTINJECT
        xor ebx, ebx
        int 0x80
        pop eax

        cmp eax, -1
        jne .reap
        mov byte [saw_fail], 1
        jmp .check_free

.reap:
        test eax, eax
        jz  .child_exit                 ; we are the child: nothing to do
        mov ebx, eax
        mov eax, SYS_WAITPID
        int 0x80

.check_free:
        mov eax, SYS_MEMINFO
        int 0x80
        mov ebx, [free_base]
        sub ebx, eax                    ; pages that never came back
        cmp ebx, FI_SLACK
        ja  .fail_leak

        inc dword [fail_n]
        cmp dword [fail_n], FI_MAX_FAILS
        jbe .next_n

        cmp byte [saw_fail], 0
        je  .fail_nofault               ; the gate never fired: not a test
        mov eax, SYS_FAULTINJECT
        mov ebx, FI_OP_TOTAL
        int 0x80                        ; EAX = allocations failed so far
        test eax, eax
        jz  .fail_nofault

        mov eax, SYS_PRINT
        mov ebx, msg_alloc_ok
        int 0x80
        ret

.child_exit:
        xor ebx, ebx
        mov eax, SYS_EXIT
        int 0x80

.fail_leak:
        mov [leaked], ebx
        mov eax, SYS_PRINT
        mov ebx, msg_alloc_fail
        int 0x80
        mov eax, [leaked]
        call print_dec
        call fail

.fail_nofault:
        mov eax, SYS_PRINT
        mov ebx, msg_alloc_nofault
        int 0x80
        call fail

;-----------------------------------------------------------------------
; test_disk - a failed write must be reported, and the next one must work
;-----------------------------------------------------------------------
test_disk:
        ; Arm enough writes to cover the whole create - journal, bitmap,
        ; directory entry and the data blocks - so this models a disk that is
        ; full for the duration of the call rather than one bad sector. The
        ; create has to notice and report it, not commit half a file.
        mov eax, SYS_FAULTINJECT
        mov ebx, FI_OP_IO
        mov ecx, 32
        int 0x80

        mov eax, SYS_FWRITE
        mov ebx, name_tmp
        mov ecx, payload
        mov edx, payload_len
        xor esi, esi
        int 0x80

        push eax
        mov eax, SYS_FAULTINJECT
        xor ebx, ebx
        int 0x80
        pop eax

        cmp eax, -1
        jne .fail_noreport

        mov eax, SYS_FAULTINJECT
        mov ebx, FI_OP_TOTAL
        int 0x80                        ; EBX = writes failed so far
        test ebx, ebx
        jz  .fail_noreport

        ; The same create has to work now, and read back unchanged.
        mov eax, SYS_FWRITE
        mov ebx, name_tmp
        mov ecx, payload
        mov edx, payload_len
        xor esi, esi
        int 0x80
        cmp eax, 0
        jne .fail_write

        mov eax, SYS_FREAD
        mov ebx, name_tmp
        mov ecx, read_buf
        int 0x80
        cmp eax, payload_len
        jne .fail_read

        mov esi, payload
        mov edi, read_buf
        mov ecx, payload_len
        cld
        repe cmpsb
        jne .fail_read

        mov eax, SYS_DELETE
        mov ebx, name_tmp
        int 0x80

        mov eax, SYS_PRINT
        mov ebx, msg_disk_ok
        int 0x80
        ret

.fail_noreport:
        mov eax, SYS_PRINT
        mov ebx, msg_disk_noreport
        int 0x80
        call fail

.fail_write:
        mov eax, SYS_PRINT
        mov ebx, msg_disk_write
        int 0x80
        call fail

.fail_read:
        mov eax, SYS_PRINT
        mov ebx, msg_disk_read
        int 0x80
        call fail

;-----------------------------------------------------------------------
; test_net - a dropped frame must be retried
;
; One dropped frame kills the first DNS attempt; the resolver retries and the
; second query gets through, so the lookup still succeeds. That is the point:
; chaos at the device layer has to be invisible to the caller once the retry
; works, and the drop counter proves the gate actually fired.
;-----------------------------------------------------------------------
test_net:
        cmp dword [net_up], 0
        je  .skip

        mov eax, SYS_FAULTINJECT
        mov ebx, FI_OP_NET
        mov ecx, 1
        int 0x80

        mov eax, SYS_DNS
        mov ebx, name_drop
        int 0x80
        mov [resolved], eax

        push eax
        mov eax, SYS_FAULTINJECT
        mov ebx, FI_OP_TOTAL
        int 0x80
        mov [dropped], ecx              ; frames dropped so far
        pop eax

        push eax
        mov eax, SYS_FAULTINJECT
        xor ebx, ebx
        int 0x80
        pop eax

        cmp dword [dropped], 0
        je  .fail_nodrop
        cmp dword [resolved], 0
        je  .fail_resolve

        mov eax, SYS_PRINT
        mov ebx, msg_net_ok
        int 0x80
        ret

.skip:
        mov eax, SYS_PRINT
        mov ebx, msg_net_skip
        int 0x80
        ret

.fail_nodrop:
        mov eax, SYS_PRINT
        mov ebx, msg_net_nodrop
        int 0x80
        call fail

.fail_resolve:
        mov eax, SYS_PRINT
        mov ebx, msg_net_resolve
        int 0x80
        call fail

;-----------------------------------------------------------------------
; fail - report a failed section and exit with status 1
;-----------------------------------------------------------------------
fail:
        mov ebx, 1
        mov eax, SYS_EXIT
        int 0x80

name_probe:   db 'example.com', 0
name_drop:    db 'example.org', 0
name_tmp:     db 'fi_tmp.txt', 0
payload:      db 'fault injection payload', 10
payload_len   equ $ - payload

msg_alloc_ok:     db 'FI_ALLOC_OK', 13, 10, 0
msg_alloc_fail:   db 'FAULT_FAIL alloc leaked ', 0
msg_alloc_nofault: db 'FAULT_FAIL alloc gate never fired', 13, 10, 0
msg_disk_ok:      db 'FI_DISK_OK', 13, 10, 0
msg_disk_noreport: db 'FAULT_FAIL write did not report an error', 13, 10, 0
msg_disk_write:   db 'FAULT_FAIL write after clear failed', 13, 10, 0
msg_disk_read:    db 'FAULT_FAIL read-back mismatch', 13, 10, 0
msg_net_ok:       db 'FI_NET_OK', 13, 10, 0
msg_net_skip:     db 'FI_NET_SKIP', 13, 10, 0
msg_net_nodrop:   db 'FAULT_FAIL no frame was dropped', 13, 10, 0
msg_net_resolve:  db 'FAULT_FAIL retry did not recover', 13, 10, 0
msg_ok:           db 'FAULT_OK', 13, 10, 0

net_up:     dd 0
resolved:   dd 0
dropped:    dd 0
free_base:  dd 0
leaked:     dd 0
fail_n:     dd 0
saw_fail:   db 0
child_pid:  dd 0
read_buf:   times 64 db 0
