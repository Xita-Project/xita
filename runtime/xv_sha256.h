#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct { uint32_t h[8]; uint64_t bytes; unsigned used; unsigned char block[64]; } xv_sha256;
void xv_sha256_init(xv_sha256 *s);
void xv_sha256_add(xv_sha256 *s,const void *data,size_t size);
void xv_sha256_end(xv_sha256 *s,char hex[65]);
