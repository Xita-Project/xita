/* Admission helpers for the experimental native feature builder.
 * No runtime hook. Caller owns mapping lifetime and allocation validity. */
#ifndef XK_FEATURE_MEMORY_H
#define XK_FEATURE_MEMORY_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t *host;
    uint64_t physical,bytes;
} xk_feature_span;

/* Accept only one physically contiguous span. Refusal leaves result unchanged.
 * Page-table offsets describe the same arena as the translated guest accesses.
 * This validates translation/bounds, not allocation ownership or permissions. */
static inline int xk_feature_map_span(uint8_t *ram,uint64_t ram_bytes,
        const uint32_t *pages,size_t page_count,uint32_t address,size_t bytes,
        xk_feature_span *result){
    if(!ram || !pages || !bytes || ram_bytes>SIZE_MAX)return 0;
    uint64_t end=(uint64_t)address+bytes;
    if(end>UINT64_C(0x100000000) || end<=address)return 0;
    uint32_t first=address>>12,last=(uint32_t)((end-1)>>12);
    if(last>=page_count)return 0;
    uint64_t base=pages[first],physical=base+(address&4095u);
    if((base&4095u) || physical>ram_bytes || bytes>ram_bytes-physical)return 0;
    for(uint32_t i=first;i<=last;i++)
        if((uint64_t)pages[i]!=base+(uint64_t)(i-first)*4096u)return 0;
    xk_feature_span span={ram+(size_t)physical,physical,bytes};
    *result=span;return 1;
}

static inline int xk_feature_spans_overlap(const xk_feature_span *a,
                                         const xk_feature_span *b){
    /* Subtraction avoids overflowing the upper bound, even for test inputs. */
    if(!a->bytes || !b->bytes)return 0;
    return a->physical<=b->physical ? b->physical-a->physical<a->bytes
                                  : a->physical-b->physical<b->bytes;
}

/* Output and guest scratch stack must not overlap each other or any cached
 * read span (headers, lists, geometry, matrix, constants). Read/read aliasing
 * is permitted. Every argument must already have passed map_span. */
static inline int xk_feature_layout_disjoint(const xk_feature_span *output,
        const xk_feature_span *stack,const xk_feature_span *reads,size_t count){
    if(xk_feature_spans_overlap(output,stack))return 0;
    for(size_t i=0;i<count;i++)
        if(xk_feature_spans_overlap(output,&reads[i]) ||
           xk_feature_spans_overlap(stack,&reads[i]))return 0;
    return 1;
}
#endif
