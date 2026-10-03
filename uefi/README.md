# MiniNux UEFI

`boot.c` is the freestanding UEFI entry point. The BIOS loader remains the
default boot path while the UEFI loader is built incrementally.

The next UEFI layer will use the loaded image and system table to access the
UEFI console and read the MiniNux kernel from a FAT EFI System Partition.