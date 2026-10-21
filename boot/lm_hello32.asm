;=======================================================================
; lm_hello32.asm - a 32-bit program, /hello32 on the HBFS volume
;
; Runs in compatibility mode under the 64-bit nucleus and reaches the kernel
; the way the 32-bit tree does: INT 0x80, number in EAX, arguments in
; EBX/ECX/EDX, result in EAX. It is the proof that a legacy binary's entry
; path still works, and that the file calls the tree's own programs make -
; stat, readdir, chdir, getcwd, seek - answer on the 32-bit side of the
; bridge, including the 12-byte stat buffer and the name/index readdir form.
;=======================================================================

[BITS 32]
[ORG 0x40000000]        ; the address the nucleus maps the code page at

SYS_EXIT    equ 0
SYS_WRITE   equ 7
SYS_OPEN    equ 5
SYS_READ    equ 6
SYS_CLOSE   equ 8
SYS_SEEK    equ 10
SYS_STAT    equ 11
SYS_READDIR equ 13
SYS_CHDIR   equ 26
SYS_GETCWD  equ 27
SYS_GETPID  equ 54

; Buffers live in this task's stack page, below the return addresses.
CWD_BUF  equ 0x40001C00
DATA_BUF equ 0x40001B00
NAME_BUF equ 0x40001A00         ; readdir's 256-byte name buffer
STAT_BUF equ 0x40001900         ; 12-byte stat buffer: size, type, mtime

start:
        mov eax, SYS_WRITE
        mov ebx, 1
        mov ecx, msg
        mov edx, msg_end - msg
        int 0x80
        cmp eax, msg_end - msg
        jne fail

        mov eax, SYS_GETPID
        int 0x80
        cmp eax, 1              ; task 1 runs this program
        jne fail

        ; getcwd: the root, one character and its NUL.
        mov eax, SYS_GETCWD
        mov ebx, CWD_BUF
        int 0x80
        cmp eax, 1
        jne fail
        cmp byte [CWD_BUF], '/'
        jne fail

        ; readdir: index 0 is an entry, and the name it copies out is not empty.
        mov eax, SYS_READDIR
        mov ebx, NAME_BUF
        xor ecx, ecx
        int 0x80
        cmp eax, 1
        jb fail                 ; 0 = free slot, -1 = past the end
        cmp byte [NAME_BUF], 0
        je fail

        ; stat, legacy form: no buffer, so the size comes back in EAX and the
        ; block count in ECX.
        mov eax, SYS_STAT
        mov ebx, motd_path
        xor ecx, ecx
        int 0x80
        cmp eax, 16
        jne fail
        cmp ecx, 1
        jne fail

        ; stat with the 12-byte buffer: size, type, mtime.
        mov eax, SYS_STAT
        mov ebx, motd_path
        mov ecx, STAT_BUF
        int 0x80
        test eax, eax
        jnz fail
        cmp dword [STAT_BUF], 16
        jne fail
        cmp dword [STAT_BUF + 4], 1     ; type 1 = a text file
        jne fail

        ; chdir into the subdirectory, then resolve a name relative to it.
        mov eax, SYS_CHDIR
        mov ebx, docs_path
        int 0x80
        test eax, eax
        jnz fail

        mov eax, SYS_STAT
        mov ebx, readme_path
        xor ecx, ecx
        int 0x80
        cmp eax, 16
        jne fail

        ; Back to the root, which must be reported as "/".
        mov eax, SYS_CHDIR
        mov ebx, root_path
        int 0x80
        test eax, eax
        jnz fail
        mov eax, SYS_GETCWD
        mov ebx, CWD_BUF
        int 0x80
        cmp eax, 1
        jne fail
        cmp byte [CWD_BUF], '/'
        jne fail

        ; seek: /motd is "hello from hbfs", so offset 4 is "o fr".
        mov eax, SYS_OPEN
        mov ebx, motd_path
        int 0x80
        test eax, eax
        js fail
        mov esi, eax

        mov eax, SYS_SEEK
        mov ebx, esi
        mov ecx, 4
        xor edx, edx            ; whence 0 = from the start
        int 0x80
        cmp eax, 4
        jne fail

        mov eax, SYS_READ
        mov ebx, esi
        mov ecx, DATA_BUF
        mov edx, 4
        int 0x80
        cmp eax, 4
        jne fail
        cmp byte [DATA_BUF], 'o'
        jne fail

        mov eax, SYS_CLOSE
        mov ebx, esi
        int 0x80

        mov eax, SYS_WRITE
        mov ebx, 1
        mov ecx, ok_msg
        mov edx, ok_end - ok_msg
        int 0x80

        mov eax, SYS_EXIT
        mov ebx, 0x32
        int 0x80

fail:
        mov eax, SYS_WRITE
        mov ebx, 1
        mov ecx, bad_msg
        mov edx, bad_end - bad_msg
        int 0x80

        mov eax, SYS_EXIT
        mov ebx, 0xBAD
        int 0x80
.hang:
        jmp .hang

msg:     db "hello from 32-bit mode", 10
msg_end:
ok_msg:  db "32-bit file calls ok", 10
ok_end:
bad_msg: db "32-bit file calls FAILED", 10
bad_end:
motd_path:   db "/motd", 0
docs_path:   db "docs", 0
readme_path: db "readme.txt", 0
root_path:   db "/", 0
