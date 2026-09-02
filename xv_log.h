#ifndef XV_LOG_H
#define XV_LOG_H
/* Shared log sink for the Vita build (console + ux0:data/xboxvita/xboxvita.log). */
void xv_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void xv_log_flush(void);
#endif
