;=======================================================================
; data.inc - Mellivora Data Structures API
;
; Common data structures for application development: stack, queue,
; circular buffer, bitmap, and sorted insertion helpers.
;
; These operate on caller-provided memory buffers.
;
; Functions:
;   ds_stack_init     - Initialize a stack
;   ds_stack_push     - Push dword onto stack
;   ds_stack_pop      - Pop dword from stack
;   ds_stack_peek     - Peek top of stack
;   ds_stack_empty    - Check if stack is empty
;   ds_stack_count    - Get number of items
;   ds_queue_init     - Initialize a circular queue
;   ds_queue_push     - Enqueue dword
;   ds_queue_pop      - Dequeue dword
;   ds_queue_peek     - Peek front of queue
;   ds_queue_empty    - Check if queue is empty
;   ds_queue_full     - Check if queue is full
;   ds_queue_count    - Get number of items
;   ds_bmap_set       - Set bit in bitmap
;   ds_bmap_clear     - Clear bit in bitmap
;   ds_bmap_test      - Test bit in bitmap
;   ds_bmap_find_free - Find first clear bit
;   ds_bmap_count_set - Count set bits
;   ds_sort_insert    - Insertion sort on dword array
;   ds_binary_search  - Binary search in sorted dword array
;   ds_array_swap     - Swap two elements in dword array
;   ds_array_reverse  - Reverse a dword array
;   ds_array_min      - Find minimum in dword array
;   ds_array_max      - Find maximum in dword array
;   ds_array_sum      - Sum all elements in dword array
;=======================================================================

; Stack header offsets (caller allocates header + data contiguously)
STK_BASE        equ 0          ; dword: base of data area (header + STK_HDR)
STK_CAPACITY    equ 4          ; dword: max dwords
STK_TOP         equ 8          ; dword: index of next push slot
STK_HDR_SIZE    equ 12

; Queue header offsets
QUE_BASE        equ 0          ; dword: base of data area
QUE_CAPACITY    equ 4          ; dword: max dwords
QUE_HEAD        equ 8          ; dword: read index
QUE_TAIL        equ 12         ; dword: write index
QUE_COUNT       equ 16         ; dword: current count
QUE_HDR_SIZE    equ 20

;-----------------------------------------------------------------------
; ds_stack_init - Initialize a stack
; Input:  EDI = header (STK_HDR_SIZE bytes)
;         ESI = data buffer (capacity * 4 bytes)
;         ECX = capacity (max dwords)
; Output: none
;-----------------------------------------------------------------------
ds_stack_init:
        mov [edi + STK_BASE], esi
        mov [edi + STK_CAPACITY], ecx
        mov dword [edi + STK_TOP], 0
        ret

;-----------------------------------------------------------------------
; ds_stack_push - Push a dword onto the stack
; Input:  EDI = stack header, EAX = value
; Output: Carry flag clear on success, set if full
;-----------------------------------------------------------------------
ds_stack_push:
        push ebx
        push ecx
        mov ecx, [edi + STK_TOP]
        cmp ecx, [edi + STK_CAPACITY]
        jge .dsp_full
        mov ebx, [edi + STK_BASE]
        mov [ebx + ecx*4], eax
        inc dword [edi + STK_TOP]
        clc
        pop ecx
        pop ebx
        ret
.dsp_full:
        stc
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; ds_stack_pop - Pop a dword from the stack
; Input:  EDI = stack header
; Output: EAX = popped value
;         Carry flag clear on success, set if empty
;-----------------------------------------------------------------------
ds_stack_pop:
        push ebx
        push ecx
        mov ecx, [edi + STK_TOP]
        test ecx, ecx
        jz .dspp_empty
        dec ecx
        mov [edi + STK_TOP], ecx
        mov ebx, [edi + STK_BASE]
        mov eax, [ebx + ecx*4]
        clc
        pop ecx
        pop ebx
        ret
.dspp_empty:
        stc
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; ds_stack_peek - Peek at top of stack without removing
; Input:  EDI = stack header
; Output: EAX = top value, Carry set if empty
;-----------------------------------------------------------------------
ds_stack_peek:
        push ebx
        push ecx
        mov ecx, [edi + STK_TOP]
        test ecx, ecx
        jz .dspk_empty
        dec ecx
        mov ebx, [edi + STK_BASE]
        mov eax, [ebx + ecx*4]
        clc
        pop ecx
        pop ebx
        ret
