# Vita application and renderer

This folder contains the native Vita application, graphics submission,
resource handling, workers and profiling. Build from the repository root:

```sh
make RECOMP=1
```

| Files | Responsibility |
| --- | --- |
| `main.c`, `xv_boot.c` | Application startup, frame pump and game launch. |
| `xv_ui_gxm.c`, `xv_shader.c` | GXM rendering, texture handling and shader programs. |
| `xv_d3d.c` | Translated draw commands and graphics state. |
| `xv_gpu_upload.c`, `xv_vertex_upload.c` | Owned GPU upload memory and vertex snapshots. |
| `xv_texture_worker.c`, `xv_geometry_worker.c` | Background preparation work. |
| `xv_cpu.c`, `xv_benchmark.c`, `xv_*profile.c` | Utilization, benchmarks and timing. |
| `xv_scene.c` | Standalone scene/mock rendering support. |

Related headers live beside their implementations. The shared Xbox interfaces
are in [recomp/kernel/](../recomp/kernel/), the launcher UI is in
[dashboard/](../dashboard/README.md), and shader inputs are in `shaders/`.
The file move changes source paths, not the division of CPU/GPU work.
