/*
 * lm_e1000.c - the Intel 8254x network adapter
 *
 * The one device on a PC that a kernel cannot fake: the rings are in physical
 * memory, the descriptors carry physical addresses, and the adapter reads and
 * writes them without the CPU's help. So this driver is the first place the port
 * has to hand real physical addresses to hardware, and the first place bus
 * mastering matters.
 *
 * Two things about it are worth knowing before reading the registers:
 *
 *   - The register block is memory-mapped at the address in BAR0, which on a PC
 *     sits somewhere around 0xFEB00000 - far outside the 1 GB the direct map
 *     covers, and not RAM at all. So a window in the kernel's own half of the
 *     address space is mapped onto it at boot, with caching disabled: a cached
 *     read of a device register would answer from the cache and the driver would
 *     never see the device move. Every task's page tables share that half, so the
 *     window is made once and seen everywhere.
 *   - Nothing here uses interrupts. Like the disk, the adapter is polled: the
 *     receive tail is a count of descriptors already given back and the transmit
 *     side waits for the descriptor's done bit. A 100 Hz tick is not a network
 *     latency budget, and a polled driver needs no handler, no mask and nothing
 *     to wake.
 */

#include "lm.h"

/* A window of its own in the kernel half: PML4[511]/PDPT[509], a gigabyte no
 * other part of the port uses. */
#define E1000_MMIO_BASE  0xFFFFFFFF40000000ULL
#define E1000_MMIO_PAGES 32                    /* 128 KB: the whole register file */

#define REG_CTRL   0x0000
#define REG_STATUS 0x0008
#define REG_EECD   0x0010
#define REG_EERD   0x0014
#define REG_ICR    0x00C0
#define REG_IMS    0x00D0
#define REG_IMC    0x00D8
#define REG_RCTL   0x0100
#define REG_RDBAL  0x2800
#define REG_RDBAH  0x2804
#define REG_RDLEN  0x2808
#define REG_RDH    0x2810
#define REG_RDT    0x2818
#define REG_TCTL   0x0400
#define REG_TIPG   0x0410
#define REG_TDBAL  0x3800
#define REG_TDBAH  0x3804
#define REG_TDLEN  0x3808
#define REG_TDH    0x3810
#define REG_TDT    0x3818
#define REG_RAL    0x5400
#define REG_RAH    0x5404

#define CTRL_RST   (1u << 26)          /* self-clearing: the device is resetting */
#define CTRL_SLU   (1u << 6)           /* set link up */
#define CTRL_ASDE  (1u << 5)           /* auto-speed detection */

#define STATUS_LU  (1u << 1)           /* link up */

#define RCTL_EN    (1u << 1)
#define RCTL_UPE   (1u << 3)           /* unicast promiscuous */
#define RCTL_MPE   (1u << 4)           /* multicast promiscuous */
#define RCTL_BAM   (1u << 15)          /* broadcast accept */
#define RCTL_BSIZE_2048 0u             /* the 82540's only 2 KB encoding */
#define RCTL_SECRC (1u << 26)          /* strip the CRC */

#define TCTL_EN    (1u << 1)
#define TCTL_PSP   (1u << 3)           /* pad short frames */
#define TCTL_CT    0x10u               /* collision threshold, in the datasheet */
#define TCTL_COLD  0x40u               /* collision distance */

#define DESC_DD    0x01                /* descriptor done */
#define TX_CMD_EOP 0x01
#define TX_CMD_IFCS 0x02               /* insert the frame check sequence */
#define TX_CMD_RS  0x08                /* report status back */

#define RX_COUNT 32
#define TX_COUNT 8
#define RX_BUFFER_SIZE 2048

struct rx_desc {
    u64 buffer;             /* physical address of the packet buffer */
    u16 length;
    u16 checksum;
    u8 status;
    u8 errors;
    u16 special;
} __attribute__((packed));

