# Makefile for JamesM's kernel tutorials -- 64-bit port.
#
# We link an ELF64 kernel, then rewrite the container to ELF32 with objcopy.
# Multiboot 1 loaders (GRUB Legacy, qemu -kernel) only parse ELF32 headers, but
# they will happily load and jump into an image whose *contents* are 64-bit.
# Every address in this kernel is well below 4 GiB, so nothing is lost.

SOURCES=boot.o main.o monitor.o common.o

CFLAGS=-m64 -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
       -fno-pie -fno-pic -ffreestanding -mno-red-zone \
       -mno-mmx -mno-sse -mno-sse2 -Wall -g
LDFLAGS=-T link.ld -m elf_x86_64 -z max-page-size=0x1000 -z noexecstack
ASFLAGS=-felf64 -g -F dwarf

all: $(SOURCES) link

clean:
	-rm -f *.o kernel kernel64.elf

link:
	ld $(LDFLAGS) -o kernel64.elf $(SOURCES)
	objcopy -I elf64-x86-64 -O elf32-i386 kernel64.elf kernel

.s.o:
	nasm $(ASFLAGS) $<
