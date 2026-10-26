;=======================================================================
; object.inc - the object manager (M1)
;
; One model for every resource a program can name, hold, share, wait on or
; secure. A resource is a typed object with a header; a program reaches it
; through a handle carrying rights; the object has a name in one namespace, a
; security descriptor, and a signalled state others can wait on.
;
; The contract is docs/spec/OBJECTS.md; the layout below is mirrored by
; c/include/mv/objects.h, whose compile-time assertions fail the kernel module
; build if the two ever disagree.
;
; Register discipline: every routine here preserves every register but the
; ones it documents in its header comment, because callers hold object
; pointers, rights masks and name pointers across calls. Bodies that touch more
; than two registers use pushad/popad rather than a hand-written save list -
; that is the rule ABI.md §4 records, and the reason is in the CHANGELOG entry
; for M0's allocator bug.
;=======================================================================

%ifndef OBJECT_INC
%define OBJECT_INC

;-----------------------------------------------------------------------
; Object header. 64 bytes, 16-byte aligned, so a type body starts at a fixed
; offset. Nothing may be inserted before OBJ_TYPE_DATA.
;-----------------------------------------------------------------------
OBJ_TYPE        equ 0           ; dd  type index, immutable
OBJ_FLAGS       equ 4           ; dd  OF_* below
OBJ_REFS        equ 8           ; dd  pointer references
OBJ_HANDLES     equ 12          ; dd  open handle count
OBJ_NAME_OFF    equ 16          ; dd  offset to the name, 0 = unnamed
OBJ_NAME_LEN    equ 20          ; dw  length, no terminator
OBJ_RESERVED    equ 22          ; dw
OBJ_SECURITY    equ 24          ; dd  security descriptor pointer
OBJ_WAITERS     equ 28          ; dd  head of the waiter list
OBJ_CREATED     equ 32          ; dq  mv_time
OBJ_OWNER_PID   equ 40          ; dd  creator
OBJ_QUOTA       equ 44          ; dd  bytes charged to the creator
OBJ_TYPE_DATA   equ 48          ; 16 bytes of per-type header space
OBJ_TD_SIGNAL   equ 48          ; dd  signalled state (1 = signalled)
OBJ_TD_SPARE    equ 52          ; 12 bytes for the type

OBJ_NAME        equ 64          ; 256 bytes, ASCII, NUL-terminated
OBJ_NAME_MAX    equ 255         ; documented longest name
OBJ_BODY        equ 320         ; 192 bytes of type body
OBJ_BODY_SIZE   equ 192
OBJ_SIZE        equ 512         ; total; a kmem cache of 512-byte bodies

; Namespace links live in every body so any object can sit in the tree.
NS_NEXT         equ 0           ; dd  next sibling
NS_CHILD        equ 4           ; dd  first child (directories)
NS_PARENT       equ 8           ; dd  owning directory
NS_FLAGS        equ 12          ; dd  NF_*
NS_RESERVED     equ 16          ; type bodies begin here

; OBJ_FLAGS
OF_PERMANENT    equ 1           ; not destroyed when the last handle closes
OF_SESSION      equ 2           ; named per session, not global
OF_KERNEL_ONLY  equ 4           ; a ring-3 open is refused
OF_INHERITABLE  equ 8           ; passed across process creation
OF_DELETING     equ 16          ; rundown: no new pointer references
OF_NAMED        equ 32          ; present in the namespace

; NS_FLAGS
NF_IN_NS        equ 1
NF_SYMLINK      equ 2

;-----------------------------------------------------------------------
; Generic rights. The low half is generic, the high half type-specific;
; MV_RIGHTS_ALL asks the type what "all" means rather than being a mask.
;-----------------------------------------------------------------------
RIGHT_QUERY_STATE       equ 0x0001
RIGHT_SET_STATE         equ 0x0002
RIGHT_READ_DATA         equ 0x0004
RIGHT_WRITE_DATA        equ 0x0008
RIGHT_EXECUTE           equ 0x0010
RIGHT_DELETE            equ 0x0020
RIGHT_READ_ATTR         equ 0x0040
RIGHT_WRITE_ATTR        equ 0x0080
RIGHT_SYNCHRONIZE       equ 0x0100
RIGHT_GENERIC_MASK      equ 0x01FF
MV_RIGHTS_ALL           equ 0x00010000    ; desired-rights flag: "all generic"
RIGHT_TYPE_SHIFT        equ 9

;-----------------------------------------------------------------------
; Types. The table is data, not a switch: a type is a record, and naming,
; securing, counting and closing all read it.
;
;   TT_NAME    dd  type name for `objects` and the audit log
;   TT_ALL     dd  the rights this type defines - and therefore what
;                  MV_RIGHTS_ALL means for it (OBJECTS.md S8)
;   TT_FLAGS   dd  TF_*
;   TT_CLOSE   dd  routine(obj), or 0 - runs at destruction
;   TT_ENTRY   equ record size
;-----------------------------------------------------------------------
TT_NAME         equ 0
TT_ALL          equ 4
TT_FLAGS        equ 8
TT_CLOSE        equ 12
TT_ENTRY        equ 16

TF_WAITABLE     equ 1           ; carries a signalled state
TF_NAMED        equ 2           ; may live in the namespace
TF_SECURED      equ 4           ; gets a default descriptor

OBJ_T_DIRECTORY equ 0
OBJ_T_EVENT     equ 1
OBJ_T_MUTANT    equ 2
OBJ_T_SEMAPHORE equ 3
OBJ_T_TIMER     equ 4
OBJ_T_TOKEN     equ 5
OBJ_T_PROCESS   equ 6
OBJ_T_SYMLINK   equ 7
OBJ_T_COUNT     equ 8

; Type body layouts, after the 16 bytes of namespace links.
EV_MANUAL       equ NS_RESERVED + 0      ; dd non-zero = manual reset
EV_STATE        equ NS_RESERVED + 4      ; dd 1 = signalled (mirrors TD_SIGNAL)
MU_OWNER        equ NS_RESERVED + 0      ; dd owning pid, 0 = unowned
MU_RECURSE      equ NS_RESERVED + 4      ; dd acquisition count
MU_ABANDONED    equ NS_RESERVED + 8      ; dd owner exited holding it
SE_COUNT        equ NS_RESERVED + 0      ; dd current count
SE_MAX          equ NS_RESERVED + 4      ; dd maximum count
TM_DUE          equ NS_RESERVED + 0      ; dd tick at which it signals
TM_PERIOD       equ NS_RESERVED + 4      ; dd reload period, 0 = one-shot
TG_USER         equ NS_RESERVED + 0      ; dd uid
TG_GROUP        equ NS_RESERVED + 4      ; dd gid
TG_SESSION      equ NS_RESERVED + 8      ; dd session id
TG_INTEGRITY    equ NS_RESERVED + 12     ; dd 0..5
TG_PRIV         equ NS_RESERVED + 16     ; dd privilege mask
TG_UID          equ NS_RESERVED + 20     ; dd 12-bit SID key
PR_TCB          equ NS_RESERVED + 0      ; dd the TCB implementing it
PR_EXIT         equ NS_RESERVED + 4      ; dd exit code

;-----------------------------------------------------------------------
; Handle table. A handle is (generation << 18) | (index << 2), so a closed and
; reused slot cannot be reached with the old number and a handle is never a
; small integer. Entries are 8 bytes and a table is a contiguous run of pages,
; which grows by 4096 entries and is refused on quota rather than silently
; truncated.
;-----------------------------------------------------------------------
HE_OBJ          equ 0           ; dd object pointer, 0 = free slot
HE_RIGHTS       equ 4           ; dw rights mask
HE_GEN          equ 6           ; dw generation, 1..0xFFFF
HE_ENTRY        equ 8

HANDLE_INDEX_SHIFT  equ 2           ; low two bits are zero (ABI.md §6)
HANDLE_GEN_SHIFT    equ 18
HANDLE_INDEX_MASK   equ 0x0000FFFF  ; 16 bits: 65 536 handles
HANDLE_GEN_MASK     equ 0x00003FFF  ; 14 bits of generation
HANDLE_DEFAULT_CAP  equ 4096
HANDLE_MAX_CAP      equ 65536
HANDLE_PAGE_ENTRIES equ 4096    ; entries per growth step (8 pages)

;-----------------------------------------------------------------------
; Wait limits and status codes returned to user space
;-----------------------------------------------------------------------
WAIT_MAX_HANDLES equ 64
WAIT_TIMEOUT    equ -1          ; MV_TIMEOUT
WAIT_USER_APC   equ -2          ; alertable wait interrupted
WAIT_FAILED     equ -3

;-----------------------------------------------------------------------
; mv_status values a program can compare against (ABI.md §5). The two new
; object calls put these in EDX and return -1 in EAX, so a legacy caller sees
; a failure and a new one can say which.
;-----------------------------------------------------------------------
MV_STATUS_OK                equ 0x00000000
MV_STATUS_TIMEOUT           equ 0x00000102
MV_STATUS_PENDING           equ 0x00000103
MV_STATUS_INVALID_HANDLE    equ 0xC0000008
MV_STATUS_INVALID_PARAMETER equ 0xC000000D
MV_STATUS_ACCESS_DENIED     equ 0xC0000022
MV_STATUS_NO_MEMORY         equ 0xC0000017
MV_STATUS_BUFFER_TOO_SMALL  equ 0xC0000023
MV_STATUS_NOT_SUPPORTED     equ 0xC00000BB

OBJ_MAX_DEPTH   equ 32          ; namespace depth limit
SYMLINK_MAX     equ 32          ; symlink follow limit

; Wait-block reason, recorded in the TCB so a hung system can say what each
; task waits for (RELIABILITY.md).
WAITR_NONE      equ 0
WAITR_SLEEP     equ 1
WAITR_OBJECT    equ 2
WAITR_PIPE      equ 3

;-----------------------------------------------------------------------
; obj_type_desc - EAX = type id -> EAX = type record, or 0 when out of range
;-----------------------------------------------------------------------
obj_type_desc:
        cmp eax, OBJ_T_COUNT
        jae .otd_bad
        shl eax, 4                      ; TT_ENTRY is 16
        add eax, obj_type_table
        ret
.otd_bad:
        xor eax, eax
        ret

;-----------------------------------------------------------------------
; obj_alloc - take a zeroed object body and give it the creation reference
;   In : EAX = type id
;   Out: EAX = object pointer (refs = 1), or 0 when out of memory
;-----------------------------------------------------------------------
obj_alloc:
        push ebx
        push ecx
        push edx
        push esi
        push edi
        push eax
        push eax
        pop eax
        pop eax
        cmp eax, OBJ_T_COUNT
        jae .oa_fail

        mov [.oa_type], eax
        mov eax, [cache_object]
        call kmem_alloc
        test eax, eax
        jz .oa_fail
        mov edi, eax

        mov ecx, [.oa_type]
        mov [edi + OBJ_TYPE], ecx
        mov dword [edi + OBJ_REFS], 1
        mov dword [edi + OBJ_HANDLES], 0
        mov dword [edi + OBJ_NAME_OFF], OBJ_NAME
        mov dword [edi + OBJ_SECURITY], 0
        mov dword [edi + OBJ_WAITERS], 0
        mov dword [edi + OBJ_TD_SIGNAL], 0
        mov dword [edi + NS_FLAGS], 0

        ; Charge the body to the creator and remember who that is.
        mov [.oa_obj], edi
        call obj_ctx
        test eax, eax
        jz .oa_nopid
        mov edx, [eax + TCB_PID]
        mov edi, [.oa_obj]
        mov [edi + OBJ_OWNER_PID], edx
.oa_nopid:
        mov edi, [.oa_obj]
        mov dword [edi + OBJ_QUOTA], OBJ_SIZE

        ; Live and peak counters, per type, for `objects`.
        mov eax, [.oa_type]
        inc dword [obj_type_live + eax*4]
        mov ecx, [obj_type_live + eax*4]
        cmp ecx, [obj_type_peak + eax*4]
        jbe .oa_nopeak
        mov [obj_type_peak + eax*4], ecx
.oa_nopeak:
        inc dword [obj_total_live]

        mov eax, edi
        jmp .oa_done
.oa_fail:
        xor eax, eax
.oa_done:
        pop edi
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret
.oa_type: dd 0
.oa_obj:  dd 0

;-----------------------------------------------------------------------
; obj_ref - take a pointer reference
;   In : EAX = object          Out: EAX unchanged; refs incremented
;
; A reference on a deleting object is refused: that is the rundown rule, and
; the caller must treat a 0 return from its own lookup as "about to vanish".
; The routine still returns the pointer, so callers that already hold a
; reference are unaffected.
;-----------------------------------------------------------------------
obj_ref:
        pushad
        test eax, eax
        jz .or_done
        mov edi, eax
        test dword [edi + OBJ_FLAGS], OF_DELETING
        jnz .or_done                    ; rundown: do not resurrect
        inc dword [edi + OBJ_REFS]
.or_done:
        popad
        ret

;-----------------------------------------------------------------------
; obj_deref - drop a pointer reference; destroy when the last one goes
;   In : EAX = object (0 is ignored)
;
; Destruction runs the type's close routine, unhooks the name from the
; namespace, returns the descriptor and the body, and decrements the type's
; live count. An underflow stops the machine and names the type, because the
; alternative is a use-after-free that shows up as corruption elsewhere.
;-----------------------------------------------------------------------
obj_deref:
        pushad
        test eax, eax
        jz .od_done
        mov edi, eax
        mov ecx, [edi + OBJ_REFS]
        test ecx, ecx
        jz .od_underflow
        dec ecx
        mov [edi + OBJ_REFS], ecx
        jnz .od_done                    ; still referenced
        call obj_destroy
.od_done:
        popad
        ret
