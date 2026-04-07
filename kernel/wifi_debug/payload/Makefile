CC			:= ppu-gcc
CXX			:= ppu-g++
LD			:= ppu-ld
OBJCOPY		:= ppu-objcopy
AR 			:= ppu-ar
STRIP		:= ppu-strip

INCLUDE		= ../lv2/include ../lv1/include ../debug/include
INCLUDE		+= $(PS3DEV)/ppu/ppu/include
LIBSDIR		=
LIBS 			=
TARGET		= payload

FIRMWARE ?= 493C
SUPPORTED_FIRMWARES := 480C 481C 482C 482D 483C 484C 484D 485C 486C 487C 488C 489C 490C 491C 492C 493C

ifeq ($(FIRMWARE),480C)
FIRMWARE_DEF := FIRMWARE_4_80
else ifeq ($(FIRMWARE),481C)
FIRMWARE_DEF := FIRMWARE_4_81
else ifeq ($(FIRMWARE),482C)
FIRMWARE_DEF := FIRMWARE_4_82
else ifeq ($(FIRMWARE),482D)
FIRMWARE_DEF := FIRMWARE_4_82DEX
else ifeq ($(FIRMWARE),483C)
FIRMWARE_DEF := FIRMWARE_4_83
else ifeq ($(FIRMWARE),484C)
FIRMWARE_DEF := FIRMWARE_4_84
else ifeq ($(FIRMWARE),484D)
FIRMWARE_DEF := FIRMWARE_4_84DEX
else ifeq ($(FIRMWARE),485C)
FIRMWARE_DEF := FIRMWARE_4_85
else ifeq ($(FIRMWARE),486C)
FIRMWARE_DEF := FIRMWARE_4_86
else ifeq ($(FIRMWARE),487C)
FIRMWARE_DEF := FIRMWARE_4_87
else ifeq ($(FIRMWARE),488C)
FIRMWARE_DEF := FIRMWARE_4_88
else ifeq ($(FIRMWARE),489C)
FIRMWARE_DEF := FIRMWARE_4_89
else ifeq ($(FIRMWARE),490C)
FIRMWARE_DEF := FIRMWARE_4_90
else ifeq ($(FIRMWARE),491C)
FIRMWARE_DEF := FIRMWARE_4_91
else ifeq ($(FIRMWARE),492C)
FIRMWARE_DEF := FIRMWARE_4_92
else ifeq ($(FIRMWARE),493C)
FIRMWARE_DEF := FIRMWARE_4_93
else
$(error Unsupported FIRMWARE '$(FIRMWARE)'. Supported values: $(SUPPORTED_FIRMWARES))
endif

CFLAGS = -ffunction-sections -fdata-sections -Os -m64 -fno-builtin -fno-exceptions \
			   -Os -Wall $(foreach dir,$(INCLUDE),-I$(dir)) \
			   -DLV2 -D$(FIRMWARE_DEF) -ffreestanding
CFLAGS += --std=gnu99

LDFLAGS = -T $(TARGET).ld -nostartfiles -nostdlib -nodefaultlibs -Wl,-static -Wl,-s -L. $(foreach dir,$(LIBSDIR),-L$(dir)) $(LIBS) -Wl,--gc-sections -Wl

OBJS = start.o main.o ../lv2/src/libc.o ../lv2/src/memory.o ../lv2/src/io.o ../lv2/src/thread.o ../lv2/src/process.o ../lv2/src/net.o ../lv2/src/syscall.o

OUTPUT ?= $(TARGET).bin

.PHONY: all single clean clean-objs $(SUPPORTED_FIRMWARES)

all: $(SUPPORTED_FIRMWARES)

single: $(OUTPUT)

$(SUPPORTED_FIRMWARES):
	@$(MAKE) clean-objs --no-print-directory
	@$(MAKE) single --no-print-directory FIRMWARE=$@ OUTPUT=$(TARGET)_$@.bin

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@rm -f *.o *.elf ../lv1/src/*.o ../lv2/src/*.o ../debug/src/*.o

$(TARGET)_%.bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@rm -f *.o *.elf ../lv1/src/*.o ../lv2/src/*.o ../debug/src/*.o

$(TARGET).elf: $(OBJS) $(TARGET).ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean-objs:
	rm -f *.o *.elf ../lv1/src/*.o ../lv2/src/*.o ../debug/src/*.o

clean:
	rm -f *.o *.elf *.bin *.map ../lv1/src/*.o ../lv2/src/*.o ../debug/src/*.o
