#!/usr/bin/env python3
"""Exercise real quality code against synthetic guards and locally owned caches."""
from pathlib import Path
import os, shlex, struct, subprocess, sys, tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from halo_map import HaloMap, TAG_BASE

with tempfile.TemporaryDirectory(prefix='xita-quality-test-') as directory:
    d=Path(directory); exe=d/'quality'
    subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-O2','-I'+str(ROOT),'-I'+str(ROOT/'recomp'),
        '-ffunction-sections','-fdata-sections',*shlex.split(os.environ.get('QUALITY_TEST_CFLAGS','')),
        str(ROOT/'tools/tests/quality_host.c'),str(ROOT/'recomp/kernel/xk_quality.c'),
        str(ROOT/'recomp/xv_x86rt.c'),'-Wl,--gc-sections','-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe),'budget','unused'],check=True)
    maps=sys.argv[1:] or ['haloce/maps/bloodgulch.map','haloce/maps/beavercreek.map','haloce/maps/a10.map','haloce/maps/ui.map']
    for path in maps:
        m=HaloMap(str(ROOT/path),str(ROOT/'.mapcache'))
        original=m.data[m.tag_offset:m.tag_offset+m.tag_size]
        source=d/'input';target=d/'output';source.write_bytes(original)
        original_env=dict(os.environ,XV_MATERIAL_QUALITY='2',XV_GLOW_QUALITY='2',XV_PARTICLE_QUALITY='2',XV_DECAL_SECONDS='0')
        subprocess.run([str(exe),str(source),str(target)],env=original_env,check=True)
        assert target.read_bytes()==original,(path,'defaults changed tags')
        for quality in (0,1):
            env=dict(original_env,XV_MATERIAL_QUALITY=str(quality),XV_GLOW_QUALITY=str(quality),XV_PARTICLE_QUALITY=str(quality),XV_DECAL_SECONDS='5')
            subprocess.run([str(exe),str(source),str(target)],env=env,check=True)
            changed=target.read_bytes(); allowed=set(); counts={}
            def permit(offset,n): allowed.update(range(offset,offset+n))
            def u32(b,p):return struct.unpack_from('<I',b,p)[0]
            for t in m.tags:
                p=t.data_addr-TAG_BASE; g=t.groups[0]
                if not 0<=p<len(original) or t.external:continue
                if g=='senv':
                    for offset in (0xc4,0xd8,0x108):permit(p+offset,4)
                    if quality==0:
                        for offset in (0x330,0x290,0x2f4,0x2f8,0x134):permit(p+offset,4)
                    assert changed[p+0x28:p+0x2c]==original[p+0x28:p+0x2c]
                    assert changed[p+0x88:p+0x98]==original[p+0x88:p+0x98]
                    assert changed[p+0x180:p+0x27c]==original[p+0x180:p+0x27c]
                elif g=='soso':
                    permit(p+0xe8,4)
                    if quality==0:
                        for offset in (0x170,0x144,0x154):permit(p+offset,4)
                    assert changed[p:p+0xdc]==original[p:p+0xdc]
                elif g=='lens':
                    count=u32(original,p+0xc4);addr=u32(original,p+0xc8)-TAG_BASE
                    permit(p+0xc4,4);permit(addr,count*128)
                    new_count=u32(changed,p+0xc4)
                    assert new_count<=count
                    if quality==0:assert new_count==0
                    else:
                        expected=b''.join(original[addr+j*128:addr+(j+1)*128] for j in range(count)
                            if abs(struct.unpack_from('<f',original,addr+j*128+0x1c)[0])<=.0001)
                        assert changed[addr:addr+new_count*128]==expected
                elif g=='part':
                    permitted=any(n in t.name.lower() for n in ('smoke','spark','dust','steam')) and not any(n in t.name.lower() for n in ('plasma','projectile','bullet','tracer','energy'))
                    permitted &= all(u32(original,p+x)==0xffffffff for x in (0x30,0x54,0x64))
                    if permitted:permit(p+0x38,16)
                    else:assert changed[p:p+356]==original[p:p+356]
                elif g=='deca':
                    flags,kind=struct.unpack_from('<HH',original,p)
                    if kind!=3 and not flags&16:permit(p+0x78,16)
                if changed[p:p+4]!=original[p:p+4]:counts[g]=counts.get(g,0)+1
            differences=[i for i,(a,b) in enumerate(zip(original,changed)) if a!=b]
            assert differences and all(i in allowed for i in differences),(path,quality,'unexpected tag mutation')
            # Reject a truncated index before applying any quality changes.
            corrupt=bytearray(original);struct.pack_into('<I',corrupt,0,len(original)+TAG_BASE-16)
            source.write_bytes(corrupt)
            subprocess.run([str(exe),str(source),str(target)],env=env,check=True)
            assert target.read_bytes()==corrupt
            source.write_bytes(original)
            print(m.name,'quality',quality,':',len(differences),'changed bytes confined to permitted visual fields')
print('Quality defaults, map guards, material/alpha preservation, flare selection, cosmetics and decal budget passed')
