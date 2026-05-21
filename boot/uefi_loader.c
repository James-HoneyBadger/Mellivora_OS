/*
 * Mellivora OS - UEFI Bootloader (boot/uefi_loader.c)
 *
 * PE32+ EFI application that:
 *   1. Locates and loads the kernel binary (mellivora.bin) from the ESP.
 *   2. Queries the UEFI GOP protocol for the best linear framebuffer mode.
 *   3. Retrieves the UEFI memory map (replaces E820 from BIOS stage2).
 *   4. Exits Boot Services and jumps to the kernel entry point at 0x100000.
 *
 * Build with gnu-efi (see Makefile target 'uefi'):
 *   x86_64-linux-gnu-gcc -ffreestanding -fno-stack-protector -fpic     \
 *     -fshort-wchar -mno-red-zone -target x86_64-unknown-windows        \
 *     -I/usr/include/efi -I/usr/include/efi/x86_64                     \
 *     -I/usr/include/efi/protocol -DEFI_FUNCTION_WRAPPER               \
 *     -c boot/uefi_loader.c -o .build/uefi/uefi_loader.o
 *
 * Boot-info layout at 0x500 (kernel reads same address regardless of boot path):
 *   0x500  UINT32  boot drive (0x80 = UEFI / first disk)
 *   0x504  UINT32  memory map entry count
 *   0x508  UINT32  memory map linear address (E820-compatible entries)
 *   0x510  UINT64  GOP framebuffer base address
 *   0x518  UINT32  GOP framebuffer width (pixels)
 *   0x51C  UINT32  GOP framebuffer height (pixels)
 *   0x520  UINT32  GOP pixels per scan line (pitch)
 *   0x524  UINT32  GOP pixel format  (0=RGBX, 1=BGRX, 2=bitmask, 3=BLT-only)
 *   0x528  UINT32  UEFI magic (0xEF100000 when UEFI-booted)
 *   0x52C  UINT32  RSDP address (ACPI 1/2 pointer from UEFI config table)
 */

#include <efi.h>
#include <efilib.h>

/* -------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */
#define KERNEL_LOAD_ADDR    0x00100000UL   /* 1 MB — same as BIOS/stage2 */
#define KERNEL_MAX_SIZE     (4UL * 1024 * 1024)  /* 4 MB ceiling */
#define KERNEL_FILENAME     L"mellivora.bin"

/* Boot-info area (4 KB below 0x1000 is safe; kernel does not use 0x500-0x600) */
#define BI_DRIVE        ((volatile UINT32 *)0x500)
#define BI_MMAP_CNT     ((volatile UINT32 *)0x504)
#define BI_MMAP_PTR     ((volatile UINT32 *)0x508)
#define BI_GOP_ADDR     ((volatile UINT64 *)0x510)
#define BI_GOP_W        ((volatile UINT32 *)0x518)
#define BI_GOP_H        ((volatile UINT32 *)0x51C)
#define BI_GOP_PITCH    ((volatile UINT32 *)0x520)
#define BI_GOP_FMT      ((volatile UINT32 *)0x524)
#define BI_UEFI_MAGIC   ((volatile UINT32 *)0x528)
#define BI_RSDP         ((volatile UINT32 *)0x52C)

#define UEFI_BOOT_MAGIC 0xEF100000U

/* Maximum E820-compatible entries we will write */
#define MMAP_MAX        512

/* EFI memory type -> E820 type conversion */
static inline UINT32 efi_to_e820(UINT32 t)
{
    switch (t) {
    case EfiConventionalMemory:
    case EfiBootServicesCode:
    case EfiBootServicesData:
    case EfiLoaderCode:
    case EfiLoaderData:     return 1;   /* usable */
    case EfiACPIReclaimMemory: return 3;
    case EfiACPIMemoryNVS:  return 4;
    default:                return 2;   /* reserved */
    }
}

/* E820-compatible entry (24 bytes, same as BIOS INT 15/E820 format) */
typedef struct {
    UINT64 base;
    UINT64 length;
    UINT32 type;
    UINT32 attribs;
} __attribute__((packed)) MemEntry;

/* Statically allocated memory-map buffer in the BSS segment.
 * Placed so the kernel can read it after ExitBootServices. */
static MemEntry g_mmap[MMAP_MAX];

/* Module-global copy of the ImageHandle (needed in build_mmap) */
static EFI_HANDLE g_image_handle;

/* -------------------------------------------------------------------------
 * locate_rsdp - Find ACPI RSDP from UEFI configuration tables
 * ---------------------------------------------------------------------- */
