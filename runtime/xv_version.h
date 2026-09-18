#ifndef XV_VERSION_H
#define XV_VERSION_H
/* Release builds inject build/xv_build.h. Standalone host tools remain usable. */
#ifndef XV_BUILD_VERSION
#define XV_BUILD_VERSION "development"
#endif
#ifndef XV_BUILD_REVISION
#define XV_BUILD_REVISION "unknown"
#endif
#define XV_BUILD_LABEL "V" XV_BUILD_VERSION " / " XV_BUILD_REVISION
#endif
