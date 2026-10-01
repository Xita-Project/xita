/* Shared synthetic high-index geometry, no game assets. */
static void scope_fixture(xctx *c){
 memset(g_xram,0,0x300000);reference_init_constants();
 for(unsigned i=0;i<0x300;i++)g_xpt[i]=4096*i;
 const unsigned geom=0x20000,vertices=0x80000,edges=0x100000,surfaces=0x180000,q=0x30000,sp=0x10000;
 put32(geom+0x54,16384);put32(geom+0x58,vertices);
 put32(geom+0x48,16384);put32(geom+0x4c,edges);
 put32(geom+0x3c,16384);put32(geom+0x40,surfaces);
 for(unsigned i=0;i<2;i++){
  unsigned v=vertices+(16000+i)*16,e=edges+(16000+i)*24,s=surfaces+(16000+i)*12;
  put32(v,bits(1+i));put32(v+4,bits(2));put32(v+8,bits(3));put32(v+12,16000+i);
  put32(e+16,16000+i);g_xram[s+8]=3;g_xram[s+9]=5;g_xram[s+10]=7;
 }
 put32(q+0x808,8);for(unsigned i=0;i<8;i++)put32(q+0x80c+i*4,16000+i%2);
 put32(sp,0x12345678);put32(sp+4,geom);put32(sp+12,bits(2));put32(sp+16,bits(.5));put32(sp+20,0x40000);
 memset(c,0,sizeof *c);c->r[0]=UINT32_MAX;c->r[4]=sp;c->r[7]=q;c->fcw=0x37f;c->preempt=10000;
}
