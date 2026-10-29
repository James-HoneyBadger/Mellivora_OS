;=======================================================================
; session.inc - accounts, logon and sessions (M4, SERVICES.md sections 2-4)
;
; There is one identity in the tree before this: uid 0, no account, no session,
; and the console is whoever is typing. This file adds the three things that
; make a machine shared:
;
;   * an account store under \Registry\Machine\Security\Accounts\<name> with a
;     uid, a salt and a hash - never the password;
;   * logon, which verifies the password, makes a session, loads the user's
;     hive and starts the auto services that wait for a user;
;   * a session table. Session 0 is services and is not the console; a logged-on
;     user gets 1, 2 or 3. A program's TCB carries the session it was launched
;     in, and the object namespace already refuses \Sessions\<n>\ to another
;     session without SE_SESSION_CROSS, so "switch user" is a real boundary and
;     not a redraw.
;
; The password hash is Argon2id (kernel/argon2.inc) with the parameters in
; that file, one lane, and the account's eight-byte salt. The record carries a
; scheme number next to the hash: a record written by an older or different
; function is refused rather than compared against the wrong one, and
; `account add` re-sets such a record instead of reporting a duplicate.
;=======================================================================

SESS_MAX         equ 4
SESS_ENTRY       equ 48
SESS_F_ID        equ 0
SESS_F_STATE     equ 4
SESS_F_UID       equ 8
SESS_F_NAME      equ 12
SESS_F_NS        equ 28
SESS_F_CONSOLE   equ 32
SESS_F_TICK      equ 36

SESS_FREE        equ 0
SESS_ACTIVE      equ 1
SESS_LOCKED      equ 2

ACC_PREFIX       db "\Registry\Machine\Security\Accounts", 0
ACC_ROOT         db "\Registry\Machine\Security", 0
ACC_VAL_UID      db "Uid", 0
ACC_VAL_SALT     db "Salt", 0
ACC_VAL_HASH     db "Hash", 0
ACC_VAL_FLAGS    db "Flags", 0
ACC_VAL_ALGO     db "Algo", 0

; The scheme number a stored hash is tagged with. Any change to the Argon2id
; parameters or to a2_lock's shape makes this a new number.
SESS_KDF_SCHEME  equ 1
SESS_PW_MAX      equ 48          ; longer passwords are taken at this length
SESS_KDF_PAGES   equ (A2_M_KIB * 1024) / 4096

USER_PREFIX      db "\Registry\User", 0
SESS_OBJECTS     db "Sessions", 0
SESS_BNO         db "BaseNamedObjects", 0
PROFILE_DIR      db "Users", 0

align 4
sess_table:      times SESS_MAX * SESS_ENTRY db 0
sess_count:      dd 0
sess_current:    dd 0
sess_name_buf:   times 24 db 0
sess_scan_buf:   times 24 db 0
sess_path_buf:   times SETT_KEY_PATH db 0
sess_hive_buf:   times 32 db 0
sess_pw_buf:     times 48 db 0
sess_uid_buf:    times 4 db 0
sess_salt_buf:   times 8 db 0
sess_hash_buf:   times 32 db 0
sess_calc_buf:   times 32 db 0
sess_ident_buf:  times 8 db 0
sess_dec_buf:    times 12 db 0
sess_acct_count: dd 0

align 16
acct_key_material: times 32 db 0
acct_key_expanded: times 240 db 0
acct_block_a:      times 16 db 0
acct_block_b:      times 16 db 0

;-----------------------------------------------------------------------
; sess_path_of_account - ESI = account name -> sess_path_buf
;-----------------------------------------------------------------------
sess_path_of_account:
        push eax
        push edi
        mov [.spa_name], esi
        lea edi, [sess_path_buf]
        mov esi, ACC_PREFIX
        call str_copy
        dec edi
        mov al, '\'
        stosb
        mov esi, [.spa_name]
        call str_copy
        lea esi, [sess_path_buf]
        pop edi
        pop eax
        ret
.spa_name: dd 0

;-----------------------------------------------------------------------
; sess_uid_alloc - EAX = the next uid to hand out (1000 and up)
;-----------------------------------------------------------------------
;-----------------------------------------------------------------------
; sess_uid_alloc - EAX = the next uid to hand out (1000 and up)
;-----------------------------------------------------------------------
sess_uid_alloc:
        pushad
        mov dword [.sua_i], 0
        mov dword [.sua_max], 999