.od_underflow:
        mov esi, obj_msg_underflow
        call serial_print
        mov eax, [edi + OBJ_TYPE]
        call serial_print_hex
        mov eax, edi
        call serial_print_hex
        mov esi, obj_msg_newline
        call serial_print
        cli
.od_halt:
        hlt
        jmp .od_halt

;-----------------------------------------------------------------------
; obj_destroy - free an object whose reference count reached zero
;   In : EDI = object, not 0, refs already 0
;
; Called only from obj_deref. Runs the type's close routine first, so a type
; that owns something external releases it before the body goes back to the
; cache.
;-----------------------------------------------------------------------
obj_destroy:
        pushad
        mov [.od_obj], edi

        mov dword [edi + OBJ_FLAGS], OF_DELETING

        ; The type's close routine, if it has one.
        mov eax, [edi + OBJ_TYPE]
        call obj_type_desc
        test eax, eax
        jz .odesc_noclose
        mov eax, [eax + TT_CLOSE]
        test eax, eax
        jz .odesc_noclose
        mov ebx, [.od_obj]
        call eax
.odesc_noclose:

        ; Unhook from the namespace so a freed body cannot be reached by name.
        mov eax, [.od_obj]
        call obj_ns_unlink

        ; Fill the security descriptor back in, if the type had one.
        mov eax, [.od_obj]
        call obj_security_free

        ; Counters.
        mov edi, [.od_obj]
        mov eax, [edi + OBJ_TYPE]
        cmp dword [obj_type_live + eax*4], 0
        je .odesc_nofloor
        dec dword [obj_type_live + eax*4]
.odesc_nofloor:
        cmp dword [obj_total_live], 0
        je .odesc_nototal
        dec dword [obj_total_live]
.odesc_nototal:

        mov eax, [cache_object]
        mov ebx, [.od_obj]
        call kmem_free
        popad
        ret
.od_obj: dd 0

;-----------------------------------------------------------------------
; obj_ctx - the process context the caller is running as
;   Out: EAX = its TCB, or 0 for the kernel/shell (the SYSTEM token)
;
; Called from every object syscall. The ring the INT 0x80 came from decides:
; a ring-3 caller is the RUNNING task, and task_current names it, so the
; common case is one multiply. Ring 0 is the shell and its foreground path,
; which is the kernel's own authority - there is no task to charge a handle
; to, and giving the shell a token of its own would only mean pretending.
;
; The state is checked rather than trusted: task_current keeps the last task
; switched away from while the shell runs, and charging a handle to that is
; exactly the stale-slot bug errno_set documents. A task that is not RUNNING
; falls back to the scan, and to 0 if nothing is running.
;-----------------------------------------------------------------------
obj_ctx:
        cmp byte [syscall_ring], 0
        je .oc_none                     ; ring 0: kernel authority
        mov eax, [task_current]
        cmp eax, MAX_TASKS
        jae .oc_scan
        imul eax, TCB_SIZE
        add eax, task_table
        cmp dword [eax + TCB_STATE], TASK_RUNNING
        jne .oc_scan
        ret
.oc_scan:
        call sched_get_current_task
        ret
.oc_none:
        xor eax, eax
        ret

;=======================================================================
; Handle tables
;
; One table per process, a contiguous run of pages so an index is an offset
; and growth is a copy. The right place to grow is before an open fails, not
; after: a caller that gets MV_NO_MEMORY has been told the truth.
;=======================================================================

;-----------------------------------------------------------------------
; ht_slot - address of a handle entry in the current context's table
;   In : EAX = index, EBX = table base
;   Out: EAX = entry address
;-----------------------------------------------------------------------
ht_slot:
        shl eax, 3                      ; HE_ENTRY is 8
        add eax, ebx
        ret

;-----------------------------------------------------------------------
; ht_grow - give the current context's table room for HANDLE_PAGE_ENTRIES more
;   In : EDI = TCB
;   Out: CF=0 grown, CF=1 out of memory
;
; The new run is contiguous pages, so the entries stay at index * 8 from its
; base. The old run is copied and freed. Capacity is capped at HANDLE_MAX_CAP
; and the refusal is a status, never a silent truncation.
;-----------------------------------------------------------------------
ht_grow:
        pushad
        mov [.hg_tcb], edi
        mov eax, [edi + TCB_HCAP]
        cmp eax, HANDLE_MAX_CAP
        jae .hg_fail
        mov ecx, HANDLE_PAGE_ENTRIES
        cmp eax, HANDLE_DEFAULT_CAP
        jae .hg_ready                   ; already at or past the default step
        mov ecx, HANDLE_DEFAULT_CAP     ; first growth is a whole default table
.hg_ready:
        mov [.hg_add], ecx
        add ecx, eax                    ; new capacity
        cmp ecx, HANDLE_MAX_CAP
        jbe .hg_cap_ok
        mov ecx, HANDLE_MAX_CAP
.hg_cap_ok:
        mov [.hg_cap], ecx
        shl ecx, 3                      ; bytes of entries
        add ecx, 4095
        shr ecx, 12                     ; pages
        mov [.hg_pages], ecx
        call pmm_alloc_pages
        test eax, eax
        jz .hg_fail
        mov [.hg_new], eax

        ; A handle table must start empty. The PMM hands out pages with
        ; whatever the previous owner left in them, and a "free" slot is one
        ; whose object pointer is zero - so without this the first scan sees
        ; 4096 occupied slots and the table grows until the cap refuses it.
        mov edi, eax
        mov ecx, [.hg_pages]
        shl ecx, 10                     ; dwords in the run
        xor eax, eax
        cld
        rep stosd

        ; Copy the live entries over, then free the old run.
        mov edi, eax
        mov esi, [.hg_tcb]
        mov esi, [esi + TCB_HANDLES]
        test esi, esi
        jz .hg_no_copy
        mov ecx, [.hg_tcb]
        mov ecx, [ecx + TCB_HCAP]
        shl ecx, 1                      ; dwords (8 bytes per entry)
        cld
        rep movsd
.hg_no_copy:
        mov edi, [.hg_tcb]
        mov eax, [edi + TCB_HANDLES]
        test eax, eax
        jz .hg_install
        mov ecx, [edi + TCB_HCAP]
        shl ecx, 3
        add ecx, 4095
        shr ecx, 12
        call pmm_free_pages             ; EAX = base, ECX = pages
.hg_install:
        mov edi, [.hg_tcb]
        mov eax, [.hg_new]
        mov [edi + TCB_HANDLES], eax
        mov eax, [.hg_cap]
        mov [edi + TCB_HCAP], eax
        popad
        clc
        ret
.hg_fail:
        popad
        stc
        ret
.hg_tcb:   dd 0
.hg_add:   dd 0
.hg_cap:   dd 0
.hg_pages: dd 0
.hg_new:   dd 0

;-----------------------------------------------------------------------
; ht_open - put a handle on an object
;   In : EAX = object, EBX = rights, ECX = 1 to mark inheritable
;   Out: EAX = handle, or 0 with EDX = status
;
; Takes a pointer reference and increments the object's handle count, so the
; object outlives the handle if something else (a waiter, an in-flight
; operation) still holds it.
;-----------------------------------------------------------------------
ht_open:
        pushad
        mov [.ho_obj], eax
        mov [.ho_inherit], ecx

        ; A handle carries concrete rights. MV_RIGHTS_ALL is a request, not a
        ; mask: the type answers what "all" means for it, once, here - so a
        ; handle's rights word is always something an operation can test.
        mov [.ho_rights], ebx
        test ebx, MV_RIGHTS_ALL
        jz .ho_rights_ready
        mov eax, [.ho_obj]
        mov eax, [eax + OBJ_TYPE]
        call obj_type_desc
        mov eax, [eax + TT_ALL]
        mov [.ho_rights], eax
.ho_rights_ready:

        call obj_ctx
        test eax, eax
        jz .ho_deny                     ; no process context: no place to record
        mov [.ho_tcb], eax
        mov edi, eax

        ; Find a free slot; grow when the run is full.
        mov ebx, [edi + TCB_HANDLES]
.ho_find:
        test ebx, ebx
        jz .ho_need_grow
        mov ecx, [edi + TCB_HCAP]
        xor esi, esi
.ho_scan:
        cmp esi, ecx
        jae .ho_need_grow
        mov eax, esi
        call ht_slot
        cmp dword [eax + HE_OBJ], 0
        je .ho_take
        inc esi
        jmp .ho_scan

.ho_need_grow:
        call ht_grow
        jc .ho_nomem
        mov edi, [.ho_tcb]
        mov ebx, [edi + TCB_HANDLES]
        jmp .ho_find

.ho_take:
        mov [.ho_index], esi
        mov edi, [.ho_tcb]
        mov ecx, [edi + TCB_HCOUNT]
        cmp ecx, HANDLE_MAX_CAP
        jae .ho_nomem

        mov eax, esi
        call ht_slot
        mov edi, eax
        mov eax, [.ho_obj]
        mov [edi + HE_OBJ], eax

        ; A slot keeps its generation so the number it had cannot be used
        ; again, and the generation never takes the value 0 (which a handle
        ; must not have).
        mov eax, [edi + HE_GEN]
        inc eax
        and eax, HANDLE_GEN_MASK
        jnz .ho_gen_ok
        mov eax, 1
.ho_gen_ok:
        mov [edi + HE_GEN], eax

        mov ecx, [.ho_rights]
        mov [edi + HE_RIGHTS], cx
        mov dword [.ho_gen], eax

        ; Mark the object inheritable when asked.
        test dword [.ho_inherit], 1
        jz .ho_no_inherit
        mov eax, [.ho_obj]
        or dword [eax + OBJ_FLAGS], OF_INHERITABLE
.ho_no_inherit:
        mov eax, [.ho_obj]
        call obj_ref
        inc dword [eax + OBJ_HANDLES]

        mov edi, [.ho_tcb]
        inc dword [edi + TCB_HCOUNT]

        mov eax, [.ho_index]
        shl eax, HANDLE_INDEX_SHIFT
        mov ecx, [.ho_gen]
        shl ecx, HANDLE_GEN_SHIFT
        or eax, ecx
        mov [esp + 28], eax
        xor edx, edx
        popad
        clc
        mov edx, 0
        ret
.ho_deny:
        popad
        mov eax, 0
        mov edx, MV_STATUS_ACCESS_DENIED
        stc
        ret
.ho_nomem:
        popad
        mov eax, 0
        mov edx, MV_STATUS_NO_MEMORY
        stc
        ret
.ho_obj:     dd 0
.ho_rights:  dd 0
.ho_inherit: dd 0
.ho_tcb:     dd 0
.ho_index:   dd 0
.ho_gen:     dd 0

;-----------------------------------------------------------------------
; ht_entry - resolve a handle to its entry in the current context
;   In : EAX = handle
;   Out: EAX = entry address, or 0; CF=0 found, CF=1 not (stale or unknown)
;;
; A handle is only valid for the slot and generation it was issued with, so a
; number captured before a close cannot reach whatever took the slot next.
;-----------------------------------------------------------------------
ht_entry:
        pushad
        mov [.he_handle], eax
        call obj_ctx
        test eax, eax
        jz .he_fail
        mov edi, eax
        mov ebx, [edi + TCB_HANDLES]
        test ebx, ebx
        jz .he_fail
        mov eax, [.he_handle]
        mov esi, eax
        shr esi, HANDLE_INDEX_SHIFT
        and esi, HANDLE_INDEX_MASK      ; index
        shr eax, HANDLE_GEN_SHIFT       ; generation
        and eax, HANDLE_GEN_MASK
        test eax, eax
        jz .he_fail                     ; generation 0 is never issued
        cmp esi, [edi + TCB_HCAP]
        jae .he_fail
        mov [.he_gen], eax
        mov eax, esi
        call ht_slot
        cmp dword [eax + HE_OBJ], 0
        je .he_fail
        mov ecx, [.he_gen]
        cmp word [eax + HE_GEN], cx     ; a word field needs a word compare
        jne .he_fail
        mov [esp + 28], eax
        popad
        clc
        ret
.he_fail:
        popad
        xor eax, eax                    ; before stc: xor clears the carry flag
        stc
        ret
.he_handle: dd 0
.he_gen:    dd 0

;-----------------------------------------------------------------------
; ht_use - resolve a handle and check the rights it carries
;   In : EAX = handle, EBX = rights required
;   Out: EAX = object (referenced by the handle, do not free), or 0
;        EDX = status: 0, MV_STATUS_INVALID_HANDLE, MV_STATUS_ACCESS_DENIED
;
; Rights are enforced at the operation and not only at open, which is what
; makes WRITE_DATA on an event mean the same thing as WRITE_DATA on a file.
;-----------------------------------------------------------------------
ht_use:
        pushad
        mov [.hu_rights], ebx
        call ht_entry
        jc .hu_stale
        mov esi, eax
        mov ebx, [.hu_rights]
        movzx ecx, word [esi + HE_RIGHTS]   ; a word field needs a word read
        mov [.hu_have], ecx
        and ecx, ebx
        cmp ecx, ebx
        jne .hu_denied
        mov eax, [esi + HE_OBJ]
        mov [esp + 28], eax
        popad
        mov edx, 0
        clc
        ret
.hu_stale:
        popad
        xor eax, eax
        mov edx, MV_STATUS_INVALID_HANDLE
        stc
        ret
.hu_denied:
        popad
        xor eax, eax
        mov edx, MV_STATUS_ACCESS_DENIED
        stc
        ret
.hu_rights: dd 0
.hu_have:   dd 0

;-----------------------------------------------------------------------
; ht_close - drop a handle
;   In : EAX = handle          Out: EAX = 0, or -1 with EDX = status
;-----------------------------------------------------------------------
ht_close:
        pushad
        call ht_entry
        jc .hc_stale
        mov edi, eax
        mov eax, [edi + HE_OBJ]
        mov [.hc_obj], eax
        mov dword [edi + HE_OBJ], 0     ; the slot is free; the generation stays
        mov dword [edi + HE_RIGHTS], 0

        mov eax, [.hc_obj]
        cmp dword [eax + OBJ_HANDLES], 0
        je .hc_nocount
        dec dword [eax + OBJ_HANDLES]
