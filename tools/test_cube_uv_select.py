"""Emit a portable C++ differential harness for the actual generated Cg helper.

Run: python3 tools/test_cube_uv_select.py /tmp/cube.cpp
     c++ -O2 -fsanitize=address,undefined /tmp/cube.cpp -o /tmp/cube && /tmp/cube
Cross-compile the same file for the Pi. This checks arithmetic/face selection,
not Cg compiler lowering, GPU texture derivatives, or Vita performance.
"""
import argparse
import sys
import pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'recompiler'))
from pixelshader_recomp_gen import cube_uv_helper
pre=r'''
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
struct float2 { float x,y; float2(float a,float b):x(a),y(b){} };
struct float3 { float x,y,z; };
static float3 abs(float3 a) { return {std::fabs(a.x),std::fabs(a.y),std::fabs(a.z)}; }
static float max(float a,float b) { return std::fmax(a,b); }
static float2 operator/(float2 a,float b) { return {a.x/b,a.y/b}; }
'''
post=r'''
static uint32_t bits(float f) { uint32_t u; memcpy(&u,&f,4);return u; }
static float val(uint32_t u) { float f;memcpy(&f,&u,4);return f; }
static bool same(float a,float b) { return bits(a)==bits(b) || (std::isnan(a)&&std::isnan(b)); }
static unsigned tested;
static void check(float3 d) {
 float2 a=reference(d),b=candidate(d); ++tested;
 if(!same(a.x,b.x)||!same(a.y,b.y)) { printf("mismatch %08x %08x %08x\n",bits(d.x),bits(d.y),bits(d.z));exit(1); }
}
int main() {
 const uint32_t v[]={0,0x80000000,1,0x80000001,0x00800000,0x80800000,0x3f800000,0xbf800000,0x40000000,0xc0000000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc00000};
 for(auto x:v)for(auto y:v)for(auto z:v)check({val(x),val(y),val(z)});
 uint32_t seed=0x8b76a44;
 auto next=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return val(seed);};
 for(int i=0;i<1000000;++i) { float x=next(),y=next(),z=next();check({x,y,z}); if(i%8==0) {check({x,x,z});check({x,y,x});check({x,y,y});} }
 printf("PASS %u coordinate triples; exact bits except NaN payloads\n",tested);
}
'''
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=pathlib.Path)
    args = parser.parse_args()
    args.output.write_text(pre+cube_uv_helper().replace('xv_cube_uv','reference')+cube_uv_helper(True).replace('xv_cube_uv','candidate')+post)


if __name__ == "__main__":
    main()
