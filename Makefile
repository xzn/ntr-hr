DEV_BIN_DIR := $(DEVKITARM)/bin

CC := $(DEV_BIN_DIR)/arm-none-eabi-gcc
CXX := $(DEV_BIN_DIR)/arm-none-eabi-g++
OBJCOPY := $(DEV_BIN_DIR)/arm-none-eabi-objcopy
LD := $(DEV_BIN_DIR)/arm-none-eabi-ld
CP := cp

CFLAGS := -Ofast -g -march=armv6k -mtune=mpcore -mfloat-abi=hard -fno-strict-aliasing
CPPFLAGS := -Iinclude
LDFLAGS := -pie -Wl,--gc-sections -T 3ds.ld -Wl,-Map=test.map
LDLIBS := -lc -lm -lgcc -nostdlib

SRC_C := $(wildcard source/dsp/*.c) $(wildcard source/*.c) $(wildcard source/libctru/*.c)
SRC_S := $(wildcard source/*.s) $(wildcard source/libctru/*.s)
NS_SRC_C += $(wildcard source/ns/*.c) $(wildcard source/jpeg/*.c)
NS_OBJ := $(addprefix obj/,$(notdir $(NS_SRC_C:.c=.o)))
OBJ := $(addprefix obj/,$(notdir $(SRC_C:.c=.o) $(SRC_S:.s=.o)) ns_lto.o)
DEP := $(OBJ:.o=.d) $(NS_OBJ:.o=.d)

PAYLOAD_BIN_NAME := ntr.n3ds.bin
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

$(PAYLOAD_LOCAL_BIN): $(PAYLOAD_LOCAL_ELF) | release
	$(OBJCOPY) -O binary $< $@ -S

release:
	mkdir $@

$(PAYLOAD_LOCAL_ELF): $(OBJ)
	$(CC) -flto=auto $(CFLAGS) -o $@ $(LDFLAGS) $(filter-out obj/bootloader.o,$^) $(LDLIBS)

CC_WARNS = -Wall -Wextra

CC_CMD = $(CC) $(CFLAGS) $(CPPFLAGS) -MMD -c -o $@ $< $(CC_WARNS)
NS_CC_CMD = $(CC) -flto $(CFLAGS) $(CPPFLAGS) -MMD -c -o $@ $< $(CC_WARNS) -Iinclude/jpeg

obj/%.o: source/%.s
	$(CC_CMD)

obj/%.o: source/libctru/%.s
	$(CC_CMD)

obj/%.o: source/dsp/%.c
	$(CC_CMD)

obj/%.o: source/%.c
	$(CC_CMD)

obj/%.o: source/libctru/%.c
	$(CC_CMD)

obj/%.o: source/ns/%.c
	$(NS_CC_CMD)

obj/%.o: source/jpeg/%.c
	$(NS_CC_CMD) -Wno-attribute-alias -Wno-unused-variable -Wno-unused-parameter -Wno-unused-but-set-variable

obj/ns_lto.o: $(NS_OBJ)
	$(CC) -flto $(CFLAGS) -r -o $@ $^

-include $(DEP)

.PHONY: clean all install

clean:
	-rm test.map $(PAYLOAD_LOCAL_BIN) $(PAYLOAD_LOCAL_ELF) obj/*.d obj/*.o
