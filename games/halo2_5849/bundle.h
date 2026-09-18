#pragma once
/* Resource namespace is isolated from CE when both executables share app0. */
#ifdef H2_BUNDLED
#define H2_APP "app0:halo2/"
#define H2_ENV "ux0:data/xita-halo2/env.txt"
#define H2_SHADERS "ux0:data/xita-halo2/shaders/"
#else
#define H2_APP "app0:"
#define H2_ENV "ux0:data/xita/env.txt"
#define H2_SHADERS "ux0:data/xita/shaders/"
#endif