struct tx_desc {
    u64 buffer;
    u16 length;
    u8 checksum_offset;
    u8 command;
    u8 status;
    u8 checksum_start;
    u16 special;
} __attribute__((packed));

/* The descriptors are written by the device, so they are read through volatile
 * pointers: without that the compiler is entitled to hoist the status load out
 * of a polling loop and wait for a bit that it already knows is clear. */
static volatile u8 *regs;
static volatile struct rx_desc *rx_ring;
static volatile struct tx_desc *tx_ring;
static u64 rx_buffer[RX_COUNT];
static u64 tx_buffer[TX_COUNT];
static int rx_next;
static int tx_next;
static int present;
static u8 mac[6];
static u32 bar0;

static u32 reg_read(u32 offset)
{
    return *(volatile u32 *)(regs + offset);
}

static void reg_write(u32 offset, u32 value)
{
    *(volatile u32 *)(regs + offset) = value;
}

/* A device register write is not complete when the store retires; the read-back
 * is the only thing that makes the CPU wait for it. */
static void reg_flush(void)
{
    (void)reg_read(REG_STATUS);
}

static void pause_loops(u32 count)
{
    for (u32 i = 0; i < count; i++) {
        __asm__ volatile("pause" ::: "memory");
    }
}

static const u16 known_ids[] = {
    0x100E,     /* 82540EM, what QEMU gives you */
    0x100F,     /* 82545EM */
    0x1004,     /* 82543GC */
    0x10D3,     /* 82574L */
};

static int id_known(u16 id)
{
    for (u32 i = 0; i < sizeof(known_ids) / sizeof(known_ids[0]); i++) {
        if (known_ids[i] == id) {
            return 1;
        }
    }
    return 0;
}

/* Read one 16-bit word out of the serial EEPROM. The controller does the shift
 * register work; START clears itself when the word has arrived. */
static int eeprom_read(u8 address, u16 *out)
{
    reg_write(REG_EERD, ((u32)address << 8) | 1u);
    for (int spin = 0; spin < 100000; spin++) {
        u32 value = reg_read(REG_EERD);
        if ((value & 0x10) != 0) {          /* DONE */
            *out = (u16)(value >> 16);
            return 0;
        }
    }
    return -1;
}

static void read_mac(void)
{
    u32 low = reg_read(REG_RAL);
    u32 high = reg_read(REG_RAH);
    if ((low != 0 && low != 0xFFFFFFFFu) || (high & 0xFFFF) != 0) {
        for (int i = 0; i < 4; i++) {
            mac[i] = (u8)(low >> (8 * i));
        }
        mac[4] = (u8)(high >> 0);
        mac[5] = (u8)(high >> 8);
        return;
    }

    /* The auto-read did not run (a reset can leave it to the driver). */
    u16 words[3] = {0, 0, 0};
    for (u8 i = 0; i < 3; i++) {
        (void)eeprom_read(i, &words[i]);
    }
    for (int i = 0; i < 3; i++) {
        mac[i * 2] = (u8)(words[i] & 0xFF);
        mac[i * 2 + 1] = (u8)(words[i] >> 8);
    }
    reg_write(REG_RAL, (u32)mac[0] | ((u32)mac[1] << 8)
                        | ((u32)mac[2] << 16) | ((u32)mac[3] << 24));
    reg_write(REG_RAH, (u32)mac[4] | ((u32)mac[5] << 8) | (1u << 31));
}

/* One frame in, one frame out. RX buffers are a frame each and the rings are one
 * frame each, so nothing here needs a contiguous megabyte from the allocator. */
