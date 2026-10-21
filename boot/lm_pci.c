/*
 * lm_pci.c - PCI configuration-space enumeration for the 64-bit nucleus
 *
 * Roadmap P2, first driver. The 32-bit tree finds its devices the same way; this
 * is the enumeration half the block driver and, later, the network and display
 * drivers need: walk bus/device/function, read the vendor and class, and keep
 * the entries that matter.
 *
 * Configuration space is reached through the two legacy ports (0xCF8 address,
 * 0xCFC data) rather than ECAM, because firmware has already assigned bus 0 and
 * every machine that boots this nucleus through the BIOS path answers there.
 */

#include "lm.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_MAX_DEVICES 16

struct pci_device {
    u8 bus;
    u8 device;
    u8 function;
    u16 vendor;
    u16 id;
    u8 class_code;
    u8 subclass;
};

static struct pci_device devices[PCI_MAX_DEVICES];
static int device_count;

static void outl(u16 port, u32 value)
{
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static u32 inl(u16 port)
{
    u32 value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static u32 pci_read(u8 bus, u8 device, u8 function, u8 offset)
{
    u32 address = (1U << 31)
                | ((u32)bus << 16)
                | ((u32)device << 11)
                | ((u32)function << 8)
                | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_record(u8 bus, u8 device, u8 function)
{
    u32 reg0 = pci_read(bus, device, function, 0x00);
    u16 vendor = (u16)(reg0 & 0xFFFF);
    if (vendor == 0xFFFF || device_count == PCI_MAX_DEVICES) {
        return;
    }

    struct pci_device *d = &devices[device_count++];
    d->bus = bus;
    d->device = device;
    d->function = function;
    d->vendor = vendor;
    d->id = (u16)(reg0 >> 16);

    u32 class_reg = pci_read(bus, device, function, 0x08);
    d->class_code = (u8)(class_reg >> 24);
    d->subclass = (u8)(class_reg >> 16);
}

int pci_init(void)
{
    device_count = 0;

    for (u16 bus = 0; bus < 256; bus++) {
        for (u8 device = 0; device < 32; device++) {
            u32 reg0 = pci_read((u8)bus, device, 0, 0x00);
            if ((u16)(reg0 & 0xFFFF) == 0xFFFF) {
                continue;
            }
            pci_record((u8)bus, device, 0);

            /* Bit 7 of the header type says the device has more than one
             * function; only then is it worth probing the rest. */
            u32 header = pci_read((u8)bus, device, 0, 0x0C);
            if ((header & 0x00800000U) != 0) {
                for (u8 function = 1; function < 8; function++) {
                    pci_record((u8)bus, device, function);
                }
            }
        }
    }

    kputs("[pci] devices=");
    kput_dec((u64)device_count);
    kputs("\r\n");

    /* The block driver's controller, if the machine has one. */
    const struct pci_device *ide = pci_find(0x01, 0x01);
    if (ide != NULL) {
        kputs("[pci] ide=");
        kput_hex(ide->vendor, 4);
        kputs(":");
        kput_hex(ide->id, 4);
        kputs("\r\n");
    }
    return device_count;
}

/* The first device with this class/subclass, or NULL. Class 0x01/0x01 is the
 * legacy IDE controller, 0x01/0x06 SATA, 0x02/0x00 Ethernet, 0x03/0x00 VGA. */
const struct pci_device *pci_find(u8 class_code, u8 subclass)
{
    for (int i = 0; i < device_count; i++) {
        if (devices[i].class_code == class_code && devices[i].subclass == subclass) {
            return &devices[i];
        }
    }
    return NULL;
}

int pci_count(void)
{
    return device_count;
}
