# MiniNux UEFI USB survey

`boot.c` builds as `EFI/BOOT/BOOTX64.EFI` on a FAT32 EFI partition. The app
enumerates USB devices exposed through `EFI_USB_IO_PROTOCOL`, prints VID:PID,
device class and revision, then saves the inventory as `/MININUX.TXT` on the
same EFI partition.

Build and test the removable-media image with `make usb-image`. This is a
UEFI-native hardware-survey application; it does not yet launch the separate
32-bit BIOS kernel.

## Affichage

L'écran noir sur le Mac venait du texte UEFI (ConOut), non affiché par le firmware Apple. L'application dessine maintenant le rapport directement dans le framebuffer GOP (fond bleu, police 8x16 dans `font8x16.h`, agrandie selon la résolution). ConOut n'est utilisé que si le GOP est indisponible. Le rapport reste écrit dans `MININUX.TXT`.
