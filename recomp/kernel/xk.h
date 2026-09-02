/*
 * xk.h - XboxVita kernel translator (blueprint section 3): the Xbox kernel surface the
 * recompiled game calls, implemented over a tiny OS abstraction (xk_os.h) so the same
 * code runs on the Vita (fibers on core 0) and in the host harness (ucontext).
 *
 * Calling convention: every xk_<Export>(xctx *c) reads stdcall args with X_ARG(n) and
 * finishes with X_RET(n).  fastcall exports take ecx/edx.  Return value in eax.
 */
#pragma once
#include <stdarg.h>
#include "../xv_x86rt.h"
#include "xk_os.h"

#define XK_LOG(...) xk_os_log(__VA_ARGS__)

/* ---- NTSTATUS ---------------------------------------------------------------------- */
#define STATUS_SUCCESS                  0x00000000u
#define STATUS_TIMEOUT                  0x00000102u
#define STATUS_PENDING                  0x00000103u
#define STATUS_ALERTED                  0x00000101u
#define STATUS_USER_APC                 0x000000C0u
#define STATUS_BUFFER_OVERFLOW          0x80000005u
#define STATUS_NO_MORE_FILES            0x80000006u
#define STATUS_UNSUCCESSFUL             0xC0000001u
#define STATUS_NOT_IMPLEMENTED          0xC0000002u
#define STATUS_INVALID_INFO_CLASS       0xC0000003u
#define STATUS_INFO_LENGTH_MISMATCH     0xC0000004u
#define STATUS_INVALID_HANDLE           0xC0000008u
#define STATUS_INVALID_PARAMETER        0xC000000Du
#define STATUS_NO_SUCH_FILE             0xC000000Fu
#define STATUS_END_OF_FILE              0xC0000011u
#define STATUS_NO_MEMORY                0xC0000017u
#define STATUS_ACCESS_DENIED            0xC0000022u
#define STATUS_BUFFER_TOO_SMALL         0xC0000023u
#define STATUS_OBJECT_NAME_INVALID      0xC0000033u
#define STATUS_OBJECT_NAME_NOT_FOUND    0xC0000034u
#define STATUS_OBJECT_NAME_COLLISION    0xC0000035u
#define STATUS_OBJECT_PATH_NOT_FOUND    0xC000003Au
#define STATUS_INSUFFICIENT_RESOURCES   0xC000009Au
#define STATUS_NOT_A_DIRECTORY          0xC0000103u
#define STATUS_FILE_IS_A_DIRECTORY      0xC00000BAu
#define STATUS_TOO_MANY_OPENED_FILES    0xC000011Fu
#define STATUS_DIRECTORY_NOT_EMPTY      0xC0000101u
#define STATUS_CANNOT_DELETE            0xC0000121u
#define STATUS_INVALID_DEVICE_REQUEST   0xC0000010u

/* ---- guest structures (offsets) ------------------------------------------------------- */
/* ANSI_STRING { USHORT Length; USHORT MaximumLength; PCHAR Buffer; } */
#define AS_LEN(a)     X_M16(a)
#define AS_MAX(a)     X_M16((a) + 2)
#define AS_BUF(a)     X_M32((a) + 4)
/* OBJECT_ATTRIBUTES { HANDLE RootDirectory; PANSI_STRING ObjectName; ULONG Attributes; } */
#define OA_ROOT(o)    X_M32(o)
#define OA_NAME(o)    X_M32((o) + 4)
#define OA_ATTR(o)    X_M32((o) + 8)
/* IO_STATUS_BLOCK { NTSTATUS Status; ULONG_PTR Information; } */
#define IOSB_STATUS(i) X_M32(i)
#define IOSB_INFO(i)   X_M32((i) + 4)
/* LARGE_INTEGER */
#define LI64(a)       X_M64(a)

/* The ObDosDevicesDirectory pseudo handle XAPI passes as RootDirectory */
#define OB_DOS_DEVICES_DIRECTORY  0xFFFFFFFDu
#define OB_WIN32_NAMED_OBJECTS    0xFFFFFFFEu

