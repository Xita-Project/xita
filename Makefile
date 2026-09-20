XV_POSE_PIPELINE ?= 0
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
SRCS      := runtime/main.c runtime/xv_shader.c runtime/xv_d3d.c runtime/xv_scene.c runtime/xv_ui_gxm.c runtime/xv_log.c runtime/xv_benchmark.c runtime/xv_cpu.c runtime/xv_texture_worker.c runtime/xv_geometry_worker.c runtime/xv_gpu_upload.c runtime/xv_vertex_upload.c runtime/xv_vertex_prepare.c runtime/xv_vertex_capture.c runtime/xv_upload_worker.c runtime/xv_draw_profile.c runtime/xv_render_profile.c
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
XV_DEPTH_STORE_DEFAULT ?= 0
ifneq ($(words $(XV_DEPTH_STORE_DEFAULT)),1)
$(error XV_DEPTH_STORE_DEFAULT must be 0 or 1)
endif
ifneq ($(filter $(XV_DEPTH_STORE_DEFAULT),0 1),$(XV_DEPTH_STORE_DEFAULT))
$(error XV_DEPTH_STORE_DEFAULT must be 0 or 1)
endif
ifeq ($(XV_DEPTH_STORE),1)
ifneq ($(RECOMP),1)
$(error XV_DEPTH_STORE requires RECOMP=1)
endif
$(BUILD)/runtime/main.o $(BUILD)/runtime/xv_d3d.o $(BUILD)/runtime/xv_shader.o $(BUILD)/runtime/xv_ui_gxm.o: CFLAGS += -DXV_DEPTH_STORE
$(BUILD)/runtime/xv_d3d.o: CFLAGS += -DXV_DEPTH_STORE_DEFAULT=$(XV_DEPTH_STORE_DEFAULT)
endif
.PHONY: force-depth-store-config force-depth-store-startup-config
force-depth-store-config:
force-depth-store-startup-config:
$(BUILD)/depth-store.config: force-depth-store-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(if $(filter 1,$(XV_DEPTH_STORE)),1,0)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/main.o $(BUILD)/runtime/xv_d3d.o $(BUILD)/runtime/xv_shader.o $(BUILD)/runtime/xv_ui_gxm.o: $(BUILD)/depth-store.config
$(BUILD)/depth-store-startup.config: force-depth-store-startup-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(if $(filter 1,$(XV_DEPTH_STORE)),$(XV_DEPTH_STORE_DEFAULT),0)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/xv_d3d.o: $(BUILD)/depth-store-startup.config
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
# Ordered exact query publication may precede older full-frame retirement.
# Experimental compile selector, OFF by default; only the pump and reader own it.
XV_QUERY_PREFIX_PUBLISH ?= 0
ifneq ($(words $(XV_QUERY_PREFIX_PUBLISH)),1)
$(error XV_QUERY_PREFIX_PUBLISH must be 0 or 1)
endif
ifneq ($(filter $(XV_QUERY_PREFIX_PUBLISH),0 1),$(XV_QUERY_PREFIX_PUBLISH))
$(error XV_QUERY_PREFIX_PUBLISH must be 0 or 1)
endif
ifeq ($(XV_QUERY_PREFIX_PUBLISH),1)
ifneq ($(RECOMP):$(XV_QUERY_BOUNDARY):$(XV_FLARE_QUERY_OVERLAP),1:1:1)
$(error XV_QUERY_PREFIX_PUBLISH requires RECOMP=1 XV_QUERY_BOUNDARY=1 XV_FLARE_QUERY_OVERLAP=1)
endif
$(BUILD)/runtime/main.o $(BUILD)/runtime/xv_d3d.o: CFLAGS += -DXV_QUERY_PREFIX_PUBLISH=1
endif
.PHONY: force-query-prefix-config
force-query-prefix-config:
$(BUILD)/query-prefix.config: force-query-prefix-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_QUERY_PREFIX_PUBLISH)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/main.o $(BUILD)/runtime/xv_d3d.o: $(BUILD)/query-prefix.config
# Two coarse owner scopes. Default OFF, with no XV_PHASE or worker-policy change.
XV_OWNER_PHASE ?= 0
XV_MODEL_FOG ?= 0
XV_MODEL_UV ?= 0
XV_MODEL_UV_CROSS_MODEL ?= 0
XV_OWNER_PHASE_DEFAULT ?= 0
XV_SCENE_PARTITION ?= 0
XV_SCENE_BUCKET0_DETAIL ?= 0
XV_SCENE_BUCKET1_DETAIL ?= 0
# Ordered portal register/flag caching, selected only in its regenerated unit.
XV_NATIVE_VISIBILITY_PORTAL_LOOP ?= 0
XV_CLIP_DISTANCE_SPANS ?= 0
XV_TYPED_PORTAL_POLYGON ?= 0
XV_TYPED_SUBCLUSTER ?= 0
XV_NATIVE_VISIBILITY_JOBS ?= 0
XV_NATIVE_VISIBILITY_PASS ?= 0
ifneq ($(words $(XV_NATIVE_VISIBILITY_PASS)),1)
$(error XV_NATIVE_VISIBILITY_PASS must be 0 or 1)
endif
ifneq ($(filter $(XV_NATIVE_VISIBILITY_PASS),0 1),$(XV_NATIVE_VISIBILITY_PASS))
$(error XV_NATIVE_VISIBILITY_PASS must be 0 or 1)
endif
ifneq ($(words $(XV_NATIVE_VISIBILITY_JOBS)),1)
$(error XV_NATIVE_VISIBILITY_JOBS must be 0 or 1)
endif
ifneq ($(filter $(XV_NATIVE_VISIBILITY_JOBS),0 1),$(XV_NATIVE_VISIBILITY_JOBS))
$(error XV_NATIVE_VISIBILITY_JOBS must be 0 or 1)
endif
ifneq ($(words $(XV_TYPED_SUBCLUSTER)),1)
$(error XV_TYPED_SUBCLUSTER must be 0 or 1)
endif
ifneq ($(filter $(XV_TYPED_SUBCLUSTER),0 1),$(XV_TYPED_SUBCLUSTER))
$(error XV_TYPED_SUBCLUSTER must be 0 or 1)
endif
ifneq ($(words $(XV_TYPED_PORTAL_POLYGON)),1)
$(error XV_TYPED_PORTAL_POLYGON must be 0 or 1)
endif
ifneq ($(filter $(XV_TYPED_PORTAL_POLYGON),0 1),$(XV_TYPED_PORTAL_POLYGON))
$(error XV_TYPED_PORTAL_POLYGON must be 0 or 1)
endif
ifneq ($(words $(XV_CLIP_DISTANCE_SPANS)),1)
$(error XV_CLIP_DISTANCE_SPANS must be 0 or 1)
endif
ifneq ($(filter $(XV_CLIP_DISTANCE_SPANS),0 1),$(XV_CLIP_DISTANCE_SPANS))
$(error XV_CLIP_DISTANCE_SPANS must be 0 or 1)
endif
ifneq ($(words $(XV_NATIVE_VISIBILITY_PORTAL_LOOP)),1)
$(error XV_NATIVE_VISIBILITY_PORTAL_LOOP must be 0 or 1)
endif
ifneq ($(filter $(XV_NATIVE_VISIBILITY_PORTAL_LOOP),0 1),$(XV_NATIVE_VISIBILITY_PORTAL_LOOP))
$(error XV_NATIVE_VISIBILITY_PORTAL_LOOP must be 0 or 1)
endif
XV_PALETTE_PREFIX_REUSE ?= 0
ifneq ($(words $(XV_PALETTE_PREFIX_REUSE)),1)
$(error XV_PALETTE_PREFIX_REUSE must be 0 or 1)
endif
ifneq ($(filter $(XV_PALETTE_PREFIX_REUSE),0 1),$(XV_PALETTE_PREFIX_REUSE))
$(error XV_PALETTE_PREFIX_REUSE must be 0 or 1)
endif
ifneq ($(words $(XV_SCENE_BUCKET0_DETAIL)),1)
$(error XV_SCENE_BUCKET0_DETAIL must be 0 or 1)
endif
ifneq ($(filter $(XV_SCENE_BUCKET0_DETAIL),0 1),$(XV_SCENE_BUCKET0_DETAIL))
$(error XV_SCENE_BUCKET0_DETAIL must be 0 or 1)
endif
ifneq ($(words $(XV_SCENE_BUCKET1_DETAIL)),1)
$(error XV_SCENE_BUCKET1_DETAIL must be 0 or 1)
endif
ifneq ($(filter $(XV_SCENE_BUCKET1_DETAIL),0 1),$(XV_SCENE_BUCKET1_DETAIL))
$(error XV_SCENE_BUCKET1_DETAIL must be 0 or 1)
endif
ifneq ($(words $(XV_SCENE_PARTITION)),1)
$(error XV_SCENE_PARTITION must be 0 or 1)
endif
ifneq ($(filter $(XV_SCENE_PARTITION),0 1),$(XV_SCENE_PARTITION))
$(error XV_SCENE_PARTITION must be 0 or 1)
endif
ifneq ($(words $(XV_OWNER_PHASE)),1)
$(error XV_OWNER_PHASE must be 0 or 1)
endif
ifneq ($(filter $(XV_OWNER_PHASE),0 1),$(XV_OWNER_PHASE))
$(error XV_OWNER_PHASE must be 0 or 1)
endif
ifneq ($(words $(XV_OWNER_PHASE_DEFAULT)),1)
$(error XV_OWNER_PHASE_DEFAULT must be 0 or 1)
endif
ifneq ($(filter $(XV_OWNER_PHASE_DEFAULT),0 1),$(XV_OWNER_PHASE_DEFAULT))
$(error XV_OWNER_PHASE_DEFAULT must be 0 or 1)
endif
ifeq ($(XV_OWNER_PHASE),1)
ifneq ($(RECOMP),1)
$(error XV_OWNER_PHASE requires RECOMP=1)
endif
$(BUILD)/runtime/main.o $(BUILD)/runtime/xv_ui_gxm.o: CFLAGS += -DXV_OWNER_PHASE
endif
.PHONY: force-owner-phase-config force-owner-phase-startup-config
force-owner-phase-config:
force-owner-phase-startup-config:
$(BUILD)/owner-phase.config: force-owner-phase-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_OWNER_PHASE)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/owner-phase-startup.config: force-owner-phase-startup-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(if $(filter 1,$(XV_OWNER_PHASE)),$(XV_OWNER_PHASE_DEFAULT),0)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/main.o $(BUILD)/runtime/xv_ui_gxm.o: $(BUILD)/owner-phase.config
.PHONY: force-scene-partition-config
force-scene-partition-config:
.PHONY: force-scene-bucket0-detail-config
force-scene-bucket0-detail-config:
$(BUILD)/scene-bucket0-detail.config: force-scene-bucket0-detail-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_SCENE_BUCKET0_DETAIL)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
.PHONY: force-palette-prefix-config
force-palette-prefix-config:
$(BUILD)/palette-prefix.config: force-palette-prefix-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_PALETTE_PREFIX_REUSE)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
.PHONY: force-scene-bucket1-detail-config
force-scene-bucket1-detail-config:
$(BUILD)/scene-bucket1-detail.config: force-scene-bucket1-detail-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_SCENE_BUCKET1_DETAIL)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
.PHONY: force-native-visibility-portal-config
force-native-visibility-portal-config:
$(BUILD)/native-visibility-portal.config: force-native-visibility-portal-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_NATIVE_VISIBILITY_PORTAL_LOOP)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/scene-partition.config: force-scene-partition-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_SCENE_PARTITION)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
# Grouped exact vertex comparisons: opt-in at process start, independent of
# graphics quality. Only the uploader consumes this build default.
XV_PACKED_VERTEX_LAYOUT ?= 0
ifneq ($(words $(XV_PACKED_VERTEX_LAYOUT)),1)
$(error XV_PACKED_VERTEX_LAYOUT must be 0 or 1)
endif
ifneq ($(filter $(XV_PACKED_VERTEX_LAYOUT),0 1),$(XV_PACKED_VERTEX_LAYOUT))
$(error XV_PACKED_VERTEX_LAYOUT must be 0 or 1)
endif
# All runtime owners of shader/prepare ABI, never generated guest units.
PACKED_VERTEX_OBJECTS := $(addprefix $(BUILD)/runtime/,$(addsuffix .o,main xv_d3d xv_shader xv_ui_gxm xv_vertex_upload xv_vertex_prepare xv_vertex_capture))
$(PACKED_VERTEX_OBJECTS): CFLAGS += -DXV_PACKED_VERTEX_LAYOUT=$(XV_PACKED_VERTEX_LAYOUT)
.PHONY: force-packed-vertex-config
force-packed-vertex-config:
$(BUILD)/packed-vertex.config: force-packed-vertex-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_PACKED_VERTEX_LAYOUT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(PACKED_VERTEX_OBJECTS): $(BUILD)/packed-vertex.config runtime/xv_packed_vertex.h
# Captured vertex preparation: startup-only trial, no benchmark mode.
XV_VERTEX_CAPTURE_DEFAULT ?= 0
ifneq ($(words $(XV_VERTEX_CAPTURE_DEFAULT)),1)
$(error XV_VERTEX_CAPTURE_DEFAULT must be 0 or 1)
endif
ifneq ($(filter $(XV_VERTEX_CAPTURE_DEFAULT),0 1),$(XV_VERTEX_CAPTURE_DEFAULT))
$(error XV_VERTEX_CAPTURE_DEFAULT must be 0 or 1)
endif
$(BUILD)/runtime/xv_vertex_capture.o: CFLAGS += -DXV_VERTEX_CAPTURE_DEFAULT=$(XV_VERTEX_CAPTURE_DEFAULT)
.PHONY: force-vertex-capture-config
force-vertex-capture-config:
$(BUILD)/vertex-capture.config: force-vertex-capture-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_VERTEX_CAPTURE_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/xv_vertex_capture.o: $(BUILD)/vertex-capture.config
# Notification coalescing belongs only to the capture FIFO implementation.
XV_VERTEX_CAPTURE_NOTIFY ?= 0
ifneq ($(words $(XV_VERTEX_CAPTURE_NOTIFY)),1)
$(error XV_VERTEX_CAPTURE_NOTIFY must be 0 or 1)
endif
ifneq ($(filter $(XV_VERTEX_CAPTURE_NOTIFY),0 1),$(XV_VERTEX_CAPTURE_NOTIFY))
$(error XV_VERTEX_CAPTURE_NOTIFY must be 0 or 1)
endif
$(BUILD)/runtime/xv_vertex_capture.o: CFLAGS += -DXV_VERTEX_CAPTURE_NOTIFY=$(XV_VERTEX_CAPTURE_NOTIFY)
.PHONY: force-capture-notify-config
force-capture-notify-config:
$(BUILD)/capture-notify.config: force-capture-notify-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_VERTEX_CAPTURE_NOTIFY)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/xv_vertex_capture.o: $(BUILD)/capture-notify.config
# Opt-in sampled timing; ordinary builds keep it disabled.
XV_VERTEX_CAPTURE_DETAIL_DEFAULT ?= 0
ifneq ($(words $(XV_VERTEX_CAPTURE_DETAIL_DEFAULT)),1)
$(error XV_VERTEX_CAPTURE_DETAIL_DEFAULT must be 0 or 1)
endif
ifneq ($(filter $(XV_VERTEX_CAPTURE_DETAIL_DEFAULT),0 1),$(XV_VERTEX_CAPTURE_DETAIL_DEFAULT))
$(error XV_VERTEX_CAPTURE_DETAIL_DEFAULT must be 0 or 1)
endif
$(BUILD)/runtime/xv_vertex_capture.o: CFLAGS += -DXV_VERTEX_CAPTURE_DETAIL_DEFAULT=$(XV_VERTEX_CAPTURE_DETAIL_DEFAULT)
# Trusted tag-resident vertex reuse (research trial): skip the byte compare for
# sources inside the 22 MB tag cache under the same map-read generation.
XV_CAPTURE_TRUST_TAGS ?= 0
XV_CAPTURE_TRUST_TAGS_DEFAULT ?= 0
ifneq ($(filter $(XV_CAPTURE_TRUST_TAGS),0 1),$(XV_CAPTURE_TRUST_TAGS))
$(error XV_CAPTURE_TRUST_TAGS must be 0 or 1)
endif
ifneq ($(filter $(XV_CAPTURE_TRUST_TAGS_DEFAULT),0 1),$(XV_CAPTURE_TRUST_TAGS_DEFAULT))
$(error XV_CAPTURE_TRUST_TAGS_DEFAULT must be 0 or 1)
endif
$(BUILD)/runtime/xv_vertex_capture.o: CFLAGS += -DXV_CAPTURE_TRUST_TAGS=$(XV_CAPTURE_TRUST_TAGS) -DXV_CAPTURE_TRUST_TAGS_DEFAULT=$(XV_CAPTURE_TRUST_TAGS_DEFAULT)
.PHONY: force-capture-trust-config
force-capture-trust-config:
$(BUILD)/capture-trust.config: force-capture-trust-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_CAPTURE_TRUST_TAGS):$(XV_CAPTURE_TRUST_TAGS_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/xv_vertex_capture.o: $(BUILD)/capture-trust.config
.PHONY: force-capture-detail-config
force-capture-detail-config:
$(BUILD)/capture-detail.config: force-capture-detail-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_VERTEX_CAPTURE_DETAIL_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/xv_vertex_capture.o: $(BUILD)/capture-detail.config
XV_VERTEX_CAPTURE_REUSE ?= 0
ifneq ($(words $(XV_VERTEX_CAPTURE_REUSE)),1)
$(error XV_VERTEX_CAPTURE_REUSE must be 0 or 1)
endif
ifneq ($(filter $(XV_VERTEX_CAPTURE_REUSE),0 1),$(XV_VERTEX_CAPTURE_REUSE))
$(error XV_VERTEX_CAPTURE_REUSE must be 0 or 1)
endif
$(BUILD)/runtime/xv_vertex_capture.o: CFLAGS += -DXV_VERTEX_CAPTURE_REUSE=$(XV_VERTEX_CAPTURE_REUSE)
.PHONY: force-capture-reuse-config
force-capture-reuse-config:
$(BUILD)/capture-reuse.config: force-capture-reuse-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_VERTEX_CAPTURE_REUSE)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/xv_vertex_capture.o: $(BUILD)/capture-reuse.config
# Reuse already completed results without publishing another worker job.
XV_VERTEX_CAPTURE_READY ?= 0
ifneq ($(words $(XV_VERTEX_CAPTURE_READY)),1)
$(error XV_VERTEX_CAPTURE_READY must be 0 or 1)
endif
ifneq ($(filter $(XV_VERTEX_CAPTURE_READY),0 1),$(XV_VERTEX_CAPTURE_READY))
$(error XV_VERTEX_CAPTURE_READY must be 0 or 1)
endif
ifeq ($(XV_VERTEX_CAPTURE_READY),1)
ifneq ($(XV_VERTEX_CAPTURE_REUSE),1)
$(error XV_VERTEX_CAPTURE_READY requires XV_VERTEX_CAPTURE_REUSE=1)
endif
endif
$(BUILD)/runtime/xv_vertex_capture.o: CFLAGS += -DXV_VERTEX_CAPTURE_READY=$(XV_VERTEX_CAPTURE_READY)
.PHONY: force-capture-ready-config
force-capture-ready-config:
$(BUILD)/capture-ready.config: force-capture-ready-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_VERTEX_CAPTURE_READY)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/xv_vertex_capture.o: $(BUILD)/capture-ready.config
# Exact immutable uploads retained across GPU frame slots. Off by default.
XV_VERTEX_PERSISTENT ?= 0
ifneq ($(words $(XV_VERTEX_PERSISTENT)),1)
$(error XV_VERTEX_PERSISTENT must be 0 or 1)
endif
ifneq ($(filter $(XV_VERTEX_PERSISTENT),0 1),$(XV_VERTEX_PERSISTENT))
$(error XV_VERTEX_PERSISTENT must be 0 or 1)
endif
$(BUILD)/runtime/xv_vertex_capture.o: CFLAGS += -DXV_VERTEX_PERSISTENT=$(XV_VERTEX_PERSISTENT)
.PHONY: force-vertex-persistent-config
force-vertex-persistent-config:
$(BUILD)/vertex-persistent.config: force-vertex-persistent-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_VERTEX_PERSISTENT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/xv_vertex_capture.o: $(BUILD)/vertex-persistent.config runtime/xv_vertex_persistent.h
# Exact wide comparison, limited to the two vertex preparation owners.
XV_VERTEX_WIDE_COMPARE ?= 0
ifneq ($(words $(XV_VERTEX_WIDE_COMPARE)),1)
$(error XV_VERTEX_WIDE_COMPARE must be 0 or 1)
endif
ifneq ($(filter $(XV_VERTEX_WIDE_COMPARE),0 1),$(XV_VERTEX_WIDE_COMPARE))
$(error XV_VERTEX_WIDE_COMPARE must be 0 or 1)
endif
VERTEX_WIDE_OBJECTS := $(BUILD)/runtime/xv_vertex_capture.o $(BUILD)/runtime/xv_vertex_upload.o
$(VERTEX_WIDE_OBJECTS): CFLAGS += -DXV_VERTEX_WIDE_COMPARE=$(XV_VERTEX_WIDE_COMPARE)
.PHONY: force-vertex-wide-config
force-vertex-wide-config:
$(BUILD)/vertex-wide.config: force-vertex-wide-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_VERTEX_WIDE_COMPARE)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(VERTEX_WIDE_OBJECTS): $(BUILD)/vertex-wide.config
XV_VERTEX_CAPTURE_PACKED ?= 0
ifneq ($(words $(XV_VERTEX_CAPTURE_PACKED)),1)
$(error XV_VERTEX_CAPTURE_PACKED must be 0 or 1)
endif
ifneq ($(filter $(XV_VERTEX_CAPTURE_PACKED),0 1),$(XV_VERTEX_CAPTURE_PACKED))
$(error XV_VERTEX_CAPTURE_PACKED must be 0 or 1)
endif
ifeq ($(XV_VERTEX_CAPTURE_PACKED),1)
ifneq ($(XV_PACKED_VERTEX_LAYOUT),1)
$(error XV_VERTEX_CAPTURE_PACKED requires XV_PACKED_VERTEX_LAYOUT=1)
endif
endif
CAPTURE_PACKED_OBJECTS := $(BUILD)/runtime/xv_vertex_capture.o $(BUILD)/runtime/xv_vertex_upload.o
$(CAPTURE_PACKED_OBJECTS): CFLAGS += -DXV_VERTEX_CAPTURE_PACKED=$(XV_VERTEX_CAPTURE_PACKED)
.PHONY: force-capture-packed-config
force-capture-packed-config:
$(BUILD)/capture-packed.config: force-capture-packed-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_VERTEX_CAPTURE_PACKED)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(CAPTURE_PACKED_OBJECTS): $(BUILD)/capture-packed.config runtime/xv_vertex_upload.h
XV_VERTEX_BLOCK_LOADS_DEFAULT ?= 0
ifneq ($(words $(XV_VERTEX_BLOCK_LOADS_DEFAULT)),1)
$(error XV_VERTEX_BLOCK_LOADS_DEFAULT must be 0 or 1)
endif
ifneq ($(filter $(XV_VERTEX_BLOCK_LOADS_DEFAULT),0 1),$(XV_VERTEX_BLOCK_LOADS_DEFAULT))
$(error XV_VERTEX_BLOCK_LOADS_DEFAULT must be 0 or 1)
endif
$(BUILD)/runtime/xv_vertex_upload.o: CFLAGS += -DXV_VERTEX_BLOCK_LOADS_DEFAULT=$(XV_VERTEX_BLOCK_LOADS_DEFAULT)
.PHONY: force-vertex-block-startup-config
force-vertex-block-startup-config:
$(BUILD)/vertex-block-startup.config: force-vertex-block-startup-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_VERTEX_BLOCK_LOADS_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/xv_vertex_upload.o: $(BUILD)/vertex-block-startup.config
# Exact retired-slot vertex residency: process-start selection, preserving
# explicit XV_VERTEX_RESIDENT environment parsing. Only the uploader uses it.
XV_VERTEX_RESIDENT_DEFAULT ?= 0
ifneq ($(words $(XV_VERTEX_RESIDENT_DEFAULT)),1)
$(error XV_VERTEX_RESIDENT_DEFAULT must be 0 or 1)
endif
ifneq ($(filter $(XV_VERTEX_RESIDENT_DEFAULT),0 1),$(XV_VERTEX_RESIDENT_DEFAULT))
$(error XV_VERTEX_RESIDENT_DEFAULT must be 0 or 1)
endif
$(BUILD)/runtime/xv_vertex_upload.o: CFLAGS += -DXV_VERTEX_RESIDENT_DEFAULT=$(XV_VERTEX_RESIDENT_DEFAULT)
.PHONY: force-vertex-resident-startup-config
force-vertex-resident-startup-config:
$(BUILD)/vertex-resident-startup.config: force-vertex-resident-startup-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_VERTEX_RESIDENT_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/xv_vertex_upload.o: $(BUILD)/vertex-resident-startup.config

