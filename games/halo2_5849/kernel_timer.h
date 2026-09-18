#pragma once
#include "kernel/xk.h"
/* H2-only cooperative timer queue. Unknown/unmapped objects and DPC blocking
 * stop explicitly; this is not a hardware interrupt controller. */
void h2_timer_poll(xctx *worker_context);
void h2_timer_fault(xctx *, const char *, uint32_t, uint32_t);
void __wrap_xk_KeInitializeTimerEx(xctx *);
void __wrap_xk_KeInitializeDpc(xctx *);
void __wrap_xk_KeSetTimer(xctx *);
void __wrap_xk_KeSetTimerEx(xctx *);
void __wrap_xk_KeCancelTimer(xctx *);
void __wrap_xk_KeInsertQueueDpc(xctx *);
void __wrap_xk_KeRemoveQueueDpc(xctx *);
void __wrap_xk_KfRaiseIrql(xctx *);
void __wrap_xk_KfLowerIrql(xctx *);
void __wrap_xk_KeRaiseIrqlToDpcLevel(xctx *);
