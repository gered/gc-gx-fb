ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>devkitPro")
endif
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

# ------------------------------------------------------------------------------
TARGET         := gc-gx-fb

# ------------------------------------------------------------------------------
# source file location(s), include/lib dirs, libraries to use
SRC_DIR        := src
INCLUDE_DIRS   :=

LIB_DIRS        = -L$(LIBOGC_LIB_DIR)
LIBS            = -ldb $(PLATFORM_LIBS)

# ------------------------------------------------------------------------------
# compiler/linker flags

INCLUDES        = -I$(DEVKITPRO)/libogc/include $(foreach dir,$(INCLUDE_DIRS),-I$(dir))

CFLAGS          = -g -O0 $(MACHDEP) $(INCLUDES)
CXXFLAGS        = $(CFLAGS)
AFLAGS          =
LDFLAGS         = -g $(MACHDEP) -Wl,-Map,$(notdir $@).map $(LIB_DIRS) $(LIBS)

# ------------------------------------------------------------------------------
# finding all source files to be compiled and where to place object files

SRC_FILES      := $(shell find $(SRC_DIR) -type f)
SRC_FILES      := $(filter $(addprefix %, .cpp .c .s), $(SRC_FILES))

OBJ_DIR        := obj
OBJ_FILES      := $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(SRC_FILES)))
OBJ_DIRS       := $(sort $(dir $(OBJ_FILES)))

DEPS_DIR       := $(OBJ_DIR)
DEPENDS        := $(OBJ_FILES:.o=.d)

# ------------------------------------------------------------------------------

.SUFFIXES:
.PHONY: clean run

all: $(TARGET).dol

$(TARGET).dol: $(TARGET).elf

$(TARGET).elf: $(OBJ_FILES)

clean:
	rm -f $(TARGET).dol
	rm -f $(TARGET).elf
	rm -f $(TARGET).elf.map
	rm -rf $(OBJ_DIR)

run: $(TARGET).dol
	$(DEVKITPRO)/tools/bin/wiiload $(TARGET).dol

# ------------------------------------------------------------------------------
# compiler/toolchain related stuff

ifeq ($(PLATFORM),cube)
MACHDEP         = -DGEKKO -mogc -mcpu=750 -meabi -mhard-float
LIBOGC_LIB_DIR  = $(DEVKITPRO)/libogc/lib/cube
PLATFORM_LIBS   = -lfat -logc -lm
else
MACHDEP         = -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float
LIBOGC_LIB_DIR  = $(DEVKITPRO)/libogc/lib/wii
PLATFORM_LIBS   = -lwiiuse -lbte -lfat -logc -lm
endif

CC             := $(DEVKITPPC)/bin/powerpc-eabi-gcc
CXX            := $(DEVKITPPC)/bin/powerpc-eabi-g++
AS             := $(DEVKITPPC)/bin/powerpc-eabi-as
LD             := $(DEVKITPPC)/bin/powerpc-eabi-gcc
OBJCOPY        := $(DEVKITPPC)/bin/powerpc-eabi-objcopy
ELF2DOL        := $(DEVKITPRO)/tools/bin/elf2dol
BIN2S          := $(DEVKITPRO)/tools/bin/bin2s

# ------------------------------------------------------------------------------
# standard compilation for the supported source file types, and binary linking

$(OBJ_DIR)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) -MMD -MP -MF $(DEPS_DIR)/$*.d $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -MMD -MP -MF $(DEPS_DIR)/$*.d $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.s.o: %s
	@mkdir -p $(dir $@)
	$(CC) -MMD -MP -MF $(DEPS_DIR)/$*.d -x assembler-with-cpp $(AFLAGS) -c$< -o $@

%.elf:
	$(LD) $^ $(LDFLAGS) -o $@

%.dol: %.elf
	$(ELF2DOL) $< $@

# ------------------------------------------------------------------------------

-include $(DEPENDS)
