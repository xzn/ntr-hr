DEV_BIN_DIR := $(DEVKITARM)/bin

CC_NAME = @echo $(notdir $@);
CC = $(CC_NAME) $(DEV_BIN_DIR)/arm-none-eabi-gcc
CXX := $(DEV_BIN_DIR)/arm-none-eabi-g++
OBJCOPY := $(DEV_BIN_DIR)/arm-none-eabi-objcopy
LD := $(DEV_BIN_DIR)/arm-none-eabi-ld
CP := cp

CTRU_DIR := libctru/libctru

CFLAGS := -Ofast -g -march=armv6k -mtune=mpcore -mfloat-abi=hard -fno-strict-aliasing -ffunction-sections -fdata-sections
CPPFLAGS := -Iinclude -Ilibctru/libctru/include
LDFLAGS = -pie -Wl,--gc-sections -Wl,-Map=$(basename $(notdir $@)).map,-z,noexecstack
LDLIBS = -nostartfiles -L. -lctru_ntr -L$(LIB_RS_DIR)

SRC_C := $(wildcard source/*.c)
SRC_S := $(wildcard source/*.s)

SRC_BOOT_C := $(wildcard source/boot/*.c)
OBJ_BOOT := $(addprefix obj/,$(notdir $(SRC_BOOT_C:.c=.o)))

SRC_MENU_C := $(wildcard source/menu/*.c)
OBJ_MENU := $(addprefix obj/,$(notdir $(SRC_MENU_C:.c=.o)))

SRC_PM_C := $(wildcard source/pm/*.c)
OBJ_PM := $(addprefix obj/,$(notdir $(SRC_PM_C:.c=.o)))

SRC_GAME_C := $(wildcard source/game/*.c)
OBJ_GAME := $(addprefix obj/,$(notdir $(SRC_GAME_C:.c=.o)))

SRC_NWM_C := $(wildcard source/nwm/*.c)
OBJ_NWM := $(addprefix obj/,$(notdir $(SRC_NWM_C:.c=.o)))

SRC_RP_C += $(wildcard source/jpeg/*.c)
OBJ_RP := $(addprefix obj/,$(notdir $(SRC_RP_C:.c=.o)))

OBJ := $(addprefix obj/,$(notdir $(SRC_C:.c=.o) $(SRC_S:.s=.o)))
DEP := $(OBJ:.o=.d) $(OBJ_BOOT:.o=.d) $(OBJ_MENU:.o=.d) $(OBJ_PM:.o=.d) $(OBJ_GAME:.o=.d) $(OBJ_NWM:.o=.d) $(OBJ_RP:.o=.d)

NTR_BIN_BOOT := ntr.hr.boot.bin
NTR_BIN_MENU := ntr.hr.menu.bin
NTR_BIN_PM := ntr.hr.pm.bin
NTR_BIN_NWM := ntr.hr.nwm.bin
NTR_BIN_GAME := ntr.hr.game.bin

LIB_RS_DIR := target/armv6k-nintendo-3ds/release
LIB_NWM_RS := $(LIB_RS_DIR)/libnwm_rs.a

PAYLOAD_BIN := $(NTR_BIN_BOOT) $(NTR_BIN_MENU) $(NTR_BIN_PM) $(NTR_BIN_NWM) $(NTR_BIN_GAME)
PAYLOAD_TARGET_DIR := ../BootNTR-Bins/romfs
PAYLOAD_TARGET_BIN := $(addprefix $(PAYLOAD_TARGET_DIR)/,$(PAYLOAD_BIN))

PAYLOAD_LOCAL_BIN := $(addprefix release/,$(PAYLOAD_BIN))
PAYLOAD_LOCAL_ELF := $(addprefix bin/,$(PAYLOAD_BIN:.bin=.elf))

all: $(PAYLOAD_LOCAL_BIN) $(PAYLOAD_LOCAL_ELF)

install: $(PAYLOAD_TARGET_BIN)

.NOTPARALLEL: rs

rs: $(LIB_NWM_RS)

CP_CMD = @echo \* $(notdir $@) \*; $(CP) $< $@

$(PAYLOAD_TARGET_DIR)/%.bin: release/%.bin
	$(CP_CMD)

release/%.bin: bin/%.elf | release
	$(CC_NAME) $(OBJCOPY) -O binary $< $@ -S

release:
	mkdir $@

bin/$(NTR_BIN_BOOT:.bin=.elf): $(OBJ) $(OBJ_BOOT) libctru_ntr.a 3ds.ld
	$(CC) -flto=auto $(CFLAGS) -o $@ -T 3ds.ld $(LDFLAGS) $(filter-out obj/bootloader.o,$(OBJ) $(OBJ_BOOT)) $(LDLIBS)

bin/$(NTR_BIN_MENU:.bin=.elf): $(OBJ) $(OBJ_MENU) libctru_ntr.a 3ds.ld
	$(CC) -flto=auto $(CFLAGS) -o $@ -T 3ds.ld $(LDFLAGS) $(filter-out obj/bootloader.o,$(OBJ) $(OBJ_MENU)) $(LDLIBS)

bin/$(NTR_BIN_PM:.bin=.elf): $(OBJ) $(OBJ_PM) libctru_ntr.a 3ds.ld
	$(CC) -flto=auto $(CFLAGS) -o $@ -T 3ds.ld $(LDFLAGS) $(filter-out obj/bootloader.o,$(OBJ) $(OBJ_PM)) $(LDLIBS)

bin/$(NTR_BIN_GAME:.bin=.elf): $(OBJ) $(OBJ_GAME) libctru_ntr.a 3ds.ld
	$(CC) -flto=auto $(CFLAGS) -o $@ -T 3ds.ld $(LDFLAGS) $(filter-out obj/bootloader.o,$(OBJ) $(OBJ_GAME)) $(LDLIBS)

bin/$(NTR_BIN_NWM:.bin=.elf): $(OBJ) obj/rp_lto.o libctru_ntr.a 3dst.ld $(LIB_NWM_RS)
	$(CC) -flto=auto $(CFLAGS) -o $@ -T 3dst.ld $(LDFLAGS) $(filter-out obj/bootloader.o,$(OBJ) obj/rp_lto.o) $(LDLIBS) -lnwm_rs

$(LIB_NWM_RS): source/nwm_rs
	cargo -Z unstable-options -C $< build --release

libctru_ntr.a: $(CTRU_DIR)/lib/libctru.a
	$(CP_CMD)

$(CTRU_DIR)/lib/libctru.a:
	$(MAKE) -C $(CTRU_DIR) lib/libctru.a

CC_WARNS = -Wall -Wextra

CC_CMD = $(CC) $(CFLAGS) $(CPPFLAGS) -MMD -c -o $@ $< $(CC_WARNS)
RP_CC_CMD = $(CC) -flto $(CFLAGS) $(CPPFLAGS) -MMD -c -o $@ $< $(CC_WARNS) -Iinclude/jpeg

obj/%.o: source/%.s
	$(CC_CMD)

obj/%.o: source/%.c
	$(CC_CMD)

obj/%.o: source/boot/%.c
	$(CC_CMD)

obj/%.o: source/menu/%.c
	$(CC_CMD) -DNTR_BIN_PM=\"$(NTR_BIN_PM)\" -DNTR_BIN_NWM=\"$(NTR_BIN_NWM)\"

obj/%.o: source/pm/%.c
	$(CC_CMD) -DNTR_BIN_GAME=\"$(NTR_BIN_GAME)\"

obj/%.o: source/game/%.c
	$(CC_CMD)

obj/%.o: source/nwm/%.c
	$(RP_CC_CMD)

obj/%.o: source/jpeg/%.c
	$(RP_CC_CMD) -Wno-attribute-alias -Wno-unused-variable -Wno-unused-parameter -Wno-unused-but-set-variable

obj/rp_lto.o: $(OBJ_NWM) $(OBJ_RP)
	$(CC) -flto $(CFLAGS) -r -o $@ $^

-include $(DEP)

.PHONY: clean all install

clean:
	-rm *.map bin/*.elf release/*.bin obj/*.d obj/*.o lib*.a
	-rm target/ -rf
	$(MAKE) -C $(CTRU_DIR) clean
