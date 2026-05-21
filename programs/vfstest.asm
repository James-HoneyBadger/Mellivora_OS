; vfstest.asm — VFS layer integration test for Mellivora OS v10
;
; Tests:
;   1. SYS_FREAD  /proc/version       (procfs read)
;   2. SYS_OPEN + SYS_READ + SYS_CLOSE  /proc/meminfo  (fd-based procfs)
;   3. SYS_FWRITE /tmp/vfstest.txt    (tmpfs write)
;   4. SYS_FREAD  /tmp/vfstest.txt    (tmpfs read-back)
;   5. SYS_OPEN + SYS_READ + SYS_CLOSE  /dev/zero  (devfs read, verify zeros)
;   6. SYS_CHDIR /dev + SYS_READDIR   (CWD VFS navigation + devfs readdir)
;   7. SYS_DELETE /tmp/vfstest.txt    (tmpfs unlink)
;   8. SYS_OPEN + SYS_WRITE /dev/full  (write must return -1 / ENOSPC)
;   9. Pass/fail summary

%include "syscalls.inc"

start:
        mov eax, SYS_PRINT
        mov ebx, msg_banner
        int 0x80

;-----------------------------------------------
; Test 1: SYS_FREAD /proc/version
;-----------------------------------------------
.t1:
        mov eax, SYS_PRINT
        mov ebx, msg_t1
        int 0x80

        mov eax, SYS_FREAD
        mov ebx, path_proc_version
        mov ecx, io_buf
        int 0x80

        test eax, eax
        jz .t1_fail

        ; Null-terminate just in case (EAX = bytes read)
        mov byte [io_buf + eax], 0
        mov eax, SYS_PRINT
        mov ebx, io_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_newline
        int 0x80
        jmp .t2

.t1_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        inc dword [fail_count]

;-----------------------------------------------
; Test 2: SYS_OPEN + SYS_READ + SYS_CLOSE  /proc/meminfo
;-----------------------------------------------
.t2:
        mov eax, SYS_PRINT
        mov ebx, msg_t2
        int 0x80

        mov eax, SYS_OPEN
        mov ebx, path_proc_meminfo
        mov ecx, 1              ; FD_FLAG_READ
        int 0x80

        cmp eax, -1
        je .t2_fail

        mov [t2_fd], eax

        mov eax, SYS_READ
        mov ebx, [t2_fd]
        mov ecx, io_buf
        mov edx, IO_BUF_SIZE - 1
        int 0x80

        cmp eax, -1
        je .t2_close_fail

        mov byte [io_buf + eax], 0
        mov eax, SYS_PRINT
        mov ebx, io_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_newline
        int 0x80
        jmp .t2_close

.t2_close_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        inc dword [fail_count]

.t2_close:
        mov eax, SYS_CLOSE
        mov ebx, [t2_fd]
        int 0x80
        jmp .t3

.t2_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        inc dword [fail_count]

;-----------------------------------------------
; Test 3: SYS_FWRITE /tmp/vfstest.txt
;-----------------------------------------------
.t3:
        mov eax, SYS_PRINT
        mov ebx, msg_t3
        int 0x80

        mov eax, SYS_FWRITE
        mov ebx, path_tmp_test
        mov ecx, payload_data
        mov edx, payload_len
        xor esi, esi            ; FTYPE_TEXT = 0
        int 0x80

        cmp eax, -1
        je .t3_fail

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        jmp .t4

.t3_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        inc dword [fail_count]

;-----------------------------------------------
; Test 4: SYS_FREAD /tmp/vfstest.txt (read-back verify)
;-----------------------------------------------
.t4:
        mov eax, SYS_PRINT
        mov ebx, msg_t4
        int 0x80

        mov eax, SYS_FREAD
        mov ebx, path_tmp_test
        mov ecx, io_buf
        int 0x80

        test eax, eax
        jz .t4_fail

        ; Verify first bytes match payload_data
        mov byte [io_buf + eax], 0
        mov esi, io_buf
        mov edi, payload_data
        mov ecx, payload_len
.t4_cmp:
        cmpsb
        jne .t4_mismatch
        loop .t4_cmp

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        jmp .t5

.t4_fail:
.t4_mismatch:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        inc dword [fail_count]

;-----------------------------------------------
; Test 5: SYS_OPEN + SYS_READ /dev/zero (verify zero bytes)
;-----------------------------------------------
.t5:
        mov eax, SYS_PRINT
        mov ebx, msg_t5
        int 0x80

        mov eax, SYS_OPEN
        mov ebx, path_dev_zero
        mov ecx, 1              ; FD_FLAG_READ
        int 0x80

        cmp eax, -1
        je .t5_fail

        mov [t2_fd], eax        ; reuse fd slot

        mov eax, SYS_READ
        mov ebx, [t2_fd]
        mov ecx, io_buf
        mov edx, 16
        int 0x80

        cmp eax, -1
        je .t5_close_fail

        ; Verify all 16 bytes are zero
        mov ecx, eax            ; bytes returned
        mov esi, io_buf
.t5_verify:
        lodsb
        test al, al
        jnz .t5_nonzero
        loop .t5_verify

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        jmp .t5_close

