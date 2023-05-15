DEV_BIN_DIR := $(DEVKITARM)/bin

CC := $(DEV_BIN_DIR)/arm-none-eabi-gcc
CXX := $(DEV_BIN_DIR)/arm-none-eabi-g++
OBJCOPY := $(DEV_BIN_DIR)/arm-none-eabi-objcopy
LD := $(DEV_BIN_DIR)/arm-none-eabi-ld
CP := cp

CFLAGS := -Ofast -s -march=armv6 -mlittle-endian -fomit-frame-pointer -ffast-math -march=armv6k -mtune=mpcore -mfloat-abi=hard
CPPFLAGS := -Iinclude
LDFLAGS := -L. -A armv6k -pie --print-gc-sections -T 3ds.ld -Map=test.map
LDLIBS := -lc -lm -lgcc --nostdlib

SRC_C := $(wildcard source/dsp/*.c) $(wildcard source/ns/*.c) $(wildcard source/*.c) $(wildcard source/libctru/*.c)
SRC_C += $(wildcard source/ffmpeg/libavcodec/*.c) $(wildcard source/ffmpeg/libavfilter/*.c) $(wildcard source/ffmpeg/libavutil/*.c)
SRC_C += $(wildcard source/jpeg_ls/*.c)
SRC_S := $(wildcard source/*.s) $(wildcard source/libctru/*.s)
OBJ := $(addprefix obj/,$(notdir $(SRC_S:.s=.o) $(SRC_C:.c=.o)))
DEP := $(OBJ:.o=.d)

PAYLOAD_BIN_NAME := ntr.n3ds.hr.bin
PAYLOAD_TARGET_DIR := ../BootNTR-Selector/romfs/
PAYLOAD_TARGET_BIN := $(PAYLOAD_TARGET_DIR)$(PAYLOAD_BIN_NAME)

PAYLOAD_LOCAL_BIN := release/$(PAYLOAD_BIN_NAME)
PAYLOAD_BIN := payload.bin
PAYLOAD_ELF := a.out
PAYLOAD_LOCAL_ELF := bin/homebrew.elf

all: $(PAYLOAD_LOCAL_BIN) $(PAYLOAD_LOCAL_ELF)

install: $(PAYLOAD_TARGET_BIN)

CP_CMD = $(CP) $< $@

$(PAYLOAD_TARGET_BIN): $(PAYLOAD_LOCAL_BIN)
	$(CP_CMD)

$(PAYLOAD_LOCAL_BIN): $(PAYLOAD_LOCAL_ELF)
	$(OBJCOPY) -O binary $< $@ -S

$(PAYLOAD_LOCAL_ELF): $(OBJ)
	$(LD) -o $@ $(LDFLAGS) $(filter-out obj/bootloader.o,$^) $(LDLIBS)

CC_CMD = $(CC) $(CFLAGS) $(CPPFLAGS) -MMD -c -o $@ $<

obj/%.o: source/%.s
	$(CC_CMD)

obj/%.o: source/libctru/%.s
	$(CC_CMD)

obj/%.o: source/dsp/%.c
	$(CC_CMD)

obj/%.o: source/ns/%.c
	$(CC_CMD)

obj/rp.o: source/ns/rp.c
	$(CC_CMD) -Isource/ffmpeg

obj/%.o: source/%.c
	$(CC_CMD)

obj/%.o: source/libctru/%.c
	$(CC_CMD)

obj/%.o: source/ffmpeg/libavcodec/%.c
	$(CC_CMD) -Isource/ffmpeg

obj/%.o: source/ffmpeg/libavfilter/%.c
	$(CC_CMD) -Isource/ffmpeg

obj/%.o: source/ffmpeg/libavutil/%.c
	$(CC_CMD) -Isource/ffmpeg

obj/%.o: source/jpeg_ls/%.c
	$(CC_CMD)

-include $(DEP)

.PHONY: clean all install

clean:
	-rm test.map $(PAYLOAD_LOCAL_BIN) $(PAYLOAD_LOCAL_ELF) obj/*.d obj/*.o
