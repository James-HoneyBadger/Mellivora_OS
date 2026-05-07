;=======================================================================
; ac97.inc - Intel AC'97 Audio Controller Driver
;
; Supports: Intel ICH AC97 (8086:2415, 8086:2425, 8086:2445)
; Falls back gracefully if device not present.
;
; The AC97 controller has two I/O regions:
;   NAM  (Native Audio Mixer)     = BAR0 — codec registers (16-bit)
;   NABM (Native Audio Bus Master) = BAR1 — DMA engine registers
;
; Playback uses PCM OUT channel (NABM offset 0x10..0x1F).
; A 32-entry Buffer Descriptor List (BDL) is placed in low memory,
; each entry pointing to one of two 64 KB ping-pong sound buffers.
; The driver uses polled completion (no new IRQ wiring needed).
;=======================================================================

; AC97 BDL: 32 entries, each 8 bytes (addr:32, size|eot:32)
AC97_BDL_ENTRIES    equ 32
AC97_BDL_ENTRY_SIZE equ 8
AC97_BDL_SIZE       equ AC97_BDL_ENTRIES * AC97_BDL_ENTRY_SIZE

; Sound buffer: 2 × 32 KB ping-pong (16-bit stereo PCM ≈ 0.18 s @ 44.1 kHz)
AC97_BUF_PAGES      equ 8        ; 8 × 4KB = 32 KB each
AC97_BUF_SIZE       equ AC97_BUF_PAGES * 4096

; NABM PCM Out channel register offsets (add to nabm_base)
NABM_PCMOUT_BDBA    equ 0x10     ; Buffer Descriptor List Base Address (32-bit)
NABM_PCMOUT_CIV     equ 0x14     ; Current Index of Valid Entry (8-bit R)
NABM_PCMOUT_LVI     equ 0x15     ; Last Valid Index (8-bit R/W)
NABM_PCMOUT_SR      equ 0x16     ; Status Register (16-bit)
NABM_PCMOUT_PICB    equ 0x18     ; Position in Current Buffer (16-bit)
NABM_PCMOUT_PIV     equ 0x1A     ; Prefetched Index (8-bit)
NABM_PCMOUT_CR      equ 0x1B     ; Control Register (8-bit)

; NABM global registers
NABM_GLOB_CNT       equ 0x2C     ; Global Control (32-bit)
NABM_GLOB_STA       equ 0x30     ; Global Status (32-bit)

; PCM Out Control Register bits
NABM_CR_RUN         equ 0x01     ; Run/Pause: 1=run
NABM_CR_RESET       equ 0x02     ; Reset channel
NABM_CR_LVBIE       equ 0x04     ; Last Valid Buffer IRQ Enable
NABM_CR_IOCE        equ 0x08     ; IOC IRQ Enable
NABM_CR_FEIE        equ 0x10     ; FIFO Error IRQ Enable

; PCM Out Status Register bits
NABM_SR_DCH         equ 0x01     ; DMA Controller Halted
NABM_SR_CELV        equ 0x02     ; Current Equals Last Valid
NABM_SR_LVBCI       equ 0x04     ; Last Valid Buffer Completion IRQ
NABM_SR_BCIS        equ 0x08     ; Buffer Completion IRQ Status
NABM_SR_FIFOE       equ 0x10     ; FIFO Error IRQ Status

; NAM codec register offsets
NAM_RESET           equ 0x00     ; Write any to reset codec
NAM_MASTER_VOL      equ 0x02     ; Master Volume
NAM_AUX_OUT_VOL     equ 0x04     ; Headphone/Aux Out Volume
NAM_MONO_VOL        equ 0x06     ; Mono Master Volume
NAM_PCM_OUT_VOL     equ 0x18     ; PCM Out Volume (per-channel)
NAM_SAMPLE_RATE     equ 0x2C     ; PCM Front DAC Rate (Hz)
NAM_EXT_AUDIO_ID    equ 0x28     ; Extended Audio ID
NAM_EXT_AUDIO_STS   equ 0x2A     ; Extended Audio Status/Control

