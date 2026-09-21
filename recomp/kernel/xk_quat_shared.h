/* Optimistic shared-output quaternion calculation. Caller has checked aliasing
 * and captured inputs under the object guard. No guest pointers are consumed
 * while it is released. Publication linearizes under the reacquired guard. */
#ifndef XITA_QUAT_SHARED_H
#define XITA_QUAT_SHARED_H
extern int xv_object_quat_shared_admit(xctx *,int);
static unsigned quat_shared_attempts,quat_shared_commits,quat_shared_retries;
void xv_quat_shared_report(unsigned frames)
{
    XK_LOG("[quat-shared] %u frames attempts %u committed %u retries %u; guarded validation, not wait time\n",
        frames,quat_shared_attempts,quat_shared_commits,quat_shared_retries);
    quat_shared_attempts=quat_shared_commits=quat_shared_retries=0;
}
static uint32_t quat_shared_fp_get(void)
{
#if defined(__arm__)
    uint32_t value;__asm__ volatile("vmrs %0, fpscr":"=r"(value)::"memory");return value;
#elif defined(__x86_64__)
    return _mm_getcsr();
#else
    return 0;
#endif
}
static void quat_shared_fp_set(uint32_t value)
{
#if defined(__arm__)
    __asm__ volatile("vmsr fpscr, %0"::"r"(value):"memory");
#elif defined(__x86_64__)
    _mm_setcsr(value);
#else
    (void)value;
#endif
}
/* 0: not attempted, 1: committed, 2: input/mapping changed; guarded retry. */
static int xv_quat_shared_try(xctx *c,int *guard,const float input[4],
    const float *ip,float *output,float *scratch,const void *constants,
    uint32_t zero,uint32_t two,uint32_t one)
{
    /* Already-private and owner calls cannot use this transaction. Reject
     * before FP inspection or another native-thread identity query. */
    if(*guard<2||!xv_object_quat_shared_admit(c,*guard)||!point_fp_supported())return 0;
    quat_shared_attempts++;
    uint32_t before_fp=quat_shared_fp_get();
    xctx trial=*c;
    float result[13],spills[6];
    int original_guard=*guard;
    xv_object_math_unlock(guard);*guard=0;
#ifdef XV_QUAT_SHARED_TEST
    extern void xv_quat_shared_test_released(xctx *);
    xv_quat_shared_test_released(c);
#endif
    xv_quaternion_snapshot(&trial,input,result,spills,zero,two,one);
    uint32_t result_fp=quat_shared_fp_get();
    *guard=xv_object_math_lock();
    if(*guard!=original_guard)abort();
    /* Re-resolve every guest span before dereferencing it after the unlock.
     * Exact input equality permits a commit at this point even after ABA.
     * No result bytes reached shared output before validation succeeds. */
    if(math_span(c->r[1],16)!=ip||math_span(c->r[2],52)!=output||
       math_span(c->r[4]-24u,24)!=scratch||math_span(0x1F0A68u,0xA0u)!=constants||
       memcmp(input,ip,16)||X_M32(0x1F0A68u)!=zero||
       X_M32(0x1F0B04u)!=two||X_M32(0x1F0A78u)!=one) {
        quat_shared_retries++;quat_shared_fp_set(before_fp);return 2;
    }
    memcpy(output,result,sizeof result);memcpy(scratch,spills,sizeof spills);
    *c=trial;quat_shared_commits++;quat_shared_fp_set(result_fp);
    return 1;
}
#endif
