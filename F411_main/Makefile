PROJECT     = usfmcalc_f411
ARMCPU      = cortex-m4
STM32MCU    = STM32F411xE
STM32FAMILY = STM32F4
STM32FAMILY_ = $(shell echo $(STM32FAMILY) | tr '[:upper:]' '[:lower:]')

################################################################################
## kludge to handle directory names with spaces
SPACECHAR   =
SPACECHAR  +=
# From F411_main/ go up TWO levels to reach armprojects/
BASEPATH    = $(subst $(SPACECHAR),\,$(shell cd ../..;pwd))

include $(BASEPATH)/masterF4.mk

################################################################################
# masterF4.mk uses relative ../xxx paths assuming the project sits one level
# below armprojects/.  We are two levels below, so fix every such path with
# the absolute BASEPATH computed above.

OPENCM3BASE := $(BASEPATH)/libopencm3.my
OPENCM3INC  := $(OPENCM3BASE)/include
OPENCM3LD   := $(OPENCM3BASE)/lib

# Rebuild INCLUDE: local dir (pins, config, ld), shared root (kernel.h,
# usb_vcp.h), library
INCLUDE := -I. -I.. -I$(OPENCM3INC)

################################################################################
# Shared application sources live one level up (project root).
#
# OBJ must be recalculated because master.mk set it with := at parse time,
# before we replaced SRC.  The explicit $(BINDIR)/$(PROJECT).elf prerequisite
# line is needed because the link rule in master.mk also recorded its deps at
# parse time (old OBJ).

SRC  = $(wildcard ./*.c) ../usfmcalc.c ../usb_vcp.c

OBJ := $(addprefix $(OBJDIR)/,$(notdir $(SRC:.c=.o)))
OBJ += $(addprefix $(OBJDIR)/,$(notdir $(ASM:.s=.o)))

$(BINDIR)/$(PROJECT).elf: $(OBJ)

################################################################################

# startup.c provides reset_handler; skip toolchain crt0/crtbegin entirely
LDFLAGS += -nostartfiles
CFLAGS  += -O2

################################################################################
# Compile rule for shared sources that live in the parent directory

$(OBJDIR)/%.o: ../%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