.t5_nonzero:
.t5_close_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        inc dword [fail_count]

.t5_close:
        mov eax, SYS_CLOSE
        mov ebx, [t2_fd]
        int 0x80
        jmp .t6

.t5_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        inc dword [fail_count]

;-----------------------------------------------
; Test 6: SYS_READDIR in /dev (CWD-based VFS dispatch)
;-----------------------------------------------
.t6:
        mov eax, SYS_PRINT
        mov ebx, msg_t6
        int 0x80

        ; Change to /dev so sys_readdir uses the devfs backend
        mov eax, SYS_CHDIR
        mov ebx, path_dev
        int 0x80

        cmp eax, -1
        je .t6_fail

        ; List up to 8 entries
        xor ebp, ebp            ; entry index
.t6_loop:
        cmp ebp, 8
        jge .t6_done

        mov eax, SYS_READDIR
        mov ebx, name_buf
        mov ecx, ebp
        int 0x80

        cmp eax, -1
        je .t6_done             ; end of directory

        ; Print "  <name>\n"
        push eax
        mov eax, SYS_PRINT
        mov ebx, msg_indent
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, name_buf
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_newline
        int 0x80
        pop eax

        inc ebp
        jmp .t6_loop

.t6_done:
        ; Return to root
        mov eax, SYS_CHDIR
        mov ebx, path_root
        int 0x80
        jmp .t7

.t6_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        inc dword [fail_count]
        ; Still try to get back to /
        mov eax, SYS_CHDIR
        mov ebx, path_root
        int 0x80

;-----------------------------------------------
; Test 7: SYS_DELETE /tmp/vfstest.txt
;-----------------------------------------------
.t7:
        mov eax, SYS_PRINT
        mov ebx, msg_t7
        int 0x80

        mov eax, SYS_DELETE
        mov ebx, path_tmp_test
        int 0x80

        cmp eax, -1
        je .t7_fail

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        jmp .t8

.t7_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        inc dword [fail_count]

;-----------------------------------------------
; Test 8: SYS_OPEN + SYS_WRITE /dev/full  (must return -1)
;-----------------------------------------------
.t8:
        mov eax, SYS_PRINT
        mov ebx, msg_t8
        int 0x80

        mov eax, SYS_OPEN
        mov ebx, path_dev_full
        mov ecx, 2              ; FD_FLAG_WRITE
        int 0x80

        cmp eax, -1
        je .t8_fail

        mov [t2_fd], eax

        mov eax, SYS_WRITE
        mov ebx, [t2_fd]
        mov ecx, payload_data
        mov edx, payload_len
        int 0x80

        ; Close regardless of write result
        push eax
        mov eax, SYS_CLOSE
        mov ebx, [t2_fd]
        int 0x80
        pop eax

        ; Write must have returned -1
        cmp eax, -1
        jne .t8_fail

        mov eax, SYS_PRINT
        mov ebx, msg_ok
        int 0x80
        jmp .summary

.t8_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_fail
        int 0x80
        inc dword [fail_count]

;-----------------------------------------------
; Summary
;-----------------------------------------------
.summary:
        cmp dword [fail_count], 0
        jne .print_fail

        mov eax, SYS_PRINT
        mov ebx, msg_pass
        int 0x80
        jmp .done

.print_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_had_fails
        int 0x80

.done:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;=======================================================================
; Data
;=======================================================================

path_proc_version: db "/proc/version", 0
path_proc_meminfo: db "/proc/meminfo", 0
path_dev_zero:     db "/dev/zero", 0
path_dev_full:     db "/dev/full", 0
path_dev:          db "/dev", 0
path_root:         db "/", 0
path_tmp_test:     db "/tmp/vfstest.txt", 0

payload_data: db "Hello from vfstest!", 10, 0
payload_len   equ $ - payload_data - 1   ; exclude null terminator

msg_banner:    db "=== VFS Integration Test (Mellivora v10) ===", 10, 0
msg_t1:        db "[T1] SYS_FREAD /proc/version ... ", 0
msg_t2:        db "[T2] SYS_OPEN+READ /proc/meminfo ... ", 10, 0
msg_t3:        db "[T3] SYS_FWRITE /tmp/vfstest.txt ... ", 0
msg_t4:        db "[T4] SYS_FREAD /tmp/vfstest.txt (verify) ... ", 0
msg_t5:        db "[T5] /dev/zero read 16 bytes, verify zeros ... ", 0
msg_t6:        db "[T6] SYS_CHDIR /dev + SYS_READDIR:", 10, 0
msg_t7:        db "[T7] SYS_DELETE /tmp/vfstest.txt ... ", 0
msg_t8:        db "[T8] SYS_WRITE /dev/full (expect -1) ... ", 0
msg_ok:        db "OK", 10, 0
msg_fail:      db "FAIL", 10, 0
msg_pass:      db 10, "All tests PASSED.", 10, 0
msg_had_fails: db 10, "Some tests FAILED.", 10, 0
msg_newline:   db 10, 0
msg_indent:    db "  ", 0

fail_count: dd 0
t2_fd:      dd 0

IO_BUF_SIZE equ 2048
name_buf: times 256  db 0
io_buf:   times IO_BUF_SIZE db 0