.sua_loop:
        mov eax, [.sua_i]
        mov esi, ACC_PREFIX
        lea edi, [sess_scan_buf]
        mov edx, 23
        call sett_child_name
        jc .sua_done
        lea esi, [sess_scan_buf]
        call sess_path_of_account
        mov edi, esi
        mov esi, ACC_VAL_UID
        call sett_kv_get_u32
        jc .sua_next
        cmp eax, [.sua_max]
        jbe .sua_next
        mov [.sua_max], eax
.sua_next:
        inc dword [.sua_i]
        cmp dword [.sua_i], 32
        jb .sua_loop
.sua_done:
        mov eax, [.sua_max]
        inc eax
        mov [esp + 28], eax
        popad
        ret
.sua_i:   dd 0
.sua_max: dd 0

;=======================================================================
; The password function
;=======================================================================

;-----------------------------------------------------------------------
; sess_chunk8 - eight password bytes from an offset, zero padded
;   In : ESI = password, EDX = offset, EDI = destination
;-----------------------------------------------------------------------
sess_chunk8:
        push eax
        push ecx
        push esi
        push edi
        mov ecx, edx
.ch8_skip:
        test ecx, ecx
        jz .ch8_copy
        cmp byte [esi], 0
        je .ch8_skip_pad
        inc esi
        dec ecx
        jmp .ch8_skip
.ch8_skip_pad:
        mov ecx, 8
        jmp .ch8_pad
.ch8_copy:
        mov ecx, 8
.ch8_loop:
        test ecx, ecx
        jz .ch8_done
        mov al, [esi]
        test al, al
        jz .ch8_pad
        mov [edi], al
        inc esi
        inc edi
        dec ecx
        jmp .ch8_loop
.ch8_pad:
        xor eax, eax
        rep stosb
.ch8_done:
        pop edi
        pop esi
        pop ecx
        pop eax
        ret

; ESI = password, EDI = 8-byte salt, EBX = 32-byte output
; Argon2id, one lane, the parameters in argon2.inc. The memory cost is taken
; from the page allocator for the duration of the hash and given back after,
; so a logon is the only moment a MiB is held. CF set when the memory cannot
; be had or the parameters are refused - a caller must not treat a failure as
; a hash.
sess_hash_password:
        pushad
        mov [.shp_pw], esi
        mov [.shp_salt], edi
        mov [.shp_out], ebx
        xor edx, edx
.shp_scan:
        cmp edx, SESS_PW_MAX
        jae .shp_have_len
        cmp byte [esi + edx], 0
        je .shp_have_len
        inc edx
        jmp .shp_scan
.shp_have_len:
        mov [.shp_len], edx

        mov ecx, SESS_KDF_PAGES          ; pages for the memory cost
        call pmm_alloc_pages
        test eax, eax
        jz .shp_fail
        mov [.shp_mem], eax

        lea ebx, [sess_kdf_par]
        mov eax, [.shp_pw]
        mov [ebx + A2P_PWD], eax
        mov eax, [.shp_len]
        mov [ebx + A2P_PWDLEN], eax
        mov eax, [.shp_salt]
        mov [ebx + A2P_SALT], eax
        mov dword [ebx + A2P_SALTLEN], 8
        mov eax, [.shp_out]
        mov [ebx + A2P_OUT], eax
        mov dword [ebx + A2P_OUTLEN], A2_TAGLEN
        mov dword [ebx + A2P_MKIB], A2_M_KIB
        mov dword [ebx + A2P_TCOST], A2_T_COST
        mov eax, [.shp_mem]
        mov [ebx + A2P_MEM], eax
        mov eax, ebx
        call argon2id_hash
        mov [.shp_status], eax

        mov eax, [.shp_mem]
        mov ecx, SESS_KDF_PAGES
        call pmm_free_pages
        cmp dword [.shp_status], 0
        jne .shp_fail
        popad
        clc
        ret
.shp_fail:
        popad
        stc
        ret
.shp_pw:     dd 0
.shp_salt:   dd 0
.shp_out:    dd 0
.shp_len:    dd 0
.shp_mem:    dd 0
.shp_status: dd 0

sess_kdf_par: times A2P_SIZE db 0

;=======================================================================
; Accounts
;=======================================================================

