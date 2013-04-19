AS = sh-elf-as -little
LD = sh-elf-ld -EL
CC = sh-elf-gcc -ml

CFLAGS = -O4 -m4-single-only -mhitachi -ffreestanding

OBJS = startup.o main.o vmop.o vmsfs.o cdfs.o syscall.o video.o maple.o serial.o launch.o

all : 1ST_READ.BIN IP.BIN

test : bootcd.elf
	ipupload.pike < bootcd.elf

bootcd.srec : $(OBJS)
	$(CC) -Wl,-Ttext=0x8ce00000,--oformat,srec -o $@ $(CFLAGS) -nostartfiles -nostdlib $(OBJS) -lgcc -lc -lgcc


bootcd.elf : $(OBJS)
	$(CC) -Wl,-Ttext=0x8ce00000 -o $@ $(CFLAGS) -nostartfiles -nostdlib $(OBJS) -lgcc -lc -lgcc


1ST_READ.BIN : bootcd.bin
	scramble $< $@

bootcd.bin : bootcd.elf
	sh-elf-objcopy -O binary -R .stack $< $@

IP.BIN : ip.txt
	makeip $< $@