# Exact indexed groups for retired RAW snapshots; no guest-pointer lifetime cache.
XV_VERTEX_RESIDENT_REFERENCES_DEFAULT ?= 0
ifneq ($(words $(XV_VERTEX_RESIDENT_REFERENCES_DEFAULT)),1)
$(error XV_VERTEX_RESIDENT_REFERENCES_DEFAULT must be 0 or 1)
endif
ifneq ($(filter $(XV_VERTEX_RESIDENT_REFERENCES_DEFAULT),0 1),$(XV_VERTEX_RESIDENT_REFERENCES_DEFAULT))
$(error XV_VERTEX_RESIDENT_REFERENCES_DEFAULT must be 0 or 1)
endif
$(BUILD)/runtime/xv_vertex_upload.o: CFLAGS += -DXV_VERTEX_RESIDENT_REFERENCES_DEFAULT=$(XV_VERTEX_RESIDENT_REFERENCES_DEFAULT)
.PHONY: force-vertex-resident-references-startup-config
force-vertex-resident-references-startup-config:
$(BUILD)/vertex-resident-references-startup.config: force-vertex-resident-references-startup-config
	@mkdir -p $(dir $@)
	@printf '%s\n' '$(XV_VERTEX_RESIDENT_REFERENCES_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/xv_vertex_upload.o: $(BUILD)/vertex-resident-references-startup.config
# Decoded RGBA layout candidate: select at process start, preserving texture
# quality and explicit XV_RGBA_SWIZZLED environment selection. OFF by default.
XV_RGBA_SWIZZLED_DEFAULT ?= 0
ifneq ($(words $(XV_RGBA_SWIZZLED_DEFAULT)),1)
$(error XV_RGBA_SWIZZLED_DEFAULT must be 0 or 1)
endif
ifneq ($(filter $(XV_RGBA_SWIZZLED_DEFAULT),0 1),$(XV_RGBA_SWIZZLED_DEFAULT))
$(error XV_RGBA_SWIZZLED_DEFAULT must be 0 or 1)
endif
$(BUILD)/runtime/xv_ui_gxm.o: CFLAGS += -DXV_RGBA_SWIZZLED_DEFAULT=$(XV_RGBA_SWIZZLED_DEFAULT)
.PHONY: force-rgba-layout-startup-config
force-rgba-layout-startup-config:
$(BUILD)/rgba-layout-startup.config: force-rgba-layout-startup-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_RGBA_SWIZZLED_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/xv_ui_gxm.o: $(BUILD)/rgba-layout-startup.config
# Existing XV_TEXTURE_STATE_CACHE policy: absent environment uses this process-start default.
# Only replay/recording configuration in xv_d3d consumes it; guest units unchanged.
XV_TEXTURE_STATE_CACHE_DEFAULT ?= 0
ifneq ($(words $(XV_TEXTURE_STATE_CACHE_DEFAULT)),1)
$(error XV_TEXTURE_STATE_CACHE_DEFAULT must be 0 or 1)
endif
ifneq ($(filter $(XV_TEXTURE_STATE_CACHE_DEFAULT),0 1),$(XV_TEXTURE_STATE_CACHE_DEFAULT))
$(error XV_TEXTURE_STATE_CACHE_DEFAULT must be 0 or 1)
endif
$(BUILD)/runtime/xv_d3d.o: CFLAGS += -DXV_TEXTURE_STATE_CACHE_DEFAULT=$(XV_TEXTURE_STATE_CACHE_DEFAULT)
.PHONY: force-texture-state-startup-config
force-texture-state-startup-config:
$(BUILD)/texture-state-startup.config: force-texture-state-startup-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_TEXTURE_STATE_CACHE_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/xv_d3d.o: $(BUILD)/texture-state-startup.config
# Detailed draw timers may be disabled while retaining joined cache counters.
XV_DRAW_PROFILE_DEFAULT ?= 1
ifneq ($(words $(XV_DRAW_PROFILE_DEFAULT)),1)
$(error XV_DRAW_PROFILE_DEFAULT must be 0 or 1)
endif
ifneq ($(filter $(XV_DRAW_PROFILE_DEFAULT),0 1),$(XV_DRAW_PROFILE_DEFAULT))
$(error XV_DRAW_PROFILE_DEFAULT must be 0 or 1)
endif
$(BUILD)/runtime/xv_draw_profile.o: CFLAGS += -DXV_DRAW_PROFILE_DEFAULT=$(XV_DRAW_PROFILE_DEFAULT)
.PHONY: force-draw-profile-startup-config
force-draw-profile-startup-config:
$(BUILD)/draw-profile-startup.config: force-draw-profile-startup-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_DRAW_PROFILE_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/xv_draw_profile.o: $(BUILD)/draw-profile-startup.config