.dspk_empty:
        stc
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; ds_stack_empty - Check if stack is empty
; Input:  EDI = stack header
; Output: EAX = 1 if empty, 0 if not
;-----------------------------------------------------------------------
ds_stack_empty:
        cmp dword [edi + STK_TOP], 0
        je .dse_yes
        xor eax, eax
        ret
.dse_yes:
        mov eax, 1
        ret

;-----------------------------------------------------------------------
; ds_stack_count - Get number of items on stack
; Input:  EDI = stack header
; Output: EAX = count
;-----------------------------------------------------------------------
ds_stack_count:
        mov eax, [edi + STK_TOP]
        ret

;-----------------------------------------------------------------------
; ds_queue_init - Initialize a circular queue
; Input:  EDI = header (QUE_HDR_SIZE bytes)
;         ESI = data buffer (capacity * 4 bytes)
;         ECX = capacity (max dwords)
; Output: none
;-----------------------------------------------------------------------
ds_queue_init:
        mov [edi + QUE_BASE], esi
        mov [edi + QUE_CAPACITY], ecx
        mov dword [edi + QUE_HEAD], 0
        mov dword [edi + QUE_TAIL], 0
        mov dword [edi + QUE_COUNT], 0
        ret

;-----------------------------------------------------------------------
; ds_queue_push - Enqueue a dword
; Input:  EDI = queue header, EAX = value
; Output: Carry clear on success, set if full
;-----------------------------------------------------------------------
ds_queue_push:
        push ebx
        push ecx
        push edx
        mov ecx, [edi + QUE_COUNT]
        cmp ecx, [edi + QUE_CAPACITY]
        jge .dqp_full

        mov ebx, [edi + QUE_BASE]
        mov edx, [edi + QUE_TAIL]
        mov [ebx + edx*4], eax

        ; Advance tail circularly
        inc edx
        cmp edx, [edi + QUE_CAPACITY]
        jb .dqp_nowrap
        xor edx, edx
.dqp_nowrap:
        mov [edi + QUE_TAIL], edx
        inc dword [edi + QUE_COUNT]
        clc
        pop edx
        pop ecx
        pop ebx
        ret
.dqp_full:
        stc
        pop edx
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; ds_queue_pop - Dequeue a dword
; Input:  EDI = queue header
; Output: EAX = dequeued value, Carry set if empty
;-----------------------------------------------------------------------
ds_queue_pop:
        push ebx
        push ecx
        mov ecx, [edi + QUE_COUNT]
        test ecx, ecx
        jz .dqpp_empty

        mov ebx, [edi + QUE_BASE]
        mov ecx, [edi + QUE_HEAD]
        mov eax, [ebx + ecx*4]

        inc ecx
        cmp ecx, [edi + QUE_CAPACITY]
        jb .dqpp_nowrap
        xor ecx, ecx
.dqpp_nowrap:
        mov [edi + QUE_HEAD], ecx
        dec dword [edi + QUE_COUNT]
        clc
        pop ecx
        pop ebx
        ret
.dqpp_empty:
        stc
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; ds_queue_peek - Peek at front of queue
; Input:  EDI = queue header
; Output: EAX = front value, Carry set if empty
;-----------------------------------------------------------------------
ds_queue_peek:
        push ebx
        push ecx
        cmp dword [edi + QUE_COUNT], 0
        je .dqpk_empty
        mov ebx, [edi + QUE_BASE]
        mov ecx, [edi + QUE_HEAD]
        mov eax, [ebx + ecx*4]
        clc
        pop ecx
        pop ebx
        ret
.dqpk_empty:
        stc
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; ds_queue_empty - Check if queue is empty
; Input:  EDI = queue header
; Output: EAX = 1 if empty, 0 if not
;-----------------------------------------------------------------------
ds_queue_empty:
        cmp dword [edi + QUE_COUNT], 0
        je .dqe_yes
        xor eax, eax
        ret
.dqe_yes:
        mov eax, 1
        ret

;-----------------------------------------------------------------------
; ds_queue_full - Check if queue is full
; Input:  EDI = queue header
; Output: EAX = 1 if full, 0 if not
;-----------------------------------------------------------------------
ds_queue_full:
        mov eax, [edi + QUE_COUNT]
        cmp eax, [edi + QUE_CAPACITY]
        jge .dqf_yes
        xor eax, eax
        ret
.dqf_yes:
        mov eax, 1
        ret

