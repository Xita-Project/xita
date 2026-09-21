# Native Halo adapter. Paths remain stable for existing differential-test tools;
# these objects are owned by libxita_game, never the shared system archive.
XITA_GAME_SRCS := recomp/kernel/xk_quality.c recomp/kernel/xk_math.c \
                  recomp/kernel/xk_clip.c recomp/kernel/xk_bounds.c recomp/kernel/xk_flare.c \
                  recomp/kernel/xk_geometry.c

# Explicit experiment; ordinary Halo builds retain their existing objects.
ifeq ($(XV_NATIVE_OBJECT_BASIS),1)
XITA_GAME_SRCS += recomp/kernel/xk_object_basis.c
endif
ifeq ($(XV_NATIVE_MODEL_PALETTE),1)
XITA_GAME_SRCS += recomp/kernel/xk_palette.c
endif
ifeq ($(XV_NATIVE_MODEL_HIERARCHY),1)
XITA_GAME_SRCS += recomp/kernel/xk_hierarchy.c recomp/kernel/xk_marker.c
endif
ifeq ($(XV_NATIVE_OBJECT_SCAN),1)
XITA_GAME_SRCS += recomp/kernel/xk_object_scan.c
endif
ifeq ($(XV_NATIVE_OBJECT_COLLECT),1)
XITA_GAME_SRCS += recomp/kernel/xk_object_collect.c
endif
ifeq ($(XV_NATIVE_COLLISION_VERTICES),1)
XITA_GAME_SRCS += recomp/kernel/xk_collision_vertices_control.c
endif
ifeq ($(XV_NATIVE_SEGMENT_SPHERE),1)
XITA_GAME_SRCS += recomp/kernel/xk_segment_sphere_control.c
endif
ifeq ($(XV_NATIVE_COLLISION_TRAVERSAL),1)
XITA_GAME_SRCS += recomp/kernel/xk_collision_traversal_control.c
endif
# Owned separate translation unit: do not append fusion to a large guest unit.
ifeq ($(XV_NATIVE_QUERY_FUSION),1)
XITA_GAME_SRCS += recomp/query_fusion.c
endif
ifeq ($(XV_NATIVE_SOLVER_FUSION),1)
XITA_GAME_SRCS += recomp/solver_fusion.c
endif
ifeq ($(XV_EXPERIMENTAL_OBJECT_JOBS),1)
XITA_GAME_SRCS += recomp/kernel/xk_object_jobs.c
endif
XITA_GAME_SRCS += recomp/kernel/xk_render_view.c
XITA_GAME_SRCS += recomp/kernel/xk_scene_thread.c
XITA_GAME_SRCS += recomp/kernel/xk_crt_float.c
ifeq ($(XV_WORKER_QUERY),1)
ifeq ($(XV_TYPED_CLUSTER_QUERY),1)
XITA_GAME_SRCS += recomp/kernel/xk_cluster_runtime.c recomp/kernel/xk_cluster_snapshot.c \
                  recomp/kernel/xk_cluster_query.c recomp/kernel/xk_cluster_query_replay.c
else
XITA_GAME_SRCS += recomp/kernel/xk_worker_query.c
endif
endif
ifeq ($(XV_QUAT_CACHE),1)
XITA_GAME_SRCS += recomp/kernel/xk_quat_cache.c
endif

# Generated from the owned image only; disabled in ordinary builds.
ifeq ($(XV_NATIVE_POLYGON_EDGE),1)
XITA_GAME_SRCS += recomp/kernel/xk_polygon_edge.c recomp/kernel/xk_polygon_edge_control.c
endif

# Count-only light-group observer; no new jobs or query implementation.
ifeq ($(XV_LIGHT_QUERY_CENSUS),1)
XITA_GAME_SRCS += recomp/kernel/xk_light_census.c
endif

ifeq ($(XV_NATIVE_CLIP_REGION),1)
XITA_GAME_SRCS += recomp/kernel/xk_clip_region.c recomp/kernel/xk_clip_region_control.c
endif
ifeq ($(XV_TYPED_PORTAL_POLYGON),1)
XITA_GAME_SRCS += recomp/kernel/xk_portal_polygon.c recomp/kernel/xk_portal_polygon_math.c
endif
ifeq ($(XV_TYPED_SUBCLUSTER),1)
XITA_GAME_SRCS += recomp/kernel/xk_subcluster.c recomp/kernel/xk_subcluster_math.c
endif
ifeq ($(XV_NATIVE_VISIBILITY_PASS),1)
XITA_GAME_SRCS += recomp/kernel/xk_visibility_pass.c
endif

# Optional two-scope owner elapsed census; no general phase/worker controls.
ifeq ($(XV_OWNER_PHASE),1)
XITA_GAME_SRCS += recomp/kernel/xk_owner_phase.c
endif

# Primary model fog arithmetic reuse; owner observer supplies live ownership.
ifeq ($(XV_MODEL_FOG),1)
XITA_GAME_SRCS += recomp/kernel/xk_model_fog.c
endif

# Scoped common-UV computation reuse; compile default OFF.
ifeq ($(XV_MODEL_UV),1)
XITA_GAME_SRCS += recomp/kernel/xk_model_uv.c
endif

# Probe only: bounded owned input history, never a result cache.
ifeq ($(XV_QUERY_REPEAT_CENSUS),1)
XITA_GAME_SRCS += recomp/kernel/xk_query_repeat.c recomp/kernel/xk_query_repeat_probe.c
endif

ifeq ($(XV_QUERY_REUSE),1)
XITA_GAME_SRCS += recomp/query_capture.c recomp/kernel/xk_query_reuse.c \
                 recomp/kernel/xk_query_memory.c recomp/kernel/xk_query_capture.c \
                 recomp/kernel/xk_query_cpu.c
endif

ifeq ($(XV_NATIVE_CONSTANT_PACK),1)
XITA_GAME_SRCS += recomp/kernel/xk_constant_pack.c
endif

ifeq ($(XV_POSE_PIPELINE),1)
XITA_GAME_SRCS += recomp/kernel/xk_pose_pipeline.c recomp/kernel/xk_frame_snapshot.c
endif
