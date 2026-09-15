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
ifeq ($(XV_EXPERIMENTAL_OBJECT_JOBS),1)
XITA_GAME_SRCS += recomp/kernel/xk_object_jobs.c
endif
ifeq ($(XV_QUAT_CACHE),1)
XITA_GAME_SRCS += recomp/kernel/xk_quat_cache.c
endif
