CC ?= cc
.DEFAULT_GOAL := test
CFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing
CFLAGS += -I../.. -I../../runtime
VITASDK ?= $(HOME)/vitasdk
PYTHON ?= python3

.PHONY: test-log-batch
test-log-batch: build/log_batch_test
	./build/log_batch_test

build/log_batch_test: log_batch_test.c ../../runtime/xv_log.c ../../runtime/xv_log.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -idirafter $(VITASDK)/arm-vita-eabi/include log_batch_test.c -o $@

.PHONY: test-upload-worker
test-upload-worker: build/upload_worker_test
	./build/upload_worker_test

build/upload_worker_test: Makefile upload_worker_test.c vertex_upload_test.c ../../runtime/xv_vertex_upload.c ../../runtime/xv_vertex_upload.h ../../runtime/xv_upload_worker.c ../../runtime/xv_upload_worker.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -idirafter $(VITASDK)/arm-vita-eabi/include upload_worker_test.c -pthread -o $@

.PHONY: test-audio
test-audio: build/audio_test
	./build/audio_test

build/audio_test: audio_test.c ../kernel/xk_audio.c ../kernel/xk_audio.h ../xv_x86rt.c ../xv_x86rt.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -ffunction-sections -fdata-sections audio_test.c ../xv_x86rt.c -Wl,--gc-sections -lm -o $@

.PHONY: test-stencil
test-stencil: build/stencil_test
	./build/stencil_test

build/stencil_test: stencil_test.c ../../runtime/xv_stencil.h ../../runtime/xv_stencil_gxm.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=c11 -idirafter $(VITASDK)/arm-vita-eabi/include stencil_test.c -o $@

# Optional integration test: requires the user's locally recompiled Halo 3925.
.PHONY: test-signing
test-signing:
	$(PYTHON) signing_test.py

.PHONY: test-variants
test-variants:
	$(PYTHON) variant_test.py

.PHONY: test-split-screen
test-split-screen:
	$(PYTHON) split_screen_test.py

.PHONY: test-callbacks
test-callbacks:
	$(PYTHON) callback_test.py

.PHONY: test-vertex-shaders
test-vertex-shaders: build/vertex_shader_test
	./build/vertex_shader_test

build/vertex_shader_test: Makefile vertex_shader_test.c ../xv_x86rt.c ../kernel/xk_mem.c ../kernel/xd3d.c ../kernel/xd3d.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -ffunction-sections -fdata-sections vertex_shader_test.c ../xv_x86rt.c ../kernel/xk_mem.c ../kernel/xd3d.c -Wl,--gc-sections -lm -o $@

.PHONY: test-input
test-input: build/vita_input_test
	./build/vita_input_test
	./build/vita_input_test rear

build/vita_input_test: vita_input_test.c ../kernel/xk_os_vita.c ../kernel/xk.h ../../runtime/xv_benchmark.c ../../runtime/xv_remote.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -ffunction-sections -fdata-sections -idirafter $(VITASDK)/arm-vita-eabi/include vita_input_test.c ../../runtime/xv_benchmark.c -Wl,--gc-sections -lm -o $@

.PHONY: test-remote
test-remote:
	python3 ../../tools/test_remote.py

.PHONY: test
test: build/runtime_test build/immediate_test build/file_io_test build/file_open_test build/crypto_test build/profiler_test build/profiler_traced_test build/dispatch_profile_test build/div32_test
	./build/runtime_test
	./build/immediate_test
	./build/file_io_test
	./build/file_open_test
	./build/crypto_test
	./build/profiler_test
	./build/profiler_traced_test
	./build/dispatch_profile_test
	./build/div32_test

.PHONY: test-div32
test-div32: build/div32_test
	./build/div32_test

build/div32_test: Makefile div32_test.c ../xv_x86rt.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -I.. div32_test.c -lm -o $@

build/profiler_test: Makefile profiler_test.c ../xv_funchist.c ../xv_x86rt.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -ffunction-sections -fdata-sections profiler_test.c -Wl,--gc-sections -o $@

build/profiler_traced_test: Makefile profiler_test.c ../xv_funchist.c ../xv_x86rt.h
	mkdir -p build
	$(CC) $(CFLAGS) -DXV_TEST_GUEST_TRACE=1 -std=gnu11 -ffunction-sections -fdata-sections profiler_test.c -Wl,--gc-sections -o $@