; Read uid, salt and hash for an account.
;   In : ESI = name -> CF set when the account is missing or malformed
sess_load_account:
        pushad
        mov [.sla_name], esi
        call sess_path_of_account
        mov [.sla_path], esi
        mov edi, esi
        mov esi, ACC_VAL_UID
        call sett_kv_get_u32
        jc .sla_no
        mov [sess_uid_buf], eax
        ; A record from another scheme cannot be verified by this one.
        mov edi, [.sla_path]
        mov esi, ACC_VAL_ALGO
        call sett_kv_get_u32
        jc .sla_no
        cmp eax, SESS_KDF_SCHEME
        jne .sla_no
        mov edi, [.sla_path]
        mov esi, ACC_VAL_SALT
        call sett_kv_lookup
        jc .sla_no
        call sett_val_base
        cmp dword [ebx + 4], SETT_T_BINARY
        jne .sla_no
        cmp dword [ebx + 12], 8
        jne .sla_no
        lea esi, [sett_arena]
        add esi, [ebx + 8]
        lea edi, [sess_salt_buf]
        mov ecx, 8
        cld
        rep movsb
        mov edi, [.sla_path]
        mov esi, ACC_VAL_HASH
        call sett_kv_lookup
        jc .sla_no
        call sett_val_base
        cmp dword [ebx + 4], SETT_T_BINARY
        jne .sla_no
        cmp dword [ebx + 12], 32
        jne .sla_no
        lea esi, [sett_arena]
        add esi, [ebx + 8]
        lea edi, [sess_hash_buf]
        mov ecx, 32
        cld
        rep movsb
        popad
        clc
        ret
.sla_no:
        popad
        stc
        ret
.sla_name: dd 0
.sla_path: dd 0

; ESI = password, EDI = salt -> sess_calc_buf holds the hash
sess_calc:
        lea ebx, [sess_calc_buf]
        call sess_hash_password
        ret

; Verify a name and password.
;   In : ESI = name, EDI = password -> EAX = uid, CF set when refused
sess_verify:
        pushad
        mov [.sv_name], esi
        mov [.sv_pw], edi
        call sess_load_account
        jc .sv_no
        mov esi, [.sv_pw]
        lea edi, [sess_salt_buf]
        call sess_calc
        jc .sv_no
        ; compare the two 32-byte hashes
        lea esi, [sess_calc_buf]
        lea edi, [sess_hash_buf]
        mov ecx, 32
.sv_cmp:
        mov al, [esi]
        cmp al, [edi]
        jne .sv_no
        inc esi
        inc edi
        dec ecx
        jnz .sv_cmp
        mov eax, [sess_uid_buf]
        mov [esp + 28], eax              ; EAX position in the pushad frame
        popad
        clc
        ret
.sv_no:
        popad
        stc
        ret
.sv_name: dd 0
.sv_pw:   dd 0
.sv_uid:  dd 0

;=======================================================================
; Sessions
;=======================================================================

; EAX = id -> EDI = session record, CF set when there is no such session
sess_slot:
        cmp eax, SESS_MAX
        jae .ssl_no
        imul eax, SESS_ENTRY
        add eax, sess_table
        mov edi, eax
        clc
        ret
.ssl_no:
        stc
        ret

; Find a free session slot from 1 (0 is services) -> EAX = id, CF when full
sess_alloc:
        push ecx
        mov ecx, 1
.sal_loop:
        cmp ecx, SESS_MAX
        jae .sal_full
        mov eax, ecx
        imul eax, SESS_ENTRY
        add eax, sess_table
        cmp dword [eax + SESS_F_STATE], SESS_FREE
        je .sal_found
        inc ecx
        jmp .sal_loop
.sal_found:
        mov eax, ecx
        pop ecx
        clc
        ret
.sal_full:
        pop ecx
        stc
        ret

; Print the session table.
sess_print_all:
        pushad
        mov dword [.spa_i], 0
.spa_loop:
        mov eax, [.spa_i]
        cmp eax, SESS_MAX
        jae .spa_done
        call sess_slot
        jc .spa_done
        cmp dword [edi + SESS_F_STATE], SESS_FREE
        je .spa_next
        mov [.spa_rec], edi
        mov eax, [edi + SESS_F_ID]
        call vga_print_dec
        mov esi, msg_sess_name
        call vga_print
        mov edi, [.spa_rec]
        lea esi, [edi + SESS_F_NAME]
        call vga_print
        mov esi, msg_sess_uid
        call vga_print
        mov edi, [.spa_rec]
        mov eax, [edi + SESS_F_UID]
        call vga_print_dec
        mov esi, msg_sess_state
        call vga_print
        mov edi, [.spa_rec]
        mov eax, [edi + SESS_F_STATE]
        call sess_state_word
        call vga_print
        cmp dword [sess_current], 0
        je .spa_nocons
        mov eax, [sess_current]
        cmp eax, [edi + SESS_F_ID]
        jne .spa_nocons
        mov esi, msg_sess_console
        call vga_print
.spa_nocons:
        call vga_newline
