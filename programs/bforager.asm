; bforager.asm - BForager - Burrows Web Browser
; GUI browser for Burrows with an address bar, clickable links, and
; HTTP/1.0 page fetching. Non-HTTP protocols are handed off to the
; existing ftp, telnet, and gopher programs.

%include "syscalls.inc"
%include "lib/gui.inc"
%include "lib/net.inc"

WIN_W           equ 560
WIN_H           equ 372
NAV_Y           equ 8
NAV_W           equ 24
NAV_H           equ 24
BACK_X          equ 10
FORWARD_X       equ 38
HOME_X          equ 66
RELOAD_X        equ 94
ADDR_X          equ 126
ADDR_Y          equ 8
ADDR_W          equ 314
ADDR_H          equ 24
GO_X            equ 448
GO_Y            equ 8
GO_W            equ 48
GO_H            equ 24
BODY_X          equ 10
BODY_Y          equ 42
BODY_W          equ 540
BODY_H          equ 216
STATUS_Y        equ 264
STATUS_H        equ 18
LINKS_Y         equ 288
LINKS_H         equ 72
LINE_STRIDE     equ 67
LINE_CHARS      equ 66
MAX_LINES       equ 64
MAX_LINKS       equ 6
LINK_STRIDE     equ 256
MAX_RESPONSE    equ 32768
MAX_HISTORY     equ 16
HISTORY_STRIDE  equ 256
SCHEME_HTTP     equ 0
SCHEME_FTP      equ 1
SCHEME_TELNET   equ 2
SCHEME_GOPHER   equ 3

start:
        mov eax, 38
        mov ebx, 32
        mov ecx, WIN_W
        mov edx, WIN_H
        mov esi, title_str
        call gui_create_window
        cmp eax, -1
        je .exit
        mov [win_id], eax

        mov byte [edit_focus], 1
        mov dword [scroll_line], 0
        mov dword [link_count], 0
        mov dword [page_line_count], 1
        mov dword [address_len], 0
        mov byte [address_buf], 0

        mov esi, msg_idle
        mov edi, status_buf
        call copy_zstr

        mov ebx, arg_buf
        mov eax, SYS_GETARGS
        int 0x80
        test eax, eax
        jz .default_address
        mov esi, arg_buf
        mov edi, address_buf
        mov ecx, 255
.copy_args:
        lodsb
        stosb
        test al, al
        jz .args_done
        dec ecx
        jnz .copy_args
        mov byte [edi], 0
.args_done:
        mov esi, address_buf
        call str_len
        mov [address_len], eax
        call browser_navigate
        jmp .main_loop

.default_address:
        mov esi, home_url
        mov edi, address_buf
        call copy_zstr
        mov esi, address_buf
        call str_len
        mov [address_len], eax
        call browser_navigate

.main_loop:
        call gui_compose
        call draw_browser
        call gui_flip

        call gui_poll_event
        cmp eax, EVT_CLOSE
        je .close
        cmp eax, EVT_KEY_PRESS
        je .handle_key
        cmp eax, EVT_MOUSE_CLICK
        je .handle_click
        jmp .main_loop

.handle_key:
        cmp bl, 27
        je .close
        cmp bl, 13
        je .nav_go
        cmp bl, KEY_LEFT
        je .nav_back
        cmp bl, KEY_RIGHT
        je .nav_forward
        cmp bl, 8
        je .key_backspace
        cmp bl, KEY_UP
        je .scroll_up
        cmp bl, KEY_DOWN
        je .scroll_down
        cmp bl, 9
        je .toggle_focus
        cmp byte [edit_focus], 1
        je .check_text_input
        mov al, bl
        or al, 0x20
        cmp al, 'h'
        je .nav_home
        cmp al, 'r'
        je .nav_reload
        cmp al, 's'
        je .nav_set_home
        jmp .main_loop

.check_text_input:
        cmp byte [edit_focus], 1
        jne .main_loop
        cmp bl, 32
        jl .main_loop
        cmp bl, 126
        jg .main_loop
        mov ecx, [address_len]
        cmp ecx, 255
        jge .main_loop
        mov [address_buf + ecx], bl
        inc dword [address_len]
        mov ecx, [address_len]
        mov byte [address_buf + ecx], 0
        jmp .main_loop

.key_backspace:
        cmp byte [edit_focus], 1
        jne .main_loop
        cmp dword [address_len], 0
        je .main_loop
        dec dword [address_len]
        mov ecx, [address_len]
        mov byte [address_buf + ecx], 0
        jmp .main_loop

.scroll_up:
        cmp dword [scroll_line], 0
        je .main_loop
        dec dword [scroll_line]
        jmp .main_loop

.scroll_down:
        mov eax, [page_line_count]
        sub eax, 13
        cmp eax, 0
        jle .main_loop
        cmp [scroll_line], eax
        jge .main_loop
        inc dword [scroll_line]
        jmp .main_loop

.toggle_focus:
        xor byte [edit_focus], 1
        jmp .main_loop

.nav_back:
        cmp byte [edit_focus], 1
        je .main_loop
        call browser_go_back
        jmp .main_loop

.nav_forward:
        cmp byte [edit_focus], 1
        je .main_loop
        call browser_go_forward
        jmp .main_loop

.nav_home:
        call browser_go_home
        jmp .main_loop

.nav_reload:
        call browser_reload_current
        jmp .main_loop

.nav_set_home:
        call browser_set_home
        jmp .main_loop

.nav_go:
        call browser_navigate
        jmp .main_loop

.handle_click:
        cmp ecx, NAV_Y
        jl .check_address
        cmp ecx, NAV_Y + NAV_H
        jg .check_address
        cmp ebx, BACK_X
        jl .check_address
        cmp ebx, BACK_X + NAV_W
        jle .click_back
        cmp ebx, FORWARD_X
        jl .check_address
        cmp ebx, FORWARD_X + NAV_W
        jle .click_forward
        cmp ebx, HOME_X
        jl .check_address
        cmp ebx, HOME_X + NAV_W
        jle .click_home
        cmp ebx, RELOAD_X
        jl .check_address
        cmp ebx, RELOAD_X + NAV_W
        jle .click_reload

