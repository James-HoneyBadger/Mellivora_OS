; forth.asm - Forth Language Interpreter for Mellivora OS
; A stack-based programming language with interactive REPL.
; Supports: arithmetic, stack ops, comparisons, variables, loops, definitions.
; Type 'bye' to exit, 'words' to list, 'help' for help.
%include "syscalls.inc"
%include "lib/io.inc"
%include "lib/string.inc"

STACK_SIZE      equ 256
RSTACK_SIZE     equ 64
DICT_SIZE       equ 4096        ; bytes for user definitions
INPUT_SIZE      equ 256
PAD_SIZE        equ 256

start:
        ; Seed random
        mov eax, SYS_GETTIME
        int 0x80
        mov [rand_state], eax

        ; Init stacks
        mov dword [sp_depth], 0
        mov dword [rsp_depth], 0
        mov qword [dict_ptr], user_dict
        mov dword [compiling], 0

        ; Welcome
        mov eax, SYS_SETCOLOR
        mov ebx, 0x0A
        int 0x80
        mov esi, welcome_str
        call io_println
        mov eax, SYS_SETCOLOR
        mov ebx, COLOR_DEFAULT
        int 0x80

;=== REPL ===
.repl:
        ; Print prompt
        cmp dword [compiling], 0
        jne .comp_prompt
        mov esi, prompt_ok
        call io_print
        jmp .read_line
.comp_prompt:
        mov esi, prompt_comp
        call io_print

.read_line:
        mov edi, input_buf
        mov ecx, INPUT_SIZE
        call io_read_line

        ; Tokenize and interpret
        mov esi, input_buf
        call interpret_line

        jmp .repl

;=======================================
; interpret_line - Process all words in ESI
;=======================================
interpret_line:
        PUSHALL
.il_next:
        call skip_spaces
        cmp byte [esi], 0
        je .il_done

        ; Get next word
        call get_word           ; ESI advances, word in word_buf

        ; Check for special words first
        mov edi, word_buf

        ; "bye"
        push rsi
        mov esi, w_bye
        call str_icmp
        pop rsi
        test eax, eax
        jz .do_bye

        ; "words"
        push rsi
        mov esi, w_words
        call str_icmp
        pop rsi
        test eax, eax
        jz .do_words

        ; "help"
        push rsi
        mov esi, w_help
        call str_icmp
        pop rsi
        test eax, eax
        jz .do_help

        ; Compilation mode?
        cmp dword [compiling], 0
        jne .compile_word

        ; Check if it's a colon definition start
        cmp byte [word_buf], ':'
        jne .not_colon
        cmp byte [word_buf + 1], 0
        jne .not_colon
        ; Start compiling
        mov dword [compiling], 1
        ; Get the definition name
        call skip_spaces
        call get_word
        ; Store name in current definition
        mov rdi, [dict_ptr]
        push rsi
        mov esi, word_buf
        call str_copy
        pop rsi
        ; Skip past name to body
        mov rdi, [dict_ptr]
.find_end_name:
        cmp byte [edi], 0
        je .found_end_name
        inc edi
        jmp .find_end_name
.found_end_name:
        inc edi                 ; past null
        mov [def_body], rdi     ; body starts here
        mov byte [edi], 0      ; init empty body
        jmp .il_next

.not_colon:
        ; Try built-in words
        call execute_word
        jmp .il_next

.compile_word:
        ; Check for semicolon
        cmp byte [word_buf], ';'
        jne .comp_append
        cmp byte [word_buf + 1], 0
        jne .comp_append
        ; End compilation
        mov dword [compiling], 0
        ; Null terminate body, advance dict_ptr
        mov rdi, [def_body]
.find_body_end:
        cmp byte [edi], 0
        je .body_ended
        inc edi
        jmp .find_body_end
.body_ended:
        inc edi
        mov [dict_ptr], rdi
        mov esi, str_ok
        push rsi
        call io_println
        pop rsi
        jmp .il_next

.comp_append:
        ; Append word + space to definition body
        mov rdi, [def_body]
.seek_body_end:
        cmp byte [edi], 0
        je .append_here
        inc edi
        jmp .seek_body_end
.append_here:
        ; Append word_buf contents
        push rsi
        mov esi, word_buf
