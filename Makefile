# ---------------------------------------------------------------------------
#  Xita — vitasdk Makefile
#
#  make RECOMP=1   build xita.vpk (the runtime; plain make builds the xita-skel mock) (ELF -> VELF -> eboot.bin -> VPK)
#  make shaders    (re)compile shaders/*.cg -> shaders/*.gxp   [needs psp2cgc]
#  make clean      remove build products
#
#  Requires $VITASDK to point at the toolchain (see ~/.zshrc) so that
#  arm-vita-eabi-gcc, vita-elf-create, vita-make-fself, vita-mksfoex and
#  vita-pack-vpk are on PATH.  Recipes use TAB indentation (required by make).
# ---------------------------------------------------------------------------

# --- 1. project identity -----------------------------------------------------
PROJECT   := xita-skel
TITLE_ID  := XITA00001
TITLE     := Xita

# --- toolchain ---------------------------------------------------------------
ifeq ($(VITASDK),)
$(error VITASDK is not set — export VITASDK=$$HOME/vitasdk and PATH=$$VITASDK/bin:$$PATH)
endif

PREFIX    := arm-vita-eabi
CC        := $(PREFIX)-gcc
STRIP     := $(PREFIX)-strip
SIZE      := $(PREFIX)-size

# --- 3. sources + flags ------------------------------------------------------
SRCS      := main.c xv_shader.c xv_d3d.c xv_scene.c xv_ui_gxm.c xv_log.c
BUILD     := build
OBJS      := $(patsubst %.c,$(BUILD)/%.o,$(SRCS))
DEPS      := $(OBJS:.o=.d)

# Generated at build time from the Stage 2b/3 shader manifest (see gen_layouts.py).
LAYOUTS_H := shaders/xv_layouts.h
LAYOUTS_SRC := shaders/halo_shaders.json

CFLAGS    := -O2 -mthumb -Wall -Wextra -Wno-unused-parameter -MMD -MP -I. -Ishaders
LDFLAGS   := -Wl,-q

# --- 4. linker stubs ---------------------------------------------------------
# The five hardware modules main.c talks to directly …
LIBS      := -lSceGxm_stub \
             -lSceDisplay_stub \
             -lSceKernelThreadMgr_stub \
             -lSceSysmem_stub \
             -lSceProcessmgr_stub
# … plus SceLibKernel (sceClibPrintf for XV_LOG) and libm (scene camera math).
LIBS      += -lSceLibKernel_stub -lSceTouch_stub -lm

# --- Stage 4: run the recompiled Halo engine instead of the mock game (make RECOMP=1) ---
# Swaps the runtime D3D HLE (xv_d3d.c/xv_scene.c) for the recompiled engine + kernel translator
# (recomp/, linked as librecomp.a) and the GXM UI bridge; see xv_boot.c / xv_ui_gxm.c.
RECOMP    ?= 0
ifeq ($(RECOMP),1)
CFLAGS    += -DXV_RUN_RECOMP -Irecomp -Irecomp/kernel
SRCS      := main.c xv_shader.c xv_d3d.c xv_ui_gxm.c xv_boot.c xv_log.c
OBJS      := $(patsubst %.c,$(BUILD)/%.o,$(SRCS))
DEPS      := $(OBJS:.o=.d)
RECOMP_LINK_LIB := $(BUILD)/recomp/librecomp.a
LIBS      += -lSceNet_stub -lSceNetCtl_stub -lScePspnetAdhoc_stub -lSceSysmodule_stub -lSceCommonDialog_stub
LIBS      += -lSceCtrl_stub -lSceRtc_stub -lSceIofilemgr_stub -lSceAudio_stub -lScePower_stub
PROJECT   := xita
SFO_EXTRA := -d ATTRIBUTE2=12      # extended memory mode: +109 MB for the arena/heap/texture pool
VPK       := $(PROJECT).vpk
endif