.spa_next:
        inc dword [.spa_i]
        jmp .spa_loop
.spa_done:
        popad
        ret
.spa_i:   dd 0
.spa_rec: dd 0

sess_state_word:
        cmp eax, SESS_ACTIVE
        je .ssw_active
        cmp eax, SESS_LOCKED
        je .ssw_locked
        mov esi, msg_sess_freed
        ret
.ssw_active:
        mov esi, msg_sess_active
        ret
.ssw_locked:
        mov esi, msg_sess_lockedword
        ret

; Make \Sessions\<id> and its BaseNamedObjects exist.
;   In : EAX = id
sess_make_namespace:
        pushad
        mov [.smn_id], eax
        ; The name, in decimal, in a buffer the link copies.
        mov eax, [.smn_id]
        lea edi, [sess_dec_buf]
        call sess_dec
        ; Sessions 0 and 1 are built at boot; a later one is created here.
        cmp dword [.smn_id], 0
        je .smn_done
        cmp dword [.smn_id], 1
        je .smn_done                     ; built at boot
        mov eax, OBJ_T_DIRECTORY
        lea ebx, [sess_dec_buf]
        mov ecx, [obj_sessions]
        mov edx, INTEGRITY_SYSTEM
        call obj_new_named
        test eax, eax
        jz .smn_done
        mov [.smn_ns], eax
        mov eax, OBJ_T_DIRECTORY
        mov ebx, SESS_BNO
        mov ecx, [.smn_ns]
        mov edx, INTEGRITY_MEDIUM
        call obj_new_named
.smn_done:
        popad
        ret
.smn_id: dd 0
.smn_ns: dd 0

; EAX = id, EDI = destination -> decimal text (NUL-terminated)
sess_dec:
        pushad
        mov [.sd_id], eax
        mov [.sd_dest], edi
        lea ebx, [sess_ident_buf + 11]
        mov byte [ebx], 0
        mov eax, [.sd_id]
        mov ecx, 10
.sd_loop:
        xor edx, edx
        div ecx
        add dl, '0'
        dec ebx
        mov [ebx], dl
        test eax, eax
        jnz .sd_loop
        mov esi, ebx
        mov edi, [.sd_dest]
        call str_copy
        popad
        ret
.sd_id:   dd 0
.sd_dest: dd 0

;=======================================================================
; Logon, logoff, lock, unlock, switch
;=======================================================================

; ESI = name, EDI = password -> EAX = session id, CF set when refused
sess_logon:
        pushad
        mov [.slo_name], esi
        mov [.slo_pw], edi
        call sess_verify
        jc .slo_refused
        mov [.slo_uid], eax

        ; Refuse a second logon of the same account: one account, one session.
        mov dword [.slo_i], 1
.slo_dup:
        mov eax, [.slo_i]
        cmp eax, SESS_MAX
        jae .slo_alloc
        call sess_slot
        cmp dword [edi + SESS_F_STATE], SESS_FREE
        je .slo_dup_next
        mov eax, [edi + SESS_F_UID]
        cmp eax, [.slo_uid]
        jne .slo_dup_next
        mov esi, msg_sess_alreadylogon
        call vga_print
        jmp .slo_refused
.slo_dup_next:
        inc dword [.slo_i]
        jmp .slo_dup

.slo_alloc:
        call sess_alloc
        jc .slo_full
        mov [.slo_id], eax
        call sess_slot
        mov [.slo_rec], edi
        mov eax, [.slo_id]
        mov [edi + SESS_F_ID], eax
        mov dword [edi + SESS_F_STATE], SESS_ACTIVE
        mov eax, [.slo_uid]
        mov [edi + SESS_F_UID], eax
        mov eax, [tick_count]
        mov [edi + SESS_F_TICK], eax
        lea edi, [edi + SESS_F_NAME]
        mov esi, [.slo_name]
        call str_copy

        mov eax, [.slo_id]
        call sess_make_namespace

        ; The profile: a directory under /Users, and a hive file for the user's
        ; keys. A missing profile is created, and creating it is one write.
        lea esi, [.slo_name]
        call sess_profile_create

        ; Load the user's hive under \Registry\User\<name>.
        lea edi, [sess_hive_buf]
        mov esi, [.slo_name]
        call sess_hive_name
        lea esi, [sess_hive_buf]
        call sess_hive_exists
        jc .slo_nohive
        lea esi, [sess_hive_buf]
        call sett_hive_load