# Experimental CPU index metadata. Explicit startup environment takes precedence.
XV_INDEX_METADATA_DEFAULT ?= 0
ifneq ($(words $(XV_INDEX_METADATA_DEFAULT)),1)
$(error XV_INDEX_METADATA_DEFAULT must be 0 or 1)
endif
ifneq ($(filter $(XV_INDEX_METADATA_DEFAULT),0 1),$(XV_INDEX_METADATA_DEFAULT))
$(error XV_INDEX_METADATA_DEFAULT must be 0 or 1)
endif
$(BUILD)/runtime/xv_d3d.o: CFLAGS += -DXV_INDEX_METADATA_DEFAULT=$(XV_INDEX_METADATA_DEFAULT)
.PHONY: force-index-metadata-startup-config
force-index-metadata-startup-config:
$(BUILD)/index-metadata-startup.config: force-index-metadata-startup-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_INDEX_METADATA_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/xv_d3d.o: $(BUILD)/index-metadata-startup.config
# Existing XV_DEPTH_PREPARE policy: absent environment uses this process-start default.
# Only replay/recording configuration in xv_d3d consumes it; guest units unchanged.
XV_DEPTH_PREPARE_DEFAULT ?= 0
ifneq ($(words $(XV_DEPTH_PREPARE_DEFAULT)),1)
$(error XV_DEPTH_PREPARE_DEFAULT must be 0 or 1)
endif
ifneq ($(filter $(XV_DEPTH_PREPARE_DEFAULT),0 1),$(XV_DEPTH_PREPARE_DEFAULT))
$(error XV_DEPTH_PREPARE_DEFAULT must be 0 or 1)
endif
$(BUILD)/runtime/xv_d3d.o: CFLAGS += -DXV_DEPTH_PREPARE_DEFAULT=$(XV_DEPTH_PREPARE_DEFAULT)
.PHONY: force-depth-prepare-startup-config
force-depth-prepare-startup-config:
$(BUILD)/depth-prepare-startup.config: force-depth-prepare-startup-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_DEPTH_PREPARE_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/xv_d3d.o: $(BUILD)/depth-prepare-startup.config
# Diagnostic existing-scene completion census. OFF unless explicitly compiled.
# Capacity is an independently verified SDK/runtime contract, not an allocation
# request. Zero (the default) records structural declines without adding fences.
XV_SCENE_CENSUS ?= 0
XV_SCENE_CENSUS_NOTIFICATION_WORDS ?= 0
ifneq ($(words $(XV_SCENE_CENSUS)),1)
$(error XV_SCENE_CENSUS must be 0 or 1)
endif
ifneq ($(filter $(XV_SCENE_CENSUS),0 1),$(XV_SCENE_CENSUS))
$(error XV_SCENE_CENSUS must be 0 or 1)
endif
ifeq ($(XV_SCENE_CENSUS),1)
ifneq ($(RECOMP),1)
$(error XV_SCENE_CENSUS requires RECOMP=1)
endif
$(BUILD)/runtime/main.o $(BUILD)/runtime/xv_d3d.o: CFLAGS += -DXV_SCENE_CENSUS
$(BUILD)/runtime/main.o: CFLAGS += -DXV_SCENE_CENSUS_NOTIFICATION_WORDS=$(XV_SCENE_CENSUS_NOTIFICATION_WORDS)
endif
.PHONY: force-scene-census-config force-scene-census-words-config
force-scene-census-config:
force-scene-census-words-config:
$(BUILD)/scene-census.config: force-scene-census-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_SCENE_CENSUS)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/scene-census-words.config: force-scene-census-words-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(if $(filter 1,$(XV_SCENE_CENSUS)),$(XV_SCENE_CENSUS_NOTIFICATION_WORDS),0)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/main.o $(BUILD)/runtime/xv_d3d.o: $(BUILD)/scene-census.config
$(BUILD)/runtime/main.o: $(BUILD)/scene-census-words.config
# Runtime compilation uses -MMD: header edits rebuild their actual includers.
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
ifeq ($(XV_CLIP_DISTANCE_SPANS),1)
ifneq ($(RECOMP):$(GAME_PROFILE):$(XV_NATIVE_CLIP_REGION),1:halo_ce_3925:1)
$(error XV_CLIP_DISTANCE_SPANS requires RECOMP=1 GAME_PROFILE=halo_ce_3925 XV_NATIVE_CLIP_REGION=1)
endif
endif
ifeq ($(XV_TYPED_PORTAL_POLYGON),1)
ifneq ($(RECOMP):$(GAME_PROFILE):$(XV_NATIVE_CLIP_REGION):$(XV_LIGHT_QUERY_CENSUS):$(XV_EXPERIMENTAL_OBJECT_JOBS):$(XV_NATIVE_VISIBILITY_PORTAL_LOOP),1:halo_ce_3925:1:1:1:1)
$(error XV_TYPED_PORTAL_POLYGON requires the Halo CE native clip, portal loop and owner/census backend)
endif
endif
ifeq ($(XV_TYPED_SUBCLUSTER),1)
ifneq ($(RECOMP):$(GAME_PROFILE):$(XV_NATIVE_CLIP_REGION):$(XV_LIGHT_QUERY_CENSUS):$(XV_EXPERIMENTAL_OBJECT_JOBS),1:halo_ce_3925:1:1:1)
$(error XV_TYPED_SUBCLUSTER requires Halo CE, native clipping and the owner/census backend)
endif
endif
ifeq ($(XV_NATIVE_VISIBILITY_JOBS),1)
ifneq ($(RECOMP):$(GAME_PROFILE):$(XV_EXPERIMENTAL_OBJECT_JOBS):$(XV_LIGHT_QUERY_CENSUS):$(XV_TYPED_SUBCLUSTER),1:halo_ce_3925:1:1:1)
$(error XV_NATIVE_VISIBILITY_JOBS requires the Halo CE native subcluster and owner/worker backend)
endif
endif
ifeq ($(XV_NATIVE_VISIBILITY_PASS),1)
ifneq ($(RECOMP):$(GAME_PROFILE):$(XV_NATIVE_VISIBILITY_JOBS):$(XV_TYPED_SUBCLUSTER),1:halo_ce_3925:1:1)
$(error XV_NATIVE_VISIBILITY_PASS requires the Halo CE native visibility workers and subclusters)
endif
endif
ifeq ($(XV_NATIVE_VISIBILITY_PORTAL_LOOP),1)
ifneq ($(RECOMP):$(GAME_PROFILE),1:halo_ce_3925)
$(error XV_NATIVE_VISIBILITY_PORTAL_LOOP requires RECOMP=1 GAME_PROFILE=halo_ce_3925)
endif
endif
ifeq ($(XV_NATIVE_CONSTANT_PACK),1)
ifneq ($(RECOMP):$(GAME_PROFILE):$(XV_OWNER_PHASE):$(XV_EXPERIMENTAL_OBJECT_JOBS),1:halo_ce_3925:1:1)
$(error XV_NATIVE_CONSTANT_PACK requires CE, owner phase tracking and object jobs)
endif
endif
ifeq ($(XV_PALETTE_PREFIX_REUSE),1)
ifneq ($(RECOMP):$(XV_NATIVE_MODEL_PALETTE):$(GAME_PROFILE),1:1:halo_ce_3925)
$(error XV_PALETTE_PREFIX_REUSE requires RECOMP=1 XV_NATIVE_MODEL_PALETTE=1 GAME_PROFILE=halo_ce_3925)
endif
endif
ifeq ($(XV_SCENE_BUCKET0_DETAIL),1)
ifneq ($(RECOMP):$(XV_OWNER_PHASE):$(XV_SCENE_PARTITION):$(GAME_PROFILE),1:1:1:halo_ce_3925)
$(error XV_SCENE_BUCKET0_DETAIL requires RECOMP=1 XV_OWNER_PHASE=1 XV_SCENE_PARTITION=1 GAME_PROFILE=halo_ce_3925)
endif
endif
ifeq ($(XV_SCENE_BUCKET1_DETAIL),1)
ifneq ($(RECOMP):$(XV_OWNER_PHASE):$(XV_SCENE_PARTITION):$(GAME_PROFILE),1:1:1:halo_ce_3925)
$(error XV_SCENE_BUCKET1_DETAIL requires RECOMP=1 XV_OWNER_PHASE=1 XV_SCENE_PARTITION=1 GAME_PROFILE=halo_ce_3925)
endif
endif
XV_OBJECT_PASS_TIMING ?= 0
ifneq ($(words $(XV_OBJECT_PASS_TIMING)),1)
$(error XV_OBJECT_PASS_TIMING must be 0 or 1)
endif
ifneq ($(filter $(XV_OBJECT_PASS_TIMING),0 1),$(XV_OBJECT_PASS_TIMING))
$(error XV_OBJECT_PASS_TIMING must be 0 or 1)
endif
ifeq ($(XV_OBJECT_PASS_TIMING),1)
ifneq ($(RECOMP),1)
$(error XV_OBJECT_PASS_TIMING requires RECOMP=1)
endif
ifneq ($(XV_EXPERIMENTAL_OBJECT_JOBS) $(XV_OWNER_PHASE),1 1)
$(error XV_OBJECT_PASS_TIMING requires XV_EXPERIMENTAL_OBJECT_JOBS=1 XV_OWNER_PHASE=1)
endif
endif
ifeq ($(XV_SCENE_PARTITION),1)
ifneq ($(RECOMP):$(XV_OWNER_PHASE):$(GAME_PROFILE),1:1:halo_ce_3925)
$(error XV_SCENE_PARTITION requires RECOMP=1 XV_OWNER_PHASE=1 GAME_PROFILE=halo_ce_3925)
endif
endif
# Explicit private startup trial, not a persisted/user-facing graphics default.
XV_CLIP_REGION_TRIAL ?= 0
ifneq ($(words $(XV_CLIP_REGION_TRIAL)),1)
$(error XV_CLIP_REGION_TRIAL must be 0 or 1)
endif
ifneq ($(filter $(XV_CLIP_REGION_TRIAL),0 1),$(XV_CLIP_REGION_TRIAL))
$(error XV_CLIP_REGION_TRIAL must be 0 or 1)
endif
ifeq ($(XV_CLIP_REGION_TRIAL),1)
ifneq ($(RECOMP):$(GAME_PROFILE):$(XV_NATIVE_CLIP_REGION):$(XV_LIGHT_QUERY_CENSUS):$(XV_EXPERIMENTAL_OBJECT_JOBS),1:halo_ce_3925:1:1:1)
$(error XV_CLIP_REGION_TRIAL requires RECOMP=1 GAME_PROFILE=halo_ce_3925 XV_NATIVE_CLIP_REGION=1 XV_LIGHT_QUERY_CENSUS=1 XV_EXPERIMENTAL_OBJECT_JOBS=1)
endif
$(BUILD)/runtime/xv_benchmark.o: CFLAGS += -DXV_CLIP_REGION_TRIAL=1
endif
.PHONY: force-clip-region-trial-config
force-clip-region-trial-config:
$(BUILD)/clip-region-trial.config: force-clip-region-trial-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_CLIP_REGION_TRIAL)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/xv_benchmark.o: $(BUILD)/clip-region-trial.config

# Exact polygon-edge helper: separate private process-start selection.
XV_POLYGON_EDGE_TRIAL ?= 0
ifneq ($(words $(XV_POLYGON_EDGE_TRIAL)),1)
$(error XV_POLYGON_EDGE_TRIAL must be 0 or 1)
endif
ifneq ($(filter $(XV_POLYGON_EDGE_TRIAL),0 1),$(XV_POLYGON_EDGE_TRIAL))
$(error XV_POLYGON_EDGE_TRIAL must be 0 or 1)
endif
ifeq ($(XV_POLYGON_EDGE_TRIAL),1)
ifneq ($(RECOMP):$(GAME_PROFILE):$(XV_NATIVE_POLYGON_EDGE):$(XV_LIGHT_QUERY_CENSUS):$(XV_EXPERIMENTAL_OBJECT_JOBS),1:halo_ce_3925:1:1:1)
$(error XV_POLYGON_EDGE_TRIAL requires RECOMP=1 GAME_PROFILE=halo_ce_3925 XV_NATIVE_POLYGON_EDGE=1 XV_LIGHT_QUERY_CENSUS=1 XV_EXPERIMENTAL_OBJECT_JOBS=1)
endif
$(BUILD)/runtime/xv_benchmark.o: CFLAGS += -DXV_POLYGON_EDGE_TRIAL=1
endif
.PHONY: force-polygon-edge-trial-config
force-polygon-edge-trial-config:
$(BUILD)/polygon-edge-trial.config: force-polygon-edge-trial-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_POLYGON_EDGE_TRIAL)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/xv_benchmark.o: $(BUILD)/polygon-edge-trial.config

XV_MODEL_BATCHES_TRIAL ?= 0
ifneq ($(words $(XV_MODEL_BATCHES_TRIAL)),1)
$(error XV_MODEL_BATCHES_TRIAL must be 0 or 1)
endif
ifneq ($(filter $(XV_MODEL_BATCHES_TRIAL),0 1),$(XV_MODEL_BATCHES_TRIAL))
$(error XV_MODEL_BATCHES_TRIAL must be 0 or 1)
endif
ifeq ($(XV_MODEL_BATCHES_TRIAL),1)
ifneq ($(RECOMP),1)
$(error XV_MODEL_BATCHES_TRIAL requires RECOMP=1)
endif
ifneq ($(GAME_PROFILE),halo_ce_3925)
$(error XV_MODEL_BATCHES_TRIAL requires GAME_PROFILE=halo_ce_3925)
endif
ifneq ($(XV_NATIVE_MODEL_PALETTE) $(XV_NATIVE_MODEL_HIERARCHY),1 1)
$(error XV_MODEL_BATCHES_TRIAL requires XV_NATIVE_MODEL_PALETTE=1 XV_NATIVE_MODEL_HIERARCHY=1)
endif
$(BUILD)/runtime/main.o: CFLAGS += -DXV_MODEL_BATCHES_TRIAL=1 -DXV_NATIVE_MODEL_HIERARCHY
endif
.PHONY: force-model-batches-trial-config
force-model-batches-trial-config:
$(BUILD)/model-batches-trial.config: force-model-batches-trial-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_MODEL_BATCHES_TRIAL)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/main.o: $(BUILD)/model-batches-trial.config
ifeq ($(XV_QUERY_PREFIX_PUBLISH),1)
ifneq ($(GAME_PROFILE),halo_ce_3925)
$(error XV_QUERY_PREFIX_PUBLISH requires the Halo CE retained-generation Present contract)
endif
endif
ifeq ($(wildcard games/$(GAME_PROFILE)/runtime.mk),)
$(error No native runtime adapter for GAME_PROFILE=$(GAME_PROFILE))
endif
include games/$(GAME_PROFILE)/runtime.mk
ifeq ($(RECOMP),1)
CFLAGS    += -DXV_RUN_RECOMP -Irecomp -Irecomp/kernel
SRCS      := runtime/main.c runtime/xv_shader.c runtime/xv_d3d.c runtime/xv_ui_gxm.c runtime/xv_boot.c runtime/xv_log.c runtime/xv_benchmark.c runtime/xv_cpu.c runtime/xv_texture_worker.c runtime/xv_geometry_worker.c runtime/xv_gpu_upload.c runtime/xv_vertex_upload.c runtime/xv_vertex_prepare.c runtime/xv_vertex_capture.c runtime/xv_upload_worker.c runtime/xv_draw_profile.c runtime/xv_render_profile.c runtime/xv_settings.c dashboard/xv_dash.c
SRCS      += runtime/xv_remote.c runtime/xv_update.c runtime/xv_update_halo2.c runtime/xv_sha256.c
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

# Dedicated startup probe only; no ordinary-build PMU imports or frame work.
XV_CPU_PMON_PROBE ?= 0
ifneq ($(words $(XV_CPU_PMON_PROBE)),1)
$(error XV_CPU_PMON_PROBE must be 0 or 1)
endif
ifneq ($(filter $(XV_CPU_PMON_PROBE),0 1),$(XV_CPU_PMON_PROBE))
$(error XV_CPU_PMON_PROBE must be 0 or 1)
endif
$(BUILD)/runtime/main.o: CFLAGS += -DXV_CPU_PMON_PROBE=$(XV_CPU_PMON_PROBE)
.PHONY: force-pmon-probe-config
force-pmon-probe-config:
$(BUILD)/pmon-probe.config: force-pmon-probe-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_CPU_PMON_PROBE)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/runtime/main.o: $(BUILD)/pmon-probe.config
ifeq ($(XV_CPU_PMON_PROBE),1)
OBJS += $(BUILD)/runtime/xv_pmon_probe.o
DEPS += $(BUILD)/runtime/xv_pmon_probe.d
LIBS += -lScePerf_stub
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

