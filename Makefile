DEV_BIN_DIR := $(DEVKITARM)/bin

CC := $(DEV_BIN_DIR)/arm-none-eabi-gcc
CXX := $(DEV_BIN_DIR)/arm-none-eabi-g++
OBJCOPY := $(DEV_BIN_DIR)/arm-none-eabi-objcopy
LD := $(DEV_BIN_DIR)/arm-none-eabi-ld
CP := cp

CFLAGS := -s -Ofast -march=armv6k -mtune=mpcore -mfloat-abi=hard
CPPFLAGS := -Iinclude
LDFLAGS := $(CFLAGS) -pie -Wl,--gc-sections -T 3ds.ld -Wl,-Map=test.map
LDLIBS := -L. -lc -lm -lgcc -nostdlib

SRC_C := $(wildcard source/dsp/*.c) $(wildcard source/ns/*.c) $(wildcard source/*.c) $(wildcard source/libctru/*.c)
SRC_S := $(wildcard source/*.s) $(wildcard source/libctru/*.s)
RP_SRC_C := $(wildcard source/rp/*.c) $(wildcard source/misc/*.c)
RP_SRC_C += $(wildcard source/ffmpeg/libavcodec/*.c) $(wildcard source/ffmpeg/libavfilter/*.c) $(wildcard source/ffmpeg/libavutil/*.c)
RP_SRC_C += $(wildcard source/jpeg_ls/*.c)
RP_SRC_C += $(wildcard source/jpeg_turbo/*.c)
RP_SRC_X += $(filter-out %iz_dec.cpp,$(wildcard source/imagezero/*.cpp))
RP_OBJ := $(addprefix obj/,$(notdir $(RP_SRC_C:.c=.o) $(RP_SRC_X:.cpp=.o)))
OBJ := $(addprefix obj/,$(notdir $(SRC_C:.c=.o) $(SRC_S:.s=.o)) rp.o)
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
	$(CC) -flto=auto $(CFLAGS) -o $@ $(LDFLAGS) $(filter-out obj/bootloader.o,$^) $(LDLIBS)

CC_CMD = $(CC) $(CFLAGS) $(CPPFLAGS) -MMD -c -o $@ $<
RP_CC_CMD = $(CC) -flto $(CFLAGS) $(CPPFLAGS) -MMD -c -o $@ $<
RP_CXX_CMD = $(CXX) -flto $(CFLAGS) $(CPPFLAGS) -fno-exceptions -MMD -c -o $@ $<

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

obj/%.o: source/rp/%.c
	$(RP_CC_CMD) -Isource/ffmpeg -Isource/misc -Wall -Wextra

obj/%.o: source/misc/%.c
	$(RP_CC_CMD) -Isource/misc

obj/%.o: source/ffmpeg/libavcodec/%.c
	$(RP_CC_CMD) -Isource/ffmpeg

obj/%.o: source/ffmpeg/libavfilter/%.c
	$(RP_CC_CMD) -Isource/ffmpeg

obj/%.o: source/ffmpeg/libavutil/%.c
	$(RP_CC_CMD) -Isource/ffmpeg

obj/%.o: source/jpeg_ls/%.c
	$(RP_CC_CMD)

obj/%.o: source/jpeg_turbo/%.c
	$(RP_CC_CMD)

obj/%.o: source/imagezero/%.cpp
	$(RP_CXX_CMD)

obj/rp.o: $(RP_OBJ)
	$(CC) -flto $(CFLAGS) -r -nostdlib -o $@ $^

-include $(DEP)

.PHONY: clean all install

clean:
	-rm test.map $(PAYLOAD_LOCAL_BIN) $(PAYLOAD_LOCAL_ELF) obj/*.d obj/*.o
