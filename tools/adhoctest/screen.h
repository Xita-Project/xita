#ifndef ADHOCTEST_SCREEN_H
#define ADHOCTEST_SCREEN_H
int screen_init(void);
void screen_status(int slot, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void screen_close(void);
void screen_draw(int dialog);
void log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int result(const char *name, int rc);
#define CALL(expr) result(#expr, (expr))
#endif