.hc_nocount:
        call obj_ctx
        test eax, eax
        jz .hc_deref
        cmp dword [eax + TCB_HCOUNT], 0
        je .hc_deref
        dec dword [eax + TCB_HCOUNT]
.hc_deref:
        mov eax, [.hc_obj]
        call obj_deref
        popad
        xor eax, eax
        clc
        ret
.hc_stale:
        popad
        mov eax, -1
        mov edx, MV_STATUS_INVALID_HANDLE
        stc
        ret
.hc_obj: dd 0

;-----------------------------------------------------------------------
; ht_close_all - release every handle in the current context
;
; Called when a task exits. A handle left open at exit is a leak the object
; counters would show forever, and on the exit path there is no one left to
; tell, so it is not an error - it is the teardown.
;-----------------------------------------------------------------------
ht_close_all:
        pushad
        call obj_ctx
        test eax, eax
        jz .hca_done
        mov [.hca_tcb], eax
        mov edi, eax
        mov ebx, [edi + TCB_HANDLES]
        test ebx, ebx
        jz .hca_free_table
        xor esi, esi
.hca_loop:
        mov edi, [.hca_tcb]
        cmp esi, [edi + TCB_HCAP]
        jae .hca_free_table
        mov eax, esi
        call ht_slot
        mov edi, eax
        cmp dword [edi + HE_OBJ], 0
        je .hca_next
        mov eax, [edi + HE_OBJ]
        mov [.hca_obj], eax
        mov dword [edi + HE_OBJ], 0
        mov dword [edi + HE_RIGHTS], 0
        cmp dword [eax + OBJ_HANDLES], 0
        je .hca_nocount
        dec dword [eax + OBJ_HANDLES]
.hca_nocount:
        mov eax, [.hca_obj]
        call obj_deref
.hca_next:
        inc esi
        jmp .hca_loop

.hca_free_table:
        mov edi, [.hca_tcb]
        mov dword [edi + TCB_HCOUNT], 0
        mov eax, [edi + TCB_HANDLES]
        test eax, eax
        jz .hca_clear
        mov ecx, [edi + TCB_HCAP]
        shl ecx, 3
        add ecx, 4095
        shr ecx, 12
        call pmm_free_pages
.hca_clear:
        mov edi, [.hca_tcb]
        mov dword [edi + TCB_HANDLES], 0
        mov dword [edi + TCB_HCAP], 0
        ; The token and the process object are references too.
        mov eax, [edi + TCB_TOKEN]
        mov dword [edi + TCB_TOKEN], 0
        call obj_deref
        mov edi, [.hca_tcb]
        mov eax, [edi + TCB_PROCOBJ]
        mov dword [edi + TCB_PROCOBJ], 0
        call obj_deref
.hca_done:
        popad
        ret
.hca_tcb: dd 0
.hca_obj: dd 0

;=======================================================================
; Identity, tokens and the access check
;
; In this wave the truth about who a caller is lives in the TCB: setuid,
; setgid and the session the scheduler already maintains are what the
; existing frozen syscalls check, and a second copy that could disagree with
; them would be a bug generator. The token object is the handle-able view of
; that identity - what a program queries, what a later wave will let a thread
; impersonate - and it is refreshed from the TCB whenever it is read.
;
; Security descriptors are real: a DACL of ACEs, walked in canonical order,
; with deny winning over allow for the same SID.
;=======================================================================

; Privilege bits (SECURITY.md §3).
PRIV_DEBUG          equ 0x0001
PRIV_SESSION_CROSS  equ 0x0002
PRIV_TAKE_OWNERSHIP equ 0x0004
PRIV_BACKUP         equ 0x0008
PRIV_RESTORE        equ 0x0010
PRIV_SHUTDOWN       equ 0x0020
PRIV_AUDIT          equ 0x0040
PRIV_LOAD_DRIVER    equ 0x0080
PRIV_TCB            equ 0x0100

INTEGRITY_UNTRUSTED equ 0
INTEGRITY_LOW       equ 1
INTEGRITY_MEDIUM    equ 2
INTEGRITY_HIGH      equ 3
INTEGRITY_SYSTEM    equ 4
INTEGRITY_MAX       equ 5

; Security descriptor. Fixed-size and self-contained so it can be copied when
; an object is shared; the DACL is an inline array of fixed ACEs.
SD_OWNER        equ 0           ; dd uid
SD_GROUP        equ 4           ; dd gid
SD_INTEG        equ 8           ; dd integrity level
SD_NACES        equ 12          ; dd ACEs in the DACL
SD_ACES         equ 16          ; ACE array
SD_ACE          equ 8           ; db type, db sid, dw mask, dw pad
SD_ACE_MAX      equ 6
SD_SIZE         equ SD_ACES + (SD_ACE * SD_ACE_MAX)

ACE_TYPE        equ 0           ; 0 allow, 1 deny
ACE_SID         equ 1           ; uid & 0xFE, or ACE_WORLD
ACE_MASK        equ 2           ; dw
ACE_PAD         equ 6           ; dw
ACE_ALLOW       equ 0
ACE_DENY        equ 1
ACE_WORLD       equ 0xFF

;-----------------------------------------------------------------------
; obj_identity - who the caller is
;   Out: EAX = uid, EBX = gid, ECX = session, EDX = integrity level
;        ESI = 1 when the caller is the kernel/SYSTEM, 0 otherwise
;
; The kernel and the shell are SYSTEM: uid 0, session 0 and the highest
; integrity. That is the compatibility identity SECURITY.md §3 keeps for the
; frozen syscalls, not a special case hidden in the object manager - every
; check below reads it the same way it reads a user's.
;-----------------------------------------------------------------------
obj_identity:
        push edi
        call obj_ctx
        test eax, eax
        jz .oi_system
        mov edi, eax
        mov eax, [edi + TCB_EUID]
        mov ebx, [edi + TCB_EGID]
        mov ecx, [edi + TCB_SID]
        mov edx, INTEGRITY_MEDIUM
        test eax, eax                    ; uid 0 is Administrators + system
        jnz .oi_user
.oi_system:
        xor eax, eax
        xor ebx, ebx
        xor ecx, ecx
        mov edx, INTEGRITY_SYSTEM
        mov esi, 1
        pop edi
        ret
.oi_user:
        xor esi, esi
        pop edi
        ret

;-----------------------------------------------------------------------
; obj_has_priv - does the caller hold this privilege
;   In : EBX = privilege bit       Out: CF=0 yes, CF=1 no
;
; uid 0 holds them all, which is the documented meaning of the compatibility
; identity. Nothing else does in M1; a granted privilege list arrives with the
; service manager that assigns them.
;-----------------------------------------------------------------------
obj_has_priv:
        pushad
        call obj_identity
        mov [.ohp_sys], esi
        popad
        cmp dword [.ohp_sys], 0
        je .ohp_no
        clc
        ret
.ohp_no:
        stc
        ret
.ohp_sys: dd 0

;-----------------------------------------------------------------------
; obj_security_alloc - give an object its default descriptor
;   In : EAX = object
;   Out: EAX = 1 on success, 0 when out of memory
;
; The default grants the creator full control and everybody else the rights
; that do not touch the object's contents. That is SECURITY.md §5.3's "a new
; object gets the descriptor its type names": a created event is not public,
; and a program that wants it public says so.
;-----------------------------------------------------------------------
obj_security_alloc:
        pushad
        mov [.osa_obj], eax
        mov eax, [cache_security]
        call kmem_alloc
        test eax, eax
        jz .osa_fail
        mov edi, eax

        mov eax, [.osa_obj]
        mov [eax + OBJ_SECURITY], edi

        call obj_identity
        mov [edi + SD_OWNER], eax
        mov [edi + SD_GROUP], ebx
        mov [edi + SD_INTEG], INTEGRITY_MEDIUM

        ; ACE 0: the owner, everything the type defines.
        mov eax, [.osa_obj]
        mov eax, [eax + OBJ_TYPE]
        call obj_type_desc
        mov ecx, [eax + TT_ALL]
        mov [edi + SD_ACES + 0 * SD_ACE + ACE_MASK], ecx
        mov byte [edi + SD_ACES + 0 * SD_ACE + ACE_TYPE], ACE_ALLOW
        call obj_identity
        mov [edi + SD_ACES + 0 * SD_ACE + ACE_SID], al

        ; ACE 1: everybody else, query and synchronise only - plus the right
        ; to traverse, which is what a directory is for. Without it no
        ; unprivileged caller could walk the tree at all.
        mov ecx, RIGHT_QUERY_STATE | RIGHT_READ_ATTR | RIGHT_SYNCHRONIZE
        mov eax, [.osa_obj]
        mov eax, [eax + OBJ_TYPE]
        cmp eax, OBJ_T_DIRECTORY
        jne .osa_not_dir
        or ecx, RIGHT_EXECUTE
.osa_not_dir:
        mov [edi + SD_ACES + 1 * SD_ACE + ACE_MASK], ecx
        mov byte [edi + SD_ACES + 1 * SD_ACE + ACE_TYPE], ACE_ALLOW
        mov byte [edi + SD_ACES + 1 * SD_ACE + ACE_SID], ACE_WORLD

        mov dword [edi + SD_NACES], 2
        mov dword [esp + 28], 1
        popad
        ret
.osa_fail:
        mov dword [esp + 28], 0
        popad
        ret
.osa_obj: dd 0

;-----------------------------------------------------------------------
; obj_security_free - return an object's descriptor to its cache
;   In : EAX = object
;-----------------------------------------------------------------------
obj_security_free:
        pushad
        test eax, eax
        jz .osf_done
        mov edi, eax
        mov ebx, [edi + OBJ_SECURITY]
        test ebx, ebx
        jz .osf_done
        mov dword [edi + OBJ_SECURITY], 0
        mov eax, [cache_security]
        call kmem_free
.osf_done:
        popad
        ret

;-----------------------------------------------------------------------
; obj_access - the one access check
;   In : EAX = object, EBX = desired rights (may include MV_RIGHTS_ALL)
;   Out: CF=0 granted, EAX = the rights granted
;        CF=1 denied,   EAX = 0
;
; Order matters and follows SECURITY.md §5: NULL descriptor grants all;
; generic-all is mapped by the type; mandatory no-write-up is evaluated before
; the DACL and can only deny; then deny ACEs before allow ACEs; then the
; privilege that bypasses the check.
;-----------------------------------------------------------------------
obj_access:
        pushad
        mov [.oa_obj2], eax
        mov [.oa_desired], ebx

        ; 1. No descriptor: unprotected, so everything the type defines.
        mov eax, [.oa_obj2]
        mov eax, [eax + OBJ_SECURITY]
        test eax, eax
        jnz .oa_have_desc
        mov eax, [.oa_obj2]
        mov eax, [eax + OBJ_TYPE]
        call obj_type_desc
        mov ecx, [eax + TT_ALL]
        mov [esp + 28], ecx
        popad
        clc
        ret

.oa_have_desc:
        mov [.oa_sd], eax
        mov edi, eax

        ; 2. Map generic-all to the type's specific rights.
        mov ebx, [.oa_desired]
        test ebx, MV_RIGHTS_ALL
        jz .oa_specific
        mov eax, [.oa_obj2]
        mov eax, [eax + OBJ_TYPE]
        call obj_type_desc
        mov ecx, [eax + TT_ALL]
        mov ebx, ecx
        mov [.oa_desired], ebx
.oa_specific:

        ; 3. Mandatory policy: a write below the object's integrity is refused
        ;    before the DACL is consulted, and nothing but SE_TCB crosses it.
        mov ecx, ebx
        and ecx, RIGHT_WRITE_DATA | RIGHT_SET_STATE | RIGHT_DELETE | RIGHT_WRITE_ATTR
        jz .oa_dacl
        mov edi, [.oa_sd]
        mov edx, [edi + SD_INTEG]
        push edx                        ; keep the object's level
        call obj_identity               ; EDX = caller's level
        pop ecx                         ; ECX = object's level
        cmp edx, ecx
        jae .oa_dacl                    ; at or above the object's level
        ; Below it: a write-up. SE_TCB is the only thing that crosses.
        mov ebx, PRIV_TCB
        call obj_has_priv
        jc .oa_mandatory_denied
.oa_dacl:

        ; 4. Walk the DACL. Deny ACEs subtract, allow ACEs add; a SID matches
        ;    the caller's uid or the world entry.
        call obj_identity               ; EAX = uid, ESI = 1 for SYSTEM
        mov [.oa_uid], eax
        mov [.oa_system], esi
        mov edi, [.oa_sd]
        mov ecx, [edi + SD_NACES]
        cmp ecx, SD_ACE_MAX
        jbe .oa_naces_ok
        mov ecx, SD_ACE_MAX
.oa_naces_ok:
        xor edx, edx                    ; EDX = accumulated allow mask
        xor ebx, ebx                    ; EBX = accumulated deny mask
        lea esi, [edi + SD_ACES]
.oa_ace_loop:
        test ecx, ecx
        jz .oa_aces_done
        movzx eax, byte [esi + ACE_SID]
        cmp eax, ACE_WORLD
        je .oa_sid_matches
        cmp eax, [.oa_uid]
        jne .oa_ace_next
.oa_sid_matches:
        movzx eax, byte [esi + ACE_TYPE]
        cmp eax, ACE_DENY
        je .oa_ace_deny
        mov eax, [esi + ACE_MASK]
        or edx, eax
        jmp .oa_ace_next
.oa_ace_deny:
        mov eax, [esi + ACE_MASK]
        or ebx, eax
.oa_ace_next:
        add esi, SD_ACE
        dec ecx
        jmp .oa_ace_loop

