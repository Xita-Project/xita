/* XV_QSERIAL storage: the scene helper's private query serials (see xk_qserial.h). */
#include <stdint.h>
#include <stdlib.h>
#include "xk.h"
uint8_t xv_qserial_private[0x820] __attribute__((aligned(16))) = { [0x81F] = 0x80 };   /* helper object serial starts at 0x80000000 */
int xv_qserial_on = 1;
/* Called once when the scene thread configures (env is loaded by then). */
void xv_qserial_configure(void)
{
    const char *e = getenv("XV_QSERIAL"); xv_qserial_on = e ? atoi(e) != 0 : 1;
    XK_LOG("[qserial] %s\n", xv_qserial_on ? "on: the scene helper uses private object-query serials (2D2FA9/2D2FAC/2D2FB0 stamps/2FC684)" : "off");
}
