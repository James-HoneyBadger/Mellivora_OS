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