.oa_aces_done:
        ; A deny wins over an allow for the same right.
        mov eax, edx
        not ebx
        and eax, ebx
        mov [.oa_granted], eax

        ; 6. The bypass privilege: SE_DEBUG reads any object (SECURITY.md §3).
        mov ecx, [.oa_desired]
        and eax, ecx
        cmp eax, ecx
        je .oa_grant

        mov ebx, PRIV_DEBUG
        call obj_has_priv
        jc .oa_deny
        ; Bypass grants what was asked for, and says so in the audit log.
        mov dword [obj_audit_last], BYPASS
        mov eax, [.oa_desired]
        mov [esp + 28], eax
        popad
        clc
        ret

.oa_grant:
        mov eax, [.oa_granted]
        mov [esp + 28], eax
        popad
        clc
        ret

.oa_deny:
        ; The failed right is recorded for the audit log; the caller is told
        ; only that it was refused, because the mask it did not get is not the
        ; caller's business beyond that.
        mov eax, [.oa_desired]
        mov ecx, [.oa_granted]
        not ecx
        and eax, ecx
        mov [obj_audit_last], eax
        mov dword [esp + 28], 0
        popad
        stc
        ret

.oa_mandatory_denied:
        mov dword [obj_audit_last], MANDATORY
        mov dword [esp + 28], 0
        popad
        stc
        ret

.oa_obj2:    dd 0
.oa_desired: dd 0
.oa_sd:      dd 0
.oa_uid:     dd 0
.oa_system:  dd 0
.oa_granted: dd 0

BYPASS          equ 1           ; audit reasons
MANDATORY       equ 2

;=======================================================================
; The namespace
;
; One tree, case-insensitive and case-preserving. A directory holds children
; on a singly-linked sibling list; every object carries the links, so anything
; can be given a name. Lookup walks one segment at a time, which is what makes
; "longest matching named object wins" fall out rather than be arranged.
;=======================================================================

SL_TARGET       equ NS_RESERVED + 0
SL_TARGET_MAX   equ 176

;-----------------------------------------------------------------------
; obj_ns_lower - fold one ASCII character to lower case
;   In : AL            Out: AL folded, other registers untouched
;
; Only ASCII is folded. A name stored in another encoding keeps its bytes and
; compares against them; the fixed table in c/rt/utf.c is what a later wave
; will use when names become char16.
;-----------------------------------------------------------------------
obj_ns_lower:
        cmp al, 'A'
        jb .onl_done
        cmp al, 'Z'
        ja .onl_done
        add al, 'a' - 'A'
.onl_done:
        ret

;-----------------------------------------------------------------------
; obj_ns_seg_match - does an object's name equal a segment
;   In : ESI = segment (not terminated by NUL), ECX = length,
;        EDI = object pointer
;   Out: EAX = 1 on match, 0 on no match
;
; The segment must match the whole stored name, so "Event" does not match an
; object called "EventLog". Case is folded on both sides.
;-----------------------------------------------------------------------
obj_ns_seg_match:
        pushad
        mov dword [.onsm_r], 0
        cmp ecx, OBJ_NAME_MAX
        ja .onsm_done
        cmp dword [edi + OBJ_NAME_OFF], 0
        je .onsm_done
        mov edx, edi
        add edx, [edi + OBJ_NAME_OFF]   ; object's stored name
        xor ebx, ebx
.onsm_loop:
        cmp ebx, ecx
        jae .onsm_end
        mov al, [esi + ebx]
        call obj_ns_lower
        mov [.onsm_c], al
        mov al, [edx + ebx]
        call obj_ns_lower
        cmp al, [.onsm_c]
        jne .onsm_done
        inc ebx
        jmp .onsm_loop
.onsm_end:
        cmp byte [edx + ebx], 0         ; the stored name ends here
        jne .onsm_done
        mov dword [.onsm_r], 1
.onsm_done:
        mov eax, [.onsm_r]
        mov [esp + 28], eax
        popad
        ret
.onsm_r: dd 0
.onsm_c: db 0

;-----------------------------------------------------------------------
; obj_ns_find_child - look a segment up among a directory's children
;   In : EAX = directory, EBX = segment pointer, ECX = length
;   Out: EAX = child (not referenced), or 0
;-----------------------------------------------------------------------
obj_ns_find_child:
        pushad
        test eax, eax
        jz .onfc_none
        mov edi, eax
        mov edi, [edi + OBJ_BODY + NS_CHILD]
.onfc_loop:
        test edi, edi
        jz .onfc_none
        mov esi, ebx
        mov [.onfc_next], edi
        call obj_ns_seg_match
        test eax, eax
        jnz .onfc_hit
        mov edi, [.onfc_next]
        mov edi, [edi + OBJ_BODY + NS_NEXT]
        jmp .onfc_loop
.onfc_hit:
        mov eax, [.onfc_next]
        mov [esp + 28], eax
        popad
        ret
.onfc_none:
        mov dword [esp + 28], 0
        popad
        ret
.onfc_next: dd 0

;-----------------------------------------------------------------------
; obj_ns_unlink - take an object's name away
;   In : EAX = object (0 and unnamed objects are ignored)
;
; Runs at destruction, so a freed body can never be reached by name. Does not
; touch reference counts: a name is not a reference (OBJECTS.md §7).
;-----------------------------------------------------------------------
obj_ns_unlink:
        pushad
        test eax, eax
        jz .onu_done
        mov edi, eax
        test dword [edi + OBJ_BODY + NS_FLAGS], NF_IN_NS
        jz .onu_done
        mov esi, [edi + OBJ_BODY + NS_PARENT]
        and dword [edi + OBJ_BODY + NS_FLAGS], ~NF_IN_NS
        mov dword [edi + OBJ_BODY + NS_PARENT], 0
        test esi, esi
        jz .onu_done

        ; Unlink from the parent's child list.
        cmp dword [esi + OBJ_BODY + NS_CHILD], edi
        jne .onu_not_head
        mov eax, [edi + OBJ_BODY + NS_NEXT]
        mov [esi + OBJ_BODY + NS_CHILD], eax
        jmp .onu_clear
.onu_not_head:
        mov eax, [esi + OBJ_BODY + NS_CHILD]
.onu_scan:
        test eax, eax
        jz .onu_clear
        cmp [eax + OBJ_BODY + NS_NEXT], edi
        je .onu_found
        mov eax, [eax + OBJ_BODY + NS_NEXT]
        jmp .onu_scan
.onu_found:
        mov ecx, [edi + OBJ_BODY + NS_NEXT]
        mov [eax + OBJ_BODY + NS_NEXT], ecx
.onu_clear:
        mov dword [edi + OBJ_BODY + NS_NEXT], 0
.onu_done:
        popad
        ret

;-----------------------------------------------------------------------
; obj_ns_link - give an object a name in a directory
;   In : EAX = object, EBX = name pointer, ECX = parent directory
;   Out: EAX = status (0 ok)
;
; Creating a name is a write to the directory, so WRITE_DATA on the parent is
; required unless the caller is the kernel. A duplicate name in the same
; directory is refused: the dispositions are the caller's business, and this
; is the primitive underneath them.
;-----------------------------------------------------------------------
obj_ns_link:
        pushad
        mov [.onl_obj], eax
        mov [.onl_parent], ecx
        mov [.onl_name_ptr], ebx
        mov dword [.onl_status], 0

        ; Length of the name, and the documented limits.
        mov edi, ebx
        xor ecx, ecx
.onl_count:
        cmp byte [edi + ecx], 0
        je .onl_len_done
        inc ecx
        cmp ecx, OBJ_NAME_MAX
        ja .onl_too_long
        jmp .onl_count
.onl_too_long:
        mov dword [.onl_status], MV_STATUS_INVALID_PARAMETER
        jmp .onl_ret
.onl_len_done:
        test ecx, ecx
        jz .onl_too_long
        mov [.onl_len], ecx

        ; Depth: walk up from the parent, refusing a tree deeper than the
        ; documented limit rather than letting a program build one.
        mov eax, [.onl_parent]
        xor ecx, ecx
.onl_depth:
        test eax, eax
        jz .onl_depth_ok
        inc ecx
        cmp ecx, OBJ_MAX_DEPTH
        ja .onl_too_deep
        mov eax, [eax + OBJ_BODY + NS_PARENT]
        jmp .onl_depth
.onl_too_deep:
        mov dword [.onl_status], MV_STATUS_INVALID_PARAMETER
        jmp .onl_ret
.onl_depth_ok:

        ; WRITE_DATA on the directory.
        mov eax, [.onl_parent]
        mov ebx, RIGHT_WRITE_DATA
        call obj_access
        jc .onl_denied

        ; No duplicate in this directory.
        mov eax, [.onl_parent]
        mov ebx, [.onl_name_ptr]
        mov ecx, [.onl_len]
        call obj_ns_find_child
        test eax, eax
        jnz .onl_exists

        ; Copy the name into the object.
        mov edi, [.onl_obj]
        mov edi, [edi + OBJ_NAME_OFF]
        mov eax, [.onl_obj]
        add edi, eax
        mov esi, [.onl_name_ptr]
        mov ecx, [.onl_len]
        cld
        rep movsb
        mov byte [edi], 0

        mov edi, [.onl_obj]
        mov eax, [.onl_len]
        mov [edi + OBJ_NAME_LEN], ax
        or dword [edi + OBJ_FLAGS], OF_NAMED

        ; Push onto the front of the parent's child list.
        mov ecx, [.onl_parent]
        mov eax, [.onl_obj]
        mov edx, [ecx + OBJ_BODY + NS_CHILD]
        mov [eax + OBJ_BODY + NS_NEXT], edx
        mov [ecx + OBJ_BODY + NS_CHILD], eax
        mov [eax + OBJ_BODY + NS_PARENT], ecx
        or dword [eax + OBJ_BODY + NS_FLAGS], NF_IN_NS
        jmp .onl_ret

.onl_denied:
        mov dword [.onl_status], MV_STATUS_ACCESS_DENIED
        jmp .onl_ret
.onl_exists:
        mov dword [.onl_status], MV_STATUS_INVALID_PARAMETER
.onl_ret:
        mov eax, [.onl_status]
        mov [esp + 28], eax
        popad
        ret
.onl_obj:      dd 0
.onl_parent:   dd 0
.onl_status:   dd 0
.onl_len:      dd 0
.onl_name_ptr: dd 0

;-----------------------------------------------------------------------
; obj_ns_walk - resolve a name to an object without taking a reference
;   In : EAX = NUL-terminated name, '\'-separated, leading '\' optional
;   Out: EAX = object, or 0;  EDX = status
;
; Every directory crossed must grant traverse (RIGHT_EXECUTE), and a name that
; enters another session's subtree is refused without SE_SESSION_CROSS. The
; object returned is not referenced: the caller takes the reference it wants
; while it holds the only CPU.
;-----------------------------------------------------------------------
obj_ns_walk:
        pushad
        mov esi, eax
        cmp byte [esi], '\'
        jne .onw_session
        inc esi
.onw_session:
        mov [.onw_segptr], esi

        ; A path into \Sessions\<n>\ is a namespace boundary, not just a
        ; directory: refuse it for a caller in another session unless it holds
        ; SE_SESSION_CROSS.
        mov eax, obj_str_sessions
        mov ebx, esi
        call obj_str_prefix_i
        test eax, eax
        jz .onw_start
        lea ebx, [esi + 9]              ; past "Sessions\"
        xor ecx, ecx
        xor edx, edx
.onw_sess_digits:
        movzx eax, byte [ebx]
        cmp al, '0'
        jb .onw_sess_done
        cmp al, '9'
        ja .onw_sess_done
        imul edx, edx, 10
        sub eax, '0'
        add edx, eax
        inc ebx
        inc ecx
        cmp ecx, 6
        jb .onw_sess_digits
.onw_sess_done:
        test ecx, ecx
        jz .onw_start                   ; "Sessions\x" names no session
        cmp byte [ebx], '\'
        jne .onw_start                  ; the directory itself is reachable
        mov [.onw_needed], edx          ; obj_identity clobbers EDX
        call obj_identity               ; ECX = the caller's session
        cmp [.onw_needed], ecx
        je .onw_start
        mov ebx, PRIV_SESSION_CROSS
        call obj_has_priv
        jnc .onw_start
        mov dword [.onw_status], MV_STATUS_ACCESS_DENIED
        jmp .onw_ret

.onw_start:
        mov eax, [obj_root]
        mov [.onw_cur], eax
        mov dword [.onw_depth], 0

.onw_seg:
        mov esi, [.onw_segptr]
        movzx eax, byte [esi]
        test al, al
        jz .onw_found

        xor ecx, ecx
.onw_seglen_loop:
        movzx eax, byte [esi + ecx]
        test al, al
        jz .onw_seglen_done
        cmp al, '\'
        je .onw_seglen_done
        inc ecx
        cmp ecx, OBJ_NAME_MAX
        ja .onw_bad
        jmp .onw_seglen_loop
.onw_seglen_done:
        test ecx, ecx
        jnz .onw_descend
        inc esi                         ; empty segment: step over the separator
        mov [.onw_segptr], esi
        jmp .onw_seg

.onw_descend:
        mov [.onw_seglen], ecx
        inc dword [.onw_depth]
        cmp dword [.onw_depth], OBJ_MAX_DEPTH
        ja .onw_bad

        mov eax, [.onw_cur]
        mov ebx, RIGHT_EXECUTE
        call obj_access                 ; traverse the directory we are leaving
        jc .onw_denied

        mov eax, [.onw_cur]
        mov ebx, [.onw_segptr]
        mov ecx, [.onw_seglen]
        call obj_ns_find_child
        test eax, eax
        jz .onw_notfound

        ; A symlink is followed to its target, which is absolute in this wave.
        test dword [eax + OBJ_BODY + NS_FLAGS], NF_SYMLINK
        jz .onw_advance
        inc dword [.onw_links]
        cmp dword [.onw_links], SYMLINK_MAX
        ja .onw_notfound
        lea esi, [eax + OBJ_BODY + SL_TARGET]
        cmp byte [esi], '\'
        jne .onw_bad                    ; relative targets are not built yet
        mov [.onw_segptr], esi
        jmp .onw_start