/* ---- kernel object model ------------------------------------------------------------- */
typedef enum { XO_NONE = 0, XO_FILE, XO_DIRECTORY, XO_EVENT, XO_MUTANT, XO_SEMAPHORE, XO_THREAD, XO_SYMLINK, XO_TIMER } xk_objtype;

typedef struct xk_obj {
    xk_objtype type;
    int        refs;
    uint32_t   guest;        /* guest-visible object body (KEVENT/KTHREAD...) or 0 */
    union {
        struct { xk_file *f; int is_dir; char *path; int delete_on_close; uint64_t pos; int append; xk_dir *dir; char *pattern; int map_type; } file;   /* map_type: cache header +0x60 (0 sp, 1 mp, 2 ui) once the header was read, else -1 */
        struct { int signaled; int manual; } event;
        struct { struct xk_thread *owner; int count; int abandoned; } mutant;
        struct { int count; int limit; } sem;
        struct xk_thread *thread;
        struct { char *target; } symlink;
        struct { int signaled; uint64_t due; int64_t period; } timer;
    } u;
} xk_obj;

#define XK_MAX_HANDLES 1024
uint32_t xk_handle_create(xk_obj *o);                   /* returns guest handle (never 0) */
xk_obj  *xk_handle_get(uint32_t h);
xk_obj  *xk_handle_get_type(uint32_t h, xk_objtype t);
void     xk_handle_close(uint32_t h);
xk_obj  *xk_obj_new(xk_objtype t);
void     xk_obj_ref(xk_obj *o);
void     xk_obj_deref(xk_obj *o);
xk_obj  *xk_obj_from_guest(uint32_t guest_body);          /* KEVENT/KTHREAD pointer -> object */

/* ---- threads / scheduler --------------------------------------------------------------- */
#define XK_KPCR_SIZE     0x300
#define XK_KTHREAD_SIZE  0x200
/* KPCR / NT_TIB offsets (Xbox kernel) */
#define KPCR_TIB_EXCEPTIONLIST 0x00
#define KPCR_TIB_STACKBASE     0x04
#define KPCR_TIB_STACKLIMIT    0x08
#define KPCR_TIB_SELF          0x18
#define KPCR_SELFPCR           0x1C
#define KPCR_PRCB              0x20
#define KPCR_IRQL              0x24
#define KPCR_PRCBDATA          0x28          /* KPRCB.CurrentThread at +0 */
/* KTHREAD offsets we honour */
#define KTHREAD_TLSDATA        0x28
#define KTHREAD_PRIORITY       0x33
#define KTHREAD_BASEPRIORITY   0x5F
#define KTHREAD_UNIQUE_ID      0x1F0         /* our own: thread id */
#define KTHREAD_EXITSTATUS     0x120         /* ETHREAD.ExitStatus: XAPI GetExitCodeThread reads it directly */
#define KTHREAD_SIGNALSTATE    0x04          /* dispatcher header SignalState (byte): set when the thread exits */

typedef struct xk_thread {
    xctx       ctx;
    xk_fiber  *fiber;
    xk_obj    *obj;
    uint32_t   kthread;      /* guest KTHREAD */
    uint32_t   kpcr;         /* guest KPCR (fs base) */
    uint32_t   stack_base, stack_limit, stack_alloc;
    uint32_t   tls;          /* guest TLS block */
    uint32_t   start_routine, start_context, system_routine;
    void     (*host_entry)(xctx *c, void *arg); void *host_arg;
    int        id;
    int        state;        /* 0 ready, 1 blocked, 2 suspended, 3 exited */
    int        suspend_count;
    uint32_t   exit_status;
    /* wait */
    xk_obj    *wait_objs[16]; int wait_n; int wait_all; uint64_t wait_until; int wait_result;
    int        alertable;
    /* user APCs (I/O completion routines, NtQueueApcThread): routine(arg1, arg2, arg3) */
    struct { uint32_t routine, a1, a2, a3; } apc[16]; int napc;
    struct xk_thread *next;
} xk_thread;

