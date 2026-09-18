"""Synthetic output initialization, strip packet, DPH and pixel producer checks."""
import copy
from pathlib import Path
import struct
import tempfile
import unittest
import numpy as np
import prepare_luma_shaders as h
from check_luma_probe import dph,tint


def definition():
    s=[0]*2048;s[0x1E60//4]=0x11101;s[0x1E70//4]=1;s[0x288//4]=12;s[0x1E40//4]=0xC00
    return h.pixel_definition(s)


class LumaPreparation(unittest.TestCase):
    def test_only_unwritten_texture_w_is_corrected(self):
        source='    OUT.position = float4(oPos.xyz * oPos.z * 0.9999, oPos.w);\n'
        source+='float4 '+', '.join(n+' = float4(0.0, 0.0, 0.0, 0.0)'for n in ('oD0','oT0','oT1','oT2','oT3'))+';\n'
        corrected=h.adapt_vertex_source(source)
        self.assertEqual(corrected.count(' = float4(0.0, 0.0, 0.0, 1.0)'),4)
        self.assertIn('oD0 = float4(0.0, 0.0, 0.0, 0.0)',corrected)
        self.assertIn('screen.x / 640.0',corrected)
        for bad in (source.replace('oT2 =','bad ='),source+'    OUT.position = anything;\n',source.replace('0.9999','1.0')):
            with self.assertRaises(ValueError):h.adapt_vertex_source(bad)

    def test_dph_implicit_one_ignores_input_w(self):
        inputs=np.array([[2,3,5,0],[2,3,5,123],[2,3,5,-9]],dtype=float);c=np.array([7,11,13,17])
        self.assertTrue(np.array_equal(dph(inputs,c),[129,129,129]))
        self.assertTrue(np.array_equal(dph(inputs,np.array([1,0,0,4])),[6,6,6]))

    def test_dot_clamps_before_final_tint_and_alpha_is_zero(self):
        sampled=np.array([[1,1,1,.7],[0,0,0,.3],[.1,.2,.3,1]])
        actual=tint(sampled,np.array([.5,.7,.2]),np.array([.1,.2,.3]))
        self.assertTrue(np.array_equal(actual[0],[.1,.2,.3,0]))
        self.assertTrue(np.array_equal(actual[1],[0,0,0,0]))
        other=sampled.copy();other[:,3]=1-other[:,3]
        self.assertTrue(np.array_equal(actual,tint(other,np.array([.5,.7,.2]),np.array([.1,.2,.3]))))

    def test_unused_mux_control_does_not_allow_mux_operation(self):
        d=definition();text=h.fragment_source(d);self.assertEqual(text.count('tex2Dproj'),1)
        for part in ('rgb_out','alpha_out'):
            bad=copy.deepcopy(d);bad['stages'][0][part]['mux']=True
            with self.assertRaises(ValueError):h.fragment_source(bad)
        for reg in ('v0','t1','r1'):
            bad=copy.deepcopy(d);bad['stages'][0]['rgb_in'][0]['reg']=reg
            with self.assertRaises(ValueError):h.fragment_source(bad)

    def test_full_strip_packet_bits_and_invalid_orders(self):
        words=[];expected=[]
        def packet(method,values):words.extend([(len(values)<<18)|method,*values])
        packet(0x17FC,[6])
        for v in range(4):
            regs=[0]*32
            for r,m in ((1,0x1A10),(2,0x1A20),(3,0x1A30),(4,0x1A40),(0,0x1A00)):
                values=[v*100+r*4+i for i in range(4)];regs[r*4:r*4+4]=values;packet(m,values)
            expected.extend(regs)
        packet(0x17FC,[0]);data=struct.pack(f'<{len(words)}I',*words);raw=struct.pack('<4I',0x1000,len(data),0x1000+len(data),0)+data
        self.assertEqual(h.vertices_from_push(raw,0x1000),struct.pack('<128I',*expected))
        for offset,value in ((20,7),(24,0x101A00),(12,1),(len(raw)-4,1)):
            bad=bytearray(raw);struct.pack_into('<I',bad,offset,value)
            with self.assertRaises(ValueError):h.vertices_from_push(bad,0x1000)
        with self.assertRaises(ValueError):h.vertices_from_push(raw[:-1],0x1000)

    def test_wrong_capture_emits_nothing(self):
        with tempfile.TemporaryDirectory()as tmp:
            p=Path(tmp)/'wrong';p.write_bytes(b'synthetic')
            with self.assertRaises(ValueError):h.prepare(p,p,p,p,Path(tmp)/'out')
            self.assertFalse((Path(tmp)/'out').exists())

if __name__=='__main__':unittest.main()
