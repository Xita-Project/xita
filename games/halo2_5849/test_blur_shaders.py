"""Synthetic averaging, clamped edges, alpha gain and shader admission tests."""
import copy
import struct
from pathlib import Path
import tempfile
import unittest
import numpy as np
import prepare_blur_shaders as h
from check_composition_probe import sample
from check_blur_probe import decode_sampler


def definition():
    s=[0]*2048
    s[0x1E60//4]=0x11003;s[0x1E70//4]=0x8421
    s[0x288//4]=12;s[0x28C//4]=28<<8
    s[0x1E40//4]=0xC00;s[0xAA0//4]=0xC00 # synthetic zero-producing r0 stage
    return h.pixel_definition(s)


class BlurPreparation(unittest.TestCase):
    def test_exact_stage_count_and_sampler_modes(self):
        d=definition();out=h.fragment_source(d)
        self.assertEqual(out.count('tex2Dproj('),4)
        self.assertEqual(out.count('float2(160.0,120.0)'),4)
        self.assertEqual(out.count('combiner stage'),3)
        for count in (0,2,4,8):
            bad=copy.deepcopy(d);bad['stage_count']=count
            with self.assertRaises(ValueError):h.fragment_source(bad)
        for unit in range(4):
            bad=copy.deepcopy(d);bad['textures'][unit]['mode']='NONE'
            with self.assertRaises(ValueError):h.fragment_source(bad)

    def test_live_components_and_destinations_must_be_supported(self):
        d=definition();d['stages'][0]['rgb_in'][0]['reg']='r1'
        with self.assertRaises(ValueError):h.fragment_source(d)
        d=definition();d['stages'][0]['alpha_in'][0]['reg']='v0'
        with self.assertRaises(ValueError):h.fragment_source(d)
        d=definition();d['stages'][0]['rgb_out']['sum']='t0'
        with self.assertRaises(ValueError):h.fragment_source(d)
        d=definition();d['final']['e']['reg']='ef_prod'
        with self.assertRaises(ValueError):h.fragment_source(d)

    def test_generator_controls_and_definition_are_preserved(self):
        d=definition();before=copy.deepcopy(d);saved=h.pg.SAME_C[:]
        try:
            h.pg.SAME_C[:]=[True,True];h.fragment_source(d)
            self.assertEqual(h.pg.SAME_C,[True,True]);self.assertEqual(d,before)
        finally:h.pg.SAME_C[:]=saved

    def test_four_half_texel_samples_equal_separable_121_kernel(self):
        y,x=np.indices((120,160));image=np.stack([(x*17+y*7)&255,(x*5+y*23)&255,(x*11+y*13)&255,(x*3+y*7)&255],axis=-1)/255
        average=0
        for dx,dy in ((-.5,-.5),(.5,-.5),(-.5,.5),(.5,.5)):
            average=average+sample(image,(x+.5+dx)/160,(y+.5+dy)/120)/4
        padded=np.pad(image,((1,1),(1,1),(0,0)),mode='edge');reference=0
        for row,wy in enumerate((1,2,1)):
            for col,wx in enumerate((1,2,1)):
                reference=reference+padded[row:row+120,col:col+160]*wx*wy/16
        self.assertTrue(np.allclose(average,reference,rtol=0,atol=1e-12))
        # Includes corners: clamping is part of the actual sampler contract.
        self.assertTrue(np.allclose(average[0,0],reference[0,0],rtol=0,atol=1e-12))

    def test_original_alpha_gain_and_final_clamp(self):
        alpha=np.array([[0,.25,.5,1],[1,.5,.75,1],[0,1,.25,1],[1,0,.5,1]])
        pair0=(alpha[0]+alpha[1])/2;pair1=(alpha[2]+alpha[3])/2
        for gain in (0,.125,178/255,1):
            staged=np.clip((pair0+pair1)*gain,-1,1)
            final=np.clip(staged,0,1)
            self.assertTrue(np.array_equal(final,np.clip(alpha.mean(axis=0)*2*gain,0,1)))
        self.assertEqual(np.clip((pair0[-1]+pair1[-1])*178/255,0,1),1)

    def test_explicit_readback_encoding_and_invalid_captures(self):
        pixel=np.array([0,16384,32768,65535],dtype='<u2')
        raw=np.tile(pixel,(160*120,1)).tobytes()
        a=decode_sampler(raw,'vita3k-unorm16')
        self.assertTrue(np.array_equal(a[0,0],pixel.astype(float)/65535))
        for encoding in (None,'guess','native'):
            with self.assertRaises(ValueError):decode_sampler(raw,encoding)
        with self.assertRaises(ValueError):decode_sampler(raw[:-1],'vita3k-unorm16')
        with self.assertRaises(ValueError):decode_sampler(raw,'half') # 0xffff is NaN
        half=np.tile(np.array([0,.25,.5,1],dtype='<f2'),(160*120,1)).tobytes()
        self.assertTrue(np.array_equal(decode_sampler(half,'half')[0,0],[0,.25,.5,1]))

    def test_complete_consistent_original_variant_inputs(self):
        patterns=[]
        for radius in (.5,.625,.78125,.96875):
            v=np.zeros((4,7,4),dtype='<f4')
            for index,(x,y)in enumerate(((0,0),(0,480),(640,480),(640,0))):
                v[index,0]=[x,y,1,1]
                for unit,(dx,dy)in enumerate(((-1,-1),(1,-1),(-1,1),(1,1))):v[index,unit+1]=[x+dx*radius,y+dy*radius,0,1]
            patterns.append(v.tobytes())
        self.assertEqual(h.validate_variants(patterns),b''.join(patterns))
        for bad in (patterns[:3],patterns+[patterns[0]],[patterns[0][:-1]]+patterns[1:]):
            with self.assertRaises(ValueError):h.validate_variants(bad)
        for offset,value in ((0,1),(6*4,1),(4*4,0x7FC12345),(4*4,0xC0000000)):
            altered=bytearray(patterns[1]);struct.pack_into('<I',altered,offset,value)
            with self.assertRaises(ValueError):h.validate_variants([patterns[0],bytes(altered),*patterns[2:]])

    def test_wrong_owned_inputs_write_nothing(self):
        with tempfile.TemporaryDirectory()as tmp:
            p=Path(tmp)/'wrong';p.write_bytes(b'no game data')
            with self.assertRaises(ValueError):h.prepare(p,p,p,p,Path(tmp)/'out')
            self.assertFalse((Path(tmp)/'out').exists())

if __name__=='__main__':unittest.main()
