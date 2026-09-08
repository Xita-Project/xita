PYTHON ?= python3
PROJECT := xita_dash
TITLE_ID := XITADASH1
TITLE := Xita Dashboard
LICENSE_FILES := ../LICENSE ../NOTICE ../THIRD_PARTY.md $(wildcard ../LICENSES/*.txt)
LICENSE_ARGS := $(foreach f,$(LICENSE_FILES),-a $(f)=$(patsubst ../%,%,$(f)))
VITASDK ?= $(HOME)/vitasdk
CC := $(VITASDK)/bin/arm-vita-eabi-gcc
HOST_CC := gcc
CFLAGS := -std=c11 -O2 -mthumb -Wall -Wextra
LIBS := -lSceAppMgr_stub -lSceCtrl_stub -lSceTouch_stub -lSceDisplay_stub \
        -lSceSysmem_stub -lSceIofilemgr_stub -lSceKernelThreadMgr_stub \
        -lSceProcessmgr_stub -lSceLibKernel_stub -lm

.PHONY: all clean host-test
all: $(PROJECT).vpk
build out:
	mkdir -p $@
license_text.h: ../LICENSE ../NOTICE ../tools/embed_license.py
	$(PYTHON) ../tools/embed_license.py
build/$(PROJECT).elf: xv_dash.c xv_dash.h font.h license_text.h main_standalone.c | build
	$(CC) $(CFLAGS) -Wl,-q -o $@ xv_dash.c main_standalone.c $(LIBS)
build/$(PROJECT).velf: build/$(PROJECT).elf
	$(VITASDK)/bin/vita-elf-create $< $@
build/eboot.bin: build/$(PROJECT).velf
	$(VITASDK)/bin/vita-make-fself -s $< $@
build/param.sfo: Makefile | build
	$(VITASDK)/bin/vita-mksfoex -s TITLE_ID=$(TITLE_ID) "$(TITLE)" $@
$(PROJECT).vpk: build/eboot.bin build/param.sfo ../sce_sys/icon0.png $(LICENSE_FILES)
	$(VITASDK)/bin/vita-pack-vpk -s build/param.sfo -b build/eboot.bin -a ../sce_sys/icon0.png=sce_sys/icon0.png $(LICENSE_ARGS) $@
build/host_test: xv_dash.c xv_dash.h font.h license_text.h host_test.c | build
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -o $@ xv_dash.c host_test.c -lm
host-test: build/host_test | out
	./build/host_test
clean:
	rm -rf build out $(PROJECT).vpk
