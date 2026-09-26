import hashlib
from pathlib import Path
import tempfile
import unittest
import zipfile
from package_vpk import update_contract
from prepare_release_update import prepare

class Release(unittest.TestCase):
    def test_identity_integrity_and_paths(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);vpk=p/'xita.vpk'
            runtime=b'SCE\0XITA-DISTRIBUTION:tester-v1 V0.3.0'+bytes(4096)
            files={'game-a.self':runtime,'distribution.txt':b'tester\n','release-ca.pem':b'ca','eboot.bin':b'loader'}
            def write():
                files['update-contract.txt']=(update_contract(files)+'\n').encode()
                with zipfile.ZipFile(vpk,'w') as z:
                    for k,v in files.items():z.writestr(k,v)
            write()
            sha=prepare(vpk,'v0.3.0','0.3.0','Fixes.',p/'good')
            self.assertEqual(sha,hashlib.sha256(runtime).hexdigest())
            self.assertEqual((p/'good/xita-runtime.self').read_bytes(),runtime)
            for tag,version,notes in [('../bad','0.3.0','ok'),('v1','missing','ok'),('v1','0.3.0','bad\nnotes')]:
                with self.assertRaises(ValueError):prepare(vpk,tag,version,notes,p/'bad')
                self.assertFalse((p/'bad').exists())
            files['distribution.txt']=b'developer\n';write()
            with self.assertRaises(ValueError):prepare(vpk,'v1','0.3.0','ok',p/'bad')
            files['distribution.txt']=b'tester\n';files['game-a.self']+=b'XITA-DISTRIBUTION:developer-v1';write()
            with self.assertRaises(ValueError):prepare(vpk,'v1','0.3.0','ok',p/'bad')

if __name__=='__main__':unittest.main()