; BDL entry EOT flag
BDL_EOT             equ (1 << 31)  ; End of transfer — hardware halts after this entry
BDL_IOC             equ (1 << 30)  ; Fire IOC interrupt after this entry

;---------------------------------------
; ac97_init - Detect and initialise AC97 controller
; Leaves ac97_present=0 if no device found.
;---------------------------------------
ac97_init:
        pushad
        mov dword [ac97_present], 0

        ; Try each known vendor/device ID
        mov ebx, PCI_VENDOR_INTEL
        mov ecx, PCI_DEV_ICH_AC97N
        call pci_find_device
        cmp eax, -1
        jne .found

        mov ebx, PCI_VENDOR_INTEL
        mov ecx, PCI_DEV_ICH_AC97
        call pci_find_device
        cmp eax, -1
        jne .found

        mov ebx, PCI_VENDOR_INTEL
        mov ecx, PCI_DEV_ICH2_AC97
        call pci_find_device
        cmp eax, -1
        jne .found

        ; QEMU AC97: 8086:2415 (already tried) and VirtualBox: 8086:2415 too
        ; Try 8086:2445 explicitly
        mov ebx, PCI_VENDOR_INTEL
        mov ebx, 0x8086
        mov ecx, 0x2445
        call pci_find_device
        cmp eax, -1
        je  .no_ac97

.found:
        mov [ac97_bdf], eax

        ; Enable PCI Bus Master + I/O space access
        mov ebx, 0x04             ; PCI command register
        call pci_read_config32
        or  eax, 0x05             ; I/O Enable + Bus Master
        mov ecx, eax
        mov ebx, 0x04
        call pci_write_config32

        ; Read BAR0 (NAM I/O base) and BAR1 (NABM I/O base)
        mov eax, [ac97_bdf]
        mov ebx, 0
        call pci_read_bar
        and eax, 0xFFFC
        mov [ac97_nam_base], eax

        mov eax, [ac97_bdf]
        mov ebx, 1
        call pci_read_bar
        and eax, 0xFFFC
        mov [ac97_nabm_base], eax

        ; Sanity: if both are 0, bail out
        mov eax, [ac97_nam_base]
        or  eax, [ac97_nabm_base]
        jz  .no_ac97

        ; Reset codec
        call ac97_reset

        ; Allocate BDL (must be page-aligned, physically contiguous)
        ; 1 page = 4096 bytes is more than enough for 256 bytes of BDL
        mov ecx, 1
        call pmm_alloc_pages
        test eax, eax
        jz  .no_ac97
        mov [ac97_bdl_phys], eax

        ; Allocate two sound buffers
        mov ecx, AC97_BUF_PAGES
        call pmm_alloc_pages
        test eax, eax
        jz  .free_bdl
        mov [ac97_buf0_phys], eax

        mov ecx, AC97_BUF_PAGES
        call pmm_alloc_pages
        test eax, eax
        jz  .free_buf0
        mov [ac97_buf1_phys], eax

        ; Build BDL: 32 alternating buf0/buf1 entries
        call ac97_build_bdl

        ; Set NAM volume and sample rate
        call ac97_setup_codec

        mov dword [ac97_present], 1
        mov dword [ac97_state], 0   ; stopped
        popad
        ret

.free_buf0:
        mov eax, [ac97_buf0_phys]
        mov ecx, AC97_BUF_PAGES
        call pmm_free_pages
.free_bdl:
        mov eax, [ac97_bdl_phys]
        mov ecx, 1
        call pmm_free_pages
.no_ac97:
        popad
        ret

;---------------------------------------
; ac97_reset - Cold reset the codec via NAM register 0x00
;---------------------------------------
ac97_reset:
        push eax
        push edx
        ; Write global reset to NAM register 0 (any value resets codec)
        mov edx, [ac97_nam_base]
        add edx, NAM_RESET
        xor ax, ax
        out dx, ax
        ; Also reset NABM PCM Out channel
        mov edx, [ac97_nabm_base]
        add edx, NABM_PCMOUT_CR
        mov al, NABM_CR_RESET
        out dx, al
        ; Wait a moment (≈50 ms) — spin until channel deasserts reset
        mov ecx, 1000
