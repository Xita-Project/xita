/* Halo 3925 0x53FA0: sort visible triangle numbers, then expand to WORD indices.
 * This has no callbacks other than the signed integer ordering at 0x53F60.
 * Workers only access private scratch; guest memory is read/written by the caller.
 */
#include <stdlib.h>
#include "xk.h"
#include "../../runtime/xv_geometry_sort.h"

void xv_geometry_sort_parallel(int32_t *, unsigned) __attribute__((weak));
static int32_t *scratch;

/* Sphere BSP traversal, 0x87ECC..0x87EE5. Only the caller's register state
 * changes. Keep both live and popped x87 slots, double operation order and
 * the original paged loads; no shared scratch, cache or counter is needed. */
int xv_bsp_sphere_plane_distance(xctx *c)
{
    /* NaN operand priority can change when the ARM compiler allocates native
     * temporaries differently. Keep the original block for exceptional input
     * and unmasked FP traps, before changing any guest or native FP state. */
#if defined(__arm__)
    uint32_t fpscr;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr) :: "memory");
    if(fpscr&0x00009f00u)return 0;
#endif
    uint32_t plane=c->r[0]+c->r[5], point=c->r[1];
    uint32_t words[7];
    x_guest_read(words,plane,16);x_guest_read(words+4,point,12);
    for(unsigned i=0;i<7;i++)if((words[i]&0x7f800000u)==0x7f800000u)return 0;
    float values[7];memcpy(values,words,sizeof values);
    double z=(double)values[2]*(double)values[6];
    c->r[0]=plane;
    double y=(double)values[1]*(double)values[5];
    double sum=z+y;
    double x=(double)values[0]*(double)values[4];
    double distance=(sum+x)-(double)values[3];
    c->st[(c->fsp-2u)&7u]=x;
    c->fsp=(c->fsp-1u)&7u;
    X_ST(0)=distance;
    return 1;
}

/* 3925 BSP segment traversal, 0x88BA5..0x88BF9. Keep double intermediates,
 * float spill points, operation order and the x87 stack exactly as lifted.
 * This replaces stack-machine bookkeeping with native scalar arithmetic. */
void xv_bsp_plane_interval(xctx *c)
{
    uint32_t point=c->r[1];
    c->r[0]=x_shl32(c,c->r[0],4)+c->r[2];
    uint32_t plane=c->r[0];
    double origin=(x87_load_f32(c,point+8)*x87_load_f32(c,plane+8)+
                   x87_load_f32(c,point+4)*x87_load_f32(c,plane+4))+
                   x87_load_f32(c,plane)*x87_load_f32(c,point);
    c->r[1]=X_M32(c->r[5]+0x14);
    origin-=x87_load_f32(c,plane+12);
    x87_store_f32(c,c->r[4]+0x14,origin);
    uint32_t direction=c->r[1];
    double delta=(x87_load_f32(c,direction+8)*x87_load_f32(c,plane+8)+
                  x87_load_f32(c,direction+4)*x87_load_f32(c,plane+4))+
                  x87_load_f32(c,plane)*x87_load_f32(c,direction);
    x87_store_f32(c,c->r[4]+0x10,delta);
    double first=delta*x87_load_f32(c,c->r[4]+0x20)+x87_load_f32(c,c->r[4]+0x14);
    double last=x87_load_f32(c,c->r[4]+0x10)*x87_load_f32(c,c->r[4]+0x24)+x87_load_f32(c,c->r[4]+0x14);
    x87_store_f32(c,c->r[4]+0x18,last);
    c->st[(c->fsp-2u)&7u]=last;
    c->fsp=(c->fsp-1u)&7u;
    X_ST(0)=first;
}

void xv_hle_HaloBuildVisibleIndices(xctx *c)
{
    uint32_t output = c->r[0], input = c->r[1];
    uint32_t world = X_M32(0x39BE58);
    int count = (int16_t)X_ARG(0);
    if (!scratch && count > 0) scratch = malloc(32768u * sizeof *scratch);
    if (scratch && count > 0) {
        x_guest_read(scratch, input, (unsigned)count * sizeof *scratch);
        if (xv_geometry_sort_parallel) xv_geometry_sort_parallel(scratch, (unsigned)count);
        else xv_sort_triangles(scratch, (unsigned)count);
        x_guest_write(input, scratch, (unsigned)count * sizeof *scratch);
    } else {
        /* Preserve the original path for allocation failure and nonpositive counts. */
        c->r[0] = (uint32_t)count;
        X_PUSH32(0x53F60); X_PUSH32(0x53FC0);
        xv_call(c, 0x11B5A0);
    }
    for (int i = 0; i < count; i++) {
        uint32_t triangle = X_M32(input + (unsigned)i * 4);
        c->r[2] = X_M32(world + 0xFC);
        c->r[0] = c->r[2] + triangle * 6u;
        X_R16(2) = X_M16(c->r[0]); X_M16(output) = X_R16(2);
        X_R16(2) = X_M16(c->r[0] + 2); X_M16(output + 2) = X_R16(2);
        X_R16(0) = X_M16(c->r[0] + 4); X_M16(output + 4) = X_R16(0);
        output += 6;
    }
    if (count > 0) {
        c->r[1] = 0;
        X_FLAGS(XK_SUB, 1, 1, 0, 32);
        c->f_cf_override = 1; c->f_cf = output < 6;
    } else {
        X_FLAGS(XK_LOGIC, 0, 0, (uint16_t)count, 16);
    }
    X_RET(1);
}