.check_address:
        cmp ecx, ADDR_Y
        jl .check_go
        cmp ecx, ADDR_Y + ADDR_H
        jg .check_go
        cmp ebx, ADDR_X
        jl .check_go
        cmp ebx, ADDR_X + ADDR_W
        jg .check_go
        mov byte [edit_focus], 1
        jmp .main_loop

.click_back:
        mov byte [edit_focus], 0
        call browser_go_back
        jmp .main_loop

.click_forward:
        mov byte [edit_focus], 0
        call browser_go_forward
        jmp .main_loop

.click_home:
        mov byte [edit_focus], 0
        call browser_go_home
        jmp .main_loop

.click_reload:
        mov byte [edit_focus], 0
        call browser_reload_current
        jmp .main_loop

.check_go:
        cmp ecx, GO_Y
        jl .check_links
        cmp ecx, GO_Y + GO_H
        jg .check_links
        cmp ebx, GO_X
        jl .check_links
        cmp ebx, GO_X + GO_W
        jg .check_links
        call browser_navigate
        jmp .main_loop

.check_links:
        call click_link_hit
        cmp eax, -1
        je .main_loop
        call navigate_link_index
        jmp .main_loop

.close:
        mov eax, [win_id]
        call gui_destroy_window
.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

draw_browser:
        PUSHALL
        ; main background
        mov eax, [win_id]
        xor ebx, ebx
        xor ecx, ecx
        mov edx, WIN_W
        mov esi, WIN_H
        mov edi, 0x00F4F1E8
        call gui_fill_rect

        ; top chrome
        mov eax, [win_id]
        mov ebx, 0
        mov ecx, 0
        mov edx, WIN_W
        mov esi, 36
        mov edi, 0x005A6B3A
        call gui_fill_rect

        ; navigation buttons
        mov eax, [win_id]
        mov ebx, BACK_X
        mov ecx, NAV_Y
        mov edx, NAV_W
        mov esi, NAV_H
        mov edi, 0x0096B85C
        call gui_fill_rect

        mov eax, [win_id]
        mov ebx, FORWARD_X
        mov ecx, NAV_Y
        mov edx, NAV_W
        mov esi, NAV_H
        mov edi, 0x0096B85C
        call gui_fill_rect

        mov eax, [win_id]
        mov ebx, HOME_X
        mov ecx, NAV_Y
        mov edx, NAV_W
        mov esi, NAV_H
        mov edi, 0x00C7922C
        call gui_fill_rect

        mov eax, [win_id]
        mov ebx, RELOAD_X
        mov ecx, NAV_Y
        mov edx, NAV_W
        mov esi, NAV_H
        mov edi, 0x0096B85C
        call gui_fill_rect

        ; address box
        mov eax, [win_id]
        mov ebx, ADDR_X
        mov ecx, ADDR_Y
        mov edx, ADDR_W
        mov esi, ADDR_H
        mov edi, 0x00FFFFFF
        call gui_fill_rect

        ; go button
        mov eax, [win_id]
        mov ebx, GO_X
        mov ecx, GO_Y
        mov edx, GO_W
        mov esi, GO_H
        mov edi, 0x00C7922C
        call gui_fill_rect

        ; body surface
        mov eax, [win_id]
        mov ebx, BODY_X
        mov ecx, BODY_Y
        mov edx, BODY_W
        mov esi, BODY_H
        mov edi, 0x00FFFDF7
        call gui_fill_rect

        ; status bar
        mov eax, [win_id]
        mov ebx, BODY_X
        mov ecx, STATUS_Y
        mov edx, BODY_W
        mov esi, STATUS_H
        mov edi, 0x00E6DFC9
        call gui_fill_rect

        ; links panel
        mov eax, [win_id]
        mov ebx, BODY_X
        mov ecx, LINKS_Y
        mov edx, BODY_W
        mov esi, LINKS_H
        mov edi, 0x00EFE8D5
        call gui_fill_rect

        ; chrome labels
        mov eax, [win_id]
        mov ebx, BACK_X + 8
        mov ecx, 12
        mov esi, back_label
        mov edi, 0x00FFFFFF
        call gui_draw_text

        mov eax, [win_id]
        mov ebx, FORWARD_X + 8
        mov ecx, 12
        mov esi, forward_label
        mov edi, 0x00FFFFFF
        call gui_draw_text

        mov eax, [win_id]
        mov ebx, HOME_X + 8
        mov ecx, 12
        mov esi, home_label
        mov edi, 0x00FFFFFF
        call gui_draw_text

        mov eax, [win_id]
        mov ebx, GO_X + 12
        mov ecx, GO_Y + 5
        mov esi, go_label
        mov edi, 0x00FFFFFF
        call gui_draw_text

        mov eax, [win_id]
        mov ebx, RELOAD_X + 8
        mov ecx, 12
        mov esi, reload_label
        mov edi, 0x00FFFFFF
        call gui_draw_text

        mov eax, [win_id]
        mov ebx, ADDR_X + 8
        mov ecx, ADDR_Y + 5
        mov esi, address_buf
        mov edi, 0x00000000
        call gui_draw_text

        cmp byte [edit_focus], 1
        jne .draw_page
        mov eax, [address_len]
        shl eax, 3
        add eax, ADDR_X + 8
        mov ebx, eax
        mov eax, [win_id]
        mov ecx, ADDR_Y + 5
        mov esi, cursor_str
        mov edi, 0x00303030
        call gui_draw_text

.draw_page:
        mov eax, [win_id]
        mov ebx, BODY_X + 4
        mov ecx, STATUS_Y + 2
        mov esi, status_buf
        mov edi, 0x00303030
        call gui_draw_text

        call build_home_indicator
        mov eax, [win_id]
        mov ebx, BODY_X + 320
        mov ecx, STATUS_Y + 2
        mov esi, home_ind_buf
        mov edi, 0x005A6B3A
        call gui_draw_text

        xor ecx, ecx
        mov edx, BODY_Y + 6
        mov ebx, [scroll_line]
