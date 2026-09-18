#pragma once
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <sys/types.h>
#include <math.h>
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define MIN(x,y) ((x)<(y)?(x):(y))
#define MAX(x,y) ((x)>(y)?(x):(y))

/* Interpreter assertions must remain terminal even in release builds. */
#undef assert
_Noreturn void h2_dsp_assert_failure(const char *, const char *, int);
#define assert(v) ((v) ? (void)0 : h2_dsp_assert_failure(#v, __FILE__, __LINE__))
