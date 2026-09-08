#ifndef XV_FRAME_SLOTS_H
#define XV_FRAME_SLOTS_H
#include <stdint.h>

#define XV_FRAME_SLOTS 3u
/* Ticket storage is power-of-two so uint32_t ticket wrap preserves its order.
 * Application buffer ownership is explicit and does not depend on parity. */
#define XV_FRAME_TICKETS 4u
typedef struct { uint32_t ticket; int owned; } xv_slot_owner;
static inline int xv_ticket_complete(uint32_t completed, uint32_t ticket)
{ return (int32_t)(completed - ticket) >= 0; }
static inline int xv_slot_busy(const xv_slot_owner *slot, uint32_t completed)
{ return slot->owned && !xv_ticket_complete(completed, slot->ticket); }
#endif
