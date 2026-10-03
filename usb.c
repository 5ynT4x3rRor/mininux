#include "usb.h"

#define POOL_BASE 0x300000U
#define COMMAND_TRBS 32U
#define EVENT_TRBS 64U
#define TRANSFER_TRBS 16U
#define MAX_DEPTH 2U
#define PORT_PRESERVE 0x0e01c3e0U

typedef volatile unsigned int vu32;

struct ring {
    vu32 *trb;
    unsigned int size;
    unsigned int index;
    unsigned int cycle;
};

struct device {
    unsigned int slot;
    struct ring ring;
    vu32 *output;
    vu32 *input;
};

static struct {
    vu32 *op;
    vu32 *runtime;
    vu32 *doorbell;
    vu32 *dcbaa;
    vu32 *event;
    unsigned char *buffer;
    struct ring command;
    unsigned int context_size;
    unsigned int max_slots;
    unsigned int event_index;
    unsigned int event_cycle;
    struct usb_entry *entries;
    unsigned int count;
    unsigned int max;
} xhci;

static unsigned int pool_next;

static void udelay(unsigned int microseconds)
{
    for (unsigned int index = 0; index < microseconds; index++) {
        __asm__ volatile ("inb $0x80, %%al" : : : "al");
    }
}

static void mdelay(unsigned int milliseconds)
{
    udelay(milliseconds * 1000U);
}

static void *pool_alloc(unsigned int size, unsigned int align)
{
    vu32 *words;
    unsigned int address;

    pool_next = (pool_next + align - 1U) & ~(align - 1U);
    address = pool_next;
    pool_next += size;
    words = (vu32 *)address;
    for (unsigned int index = 0; index < size / 4U; index++) {
        words[index] = 0;
    }
    return (void *)address;
}

static void ring_init(struct ring *ring, unsigned int size)
{
    ring->trb = (vu32 *)pool_alloc(size * 16U, 4096U);
    ring->size = size;
    ring->index = 0;
    ring->cycle = 1;
}

static void ring_push(struct ring *ring, unsigned int p0, unsigned int p1,
                      unsigned int p2, unsigned int control)
{
    vu32 *trb = ring->trb + ring->index * 4U;

    trb[0] = p0;
    trb[1] = p1;
    trb[2] = p2;
    trb[3] = control | ring->cycle;
    ring->index++;

    if (ring->index == ring->size - 1U) {
        trb = ring->trb + ring->index * 4U;
        trb[0] = (unsigned int)ring->trb;
        trb[1] = 0;
        trb[2] = 0;
        trb[3] = (6U << 10) | 2U | ring->cycle;
        ring->index = 0;
        ring->cycle ^= 1U;
    }
}

/* Returns the completion code, or 0xff on timeout. */
static unsigned int wait_event(unsigned int wanted_type, unsigned int *slot)
{
    for (unsigned int tries = 0; tries < 300000U; ) {
        vu32 *trb = xhci.event + xhci.event_index * 4U;

        if ((trb[3] & 1U) == xhci.event_cycle) {
            unsigned int type = (trb[3] >> 10) & 0x3fU;
            unsigned int code = trb[2] >> 24;
            unsigned int event_slot = trb[3] >> 24;

            xhci.event_index++;
            if (xhci.event_index == EVENT_TRBS) {
                xhci.event_index = 0;
                xhci.event_cycle ^= 1U;
            }
            xhci.runtime[14] = ((unsigned int)(xhci.event + xhci.event_index * 4U)) | 8U;
            xhci.runtime[15] = 0;

            if (type == wanted_type) {
                if (slot != 0) {
                    *slot = event_slot;
                }
                return code;
            }
            continue;
        }
        udelay(1);
        tries++;
    }
    return 0xffU;
}

static unsigned int run_command(unsigned int p0, unsigned int control,
                                unsigned int *slot)
{
    ring_push(&xhci.command, p0, 0, 0, control);
    xhci.doorbell[0] = 0;
    return wait_event(33U, slot);
}

static unsigned int control_transfer(struct device *device, unsigned int request_type,
                                     unsigned int request, unsigned int value,
                                     unsigned int index, unsigned int length)
{
    unsigned int incoming = (request_type & 0x80U) != 0;
    unsigned int transfer_type = length == 0 ? 0U : (incoming ? 3U : 2U);
    unsigned int code;

    for (unsigned int offset = 0; offset < 64U; offset++) {
        xhci.buffer[offset] = 0;
    }

    ring_push(&device->ring, request_type | (request << 8) | (value << 16),
              index | (length << 16), 8U,
              (2U << 10) | (1U << 6) | (transfer_type << 16));
    if (length != 0) {
        ring_push(&device->ring, (unsigned int)xhci.buffer, 0, length,
                  (3U << 10) | (incoming ? (1U << 16) : 0U));
    }
    ring_push(&device->ring, 0, 0, 0,
              (4U << 10) | (1U << 5) |
              ((length == 0 || !incoming) ? (1U << 16) : 0U));
    xhci.doorbell[device->slot] = 1;

    code = wait_event(32U, 0);
    return code == 1U || code == 13U;
}