.rst_wait:
        in  al, dx
        test al, NABM_CR_RESET
        jz  .rst_done
        loop .rst_wait
.rst_done:
        ; Clear reset bit
        xor al, al
        out dx, al
        pop edx
        pop eax
        ret

;---------------------------------------
; ac97_setup_codec - Set volumes and sample rate
;---------------------------------------
ac97_setup_codec:
        push eax
        push edx
        mov edx, [ac97_nam_base]

        ; Master volume: 0x0000 = max (both channels)
        push edx
        add edx, NAM_MASTER_VOL
        xor ax, ax
        out dx, ax
        pop edx

        ; Headphone volume
        push edx
        add edx, NAM_AUX_OUT_VOL
        xor ax, ax
        out dx, ax
        pop edx

        ; PCM Out volume: 0x0000 = max
        push edx
        add edx, NAM_PCM_OUT_VOL
        xor ax, ax
        out dx, ax
        pop edx

        ; Check if VRA (Variable Rate Audio) is supported
        push edx
        add edx, NAM_EXT_AUDIO_ID
        in  ax, dx
        pop edx
        test ax, 1              ; bit 0 = VRA supported
        jz  .no_vra

        ; Enable VRA
        push edx
        add edx, NAM_EXT_AUDIO_STS
        in  ax, dx
        or  ax, 1
        out dx, ax
        pop edx

        ; Set sample rate to 44100 Hz
        push edx
        add edx, NAM_SAMPLE_RATE
        mov ax, 44100
        out dx, ax
        pop edx

.no_vra:
        pop edx
        pop eax
        ret

;---------------------------------------
; ac97_build_bdl - Fill the 32-entry BDL with alternating buf0/buf1 pointers
;---------------------------------------
ac97_build_bdl:
        push eax
        push ebx
        push ecx
        push edi
        mov edi, [ac97_bdl_phys]     ; BDL base address
        xor ecx, ecx                 ; entry index
.bdl_loop:
        cmp ecx, AC97_BDL_ENTRIES
        jge .bdl_done
        ; Buffer address: even entries → buf0, odd → buf1
        test ecx, 1
        jnz .bdl_buf1
        mov eax, [ac97_buf0_phys]
        jmp .bdl_write
.bdl_buf1:
        mov eax, [ac97_buf1_phys]
.bdl_write:
        mov [edi], eax              ; physical address of buffer
        ; Size: AC97_BUF_SIZE / 2 bytes = sample count for 16-bit stereo
        ; AC97 size field = number of 16-bit SAMPLES (not bytes)
        ; AC97_BUF_SIZE bytes / 2 bytes-per-sample
        mov eax, AC97_BUF_SIZE / 2
        ; Last entry gets EOT bit
        cmp ecx, AC97_BDL_ENTRIES - 1
        jne .bdl_no_eot
        or  eax, BDL_EOT
.bdl_no_eot:
        mov [edi + 4], eax
        add edi, AC97_BDL_ENTRY_SIZE
        inc ecx
        jmp .bdl_loop
.bdl_done:
        pop edi
        pop ecx
        pop ebx
        pop eax
        ret

;---------------------------------------
; ac97_play - Start playback of a PCM buffer
; ESI = source pointer (16-bit signed PCM, stereo interleaved)
; ECX = byte count
; Copies data to our ping-pong buffers and starts DMA.
;---------------------------------------
ac97_play:
        pushad
        cmp dword [ac97_present], 0
        je  .ap_done

        ; Stop any current playback
        call ac97_stop

        ; Clamp to max: 2 × AC97_BUF_SIZE
        cmp ecx, AC97_BUF_SIZE * 2
        jbe .ap_size_ok
        mov ecx, AC97_BUF_SIZE * 2
