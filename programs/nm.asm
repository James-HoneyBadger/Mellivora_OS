; nm.asm - ELF32 symbol table lister for Mellivora OS
; Usage: nm [file...]
; Output: address  type  name
; Types: T=text, D=data, B=bss, R=rodata, U=undefined, ?=other
%include "syscalls.inc"

; ELF32 header offsets
ELF_MAGIC       equ 0x464C457F  ; 0x7F 'E' 'L' 'F'
ELF_CLASS       equ 4
ELF_DATA        equ 5
ELF_E_SHOFF     equ 0x20        ; section header offset (4 bytes)
ELF_E_SHENTSIZE equ 0x2E        ; section header entry size (2 bytes)
ELF_E_SHNUM     equ 0x30        ; section header count (2 bytes)
ELF_E_SHSTRNDX  equ 0x32        ; index of section name string table (2 bytes)

; ELF32 section header offsets (each is 40 bytes)
SHT_NULL        equ 0
SHT_PROGBITS    equ 1
SHT_SYMTAB      equ 2
SHT_STRTAB      equ 3

SH_NAME         equ 0           ; sh_name (4 bytes - index into shstrtab)
SH_TYPE         equ 4           ; sh_type (4 bytes)
SH_FLAGS        equ 8           ; sh_flags (4 bytes)
SH_ADDR         equ 12          ; sh_addr (4 bytes)
SH_OFFSET       equ 16          ; sh_offset (4 bytes)
SH_SIZE         equ 20          ; sh_size (4 bytes)
SH_LINK         equ 24          ; sh_link (4 bytes) - for SHT_SYMTAB: index of strtab
SH_INFO         equ 28          ; sh_info (4 bytes) - for SHT_SYMTAB: index of first global
SH_ENTSIZE      equ 36          ; sh_entsize (4 bytes)
SH_HDR_SIZE     equ 40

; ELF32 symbol entry offsets (each is 16 bytes)
ST_NAME         equ 0           ; st_name (4 bytes)
ST_VALUE        equ 4           ; st_value (4 bytes)
ST_SIZE         equ 8           ; st_size (4 bytes)
ST_INFO         equ 12          ; st_info (1 byte) - (bind<<4)|type
ST_OTHER        equ 13          ; st_other (1 byte)
ST_SHNDX        equ 14          ; st_shndx (2 bytes)
SYM_ENTRY_SIZE  equ 16

; Symbol binding
STB_LOCAL       equ 0
STB_GLOBAL      equ 1
STB_WEAK        equ 2

; Symbol types
STT_NOTYPE      equ 0
STT_OBJECT      equ 1
STT_FUNC        equ 2
STT_SECTION     equ 3
STT_FILE        equ 4

MAX_FILE        equ 131072      ; 128 KB max binary

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80

        lea esi, [arg_buf]
        ; skip argv[0]
.skip_self:
        lodsb
        test al, al
        jnz .skip_self

        ; Check for args
        mov [any_file], dword 0

.next_file_arg:
        ; find next non-null arg
        cmp byte [esi], 0
        jne .got_arg
        cmp esi, arg_buf + 512
        jge .no_more
        inc esi
        jmp .next_file_arg
.got_arg:
        mov [any_file], dword 1
        ; copy filename
        lea edi, [filename_buf]
.cp_fn:
        lodsb
        stosb
        test al, al
        jnz .cp_fn

        call process_file
        jmp .next_file_arg

.no_more:
        cmp dword [any_file], 0
        jz .usage
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.usage:
        mov eax, SYS_PRINT
        mov ebx, usage_str
        int 0x80
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

;-----------------------------------------------
; process_file - load and parse one ELF file
process_file:
        pushad

        ; Read file
        mov eax, SYS_FREAD
        lea ebx, [filename_buf]
        lea ecx, [file_buf]
        mov edx, MAX_FILE
        int 0x80
        test eax, eax
        js .pf_err_read
        mov [file_size], eax

        ; Check ELF magic
        mov eax, [file_buf]
        cmp eax, ELF_MAGIC
        jne .pf_err_elf

        ; Check 32-bit
        cmp byte [file_buf + ELF_CLASS], 1
        jne .pf_err_elf

        ; Get section header info
        mov eax, [file_buf + ELF_E_SHOFF]
        mov [sh_off], eax
        movzx eax, word [file_buf + ELF_E_SHNUM]
        mov [sh_num], eax
        movzx eax, word [file_buf + ELF_E_SHSTRNDX]
        mov [shstrndx], eax

        ; Find symtab section
        mov dword [symtab_off], 0
        mov dword [symtab_size], 0
        mov dword [strtab_off], 0
        mov dword [strtab_size], 0
        mov dword [symtab_link], 0

        xor ecx, ecx    ; section index