.PHONY: test-render-profile
test-render-profile: build/render_profile_test build/render_profile_traced_test build/render_profile_no_clock_test
	./build/render_profile_test
	./build/render_profile_test 1
	./build/render_profile_traced_test
	./build/render_profile_traced_test 0
	./build/render_profile_no_clock_test 1

build/render_profile_test: render_profile_test.c ../../runtime/xv_render_profile.c ../../runtime/xv_render_profile.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 render_profile_test.c ../../runtime/xv_render_profile.c -o $@

build/render_profile_traced_test: render_profile_test.c ../../runtime/xv_render_profile.c ../../runtime/xv_render_profile.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -DXV_TEST_GUEST_TRACE=1 render_profile_test.c ../../runtime/xv_render_profile.c -o $@

build/render_profile_no_clock_test: render_profile_test.c ../../runtime/xv_render_profile.c ../../runtime/xv_render_profile.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -DXV_TEST_NO_CLOCK=1 render_profile_test.c ../../runtime/xv_render_profile.c -o $@

build/dispatch_profile_test: Makefile dispatch_profile_test.c ../xv_x86rt.c ../xv_x86rt.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -ffunction-sections -fdata-sections dispatch_profile_test.c ../xv_x86rt.c -Wl,--gc-sections -lm -o $@

build/crypto_test: Makefile crypto_test.c ../kernel/xk_crypto.c ../xv_x86rt.c ../xv_x86rt.h ../kernel/xk.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -ffunction-sections -fdata-sections crypto_test.c ../kernel/xk_crypto.c ../xv_x86rt.c -Wl,--gc-sections -lm -o $@

build/file_io_test: Makefile file_io_test.c ../kernel/xk_file.c ../xv_x86rt.c ../xv_x86rt.h ../kernel/xk.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -ffunction-sections -fdata-sections file_io_test.c ../kernel/xk_file.c ../xv_x86rt.c -Wl,--gc-sections -lm -o $@

build/file_open_test: Makefile file_open_test.c ../kernel/xk_file.c ../xv_x86rt.h ../kernel/xk.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -ffunction-sections -fdata-sections file_open_test.c ../kernel/xk_file.c -Wl,--gc-sections -o $@

build/immediate_test: Makefile immediate_test.c ../xv_x86rt.c ../kernel/xk_mem.c ../kernel/xd3d.c ../kernel/xd3d.h ../xv_x86rt.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -ffunction-sections -fdata-sections immediate_test.c ../xv_x86rt.c ../kernel/xk_mem.c ../kernel/xd3d.c -Wl,--gc-sections -lm -o $@

build/runtime_test: Makefile runtime_test.c ../xv_x86rt.c ../xv_x86rt.h ../kernel/xk_mem.c ../kernel/xk.h ../kernel/xk_os.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -ffunction-sections -fdata-sections runtime_test.c ../xv_x86rt.c ../kernel/xk_mem.c -Wl,--gc-sections -lm -o $@

.PHONY: test-read-retry
test-read-retry: build/read_retry_test
	./build/read_retry_test

.PHONY: test-file-sizes
test-file-sizes: build/vita_file_size_test
	./build/vita_file_size_test

build/vita_file_size_test: vita_file_size_test.c ../kernel/xk_os_vita.c ../kernel/xk_read_retry.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -ffunction-sections -fdata-sections -idirafter $(VITASDK)/arm-vita-eabi/include vita_file_size_test.c -Wl,--gc-sections -o $@

build/read_retry_test: read_retry_test.c ../kernel/xk_read_retry.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 read_retry_test.c -o $@

# Uses SDK type declarations, but links fake I/O and runs on the host.
.PHONY: test-cpu
test-cpu: build/cpu_usage_test
	./build/cpu_usage_test

build/cpu_usage_test: Makefile cpu_usage_test.c ../../runtime/xv_cpu.c ../../runtime/xv_cpu.h ../../runtime/xv_log.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -idirafter $(VITASDK)/arm-vita-eabi/include cpu_usage_test.c -o $@

.PHONY: test-textures
test-textures: build/texture_cache_test build/texture_worker_test
	./build/texture_cache_test
	XV_PALETTE_HASH_CACHE=0 ./build/texture_cache_test
	XV_EXTENDED_BC=1 ./build/texture_cache_test
	./build/texture_worker_test