.slo_nohive:

        mov eax, [.slo_id]
        mov [sess_current], eax

        ; Auto services and logon tasks start once there is a session.
        call svc_start_auto
        call taskq_logon

        mov esi, msg_sess_welcome
        call vga_print
        mov esi, [.slo_name]
        call vga_print
        mov esi, msg_sess_session
        call vga_print
        mov eax, [.slo_id]
        call vga_print_dec
        call vga_newline
        mov eax, [.slo_id]
        mov [esp + 28], eax
        popad
        clc
        ret
.slo_refused:
        ; The audit record does not say which half was wrong.
        LOGBOOT LOG_WARN, msg_sess_logonfail
        mov esi, msg_sess_badlogin
        call vga_print
        popad
        stc
        ret
.slo_full:
        mov esi, msg_sess_nofree
        call vga_print
        popad
        stc
        ret
.slo_name: dd 0
.slo_pw:   dd 0
.slo_uid:  dd 0
.slo_id:   dd 0
.slo_rec:  dd 0
.slo_i:    dd 0

; ESI = user name -> sess_hive_buf = "<name>.hiv"
sess_hive_name:
        push eax
        push edi
        push esi
        mov edi, sess_hive_buf
        call str_copy
        lea edi, [sess_hive_buf]
.shn_len:
        cmp byte [edi], 0
        je .shn_add
        inc edi
        jmp .shn_len
.shn_add:
        mov dword [edi], '.hiv'
        mov byte [edi + 4], 0
        pop esi
        pop edi
        pop eax
        ret

; ESI = file name -> CF set when the file is not there
sess_hive_exists:
        pushad
        call file_save_cwd
        call sett_pin_root
        call hbfs_load_root_dir
        call hbfs_find_file
        jc .she_no
        call file_restore_cwd
        popad
        clc
        ret
.she_no:
        call file_restore_cwd
        popad
        stc
        ret

; ESI = user name: create /Users and /Users/<name>, leaving the hive file name
; in sess_hive_buf.
sess_profile_create:
        pushad
        mov [.spc_name], esi
        call path_save_cwd
        call sett_pin_root
        mov esi, PROFILE_DIR
        call hbfs_mkdir                    ; already there is not an error below
        mov esi, PROFILE_DIR
        call cmd_cd_internal
        test eax, eax
        jnz .spc_restore
        mov esi, [.spc_name]
        call hbfs_mkdir
.spc_restore:
        call path_restore_cwd
        popad
        ret
.spc_name: dd 0

; Log the current session off: save and unload its hive, free the slot.
sess_logoff:
        pushad
        mov eax, [sess_current]
        cmp eax, 0
        je .slo_none
        call sess_slot
        jc .slo_none
        mov [.slo_rec], edi
        ; Save the hive, then drop the user's keys from the live tree.
        lea esi, [edi + SESS_F_NAME]
        call sess_hive_name
        ; The hive's keys are \Registry\User\<name>
        lea edi, [sess_path_buf]
        mov esi, USER_PREFIX
        call str_copy
        dec edi
        mov al, '\'
        stosb
        mov edi, [.slo_rec]
        lea esi, [edi + SESS_F_NAME]
        call str_copy
        lea esi, [sess_path_buf]
        lea edi, [sess_hive_buf]
        call sett_hive_save
        lea edi, [sess_path_buf]
        call sett_kv_delkey
        ; Free the record.
        mov edi, [.slo_rec]
        mov dword [edi + SESS_F_STATE], SESS_FREE
        mov dword [sess_current], 0
        mov esi, msg_sess_loggedoff
        call vga_print
        jmp .slo_done
.slo_none:
        mov esi, msg_sess_nosession
        call vga_print
.slo_done:
        popad
        ret
.slo_rec: dd 0

;=======================================================================
; Who the shell is, and the commands
;=======================================================================

; Print the identity the sessions give the console.
sess_whoami:
        pushad
        mov eax, [sess_current]
        test eax, eax
        jz .sw_system
        call sess_slot
        mov [.sw_rec], edi
        mov eax, [edi + SESS_F_UID]
        mov ebx, eax
        mov esi, msg_who_user
        call vga_print
        mov edi, [.sw_rec]
        lea esi, [edi + SESS_F_NAME]
        call vga_print
        mov esi, msg_who_uid
        call vga_print
        mov edi, [.sw_rec]
        mov eax, [edi + SESS_F_UID]
        call vga_print_dec
        mov esi, msg_who_session
        call vga_print
        mov edi, [.sw_rec]
        mov eax, [edi + SESS_F_ID]
        call vga_print_dec
        mov esi, msg_who_groups
        call vga_print
        mov edi, [.sw_rec]
        mov eax, [edi + SESS_F_UID]
        call vga_print_dec
        mov esi, msg_who_privs_user
        call vga_print
        call vga_newline
        jmp .sw_done