# --- 2. shader ingestion -----------------------------------------------------
# Stage 3 (shader_recomp_gen.py) writes .cg here; psp2cgc turns them into .gxp.
# The build looks for halo_shader_0.gxp explicitly and ships every .gxp found
# under app0:shaders/ inside the VPK, where the runtime loads them.
SHADER_DIR      := shaders
SHADER_PRIMARY  := $(SHADER_DIR)/halo_shader_0.gxp
SHADER_GXP      := $(sort $(SHADER_PRIMARY) $(wildcard $(SHADER_DIR)/*.gxp))
SHADER_CG       := $(wildcard $(SHADER_DIR)/*.cg)
PSP2CGC         := $(shell command -v psp2cgc 2>/dev/null)

# Only files that actually exist can be packed; missing ones become a warning
# rather than a broken build (psp2cgc is not part of vitasdk).
SHADER_PRESENT  := $(wildcard $(SHADER_GXP))
SHADER_MISSING  := $(filter-out $(SHADER_PRESENT),$(SHADER_GXP))
VPK_SHADER_ARGS := $(foreach s,$(SHADER_PRESENT),-a $(s)=$(s))

# Optional LiveArea assets (icon0.png, bg.png, startup.png, template.xml) and
# scene packs exported by halo_scene_export.py (assets/*.bin -> app0:assets/).
SCE_SYS_DIR     := sce_sys
SCE_SYS_FILES   := $(wildcard $(SCE_SYS_DIR)/*.png) $(wildcard $(SCE_SYS_DIR)/livearea/contents/*)
SCENE_FILES     := $(if $(filter 1,$(RECOMP)),,$(wildcard assets/*.bin))   # mock-only; the real-game (RECOMP) build never loads it
VPK_ASSET_ARGS  := $(foreach f,$(SCE_SYS_FILES) $(SCENE_FILES),-a $(f)=$(f))

# --- 5. packaging outputs ----------------------------------------------------
ELF       := $(BUILD)/$(PROJECT).elf
VELF      := $(BUILD)/$(PROJECT).velf
EBOOT     := $(BUILD)/eboot.bin
SFO       := $(BUILD)/param.sfo
VPK       := $(PROJECT).vpk

# ---------------------------------------------------------------------------
#  Rules
# ---------------------------------------------------------------------------
.PHONY: all shaders clean size
.DEFAULT_GOAL := all

all: $(VPK)

# generated layout tables (recompiled-shader attribute layouts) --------------------
# The XBE + Stage 1 manifest let the generator hash each Xbox function blob so the
# D3D HLE can recognise programs the game passes to CreateVertexShader().
XBE       ?= haloce/default.xbe
XBE_JSON  ?= game_manifest.json
$(LAYOUTS_H): $(LAYOUTS_SRC) gen_layouts.py shader_recomp_gen.py
	python3 gen_layouts.py $(LAYOUTS_SRC) $@ $(if $(wildcard $(XBE)),$(XBE) $(XBE_JSON))

$(BUILD)/main.o: $(LAYOUTS_H)

# compile ----------------------------------------------------------------------
$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	@mkdir -p $(BUILD)

# link -------------------------------------------------------------------------
$(ELF): $(OBJS) $(RECOMP_LINK_LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(RECOMP_LINK_LIB) $(LIBS)
	@$(SIZE) $@

# ELF -> VELF (resolves NIDs / import stubs) -> signed fself --------------------
$(VELF): $(ELF)
	vita-elf-create $< $@

$(EBOOT): $(VELF)
	vita-make-fself -s $< $@

$(SFO): Makefile | $(BUILD)
	vita-mksfoex -s TITLE_ID=$(TITLE_ID) $(SFO_EXTRA) "$(TITLE)" $@

# VPK: eboot + param.sfo + shaders/*.gxp (+ sce_sys assets when present) ------
$(VPK): $(EBOOT) $(SFO) $(SHADER_PRESENT) $(SCE_SYS_FILES) $(SCENE_FILES)
ifneq ($(SHADER_MISSING),)
	@echo "warning: shader(s) not found, VPK built without them: $(SHADER_MISSING)"
	@echo "         (run 'make shaders' with psp2cgc on PATH, or drop prebuilt .gxp files in $(SHADER_DIR)/)"
endif
	vita-pack-vpk -s $(SFO) -b $(EBOOT) $(VPK_SHADER_ARGS) $(VPK_ASSET_ARGS) $@
	@echo "built $@  (title $(TITLE_ID), shaders packed: $(if $(SHADER_PRESENT),$(SHADER_PRESENT),none))"

# shaders: .cg (Stage 3 output) -> .gxp ---------------------------------------
# Two back ends for the same Cg compiler:
#   * psp2cgc on PATH (official SDK)      -> compiled right here
#   * no psp2cgc                          -> compiled ON THE VITA by tools/shadercomp,
#     which drives the console's own libshacccg.suprx.  Push sources with
#     `make shaders-device VITA_IP=…`, pull results with `make shaders-pull VITA_IP=…`
#     (VitaShell FTP server, port 1337).
shaders: $(SHADER_CG:.cg=.gxp)

ifneq ($(PSP2CGC),)
$(SHADER_DIR)/%.gxp: $(SHADER_DIR)/%.cg
	$(PSP2CGC) -profile sce_vp_psp2 -O3 -o $@ $<
else
# No prerequisite on purpose: an existing .gxp is never "stale" here (it can only be
# rebuilt on the Vita); a missing one prints the device workflow and fails.
$(SHADER_DIR)/%.gxp:
	@echo "psp2cgc not available: $(@:.gxp=.cg) must be compiled on the Vita."
	@echo "  1. make shadercomp                 (builds tools/shadercomp/xv_shadercomp.vpk, install it once)"
	@echo "  2. make shaders-device VITA_IP=…    (uploads $(SHADER_DIR)/*.cg to ux0:data/xita/shaders/)"
	@echo "  3. run 'XV Shader Compiler' on the Vita (green screen = all compiled)"
	@echo "  4. make shaders-pull VITA_IP=…      (downloads the .gxp files back into $(SHADER_DIR)/)"
	@false
endif

VITA_IP        ?=
VITA_FTP_PORT  ?= 1337
DEVICE_SHADERS := ux0:/data/xita/shaders
SHADERCOMP_DIR := tools/shadercomp

.PHONY: shadercomp shaders-device shaders-pull

shadercomp:
	$(MAKE) -C $(SHADERCOMP_DIR)

define need_vita_ip
	@test -n "$(VITA_IP)" || { echo "set VITA_IP=<address shown in VitaShell FTP> (SELECT in VitaShell)"; exit 1; }
endef

shaders-device: $(SHADER_CG)
	$(need_vita_ip)
	@test -n "$(SHADER_CG)" || { echo "no .cg files in $(SHADER_DIR)/ — run shader_recomp_gen.py first"; exit 1; }
	@for f in $(SHADER_CG); do \
		echo "upload $$f"; \
		curl -sS --ftp-create-dirs -T "$$f" "ftp://$(VITA_IP):$(VITA_FTP_PORT)/$(DEVICE_SHADERS)/" || exit 1; \
	done
	@echo "uploaded $(words $(SHADER_CG)) shader(s); now run 'XV Shader Compiler' on the Vita, then: make shaders-pull VITA_IP=$(VITA_IP)"

# --- USB variant (VitaShell "USB device" mode exposes ux0: as a mass-storage volume) --
# Auto-detects the mount under /run/media or /media (a volume with app/ and data/);
# override with VITA_MOUNT=/path if needed.
VITA_MOUNT ?= $(firstword $(foreach m,$(wildcard /run/media/$(USER)/* /media/$(USER)/* /media/*),\
                 $(if $(and $(wildcard $(m)/app),$(wildcard $(m)/data)),$(m))))
SHACCCG    := libshacccg.suprx

.PHONY: shaders-usb shaders-pull-usb vita-eject

define need_vita_mount
	@test -n "$(VITA_MOUNT)" -a -d "$(VITA_MOUNT)/data" || { echo "Vita not mounted: enable USB device in VitaShell (SELECT), mount it (udisksctl mount -b /dev/sdX), or pass VITA_MOUNT=…"; exit 1; }
endef

shaders-usb: $(SHADER_CG)
	$(need_vita_mount)
	@test -n "$(SHADER_CG)" || { echo "no .cg files in $(SHADER_DIR)/ — run shader_recomp_gen.py first"; exit 1; }
	@mkdir -p "$(VITA_MOUNT)/data/xita/shaders"
	@if [ -f $(SHACCCG) ] && [ ! -f "$(VITA_MOUNT)/data/$(SHACCCG)" ]; then cp -v $(SHACCCG) "$(VITA_MOUNT)/data/"; fi
	@cp -v $(SHADER_CG) "$(VITA_MOUNT)/data/xita/shaders/"
	@if [ -f $(SHADERCOMP_DIR)/xv_shadercomp.vpk ]; then cp -v $(SHADERCOMP_DIR)/xv_shadercomp.vpk "$(VITA_MOUNT)/"; fi
	@sync
	@echo "staged on $(VITA_MOUNT): $(words $(SHADER_CG)) shader(s), $(SHACCCG), xv_shadercomp.vpk"
	@echo "next: make vita-eject, install ux0:/xv_shadercomp.vpk with VitaShell, run it, reconnect USB, make shaders-pull-usb"

# --- game deploy: VPK + the data the recompiled engine streams from ux0:data/xita/ ------
# Menu needs only ui.map (14 MB) + halo_image.bin (3.7 MB); add campaign/MP maps with
# DEPLOY_MAPS="ui.map a10.map bloodgulch.map".  Save data (cache files, profiles) is created
# by the game under ux0:data/xita/save/ on first boot; the log lands in xita.log there.
DEPLOY_MAPS   ?= ui.map
DEPLOY_IMAGE  := recomp/halo_image.bin
DEPLOY_MAPDIR := haloce/maps

.PHONY: deploy-usb deploy-ftp deploy-log-usb

deploy-usb: $(VPK)
	$(need_vita_mount)
	@mkdir -p "$(VITA_MOUNT)/data/xita/haloce/maps"
	@cp -v $(VPK) "$(VITA_MOUNT)/"
	@cp -v $(DEPLOY_IMAGE) "$(VITA_MOUNT)/data/xita/"
	@for m in $(DEPLOY_MAPS); do cp -v $(DEPLOY_MAPDIR)/$$m "$(VITA_MOUNT)/data/xita/haloce/maps/"; done
	@sync
	@echo "staged on $(VITA_MOUNT): $(VPK), halo_image.bin, maps: $(DEPLOY_MAPS)"
	@echo "next: make vita-eject; on the Vita install ux0:/$(VPK) with VitaShell (X on it), then launch Xita from LiveArea"

deploy-ftp: $(VPK)
	$(need_vita_ip)
	@curl -sS -T $(VPK) "ftp://$(VITA_IP):$(VITA_FTP_PORT)/ux0:/" || exit 1
	@curl -sS --ftp-create-dirs -T $(DEPLOY_IMAGE) "ftp://$(VITA_IP):$(VITA_FTP_PORT)/ux0:/data/xita/" || exit 1
	@for m in $(DEPLOY_MAPS); do echo "upload $$m"; curl -sS --ftp-create-dirs -T $(DEPLOY_MAPDIR)/$$m "ftp://$(VITA_IP):$(VITA_FTP_PORT)/ux0:/data/xita/haloce/maps/" || exit 1; done
	@echo "uploaded $(VPK) + data; on the Vita install ux0:/$(VPK) with VitaShell, then launch Xita"

# pull the on-device log after a run (USB): ux0:data/xita/xita.log -> ./vita_run.log
deploy-log-usb:
	$(need_vita_mount)
	@cp -v "$(VITA_MOUNT)/data/xita/xita.log" vita_run.log && tail -40 vita_run.log

shaders-pull-usb:
	$(need_vita_mount)
	@mkdir -p $(SHADER_DIR)
	@cp -v "$(VITA_MOUNT)"/data/xita/shaders/*.gxp $(SHADER_DIR)/ 2>/dev/null || { echo "no .gxp on the Vita yet — run 'XV Shader Compiler' first"; cat "$(VITA_MOUNT)/data/xita/shaders/compile.log" 2>/dev/null; exit 1; }
	@cp "$(VITA_MOUNT)/data/xita/shaders/compile.log" $(SHADER_DIR)/ 2>/dev/null || true
	@echo "--- compile.log ---"; cat $(SHADER_DIR)/compile.log 2>/dev/null || true
	@echo "pulled: $$(ls $(SHADER_DIR)/*.gxp | tr '\n' ' ') — run 'make' to pack them"

# --- Vita3K emulator variant (no console needed) ---------------------------------
# Requires ~/vita3k/ubuntu/Vita3K with firmware installed, libshacccg.suprx in the
# emulator's ur0:data/, and xv_shadercomp installed once (make vita3k-install-tools).
.PHONY: vita3k-install-tools vita3k-shaders vita3k-run

vita3k-install-tools: shadercomp
	tools/vita3k.sh install $(SHADERCOMP_DIR)/xv_shadercomp.vpk XVSC00001

vita3k-shaders:
	tools/vita3k.sh shaders $(SHADER_DIR)

vita3k-run: $(VPK)
	tools/vita3k.sh install $(VPK) $(TITLE_ID)
	tools/vita3k.sh run $(TITLE_ID) $(or $(SECS),45)

vita-eject:
	$(need_vita_mount)
	@sync && udisksctl unmount -b "$$(findmnt -rn -o SOURCE -T '$(VITA_MOUNT)')"
	@echo "safe to press X / disconnect USB on the Vita"

shaders-pull:
	$(need_vita_ip)
	@mkdir -p $(SHADER_DIR)
	@for f in $(SHADER_CG); do \
		g="$${f%.cg}.gxp"; echo "download $$g"; \
		curl -sS "ftp://$(VITA_IP):$(VITA_FTP_PORT)/$(DEVICE_SHADERS)/$$(basename $$g)" -o "$$g" || exit 1; \
	done
	@curl -sS "ftp://$(VITA_IP):$(VITA_FTP_PORT)/$(DEVICE_SHADERS)/compile.log" -o $(SHADER_DIR)/compile.log || true
	@echo "pulled $(words $(SHADER_CG)) .gxp file(s); run 'make' to pack them"

# utilities --------------------------------------------------------------------
size: $(ELF)
	$(SIZE) $(ELF)

clean:
	rm -rf $(BUILD) $(VPK)

-include $(DEPS)

# ---- Stage 4: recompiled game (ARM objects; linked into the runtime once the GXM bridge lands) ----
RECOMP_DIR   := recomp
RECOMP_BUILD := $(BUILD)/recomp
RECOMP_SRCS  := $(wildcard $(RECOMP_DIR)/code_*.c) $(RECOMP_DIR)/xv_fn_table.c $(RECOMP_DIR)/xv_stubs_default.c \
                $(RECOMP_DIR)/xv_x86rt.c $(RECOMP_DIR)/kernel/xk_mem.c $(RECOMP_DIR)/kernel/xk_rtl.c \
                $(RECOMP_DIR)/kernel/xk_file.c $(RECOMP_DIR)/kernel/xk_thread.c $(RECOMP_DIR)/kernel/xk_xapi.c $(RECOMP_DIR)/kernel/xk_net.c \
                $(RECOMP_DIR)/kernel/xd3d.c $(RECOMP_DIR)/kernel/xk_audio.c $(RECOMP_DIR)/kernel/xk_os_vita.c \
                $(RECOMP_DIR)/xv_trace_stub.c $(RECOMP_DIR)/xv_funchist.c
RECOMP_OBJS  := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(RECOMP_SRCS))
RECOMP_CFLAGS := -O2 -fno-strict-aliasing -mthumb -mcpu=cortex-a9 -mfpu=neon -w -std=gnu11 -I$(RECOMP_DIR) -I$(RECOMP_DIR)/kernel

# lifted code (code_*.c) only includes xv_recomp_protos.h -> xv_x86rt.h; the kernel/HLE objects use -MMD
# so a kernel header edit does not recompile the ~35 MB of generated code.
$(RECOMP_BUILD)/code_%.o: $(RECOMP_DIR)/code_%.c $(RECOMP_DIR)/xv_recomp_protos.h $(RECOMP_DIR)/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(CC) $(RECOMP_CFLAGS) -c $< -o $@
$(RECOMP_BUILD)/%.o: $(RECOMP_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(RECOMP_CFLAGS) -MMD -MP -c $< -o $@
-include $(RECOMP_OBJS:.o=.d)

$(RECOMP_BUILD)/librecomp.a: $(RECOMP_OBJS)
	$(PREFIX)-gcc-ar rcs $@ $^

recomp-lib: $(RECOMP_BUILD)/librecomp.a
	@$(PREFIX)-size -t $< | tail -1

.PHONY: recomp-lib