;-----------------------------------------------------------------------
; ds_queue_count - Get number of items in queue
; Input:  EDI = queue header
; Output: EAX = count
;-----------------------------------------------------------------------
ds_queue_count:
        mov eax, [edi + QUE_COUNT]
        ret

;-----------------------------------------------------------------------
; ds_bmap_set - Set a bit in bitmap
; Input:  ESI = bitmap base, EAX = bit index
; Output: none
;-----------------------------------------------------------------------
ds_bmap_set:
        push eax
        push ebx
        push ecx
        mov ebx, eax
        shr ebx, 3              ; byte index
        mov ecx, eax
        and ecx, 7              ; bit within byte
        mov al, 1
        shl al, cl
        or [esi + ebx], al
        pop ecx
        pop ebx
        pop eax
        ret

;-----------------------------------------------------------------------
; ds_bmap_clear - Clear a bit in bitmap
; Input:  ESI = bitmap base, EAX = bit index
; Output: none
;-----------------------------------------------------------------------
ds_bmap_clear:
        push eax
        push ebx
        push ecx
        mov ebx, eax
        shr ebx, 3
        mov ecx, eax
        and ecx, 7
        mov al, 1
        shl al, cl
        not al
        and [esi + ebx], al
        pop ecx
        pop ebx
        pop eax
        ret

;-----------------------------------------------------------------------
; ds_bmap_test - Test a bit in bitmap
; Input:  ESI = bitmap base, EAX = bit index
; Output: EAX = 1 if set, 0 if clear
;-----------------------------------------------------------------------
ds_bmap_test:
        push ebx
        push ecx
        mov ebx, eax
        shr ebx, 3              ; byte index
        mov ecx, eax
        and ecx, 7              ; bit within byte
        movzx eax, byte [esi + ebx]
        shr eax, cl
        and eax, 1
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; ds_bmap_find_free - Find first clear bit in bitmap
; Input:  ESI = bitmap base, ECX = total bits
; Output: EAX = index of first clear bit, or -1 if all set
;-----------------------------------------------------------------------
ds_bmap_find_free:
        push ebx
        push ecx
        push edx
        xor eax, eax           ; bit index

.dbff_loop:
        cmp eax, ecx
        jge .dbff_none

        ; Test this bit
        push ecx
        push eax
        mov edx, eax
        shr edx, 3             ; byte
        mov ebx, eax
        and ebx, 7             ; bit
        movzx eax, byte [esi + edx]
        mov cl, bl
        shr al, cl
        test al, 1
        pop eax
        pop ecx
        jz .dbff_found

        inc eax
        jmp .dbff_loop

.dbff_found:
        pop edx
        pop ecx
        pop ebx
        ret

.dbff_none:
        mov eax, -1
        pop edx
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; ds_bmap_count_set - Count number of set bits
; Input:  ESI = bitmap base, ECX = total bits
; Output: EAX = count of set bits
;-----------------------------------------------------------------------
ds_bmap_count_set:
        push ebx
        push ecx
        push edx
        push esi

        ; Process byte by byte
        xor eax, eax           ; count
        mov edx, ecx
        add edx, 7
        shr edx, 3             ; byte count

.dbcs_byte:
        cmp edx, 0
        jle .dbcs_done
        movzx ebx, byte [esi]
        ; Count bits in BL using lookup or loop
.dbcs_bit:
        test bl, bl
        jz .dbcs_next_byte
        mov ecx, ebx
        and ecx, 1
        add eax, ecx
        shr ebx, 1
        jmp .dbcs_bit
.dbcs_next_byte:
        inc esi
        dec edx
        jmp .dbcs_byte

.dbcs_done:
        pop esi
        pop edx
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; ds_sort_insert - Insertion sort on dword array (unsigned)
; Input:  ESI = array base, ECX = element count
; Output: none (array sorted in place, ascending)
;-----------------------------------------------------------------------
ds_sort_insert:
        pushad
        cmp ecx, 1
        jle .dsi_done

        mov edx, 1             ; i = 1
.dsi_outer:
        cmp edx, ecx
        jge .dsi_done

        mov eax, [esi + edx*4] ; key = arr[i]
        mov ebx, edx
        dec ebx                 ; j = i - 1

.dsi_inner:
        cmp ebx, 0
        jl .dsi_insert
        cmp [esi + ebx*4], eax
        jbe .dsi_insert

        ; Shift arr[j] to arr[j+1]
        push eax
        mov eax, [esi + ebx*4]
        mov [esi + ebx*4 + 4], eax
        pop eax
        dec ebx
        jmp .dsi_inner