.sw_system:
        mov esi, msg_who_system
        call vga_print
.sw_done:
        popad
        ret
.sw_rec: dd 0

; `account ...`
sess_account_command:
        pushad
        mov esi, line_buffer
        mov edi, cmd_account_str
        call str_starts_with
        jc .have
        jmp .usage
.have:
        mov edi, sess_name_buf
        mov edx, 16
        call sett_word
        jc .usage
        mov [.argp], esi
        mov esi, sess_name_buf
        mov edi, acct_word_add
        call sett_ci_equal
        jc .add
        mov esi, sess_name_buf
        mov edi, acct_word_del
        call sett_ci_equal
        jc .del
        mov esi, sess_name_buf
        mov edi, acct_word_list
        call sett_ci_equal
        jc .list
        jmp .usage

.add:
        mov esi, [.argp]
        mov edi, sess_name_buf
        mov edx, 15
        call sett_word
        jc .usage
        mov edi, sess_pw_buf
        mov edx, 31
        call sett_word
        jc .usage
        ; Refuse a duplicate, unless the record was written by another scheme:
        ; that one cannot be verified any more, so `account add` re-sets it
        ; under the same uid rather than leaving a name nobody can log on with.
        lea esi, [sess_name_buf]
        call sess_path_of_account
        mov edi, esi
        mov esi, ACC_VAL_UID
        call sett_kv_get_u32
        jc .add_new
        mov [.uid], eax
        mov edi, esi
        mov esi, ACC_VAL_ALGO
        call sett_kv_get_u32
        jnc .add_have_algo
        jmp .add_stale
.add_have_algo:
        cmp eax, SESS_KDF_SCHEME
        jne .add_stale
        jmp .add_dup
.add_new:
        call sess_uid_alloc
        mov [.uid], eax
.add_stale:
        ; Salt from the clock: the tick and the RTC's second, twice, so two
        ; accounts made in the same second still differ.
        mov eax, [tick_count]
        mov [sess_salt_buf], eax
        mov eax, [tick_count]
        imul eax, 2654435761
        xor eax, [sess_uid_buf]
        mov [sess_salt_buf + 4], eax
        lea esi, [sess_pw_buf]
        lea edi, [sess_salt_buf]
        lea ebx, [sess_calc_buf]
        call sess_hash_password
        jc .add_fail
        ; sess_uid_alloc walks the account keys and that walk shares
        ; sess_path_buf, so the path is taken again before anything is written.
        lea esi, [sess_name_buf]
        call sess_path_of_account
        mov [.path], esi
        ; Write uid, scheme, salt and hash.
        mov edi, [.path]
        mov esi, ACC_VAL_UID
        mov ebx, [.uid]
        call sett_kv_set_u32
        jc .add_fail
        mov edi, [.path]
        mov esi, ACC_VAL_ALGO
        mov ebx, SESS_KDF_SCHEME
        call sett_kv_set_u32
        jc .add_fail
        mov edi, [.path]
        mov esi, ACC_VAL_SALT
        lea edx, [sess_salt_buf]
        mov ecx, 8
        call sett_kv_set_bin
        jc .add_fail
        mov edi, [.path]
        mov esi, ACC_VAL_HASH
        lea edx, [sess_calc_buf]
        mov ecx, 32
        call sett_kv_set_bin
        jc .add_fail
        call sett_save
        jc .add_fail
        mov esi, msg_acc_added
        call vga_print
        lea esi, [sess_name_buf]
        call vga_print
        call vga_newline
        jmp .done
.add_dup:
        mov esi, msg_acc_dup
        call vga_print
        jmp .done
.add_fail:
        mov esi, msg_acc_fail
        call vga_print
        jmp .done

.del:
        mov esi, [.argp]
        mov edi, sess_name_buf
        mov edx, 15
        call sett_word
        jc .usage
        lea esi, [sess_name_buf]
        call sess_path_of_account
        mov edi, esi
        call sett_kv_delkey
        jc .del_none
        call sett_save
        mov esi, msg_acc_deleted
        call vga_print
        jmp .done
.del_none:
        mov esi, msg_acc_nosuch
        call vga_print
        jmp .done

.list:
        mov dword [.i], 0