.PHONY: version-force
$(BUILD)/xv_build.h: version-force version.json tools/gen_build_version.py
	$(PYTHON) tools/gen_build_version.py --output $@ $(if $(BUILD_REVISION),--revision $(BUILD_REVISION),)
VERSION_OBJS = $(BUILD)/runtime/main.o $(BUILD)/runtime/xv_ui_gxm.o $(BUILD)/runtime/xv_remote.o $(BUILD)/dashboard/xv_dash.o
$(VERSION_OBJS): $(BUILD)/xv_build.h
$(VERSION_OBJS): CFLAGS += -include $(abspath $(BUILD)/xv_build.h)

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

$(SFO): Makefile version.json | $(BUILD)
	vita-mksfoex -s TITLE_ID=$(TITLE_ID) -s APP_VER=$$($(PYTHON) tools/gen_build_version.py --sfo) $(SFO_EXTRA) "$(TITLE)" $@

# Stable updater boot helper; generated game code is never linked into it.
$(BUILD)/update-launcher.elf: runtime/xv_update_launcher.c runtime/xv_update.c runtime/xv_update_halo2.c runtime/xv_sha256.c runtime/xv_update.h runtime/xv_update_halo2.h runtime/xv_launch_args.h runtime/xv_sha256.h Makefile
	@mkdir -p $(BUILD)
	$(CC) -O2 -mthumb -Wall -Wextra -Iruntime $(LDFLAGS) -o $@ runtime/xv_update_launcher.c runtime/xv_update.c runtime/xv_update_halo2.c runtime/xv_sha256.c -lSceAppMgr_stub -lSceIofilemgr_stub -lScePower_stub -lSceProcessmgr_stub -lSceKernelThreadMgr_stub -lSceLibKernel_stub
$(BUILD)/update-launcher.velf: $(BUILD)/update-launcher.elf
	vita-elf-create $< $@
$(BUILD)/update-launcher.self: $(BUILD)/update-launcher.velf
	# Only this fixed-path helper needs app-directory write access. The game stays safe.
	vita-make-fself $< $@
ifeq ($(RECOMP),1)
UPDATE_LAUNCHER := $(BUILD)/update-launcher.self
endif

# VPK: eboot + param.sfo + shaders/*.gxp (+ sce_sys assets when present) ------
$(VPK): $(EBOOT) $(SFO) $(SHADER_PRESENT) $(SCE_SYS_FILES) $(SCENE_FILES) $(LICENSE_FILES) $(UPDATE_LAUNCHER) $(HALO2_PACKAGE) tools/package_vpk.py
ifneq ($(SHADER_MISSING),)
	@echo "warning: shader(s) not found, VPK built without them: $(SHADER_MISSING)"
	@echo "         (run 'make shaders' with psp2cgc on PATH, or drop prebuilt .gxp files in $(SHADER_DIR)/)"