build/texture_worker_test: texture_worker_test.c ../../runtime/xv_texture_worker.c ../../runtime/xv_texture_worker.h ../../runtime/xv_texture_decode.h ../../runtime/xv_bc_layout.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -idirafter $(VITASDK)/arm-vita-eabi/include texture_worker_test.c -pthread -o $@

build/texture_cache_test: Makefile texture_cache_test.c ../../runtime/xv_ui_gxm.c ../../runtime/xv_ui_gxm.h ../../runtime/xv_texture_worker.h ../../runtime/xv_texture_decode.h ../../runtime/xv_bc_layout.h ../../shaders/xv_layouts.h
	mkdir -p build
	$(CC) $(CFLAGS) -I../.. -Wno-pointer-to-int-cast -std=gnu11 -ffunction-sections -fdata-sections -idirafter $(VITASDK)/arm-vita-eabi/include texture_cache_test.c -Wl,--gc-sections -lm -o $@

.PHONY: test-shaders
test-shaders: build/shader_loader_test build/hud_program_test build/ps_cache_test
	./build/shader_loader_test
	./build/shader_loader_test override
	./build/hud_program_test
	./build/ps_cache_test
	$(PYTHON) ../../tools/test_ps_methods.py
	$(PYTHON) ../../tools/test_ps_programs.py
	$(PYTHON) ../../tools/test_depth_shader.py

.PHONY: test-frames
test-frames: build/frame_handoff_test build/frame_pacer_test build/vertex_upload_test build/bytes_equal_test
	./build/frame_handoff_test
	./build/frame_pacer_test
	./build/vertex_upload_test
	./build/bytes_equal_test
	$(PYTHON) ../../tools/test_frame_acquisition.py
	$(PYTHON) ../../tools/test_frame_completion.py
	$(PYTHON) ../../tools/test_scene_fences.py
	$(PYTHON) ../../tools/test_update_handoff.py

.PHONY: test-vertex-references
test-vertex-references:
	$(PYTHON) ../../tools/test_vertex_references.py

build/vertex_upload_test: ../../runtime/xv_vertex_refs.h vertex_upload_test.c ../../runtime/xv_vertex_upload.c ../../runtime/xv_vertex_upload.h ../../runtime/xv_frame_slots.h ../../runtime/xv_gpu_upload.h ../../runtime/xv_bytes_equal.h ../../runtime/xv_snapshot_copy.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -idirafter $(VITASDK)/arm-vita-eabi/include vertex_upload_test.c -o $@

build/bytes_equal_test: bytes_equal_test.c ../../runtime/xv_bytes_equal.h
	mkdir -p build
	$(CC) $(CFLAGS) bytes_equal_test.c -o $@

build/frame_pacer_test: frame_pacer_test.c ../../runtime/xv_frame_pacer.h
	mkdir -p build
	$(CC) $(CFLAGS) frame_pacer_test.c -o $@

build/frame_handoff_test: Makefile frame_handoff_test.c ../../runtime/xv_ui_gxm.c ../../runtime/xv_ui_gxm.h
	mkdir -p build
	$(CC) $(CFLAGS) -I../.. -Wno-pointer-to-int-cast -std=gnu11 -ffunction-sections -fdata-sections -idirafter $(VITASDK)/arm-vita-eabi/include frame_handoff_test.c -Wl,--gc-sections -lm -o $@

build/ps_cache_test: Makefile ps_cache_test.c ../../runtime/xv_d3d.c ../../runtime/xv_d3d.h ../../runtime/xv_index_copy.h ../../runtime/xv_vertex_refs.h ../../runtime/xv_bytes_equal.h ../../shaders/xv_ps_table.h
	mkdir -p build
	$(CC) $(CFLAGS) -I../.. -I../../shaders -Wno-pointer-to-int-cast -std=gnu11 -ffunction-sections -fdata-sections -idirafter $(VITASDK)/arm-vita-eabi/include ps_cache_test.c -Wl,--gc-sections -lm -o $@

build/hud_program_test: hud_program_test.c ../../runtime/xv_hud.h ../../runtime/xv_ps_key.h
	mkdir -p build
	$(CC) $(CFLAGS) hud_program_test.c -o $@

build/shader_loader_test: Makefile shader_loader_test.c ../../runtime/xv_shader.c ../../runtime/xv_shader.h ../../runtime/xv_log.h ../../shaders/xv_ps_gxp.h ../../shaders/xv_vs_gxp.h
	mkdir -p build
	$(CC) $(CFLAGS) -Wno-pointer-to-int-cast -std=gnu11 -ffunction-sections -fdata-sections -idirafter $(VITASDK)/arm-vita-eabi/include shader_loader_test.c -Wl,--gc-sections -o $@

