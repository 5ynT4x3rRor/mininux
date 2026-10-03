ASM := nasm
CC := gcc
LD := ld
QEMU := qemu-system-x86_64

.PHONY: all run read-report read-bios-report tinyc-check tinyc-test uefi-check uefi-image usb-image clean

B := build
K := src/kernel
U := src/uefi
T := src/tinyc

all: mininux.img

$(B):
	mkdir -p $(B)

$(B)/boot.bin: src/boot/boot.asm | $(B)
	$(ASM) -f bin $< -o $@

KERNEL_CFLAGS := -m32 -Os -fno-tree-loop-distribute-patterns -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdlib

KERNEL_OBJS := $(B)/kernel.o $(B)/usb.o $(B)/crypto.o $(B)/persist.o

$(B)/kernel.o: $(K)/kernel.c $(K)/usb.h $(K)/crypto.h $(K)/persist.h | $(B)
	$(CC) -m32 -Os -fno-tree-loop-distribute-patterns -fno-toplevel-reorder -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdlib -c $< -o $@

$(B)/usb.o: $(K)/usb.c $(K)/usb.h | $(B)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(B)/crypto.o: $(K)/crypto.c $(K)/crypto.h | $(B)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(B)/persist.o: $(K)/persist.c $(K)/persist.h $(K)/crypto.h | $(B)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(B)/kernel.bin: $(KERNEL_OBJS)
	$(LD) -m elf_i386 -e kernel_main -Ttext 0x10000 -z noseparate-code --oformat binary -o $@ $(KERNEL_OBJS)

$(B)/kernel.padded: $(B)/kernel.bin
	test "$$(wc -c < $<)" -le 30720
	cp $< $@
	truncate -s 32768 $@

mininux.img: $(B)/boot.bin $(B)/kernel.padded filesystem/manifest
	cat $(B)/boot.bin $(B)/kernel.padded filesystem/manifest > $(B)/$@
	truncate -s 33792 $(B)/$@
	cp $(B)/$@ $@

run: mininux.img
	$(QEMU) -drive format=raw,file=$<

read-report:
	@case "$$DEV" in /dev/sd[a-z]|/dev/nvme[0-9]n[0-9]|/dev/mmcblk[0-9]) ;; *) echo "usage: make read-report DEV=/dev/sdX"; exit 1;; esac
	@P="$$DEV"1; test -b "$$P" || { echo "partition UEFI absente: $$P"; exit 1; }
	mcopy -i "$$DEV"1 ::/MININUX.TXT rapport-mininux.txt
	@echo "rapport-mininux.txt ecrit"

read-bios-report:
	@case "$$DEV" in /dev/sd[a-z]|/dev/nvme[0-9]n[0-9]|/dev/mmcblk[0-9]) ;; *) echo "usage: make read-bios-report DEV=/dev/sdX"; exit 1;; esac
	dd if="$$DEV" bs=512 skip=64 count=128 status=none | tr -d '\000' > rapport-mininux.txt
	@echo "rapport-mininux.txt ecrit"

usb-image: mininux-uefi.img
	cp $< mininux-usb.img

tinyc-check:
	$(CC) -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc -I$(T) -c $(T)/lexer.c -o .mininux-tinyc-lexer.o
	$(CC) -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc -I$(T) -c $(T)/parser.c -o .mininux-tinyc-parser.o
	$(CC) -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc -I$(T) -c $(T)/codegen.c -o .mininux-tinyc-codegen.o
	$(CC) -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc -I$(T) -c $(T)/compiler.c -o .mininux-tinyc-compiler.o

tinyc-test:
	$(CC) -I$(T) $(T)/lexer.c $(T)/parser.c $(T)/codegen.c $(T)/compiler.c $(T)/test_compiler.c -o .mininux-tinyc-test
	./.mininux-tinyc-test
	rm -f .mininux-tinyc-test

uefi-check:
	$(CC) -I/usr/include/efi -I/usr/include/efi/x86_64 -ffreestanding -fshort-wchar -mno-red-zone -maccumulate-outgoing-args -fno-stack-protector -c $(U)/boot.c -o .mininux-uefi-boot.o
	rm -f .mininux-uefi-boot.o

$(U)/boot.o: $(U)/boot.c
	$(CC) -I/usr/include/efi -I/usr/include/efi/x86_64 -ffreestanding -fpic -fshort-wchar -mno-red-zone -maccumulate-outgoing-args -fno-stack-protector -c $< -o $@

$(U)/boot.so: $(U)/boot.o
	$(LD) -nostdlib -znocombreloc -shared -Bsymbolic -T /usr/lib/elf_x86_64_efi.lds /usr/lib/crt0-efi-x86_64.o $< -L/usr/lib -lefi -lgnuefi -o $@

$(U)/BOOTX64.EFI: $(U)/boot.so
	objcopy -I elf64-x86-64 -O efi-app-x86_64 -j .text -j .sdata -j .data -j .rodata -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc $< $@

uefi-image: mininux-uefi.img

mininux-uefi.img: $(U)/BOOTX64.EFI
	dd if=/dev/zero of=mininux-uefi.img bs=1M count=64 status=none
	parted -s mininux-uefi.img mklabel msdos
	parted -s mininux-uefi.img mkpart primary fat32 1MiB 100%
	parted -s mininux-uefi.img set 1 esp on
	mkfs.fat -F 32 --offset=2048 mininux-uefi.img >/dev/null
	mmd -i mininux-uefi.img@@1048576 ::/EFI ::/EFI/BOOT
	mcopy -i mininux-uefi.img@@1048576 $(U)/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI

clean:
	rm -rf $(B) mininux.img mininux-usb.img mininux-uefi.img $(U)/boot.o $(U)/boot.so $(U)/BOOTX64.EFI .mininux-tinyc-*.o
