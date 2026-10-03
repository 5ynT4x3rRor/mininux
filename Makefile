ASM := nasm
CC := gcc
LD := ld
QEMU := qemu-system-x86_64

.PHONY: all run tinyc-check tinyc-test uefi-check uefi-image usb-image clean

all: mininux.img

boot.bin: boot.asm
	$(ASM) -f bin $< -o $@

kernel.o: kernel.c
	$(CC) -m32 -Os -fno-toplevel-reorder -ffreestanding -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdlib -c $< -o $@

kernel.bin: kernel.o
	$(LD) -m elf_i386 -e kernel_main -Ttext 0x10000 --oformat binary -o $@ $<

kernel.padded: kernel.bin
	test "$$(wc -c < $<)" -le 10240
	cp $< $@
	truncate -s 10240 $@

mininux.img: boot.bin kernel.padded filesystem/manifest
	cat boot.bin kernel.padded filesystem/manifest > $@
	truncate -s 11264 $@

run: mininux.img
	$(QEMU) -drive format=raw,file=$<

usb-image: mininux.img
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
	$(CC) -ffreestanding -fshort-wchar -mno-red-zone -fno-stack-protector -nostdinc -c uefi/boot.c -o .mininux-uefi-boot.o
	rm -f .mininux-uefi-boot.o

uefi/boot.o: uefi/boot.c
	$(CC) -I/usr/include/efi -I/usr/include/efi/x86_64 -ffreestanding -fpic -fshort-wchar -mno-red-zone -fno-stack-protector -c $< -o $@

uefi/boot.so: uefi/boot.o
	$(LD) -nostdlib -znocombreloc -T /usr/lib/elf_x86_64_efi.lds /usr/lib/crt0-efi-x86_64.o $< -L/usr/lib -lefi -lgnuefi -o $@

uefi/BOOTX64.EFI: uefi/boot.so
	objcopy -j .text -j .sdata -j .data -j .rodata -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc --target=efi-app-x86_64 $< $@

uefi-image: uefi/BOOTX64.EFI
	rm -f uefi.img
	dd if=/dev/zero of=uefi.img bs=1M count=16 status=none
	mkfs.fat -F 32 uefi.img >/dev/null
	mmd -i uefi.img ::/EFI ::/EFI/BOOT
	mcopy -i uefi.img uefi/BOOTX64.EFI ::/EFI/BOOT/

clean:
	rm -f boot.bin kernel.o kernel.bin kernel.padded mininux.img mininux-usb.img uefi/boot.o uefi/boot.so uefi/BOOTX64.EFI uefi.img .mininux-tinyc-*.o