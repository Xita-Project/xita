"""Optional full-entry vertex pass; all other collision code stays original."""
import hashlib
SPANS={0x86F50:(884,'dac5ac8da738ab412fd265fb824a2a6abe9cde4f6fef19fb1a873ba5aba73d34')}
def matches(image):
    return all(hashlib.sha256(image.bytes_at(a,n) or b'').hexdigest()==h for a,(n,h) in SPANS.items())
def hook(body):
    start='L_00086F9B:\n';end='L_0008709A:\n'
    assert body.count(start)==1 and body.count(end)==1,'collision vertex boundary drift'
    region=body[body.index(start):body.index(end)]
    assert region.count('X_PREEMPT()')==4 and 'f_000' not in region,'collision vertex region drift'
    block='''#ifdef XV_NATIVE_COLLISION_VERTICES
    { unsigned cv_scope_ __attribute__((cleanup(xv_collision_vertices_end))) = xv_collision_vertices_begin();
      if (cv_scope_) {
        unsigned cv_resume_ = xv_collision_vertices(c, xram_, xpt_);
        if (cv_resume_ == 1) goto L_00086FBA;
        if (cv_resume_ == 2) goto L_00087050;
        goto L_0008709A;
      } }
#endif
'''
    return '#ifdef XV_NATIVE_COLLISION_VERTICES\n#include "kernel/xk_collision_vertices.h"\n#endif\n'+body.replace(start,start+block,1)