static int alloc_rings(void)
{
    u64 frame = pmm_alloc_frame();
    if (frame == 0) {
        return -1;
    }
    rx_ring = (volatile struct rx_desc *)vmm_phys(frame);
    reg_write(REG_RDBAL, (u32)(frame & 0xFFFFFFFF));
    reg_write(REG_RDBAH, (u32)(frame >> 32));
    reg_write(REG_RDLEN, sizeof(struct rx_desc) * RX_COUNT);
    reg_write(REG_RDH, 0);
    reg_write(REG_RDT, RX_COUNT - 1);       /* every descriptor is the driver's */

    for (int i = 0; i < RX_COUNT; i++) {
        u64 buffer = pmm_alloc_frame();
        if (buffer == 0) {
            return -1;
        }
        rx_buffer[i] = buffer;
        rx_ring[i].buffer = buffer;
        rx_ring[i].status = 0;
    }

    frame = pmm_alloc_frame();
    if (frame == 0) {
        return -1;
    }
    tx_ring = (volatile struct tx_desc *)vmm_phys(frame);
    reg_write(REG_TDBAL, (u32)(frame & 0xFFFFFFFF));
    reg_write(REG_TDBAH, (u32)(frame >> 32));
    reg_write(REG_TDLEN, sizeof(struct tx_desc) * TX_COUNT);
    reg_write(REG_TDH, 0);
    reg_write(REG_TDT, 0);

    for (int i = 0; i < TX_COUNT; i++) {
        u64 buffer = pmm_alloc_frame();
        if (buffer == 0) {
            return -1;
        }
        tx_buffer[i] = buffer;
        tx_ring[i].buffer = buffer;
        tx_ring[i].status = DESC_DD;        /* nothing outstanding */
    }
    return 0;
}

int e1000_init(void)
{
    const struct pci_device *nic = pci_find(0x02, 0x00);
    if (nic == NULL) {
        return -1;
    }
    if (nic->vendor != 0x8086 || !id_known(nic->id)) {
        kputs("[e1000] not an 8254x, left alone\r\n");
        return -1;
    }

    /* Memory space and bus mastering. Without the second bit the rings are never
     * read and every transmit descriptor stays unowned forever, which looks
     * exactly like a dead card. */
    u32 command = pci_config_read32(nic->bus, nic->device, nic->function, 0x04);
    pci_config_write32(nic->bus, nic->device, nic->function, 0x04,
                       command | 0x02 | 0x04);

    u32 raw = pci_config_read32(nic->bus, nic->device, nic->function, 0x10);
    if ((raw & 0x01) != 0) {
        kputs("[e1000] BAR0 is I/O space, not supported\r\n");
        return -1;
    }
    bar0 = raw & 0xFFFFFFF0u;

    for (u32 i = 0; i < E1000_MMIO_PAGES; i++) {
        if (vmm_map(E1000_MMIO_BASE + (u64)i * FRAME_SIZE,
                    (u64)(bar0 & ~0xFFFu) + (u64)i * FRAME_SIZE,
                    VMM_W | VMM_PCD | VMM_NX) != 0) {
            kputs("[e1000] FAIL the register window would not map\r\n");
            return -1;
        }
    }
    regs = (volatile u8 *)(unsigned long)E1000_MMIO_BASE;

    /* Reset, and wait for the device to say it is done rather than guessing how
     * long it takes. */
    reg_write(REG_CTRL, reg_read(REG_CTRL) | CTRL_RST);
    reg_flush();
    int came_back = 0;
    for (int spin = 0; spin < 100000; spin++) {
        if ((reg_read(REG_CTRL) & CTRL_RST) == 0) {
            came_back = 1;
            break;
        }
        pause_loops(10);
    }
    if (!came_back) {
        kputs("[e1000] FAIL the device never came out of reset\r\n");
        return -1;
    }
    pause_loops(1000);

    /* Polled, so no interrupts: mask them all and clear whatever the reset left. */
    reg_write(REG_IMC, 0xFFFFFFFF);
    (void)reg_read(REG_ICR);

    reg_write(REG_CTRL, reg_read(REG_CTRL) | CTRL_SLU | CTRL_ASDE);
    read_mac();

    if (alloc_rings() != 0) {
        kputs("[e1000] FAIL out of frames for the rings\r\n");
        return -1;
    }

    reg_write(REG_RCTL, RCTL_EN | RCTL_UPE | RCTL_MPE | RCTL_BAM | RCTL_SECRC
                        | RCTL_BSIZE_2048);
    reg_write(REG_TCTL, TCTL_EN | TCTL_PSP | (TCTL_CT << 4) | (TCTL_COLD << 12));
    reg_write(REG_TIPG, 10 | (8 << 10) | (6 << 20));
    reg_flush();

    rx_next = 0;
    tx_next = 0;
    present = 1;
    return 0;
}

