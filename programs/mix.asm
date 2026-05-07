; mix.asm - WAV Audio Mixer (v9.0)
; Mixes two WAV files and plays via sys_audio_write_chan.
; Usage: mix <file1.wav> <file2.wav>
; Both files must be 44100 Hz, 16-bit, stereo PCM.

%include "syscalls.inc"

WAV_HDR_SIZE equ 44
MIX_BUF_SIZE equ 65536       ; 64 KB mix chunk

start:
        mov eax, SYS_GETARGS
        mov ebx, arg_buf
        int 0x80

        mov esi, arg_buf
        call skip_spaces
        cmp byte [esi], 0
        je .usage

        ; Parse file1
        mov edi, fname1
        call copy_token
        call skip_spaces
        cmp byte [esi], 0
        je .usage

        ; Parse file2
        mov edi, fname2
        call copy_token

        ; Open audio channel
        mov eax, SYS_AUDIO_OPEN
        int 0x80
        cmp eax, -1
        je .no_audio
        mov [chan_id], eax

        ; Read and mix files chunk by chunk
        ; For simplicity: read entire file1 and file2 into memory, mix, send
        mov eax, SYS_FREAD
        mov ebx, fname1
        mov esi, buf1
        mov ecx, WAV_HDR_SIZE + MIX_BUF_SIZE
        int 0x80
        cmp eax, -1
        je .open_fail
        mov [len1], eax

        mov eax, SYS_FREAD
        mov ebx, fname2
        mov esi, buf2
        mov ecx, WAV_HDR_SIZE + MIX_BUF_SIZE
        int 0x80
        cmp eax, -1
        je .open_fail
        mov [len2], eax

        ; Mix: for each 16-bit sample pair, add and clamp
        ; Skip WAV headers
        mov esi, buf1 + WAV_HDR_SIZE
        mov edi, buf2 + WAV_HDR_SIZE
        mov ecx, [len1]
        sub ecx, WAV_HDR_SIZE
        push ecx
        mov ecx, [len2]
        sub ecx, WAV_HDR_SIZE
        pop ebx
        cmp ecx, ebx
        jle .use_len2
        mov ecx, ebx           ; use min(len1,len2)
.use_len2:
        ; ecx = number of bytes to mix (samples * 2 bytes each)
        shr ecx, 1             ; pairs of bytes → 16-bit samples
        mov [mix_samples], ecx

        xor ebx, ebx           ; sample index
.mix_loop:
        cmp ebx, [mix_samples]
        jge .mix_done
        ; Load 16-bit signed sample from each buffer
        movsx eax, word [esi + ebx*2]
        movsx edx, word [edi + ebx*2]
        add eax, edx
        ; Clamp to [-32768, 32767]
        cmp eax, 32767
        jle .clamp_lo
        mov eax, 32767
        jmp .store_sample
.clamp_lo:
        cmp eax, -32768
        jge .store_sample
        mov eax, -32768
.store_sample:
        mov [mix_out + ebx*2], ax
        inc ebx
        jmp .mix_loop
.mix_done:

        ; Send mixed PCM to audio channel
        mov eax, SYS_AUDIO_WRITE
        mov ebx, [chan_id]
        mov esi, mix_out
        mov ecx, [mix_samples]
        shl ecx, 1             ; bytes = samples * 2
        int 0x80

        ; Close channel
        mov eax, SYS_AUDIO_CLOSE_CHAN
        mov ebx, [chan_id]
        int 0x80

        ; Done
        mov eax, SYS_PRINT
        mov ebx, msg_done
        int 0x80
        jmp .exit

.no_audio:
        mov eax, SYS_PRINT
        mov ebx, msg_no_audio
        int 0x80
        jmp .exit

.open_fail:
        mov eax, SYS_PRINT
        mov ebx, msg_open_fail
        int 0x80
        jmp .exit

.usage:
        mov eax, SYS_PRINT
        mov ebx, msg_usage
        int 0x80

.exit:
        mov eax, SYS_EXIT
        xor ebx, ebx
        int 0x80

;---------------------------------------
; copy_token: copy word from ESI to EDI (up to space/null)
;---------------------------------------
copy_token:
        push ecx
        mov ecx, 127
.ct_loop:
        lodsb
        cmp al, ' '
        je .ct_done
        cmp al, 0
        je .ct_done
        stosb
        dec ecx
        jnz .ct_loop
.ct_done:
        mov byte [edi], 0
        pop ecx
        ret

;---------------------------------------
; skip_spaces
;---------------------------------------
skip_spaces:
        lodsb
        cmp al, ' '
        je skip_spaces
        dec esi
        ret

;=======================================================================
; DATA
;=======================================================================
msg_usage:     db "Usage: mix <file1.wav> <file2.wav>", 0x0A, 0
msg_done:      db "Mix complete.", 0x0A, 0
msg_no_audio:  db "Error: audio not available", 0x0A, 0
msg_open_fail: db "Error: cannot open input file", 0x0A, 0

arg_buf:       times 256 db 0
fname1:        times 128 db 0
fname2:        times 128 db 0
chan_id:       dd 0
len1:          dd 0
len2:          dd 0
mix_samples:   dd 0

; Demand-paged buffers (v9.0 paging)
buf1:     times WAV_HDR_SIZE + MIX_BUF_SIZE db 0
buf2:     times WAV_HDR_SIZE + MIX_BUF_SIZE db 0
mix_out:  times MIX_BUF_SIZE db 0
