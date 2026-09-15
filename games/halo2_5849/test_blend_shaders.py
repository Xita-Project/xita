"""Synthetic packet, producer admission and screen-blend math checks."""
import copy
from pathlib import Path
import struct
import tempfile
import unittest
import numpy as np
import prepare_blend_shaders as h
from check_blend_probe import equations


def definition():
    s=[0]*2048;s[0x1E60//4]=0x11001;s[0x1E70//4]=1
    s[0x288//4]=12;s[0x1E40//4]=0xC00
    return h.pixel_definition(s)


def ring():
    words=[]
    def packet(m,values):words.extend([(len(values)<<18)|m,*values])
    packet(0x17FC,[7])
    expected=[]
    for v in range(4):
        regs=[0]*28
        for reg,method in ((5,0x1A50),(1,0x1A10),(0,0x1518)):
            values=[v*100+reg*4+i for i in range(4)];regs[reg*4:reg*4+4]=values;packet(method,values)
        expected.extend(regs)
    packet(0x17FC,[0]);base=0x1000;data=struct.pack(f'<{len(words)}I',*words)
    return struct.pack('<4I',base,len(data),base+len(data),0)+data,struct.pack('<112I',*expected)


class BlendPreparation(unittest.TestCase):
    def test_only_bound_texture_and_single_stage(self):
        d=definition();text=h.fragment_source(d)
        self.assertEqual(text.count('tex2Dproj('),1);self.assertIn('float2(160.0,120.0)',text)
        for key,value in (('stage_count',2),('same_c0',True),('mux_msb',True)):
            bad=copy.deepcopy(d);bad[key]=value
            with self.assertRaises(ValueError):h.fragment_source(bad)
        for unit in range(4):
            bad=copy.deepcopy(d);bad['textures'][unit]['mode']='NONE'if unit==0 else'PROJECT2D'
            with self.assertRaises(ValueError):h.fragment_source(bad)

    def test_no_unproduced_inputs_or_unsupported_writes(self):
        for reg in ('v0','t1','r1'):
            d=definition();d['stages'][0]['rgb_in'][0]['reg']=reg
            with self.assertRaises(ValueError):h.fragment_source(d)
        d=definition();d['stages'][0]['rgb_out']['sum']='t0'
        with self.assertRaises(ValueError):h.fragment_source(d)
        d=definition();before=copy.deepcopy(d);saved=h.pg.SAME_C[:]
        try:
            h.pg.SAME_C[:]=[True,True];h.fragment_source(d)
            self.assertEqual(h.pg.SAME_C,[True,True]);self.assertEqual(d,before)
        finally:h.pg.SAME_C[:]=saved

    def test_packets_preserve_bits_and_reject_wrong_bounds_or_order(self):
        raw,expected=ring();self.assertEqual(h.vertices_from_push(raw,0x1000),expected)
        for b in (raw[:15],raw[:-1],raw[:20],raw[:16]+struct.pack('<I',0)+raw[20:]):
            with self.assertRaises(ValueError):h.vertices_from_push(b,0x1000)
        for offset,value in ((12,1),(24,0x101A40),(20,8),(len(raw)-4,1)):
            altered=bytearray(raw);struct.pack_into('<I',altered,offset,value)
            with self.assertRaises(ValueError):h.vertices_from_push(altered,0x1000)

    def test_rgb_gain_screen_blend_and_alpha_preservation(self):
        colors=np.linspace(0,1,257);sample=np.stack([colors,1-colors,colors*.5,colors],axis=-1)
        dest=np.stack([1-colors,colors,colors*.25,1-colors*.5],axis=-1)
        for gain in (0,.25,96/255,1):
            src,out=equations(sample,np.full(3,gain),dest)
            self.assertTrue(np.array_equal(out[...,3],dest[...,3]))
            wanted=1-(1-dest[...,:3])*(1-np.minimum(2*gain*sample[...,:3],1))
            self.assertTrue(np.allclose(out[...,:3],wanted,rtol=0,atol=2e-16))
            self.assertTrue(np.all(src[...,3]==0))
        self.assertTrue(np.array_equal(equations(sample,np.zeros(3),dest)[1],dest))
        changed=sample.copy();changed[...,3]=1-changed[...,3]
        self.assertTrue(np.array_equal(equations(sample,np.ones(3),dest)[1],equations(changed,np.ones(3),dest)[1]))

    def test_wrong_owned_capture_writes_nothing(self):
        with tempfile.TemporaryDirectory()as tmp:
            p=Path(tmp)/'wrong';p.write_bytes(b'synthetic')
            with self.assertRaises(ValueError):h.prepare(p,p,p,p,Path(tmp)/'out')
            self.assertFalse((Path(tmp)/'out').exists())

if __name__=='__main__':unittest.main()