/* Copy a frame in and hand the descriptor to the device. Returns once the
 * descriptor comes back with the done bit set, which is the only proof that the
 * frame actually left. */
int e1000_send(const void *frame, u32 length)
{
    if (!present || length == 0 || length > RX_BUFFER_SIZE) {
        return -1;
    }

    int slot = tx_next;
    if ((tx_ring[slot].status & DESC_DD) == 0) {
        return -1;                          /* still holding the last frame */
    }

    u8 *target = vmm_phys(tx_buffer[slot]);
    if (target == NULL) {
        return -1;
    }
    memcpy(target, frame, length);
    tx_ring[slot].length = (u16)length;
    tx_ring[slot].command = TX_CMD_EOP | TX_CMD_IFCS | TX_CMD_RS;
    tx_ring[slot].status = 0;

    tx_next = (slot + 1) % TX_COUNT;
    reg_write(REG_TDT, (u32)tx_next);
    reg_flush();

    for (int spin = 0; spin < 1000000; spin++) {
        if ((tx_ring[slot].status & DESC_DD) != 0) {
            return 0;
        }
    }
    return -1;
}

/* 1 and the length when a frame arrived, 0 when none has, -1 if the driver is
 * not there. Never blocks: a caller that wants to wait asks again. */
int e1000_recv(void *buffer, u32 *length)
{
    if (!present) {
        return -1;
    }

    int slot = rx_next;
    if ((rx_ring[slot].status & DESC_DD) == 0) {
        return 0;
    }

    u32 len = rx_ring[slot].length;
    if (len > RX_BUFFER_SIZE) {
        len = RX_BUFFER_SIZE;
    }
    const u8 *source = vmm_phys(rx_buffer[slot]);
    if (source != NULL && buffer != NULL) {
        memcpy(buffer, source, len);
    }

    rx_ring[slot].status = 0;               /* the device may have it again */
    rx_next = (slot + 1) % RX_COUNT;
    reg_write(REG_RDT, (u32)slot);
    reg_flush();

    if (length != NULL) {
        *length = len;
    }
    return 1;
}

const u8 *e1000_mac(void)
{
    return mac;
}

int e1000_present(void)
{
    return present;
}

/*---------------------------------------
 * The boot self-test: a real ARP request, answered by the user-mode network
 * QEMU puts behind the card. It proves the whole path in one frame - the PCI
 * handover, the mapped window, the descriptor rings, bus mastering, the MAC the
 * device reports and the receive side - without needing a protocol stack or an
 * IP address, because ARP is what a machine does before it has either.
 *---------------------------------------*/

#define ARP_WHO_HAS 0x0001
#define ARP_IS_AT   0x0002

static const u8 guest_ip[4] = {10, 0, 2, 15};    /* what the user-mode net hands out */
static const u8 gateway_ip[4] = {10, 0, 2, 2};

static void put_be16(u8 *at, u16 value)
{
    at[0] = (u8)(value >> 8);
    at[1] = (u8)value;
}

static u16 get_be16(const u8 *at)
{
    return (u16)(((u16)at[0] << 8) | at[1]);
}

/* An Ethernet frame: 14 bytes of header, then ARP - htype, ptype, hlen, plen and
 * the operation, then the sender's hardware address, the sender's protocol
 * address, the target's hardware address and the target's protocol address. The
 * target's hardware address is what the request is asking for, so it is zero;
 * everything after it is 4 bytes of address, not 6. */
