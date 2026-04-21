; wget.asm - Simple HTTP/1.0 GET client
; Usage: wget <url>
;        wget http://hostname/path [output_file]

%include "syscalls.inc"
%include "lib/net.inc"
%include "lib/http.inc"
%include "lib/string.inc"

RECV_BUF_SIZE   equ 131072      ; 128 KB receive buffer
HTTP_PORT       equ 80

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80
        test eax, eax
        jz .usage

        mov esi, arg_buf
        call skip_spaces
        cmp byte [esi], 0
        je .usage

        ; Parse URL: http://hostname/path or hostname/path or hostname
        ; Skip "http://" if present
        push esi
        mov edi, proto_str
        call str_starts_with
        test eax, eax
        jz .skip_http
        add esi, 7              ; skip "http://"
.skip_http:
        pop eax                 ; discard saved ESI

        ; Extract hostname (up to '/', ' ', or '\0')
        mov edi, hostname
        xor ecx, ecx
.copy_host:
        mov al, [esi]
        cmp al, '/'
        je .host_done
        cmp al, ' '
        je .host_done
        cmp al, 0
        je .host_done
        mov [edi + ecx], al
        inc ecx
        inc esi
        jmp .copy_host
.host_done:
        mov byte [edi + ecx], 0

        ; Extract path (rest of URL, or default to "/")
        cmp byte [esi], '/'
        jne .default_path
        mov edi, path
        xor ecx, ecx
.copy_path:
        mov al, [esi]
        cmp al, ' '
        je .path_done
        cmp al, 0
        je .path_done
        mov [edi + ecx], al
        inc ecx
        inc esi
        jmp .copy_path
.path_done:
        mov byte [edi + ecx], 0
        jmp .check_outfile

.default_path:
        mov byte [path], '/'
        mov byte [path + 1], 0

.check_outfile:
        call skip_spaces
        cmp byte [esi], 0
        je .gen_outfile

        ; Optional output filename
        mov edi, outfile
        xor ecx, ecx
.copy_out:
        mov al, [esi]
        cmp al, ' '
        je .out_done
        cmp al, 0
        je .out_done
        mov [edi + ecx], al
        inc ecx
        inc esi
        jmp .copy_out
.out_done:
        mov byte [edi + ecx], 0
        jmp .resolve

.gen_outfile:
        ; Use last component of path as filename, or "index.html"
        mov esi, path
        mov edi, outfile
        xor ecx, ecx
        ; Find last '/'
        mov ebx, esi
.find_last_slash:
        mov al, [esi]
        test al, al
        jz .found_last_slash
        cmp al, '/'
        jne .fls_next
        lea ebx, [esi + 1]
.fls_next:
        inc esi
        jmp .find_last_slash
.found_last_slash:
        ; ebx = start of filename part
        cmp byte [ebx], 0
        jne .copy_outfile_name
        ; Empty filename - use "index.html"
        mov esi, default_outfile
        jmp .copy_of
.copy_outfile_name:
        mov esi, ebx
.copy_of:
        xor ecx, ecx
.copy_of_loop:
        mov al, [esi + ecx]
        test al, al
        jz .copy_of_done
        mov [edi + ecx], al
        inc ecx
        jmp .copy_of_loop
.copy_of_done:
        mov byte [edi + ecx], 0

.resolve:
        ; Resolve hostname
        mov eax, SYS_DNS
        mov ebx, hostname
        int 0x80
        test eax, eax
        jz .dns_fail
        mov [target_ip], eax

        ; Set Host header for virtual-hosted servers
        mov dword [http_req_host], hostname

        ; Print status
        mov eax, SYS_PRINT
        mov ebx, msg_connecting
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, hostname
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, msg_saving
        int 0x80
        mov eax, SYS_PRINT
        mov ebx, outfile
        int 0x80
        mov eax, SYS_PUTCHAR
        mov ebx, 10
        int 0x80

        ; Fetch via HTTP GET
        mov eax, [target_ip]
        mov ebx, HTTP_PORT
        mov ecx, path
        mov edx, save_buf
        mov esi, RECV_BUF_SIZE - 1
        call http_get
        cmp eax, -1
        je .conn_fail
        mov [body_len], eax

        ; Write to file
        mov eax, SYS_FWRITE
        mov ebx, outfile
        mov ecx, save_buf
        mov edx, [body_len]
        mov esi, 1              ; FTYPE_TEXT
        int 0x80
        cmp eax, -1
        je .write_fail

        ; Print success
        mov eax, SYS_PRINT
        mov ebx, msg_saved
        int 0x80
        mov eax, [body_len]
        call print_dec
        mov eax, SYS_PRINT
        mov ebx, msg_bytes
        int 0x80

        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

.write_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_write_fail
        int 0x80
        jmp .exit

.dns_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_dns_fail
        int 0x80
        jmp .exit

.conn_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_conn_fail
        int 0x80
        jmp .exit

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80
.exit:
        mov eax, SYS_EXIT
        mov ebx, 1
        int 0x80

proto_str:      db "http://", 0
default_outfile: db "index.html", 0

msg_usage:      db "Usage: wget [http://]<host>[/path] [outfile]", 10, 0
msg_connecting: db "Connecting to ", 0
msg_saving:     db "... saving to ", 0
msg_saved:      db "Saved ", 0
msg_bytes:      db " bytes.", 10, 0
msg_dns_fail:   db "wget: DNS resolution failed", 10, 0
msg_conn_fail:  db "wget: connection failed", 10, 0
msg_write_fail: db "wget: cannot write file", 10, 0

hostname:       times 256 db 0
path:           times 512 db 0
outfile:        times 128 db 0
target_ip:      dd 0
body_len:       dd 0
arg_buf:        times 1024 db 0
http_req_buf:   times 512 db 0
http_resp_buf:  times 65536 db 0
save_buf:       times RECV_BUF_SIZE db 0