.PHONY: test-geometry
test-geometry: build/geometry_worker_test
	./build/geometry_worker_test
	$(PYTHON) geometry_test.py

build/geometry_worker_test: geometry_worker_test.c ../../runtime/xv_geometry_sort.h ../../runtime/xv_geometry_worker.c ../../runtime/xv_geometry_worker.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -I../.. -idirafter $(VITASDK)/arm-vita-eabi/include geometry_worker_test.c -pthread -o $@

.PHONY: test-draw-prep
test-draw-prep: build/index_copy_test build/draw_profile_test build/draw_profile_no_clock_test build/ps_cache_test build/draw_state_test
	./build/draw_state_test
	./build/index_copy_test
	./build/draw_profile_test
	./build/draw_profile_test disabled
	./build/draw_profile_no_clock_test
	./build/ps_cache_test
	XV_DRAW_SCAN_NEON=1 ./build/ps_cache_test
	XV_INDEX_SCAN_CACHED=0 ./build/ps_cache_test

build/index_copy_test: index_copy_test.c ../../runtime/xv_index_copy.h ../../runtime/xv_vertex_refs.h
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@

build/draw_state_test: draw_state_test.c ../../runtime/xv_draw_state.h ../../runtime/xv_d3d.c ../../runtime/xv_bytes_equal.h ../../runtime/xv_index_copy.h ../../runtime/xv_vertex_refs.h
	mkdir -p build
	$(CC) $(CFLAGS) -I../.. -I../../shaders -Wno-pointer-to-int-cast -std=gnu11 -ffunction-sections -fdata-sections -idirafter $(VITASDK)/arm-vita-eabi/include draw_state_test.c -Wl,--gc-sections -lm -o $@

build/draw_profile_test: draw_profile_test.c ../../runtime/xv_draw_profile.c ../../runtime/xv_draw_profile.h
	mkdir -p build
	$(CC) $(CFLAGS) draw_profile_test.c ../../runtime/xv_draw_profile.c -o $@

build/draw_profile_no_clock_test: draw_profile_test.c ../../runtime/xv_draw_profile.c ../../runtime/xv_draw_profile.h
	mkdir -p build
	$(CC) $(CFLAGS) -DXV_TEST_NO_CLOCK draw_profile_test.c ../../runtime/xv_draw_profile.c -o $@

.PHONY: test-visibility
test-visibility: build/visibility_test
	./build/visibility_test
	./build/visibility_test 0
	./build/visibility_test 5000
	./build/visibility_test -5
	./build/visibility_test 100 fixed

.PHONY: test-cpu-prep
test-completion: build/resolution_benchmark_test build/native_math_benchmark_test build/no_point_math_benchmark_test build/scheduler_notify_test build/scheduler_vita_notify_test
	./build/resolution_benchmark_test
	./build/no_point_math_benchmark_test
	XV_BENCHMARK_VERTEX_WORKER=1 XV_BENCHMARK_VERTEX_REFERENCES=1 ./build/resolution_benchmark_test
	XV_BENCHMARK_DRAW_SCAN=1 ./build/resolution_benchmark_test
	XV_BENCHMARK_VERTEX_COPY=1 ./build/resolution_benchmark_test
	XV_BENCHMARK_DRAW_SCAN=1 XV_BENCHMARK_VERTEX_COPY=1 ./build/resolution_benchmark_test
	XV_BENCHMARK_OBJECT_BASIS=1 XV_BENCHMARK_VERTEX_WORKER=1 ./build/resolution_benchmark_test
	XV_BENCHMARK_MODEL_PALETTE=1 ./build/resolution_benchmark_test
	XV_BENCHMARK_OBJECT_BASIS=1 XV_BENCHMARK_MODEL_PALETTE=1 XV_BENCHMARK_VERTEX_WORKER=1 ./build/native_math_benchmark_test
	XV_BENCHMARK_MODEL_PALETTE=1 XV_BENCHMARK_VERTEX_WORKER=1 ./build/native_math_benchmark_test
	XV_BENCHMARK_OBJECT_BASIS=1 XV_NATIVE_MATH=0 ./build/native_math_benchmark_test
	XV_BENCHMARK_MODEL_PALETTE=1 XV_NATIVE_MATH=0 ./build/native_math_benchmark_test
	./build/scheduler_notify_test
	./build/scheduler_vita_notify_test

