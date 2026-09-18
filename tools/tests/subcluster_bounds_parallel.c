/* Pure copied-input experiment; not the Vita production queue or admission. */
#include "../experiments/subcluster_bounds.h"
#include <assert.h>
#include <fenv.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { COUNT=1027 };
typedef struct { const xs_frustum *f; const xs_box *b; xs_bounds_result *out; unsigned n; int round; } Job;
static void *run(void *p)
{
    Job *j=p;assert(!fesetround(j->round));
    xs_bounds_batch(j->f,j->b,j->n,j->out);return NULL;
}
int main(void)
{
    xs_box *b=malloc(COUNT*sizeof(*b)),*copy=malloc(COUNT*sizeof(*b));
    xs_bounds_result *serial=malloc(COUNT*sizeof(*serial)),*parallel=malloc(COUNT*sizeof(*parallel));
    assert(b&&copy&&serial&&parallel);
    xs_frustum f={.plane={{1,.25,-.5,2},{-1,.5,.25,2},{.25,1,.5,2},{.5,-1,-.25,2}},
                  .enclosing={.axis={{-8,8},{-8,8},{-8,8}}}},saved=f;
    for(unsigned i=0;i<COUNT;++i)for(unsigned a=0;a<3;++a) {
        b[i].axis[a][0]=((int)((i*(a+3))%67)-33)*.25f;
        b[i].axis[a][1]=b[i].axis[a][0]+(i%9)*.125f;
    }
    memcpy(copy,b,COUNT*sizeof(*b));
    int modes[]={FE_TONEAREST,FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO};
    for(unsigned m=0;m<4;++m) {
        assert(!fesetround(modes[m]));xs_bounds_batch(&f,b,COUNT,serial);
        for(unsigned repeat=0;repeat<32;++repeat) {
            unsigned cut=repeat ? 1+(repeat*31)%COUNT : 0;
            memset(parallel,0xa5,COUNT*sizeof(*parallel));
            Job j[2]={{&f,b,parallel,cut,modes[m]},
                      {&f,b+cut,parallel+cut,COUNT-cut,modes[m]}};
            pthread_t t[2];
            for(unsigned i=0;i<2;++i)assert(!pthread_create(t+i,NULL,run,j+i));
            for(unsigned i=0;i<2;++i)assert(!pthread_join(t[i],NULL));
            assert(!memcmp(serial,parallel,COUNT*sizeof(*serial)));
            assert(!memcmp(b,copy,COUNT*sizeof(*b)));assert(!memcmp(&f,&saved,sizeof(f)));
        }
    }
    free(b);free(copy);free(serial);free(parallel);
    puts("PASS 128 two-worker partitions, 1027 boxes each, four rounding modes; inputs unchanged");
}