.find_symtab:
        cmp ecx, [sh_num]
        jge .symtab_found
        ; compute section header address
        mov eax, [sh_off]
        imul edx, ecx, SH_HDR_SIZE
        add eax, edx
        ; eax = offset into file_buf for this sh
        mov ebx, eax
        mov eax, [file_buf + ebx + SH_TYPE]
        cmp eax, SHT_SYMTAB
        jne .find_sym_next
        ; Found symtab
        mov eax, [file_buf + ebx + SH_OFFSET]
        mov [symtab_off], eax
        mov eax, [file_buf + ebx + SH_SIZE]
        mov [symtab_size], eax
        mov eax, [file_buf + ebx + SH_LINK]   ; index of strtab
        mov [symtab_link], eax
        jmp .find_strtab
.find_sym_next:
        inc ecx
        jmp .find_symtab

.find_strtab:
        ; Get strtab pointed to by symtab_link
        mov ecx, [symtab_link]
        mov eax, [sh_off]
        imul edx, ecx, SH_HDR_SIZE
        add eax, edx
        mov ebx, eax
        mov eax, [file_buf + ebx + SH_OFFSET]
        mov [strtab_off], eax
        mov eax, [file_buf + ebx + SH_SIZE]
        mov [strtab_size], eax

.symtab_found:
        cmp dword [symtab_off], 0
        je .pf_no_syms

        ; Print filename header if more than one file
        ; (for simplicity, always print)
        ; not printing filename header to keep output clean

        ; Iterate over symbols
        mov esi, [symtab_off]
        add esi, file_buf       ; absolute ptr
        mov edi, [symtab_size]
        xor ecx, ecx

.sym_loop:
        cmp ecx, edi
        jge .sym_done
        ; ecx = bytes consumed

        ; Get symbol fields
        mov eax, [esi + ST_NAME]        ; name index
        mov [sym_name_idx], eax
        mov eax, [esi + ST_VALUE]       ; address
        mov [sym_value], eax
        mov al, [esi + ST_INFO]
        mov [sym_info], al
        movzx eax, word [esi + ST_SHNDX]
        mov [sym_shndx], eax

        ; Skip STT_FILE and STT_SECTION entries
        movzx eax, byte [sym_info]
        and al, 0x0F            ; type = low 4 bits
        cmp al, STT_FILE
        je .sym_skip
        cmp al, STT_SECTION
        je .sym_skip

        ; Get symbol name
        mov eax, [sym_name_idx]
        test eax, eax
        jz .sym_skip            ; skip unnamed symbols

        mov ebx, [strtab_off]
        add ebx, file_buf
        add ebx, eax
        ; Check name is not empty
        cmp byte [ebx], 0
        je .sym_skip

        ; Determine type letter
        call get_sym_type       ; eax=type char
        mov [sym_type_char], al

        ; Print: "xxxxxxxx T name\n"
        ; Print address (8 hex digits)
        mov eax, [sym_value]
        call print_hex32
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

        ; Print type char
        mov eax, SYS_PUTCHAR
        movzx ebx, byte [sym_type_char]
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, ' '
        int 0x80

        ; Print name
        mov eax, [sym_name_idx]
        mov ebx, [strtab_off]
        add ebx, file_buf
        add ebx, eax
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 0x0A
        int 0x80

.sym_skip:
        add esi, SYM_ENTRY_SIZE
        add ecx, SYM_ENTRY_SIZE
        jmp .sym_loop

.sym_done:
        popad
        ret

.pf_no_syms:
        lea ebx, [filename_buf]
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, err_no_syms
        int 0x80
        popad
        ret

.pf_err_read:
        lea ebx, [filename_buf]
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, err_read
        int 0x80
        popad
        ret

.pf_err_elf:
        lea ebx, [filename_buf]
        mov eax, SYS_PRINT
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, err_not_elf
        int 0x80
        popad
        ret

