#include "../../runtime/xv_clip_diagnostic.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
 float m[4][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
 float v[3][4]={{-2,0,0,0},{-3,.5f,0,0},{-4,-.5f,0,0}};
 uint16_t ix[3]={0,1,2};
 #define RUN(b,s,o,ib,n) xv_clip_diagnostic(v,b,s,o,ix,ib,n,m)
 assert(RUN(sizeof v,16,0,sizeof ix,3)==1);
 assert(!RUN(20,16,0,sizeof ix,3));
 assert(!RUN(sizeof v,16,8,sizeof ix,3));
 assert(!RUN(sizeof v,16,0,4,3));
 assert(!RUN(sizeof v,16,0,sizeof ix,0));
 ix[2]=65535;assert(!RUN(sizeof v,16,0,sizeof ix,3));ix[2]=2;
 v[2][0]=0;assert(!RUN(sizeof v,16,0,sizeof ix,3));
 v[2][0]=NAN;assert(!RUN(sizeof v,16,0,sizeof ix,3));
 v[2][0]=-1;assert(!RUN(sizeof v,16,0,sizeof ix,3));
 v[2][0]=-4;m[0][0]=INFINITY;assert(!RUN(sizeof v,16,0,sizeof ix,3));
 m[0][0]=1;m[3][3]=4;assert(!RUN(sizeof v,16,0,sizeof ix,3));
 m[3][3]=1;
 for(unsigned plane=0;plane<4;plane++) {
   for(unsigned j=0;j<3;j++) {v[j][0]=v[j][1]=0;v[j][plane/2]=(plane%2)?2:-2;}
   assert(RUN(sizeof v,16,0,sizeof ix,3)==(1u<<plane));
 }
 /* All vertices outside, but no shared plane: may cross the viewport. */
 v[0][0]=-2;v[0][1]=0;v[1][0]=2;v[1][1]=0;v[2][0]=0;v[2][1]=2;
 assert(!RUN(sizeof v,16,0,sizeof ix,3));
 unsigned char packed[37],unaligned_ix[7];
 for(unsigned j=0;j<3;j++){v[j][0]=-2;v[j][1]=0;memcpy(packed+1+j*12,v[j],12);}
 memcpy(unaligned_ix+1,ix,6);
 assert(xv_clip_diagnostic(packed+1,36,12,0,unaligned_ix+1,6,3,m)==1);
 puts("clip diagnostic: bounds, invalid values, mixed planes and boundary guards passed");
}