endif
ifeq ($(RECOMP),1)
	$(PYTHON) tools/package_vpk.py --root . --eboot $(EBOOT) --sfo $(SFO) --launcher $(UPDATE_LAUNCHER) $(if $(HALO2_PACKAGE),--halo2-package $(HALO2_PACKAGE),) --output $@
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
# Discover the two selected generated units, not every guest object.
OWNER_PHASE_HOOK_SRCS := $(shell rg -l 'XV_OWNER_PHASE_SCOPE' $(RECOMP_DIR)/code_*.c 2>/dev/null)
OWNER_PHASE_HOOK_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(OWNER_PHASE_HOOK_SRCS))
OWNER_PHASE_SYS_OBJS := $(RECOMP_BUILD)/kernel/xd3d.o $(RECOMP_BUILD)/kernel/xk_owner_phase.o
ifeq ($(XV_OWNER_PHASE),1)
ifneq ($(GAME_PROFILE),halo_ce_3925)
$(error XV_OWNER_PHASE requires GAME_PROFILE=halo_ce_3925)
endif
ifneq ($(words $(shell rg -o 'XV_OWNER_PHASE_SCOPE:' $(OWNER_PHASE_HOOK_SRCS) 2>/dev/null)),2)
$(error XV_OWNER_PHASE requires the two regenerated FA920/BCB30 owner scope hooks)
endif
$(OWNER_PHASE_HOOK_OBJS) $(OWNER_PHASE_SYS_OBJS): RECOMP_CFLAGS += -DXV_OWNER_PHASE
$(RECOMP_BUILD)/kernel/xk_owner_phase.o: RECOMP_CFLAGS += -DXV_OWNER_PHASE_DEFAULT=$(XV_OWNER_PHASE_DEFAULT)
endif
$(OWNER_PHASE_HOOK_OBJS) $(OWNER_PHASE_SYS_OBJS): $(BUILD)/owner-phase.config recomp/kernel/xk_owner_phase.h
$(RECOMP_BUILD)/kernel/xk_owner_phase.o: $(BUILD)/owner-phase-startup.config
$(RECOMP_BUILD)/libxita_sys.a $(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/libxita_guest.a $(RECOMP_BUILD)/librecomp.a: $(BUILD)/owner-phase.config
# Primary-only fog memo, with compile-mode transitions scoped to its users.
ifneq ($(words $(XV_MODEL_FOG)),1)
$(error XV_MODEL_FOG must be 0 or 1)
endif
ifneq ($(filter $(XV_MODEL_FOG),0 1),$(XV_MODEL_FOG))
$(error XV_MODEL_FOG must be 0 or 1)
endif
MODEL_FOG_SRCS := $(shell rg -l 'XV_MODEL_FOG_SCOPE:' $(RECOMP_DIR)/code_*.c 2>/dev/null)
MODEL_FOG_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(MODEL_FOG_SRCS))
ifeq ($(XV_MODEL_FOG),1)
ifneq ($(RECOMP):$(XV_OWNER_PHASE):$(GAME_PROFILE),1:1:halo_ce_3925)
$(error XV_MODEL_FOG requires RECOMP=1 XV_OWNER_PHASE=1 GAME_PROFILE=halo_ce_3925)
endif
ifneq ($(words $(shell rg -o 'XV_MODEL_FOG_SCOPE:' $(MODEL_FOG_SRCS) 2>/dev/null)),1)
$(error XV_MODEL_FOG requires the regenerated primary 70110 fog hook)
endif
$(MODEL_FOG_OBJS) $(RECOMP_BUILD)/kernel/xk_model_fog.o $(RECOMP_BUILD)/kernel/xk_owner_phase.o: RECOMP_CFLAGS += -DXV_MODEL_FOG=1
$(RECOMP_BUILD)/kernel/xk_model_fog.o: RECOMP_CFLAGS += -DXV_OWNER_PHASE
endif
.PHONY: force-model-fog-config
force-model-fog-config:
$(RECOMP_BUILD)/model-fog.config: force-model-fog-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_MODEL_FOG)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(MODEL_FOG_OBJS) $(RECOMP_BUILD)/kernel/xk_model_fog.o $(RECOMP_BUILD)/kernel/xk_owner_phase.o: $(RECOMP_BUILD)/model-fog.config
$(RECOMP_BUILD)/kernel/xk_model_fog.o: recomp/kernel/xk_model_fog.h
$(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/libxita_guest.a $(RECOMP_BUILD)/librecomp.a: $(RECOMP_BUILD)/model-fog.config
# Selected-model common-UV reuse, with compile-mode transitions scoped to its users.
ifneq ($(words $(XV_MODEL_UV)),1)
$(error XV_MODEL_UV must be 0 or 1)
endif
ifneq ($(filter $(XV_MODEL_UV),0 1),$(XV_MODEL_UV))
$(error XV_MODEL_UV must be 0 or 1)
endif
MODEL_UV_SRCS := $(shell rg -l 'XV_MODEL_UV_(SCOPE|CALLS):' $(RECOMP_DIR)/code_*.c 2>/dev/null)
MODEL_UV_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(MODEL_UV_SRCS))
ifeq ($(XV_MODEL_UV),1)
ifneq ($(RECOMP):$(XV_OWNER_PHASE):$(GAME_PROFILE),1:1:halo_ce_3925)
$(error XV_MODEL_UV requires RECOMP=1 XV_OWNER_PHASE=1 GAME_PROFILE=halo_ce_3925)
endif
ifneq ($(words $(shell rg -o 'XV_MODEL_UV_(SCOPE|CALLS):' $(MODEL_UV_SRCS) 2>/dev/null)),2)
$(error XV_MODEL_UV requires the regenerated primary A26B0 and 70110 UV hooks)
endif
$(MODEL_UV_OBJS) $(RECOMP_BUILD)/kernel/xk_model_uv.o $(RECOMP_BUILD)/kernel/xk_owner_phase.o: RECOMP_CFLAGS += -DXV_MODEL_UV=1
$(RECOMP_BUILD)/kernel/xk_model_uv.o: RECOMP_CFLAGS += -DXV_OWNER_PHASE
endif
.PHONY: force-model-uv-config
force-model-uv-config:
$(RECOMP_BUILD)/model-uv.config: force-model-uv-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_MODEL_UV)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(MODEL_UV_OBJS) $(RECOMP_BUILD)/kernel/xk_model_uv.o $(RECOMP_BUILD)/kernel/xk_owner_phase.o: $(RECOMP_BUILD)/model-uv.config
$(RECOMP_BUILD)/kernel/xk_model_uv.o: recomp/kernel/xk_model_uv.h
$(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/libxita_guest.a $(RECOMP_BUILD)/librecomp.a: $(RECOMP_BUILD)/model-uv.config
# Cross-model value retention changes only helper/owner code, never guest hooks.
ifneq ($(words $(XV_MODEL_UV_CROSS_MODEL)),1)
$(error XV_MODEL_UV_CROSS_MODEL must be 0 or 1)
endif
ifneq ($(filter $(XV_MODEL_UV_CROSS_MODEL),0 1),$(XV_MODEL_UV_CROSS_MODEL))
$(error XV_MODEL_UV_CROSS_MODEL must be 0 or 1)
endif
ifeq ($(XV_MODEL_UV_CROSS_MODEL),1)
ifneq ($(XV_MODEL_UV),1)
$(error XV_MODEL_UV_CROSS_MODEL requires XV_MODEL_UV=1)
endif
$(RECOMP_BUILD)/kernel/xk_model_uv.o $(RECOMP_BUILD)/kernel/xk_owner_phase.o: RECOMP_CFLAGS += -DXV_MODEL_UV_CROSS_MODEL=1
endif
.PHONY: force-model-uv-cross-config
force-model-uv-cross-config:
$(RECOMP_BUILD)/model-uv-cross.config: force-model-uv-cross-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_MODEL_UV_CROSS_MODEL)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_model_uv.o $(RECOMP_BUILD)/kernel/xk_owner_phase.o: $(RECOMP_BUILD)/model-uv-cross.config recomp/kernel/xk_model_uv.h
$(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/libxita_guest.a $(RECOMP_BUILD)/librecomp.a: $(RECOMP_BUILD)/model-uv-cross.config
# Only the selected scene unit and existing observer own this opt-in flag.
SCENE_PARTITION_SRCS := $(shell rg -l 'XV_SCENE_PARTITION_SCOPE:' $(RECOMP_DIR)/code_*.c 2>/dev/null)
SCENE_PARTITION_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(SCENE_PARTITION_SRCS))
ifeq ($(XV_SCENE_PARTITION),1)
ifneq ($(words $(shell rg -o 'XV_SCENE_PARTITION_SCOPE:' $(SCENE_PARTITION_SRCS) 2>/dev/null)),1)
$(error XV_SCENE_PARTITION requires the selectively regenerated primary 5D410 scope)
endif
$(SCENE_PARTITION_OBJS) $(RECOMP_BUILD)/kernel/xk_owner_phase.o: RECOMP_CFLAGS += -DXV_SCENE_PARTITION=1
endif
$(SCENE_PARTITION_OBJS) $(RECOMP_BUILD)/kernel/xk_owner_phase.o: $(BUILD)/scene-partition.config
$(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/libxita_guest.a $(RECOMP_BUILD)/librecomp.a: $(BUILD)/scene-partition.config
ifeq ($(XV_SCENE_BUCKET0_DETAIL),1)
ifneq ($(words $(shell rg -o 'XV_SCENE_BUCKET0_DETAIL_SCOPE:' $(SCENE_PARTITION_SRCS) 2>/dev/null)),1)
$(error XV_SCENE_BUCKET0_DETAIL requires the selectively regenerated five bucket0 cuts)
endif
$(SCENE_PARTITION_OBJS) $(RECOMP_BUILD)/kernel/xk_owner_phase.o: RECOMP_CFLAGS += -DXV_SCENE_BUCKET0_DETAIL=1
endif
$(SCENE_PARTITION_OBJS) $(RECOMP_BUILD)/kernel/xk_owner_phase.o: $(BUILD)/scene-bucket0-detail.config
$(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/libxita_guest.a $(RECOMP_BUILD)/librecomp.a: $(BUILD)/scene-bucket0-detail.config
ifeq ($(XV_SCENE_BUCKET1_DETAIL),1)
ifneq ($(words $(shell rg -o 'XV_SCENE_BUCKET1_DETAIL_SCOPE:' $(SCENE_PARTITION_SRCS) 2>/dev/null)),1)
$(error XV_SCENE_BUCKET1_DETAIL requires the selectively regenerated eleven bucket1 cuts)
endif
$(SCENE_PARTITION_OBJS) $(RECOMP_BUILD)/kernel/xk_owner_phase.o $(RECOMP_BUILD)/kernel/xd3d.o: RECOMP_CFLAGS += -DXV_SCENE_BUCKET1_DETAIL=1
endif
$(SCENE_PARTITION_OBJS) $(RECOMP_BUILD)/kernel/xk_owner_phase.o $(RECOMP_BUILD)/kernel/xd3d.o: $(BUILD)/scene-bucket1-detail.config
$(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/libxita_guest.a $(RECOMP_BUILD)/librecomp.a: $(BUILD)/scene-bucket1-detail.config
# No guest header or global guest flags change for this optional loop.
VISIBILITY_PORTAL_SRCS := $(shell rg -l 'XV_NATIVE_VISIBILITY_PORTAL_LOOP_SCOPE:' $(RECOMP_DIR)/code_*.c 2>/dev/null)
VISIBILITY_PORTAL_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(VISIBILITY_PORTAL_SRCS))
ifeq ($(XV_NATIVE_VISIBILITY_PORTAL_LOOP),1)
ifneq ($(words $(VISIBILITY_PORTAL_SRCS)),1)
$(error XV_NATIVE_VISIBILITY_PORTAL_LOOP requires exactly one selectively regenerated guest unit)
endif
ifneq ($(words $(shell rg -o 'XV_NATIVE_VISIBILITY_PORTAL_LOOP_SCOPE:' $(VISIBILITY_PORTAL_SRCS) 2>/dev/null)),1)
$(error XV_NATIVE_VISIBILITY_PORTAL_LOOP requires the selectively regenerated primary 532E0 loop)
endif
$(VISIBILITY_PORTAL_OBJS): RECOMP_CFLAGS += -DXV_NATIVE_VISIBILITY_PORTAL_LOOP=1
endif
$(VISIBILITY_PORTAL_OBJS): $(BUILD)/native-visibility-portal.config
$(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/libxita_guest.a $(RECOMP_BUILD)/librecomp.a: $(BUILD)/native-visibility-portal.config
# Typed clipping replaces one pinned call, leaving interior and other callers.
TYPED_PORTAL_SRCS := $(shell rg -l 'XV_TYPED_PORTAL_POLYGON_SCOPE:' $(RECOMP_DIR)/code_*.c 2>/dev/null)
TYPED_PORTAL_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(TYPED_PORTAL_SRCS))
ifeq ($(XV_TYPED_PORTAL_POLYGON),1)
ifneq ($(words $(TYPED_PORTAL_SRCS)),1)
$(error XV_TYPED_PORTAL_POLYGON requires one selectively prepared portal unit)
endif
ifneq ($(words $(shell rg -o 'XV_TYPED_PORTAL_POLYGON_SCOPE:' $(TYPED_PORTAL_SRCS) 2>/dev/null)),1)
$(error XV_TYPED_PORTAL_POLYGON requires exactly one callsite marker)
endif
endif
.PHONY: force-typed-portal-config
force-typed-portal-config:
$(RECOMP_BUILD)/typed-portal.config: force-typed-portal-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_TYPED_PORTAL_POLYGON)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(TYPED_PORTAL_OBJS) $(RECOMP_BUILD)/kernel/xk_clip_region_control.o: RECOMP_CFLAGS += -DXV_TYPED_PORTAL_POLYGON=$(XV_TYPED_PORTAL_POLYGON)
$(TYPED_PORTAL_OBJS) $(RECOMP_BUILD)/kernel/xk_clip_region_control.o $(RECOMP_BUILD)/kernel/xk_portal_polygon.o $(RECOMP_BUILD)/kernel/xk_portal_polygon_math.o: $(RECOMP_BUILD)/typed-portal.config
$(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/libxita_guest.a $(RECOMP_BUILD)/librecomp.a: $(RECOMP_BUILD)/typed-portal.config
$(RECOMP_BUILD)/kernel/xk_portal_polygon_math.o: RECOMP_CFLAGS += -ffp-contract=off -frounding-math
# Subcluster classification and ordered publication use one pinned caller.
SUBCLUSTER_SRCS := $(shell rg -l 'XV_TYPED_SUBCLUSTER_SCOPE:' $(RECOMP_DIR)/code_*.c 2>/dev/null)
SUBCLUSTER_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(SUBCLUSTER_SRCS))
ifeq ($(XV_TYPED_SUBCLUSTER),1)
ifneq ($(words $(SUBCLUSTER_SRCS)),1)
$(error XV_TYPED_SUBCLUSTER requires one selectively prepared caller unit)
endif
ifneq ($(words $(shell rg -o 'XV_TYPED_SUBCLUSTER_SCOPE:' $(SUBCLUSTER_SRCS) 2>/dev/null)),1)
$(error XV_TYPED_SUBCLUSTER requires exactly one scope marker)
endif
endif
.PHONY: force-subcluster-config
force-subcluster-config:
$(RECOMP_BUILD)/subcluster.config: force-subcluster-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_TYPED_SUBCLUSTER)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(SUBCLUSTER_OBJS) $(RECOMP_BUILD)/kernel/xk_clip_region_control.o: RECOMP_CFLAGS += -DXV_TYPED_SUBCLUSTER=$(XV_TYPED_SUBCLUSTER)
$(SUBCLUSTER_OBJS) $(RECOMP_BUILD)/kernel/xk_clip_region_control.o $(RECOMP_BUILD)/kernel/xk_subcluster.o $(RECOMP_BUILD)/kernel/xk_subcluster_math.o: $(RECOMP_BUILD)/subcluster.config
$(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/libxita_guest.a $(RECOMP_BUILD)/librecomp.a: $(RECOMP_BUILD)/subcluster.config
$(RECOMP_BUILD)/kernel/xk_subcluster_math.o: RECOMP_CFLAGS += -ffp-contract=off -frounding-math
.PHONY: force-visibility-jobs-config
force-visibility-jobs-config:
$(RECOMP_BUILD)/visibility-jobs.config: force-visibility-jobs-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_NATIVE_VISIBILITY_JOBS)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: RECOMP_CFLAGS += -DXV_NATIVE_VISIBILITY_JOBS=$(XV_NATIVE_VISIBILITY_JOBS)
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: $(RECOMP_BUILD)/visibility-jobs.config
$(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/libxita_guest.a $(RECOMP_BUILD)/librecomp.a: $(RECOMP_BUILD)/visibility-jobs.config
VISIBILITY_PASS_SRCS := $(shell rg -l 'XV_NATIVE_VISIBILITY_PASS_SCOPE:' $(RECOMP_DIR)/code_*.c 2>/dev/null)
VISIBILITY_PASS_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(VISIBILITY_PASS_SRCS))
ifeq ($(XV_NATIVE_VISIBILITY_PASS),1)
ifneq ($(words $(VISIBILITY_PASS_SRCS)),1)
$(error XV_NATIVE_VISIBILITY_PASS requires one selectively prepared caller unit)
endif
ifneq ($(words $(shell rg -o 'XV_NATIVE_VISIBILITY_PASS_SCOPE:' $(VISIBILITY_PASS_SRCS) 2>/dev/null)),1)
$(error XV_NATIVE_VISIBILITY_PASS requires exactly one scope marker)
endif
endif
.PHONY: force-visibility-pass-config
force-visibility-pass-config:
$(RECOMP_BUILD)/visibility-pass.config: force-visibility-pass-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_NATIVE_VISIBILITY_PASS)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(VISIBILITY_PASS_OBJS) $(RECOMP_BUILD)/kernel/xk_clip_region_control.o: RECOMP_CFLAGS += -DXV_NATIVE_VISIBILITY_PASS=$(XV_NATIVE_VISIBILITY_PASS)
$(VISIBILITY_PASS_OBJS) $(RECOMP_BUILD)/kernel/xk_clip_region_control.o $(RECOMP_BUILD)/kernel/xk_visibility_pass.o: $(RECOMP_BUILD)/visibility-pass.config
$(RECOMP_BUILD)/kernel/xk_visibility_pass.o: RECOMP_CFLAGS += -DXV_NATIVE_VISIBILITY_JOBS=1
$(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/libxita_guest.a $(RECOMP_BUILD)/librecomp.a: $(RECOMP_BUILD)/visibility-pass.config
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
ifeq ($(XV_NATIVE_COLLISION_TRAVERSAL),1)
RECOMP_CFLAGS += -DXV_NATIVE_COLLISION_TRAVERSAL
endif
XV_NATIVE_COLLISION_TRAVERSAL_DEFAULT ?= 0
ifneq ($(words $(XV_NATIVE_COLLISION_TRAVERSAL_DEFAULT)),1)
$(error XV_NATIVE_COLLISION_TRAVERSAL_DEFAULT must be 0 or 1)
endif
ifneq ($(filter $(XV_NATIVE_COLLISION_TRAVERSAL_DEFAULT),0 1),$(XV_NATIVE_COLLISION_TRAVERSAL_DEFAULT))
$(error XV_NATIVE_COLLISION_TRAVERSAL_DEFAULT must be 0 or 1)
endif
COLLISION_TRAVERSAL_HOOK_SRCS := $(shell grep -l XV_NATIVE_COLLISION_TRAVERSAL $(XITA_GUEST_SRCS) 2>/dev/null)
COLLISION_TRAVERSAL_HOOK_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(COLLISION_TRAVERSAL_HOOK_SRCS))
.PHONY: force-collision-traversal-config force-collision-traversal-startup-config
force-collision-traversal-config:
force-collision-traversal-startup-config:
$(RECOMP_BUILD)/collision-traversal.config: force-collision-traversal-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(if $(filter 1,$(XV_NATIVE_COLLISION_TRAVERSAL)),1,0)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/collision-traversal-startup.config: force-collision-traversal-startup-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_NATIVE_COLLISION_TRAVERSAL_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(COLLISION_TRAVERSAL_HOOK_OBJS) $(RECOMP_BUILD)/kernel/xk_collision_traversal_control.o: recomp/kernel/xk_collision_traversal.h $(RECOMP_BUILD)/collision-traversal.config
$(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/librecomp.a: $(RECOMP_BUILD)/collision-traversal.config
$(RECOMP_BUILD)/kernel/xk_collision_traversal_control.o: $(RECOMP_BUILD)/collision-traversal-startup.config
$(RECOMP_BUILD)/kernel/xk_collision_traversal_control.o: RECOMP_CFLAGS += -DXV_NATIVE_COLLISION_TRAVERSAL_DEFAULT=$(XV_NATIVE_COLLISION_TRAVERSAL_DEFAULT)
# Qualified caller-specific collision query fusion. No runtime controls, and no
# flag on generic code_013/code_016: their compiler layout must stay unchanged.
XV_NATIVE_SOLVER_FUSION ?= 0
ifneq ($(words $(XV_NATIVE_SOLVER_FUSION)),1)
$(error XV_NATIVE_SOLVER_FUSION must be 0 or 1)
endif
ifneq ($(filter $(XV_NATIVE_SOLVER_FUSION),0 1),$(XV_NATIVE_SOLVER_FUSION))
$(error XV_NATIVE_SOLVER_FUSION must be 0 or 1)
endif
SOLVER_FUSION_OBJECTS := $(RECOMP_BUILD)/code_028.o $(RECOMP_BUILD)/solver_fusion.o
.PHONY: force-solver-fusion-config solver-fusion-generate
force-solver-fusion-config:
$(RECOMP_BUILD)/solver-fusion.config: force-solver-fusion-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_NATIVE_SOLVER_FUSION)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(SOLVER_FUSION_OBJECTS) $(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/libxita_guest.a $(RECOMP_BUILD)/librecomp.a: $(RECOMP_BUILD)/solver-fusion.config
ifeq ($(XV_NATIVE_SOLVER_FUSION),1)
ifneq ($(XV_NATIVE_QUERY_FUSION),1)
$(error XV_NATIVE_SOLVER_FUSION requires XV_NATIVE_QUERY_FUSION=1)
endif
ifeq ($(XV_OBJECT_SOLVER_EXPERIMENT),1)
$(error XV_NATIVE_SOLVER_FUSION must retain the actor transaction; disable XV_OBJECT_SOLVER_EXPERIMENT)
endif
$(SOLVER_FUSION_OBJECTS): RECOMP_CFLAGS += -DXV_NATIVE_SOLVER_FUSION=1
$(RECOMP_BUILD)/solver_fusion.o: RECOMP_CFLAGS += -ffunction-sections -fdata-sections -fstack-usage
$(SOLVER_FUSION_OBJECTS): $(RECOMP_BUILD)/query-fusion.generated.json
endif
XV_NATIVE_QUERY_FUSION ?= 0
ifneq ($(words $(XV_NATIVE_QUERY_FUSION)),1)
$(error XV_NATIVE_QUERY_FUSION must be 0 or 1)
endif
ifneq ($(filter $(XV_NATIVE_QUERY_FUSION),0 1),$(XV_NATIVE_QUERY_FUSION))
$(error XV_NATIVE_QUERY_FUSION must be 0 or 1)
endif
# Query-only copies of two unchanged f32 primitives. No shared compiler flag.
XV_QUERY_F32_INLINE ?= 0
ifneq ($(words $(XV_QUERY_F32_INLINE)),1)
$(error XV_QUERY_F32_INLINE must be 0 or 1)
endif
ifneq ($(filter $(XV_QUERY_F32_INLINE),0 1),$(XV_QUERY_F32_INLINE))
$(error XV_QUERY_F32_INLINE must be 0 or 1)
endif
ifeq ($(XV_QUERY_F32_INLINE),1)
ifneq ($(XV_NATIVE_QUERY_FUSION),1)
$(error XV_QUERY_F32_INLINE requires XV_NATIVE_QUERY_FUSION=1)
endif
endif
# One exact-state SSE interval, composed only inside the separate query unit.
XV_QUERY_SEMANTIC_LEAF ?= 0
ifneq ($(words $(XV_QUERY_SEMANTIC_LEAF)),1)
$(error XV_QUERY_SEMANTIC_LEAF must be 0 or 1)
endif
ifneq ($(filter $(XV_QUERY_SEMANTIC_LEAF),0 1),$(XV_QUERY_SEMANTIC_LEAF))
$(error XV_QUERY_SEMANTIC_LEAF must be 0 or 1)
endif
ifeq ($(XV_QUERY_SEMANTIC_LEAF),1)
ifneq ($(XV_QUERY_F32_INLINE),1)
$(error XV_QUERY_SEMANTIC_LEAF requires XV_QUERY_F32_INLINE=1)
endif
endif
.PHONY: force-query-semantic-config
force-query-semantic-config:
$(RECOMP_BUILD)/query-semantic.config: force-query-semantic-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_QUERY_SEMANTIC_LEAF)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
# Ordered edge membership only inside the qualified semantic query unit.
XV_QUERY_MEMBERSHIP_SCALAR ?= 0
ifneq ($(words $(XV_QUERY_MEMBERSHIP_SCALAR)),1)
$(error XV_QUERY_MEMBERSHIP_SCALAR must be 0 or 1)
endif
ifneq ($(filter $(XV_QUERY_MEMBERSHIP_SCALAR),0 1),$(XV_QUERY_MEMBERSHIP_SCALAR))
$(error XV_QUERY_MEMBERSHIP_SCALAR must be 0 or 1)
endif
ifeq ($(XV_QUERY_MEMBERSHIP_SCALAR),1)
ifneq ($(XV_QUERY_SEMANTIC_LEAF),1)
$(error XV_QUERY_MEMBERSHIP_SCALAR requires XV_QUERY_SEMANTIC_LEAF=1)
endif
$(RECOMP_BUILD)/query_fusion.o: RECOMP_CFLAGS += -DXV_QUERY_MEMBERSHIP_SCALAR=1
endif
.PHONY: force-query-membership-config
force-query-membership-config:
$(RECOMP_BUILD)/query-membership.config: force-query-membership-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_QUERY_MEMBERSHIP_SCALAR)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
# Ordered ancestor membership, composed after the qualified edge scalar path.
XV_QUERY_ANCESTOR_SCALAR ?= 0
ifneq ($(words $(XV_QUERY_ANCESTOR_SCALAR)),1)
$(error XV_QUERY_ANCESTOR_SCALAR must be 0 or 1)
endif
ifneq ($(filter $(XV_QUERY_ANCESTOR_SCALAR),0 1),$(XV_QUERY_ANCESTOR_SCALAR))
$(error XV_QUERY_ANCESTOR_SCALAR must be 0 or 1)
endif
ifeq ($(XV_QUERY_ANCESTOR_SCALAR),1)
ifneq ($(XV_QUERY_MEMBERSHIP_SCALAR),1)
$(error XV_QUERY_ANCESTOR_SCALAR requires XV_QUERY_MEMBERSHIP_SCALAR=1)
endif
$(RECOMP_BUILD)/query_fusion.o: RECOMP_CFLAGS += -DXV_QUERY_ANCESTOR_SCALAR=1
endif
.PHONY: force-query-ancestor-config
force-query-ancestor-config:
$(RECOMP_BUILD)/query-ancestor.config: force-query-ancestor-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_QUERY_ANCESTOR_SCALAR)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
# Extend the complete query only at the qualified primary object-space caller.
XV_QUERY_OBJECT_SPACE ?= 0
ifneq ($(words $(XV_QUERY_OBJECT_SPACE)),1)
$(error XV_QUERY_OBJECT_SPACE must be 0 or 1)
endif
ifneq ($(filter $(XV_QUERY_OBJECT_SPACE),0 1),$(XV_QUERY_OBJECT_SPACE))
$(error XV_QUERY_OBJECT_SPACE must be 0 or 1)
endif
ifeq ($(XV_QUERY_OBJECT_SPACE),1)
ifneq ($(XV_NATIVE_QUERY_FUSION),1)
$(error XV_QUERY_OBJECT_SPACE requires XV_NATIVE_QUERY_FUSION=1)
endif
$(RECOMP_BUILD)/code_028.o $(RECOMP_BUILD)/query_fusion.o: RECOMP_CFLAGS += -DXV_QUERY_OBJECT_SPACE=1
endif
.PHONY: force-query-object-config
force-query-object-config:
$(RECOMP_BUILD)/query-object.config: force-query-object-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_QUERY_OBJECT_SPACE)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
# Single-child world BSP runs; retain the outer actor transaction and callbacks.
XV_QUERY_WORLD_RUN ?= 0
ifneq ($(words $(XV_QUERY_WORLD_RUN)),1)
$(error XV_QUERY_WORLD_RUN must be 0 or 1)
endif
ifneq ($(filter $(XV_QUERY_WORLD_RUN),0 1),$(XV_QUERY_WORLD_RUN))
$(error XV_QUERY_WORLD_RUN must be 0 or 1)
endif
ifeq ($(XV_QUERY_WORLD_RUN),1)
ifneq ($(XV_QUERY_OBJECT_SPACE) $(XV_QUERY_ANCESTOR_SCALAR) $(XV_EXPERIMENTAL_OBJECT_JOBS),1 1 1)
$(error XV_QUERY_WORLD_RUN requires XV_QUERY_OBJECT_SPACE=1 XV_QUERY_ANCESTOR_SCALAR=1 XV_EXPERIMENTAL_OBJECT_JOBS=1)
endif
$(RECOMP_BUILD)/query_fusion.o $(RECOMP_BUILD)/kernel/xk_object_jobs.o: RECOMP_CFLAGS += -DXV_QUERY_WORLD_RUN=1
endif
.PHONY: force-query-world-run-config
force-query-world-run-config:
$(RECOMP_BUILD)/query-world-run.config: force-query-world-run-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_QUERY_WORLD_RUN)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: $(RECOMP_BUILD)/query-world-run.config
# Input-only world-query repetition observer; no cached results or lock changes.
XV_QUERY_REPEAT_CENSUS ?= 0
ifneq ($(words $(XV_QUERY_REPEAT_CENSUS)),1)
$(error XV_QUERY_REPEAT_CENSUS must be 0 or 1)
endif
ifneq ($(filter $(XV_QUERY_REPEAT_CENSUS),0 1),$(XV_QUERY_REPEAT_CENSUS))
$(error XV_QUERY_REPEAT_CENSUS must be 0 or 1)
endif
ifeq ($(XV_QUERY_REPEAT_CENSUS),1)
ifneq ($(XV_QUERY_WORLD_RUN),1)
$(error XV_QUERY_REPEAT_CENSUS requires XV_QUERY_WORLD_RUN=1)
endif
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: RECOMP_CFLAGS += -DXV_QUERY_REPEAT_CENSUS=1
endif
.PHONY: force-query-repeat-config
force-query-repeat-config:
$(RECOMP_BUILD)/query-repeat.config: force-query-repeat-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_QUERY_REPEAT_CENSUS)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: $(RECOMP_BUILD)/query-repeat.config
# Selective exact world-query reuse; ordinary builds retain their original TU.
XV_QUERY_REUSE ?= 0
ifneq ($(words $(XV_QUERY_REUSE)),1)
$(error XV_QUERY_REUSE must be 0 or 1)
endif
ifneq ($(filter $(XV_QUERY_REUSE),0 1),$(XV_QUERY_REUSE))
$(error XV_QUERY_REUSE must be 0 or 1)
endif
ifeq ($(XV_QUERY_REUSE),1)
ifneq ($(XV_QUERY_WORLD_RUN) $(XV_QUERY_F32_INLINE) $(XV_NATIVE_SOLVER_FUSION),1 1 1)
$(error XV_QUERY_REUSE requires the qualified world-run/f32/solver build)
endif
ifeq ($(XV_QUERY_REPEAT_CENSUS),1)
$(error XV_QUERY_REUSE and XV_QUERY_REPEAT_CENSUS are separate trials)
endif
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: RECOMP_CFLAGS += -DXV_QUERY_REUSE=1
$(RECOMP_BUILD)/query_capture.o: RECOMP_CFLAGS += -DXV_NATIVE_QUERY_FUSION=1 -DXV_QUERY_WORLD_RUN=1 -DXV_QUERY_OBJECT_SPACE=1 -DXV_QUERY_ANCESTOR_SCALAR=1 -DXV_QUERY_MEMBERSHIP_SCALAR=1
$(RECOMP_BUILD)/query_capture.o: $(RECOMP_BUILD)/query-fusion.generated.json $(RECOMP_BUILD)/object-hold.config
endif
.PHONY: force-query-reuse-config
force-query-reuse-config:
$(RECOMP_BUILD)/query-reuse.config: force-query-reuse-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_QUERY_REUSE)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_object_jobs.o $(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/librecomp.a: $(RECOMP_BUILD)/query-reuse.config
# World-route BSP query released from the actor guard (research trial).
XV_QUERY_UNLOCK ?= 0
XV_QUERY_UNLOCK_DEFAULT ?= 0
ifneq ($(filter $(XV_QUERY_UNLOCK),0 1),$(XV_QUERY_UNLOCK))
$(error XV_QUERY_UNLOCK must be 0 or 1)
endif
ifneq ($(filter $(XV_QUERY_UNLOCK_DEFAULT),0 1),$(XV_QUERY_UNLOCK_DEFAULT))
$(error XV_QUERY_UNLOCK_DEFAULT must be 0 or 1)
endif
ifeq ($(XV_QUERY_UNLOCK),1)
ifneq ($(XV_EXPERIMENTAL_OBJECT_JOBS) $(XV_QUERY_REUSE) $(XV_NATIVE_QUERY_FUSION),1 1 1)
$(error XV_QUERY_UNLOCK requires XV_EXPERIMENTAL_OBJECT_JOBS=1 XV_QUERY_REUSE=1 XV_NATIVE_QUERY_FUSION=1)
endif
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: RECOMP_CFLAGS += -DXV_QUERY_UNLOCK=1 -DXV_QUERY_UNLOCK_DEFAULT=$(XV_QUERY_UNLOCK_DEFAULT)
$(RECOMP_BUILD)/kernel/xk_query_reuse.o: RECOMP_CFLAGS += -DXV_QUERY_UNLOCK=1
endif
.PHONY: force-query-unlock-config
force-query-unlock-config:
$(RECOMP_BUILD)/query-unlock.config: force-query-unlock-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_QUERY_UNLOCK):$(XV_QUERY_UNLOCK_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_object_jobs.o $(RECOMP_BUILD)/kernel/xk_query_reuse.o: $(RECOMP_BUILD)/query-unlock.config
# Heavy/light object-job lane split (research trial).
XV_OBJECT_JOB_SPLIT ?= 0
XV_OBJECT_JOB_SPLIT_DEFAULT ?= 0
ifneq ($(filter $(XV_OBJECT_JOB_SPLIT),0 1),$(XV_OBJECT_JOB_SPLIT))
$(error XV_OBJECT_JOB_SPLIT must be 0 or 1)
endif
ifneq ($(filter $(XV_OBJECT_JOB_SPLIT_DEFAULT),0 1),$(XV_OBJECT_JOB_SPLIT_DEFAULT))
$(error XV_OBJECT_JOB_SPLIT_DEFAULT must be 0 or 1)
endif
ifeq ($(XV_OBJECT_JOB_SPLIT),1)
ifneq ($(XV_EXPERIMENTAL_OBJECT_JOBS),1)
$(error XV_OBJECT_JOB_SPLIT requires XV_EXPERIMENTAL_OBJECT_JOBS=1)
endif
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: RECOMP_CFLAGS += -DXV_OBJECT_JOB_SPLIT=1 -DXV_OBJECT_JOB_SPLIT_DEFAULT=$(XV_OBJECT_JOB_SPLIT_DEFAULT)
endif
.PHONY: force-job-split-config
force-job-split-config:
$(RECOMP_BUILD)/job-split.config: force-job-split-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_OBJECT_JOB_SPLIT):$(XV_OBJECT_JOB_SPLIT_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: $(RECOMP_BUILD)/job-split.config
# Owner participation in two-worker object batches (research trial).
XV_OBJECT_OWNER_LANE ?= 0
XV_OBJECT_OWNER_LANE_DEFAULT ?= 0
ifneq ($(filter $(XV_OBJECT_OWNER_LANE),0 1),$(XV_OBJECT_OWNER_LANE))
$(error XV_OBJECT_OWNER_LANE must be 0 or 1)
endif
ifneq ($(filter $(XV_OBJECT_OWNER_LANE_DEFAULT),0 1),$(XV_OBJECT_OWNER_LANE_DEFAULT))
$(error XV_OBJECT_OWNER_LANE_DEFAULT must be 0 or 1)
endif
ifeq ($(XV_OBJECT_OWNER_LANE),1)
ifneq ($(XV_EXPERIMENTAL_OBJECT_JOBS),1)
$(error XV_OBJECT_OWNER_LANE requires XV_EXPERIMENTAL_OBJECT_JOBS=1)
endif
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: RECOMP_CFLAGS += -DXV_OBJECT_OWNER_LANE=1 -DXV_OBJECT_OWNER_LANE_DEFAULT=$(XV_OBJECT_OWNER_LANE_DEFAULT)
endif
.PHONY: force-owner-lane-config
force-owner-lane-config:
$(RECOMP_BUILD)/owner-lane.config: force-owner-lane-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_OBJECT_OWNER_LANE):$(XV_OBJECT_OWNER_LANE_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: $(RECOMP_BUILD)/owner-lane.config
# Object worker lanes: 2 (cores 0/1) or 3 (third worker shares core 2 with the owner).
XV_OBJECT_WORKERS ?= 2
ifneq ($(filter $(XV_OBJECT_WORKERS),1 2 3),$(XV_OBJECT_WORKERS))
$(error XV_OBJECT_WORKERS must be 1, 2 or 3)
endif
$(RECOMP_BUILD)/kernel/xk_object_jobs.o $(RECOMP_BUILD)/kernel/xk_worker_query.o $(RECOMP_BUILD)/kernel/xk_math.o: RECOMP_CFLAGS += -DXV_OBJECT_WORKERS=$(XV_OBJECT_WORKERS)
.PHONY: force-object-workers-config
force-object-workers-config:
$(RECOMP_BUILD)/object-workers.config: force-object-workers-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_OBJECT_WORKERS)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_object_jobs.o $(RECOMP_BUILD)/kernel/xk_worker_query.o $(RECOMP_BUILD)/kernel/xk_math.o: $(RECOMP_BUILD)/object-workers.config
# Guest phase timing on from process start (diagnostic; disables object jobs).
XV_PHASE_TIMING_DEFAULT ?= 0
ifneq ($(filter $(XV_PHASE_TIMING_DEFAULT),0 1),$(XV_PHASE_TIMING_DEFAULT))
$(error XV_PHASE_TIMING_DEFAULT must be 0 or 1)
endif
$(RECOMP_BUILD)/xv_phase.o: RECOMP_CFLAGS += -DXV_PHASE_TIMING_DEFAULT=$(XV_PHASE_TIMING_DEFAULT)
.PHONY: force-phase-default-config
force-phase-default-config:
$(RECOMP_BUILD)/phase-default.config: force-phase-default-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_PHASE_TIMING_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/xv_phase.o: $(RECOMP_BUILD)/phase-default.config
XV_QUERY_REUSE_PROFILE ?= 0
ifneq ($(words $(XV_QUERY_REUSE_PROFILE)),1)
$(error XV_QUERY_REUSE_PROFILE must be 0 or 1)
endif
ifneq ($(filter $(XV_QUERY_REUSE_PROFILE),0 1),$(XV_QUERY_REUSE_PROFILE))
$(error XV_QUERY_REUSE_PROFILE must be 0 or 1)
endif
ifeq ($(XV_QUERY_REUSE_PROFILE),1)
ifneq ($(XV_QUERY_REUSE),1)
$(error XV_QUERY_REUSE_PROFILE requires XV_QUERY_REUSE=1)
endif
endif
$(RECOMP_BUILD)/kernel/xk_query_reuse.o: RECOMP_CFLAGS += -DXV_QUERY_REUSE_PROFILE=$(XV_QUERY_REUSE_PROFILE)
.PHONY: force-query-reuse-profile-config
force-query-reuse-profile-config:
$(RECOMP_BUILD)/query-reuse-profile.config: force-query-reuse-profile-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_QUERY_REUSE_PROFILE)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_query_reuse.o: $(RECOMP_BUILD)/query-reuse-profile.config
# Keep this exact canonical closure in sync with tools/query_f32_primitives.py.
# Never glob a generated directory: regeneration must not copy its own output.
QUERY_F32_HEADERS := xv_recomp_protos.h xv_x86rt.h xv_phase.h \
    kernel/xk_object_jobs.h kernel/xk_light_census.h kernel/xk_collision_vertices.h \
    kernel/xk_segment_sphere.h kernel/xk_collision_traversal.h
