# Need 2026-08-02 nightly rust for now

DEV_BIN_DIR := $(DEVKITARM)/bin
UNAME := $(shell uname)

# USE_CLANG = 1
# USE_LTO = 1

# When compiling with clang libclang_rt.builtins-arm.a will need to be obtained elsewhere.
# See https://llvm.org/docs/HowToCrossCompileBuiltinsOnArm.html

CC_NAME = @echo $(notdir $@);
AS = $(CC_NAME) $(DEV_BIN_DIR)/arm-none-eabi-as
CLANG_FLAGS = -target arm-none-eabi
CLANG_FLAGS += --sysroot $(DEVKITARM)/arm-none-eabi
CLANG_FLAGS += -Wno-c2x-extensions -Wno-reserved-user-defined-literal

ifeq ($(USE_CLANG),1)
CC = $(CC_NAME) clang $(CLANG_FLAGS)
CXX = $(CC_NAME) clang++ $(CLANG_FLAGS)
RSFLAGS = RUSTFLAGS="-C panic=abort -Clinker-plugin-lto"
else
CC = $(CC_NAME) $(DEV_BIN_DIR)/arm-none-eabi-gcc
CXX = $(CC_NAME) $(DEV_BIN_DIR)/arm-none-eabi-g++
RSFLAGS = RUSTFLAGS="-C panic=abort"
endif

ifeq ($(USE_LTO),1)
LTO = -flto
else
LTO =
endif

OBJCOPY = $(DEV_BIN_DIR)/arm-none-eabi-objcopy
LD = $(DEV_BIN_DIR)/arm-none-eabi-ld
CP = cp

CTRU_DIR := libctru/libctru

CFLAGS := -O3 -ffast-math -g -march=armv6k -mtune=mpcore -mfloat-abi=hard -mfpu=vfp -mtp=soft -fno-strict-aliasing -fshort-enums
CFLAGS += -ffunction-sections -fdata-sections
CPPFLAGS := -Iinclude -Ilibctru/libctru/include -D__3DS__ -Wno-comment
LDFLAGS = -Wl,--gc-sections -Wl,-Map=$(basename $(notdir $@)).map,-z,notext,-z,noexecstack -L. -L$(DEVKITARM)/arm-none-eabi/lib/armv6k/fpu
ifeq ($(USE_CLANG),1)
LDFLAGS += -fuse-ld=lld
endif
LDLIBS := -lctru_ntr -lsysbase
LDLIBS += -Wl,-pie
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