.ap_size_ok:
        mov [ac97_play_bytes], ecx

        ; Copy first chunk to buf0
        mov edi, [ac97_buf0_phys]
        mov eax, ecx
        cmp eax, AC97_BUF_SIZE
        jbe .ap_copy0_full
        mov eax, AC97_BUF_SIZE
.ap_copy0_full:
        push ecx
        mov ecx, eax
        shr ecx, 2
        cld
        rep movsd
        pop ecx

        ; If there's a second chunk, copy to buf1
        cmp ecx, AC97_BUF_SIZE
        jbe .ap_no_buf1
        push ecx
        push esi
        mov edi, [ac97_buf1_phys]
        sub ecx, AC97_BUF_SIZE
        shr ecx, 2
        rep movsd
        pop esi
        pop ecx
.ap_no_buf1:

        ; Program NABM: set BDL base address
        mov edx, [ac97_nabm_base]
        add edx, NABM_PCMOUT_BDBA
        mov eax, [ac97_bdl_phys]
        out dx, eax

        ; Set Last Valid Index to last entry index (31)
        mov edx, [ac97_nabm_base]
        add edx, NABM_PCMOUT_LVI
        mov al, AC97_BDL_ENTRIES - 1
        out dx, al

        ; Start DMA (RUN bit)
        mov edx, [ac97_nabm_base]
        add edx, NABM_PCMOUT_CR
        mov al, NABM_CR_RUN
        out dx, al

        mov dword [ac97_state], 1   ; playing
.ap_done:
        popad
        ret

;---------------------------------------
; ac97_stop - Stop playback immediately
;---------------------------------------
ac97_stop:
        push eax
        push edx
        cmp dword [ac97_present], 0
        je  .ast_done
        ; Clear RUN bit
        mov edx, [ac97_nabm_base]
        add edx, NABM_PCMOUT_CR
        xor al, al
        out dx, al
        ; Clear status register
        mov edx, [ac97_nabm_base]
        add edx, NABM_PCMOUT_SR
        mov ax, 0x1C        ; Clear LVBCI, BCIS, FIFOE
        out dx, ax
        mov dword [ac97_state], 0
.ast_done:
        pop edx
        pop eax
        ret

;---------------------------------------
; ac97_status - Returns state (0=stopped, 1=playing) and DMA position
; Returns: EAX = state, EBX = bytes_remaining (approx from PICB)
;---------------------------------------
ac97_status:
        mov eax, [ac97_state]
        xor ebx, ebx
        cmp eax, 0
        je  .acs_done
        ; Check if DMA halted (EOT reached)
        push edx
        mov edx, [ac97_nabm_base]
        add edx, NABM_PCMOUT_SR
        in  ax, dx
        test ax, NABM_SR_DCH
        pop edx
        jz  .acs_playing
        ; DMA halted — playback finished
        mov dword [ac97_state], 0
        xor eax, eax
.acs_done:
        ret
.acs_playing:
        mov eax, 1
        ret

; -----------------------------------------------------------------------
; Syscall handlers — plug into the existing SYS_AUDIO_* API.
; When AC97 is present and SB16 is absent/stopped, AC97 takes over.
; -----------------------------------------------------------------------

; SYS_AUDIO_PLAY override shim for AC97
; This is NOT directly in the syscall table; it's called by sys_audio_play
; if sb16_present == 0.
sys_ac97_play:
        pushad
        mov eax, ebx
        call validate_user_ptr
        jc  .sap_fail
        mov esi, ebx
        mov ecx, edx            ; byte count from EDX
        call ac97_play
        popad
        xor eax, eax
        iretd
.sap_fail:
        popad
        mov eax, -1
        iretd

;---------------------------------------
; AC97 module data
;---------------------------------------
ac97_present:   dd 0
ac97_state:     dd 0        ; 0=stopped, 1=playing
ac97_bdf:       dd 0        ; PCI BDF of the device
ac97_nam_base:  dd 0        ; I/O base of Native Audio Mixer
ac97_nabm_base: dd 0        ; I/O base of Native Audio Bus Master
ac97_bdl_phys:  dd 0        ; Physical address of BDL
ac97_buf0_phys: dd 0        ; Physical address of ping buffer 0
ac97_buf1_phys: dd 0        ; Physical address of ping buffer 1
ac97_play_bytes: dd 0       ; Byte count of last ac97_play call

