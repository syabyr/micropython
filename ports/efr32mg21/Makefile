BOARD ?= efr32mg21_devboard

ifeq ($(wildcard boards/$(BOARD)/.),)
$(error Invalid BOARD specified)
endif

FROZEN_MANIFEST ?= boards/manifest.py

include boards/$(BOARD)/mpconfigboard.mk

BUILD ?= build-$(BOARD)

include ../../py/mkenv.mk

# Default to building a cross compiler
CROSS ?= 1

# qstr definitions (must come before including py.mk)
QSTR_DEFS = qstrdefsport.h

# include py core make definitions
include $(TOP)/py/py.mk

ifeq ($(CROSS), 1)
CROSS_COMPILE = arm-none-eabi-
endif

# Silicon Labs SDK layout.
#
# The emlib + device headers + CMSIS come from the simplicity-sdk.  The
# simplicity-sdk does not ship a bare-metal GCC linker script, so the
# startup file and linker script are taken from lib/efr32-base (which is a
# GCC-ready subset of the same family of SDKs).
SIMPLICITY_SDK ?= ../../lib/simplicity-sdk
EFR32_BASE ?= ../../lib/efr32-base

EFM_FAMILY_UC := $(shell echo '$(EFM_FAMILY)' | tr '[:lower:]' '[:upper:]')
SDK_DEV_DIR    := $(SIMPLICITY_SDK)/platform/Device/SiliconLabs/$(EFM_FAMILY_UC)
SDK_EMLIB_SRC  := $(SIMPLICITY_SDK)/platform/emlib/src
SDK_EMLIB_INC  := $(SIMPLICITY_SDK)/platform/emlib/inc
SDK_COMMON_INC := $(SIMPLICITY_SDK)/platform/common/inc
SDK_COMMON_SRC := $(SIMPLICITY_SDK)/platform/common/src
SDK_COMMON_CFG := $(SIMPLICITY_SDK)/platform/common/config
SDK_CMSIS_INC  := $(SIMPLICITY_SDK)/platform/CMSIS/Core/Include

INC += -I.
INC += -I$(SDK_EMLIB_INC)
INC += -I$(SDK_COMMON_INC)
INC += -I$(SDK_COMMON_CFG)
INC += -I$(SDK_DEV_DIR)/Include
INC += -I$(SDK_CMSIS_INC)
INC += -D__STARTUP_CLEAR_BSS
INC += -D__STACK_SIZE=0x1000
INC += -D__HEAP_SIZE=0x6000
# Run entirely in secure state (no TrustZone partition); also selects the
# *_S_BASE peripheral addresses and skips the CMSE-dependent SMU config.
INC += -DSL_TRUSTZONE_SECURE
# The newer CMSIS headers define __cmsis_start() (which redeclares the
# __copy_table_* symbols with a conflicting type vs. the legacy GCC startup
# in lib/efr32-base). Defining __PROGRAM_START suppresses that inline helper.
INC += -D__PROGRAM_START=_start

INC += -I$(TOP)
INC += -I$(BUILD)
INC += -Iboards/$(BOARD)
INC += -Irail

ifeq ($(CROSS), 1)
DFU = $(TOP)/tools/dfu.py
PYDFU = $(TOP)/tools/pydfu.py
CFLAGS_CORTEX_M33 = \
	-mthumb \
	-mcpu=cortex-m33 \
	-mfloat-abi=hard \
	-mfpu=fpv5-sp-d16 \
	-fsingle-precision-constant \
	-Wdouble-promotion \
	-D$(EFM_DEV) \
	-falign-functions=2 \

LIBM	:= $(shell $(CROSS_COMPILE)gcc $(CFLAGS_$(MCU_SERIES)) --print-file-name=libm.a)
LIBGCC	:= $(shell $(CROSS_COMPILE)gcc $(CFLAGS_$(MCU_SERIES)) --print-libgcc)

# Prebuilt RAIL library (Series 2, efr32xg21).  Supplies the RAIL API and
# all radio interrupt handlers; the app provides the 802.15.4 PHY config.
RAIL_LIB = rail/librail_efr32xg21_gcc_release.a

LIBS += $(LIBM) $(LIBGCC) $(RAIL_LIB)

LINKER_SCRIPT = $(EFR32_BASE)/device/$(EFM_FAMILY_UC)/Source/GCC/$(EFM_FAMILY).ld