.copy_word:
        lodsb
        stosb
        test al, al
        jnz .copy_word
        ; Replace null with space
        dec edi
        mov byte [edi], ' '
        inc edi
        mov byte [edi], 0      ; null terminate
        pop rsi
        jmp .il_next

.do_bye:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.do_words:
        call show_words
        jmp .il_next

.do_help:
        call show_help
        jmp .il_next

.il_done:
        ; Print ok if not compiling
        cmp dword [compiling], 0
        jne .il_ret
        mov esi, str_ok
        call io_println
.il_ret:
        POPALL
        ret

;=======================================
; execute_word - Execute word in word_buf
;=======================================
execute_word:
        PUSHALL
        mov edi, word_buf

        ; Try to parse as number first
        push rdi
        mov esi, edi
        call try_parse_number   ; EAX=number, CF=0 if success
        pop rdi
        jnc .ew_push_num

        ; --- Arithmetic ---
        cmp byte [edi], '+'
        jne .ew_not_plus
        cmp byte [edi+1], 0
        jne .ew_not_plus
        call pop_val            ; EAX = b
        mov ebx, eax
        call pop_val            ; EAX = a
        add eax, ebx
        call push_val
        jmp .ew_done
.ew_not_plus:
        cmp byte [edi], '-'
        jne .ew_not_minus
        cmp byte [edi+1], 0
        jne .ew_not_minus
        call pop_val
        mov ebx, eax
        call pop_val
        sub eax, ebx
        call push_val
        jmp .ew_done
.ew_not_minus:
        cmp byte [edi], '*'
        jne .ew_not_mul
        cmp byte [edi+1], 0
        jne .ew_not_mul
        call pop_val
        mov ebx, eax
        call pop_val
        imul eax, ebx
        call push_val
        jmp .ew_done
.ew_not_mul:
        cmp byte [edi], '/'
        jne .ew_not_div
        cmp byte [edi+1], 0
        jne .ew_not_div
        call pop_val
        mov ecx, eax
        call pop_val
        test ecx, ecx
        jz .ew_div_zero
        cdq
        idiv ecx
        call push_val
        jmp .ew_done
.ew_div_zero:
        mov esi, err_div0
        call io_println
        jmp .ew_done
.ew_not_div:

        ; mod
        push rsi
        mov esi, w_mod
        call str_icmp
        pop rsi
        test eax, eax
        jnz .ew_not_mod
        call pop_val
        mov ecx, eax
        call pop_val
        test ecx, ecx
        jz .ew_done
        cdq
        idiv ecx
        mov eax, edx
        call push_val
        jmp .ew_done
.ew_not_mod:

        ; --- Stack ops ---
        ; dup
        push rsi
        mov esi, w_dup
        call str_icmp
        pop rsi
        test eax, eax
        jnz .ew_not_dup
        call pop_val
        call push_val
        call push_val
        jmp .ew_done
.ew_not_dup:
        ; drop
        push rsi
        mov esi, w_drop
        call str_icmp
        pop rsi
        test eax, eax
        jnz .ew_not_drop
        call pop_val
        jmp .ew_done
.ew_not_drop:
        ; swap
        push rsi
        mov esi, w_swap
        call str_icmp
        pop rsi
        test eax, eax
        jnz .ew_not_swap
        call pop_val
        mov ebx, eax
        call pop_val
        push rax
        mov eax, ebx
        call push_val
        pop rax
        call push_val
        jmp .ew_done
.ew_not_swap:
        ; over
        push rsi
        mov esi, w_over
        call str_icmp
        pop rsi
        test eax, eax
        jnz .ew_not_over
        call pop_val
        mov ebx, eax
        call pop_val
        push rax
        call push_val
        mov eax, ebx
        call push_val
        pop rax
        call push_val
        jmp .ew_done
.ew_not_over:
        ; rot
        push rsi
        mov esi, w_rot
        call str_icmp
        pop rsi
        test eax, eax
        jnz .ew_not_rot
        call pop_val            ; c
        mov ecx, eax
        call pop_val            ; b
        mov ebx, eax
        call pop_val            ; a -> b c a
        push rax
        mov eax, ebx
        call push_val
        mov eax, ecx
        call push_val
        pop rax
        call push_val
        jmp .ew_done
