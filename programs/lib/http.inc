;=======================================================================
; http.inc - User-space HTTP/1.0 client helper library
;
; Provides a simple blocking HTTP GET interface over TCP.
; Requires: syscalls.inc, lib/net.inc
;
; The caller MUST declare the following BSS buffers before %include:
;   http_req_buf:   resb 512
;   http_resp_buf:  resb 65536
;
; Usage:
;   ; EAX = IP (LE), EBX = port, ECX = path ptr, EDX = body out-buf, ESI = max
;   call http_get
;   ; EAX = body byte count, -1 on error
;=======================================================================

HTTP_PORT       equ 80          ; Default HTTP port

;---------------------------------------
; http_get - Blocking HTTP/1.0 GET request
;
; Inputs (all preserved except EAX):
;   EAX = IPv4 address (little-endian)
;   EBX = port (host byte order)
;   ECX = pointer to null-terminated path (e.g. "/index.html")
;   EDX = pointer to output buffer (receives response body)
;   ESI = output buffer maximum size
;
; Returns:
;   EAX = body length written (-1 on error)
;   Output buffer is null-terminated on success.
;---------------------------------------
http_get:
        pushad                  ; Save all regs; EAX returned via [esp+28]

        ; Save args to locals
        mov [.ip],    eax
        mov [.port],  ebx
        mov [.path],  ecx
        mov [.outbuf],edx
        mov [.outmax],esi

        ; -- Create TCP socket --
        mov eax, NET_TCP
        call net_socket
        cmp eax, -1
        je .fail_nofd
        mov [.fd], eax

        ; -- Connect --
        mov eax, [.fd]
        mov ebx, [.ip]
        mov ecx, [.port]
        call net_connect
        test eax, eax
        jnz .fail

        ; -- Build GET request in http_req_buf --
        ; "GET <path> HTTP/1.0\r\nConnection: close\r\n\r\n"
        mov edi, http_req_buf
        mov byte [edi+0], 'G'
        mov byte [edi+1], 'E'
        mov byte [edi+2], 'T'
        mov byte [edi+3], ' '
        add edi, 4
        ; copy path
        mov esi, [.path]
.copy_path:
        lodsb
        test al, al
        jz .path_done
        stosb
        jmp .copy_path
.path_done:
        ; append " HTTP/1.0\r\nConnection: close\r\n\r\n"
        mov esi, .req_suffix
.copy_suffix:
        lodsb
        test al, al
        jz .suffix_done
        stosb
        jmp .copy_suffix
.suffix_done:
        ; compute request length
        mov ecx, edi
        sub ecx, http_req_buf

        ; -- Send request --
        mov eax, [.fd]
        mov ebx, http_req_buf
        call net_send
        cmp eax, -1
        je .fail

        ; -- Receive response into http_resp_buf --
        mov edi, http_resp_buf
        mov dword [.total], 0
        mov dword [.cap], 65536

.recv_loop:
        mov ecx, [.cap]
        test ecx, ecx
        jle .recv_done
        cmp ecx, 4096
        jle .recv_chunk
        mov ecx, 4096
.recv_chunk:
        mov eax, [.fd]
        mov ebx, edi
        call net_recv
        cmp eax, -1
        je .recv_done           ; connection closed / error
        test eax, eax
        jz .recv_yield          ; no data, yield and retry
        add edi, eax
        add [.total], eax
        sub [.cap], eax
        jmp .recv_loop
.recv_yield:
        mov eax, SYS_YIELD
        int 0x80
        jmp .recv_loop
.recv_done:
        mov byte [edi], 0       ; null-terminate raw response

        ; -- Close socket --
        mov eax, [.fd]
        call net_close

        ; -- Skip HTTP headers (find "\r\n\r\n") --
        mov esi, http_resp_buf
        mov ecx, [.total]
.find_hdr_end:
        cmp ecx, 4
        jl .no_body
        cmp byte [esi+0], 0x0D
        jne .find_next
        cmp byte [esi+1], 0x0A
        jne .find_next
        cmp byte [esi+2], 0x0D
        jne .find_next
        cmp byte [esi+3], 0x0A
        jne .find_next
        add esi, 4
        sub ecx, 4
        jmp .copy_body
.find_next:
        inc esi
        dec ecx
        jmp .find_hdr_end

.no_body:
        ; No separator found - return entire response as body
        mov esi, http_resp_buf
        mov ecx, [.total]

.copy_body:
        ; ECX = body length, ESI = body start
        ; Copy min(ECX, outmax) to outbuf
        mov edi, [.outbuf]
        mov ebx, [.outmax]
        cmp ecx, ebx
        jle .body_len_ok
        mov ecx, ebx
.body_len_ok:
        mov eax, ecx            ; return value = bytes copied
        rep movsb
        mov byte [edi], 0       ; null-terminate
        mov [esp + 28], eax     ; store return value in pushad EAX slot
        popad
        ret

.fail:
        mov eax, [.fd]
        call net_close
.fail_nofd:
        mov dword [esp + 28], -1
        popad
        ret

; Request suffix (embedded in code section, after ret - safe)
.req_suffix: db " HTTP/1.0", 0x0D, 0x0A
             db "Connection: close", 0x0D, 0x0A
             db 0x0D, 0x0A, 0

; Per-call scratch (static, non-reentrant)
.fd:     dd -1
.ip:     dd 0
.port:   dd 0
.path:   dd 0
.outbuf: dd 0
.outmax: dd 0
.total:  dd 0
.cap:    dd 0

;---------------------------------------
; http_parse_status - Extract HTTP status code from raw response header
;   ESI = pointer to start of raw response ("HTTP/1.0 200 OK...")
; Returns: EAX = status code, or -1 if not parseable
; Clobbers: ESI
;---------------------------------------
http_parse_status:
        ; Skip past "HTTP/x.x " prefix
.skip:
        lodsb
        test al, al
        jz .fail
        cmp al, ' '
        jne .skip
        ; Parse decimal digits
        xor eax, eax
        xor ecx, ecx
.digit:
        movzx edx, byte [esi]
        cmp dl, '0'
        jb .end
        cmp dl, '9'
        ja .end
        imul eax, 10
        sub dl, '0'
        add eax, edx
        inc ecx
        inc esi
        jmp .digit
.end:
        test ecx, ecx
        jnz .done
.fail:
        mov eax, -1
.done:
        ret
