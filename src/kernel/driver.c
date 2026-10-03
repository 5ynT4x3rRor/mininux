#include "driver.h"
#include "usb.h"

#define ANY 0xffffU
#define PCI_COMMAND 0x04
#define PCI_MEMORY_AND_MASTER 0x6U

struct driver {
    const char *name;
    unsigned short vendor;
    unsigned short device;
    unsigned short class_sub;
    unsigned char (*init)(unsigned char bus, unsigned char slot, unsigned char function);
};

static struct driver_record records[DRV_MAX_RECORDS];
static unsigned int record_total;

/* Active l'acces memoire et le bus mastering, puis verifie la prise en compte. */
static unsigned char init_xhci(unsigned char bus, unsigned char slot, unsigned char function)
{
    unsigned int command = pci_read_config(bus, slot, function, PCI_COMMAND);

    pci_write_config(bus, slot, function, PCI_COMMAND,
                     (command & 0xffffU) | PCI_MEMORY_AND_MASTER);
    command = pci_read_config(bus, slot, function, PCI_COMMAND);
    return (command & PCI_MEMORY_AND_MASTER) == PCI_MEMORY_AND_MASTER ? 0 : 1;
}

/* Table de pilotes. Sans fonction d'init, le materiel est reconnu mais non pilote. */
static const struct driver drivers[] = {
    {"xhci",     ANY,    ANY,    0x0c03, init_xhci},
    {"ahci",     ANY,    ANY,    0x0106, 0},
    {"nvme",     ANY,    ANY,    0x0108, 0},
    {"bcm4360",  0x14e4, 0x43a0, ANY,    0},
    {"intel-gfx", 0x8086, ANY,   0x0300, 0},
    {"hda-audio", ANY,   ANY,    0x0403, 0},
    {"bcm-bt-usb", ANY,  ANY,    0x0d11, 0}
};

#define DRIVER_TABLE (sizeof(drivers) / sizeof(drivers[0]))

static void add_record(const char *name, unsigned short vendor, unsigned short device,
                       unsigned char bus, unsigned char slot, unsigned char function,
                       unsigned char class_code, unsigned char state)
{
    if (record_total < DRV_MAX_RECORDS) {
        struct driver_record *record = &records[record_total++];

        record->name = name;
        record->vendor = vendor;
        record->device = device;
        record->bus = bus;
        record->slot = slot;
        record->function = function;
        record->class_code = class_code;
        record->state = state;
    }
}

static void probe(unsigned char bus, unsigned char slot, unsigned char function)
{
    unsigned int id = pci_read_config(bus, slot, function, 0);
    unsigned int klass = pci_read_config(bus, slot, function, 8);
    unsigned short vendor = (unsigned short)(id & 0xffffU);
    unsigned short device = (unsigned short)(id >> 16);
    unsigned char class_code = (unsigned char)(klass >> 24);
    unsigned short class_sub = (unsigned short)(klass >> 16);

    /* Les ponts et le chipset n'ont pas besoin de pilote dedie. */
    if (class_code == 0x06) {
        return;
    }
    for (unsigned int index = 0; index < DRIVER_TABLE; index++) {
        const struct driver *driver = &drivers[index];

        if ((driver->vendor != ANY && driver->vendor != vendor) ||
            (driver->device != ANY && driver->device != device) ||
            (driver->class_sub != ANY && driver->class_sub != class_sub)) {
            continue;
        }
        if (driver->init == 0) {
            add_record(driver->name, vendor, device, bus, slot, function,
                       class_code, DRV_NO_DRIVER);
        } else {
            add_record(driver->name, vendor, device, bus, slot, function, class_code,
                       driver->init(bus, slot, function) == 0 ? DRV_LOADED : DRV_FAILED);
        }
        return;
    }
}

void drivers_load(unsigned char disk_ok, unsigned char keyboard_ok)
{
    record_total = 0;
    add_record("vga-text", 0, 0, 0, 0, 0, 0, DRV_LOADED);
    add_record("ps2-kbd", 0, 0, 0, 0, 0, 0, keyboard_ok ? DRV_LOADED : DRV_FAILED);
    add_record("bios-disk", 0, 0, 0, 0, 0, 0, disk_ok ? DRV_LOADED : DRV_FAILED);
    for (unsigned int bus = 0; bus < 256; bus++) {
        for (unsigned int slot = 0; slot < 32; slot++) {
            unsigned int id = pci_read_config((unsigned char)bus, (unsigned char)slot, 0, 0);
            unsigned int header = pci_read_config((unsigned char)bus, (unsigned char)slot, 0, 0x0c);
            unsigned int functions = (header & 0x00800000U) != 0 ? 8 : 1;

            if ((id & 0xffffU) == 0xffffU) {
                continue;
            }
            for (unsigned int function = 0; function < functions; function++) {
                id = pci_read_config((unsigned char)bus, (unsigned char)slot,
                                     (unsigned char)function, 0);
                if ((id & 0xffffU) != 0xffffU) {
                    probe((unsigned char)bus, (unsigned char)slot, (unsigned char)function);
                }
            }
        }
    }
}

unsigned int driver_count(void)
{
    return record_total;
}

const struct driver_record *driver_get(unsigned int index)
{
    return index < record_total ? &records[index] : 0;
}

unsigned int driver_count_state(unsigned char state)
{
    unsigned int count = 0;

    for (unsigned int index = 0; index < record_total; index++) {
        if (records[index].state == state) {
            count++;
        }
    }
    return count;
}