static void enumerate_device(unsigned int speed, unsigned int root_port,
                             unsigned int route, unsigned int level,
                             unsigned int tt_slot, unsigned int tt_port);

static void scan_hub(struct device *hub, unsigned int speed, unsigned int root_port,
                     unsigned int route, unsigned int level, unsigned int ports,
                     unsigned int hub_slot)
{
    for (unsigned int port = 1; port <= ports; port++) {
        control_transfer(hub, 0x23U, 3U, 8U, port, 0);
    }
    mdelay(200);

    for (unsigned int port = 1; port <= ports; port++) {
        unsigned int status;
        unsigned int child_speed;
        unsigned int child_tt_slot = 0;
        unsigned int child_tt_port = 0;

        if (!control_transfer(hub, 0xa3U, 0, 0, port, 4U)) {
            continue;
        }
        status = xhci.buffer[0] | ((unsigned int)xhci.buffer[1] << 8);
        if (!(status & 1U)) {
            continue;
        }

        control_transfer(hub, 0x23U, 3U, 4U, port, 0);
        for (unsigned int tries = 0; tries < 50U; tries++) {
            mdelay(10);
            if (!control_transfer(hub, 0xa3U, 0, 0, port, 4U)) {
                break;
            }
            status = xhci.buffer[0] | ((unsigned int)xhci.buffer[1] << 8);
            if (xhci.buffer[2] & 0x10U) {
                break;
            }
        }
        control_transfer(hub, 0x23U, 1U, 20U, port, 0);
        mdelay(20);

        if (!(status & 2U)) {
            continue;
        }
        child_speed = (status & 0x200U) ? 2U : ((status & 0x400U) ? 3U : 1U);
        if (speed == 3U && child_speed < 3U) {
            child_tt_slot = hub_slot;
            child_tt_port = port;
        }
        enumerate_device(child_speed, root_port, route | (port << (4U * level)),
                         level + 1U, child_tt_slot, child_tt_port);
    }
}

