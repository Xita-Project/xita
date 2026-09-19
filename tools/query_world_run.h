extern unsigned xv_object_world_run_admit(xctx *);
extern int xv_watch_n,xv_trace_funcs;
extern unsigned xk_mem_arena_size(void);
/* World BSP single-child run: publish only node index / consumed budget.
 * Always leave the last visited node to its unchanged emitted block. */
static unsigned nq_run_calls,nq_run_chunks,nq_run_nodes,nq_run_max,nq_run_declines;
static inline unsigned nq_run_fpscr(void) {unsigned v;__asm__ volatile("vmrs %0,fpscr":"=r"(v)::"memory");return v;}
static inline void nq_run_restore(unsigned v) {__asm__ volatile("vmsr fpscr,%0"::"r"(v):"memory");}
static inline int nq_run_span(uint8_t *arena,const uint32_t *pages,unsigned a,unsigned n,unsigned size,const void **out)
{
 if((a&3)||a>0xffffffffu-n+1u||(a&4095u)>4096u-n)return 0;
 unsigned offset=pages[a>>12]+(a&4095u);
 if(offset<pages[a>>12]||size<n||offset>size-n)return 0;
 *out=arena+offset;return 1;
}
static inline unsigned nq_run_word(const void *p) {unsigned v;memcpy(&v,p,4);return v;}
static inline double nq_run_value(unsigned v) {float f;memcpy(&f,&v,4);return (double)f;}
static __attribute__((noinline)) unsigned nq_run3(xctx *c,xctx *guest,uint8_t *arena,const uint32_t *pages,
 unsigned *index,unsigned nodes,unsigned planes,unsigned point,unsigned query,unsigned sp)
{
 if(!xv_object_world_run_admit(guest))return 0;
 nq_run_calls++;
 if(xv_watch_n||xv_trace_funcs||c->preempt<=1||g_xram!=arena||g_xpt!=pages){nq_run_declines++;return 0;}
 unsigned fp=nq_run_fpscr();if(fp&0x9f00u){nq_run_declines++;return 0;}
 unsigned size=xk_mem_arena_size();
 const void *center,*radius,*negative;
 if(!nq_run_span(arena,pages,point,12,size,&center)||
    !nq_run_span(arena,pages,query+0x10u,4,size,&radius)||
    !nq_run_span(arena,pages,sp+0x10u,4,size,&negative)){nq_run_declines++;return 0;}
 unsigned words[5];memcpy(words,center,12);words[3]=nq_run_word(radius);words[4]=nq_run_word(negative);
 for(unsigned i=0;i<5;i++)if((words[i]&0x7f800000u)==0x7f800000u){nq_run_declines++;return 0;}
 double cx=nq_run_value(words[0]),cy=nq_run_value(words[1]),cz=nq_run_value(words[2]);
 double r=nq_run_value(words[3]),nr=nq_run_value(words[4]);
 unsigned current=*index,skipped=0;int budget=c->preempt;
 for(;;) {
  const void *node,*plane;
  unsigned node_address=nodes+12u*current;
  if(!nq_run_span(arena,pages,node_address,12,size,&node))break;
  unsigned plane_id=nq_run_word(node),plane_address=planes+(plane_id<<4);
  if(!nq_run_span(arena,pages,plane_address,16,size,&plane))break;
  unsigned p[4];memcpy(p,plane,16);
  if((p[0]&0x7f800000u)==0x7f800000u||(p[1]&0x7f800000u)==0x7f800000u||
     (p[2]&0x7f800000u)==0x7f800000u||(p[3]&0x7f800000u)==0x7f800000u)break;
  double z=xv_ct_mul(nq_run_value(p[2]),cz),y=xv_ct_mul(nq_run_value(p[1]),cy);
  double sum=xv_ct_add(z,y),x=xv_ct_mul(nq_run_value(p[0]),cx);
  double distance=xv_ct_sub(xv_ct_add(sum,x),nq_run_value(p[3]));
  unsigned side;int consumed;
  if(distance<=nr) {side=0;consumed=2;if(budget<=1)break;}
  else if(distance<r)break;
  else {side=1;consumed=1;}
  /* Never load the child across an actual87F96 callback. */
  unsigned next=nq_run_word((const unsigned char *)node+4u+side*4u);
  if((int)next<0||budget<=consumed)break;
  current=next;budget-=consumed;skipped++;
  if(skipped==32)break;
 }
 if(!skipped){nq_run_restore(fp);nq_run_declines++;return 0;}
 *index=current;c->preempt=budget;nq_run_chunks++;nq_run_nodes+=skipped;
 if(skipped>nq_run_max)nq_run_max=skipped;
 return skipped;
}

/* Caller has joined all object workers. The admission predicate excludes any
 * other writer, and the retained actor mutex serializes admitted workers. */
extern void xk_os_log(const char *fmt,...);
void xv_query_world_run_report(unsigned frames)
{
    xv_object_math_report_check();
    xk_os_log("[query-world-run] %u frames calls %u chunks %u nodes %u max %u declines %u; retained actor guard and original final node\n",
        frames,nq_run_calls,nq_run_chunks,nq_run_nodes,nq_run_max,nq_run_declines);
    nq_run_calls=nq_run_chunks=nq_run_nodes=nq_run_max=nq_run_declines=0;
}
