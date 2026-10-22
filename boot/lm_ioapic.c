/*
 * lm_ioapic.c - the I/O APIC
 *
 * The other end of the interrupt path: one chip that takes the machine's 24
 * interrupt lines and forwards each of them to a CPU's local APIC, on a vector
 * and with a mask of its own. That is what replaces the 8259 once there is more
 * than one CPU to send an interrupt to, and it is what lets an ISA line reach a
 * vector above the sixteen the 8259 can carry.
 *
 * The programming model is two registers: a selector at 0x00 and a window at
 * 0x10. Writing an index to the selector and then reading or writing the window
 * gets you any register - the identification at index 0, the version (and with
 * it the number of redirection entries) at 1, and one 64-bit redirection entry
 * per line from index 16.
 *
 * Everything starts masked, and stays masked until a driver asks for its line:
 * an unmasked entry pointing at a vector with no handler is a machine that dies
 * on its first interrupt rather than one that boots.
 */

#include "lm.h"

#define IOREGSEL 0x00
#define IOWIN    0x10

#define IOAPIC_ID     0x00
#define IOAPIC_VERSION 0x01
#define IOAPIC_REDTBL 0x10

/* A redirection entry's low half: vector in bits 0-7, delivery mode in 8-10
 * (0 = fixed), destination mode bit 11 (0 = physical), and the mask in bit 16. */
#define REDIR_MASKED  (1u << 16)

static volatile u32 *ioapic;
static u32 entry_count;
static u32 ioapic_id_value;
static u32 ioapic_version;

static u32 io_read(u32 index)
{
    ioapic[IOREGSEL / 4] = index;
    return ioapic[IOWIN / 4];
}

static void io_write(u32 index, u32 value)
{
    ioapic[IOREGSEL / 4] = index;
    ioapic[IOWIN / 4] = value;
}

int ioapic_init(void)
{
    u32 base = acpi_ioapic_address();
    if (base == 0) {
        base = 0xFEC00000u;             /* the architectural default */
    }

    if (vmm_map(LM_MMIO_PAGE(LM_MMIO_IOAPIC), base,
                VMM_W | VMM_PCD | VMM_NX) != 0) {
        kputs("[ioapic] FAIL the register page would not map\r\n");
        return -1;
    }
    ioapic = (volatile u32 *)(unsigned long)LM_MMIO_PAGE(LM_MMIO_IOAPIC);

    ioapic_id_value = io_read(IOAPIC_ID) >> 24;
    ioapic_version = io_read(IOAPIC_VERSION);
    entry_count = ((ioapic_version >> 16) & 0xFF) + 1;

    for (u32 line = 0; line < entry_count; line++) {
        io_write(IOAPIC_REDTBL + line * 2, REDIR_MASKED | (LM_IRQ_FIRST + 0));
        io_write(IOAPIC_REDTBL + line * 2 + 1, 0);
    }

    /* From here the 8259 is masked and everything that wants an interrupt goes
     * through this chip, so the acknowledge is the local APIC's. */
    irq_use_ioapic();

    kputs("[ioapic] id=");
    kput_dec(ioapic_id_value);
    kputs(" version=");
    kput_hex(ioapic_version & 0xFF, 2);
    kputs(" entries=");
    kput_dec(entry_count);
    kputs(" ioapic=ok\r\n");
    return 0;
}

/* Line `gsi` reaches `vector` on the CPU whose APIC id is `dest`. */
int ioapic_route(u32 gsi, u8 vector, u32 dest, int masked)
{
    if (ioapic == NULL || gsi >= entry_count) {
        return -1;
    }

    u32 low = (u32)vector | (masked ? REDIR_MASKED : 0);
    io_write(IOAPIC_REDTBL + gsi * 2, low);
    io_write(IOAPIC_REDTBL + gsi * 2 + 1, dest << 24);
    (void)io_read(IOAPIC_REDTBL + gsi * 2);
    return 0;
}

u32 ioapic_entries(void)
{
    return entry_count;
}
