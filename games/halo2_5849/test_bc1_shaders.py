"""Synthetic BC1 preparation and codec checks; no owned capture fixtures."""
import copy
from pathlib import Path
import struct
import tempfile
import unittest
import numpy as np
import prepare_bc1_shaders as h
from check_bc1_probe import decode, sample, synthetic


def definition():
    setup=[0]*2048
    setup[0x1E60//4]=0x11003
    setup[0x1E70//4]=sum(1<<(5*i) for i in range(4))
    setup[0x288//4]=12
    setup[0x28C//4]=8<<8
    return h.pixel_definition(setup)


def packet(method,words):
    return struct.pack('<'+'I'*(len(words)+1),(len(words)<<18)|method,*words)


def ring():
    data=packet(0x17FC,[7])
    for vertex in range(4):
        for reg,method in ((4,0x1A40),(3,0x1A30),(2,0x1A20),(1,0x1A10),(0,0x1518)):
            data+=packet(method,[0x12340000+vertex*28+reg*4+i for i in range(4)])
    data+=packet(0x17FC,[0])
    return struct.pack('<4I',0x1000,len(data),0x1000+len(data),0)+data


class BC1Preparation(unittest.TestCase):
    def test_shared_state_and_final_factors(self):
        d=definition();d['final']['b']['reg']='c0';d['final']['c']['reg']='c1'
        before=copy.deepcopy(d);saved=h.pg.SAME_C[:]
        try:
            h.pg.SAME_C[:]=[True,True]
            source=h.fragment_source(d)
            self.assertEqual(h.pg.SAME_C,[True,True]);self.assertEqual(d,before)
            self.assertIn('psc[16]',source);self.assertIn('psc[17]',source)
            self.assertEqual(source.count('tex2Dproj('),4)
            self.assertNotIn('float2(640.0',source)
        finally:h.pg.SAME_C[:]=saved

    def test_modes_and_missing_inputs(self):
        for unit in range(4):
            for key,value in (('mode','CUBEMAP'),('dot_mapping',1),('compare_mode',1)):
                d=definition();d['textures'][unit][key]=value
                with self.assertRaises(ValueError):h.fragment_source(d)
        for reg in ('v0','v1','fog','ef_prod','v1r0_sum'):
            d=definition();d['stages'][0]['rgb_in'][0]['reg']=reg
            with self.assertRaises(ValueError):h.fragment_source(d)
            d=definition();d['stages'][0]['rgb_out']['ab']=reg
            with self.assertRaises(ValueError):h.fragment_source(d)
            d=definition();d['final']['g']['reg']=reg
            with self.assertRaises(ValueError):h.fragment_source(d)
        for key,value in (('same_c0',True),('same_c1',True),('mux_msb',True),('stage_count',2),('warnings',['synthetic'])):
            d=definition();d[key]=value
            with self.assertRaises(ValueError):h.fragment_source(d)
        for key,value in (('present',False),('flags',1)):
            d=definition();d['final'][key]=value
            with self.assertRaises(ValueError):h.fragment_source(d)
        d=definition();d['final']['e']['reg']='t0'
        with self.assertRaises(ValueError):h.fragment_source(d)

    def test_original_order_without_owned_words(self):
        raw=ring();out=h.vertices_from_push(raw,0x1000)
        values=struct.unpack('<112I',out)
        for vertex in range(4):
            self.assertEqual(values[vertex*28:vertex*28+20],tuple(0x12340000+vertex*28+i for i in range(20)))
            self.assertEqual(values[vertex*28+20:vertex*28+28],(0,)*8)
        self.assertEqual(raw,ring())

    def test_packet_and_bounds_reject(self):
        raw=ring()
        for n in range(len(raw)):
            with self.assertRaises(ValueError):h.vertices_from_push(raw[:n],0x1000)
        for offset in (0,4,8,12,16,20,24,len(raw)-8,len(raw)-4):
            bad=bytearray(raw);word=struct.unpack_from('<I',bad,offset)[0];struct.pack_into('<I',bad,offset,word^0x80000000)
            with self.assertRaises(ValueError):h.vertices_from_push(bad,0x1000)
        for addr in (0,0xFFF,0x1001,0x1004,0xFFFFFFFF):
            with self.assertRaises(ValueError):h.vertices_from_push(raw,addr)

    def test_wrong_revision_does_not_create_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);src=root/'synthetic';src.write_bytes(b'fixture')
            with self.assertRaises(ValueError):h.prepare(src,src,src,src,root/'output')
            self.assertFalse((root/'output').exists())

    def test_codec_transparency_endpoints_and_wrap(self):
        image=decode(synthetic())
        self.assertTrue(np.array_equal(image[0,3],[0,0,0,0]))
        self.assertTrue(np.array_equal(image[0,4],[0,1,0,1]))
        self.assertTrue(np.array_equal(image[4,3],[0,0,0,0]))
        self.assertTrue(np.allclose(image[0,2],[1,.5,.5,1]))
        equal=decode(struct.pack('<HHI',0x7E0,0x7E0,0xFFFFFFFF)*4)
        self.assertFalse(np.any(equal))
        u=np.array([-.5,-.125,0,.5,.99,2.0]);v=np.array([.1,.2,.3,.4,.5,.6])
        self.assertTrue(np.allclose(sample(image,u,v),sample(image,u+4,v-3)))
        for n in (0,31,33,64):
            with self.assertRaises(ValueError):decode(bytes(n))

if __name__=='__main__':unittest.main()