.onw_advance:
        mov [.onw_cur], eax
        mov esi, [.onw_segptr]
        add esi, [.onw_seglen]
        cmp byte [esi], '\'
        jne .onw_advance_nosep
        inc esi
.onw_advance_nosep:
        mov [.onw_segptr], esi
        jmp .onw_seg

.onw_found:
        mov eax, [.onw_cur]
        mov [esp + 28], eax
        xor edx, edx
        popad
        ret
.onw_notfound:
        mov dword [.onw_status], MV_STATUS_INVALID_PARAMETER
        jmp .onw_ret
.onw_bad:
        mov dword [.onw_status], MV_STATUS_INVALID_PARAMETER
        jmp .onw_ret
.onw_denied:
        mov dword [.onw_status], MV_STATUS_ACCESS_DENIED
.onw_ret:
        mov dword [esp + 28], 0
        mov edx, [.onw_status]
        mov [esp + 20], edx             ; popad restores EDX: use its slot
        popad
        ret
.onw_segptr: dd 0
.onw_cur:    dd 0
.onw_status: dd 0
.onw_seglen: dd 0
.onw_depth:  dd 0
.onw_links:  dd 0
.onw_needed: dd 0

;-----------------------------------------------------------------------
; obj_str_prefix_i - case-insensitive "does EBX start with EAX's string"
;   In : EAX = prefix, EBX = candidate
;   Out: EAX = 1 when it does, 0 when it does not
;-----------------------------------------------------------------------
obj_str_prefix_i:
        pushad
        mov esi, eax
        mov edi, ebx
        xor ecx, ecx
.ospi_loop:
        mov al, [esi + ecx]
        test al, al
        jz .ospi_yes
        call obj_ns_lower
        mov [.ospi_c], al
        mov al, [edi + ecx]
        call obj_ns_lower
        cmp al, [.ospi_c]
        jne .ospi_no
        inc ecx
        jmp .ospi_loop
.ospi_yes:
        mov dword [esp + 28], 1
        popad
        ret
.ospi_no:
        mov dword [esp + 28], 0
        popad
        ret
.ospi_c: db 0


;=======================================================================
; Waiting
;
; A wait is a set: one or many objects, any or all of them, with a timeout.
; The waiting task queues on every object in the set and is completed by
; whoever signals, so a signal never has to find a task that is not looking.
;
; Two-phase, because a wait that must commit only at the end cannot consume
; as it goes: obj_wait_eval decides without side effects, and obj_wait_consume
; commits once. That is what lets WaitAll over an auto-reset event behave -
; the event is taken only when the whole set is satisfied, not before.
;
; The queue is FIFO per object, so the first task to wait on an auto-reset
; event is the first to get it.
;=======================================================================

WAITK_ONE       equ 1
WAITK_ANY       equ 2
WAITK_ALL       equ 3

WS_KIND         equ 0
WS_COUNT        equ 4
WS_INDEX        equ 8
WS_TCB          equ 12
WS_OBJS         equ 16           ; dd[WAIT_MAX_HANDLES]
WS_RECS         equ WS_OBJS + (WAIT_MAX_HANDLES * 4)
WS_SIZE         equ WS_RECS + (WAIT_MAX_HANDLES * 4)

WT_NEXT         equ 0
WT_SET          equ 4
WT_INDEX        equ 8
WT_ENTRY        equ 16

;-----------------------------------------------------------------------
; obj_waitset - the calling task's wait set, allocated on first use
;   Out: EAX = wait set, or 0 when out of memory
;-----------------------------------------------------------------------
obj_waitset:
        pushad
        call obj_ctx
        test eax, eax
        jz .ows_none
        mov edi, eax
        mov eax, [edi + TCB_WAITSET]
        test eax, eax
        jnz .ows_have
        mov eax, [cache_waitset]
        call kmem_alloc
        test eax, eax
        jz .ows_none
        mov [edi + TCB_WAITSET], eax
.ows_have:
        mov [esp + 28], eax
        popad
        ret
.ows_none:
        mov dword [esp + 28], 0
        popad
        ret

;-----------------------------------------------------------------------
; obj_waitable - can this object be waited on
;   In : EAX = object      Out: EAX = 1 / 0
;-----------------------------------------------------------------------
obj_waitable:
        pushad
        test eax, eax
        jz .ow_no
        mov ecx, eax
        mov eax, [ecx + OBJ_TYPE]
        call obj_type_desc
        test eax, eax
        jz .ow_no
        test dword [eax + TT_FLAGS], TF_WAITABLE
        jz .ow_no
        mov dword [esp + 28], 1
        popad
        ret
.ow_no:
        mov dword [esp + 28], 0
        popad
        ret

;-----------------------------------------------------------------------
; obj_wait_eval - is a wait set satisfied, without taking anything
;   In : EAX = wait set
;   Out: EAX = 1 satisfied / 0 not;  EDX = index that satisfied it
;-----------------------------------------------------------------------
obj_wait_eval:
        pushad
        mov edi, eax
        mov [.owe_set], edi
        mov ecx, [edi + WS_COUNT]
        xor esi, esi
        xor edx, edx                    ; count of signalled slots seen
        mov dword [.owe_first], -1
.owe_loop:
        cmp esi, ecx
        jae .owe_done
        mov eax, [edi + WS_OBJS + esi*4]
        mov eax, [eax + OBJ_TD_SIGNAL]
        test eax, eax
        jz .owe_next
        inc edx
        cmp dword [.owe_first], -1
        jne .owe_next
        mov [.owe_first], esi
.owe_next:
        inc esi
        jmp .owe_loop
.owe_done:
        mov ecx, [.owe_set]
        mov eax, [ecx + WS_KIND]
        cmp eax, WAITK_ALL
        je .owe_all
        ; one / any: a single signalled slot satisfies it
        test edx, edx
        jz .owe_no
        mov edx, [.owe_first]
        mov [esp + 20], edx             ; popad restores EDX: use its slot
        mov dword [esp + 28], 1
        popad
        ret
.owe_all:
        mov eax, [ecx + WS_COUNT]
        cmp edx, eax
        jb .owe_no
        mov edx, [.owe_first]
        mov [esp + 20], edx
        mov dword [esp + 28], 1
        popad
        ret
.owe_no:
        xor edx, edx
        mov dword [esp + 28], 0
        popad
        ret
.owe_set:   dd 0
.owe_first: dd 0

;-----------------------------------------------------------------------
; obj_wait_consume - commit a satisfied wait
;   In : EAX = wait set, EDX = index to consume
;
; Auto-reset events clear, semaphores count down, mutants take an owner. A
; manual-reset event, a timer and a process stay signalled: that is the
; difference between the two kinds of object and not an optimisation.
;-----------------------------------------------------------------------
obj_wait_consume:
        pushad
        mov edi, eax
        mov [.owc_set], edi
        mov ecx, [edi + WS_KIND]
        cmp ecx, WAITK_ALL
        jne .owc_one
        ; WaitAll consumes every slot in the set.
        xor esi, esi
.owc_all_loop:
        cmp esi, [edi + WS_COUNT]
        jae .owc_done
        mov eax, [edi + WS_OBJS + esi*4]
        call obj_consume_one
        inc esi
        jmp .owc_all_loop
.owc_one:
        mov eax, [edi + WS_OBJS + edx*4]
        call obj_consume_one
.owc_done:
        popad
        ret
.owc_set: dd 0

;-----------------------------------------------------------------------
; obj_consume_one - take one object's signal by consuming it
;   In : EAX = object
;-----------------------------------------------------------------------
obj_consume_one:
        pushad
        test eax, eax
        jz .oco_done
        mov edi, eax
        mov eax, [edi + OBJ_TYPE]
        cmp eax, OBJ_T_EVENT
        je .oco_event
        cmp eax, OBJ_T_SEMAPHORE
        je .oco_sem
        cmp eax, OBJ_T_MUTANT
        je .oco_mutant
        ; A manual-reset event, a timer and a process keep their signal.
        jmp .oco_done
.oco_event:
        cmp dword [edi + OBJ_BODY + EV_MANUAL], 0
        jne .oco_done                   ; manual reset stays signalled
        mov dword [edi + OBJ_BODY + EV_STATE], 0
        mov dword [edi + OBJ_TD_SIGNAL], 0
        jmp .oco_done
.oco_sem:
        mov eax, [edi + OBJ_BODY + SE_COUNT]
        test eax, eax
        jz .oco_done
        dec eax
        mov [edi + OBJ_BODY + SE_COUNT], eax
        test eax, eax
        jnz .oco_done
        mov dword [edi + OBJ_TD_SIGNAL], 0
        jmp .oco_done
.oco_mutant:
        mov dword [edi + OBJ_BODY + MU_OWNER], 1     ; taken; owner set by caller
        mov dword [edi + OBJ_TD_SIGNAL], 0
.oco_done:
        popad
        ret

;-----------------------------------------------------------------------
; obj_wait_enqueue - queue the calling task on every object in its set
;   In : EAX = wait set, EBX = TCB
;   Out: EAX = 1 queued, 0 out of memory (nothing left queued)
;-----------------------------------------------------------------------
obj_wait_enqueue:
        pushad
        mov edi, eax
        mov [.owe2_set], edi
        mov [.owe2_tcb], ebx
        mov [edi + WS_TCB], ebx         ; the signaller finds the task through this
        mov ecx, [edi + WS_COUNT]
        xor esi, esi
.owq_loop:
        cmp esi, ecx
        jae .owq_ok
        mov eax, [cache_waiter]
        call kmem_alloc
        test eax, eax
        jz .owq_fail
        mov [edi + WS_RECS + esi*4], eax
        mov edx, [.owe2_set]
        mov [eax + WT_SET], edx
        mov [eax + WT_INDEX], esi
        mov dword [eax + WT_NEXT], 0

        ; Append to the object's waiter list: FIFO, so the first task to wait
        ; is the first to be offered an auto-reset signal.
        mov edx, [edi + WS_OBJS + esi*4]
        mov ebx, edx
        add ebx, OBJ_WAITERS
.owq_tail:
        mov edx, [ebx]
        test edx, edx
        jz .owq_link
        lea ebx, [edx + WT_NEXT]
        jmp .owq_tail
.owq_link:
        mov [ebx], eax
        inc esi
        jmp .owq_loop
.owq_ok:
        mov dword [esp + 28], 1
        popad
        ret
.owq_fail:
        ; Undo the links taken so far so nothing points at freed records.
        mov ecx, esi
        mov edi, [.owe2_set]
        xor esi, esi
.owq_undo:
        cmp esi, ecx
        jae .owq_undo_done
        mov ebx, [edi + WS_RECS + esi*4]
        test ebx, ebx
        jz .owq_undo_next
        mov eax, [cache_waiter]
        call kmem_free
        mov dword [edi + WS_RECS + esi*4], 0
.owq_undo_next:
        inc esi
        jmp .owq_undo
.owq_undo_done:
        mov dword [esp + 28], 0
        popad
        ret
.owe2_set: dd 0
.owe2_tcb: dd 0

;-----------------------------------------------------------------------
; obj_wait_dequeue - take the calling task's records off every object's list
;   In : EAX = wait set
;-----------------------------------------------------------------------
obj_wait_dequeue:
        pushad
        mov edi, eax
        mov [.owd_set], edi
        xor esi, esi
.owd_loop:
        cmp esi, [edi + WS_COUNT]
        jae .owd_done
        mov ebx, [edi + WS_RECS + esi*4]
        test ebx, ebx
        jz .owd_next
        mov eax, [edi + WS_OBJS + esi*4]
        lea edi, [eax + OBJ_WAITERS]
.owd_scan:
        mov eax, [edi]
        test eax, eax
        jz .owd_next
        cmp eax, ebx
        je .owd_unhook
        lea edi, [eax + WT_NEXT]
        jmp .owd_scan
.owd_unhook:
        mov eax, [ebx + WT_NEXT]
        mov [edi], eax
        mov eax, [cache_waiter]
        call kmem_free
        mov edi, [.owd_set]
        mov dword [edi + WS_RECS + esi*4], 0
.owd_next:
        mov edi, [.owd_set]
        inc esi
        jmp .owd_loop
.owd_done:
        popad
        ret
.owd_set: dd 0

;-----------------------------------------------------------------------
; obj_signal - mark an object signalled and complete whoever it satisfies
;   In : EAX = object
;
; The object is marked first, so a waiter that is completed sees the state it
; was completed for. Then the FIFO waiter list is offered the signal, one
; waiter at a time, and the walk restarts after each success because the list
; it is walking has just changed.
;-----------------------------------------------------------------------
obj_signal:
        pushad
        test eax, eax
        jz .osig_done
        mov dword [eax + OBJ_TD_SIGNAL], 1
        call obj_satisfy_waiters
.osig_done:
        popad
        ret

;-----------------------------------------------------------------------
; obj_reset - clear an object's signal
;   In : EAX = object
;-----------------------------------------------------------------------
obj_reset:
        pushad
        test eax, eax
        jz .ores_done
        mov dword [eax + OBJ_TD_SIGNAL], 0
        mov dword [eax + OBJ_BODY + EV_STATE], 0
.ores_done:
        popad
        ret

;-----------------------------------------------------------------------
; obj_satisfy_waiters - offer an object's signal to its waiters, in order
;   In : EAX = object
;-----------------------------------------------------------------------
obj_satisfy_waiters:
        pushad
        mov [.osw_obj], eax
.osw_restart:
        mov edi, [.osw_obj]
        mov ebx, [edi + OBJ_WAITERS]
