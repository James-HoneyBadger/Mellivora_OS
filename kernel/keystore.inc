;=======================================================================
; keystore.inc - named 32-byte secrets, held in kernel memory
;
; docs/spec/STORAGE.md section 8 says a volume key is "wrapped by the machine
; key in the store", and docs/spec/CONFIG.md puts that store in the Sett (M4).
; Until the Sett exists, this is the store: named secrets in kernel memory, set
; from the shell, forgotten at reboot. A volume formatted with a key from here
; refuses to mount until the same secret is put back, which is exactly the
; behaviour the Sett will keep - only the persistence changes.
;
; The rules that outlive M4 and are enforced here:
;   * a secret is never printed - `key list` shows names and lengths, nothing
;     else, and there is no command that reads a secret back out;
;   * a secret is never a command line in the audit trail: `key add` takes the
;     hex, and the shell drops the line from history (see shell history, which
;     redacts `key add`);
;   * replacing a name requires deleting it first, so a typo cannot silently
;     become the volume's key.
;=======================================================================

KEY_SLOTS        equ 8
KEY_NAME_LEN     equ 16             ; 15 characters and a null
KEY_SECRET_BYTES equ 32

align 4
key_names:   times KEY_SLOTS * KEY_NAME_LEN db 0
key_secrets: times KEY_SLOTS * KEY_SECRET_BYTES db 0
key_used:    times KEY_SLOTS db 0
key_count:   dd 0

;-----------------------------------------------------------------------
; key_slot_of - base address of a slot's name and secret
;   In : EAX = slot   Out: EBX = name base, EDI = secret base
;-----------------------------------------------------------------------
key_slot_of:
        mov ebx, eax
        imul ebx, KEY_NAME_LEN
        add ebx, key_names
        mov edi, eax
        imul edi, KEY_SECRET_BYTES
        add edi, key_secrets
        ret

;-----------------------------------------------------------------------
; key_find - look a name up
;   In : ESI = name (null-terminated)
;   Out: EAX = slot, CF clear; CF set when there is no such key
;-----------------------------------------------------------------------
key_find:
        pushad
        mov [.kf_name], esi
        xor ecx, ecx
.kf_loop:
        cmp ecx, KEY_SLOTS
        jae .kf_none
        cmp byte [key_used + ecx], 0
        je .kf_next
        mov eax, ecx
        call key_slot_of
        mov esi, [.kf_name]
        call key_name_equal
        jc .kf_found
.kf_next:
        inc ecx
        jmp .kf_loop
.kf_found:
        mov [esp + 28], ecx
        popad
        clc
        ret
.kf_none:
        popad
        stc
        ret
.kf_name: dd 0

;-----------------------------------------------------------------------
; key_name_equal - compare ESI with the name at EBX
;   Out: CF set when equal
;-----------------------------------------------------------------------
key_name_equal:
        push esi
        push edi
        push ecx
        mov edi, ebx
        mov ecx, KEY_NAME_LEN
        cld
.kne_loop:
        mov al, [esi]
        cmp al, [edi]
        jne .kne_no
        test al, al
        jz .kne_yes
        inc esi
        inc edi
        dec ecx
        jnz .kne_loop
.kne_yes:
        pop ecx
        pop edi
        pop esi
        stc
        ret
.kne_no:
        pop ecx
        pop edi
        pop esi
        clc
        ret

;-----------------------------------------------------------------------
; key_add - store a secret under a name
;   In : ESI = name, EDI = 64 hex characters
;   Out: EAX = slot, CF clear; CF set on bad input or duplicate name
;-----------------------------------------------------------------------
key_add:
        pushad
        mov [.ka_name], esi
        mov [.ka_hex], edi

        ; A key that already exists is refused: an accidental re-add must not
        ; replace the secret a volume was made with.
        call key_find
        jnc .ka_dup

        ; Find a free slot
        xor ecx, ecx
.ka_free:
        cmp ecx, KEY_SLOTS
        jae .ka_full
        cmp byte [key_used + ecx], 0
        je .ka_have
        inc ecx
        jmp .ka_free
.ka_have:
        mov [.ka_slot], ecx
        mov eax, ecx
        call key_slot_of
        mov [.ka_namebase], ebx
        mov [.ka_secretbase], edi

        ; Name: copy, refusing an over-long one.
        mov esi, [.ka_name]
        mov edi, [.ka_namebase]
        mov ecx, KEY_NAME_LEN - 1
.ka_copyname:
        mov al, [esi]
        test al, al
        jz .ka_nameend
        mov [edi], al
        inc esi
        inc edi
        dec ecx
        jnz .ka_copyname
        ; Ran out of room with characters left: refuse rather than truncate,
        ; because two names that differ only past the cut would be one key.
        cmp byte [esi], 0
        jne .ka_badname
.ka_nameend:
        mov byte [edi], 0

        ; Secret: exactly 64 hex characters.
        mov esi, [.ka_hex]
        mov edi, [.ka_secretbase]
        mov dword [.ka_i], 0
.ka_parse:
        cmp dword [.ka_i], KEY_SECRET_BYTES
        jae .ka_parsed
        call parse_hex_byte
        jc .ka_badhex
        mov [edi], al
        inc edi
        inc dword [.ka_i]
        jmp .ka_parse
.ka_parsed:
        ; Nothing may follow: a 65th character is a typo, not a longer key.
        cmp byte [esi], 0
        jne .ka_badhex

        mov eax, [.ka_slot]
        mov byte [key_used + eax], 1
        inc dword [key_count]
        popad
        clc
        ret
.ka_badname:
.ka_badhex:
        ; A refused add must not leave a half-written slot behind.
        mov eax, [.ka_slot]
        mov byte [key_used + eax], 0
        mov eax, [.ka_secretbase]
        mov ecx, KEY_SECRET_BYTES / 4
        xor edx, edx
.push_edx:
        mov [eax], edx
        add eax, 4
        dec ecx
        jnz .push_edx
        popad
        stc
        ret
.ka_dup:
.ka_full:
        popad
        stc
        ret
.ka_name:       dd 0
.ka_hex:        dd 0
.ka_slot:       dd 0
.ka_namebase:   dd 0
.ka_secretbase: dd 0
.ka_i:          dd 0

;-----------------------------------------------------------------------
; key_del - forget a name
;   In : ESI = name   Out: CF set when there is no such key
;-----------------------------------------------------------------------
key_del:
        pushad
        call key_find
        jc .kd_none
        mov [.kd_slot], eax
        mov ecx, eax
        call key_slot_of
        ; Zero the secret before freeing the slot: a deleted key must not be
        ; recoverable from the buffer it used to live in.
        mov ecx, KEY_SECRET_BYTES / 4
        xor edx, edx
.kd_zero:
        mov [edi], edx
        add edi, 4
        dec ecx
        jnz .kd_zero
        mov eax, [.kd_slot]
        mov ecx, KEY_NAME_LEN
        mov edi, eax
        imul edi, KEY_NAME_LEN
        add edi, key_names
        xor edx, edx
.kd_zeroname:
        mov [edi], dl
        inc edi
        dec ecx
        jnz .kd_zeroname
        mov byte [key_used + eax], 0
        dec dword [key_count]
        popad
        clc
        ret
.kd_none:
        popad
        stc
        ret
.kd_slot:   dd 0

;-----------------------------------------------------------------------
; key_secret_of - address of a slot's secret
;   In : EAX = slot   Out: ESI = secret
;-----------------------------------------------------------------------
key_secret_of:
        imul eax, KEY_SECRET_BYTES
        add eax, key_secrets
        mov esi, eax
        ret