.list_loop:
        mov eax, [.i]
        mov esi, ACC_PREFIX
        lea edi, [sess_name_buf]
        mov edx, 23
        call sett_child_name
        jc .list_done
        lea esi, [sess_name_buf]
        call sess_path_of_account
        mov edi, esi
        mov esi, ACC_VAL_UID
        call sett_kv_get_u32
        mov [.uid], eax
        lea esi, [sess_name_buf]
        call vga_print
        mov esi, msg_acc_uid
        call vga_print
        mov eax, [.uid]
        call vga_print_dec
        call vga_newline
        inc dword [.i]
        jmp .list_loop
.list_done:
        cmp dword [.i], 0
        jne .done
        mov esi, msg_acc_none
        call vga_print
        jmp .done
.usage:
        mov esi, msg_acc_usage
        call vga_print
.done:
        popad
        ret
.uid:  dd 0
.path: dd 0
.i:    dd 0
.argp: dd 0

; `logon NAME PASSWORD`
sess_logon_command:
        pushad
        mov esi, line_buffer
        mov edi, cmd_logon_str
        call str_starts_with
        jc .have
        jmp .usage
.have:
        mov edi, sess_name_buf
        mov edx, 15
        call sett_word
        jc .usage
        mov edi, sess_pw_buf
        mov edx, 31
        call sett_word
        jc .usage
        lea esi, [sess_name_buf]
        lea edi, [sess_pw_buf]
        call sess_logon
        jc .done
        jmp .done
.usage:
        mov esi, msg_logon_usage
        call vga_print
.done:
        popad
        ret

; `logoff [ID]`
sess_logoff_command:
        pushad
        mov esi, line_buffer
        mov edi, cmd_logoff_str
        call str_starts_with
        jc .have
        jmp .usage
.have:
        mov edi, sess_name_buf
        mov edx, 15
        call sett_word
        jc .current
        lea esi, [sess_name_buf]
        call parse_decimal
        mov [sess_current], eax
.current:
        call sess_logoff
        jmp .done
.usage:
        mov esi, msg_logoff_usage
        call vga_print
.done:
        popad
        ret

; `sessions`
sess_sessions_command:
        pushad
        mov esi, msg_sess_header
        call vga_print
        call sess_print_all
        popad
        ret

