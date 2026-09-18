# Independent Halo 2 startup target; never uses recomp/ generated CE output.
ROOT := $(abspath ../..)
VITASDK ?= $(HOME)/vitasdk
CC := $(VITASDK)/bin/arm-vita-eabi-gcc
HOST_CC ?= cc
GUEST_OPT ?= -O1
HOST_CHANNEL ?= 0
QUAD_RENDER ?= 0
SCREEN_RENDER ?= 0
BC1_RENDER ?= 0
COMPOSITION_RENDER ?= 0
THRESHOLD_RENDER ?= 0
BLUR_RENDER ?= 0
BLEND_RENDER ?= 0
LUMA_RENDER ?= 0
SPRITE_RENDER ?= 0
MENU_RENDER ?= 0
MENU_GXM_DEFAULT ?= 0
ifneq ($(filter $(MENU_GXM_DEFAULT),0 1),$(MENU_GXM_DEFAULT))
$(error MENU_GXM_DEFAULT must be 0 or 1)
endif
AUDIO_HOST ?= 0
AUDIO_DSP ?= 0
AUDIO_SPATIAL_MODEL ?= 0
AUDIO_FILTER_MODEL ?= 0
FX_PROBE_SECOND ?= 0
AUDIO_EFFECTS_UNAVAILABLE ?= 0
AUDIO_MULTIBIN_UNAVAILABLE ?= 0
GENERATED ?= $(ROOT)/local/halo2_5849/boot/generated
BUILD ?= $(ROOT)/local/halo2_5849/boot/build
IMAGE ?= $(ROOT)/local/halo2_5849/boot/halo2_image.bin
QUAD_SHADERS ?= $(ROOT)/local/halo2_5849/quad-shaders
MENU_SHADERS ?= $(ROOT)/local/halo2_5849/menu-shaders
MENU_FILES = $(wildcard $(MENU_SHADERS)/h2menu*.gxp)
MENU_PACK = $(foreach f,$(MENU_FILES),-a $(f)=$(notdir $(f)))
SCREEN_SHADERS ?= $(ROOT)/local/halo2_5849/screen-shaders
CFLAGS := -O2 -mthumb -mcpu=cortex-a9 -mfpu=neon -std=gnu11 -fno-strict-aliasing -ffunction-sections -fdata-sections -DXV_CHECK_GUEST_ADDRESS=1 -MMD -MP -I$(ROOT) -I$(ROOT)/runtime -I$(ROOT)/recomp -I$(ROOT)/recomp/kernel
CFLAGS += -DXV_EFLAGS_ID=1
$(BUILD)/recomp/kernel/xk_crypto.o: CFLAGS += -DXV_NONZERO_HD_KEY=1
SYS := recomp/xv_x86rt.c recomp/kernel/xk_mem.c recomp/kernel/xk_rtl.c recomp/kernel/xk_file.c recomp/kernel/xk_thread.c recomp/kernel/xk_xapi.c recomp/kernel/xk_crypto.c recomp/kernel/xk_os_vita.c runtime/xv_cpu.c
OBJECTS := $(addprefix $(BUILD)/,$(SYS:.c=.o)) $(BUILD)/boot.o $(BUILD)/kernel_stack.o $(BUILD)/command_snapshot.o $(BUILD)/cache_volume.o $(BUILD)/instance_memory.o $(BUILD)/gpu_bus.o $(BUILD)/nv2a_regs.o $(BUILD)/strict-kernel.o $(BUILD)/xv_fn_table.o $(patsubst $(GENERATED)/%.c,$(BUILD)/%.o,$(wildcard $(GENERATED)/code_*.c))
ifeq ($(HOST_CHANNEL),1)
OBJECTS += $(addprefix $(BUILD)/,host_channel_runtime.o command_state.o host_channel.o host_tiles.o push_stream.o gpu_objects.o kelvin_clear.o scanout.o input.o audio_unavailable.o)
OBJECTS += $(BUILD)/kernel_timer.o $(BUILD)/fp_environment.o $(BUILD)/linear_texture.o
endif
ifeq ($(AUDIO_HOST),1)
ifneq ($(HOST_CHANNEL),1)
$(error AUDIO_HOST=1 requires HOST_CHANNEL=1)
endif
OBJECTS += $(BUILD)/audio_host.o $(BUILD)/audio_vita.o $(BUILD)/audio_bins.o $(BUILD)/audio_progress.o $(BUILD)/audio_mixer_bridge.o
$(BUILD)/boot.o: CFLAGS += -DH2_AUDIO_HOST=1
$(BUILD)/audio_mixer_bridge.o: CFLAGS += -DXK_AUDIO_OUTPUT_FILTER=h2_audio_bins_filter -Dxk_os_audio_open=h2_audio_sink_open -Dxk_os_audio_mutex_lock=h2_audio_sink_lock -Dxk_os_audio_mutex_unlock=h2_audio_sink_unlock
endif
ifeq ($(AUDIO_DSP),1)
ifneq ($(AUDIO_HOST),1)
$(error AUDIO_DSP=1 requires AUDIO_HOST=1)
endif
ifeq ($(AUDIO_EFFECTS_UNAVAILABLE),1)
$(error AUDIO_DSP=1 conflicts with AUDIO_EFFECTS_UNAVAILABLE=1)
endif
OBJECTS += $(BUILD)/dsp_engine.o $(BUILD)/dsp_asset.o $(BUILD)/dsp_core.o $(BUILD)/audio_fx.o
$(BUILD)/audio_host.o $(BUILD)/audio_vita.o: CFLAGS += -DH2_AUDIO_DSP=1
$(BUILD)/audio_fx.o: CFLAGS += -frounding-math -ffp-contract=off
$(BUILD)/dsp_engine.o $(BUILD)/dsp_core.o: CFLAGS += -O2 -I$(CURDIR)/dsp/compat -I$(CURDIR)/dsp
DSP_FILES = $(DSP_ASSET)
DSP_PACK = -a $(DSP_ASSET)=halo2-dsp.bin
endif
ifeq ($(AUDIO_SPATIAL_MODEL),1)
ifneq ($(AUDIO_DSP),1)
$(error AUDIO_SPATIAL_MODEL=1 requires AUDIO_DSP=1)
endif
$(BUILD)/audio_host.o: CFLAGS += -DH2_AUDIO_SPATIAL_MODEL=1
endif
ifeq ($(AUDIO_FILTER_MODEL),1)
ifneq ($(AUDIO_DSP),1)
$(error AUDIO_FILTER_MODEL=1 requires AUDIO_DSP=1)
endif
$(BUILD)/audio_host.o: CFLAGS += -DH2_AUDIO_FILTER_MODEL=1
endif
ifeq ($(AUDIO_EFFECTS_UNAVAILABLE),1)
ifneq ($(AUDIO_HOST),1)
$(error AUDIO_EFFECTS_UNAVAILABLE=1 requires AUDIO_HOST=1)
endif
$(BUILD)/audio_host.o: CFLAGS += -DH2_AUDIO_EFFECTS_UNAVAILABLE=1
endif
ifeq ($(AUDIO_MULTIBIN_UNAVAILABLE),1)
ifneq ($(AUDIO_HOST),1)
$(error AUDIO_MULTIBIN_UNAVAILABLE=1 requires AUDIO_HOST=1)
endif
$(BUILD)/audio_host.o: CFLAGS += -DH2_AUDIO_MULTIBIN_UNAVAILABLE=1
endif
ifeq ($(BC1_RENDER),1)
ifneq ($(SCREEN_RENDER),1)
$(error BC1_RENDER=1 requires SCREEN_RENDER=1)
endif
OBJECTS += $(BUILD)/bc1_draw.o
$(BUILD)/host_channel_runtime.o $(BUILD)/quad_gxm.o: CFLAGS += -DH2_BC1_RENDER=1
BC1_FILES = $(addprefix $(BC1_SHADERS)/,bc1.vert.gxp bc1.frag.gxp bc1.contract.bin)
BC1_PACK = $(foreach f,$(BC1_FILES),-a $(f)=$(notdir $(f)))
endif
ifeq ($(COMPOSITION_RENDER),1)
ifneq ($(BC1_RENDER),1)
$(error COMPOSITION_RENDER=1 requires BC1_RENDER=1)
endif
OBJECTS += $(BUILD)/composition_draw.o
$(BUILD)/host_channel_runtime.o $(BUILD)/quad_gxm.o: CFLAGS += -DH2_COMPOSITION_RENDER=1
COMPOSITION_FILES = $(addprefix $(COMPOSITION_SHADERS)/,composition.vert.gxp composition.frag.gxp composition.copy.frag.gxp composition.contract.bin)
COMPOSITION_PACK = $(foreach f,$(COMPOSITION_FILES),-a $(f)=$(notdir $(f)))
endif
ifeq ($(THRESHOLD_RENDER),1)
ifneq ($(COMPOSITION_RENDER),1)
$(error THRESHOLD_RENDER=1 requires COMPOSITION_RENDER=1)
endif
OBJECTS += $(BUILD)/threshold_draw.o
$(BUILD)/host_channel_runtime.o $(BUILD)/quad_gxm.o: CFLAGS += -DH2_THRESHOLD_RENDER=1
THRESHOLD_FILES = $(addprefix $(THRESHOLD_SHADERS)/,threshold.vert.gxp threshold.frag.gxp threshold.contract.bin)
THRESHOLD_PACK = $(foreach f,$(THRESHOLD_FILES),-a $(f)=$(notdir $(f)))
endif
ifeq ($(BLUR_RENDER),1)
ifneq ($(THRESHOLD_RENDER),1)
$(error BLUR_RENDER=1 requires THRESHOLD_RENDER=1)
endif
OBJECTS += $(BUILD)/blur_draw.o
$(BUILD)/host_channel_runtime.o $(BUILD)/quad_gxm.o: CFLAGS += -DH2_BLUR_RENDER=1
BLUR_FILES = $(addprefix $(BLUR_SHADERS)/,blur.vert.gxp blur.frag.gxp blur.contract.bin)
BLUR_PACK = $(foreach f,$(BLUR_FILES),-a $(f)=$(notdir $(f)))
endif
ifeq ($(BLEND_RENDER),1)
ifneq ($(BLUR_RENDER),1)
$(error BLEND_RENDER=1 requires BLUR_RENDER=1)
endif
OBJECTS += $(BUILD)/blend_draw.o
$(BUILD)/host_channel_runtime.o $(BUILD)/quad_gxm.o: CFLAGS += -DH2_BLEND_RENDER=1
BLEND_FILES = $(addprefix $(BLEND_SHADERS)/,blend.vert.gxp blend.frag.gxp blend.copy.frag.gxp blend.contract.bin)
BLEND_PACK = $(foreach f,$(BLEND_FILES),-a $(f)=$(notdir $(f)))
endif
ifeq ($(LUMA_RENDER),1)
ifneq ($(BLEND_RENDER),1)
$(error LUMA_RENDER=1 requires BLEND_RENDER=1)
endif
OBJECTS += $(BUILD)/luma_draw.o
$(BUILD)/host_channel_runtime.o $(BUILD)/quad_gxm.o: CFLAGS += -DH2_LUMA_RENDER=1
LUMA_FILES = $(addprefix $(LUMA_SHADERS)/,luma.vert.gxp luma.frag.gxp luma.contract.bin)
LUMA_PACK = $(foreach f,$(LUMA_FILES),-a $(f)=$(notdir $(f)))
endif
ifeq ($(SPRITE_RENDER),1)
ifneq ($(LUMA_RENDER),1)
$(error SPRITE_RENDER=1 requires LUMA_RENDER=1)
endif
OBJECTS += $(BUILD)/sprite_draw.o
$(BUILD)/host_channel_runtime.o $(BUILD)/quad_gxm.o: CFLAGS += -DH2_SPRITE_RENDER=1
SPRITE_FILES = $(addprefix $(SPRITE_SHADERS)/,sprite.vert.gxp sprite.frag.gxp sprite.copy.frag.gxp sprite.contract.bin)
SPRITE_PACK = $(foreach f,$(SPRITE_FILES),-a $(f)=$(notdir $(f)))
endif
ifeq ($(MENU_RENDER),1)
ifneq ($(SPRITE_RENDER),1)
$(error MENU_RENDER=1 requires SPRITE_RENDER=1)
endif
OBJECTS += $(BUILD)/menu_draw.o $(BUILD)/nv2a_vsh.o $(BUILD)/menu_raster.o $(BUILD)/menu_raster_pool.o $(BUILD)/menu_texture.o $(BUILD)/menu_combiner.o $(BUILD)/menu_render.o $(BUILD)/menu_gxm.o
$(BUILD)/host_channel_runtime.o: CFLAGS += -DH2_MENU_RENDER=1
ifeq ($(MENU_NOCOMBINER),1)
$(BUILD)/menu_render.o: CFLAGS += -DH2_MENU_NOCOMBINER=1
endif
endif
comma := ,
WRAPS := xk_MmCreateKernelStack xk_MmDeleteKernelStack xk_MmClaimGpuInstanceMemory xv_unimpl xk_NtOpenFile xk_NtReadFile xk_NtWriteFile xk_NtDeviceIoControlFile xk_NtFsControlFile xk_NtQueryVolumeInformationFile
ifeq ($(HOST_CHANNEL),1)
WRAPS += xk_AvSendTVEncoderOption xk_AvSetDisplayMode xk_KeWaitForSingleObject
WRAPS += xk_KeInitializeTimerEx xk_KeInitializeDpc xk_KeSetTimer xk_KeSetTimerEx xk_KeCancelTimer xk_KeInsertQueueDpc xk_KeRemoveQueueDpc
WRAPS += xk_KfRaiseIrql xk_KfLowerIrql xk_KeRaiseIrqlToDpcLevel
endif
LIBS := -lSceLibKernel_stub -lSceKernelThreadMgr_stub -lSceSysmem_stub -lSceProcessmgr_stub -lSceIofilemgr_stub -lSceRtc_stub -lSceCtrl_stub -lSceTouch_stub -lSceAudio_stub -lSceDisplay_stub -lm
ifeq ($(QUAD_RENDER),1)
ifneq ($(HOST_CHANNEL),1)
$(error QUAD_RENDER=1 requires HOST_CHANNEL=1)
endif
OBJECTS += $(BUILD)/quad_draw.o $(BUILD)/quad_gxm.o
$(BUILD)/host_channel_runtime.o: CFLAGS += -DH2_QUAD_RENDER=1
LIBS += -lSceGxm_stub
QUAD_FILES := $(QUAD_SHADERS)/quad.vert.gxp $(QUAD_SHADERS)/quad.frag.gxp $(QUAD_SHADERS)/quad.contract.bin
QUAD_PACK := -a $(QUAD_SHADERS)/quad.vert.gxp=quad.vert.gxp -a $(QUAD_SHADERS)/quad.frag.gxp=quad.frag.gxp -a $(QUAD_SHADERS)/quad.contract.bin=quad.contract.bin
endif
ifeq ($(SCREEN_RENDER),1)
ifneq ($(QUAD_RENDER),1)
$(error SCREEN_RENDER=1 requires QUAD_RENDER=1)
endif
OBJECTS += $(BUILD)/screen_draw.o $(BUILD)/dxt23_layout.o
$(BUILD)/host_channel_runtime.o $(BUILD)/quad_gxm.o: CFLAGS += -DH2_SCREEN_RENDER=1
QUAD_FILES += $(addprefix $(SCREEN_SHADERS)/,screen.vert.gxp screen.frag.gxp screen.copy.frag.gxp screen.contract.bin)
QUAD_PACK += $(foreach f,screen.vert.gxp screen.frag.gxp screen.copy.frag.gxp screen.contract.bin,-a $(SCREEN_SHADERS)/$(f)=$(f))
# Offline-compiled menu shader pairs (h2menu_vs_<hash>.gxp / h2menu_ps_<hash>.frag.gxp), packed
# into the VPK root; the GXM backend loads them by the hashes the software path logs.
MENU_SHADERS ?= $(ROOT)/local/halo2_5849/menu-shaders
MENU_GXP := $(wildcard $(MENU_SHADERS)/h2menu_*.gxp)
QUAD_PACK += $(foreach f,$(MENU_GXP),-a $(f)=$(notdir $(f)))
QUAD_FILES += $(MENU_GXP)
endif
.DEFAULT_GOAL := all
DSP_ASSET ?= $(ROOT)/local/halo2_5849/dsp/halo2-dsp.bin
DSP_INCLUDES := -I$(CURDIR)/dsp/compat -I$(CURDIR)/dsp
DSP_HEADERS := $(wildcard dsp/compat/qemu/*.h dsp/compat/*.h dsp/*.h)
.PHONY: dsp-probe
dsp-probe: $(BUILD)/dsp-probe.vpk
$(BUILD)/dsp-probe.elf: dsp_probe.c dsp_asset.c dsp_asset.h dsp_engine.c dsp_engine.h dsp/interp/dsp_cpu.c dsp/interp/dsp_cpu.h dsp/interp/dsp_cpu_regs.h dsp/interp/dsp_emu.c.inc dsp/interp/dsp_dis.c.inc $(DSP_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -mthumb -mcpu=cortex-a9 -mfpu=neon -fno-strict-aliasing -ffunction-sections -fdata-sections $(DSP_INCLUDES) $(filter %.c,$^) -Wl,-q,--gc-sections -o $@ -lSceLibKernel_stub -lSceProcessmgr_stub -lSceIofilemgr_stub -lm
$(BUILD)/dsp-probe.velf: $(BUILD)/dsp-probe.elf
	$(VITASDK)/bin/vita-elf-create $< $@
$(BUILD)/dsp-probe-eboot.bin: $(BUILD)/dsp-probe.velf
	$(VITASDK)/bin/vita-make-fself -s $< $@
$(BUILD)/dsp-probe-param.sfo: Makefile
	@mkdir -p $(dir $@)
	$(VITASDK)/bin/vita-mksfoex -s TITLE_ID=XH2D00001 "H2 Private DSP Probe" $@
$(BUILD)/dsp-probe.vpk: $(BUILD)/dsp-probe-eboot.bin $(BUILD)/dsp-probe-param.sfo $(DSP_ASSET)
	$(VITASDK)/bin/vita-pack-vpk -s $(BUILD)/dsp-probe-param.sfo -b $(BUILD)/dsp-probe-eboot.bin -a $(DSP_ASSET)=halo2-dsp.bin $@
$(BUILD)/dsp_engine_test: dsp_engine_test.c dsp_engine.c dsp_engine.h dsp/interp/dsp_cpu.c dsp/interp/dsp_cpu.h dsp/interp/dsp_cpu_regs.h dsp/interp/dsp_emu.c.inc dsp/interp/dsp_dis.c.inc $(DSP_HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -fno-strict-aliasing $(DSP_INCLUDES) $< -lm -o $@
test-host: $(BUILD)/dsp_engine_test
$(BUILD)/audio_fx_test: audio_fx_test.c audio_fx.c audio_fx.h audio_fx_limits.h audio_hrtf_model.h audio_filter_model.h dsp_engine_test.c dsp_engine.c dsp_engine.h dsp/interp/dsp_cpu.c dsp/interp/dsp_cpu.h dsp/interp/dsp_cpu_regs.h dsp/interp/dsp_emu.c.inc dsp/interp/dsp_dis.c.inc $(DSP_HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -fno-strict-aliasing $(DSP_INCLUDES) $< -lm -o $@
test-host: $(BUILD)/audio_fx_test
$(BUILD)/audio_fx_vita_test: audio_fx_vita_test.c audio_fx_fixture.c audio_fx_test.c audio_fx.c audio_fx.h audio_fx_limits.h audio_hrtf_model.h audio_filter_model.h audio_vita_test.c audio_vita.c audio_bins.c audio_bins.h audio_progress.c audio_progress.h audio_host.h audio_fx_limits.h dsp_engine_test.c dsp_engine.c dsp_engine.h dsp/interp/dsp_cpu.c $(DSP_HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -fno-strict-aliasing -I$(ROOT) -I$(ROOT)/recomp $(DSP_INCLUDES) audio_fx_vita_test.c audio_fx_fixture.c audio_progress.c -pthread -lm -o $@
$(BUILD)/audio_gp_pcm_vita_test: audio_mixer_bridge.c audio_mixer_bridge.h audio_stream_cursor.h audio_gp_pcm_vita_test.c audio_fx_vita_test.c audio_fx_fixture.c audio_fx.c audio_fx.h audio_fx_limits.h audio_filter_model.h audio_hrtf_model.h audio_vita_test.c audio_vita.c audio_bins.c audio_bins.h audio_progress.c audio_progress.h audio_host.h $(ROOT)/recomp/kernel/xk_audio.c $(ROOT)/recomp/xv_x86rt.c dsp_engine_test.c dsp_engine.c dsp_engine.h dsp/interp/dsp_cpu.c $(DSP_HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -fno-strict-aliasing -frounding-math -ffp-contract=off -ffunction-sections -fdata-sections -I$(ROOT) -I$(ROOT)/recomp $(DSP_INCLUDES) audio_gp_pcm_vita_test.c audio_fx_fixture.c audio_progress.c $(ROOT)/recomp/xv_x86rt.c -Wl,--gc-sections -pthread -lm -o $@
test-host: $(BUILD)/audio_gp_pcm_vita_test
$(BUILD)/audio_fx_host_test: audio_fx_host_test.c audio_dsp_test.c audio_buffer_test.c audio_host.c audio_host.h audio_fx_limits.h dsp_asset.h dsp_engine.h audio_progress.c audio_progress.h $(ROOT)/recomp/kernel/xk_audio.c $(ROOT)/recomp/kernel/xk_audio.h $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT) -I$(ROOT)/recomp audio_fx_host_test.c $(ROOT)/recomp/xv_x86rt.c -Wl,--gc-sections -lm -o $@
test-host: $(BUILD)/audio_fx_vita_test $(BUILD)/audio_fx_host_test
.PHONY: audio-probe
.PHONY: fx-probe
fx-probe: $(BUILD)/fxin2-probe.vpk
$(BUILD)/fxin2-probe.elf: audio_fx_probe.c audio_fx.c audio_fx.h audio_fx_limits.h audio_hrtf_model.h audio_filter_model.h audio_vita.c audio_bins.c audio_bins.h audio_progress.c audio_progress.h audio_host.h audio_fx_limits.h dsp_asset.c dsp_asset.h dsp_engine.c dsp_engine.h dsp/interp/dsp_cpu.c $(DSP_HEADERS) audio_mixer_bridge.c audio_mixer_bridge.h audio_stream_cursor.h $(ROOT)/recomp/kernel/xk_audio.h $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -mthumb -mcpu=cortex-a9 -mfpu=neon -fno-strict-aliasing -ffunction-sections -fdata-sections -DH2_AUDIO_DSP=1 -DH2_FX_PROBE_SECOND=$(FX_PROBE_SECOND) -I$(ROOT) -I$(ROOT)/recomp $(DSP_INCLUDES) -DXK_AUDIO_OUTPUT_FILTER=h2_audio_bins_filter -Dxk_os_audio_open=h2_audio_sink_open -Dxk_os_audio_mutex_lock=h2_audio_sink_lock -Dxk_os_audio_mutex_unlock=h2_audio_sink_unlock -Wl,-q,--gc-sections $(filter %.c,$^) -o $@ -lSceLibKernel_stub -lSceKernelThreadMgr_stub -lSceAudio_stub -lSceSysmem_stub -lSceProcessmgr_stub -lSceIofilemgr_stub -lm
$(BUILD)/fxin2-probe.velf: $(BUILD)/fxin2-probe.elf
	$(VITASDK)/bin/vita-elf-create $< $@
$(BUILD)/fxin2-probe-eboot.bin: $(BUILD)/fxin2-probe.velf
	$(VITASDK)/bin/vita-make-fself -s $< $@
$(BUILD)/fxin2-probe-param.sfo: Makefile
	@mkdir -p $(dir $@)
	$(VITASDK)/bin/vita-mksfoex -s TITLE_ID=XH2F00001 "H2 Private FX Sink Probe" $@
$(BUILD)/fxin2-probe.vpk: $(BUILD)/fxin2-probe-eboot.bin $(BUILD)/fxin2-probe-param.sfo $(DSP_ASSET)
	$(VITASDK)/bin/vita-pack-vpk -s $(BUILD)/fxin2-probe-param.sfo -b $(BUILD)/fxin2-probe-eboot.bin -a $(DSP_ASSET)=halo2-dsp.bin $@
audio-probe: $(BUILD)/audio-probe.vpk
$(BUILD)/audio-probe.elf: audio_probe.c audio_vita.c audio_bins.c audio_bins.h audio_progress.c audio_progress.h audio_host.h audio_fx_limits.h $(ROOT)/recomp/kernel/xk_audio.c $(ROOT)/recomp/kernel/xk_audio.h $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O1 -mthumb -mcpu=cortex-a9 -mfpu=neon -Wall -Wextra -Wno-misleading-indentation -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT) -I$(ROOT)/recomp -DXK_AUDIO_OUTPUT_FILTER=h2_audio_bins_filter -Dxk_os_audio_open=h2_audio_sink_open -Dxk_os_audio_mutex_lock=h2_audio_sink_lock -Dxk_os_audio_mutex_unlock=h2_audio_sink_unlock -Wl,-q,--gc-sections $(filter %.c,$^) -o $@ -lSceLibKernel_stub -lSceKernelThreadMgr_stub -lSceAudio_stub -lSceSysmem_stub -lSceProcessmgr_stub -lSceIofilemgr_stub -lm
$(BUILD)/audio-probe.velf: $(BUILD)/audio-probe.elf
	$(VITASDK)/bin/vita-elf-create $< $@
$(BUILD)/audio-probe-eboot.bin: $(BUILD)/audio-probe.velf
	$(VITASDK)/bin/vita-make-fself -s $< $@
$(BUILD)/audio-probe-param.sfo: Makefile
	@mkdir -p $(dir $@)
	$(VITASDK)/bin/vita-mksfoex -s TITLE_ID=XH2A00001 "H2 Synthetic Audio Probe" $@
$(BUILD)/audio-probe.vpk: $(BUILD)/audio-probe-eboot.bin $(BUILD)/audio-probe-param.sfo
	$(VITASDK)/bin/vita-pack-vpk -s $(BUILD)/audio-probe-param.sfo -b $(BUILD)/audio-probe-eboot.bin $@
.PHONY: quad-probe
# Separate synthetic utility. These shaders/constants are generated from an
# owned image and must remain private; this target does not change the game app.
quad-probe: $(BUILD)/quad-probe.vpk
$(BUILD)/quad-probe.elf: quad_probe.c
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O1 -mthumb -mcpu=cortex-a9 -mfpu=neon -Wall -Wextra -Werror -Wl,-q $< -o $@ -lSceGxm_stub -lSceSysmem_stub -lSceLibKernel_stub -lSceProcessmgr_stub
$(BUILD)/quad-probe.velf: $(BUILD)/quad-probe.elf
	$(VITASDK)/bin/vita-elf-create $< $@
$(BUILD)/quad-probe-eboot.bin: $(BUILD)/quad-probe.velf
	$(VITASDK)/bin/vita-make-fself -s $< $@
$(BUILD)/quad-probe-param.sfo: Makefile
	@mkdir -p $(dir $@)
	$(VITASDK)/bin/vita-mksfoex -s TITLE_ID=XH2T00001 "H2 Synthetic Quad Probe" $@
$(BUILD)/quad-probe.vpk: $(BUILD)/quad-probe-eboot.bin $(BUILD)/quad-probe-param.sfo $(QUAD_SHADERS)/quad.vert.gxp $(QUAD_SHADERS)/quad.frag.gxp $(QUAD_SHADERS)/constants.bin
	$(VITASDK)/bin/vita-pack-vpk -s $(BUILD)/quad-probe-param.sfo -b $(BUILD)/quad-probe-eboot.bin -a $(QUAD_SHADERS)/quad.vert.gxp=quad.vert.gxp -a $(QUAD_SHADERS)/quad.frag.gxp=quad.frag.gxp -a $(QUAD_SHADERS)/constants.bin=constants.bin $@
.PHONY: all test-host
SCREEN_PROBE_FILES := screen.vert.gxp screen.frag.gxp screen.copy.frag.gxp screen.constants.bin screen.vertices.bin screen.texture2.bin
.PHONY: screen-probe
# Isolated pixel validation only. Owned shaders/captured fixtures stay private;
# this optional utility does not change the game executable or accept a draw.
screen-probe: $(BUILD)/screen-probe.vpk
$(BUILD)/screen-probe.elf: screen_probe.c dxt23_layout.c dxt23_layout.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O1 -mthumb -mcpu=cortex-a9 -mfpu=neon -Wall -Wextra -Werror -Wl,-q screen_probe.c dxt23_layout.c -o $@ -lSceGxm_stub -lSceSysmem_stub -lSceLibKernel_stub -lSceProcessmgr_stub
$(BUILD)/screen-probe.velf: $(BUILD)/screen-probe.elf
	$(VITASDK)/bin/vita-elf-create $< $@
$(BUILD)/screen-probe-eboot.bin: $(BUILD)/screen-probe.velf
	$(VITASDK)/bin/vita-make-fself -s $< $@
$(BUILD)/screen-probe-param.sfo: Makefile
	@mkdir -p $(dir $@)
	$(VITASDK)/bin/vita-mksfoex -s TITLE_ID=XH2T00001 "H2 Screen Effect Probe" $@
$(BUILD)/screen-probe.vpk: $(BUILD)/screen-probe-eboot.bin $(BUILD)/screen-probe-param.sfo $(addprefix $(SCREEN_SHADERS)/,$(SCREEN_PROBE_FILES))
	$(VITASDK)/bin/vita-pack-vpk -s $(BUILD)/screen-probe-param.sfo -b $(BUILD)/screen-probe-eboot.bin $(foreach f,$(SCREEN_PROBE_FILES),-a $(SCREEN_SHADERS)/$(f)=$(f)) $@
# Separate BC1 validation utilities. They never alter the game draw consumer.
BC1_SHADERS ?= $(ROOT)/local/halo2_5849/bc1-shaders
BC1_PROBE_FILES := bc1.vert.gxp bc1.frag.gxp bc1.copy.frag.gxp bc1.constants.bin bc1.vertices.bin bc1.texture.bin
.PHONY: bc1-probe bc1-sample-probe
bc1-probe: $(BUILD)/bc1-probe.vpk
bc1-sample-probe: $(BUILD)/bc1-sample-probe.vpk
$(BUILD)/bc1-probe.elf $(BUILD)/bc1-sample-probe.elf: bc1_probe.c dxt23_layout.c dxt23_layout.h
	@mkdir -p $(dir $@)
	$(CC) -DH2_BC1_FLOAT_SAMPLE=$(if $(findstring -sample,$(notdir $@)),1,0) -std=gnu11 -O1 -mthumb -mcpu=cortex-a9 -mfpu=neon -Wall -Wextra -Werror -Wl,-q bc1_probe.c dxt23_layout.c -o $@ -lSceGxm_stub -lSceSysmem_stub -lSceLibKernel_stub -lSceProcessmgr_stub
$(BUILD)/bc1%.velf: $(BUILD)/bc1%.elf
	$(VITASDK)/bin/vita-elf-create $< $@
$(BUILD)/bc1%-eboot.bin: $(BUILD)/bc1%.velf
	$(VITASDK)/bin/vita-make-fself -s $< $@
$(BUILD)/bc1-probe-param.sfo: Makefile
	@mkdir -p $(dir $@)
	$(VITASDK)/bin/vita-mksfoex -s TITLE_ID=XH2T00001 "H2 BC1 Pixel Probe" $@
$(BUILD)/bc1%.vpk: $(BUILD)/bc1%-eboot.bin $(BUILD)/bc1-probe-param.sfo $(addprefix $(BC1_SHADERS)/,$(BC1_PROBE_FILES))
	$(VITASDK)/bin/vita-pack-vpk -s $(BUILD)/bc1-probe-param.sfo -b $(BUILD)/bc1$*-eboot.bin $(foreach f,$(BC1_PROBE_FILES),-a $(BC1_SHADERS)/$(f)=$(f)) $@
COMPOSITION_SHADERS ?= $(ROOT)/local/halo2_5849/composition-shaders
COMPOSITION_PROBE_FILES := composition.vert.gxp composition.frag.gxp composition.copy.frag.gxp composition.constants.bin composition.vertices.bin composition.texture0.bin composition.texture2.bin composition.texture3.bin
.PHONY: composition-probe composition-source-probe
composition-probe: $(BUILD)/composition-probe.vpk
composition-source-probe: $(BUILD)/composition-source-probe.vpk
$(BUILD)/composition-probe.elf $(BUILD)/composition-source-probe.elf: composition_probe.c dxt23_layout.c dxt23_layout.h
	@mkdir -p $(dir $@)
	$(CC) -DH2_COMPOSITION_SOURCE_PROBE=$(if $(findstring -source,$(notdir $@)),1,0) -std=gnu11 -O1 -mthumb -mcpu=cortex-a9 -mfpu=neon -Wall -Wextra -Werror -Wl,-q composition_probe.c dxt23_layout.c -o $@ -lSceGxm_stub -lSceSysmem_stub -lSceLibKernel_stub -lSceProcessmgr_stub
$(BUILD)/composition%.velf: $(BUILD)/composition%.elf
	$(VITASDK)/bin/vita-elf-create $< $@
$(BUILD)/composition%-eboot.bin: $(BUILD)/composition%.velf
	$(VITASDK)/bin/vita-make-fself -s $< $@
$(BUILD)/composition-probe-param.sfo: Makefile
	@mkdir -p $(dir $@)
	$(VITASDK)/bin/vita-mksfoex -s TITLE_ID=XH2T00001 "H2 Composition Probe" $@
$(BUILD)/composition%.vpk: $(BUILD)/composition%-eboot.bin $(BUILD)/composition-probe-param.sfo $(addprefix $(COMPOSITION_SHADERS)/,$(COMPOSITION_PROBE_FILES))
	$(VITASDK)/bin/vita-pack-vpk -s $(BUILD)/composition-probe-param.sfo -b $(BUILD)/composition$*-eboot.bin $(foreach f,$(COMPOSITION_PROBE_FILES),-a $(COMPOSITION_SHADERS)/$(f)=$(f)) $@
BLEND_SHADERS ?= $(ROOT)/local/halo2_5849/blend-shaders
BLEND_PROBE_FILES := blend.vert.gxp blend.frag.gxp blend.copy.frag.gxp blend.constants.bin blend.vertices.bin blend.texture.bin
.PHONY: blend-probe blend-source-probe
blend-probe: $(BUILD)/blend-probe.vpk
blend-source-probe: $(BUILD)/blend-source-probe.vpk
$(BUILD)/blend-probe.elf $(BUILD)/blend-source-probe.elf: blend_probe.c
	@mkdir -p $(dir $@)
	$(CC) -DH2_BLEND_SOURCE_PROBE=$(if $(findstring -source,$(notdir $@)),1,0) -std=gnu11 -O1 -mthumb -mcpu=cortex-a9 -mfpu=neon -Wall -Wextra -Werror -Wl,-q blend_probe.c -o $@ -lSceGxm_stub -lSceSysmem_stub -lSceLibKernel_stub -lSceProcessmgr_stub
$(BUILD)/blend%.velf: $(BUILD)/blend%.elf
	$(VITASDK)/bin/vita-elf-create $< $@
$(BUILD)/blend%-eboot.bin: $(BUILD)/blend%.velf
	$(VITASDK)/bin/vita-make-fself -s $< $@
$(BUILD)/blend-probe-param.sfo: Makefile
	@mkdir -p $(dir $@)
	$(VITASDK)/bin/vita-mksfoex -s TITLE_ID=XH2T00001 "H2 Blend Probe" $@
$(BUILD)/blend%.vpk: $(BUILD)/blend%-eboot.bin $(BUILD)/blend-probe-param.sfo $(addprefix $(BLEND_SHADERS)/,$(BLEND_PROBE_FILES))
	$(VITASDK)/bin/vita-pack-vpk -s $(BUILD)/blend-probe-param.sfo -b $(BUILD)/blend$*-eboot.bin $(foreach f,$(BLEND_PROBE_FILES),-a $(BLEND_SHADERS)/$(f)=$(f)) $@
THRESHOLD_SHADERS ?= $(ROOT)/local/halo2_5849/threshold-shaders
THRESHOLD_PROBE_FILES := threshold.vert.gxp threshold.frag.gxp threshold.constants.bin threshold.vertices.bin threshold.texture.bin
.PHONY: threshold-probe
threshold-probe: $(BUILD)/threshold-probe.vpk
$(BUILD)/threshold-probe.elf: threshold_probe.c
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O1 -mthumb -mcpu=cortex-a9 -mfpu=neon -Wall -Wextra -Werror -Wl,-q $< -o $@ -lSceGxm_stub -lSceSysmem_stub -lSceLibKernel_stub -lSceProcessmgr_stub
$(BUILD)/threshold-probe.velf: $(BUILD)/threshold-probe.elf
	$(VITASDK)/bin/vita-elf-create $< $@
$(BUILD)/threshold-probe-eboot.bin: $(BUILD)/threshold-probe.velf
	$(VITASDK)/bin/vita-make-fself -s $< $@
$(BUILD)/threshold-probe-param.sfo: Makefile
	@mkdir -p $(dir $@)
	$(VITASDK)/bin/vita-mksfoex -s TITLE_ID=XH2T00001 "H2 Threshold Probe" $@
$(BUILD)/threshold-probe.vpk: $(BUILD)/threshold-probe-eboot.bin $(BUILD)/threshold-probe-param.sfo $(addprefix $(THRESHOLD_SHADERS)/,$(THRESHOLD_PROBE_FILES))
	$(VITASDK)/bin/vita-pack-vpk -s $(BUILD)/threshold-probe-param.sfo -b $(BUILD)/threshold-probe-eboot.bin $(foreach f,$(THRESHOLD_PROBE_FILES),-a $(THRESHOLD_SHADERS)/$(f)=$(f)) $@
LUMA_SHADERS ?= $(ROOT)/local/halo2_5849/luma-shaders
SPRITE_SHADERS ?= $(ROOT)/local/halo2_5849/sprite-shaders
SPRITE_PROBE_FILES := sprite.vert.gxp sprite.frag.gxp sprite.copy.frag.gxp sprite.constants.bin sprite.vertices.bin sprite.texture.bin sprite.vertex-constants.bin
.PHONY: sprite-probe sprite-source-probe sprite-constant-probe sprite-constant-source-probe
sprite-probe: $(BUILD)/sprite-probe.vpk
sprite-source-probe: $(BUILD)/sprite-source-probe.vpk
sprite-constant-probe: $(BUILD)/sprite-constant-probe.vpk
sprite-constant-source-probe: $(BUILD)/sprite-constant-source-probe.vpk
.PHONY: sprite-point-probe
sprite-point-probe: $(BUILD)/sprite-point-probe.vpk
$(BUILD)/sprite-point-probe.elf: sprite_point_probe.c dxt23_layout.c dxt23_layout.h Makefile
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O1 -mthumb -mcpu=cortex-a9 -mfpu=neon -Wall -Wextra -Werror -Wl,-q,--defsym=__sce_headroom=0x1000 sprite_point_probe.c dxt23_layout.c -o $@ -lSceGxm_stub -lSceSysmem_stub -lSceLibKernel_stub -lSceProcessmgr_stub
$(BUILD)/sprite-point-probe.velf: $(BUILD)/sprite-point-probe.elf
	$(VITASDK)/bin/vita-elf-create $< $@
$(BUILD)/sprite-point-probe-eboot.bin: $(BUILD)/sprite-point-probe.velf
	$(VITASDK)/bin/vita-make-fself -s $< $@
$(BUILD)/sprite-point-probe.vpk: $(BUILD)/sprite-point-probe-eboot.bin $(BUILD)/sprite-probe-param.sfo $(addprefix $(SPRITE_SHADERS)/,$(SPRITE_PROBE_FILES))
	$(VITASDK)/bin/vita-pack-vpk -s $(BUILD)/sprite-probe-param.sfo -b $(BUILD)/sprite-point-probe-eboot.bin $(foreach f,$(SPRITE_PROBE_FILES),-a $(SPRITE_SHADERS)/$(f)=$(f)) $@
$(BUILD)/sprite-probe.elf $(BUILD)/sprite-source-probe.elf $(BUILD)/sprite-constant-probe.elf $(BUILD)/sprite-constant-source-probe.elf: sprite_probe.c dxt23_layout.c dxt23_layout.h Makefile
	@mkdir -p $(dir $@)
	$(CC) -DH2_SPRITE_SOURCE_PROBE=$(if $(findstring -source,$(notdir $@)),1,0) -DH2_SPRITE_CONSTANT_PROBE=$(if $(findstring -constant,$(notdir $@)),1,0) -std=gnu11 -O1 -mthumb -mcpu=cortex-a9 -mfpu=neon -Wall -Wextra -Werror -Wl,-q,--defsym=__sce_headroom=0x1000 sprite_probe.c dxt23_layout.c -o $@ -lSceGxm_stub -lSceSysmem_stub -lSceLibKernel_stub -lSceProcessmgr_stub
$(BUILD)/sprite-probe.velf $(BUILD)/sprite-source-probe.velf $(BUILD)/sprite-constant-probe.velf $(BUILD)/sprite-constant-source-probe.velf: $(BUILD)/%.velf: $(BUILD)/%.elf
	$(VITASDK)/bin/vita-elf-create $< $@
$(BUILD)/sprite-probe-eboot.bin: $(BUILD)/sprite-probe.velf
	$(VITASDK)/bin/vita-make-fself -s $< $@
$(BUILD)/sprite-source-probe-eboot.bin: $(BUILD)/sprite-source-probe.velf
	$(VITASDK)/bin/vita-make-fself -s $< $@
$(BUILD)/sprite-constant-probe-eboot.bin $(BUILD)/sprite-constant-source-probe-eboot.bin: $(BUILD)/%-eboot.bin: $(BUILD)/%.velf
	$(VITASDK)/bin/vita-make-fself -s $< $@
$(BUILD)/sprite-probe-param.sfo: Makefile
	@mkdir -p $(dir $@)
	$(VITASDK)/bin/vita-mksfoex -s TITLE_ID=XH2T00001 "H2 Packed Color Probe" $@
$(BUILD)/sprite-probe.vpk: $(BUILD)/sprite-probe-eboot.bin $(BUILD)/sprite-probe-param.sfo $(addprefix $(SPRITE_SHADERS)/,$(SPRITE_PROBE_FILES))
	$(VITASDK)/bin/vita-pack-vpk -s $(BUILD)/sprite-probe-param.sfo -b $(BUILD)/sprite-probe-eboot.bin $(foreach f,$(SPRITE_PROBE_FILES),-a $(SPRITE_SHADERS)/$(f)=$(f)) $@
$(BUILD)/sprite-source-probe.vpk: $(BUILD)/sprite-source-probe-eboot.bin $(BUILD)/sprite-probe-param.sfo $(addprefix $(SPRITE_SHADERS)/,$(SPRITE_PROBE_FILES))
	$(VITASDK)/bin/vita-pack-vpk -s $(BUILD)/sprite-probe-param.sfo -b $(BUILD)/sprite-source-probe-eboot.bin $(foreach f,$(SPRITE_PROBE_FILES),-a $(SPRITE_SHADERS)/$(f)=$(f)) $@
$(BUILD)/sprite-constant-probe.vpk $(BUILD)/sprite-constant-source-probe.vpk: $(BUILD)/%.vpk: $(BUILD)/%-eboot.bin $(BUILD)/sprite-probe-param.sfo $(addprefix $(SPRITE_SHADERS)/,$(SPRITE_PROBE_FILES) sprite.constant.texture.bin)
	$(VITASDK)/bin/vita-pack-vpk -s $(BUILD)/sprite-probe-param.sfo -b $< $(foreach f,$(SPRITE_PROBE_FILES) sprite.constant.texture.bin,-a $(SPRITE_SHADERS)/$(f)=$(f)) $@
LUMA_PROBE_FILES := luma.vert.gxp luma.frag.gxp luma.constants.bin luma.vertices.bin luma.texture.bin luma.vertex-constants.bin
.PHONY: luma-probe
luma-probe: $(BUILD)/luma-probe.vpk
$(BUILD)/luma-probe.elf: luma_probe.c
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O1 -mthumb -mcpu=cortex-a9 -mfpu=neon -Wall -Wextra -Werror -Wl,-q $< -o $@ -lSceGxm_stub -lSceSysmem_stub -lSceLibKernel_stub -lSceProcessmgr_stub
$(BUILD)/luma-probe.velf: $(BUILD)/luma-probe.elf
	$(VITASDK)/bin/vita-elf-create $< $@
$(BUILD)/luma-probe-eboot.bin: $(BUILD)/luma-probe.velf
	$(VITASDK)/bin/vita-make-fself -s $< $@
$(BUILD)/luma-probe-param.sfo: Makefile
	@mkdir -p $(dir $@)
	$(VITASDK)/bin/vita-mksfoex -s TITLE_ID=XH2T00001 "H2 Luminance Probe" $@
$(BUILD)/luma-probe.vpk: $(BUILD)/luma-probe-eboot.bin $(BUILD)/luma-probe-param.sfo $(addprefix $(LUMA_SHADERS)/,$(LUMA_PROBE_FILES))
	$(VITASDK)/bin/vita-pack-vpk -s $(BUILD)/luma-probe-param.sfo -b $(BUILD)/luma-probe-eboot.bin $(foreach f,$(LUMA_PROBE_FILES),-a $(LUMA_SHADERS)/$(f)=$(f)) $@
BLUR_SHADERS ?= $(ROOT)/local/halo2_5849/blur-shaders
BLUR_PROBE_FILES := blur.vert.gxp blur.frag.gxp blur.copy.frag.gxp blur.constants.bin blur.vertices.bin blur.variants.bin blur.texture.bin
.PHONY: blur-probe blur-sample-probe
blur-probe: $(BUILD)/blur-probe.vpk
blur-sample-probe: $(BUILD)/blur-sample-probe.vpk
$(BUILD)/blur-probe.elf $(BUILD)/blur-sample-probe.elf: blur_probe.c
	@mkdir -p $(dir $@)
	$(CC) -DH2_BLUR_FLOAT_SAMPLE=$(if $(findstring -sample,$(notdir $@)),1,0) -std=gnu11 -O1 -mthumb -mcpu=cortex-a9 -mfpu=neon -Wall -Wextra -Werror -Wl,-q $< -o $@ -lSceGxm_stub -lSceSysmem_stub -lSceLibKernel_stub -lSceProcessmgr_stub
$(BUILD)/blur%.velf: $(BUILD)/blur%.elf
	$(VITASDK)/bin/vita-elf-create $< $@
$(BUILD)/blur%-eboot.bin: $(BUILD)/blur%.velf
	$(VITASDK)/bin/vita-make-fself -s $< $@
$(BUILD)/blur-probe-param.sfo: Makefile
	@mkdir -p $(dir $@)
	$(VITASDK)/bin/vita-mksfoex -s TITLE_ID=XH2T00001 "H2 Blur Probe" $@
$(BUILD)/blur%.vpk: $(BUILD)/blur%-eboot.bin $(BUILD)/blur-probe-param.sfo $(addprefix $(BLUR_SHADERS)/,$(BLUR_PROBE_FILES))
	$(VITASDK)/bin/vita-pack-vpk -s $(BUILD)/blur-probe-param.sfo -b $(BUILD)/blur$*-eboot.bin $(foreach f,$(BLUR_PROBE_FILES),-a $(BLUR_SHADERS)/$(f)=$(f)) $@
test-host: $(BUILD)/dxt23_layout_test
test-host: $(BUILD)/luma_draw_test $(BUILD)/sprite_draw_test $(BUILD)/menu_draw_test $(BUILD)/nv2a_vsh_test $(BUILD)/menu_raster_test $(BUILD)/menu_texture_test $(BUILD)/menu_combiner_test
test-host: $(BUILD)/blend_draw_test
test-host: $(BUILD)/geometry_dispatch_test $(BUILD)/screen_draw_test $(BUILD)/bc1_draw_test $(BUILD)/composition_draw_test $(BUILD)/threshold_draw_test $(BUILD)/blur_draw_test
test-host: $(BUILD)/eflags_id_test $(BUILD)/eflags_id_default_test $(BUILD)/file_lifetime_test $(BUILD)/kernel_timer_test $(BUILD)/fp_environment_test $(BUILD)/linear_texture_test
test-host: $(BUILD)/quad_draw_test
test-host: $(BUILD)/blit_copy_test
test-host: $(BUILD)/audio_packet_host_test $(BUILD)/audio_stream_cursor_test
test-host: $(BUILD)/audio_multibin_failure_test $(BUILD)/audio_buffer_test $(BUILD)/audio_host_test $(BUILD)/audio_effects_failure_test $(BUILD)/audio_vita_test $(BUILD)/audio_mixer_test $(BUILD)/audio_bins_test $(BUILD)/audio_progress_test
all: $(BUILD)/halo2-boot.vpk
test-host: $(BUILD)/kernel_stack_test $(BUILD)/page_guard_test $(BUILD)/audio_unavailable_test $(BUILD)/input_test $(BUILD)/scanout_test $(BUILD)/command_state_test $(BUILD)/host_tiles_test $(BUILD)/host_channel_runtime_test $(BUILD)/host_channel_test $(BUILD)/kelvin_clear_test $(BUILD)/gpu_objects_test $(BUILD)/push_stream_test $(BUILD)/nv2a_memory_test $(BUILD)/instance_memory_test $(BUILD)/cache_volume_test $(BUILD)/nv2a_regs_test $(BUILD)/nv2a_timer_test
	$(BUILD)/dsp_engine_test
	$(BUILD)/audio_fx_test
	$(BUILD)/audio_hrtf_model_test
	$(BUILD)/audio_fx_vita_test
	$(BUILD)/audio_gp_pcm_vita_test
	$(BUILD)/audio_movie_gp_vita_test
	$(BUILD)/audio_fx_host_test
	$(BUILD)/kernel_timer_test
	$(BUILD)/linear_texture_test
	$(BUILD)/dxt23_layout_test
	$(BUILD)/screen_draw_test
	$(BUILD)/bc1_draw_test
	$(BUILD)/composition_draw_test
	$(BUILD)/threshold_draw_test
	$(BUILD)/blur_draw_test
	$(BUILD)/blend_draw_test
	$(BUILD)/luma_draw_test
	$(BUILD)/sprite_draw_test
	$(BUILD)/menu_draw_test
	$(BUILD)/nv2a_vsh_test
	$(BUILD)/menu_raster_test
	$(BUILD)/menu_texture_test
	$(BUILD)/menu_combiner_test
	$(BUILD)/geometry_dispatch_test
	$(BUILD)/quad_draw_test
	$(BUILD)/blit_copy_test
	$(BUILD)/eflags_id_test
	$(BUILD)/eflags_id_default_test
	$(BUILD)/file_lifetime_test
	$(BUILD)/fp_environment_test
	$(BUILD)/kernel_stack_test
	$(BUILD)/page_guard_test
	$(BUILD)/audio_unavailable_test
	$(BUILD)/audio_multibin_failure_test
	$(BUILD)/audio_buffer_test
	$(BUILD)/audio_host_test
	$(BUILD)/audio_dsp_test
	$(BUILD)/audio_stream_test
	$(BUILD)/audio_submix_test
	$(BUILD)/audio_effects_failure_test
	$(BUILD)/audio_vita_test
	$(BUILD)/audio_progress_test
	$(BUILD)/audio_packet_host_test
	$(BUILD)/audio_stream_cursor_test
	$(BUILD)/audio_mixer_test
	$(BUILD)/audio_bins_test
	$(BUILD)/input_test
	$(BUILD)/scanout_test
	$(BUILD)/command_state_test
	$(BUILD)/host_tiles_test
	$(BUILD)/host_channel_runtime_test
	$(BUILD)/host_channel_runtime_test --timed
	$(BUILD)/host_channel_runtime_test --active
	$(BUILD)/host_channel_test
	$(BUILD)/kelvin_clear_test
	$(BUILD)/gpu_objects_test
	$(BUILD)/push_stream_test
	$(BUILD)/nv2a_memory_test
	$(BUILD)/instance_memory_test
	$(BUILD)/cache_volume_test
	$(BUILD)/nv2a_regs_test
	$(BUILD)/nv2a_timer_test
$(BUILD)/eflags_id_test: eflags_id_test.c $(ROOT)/recomp/xv_x86rt.h $(ROOT)/recomp/xv_x86rt.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -DXV_EFLAGS_ID=1 -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT)/recomp $(filter %.c,$^) -Wl,--gc-sections -lm -o $@
$(BUILD)/eflags_id_default_test: eflags_id_test.c $(ROOT)/recomp/xv_x86rt.h $(ROOT)/recomp/xv_x86rt.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -UXV_EFLAGS_ID -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT)/recomp $(filter %.c,$^) -Wl,--gc-sections -lm -o $@
$(BUILD)/file_lifetime_test: file_lifetime_test.c $(ROOT)/recomp/kernel/xk_file.c $(ROOT)/recomp/kernel/xk_thread.c $(ROOT)/recomp/kernel/xk_rtl.c $(ROOT)/recomp/kernel/xk_os_host.c $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/kernel/xk.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-format-truncation -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT)/recomp $(filter %.c,$^) -Wl,--gc-sections -lm -o $@
$(BUILD)/kernel_timer_test: kernel_timer_test.c kernel_timer.c kernel_timer.h $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h $(ROOT)/recomp/kernel/xk.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-strict-aliasing -I$(ROOT)/recomp kernel_timer_test.c kernel_timer.c -o $@
$(BUILD)/fp_environment_test: fp_environment_test.c fp_environment.c fp_environment.h $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT)/recomp fp_environment_test.c fp_environment.c $(ROOT)/recomp/xv_x86rt.c -Wl,--gc-sections -lm -o $@
$(BUILD)/kernel_stack_test: kernel_stack_test.c kernel_stack.c kernel_stack.h $(ROOT)/recomp/kernel/xk_mem.c $(ROOT)/recomp/kernel/xk.h $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT)/recomp kernel_stack_test.c kernel_stack.c $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/kernel/xk_mem.c -Wl,--gc-sections -lm -o $@
$(BUILD)/page_guard_test: page_guard_test.c $(ROOT)/recomp/kernel/xk_mem.c $(ROOT)/recomp/kernel/xk.h $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT)/recomp page_guard_test.c $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/kernel/xk_mem.c -Wl,--gc-sections -lm -o $@
$(BUILD)/audio_unavailable_test: audio_unavailable_test.c audio_unavailable.c $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT)/recomp audio_unavailable_test.c audio_unavailable.c $(ROOT)/recomp/xv_x86rt.c -Wl,--gc-sections -lm -o $@
$(BUILD)/audio_dsp_test: audio_dsp_test.c audio_buffer_test.c audio_host.c audio_host.h audio_fx_limits.h dsp_asset.h dsp_engine.h audio_progress.c audio_progress.h $(ROOT)/recomp/kernel/xk_audio.c $(ROOT)/recomp/kernel/xk_audio.h $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT) -I$(ROOT)/recomp audio_dsp_test.c $(ROOT)/recomp/xv_x86rt.c -Wl,--gc-sections -lm -o $@
test-host: $(BUILD)/audio_dsp_test
$(BUILD)/audio_stream_test: audio_stream_test.c audio_buffer_test.c audio_host.c audio_host.h audio_fx_limits.h audio_progress.c audio_progress.h $(ROOT)/recomp/kernel/xk_audio.c $(ROOT)/recomp/kernel/xk_audio.h $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT) -I$(ROOT)/recomp audio_stream_test.c $(ROOT)/recomp/xv_x86rt.c -Wl,--gc-sections -lm -o $@
test-host: $(BUILD)/audio_stream_test
$(BUILD)/audio_submix_test: audio_submix_test.c audio_buffer_test.c audio_host.c audio_host.h audio_fx_limits.h audio_progress.c audio_progress.h $(ROOT)/recomp/kernel/xk_audio.c $(ROOT)/recomp/kernel/xk_audio.h $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT) -I$(ROOT)/recomp audio_submix_test.c $(ROOT)/recomp/xv_x86rt.c -Wl,--gc-sections -lm -o $@
test-host: $(BUILD)/audio_submix_test
$(BUILD)/audio_multibin_failure_test: audio_buffer_test.c audio_host.c audio_host.h audio_fx_limits.h audio_progress.c audio_progress.h $(ROOT)/recomp/kernel/xk_audio.c $(ROOT)/recomp/kernel/xk_audio.h $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -DH2_AUDIO_MULTIBIN_UNAVAILABLE=1 -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT) -I$(ROOT)/recomp audio_buffer_test.c $(ROOT)/recomp/xv_x86rt.c -Wl,--gc-sections -lm -o $@
$(BUILD)/audio_buffer_test: audio_buffer_test.c audio_host.c audio_host.h audio_fx_limits.h audio_progress.c audio_progress.h $(ROOT)/recomp/kernel/xk_audio.c $(ROOT)/recomp/kernel/xk_audio.h $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT) -I$(ROOT)/recomp audio_buffer_test.c $(ROOT)/recomp/xv_x86rt.c -Wl,--gc-sections -lm -o $@
$(BUILD)/audio_host_test: audio_host_test.c audio_host.c audio_host.h audio_fx_limits.h $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT) -I$(ROOT)/recomp audio_host_test.c $(ROOT)/recomp/xv_x86rt.c -Wl,--gc-sections -lm -o $@
$(BUILD)/audio_effects_failure_test: audio_host_test.c audio_host.c audio_host.h audio_fx_limits.h $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -DH2_AUDIO_EFFECTS_UNAVAILABLE=1 -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT) -I$(ROOT)/recomp audio_host_test.c $(ROOT)/recomp/xv_x86rt.c -Wl,--gc-sections -lm -o $@
$(BUILD)/audio_vita_test: audio_vita_test.c audio_vita.c audio_bins.c audio_bins.h audio_progress.c audio_progress.h audio_host.h audio_fx_limits.h $(ROOT)/recomp/kernel/xk_audio.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-strict-aliasing -I$(ROOT) -I$(ROOT)/recomp audio_vita_test.c audio_progress.c -pthread -o $@
$(BUILD)/audio_progress_test: audio_progress_test.c audio_progress.c audio_progress.h $(ROOT)/recomp/kernel/xk_audio.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -I$(ROOT) audio_progress_test.c audio_progress.c -o $@
$(BUILD)/audio_mixer_test: audio_mixer_test.c $(ROOT)/recomp/kernel/xk_audio.c $(ROOT)/recomp/kernel/xk_audio.h $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT) -I$(ROOT)/recomp $(filter %.c,$^) -Wl,--gc-sections -lm -o $@
$(BUILD)/audio_bins_test: audio_bins_test.c audio_bins.c audio_bins.h $(ROOT)/recomp/kernel/xk_audio.c $(ROOT)/recomp/kernel/xk_audio.h $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -fno-strict-aliasing -ffunction-sections -fdata-sections -DXK_AUDIO_OUTPUT_FILTER=h2_audio_bins_filter -I$(ROOT) -I$(ROOT)/recomp $(filter %.c,$^) -Wl,--gc-sections -lm -o $@
$(BUILD)/input_test: input_test.c input.c input.h $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT)/recomp input_test.c input.c $(ROOT)/recomp/xv_x86rt.c -Wl,--gc-sections -lm -o $@
$(BUILD)/scanout_test: scanout_test.c scanout.c scanout.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror scanout_test.c scanout.c -o $@
$(BUILD)/command_state_test: command_state_test.c command_state.c command_state.h kelvin_clear.c kelvin_clear.h gpu_objects.c gpu_objects.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror command_state_test.c command_state.c kelvin_clear.c gpu_objects.c -o $@
$(BUILD)/menu_draw_test: menu_draw_test.c menu_draw.c menu_draw.h command_state.h kelvin_clear.h gpu_objects.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror menu_draw_test.c -o $@
$(BUILD)/nv2a_vsh_test: nv2a_vsh_test.c nv2a_vsh.c nv2a_vsh.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror nv2a_vsh_test.c -lm -o $@
$(BUILD)/menu_raster_test: menu_raster_test.c menu_raster.c menu_raster.h menu_combiner.c menu_combiner.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror menu_raster_test.c menu_combiner.c -lm -o $@
$(BUILD)/menu_texture_test: menu_texture_test.c menu_texture.c menu_texture.h command_state.h kelvin_clear.h gpu_objects.c gpu_objects.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror menu_texture_test.c gpu_objects.c -o $@
$(BUILD)/menu_combiner_test: menu_combiner_test.c menu_combiner.c menu_combiner.h command_state.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror menu_combiner_test.c -lm -o $@
$(BUILD)/blit_copy_test: blit_copy_test.c command_state.c command_state.h kelvin_clear.c kelvin_clear.h gpu_objects.c gpu_objects.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror blit_copy_test.c command_state.c kelvin_clear.c gpu_objects.c -o $@
$(BUILD)/linear_texture_test: linear_texture_test.c linear_texture.c linear_texture.h command_state.h kelvin_clear.h gpu_objects.c gpu_objects.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror linear_texture_test.c linear_texture.c gpu_objects.c -o $@
$(BUILD)/quad_draw_test: quad_draw_test.c quad_draw.c quad_draw.h linear_texture.c linear_texture.h command_state.h kelvin_clear.h gpu_objects.c gpu_objects.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror quad_draw_test.c quad_draw.c linear_texture.c gpu_objects.c -o $@
$(BUILD)/dxt23_layout_test: dxt23_layout_test.c dxt23_layout.c dxt23_layout.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror dxt23_layout_test.c dxt23_layout.c -o $@
$(BUILD)/bc1_draw_test: bc1_draw_test.c bc1_draw.c bc1_draw.h linear_texture.c linear_texture.h gpu_objects.c gpu_objects.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -O1 -Wall -Wextra -Werror bc1_draw_test.c bc1_draw.c linear_texture.c gpu_objects.c -o $@
$(BUILD)/geometry_dispatch_test: geometry_dispatch_test.c host_channel_runtime.c host_channel_runtime.h quad_gxm.h quad_draw.h screen_draw.h bc1_draw.h composition_draw.h threshold_draw.h blur_draw.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT)/recomp $< -Wl,--gc-sections -o $@
$(BUILD)/threshold_draw_test: threshold_draw_test.c threshold_draw.c threshold_draw.h linear_texture.c linear_texture.h gpu_objects.c gpu_objects.h command_state.h kelvin_clear.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror threshold_draw_test.c threshold_draw.c linear_texture.c gpu_objects.c -o $@
$(BUILD)/blur_draw_test: blur_draw_test.c blur_draw.c blur_draw.h linear_texture.c linear_texture.h gpu_objects.c gpu_objects.h command_state.h kelvin_clear.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror blur_draw_test.c blur_draw.c linear_texture.c gpu_objects.c -o $@
$(BUILD)/composition_draw_test: composition_draw_test.c composition_draw.c composition_draw.h linear_texture.c linear_texture.h gpu_objects.c gpu_objects.h command_state.h kelvin_clear.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror composition_draw_test.c composition_draw.c linear_texture.c gpu_objects.c -o $@
$(BUILD)/blend_draw_test: blend_draw_test.c blend_draw.c blend_draw.h linear_texture.c linear_texture.h gpu_objects.c gpu_objects.h command_state.h kelvin_clear.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror blend_draw_test.c blend_draw.c linear_texture.c gpu_objects.c -o $@
$(BUILD)/luma_draw_test: luma_draw_test.c luma_draw.c luma_draw.h linear_texture.c linear_texture.h gpu_objects.c gpu_objects.h command_state.h kelvin_clear.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror luma_draw_test.c luma_draw.c linear_texture.c gpu_objects.c -o $@
$(BUILD)/sprite_draw_test: sprite_draw_test.c sprite_draw.c sprite_draw.h linear_texture.c linear_texture.h gpu_objects.c gpu_objects.h command_state.h kelvin_clear.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror sprite_draw_test.c sprite_draw.c linear_texture.c gpu_objects.c -o $@
$(BUILD)/screen_draw_test: screen_draw_test.c screen_draw.c screen_draw.h linear_texture.c linear_texture.h gpu_objects.c gpu_objects.h command_state.h kelvin_clear.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror screen_draw_test.c screen_draw.c linear_texture.c gpu_objects.c -o $@
$(BUILD)/host_channel_test: host_channel_test.c host_channel.c host_channel.h command_state.c command_state.h kelvin_clear.c kelvin_clear.h gpu_objects.c gpu_objects.h push_stream.c push_stream.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror host_channel_test.c host_channel.c command_state.c kelvin_clear.c gpu_objects.c push_stream.c -o $@
$(BUILD)/host_channel_runtime_test: host_channel_runtime_test.c host_channel_runtime.c host_channel_runtime.h fp_environment.h host_channel.c host_channel.h command_state.c command_state.h host_tiles.c host_tiles.h kelvin_clear.c kelvin_clear.h gpu_objects.c gpu_objects.h push_stream.c push_stream.h gpu_bus.c gpu_bus.h nv2a_regs.c nv2a_regs.h $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT)/recomp host_channel_runtime_test.c host_channel_runtime.c host_channel.c command_state.c host_tiles.c kelvin_clear.c gpu_objects.c push_stream.c gpu_bus.c nv2a_regs.c $(ROOT)/recomp/xv_x86rt.c -Wl,--gc-sections -lm -o $@
$(BUILD)/host_tiles_test: host_tiles_test.c host_tiles.c host_tiles.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror host_tiles_test.c host_tiles.c -o $@
$(BUILD)/kelvin_clear_test: kelvin_clear_test.c kelvin_clear.c kelvin_clear.h gpu_objects.c gpu_objects.h push_stream.c push_stream.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror kelvin_clear_test.c kelvin_clear.c gpu_objects.c push_stream.c -o $@
$(BUILD)/gpu_objects_test: gpu_objects_test.c gpu_objects.c gpu_objects.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror gpu_objects_test.c gpu_objects.c -o $@
$(BUILD)/push_stream_test: push_stream_test.c push_stream.c push_stream.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror push_stream_test.c push_stream.c -o $@
$(BUILD)/instance_memory_test: instance_memory_test.c instance_memory.c instance_memory.h $(ROOT)/recomp/kernel/xk_mem.c $(ROOT)/recomp/kernel/xk.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT)/recomp instance_memory_test.c instance_memory.c $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/kernel/xk_mem.c -Wl,--gc-sections -lm -o $@
$(BUILD)/nv2a_memory_test: nv2a_memory_test.c nv2a_regs.c nv2a_regs.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror nv2a_memory_test.c nv2a_regs.c -o $@
$(BUILD)/nv2a_timer_test: nv2a_timer_test.c nv2a_regs.c nv2a_regs.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror nv2a_timer_test.c nv2a_regs.c -o $@
$(BUILD)/nv2a_regs_test: nv2a_regs_test.c nv2a_regs.c nv2a_regs.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror nv2a_regs_test.c nv2a_regs.c -o $@
$(BUILD)/cache_volume_test: cache_volume_test.c cache_volume.c cache_volume.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT)/recomp $< -Wl,--gc-sections -o $@

$(BUILD)/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/boot.o: boot.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/cache_volume.o: cache_volume.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/instance_memory.o: instance_memory.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/gpu_bus.o: gpu_bus.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/nv2a_regs.o: nv2a_regs.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/linear_texture.o $(BUILD)/fp_environment.o $(BUILD)/kernel_timer.o $(BUILD)/kernel_stack.o $(BUILD)/command_snapshot.o $(BUILD)/host_channel_runtime.o $(BUILD)/command_state.o $(BUILD)/host_channel.o $(BUILD)/host_tiles.o $(BUILD)/push_stream.o $(BUILD)/gpu_objects.o $(BUILD)/kelvin_clear.o $(BUILD)/scanout.o $(BUILD)/input.o $(BUILD)/audio_unavailable.o: $(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/audio_mixer_bridge.o $(BUILD)/quad_draw.o $(BUILD)/quad_gxm.o $(BUILD)/screen_draw.o $(BUILD)/bc1_draw.o $(BUILD)/composition_draw.o $(BUILD)/threshold_draw.o $(BUILD)/blur_draw.o $(BUILD)/blend_draw.o $(BUILD)/luma_draw.o $(BUILD)/sprite_draw.o $(BUILD)/menu_draw.o $(BUILD)/nv2a_vsh.o $(BUILD)/menu_raster.o $(BUILD)/menu_raster_pool.o $(BUILD)/menu_texture.o $(BUILD)/menu_combiner.o $(BUILD)/menu_render.o $(BUILD)/menu_gxm.o $(BUILD)/dxt23_layout.o $(BUILD)/audio_host.o $(BUILD)/audio_vita.o $(BUILD)/audio_bins.o $(BUILD)/audio_progress.o $(BUILD)/audio_fx.o $(BUILD)/dsp_engine.o $(BUILD)/dsp_asset.o: $(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/dsp_core.o: dsp/interp/dsp_cpu.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/%.o: $(GENERATED)/%.c $(GENERATED)/xv_recomp_protos.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GUEST_OPT) -c $< -o $@
$(BUILD)/halo2-boot.elf: $(OBJECTS)
	$(CC) $(CFLAGS) -Wl,-q,--gc-sections,--defsym=__sce_headroom=0x1000 $(addprefix -Wl$(comma)--wrap=,$(WRAPS)) -o $@ $^ $(LIBS)
$(BUILD)/halo2-boot.velf: $(BUILD)/halo2-boot.elf
	$(VITASDK)/bin/vita-elf-create $< $@
$(BUILD)/eboot.bin: $(BUILD)/halo2-boot.velf
	$(VITASDK)/bin/vita-make-fself -s $< $@
$(BUILD)/param.sfo: Makefile
	@mkdir -p $(dir $@)
	$(VITASDK)/bin/vita-mksfoex -s TITLE_ID=XH2B00001 -s APP_VER=$$(python3 $(ROOT)/tools/gen_build_version.py --sfo) -d ATTRIBUTE2=12 "Xita Halo 2 Boot" $@
$(BUILD)/halo2-boot.vpk: $(MENU_FILES) $(BUILD)/eboot.bin $(BUILD)/param.sfo $(IMAGE) $(QUAD_FILES) $(BC1_FILES) $(COMPOSITION_FILES) $(THRESHOLD_FILES) $(BLUR_FILES) $(BLEND_FILES) $(LUMA_FILES) $(SPRITE_FILES) $(DSP_FILES)
	$(VITASDK)/bin/vita-pack-vpk -s $(BUILD)/param.sfo -b $(BUILD)/eboot.bin -a $(IMAGE)=halo2_image.bin $(QUAD_PACK) $(BC1_PACK) $(COMPOSITION_PACK) $(THRESHOLD_PACK) $(BLUR_PACK) $(BLEND_PACK) $(LUMA_PACK) $(SPRITE_PACK) $(DSP_PACK) $(MENU_PACK) $@
-include $(OBJECTS:.o=.d)

$(BUILD)/audio_hrtf_model_test: audio_hrtf_model_test.c audio_hrtf_model.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -frounding-math -ffp-contract=off $< -lm -o $@
test-host: $(BUILD)/audio_hrtf_model_test

$(BUILD)/audio_stream_cursor_test: audio_stream_cursor_test.c audio_stream_cursor.h audio_stream_test.c audio_buffer_test.c audio_host.c $(ROOT)/recomp/kernel/xk_audio.c $(ROOT)/recomp/kernel/xk_audio.h $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(BUILD)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT) -I$(ROOT)/recomp audio_stream_cursor_test.c $(ROOT)/recomp/xv_x86rt.c -Wl,--gc-sections -lm -o $@

$(BUILD)/audio_packet_host_test: audio_packet_host_test.c audio_dsp_test.c audio_buffer_test.c audio_host.c $(ROOT)/recomp/kernel/xk_audio.c $(ROOT)/recomp/kernel/xk_audio.h $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(BUILD)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT) -I$(ROOT)/recomp audio_packet_host_test.c $(ROOT)/recomp/xv_x86rt.c -Wl,--gc-sections -lm -o $@

$(BUILD)/audio_movie_gp_vita_test: audio_movie_gp_vita_test.c audio_mixer_bridge.c audio_mixer_bridge.h audio_stream_cursor.h audio_gp_pcm_vita_test.c audio_fx_vita_test.c audio_fx_fixture.c audio_fx.c audio_fx.h audio_fx_limits.h audio_filter_model.h audio_hrtf_model.h audio_vita_test.c audio_vita.c audio_bins.c audio_bins.h audio_progress.c audio_progress.h audio_host.h $(ROOT)/recomp/kernel/xk_audio.c $(ROOT)/recomp/xv_x86rt.c dsp_engine_test.c dsp_engine.c dsp_engine.h dsp/interp/dsp_cpu.c $(DSP_HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -fno-strict-aliasing -frounding-math -ffp-contract=off -ffunction-sections -fdata-sections -I$(ROOT) -I$(ROOT)/recomp $(DSP_INCLUDES) audio_movie_gp_vita_test.c audio_fx_fixture.c audio_progress.c $(ROOT)/recomp/xv_x86rt.c -Wl,--gc-sections -pthread -lm -o $@
test-host: $(BUILD)/audio_movie_gp_vita_test

.PHONY: version-force
$(BUILD)/xv_build.h: version-force $(ROOT)/version.json $(ROOT)/tools/gen_build_version.py
	python3 $(ROOT)/tools/gen_build_version.py --output $@ $(if $(BUILD_REVISION),--revision $(BUILD_REVISION),)
$(BUILD)/boot.o: $(BUILD)/xv_build.h
$(BUILD)/boot.o: CFLAGS += -include $(BUILD)/xv_build.h
$(BUILD)/param.sfo: $(ROOT)/version.json

$(BUILD)/menu_render.o: CFLAGS += -DH2_MENU_GXM_DEFAULT=$(MENU_GXM_DEFAULT)

.PHONY: menu-gxm-force
$(BUILD)/menu-gxm.config: menu-gxm-force
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(MENU_GXM_DEFAULT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
$(BUILD)/menu_render.o: $(BUILD)/menu-gxm.config
