"""Synthetic component-flow and equation checks; no proprietary shader fixtures."""
import copy
from pathlib import Path
import struct
import tempfile
import unittest
import numpy as np
import prepare_composition_shaders as h
from check_composition_probe import equations


def definition():
    s=[0]*2048;s[0x1E60//4]=0x11006;s[0x1E70//4]=1|(17<<5)|(9<<10)|(1<<15);s[0x1E74//4]=0x44
    s[0x288//4]=10;s[0x28C//4]=26<<8
    return h.pixel_definition(s)


class CompositionPreparation(unittest.TestCase):
    def test_producer_masks_and_parallel_inputs(self):
        d=definition();d['stages'][0]['rgb_out']['ab']='v0'
        d['stages'][1]['rgb_in'][0]['reg']='v0';h.validate_flow(d)
        d['stages'][1]['rgb_in'][0]['channel']='alpha_rep'
        with self.assertRaises(ValueError):h.validate_flow(d)
        d['stages'][0]['alpha_out']['ab']='v0';h.validate_flow(d)
        d['stages'][0]['rgb_in'][0]['reg']='v0'
        with self.assertRaises(ValueError):h.validate_flow(d) # same-stage write is too late

    def test_blue_to_alpha_and_unavailable_t1(self):
        d=definition();d['stages'][0]['rgb_out'].update(ab='v1',ab_blue_to_alpha=True)
        d['stages'][1]['alpha_in'][0].update(reg='v1',channel='alpha');h.validate_flow(d)
        d['stages'][0]['rgb_out']['ab_blue_to_alpha']=False
        with self.assertRaises(ValueError):h.validate_flow(d)
        d=definition();d['final']['g']['reg']='v0'
        with self.assertRaises(ValueError):h.validate_flow(d)
        d=definition();d['stages'][0]['rgb_in'][0]['reg']='t1'
        with self.assertRaises(ValueError):h.validate_flow(d)
        d=definition();d['stages'][0]['rgb_out']['ab']='t1'
        with self.assertRaises(ValueError):h.validate_flow(d)

    def test_mode_and_final_rejection(self):
        for unit in range(4):
            for key,value in (('mode','CUBEMAP'),('compare_mode',1),('dot_mapping',3)):
                d=definition();d['textures'][unit][key]=value
                with self.assertRaises(ValueError):h.fragment_source(d)
        for key in ('same_c0','same_c1','mux_msb'):
            d=definition();d[key]=True
            with self.assertRaises(ValueError):h.fragment_source(d)
        d=definition();d['textures'][2]['input_stage']=1
        with self.assertRaises(ValueError):h.fragment_source(d)
        d=definition();d['final']['e']['reg']='t2'
        with self.assertRaises(ValueError):h.fragment_source(d)

    def test_shared_state_and_no_definition_mutation(self):
        d=definition();before=copy.deepcopy(d);saved=h.pg.SAME_C[:]
        try:
            h.pg.SAME_C[:]=[True,True];source=h.fragment_source(d)
            self.assertEqual(h.pg.SAME_C,[True,True]);self.assertEqual(d,before)
            self.assertIn('float2(320.0,240.0)',source);self.assertIn('float2(640.0,480.0)',source)
            self.assertNotIn('uniform sampler2D tex1',source)
            self.assertEqual(source.count('combiner stage'),6)
        finally:h.pg.SAME_C[:]=saved

    def test_capture_bounds_and_wrong_owned_revision(self):
        with tempfile.TemporaryDirectory()as tmp:
            p=Path(tmp)/'input';p.write_bytes(struct.pack('<8I',1,0x800,2,2,8,16,0x11229,0)+bytes(range(16)))
            self.assertEqual(h.linear(p,0x800,2,2,8),bytes(range(16)))
            for dims in [(0x801,2,2,8),(0x800,1,2,8),(0x800,2,2,9)]:
                with self.assertRaises(ValueError):h.linear(p,*dims)
            with self.assertRaises(ValueError):h.prepare(p,p,p,p,p,p,Path(tmp)/'out')
            self.assertFalse((Path(tmp)/'out').exists())

    def test_equations_clamps_and_final_alpha_before_color_scale(self):
        c=np.zeros((18,4));c[0]=1;c[4]=.25;c[12]=.75;c[16]=[.25,.5,.75,0]
        t2=np.array([.1,.2,.3,0]);t3=np.array([.8,.7,.6,.9])
        rgb,alpha=equations(c,t2,t3)
        # Stage0 is negative, unsigned stage1 clamps to zero. Both dot results
        # are zero, so the mix selects C0; final alpha precedes final color.
        self.assertTrue(np.allclose(rgb,t2[:3]*.25*c[16,:3]))
        self.assertAlmostEqual(float(alpha),.3*.25)
        c[16,:3]=0;rgb2,alpha2=equations(c,t2,t3)
        self.assertFalse(np.any(rgb2));self.assertEqual(alpha,alpha2)

if __name__=='__main__':unittest.main()