SRC_NWM_MISC_O3DS_C :=
SRC_NWM_C := $(wildcard source/nwm/*.c) $(SRC_NWM_MISC_O3DS_C)
OBJ_NWM := $(addprefix obj/,$(notdir $(SRC_NWM_C:.c=.o)))
OBJ_NWM_O3DS := $(addprefix obj/,$(notdir $(SRC_NWM_C:.c=.o3ds.o)))

SRC_NWM_MISC_C += $(filter-out $(SRC_NWM_MISC_O3DS_C),$(wildcard source/nwm_misc/*.c))
SRC_NWM_MISC_X := $(wildcard source/nwm_misc/*.cpp)
OBJ_NWM_MISC := $(addprefix obj/,$(notdir $(SRC_NWM_MISC_C:.c=.o)))
OBJ_NWM_MISC += $(addprefix obj/,$(notdir $(SRC_NWM_MISC_X:.cpp=.o)))

OBJ := $(addprefix obj/,$(notdir $(SRC_C:.c=.o) $(SRC_S:.s=.o)))
DEP := $(OBJ:.o=.d) $(OBJ_BOOT:.o=.d) $(OBJ_MENU:.o=.d) $(OBJ_PM:.o=.d) $(OBJ_GAME:.o=.d) $(OBJ_NWM:.o=.d) $(OBJ_NWM_O3DS:.o=.d) $(OBJ_NWM_MISC:.o=.d)

NTR_BIN_BOOT := ntr.hr.boot.bin
NTR_BIN_MENU := ntr.hr.menu.bin
NTR_BIN_PM := ntr.hr.pm.bin
NTR_BIN_NWM := ntr.hr.nwm.bin
NTR_BIN_NWM_O3DS := ntr.hr.nwm.o3ds.bin
NTR_BIN_NWM_MEM3 := ntr.hr.nwm.mem3.bin
NTR_BIN_GAME := ntr.hr.game.bin

LIB_RS_DIR := target/armv6k-nintendo-3ds/release
LIB_NWM_RS := $(LIB_RS_DIR)/libnwm_rs.a

LIB_RS_DIR_O3DS := target-o3ds/armv6k-nintendo-3ds/release
LIB_NWM_RS_O3DS := $(LIB_RS_DIR_O3DS)/libnwm_rs.a

LIB_RS_DIR_MEM3 := target-mem3/armv6k-nintendo-3ds/release
LIB_NWM_RS_MEM3 := $(LIB_RS_DIR_MEM3)/libnwm_rs.a

PAYLOAD_BIN := $(NTR_BIN_BOOT) $(NTR_BIN_MENU) $(NTR_BIN_PM) $(NTR_BIN_NWM) $(NTR_BIN_NWM_O3DS) $(NTR_BIN_NWM_MEM3) $(NTR_BIN_GAME)
PAYLOAD_TARGET_DIR := ../BootNTR-Bins/romfs
PAYLOAD_TARGET_BIN := $(addprefix $(PAYLOAD_TARGET_DIR)/,$(PAYLOAD_BIN))

PAYLOAD_LOCAL_BIN := $(addprefix release/,$(PAYLOAD_BIN))
PAYLOAD_LOCAL_ELF := $(addprefix bin/,$(PAYLOAD_BIN:.bin=.elf))

all: $(PAYLOAD_LOCAL_BIN) $(PAYLOAD_LOCAL_ELF)

install: $(PAYLOAD_TARGET_BIN)

.NOTPARALLEL: rs

rs: $(LIB_NWM_RS) $(LIB_NWM_RS_O3DS) $(LIB_NWM_RS_MEM3)

CP_CMD = @echo \* $(notdir $@) \*; $(CP) $< $@

$(PAYLOAD_TARGET_DIR)/%.bin: release/%.bin
	$(CP_CMD)

release/%.bin: bin/%.elf | release
	$(CC_NAME) $(OBJCOPY) -O binary $< $@ -S

release:
	mkdir $@

bin/$(NTR_BIN_BOOT:.bin=.elf): $(OBJ) $(OBJ_BOOT) libctru_ntr.a 3ds.ld | bin
	$(CC) -flto=auto $(CFLAGS) -o $@ -T 3ds.ld $(LDFLAGS) $(OBJ) $(OBJ_BOOT) $(LDLIBS)

bin/$(NTR_BIN_MENU:.bin=.elf): $(OBJ) $(OBJ_MENU) libctru_ntr.a 3ds.ld | bin
	$(CC) -flto=auto $(CFLAGS) -o $@ -T 3ds.ld $(LDFLAGS) $(OBJ) $(OBJ_MENU) $(LDLIBS)

bin/$(NTR_BIN_PM:.bin=.elf): $(OBJ) $(OBJ_PM) libctru_ntr.a 3ds.ld | bin
	$(CC) -flto=auto $(CFLAGS) -o $@ -T 3ds.ld $(LDFLAGS) $(OBJ) $(OBJ_PM) $(LDLIBS)

bin/$(NTR_BIN_GAME:.bin=.elf): $(OBJ) $(OBJ_GAME) libctru_ntr.a 3ds.ld | bin
	$(CC) -flto=auto $(CFLAGS) -o $@ -T 3ds.ld $(LDFLAGS) $(OBJ) $(OBJ_GAME) $(LDLIBS)

bin/$(NTR_BIN_NWM:.bin=.elf): $(OBJ) $(OBJ_NWM) $(OBJ_NWM_MISC) libctru_ntr.a 3dst.ld $(LIB_NWM_RS) | bin
	$(CC) -flto=auto $(CFLAGS) -o $@ -T 3dst.ld $(LDFLAGS) $(OBJ) $(OBJ_NWM) $(OBJ_NWM_MISC) $(LDLIBS) -L$(LIB_RS_DIR) -lnwm_rs -lm

bin/$(NTR_BIN_NWM_O3DS:.bin=.elf): $(OBJ) $(OBJ_NWM_O3DS) libctru_ntr.a 3dst.ld $(LIB_NWM_RS_O3DS) | bin
	$(CC) -flto=auto $(CFLAGS) -o $@ -T 3dst.ld $(LDFLAGS) $(OBJ) $(OBJ_NWM_O3DS) $(LDLIBS) -L$(LIB_RS_DIR_O3DS) -lnwm_rs -lm

bin/$(NTR_BIN_NWM_MEM3:.bin=.elf): $(OBJ) $(OBJ_NWM_O3DS) libctru_ntr.a 3dst.ld $(LIB_NWM_RS_MEM3) | bin
	$(CC) -flto=auto $(CFLAGS) -o $@ -T 3dst.ld $(LDFLAGS) $(OBJ) $(OBJ_NWM_O3DS) $(LDLIBS) -L$(LIB_RS_DIR_MEM3) -lnwm_rs -lm

bin:
	mkdir $@

$(LIB_NWM_RS): $(shell find source/nwm_rs -type f) $(shell find . -name '*.h' -type f)
	$(RSFLAGS) cargo -Z unstable-options -C source/nwm_rs build --target-dir $(shell realpath target) --release

$(LIB_NWM_RS_O3DS): $(shell find source/nwm_rs -type f) $(shell find . -name '*.h' -type f)
	$(RSFLAGS) cargo -Z unstable-options -C source/nwm_rs build --target-dir $(shell realpath target-o3ds) --features o3ds --release

$(LIB_NWM_RS_MEM3): $(shell find source/nwm_rs -type f) $(shell find . -name '*.h' -type f)
	$(RSFLAGS) cargo -Z unstable-options -C source/nwm_rs build --target-dir $(shell realpath target-mem3) --features mem3,o3ds --release

libctru_ntr.a: $(CTRU_DIR)/lib/libctru.a
	$(CP_CMD)

$(CTRU_DIR)/lib/libctru.a:
	$(MAKE) -C $(CTRU_DIR) lib/libctru.a

CC_WARNS = -Wall -Wextra

CC_CMD = $(CC) $(LTO) $(CFLAGS) $(CPPFLAGS) -MMD -c -o $@ $< $(CC_WARNS)
NWM_CC_CMD = $(CC) $(LTO) $(CFLAGS) $(CPPFLAGS) -MMD -c -o $@ $< $(CC_WARNS)
NWM_CXX_CMD = $(CXX) $(LTO) $(CFLAGS) $(CPPFLAGS) -fno-exceptions -MMD -c -o $@ $< $(CC_WARNS) -Wno-implicit-fallthrough

obj/%.o: source/%.s | obj
	$(AS) -march=armv6k -mfloat-abi=hard -o $@ $<

obj/%.o: source/%.c | obj
	$(CC_CMD)

obj/%.o: source/boot/%.c | obj
	$(CC_CMD)

obj/%.o: source/menu/%.c | obj
	$(CC_CMD) -DNTR_BIN_PM=\"$(NTR_BIN_PM)\" -DNTR_BIN_NWM=\"$(NTR_BIN_NWM)\" -DNTR_BIN_NWM_O3DS=\"$(NTR_BIN_NWM_O3DS)\" -DNTR_BIN_NWM_MEM3=\"$(NTR_BIN_NWM_MEM3)\"

obj/%.o: source/pm/%.c | obj
	$(CC_CMD) -DNTR_BIN_GAME=\"$(NTR_BIN_GAME)\"

obj/%.o: source/game/%.c | obj
	$(CC_CMD)

obj/%.o: source/nwm/%.c | obj
	$(NWM_CC_CMD) -DNEW_3DS

obj/%.o3ds.o: source/nwm/%.c | obj
	$(NWM_CC_CMD) -DOLD_3DS

obj/%.o: source/nwm_misc/%.c | obj
	$(NWM_CC_CMD) -DNEW_3DS

obj/%.o3ds.o: source/nwm_misc/%.c | obj
	$(NWM_CC_CMD) -DOLD_3DS

obj/%.o: source/nwm_misc/%.cpp | obj
	$(NWM_CXX_CMD)

obj:
	mkdir $@

-include $(DEP)

.PHONY: clean all install rs

clean:
	-rm *.map bin/* release/* obj/* libctru_ntr.a
	-rm target/ -rf
	-rm target-o3ds/ -rf
	-rm target-mem3/ -rf
	$(MAKE) -C $(CTRU_DIR) clean