.ew_not_rot:

        ; --- Output ---
        ; . (print top)
        cmp byte [edi], '.'
        jne .ew_not_dot
        cmp byte [edi+1], 0
        jne .ew_not_dot
        call pop_val
        mov edi, num_buf
        call int_to_str
        mov esi, num_buf
        call io_print
        mov al, ' '
        call io_putchar
        jmp .ew_done
.ew_not_dot:
        ; .s (show stack)
        push rsi
        mov esi, w_dots
        call str_icmp
        pop rsi
        test eax, eax
        jnz .ew_not_dots
        call show_stack
        jmp .ew_done
.ew_not_dots:
        ; emit
        push rsi
        mov esi, w_emit
        call str_icmp
        pop rsi
        test eax, eax
        jnz .ew_not_emit
        call pop_val
        call io_putchar
        jmp .ew_done
.ew_not_emit:
        ; cr
        push rsi
        mov esi, w_cr
        call str_icmp
        pop rsi
        test eax, eax
        jnz .ew_not_cr
        call io_newline
        jmp .ew_done
.ew_not_cr:

        ; --- Comparison ---
        ; =
        cmp byte [edi], '='
        jne .ew_not_eq
        cmp byte [edi+1], 0
        jne .ew_not_eq
        call pop_val
        mov ebx, eax
        call pop_val
        cmp eax, ebx
        je .ew_true
        jmp .ew_false
.ew_not_eq:
        ; <
        cmp byte [edi], '<'
        jne .ew_not_lt
        cmp byte [edi+1], 0
        jne .ew_not_lt
        call pop_val
        mov ebx, eax
        call pop_val
        cmp eax, ebx
        jl .ew_true
        jmp .ew_false
.ew_not_lt:
        ; >
        cmp byte [edi], '>'
        jne .ew_not_gt
        cmp byte [edi+1], 0
        jne .ew_not_gt
        call pop_val
        mov ebx, eax
        call pop_val
        cmp eax, ebx
        jg .ew_true
        jmp .ew_false
.ew_not_gt:

        ; --- Miscellaneous ---
        ; random
        push rsi
        mov esi, w_random
        call str_icmp
        pop rsi
        test eax, eax
        jnz .ew_not_random
        call rand
        call push_val
        jmp .ew_done
.ew_not_random:
        ; abs
        push rsi
        mov esi, w_abs
        call str_icmp
        pop rsi
        test eax, eax
        jnz .ew_not_abs
        call pop_val
        test eax, eax
        jns .ew_abs_ok
        neg eax
.ew_abs_ok:
        call push_val
        jmp .ew_done
.ew_not_abs:
        ; negate
        push rsi
        mov esi, w_negate
        call str_icmp
        pop rsi
        test eax, eax
        jnz .ew_not_negate
        call pop_val
        neg eax
        call push_val
        jmp .ew_done
.ew_not_negate:
        ; max
        push rsi
        mov esi, w_max
        call str_icmp
        pop rsi
        test eax, eax
        jnz .ew_not_max
        call pop_val
        mov ebx, eax
        call pop_val
        cmp eax, ebx
        jge .ew_max_ok
        mov eax, ebx
.ew_max_ok:
        call push_val
        jmp .ew_done
.ew_not_max:
        ; min
        push rsi
        mov esi, w_min
        call str_icmp
        pop rsi
        test eax, eax
        jnz .ew_not_min
        call pop_val
        mov ebx, eax
        call pop_val
        cmp eax, ebx
        jle .ew_min_ok
        mov eax, ebx
.ew_min_ok:
        call push_val
        jmp .ew_done
.ew_not_min:

        ; --- Try user dictionary ---
        call find_user_word
        test eax, eax
        jnz .ew_done

        ; Unknown word
        mov esi, err_unknown
        call io_print
        mov esi, word_buf
        call io_println

        jmp .ew_done

.ew_push_num:
        call push_val
        jmp .ew_done

.ew_true:
        mov eax, -1
        call push_val
        jmp .ew_done
.ew_false:
        xor eax, eax
        call push_val
.ew_done:
        POPALL
        ret

;---------------------------------------
; Stack operations
;---------------------------------------
push_val:       ; Push EAX onto data stack
        push rbx
        mov ebx, [sp_depth]
        cmp ebx, STACK_SIZE
        jge .push_overflow
        mov [data_stack + ebx*4], eax
        inc dword [sp_depth]
        pop rbx
        ret
.push_overflow:
        push rsi
        mov esi, err_overflow
        call io_println
        pop rsi
        pop rbx
        ret