.line_loop:
        cmp ecx, 13
        jge .links_title
        cmp ebx, [page_line_count]
        jge .links_title
        push rcx
        push rbx
        mov eax, ebx
        imul eax, LINE_STRIDE
        lea esi, [page_lines + eax]
        mov eax, [win_id]
        mov ebx, BODY_X + 6
        mov ecx, edx
        mov edi, 0x00000000
        call gui_draw_text
        pop rbx
        pop rcx
        add edx, 16
        inc ecx
        inc ebx
        jmp .line_loop

.links_title:
        mov eax, [win_id]
        mov ebx, BODY_X + 6
        mov ecx, LINKS_Y + 4
        mov esi, links_label
        mov edi, 0x005A6B3A
        call gui_draw_text

        xor ecx, ecx
.draw_links:
        cmp ecx, [link_count]
        jge .draw_done
        cmp ecx, MAX_LINKS
        jge .draw_done
        mov [link_draw_idx], ecx
        mov eax, ecx
        xor edx, edx
        mov ebx, 2
        div ebx
        imul eax, 24
        add eax, LINKS_Y + 24
        mov [link_draw_y], eax
        mov eax, edx
        imul eax, 266
        add eax, BODY_X + 6
        mov [link_draw_x], eax

        mov eax, [win_id]
        mov ebx, [link_draw_x]
        mov ecx, [link_draw_y]
        mov edx, 254
        mov esi, 18
        mov edi, 0x00D7E7B2
        call gui_fill_rect

        mov ecx, [link_draw_idx]
        imul ecx, LINK_STRIDE
        lea esi, [link_labels + ecx]
        mov eax, [win_id]
        mov ebx, [link_draw_x]
        add ebx, 4
        mov ecx, [link_draw_y]
        add ecx, 4
        mov edi, 0x00304E1D
        call gui_draw_text
        mov ecx, [link_draw_idx]
        inc ecx
        jmp .draw_links

.draw_done:
        POPALL
        ret

browser_navigate:
        PUSHALL
        mov esi, address_buf
        call trim_leading_spaces
        cmp byte [esi], 0
        jne .have_url
        mov esi, msg_no_url
        mov edi, status_buf
        call copy_zstr
        jmp .bn_done
.have_url:
        mov edi, address_buf
        call copy_zstr
        mov esi, address_buf
        call trim_trailing_spaces
        mov esi, address_buf
        call str_len
        mov [address_len], eax
        cmp byte [history_suppress], 1
        je .skip_history
        call history_push_current
.skip_history:
        mov esi, address_buf
        call parse_url
        cmp eax, SCHEME_HTTP
        je .do_http
        call dispatch_non_http
        jmp .bn_done
.do_http:
        call fetch_http_page
 .bn_done:
        mov byte [history_suppress], 0
        POPALL
        ret

browser_go_back:
        PUSHALL
        cmp dword [history_index], 0
        jle .bgb_done
        dec dword [history_index]
        mov eax, [history_index]
        call history_load_index
        mov byte [history_suppress], 1
        call browser_navigate
.bgb_done:
        POPALL
        ret

browser_go_forward:
        PUSHALL
        mov eax, [history_index]
        inc eax
        cmp eax, [history_count]
        jge .bgf_done
        mov [history_index], eax
        call history_load_index
        mov byte [history_suppress], 1
        call browser_navigate
.bgf_done:
        POPALL
        ret

browser_go_home:
        PUSHALL
        mov esi, home_url
        mov edi, address_buf
        call copy_zstr
        mov esi, address_buf
        call str_len
        mov [address_len], eax
        call browser_navigate
        POPALL
        ret

browser_set_home:
        PUSHALL
        cmp byte [address_buf], 0
        je .bsh_done
        mov esi, address_buf
        mov edi, home_url
        call copy_zstr
        mov esi, msg_home_set
        mov edi, status_buf
        call copy_zstr
.bsh_done:
        POPALL
        ret

build_home_indicator:
        PUSHALL
        mov edi, home_ind_buf
        mov esi, home_prefix
        call copy_zstr
        dec edi
        mov esi, home_url
        xor ecx, ecx
.bhi_loop:
        lodsb
        test al, al
        jz .bhi_done
        stosb
        inc ecx
        cmp ecx, 20
        jb .bhi_loop
.bhi_done:
        mov byte [edi], 0
        POPALL
        ret

browser_reload_current:
        PUSHALL
        cmp byte [address_buf], 0
        je .brc_done
        mov byte [history_suppress], 1
        call browser_navigate
.brc_done:
        POPALL
        ret

history_push_current:
        PUSHALL
        cmp dword [history_count], 0
        je .hpc_store_new
        mov eax, [history_index]
        cmp eax, 0
        jl .hpc_store_new
        cmp eax, [history_count]
        jge .hpc_store_new
        imul eax, HISTORY_STRIDE
        lea esi, [history_buf + eax]
        mov edi, address_buf
        call str_eq
        cmp eax, 1
        je .hpc_done

.hpc_store_new:
        mov eax, [history_index]
        inc eax
        cmp eax, 0
        jge .hpc_set_count
        xor eax, eax
.hpc_set_count:
        mov [history_count], eax
        cmp eax, MAX_HISTORY
        jb .hpc_have_slot
        call history_shift_left
        mov eax, MAX_HISTORY - 1
        mov [history_count], eax
        mov [history_index], eax
.hpc_have_slot:
        mov eax, [history_count]
        imul eax, HISTORY_STRIDE
        lea edi, [history_buf + eax]
        mov esi, address_buf
        call copy_zstr
        mov eax, [history_count]
        mov [history_index], eax
        inc eax
        mov [history_count], eax
.hpc_done:
        POPALL
        ret

history_shift_left:
        PUSHALL
        mov esi, history_buf + HISTORY_STRIDE
        mov edi, history_buf
        mov ecx, ((MAX_HISTORY - 1) * HISTORY_STRIDE) / 4
        rep movsd
        mov edi, history_buf + ((MAX_HISTORY - 1) * HISTORY_STRIDE)
        xor eax, eax
        mov ecx, HISTORY_STRIDE / 4
        rep stosd
        POPALL
        ret

history_load_index:
        PUSHALL
        imul eax, HISTORY_STRIDE
        lea esi, [history_buf + eax]
        mov edi, address_buf
        call copy_zstr
        mov esi, address_buf
        call str_len
        mov [address_len], eax
        POPALL
        ret