static UINT64 locate_rsdp(EFI_SYSTEM_TABLE *ST)
{
    static const EFI_GUID acpi20 = ACPI_20_TABLE_GUID;
    static const EFI_GUID acpi10 = ACPI_TABLE_GUID;
    UINTN i;
    UINT64 rsdp = 0;

    for (i = 0; i < ST->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE *t = &ST->ConfigurationTable[i];
        if (CompareGuid(&t->VendorGuid, (EFI_GUID *)&acpi20) == 0) {
            rsdp = (UINT64)(UINTN)t->VendorTable;
            break;  /* prefer ACPI 2.0 */
        }
        if (CompareGuid(&t->VendorGuid, (EFI_GUID *)&acpi10) == 0) {
            rsdp = (UINT64)(UINTN)t->VendorTable;
            /* keep looking for ACPI 2.0 */
        }
    }
    return rsdp;
}

/* -------------------------------------------------------------------------
 * setup_gop - Enumerate GOP modes, select best <= 1920x1080, record info.
 * ---------------------------------------------------------------------- */
static void setup_gop(void)
{
    EFI_STATUS status;
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    UINTN num_handles = 0;
    EFI_HANDLE *handles = NULL;
    UINT32 m, best_mode;
    UINT32 best_w = 0, best_h = 0;

    /* Enumerate all handles that support GOP */
    status = uefi_call_wrapper(BS->LocateHandleBuffer, 5,
        ByProtocol, &gop_guid, NULL, &num_handles, &handles);
    if (EFI_ERROR(status) || num_handles == 0) {
        Print(L"UEFI: No GOP handles found — BGA/VBE fallback active\r\n");
        return;
    }

    /* Prefer the handle associated with the current console output */
    status = uefi_call_wrapper(BS->HandleProtocol, 3,
        handles[0], &gop_guid, (VOID **)&gop);
    if (EFI_ERROR(status)) {
        Print(L"UEFI: GOP HandleProtocol failed\r\n");
        FreePool(handles);
        return;
    }
    FreePool(handles);

    /* Walk modes and find best resolution <= 1920x1080 */
    best_mode = gop->Mode->Mode;
    best_w    = gop->Mode->Info->HorizontalResolution;
    best_h    = gop->Mode->Info->VerticalResolution;

    for (m = 0; m < gop->Mode->MaxMode; m++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
        UINTN info_sz = 0;

        status = uefi_call_wrapper(gop->QueryMode, 4, gop, m, &info_sz, &info);
        if (EFI_ERROR(status) || info == NULL) continue;

        /* Skip BLT-only modes and modes wider than 1920 */
        if (info->PixelFormat == PixelBltOnly) continue;
        if (info->HorizontalResolution > 1920) continue;
        if (info->VerticalResolution   > 1080) continue;

        if (info->HorizontalResolution >= best_w &&
            info->VerticalResolution   >= best_h) {
            best_w    = info->HorizontalResolution;
            best_h    = info->VerticalResolution;
            best_mode = m;
        }
    }

    status = uefi_call_wrapper(gop->SetMode, 2, gop, best_mode);
    if (EFI_ERROR(status)) {
        Print(L"UEFI: GOP SetMode(%u) failed (0x%lx)\r\n", best_mode, status);
        /* Fall through — use whatever mode is currently active */
    }

    *BI_GOP_ADDR  = gop->Mode->FrameBufferBase;
    *BI_GOP_W     = gop->Mode->Info->HorizontalResolution;
    *BI_GOP_H     = gop->Mode->Info->VerticalResolution;
    *BI_GOP_PITCH = gop->Mode->Info->PixelsPerScanLine;
    *BI_GOP_FMT   = (UINT32)gop->Mode->Info->PixelFormat;

    Print(L"UEFI: GOP %ux%u @ base 0x%lx (fmt %u)\r\n",
        *BI_GOP_W, *BI_GOP_H, *BI_GOP_ADDR, *BI_GOP_FMT);
}

/* -------------------------------------------------------------------------
 * load_kernel - Load mellivora.bin from the ESP into physical 0x100000.
 *
 * Returns EFI_SUCCESS and sets *size_out on success, error code otherwise.
 * ---------------------------------------------------------------------- */