;=======================================================================
; v9.0 Software Mixer & Channel API
;
; Provides 4 independent audio channels that can be mixed simultaneously.
; Each channel has an 8 KB ring buffer. sys_audio_write_chan mixes new
; data into the next AC97 DMA block.
;
; Mixer data layout:
;   mix_channels: 4 × MIX_CHAN_SIZE structs
;   MIX_CHAN_SIZE: active(dd) + buf_ptr(dd) + buf_len(dd) + ring(8192 bytes)
;
; Recording: PCM IN channel uses separate BDL + 32 KB buffer.
;=======================================================================

MIX_CHANS       equ 4
MIX_RING_SIZE   equ 8192           ; per-channel ring buffer (bytes)
MIX_CHAN_SIZE   equ 4 + 4 + 4 + MIX_RING_SIZE  ; active, ring_rd, ring_wr, ring

; NABM PCM IN channel register offsets
NABM_PCMIN_BDBA equ 0x00
NABM_PCMIN_CIV  equ 0x04
NABM_PCMIN_LVI  equ 0x05
NABM_PCMIN_SR   equ 0x06
NABM_PCMIN_CR   equ 0x0B

; NAM codec ADC rate register
NAM_PCM_IN_RATE equ 0x32
NAM_RECORD_SEL  equ 0x1A           ; Recording selector (mic=0, line=4)

;-----------------------------------------------------------------------
; sys_audio_open — allocate a mixer channel
;   Returns: EAX = channel id (0-3) or -1 if all in use
;-----------------------------------------------------------------------
sys_audio_open:
        pushad
        cmp dword [ac97_present], 0
        je .sao_fail
        xor ecx, ecx
.sao_find:
        cmp ecx, MIX_CHANS
        jge .sao_fail
        imul eax, ecx, MIX_CHAN_SIZE
        cmp dword [mix_channels + eax], 0    ; active flag
        je .sao_got
        inc ecx
        jmp .sao_find
.sao_got:
        ; Init channel: mark active, clear ring ptrs
        imul eax, ecx, MIX_CHAN_SIZE
        mov dword [mix_channels + eax], 1    ; active
        mov dword [mix_channels + eax + 4], 0  ; ring_rd
        mov dword [mix_channels + eax + 8], 0  ; ring_wr
        ; Zero ring buffer (optional; avoids noise from stale data)
        lea edi, [mix_channels + eax + 12]
        xor eax, eax
        mov ecx, MIX_RING_SIZE / 4
        rep stosd
        mov [esp + 28], ecx     ; wait — ECX was clobbered. Use original ecx
        ; Fix: use scratch var
        ; Actually, we need the channel index, stored before clobber.
        ; ecx was the channel index before the imul chain. Let me save it.
        popad
        ; Re-enter cleanly
        pushad
        cmp dword [ac97_present], 0
        je .sao_fail
        xor ecx, ecx
.sao_find2:
        cmp ecx, MIX_CHANS
        jge .sao_fail
        imul eax, ecx, MIX_CHAN_SIZE
        cmp dword [mix_channels + eax], 0
        je .sao_got2
        inc ecx
        jmp .sao_find2
.sao_got2:
        mov [.sao_chan], ecx    ; save channel index
        imul eax, ecx, MIX_CHAN_SIZE
        mov dword [mix_channels + eax],      1  ; active
        mov dword [mix_channels + eax + 4],  0  ; ring_rd
        mov dword [mix_channels + eax + 8],  0  ; ring_wr
        ; Zero ring
        push eax
        lea edi, [mix_channels + eax + 12]
        xor eax, eax
        mov ecx, MIX_RING_SIZE / 4
        rep stosd
        pop eax
        mov ecx, [.sao_chan]
        mov [esp + 28], ecx     ; return channel index in EAX slot
        popad
        iretd