build/resolution_benchmark_test: resolution_benchmark_test.c ../../runtime/xv_benchmark.c ../../runtime/xv_benchmark.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 resolution_benchmark_test.c -lm -o $@

build/no_point_math_benchmark_test: resolution_benchmark_test.c ../../runtime/xv_benchmark.c ../../runtime/xv_benchmark.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -DTEST_NO_POINT_MATH resolution_benchmark_test.c -lm -o $@

build/native_math_benchmark_test: resolution_benchmark_test.c ../../runtime/xv_benchmark.c ../../runtime/xv_benchmark.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -DXV_NATIVE_OBJECT_BASIS -DXV_NATIVE_MODEL_PALETTE resolution_benchmark_test.c -lm -o $@

build/scheduler_notify_test: scheduler_notify_test.c ../kernel/xk_os_host.c ../kernel/xk_os.h
	$(CC) $(CFLAGS) -std=gnu11 -ffunction-sections -fdata-sections scheduler_notify_test.c -Wl,--gc-sections -pthread -lm -o $@

build/scheduler_vita_notify_test: scheduler_vita_notify_test.c ../kernel/xk_os_vita.c ../kernel/xk_os.h
	$(CC) $(CFLAGS) -std=gnu11 -ffunction-sections -fdata-sections -idirafter $(VITASDK)/arm-vita-eabi/include scheduler_vita_notify_test.c -Wl,--gc-sections -lm -o $@

test-cpu-prep: build/ps_identity_test build/frame_events_test build/sleep_wait_test build/yield_handoff_test build/draw_optimization_test
	./build/ps_identity_test
	./build/ps_identity_test disabled
	./build/frame_events_test
	./build/sleep_wait_test
	./build/yield_handoff_test
	./build/yield_handoff_test disabled
	./build/draw_optimization_test
	./build/draw_optimization_test disabled

build/draw_optimization_test: draw_optimization_test.c ../../runtime/xv_d3d.c ../../runtime/xv_shader.h
	mkdir -p build
	$(CC) $(CFLAGS) -I../.. -I../../shaders -Wno-pointer-to-int-cast -std=gnu11 -ffunction-sections -fdata-sections -idirafter $(VITASDK)/arm-vita-eabi/include draw_optimization_test.c -Wl,--gc-sections -lm -o $@

.PHONY: test-texture-preparation
test-texture-preparation: build/texture_preparation_test
	./build/texture_preparation_test
	./build/texture_preparation_test disabled
	./build/texture_preparation_test override
	$(PYTHON) ../../tools/test_draw_textures.py

build/texture_preparation_test: Makefile texture_preparation_test.c ../../runtime/xv_d3d.c ../../runtime/xv_shader.h ../../shaders/xv_ps_table.h
	mkdir -p build
	$(CC) $(CFLAGS) -I../.. -I../../shaders -Wno-pointer-to-int-cast -std=gnu11 -ffunction-sections -fdata-sections -idirafter $(VITASDK)/arm-vita-eabi/include texture_preparation_test.c -Wl,--gc-sections -lm -o $@

build/yield_handoff_test: yield_handoff_test.c ../kernel/xk_thread.c ../kernel/xk.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -ffunction-sections -fdata-sections yield_handoff_test.c -Wl,--gc-sections -o $@

build/sleep_wait_test: sleep_wait_test.c ../kernel/xk_thread.c ../kernel/xk.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -ffunction-sections -fdata-sections sleep_wait_test.c -Wl,--gc-sections -o $@

build/ps_identity_test: ps_identity_test.c ../../runtime/xv_ps_identity.h ../../runtime/xv_ps_key.h ../kernel/xd3d.c ../kernel/xd3d.h ../kernel/xk_mem.c ../xv_x86rt.c
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -ffunction-sections -fdata-sections ps_identity_test.c ../xv_x86rt.c ../kernel/xk_mem.c -Wl,--gc-sections -lm -o $@

build/frame_events_test: frame_events_test.c ../../runtime/xv_frame_events.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -idirafter $(VITASDK)/arm-vita-eabi/include frame_events_test.c -pthread -o $@

