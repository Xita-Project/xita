#ifndef XV_LOG_H
#define XV_LOG_H
#include <stdint.h>
/* Shared log sink for the Vita build (console + ux0:data/xita/xita.log). */
void xv_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Already formatted text. Preserves the full record and groups its file write. */
void xv_log_write(const char *text, unsigned length);
/* Group a bounded periodic report on the calling thread. Returns 1 only to
 * the outer owner, which must call end. Other threads still write immediately;
 * oversized reports spill in complete chunks without dropping text. */
int xv_log_report_begin(void);
void xv_log_report_end(void);
void xv_log_flush(void);
/* Explicit opt-in build + startup environment XV_PROFILE_ASYNC_REPORT=1.
 * Call once after configuration, before starting report producers. */
int xv_log_async_start(void);
enum { XV_LOG_SYNC, XV_LOG_STARTING, XV_LOG_RUNNING, XV_LOG_DRAINING,
       XV_LOG_STOPPED, XV_LOG_ERROR };
enum { XV_LOG_OK=0, XV_LOG_TIMEOUT=-1, XV_LOG_IO=-2, XV_LOG_BUSY=-3,
       XV_LOG_SELF=-4, XV_LOG_UNAVAILABLE=-5 };
typedef struct {
    /* Sequence/byte/timing counters describe only periodic queued output.
     * error also exposes a sticky loss on the immediate sink; that loss cannot
     * be erased by retrying an unrelated periodic chunk. */
    unsigned state, queued, high_water, open_report;
    int error, startup_error;
    uint64_t accepted, written, synced, accepted_bytes, written_bytes, synced_bytes;
    uint64_t completed_report, failed_sequence;
    unsigned failed_console_offset, failed_file_offset;
    uint64_t pending_report, pending_captured_us;
    unsigned pending_frame, pending_chunk, pending_length;
    uint64_t backpressure_count, backpressure_us, console_us, file_wait_us, file_us, sync_us;
} xv_log_status;
void xv_log_get_status(xv_log_status *out);
/* Barriers return success only after file sync. Async timeout bounds waiting,
 * not a synchronous-mode OS syscall. Foreign open reports return BUSY.
 * shutdown requires quiesced producers; timeout/error keeps handles and bytes
 * alive for diagnosis/retry. Repeated successful shutdown is harmless. */
int xv_log_flush_wait(unsigned timeout_us);
int xv_log_shutdown(unsigned timeout_us);
/* Explicit retry resumes a failed periodic chunk at its exact saved offsets;
 * it cannot recover ordinary immediate bytes already rejected by the sink. */
int xv_log_retry(void);
/* Always bypass report buffering; console evidence precedes any file wait. */
void xv_log_criticalf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int xv_log_report_begin_frame(unsigned frame);
/* Scope only when the async sink is active; synchronous call sites unchanged. */
int xv_log_report_begin_async_frame(unsigned frame);
#endif