static void enumerate_device(unsigned int speed, unsigned int root_port,
                             unsigned int route, unsigned int level,
                             unsigned int tt_slot, unsigned int tt_port)
{
    struct device device;
    struct usb_entry *entry;
    unsigned int slot = 0;
    unsigned int code;
    unsigned int max_packet = speed == 4U ? 512U : (speed == 3U ? 64U : 8U);
    unsigned int words = xhci.context_size / 4U;
    vu32 *slot_context;
    vu32 *endpoint_context;

    if (xhci.count >= xhci.max) {
        return;
    }
    entry = &xhci.entries[xhci.count];
    entry->vendor = 0;
    entry->product = 0;
    entry->device_class = 0;
    entry->subclass = 0;
    entry->root_port = (unsigned char)root_port;
    entry->speed = (unsigned char)speed;
    entry->flags = USB_FLAG_FAILED;
    entry->route = route;

    code = run_command(0, 9U << 10, &slot);
    if (code != 1U || slot == 0 || slot > xhci.max_slots) {
        xhci.count++;
        return;
    }

    device.slot = slot;
    ring_init(&device.ring, TRANSFER_TRBS);
    device.output = (vu32 *)pool_alloc(4096U, 4096U);
    device.input = (vu32 *)pool_alloc(4096U, 4096U);

    slot_context = device.input + words;
    endpoint_context = device.input + 2U * words;
    device.input[1] = 3U;
    slot_context[0] = route | (speed << 20) | (1U << 27);
    slot_context[1] = root_port << 16;
    slot_context[2] = tt_slot | (tt_port << 8);
    endpoint_context[1] = (3U << 1) | (4U << 3) | (max_packet << 16);
    endpoint_context[2] = ((unsigned int)device.ring.trb) | 1U;
    endpoint_context[4] = 8U;

    xhci.dcbaa[slot * 2U] = (unsigned int)device.output;
    xhci.dcbaa[slot * 2U + 1U] = 0;

    code = run_command((unsigned int)device.input, (11U << 10) | (slot << 24), 0);
    if (code != 1U) {
        xhci.count++;
        return;
    }

    if (control_transfer(&device, 0x80U, 6U, 0x0100U, 0, 8U)) {
        unsigned int actual = xhci.buffer[7];

        if (speed == 4U) {
            actual = actual < 16U ? (1U << actual) : 0U;
        }
        if (actual != 0 && actual != max_packet) {
            for (unsigned int index = 0; index < 1024U; index++) {
                device.input[index] = 0;
            }
            device.input[1] = 2U;
            endpoint_context[1] = (3U << 1) | (4U << 3) | (actual << 16);
            run_command((unsigned int)device.input, (13U << 10) | (slot << 24), 0);
        }
    }

    if (control_transfer(&device, 0x80U, 6U, 0x0100U, 0, 18U)) {
        entry->device_class = xhci.buffer[4];
        entry->subclass = xhci.buffer[5];
        entry->vendor = (unsigned short)(xhci.buffer[8] | (xhci.buffer[9] << 8));
        entry->product = (unsigned short)(xhci.buffer[10] | (xhci.buffer[11] << 8));
        entry->flags = 0;
    }
    xhci.count++;

    if (entry->flags == 0 && entry->device_class == 9U) {
        unsigned int ports = 0;

        if (speed != 4U && level < MAX_DEPTH &&
            control_transfer(&device, 0x00U, 9U, 1U, 0, 0) &&
            control_transfer(&device, 0xa0U, 6U, 0x2900U, 0, 8U)) {
            ports = xhci.buffer[2];
        }
        if (ports == 0 || ports > 8U) {
            entry->flags = USB_FLAG_HUB_SKIPPED;
            return;
        }

        for (unsigned int index = 0; index < 1024U; index++) {
            device.input[index] = 0;
        }
        device.input[1] = 1U;
        for (unsigned int index = 0; index < 8U; index++) {
            slot_context[index] = device.output[index];
        }
        slot_context[0] |= 1U << 26;
        slot_context[1] = (slot_context[1] & 0x00ffffffU) | (ports << 24);
        if (run_command((unsigned int)device.input, (12U << 10) | (slot << 24), 0) != 1U) {
            entry->flags = USB_FLAG_HUB_SKIPPED;
            return;
        }
        scan_hub(&device, speed, root_port, route, level, ports, slot);
    }
}

static void scan_root_port(unsigned int port)
{
    vu32 *portsc = xhci.op + 256U + (port - 1U) * 4U;
    unsigned int value = *portsc;

    if (!(value & 1U)) {
        return;
    }
    if (!(value & (1U << 9))) {
        *portsc = (value & PORT_PRESERVE) | (1U << 9);
        mdelay(20);
        value = *portsc;
    }

    *portsc = (value & PORT_PRESERVE) | (1U << 4);
    for (unsigned int tries = 0; tries < 500U; tries++) {
        if (*portsc & (1U << 21)) {
            break;
        }
        mdelay(1);
    }
    value = *portsc;
    *portsc = (value & PORT_PRESERVE) | 0x00fe0000U;
    mdelay(20);
    value = *portsc;

    if (!(value & 2U)) {
        return;
    }
    enumerate_device((value >> 10) & 0xfU, port, 0, 0, 0, 0);
}

