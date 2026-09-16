/* Separate translation unit: the instruction harness intercepts these imports.
 * Keep definitions opaque to the fixture and candidate compiler, so it cannot
 * optimize away callers' memory writes by seeing these placeholder bodies. */
#include <stddef.h>
void *memcpy(void*d,const void*s,size_t n){(void)s;(void)n;return d;}
void *memmove(void*d,const void*s,size_t n){(void)s;(void)n;return d;}
void *memset(void*d,int v,size_t n){(void)v;(void)n;return d;}
