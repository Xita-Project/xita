"""Synthetic packet, component-flow and clipped downsample checks."""
import copy
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import numpy as np
import prepare_threshold_shaders as h
from check_composition_probe import sample


def definition():
    s=[0]*2048;s[0x1E60//4]=0x11004;s[0x1E70//4]=0x8421;s[0x288//4]=10;s[0x28C//4]=26<<8
    return h.pixel_definition(s)


def push():
    words=[]
    def packet(method,data):words.extend([(len(data)<<18)|method,*data])
    packet(0x17FC,[7])
    for v in range(4):
        for method in (0x1A50,0x1A40,0x1A30,0x1A20,0x1A10,0x1518):
            packet(method,[0x80000000|v,0x3F800000+v,0x12345678,0])
    packet(0x17FC,[0]);data=struct.pack('<%dI'%len(words),*words)
    return struct.pack('<4I',0x1000,len(data),0x1000+len(data),0)+data


class ThresholdPreparation(unittest.TestCase):
    def test_complete_24_word_vertex_packets(self):
        raw=push();v=struct.unpack('<112I',h.vertices_from_push(raw,0x1000))
        for vertex in range(4):
            for reg in range(6):self.assertEqual(v[vertex*28+reg*4],0x80000000|vertex)
            self.assertEqual(v[vertex*28+24:vertex*28+28],(0,0,0,0))
        for cut in (0,4,16,20,len(raw)-1):
            with self.assertRaises(ValueError):h.vertices_from_push(raw[:cut],0x1000)
        altered=bytearray(raw);struct.pack_into('<I',altered,24,(4<<18)|0x1A40)
        with self.assertRaises(ValueError):h.vertices_from_push(altered,0x1000)
        altered=bytearray(raw);struct.pack_into('<I',altered,len(raw)-4,1)
        with self.assertRaises(ValueError):h.vertices_from_push(altered,0x1000)

    def test_final_product_requires_prior_rgb_producers(self):
        d=definition();d['final']['b']['reg']='ef_prod'
        with self.assertRaises(ValueError):h.validate_flow(d)
        h.validate_flow(d,texture_units=(0,1,2,3),final_product=True)
        d['final']['e']['reg']='v0'
        with self.assertRaises(ValueError):h.fragment_source(d)
        d['final']['e']['reg']='ef_prod'
        with self.assertRaises(ValueError):h.fragment_source(d) # cannot read its own output

    def test_default_flow_remains_strict_and_new_route_requires_bound_inputs(self):
        d=definition();d['stages'][0]['rgb_in'][0]['reg']='t1'
        with self.assertRaises(ValueError):h.validate_flow(d)
        h.fragment_source(d)
        d['stages'][0]['rgb_in'][0]['reg']='r0'
        with self.assertRaises(ValueError):h.fragment_source(d)
        for unit in range(4):
            d=definition();d['textures'][unit]['mode']='DOTPRODUCT'
            with self.assertRaises(ValueError):h.fragment_source(d)
        d=definition();d['stages'][0]['rgb_out']['ab']='v0'
        with self.assertRaises(ValueError):h.fragment_source(d)

    def test_shared_generator_controls_restored(self):
        d=definition();saved=h.pg.SAME_C[:];before=copy.deepcopy(d)
        try:
            h.pg.SAME_C[:]=[True,True];text=h.fragment_source(d)
            self.assertEqual(h.pg.SAME_C,[True,True]);self.assertEqual(d,before)
            self.assertEqual(text.count('tex2Dproj('),4)
            self.assertEqual(text.count('combiner stage'),4)
        finally:h.pg.SAME_C[:]=saved

    def test_window_conversion_preserves_oversized_geometry(self):
        text='screen.x / 640.0,screen.y / 480.0,original_slots'
        with patch.object(h,'vertex_source',return_value=(text,{})):
            out,_=h.threshold_vertex(b'')
        self.assertEqual(out,'screen.x / 160.0,screen.y / 120.0,original_slots')
        # A 640-wide window quad clipped to 160 yields the original 4x slope.
        x=np.arange(160)+.5;ndc=2*x/160-1
        window=(ndc+1)*80;uv=window/640*2560
        self.assertTrue(np.allclose(uv,x*4,rtol=0,atol=1e-12))
        with patch.object(h,'vertex_source',return_value=('changed output',{})):
            with self.assertRaises(ValueError):h.threshold_vertex(b'')

    def test_four_bilinear_samples_equal_box_average(self):
        y,x=np.indices((480,640));image=np.stack([(x*17+y*7)&255,(x*5+y*23)&255,(x*11+y*13)&255,(x+y)&255],axis=-1)/255
        yy,xx=np.indices((120,160));out=0
        for dx,dy in ((-1,-1),(1,-1),(-1,1),(1,1)):
            out=out+sample(image,(4*(xx+.5)+dx)/640,(4*(yy+.5)+dy)/480)/4
        exact=image.reshape(120,4,160,4,4).mean(axis=(1,3))
        self.assertTrue(np.allclose(out,exact,rtol=0,atol=1e-12))
        rgb=np.clip(4*(out[...,:3]-172/255),0,1)
        self.assertTrue(np.any(rgb>0));self.assertTrue(np.any(rgb==0))

    def test_wrong_owned_capture_writes_nothing(self):
        with tempfile.TemporaryDirectory()as tmp:
            p=Path(tmp)/'wrong';p.write_bytes(b'no game data')
            with self.assertRaises(ValueError):h.prepare(p,p,p,p,Path(tmp)/'out')
            self.assertFalse((Path(tmp)/'out').exists())

if __name__=='__main__':unittest.main()
