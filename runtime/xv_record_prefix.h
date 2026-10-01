/* Private compile-time trial. OFF keeps the original whole-packet path. */
#ifndef XV_RECORD_PREFIX_H
#define XV_RECORD_PREFIX_H
#ifndef XV_RECORD_PREFIX
#define XV_RECORD_PREFIX 0
#endif
#if XV_RECORD_PREFIX != 0 && XV_RECORD_PREFIX != 1
#error XV_RECORD_PREFIX must be 0 or 1
#endif
#if XV_RECORD_PREFIX
#include <stdint.h>
typedef struct {
    uint32_t frame,command,constants,visibility;
    uint32_t upload_ticket;
    unsigned has_upload;
} xv_record_prefix;
/* The recorder owns the packet before this release; after acceptance its
 * prefix arrays remain immutable until original final frame retirement. */
int xv_record_prefix_offer(const xv_record_prefix *prefix);
#endif
#endif