.dsi_insert:
        mov [esi + ebx*4 + 4], eax
        inc edx
        jmp .dsi_outer

.dsi_done:
        popad
        ret

;-----------------------------------------------------------------------
; ds_binary_search - Binary search in sorted dword array (unsigned)
; Input:  ESI = sorted array, ECX = count, EAX = search value
; Output: EAX = index if found, -1 if not found
;         Carry clear if found, set if not
;-----------------------------------------------------------------------
ds_binary_search:
        push ebx
        push ecx
        push edx
        push edi

        xor ebx, ebx           ; low = 0
        dec ecx                 ; high = count - 1
        mov edi, eax            ; search value

.dbs_loop:
        cmp ebx, ecx
        jg .dbs_notfound

        ; mid = (low + high) / 2
        mov eax, ebx
        add eax, ecx
        shr eax, 1
        mov edx, eax            ; mid

        cmp [esi + eax*4], edi
        je .dbs_found
        ja .dbs_go_left
        ; Go right
        lea ebx, [edx + 1]
        jmp .dbs_loop
.dbs_go_left:
        lea ecx, [edx - 1]
        jmp .dbs_loop

.dbs_found:
        mov eax, edx
        clc
        pop edi
        pop edx
        pop ecx
        pop ebx
        ret

.dbs_notfound:
        mov eax, -1
        stc
        pop edi
        pop edx
        pop ecx
        pop ebx
        ret

;-----------------------------------------------------------------------
; ds_array_swap - Swap two elements in a dword array
; Input:  ESI = array base, EAX = index1, EBX = index2
; Output: none
;-----------------------------------------------------------------------
ds_array_swap:
        push ecx
        push edx
        mov ecx, [esi + eax*4]
        mov edx, [esi + ebx*4]
        mov [esi + eax*4], edx
        mov [esi + ebx*4], ecx
        pop edx
        pop ecx
        ret

;-----------------------------------------------------------------------
; ds_array_reverse - Reverse a dword array in place
; Input:  ESI = array base, ECX = count
; Output: none
;-----------------------------------------------------------------------
ds_array_reverse:
        pushad
        xor eax, eax           ; left
        mov ebx, ecx
        dec ebx                 ; right

.dar_loop:
        cmp eax, ebx
        jge .dar_done
        call ds_array_swap
        inc eax
        dec ebx
        jmp .dar_loop

.dar_done:
        popad
        ret

;-----------------------------------------------------------------------
; ds_array_min - Find minimum value in dword array (unsigned)
; Input:  ESI = array, ECX = count
; Output: EAX = minimum value, EBX = index of minimum
;-----------------------------------------------------------------------
ds_array_min:
        push ecx
        push edx
        mov eax, [esi]          ; min = arr[0]
        xor ebx, ebx           ; min_idx = 0
        mov edx, 1              ; i = 1

.dam_loop:
        cmp edx, ecx
        jge .dam_done
        cmp [esi + edx*4], eax
        jae .dam_next
        mov eax, [esi + edx*4]
        mov ebx, edx
.dam_next:
        inc edx
        jmp .dam_loop

.dam_done:
        pop edx
        pop ecx
        ret

;-----------------------------------------------------------------------
; ds_array_max - Find maximum value in dword array (unsigned)
; Input:  ESI = array, ECX = count
; Output: EAX = maximum value, EBX = index of maximum
;-----------------------------------------------------------------------
ds_array_max:
        push ecx
        push edx
        mov eax, [esi]
        xor ebx, ebx
        mov edx, 1

.dax_loop:
        cmp edx, ecx
        jge .dax_done
        cmp [esi + edx*4], eax
        jbe .dax_next
        mov eax, [esi + edx*4]
        mov ebx, edx
.dax_next:
        inc edx
        jmp .dax_loop

.dax_done:
        pop edx
        pop ecx
        ret

;-----------------------------------------------------------------------
; ds_array_sum - Sum all elements in dword array
; Input:  ESI = array, ECX = count
; Output: EAX = sum
;-----------------------------------------------------------------------
ds_array_sum:
        push ecx
        push edx
        xor eax, eax
        xor edx, edx

.das_loop:
        cmp edx, ecx
        jge .das_done
        add eax, [esi + edx*4]
        inc edx
        jmp .das_loop

.das_done:
        pop edx
        pop ecx
        ret