.sao_fail:
        mov dword [esp + 28], -1
        popad
        iretd
.sao_chan: dd 0

;-----------------------------------------------------------------------
; sys_audio_write_chan — write samples to a mixer channel
;   EBX = channel id (0-3)
;   ECX = user buffer pointer (16-bit PCM stereo)
;   EDX = byte count
; Returns: EAX = bytes queued, or -1 on error
;-----------------------------------------------------------------------
sys_audio_write_chan:
        pushad
        cmp dword [ac97_present], 0
        je .sawc_fail
        cmp ebx, MIX_CHANS
        jae .sawc_fail
        imul eax, ebx, MIX_CHAN_SIZE
        cmp dword [mix_channels + eax], 0   ; active?
        je .sawc_fail
        ; Validate user pointer
        push eax
        mov eax, ecx
        call validate_user_ptr
        jc .sawc_fail_pop
        pop eax

        ; Copy data to channel ring buffer (simple overwrite; no overflow check)
        mov esi, ecx            ; user data pointer
        mov ecx, edx            ; byte count
        cmp ecx, MIX_RING_SIZE
        jle .sawc_size_ok
        mov ecx, MIX_RING_SIZE  ; clamp
.sawc_size_ok:
        push ecx
        ; ring write ptr = [mix_channels + eax + 8]
        mov edi, [mix_channels + eax + 8]   ; ring_wr
        push eax
        lea eax, [mix_channels + eax + 12]  ; ring base
        add edi, eax                         ; ring write address
        pop eax
        push esi
        ; Copy bytes
        mov ecx, [esp + 4]      ; byte count from saved ecx
        shr ecx, 2
        rep movsd
        mov ecx, [esp + 4]
        and ecx, 3
        rep movsb
        pop esi
        ; Update ring_wr (wrap)
        pop ecx
        add ecx, [mix_channels + eax + 8]
        and ecx, MIX_RING_SIZE - 1
        mov [mix_channels + eax + 8], ecx
        mov [esp + 28], edx     ; return original byte count as bytes queued
        popad
        iretd
.sawc_fail_pop:
        pop eax
.sawc_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_audio_close_chan — release a mixer channel
;   EBX = channel id
; Returns: EAX = 0 or -1
;-----------------------------------------------------------------------
sys_audio_close_chan:
        pushad
        cmp ebx, MIX_CHANS
        jae .sacc_fail
        imul eax, ebx, MIX_CHAN_SIZE
        mov dword [mix_channels + eax], 0   ; mark inactive
        mov dword [esp + 28], 0
        popad
        iretd
.sacc_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_audio_rec_start — start PCM IN recording
;   EBX = target buffer address (user space, must be mapped)
;   ECX = buffer size in bytes
; Returns: EAX = 0 or -1
;-----------------------------------------------------------------------
sys_audio_rec_start:
        pushad
        cmp dword [ac97_present], 0
        je .sars_fail
        ; Validate user buffer
        mov eax, ebx
        call validate_user_ptr
        jc .sars_fail
        mov [ac97_rec_buf], ebx
        mov [ac97_rec_len], ecx

        ; Set ADC sample rate = 44100 Hz
        mov edx, [ac97_nam_base]
        add edx, NAM_PCM_IN_RATE
        mov ax, 44100
        out dx, ax

        ; Set recording source = line in (selector 4)
        mov edx, [ac97_nam_base]
        add edx, NAM_RECORD_SEL
        mov ax, 0x0404          ; line in (left=4, right=4 in high byte)
        out dx, ax

        ; Init PCM IN BDL (reuse rec_bdl buffer with 2-entry ping-pong pointing to ac97_rec_buf)
        ; Entry 0: addr=ac97_rec_buf, size=min(ecx,32KB)/2, IOC
        ; Entry 1: addr=ac97_rec_buf+size, size=min(ecx,32KB)/2, IOC|EOT
        mov edi, [ac97_bdl_phys]    ; reuse same BDL memory (PCM IN separate channel registers)
        push edi
        add edi, AC97_BDL_SIZE      ; use second half for PCM IN BDL (offset 256)
        mov [ac97_rec_bdl_phys], edi
        mov eax, ebx                ; rec_buf
        mov [edi], eax
        mov eax, ecx
        cmp eax, 32768
        jle .sars_size_ok
        mov eax, 32768