pop_val:        ; Pop into EAX
        push rbx
        cmp dword [sp_depth], 0
        je .pop_underflow
        dec dword [sp_depth]
        mov ebx, [sp_depth]
        mov eax, [data_stack + ebx*4]
        pop rbx
        ret
.pop_underflow:
        push rsi
        mov esi, err_underflow
        call io_println
        pop rsi
        xor eax, eax
        pop rbx
        ret

;---------------------------------------
; show_stack
;---------------------------------------
show_stack:
        PUSHALL
        mov al, '<'
        call io_putchar
        mov eax, [sp_depth]
        mov edi, num_buf
        call int_to_str
        mov esi, num_buf
        call io_print
        mov esi, str_bracket
        call io_print
        xor ecx, ecx
.ss_loop:
        cmp ecx, [sp_depth]
        jge .ss_done
        mov eax, [data_stack + ecx*4]
        mov edi, num_buf
        call int_to_str
        mov esi, num_buf
        call io_print
        mov al, ' '
        call io_putchar
        inc ecx
        jmp .ss_loop
.ss_done:
        call io_newline
        POPALL
        ret

;---------------------------------------
; find_user_word - Look up word_buf in user dict, execute if found
; Returns EAX=1 if found, 0 if not
;---------------------------------------
find_user_word:
        push rbx
        push rcx
        push rdx
        push rsi
        push rdi

        mov ebx, user_dict
.fuw_loop:
        cmp rbx, [dict_ptr]
        jge .fuw_not_found
        cmp byte [ebx], 0
        je .fuw_not_found

        ; Compare name
        mov esi, word_buf
        mov edi, ebx
        call str_icmp
        test eax, eax
        jz .fuw_found

        ; Skip name
.fuw_skip_name:
        cmp byte [ebx], 0
        je .fuw_skipped_name
        inc ebx
        jmp .fuw_skip_name
.fuw_skipped_name:
        inc ebx
        ; Skip body
.fuw_skip_body:
        cmp byte [ebx], 0
        je .fuw_skipped_body
        inc ebx
        jmp .fuw_skip_body
.fuw_skipped_body:
        inc ebx
        jmp .fuw_loop

.fuw_found:
        ; Skip past name to body
.fuw_to_body:
        cmp byte [ebx], 0
        je .fuw_at_body
        inc ebx
        jmp .fuw_to_body
