#include "kernel/xk_marker_snapshot.h"
#include <stddef.h>
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
void test_boot(void){} void xk_os_log(const char *f,...){(void)f;}
char *getenv(const char *n){(void)n;return 0;} int atoi(const char *n){(void)n;return 0;}
/* Fixture only: ownership must be supplied by the eventual production bridge. */
void snapshot_marker(xctx*c){
 xv_marker_input in; xv_marker_result out;
 in.matrix_base=X_M32(c->r[4]+0x24);
 uint32_t matrix=in.matrix_base+(uint32_t)(int32_t)(int16_t)c->r[0]*52u;
 memcpy(in.quaternion,X_G(c->r[7]+16),16);memcpy(in.translation,X_G(c->r[7]+4),12);
 memcpy(in.matrix,X_G(matrix),52);
 in.zero=X_M32(0x1f0a68);in.two=X_M32(0x1f0b04);in.one=X_M32(0x1f0a78);
 xv_marker_snapshot(c,&in,&out);
 memcpy(X_G(c->r[6]),&out.node,2);memcpy(X_G(c->r[6]+4),out.local,52);
 memcpy(X_G(c->r[6]+56),out.world,52);memcpy(X_G(c->r[4]-32),out.spills,32);
}
