#ifndef MININUX_USB_H
#define MININUX_USB_H

#define USB_FLAG_FAILED 1U
#define USB_FLAG_HUB_SKIPPED 2U

#define USB_STATUS_INIT_FAILED 1U
#define USB_STATUS_BAR_UNUSABLE 2U

struct usb_entry {
    unsigned short vendor;
    unsigned short product;
    unsigned char device_class;
    unsigned char subclass;
    unsigned char root_port;
    unsigned char speed;
    unsigned char flags;
    unsigned int route;
};

unsigned int pci_read_config(unsigned char bus, unsigned char device,
                             unsigned char function, unsigned char offset);
void pci_write_config(unsigned char bus, unsigned char device,
                      unsigned char function, unsigned char offset,
                      unsigned int value);

unsigned int usb_scan(struct usb_entry *entries, unsigned int max_entries,
                      unsigned int *controllers, unsigned int *status);

#endif
