# MiniNux UEFI USB survey

`boot.c` builds as `EFI/BOOT/BOOTX64.EFI` on a FAT32 EFI partition. The app
enumerates USB devices exposed through `EFI_USB_IO_PROTOCOL`, prints VID:PID,
device class and revision, then saves the inventory as `/MININUX.TXT` on the
same EFI partition.

Build and test the removable-media image with `make usb-image`. This is a
UEFI-native hardware-survey application; it does not yet launch the separate
32-bit BIOS kernel.