;-----------------------------------------------
; get_sym_type: determine type char from sym_info and sym_shndx
; Returns AL = type char
get_sym_type:
        movzx eax, byte [sym_info]
        mov ebx, eax
        shr ebx, 4              ; binding
        and al, 0x0F            ; type

        ; Undefined symbol (shndx == 0)
        cmp word [sym_shndx], 0
        je .gst_undef

        cmp al, STT_FUNC
        je .gst_text
        cmp al, STT_OBJECT
        je .gst_obj

        ; Check section flags for code/data/bss
        ; Map shndx to section, look at flags
        movzx ecx, word [sym_shndx]
        mov eax, [sh_off]
        imul edx, ecx, SH_HDR_SIZE
        add eax, edx
        mov edx, eax
        mov eax, [file_buf + edx + SH_TYPE]
        cmp eax, SHT_NULL
        je .gst_abs

        mov eax, [file_buf + edx + SH_FLAGS]
        test eax, 4             ; SHF_EXECINSTR
        jnz .gst_text
        test eax, 2             ; SHF_ALLOC
        jz .gst_abs
        mov eax, [file_buf + edx + SH_TYPE]
        cmp eax, SHT_PROGBITS
        je .gst_data_or_ro
        ; SHT_NOBITS = BSS
        mov al, 'b'
        jmp .gst_case

.gst_data_or_ro:
        ; Check write flag
        mov eax, [file_buf + edx + SH_FLAGS]
        test eax, 1             ; SHF_WRITE
        jz .gst_ro
        mov al, 'd'
        jmp .gst_case
.gst_ro:
        mov al, 'r'
        jmp .gst_case

.gst_text:
        mov al, 't'
        jmp .gst_case

.gst_obj:
        ; Check if BSS-like
        movzx ecx, word [sym_shndx]
        mov eax, [sh_off]
        imul edx, ecx, SH_HDR_SIZE
        add eax, edx
        mov eax, [file_buf + eax + SH_TYPE]
        cmp eax, SHT_NULL
        je .gst_data
        ; SHT_NOBITS
        cmp eax, 8
        je .gst_bss
.gst_data:
        mov al, 'd'
        jmp .gst_case
.gst_bss:
        mov al, 'b'
        jmp .gst_case

.gst_abs:
        mov al, 'a'
        jmp .gst_case

.gst_undef:
        mov al, 'u'

.gst_case:
        ; If global binding, uppercase
        mov ebx, eax
        movzx eax, byte [sym_info]
        shr al, 4
        cmp al, STB_GLOBAL
        je .gst_upper
        cmp al, STB_WEAK
        jne .gst_done
.gst_upper:
        mov al, bl
        cmp al, 'a'
        jb .gst_done
        cmp al, 'z'
        ja .gst_done
        sub al, 32      ; to uppercase
        mov bl, al
.gst_done:
        mov al, bl
        ret

;-----------------------------------------------
; print_hex32: print EAX as 8-digit hex
print_hex32:
        push eax
        push ebx
        push ecx
        mov ecx, 8
        rol eax, 4
.ph32:
        push eax
        and al, 0x0F
        cmp al, 10
        jb .ph32_digit
        add al, 'a' - 10 - '0'
.ph32_digit:
        add al, '0'
        movzx ebx, al
        push ecx
        mov eax, SYS_PUTCHAR
        int 0x80
        pop ecx
        pop eax
        rol eax, 4
        dec ecx
        jnz .ph32
        pop ecx
        pop ebx
        pop eax
        ret

;--- Data ---
usage_str:   db "Usage: nm [file...]", 0x0A, 0
err_no_syms: db ": no symbol table", 0x0A, 0
err_read:    db ": cannot read file", 0x0A, 0
err_not_elf: db ": not an ELF32 file", 0x0A, 0

;--- BSS ---
arg_buf:         times 512 db 0
filename_buf:    times 256 db 0
file_buf:        times MAX_FILE db 0
file_size:       dd 0
any_file:        dd 0
sh_off:          dd 0
sh_num:          dd 0
shstrndx:        dd 0
symtab_off:      dd 0
symtab_size:     dd 0
symtab_link:     dd 0
strtab_off:      dd 0
strtab_size:     dd 0
sym_name_idx:    dd 0
sym_value:       dd 0
sym_info:        db 0
                 db 0,0,0
sym_shndx:       dw 0
                 dw 0
sym_type_char:   db 0
                 db 0,0,0