build/visibility_test: Makefile visibility_test.c ../../runtime/xv_d3d.c ../../runtime/xv_visibility.h ../kernel/xd3d.c ../xv_x86rt.c ../kernel/xk_mem.c
	mkdir -p build
	$(CC) $(CFLAGS) -I../.. -I../../shaders -Wno-pointer-to-int-cast -std=gnu11 -ffunction-sections -fdata-sections -idirafter $(VITASDK)/arm-vita-eabi/include visibility_test.c ../xv_x86rt.c ../kernel/xk_mem.c ../kernel/xd3d.c -Wl,--gc-sections -lm -pthread -o $@

.PHONY: test-surface-desc
test-surface-desc: build/surface_desc_test
	./build/surface_desc_test

build/surface_desc_test: Makefile surface_desc_test.c ../kernel/xd3d.c ../xv_x86rt.c ../kernel/xk_mem.c
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 -ffunction-sections -fdata-sections surface_desc_test.c ../xv_x86rt.c ../kernel/xk_mem.c ../kernel/xd3d.c -Wl,--gc-sections -lm -o $@

.PHONY: test-opaque-material
test-opaque-material: build/texture_alpha_test build/opaque_material_test build/texture_cache_test
	$(PYTHON) ../../tools/test_opaque_material.py
	./build/texture_alpha_test
	./build/opaque_material_test
	./build/opaque_material_test override
	./build/opaque_material_test no-alpha-specialization
	XV_BC_MIPS=1 ./build/texture_cache_test

build/texture_alpha_test: texture_alpha_test.c ../../runtime/xv_texture_alpha.h ../../runtime/xv_texture_decode.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 texture_alpha_test.c -lm -o $@

build/opaque_material_test: opaque_material_test.c texture_preparation_test.c ../../runtime/xv_d3d.c ../../runtime/xv_texture_alpha.h ../../shaders/xv_ps_table.h
	mkdir -p build
	$(CC) $(CFLAGS) -I../.. -I../../shaders -Wno-pointer-to-int-cast -std=gnu11 -ffunction-sections -fdata-sections -idirafter $(VITASDK)/arm-vita-eabi/include opaque_material_test.c -Wl,--gc-sections -lm -o $@

build/texture_cache_test build/texture_preparation_test build/draw_optimization_test: ../../runtime/xv_texture_alpha.h

.PHONY: test-cutout
test-cutout: build/cutout_policy_test build/ps_cache_test
	$(PYTHON) ../../tools/test_cutout_specialization.py
	./build/cutout_policy_test
	./build/cutout_policy_test disabled
	./build/cutout_policy_test override
	./build/cutout_policy_test no-atest
	./build/cutout_policy_test no-alpha
	./build/ps_cache_test

build/cutout_policy_test: cutout_policy_test.c ps_cache_test.c ../../runtime/xv_d3d.c ../../runtime/xv_shader.h ../../shaders/xv_ps_table.h
	@mkdir -p build
	$(CC) $(CFLAGS) -I../.. -I../../shaders -Wno-pointer-to-int-cast -std=gnu11 -ffunction-sections -fdata-sections -idirafter $(VITASDK)/arm-vita-eabi/include cutout_policy_test.c -Wl,--gc-sections -lm -o $@

build/ps_cache_test build/texture_preparation_test build/opaque_material_test: ../../runtime/xv_shader.h

.PHONY: test-rgba-layout
test-rgba-layout: build/rgba_layout_test
	./build/rgba_layout_test

build/rgba_layout_test: Makefile rgba_layout_test.c texture_cache_test.c ../../runtime/xv_ui_gxm.c ../../runtime/xv_rgba_layout.h
	mkdir -p build
	$(CC) $(CFLAGS) -I../.. -Wno-pointer-to-int-cast -std=gnu11 -ffunction-sections -fdata-sections -idirafter $(VITASDK)/arm-vita-eabi/include rgba_layout_test.c -Wl,--gc-sections -lm -o $@

.PHONY: test-alpha-range
test-alpha-range: build/alpha_range_test
	./build/alpha_range_test

build/alpha_range_test: Makefile alpha_range_test.c ../../runtime/xv_alpha_range.h ../../runtime/xv_texture_decode.h
	mkdir -p build
	$(CC) $(CFLAGS) -std=gnu11 alpha_range_test.c -lm -o $@

.PHONY: test-profiles
test-profiles:
	$(PYTHON) ../../tools/test_game_profiles.py