static EFI_STATUS load_kernel(EFI_FILE_HANDLE root, UINTN *size_out)
{
    EFI_STATUS status;
    EFI_FILE_HANDLE fh = NULL;
    EFI_FILE_INFO *fi  = NULL;
    UINTN fi_size, read_size;
    EFI_PHYSICAL_ADDRESS load_pa;
    UINTN pages;

    /* Open kernel file */
    status = uefi_call_wrapper(root->Open, 5,
        root, &fh, KERNEL_FILENAME, EFI_FILE_MODE_READ, 0ULL);
    if (EFI_ERROR(status)) {
        Print(L"UEFI: Cannot open '%s' (0x%lx)\r\n", KERNEL_FILENAME, status);
        return status;
    }

    /* Query file size */
    fi_size = sizeof(EFI_FILE_INFO) + 512;
    status  = uefi_call_wrapper(BS->AllocatePool, 3,
        EfiLoaderData, fi_size, (VOID **)&fi);
    if (EFI_ERROR(status)) goto out_close;

    status = uefi_call_wrapper(fh->GetInfo, 4,
        fh, &GenericFileInfo, &fi_size, fi);
    if (EFI_ERROR(status)) {
        Print(L"UEFI: GetInfo failed (0x%lx)\r\n", status);
        goto out_free;
    }

    if (fi->FileSize == 0 || fi->FileSize > KERNEL_MAX_SIZE) {
        Print(L"UEFI: Bad kernel size: %lu bytes\r\n", fi->FileSize);
        status = EFI_LOAD_ERROR;
        goto out_free;
    }

    pages   = (fi->FileSize + 0xFFFUL) / 0x1000UL;
    load_pa = KERNEL_LOAD_ADDR;

    /* Allocate pages at the exact 1 MB physical address */
    status = uefi_call_wrapper(BS->AllocatePages, 4,
        AllocateAddress, EfiLoaderCode, pages, &load_pa);
    if (EFI_ERROR(status)) {
        Print(L"UEFI: AllocatePages at 0x100000 failed (0x%lx)\r\n", status);
        goto out_free;
    }

    /* Read kernel binary into allocated pages */
    read_size = fi->FileSize;
    status = uefi_call_wrapper(fh->Read, 3, fh, &read_size,
        (VOID *)(UINTN)load_pa);
    if (EFI_ERROR(status) || read_size != fi->FileSize) {
        Print(L"UEFI: Read error: got %lu of %lu bytes\r\n",
            read_size, fi->FileSize);
        status = EFI_LOAD_ERROR;
        goto out_free;
    }

    *size_out = (UINTN)fi->FileSize;
    Print(L"UEFI: Kernel loaded: %lu bytes at 0x%x\r\n",
        fi->FileSize, KERNEL_LOAD_ADDR);

out_free:
    uefi_call_wrapper(BS->FreePool, 1, fi);
out_close:
    uefi_call_wrapper(fh->Close, 1, fh);
    return status;
}

/* -------------------------------------------------------------------------
 * build_mmap_and_exit - Convert EFI memory map to E820 entries, then exit
 * Boot Services.  After this call no UEFI Boot Services may be used.
 *
 * Returns the number of memory map entries written into g_mmap[].
 * ---------------------------------------------------------------------- */
static UINTN build_mmap_and_exit(void)
{
    EFI_STATUS status;
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINTN map_size = 0, map_key = 0, desc_size = 0;
    UINT32 desc_ver = 0;
    UINTN n = 0, retries = 0;
    EFI_MEMORY_DESCRIPTOR *desc;

    /* Step 1: Determine required buffer size */
    status = uefi_call_wrapper(BS->GetMemoryMap, 5,
        &map_size, map, &map_key, &desc_size, &desc_ver);
    /* Expected: EFI_BUFFER_TOO_SMALL; any other error is fatal */
    if (status != EFI_BUFFER_TOO_SMALL) return 0;

    /* Add slack for descriptors created by our upcoming AllocatePool */
    map_size += 8 * desc_size;

    status = uefi_call_wrapper(BS->AllocatePool, 3,
        EfiLoaderData, map_size, (VOID **)&map);
    if (EFI_ERROR(status) || map == NULL) return 0;

    /* Step 2: Get the actual memory map */
    status = uefi_call_wrapper(BS->GetMemoryMap, 5,
        &map_size, map, &map_key, &desc_size, &desc_ver);
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(BS->FreePool, 1, map);
        return 0;
    }

    /* Step 3: Convert to E820-compatible entries */
    desc = map;
    while ((UINT8 *)desc < (UINT8 *)map + map_size && n < MMAP_MAX) {
        g_mmap[n].base    = desc->PhysicalStart;
        g_mmap[n].length  = desc->NumberOfPages * 0x1000ULL;
        g_mmap[n].type    = efi_to_e820(desc->Type);
        g_mmap[n].attribs = 1;
        n++;
        desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)desc + desc_size);
    }

    /* Step 4: Exit Boot Services — must use the map_key from the last
     * GetMemoryMap call.  Retry once if the map changed. */
    status = uefi_call_wrapper(BS->ExitBootServices, 2, g_image_handle, map_key);
    while (EFI_ERROR(status) && retries++ < 3) {
        /* Refresh the map key — map_size and desc_size remain valid */
        uefi_call_wrapper(BS->GetMemoryMap, 5,
            &map_size, map, &map_key, &desc_size, &desc_ver);
        status = uefi_call_wrapper(BS->ExitBootServices, 2,
            g_image_handle, map_key);
    }

    /* Do NOT free 'map' — after ExitBootServices, AllocatePool is gone */
    return n;
}

