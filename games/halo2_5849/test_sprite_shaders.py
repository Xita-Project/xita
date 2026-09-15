"""Synthetic packed-color ABI, live combiner dependencies and packet checks."""
import copy
from pathlib import Path
import struct
import tempfile
import unittest
import numpy as np
import prepare_sprite_shaders as h
from check_sprite_probe import decode_bc2, sample, transformed


def definition():
    d=dict(warnings=['t1 is read but texture stage 1 mode is NONE','t2 is read but texture stage 2 mode is NONE'],
        stage_count=2,same_c0=False,same_c1=False,mux_msb=True,
        textures=[dict(mode=mode,compare_mode=0,dot_mapping=0)for mode in ('PROJECT2D','NONE','NONE','NONE')],stages=[])
    for regs,outputs in [(('t0','c0','t1','c1'),('t0','t1','zero')),(('t2','c0','t0','v0'),('t2','r0','zero'))]:
        stage={}
        for part,channel in [('rgb','rgb'),('alpha','alpha')]:
            stage[part+'_in']=[dict(reg=reg,channel=channel,mapping='unsigned_identity')for reg in regs]
            stage[part+'_out']=dict(zip(('ab','cd','sum'),outputs),scale='identity',mux=False,ab_dot=False,
                cd_dot=False,ab_blue_to_alpha=False,cd_blue_to_alpha=False)
        d['stages'].append(stage)
    d['final']=dict(present=True,flags=0)
    for name in 'abcdefg':d['final'][name]=dict(reg='r0'if name in 'dg'else 'zero',channel='alpha'if name=='g'else'rgb',mapping='unsigned_identity')
    return d


