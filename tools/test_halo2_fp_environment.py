"""Synthetic H2-only MXCSR emission; no commercial instructions or data."""
from pathlib import Path
import tempfile
import unittest
from iced_x86 import Decoder
from recompiler import xita_recomp as recomp
from recompiler.core.hooks import NoGameHooks
from games.halo2_5849.hooks import lower_fp_environment
from tools.test_game_profiles import fixture

class EnvironmentHooks(NoGameHooks):
    def lower_instruction(self, emitter, instruction, output):
        return lower_fp_environment(emitter, instruction, output)

class EnvironmentEmission(unittest.TestCase):
    def test_opt_in_and_memory_addressing(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); data=bytearray(fixture()[0])
            code=bytes.fromhex("0fae5c8830 640fae52fc c3")
            data[0x1000:0x1000+len(code)]=code
            path=root/'synthetic.xbe'; path.write_bytes(data); image=recomp.Image(str(path))
            d=recomp.Discovery(image,{}, {}, lambda *_:None); d.add_root(0x11000); d.run()
            for hooks in (NoGameHooks(),EnvironmentHooks()):
                output=root/type(hooks).__name__; e=recomp.Emitter(image,d,{}, {},str(output),1,hooks=hooks); e.write_all()
                text=(output/'code_000.c').read_text()
                if type(hooks) is NoGameHooks:
                    self.assertIn('"stmxcsr"',text); self.assertIn('"ldmxcsr"',text)
                    self.assertNotIn('h2_stmxcsr',text)
                else:
                    self.assertFalse(e.unimpl)
                    self.assertIn('h2_stmxcsr(c, 0x00011000u, (c->r[0]+(c->r[1]*4)+0x30u))',text)
                    self.assertIn('h2_ldmxcsr(c, 0x00011005u, (c->fs_base+(c->r[2]+0xFFFFFFFCu)))',text)
    def test_unsupported_address_modes_reject(self):
        for code in ('670fae1b','650fae18'):
            with self.subTest(code=code), self.assertRaisesRegex(ValueError,'shape mismatch'):
                lower_fp_environment(None,Decoder(32,bytes.fromhex(code),ip=0x11000).decode(),[])
        self.assertFalse(lower_fp_environment(None,Decoder(32,b'\x90',ip=0x11000).decode(),[]))

if __name__=='__main__': unittest.main()
