ASM := nasm
CC := gcc
LD := ld
QEMU := qemu-system-x86_64

.PHONY: all run read-report read-bios-report tinyc-check tinyc-test uefi-check uefi-image usb-image clean

all: mininux.img

boot.bin: boot.asm
	$(ASM) -f bin $< -o $@

KERNEL_CFLAGS := -m32 -Os -fno-tree-loop-distribute-patterns -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdlib

kernel.o: kernel.c usb.h crypto.h persist.h
	$(CC) -m32 -Os -fno-tree-loop-distribute-patterns -fno-toplevel-reorder -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdlib -c $< -o $@

usb.o: usb.c usb.h
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

crypto.o: crypto.c crypto.h
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

persist.o: persist.c persist.h crypto.h
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

kernel.bin: kernel.o usb.o crypto.o persist.o
	$(LD) -m elf_i386 -e kernel_main -Ttext 0x10000 -z noseparate-code --oformat binary -o $@ kernel.o usb.o crypto.o persist.o

kernel.padded: kernel.bin
	test "$$(wc -c < $<)" -le 30720
	cp $< $@
	truncate -s 32768 $@

mininux.img: boot.bin kernel.padded filesystem/manifest
	cat boot.bin kernel.padded filesystem/manifest > $@
	truncate -s 33792 $@

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
	$(CC) -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc -Itinyc -c tinyc/lexer.c -o .mininux-tinyc-lexer.o
	$(CC) -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc -Itinyc -c tinyc/parser.c -o .mininux-tinyc-parser.o
	$(CC) -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc -Itinyc -c tinyc/codegen.c -o .mininux-tinyc-codegen.o
	$(CC) -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc -Itinyc -c tinyc/compiler.c -o .mininux-tinyc-compiler.o

tinyc-test:
	$(CC) -Itinyc tinyc/lexer.c tinyc/parser.c tinyc/codegen.c tinyc/compiler.c tinyc/test_compiler.c -o .mininux-tinyc-test
	./.mininux-tinyc-test
	rm -f .mininux-tinyc-test

uefi-check:
	$(CC) -I/usr/include/efi -I/usr/include/efi/x86_64 -ffreestanding -fshort-wchar -mno-red-zone -maccumulate-outgoing-args -fno-stack-protector -c uefi/boot.c -o .mininux-uefi-boot.o
	rm -f .mininux-uefi-boot.o

uefi/boot.o: uefi/boot.c
	$(CC) -I/usr/include/efi -I/usr/include/efi/x86_64 -ffreestanding -fpic -fshort-wchar -mno-red-zone -maccumulate-outgoing-args -fno-stack-protector -c $< -o $@

uefi/boot.so: uefi/boot.o
	$(LD) -nostdlib -znocombreloc -shared -Bsymbolic -T /usr/lib/elf_x86_64_efi.lds /usr/lib/crt0-efi-x86_64.o $< -L/usr/lib -lefi -lgnuefi -o $@

uefi/BOOTX64.EFI: uefi/boot.so
	objcopy -I elf64-x86-64 -O efi-app-x86_64 -j .text -j .sdata -j .data -j .rodata -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc $< $@

uefi-image: mininux-uefi.img

mininux-uefi.img: uefi/BOOTX64.EFI
	dd if=/dev/zero of=mininux-uefi.img bs=1M count=64 status=none
	parted -s mininux-uefi.img mklabel msdos
	parted -s mininux-uefi.img mkpart primary fat32 1MiB 100%
	parted -s mininux-uefi.img set 1 esp on
	mkfs.fat -F 32 --offset=2048 mininux-uefi.img >/dev/null
	mmd -i mininux-uefi.img@@1048576 ::/EFI ::/EFI/BOOT
	mcopy -i mininux-uefi.img@@1048576 uefi/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI

clean:
	rm -f boot.bin kernel.o usb.o crypto.o persist.o kernel.bin kernel.padded mininux.img mininux-usb.img mininux-uefi.img uefi/boot.o uefi/boot.so uefi/BOOTX64.EFI uefi.img .mininux-tinyc-*.o