extern xk_thread *xk_cur;                 /* running guest thread */
xk_thread *xk_thread_create(uint32_t stack_size, uint32_t tls_size, uint32_t start_routine, uint32_t start_context,
                            uint32_t system_routine, int suspended);
xk_thread *xk_thread_create_host(void (*entry)(xctx *c, void *arg), void *arg);   /* kernel-internal guest thread with a C body */
void       xk_sleep_us(uint64_t us);      /* block the current guest thread (scheduler-friendly) */
void       xk_thread_exit(uint32_t status);
void       xk_yield(void);                /* let other guest threads run */
void       xk_run_until_idle(void);       /* host harness: drive the scheduler */
void       xk_dump_threads(void);
void       xk_apc_queue(xk_thread *t, uint32_t routine, uint32_t a1, uint32_t a2, uint32_t a3);
int        xk_apc_deliver(xctx *c);       /* run pending APCs of the current thread; returns count */
uint32_t   xk_wait(xk_obj **objs, int n, int wait_all, int alertable, const int64_t *timeout_100ns);  /* NTSTATUS */
void       xk_signal_check(void);         /* wake waiters whose objects became signaled */
int        xk_obj_signaled(xk_obj *o);
void       xk_obj_consume(xk_obj *o, xk_thread *t);

/* ---- guest heap (physical memory) ------------------------------------------------------- */
#define XK_PAGE 4096u
void     xk_mem_init(uint32_t image_end);          /* no-op (kept for the Vita runtime) */
void     xk_mem_setup(uint32_t image_base, uint32_t image_size);   /* builds the page table; call before loading the image */
uint32_t xk_mem_image_arena_offset(void);          /* where the runtime must copy the XBE image inside g_xram */
uint32_t xk_mem_arena_size(void);                  /* bytes to allocate for g_xram */
uint32_t xk_phys_alloc(uint32_t size, uint32_t align, uint32_t lowest, uint32_t highest, int top_down);
int      xk_phys_free(uint32_t pa);
uint32_t xk_mem_alloc(uint32_t size, uint32_t align, uint32_t lowest, uint32_t highest, int top_down);  /* 0 on failure */
int      xk_mem_free(uint32_t addr);
uint32_t xk_mem_size(uint32_t addr);
uint32_t xk_mem_available(void);
uint32_t xk_kalloc(uint32_t size);        /* kernel-side small objects living in guest RAM (KTHREAD, KPCR, ...) */
void     xk_kfree(uint32_t addr);

/* ---- time ----------------------------------------------------------------------------- */
uint64_t xk_time_100ns(void);             /* system time (since 1601) */
uint64_t xk_uptime_100ns(void);           /* interrupt time (since boot) */
uint32_t xk_tick_count(void);             /* ms since boot */

/* ---- files / paths ----------------------------------------------------------------------- */
char    *xk_path_translate(const char *xbox_path, uint32_t root_handle);   /* -> malloc'd host path or NULL */
void     xk_path_add_link(const char *name, const char *target);           /* \??\D: -> \Device\Cdrom0 */
void     xk_path_mount(const char *device, const char *host_dir);          /* \Device\Cdrom0 -> ./haloce */

/* ---- misc --------------------------------------------------------------------------------- */
void     xk_init(uint32_t image_base, uint32_t image_size, uint32_t tls_dir, const char *game_dir, const char *save_dir);
void     xk_thunks_init(void);            /* rewrite kernel thunk slots: data exports -> guest vars, funcs -> magic */
extern int xk_file_in_ui_map;              /* xk_file.c: the map being streamed is a UI map (main menu) */
extern uint32_t xk_var_KeTickCount, xk_var_XboxHardwareInfo, xk_var_LaunchDataPage, xk_var_XboxKrnlVersion, xk_var_HalDiskCachePartitionCount;
const char *xk_gstr(uint32_t a);          /* guest C string (bounded) */
void     xk_ansi_to_c(uint32_t ansi_string, char *out, unsigned cap);
