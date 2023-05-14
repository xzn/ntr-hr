DEV_BIN_DIR := $(DEVKITARM)/bin

CC := $(DEV_BIN_DIR)/arm-none-eabi-gcc
CXX := $(DEV_BIN_DIR)/arm-none-eabi-g++
OBJCOPY := $(DEV_BIN_DIR)/arm-none-eabi-objcopy
LD := $(DEV_BIN_DIR)/arm-none-eabi-ld
CP := cp

CFLAGS := -Ofast -s -march=armv6 -mlittle-endian -fomit-frame-pointer -ffast-math -march=armv6k -mtune=mpcore -mfloat-abi=hard
CPPFLAGS := -Iinclude -I$(DEVKITARM)/../portlibs/3ds/include
LDFLAGS := -L. -A armv6k -pie --print-gc-sections -T 3ds.ld -Map=test.map
LDLIBS := -lc -lm -lgcc --nostdlib

SRC_C := $(wildcard source/dsp/*.c) $(wildcard source/ns/*.c) $(wildcard source/*.c) $(wildcard source/libctru/*.c)
SRC_S := $(wildcard source/ns/*.s) $(wildcard source/*.s) $(wildcard source/libctru/*.s)
OBJ := $(addprefix obj/,$(notdir $(SRC_S:.s=.o) $(SRC_C:.c=.o)))
DEP := $(OBJ:.o=.d)

PAYLOAD_BIN_NAME := ntr.n3ds.hr.bin
PAYLOAD_TARGET_DIR := ../BootNTR-Selector/romfs/
PAYLOAD_TARGET_BIN := $(PAYLOAD_TARGET_DIR)$(PAYLOAD_BIN_NAME)

PAYLOAD_LOCAL_BIN := release/$(PAYLOAD_BIN_NAME)
PAYLOAD_BIN := payload.bin
PAYLOAD_ELF := a.out
PAYLOAD_LOCAL_ELF := bin/homebrew.elf

all: $(PAYLOAD_TARGET_BIN) $(PAYLOAD_LOCAL_ELF)

CP_CMD = $(CP) $< $@

$(PAYLOAD_TARGET_BIN): $(PAYLOAD_LOCAL_BIN)
	$(CP_CMD)

$(PAYLOAD_LOCAL_BIN): $(PAYLOAD_LOCAL_ELF)
	$(OBJCOPY) -O binary $< $@ -S

$(PAYLOAD_LOCAL_ELF): $(OBJ)
	$(LD) -o $@ $(LDFLAGS) -Lobj $(filter-out obj/bootloader.o,$^) $(LDLIBS)

CC_CMD = $(CC) $(CFLAGS) $(CPPFLAGS) -MMD -c -o $@ $<

obj/%.o: source/ns/%.s
	$(CC_CMD)

obj/%.o: source/%.s
	$(CC_CMD)

obj/%.o: source/libctru/%.s
	$(CC_CMD)

obj/%.o: source/dsp/%.c
	$(CC_CMD)

obj/%.o: source/ns/%.c
	$(CC_CMD)

obj/%.o: source/%.c
	$(CC_CMD)

obj/%.o: source/libctru/%.c
	$(CC_CMD)

-include $(DEP)

.PHONY: clean

clean:
	-rm test.map $(PAYLOAD_LOCAL_BIN) $(PAYLOAD_LOCAL_ELF) obj/*.d obj/*.o