.osw_walk:
        test ebx, ebx
        jz .osw_done
        mov [.osw_next], ebx
        mov edi, [ebx + WT_SET]
        test edi, edi
        jz .osw_done

        ; A record whose owner is no longer blocked on this set is stale; it
        ; cannot happen through this code, and skipping it is cheaper than
        ; trusting it.
        mov ecx, [edi + WS_TCB]
        test ecx, ecx
        jz .osw_advance
        cmp dword [ecx + TCB_STATE], TASK_BLOCKED
        jne .osw_advance
        cmp [ecx + TCB_WAITOBJ], edi
        jne .osw_advance

        mov eax, edi
        call obj_wait_eval
        test eax, eax
        jz .osw_advance

        ; Satisfied: commit, then hand the task its index and make it runnable.
        mov edi, [.osw_next]
        mov edi, [edi + WT_SET]
        call obj_wait_consume
        mov edi, [.osw_next]
        mov edi, [edi + WT_SET]
        mov ecx, [edi + WS_TCB]
        mov [edi + WS_INDEX], edx
        mov [ecx + TCB_WAITRES], edx
        mov eax, [ecx + TCB_ESP]
        test eax, eax
        jz .osw_no_frame
        mov [eax + 28], edx             ; the blocked frame's saved EAX
.osw_no_frame:
        mov dword [ecx + TCB_WAKEUP], 0
        mov eax, edi
        call obj_wait_dequeue
        mov dword [ecx + TCB_STATE], TASK_READY
        jmp .osw_restart

.osw_advance:
        mov ebx, [.osw_next]
        mov ebx, [ebx + WT_NEXT]
        jmp .osw_walk
.osw_done:
        popad
        ret
.osw_obj:  dd 0
.osw_next: dd 0

;-----------------------------------------------------------------------
; obj_wait_timeouts - complete waits whose deadline has passed
;
; Called from irq_timer before sched_wake_sleepers. It clears TCB_WAKEUP when
; it handles a wait, so the sleeper scan cannot also wake the task without
; giving it a result - a task woken with no result would return the stale
; value it blocked with.
;-----------------------------------------------------------------------
obj_wait_timeouts:
        pushad
        mov edi, task_table
        xor ecx, ecx
.owt_loop:
        cmp ecx, MAX_TASKS
        jae .owt_done
        cmp dword [edi + TCB_STATE], TASK_BLOCKED
        jne .owt_next
        mov eax, [edi + TCB_WAITOBJ]
        test eax, eax
        jz .owt_next                    ; not a wait (a sleep or a read)
        mov eax, [edi + TCB_WAKEUP]
        test eax, eax
        jz .owt_next                    ; indefinite
        cmp [tick_count], eax
        jb .owt_next
        ; Expired: give it the timeout result, unqueue it and run it.
        mov eax, [edi + TCB_WAITOBJ]
        mov [.owt_set], eax
        mov dword [edi + TCB_WAKEUP], 0
        mov dword [edi + TCB_WAITRES], WAIT_TIMEOUT
        mov edx, [edi + TCB_ESP]
        test edx, edx
        jz .owt_no_frame
        mov dword [edx + 28], WAIT_TIMEOUT
.owt_no_frame:
        mov [.owt_tcb], edi
        push ecx
        push edi
        mov eax, [.owt_set]
        call obj_wait_dequeue
        pop edi
        pop ecx
        mov dword [edi + TCB_STATE], TASK_READY
.owt_next:
        add edi, TCB_SIZE
        inc ecx
        jmp .owt_loop
.owt_done:
        popad
        ret
.owt_set: dd 0
.owt_tcb: dd 0

;-----------------------------------------------------------------------
; obj_yield_cpu - hand the CPU to the next READY task
;   In : EDI = our TCB, already BLOCKED with TCB_ESP saved
;   Out: does not return when a task was ready; CF=1 when none was
;
; The caller is a blocked syscall, so returning here would mean waiting for a
; signal from a task that does not exist. CF=1 says so and the caller turns it
; into a timeout.
;-----------------------------------------------------------------------
obj_yield_cpu:
        pushad
        ; Our index, from our TCB.
        mov eax, edi
        sub eax, task_table
        xor edx, edx
        mov ecx, TCB_SIZE
        div ecx
        mov esi, eax                    ; our index
        mov [.oyc_me], edi
        mov ecx, MAX_TASKS
.oyc_scan:
        inc esi
        cmp esi, MAX_TASKS
        jb .oyc_check
        xor esi, esi
.oyc_check:
        mov eax, esi
        imul eax, TCB_SIZE
        lea ebx, [task_table + eax]
        cmp dword [ebx + TCB_STATE], TASK_READY
        je .oyc_switch
        dec ecx
        jnz .oyc_scan
        popad
        stc
        ret
.oyc_switch:
        mov edi, [.oyc_me]
        call sched_swap_fd_tables       ; EDI = outgoing, EBX = incoming
        call sched_swap_fpu
        mov dword [ebx + TCB_STATE], TASK_RUNNING
        mov [task_current], esi
        mov eax, ebx
        call sched_apply_cr3
        mov eax, [ebx + TCB_KSTACK]
        mov [tss_struct + 4], eax
        mov esp, [ebx + TCB_ESP]
        popad
        iret
.oyc_me: dd 0


;=======================================================================
; Initialisation
;=======================================================================

;-----------------------------------------------------------------------
; obj_new_named - create a named object as the kernel, with an integrity level
;   In : EAX = type, EBX = name, ECX = parent directory, EDX = integrity
;   Out: EAX = object (refs = 1) or 0
;
; Used only by obj_init, which runs before any task exists and therefore
; always as SYSTEM. A failure here is a boot fault, not a runtime refusal.
;-----------------------------------------------------------------------
obj_new_named:
        pushad
        mov [.onn_type], eax
        mov [.onn_name], ebx
        mov [.onn_parent], ecx
        mov [.onn_integ], edx

        call obj_alloc
        test eax, eax
        jz .onn_fail
        mov [.onn_obj], eax

        call obj_security_alloc
        test eax, eax
        jz .onn_fail
        ; The descriptor was just built; set the level the caller asked for.
        mov eax, [.onn_obj]
        mov eax, [eax + OBJ_SECURITY]
        mov edx, [.onn_integ]
        mov [eax + SD_INTEG], edx

        mov eax, [.onn_obj]
        mov ebx, [.onn_name]
        mov ecx, [.onn_parent]
        call obj_ns_link
        mov [.onn_status], eax
        pushad
        mov eax, [esp + 28]
        popad
        mov eax, [.onn_status]
        test eax, eax
        jnz .onn_fail

        mov eax, [.onn_obj]
        or dword [eax + OBJ_FLAGS], OF_PERMANENT
        mov [esp + 28], eax
        popad
        ret
.onn_fail:
        mov dword [esp + 28], 0
        popad
        ret
.onn_type:   dd 0
.onn_name:   dd 0
.onn_parent: dd 0
.onn_obj:    dd 0
.onn_integ:  dd 0
.onn_status: dd 0

;-----------------------------------------------------------------------
; obj_make_world_all - give an object's DACL a world entry with every right
;   In : EAX = object
;
; Only the kernel's own fixtures use this. It exists so a test can tell the
; mandatory policy apart from the DACL: when the DACL allows the write and the
; write is still refused, the refusal was the integrity rule.
;-----------------------------------------------------------------------
obj_make_world_all:
        pushad
        mov [.omwa_obj], eax
        test eax, eax
        jz .omwa_done
        mov edi, [eax + OBJ_SECURITY]
        test edi, edi
        jz .omwa_done
        mov eax, [.omwa_obj]
        mov eax, [eax + OBJ_TYPE]
        call obj_type_desc
        mov ecx, [eax + TT_ALL]
        mov edi, [.omwa_obj]
        mov edi, [edi + OBJ_SECURITY]
        mov [edi + SD_ACES + 1 * SD_ACE + ACE_MASK], ecx
        mov byte [edi + SD_ACES + 1 * SD_ACE + ACE_TYPE], ACE_ALLOW
        mov byte [edi + SD_ACES + 1 * SD_ACE + ACE_SID], ACE_WORLD
.omwa_done:
        popad
        ret
.omwa_obj: dd 0

;-----------------------------------------------------------------------
; obj_init - build the namespace skeleton
;   Out: EAX = 1 on success, 0 on failure
;
; Creates the tree OBJECTS.md §5 names. The directories are SYSTEM integrity,
; so an unprivileged process can traverse them but cannot add to them - the
; namespace's shape is not a program's to change. \KernelObjects\Lobby is the
; deliberate exception: its DACL allows everyone everything, and its integrity
; is SYSTEM, which is how the mandatory rule becomes observable rather than
; theoretical.
;-----------------------------------------------------------------------
obj_init:
        pushad

        ; The root: a directory with no name. Every path starts here.
        mov eax, OBJ_T_DIRECTORY
        call obj_alloc
        test eax, eax
        jz .oi_fail
        mov [obj_root], eax
        call obj_security_alloc
        test eax, eax
        jz .oi_fail
        mov eax, [obj_root]
        or dword [eax + OBJ_FLAGS], OF_PERMANENT
        mov eax, [eax + OBJ_SECURITY]
        mov dword [eax + SD_INTEG], INTEGRITY_SYSTEM

        ; The five top-level directories.
        mov esi, obj_init_dirs
.oi_top:
        mov ebx, [esi]
        test ebx, ebx
        jz .oi_after_top
        mov eax, OBJ_T_DIRECTORY
        mov ecx, [obj_root]
        mov edx, INTEGRITY_SYSTEM
        call obj_new_named
        test eax, eax
        jz .oi_fail
        mov edx, [esi + 4]
        test edx, edx
        jz .oi_top_next
        mov [edx], eax                  ; remember the well-known ones
.oi_top_next:
        add esi, 8
        jmp .oi_top

.oi_after_top:
        ; \Sessions\0 and \Sessions\1, each with BaseNamedObjects.
        mov eax, OBJ_T_DIRECTORY
        mov ebx, obj_init_s0
        mov ecx, [obj_sessions]
        mov edx, INTEGRITY_SYSTEM
        call obj_new_named
        test eax, eax
        jz .oi_fail
        mov [obj_session0], eax

        mov eax, OBJ_T_DIRECTORY
        mov ebx, obj_init_bno
        mov ecx, [obj_session0]
        mov edx, INTEGRITY_MEDIUM
        call obj_new_named
        test eax, eax
        jz .oi_fail
        mov [obj_base0], eax

        mov eax, OBJ_T_DIRECTORY
        mov ebx, obj_init_s1
        mov ecx, [obj_sessions]
        mov edx, INTEGRITY_SYSTEM
        call obj_new_named
        test eax, eax
        jz .oi_fail
        mov [obj_session1], eax

        mov eax, OBJ_T_DIRECTORY
        mov ebx, obj_init_bno
        mov ecx, [obj_session1]
        mov edx, INTEGRITY_MEDIUM
        call obj_new_named
        test eax, eax
        jz .oi_fail

        ; \KernelObjects\Lobby: DACL wide open, integrity SYSTEM.
        mov eax, OBJ_T_DIRECTORY
        mov ebx, obj_init_lobby
        mov ecx, [obj_kernelobjects]
        mov edx, INTEGRITY_SYSTEM
        call obj_new_named
        test eax, eax
        jz .oi_fail
        mov [obj_lobby], eax
        call obj_make_world_all

        mov dword [esp + 28], 1
        popad
        ret
.oi_fail:
        mov dword [esp + 28], 0
        popad
        ret


;=======================================================================
; Syscalls (187..200)
;
; They follow the frozen convention: arguments in EBX/ECX/EDX/ESI/EDI, result
; in EAX, return with iretd. A failure returns -1 and puts an mv_status in
; EDX, so a legacy caller sees what every other syscall gives it and a new one
; can say which condition it was.
;=======================================================================

OCF_EVENT_MANUAL    equ 0x0001   ; create flags, in EDI
OCF_EVENT_SIGNALLED equ 0x0002

OD_OPEN         equ 1
OD_CREATE       equ 2
OD_OPEN_IF      equ 3
OD_SUPERSEDE    equ 4
OD_OVERWRITE_IF equ 5

;-----------------------------------------------------------------------
; obj_copy_name - copy a ring-3 name into the kernel's working buffer
;   In : EAX = user pointer
;   Out: CF=0 copied (NUL-terminated, <= OBJ_NAME_MAX bytes), CF=1 bad
;-----------------------------------------------------------------------
obj_copy_name:
        pushad
        mov esi, eax
        mov edi, obj_name_buf
        xor ecx, ecx
.ocn_loop:
        cmp ecx, OBJ_NAME_MAX
        ja .ocn_bad
        mov eax, esi
        add eax, ecx
        call validate_user_ptr
        jc .ocn_bad
        mov al, [esi + ecx]
        mov [edi + ecx], al
        test al, al
        jz .ocn_ok
        inc ecx
        jmp .ocn_loop
.ocn_ok:
        popad
        clc
        ret
.ocn_bad:
        popad
        stc
        ret

;-----------------------------------------------------------------------
; obj_split_name - split obj_name_buf into obj_parent_buf and obj_leaf_buf
;   Out: EAX = leaf pointer; obj_parent_buf holds the directory path
;
; The parent path of "\KernelObjects\Foo" is "\KernelObjects"; of "Foo" it is
; "\" - the root. The leaf is never empty, so a name ending in '\' is refused.
;-----------------------------------------------------------------------
obj_split_name:
        pushad
        mov esi, obj_name_buf
        mov edi, obj_parent_buf
        mov ebx, obj_leaf_buf
        xor ecx, ecx
.osn_len:
        cmp byte [esi + ecx], 0
        je .osn_len_done
        inc ecx
        jmp .osn_len
.osn_len_done:
        test ecx, ecx
        jz .osn_bad
        ; Find the last separator.
        mov edx, -1
        xor eax, eax
