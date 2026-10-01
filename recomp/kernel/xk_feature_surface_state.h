/* Experimental 85020 boundary. Not hooked into gameplay.
 * Caller proves finite inputs, immutable constants, disjoint mapped output,
 * arguments and scratch stack for this non-yielding call. */
#ifndef XK_FEATURE_SURFACE_STATE_H
#define XK_FEATURE_SURFACE_STATE_H
#include "xk_feature_state.h"
static inline void xk_feature_surface_status(xctx *c,unsigned mask){
    X_R16(0)=c->fsw;X_FLAGS(XK_LOGIC,0,0,X_R8H(0)&mask,8);
}
static inline void xk_feature_surface_zero(xctx *c){
    uint32_t old=c->r[0];c->r[0]=0;X_FLAGS(XK_LOGIC,old,old,0,32);
}
static inline int xk_feature_surface_emit_state(xctx *c,uint8_t *output){
    uint32_t sp=c->r[4],base=X_M32(sp+44),point_address=X_M32(sp+8),plane_address=X_M32(sp+12);
    unsigned n=xk_fb_count(output+4),count=X_M16(sp+4);
    if(n>256||count>8||c->fcw!=0x37f||c->fsp>7||c->preempt<(int)(2*count+1))return 0;
    float points[8][3],plane[4],height,radius;
    x_guest_read(points,point_address,count*12);x_guest_read(plane,plane_address,16);
    x_guest_read(&height,sp+16,4);x_guest_read(&radius,sp+20,4);
    if(!isfinite(height)||!isfinite(radius))return 0;
    for(unsigned i=0;i<4;i++)if(!isfinite(plane[i]))return 0;
    for(unsigned i=0;i<count;i++)for(unsigned j=0;j<3;j++)if(!isfinite(points[i][j]))return 0;
    xk_feature_metadata m={X_M32(sp+24),X_M32(sp+28),X_M8(sp+32),X_M8(sp+36),X_M16(sp+40)};
    if(!xk_feature_build_surface(output,&m,points,count,plane,height,radius))return 0;
    c->r[2]=base;c->r[0]=n;X_FLAGS(XK_SUB,n,256,(uint16_t)(n-256),16);
    X_PUSH32(c->r[5]);c->r[5]=point_address;
    if(n==256){c->r[5]=X_POP32();c->r[4]+=48;return 1;}
    c->r[1]=x_imul32(c,n,104)+base+0x4408;xk_feature_inc_state(c,0);
    c->r[2]=m.object;c->r[0]=m.surface;X_R8L(2)=m.flags_a;X_R8L(0)=m.flags_b;X_R16(2)=m.material;
    c->r[2]=plane_address;X_PUSH32(c->r[6]);c->r[0]=c->r[1]+12;
    X_PUSH32(c->r[7]);c->r[6]=c->r[0];memcpy(&c->r[7],plane+2,4);memcpy(&c->r[2],&radius,4);
    x87_push(c,fabs((double)plane[0]));x87_push(c,fabs((double)plane[1]));x87_push(c,fabs((double)plane[2]));
    x87_compare(c,X_ST(0),X_ST(1),0);xk_feature_surface_status(c,1);
    int choose_y=0;
    if(!XF_Z(c)){x87_pop(c);choose_y=1;}
    else{
        x87_compare(c,X_ST(0),X_ST(2),0);x87_pop(c);xk_feature_surface_status(c,1);
        if(!XF_Z(c))choose_y=1;
        else{x87_pop(c);c->r[0]=2;x87_pop(c);}
    }
    if(choose_y){
        x87_compare(c,X_ST(0),X_ST(1),0);x87_pop(c);X_R16(0)=c->fsw;x87_pop(c);
        X_FLAGS(XK_LOGIC,0,0,X_R8H(0)&1,8);
        if(XF_Z(c))c->r[0]=1;else xk_feature_surface_zero(c);
    }
    unsigned axis=c->r[0];
    x87_push(c,(double)plane[axis]);x87_compare(c,X_ST(0),0,0);x87_pop(c);xk_feature_surface_status(c,0x41);
    if(XF_Z(c))c->r[0]=1;else xk_feature_surface_zero(c);
    unsigned positive=c->r[0],u=(axis+1)%3,v=(axis+2)%3;
    if(!positive){unsigned swap=u;u=v;v=swap;}
    c->r[0]=count;c->r[7]=0;X_FLAGS(XK_LOGIC,0,0,count,32);
    if(count){
        c->r[0]=0;X_PUSH32(c->r[3]);
        /* Output was already written. With no yield, only the final iteration's
         * register/retired x87 state survives; retain every budget decrement. */
        { unsigned i=count-1;
            c->r[7]=i;
            c->r[2]=x_shl32(c,positive+axis*2,2);c->r[3]=v;
            c->r[6]=point_address+i*12;memcpy(&c->r[2],&points[i][u],4);xk_feature_inc_state(c,7);
            x87_push(c,(double)points[i][v]);x87_pop(c);
            c->r[2]=count;c->r[0]=i+1;xk_feature_cmp_state(c,c->r[0],c->r[2]);
            c->preempt-=(int)(count-1);
        }
        c->r[3]=X_POP32();
    }
    x87_push(c,(double)height);x87_compare(c,X_ST(0),0,0);x87_pop(c);xk_feature_surface_status(c,0x41);
    if(XF_Z(c)){
        c->r[0]=plane_address;x87_push(c,(double)plane[2]);x87_compare(c,X_ST(0),0,0);x87_pop(c);xk_feature_surface_status(c,5);
        if(!XF_P(c)){
            x87_push(c,(double)height);X_R16(0)=axis;X_FLAGS(XK_SUB,axis,2,(uint16_t)(axis-2),16);
            X_ST(0)*=(double)plane[2];X_ST(0)=(double)plane[3]-X_ST(0);x87_pop(c);
            if(axis!=2){
                c->r[2]=positive+axis*2;c->r[0]=0;X_FLAGS(XK_SUB,v,2,(uint16_t)(v-2),16);
                X_R8L(0)=XF_Z(c)?1:0;c->r[2]=0;c->r[6]=c->r[0];c->r[0]=count;X_FLAGS(XK_LOGIC,0,0,count,32);
                if(count){
                    c->r[0]=0;
                    { unsigned i=count-1;
                        c->r[2]=i;
                        if(i)xk_feature_cmp_state(c,i,count);
                        c->r[0]=c->r[1]+(c->r[6]+i*2+10)*4;
                        x87_push(c,(double)points[i][2]);X_ST(0)-=(double)height;xk_feature_inc_state(c,2);x87_pop(c);
                        c->r[7]=count;c->r[0]=i+1;xk_feature_cmp_state(c,c->r[0],c->r[7]);
                        c->preempt-=(int)(count-1);
                    }
                }
            }
        }
    }
    c->r[7]=X_POP32();c->r[6]=X_POP32();c->r[5]=X_POP32();c->r[4]+=48;return 1;
}
#endif