/* -------------------------------------------------------------------------
 * efi_main - UEFI application entry point
 * ---------------------------------------------------------------------- */
EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL *li = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs = NULL;
    EFI_FILE_HANDLE root = NULL;
    UINTN kernel_size = 0;
    UINTN mmap_entries;
    typedef void (*kernel_fn)(void);
    kernel_fn kernel_entry;
    UINT64 rsdp;

    /* gnu-efi requires this before any Print() or library calls */
    InitializeLib(ImageHandle, SystemTable);
    g_image_handle = ImageHandle;

    Print(L"\r\n");
    Print(L"  +-------------------------------------------------+\r\n");
    Print(L"  |  Mellivora OS  -  UEFI Loader v10.0            |\r\n");
    Print(L"  +-------------------------------------------------+\r\n");
    Print(L"\r\n");

    /* Locate the filesystem on the volume we were loaded from */
    {
        EFI_GUID lip_guid  = EFI_LOADED_IMAGE_PROTOCOL_GUID;
        EFI_GUID sfsp_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

        status = uefi_call_wrapper(BS->HandleProtocol, 3,
            ImageHandle, &lip_guid, (VOID **)&li);
        if (EFI_ERROR(status)) {
            Print(L"UEFI: LoadedImage protocol unavailable (0x%lx)\r\n", status);
            return status;
        }

        status = uefi_call_wrapper(BS->HandleProtocol, 3,
            li->DeviceHandle, &sfsp_guid, (VOID **)&sfs);
        if (EFI_ERROR(status)) {
            Print(L"UEFI: SimpleFileSystem unavailable (0x%lx)\r\n", status);
            return status;
        }

        status = uefi_call_wrapper(sfs->OpenVolume, 2, sfs, &root);
        if (EFI_ERROR(status)) {
            Print(L"UEFI: OpenVolume failed (0x%lx)\r\n", status);
            return status;
        }
    }

    /* Configure framebuffer via GOP */
    Print(L"UEFI: Configuring display (GOP)...\r\n");
    setup_gop();

    /* Load kernel into physical memory at 0x100000 */
    Print(L"UEFI: Loading kernel...\r\n");
    status = load_kernel(root, &kernel_size);
    if (EFI_ERROR(status)) {
        Print(L"UEFI: Kernel load FAILED — halting.\r\n");
        for (;;) __asm__ volatile("hlt");
    }

    /* Locate ACPI RSDP before we exit boot services */
    rsdp = locate_rsdp(SystemTable);

    /* Write boot-info fields that are known before ExitBootServices */
    *BI_DRIVE      = 0x80;          /* "first hard disk" convention */
    *BI_UEFI_MAGIC = UEFI_BOOT_MAGIC;
    *BI_RSDP       = (UINT32)(rsdp & 0xFFFFFFFFUL);

    /* Build E820-compatible memory map and exit boot services.
     * After this call: no UEFI Boot Services, no Print(). */
    Print(L"UEFI: Exiting Boot Services and handing off to kernel...\r\n");
    mmap_entries = build_mmap_and_exit();

    /* Write memory-map info (boot services are now gone) */
    *BI_MMAP_CNT = (UINT32)mmap_entries;
    *BI_MMAP_PTR = (UINT32)(UINTN)g_mmap;

    /* Jump to kernel entry point */
    kernel_entry = (kernel_fn)(UINTN)KERNEL_LOAD_ADDR;
    kernel_entry();

    /* Should never return */
    for (;;) __asm__ volatile("hlt");
    return EFI_SUCCESS;
}