dispatch_non_http:
        PUSHALL
        call build_dispatch_command
        mov ebx, exec_buf
        mov eax, SYS_EXEC
        int 0x80
        cmp eax, -1
        jne .dn_done
        mov esi, msg_launch_fail
        mov edi, status_buf
        call copy_zstr
.dn_done:
        POPALL
        ret

fetch_http_page:
        PUSHALL
        call clear_page_state
        mov esi, msg_connecting
        mov edi, status_buf
        call copy_zstr

        mov esi, hostname
        call net_dns
        test eax, eax
        jz .dns_fail
        mov [server_ip], eax

        mov eax, NET_TCP
        call net_socket
        cmp eax, -1
        je .sock_fail
        mov [sockfd], eax

        mov eax, [sockfd]
        mov ebx, [server_ip]
        movzx ecx, word [port]
        call net_connect
        cmp eax, -1
        je .connect_fail

        call build_http_request
        mov eax, [sockfd]
        mov ebx, request_buf
        mov ecx, [request_len]
        call net_send
        cmp eax, -1
        je .send_fail

        mov dword [response_len], 0
        mov dword [retry_count], 0
.recv_loop:
        mov eax, [sockfd]
        mov ebx, recv_buf
        mov ecx, 1024
        call net_recv
        cmp eax, -1
        je .recv_done
        cmp eax, 0
        je .recv_retry
        mov ecx, eax
        mov esi, recv_buf
        call append_response
        mov dword [retry_count], 0
        jmp .recv_loop
.recv_retry:
        inc dword [retry_count]
        cmp dword [retry_count], 500
        jge .recv_done
        mov eax, SYS_YIELD
        int 0x80
        jmp .recv_loop

.recv_done:
        mov eax, [sockfd]
        call net_close
        mov eax, [response_len]
        mov byte [response_buf + eax], 0
        ; Check for redirect (3xx)
        call check_http_redirect
        test eax, eax
        jnz .do_redirect

        ; Check for chunked transfer encoding
        call check_chunked_encoding
        test eax, eax
        jnz .do_dechunk

        call parse_response_body
        mov esi, msg_loaded
        mov edi, status_buf
        call copy_zstr
        mov byte [http_redir_count], 0
        POPALL
        ret

.do_redirect:
        ; ESI points to redirect URL from Location: header
        inc byte [http_redir_count]
        cmp byte [http_redir_count], http_redir_max
        jg .redir_limit
        call parse_url
        POPALL
        jmp fetch_http_page

.redir_limit:
        mov byte [http_redir_count], 0
        mov esi, msg_redir_limit
        mov edi, status_buf
        call copy_zstr
        POPALL
        ret

.do_dechunk:
        call dechunk_response
        call parse_response_body
        mov esi, msg_loaded
        mov edi, status_buf
        call copy_zstr
        mov byte [http_redir_count], 0
        POPALL
        ret

.dns_fail:
        mov esi, msg_dns_fail
        mov edi, status_buf
        call copy_zstr
        POPALL
        ret
.sock_fail:
        mov esi, msg_sock_fail
        mov edi, status_buf
        call copy_zstr
        POPALL
        ret
.connect_fail:
        mov eax, [sockfd]
        call net_close
        mov esi, msg_conn_fail
        mov edi, status_buf
        call copy_zstr
        POPALL
        ret
.send_fail:
        mov eax, [sockfd]
        call net_close
        mov esi, msg_send_fail
        mov edi, status_buf
        call copy_zstr
        POPALL
        ret

append_response:
        PUSHALL
        mov edi, response_buf
        add edi, [response_len]
        mov eax, MAX_RESPONSE - 1
        sub eax, [response_len]
        cmp ecx, eax
        jle .ar_size_ok
        mov ecx, eax
.ar_size_ok:
        test ecx, ecx
        jle .ar_done
        mov edx, ecx
        rep movsb
        add [response_len], edx
.ar_done:
        POPALL
        ret

;---------------------------------------
; check_http_redirect - Check for 3xx redirect in response
; Returns: EAX = 1 if redirect found, ESI = new URL location
;          EAX = 0 if no redirect
;---------------------------------------
check_http_redirect:
        push rbx
        push rcx
        push rdx
        mov esi, response_buf
        ; Check "HTTP/1.x 3xx"
        ; Skip to status code (after first space)
.chr_skip:
        cmp byte [esi], ' '
        je .chr_got_space
        cmp byte [esi], 0
        je .chr_no
        inc esi
        jmp .chr_skip
.chr_got_space:
        inc esi
        cmp byte [esi], '3'
        jne .chr_no

        ; It's a 3xx — find "Location:" header
        mov esi, response_buf
.chr_find_loc:
        cmp byte [esi], 0
        je .chr_no
        ; Check for end of headers
        cmp dword [esi], 0x0A0D0A0D
        je .chr_no
        ; Case-insensitive "Location: "
        mov al, [esi]
        or al, 0x20
        cmp al, 'l'
        jne .chr_next
        mov al, [esi + 1]
        or al, 0x20
        cmp al, 'o'
        jne .chr_next
        mov al, [esi + 2]
        or al, 0x20
        cmp al, 'c'
        jne .chr_next
        mov al, [esi + 3]
        or al, 0x20
        cmp al, 'a'
        jne .chr_next
        cmp byte [esi + 9], ' '
        jne .chr_next
        ; Found "Location: " — ESI+10 is the URL
        add esi, 10
        ; Skip leading spaces
.chr_skip_sp:
        cmp byte [esi], ' '
        jne .chr_got_url
        inc esi
        jmp .chr_skip_sp
.chr_got_url:
        ; Copy URL to address_buf (stop at CR/LF)
        mov edi, address_buf
        xor ecx, ecx
.chr_copy_url:
        mov al, [esi + ecx]
        cmp al, 0x0D
        je .chr_url_done
        cmp al, 0x0A
        je .chr_url_done
        cmp al, 0
        je .chr_url_done
        mov [edi + ecx], al
        inc ecx
        cmp ecx, 255
        jb .chr_copy_url