.osn_scan:
        cmp eax, ecx
        jae .osn_scan_done
        cmp byte [esi + eax], '\'
        jne .osn_scan_next
        mov edx, eax
.osn_scan_next:
        inc eax
        jmp .osn_scan
.osn_scan_done:
        cmp edx, -1
        jne .osn_has_sep
        ; No separator: parent is the root, the whole name is the leaf.
        mov byte [edi], '\'
        mov byte [edi + 1], 0
        xor eax, eax
.osn_copy_leaf:
        cmp eax, ecx
        jae .osn_leaf_done
        mov dl, [esi + eax]
        mov [ebx + eax], dl
        inc eax
        jmp .osn_copy_leaf
.osn_leaf_done:
        mov byte [ebx + eax], 0
        jmp .osn_ok
.osn_has_sep:
        ; Parent is everything up to the separator; the leaf follows it.
        xor eax, eax
.osn_copy_parent:
        cmp eax, edx
        jae .osn_parent_done
        mov cl, [esi + eax]
        mov [edi + eax], cl
        inc eax
        jmp .osn_copy_parent
.osn_parent_done:
        test eax, eax
        jnz .osn_parent_terminate
        mov byte [edi], '\'
        inc eax
.osn_parent_terminate:
        mov byte [edi + eax], 0
        inc edx
        xor eax, eax
.osn_copy_leaf2:
        mov cl, [esi + edx]
        mov [ebx + eax], cl
        test cl, cl
        jz .osn_ok
        inc eax
        inc edx
        jmp .osn_copy_leaf2
.osn_ok:
        mov eax, obj_leaf_buf
        mov [esp + 28], eax
        popad
        ret
.osn_bad:
        xor eax, eax
        mov [esp + 28], eax
        popad
        ret

;-----------------------------------------------------------------------
; sys_object_create (187)
;   In : EBX = name, ECX = type, EDX = desired rights, ESI = disposition
;   Out: EAX = handle, or -1 with EDX = status
;-----------------------------------------------------------------------
sys_object_create:
        push ebx
        push ecx
        push edx
        push esi
        push edi
        mov [.soc_type], ecx
        mov [.soc_rights], edx
        mov [.soc_disp], esi
        mov [.soc_flags], edi

        cmp ecx, OBJ_T_COUNT
        jae .soc_bad_param
        ; The disposition is one of the five OBJECTS.md §5 names.
        cmp esi, OD_OPEN
        jb .soc_bad_param
        cmp esi, OD_OVERWRITE_IF
        ja .soc_bad_param
        ; Replacing an existing kernel object is not a thing this wave does;
        ; the two replacing dispositions belong to files, and say so.
        cmp esi, OD_SUPERSEDE
        jb .soc_disp_ok
        mov eax, -1
        mov edx, MV_STATUS_NOT_SUPPORTED
        jmp .soc_ret
.soc_disp_ok:
        mov eax, ebx
        call obj_copy_name
        jc .soc_bad_param

        call obj_split_name
        test eax, eax
        jz .soc_bad_param
        mov [.soc_leaf], eax

        ; Walk to the parent directory.
        mov eax, obj_parent_buf
        call obj_ns_walk
        test eax, eax
        jnz .soc_have_parent
        mov eax, -1
        jmp .soc_ret
.soc_have_parent:
        mov [.soc_parent], eax

        ; Does the name already exist?
        mov ebx, [.soc_leaf]
        xor ecx, ecx
        mov esi, ebx
.osn_leaflen:
        cmp byte [esi + ecx], 0
        je .osn_leaflen_done
        inc ecx
        jmp .osn_leaflen
.osn_leaflen_done:
        mov eax, [.soc_parent]
        call obj_ns_find_child
        test eax, eax
        jz .soc_absent

        ; Present. OPEN and OPEN_IF open it; CREATE refuses.
        mov [.soc_obj], eax
        cmp dword [.soc_disp], OD_CREATE
        jne .soc_open_existing
        mov eax, -1
        mov edx, MV_STATUS_INVALID_PARAMETER
        jmp .soc_ret

.soc_absent:
        ; Absent. OPEN refuses; everything else creates.
        cmp dword [.soc_disp], OD_OPEN
        jne .soc_create_new
        mov eax, -1
        mov edx, MV_STATUS_INVALID_PARAMETER
        jmp .soc_ret

.soc_create_new:
        mov eax, [.soc_type]
        call obj_alloc
        test eax, eax
        jz .soc_nomem
        mov [.soc_obj], eax
        mov dword [eax + OBJ_NAME_LEN], 0

        call obj_security_alloc
        test eax, eax
        jz .soc_nomem

        ; Creation parameters, from EDI. A type reads only what it defines, so
        ; one word carries the initial state of every object a program can
        ; create: EV_MANUAL and EV_SIGNALLED for an event, the initial and
        ; maximum count for a semaphore.
        mov edi, [.soc_obj]
        mov eax, [.soc_flags]
        mov ecx, [.soc_type]
        cmp ecx, OBJ_T_EVENT
        je .soc_init_event
        cmp ecx, OBJ_T_SEMAPHORE
        je .soc_init_sem
        cmp ecx, OBJ_T_TIMER
        je .soc_init_timer
        jmp .soc_init_done
.soc_init_event:
        test eax, OCF_EVENT_MANUAL
        jz .soc_init_ev_auto
        mov dword [edi + OBJ_BODY + EV_MANUAL], 1
.soc_init_ev_auto:
        test eax, OCF_EVENT_SIGNALLED
        jz .soc_init_done
        mov dword [edi + OBJ_BODY + EV_STATE], 1
        mov dword [edi + OBJ_TD_SIGNAL], 1
        jmp .soc_init_done
.soc_init_sem:
        movzx ecx, ax                   ; initial count
        mov [edi + OBJ_BODY + SE_COUNT], ecx
        shr eax, 16                     ; maximum
        jz .soc_init_sem_default
        mov [edi + OBJ_BODY + SE_MAX], eax
        jmp .soc_init_sem_signal
.soc_init_sem_default:
        mov dword [edi + OBJ_BODY + SE_MAX], 0x7FFFFFFF
.soc_init_sem_signal:
        cmp dword [edi + OBJ_BODY + SE_COUNT], 0
        je .soc_init_done
        mov dword [edi + OBJ_TD_SIGNAL], 1
        jmp .soc_init_done
.soc_init_timer:
        mov [edi + OBJ_BODY + TM_PERIOD], eax
        test eax, eax
        jz .soc_init_done
        add eax, [tick_count]
        mov [edi + OBJ_BODY + TM_DUE], eax
.soc_init_done:

        ; The name is taken by the directory's own write check inside
        ; obj_ns_link, so an unprivileged caller cannot plant a name in a
        ; directory it does not own.
        mov eax, [.soc_obj]
        mov ebx, [.soc_leaf]
        mov ecx, [.soc_parent]
        call obj_ns_link
        test eax, eax
        jz .soc_linked
        mov [.soc_status], eax
        mov eax, [.soc_obj]
        call obj_deref                  ; undo the creation reference
        mov eax, -1
        mov edx, [.soc_status]
        jmp .soc_ret
.soc_linked:
        ; The creator keeps the rights it asked for.
        mov eax, [.soc_obj]
        mov ebx, [.soc_rights]
        call obj_access
        jc .soc_denied_cleanup
        jmp .soc_make_handle

.soc_open_existing:
        mov eax, [.soc_obj]
        mov ebx, [.soc_rights]
        call obj_access
        jc .soc_denied

.soc_make_handle:
        mov eax, [.soc_obj]
        mov ebx, [.soc_rights]
        xor ecx, ecx
        call ht_open
        jc .soc_ht_fail
        ; The handle holds a reference; the creation reference is let go here
        ; or every created object would live for ever. The handle is kept in
        ; the result slot first: obj_deref returns the object pointer, and
        ; returning that where a handle belongs is the kind of mistake that
        ; only shows up two calls later.
        mov [.soc_handle], eax
        mov eax, [.soc_obj]
        call obj_deref
        mov eax, [.soc_handle]
        xor edx, edx
        jmp .soc_ret

.soc_denied_cleanup:
        mov eax, [.soc_obj]
        call obj_deref
.soc_denied:
        mov eax, -1
        mov edx, MV_STATUS_ACCESS_DENIED
        jmp .soc_ret
.soc_ht_fail:
        mov eax, -1
        jmp .soc_ret
.soc_nomem:
        mov eax, -1
        mov edx, MV_STATUS_NO_MEMORY
        jmp .soc_ret
.soc_bad_param:
        mov eax, -1
        mov edx, MV_STATUS_INVALID_PARAMETER
.soc_ret:
        mov [esp + 8], edx
        jmp obj_syscall_epilogue
.soc_type:   dd 0
.soc_rights: dd 0
.soc_disp:   dd 0
.soc_leaf:   dd 0
.soc_parent: dd 0
.soc_obj:    dd 0
.soc_status: dd 0
.soc_handle: dd 0
.soc_flags:  dd 0

;-----------------------------------------------------------------------
; sys_object_open (188)   EBX = name, ECX = desired rights
;-----------------------------------------------------------------------
sys_object_open:
        push ebx
        push ecx
        push edx
        push esi
        push edi
        mov [.soo_rights], ecx
        mov eax, ebx
        call obj_copy_name
        jc .soo_bad
        mov eax, obj_name_buf
        call obj_ns_walk
        test eax, eax
        jnz .soo_found
        ; obj_ns_walk already said why in EDX.
        mov eax, -1
        jmp .soo_out
.soo_found:
        mov [.soo_obj], eax
        mov ebx, [.soo_rights]
        call obj_access
        jc .soo_denied
        mov eax, [.soo_obj]
        mov ebx, [.soo_rights]
        xor ecx, ecx
        call ht_open
        jc .soo_bad
        xor edx, edx
        jmp .soo_out
.soo_denied:
        mov eax, -1
        mov edx, MV_STATUS_ACCESS_DENIED
        jmp .soo_out
.soo_bad:
        mov eax, -1
        mov edx, MV_STATUS_INVALID_PARAMETER
.soo_out:
        mov [esp + 8], edx
        jmp obj_syscall_epilogue
.soo_rights: dd 0
.soo_obj:    dd 0

;-----------------------------------------------------------------------
; sys_object_close (189)  EBX = handle
;-----------------------------------------------------------------------
sys_object_close:
        mov eax, ebx
        call ht_close
        jc .socl_bad
        xor edx, edx
        iretd
.socl_bad:
        mov eax, -1
        iretd

;-----------------------------------------------------------------------
; sys_object_dup (190)    EBX = handle, ECX = narrower rights
;
; A duplicate may only narrow: the rights asked for must be a subset of the
; rights the source handle already carries, so duplication is never a way to
; amplify what a caller was given.
;-----------------------------------------------------------------------
sys_object_dup:
        push ebx
        push ecx
        push edx
        push esi
        push edi
        mov [.sod_rights], ecx
        mov eax, ebx
        call ht_entry
        jc .sod_bad
        mov esi, eax
        movzx eax, word [esi + HE_RIGHTS]
        mov [.sod_have], eax

        ; "All" is a request, not a mask: map it before comparing, or a handle
        ; holding one right would appear to hold them all.
        mov ecx, [.sod_rights]
        test ecx, MV_RIGHTS_ALL
        jz .sod_specific
        mov eax, [esi + HE_OBJ]
        mov eax, [eax + OBJ_TYPE]
        call obj_type_desc
        mov ecx, [eax + TT_ALL]
        mov [.sod_rights], ecx
.sod_specific:
        mov eax, [.sod_have]
        and eax, ecx
        cmp eax, ecx
        jne .sod_denied
        mov eax, [esi + HE_OBJ]
        mov ebx, [.sod_rights]
        xor ecx, ecx
        call ht_open
        jc .sod_bad
        xor edx, edx
        jmp .sod_out
.sod_denied:
        mov eax, -1
        mov edx, MV_STATUS_ACCESS_DENIED
        jmp .sod_out
.sod_bad:
        mov eax, -1
        mov edx, MV_STATUS_INVALID_HANDLE
.sod_out:
        mov [esp + 8], edx
        jmp obj_syscall_epilogue
.sod_rights: dd 0
.sod_have:   dd 0

;-----------------------------------------------------------------------
; sys_object_query (191)  EBX = handle, ECX = buffer, EDX = length
;
; Writes { type, rights, signalled, name_len } then the name, NUL-terminated.
; Returns the bytes written, or -1 with MV_BUFFER_TOO_SMALL.
;-----------------------------------------------------------------------
OQ_TYPE         equ 0
OQ_RIGHTS       equ 4
OQ_SIGNALLED    equ 8
OQ_NAMELEN      equ 12
OQ_NAME         equ 16

sys_object_query:
        push ebx
        push ecx
        push edx
        push esi
        push edi
        mov [.soq_buf], ecx
        mov [.soq_len], edx
        cmp edx, OQ_NAME + 1
        jb .soq_small
        mov eax, ebx
        call ht_entry
        jc .soq_bad
        mov [.soq_ent], eax
        mov esi, eax
        mov edi, [esi + HE_OBJ]
        mov [.soq_obj], edi

        ; The whole buffer must be writable before the first byte goes into it.
        mov eax, [.soq_buf]
        mov ecx, [.soq_len]
        call user_range_ok
        jc .soq_bad

        mov edi, [.soq_buf]
        mov esi, [.soq_obj]
        mov eax, [esi + OBJ_TYPE]
        mov [edi + OQ_TYPE], eax
        mov eax, [.soq_ent]
        movzx eax, word [eax + HE_RIGHTS]
        mov [edi + OQ_RIGHTS], eax
        mov eax, [esi + OBJ_TD_SIGNAL]
        mov [edi + OQ_SIGNALLED], eax
        movzx ecx, word [esi + OBJ_NAME_LEN]
        mov [edi + OQ_NAMELEN], ecx

        lea esi, [esi + OBJ_NAME]
        add edi, OQ_NAME
        cld
        rep movsb
        mov byte [edi], 0
        inc edi
        mov eax, edi
        sub eax, [.soq_buf]             ; bytes written
        xor edx, edx
        jmp .soq_out

