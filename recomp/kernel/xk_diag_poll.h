#ifndef XK_DIAG_POLL_H
#define XK_DIAG_POLL_H
#include <stdint.h>
enum { XV_DIAG_SHOT=1, XV_DIAG_HIST=2 };
/* Serialized guest baton only, including controls at the joined Present
 * boundary. No filesystem or guest-state operation runs on the HTTP thread. */
void xv_diag_poll_shot(void);
int xv_diag_poll_hist(void); /* consumed one hist.now request */
int xv_diag_poll_available(unsigned path);
void xv_diag_poll_override(unsigned path, int suppress);
void xv_diag_poll_measure(void);
void xv_diag_poll_frame(uint64_t elapsed_us);
int xv_diag_poll_report(unsigned phase);
#endif