.chr_url_done:
        mov byte [edi + ecx], 0
        mov esi, address_buf
        mov eax, 1
        pop rdx
        pop rcx
        pop rbx
        ret
.chr_next:
        inc esi
        jmp .chr_find_loc
.chr_no:
        xor eax, eax
        pop rdx
        pop rcx
        pop rbx
        ret

;---------------------------------------
; check_chunked_encoding - Check for "Transfer-Encoding: chunked"
; Returns: EAX = 1 if chunked, 0 if not
;---------------------------------------
check_chunked_encoding:
        push rsi
        push rcx
        mov esi, response_buf
.cce_loop:
        cmp byte [esi], 0
        je .cce_no
        cmp dword [esi], 0x0A0D0A0D
        je .cce_no
        ; Check "chunked" (case-insensitive)
        mov al, [esi]
        or al, 0x20
        cmp al, 'c'
        jne .cce_next
        mov al, [esi + 1]
        or al, 0x20
        cmp al, 'h'
        jne .cce_next
        mov al, [esi + 2]
        or al, 0x20
        cmp al, 'u'
        jne .cce_next
        mov al, [esi + 3]
        or al, 0x20
        cmp al, 'n'
        jne .cce_next
        mov al, [esi + 4]
        or al, 0x20
        cmp al, 'k'
        jne .cce_next
        mov al, [esi + 5]
        or al, 0x20
        cmp al, 'e'
        jne .cce_next
        mov al, [esi + 6]
        or al, 0x20
        cmp al, 'd'
        jne .cce_next
        mov eax, 1
        pop rcx
        pop rsi
        ret
.cce_next:
        inc esi
        jmp .cce_loop
.cce_no:
        xor eax, eax
        pop rcx
        pop rsi
        ret

;---------------------------------------
; dechunk_response - Decode chunked transfer encoding in-place
; Finds the body (after \r\n\r\n) and reassembles chunks
;---------------------------------------
dechunk_response:
        PUSHALL
        ; Find body start
        mov esi, response_buf
        mov ecx, [response_len]
.dc_find:
        cmp ecx, 4
        jb .dc_done
        cmp dword [esi], 0x0A0D0A0D
        je .dc_found
        inc esi
        dec ecx
        jmp .dc_find
.dc_found:
        add esi, 4             ; skip \r\n\r\n
        mov edi, esi           ; write pointer = body start

        ; Process chunks
.dc_chunk:
        ; Parse hex chunk size
        xor eax, eax
.dc_hex:
        movzx ebx, byte [esi]
        cmp bl, '0'
        jb .dc_hex_letter
        cmp bl, '9'
        jbe .dc_hex_digit
.dc_hex_letter:
        or bl, 0x20
        cmp bl, 'a'
        jb .dc_hex_done
        cmp bl, 'f'
        ja .dc_hex_done
        sub bl, 'a' - 10
        jmp .dc_hex_add
.dc_hex_digit:
        sub bl, '0'
.dc_hex_add:
        shl eax, 4
        add eax, ebx
        inc esi
        jmp .dc_hex
.dc_hex_done:
        ; Skip \r\n after chunk size
        cmp byte [esi], 0x0D
        jne .dc_skip_lf
        inc esi
.dc_skip_lf:
        cmp byte [esi], 0x0A
        jne .dc_copy
        inc esi

.dc_copy:
        ; eax = chunk size
        test eax, eax
        jz .dc_finalize         ; 0-size chunk = end
        mov ecx, eax
        rep movsb
        ; Skip trailing \r\n
        cmp byte [esi], 0x0D
        jne .dc_no_cr
        inc esi
.dc_no_cr:
        cmp byte [esi], 0x0A
        jne .dc_chunk
        inc esi
        jmp .dc_chunk

.dc_finalize:
        ; Update response_len to new body end
        mov eax, edi
        sub eax, response_buf
        mov [response_len], eax
        mov byte [edi], 0
.dc_done:
        POPALL
        ret

parse_response_body:
        PUSHALL
        mov esi, response_buf
        mov ecx, [response_len]
.find_body:
        cmp ecx, 4
        jb .use_full
        cmp dword [esi], 0x0A0D0A0D
        je .body_found
        inc esi
        dec ecx
        jmp .find_body
.body_found:
        add esi, 4
        jmp .parse_now
.use_full:
        mov esi, response_buf
.parse_now:
        call parse_links
        call parse_text
        POPALL
        ret

parse_links:
        PUSHALL
        mov dword [link_count], 0
.pl_loop:
        mov al, [esi]
        test al, al
        jz .pl_done
        or al, 0x20
        cmp al, 'h'
        jne .pl_next
        mov al, [esi + 1]
        or al, 0x20
        cmp al, 'r'
        jne .pl_next
        mov al, [esi + 2]
        or al, 0x20
        cmp al, 'e'
        jne .pl_next
        mov al, [esi + 3]
        or al, 0x20
        cmp al, 'f'
        jne .pl_next
        cmp byte [esi + 4], '='
        jne .pl_next
        mov eax, [link_count]
        cmp eax, MAX_LINKS
        jge .pl_done
        lea edi, [esi + 5]
        mov al, [edi]
        cmp al, '"'
        je .quoted
        cmp al, 39
        je .singleq
        jmp .copy_unquoted
.quoted:
        inc edi
        mov dl, '"'
        jmp .copy_link
.singleq:
        inc edi
        mov dl, 39
        jmp .copy_link
.copy_unquoted:
        mov dl, ' '
.copy_link:
        mov eax, [link_count]
        imul eax, LINK_STRIDE
        lea ebx, [link_urls + eax]
        xor ecx, ecx
.copy_loop:
        mov al, [edi]
        test al, al
        jz .link_done
        cmp al, dl
        je .link_done
        cmp dl, ' '
        jne .store_char
        cmp al, ' '
        je .link_done
        cmp al, '>'
        je .link_done
.store_char:
        mov [ebx + ecx], al
        inc ecx
        inc edi
        cmp ecx, LINK_STRIDE - 1
        jb .copy_loop
.link_done:
        mov byte [ebx + ecx], 0
        push rsi
        mov esi, ebx
        mov edi, link_label_tmp
        call copy_link_label
        mov eax, [link_count]
        imul eax, LINK_STRIDE
        lea edi, [link_labels + eax]
        mov esi, link_label_tmp
        call copy_zstr
        pop rsi
        inc dword [link_count]