static unsigned int controller_scan(unsigned int base)
{
    vu32 *cap = (vu32 *)base;
    unsigned int length = (unsigned int)(*(volatile unsigned char *)base);
    unsigned int params1 = cap[1];
    unsigned int params2 = cap[2];
    unsigned int capabilities = cap[4];
    unsigned int scratchpads = (((params2 >> 21) & 0x1fU) << 5) | ((params2 >> 27) & 0x1fU);
    unsigned int ports = params1 >> 24;
    unsigned int offset = (capabilities >> 16) & 0xffffU;
    unsigned int ready = 0;
    vu32 *event_table;

    xhci.op = (vu32 *)(base + length);
    xhci.doorbell = (vu32 *)(base + (cap[5] & ~3U));
    xhci.runtime = (vu32 *)(base + (cap[6] & ~0x1fU));
    xhci.context_size = (capabilities & 4U) ? 64U : 32U;
    xhci.max_slots = params1 & 0xffU;

    for (unsigned int guard = 0; offset != 0 && guard < 64U; guard++) {
        vu32 *extended = (vu32 *)(base + offset * 4U);
        unsigned int header = extended[0];
        unsigned int next = (header >> 8) & 0xffU;

        if ((header & 0xffU) == 1U) {
            if (header & (1U << 16)) {
                extended[0] = header | (1U << 24);
                for (unsigned int tries = 0; tries < 1000U; tries++) {
                    if (!(extended[0] & (1U << 16))) {
                        break;
                    }
                    mdelay(1);
                }
            }
            break;
        }
        if (next == 0) {
            break;
        }
        offset += next;
    }

    xhci.op[0] &= ~1U;
    for (unsigned int tries = 0; tries < 200U; tries++) {
        if (xhci.op[1] & 1U) {
            ready = 1;
            break;
        }
        mdelay(1);
    }
    if (!ready) {
        return 0;
    }

    xhci.op[0] = 2U;
    ready = 0;
    for (unsigned int tries = 0; tries < 1000U; tries++) {
        if (!(xhci.op[0] & 2U) && !(xhci.op[1] & (1U << 11))) {
            ready = 1;
            break;
        }
        mdelay(1);
    }
    if (!ready || xhci.max_slots == 0) {
        return 0;
    }

    pool_next = POOL_BASE;
    xhci.buffer = (unsigned char *)pool_alloc(256U, 64U);
    xhci.dcbaa = (vu32 *)pool_alloc(4096U, 4096U);
    if (scratchpads != 0) {
        vu32 *array = (vu32 *)pool_alloc(scratchpads * 8U, 64U);

        for (unsigned int index = 0; index < scratchpads; index++) {
            array[index * 2U] = (unsigned int)pool_alloc(4096U, 4096U);
        }
        xhci.dcbaa[0] = (unsigned int)array;
    }
    ring_init(&xhci.command, COMMAND_TRBS);
    xhci.event = (vu32 *)pool_alloc(EVENT_TRBS * 16U, 4096U);
    event_table = (vu32 *)pool_alloc(16U, 64U);
    event_table[0] = (unsigned int)xhci.event;
    event_table[2] = EVENT_TRBS;
    xhci.event_index = 0;
    xhci.event_cycle = 1;

    xhci.op[14] = xhci.max_slots;
    xhci.op[12] = (unsigned int)xhci.dcbaa;
    xhci.op[13] = 0;
    xhci.op[6] = ((unsigned int)xhci.command.trb) | 1U;
    xhci.op[7] = 0;
    xhci.runtime[10] = 1;
    xhci.runtime[14] = (unsigned int)xhci.event;
    xhci.runtime[15] = 0;
    xhci.runtime[12] = (unsigned int)event_table;
    xhci.runtime[13] = 0;

    xhci.op[0] = 1U;
    ready = 0;
    for (unsigned int tries = 0; tries < 200U; tries++) {
        if (!(xhci.op[1] & 1U)) {
            ready = 1;
            break;
        }
        mdelay(1);
    }
    if (!ready) {
        return 0;
    }

    mdelay(150);
    for (unsigned int port = 1; port <= ports; port++) {
        scan_root_port(port);
    }
    return 1;
}

unsigned int usb_scan(struct usb_entry *entries, unsigned int max_entries,
                      unsigned int *controllers, unsigned int *status)
{
    xhci.entries = entries;
    xhci.count = 0;
    xhci.max = max_entries;
    *controllers = 0;
    *status = 0;

    for (unsigned int bus = 0; bus < 256U; bus++) {
        for (unsigned int device = 0; device < 32U; device++) {
            for (unsigned int function = 0; function < 8U; function++) {
                unsigned int id = pci_read_config((unsigned char)bus, (unsigned char)device,
                                                  (unsigned char)function, 0);
                unsigned int class_info;
                unsigned int bar;
                unsigned int command;

                if ((id & 0xffffU) == 0xffffU) {
                    if (function == 0) {
                        break;
                    }
                    continue;
                }
                class_info = pci_read_config((unsigned char)bus, (unsigned char)device,
                                             (unsigned char)function, 0x08);
                if ((class_info >> 8) != 0x0c0330U) {
                    continue;
                }

                (*controllers)++;
                bar = pci_read_config((unsigned char)bus, (unsigned char)device,
                                      (unsigned char)function, 0x10);
                if ((bar & 1U) != 0 || (bar & ~0xfU) == 0 ||
                    (((bar >> 1) & 3U) == 2U &&
                     pci_read_config((unsigned char)bus, (unsigned char)device,
                                     (unsigned char)function, 0x14) != 0)) {
                    *status |= USB_STATUS_BAR_UNUSABLE;
                    continue;
                }

                command = pci_read_config((unsigned char)bus, (unsigned char)device,
                                          (unsigned char)function, 0x04);
                pci_write_config((unsigned char)bus, (unsigned char)device,
                                 (unsigned char)function, 0x04,
                                 (command & 0xffffU) | 6U);
                if (!controller_scan(bar & ~0xfU)) {
                    *status |= USB_STATUS_INIT_FAILED;
                }
            }
        }
    }
    return xhci.count;
}