#define ETH_DST       0
#define ETH_SRC       6
#define ETH_TYPE      12
#define ARP_OP        20
#define ARP_SENDER_MAC 22
#define ARP_SENDER_IP 28
#define ARP_TARGET_MAC 32
#define ARP_TARGET_IP 38
#define ARP_FRAME_BYTES 42

static void build_arp_request(u8 *frame)
{
    memset(frame, 0, ARP_FRAME_BYTES);
    memset(frame + ETH_DST, 0xFF, 6);       /* broadcast */
    for (int i = 0; i < 6; i++) {
        frame[ETH_SRC + i] = mac[i];
    }
    put_be16(frame + ETH_TYPE, 0x0806);     /* ARP */

    put_be16(frame + 14, 0x0001);           /* Ethernet */
    put_be16(frame + 16, 0x0800);           /* IPv4 */
    frame[18] = 6;                          /* hardware address length */
    frame[19] = 4;                          /* protocol address length */
    put_be16(frame + ARP_OP, ARP_WHO_HAS);
    for (int i = 0; i < 6; i++) {
        frame[ARP_SENDER_MAC + i] = mac[i];
    }
    for (int i = 0; i < 4; i++) {
        frame[ARP_SENDER_IP + i] = guest_ip[i];
        frame[ARP_TARGET_IP + i] = gateway_ip[i];
    }
}

int e1000_selftest(void)
{
    kputs("[e1000] ");
    if (!present) {
        kputs("no 8254x adapter e1000=none\r\n");
        return 0;
    }

    for (int i = 0; i < 6; i++) {
        kput_hex(mac[i], 2);
        if (i < 5) {
            kputc(':');
        }
    }
    kputs(" bar=");
    kput_hex(bar0, 8);
    kputs((reg_read(REG_STATUS) & STATUS_LU) != 0 ? " link=up" : " link=down");

    static u8 frame[64];
    static u8 reply[RX_BUFFER_SIZE];
    build_arp_request(frame);
    if (e1000_send(frame, ARP_FRAME_BYTES) != 0) {
        /* The ring state is the whole diagnosis here: a head that did not move
         * means the device never looked, a head that moved means the descriptor
         * came back and the wait was wrong. */
        kputs(" FAIL the transmit descriptor never came back tdh=");
        kput_hex(reg_read(REG_TDH), 4);
        kputs(" tdt=");
        kput_hex(reg_read(REG_TDT), 4);
        kputs(" desc=");
        kput_hex(tx_buffer[0], 8);
        kputs("\r\n");
        return -1;
    }

    /* QEMU's user-mode stack answers an ARP for its own address straight away;
     * a real switch answers in milliseconds. Half a second covers both, and it
     * is measured in ticks rather than in iterations, so a card that answers
     * nothing cannot hold the boot up. */
    u64 deadline = cpu_this()->ticks + 50;
    while (cpu_this()->ticks < deadline) {
        u32 length = 0;
        int got = e1000_recv(reply, &length);
        if (got < 0) {
            break;
        }
        if (got > 0 && length >= ARP_FRAME_BYTES &&
            get_be16(reply + ETH_TYPE) == 0x0806 &&
            get_be16(reply + ARP_OP) == ARP_IS_AT &&
            reply[ARP_SENDER_IP] == gateway_ip[0] &&
            reply[ARP_SENDER_IP + 3] == gateway_ip[3]) {
            kputs(" arp=");
            for (int i = 0; i < 6; i++) {
                kput_hex(reply[ARP_SENDER_MAC + i], 2);
                if (i < 5) {
                    kputc(':');
                }
            }
            kputs(" e1000=ok\r\n");
            return 0;
        }
        pause_loops(1000);
    }

    kputs(" FAIL no answer to the ARP request rctl=");
    kput_hex(reg_read(REG_RCTL), 8);
    kputs(" rdh=");
    kput_hex(reg_read(REG_RDH), 4);
    kputs(" rdt=");
    kput_hex(reg_read(REG_RDT), 4);
    kputs("\r\n");
    return -1;
}