.pl_next:
        inc esi
        jmp .pl_loop
.pl_done:
        POPALL
        ret

parse_text:
        PUSHALL
        mov dword [page_line_count], 1
        mov dword [cur_line], 0
        mov dword [cur_col], 0
        mov byte [in_tag], 0
        mov byte [saw_space], 1
        call clear_page_lines
.pt_loop:
        mov al, [esi]
        test al, al
        jz .pt_done
        cmp byte [in_tag], 1
        je .pt_tag
        cmp al, '<'
        je .pt_enter_tag
        cmp al, 0x0D
        je .pt_next
        cmp al, 0x0A
        je .pt_newline
        cmp al, ' '
        jb .pt_space
        mov byte [saw_space], 0
        call append_page_char
        jmp .pt_next
.pt_space:
        cmp byte [saw_space], 1
        je .pt_next
        mov byte [saw_space], 1
        mov al, ' '
        call append_page_char
        jmp .pt_next
.pt_enter_tag:
        mov byte [in_tag], 1
        call maybe_break_before_tag
        jmp .pt_next
.pt_tag:
        cmp al, '>'
        jne .pt_next
        mov byte [in_tag], 0
        jmp .pt_next
.pt_newline:
        call page_newline
        mov byte [saw_space], 1
.pt_next:
        inc esi
        jmp .pt_loop
.pt_done:
        POPALL
        ret

maybe_break_before_tag:
        PUSHALL
        mov al, [esi + 1]
        or al, 0x20
        cmp al, 'b'
        je .mb_break
        cmp al, 'p'
        je .mb_break
        cmp al, 'l'
        je .mb_break
        cmp al, 'd'
        je .mb_break
        cmp al, 'h'
        je .mb_break
        cmp al, 't'
        je .mb_break
        jmp .mb_done
.mb_break:
        call page_newline
.mb_done:
        POPALL
        ret

append_page_char:
        PUSHALL
        mov dl, al
        cmp dword [cur_line], MAX_LINES
        jge .apc_done
        mov eax, [cur_col]
        cmp eax, LINE_CHARS
        jl .apc_store
        call page_newline
.apc_store:
        mov eax, [cur_line]
        imul eax, LINE_STRIDE
        add eax, [cur_col]
        mov [page_lines + eax], dl
        inc dword [cur_col]
        mov eax, [cur_line]
        imul eax, LINE_STRIDE
        add eax, [cur_col]
        mov byte [page_lines + eax], 0
.apc_done:
        POPALL
        ret

page_newline:
        PUSHALL
        cmp dword [cur_line], MAX_LINES - 1
        jge .pnl_done
        inc dword [cur_line]
        mov dword [cur_col], 0
        mov eax, [cur_line]
        inc eax
        mov [page_line_count], eax
.pnl_done:
        POPALL
        ret

clear_page_lines:
        PUSHALL
        mov edi, page_lines
        xor eax, eax
        mov ecx, MAX_LINES * LINE_STRIDE / 4
        rep stosd
        POPALL
        ret

clear_page_state:
        PUSHALL
        mov dword [link_count], 0
        mov dword [scroll_line], 0
        mov dword [page_line_count], 1
        mov dword [response_len], 0
        mov edi, response_buf
        xor eax, eax
        mov ecx, MAX_RESPONSE / 4
        rep stosd
        mov edi, link_urls
        mov ecx, (MAX_LINKS * LINK_STRIDE) / 4
        rep stosd
        mov edi, link_labels
        mov ecx, (MAX_LINKS * LINK_STRIDE) / 4
        rep stosd
        call clear_page_lines
        POPALL
        ret

build_http_request:
        PUSHALL
        mov edi, request_buf
        mov dword [edi], 'GET '
        add edi, 4
        mov esi, path
.bhr_path:
        lodsb
        test al, al
        jz .bhr_proto
        stosb
        jmp .bhr_path
.bhr_proto:
        mov esi, http_proto
.bhr_proto_loop:
        lodsb
        stosb
        test al, al
        jnz .bhr_proto_loop
        dec edi
        mov esi, host_hdr
.bhr_host_hdr:
        lodsb
        stosb
        test al, al
        jnz .bhr_host_hdr
        dec edi
        mov esi, hostname
.bhr_host:
        lodsb
        test al, al
        jz .bhr_end
        stosb
        jmp .bhr_host
.bhr_end:
        mov esi, conn_close
.bhr_cc:
        lodsb
        stosb
        test al, al
        jnz .bhr_cc
        dec edi
        mov eax, edi
        sub eax, request_buf
        mov [request_len], eax
        POPALL
        ret

parse_url:
        ; Input: ESI -> URL string
        ; Output: EAX = scheme id, hostname/path/port filled
        PUSHALL
        mov eax, SCHEME_HTTP
        mov [scheme_id], eax
        mov word [port], 80

        mov edi, hostname
        mov ecx, 256 / 4
        xor eax, eax
        rep stosd
        mov edi, path
        mov ecx, 256 / 4
        rep stosd

        mov esi, address_buf
        cmp dword [esi], 'http'
        jne .check_ftp
        mov dword [scheme_id], SCHEME_HTTP
        add esi, 4
        jmp .skip_colon
.check_ftp:
        cmp dword [esi], '://f'
        ; dummy compare anchor
        mov al, [esi]
        or al, 0x20
        cmp al, 'f'
        jne .check_telnet
        mov al, [esi + 1]
        or al, 0x20
        cmp al, 't'
        jne .check_telnet
        mov al, [esi + 2]
        or al, 0x20
        cmp al, 'p'
        jne .check_telnet
        mov dword [scheme_id], SCHEME_FTP
        mov word [port], 21
        add esi, 3
        jmp .skip_colon
