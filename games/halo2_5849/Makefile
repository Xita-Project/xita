# Independent Halo 2 startup target; never uses recomp/ generated CE output.
ROOT := $(abspath ../..)
VITASDK ?= $(HOME)/vitasdk
CC := $(VITASDK)/bin/arm-vita-eabi-gcc
HOST_CC ?= cc
GUEST_OPT ?= -O1
HOST_CHANNEL ?= 0
GENERATED ?= $(ROOT)/local/halo2_5849/boot/generated
BUILD ?= $(ROOT)/local/halo2_5849/boot/build
IMAGE ?= $(ROOT)/local/halo2_5849/boot/halo2_image.bin
CFLAGS := -O1 -mthumb -mcpu=cortex-a9 -mfpu=neon -std=gnu11 -fno-strict-aliasing -ffunction-sections -fdata-sections -DXV_CHECK_GUEST_ADDRESS=1 -MMD -MP -I$(ROOT) -I$(ROOT)/runtime -I$(ROOT)/recomp -I$(ROOT)/recomp/kernel
CFLAGS += -DXV_EFLAGS_ID=1
SYS := recomp/xv_x86rt.c recomp/kernel/xk_mem.c recomp/kernel/xk_rtl.c recomp/kernel/xk_file.c recomp/kernel/xk_thread.c recomp/kernel/xk_xapi.c recomp/kernel/xk_crypto.c recomp/kernel/xk_os_vita.c runtime/xv_cpu.c
OBJECTS := $(addprefix $(BUILD)/,$(SYS:.c=.o)) $(BUILD)/boot.o $(BUILD)/kernel_stack.o $(BUILD)/command_snapshot.o $(BUILD)/cache_volume.o $(BUILD)/instance_memory.o $(BUILD)/gpu_bus.o $(BUILD)/nv2a_regs.o $(BUILD)/strict-kernel.o $(BUILD)/xv_fn_table.o $(patsubst $(GENERATED)/%.c,$(BUILD)/%.o,$(wildcard $(GENERATED)/code_*.c))
ifeq ($(HOST_CHANNEL),1)
OBJECTS += $(addprefix $(BUILD)/,host_channel_runtime.o command_state.o host_channel.o host_tiles.o push_stream.o gpu_objects.o kelvin_clear.o scanout.o input.o audio_unavailable.o)
OBJECTS += $(BUILD)/kernel_timer.o $(BUILD)/fp_environment.o
endif
comma := ,
WRAPS := xk_MmCreateKernelStack xk_MmDeleteKernelStack xk_MmClaimGpuInstanceMemory xv_unimpl xk_NtOpenFile xk_NtReadFile xk_NtWriteFile xk_NtDeviceIoControlFile xk_NtFsControlFile xk_NtQueryVolumeInformationFile
ifeq ($(HOST_CHANNEL),1)
WRAPS += xk_AvSendTVEncoderOption xk_AvSetDisplayMode xk_KeWaitForSingleObject
WRAPS += xk_KeInitializeTimerEx xk_KeInitializeDpc xk_KeSetTimer xk_KeSetTimerEx xk_KeCancelTimer xk_KeInsertQueueDpc xk_KeRemoveQueueDpc
WRAPS += xk_KfRaiseIrql xk_KfLowerIrql xk_KeRaiseIrqlToDpcLevel
endif
LIBS := -lSceLibKernel_stub -lSceKernelThreadMgr_stub -lSceSysmem_stub -lSceProcessmgr_stub -lSceIofilemgr_stub -lSceRtc_stub -lSceCtrl_stub -lSceTouch_stub -lSceAudio_stub -lSceDisplay_stub -lm
.DEFAULT_GOAL := all
.PHONY: all test-host
test-host: $(BUILD)/eflags_id_test $(BUILD)/eflags_id_default_test $(BUILD)/file_lifetime_test $(BUILD)/kernel_timer_test $(BUILD)/fp_environment_test
all: $(BUILD)/halo2-boot.vpk
test-host: $(BUILD)/kernel_stack_test $(BUILD)/audio_unavailable_test $(BUILD)/input_test $(BUILD)/scanout_test $(BUILD)/command_state_test $(BUILD)/host_tiles_test $(BUILD)/host_channel_runtime_test $(BUILD)/host_channel_test $(BUILD)/kelvin_clear_test $(BUILD)/gpu_objects_test $(BUILD)/push_stream_test $(BUILD)/nv2a_memory_test $(BUILD)/instance_memory_test $(BUILD)/cache_volume_test $(BUILD)/nv2a_regs_test $(BUILD)/nv2a_timer_test
	$(BUILD)/kernel_timer_test
	$(BUILD)/eflags_id_test
	$(BUILD)/eflags_id_default_test
	$(BUILD)/file_lifetime_test
	$(BUILD)/fp_environment_test
	$(BUILD)/kernel_stack_test
	$(BUILD)/audio_unavailable_test
	$(BUILD)/input_test
	$(BUILD)/scanout_test
	$(BUILD)/command_state_test
	$(BUILD)/host_tiles_test
	$(BUILD)/host_channel_runtime_test
	$(BUILD)/host_channel_runtime_test --timed
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
$(BUILD)/audio_unavailable_test: audio_unavailable_test.c audio_unavailable.c $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT)/recomp audio_unavailable_test.c audio_unavailable.c $(ROOT)/recomp/xv_x86rt.c -Wl,--gc-sections -lm -o $@
$(BUILD)/input_test: input_test.c input.c input.h $(ROOT)/recomp/xv_x86rt.c $(ROOT)/recomp/xv_x86rt.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu11 -O2 -Wall -Wextra -Werror -fno-strict-aliasing -ffunction-sections -fdata-sections -I$(ROOT)/recomp input_test.c input.c $(ROOT)/recomp/xv_x86rt.c -Wl,--gc-sections -lm -o $@
$(BUILD)/scanout_test: scanout_test.c scanout.c scanout.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror scanout_test.c scanout.c -o $@
$(BUILD)/command_state_test: command_state_test.c command_state.c command_state.h kelvin_clear.c kelvin_clear.h gpu_objects.c gpu_objects.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror command_state_test.c command_state.c kelvin_clear.c gpu_objects.c -o $@
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
$(BUILD)/fp_environment.o $(BUILD)/kernel_timer.o $(BUILD)/kernel_stack.o $(BUILD)/command_snapshot.o $(BUILD)/host_channel_runtime.o $(BUILD)/command_state.o $(BUILD)/host_channel.o $(BUILD)/host_tiles.o $(BUILD)/push_stream.o $(BUILD)/gpu_objects.o $(BUILD)/kelvin_clear.o $(BUILD)/scanout.o $(BUILD)/input.o $(BUILD)/audio_unavailable.o: $(BUILD)/%.o: %.c
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
	$(VITASDK)/bin/vita-mksfoex -s TITLE_ID=XH2B00001 -d ATTRIBUTE2=12 "Xita Halo 2 Boot" $@
$(BUILD)/halo2-boot.vpk: $(BUILD)/eboot.bin $(BUILD)/param.sfo $(IMAGE)
	$(VITASDK)/bin/vita-pack-vpk -s $(BUILD)/param.sfo -b $(BUILD)/eboot.bin -a $(IMAGE)=halo2_image.bin $@
-include $(OBJECTS:.o=.d)
