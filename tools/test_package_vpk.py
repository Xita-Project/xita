#!/usr/bin/env python3
"""Package boundary tests with synthetic content; no game or SDK files."""
from pathlib import Path
import tempfile
import unittest
import zipfile
from package_vpk import package, update_contract, halo2_contract


class Packaging(unittest.TestCase):
    def test_archive_and_exclusions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            content = {"build/eboot.bin": b"SCE\0test", "build/param.sfo": b"\0PSFtest",
                       "LICENSE": b"test license", "NOTICE": b"notice", "THIRD_PARTY.md": b"notices",
                       "shaders/test.gxp": b"synthetic shader", "sce_sys/icon0.png": b"icon",
                       "haloce/default.xbe": b"excluded", "haloce/maps/test.map": b"excluded",
                       "save/user.sav": b"excluded", "libshacccg.suprx": b"excluded", ".env": b"excluded"}
            for name, data in content.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            out = root / "xita.vpk"
            result = package(root, root / "build/eboot.bin", root / "build/param.sfo", out)
            self.assertEqual(result["files"], 7)
            with zipfile.ZipFile(out) as archive:
                self.assertIsNone(archive.testzip())
                self.assertEqual(archive.read("eboot.bin"), b"SCE\0test")
                self.assertEqual(archive.read("sce_sys/param.sfo"), b"\0PSFtest")
                self.assertFalse(any(archive.read(name) == b"excluded" for name in archive.namelist()))
            before = out.read_bytes()
            (root / "NOTICE").unlink()
            with self.assertRaises(ValueError):
                package(root, root / "build/eboot.bin", root / "build/param.sfo", out)
            self.assertEqual(out.read_bytes(), before)

    def test_updater_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for name,data in {'runtime.self':b'SCE\0'+bytes(4092),'launcher.self':b'SCE\0loader',
                'param.sfo':b'\0PSFtest','LICENSE':b'license','NOTICE':b'notice','THIRD_PARTY.md':b'notices'}.items():
                (root/name).write_bytes(data)
            out=root/'candidate.vpk'
            package(root,root/'runtime.self',root/'param.sfo',out,launcher=root/'launcher.self')
            with zipfile.ZipFile(out) as z:files={name:z.read(name) for name in z.namelist()}
            self.assertEqual(files['eboot.bin'],b'SCE\0loader')
            self.assertEqual(files['game-a.self'],(root/'runtime.self').read_bytes())
            abi=update_contract(files)
            self.assertEqual(files['update-contract.txt'],(abi+'\n').encode())
            files['game-a.self']=b'SCE\0different runtime'
            files['boot-game.txt']=b'different initial runtime record'
            self.assertEqual(update_contract(files),abi)
            files['eboot.bin']+=b'different launcher'
            self.assertNotEqual(update_contract(files),abi)

    def test_combined_package(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for name,data in {'runtime.self':b'SCE\0'+bytes(4092),'launcher.self':b'SCE\0loader',
                'param.sfo':b'\0PSFtest','LICENSE':b'license','NOTICE':b'notice','THIRD_PARTY.md':b'notices'}.items():
                (root/name).write_bytes(data)
            h2=root/'h2.vpk';out=root/'combined.vpk'
            payload={'eboot.bin':b'SCE\0'+b'h'*4092,'sce_sys/param.sfo':b'\0PSFh2',
                     'halo2_image.bin':b'owned-image','bundled-mode.txt':b'xita-halo2-bundle-v1\n',
                     'h2menu_vs_abc.gxp':b'h2 shader','quad.contract.bin':b'contract'}
            def write_h2():
                with zipfile.ZipFile(h2,'w') as z:
                    for name,data in payload.items():z.writestr(name,data)
            def build():return package(root,root/'runtime.self',root/'param.sfo',out,launcher=root/'launcher.self',halo2_package=h2)
            write_h2();build()
            with zipfile.ZipFile(out) as z:files={name:z.read(name) for name in z.namelist()}
            self.assertEqual(files['sce_sys/param.sfo'],b'\0PSFtest')
            self.assertEqual(files['halo2-a.self'],payload['eboot.bin'])
            self.assertEqual(files['halo2/h2menu_vs_abc.gxp'],b'h2 shader')
            abi=update_contract(files)
            self.assertEqual(files['halo2-update-contract.txt'],(halo2_contract(abi)+'\n').encode())
            files['halo2-a.self']+=b'new runtime'
            self.assertEqual(update_contract(files),abi)
            files['halo2/h2menu_vs_abc.gxp']+=b'new shader'
            self.assertNotEqual(update_contract(files),abi)
            before=out.read_bytes()
            payload['../game-a.self']=b'bad';write_h2()
            with self.assertRaises(ValueError):build()
            self.assertEqual(out.read_bytes(),before)
            del payload['../game-a.self'];del payload['bundled-mode.txt'];write_h2()
            with self.assertRaises(ValueError):build()


if __name__ == "__main__":
    unittest.main()
