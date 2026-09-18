/* Run with ASan/UBSan: exact-size input allocations and maximum-size workspace. */
#include "portal_polygon.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    unsigned calls = 0;
    const int sizes[] = {1,2,3,4,8,16,32,64,128,255,256};
    for (unsigned i=0; i<sizeof(sizes)/sizeof(*sizes); ++i) {
        int n=sizes[i];
        for (unsigned j=0; j<sizeof(sizes)/sizeof(*sizes); ++j) {
            int edges=sizes[j];
            xp_point *input=malloc(n*sizeof(*input));
            xp_point *boundary=malloc(edges*sizeof(*boundary));
            xp_point *output=malloc(256*sizeof(*output));
            xp_point *scratch=malloc(512*sizeof(*scratch));
            assert(input && boundary && output && scratch);
            for (int k=0;k<n;++k) {
                double angle=6.283185307179586*k/n;
                input[k]=(xp_point){cos(angle)*2.5,sin(angle)*2.5};
            }
            for (int k=0;k<edges;++k) {
                double angle=6.283185307179586*k/edges;
                boundary[k]=(xp_point){cos(angle),sin(angle)};
            }
            for (int capacity=1;capacity<=256;capacity+=17) {
                xp_work work;
                int result=xp_portal_polygon(input,n,boundary,edges,capacity,
                                             .0001f,output,scratch,&work);
                assert(result>=-1 && result<=256);
                assert(work.backedges <= (unsigned)edges*257+1);
                ++calls;
            }
            free(input);free(boundary);free(output);free(scratch);
        }
    }
    printf("PASS %u bounded native polygon calls under memory sanitizers\n",calls);
    return 0;
}