CFLAGS = $(INC) -g -Wall -Werror -std=gnu99 -nostdlib $(CFLAGS_$(MCU_SERIES)) $(COPT)
LDFLAGS = -nostdlib -T $(LINKER_SCRIPT) -Map=$@.map --cref --gc-sections
else
LD = gcc
CFLAGS = -m32 $(INC) -Wall -Werror -std=gnu99 $(COPT)
LDFLAGS = -m32 -Wl,-Map=$@.map,--cref -Wl,--gc-sections
endif

CSUPEROPT = -Os # save some code space

# Tune for Debugging or Optimization
ifeq ($(DEBUG), 1)
CFLAGS += -O0 -ggdb
else
CFLAGS += -Os -DNDEBUG
CFLAGS += -fdata-sections -ffunction-sections
endif

# ieee802154_efr32xg21_configurator_out.c is NOT built: the RAIL
# library's internal configurator data (rfhal_standard_phys.o weak
# symbols) provides the IEEE 802.15.4 2.4 GHz standard PHY directly.
SRC_C = \
	main.c \
	uart_core.c \
	pin.c \
	spi.c \
	i2c.c \
	modmachine.c \
	modutime.c \
	radio.c \

PYLIB_SRC_C = \
	shared/libc/printf.c \
	shared/runtime/pyexec.c \
	shared/runtime/gchelper_generic.c \
	shared/libc/string0.c \
	shared/readline/readline.c \
	shared/runtime/sys_stdio_mphal.c \
	shared/runtime/interrupt_char.c \

PYMOD_SRC_C = \
	extmod/machine_mem.c \
	extmod/modos.c \
	extmod/vfs.c \
	extmod/vfs_reader.c \
	extmod/vfs_blockdev.c \
	extmod/vfs_lfs.c \
	lib/littlefs/lfs2.c \
	lib/littlefs/lfs2_util.c \
	machine_pin.c \
	machine_spi.c \
	machine_i2c.c \
	machine_spiflash.c \

# Scan all of the source, python libraries and python module files for QSTR
SRC_QSTR += $(SRC_C)
SRC_QSTR += $(PYLIB_SRC_C)
SRC_QSTR += $(PYMOD_SRC_C)

# EMLIB from Silicon Labs.  The .c files live outside this tree, so expose
# them to the pattern rule via vpath and reference them by bare filename.
vpath %.c $(EFR32_BASE)/device/$(EFM_FAMILY_UC)/Source/GCC
vpath %.c $(SDK_DEV_DIR)/Source
vpath %.c $(SDK_EMLIB_SRC)
vpath %.c $(SDK_COMMON_SRC)

# These are not included in the QSTR scan
LIB_C += \
	startup_$(EFM_FAMILY).c \
	system_$(EFM_FAMILY).c \
	em_usart.c \
	em_cmu.c \
	em_gpio.c \
	em_i2c.c \
	em_system.c \
	em_emu.c \
	sl_core_cortexm.c \

OBJ = $(PY_O) \
	$(addprefix $(BUILD)/, $(SRC_C:.c=.o)) \
	$(addprefix $(BUILD)/, $(PYLIB_SRC_C:.c=.o)) \
	$(addprefix $(BUILD)/, $(PYMOD_SRC_C:.c=.o)) \
	$(addprefix $(BUILD)/, $(LIB_C:.c=.o)) \

CFLAGS += $(CFLAGS_MOD)

# littlefs2 config for a bare-metal (malloc-less) build: MicroPython supplies
# lfs_malloc/lfs_free in extmod/vfs_lfs.c.
CFLAGS += -DLFS2_NO_MALLOC -DLFS2_NO_DEBUG -DLFS2_NO_WARN -DLFS2_NO_ERROR -DLFS2_NO_ASSERT

all: $(BUILD)/firmware.elf

MPY_TOOL_BIN = $(TOP)/tools/mpy-tool.py
MPY_TOOL = $(PYTHON) $(MPY_TOOL_BIN) -mlongint-impl=mpz

$(BUILD)/firmware.elf: $(OBJ)
	$(ECHO) "LINK $@"
	$(Q)$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)
	$(Q)$(SIZE) $@

$(BUILD)/firmware.bin: $(BUILD)/firmware.elf
	$(Q)$(OBJCOPY) -O binary $^ $(BUILD)/firmware.bin

flash:
	JLinkExe -NoGui 1 -ExitOnError 1 -AutoConnect 1 -If SWD -Speed 5000 -Device EFR32MG21B010F1024IM32 -CommandFile flash.jlink

include $(TOP)/py/mkrules.mk