.fuw_at_body:
        inc ebx
        ; Execute body (it's a string of Forth words)
        mov esi, ebx
        call interpret_line
        mov eax, 1
        jmp .fuw_ret

.fuw_not_found:
        xor eax, eax
.fuw_ret:
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rbx
        ret

;---------------------------------------
; show_words - List all defined words
;---------------------------------------
show_words:
        PUSHALL
        mov esi, builtin_list
        call io_println

        ; User words
        mov ebx, user_dict
.sw_loop:
        cmp rbx, [dict_ptr]
        jge .sw_done
        cmp byte [ebx], 0
        je .sw_done

        mov esi, ebx
        call io_print
        mov al, ' '
        call io_putchar

        ; Skip name
.sw_skip:
        cmp byte [ebx], 0
        je .sw_skipped
        inc ebx
        jmp .sw_skip
.sw_skipped:
        inc ebx
        ; Skip body
.sw_skip2:
        cmp byte [ebx], 0
        je .sw_skipped2
        inc ebx
        jmp .sw_skip2
.sw_skipped2:
        inc ebx
        jmp .sw_loop
.sw_done:
        call io_newline
        POPALL
        ret

;---------------------------------------
; show_help
;---------------------------------------
show_help:
        PUSHALL
        mov esi, help_text
        call io_println
        POPALL
        ret

;---------------------------------------
; Utility functions
;---------------------------------------
skip_spaces:
        cmp byte [esi], ' '
        jne .ss_done
        inc esi
        jmp skip_spaces
.ss_done:
        ret

get_word:
        ; Copy non-space chars from ESI to word_buf
        push rdi
        mov edi, word_buf
.gw_loop:
        mov al, [esi]
        cmp al, 0
        je .gw_done
        cmp al, ' '
        je .gw_done
        cmp al, 10
        je .gw_done
        mov [edi], al
        inc esi
        inc edi
        jmp .gw_loop
.gw_done:
        mov byte [edi], 0
        pop rdi
        ret

try_parse_number:
        ; ESI = string, returns EAX=value, CF=0 on success
        push rbx
        push rcx
        push rdx
        xor eax, eax
        xor ecx, ecx           ; sign flag
        cmp byte [esi], '-'
        jne .tpn_loop
        inc esi
        inc ecx
.tpn_loop:
        movzx edx, byte [esi]
        cmp dl, '0'
        jb .tpn_check
        cmp dl, '9'
        ja .tpn_check
        imul eax, 10
        sub dl, '0'
        add eax, edx
        inc esi
        jmp .tpn_loop
.tpn_check:
        cmp dl, 0
        je .tpn_ok
        cmp dl, ' '
        je .tpn_ok
        cmp dl, 10
        je .tpn_ok
        ; Not a valid number
        stc
        pop rdx
        pop rcx
        pop rbx
        ret
.tpn_ok:
        test ecx, ecx
        jz .tpn_ret
        neg eax
.tpn_ret:
        clc
        pop rdx
        pop rcx
        pop rbx
        ret

int_to_str:
        ; EAX=number, EDI=buffer -> writes decimal string
        PUSHALL
        test eax, eax
        jns .its_pos
        mov byte [edi], '-'
        inc edi
        neg eax
.its_pos:
        test eax, eax
        jnz .its_nonzero
        mov byte [edi], '0'
        mov byte [edi+1], 0
        POPALL
        ret
.its_nonzero:
        xor ecx, ecx
        mov ebx, 10
.its_push:
        test eax, eax
        jz .its_pop
        xor edx, edx
        div ebx
        push rdx
        inc ecx
        jmp .its_push
.its_pop:
        test ecx, ecx
        jz .its_term
        pop rax
        add al, '0'
        stosb
        dec ecx
        jmp .its_pop
.its_term:
        mov byte [edi], 0
        POPALL
        ret

rand:
        mov eax, [rand_state]
        imul eax, 1103515245
        add eax, 12345
        mov [rand_state], eax
        shr eax, 16
        and eax, 0x7FFF
        ret

; === String Constants ===
welcome_str:    db "Mellivora Forth v1.0 - Type 'help' for commands, 'bye' to exit", 0
prompt_ok:      db "> ", 0
prompt_comp:    db "  : ", 0
str_ok:         db " ok", 0
str_bracket:    db "> ", 0
err_unknown:    db "? Unknown: ", 0
err_div0:       db "? Division by zero", 0
err_overflow:   db "? Stack overflow", 0
err_underflow:  db "? Stack underflow", 0

w_bye:          db "bye", 0
w_words:        db "words", 0
w_help:         db "help", 0
w_dup:          db "dup", 0
w_drop:         db "drop", 0
w_swap:         db "swap", 0
w_over:         db "over", 0
w_rot:          db "rot", 0
w_dots:         db ".s", 0
w_emit:         db "emit", 0
w_cr:           db "cr", 0
w_mod:          db "mod", 0
w_random:       db "random", 0
w_abs:          db "abs", 0
w_negate:       db "negate", 0
w_max:          db "max", 0
w_min:          db "min", 0

builtin_list:
        db "Built-in: + - * / mod dup drop swap over rot . .s emit cr = < > abs negate max min random", 0

help_text:
        db "=== Forth Help ===", 0x0A
        db "Numbers: push to stack           : name ... ;  Define word", 0x0A
        db "+ - * / mod  Arithmetic           dup drop swap over rot  Stack", 0x0A
        db ". Print top   .s Show stack       emit  Print as char     cr  Newline", 0x0A
        db "= < >  Compare (true=-1)          abs negate max min  Math", 0x0A
        db "random  Push random number        words  List all words", 0x0A
        db "bye  Exit                         help  This screen", 0x0A
        db "", 0x0A
        db "Example: : square dup * ;   then: 5 square .", 0

; === BSS ===
rand_state:     dd 0
sp_depth:       dd 0
rsp_depth:      dd 0
compiling:      dd 0
dict_ptr:       dq 0
def_body:       dq 0
data_stack:     times STACK_SIZE dd 0
return_stack:   times RSTACK_SIZE dd 0
input_buf:      times INPUT_SIZE db 0
word_buf:       times 64 db 0
num_buf:        times 32 db 0
user_dict:      times DICT_SIZE db 0