;-----------------------------------------------------------------------
; sess_command_allowed - may the console run this line at all?
;   In : ESI = the command line
;   Out: CF set when the current session is locked and the line is not the
;        command that unlocks it.
;
; A locked session accepts `unlock' and nothing else. The test is on the
; session's state in the table, not on the task that asked, so a logon screen or
; a shell that dies while the session is locked leaves it locked: there is no
; task to crash back to that would carry the right to read the session's files.
;-----------------------------------------------------------------------
sess_command_allowed:
        push eax
        push ebx
        push edi
        push esi                         ; str_starts_with leaves ESI past the word
        mov eax, [sess_current]
        test eax, eax
        jz .sca_yes
        call sess_slot
        cmp dword [edi + SESS_F_STATE], SESS_LOCKED
        jne .sca_yes
        mov edi, cmd_unlock_str
        call str_starts_with
        jc .sca_yes
        stc
        jmp .sca_out
.sca_yes:
        clc
.sca_out:
        pop esi
        pop edi
        pop ebx
        pop eax
        ret

; `lock`
sess_lock_command:
        pushad
        mov eax, [sess_current]
        test eax, eax
        jz .none
        call sess_slot
        mov dword [edi + SESS_F_STATE], SESS_LOCKED
        mov esi, msg_sess_locked
        call vga_print
        jmp .done
.none:
        mov esi, msg_sess_nosession
        call vga_print
.done:
        popad
        ret

; `unlock PASSWORD`
sess_unlock_command:
        pushad
        mov esi, line_buffer
        mov edi, cmd_unlock_str
        call str_starts_with
        jc .have
        jmp .usage
.have:
        mov edi, sess_pw_buf
        mov edx, 31
        call sett_word
        jc .usage
        mov eax, [sess_current]
        test eax, eax
        jz .none
        call sess_slot
        cmp dword [edi + SESS_F_STATE], SESS_LOCKED
        jne .notlocked
        lea esi, [edi + SESS_F_NAME]
        lea edi, [sess_pw_buf]
        call sess_verify
        jc .bad
        mov eax, [sess_current]
        call sess_slot
        mov dword [edi + SESS_F_STATE], SESS_ACTIVE
        mov esi, msg_sess_unlocked
        call vga_print
        jmp .done
.bad:
        mov esi, msg_sess_badlogin
        call vga_print
        jmp .done
.notlocked:
        mov esi, msg_sess_notlocked
        call vga_print
        jmp .done
.none:
        mov esi, msg_sess_nosession
        call vga_print
        jmp .done
.usage:
        mov esi, msg_unlock_usage
        call vga_print
.done:
        popad
        ret

; `switch ID`
sess_switch_command:
        pushad
        mov esi, line_buffer
        mov edi, cmd_switch_str
        call str_starts_with
        jc .have
        jmp .usage
.have:
        mov edi, sess_name_buf
        mov edx, 15
        call sett_word
        jc .usage
        lea esi, [sess_name_buf]
        call parse_decimal
        mov [.id], eax
        call sess_slot
        jc .nosuch
        cmp dword [edi + SESS_F_STATE], SESS_FREE
        je .nosuch
        mov eax, [.id]
        mov [sess_current], eax
        mov esi, msg_sess_switched
        call vga_print
        mov eax, [.id]
        call vga_print_dec
        call vga_newline
        jmp .done
.nosuch:
        mov esi, msg_sess_nosuch
        call vga_print
        jmp .done
.usage:
        mov esi, msg_switch_usage
        call vga_print
.done:
        popad
        ret
.id: dd 0

;=======================================================================
; Boot
;=======================================================================

; Session 0 is services: it exists before anybody logs on and owns no profile.
sess_boot:
        pushad
        ; Slot 0, always.
        xor eax, eax
        call sess_slot
        mov dword [edi + SESS_F_ID], 0
        mov dword [edi + SESS_F_STATE], SESS_ACTIVE
        mov dword [edi + SESS_F_UID], 0
        mov dword [edi + SESS_F_NS], 0
        lea edi, [edi + SESS_F_NAME]
        mov esi, msg_sess_system
        call str_copy
        mov dword [sess_current], 0
        popad
        ret

acct_word_add:    db "add", 0
acct_word_del:    db "del", 0
acct_word_list:   db "list", 0

msg_sess_system:  db "SYSTEM", 0
msg_sess_name:    db " name=", 0
msg_sess_uid:     db " uid=", 0
msg_sess_state:   db " state=", 0
msg_sess_console: db " console", 0
msg_sess_active:  db "active", 0
msg_sess_lockedword: db "locked", 0
msg_sess_freed:   db "free", 0
msg_sess_header:  db "Sessions (id name uid state):", 0x0A, 0
msg_sess_alreadylogon: db "logon: that account already has a session", 0x0A, 0
msg_sess_badlogin: db "logon: the name or the password is wrong", 0x0A, 0
msg_sess_logonfail: db "logon: refused, the name or the password was wrong", 0
msg_sess_welcome: db "logon: welcome, ", 0
msg_sess_session: db " (session ", 0
msg_sess_nofree:  db "logon: no free session", 0x0A, 0
msg_sess_loggedoff: db "logoff: profile saved and hive unloaded", 0x0A, 0
msg_sess_nosession: db "sessions: nobody is logged on", 0x0A, 0
msg_sess_nosuch:  db "switch: no such session", 0x0A, 0
msg_sess_switched: db "switch: the console belongs to session ", 0
msg_sess_locked:  db "lock: the session is locked", 0x0A, 0
msg_sess_lockedcmd: db "locked: unlock PASSWORD", 0x0A, 0
msg_sess_unlocked: db "unlock: the session is active again", 0x0A, 0
msg_sess_notlocked: db "unlock: the session is not locked", 0x0A, 0
msg_who_system:   db "SYSTEM uid=0 session=0 groups=Administrators privileges=all", 0x0A, 0
msg_who_user:     db "user=", 0
msg_who_uid:      db " uid=", 0
msg_who_session:  db " session=", 0
msg_who_groups:   db " groups=Users", 0
msg_who_privs_user: db " privileges=SE_SHUTDOWN", 0x0A, 0
msg_acc_added:    db "account: added ", 0
msg_acc_dup:      db "account: that name already exists", 0x0A, 0
msg_acc_fail:     db "account: could not write the account", 0x0A, 0
msg_acc_deleted:  db "account: deleted", 0x0A, 0
msg_acc_nosuch:   db "account: no such account", 0x0A, 0
msg_acc_none:     db "account: no accounts", 0x0A, 0
msg_acc_uid:      db " uid=", 0
msg_acc_usage:
        db "account add NAME PASSWORD | account del NAME | account list", 0x0A, 0
msg_logon_usage:  db "logon NAME PASSWORD", 0x0A, 0
msg_logoff_usage: db "logoff [ID]", 0x0A, 0
msg_unlock_usage: db "unlock PASSWORD", 0x0A, 0
msg_switch_usage: db "switch ID", 0x0A, 0
