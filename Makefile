TOOLPREFIX ?= riscv64-unknown-elf-

CC = $(TOOLPREFIX)gcc
LD = $(TOOLPREFIX)ld
QEMU = qemu-system-riscv64

CFLAGS = -Wall -Werror -O0 -ggdb
CFLAGS += -march=rv64gc -mabi=lp64
CFLAGS += -mcmodel=medany -ffreestanding -fno-common -fno-builtin -nostdlib
CFLAGS += -mno-relax

LDFLAGS = -z max-page-size=4096

OBJS = entry.o start.o main.o uart.o printf.o

kernel.elf: $(OBJS) kernel.ld
	$(LD) $(LDFLAGS) -T kernel.ld -o kernel.elf $(OBJS)

entry.o: entry.S
	$(CC) $(CFLAGS) -c -o entry.o entry.S

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: kernel.elf
	$(QEMU) -machine virt -bios none -kernel kernel.elf -nographic

clean:
	rm -f *.o kernel.elf
