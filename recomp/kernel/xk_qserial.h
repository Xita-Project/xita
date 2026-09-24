/* XV_QSERIAL: the scene helper keeps private copies of Halo's object-query serials.
 *
 * Halo's spatial object queries (f_001721B0, f_00052240, f_00051D20, ...) mark visited clusters and objects with a
 * serial: the cluster serial at [2D2FAC] with its per-cluster stamp array at [2D2FB0], the "query running" byte at
 * [2D2FA9], and the object serial at [2FC684] whose value is written to each visited object's +8. A walk increments
 * the serial once and then RE-READS the global for every comparison. On the Xbox every query ran on one thread; under
 * the overlap the scene (1721B0, 5A7B0, 8AA20, 8AA50 on the helper) and the tick (52240, 56670, 1721B0, 153680, ...
 * on the owner) query at the same time, so a tick query started mid-walk makes the scene skip clusters/objects it
 * never visited (Vita perf177: one object's 17-23 draws missing for a frame 0.34 times a second; 0.01/s with the
 * overlap off) and the reverse makes tick queries miss objects.
 *
 * Accesses in the patched functions go through X_QS8 / X_QS32: on the scene helper they reach a private copy (the
 * helper's own serials, stamps and flag), elsewhere the live globals. The helper's object serial starts at 0x80000000,
 * a range the tick's serial never reaches, so stamps either thread leaves on objects can never equal the other's
 * serial (the walks compare with ==). Env XV_QSERIAL 1 on (default) / 0 off; hooks from tools/patch_qserial.py. */
#ifndef XK_QSERIAL_H
#define XK_QSERIAL_H
#include <stdint.h>
extern uint8_t xv_qserial_private[];   /* [0, 0x81C): guest 0x2D2FA8..0x2D37C4 (flag, cluster serial, stamps); [0x81C, 0x820): guest 0x2FC684 */
extern int xv_qserial_on;
extern int xv_scene_thread_on_helper(void);
#define XV_QS_PTR(a) ({ uint32_t qa_ = (uint32_t)(a); \
    (xv_qserial_on && xv_scene_thread_on_helper()) \
        ? (void *)(xv_qserial_private + (qa_ - 0x2FC684u < 4u ? 0x81Cu + (qa_ - 0x2FC684u) : qa_ - 0x2D2FA8u)) \
        : (void *)X_G(qa_); })
#define X_QS8(a)  (*(uint8_t *)XV_QS_PTR(a))
#define X_QS32(a) (*(xu32_u *)XV_QS_PTR(a))
#endif
