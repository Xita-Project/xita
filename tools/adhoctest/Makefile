PROJECT := xita_adhoctest
TITLE_ID := XITAADH01
TITLE := Xita AdHoc Test
ifeq ($(VITASDK),)
$(error VITASDK is not set)
endif
PREFIX := arm-vita-eabi
CC := $(PREFIX)-gcc
BUILD := build
CFLAGS := -std=c11 -O2 -mthumb -Wall -Wextra -Werror
# Leave room between load segments for vita-elf-create import metadata.
LDFLAGS := -Wl,-q,--defsym=__sce_headroom=0x10000
LIBS := -lSceNet_stub -lSceNetCtl_stub -lScePspnetAdhoc_stub \
        -lSceNetAdhocMatching_stub -lSceSysmodule_stub -lSceCommonDialog_stub \
        -lSceGxm_stub -lSceCtrl_stub -lSceIofilemgr_stub -lSceDisplay_stub \
        -lSceSysmem_stub -lSceKernelThreadMgr_stub -lSceProcessmgr_stub -lSceLibKernel_stub
ELF := $(BUILD)/$(PROJECT).elf
VELF := $(BUILD)/$(PROJECT).velf
EBOOT := $(BUILD)/eboot.bin
SFO := $(BUILD)/sce_sys/param.sfo
VPK := $(PROJECT).vpk
.PHONY: all clean
all: $(VPK)
$(BUILD):
	mkdir -p $@
$(ELF): main.c screen.c screen.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ main.c screen.c $(LIBS)
$(VELF): $(ELF)
	vita-elf-create $< $@
$(EBOOT): $(VELF)
	vita-make-fself -s $< $@
$(SFO): Makefile sce_sys/README.md | $(BUILD)
	mkdir -p $(dir $@)
	vita-mksfoex -s TITLE_ID=$(TITLE_ID) -s APP_VER=01.01 "$(TITLE)" $@
$(VPK): $(EBOOT) $(SFO)
	vita-pack-vpk -s $(SFO) -b $(EBOOT) $@
clean:
	rm -rf $(BUILD) $(VPK)