.check_telnet:
        mov al, [esi]
        or al, 0x20
        cmp al, 't'
        jne .check_gopher
        mov al, [esi + 1]
        or al, 0x20
        cmp al, 'e'
        jne .check_gopher
        mov al, [esi + 2]
        or al, 0x20
        cmp al, 'l'
        jne .check_gopher
        mov al, [esi + 3]
        or al, 0x20
        cmp al, 'n'
        jne .check_gopher
        mov al, [esi + 4]
        or al, 0x20
        cmp al, 'e'
        jne .check_gopher
        mov al, [esi + 5]
        or al, 0x20
        cmp al, 't'
        jne .check_gopher
        mov dword [scheme_id], SCHEME_TELNET
        mov word [port], 23
        add esi, 6
        jmp .skip_colon
.check_gopher:
        mov al, [esi]
        or al, 0x20
        cmp al, 'g'
        jne .parse_host
        mov al, [esi + 1]
        or al, 0x20
        cmp al, 'o'
        jne .parse_host
        mov al, [esi + 2]
        or al, 0x20
        cmp al, 'p'
        jne .parse_host
        mov al, [esi + 3]
        or al, 0x20
        cmp al, 'h'
        jne .parse_host
        mov al, [esi + 4]
        or al, 0x20
        cmp al, 'e'
        jne .parse_host
        mov al, [esi + 5]
        or al, 0x20
        cmp al, 'r'
        jne .parse_host
        mov dword [scheme_id], SCHEME_GOPHER
        mov word [port], 70
        add esi, 6
.skip_colon:
        cmp byte [esi], ':'
        jne .parse_host
        inc esi
        cmp word [esi], '//'
        jne .parse_host
        add esi, 2

.parse_host:
        mov edi, hostname
        xor ecx, ecx
.ph_loop:
        mov al, [esi]
        cmp al, '/'
        je .host_done
        cmp al, ':'
        je .host_port
        cmp al, 0
        je .host_done
        mov [edi + ecx], al
        inc ecx
        inc esi
        cmp ecx, 255
        jb .ph_loop
.host_done:
        mov byte [edi + ecx], 0
        jmp .parse_path
.host_port:
        mov byte [edi + ecx], 0
        inc esi
        xor eax, eax
.pp_loop:
        movzx ebx, byte [esi]
        cmp bl, '/'
        je .pp_done
        cmp bl, 0
        je .pp_done
        sub bl, '0'
        imul eax, 10
        add eax, ebx
        inc esi
        jmp .pp_loop
.pp_done:
        mov [port], ax
.parse_path:
        mov edi, path
        cmp byte [esi], '/'
        je .copy_path
        mov byte [edi], '/'
        mov byte [edi + 1], 0
        jmp .pz_done
.copy_path:
        mov ecx, 255
.cp_loop:
        lodsb
        stosb
        test al, al
        jz .pz_done
        dec ecx
        jnz .cp_loop
        mov byte [edi], 0
.pz_done:
        mov eax, [scheme_id]
        mov [rsp + 112], eax
        POPALL
        ret

build_dispatch_command:
        PUSHALL
        mov edi, exec_buf
        mov eax, [scheme_id]
        cmp eax, SCHEME_FTP
        je .bdc_ftp
        cmp eax, SCHEME_TELNET
        je .bdc_telnet
        mov esi, cmd_gopher
        jmp .bdc_copy_name
.bdc_ftp:
        mov esi, cmd_ftp
        jmp .bdc_copy_name
.bdc_telnet:
        mov esi, cmd_telnet
.bdc_copy_name:
        call copy_zstr
        dec edi
        mov al, ' '
        stosb
        mov esi, hostname
.bdc_host:
        lodsb
        test al, al
        jz .bdc_path
        stosb
        jmp .bdc_host
.bdc_path:
        mov eax, [scheme_id]
        cmp eax, SCHEME_GOPHER
        jne .bdc_port
        cmp byte [path], '/'
        jne .bdc_port
        cmp byte [path + 1], 0
        je .bdc_port
        mov al, ' '
        stosb
        mov esi, path
.bdc_gpath:
        lodsb
        test al, al
        jz .bdc_port
        stosb
        jmp .bdc_gpath
.bdc_port:
        cmp word [port], 0
        je .bdc_done
        mov eax, [scheme_id]
        cmp eax, SCHEME_FTP
        jne .bdc_other_port
        cmp word [port], 21
        je .bdc_done
.bdc_other_port:
        cmp eax, SCHEME_TELNET
        jne .bdc_gp_check
        cmp word [port], 23
        je .bdc_done
.bdc_gp_check:
        cmp eax, SCHEME_GOPHER
        jne .bdc_add_port
        cmp word [port], 70
        je .bdc_done
.bdc_add_port:
        mov al, ' '
        stosb
        movzx eax, word [port]
        call u32_to_ascii
.bdc_done:
        mov byte [edi], 0
        POPALL
        ret

click_link_hit:
        ; EBX = x, ECX = y relative to window
        PUSHALL
        mov eax, -1
        cmp ecx, LINKS_Y + 24
        jl .clh_done
        cmp ecx, LINKS_Y + 66
        jg .clh_done
        cmp ebx, BODY_X + 6
        jl .clh_done
        xor edx, edx
        mov eax, ecx
        sub eax, LINKS_Y + 24
        mov esi, 24
        div esi
        mov edi, eax            ; row
        mov eax, ebx
        sub eax, BODY_X + 6
        xor edx, edx
        mov esi, 266
        div esi
        cmp eax, 1
        jg .clh_miss
        mov esi, edx
        cmp esi, 254
        jg .clh_miss
        shl edi, 1
        add edi, eax
        cmp edi, [link_count]
        jge .clh_miss
        mov eax, edi
        jmp .clh_done
.clh_miss:
        mov eax, -1
.clh_done:
        mov [rsp + 112], eax
        POPALL
        ret

navigate_link_index:
        PUSHALL
        imul eax, LINK_STRIDE
        lea esi, [link_urls + eax]
        mov [link_nav_ptr], rsi
        call resolve_link_address
        call browser_navigate
        POPALL
        ret

resolve_link_address:
        PUSHALL
        mov rsi, [link_nav_ptr]
        mov al, [esi]
        test al, al
        jz .rla_done
        ; absolute scheme
        mov al, [esi]
        or al, 0x20
        cmp al, 'h'
        je .copy_abs
        cmp al, 'f'
        je .copy_abs
        cmp al, 't'
        je .copy_abs
        cmp al, 'g'
        je .copy_abs
        ; relative root
        cmp byte [esi], '/'
        jne .relative_simple
        mov edi, address_buf
        mov esi, http_prefix
        call copy_zstr
        dec edi
        mov esi, hostname
        call copy_zstr
        dec edi
        mov rsi, [link_nav_ptr]
        jmp .copy_tail