class SpritePreparation(unittest.TestCase):
    def test_packed_bgra_independent_channels_and_absent_components(self):
        for value in range(256):
            colors=[value, value<<8, value<<16, value<<24]
            raw=b''.join(struct.pack('<4fI',-0.0,3.5,-.25,1.5,c)for c in colors)
            out=h.unpack_vertices(raw)
            for i,c in enumerate(colors):
                v=out[i*48:(i+1)*48]
                self.assertEqual(v[:16],struct.pack('<2f2I',-0.0,3.5,0,0x3F800000))
                self.assertEqual(v[16:32],struct.pack('<2f2I',-.25,1.5,0,0x3F800000))
                self.assertEqual(v[32:],struct.pack('<4f',*((c>>shift&255)/255 for shift in (16,8,0,24))))

    def test_bad_coordinates_and_shapes_reject(self):
        raw=struct.pack('<4fI',1,2,3,4,0x10203040)*4
        for data in (raw[:-1],raw+b'\0'):
            with self.assertRaises(ValueError):h.unpack_vertices(data)
        for value in (float('nan'),float('inf'),float('-inf'),65537,-65537):
            data=bytearray(raw);struct.pack_into('<f',data,20,value)
            with self.assertRaises(ValueError):h.unpack_vertices(data)

    def test_exact_dead_branch_analysis_does_not_invent_samples(self):
        d=definition();source=h.fragment_source(d)
        self.assertEqual(source.count('tex2Dproj'),1)
        self.assertNotIn('float4 t1',source);self.assertNotIn('float4 t2',source)
        for number in range(2):
            for part in ('rgb','alpha'):
                for target in ('ab','cd','sum'):
                    bad=copy.deepcopy(d);bad['stages'][number][part+'_out'][target]='r1'
                    with self.assertRaises(ValueError):h.fragment_source(bad)
                for flag in ('mux','ab_dot','cd_dot','ab_blue_to_alpha','cd_blue_to_alpha'):
                    bad=copy.deepcopy(d);bad['stages'][number][part+'_out'][flag]=True
                    with self.assertRaises(ValueError):h.fragment_source(bad)
        for name in 'dg':
            for reg in ('t1','t2','r1','v0'):
                bad=copy.deepcopy(d);bad['final'][name]['reg']=reg
                with self.assertRaises(ValueError):h.fragment_source(bad)
        bad=copy.deepcopy(d);bad['warnings'].append('unknown')
        with self.assertRaises(ValueError):h.fragment_source(bad)
        bad=copy.deepcopy(d);bad['stages'][0]['rgb_in'].pop()
        with self.assertRaises(ValueError):h.fragment_source(bad)
        bad=copy.deepcopy(d);bad['stages'].pop()
        with self.assertRaises(ValueError):h.fragment_source(bad)

    def test_bc2_four_colors_independent_alpha_and_subblock_extent(self):
        # Endpoint0 < endpoint1 must still select four opaque BC2 RGB entries.
        alpha=sum(i<<(i*4)for i in range(16))
        data=struct.pack('<QHHI',alpha,0x001F,0xF800,0xE4E4E4E4)
        image=decode_bc2(data,4,4)
        np.testing.assert_array_equal(image[0,:,:3],[[0,0,1],[1,0,0],[1/3,0,2/3],[2/3,0,1/3]])
        np.testing.assert_array_equal(image[...,3],np.arange(16).reshape(4,4)/15)
        np.testing.assert_array_equal(decode_bc2(data,1,1),image[:1,:1])
        np.testing.assert_array_equal(sample(image,np.array([-5,6]),np.array([-1,7])),image[[0,3],[0,3]])
        with self.assertRaises(ValueError):decode_bc2(data[:-1],4,4)

    def test_original_viewport_and_live_texture_equations(self):
        vertices=np.zeros((4,3,4),dtype=np.float32);vertices[:,0]=[[0,0,0,1],[640,0,0,1],[640,480,0,1],[0,480,0,1]]
        vertices[:,1]=[[0,0,0,1],[1,0,0,1],[1,1,0,1],[0,1,0,1]];vertices[:,2]=[.25,.5,.75,1]
        c=np.zeros((178,4),dtype=np.float32)
        c[0]=[320,-240,16777215,0];c[1]=[320.5,240.5,0,0]
        c[167]=[1/320,0,0,-1.0015625];c[168]=[0,-1/240,0,1.002083333]
        c[169,3]=.5;c[170,3]=1;c[171,:2]=1;c[173]=[0,1,0,1];c[176,:2]=1
        screen,uv,color=transformed(vertices,c)
        np.testing.assert_array_equal(screen,vertices[:,0,:2]);np.testing.assert_array_equal(uv,vertices[:,1,:2])
        np.testing.assert_array_equal(color,vertices[:,2])
        c[174,2:]=[.125,-.25]
        np.testing.assert_array_equal(transformed(vertices,c)[1],vertices[:,1,:2]+[.125,-.25])

    def test_complete_nonincreasing_inline_packet(self):
        vertices=struct.pack('<4fI',.25,2,3,4,0x80402010)*4
        data=struct.pack('<2I',0x417FC,8)+struct.pack('<I',0x40501818)+vertices+struct.pack('<2I',0x417FC,0)
        raw=struct.pack('<4I',0x1000,len(data),0x1000+len(data),0)+data
        self.assertEqual(h.vertices_from_push(raw,0x1000),vertices)
        for offset,value in ((20,7),(24,0x501818),(24,0x404C1818),(len(raw)-4,8),(12,1)):
            bad=bytearray(raw);struct.pack_into('<I',bad,offset,value)
            with self.assertRaises(ValueError):h.vertices_from_push(bad,0x1000)
        with self.assertRaises(ValueError):h.vertices_from_push(raw[:-1],0x1000)

    def test_position_adaptation_does_not_strip_dual_issue(self):
        source='    OUT.position = float4(oPos.xyz * oPos.z * 0.9999, oPos.w);\n'
        source+='float4 '+', '.join(n+' = float4(0.0, 0.0, 0.0, 0.0)'for n in ('oD0','oT0','oT1','oT2'))+';\n'
        body='float4 m = r10 * c[7]; float4 i = xv_rcc(oPos.wwww); r10.zw = m.zw; r1.w = i.w;\n'
        corrected=h.adapt_vertex_source(source+body)
        self.assertIn(body,corrected);self.assertEqual(corrected.count(' = float4(0.0, 0.0, 0.0, 1.0)'),3)
        self.assertIn('oD0 = float4(0.0, 0.0, 0.0, 0.0)',corrected)
        with self.assertRaises(ValueError):h.adapt_vertex_source(source.replace('oT1 =','bad ='))

    def test_wrong_owned_capture_writes_nothing(self):
        with tempfile.TemporaryDirectory()as tmp:
            p=Path(tmp)/'bad';p.write_bytes(b'synthetic')
            with self.assertRaises(ValueError):h.prepare(p,p,p,p,Path(tmp)/'out')
            self.assertFalse((Path(tmp)/'out').exists())

if __name__=='__main__':unittest.main()