.soq_small:
        mov eax, -1
        mov edx, MV_STATUS_BUFFER_TOO_SMALL
        jmp .soq_out
.soq_bad:
        mov eax, -1
        mov edx, MV_STATUS_INVALID_HANDLE
.soq_out:
        mov [esp + 8], edx
        jmp obj_syscall_epilogue
.soq_buf: dd 0
.soq_len: dd 0
.soq_obj: dd 0
.soq_ent: dd 0


;-----------------------------------------------------------------------
; obj_wait_do - the shared body of the three wait calls
;   In : EBX = handle array, ECX = count, EDX = timeout ticks (0 = forever),
;        ESI = WAITK_*, EDI = 1 when the wait is alertable
;   Out: EAX = index (or 0 for WaitAll), WAIT_TIMEOUT, WAIT_USER_APC
;
; This routine is the whole syscall: it does not return to a caller, it ends
; in iretd, because a wait that has to block must save the frame it will be
; resumed into. That is why each handler jumps here rather than calling.
;-----------------------------------------------------------------------
sys_wait_one:
        ; WaitOne takes a handle, not an array, so marshal it into a one-entry
        ; set. The set then goes through exactly the same path as WaitAny and
        ; WaitAll, which is the point: one wait implementation, three shapes.
        mov [obj_wait_one_handle], ebx
        mov ebx, obj_wait_one_handle
        mov ecx, 1
        xor edi, edi
        mov esi, WAITK_ONE
        jmp obj_wait_do

sys_wait_any:
        mov esi, WAITK_ANY
        xor edi, edi
        jmp obj_wait_do

sys_wait_all:
        mov esi, WAITK_ALL
        xor edi, edi
        jmp obj_wait_do

obj_wait_do:
        pushad
        mov [.wd_handles], ebx
        mov [.wd_count], ecx
        mov [.wd_timeout], edx
        mov [.wd_kind], esi
        mov [.wd_alert], edi
        mov dword [.wd_result], WAIT_FAILED

        test ecx, ecx
        jz .wd_exit
        cmp ecx, WAIT_MAX_HANDLES
        ja .wd_exit
        ; A timeout of 0 polls: the set is evaluated once and the call returns.
        ; 0xFFFFFFFF waits indefinitely, which is the only value that does.
        mov dword [.wd_poll], 0
        cmp dword [.wd_timeout], 0
        jne .wd_timeout_ready
        mov dword [.wd_poll], 1
.wd_timeout_ready:

        ; An alertable wait that has a signal pending does not block: it gives
        ; the caller back the chance to run its handler. That is the only way
        ; a blocking call can be interrupted (OBJECTS.md §6).
        test edi, edi
        jz .wd_setup
        call obj_signal_pending
        test eax, eax
        jz .wd_setup
        mov dword [.wd_result], WAIT_USER_APC
        jmp .wd_exit

.wd_setup:
        ; The handle array must be readable in full - unless it is the one the
        ; WaitOne marshalling above put in kernel memory, which is why the kind
        ; is checked rather than the address.
        cmp dword [.wd_kind], WAITK_ONE
        je .wd_range_ok
        mov eax, [.wd_handles]
        mov ecx, [.wd_count]
        shl ecx, 2
        call user_range_ok
        jc .wd_exit
.wd_range_ok:

        call obj_waitset
        test eax, eax
        jz .wd_exit
        mov edi, eax
        mov [.wd_set], edi
        mov eax, [.wd_kind]
        mov [edi + WS_KIND], eax
        mov eax, [.wd_count]
        mov [edi + WS_COUNT], eax

        ; Resolve every handle to its object, requiring SYNCHRONIZE.
        mov esi, [.wd_handles]
        xor ebx, ebx
.wd_resolve:
        cmp ebx, [.wd_count]
        jae .wd_evaluate
        mov eax, [esi + ebx*4]
        push esi
        push ebx
        mov ebx, RIGHT_SYNCHRONIZE
        call ht_use
        pop ebx
        pop esi
        jc .wd_exit                     ; a handle without SYNCHRONIZE is not
        mov edi, [.wd_set]              ; waitable, and neither is a stale one
        mov [edi + WS_OBJS + ebx*4], eax
        ; Only objects whose type carries a signalled state can be waited on.
        push eax
        call obj_waitable
        pop ecx
        test eax, eax
        jz .wd_exit
        inc ebx
        jmp .wd_resolve

.wd_evaluate:
        mov edi, [.wd_set]
        mov eax, edi
        call obj_wait_eval
        test eax, eax
        jz .wd_block
        mov eax, [.wd_set]
        call obj_wait_consume
        mov eax, edx
        cmp dword [.wd_kind], WAITK_ALL
        jne .wd_have_index
        xor eax, eax                    ; WaitAll reports 0 on success
.wd_have_index:
        mov [.wd_result], eax
        jmp .wd_exit

.wd_polled_out:
        mov dword [.wd_result], WAIT_TIMEOUT
        jmp .wd_exit

.wd_block:
        cmp dword [.wd_poll], 0
        jne .wd_polled_out
        call obj_ctx
        test eax, eax
        jz .wd_exit
        mov [.wd_tcb], eax
        mov edi, eax

        mov eax, [.wd_set]
        mov ebx, [.wd_tcb]
        call obj_wait_enqueue
        test eax, eax
        jz .wd_exit                     ; out of waiter records

        mov eax, [.wd_tcb]
        mov ecx, [.wd_set]
        mov [eax + TCB_WAITOBJ], ecx
        mov ecx, [.wd_kind]
        mov [eax + TCB_WAITKIND], ecx
        mov dword [eax + TCB_WAITRES], WAIT_FAILED
        mov dword [eax + TCB_WAITR], WAITR_OBJECT

        mov ecx, [.wd_timeout]
        cmp ecx, -1
        je .wd_no_deadline              ; indefinite
        add ecx, [tick_count]
.wd_no_deadline:
        mov [eax + TCB_WAKEUP], ecx
        mov dword [eax + TCB_STATE], TASK_BLOCKED
        mov [eax + TCB_ESP], esp

        mov edi, eax
        call obj_yield_cpu
        ; Only reached when nothing else could run: a wait nobody can satisfy.
        mov eax, [.wd_tcb]
        mov dword [eax + TCB_STATE], TASK_RUNNING
        mov dword [eax + TCB_WAKEUP], 0
        mov dword [eax + TCB_WAITOBJ], 0
        mov dword [eax + TCB_WAITR], WAITR_NONE
        mov eax, [.wd_set]
        call obj_wait_dequeue
        mov dword [.wd_result], WAIT_TIMEOUT

.wd_exit:
        mov eax, [.wd_result]
        mov [esp + 28], eax
        popad
        iretd
.wd_handles: dd 0
.wd_count:   dd 0
.wd_timeout: dd 0
.wd_kind:    dd 0
.wd_alert:   dd 0
.wd_result:  dd 0
.wd_set:     dd 0
.wd_tcb:     dd 0
.wd_poll:    dd 0

;-----------------------------------------------------------------------
; obj_signal_pending - does the calling task have an unblocked signal
;   Out: EAX = 1 / 0
;-----------------------------------------------------------------------
obj_signal_pending:
        pushad
        mov dword [esp + 28], 0
        call obj_ctx
        test eax, eax
        jz .osp_done
        mov edi, eax
        mov eax, [edi + TCB_SIG_PEND]
        mov ecx, [edi + TCB_SIG_MASK]
        not ecx
        and eax, ecx
        jz .osp_done
        mov dword [esp + 28], 1
.osp_done:
        popad
        ret

;-----------------------------------------------------------------------
; sys_event_set (195)     EBX = handle
; sys_event_reset (196)   EBX = handle
; A set needs SET_STATE; a reset needs SET_STATE. QUERY_STATE alone is not
; enough to change an object other programs may be waiting on.
;-----------------------------------------------------------------------
sys_event_set:
        pushad
        mov eax, ebx
        mov ebx, RIGHT_SET_STATE
        call ht_use
        jc .oes_fail
        call obj_signal
        popad
        xor eax, eax
        iretd
.oes_fail:
        popad
        mov eax, -1
        iretd

sys_event_reset:
        pushad
        mov eax, ebx
        mov ebx, RIGHT_SET_STATE
        call ht_use
        jc .oer_fail
        call obj_reset
        popad
        xor eax, eax
        iretd
.oer_fail:
        popad
        mov eax, -1
        iretd

;-----------------------------------------------------------------------
; sys_mutant_release (197)  EBX = handle
;-----------------------------------------------------------------------
sys_mutant_release:
        pushad
        mov eax, ebx
        mov ebx, RIGHT_SET_STATE
        call ht_use
        jc .omr_fail
        mov dword [eax + OBJ_BODY + MU_OWNER], 0
        mov dword [eax + OBJ_BODY + MU_RECURSE], 0
        call obj_signal
        popad
        xor eax, eax
        iretd
.omr_fail:
        popad
        mov eax, -1
        iretd

;-----------------------------------------------------------------------
; sys_semaphore_release (198)  EBX = handle, ECX = count
;-----------------------------------------------------------------------
sys_semaphore_release:
        pushad
        mov [.osr_n], ecx
        test ecx, ecx
        jz .osr_fail
        mov eax, ebx
        mov ebx, RIGHT_SET_STATE
        call ht_use
        jc .osr_fail
        mov edi, eax
        mov eax, [edi + OBJ_BODY + SE_COUNT]
        add eax, [.osr_n]
        cmp eax, [edi + OBJ_BODY + SE_MAX]
        ja .osr_fail                    ; beyond the maximum is an error
        mov [edi + OBJ_BODY + SE_COUNT], eax
        mov eax, edi
        call obj_signal
        popad
        xor eax, eax
        iretd
.osr_fail:
        popad
        mov eax, -1
        iretd
.osr_n: dd 0

;-----------------------------------------------------------------------
; sys_object_stats (199)  EBX = buffer, ECX = length
;
; Writes OBJ_T_COUNT records of { live, peak, name_length, name } and returns
; the bytes written. This is the read-only view `objects` prints, and the
; counter a soak test watches for a leak.
;-----------------------------------------------------------------------
OS_LIVE         equ 0
OS_PEAK         equ 4
OS_NAMELEN      equ 8
OS_NAME         equ 12
OS_REC          equ 32

sys_object_stats:
        pushad
        mov [.ost_buf], ebx
        mov dword [.ost_out], 0
        mov eax, ebx
        mov ecx, OBJ_T_COUNT * OS_REC
        call user_range_ok
        jc .ost_fail
        mov edx, OBJ_T_COUNT * OS_REC
        cmp ecx, edx
        jb .ost_fail

        mov edi, [.ost_buf]
        xor esi, esi
.ost_loop:
        cmp esi, OBJ_T_COUNT
        jae .ost_done
        mov eax, [obj_type_live + esi*4]
        mov [edi + OS_LIVE], eax
        mov eax, [obj_type_peak + esi*4]
        mov [edi + OS_PEAK], eax
        ; The type name, from the type record.
        push esi
        mov eax, esi
        call obj_type_desc
        mov esi, [eax + TT_NAME]
        mov ecx, 0
.ost_namelen:
        cmp byte [esi + ecx], 0
        je .ost_namelen_done
        inc ecx
        jmp .ost_namelen
.ost_namelen_done:
        mov [edi + OS_NAMELEN], ecx
        lea ebx, [edi + OS_NAME]
.ost_copy:
        mov al, [esi]
        mov [ebx], al
        test al, al
        jz .ost_rec_done
        inc esi
        inc ebx
        jmp .ost_copy
.ost_rec_done:
        pop esi
        add edi, OS_REC
        inc esi
        jmp .ost_loop
.ost_done:
        mov dword [esp + 28], OBJ_T_COUNT * OS_REC
        popad
        iretd
.ost_fail:
        popad
        mov eax, -1
        iretd
.ost_buf: dd 0
.ost_out: dd 0

;-----------------------------------------------------------------------
; sys_token_query (200)   EBX = buffer (24 bytes)
;
; Writes { uid, gid, session, integrity, privileges, is_system }. The token is
; a view over what setuid already maintains, so this reads the same fields the
; frozen syscalls check rather than a copy that could disagree.
;-----------------------------------------------------------------------
TQ_UID          equ 0
TQ_GID          equ 4
TQ_SESSION      equ 8
TQ_INTEGRITY    equ 12
TQ_PRIVS        equ 16
TQ_ISSYSTEM     equ 20
TQ_SIZE         equ 24

sys_token_query:
        pushad
        mov [.otq_buf], ebx
        mov eax, ebx
        mov ecx, TQ_SIZE
        call user_range_ok
        jc .otq_fail
        call obj_identity
        mov edi, [.otq_buf]
        mov [edi + TQ_UID], eax
        mov [edi + TQ_GID], ebx
        mov [edi + TQ_SESSION], ecx
        mov [edi + TQ_INTEGRITY], edx
        mov eax, 0
        test esi, esi
        jz .otq_nopriv
        mov eax, 0xFFFFFFFF             ; SYSTEM holds every privilege
.otq_nopriv:
        mov [edi + TQ_PRIVS], eax
        mov [edi + TQ_ISSYSTEM], esi
        popad
        xor eax, eax
        iretd
.otq_fail:
        popad
        mov eax, -1
        iretd
.otq_buf: dd 0

; EDX is the mv_status channel: 0 on success, the condition on failure.
; It is deliberately not restored - a new caller needs to know why a call
; failed, and the frozen syscalls carry that in errno instead.
obj_syscall_epilogue:
        pop edi
        pop esi
        add esp, 4                      ; the saved EDX is the result, not state
        pop ecx
        pop ebx
        iretd

%endif ; OBJECT_INC