.relative_simple:
        mov edi, address_buf
        mov esi, http_prefix
        call copy_zstr
        dec edi
        mov esi, hostname
        call copy_zstr
        dec edi
        mov al, '/'
        stosb
        mov rsi, [link_nav_ptr]
        jmp .copy_tail
.copy_abs:
        mov edi, address_buf
        mov rsi, [link_nav_ptr]
.copy_tail:
        call copy_zstr
        mov esi, address_buf
        call str_len
        mov [address_len], eax
.rla_done:
        POPALL
        ret

copy_link_label:
        PUSHALL
        xor ecx, ecx
.cll_loop:
        mov al, [esi + ecx]
        test al, al
        jz .cll_done
        mov [edi + ecx], al
        inc ecx
        cmp ecx, 40
        jb .cll_loop
.cll_done:
        mov byte [edi + ecx], 0
        POPALL
        ret

copy_zstr:
        ; ESI src, EDI dst
        push rax
.cz_loop:
        lodsb
        stosb
        test al, al
        jnz .cz_loop
        pop rax
        ret

str_len:
        push rcx
        xor eax, eax
.sl_loop:
        cmp byte [esi + eax], 0
        je .sl_done
        inc eax
        jmp .sl_loop
.sl_done:
        pop rcx
        ret

trim_leading_spaces:
        ; ESI input, returns trimmed ESI
        push rax
.tls_loop:
        cmp byte [esi], ' '
        jne .tls_done
        inc esi
        jmp .tls_loop
.tls_done:
        pop rax
        ret

trim_trailing_spaces:
        PUSHALL
        mov edi, esi
        call str_len
        test eax, eax
        jz .tts_done
        lea edi, [esi + eax - 1]
.tts_loop:
        cmp edi, esi
        jb .tts_done
        cmp byte [edi], ' '
        jne .tts_done
        mov byte [edi], 0
        dec edi
        jmp .tts_loop
.tts_done:
        POPALL
        ret

str_eq:
        ; ESI left, EDI right, returns EAX = 1 if equal else 0
        push rbx
        xor eax, eax
.seq_loop:
        mov bl, [esi]
        cmp bl, [edi]
        jne .seq_done
        test bl, bl
        je .seq_match
        inc esi
        inc edi
        jmp .seq_loop
.seq_match:
        mov eax, 1
.seq_done:
        pop rbx
        ret

u32_to_ascii:
        ; EAX value, EDI buffer write cursor
        PUSHALL
        cmp eax, 0
        jne .uta_nonzero
        mov byte [edi], '0'
        inc edi
        jmp .uta_done
.uta_nonzero:
        xor ecx, ecx
        mov ebx, 10
.uta_push:
        xor edx, edx
        div ebx
        push rdx
        inc ecx
        test eax, eax
        jnz .uta_push
.uta_pop:
        pop rax
        add al, '0'
        stosb
        dec ecx
        jnz .uta_pop
.uta_done:
        mov [rsp + 112], edi
        POPALL
        ret

title_str:      db "BForager", 0
back_label:     db "<", 0
forward_label:  db ">", 0
home_label:     db "H", 0
reload_label:   db "R", 0
go_label:       db "GO", 0
cursor_str:     db "_", 0
links_label:    db "Links", 0
home_prefix:    db "Home: ", 0
http_prefix:    db "http://", 0
http_proto:     db " HTTP/1.1", 0x0D, 0x0A, 0
host_hdr:       db "Host: ", 0
conn_close:     db 0x0D, 0x0A, "Connection: close", 0x0D, 0x0A
                db "Accept: text/html, text/*", 0x0D, 0x0A, 0x0D, 0x0A, 0
http_redir_max: equ 5
http_redir_count: db 0
cmd_ftp:        db "ftp", 0
cmd_telnet:     db "telnet", 0
cmd_gopher:     db "gopher", 0
msg_idle:       db "Ready", 0
msg_connecting: db "Connecting...", 0
msg_loaded:     db "Loaded", 0
msg_no_url:     db "Enter a site in the address bar", 0
msg_dns_fail:   db "DNS resolution failed", 0
msg_sock_fail:  db "Socket creation failed", 0
msg_conn_fail:  db "Connection failed", 0
msg_send_fail:  db "Send failed", 0
msg_launch_fail: db "Could not launch external protocol handler", 0
msg_home_set:   db "Home page set", 0
msg_redir_limit: db "Too many redirects", 0

win_id:         dd 0
address_len:    dd 0
edit_focus:     db 1
in_tag:         db 0
saw_space:      db 1
scheme_id:      dd 0
scroll_line:    dd 0
page_line_count: dd 1
cur_line:       dd 0
cur_col:        dd 0
link_count:     dd 0
server_ip:      dd 0
sockfd:         dd 0
request_len:    dd 0
response_len:   dd 0
retry_count:    dd 0
link_nav_ptr:   dq 0
link_draw_idx:  dd 0
link_draw_x:    dd 0
link_draw_y:    dd 0
history_count:  dd 0
history_index:  dd -1
history_suppress: db 0

home_url:       db "example.com"
                times 245 db 0
home_ind_buf:   times 32 db 0

port:           dw 80
address_buf:    times 256 db 0
hostname:       times 256 db 0
path:           times 256 db 0
status_buf:     times 128 db 0
arg_buf:        times 512 db 0
request_buf:    times 1024 db 0
recv_buf:       times 1024 db 0
response_buf:   times MAX_RESPONSE db 0
page_lines:     times MAX_LINES * LINE_STRIDE db 0
link_urls:      times MAX_LINKS * LINK_STRIDE db 0
link_labels:    times MAX_LINKS * LINK_STRIDE db 0
history_buf:    times MAX_HISTORY * HISTORY_STRIDE db 0
link_label_tmp: times 64 db 0
exec_buf:       times 320 db 0