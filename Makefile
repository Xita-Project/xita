PYTHON ?= python3
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
SRCS      := runtime/main.c runtime/xv_shader.c runtime/xv_d3d.c runtime/xv_scene.c runtime/xv_ui_gxm.c runtime/xv_log.c runtime/xv_benchmark.c runtime/xv_cpu.c runtime/xv_texture_worker.c runtime/xv_geometry_worker.c runtime/xv_gpu_upload.c runtime/xv_vertex_upload.c runtime/xv_vertex_prepare.c runtime/xv_upload_worker.c runtime/xv_draw_profile.c runtime/xv_render_profile.c
BUILD     := build
OBJS      := $(patsubst %.c,$(BUILD)/%.o,$(SRCS))
DEPS      := $(OBJS:.o=.d)

# Generated at build time from the Stage 2b/3 shader manifest (see recompiler/gen_layouts.py).
LAYOUTS_H := shaders/xv_layouts.h
LAYOUTS_SRC := shaders/halo_shaders.json

CFLAGS    := -O2 -mthumb -Wall -Wextra -Wno-unused-parameter -MMD -MP -I. -Iruntime -Ishaders
# Explicit research build; ordinary builds omit the writer. A separately
# selected startup default preserves the environment/config opt-out.
ifeq ($(XV_PROFILE_ASYNC_REPORT),1)
CFLAGS += -DXV_PROFILE_ASYNC_REPORT
ifeq ($(XV_PROFILE_ASYNC_REPORT_DEFAULT),1)
CFLAGS += -DXV_PROFILE_ASYNC_REPORT_DEFAULT=1
endif
endif
.PHONY: force-async-report-config
force-async-report-config:
$(BUILD)/async-report.config: force-async-report-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(if $(filter 1,$(XV_PROFILE_ASYNC_REPORT)),1,0)' '$(if $(filter 1,$(XV_PROFILE_ASYNC_REPORT_DEFAULT)),1,0)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/xv_log.o: $(BUILD)/async-report.config
# Pump-only diagnostic. Track both transitions so an incremental ordinary
# build cannot accidentally retain its additional notification clock reads.
ifeq ($(XV_GPU_PACKET_TIMING),1)
$(BUILD)/runtime/main.o: CFLAGS += -DXV_GPU_PACKET_TIMING=1
endif
.PHONY: force-gpu-packet-config
force-gpu-packet-config:
$(BUILD)/gpu-packet.config: force-gpu-packet-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(if $(filter 1,$(XV_GPU_PACKET_TIMING)),1,0)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/main.o: $(BUILD)/gpu-packet.config
# Optional read-only continuation proof. Default OFF; both compile transitions
# rebuild every owner of the guarded interface.
ifeq ($(XV_DEPTH_STORE),1)
ifneq ($(RECOMP),1)
$(error XV_DEPTH_STORE requires RECOMP=1)
endif
$(BUILD)/runtime/main.o $(BUILD)/runtime/xv_d3d.o $(BUILD)/runtime/xv_shader.o $(BUILD)/runtime/xv_ui_gxm.o: CFLAGS += -DXV_DEPTH_STORE
endif
.PHONY: force-depth-store-config
force-depth-store-config:
$(BUILD)/depth-store.config: force-depth-store-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(if $(filter 1,$(XV_DEPTH_STORE)),1,0)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/main.o $(BUILD)/runtime/xv_d3d.o $(BUILD)/runtime/xv_shader.o $(BUILD)/runtime/xv_ui_gxm.o: $(BUILD)/depth-store.config
# Existing-scene exact query completion candidate; compiled and runtime OFF by default.
XV_QUERY_BOUNDARY_DEFAULT ?= 0
ifneq ($(words $(XV_QUERY_BOUNDARY_DEFAULT)),1)
$(error XV_QUERY_BOUNDARY_DEFAULT must be 0 or 1)
endif
ifneq ($(filter $(XV_QUERY_BOUNDARY_DEFAULT),0 1),$(XV_QUERY_BOUNDARY_DEFAULT))
$(error XV_QUERY_BOUNDARY_DEFAULT must be 0 or 1)
endif
ifeq ($(XV_QUERY_BOUNDARY),1)
ifneq ($(RECOMP),1)
$(error XV_QUERY_BOUNDARY requires RECOMP=1)
endif
$(BUILD)/runtime/main.o $(BUILD)/runtime/xv_d3d.o: CFLAGS += -DXV_QUERY_BOUNDARY
$(BUILD)/runtime/main.o: CFLAGS += -DXV_QUERY_BOUNDARY_DEFAULT=$(XV_QUERY_BOUNDARY_DEFAULT)
endif
.PHONY: force-query-boundary-config force-query-boundary-startup-config
force-query-boundary-config:
force-query-boundary-startup-config:
$(BUILD)/query-boundary.config: force-query-boundary-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(if $(filter 1,$(XV_QUERY_BOUNDARY)),1,0)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/main.o $(BUILD)/runtime/xv_d3d.o: $(BUILD)/query-boundary.config
# Startup selection belongs only to main; no guest or replay unit changes.
# An absent feature ignores the default and keeps incremental OFF builds idle.
$(BUILD)/query-boundary-startup.config: force-query-boundary-startup-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(if $(filter 1,$(XV_QUERY_BOUNDARY)),$(XV_QUERY_BOUNDARY_DEFAULT),0)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/main.o: $(BUILD)/query-boundary-startup.config
# Pump-side sealed-list census; no fences or scheduling changes. Default OFF.
ifeq ($(XV_VISIBILITY_PLACEMENT),1)
$(BUILD)/runtime/xv_d3d.o: CFLAGS += -DXV_VISIBILITY_PLACEMENT
endif
.PHONY: force-visibility-placement-config
force-visibility-placement-config:
$(BUILD)/visibility-placement.config: force-visibility-placement-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(if $(filter 1,$(XV_VISIBILITY_PLACEMENT)),1,0)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/xv_d3d.o: $(BUILD)/visibility-placement.config
# Reserve room for vita-elf-create's module/import metadata before the next
# load segment. Traced builds can otherwise end too close to its boundary.
LDFLAGS   := -Wl,-q,--defsym=__sce_headroom=0x1000

