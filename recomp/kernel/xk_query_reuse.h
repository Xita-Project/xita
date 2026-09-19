#ifndef XK_QUERY_REUSE_H
#define XK_QUERY_REUSE_H
struct xctx;
/* Selected world wrapper only, inside its existing actor guard. Calls exactly
 * one original/captured query or a fully validated replay. No new lock, timer,
 * allocation or guest pointer ownership. Capture generation must invalidate
 * BOTH records before every callback/unknown execution path. */
void xv_query_reuse_run(struct xctx *c);
/* Both hooks require the joined owner; epoch means object pass, not frame. */
void xv_query_reuse_epoch(void);
void xv_query_reuse_report(unsigned frames);
#endif