.PHONY: force-query-f32-config
force-query-f32-config:
$(RECOMP_BUILD)/query-f32.config: force-query-f32-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_QUERY_F32_INLINE)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
QUERY_FUSION_OBJECTS := $(RECOMP_BUILD)/code_028.o $(RECOMP_BUILD)/query_fusion.o
QUERY_FUSION_INPUTS := tools/query_world_run.py tools/query_object_space.py games/halo_ce_3925/discovery.py tools/query_ancestor_scalar.py tools/query_membership_scalar.py tools/query_semantic_leaf.py tools/query_f32_primitives.py tools/gen_native_query_fusion.py tools/gen_native_solver_fusion.py tools/prototype_collision_query.py \
    tools/tests/collision_query_fusion.c $(wildcard recompiler/*.py recompiler/core/*.py games/halo_ce_3925/*.py) \
    recomp/kernel/xk_collision_vertices.h recomp/kernel/xk_segment_sphere.h \
    recomp/kernel/xk_collision_traversal.h recomp/kernel/xk_geometry.c \
    $(RECOMP_DIR)/code_013.c $(RECOMP_DIR)/code_016.c $(RECOMP_DIR)/code_028.c $(XBE) $(XBE_JSON)
ifeq ($(XV_NATIVE_SOLVER_FUSION),1)
QUERY_FUSION_INPUTS += tools/prototype_collision_solver.py recomp/xv_x86rt.h $(RECOMP_DIR)/code_000.c
endif
QUERY_FUSION_OUTPUTS := $(RECOMP_DIR)/query_fusion.c
ifeq ($(XV_QUERY_REUSE),1)
QUERY_FUSION_INPUTS += tools/query_memory_capture.py
QUERY_FUSION_OUTPUTS += $(RECOMP_DIR)/query_capture.c $(RECOMP_DIR)/query_capture_world_run.h \
    $(RECOMP_DIR)/query_capture_semantic_leaf.h $(addprefix $(RECOMP_DIR)/query_capture_primitives/,$(QUERY_F32_HEADERS))
endif
ifeq ($(XV_QUERY_WORLD_RUN),1)
QUERY_FUSION_INPUTS += tools/query_world_run.h
QUERY_FUSION_OUTPUTS += $(RECOMP_DIR)/query_world_run.h
endif
ifeq ($(XV_QUERY_SEMANTIC_LEAF),1)
QUERY_FUSION_INPUTS += tools/query_semantic_leaf.h
QUERY_FUSION_OUTPUTS += $(RECOMP_DIR)/query_semantic_leaf.h
endif
ifeq ($(XV_QUERY_F32_INLINE),1)
QUERY_FUSION_INPUTS += $(addprefix $(RECOMP_DIR)/,$(QUERY_F32_HEADERS))
QUERY_FUSION_OUTPUTS += $(addprefix $(RECOMP_DIR)/query_f32_primitives/,$(QUERY_F32_HEADERS))
endif
ifeq ($(XV_NATIVE_SOLVER_FUSION),1)
QUERY_FUSION_OUTPUTS += $(RECOMP_DIR)/solver_fusion.c $(RECOMP_DIR)/solver_primitives.h
endif
.PHONY: force-query-fusion-config force-query-fusion-missing query-fusion-generate
force-query-fusion-config:
force-query-fusion-missing:
$(RECOMP_BUILD)/query-fusion.config: force-query-fusion-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_NATIVE_QUERY_FUSION)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(QUERY_FUSION_OBJECTS) $(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/libxita_guest.a $(RECOMP_BUILD)/librecomp.a: $(RECOMP_BUILD)/query-fusion.config
ifeq ($(XV_NATIVE_QUERY_FUSION),1)
ifneq ($(RECOMP),1)
$(error XV_NATIVE_QUERY_FUSION requires RECOMP=1)
endif
ifneq ($(GAME_PROFILE),halo_ce_3925)
$(error XV_NATIVE_QUERY_FUSION requires GAME_PROFILE=halo_ce_3925)
endif
ifneq ($(XV_NATIVE_BSP_SPHERE) $(XV_NATIVE_COLLISION_VERTICES) $(XV_NATIVE_SEGMENT_SPHERE) $(XV_NATIVE_COLLISION_TRAVERSAL),1 1 1 1)
$(error XV_NATIVE_QUERY_FUSION requires XV_NATIVE_BSP_SPHERE=1 XV_NATIVE_COLLISION_VERTICES=1 XV_NATIVE_SEGMENT_SPHERE=1 XV_NATIVE_COLLISION_TRAVERSAL=1)
endif
$(QUERY_FUSION_OBJECTS): RECOMP_CFLAGS += -DXV_NATIVE_QUERY_FUSION=1
$(QUERY_FUSION_OBJECTS): $(RECOMP_BUILD)/query-fusion.generated.json
# The stamp, published last, owns generation. A missing query source forces
# regeneration too. Keeping the source's stamp dependency order-only permits
# content-only updates without repeatedly regenerating unchanged output bytes.
# Both affected objects wait for the stamp before parallel compilation.
# One generator owns both caller edits. The solver config is also a stamp input
# on the OFF transition, restoring the exact query-only caller before compile.
$(RECOMP_BUILD)/query-fusion.generated.json: $(QUERY_FUSION_INPUTS) $(RECOMP_BUILD)/solver-fusion.config $(RECOMP_BUILD)/query-f32.config $(RECOMP_BUILD)/query-semantic.config $(RECOMP_BUILD)/query-membership.config $(RECOMP_BUILD)/query-ancestor.config $(RECOMP_BUILD)/query-object.config $(RECOMP_BUILD)/query-world-run.config $(RECOMP_BUILD)/query-repeat.config $(RECOMP_BUILD)/query-reuse.config $(if $(filter-out $(wildcard $(QUERY_FUSION_OUTPUTS)),$(QUERY_FUSION_OUTPUTS)),force-query-fusion-missing)
	$(PYTHON) tools/gen_native_query_fusion.py --xbe $(XBE) --manifest $(XBE_JSON) --recomp-dir $(RECOMP_DIR) --receipt $(RECOMP_BUILD)/query-fusion.generated.json $(if $(filter 1,$(XV_NATIVE_SOLVER_FUSION)),--solver-fusion 1,) $(if $(filter 1,$(XV_QUERY_F32_INLINE)),--query-f32-inline 1,) $(if $(filter 1,$(XV_QUERY_SEMANTIC_LEAF)),--query-semantic-leaf 1,) $(if $(filter 1,$(XV_QUERY_MEMBERSHIP_SCALAR)),--query-membership-scalar 1,) $(if $(filter 1,$(XV_QUERY_ANCESTOR_SCALAR)),--query-ancestor-scalar 1,) $(if $(filter 1,$(XV_QUERY_OBJECT_SPACE)),--query-object-space 1,) $(if $(filter 1,$(XV_QUERY_WORLD_RUN)),--query-world-run 1,) $(if $(filter 1,$(XV_QUERY_REPEAT_CENSUS)),--query-repeat-census 1,) $(if $(filter 1,$(XV_QUERY_REUSE)),--query-reuse 1,)
$(QUERY_FUSION_OUTPUTS): | $(RECOMP_BUILD)/query-fusion.generated.json
	@test -f $@
query-fusion-generate: $(RECOMP_BUILD)/query-fusion.generated.json $(RECOMP_DIR)/query_fusion.c
else
query-fusion-generate:
	@echo 'Set XV_NATIVE_QUERY_FUSION=1 and its compiled prerequisites to generate the owned fusion unit.' >&2
	@false
endif
ifeq ($(XV_NATIVE_SOLVER_FUSION),1)
solver-fusion-generate: $(RECOMP_BUILD)/query-fusion.generated.json $(RECOMP_DIR)/solver_fusion.c $(RECOMP_DIR)/solver_primitives.h
else
solver-fusion-generate:
	@echo 'Set XV_NATIVE_SOLVER_FUSION=1 with XV_NATIVE_QUERY_FUSION=1 to generate the owned solver unit.' >&2
	@false
endif
CONSTANT_PACK_SRCS := $(shell rg -l 'xv_constant_pack_prefix' $(RECOMP_DIR)/code_*.c 2>/dev/null)
CONSTANT_PACK_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(CONSTANT_PACK_SRCS))
ifeq ($(XV_NATIVE_CONSTANT_PACK),1)
ifeq ($(strip $(CONSTANT_PACK_SRCS)),)
$(error XV_NATIVE_CONSTANT_PACK requires regenerated 7E530 hook)
endif
$(CONSTANT_PACK_OBJS) $(RECOMP_BUILD)/kernel/xk_constant_pack.o $(RECOMP_BUILD)/kernel/xk_owner_phase.o: RECOMP_CFLAGS += -DXV_NATIVE_CONSTANT_PACK
$(RECOMP_BUILD)/kernel/xk_constant_pack.o: RECOMP_CFLAGS += -DXV_OWNER_PHASE
endif
.PHONY: force-constant-pack-config
force-constant-pack-config:
$(BUILD)/constant-pack.config: force-constant-pack-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_NATIVE_CONSTANT_PACK)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(CONSTANT_PACK_OBJS) $(RECOMP_BUILD)/kernel/xk_constant_pack.o $(RECOMP_BUILD)/kernel/xk_owner_phase.o: $(BUILD)/constant-pack.config recomp/kernel/xk_constant_pack.h
$(RECOMP_BUILD)/libxita_guest.a $(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/librecomp.a: $(BUILD)/constant-pack.config
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

# Passive accepted-pass timing and raw quiescent worker-clock snapshots.
# Explicit diagnostic startup build only; no runtime selector or changed jobs.
ifeq ($(XV_OBJECT_PASS_TIMING),1)
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: RECOMP_CFLAGS += -DXV_OBJECT_PASS_TIMING=1
endif
.PHONY: force-object-pass-timing-config
force-object-pass-timing-config:
$(RECOMP_BUILD)/object-pass-timing.config: force-object-pass-timing-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_OBJECT_PASS_TIMING)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: $(RECOMP_BUILD)/object-pass-timing.config

# Guard-retained C query adapter. Ordinary builds contain no adapter/state.
XV_WORKER_QUERY_DEFAULT ?= 0
ifneq ($(filter $(XV_WORKER_QUERY_DEFAULT),0 1),$(XV_WORKER_QUERY_DEFAULT))
$(error XV_WORKER_QUERY_DEFAULT must be 0 or 1)
endif
ifeq ($(XV_WORKER_QUERY_DEFAULT),1)
ifneq ($(XV_WORKER_QUERY),1)
$(error XV_WORKER_QUERY_DEFAULT=1 requires XV_WORKER_QUERY=1)
endif
endif
ifeq ($(XV_WORKER_QUERY),1)
ifneq ($(XV_EXPERIMENTAL_OBJECT_JOBS),1)
$(error XV_WORKER_QUERY requires XV_EXPERIMENTAL_OBJECT_JOBS=1)
endif
RECOMP_CFLAGS += -DXV_WORKER_QUERY -DXV_WORKER_QUERY_DEFAULT=$(XV_WORKER_QUERY_DEFAULT)
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
	@printf '%s\n' '$(if $(filter 1,$(XV_WORKER_QUERY)),1,0)/$(if $(filter 1,$(XV_TYPED_CLUSTER_QUERY)),1,0)/$(XV_WORKER_QUERY_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(XITA_GUEST_OBJS) $(XITA_GAME_OBJS) $(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/librecomp.a: $(RECOMP_BUILD)/worker-query.config
$(RECOMP_BUILD)/kernel/xk_worker_query.o: recomp/kernel/xk_worker_query_generated.inc
$(RECOMP_BUILD)/kernel/xk_worker_query.o: RECOMP_CFLAGS += -ffp-contract=off -frounding-math
$(addprefix $(RECOMP_BUILD)/kernel/,xk_cluster_runtime.o xk_cluster_snapshot.o xk_cluster_query.o xk_cluster_query_replay.o): RECOMP_CFLAGS += -ffp-contract=off -frounding-math
recomp/kernel/xk_worker_query_generated.inc: tools/gen_worker_query.py recompiler/xita_recomp.py $(XBE) $(XBE_JSON)
	$(PYTHON) tools/gen_worker_query.py --xbe $(XBE) --manifest $(XBE_JSON)

# Private numerical overlap only; default off even in typed-query builds.
XV_QUERY_OVERLAP_DEFAULT ?= 0
ifneq ($(filter $(XV_QUERY_OVERLAP_DEFAULT),0 1),$(XV_QUERY_OVERLAP_DEFAULT))
$(error XV_QUERY_OVERLAP_DEFAULT must be 0 or 1)
endif
ifeq ($(XV_QUERY_OVERLAP_DEFAULT),1)
ifneq ($(XV_TYPED_CLUSTER_QUERY),1)
$(error XV_QUERY_OVERLAP_DEFAULT=1 requires XV_TYPED_CLUSTER_QUERY=1)
endif
endif
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: RECOMP_CFLAGS += -DXV_QUERY_OVERLAP_DEFAULT=$(XV_QUERY_OVERLAP_DEFAULT)
.PHONY: force-query-overlap-config
force-query-overlap-config:
$(RECOMP_BUILD)/query-overlap.config: force-query-overlap-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_QUERY_OVERLAP_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: $(RECOMP_BUILD)/query-overlap.config

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
# Process-start default for the sampled holder profile (XV_OBJECT_HOLDS overrides).
XV_OBJECT_HOLDS_DEFAULT ?= 0
ifneq ($(words $(XV_OBJECT_HOLDS_DEFAULT)),1)
$(error XV_OBJECT_HOLDS_DEFAULT must be 0 or 1)
endif
ifneq ($(filter $(XV_OBJECT_HOLDS_DEFAULT),0 1),$(XV_OBJECT_HOLDS_DEFAULT))
$(error XV_OBJECT_HOLDS_DEFAULT must be 0 or 1)
endif
ifeq ($(XV_OBJECT_HOLD_BUILD),1)
RECOMP_CFLAGS += -DXV_OBJECT_HOLD_PROFILE
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: RECOMP_CFLAGS += -DXV_OBJECT_HOLDS_DEFAULT=$(XV_OBJECT_HOLDS_DEFAULT)
endif
.PHONY: force-object-hold-config
force-object-hold-config:
$(RECOMP_BUILD)/object-hold.config: force-object-hold-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_OBJECT_HOLD_BUILD):$(XV_OBJECT_HOLDS_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: $(RECOMP_BUILD)/object-hold.config
$(filter $(RECOMP_BUILD)/code_%.o,$(RECOMP_OBJS)): $(RECOMP_BUILD)/object-hold.config
$(RECOMP_BUILD)/query_fusion.o $(RECOMP_BUILD)/solver_fusion.o: $(RECOMP_BUILD)/object-hold.config

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
recomp/kernel/xk_polygon_edge.c: tools/gen_native_polygon_edge.py games/halo_ce_3925/hooks.py games/halo_ce_3925/discovery.py recompiler/xita_recomp.py $(XBE) $(XBE_JSON)
	$(PYTHON) tools/gen_native_polygon_edge.py --xbe $(XBE) --manifest $(XBE_JSON)

# Optional clip region: generated units, helper and bridge share a tracked mode.
ifeq ($(XV_POLYGON_EDGE_TRIAL),1)
$(RECOMP_BUILD)/kernel/xd3d.o: RECOMP_CFLAGS += -DXV_POLYGON_EDGE_TRIAL=1
endif
$(RECOMP_BUILD)/kernel/xd3d.o: $(BUILD)/polygon-edge-trial.config
ifeq ($(XV_CLIP_REGION_TRIAL),1)
$(RECOMP_BUILD)/kernel/xd3d.o: RECOMP_CFLAGS += -DXV_CLIP_REGION_TRIAL=1
endif
$(RECOMP_BUILD)/kernel/xd3d.o: $(BUILD)/clip-region-trial.config
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
# Only the fused clipping unit consumes this option. Rebuild on either change
# of value, retaining all other cumulative guest/native objects.
.PHONY: force-clip-distance-config
force-clip-distance-config:
$(RECOMP_BUILD)/clip-distance.config: force-clip-distance-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_CLIP_DISTANCE_SPANS)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_clip_region.o: $(RECOMP_BUILD)/clip-distance.config
$(RECOMP_BUILD)/kernel/xk_clip_region.o: RECOMP_CFLAGS += -DXV_CLIP_DISTANCE_SPANS=$(XV_CLIP_DISTANCE_SPANS)
recomp/kernel/xk_clip_region.c: tools/gen_native_clip_region.py tools/gen_native_clip.py tools/clip_distance_spans.py games/halo_ce_3925/clip_region.py games/halo_ce_3925/hooks.py games/halo_ce_3925/discovery.py recompiler/xita_recomp.py $(XBE) $(XBE_JSON)
	$(PYTHON) tools/gen_native_clip_region.py --xbe $(XBE) --manifest $(XBE_JSON)
recomp/kernel/xk_clip.c: tools/gen_native_clip.py games/halo_ce_3925/hooks.py games/halo_ce_3925/clip_region.py recompiler/xita_recomp.py $(XBE) $(XBE_JSON)
	$(PYTHON) tools/gen_native_clip.py

# Captured marker record: explicit Halo worker build, default off.
XV_NATIVE_MARKER_RECORD ?= 0
ifneq ($(filter $(XV_NATIVE_MARKER_RECORD),0 1),$(XV_NATIVE_MARKER_RECORD))
$(error XV_NATIVE_MARKER_RECORD must be 0 or 1)
endif
ifeq ($(XV_NATIVE_MARKER_RECORD),1)
ifneq ($(RECOMP):$(GAME_PROFILE):$(XV_EXPERIMENTAL_OBJECT_JOBS),1:halo_ce_3925:1)
$(error XV_NATIVE_MARKER_RECORD requires RECOMP=1 GAME_PROFILE=halo_ce_3925 XV_EXPERIMENTAL_OBJECT_JOBS=1)
endif
endif
MARKER_HOOK_SRCS := $(shell rg -l XV_NATIVE_MARKER_RECORD $(XITA_GUEST_SRCS) 2>/dev/null)
MARKER_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(MARKER_HOOK_SRCS)) $(RECOMP_BUILD)/kernel/xk_marker.o $(RECOMP_BUILD)/kernel/xk_object_jobs.o
$(MARKER_OBJS): RECOMP_CFLAGS += -DXV_NATIVE_MARKER_RECORD=$(XV_NATIVE_MARKER_RECORD)
.PHONY: force-marker-config
force-marker-config:
$(RECOMP_BUILD)/marker.config: force-marker-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_NATIVE_MARKER_RECORD)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(MARKER_OBJS): $(RECOMP_BUILD)/marker.config
$(RECOMP_BUILD)/kernel/xk_marker.o: recomp/kernel/xk_marker_snapshot.h recomp/kernel/xk_quaternion_snapshot.h recomp/kernel/xk_matrix_snapshot.h
$(RECOMP_BUILD)/kernel/xk_marker.o: RECOMP_CFLAGS += -O3 -ffp-contract=off

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

# Only the retained original final node may contain smaller normal terms.
XV_HIERARCHY_FINAL_NORMAL ?= 0
ifneq ($(words $(XV_HIERARCHY_FINAL_NORMAL)),1)
$(error XV_HIERARCHY_FINAL_NORMAL must be 0 or 1)
endif
ifneq ($(filter $(XV_HIERARCHY_FINAL_NORMAL),0 1),$(XV_HIERARCHY_FINAL_NORMAL))
$(error XV_HIERARCHY_FINAL_NORMAL must be 0 or 1)
endif
ifeq ($(XV_HIERARCHY_FINAL_NORMAL),1)
ifneq ($(XV_NATIVE_MODEL_HIERARCHY),1)
$(error XV_HIERARCHY_FINAL_NORMAL requires XV_NATIVE_MODEL_HIERARCHY=1)
endif
$(RECOMP_BUILD)/kernel/xk_hierarchy.o: RECOMP_CFLAGS += -DXV_HIERARCHY_FINAL_NORMAL=1
endif
.PHONY: force-hierarchy-final-config
force-hierarchy-final-config:
$(RECOMP_BUILD)/hierarchy-final.config: force-hierarchy-final-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_HIERARCHY_FINAL_NORMAL)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_hierarchy.o: $(RECOMP_BUILD)/hierarchy-final.config

# Matrix-only admission of finite normal terms; actual batched poses keep the
# preceding domain. Scope the option and its rebuild dependency to this unit.
XV_HIERARCHY_MATRIX_NORMAL ?= 0
ifneq ($(words $(XV_HIERARCHY_MATRIX_NORMAL)),1)
$(error XV_HIERARCHY_MATRIX_NORMAL must be 0 or 1)
endif
ifneq ($(filter $(XV_HIERARCHY_MATRIX_NORMAL),0 1),$(XV_HIERARCHY_MATRIX_NORMAL))
$(error XV_HIERARCHY_MATRIX_NORMAL must be 0 or 1)
endif
ifeq ($(XV_HIERARCHY_MATRIX_NORMAL),1)
ifneq ($(XV_NATIVE_MODEL_HIERARCHY),1)
$(error XV_HIERARCHY_MATRIX_NORMAL requires XV_NATIVE_MODEL_HIERARCHY=1)
endif
$(RECOMP_BUILD)/kernel/xk_hierarchy.o: RECOMP_CFLAGS += -DXV_HIERARCHY_MATRIX_NORMAL=1
endif
.PHONY: force-hierarchy-matrix-config
force-hierarchy-matrix-config:
$(RECOMP_BUILD)/hierarchy-matrix.config: force-hierarchy-matrix-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_HIERARCHY_MATRIX_NORMAL)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_hierarchy.o: $(RECOMP_BUILD)/hierarchy-matrix.config

# Unroll only bounded native math units. Scalar VFP operations retain their
# established operand order; no global fast-math or guest codegen change.
$(RECOMP_BUILD)/kernel/xk_math.o: recomp/kernel/xk_quaternion_snapshot.h recomp/kernel/xk_matrix_snapshot.h
$(RECOMP_BUILD)/kernel/xk_math.o: RECOMP_CFLAGS += -O3 -funroll-loops -ffp-contract=off
ifeq ($(XV_NATIVE_MATRIX_NEON),1)
$(RECOMP_BUILD)/kernel/xk_math.o: RECOMP_CFLAGS += -DXV_NATIVE_MATRIX_NEON
endif
# The optional batch validates layouts, scheduling and numeric inputs before
# using bounded products. Keep reassociation/FMA disabled while unrolling it.
$(RECOMP_BUILD)/kernel/xk_constant_pack.o: RECOMP_CFLAGS += -O3 -funroll-loops -ffp-contract=off
$(RECOMP_BUILD)/kernel/xk_palette.o: RECOMP_CFLAGS += -O3 -funroll-loops -ffp-contract=off
$(RECOMP_BUILD)/kernel/xk_hierarchy.o: RECOMP_CFLAGS += -O3 -ffp-contract=off
ifeq ($(XV_PALETTE_PREFIX_REUSE),1)
$(RECOMP_BUILD)/kernel/xk_palette.o: RECOMP_CFLAGS += -DXV_PALETTE_PREFIX_REUSE=1
endif
$(RECOMP_BUILD)/kernel/xk_palette.o: $(BUILD)/palette-prefix.config
ifeq ($(XV_PALETTE_JOB_PROFILE),1)
$(RECOMP_BUILD)/kernel/xk_palette.o: RECOMP_CFLAGS += -DXV_PALETTE_JOB_PROFILE=1
endif
$(RECOMP_BUILD)/kernel/xk_object_basis.o: RECOMP_CFLAGS += -ffp-contract=off
$(RECOMP_BUILD)/kernel/xk_clip.o: RECOMP_CFLAGS += -ffp-contract=off
$(RECOMP_BUILD)/kernel/xk_bounds.o: RECOMP_CFLAGS += -ffp-contract=off
ifeq ($(XV_HLE_DISPATCH_CACHE),1)
$(RECOMP_BUILD)/xv_x86rt.o: RECOMP_CFLAGS += -DXV_HLE_DISPATCH_CACHE
endif
recomp/kernel/xk_bounds.c: tools/gen_native_bounds.py games/halo_ce_3925/hooks.py games/halo_ce_3925/discovery.py recompiler/xita_recomp.py $(XBE) $(XBE_JSON)
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

# Experimental complete previous-frame model palettes, never simulation state.
ifneq ($(words $(XV_POSE_PIPELINE)),1)
$(error XV_POSE_PIPELINE must be 0 or 1)
endif
ifneq ($(filter $(XV_POSE_PIPELINE),0 1),$(XV_POSE_PIPELINE))
$(error XV_POSE_PIPELINE must be 0 or 1)
endif
ifeq ($(XV_POSE_PIPELINE),1)
ifneq ($(RECOMP):$(XV_NATIVE_MODEL_PALETTE):$(XV_OWNER_PHASE):$(XV_EXPERIMENTAL_OBJECT_JOBS):$(XV_SCENE_BUCKET0_DETAIL):$(GAME_PROFILE),1:1:1:1:1:halo_ce_3925)
$(error XV_POSE_PIPELINE requires Halo CE native palette, owner phase, object jobs and model detail hooks)
endif
RECOMP_CFLAGS += -DXV_POSE_PIPELINE=1
$(RECOMP_BUILD)/kernel/xk_palette.o: RECOMP_CFLAGS += -DXV_OWNER_PHASE
$(BUILD)/runtime/xv_d3d.o: CFLAGS += -DXV_POSE_PIPELINE=1
endif
.PHONY: force-pose-pipeline-config
force-pose-pipeline-config:
$(BUILD)/pose-pipeline.config: force-pose-pipeline-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_POSE_PIPELINE)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_palette.o $(BUILD)/runtime/xv_d3d.o: $(BUILD)/pose-pipeline.config

POSE_PIPELINE_HOOK_SRCS := $(shell rg -l 'XV_POSE_SCOPE:|XV_POSE_RETIRE:' $(RECOMP_DIR)/code_*.c 2>/dev/null)
POSE_PIPELINE_HOOK_OBJS := $(patsubst $(RECOMP_DIR)/%.c,$(RECOMP_BUILD)/%.o,$(POSE_PIPELINE_HOOK_SRCS))
ifeq ($(XV_POSE_PIPELINE),1)
ifneq ($(words $(shell rg -o 'XV_POSE_SCOPE:|XV_POSE_RETIRE:' $(POSE_PIPELINE_HOOK_SRCS) 2>/dev/null)),3)
$(error XV_POSE_PIPELINE requires regenerated model owner and both retirement hooks)
endif
endif
$(POSE_PIPELINE_HOOK_OBJS): $(BUILD)/pose-pipeline.config
$(RECOMP_BUILD)/libxita_game.a $(RECOMP_BUILD)/libxita_guest.a: $(BUILD)/pose-pipeline.config

# Experimental private-stack inner clipping; shared/nested cases keep the guard.
XV_CLIP_PRIVATE ?= 0
ifneq ($(words $(XV_CLIP_PRIVATE)),1)
$(error XV_CLIP_PRIVATE must be 0 or 1)
endif
ifneq ($(filter $(XV_CLIP_PRIVATE),0 1),$(XV_CLIP_PRIVATE))
$(error XV_CLIP_PRIVATE must be 0 or 1)
endif
ifeq ($(XV_CLIP_PRIVATE),1)
ifneq ($(RECOMP):$(GAME_PROFILE):$(XV_NATIVE_CLIP_REGION):$(XV_EXPERIMENTAL_OBJECT_JOBS),1:halo_ce_3925:1:1)
$(error XV_CLIP_PRIVATE requires Halo CE, native clip region and object workers)
endif
endif
.PHONY: force-clip-private-config
force-clip-private-config:
$(BUILD)/clip-private.config: force-clip-private-config
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(XV_CLIP_PRIVATE)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_clip_region.o $(RECOMP_BUILD)/kernel/xk_object_jobs.o: RECOMP_CFLAGS += -DXV_CLIP_PRIVATE=$(XV_CLIP_PRIVATE)
$(RECOMP_BUILD)/kernel/xk_clip_region.o $(RECOMP_BUILD)/kernel/xk_object_jobs.o: $(BUILD)/clip-private.config

# Explicit cumulative-build selection; ordinary private-quaternion default is off.
XV_OBJECT_QUAT_DEFAULT ?= 0
ifneq ($(words $(XV_OBJECT_QUAT_DEFAULT)),1)
$(error XV_OBJECT_QUAT_DEFAULT must be 0 or 1)
endif
ifneq ($(filter $(XV_OBJECT_QUAT_DEFAULT),0 1),$(XV_OBJECT_QUAT_DEFAULT))
$(error XV_OBJECT_QUAT_DEFAULT must be 0 or 1)
endif
ifeq ($(XV_OBJECT_QUAT_DEFAULT),1)
ifneq ($(RECOMP):$(GAME_PROFILE):$(XV_OBJECT_QUAT_BUILD),1:halo_ce_3925:1)
$(error XV_OBJECT_QUAT_DEFAULT requires Halo CE object quaternion workers)
endif
ifeq ($(XV_QUAT_CACHE),1)
$(error XV_OBJECT_QUAT_DEFAULT cannot bypass the shared quaternion cache)
endif
endif
.PHONY: force-object-quat-default-config
force-object-quat-default-config:
$(RECOMP_BUILD)/object-quat-default.config: force-object-quat-default-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_OBJECT_QUAT_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: RECOMP_CFLAGS += -DXV_OBJECT_QUAT_DEFAULT=$(XV_OBJECT_QUAT_DEFAULT)
$(RECOMP_BUILD)/kernel/xk_object_jobs.o: $(RECOMP_BUILD)/object-quat-default.config

# Hierarchy captured computation; guest publication retains the original guard.
XV_HIERARCHY_SNAPSHOT ?= 0
ifneq ($(filter $(XV_HIERARCHY_SNAPSHOT),0 1),$(XV_HIERARCHY_SNAPSHOT))
$(error XV_HIERARCHY_SNAPSHOT must be 0 or 1)
endif
ifeq ($(XV_HIERARCHY_SNAPSHOT),1)
ifneq ($(RECOMP):$(GAME_PROFILE):$(XV_EXPERIMENTAL_OBJECT_JOBS):$(XV_NATIVE_MODEL_HIERARCHY),1:halo_ce_3925:1:1)
$(error XV_HIERARCHY_SNAPSHOT requires Halo CE object workers and native hierarchy)
endif
endif
HIERARCHY_SNAPSHOT_OBJS := $(RECOMP_BUILD)/kernel/xk_hierarchy.o $(RECOMP_BUILD)/kernel/xk_object_jobs.o
$(HIERARCHY_SNAPSHOT_OBJS): RECOMP_CFLAGS += -DXV_HIERARCHY_SNAPSHOT=$(XV_HIERARCHY_SNAPSHOT)
.PHONY: force-hierarchy-snapshot-config
force-hierarchy-snapshot-config:
$(RECOMP_BUILD)/hierarchy-snapshot.config: force-hierarchy-snapshot-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_HIERARCHY_SNAPSHOT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(HIERARCHY_SNAPSHOT_OBJS): $(RECOMP_BUILD)/hierarchy-snapshot.config recomp/kernel/xk_hierarchy_runtime.h

# Waiting object workers may help pure hierarchy work; publisher keeps guard.
XV_HIERARCHY_ASSIST ?= 0
ifneq ($(filter $(XV_HIERARCHY_ASSIST),0 1),$(XV_HIERARCHY_ASSIST))
$(error XV_HIERARCHY_ASSIST must be 0 or 1)
endif
ifeq ($(XV_HIERARCHY_ASSIST),1)
ifneq ($(RECOMP):$(GAME_PROFILE):$(XV_EXPERIMENTAL_OBJECT_JOBS):$(XV_NATIVE_MODEL_HIERARCHY),1:halo_ce_3925:1:1)
$(error XV_HIERARCHY_ASSIST requires Halo CE object workers and native hierarchy)
endif
endif
$(HIERARCHY_SNAPSHOT_OBJS): RECOMP_CFLAGS += -DXV_HIERARCHY_ASSIST=$(XV_HIERARCHY_ASSIST)
.PHONY: force-hierarchy-assist-config
force-hierarchy-assist-config:
$(RECOMP_BUILD)/hierarchy-assist.config: force-hierarchy-assist-config
	@mkdir -p $(RECOMP_BUILD)
	@printf '%s\n' '$(XV_HIERARCHY_ASSIST)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(HIERARCHY_SNAPSHOT_OBJS): $(RECOMP_BUILD)/hierarchy-assist.config recomp/kernel/xk_captured_task.h
