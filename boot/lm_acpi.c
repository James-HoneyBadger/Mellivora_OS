/*
 * lm_acpi.c - finding the machine's own description of itself
 *
 * This is the first step of P3 and it is discovery only: the firmware publishes
 * a set of tables that say how many CPUs there are, where the local and I/O
 * APICs live, which interrupts they carry and which ISA IRQ goes where. Nothing
 * here programs anything - it finds the tables, checks their checksums and reads
 * the interrupt-controller description out of the MADT, which is what the next
 * step (an APIC timer, then more than one CPU) needs to exist before it can
 * start.
 *
 * The tables are reached through the direct map, so an address above the 1 GB it
 * covers cannot be read yet: that is checked rather than assumed, and a machine
 * that puts its tables out of reach is reported as unreadable instead of being
 * followed off the end of a mapping.
 */

#include "lm.h"

#define RSDP_SIGNATURE "RSD PTR "
#define RSDT_SIGNATURE "RSDT"
#define XSDT_SIGNATURE "XSDT"
#define MADT_SIGNATURE "APIC"

#define ACPI_MADT_LAPIC         0x00    /* processor local APIC */
#define ACPI_MADT_IOAPIC        0x01
#define ACPI_MADT_ISO           0x02    /* interrupt source override */
#define ACPI_MADT_LAPIC_NMI     0x04
#define ACPI_MADT_LAPIC_ADDR    0x05    /* ... over 64 bits */

struct acpi_info {
    u8 have_tables;
    u32 rsdp;
    u32 table_count;
    u64 lapic_address;
    u32 ioapic_address;
    u32 ioapic_gsi;
    u32 cpu_count;          /* local APICs that say they are enabled */
    u32 iso_count;
};

static struct acpi_info info;

static const u8 *at(u64 phys)
{
    return vmm_phys(phys);
}

static int signature_is(const u8 *base, const char *want)
{
    for (int i = 0; i < 4; i++) {
        if (base[i] != (u8)want[i]) {
            return 0;
        }
    }
    return 1;
}

/* The sum of every byte of the table, header included, is zero. */
static int checksum_ok(const u8 *base, u32 length)
{
    u8 sum = 0;
    for (u32 i = 0; i < length; i++) {
        sum = (u8)(sum + base[i]);
    }
    return sum == 0;
}

/* The RSDP sits on a 16-byte boundary either in the first kilobyte of the EBDA
 * or in the BIOS ROM area, and the only way to find it is to look. */
static u32 find_rsdp(void)
{
    static const u32 ranges[2][2] = {
        {0x00080000u, 0x000A0000u},     /* extended BIOS data area */
        {0x000E0000u, 0x00100000u},     /* the ROM area above it */
    };

    for (int r = 0; r < 2; r++) {
        for (u32 address = ranges[r][0]; address < ranges[r][1]; address += 16) {
            const u8 *candidate = at(address);
            if (candidate == NULL) {
                continue;
            }
            if (signature_is(candidate, RSDP_SIGNATURE) &&
                checksum_ok(candidate, 20)) {
                return address;
            }
        }
    }
    return 0;
}

/* Walk one table's entries looking for the MADT. */
static void scan_table(u64 address)
{
    const u8 *table = at(address);
    if (table == NULL || !signature_is(table, "APIC")) {
        return;
    }

    u32 length = *(const u32 *)(table + 4);
    if (length < 44 || length > 0x10000) {
        return;
    }

    info.lapic_address = (u64)*(const u32 *)(table + 36);
    u32 entries_end = length;
    u32 offset = 44;

    while (offset + 2 <= entries_end) {
        u8 type = table[offset];
        u8 entry_length = table[offset + 1];
        if (entry_length < 2 || offset + entry_length > entries_end) {
            break;
        }

        switch (type) {
        case ACPI_MADT_LAPIC: {
            /* The second byte is the ACPI processor id, the third the APIC id,
             * and the flags say whether this one is actually usable. */
            u32 flags = *(const u32 *)(table + offset + 4);
            if ((flags & 0x01) != 0) {
                info.cpu_count++;
            }
            break;
        }
        case ACPI_MADT_IOAPIC:
            if (info.ioapic_address == 0) {
                info.ioapic_address = *(const u32 *)(table + offset + 4);
                info.ioapic_gsi = *(const u32 *)(table + offset + 8);
            }
            break;
        case ACPI_MADT_ISO:
            info.iso_count++;
            break;
        case ACPI_MADT_LAPIC_ADDR:
            /* A 64-bit local APIC address overrides the 32-bit one. */
            info.lapic_address = *(const u64 *)(table + offset + 4);
            break;
        default:
            break;
        }
        offset += entry_length;
    }
    info.have_tables = 1;
}

int acpi_init(void)
{
    info.rsdp = find_rsdp();
    if (info.rsdp == 0) {
        kputs("[acpi] no RSDP acpi=none\r\n");
        return -1;
    }

    const u8 *rsdp = at(info.rsdp);
    u8 revision = rsdp[15];
    u64 root = 0;
    int is_xsdt = 0;

    if (revision >= 2 && checksum_ok(rsdp, 36)) {
        root = *(const u64 *)(rsdp + 24);
        is_xsdt = 1;
    }
    if (root == 0) {
        root = *(const u32 *)(rsdp + 16);
        is_xsdt = 0;
    }
    if (root == 0) {
        kputs("[acpi] the RSDP names no root table acpi=none\r\n");
        return -1;
    }

    const u8 *sdt = at(root);
    if (sdt == NULL ||
        !signature_is(sdt, is_xsdt ? XSDT_SIGNATURE : RSDT_SIGNATURE)) {
        kputs("[acpi] the root table is out of the direct map acpi=none\r\n");
        return -1;
    }

    u32 length = *(const u32 *)(sdt + 4);
    if (length > 0x10000) {
        kputs("[acpi] the root table is nonsense acpi=none\r\n");
        return -1;
    }
    info.table_count = (length - 36) / (is_xsdt ? 8 : 4);

    for (u32 i = 0; i < info.table_count; i++) {
        u64 entry;
        if (is_xsdt) {
            entry = *(const u64 *)(sdt + 36 + i * 8);
        } else {
            entry = (u64)*(const u32 *)(sdt + 36 + i * 4);
        }
        scan_table(entry);
    }

    kputs("[acpi] rsdp=");
    kput_hex(info.rsdp, 8);
    kputs(is_xsdt ? " xsdt=" : " rsdt=");
    kput_dec(info.table_count);
    if (!info.have_tables) {
        kputs(" no MADT acpi=none\r\n");
        return -1;
    }

    kputs(" lapic=");
    kput_hex(info.lapic_address, 8);
    kputs(" cpus=");
    kput_dec(info.cpu_count);
    kputs(" ioapic=");
    kput_hex(info.ioapic_address, 8);
    kputs(" acpi=ok\r\n");
    return 0;
}

u64 acpi_lapic_address(void)
{
    return info.lapic_address;
}

u32 acpi_cpu_count(void)
{
    return info.cpu_count;
}

u32 acpi_ioapic_address(void)
{
    return info.ioapic_address;
}
