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
XITA_GAME_SRCS += recomp/kernel/xk_hierarchy.c
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
ifeq ($(XV_EXPERIMENTAL_OBJECT_JOBS),1)
XITA_GAME_SRCS += recomp/kernel/xk_object_jobs.c
endif
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