# --- 4. linker stubs ---------------------------------------------------------
# The five hardware modules runtime/main.c talks to directly …
LIBS      := -lSceGxm_stub \
             -lSceDisplay_stub \
             -lSceKernelThreadMgr_stub \
             -lSceSysmem_stub \
             -lSceProcessmgr_stub
# … plus SceLibKernel (sceClibPrintf for XV_LOG) and libm (scene camera math).
LIBS      += -lSceLibKernel_stub -lSceTouch_stub -lm

# --- Stage 4: run the recompiled Halo engine instead of the mock game (make RECOMP=1) ---
# Swaps the runtime D3D HLE (runtime/xv_d3d.c/xv_scene.c) for the recompiled engine + kernel translator
# (recomp/, linked as librecomp.a) and the GXM UI bridge; see runtime/xv_boot.c / runtime/xv_ui_gxm.c.
RECOMP    ?= 0
GAME_PROFILE ?= halo_ce_3925
ifeq ($(wildcard games/$(GAME_PROFILE)/runtime.mk),)
$(error No native runtime adapter for GAME_PROFILE=$(GAME_PROFILE))
endif
include games/$(GAME_PROFILE)/runtime.mk
ifeq ($(RECOMP),1)
CFLAGS    += -DXV_RUN_RECOMP -Irecomp -Irecomp/kernel
SRCS      := runtime/main.c runtime/xv_shader.c runtime/xv_d3d.c runtime/xv_ui_gxm.c runtime/xv_boot.c runtime/xv_log.c runtime/xv_benchmark.c runtime/xv_cpu.c runtime/xv_texture_worker.c runtime/xv_geometry_worker.c runtime/xv_gpu_upload.c runtime/xv_vertex_upload.c runtime/xv_vertex_prepare.c runtime/xv_upload_worker.c runtime/xv_draw_profile.c runtime/xv_render_profile.c runtime/xv_settings.c dashboard/xv_dash.c
SRCS      += runtime/xv_remote.c runtime/xv_update.c runtime/xv_sha256.c
OBJS      := $(patsubst %.c,$(BUILD)/%.o,$(SRCS))
DEPS      := $(OBJS:.o=.d)
# Select the reviewed native adapter independently of generated game objects.
RECOMP_LINK_LIB = $(BUILD)/recomp/libxita_sys.a $(BUILD)/recomp/libxita_game.a $(BUILD)/recomp/libxita_guest.a
# Strong implementations must be retained even when generated declarations are
# weak. Otherwise a static archive can silently leave the compatibility stub.
RECOMP_LINK_FLAGS = -Wl,--whole-archive $(RECOMP_BUILD)/libxita_sys.a $(RECOMP_BUILD)/libxita_game.a -Wl,--no-whole-archive $(RECOMP_BUILD)/libxita_guest.a
LIBS      += -lSceNet_stub -lSceNetCtl_stub -lScePspnetAdhoc_stub -lSceSysmodule_stub -lSceCommonDialog_stub
LIBS      += -lSceAppMgr_stub -lSceCtrl_stub -lSceRtc_stub -lSceIofilemgr_stub -lSceAudio_stub -lScePower_stub
PROJECT   := xita
SFO_EXTRA := -d ATTRIBUTE2=12      # extended memory mode: +109 MB for the arena/heap/texture pool
VPK       := $(PROJECT).vpk
endif