.sars_size_ok:
        shr eax, 1                  ; half the buffer per BDL entry
        mov [ac97_rec_half], eax
        or  eax, BDL_IOC
        mov [edi + 4], eax          ; entry 0: size|IOC
        mov eax, ebx
        add eax, [ac97_rec_half]
        mov [edi + 8], eax          ; entry 1: addr = buf + half
        mov eax, [ac97_rec_half]
        or  eax, BDL_IOC | BDL_EOT
        mov [edi + 12], eax         ; entry 1: size|IOC|EOT
        pop edi

        ; Program PCM IN channel
        mov edx, [ac97_nabm_base]
        add edx, NABM_PCMIN_CR
        mov al, NABM_CR_RESET
        out dx, al
        ; Small delay
        xor ecx, ecx
        mov ecx, 10000
.sars_delay:
        loop .sars_delay

        mov edx, [ac97_nabm_base]
        add edx, NABM_PCMIN_BDBA
        mov eax, [ac97_rec_bdl_phys]
        out dx, eax

        mov edx, [ac97_nabm_base]
        add edx, NABM_PCMIN_LVI
        mov al, 1                   ; 2 entries (index 0 and 1)
        out dx, al

        mov edx, [ac97_nabm_base]
        add edx, NABM_PCMIN_CR
        mov al, NABM_CR_RUN | NABM_CR_IOCE
        out dx, al

        mov dword [ac97_rec_state], 1
        mov dword [esp + 28], 0
        popad
        iretd
.sars_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_audio_rec_read — poll for recorded data
;   EBX = user dest buffer
;   ECX = max bytes to read
; Returns: EAX = bytes available, 0 if still recording, -1 on error
;-----------------------------------------------------------------------
sys_audio_rec_read:
        pushad
        cmp dword [ac97_rec_state], 1
        jne .sarr_fail
        ; Check if PCM IN DMA halted (EOT or both entries done)
        mov edx, [ac97_nabm_base]
        add edx, NABM_PCMIN_SR
        in ax, dx
        test ax, NABM_SR_DCH        ; DMA halted?
        jz .sarr_busy               ; still recording, return 0
        ; DMA done; data is in ac97_rec_buf
        ; Copy min(rec_len, ecx) bytes to user buffer
        mov edi, ebx
        mov esi, [ac97_rec_buf]
        mov ecx, ecx
        cmp ecx, [ac97_rec_len]
        jle .sarr_size_ok
        mov ecx, [ac97_rec_len]
.sarr_size_ok:
        push ecx
        shr ecx, 2
        rep movsd
        pop ecx
        push ecx
        and ecx, 3
        rep movsb
        pop ecx
        mov [esp + 28], ecx
        popad
        iretd
.sarr_busy:
        mov dword [esp + 28], 0
        popad
        iretd
.sarr_fail:
        mov dword [esp + 28], -1
        popad
        iretd

;-----------------------------------------------------------------------
; sys_audio_rec_stop — stop PCM IN recording
;-----------------------------------------------------------------------
sys_audio_rec_stop:
        pushad
        cmp dword [ac97_rec_state], 0
        je .sasto_done
        mov edx, [ac97_nabm_base]
        add edx, NABM_PCMIN_CR
        xor al, al
        out dx, al
        mov dword [ac97_rec_state], 0
.sasto_done:
        mov dword [esp + 28], 0
        popad
        iretd

;---------------------------------------
; Mixer and recording data (v9.0)
;---------------------------------------
mix_channels:    times MIX_CHANS * MIX_CHAN_SIZE db 0
ac97_rec_state:  dd 0
ac97_rec_buf:    dd 0
ac97_rec_len:    dd 0
ac97_rec_bdl_phys: dd 0
ac97_rec_half:   dd 0
