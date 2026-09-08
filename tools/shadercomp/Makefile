# xv_shadercomp — on-device Cg -> GXP compiler (see runtime/main.c)
#   make            -> xv_shadercomp.vpk   (install with VitaShell, run once)

PROJECT   := xv_shadercomp
TITLE_ID  := XVSC00001
TITLE     := XV Shader Compiler

ifeq ($(VITASDK),)
$(error VITASDK is not set)
endif

PREFIX    := arm-vita-eabi
CC        := $(PREFIX)-gcc
BUILD     := build

CFLAGS    := -O2 -mthumb -Wall -Wextra -Wno-unused-parameter
LDFLAGS   := -Wl,-q

# SceShaccCg (libshacccg.suprx) is driven directly; the module is loaded with
# sceKernelLoadStartModule (SceLibKernel).  No vitaShaRK / SceShaccCgExt patches.
LIBS      := -lSceShaccCg_stub \
             -lSceIofilemgr_stub -lSceDisplay_stub -lSceSysmem_stub \
             -lSceKernelThreadMgr_stub -lSceProcessmgr_stub -lSceLibKernel_stub

ELF   := $(BUILD)/$(PROJECT).elf
VELF  := $(BUILD)/$(PROJECT).velf
EBOOT := $(BUILD)/eboot.bin
SFO   := $(BUILD)/param.sfo
VPK   := $(PROJECT).vpk

.PHONY: all clean

all: $(VPK)

$(BUILD):
	@mkdir -p $(BUILD)

$(ELF): runtime/main.c | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)

$(VELF): $(ELF)
	vita-elf-create $< $@

$(EBOOT): $(VELF)
	vita-make-fself -s $< $@

$(SFO): | $(BUILD)
	vita-mksfoex -s TITLE_ID=$(TITLE_ID) "$(TITLE)" $@

$(VPK): $(EBOOT) $(SFO)
	vita-pack-vpk -s $(SFO) -b $(EBOOT) $@
	@echo "built $@ — install via VitaShell, then: make -C ../.. shaders-device VITA_IP=<vita ip>"

clean:
	rm -rf $(BUILD) $(VPK)