# --- 2. shader ingestion -----------------------------------------------------
# Stage 3 (recompiler/shader_recomp_gen.py) writes .cg here; psp2cgc turns them into .gxp.
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
# scene packs exported by recompiler/halo_scene_export.py (assets/*.bin -> app0:assets/).
SCE_SYS_DIR     := sce_sys
SCE_SYS_FILES   := $(wildcard $(SCE_SYS_DIR)/*.png) $(wildcard $(SCE_SYS_DIR)/livearea/contents/*)
SCENE_FILES     := $(if $(filter 1,$(RECOMP)),,$(wildcard assets/*.bin))   # mock-only; the real-game (RECOMP) build never loads it
LICENSE_FILES  := LICENSE NOTICE THIRD_PARTY.md $(wildcard LICENSES/*.txt)
VPK_ASSET_ARGS  := $(foreach f,$(SCE_SYS_FILES) $(SCENE_FILES) $(LICENSE_FILES),-a $(f)=$(f))

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

dashboard/license_text.h: LICENSE NOTICE tools/embed_license.py
	$(PYTHON) tools/embed_license.py
$(BUILD)/dashboard/xv_dash.o: dashboard/license_text.h

# generated layout tables (recompiled-shader attribute layouts) --------------------
# The XBE + Stage 1 manifest let the generator hash each Xbox function blob so the
# D3D HLE can recognise programs the game passes to CreateVertexShader().
XBE       ?= haloce/default.xbe
XBE_JSON  ?= local/halo_ce_3925/game_manifest.json
$(LAYOUTS_H): Makefile $(LAYOUTS_SRC) recompiler/gen_layouts.py recompiler/shader_recomp_gen.py $(if $(filter 1,$(RECOMP)),$(XBE) $(XBE_JSON))
	$(PYTHON) recompiler/gen_layouts.py $(LAYOUTS_SRC) $@ $(if $(wildcard $(XBE)),$(XBE) $(XBE_JSON))

$(BUILD)/runtime/main.o: $(LAYOUTS_H)

HUD_GXP := $(foreach h,A972FE61 5D70F0B3 EB818129,shaders/ps_$(h)_1D.frag.gxp)
shaders/xv_hud_gxp.h: tools/embed_hud_shaders.py $(HUD_GXP)
	$(PYTHON) tools/embed_hud_shaders.py $@
$(BUILD)/runtime/xv_shader.o: shaders/xv_hud_gxp.h
shaders/xv_vs_gxp.h: tools/embed_vertex_shaders.py tools/test_vertex_varyings.py shaders/xv_layouts.h $(wildcard shaders/halo_vs_*.gxp shaders/halo_vs_*.cg)
	$(PYTHON) tools/test_vertex_varyings.py
	$(PYTHON) tools/embed_vertex_shaders.py $@
$(BUILD)/runtime/xv_shader.o: shaders/xv_vs_gxp.h
shaders/xv_ps_gxp.h: tools/embed_ps_shaders.py shaders/xv_ps_table.h $(wildcard shaders/ps_*.gxp) shaders/xv_color.frag.gxp shaders/xv_texmod.frag.gxp shaders/xv_tex0.frag.gxp shaders/xv_lm.frag.gxp
	$(PYTHON) tools/embed_ps_shaders.py $@
$(BUILD)/runtime/xv_shader.o: shaders/xv_ps_gxp.h

# compile ----------------------------------------------------------------------
$(BUILD)/%.o: %.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	@mkdir -p $(BUILD)

# link -------------------------------------------------------------------------
$(ELF): $(OBJS) $(RECOMP_LINK_LIB) Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(RECOMP_LINK_FLAGS) $(LIBS)
ifeq ($(RECOMP),1)
	@$(PREFIX)-nm $@ | awk '$$2 == "T" { strong[$$3] = 1 } END { if (!strong["xv_hle_HaloBuildVisibleIndices"] || !strong["xv_hle_HaloSignSavedRecord"]) { print "required Halo HLE resolved to a compatibility stub"; exit 1 } }'
endif
	@$(SIZE) $@

# ELF -> VELF (resolves NIDs / import stubs) -> signed fself --------------------
$(VELF): $(ELF)
	vita-elf-create $< $@

$(EBOOT): $(VELF)
	vita-make-fself -s $< $@

$(SFO): Makefile | $(BUILD)
	vita-mksfoex -s TITLE_ID=$(TITLE_ID) $(SFO_EXTRA) "$(TITLE)" $@

# Stable updater boot helper; generated game code is never linked into it.
$(BUILD)/update-launcher.elf: runtime/xv_update_launcher.c runtime/xv_update.c runtime/xv_sha256.c runtime/xv_update.h runtime/xv_sha256.h Makefile
	@mkdir -p $(BUILD)
	$(CC) -O2 -mthumb -Wall -Wextra -Iruntime $(LDFLAGS) -o $@ runtime/xv_update_launcher.c runtime/xv_update.c runtime/xv_sha256.c -lSceAppMgr_stub -lSceIofilemgr_stub -lScePower_stub -lSceProcessmgr_stub -lSceKernelThreadMgr_stub -lSceLibKernel_stub
$(BUILD)/update-launcher.velf: $(BUILD)/update-launcher.elf
	vita-elf-create $< $@
$(BUILD)/update-launcher.self: $(BUILD)/update-launcher.velf
	# Only this fixed-path helper needs app-directory write access. The game stays safe.
	vita-make-fself $< $@
ifeq ($(RECOMP),1)
UPDATE_LAUNCHER := $(BUILD)/update-launcher.self
endif

# VPK: eboot + param.sfo + shaders/*.gxp (+ sce_sys assets when present) ------
$(VPK): $(EBOOT) $(SFO) $(SHADER_PRESENT) $(SCE_SYS_FILES) $(SCENE_FILES) $(LICENSE_FILES) $(UPDATE_LAUNCHER) tools/package_vpk.py
ifneq ($(SHADER_MISSING),)
	@echo "warning: shader(s) not found, VPK built without them: $(SHADER_MISSING)"
	@echo "         (run 'make shaders' with psp2cgc on PATH, or drop prebuilt .gxp files in $(SHADER_DIR)/)"
endif
ifeq ($(RECOMP),1)
	$(PYTHON) tools/package_vpk.py --root . --eboot $(EBOOT) --sfo $(SFO) --launcher $(UPDATE_LAUNCHER) --output $@
else
	vita-pack-vpk -s $(SFO) -b $(EBOOT) $(VPK_SHADER_ARGS) $(VPK_ASSET_ARGS) $@
endif
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
	@test -n "$(SHADER_CG)" || { echo "no .cg files in $(SHADER_DIR)/ — run recompiler/shader_recomp_gen.py first"; exit 1; }
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
	@test -n "$(SHADER_CG)" || { echo "no .cg files in $(SHADER_DIR)/ — run recompiler/shader_recomp_gen.py first"; exit 1; }
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
XITA_GUEST_SRCS := $(wildcard $(RECOMP_DIR)/code_*.c) $(RECOMP_DIR)/xv_fn_table.c $(RECOMP_DIR)/xv_stubs_default.c
XITA_SYS_SRCS := $(RECOMP_DIR)/xv_x86rt.c $(RECOMP_DIR)/kernel/xk_mem.c $(RECOMP_DIR)/kernel/xk_rtl.c \
                 $(RECOMP_DIR)/kernel/xk_file.c $(RECOMP_DIR)/kernel/xk_thread.c $(RECOMP_DIR)/kernel/xk_xapi.c $(RECOMP_DIR)/kernel/xk_net.c \
                 $(RECOMP_DIR)/kernel/xd3d.c $(RECOMP_DIR)/kernel/xk_diag_poll.c $(RECOMP_DIR)/kernel/xk_audio.c $(RECOMP_DIR)/kernel/xk_crypto.c $(RECOMP_DIR)/kernel/xk_os_vita.c \
                 $(RECOMP_DIR)/xv_trace_stub.c $(RECOMP_DIR)/xv_funchist.c $(RECOMP_DIR)/xv_phase.c
RECOMP_SRCS := $(XITA_GUEST_SRCS) $(XITA_SYS_SRCS) $(XITA_GAME_SRCS)
RECOMP_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(RECOMP_SRCS))
XITA_GUEST_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(XITA_GUEST_SRCS))
XITA_SYS_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(XITA_SYS_SRCS))
XITA_GAME_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(XITA_GAME_SRCS))
RECOMP_CFLAGS := -O2 -fno-strict-aliasing -mthumb -mcpu=cortex-a9 -mfpu=neon -w -std=gnu11 -I. -Iruntime -I$(RECOMP_DIR) -I$(RECOMP_DIR)/kernel
ifeq ($(XV_NATIVE_BSP_SPHERE),1)
RECOMP_CFLAGS += -DXV_NATIVE_BSP_SPHERE
endif
ifeq ($(XV_NATIVE_COLLISION_VERTICES),1)
RECOMP_CFLAGS += -DXV_NATIVE_COLLISION_VERTICES
CFLAGS += -DXV_NATIVE_COLLISION_VERTICES
endif
XV_NATIVE_COLLISION_VERTICES_DEFAULT ?= 0
ifneq ($(words $(XV_NATIVE_COLLISION_VERTICES_DEFAULT)),1)
$(error XV_NATIVE_COLLISION_VERTICES_DEFAULT must be 0 or 1)
endif
ifneq ($(filter $(XV_NATIVE_COLLISION_VERTICES_DEFAULT),0 1),$(XV_NATIVE_COLLISION_VERTICES_DEFAULT))
$(error XV_NATIVE_COLLISION_VERTICES_DEFAULT must be 0 or 1)
endif
$(RECOMP_BUILD)/kernel/xk_collision_vertices_control.o: RECOMP_CFLAGS += -DXV_NATIVE_COLLISION_VERTICES_DEFAULT=$(XV_NATIVE_COLLISION_VERTICES_DEFAULT)
COLLISION_VERTEX_HOOK_SRCS := $(shell grep -l XV_NATIVE_COLLISION_VERTICES $(XITA_GUEST_SRCS) 2>/dev/null)
COLLISION_VERTEX_HOOK_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(COLLISION_VERTEX_HOOK_SRCS))
.PHONY: force-collision-vertices-config
force-collision-vertices-config:
$(RECOMP_BUILD)/collision-vertices.config: force-collision-vertices-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(if $(filter 1,$(XV_NATIVE_COLLISION_VERTICES)),1,0) $(XV_NATIVE_COLLISION_VERTICES_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(COLLISION_VERTEX_HOOK_OBJS): recomp/kernel/xk_collision_vertices.h
$(COLLISION_VERTEX_HOOK_OBJS) $(RECOMP_BUILD)/kernel/xk_collision_vertices_control.o: $(RECOMP_BUILD)/collision-vertices.config
$(BUILD)/runtime/main.o: $(RECOMP_BUILD)/collision-vertices.config
$(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/librecomp.a: $(RECOMP_BUILD)/collision-vertices.config
ifeq ($(XV_NATIVE_SEGMENT_SPHERE),1)
RECOMP_CFLAGS += -DXV_NATIVE_SEGMENT_SPHERE
CFLAGS += -DXV_NATIVE_SEGMENT_SPHERE
endif
XV_NATIVE_SEGMENT_SPHERE_DEFAULT ?= 0
ifneq ($(words $(XV_NATIVE_SEGMENT_SPHERE_DEFAULT)),1)
$(error XV_NATIVE_SEGMENT_SPHERE_DEFAULT must be 0 or 1)
endif
ifneq ($(filter $(XV_NATIVE_SEGMENT_SPHERE_DEFAULT),0 1),$(XV_NATIVE_SEGMENT_SPHERE_DEFAULT))
$(error XV_NATIVE_SEGMENT_SPHERE_DEFAULT must be 0 or 1)
endif
SEGMENT_SPHERE_HOOK_SRCS := $(shell grep -l XV_NATIVE_SEGMENT_SPHERE $(XITA_GUEST_SRCS) 2>/dev/null)
SEGMENT_SPHERE_HOOK_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(SEGMENT_SPHERE_HOOK_SRCS))
.PHONY: force-segment-sphere-config force-segment-sphere-startup-config
force-segment-sphere-config:
force-segment-sphere-startup-config:
$(RECOMP_BUILD)/segment-sphere.config: force-segment-sphere-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(if $(filter 1,$(XV_NATIVE_SEGMENT_SPHERE)),1,0)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/segment-sphere-startup.config: force-segment-sphere-startup-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_NATIVE_SEGMENT_SPHERE_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(SEGMENT_SPHERE_HOOK_OBJS) $(RECOMP_BUILD)/kernel/xk_segment_sphere_control.o: recomp/kernel/xk_segment_sphere.h
$(SEGMENT_SPHERE_HOOK_OBJS) $(RECOMP_BUILD)/kernel/xk_segment_sphere_control.o $(BUILD)/runtime/main.o: $(RECOMP_BUILD)/segment-sphere.config
$(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/librecomp.a: $(RECOMP_BUILD)/segment-sphere.config
$(RECOMP_BUILD)/kernel/xk_segment_sphere_control.o: $(RECOMP_BUILD)/segment-sphere-startup.config
$(RECOMP_BUILD)/kernel/xk_segment_sphere_control.o: RECOMP_CFLAGS += -DXV_NATIVE_SEGMENT_SPHERE_DEFAULT=$(XV_NATIVE_SEGMENT_SPHERE_DEFAULT)
ifeq ($(XV_NATIVE_MODEL_PALETTE),1)
RECOMP_CFLAGS += -DXV_NATIVE_MODEL_PALETTE
CFLAGS += -DXV_NATIVE_MODEL_PALETTE
endif
ifeq ($(XV_NATIVE_MODEL_HIERARCHY),1)
RECOMP_CFLAGS += -DXV_NATIVE_MODEL_HIERARCHY
endif
ifeq ($(XV_NATIVE_OBJECT_BASIS),1)
RECOMP_CFLAGS += -DXV_NATIVE_OBJECT_BASIS
CFLAGS += -DXV_NATIVE_OBJECT_BASIS
endif
ifeq ($(XV_NATIVE_OBJECT_SCAN),1)
RECOMP_CFLAGS += -DXV_NATIVE_OBJECT_SCAN
CFLAGS += -DXV_NATIVE_OBJECT_SCAN
endif
ifeq ($(XV_NATIVE_OBJECT_COLLECT),1)
RECOMP_CFLAGS += -DXV_NATIVE_OBJECT_COLLECT
CFLAGS += -DXV_NATIVE_OBJECT_COLLECT
endif
XV_NATIVE_OBJECT_COLLECT_DEFAULT ?= 0
ifneq ($(words $(XV_NATIVE_OBJECT_COLLECT_DEFAULT)),1)
$(error XV_NATIVE_OBJECT_COLLECT_DEFAULT must be 0 or 1)
endif
ifneq ($(filter $(XV_NATIVE_OBJECT_COLLECT_DEFAULT),0 1),$(XV_NATIVE_OBJECT_COLLECT_DEFAULT))
$(error XV_NATIVE_OBJECT_COLLECT_DEFAULT must be 0 or 1)
endif
# Track this optional mode even during incremental builds. Archive membership
# and the generated hook must change together when the feature is toggled.
COLLECT_HOOK_SRCS := $(shell grep -l XV_NATIVE_OBJECT_COLLECT $(XITA_GUEST_SRCS) 2>/dev/null)
COLLECT_HOOK_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(COLLECT_HOOK_SRCS))
.PHONY: force-object-collect-config
force-object-collect-config:
$(RECOMP_BUILD)/object-collect.config: force-object-collect-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(if $(filter 1,$(XV_NATIVE_OBJECT_COLLECT)),1,0)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(COLLECT_HOOK_OBJS) $(RECOMP_BUILD)/kernel/xk_object_collect.o $(BUILD)/runtime/main.o $(BUILD)/runtime/xv_ui_gxm.o: $(RECOMP_BUILD)/object-collect.config
$(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/librecomp.a: $(RECOMP_BUILD)/object-collect.config
# Startup selection is consumed only by this helper. Changing it must not
# rebuild the large generated units or change their qualified compile flags.
.PHONY: force-object-collect-startup-config
force-object-collect-startup-config:
$(RECOMP_BUILD)/object-collect-startup.config: force-object-collect-startup-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_NATIVE_OBJECT_COLLECT_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_object_collect.o: $(RECOMP_BUILD)/object-collect-startup.config
$(RECOMP_BUILD)/kernel/xk_object_collect.o: RECOMP_CFLAGS += -ffp-contract=off -DXV_NATIVE_OBJECT_COLLECT_DEFAULT=$(XV_NATIVE_OBJECT_COLLECT_DEFAULT)
ifeq ($(XV_QUAT_CACHE),1)
RECOMP_CFLAGS += -DXV_QUAT_CACHE
endif
ifeq ($(XV_GUEST_AFFINITY),1)
CFLAGS += -DXV_GUEST_AFFINITY
endif
ifeq ($(XV_VERTEX_PROFILE),1)
$(BUILD)/runtime/xv_vertex_upload.o: CFLAGS += -DXV_VERTEX_PROFILE=1
endif
ifeq ($(XV_FLARE_QUERY_OVERLAP),1)
RECOMP_CFLAGS += -DXV_FLARE_QUERY_OVERLAP
CFLAGS += -DXV_FLARE_QUERY_OVERLAP
endif

# Native replacements must retain the lifted multiply/add rounding points.
ifeq ($(XV_EXPERIMENTAL_OBJECT_JOBS),1)
RECOMP_CFLAGS += -DXV_EXPERIMENTAL_OBJECT_JOBS
CFLAGS += -DXV_EXPERIMENTAL_OBJECT_JOBS
endif

# Guard-retained C query adapter. Ordinary builds contain no adapter/state.
ifeq ($(XV_WORKER_QUERY),1)
ifneq ($(XV_EXPERIMENTAL_OBJECT_JOBS),1)
$(error XV_WORKER_QUERY requires XV_EXPERIMENTAL_OBJECT_JOBS=1)
endif
RECOMP_CFLAGS += -DXV_WORKER_QUERY
endif
ifeq ($(XV_TYPED_CLUSTER_QUERY),1)
ifneq ($(XV_WORKER_QUERY),1)
$(error XV_TYPED_CLUSTER_QUERY requires XV_WORKER_QUERY=1)
endif
RECOMP_CFLAGS += -DXV_TYPED_CLUSTER_QUERY
endif
.PHONY: force-worker-query-config
force-worker-query-config:
$(RECOMP_BUILD)/worker-query.config: force-worker-query-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(if $(filter 1,$(XV_WORKER_QUERY)),1,0)/$(if $(filter 1,$(XV_TYPED_CLUSTER_QUERY)),1,0)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(XITA_GUEST_OBJS) $(XITA_GAME_OBJS) $(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/librecomp.a: $(RECOMP_BUILD)/worker-query.config
$(RECOMP_BUILD)/kernel/xk_worker_query.o: recomp/kernel/xk_worker_query_generated.inc
$(RECOMP_BUILD)/kernel/xk_worker_query.o: RECOMP_CFLAGS += -ffp-contract=off -frounding-math
$(addprefix $(RECOMP_BUILD)/kernel/,xk_cluster_runtime.o xk_cluster_snapshot.o xk_cluster_query.o xk_cluster_query_replay.o): RECOMP_CFLAGS += -ffp-contract=off -frounding-math
recomp/kernel/xk_worker_query_generated.inc: tools/gen_worker_query.py recompiler/xita_recomp.py $(XBE) $(XBE_JSON)
	$(PYTHON) tools/gen_worker_query.py --xbe $(XBE) --manifest $(XBE_JSON)

# Count-only diagnostic build; disabled unless explicitly requested. All guard
# users share the same scope instrumentation, including unchanged idle guards.
ifeq ($(XV_LIGHT_QUERY_CENSUS),1)
ifneq ($(XV_EXPERIMENTAL_OBJECT_JOBS),1)
$(error XV_LIGHT_QUERY_CENSUS requires XV_EXPERIMENTAL_OBJECT_JOBS=1)
endif
RECOMP_CFLAGS += -DXV_LIGHT_QUERY_CENSUS
CFLAGS += -DXV_LIGHT_QUERY_CENSUS
endif
.PHONY: force-light-census-config
force-light-census-config:
$(RECOMP_BUILD)/light-census.config: force-light-census-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(if $(filter 1,$(XV_LIGHT_QUERY_CENSUS)),1,0)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(OBJS) $(XITA_GUEST_OBJS) $(XITA_SYS_OBJS) $(XITA_GAME_OBJS): $(RECOMP_BUILD)/light-census.config

# Opt-in research boundary, runtime default OFF even in this build. Memory
# ownership admission is implemented; enclosing object ordering is unqualified.
XV_OBJECT_SOLVER_BUILD := $(if $(and $(filter 1,$(XV_EXPERIMENTAL_OBJECT_JOBS)),$(filter 1,$(XV_OBJECT_SOLVER_EXPERIMENT))),1,0)
ifeq ($(XV_OBJECT_SOLVER_BUILD),1)
RECOMP_CFLAGS += -DXV_OBJECT_SOLVER_EXPERIMENT
endif
.PHONY: force-object-solver-config
force-object-solver-config:
$(RECOMP_BUILD)/object-solver.config: force-object-solver-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_OBJECT_SOLVER_BUILD)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: $(RECOMP_BUILD)/object-solver.config recomp/kernel/xk_object_solver.h
$(filter $(RECOMP_BUILD)/code_%.o,$(RECOMP_OBJS)): $(RECOMP_BUILD)/object-solver.config

# Sampled lock-holder diagnostic is absent from ordinary acquisition/release.
XV_OBJECT_HOLD_BUILD := $(if $(and $(filter 1,$(XV_EXPERIMENTAL_OBJECT_JOBS)),$(filter 1,$(XV_OBJECT_HOLD_PROFILE))),1,0)
ifeq ($(XV_OBJECT_HOLD_BUILD),1)
RECOMP_CFLAGS += -DXV_OBJECT_HOLD_PROFILE
endif
.PHONY: force-object-hold-config
force-object-hold-config:
$(RECOMP_BUILD)/object-hold.config: force-object-hold-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_OBJECT_HOLD_BUILD)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: $(RECOMP_BUILD)/object-hold.config
$(filter $(RECOMP_BUILD)/code_%.o,$(RECOMP_OBJS)): $(RECOMP_BUILD)/object-hold.config

# Explicit research build only. Track this flag for the two affected objects so
# switching a reused build directory cannot silently retain the previous mode.
XV_OBJECT_POINT_BUILD := $(if $(and $(filter 1,$(XV_EXPERIMENTAL_OBJECT_JOBS)),$(filter 1,$(XV_OBJECT_POINT_EXPERIMENT))),1,0)
ifeq ($(XV_OBJECT_POINT_BUILD),1)
$(RECOMP_BUILD)/kernel/xk_math.o $(RECOMP_BUILD)/kernel/xk_object_jobs.o: RECOMP_CFLAGS += -DXV_OBJECT_POINT_EXPERIMENT
endif
.PHONY: force-object-point-config
force-object-point-config:
$(RECOMP_BUILD)/object-point.config: force-object-point-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_OBJECT_POINT_BUILD)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_math.o $(RECOMP_BUILD)/kernel/xk_object_jobs.o: $(RECOMP_BUILD)/object-point.config

XV_OBJECT_QUAT_PROFILE_BUILD := $(if $(and $(filter 1,$(XV_EXPERIMENTAL_OBJECT_JOBS)),$(filter 1,$(XV_OBJECT_QUAT_PROFILE))),1,0)
ifeq ($(XV_OBJECT_QUAT_PROFILE_BUILD),1)
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: RECOMP_CFLAGS += -DXV_OBJECT_QUAT_PROFILE
endif
.PHONY: force-object-quat-profile-config
force-object-quat-profile-config:
$(RECOMP_BUILD)/object-quat-profile.config: force-object-quat-profile-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_OBJECT_QUAT_PROFILE_BUILD)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: $(RECOMP_BUILD)/object-quat-profile.config

XV_OBJECT_QUAT_BUILD := $(if $(and $(filter 1,$(XV_EXPERIMENTAL_OBJECT_JOBS)),$(filter 1,$(XV_OBJECT_QUAT_EXPERIMENT))),1,0)
ifeq ($(XV_OBJECT_QUAT_BUILD),1)
$(RECOMP_BUILD)/kernel/xk_math.o $(RECOMP_BUILD)/kernel/xk_object_jobs.o: RECOMP_CFLAGS += -DXV_OBJECT_QUAT_EXPERIMENT
endif
.PHONY: force-object-quat-config
force-object-quat-config:
$(RECOMP_BUILD)/object-quat.config: force-object-quat-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_OBJECT_QUAT_BUILD)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_math.o $(RECOMP_BUILD)/kernel/xk_object_jobs.o: $(RECOMP_BUILD)/object-quat.config

# The pose experiment changes only the pool and units containing its exact
# loop hooks. Remember both flag transitions in a reused build directory.
XV_OBJECT_POSE_BUILD := $(if $(and $(filter 1,$(XV_EXPERIMENTAL_OBJECT_JOBS)),$(filter 1,$(XV_OBJECT_POSE_EXPERIMENT))),1,0)
POSE_HOOK_SRCS := $(shell grep -l XV_OBJECT_POSE_SCOPE $(XITA_GUEST_SRCS) 2>/dev/null)
POSE_HOOK_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(POSE_HOOK_SRCS))
ifeq ($(XV_OBJECT_POSE_BUILD),1)
$(POSE_HOOK_OBJS) $(RECOMP_BUILD)/kernel/xk_object_jobs.o: RECOMP_CFLAGS += -DXV_OBJECT_POSE_EXPERIMENT
endif
.PHONY: force-object-pose-config
force-object-pose-config:
$(RECOMP_BUILD)/object-pose.config: force-object-pose-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_OBJECT_POSE_BUILD)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(POSE_HOOK_OBJS) $(RECOMP_BUILD)/kernel/xk_object_jobs.o: $(RECOMP_BUILD)/object-pose.config

# Explicit polygon-edge build mode; runtime controls remain separate.
EDGE_HOOK_SRCS := $(shell rg -l XV_NATIVE_POLYGON_EDGE $(XITA_GUEST_SRCS) 2>/dev/null)
EDGE_HOOK_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(EDGE_HOOK_SRCS))
EDGE_NATIVE_OBJS := $(RECOMP_BUILD)/kernel/xk_polygon_edge.o $(RECOMP_BUILD)/kernel/xk_polygon_edge_control.o
ifeq ($(XV_NATIVE_POLYGON_EDGE),1)
$(EDGE_HOOK_OBJS): RECOMP_CFLAGS += -DXV_NATIVE_POLYGON_EDGE
endif
.PHONY: force-polygon-edge-config
force-polygon-edge-config:
$(RECOMP_BUILD)/polygon-edge.config: force-polygon-edge-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(if $(filter 1,$(XV_NATIVE_POLYGON_EDGE)),1,0)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(EDGE_HOOK_OBJS) $(EDGE_NATIVE_OBJS): $(RECOMP_BUILD)/polygon-edge.config
$(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/librecomp.a: $(RECOMP_BUILD)/polygon-edge.config
$(RECOMP_BUILD)/kernel/xk_polygon_edge.o: RECOMP_CFLAGS += -ffp-contract=off
recomp/kernel/xk_polygon_edge.c: tools/gen_native_polygon_edge.py games/halo_ce_3925/hooks.py recompiler/xita_recomp.py $(XBE) $(XBE_JSON)
	$(PYTHON) tools/gen_native_polygon_edge.py --xbe $(XBE) --manifest $(XBE_JSON)

# Optional clip region: generated units, helper and bridge share a tracked mode.
REGION_HOOK_SRCS := $(shell rg -l XV_NATIVE_CLIP_REGION $(XITA_GUEST_SRCS) 2>/dev/null)
REGION_HOOK_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(REGION_HOOK_SRCS))
REGION_NATIVE_OBJS := $(RECOMP_BUILD)/kernel/xk_clip_region.o $(RECOMP_BUILD)/kernel/xk_clip_region_control.o
ifeq ($(XV_NATIVE_CLIP_REGION),1)
$(REGION_HOOK_OBJS): RECOMP_CFLAGS += -DXV_NATIVE_CLIP_REGION
endif
.PHONY: force-clip-region-config
force-clip-region-config:
$(RECOMP_BUILD)/clip-region.config: force-clip-region-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(if $(filter 1,$(XV_NATIVE_CLIP_REGION)),1,0)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(REGION_HOOK_OBJS) $(REGION_NATIVE_OBJS): $(RECOMP_BUILD)/clip-region.config
$(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/librecomp.a: $(RECOMP_BUILD)/clip-region.config
$(RECOMP_BUILD)/kernel/xk_clip_region.o: RECOMP_CFLAGS += -ffp-contract=off
recomp/kernel/xk_clip_region.c: tools/gen_native_clip_region.py tools/gen_native_clip.py games/halo_ce_3925/clip_region.py games/halo_ce_3925/hooks.py recompiler/xita_recomp.py $(XBE) $(XBE_JSON)
	$(PYTHON) tools/gen_native_clip_region.py --xbe $(XBE) --manifest $(XBE_JSON)
recomp/kernel/xk_clip.c: tools/gen_native_clip.py games/halo_ce_3925/hooks.py games/halo_ce_3925/clip_region.py recompiler/xita_recomp.py $(XBE) $(XBE_JSON)
	$(PYTHON) tools/gen_native_clip.py

# Only generated units containing this optional hook depend on its build mode.
# This also handles changed shard numbering after regeneration.
HIERARCHY_HOOK_SRCS := $(shell grep -l XV_NATIVE_MODEL_HIERARCHY $(XITA_GUEST_SRCS) 2>/dev/null)
HIERARCHY_HOOK_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(HIERARCHY_HOOK_SRCS))
.PHONY: force-model-hierarchy-config
force-model-hierarchy-config:
$(RECOMP_BUILD)/model-hierarchy.config: force-model-hierarchy-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(if $(filter 1,$(XV_NATIVE_MODEL_HIERARCHY)),1,0)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(HIERARCHY_HOOK_OBJS) $(RECOMP_BUILD)/kernel/xk_math.o $(RECOMP_BUILD)/kernel/xk_hierarchy.o: $(RECOMP_BUILD)/model-hierarchy.config

# Unroll only bounded native math units. Scalar VFP operations retain their
# established operand order; no global fast-math or guest codegen change.
$(RECOMP_BUILD)/kernel/xk_math.o: RECOMP_CFLAGS += -O3 -funroll-loops -ffp-contract=off
ifeq ($(XV_NATIVE_MATRIX_NEON),1)
$(RECOMP_BUILD)/kernel/xk_math.o: RECOMP_CFLAGS += -DXV_NATIVE_MATRIX_NEON
endif
# The optional batch validates layouts, scheduling and numeric inputs before
# using bounded products. Keep reassociation/FMA disabled while unrolling it.
$(RECOMP_BUILD)/kernel/xk_palette.o: RECOMP_CFLAGS += -O3 -funroll-loops -ffp-contract=off
$(RECOMP_BUILD)/kernel/xk_hierarchy.o: RECOMP_CFLAGS += -O3 -ffp-contract=off
ifeq ($(XV_PALETTE_JOB_PROFILE),1)
$(RECOMP_BUILD)/kernel/xk_palette.o: RECOMP_CFLAGS += -DXV_PALETTE_JOB_PROFILE=1
endif
$(RECOMP_BUILD)/kernel/xk_object_basis.o: RECOMP_CFLAGS += -ffp-contract=off
$(RECOMP_BUILD)/kernel/xk_clip.o: RECOMP_CFLAGS += -ffp-contract=off
$(RECOMP_BUILD)/kernel/xk_bounds.o: RECOMP_CFLAGS += -ffp-contract=off
ifeq ($(XV_HLE_DISPATCH_CACHE),1)
$(RECOMP_BUILD)/xv_x86rt.o: RECOMP_CFLAGS += -DXV_HLE_DISPATCH_CACHE
endif
recomp/kernel/xk_bounds.c: tools/gen_native_bounds.py games/halo_ce_3925/hooks.py recompiler/xita_recomp.py $(XBE) $(XBE_JSON)
	$(PYTHON) tools/gen_native_bounds.py

# Lifted code includes xv_recomp_protos.h -> xv_x86rt.h and, for diagnostic
# generation, xv_phase.h. The kernel/HLE objects use -MMD
# so a kernel header edit does not recompile the ~35 MB of generated code.
$(RECOMP_BUILD)/code_%.o: $(RECOMP_DIR)/code_%.c $(RECOMP_DIR)/xv_recomp_protos.h $(RECOMP_DIR)/xv_x86rt.h $(RECOMP_DIR)/xv_phase.h
	@mkdir -p $(dir $@)
	$(CC) $(RECOMP_CFLAGS) -c $< -o $@
$(RECOMP_BUILD)/%.o: $(RECOMP_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(RECOMP_CFLAGS) -MMD -MP -c $< -o $@
-include $(RECOMP_OBJS:.o=.d)

# Recreate archives so removed/renamed members cannot survive an incremental build.
$(RECOMP_BUILD)/libxita_sys.a: $(XITA_SYS_OBJS) Makefile
	@rm -f $@
	$(PREFIX)-gcc-ar rcs $@ $(XITA_SYS_OBJS)
$(RECOMP_BUILD)/libxita_game.a: $(XITA_GAME_OBJS) games/$(GAME_PROFILE)/runtime.mk Makefile
	@rm -f $@
	$(PREFIX)-gcc-ar rcs $@ $(XITA_GAME_OBJS)
$(RECOMP_BUILD)/libxita_guest.a: $(XITA_GUEST_OBJS) Makefile
	@rm -f $@
	$(PREFIX)-gcc-ar rcs $@ $(XITA_GUEST_OBJS)

# Compatibility target for developer tools that still request the combined archive.
$(RECOMP_BUILD)/librecomp.a: $(RECOMP_OBJS) Makefile
	@rm -f $@
	$(PREFIX)-gcc-ar rcs $@ $(RECOMP_OBJS)

recomp-lib: $(RECOMP_BUILD)/libxita_sys.a $(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/libxita_guest.a
	@$(PREFIX)-size -t $^ | tail -1

.PHONY: recomp-lib
