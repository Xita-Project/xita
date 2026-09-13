#ifndef XV_LOG_H
#define XV_LOG_H
/* Shared log sink for the Vita build (console + ux0:data/xita/xita.log). */
void xv_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Already formatted text. Preserves the full record and groups its file write. */
void xv_log_write(const char *text, unsigned length);
void xv_log_flush(void);
#endif
