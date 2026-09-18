#!/usr/bin/env python3
"""Game isolation, interrupted candidates and rollback, using synthetic SELFs."""
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from package_vpk import update_record, halo2_contract

ROOT = Path(__file__).resolve().parents[1]

def main():
    original=Path.cwd()
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory);so=root/'update.so'
        subprocess.run(['cc','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',
            *[str(ROOT/'runtime'/p) for p in ('xv_update.c','xv_update_halo2.c','xv_sha256.c')],'-o',str(so)],check=True)
        lib=C.CDLL(str(so))
        app=root/'ux0:app/XITA00001';app.mkdir(parents=True)
        (root/'app0:').symlink_to(app,target_is_directory=True)
        abi=hashlib.sha256(b'shared immutable assets').hexdigest()
        old=[b'SCE\0'+b'C'*4092,b'SCE\0'+b'H'*4092]
        new=[b'SCE\0'+b'c'*8100,b'SCE\0'+b'h'*9000]
        contracts=[abi,halo2_contract(abi)]
        prefixes=['xv_update_','xv_halo2_update_']
        def call(game,op,*args):return getattr(lib,prefixes[game]+op)(*args)
        for game in (0,1):
            getattr(lib,prefixes[game]+'begin').argtypes=[C.c_uint,C.c_char_p,C.c_char_p]
            getattr(lib,prefixes[game]+'chunk').argtypes=[C.c_uint,C.c_void_p,C.c_uint]
            name='game' if game==0 else 'halo2'
            (app/f'{name}-a.self').write_bytes(old[game])
            (app/('update-contract.txt' if game==0 else 'halo2-update-contract.txt')).write_text(contracts[game]+'\n')
            (app/f'boot-{name}.txt').write_bytes(update_record(len(old[game]),hashlib.sha256(old[game]).hexdigest(),contracts[game]))
        def stage(game,data):
            assert call(game,'begin',len(data),hashlib.sha256(data).hexdigest().encode(),contracts[game].encode())==0
            assert call(game,'chunk',0,data,len(data))==0
            assert call(game,'finish')==0
        os.chdir(root)
        try:
            for game in (0,1):
                call(game,'init');assert call(game,'boot')==0
                assert call(game,'begin',len(new[game]),hashlib.sha256(new[game]).hexdigest().encode(),contracts[1-game].encode())==-1
            assert call(0,'begin',70*1024*1024,b'a'*64,contracts[0].encode())==-1
            assert call(1,'begin',129*1024*1024,b'a'*64,contracts[1].encode())==-1
            assert call(1,'begin',70*1024*1024,b'a'*64,contracts[1].encode())==0
            call(1,'close')
            # Interleave transfers: independent metadata, hashes, offsets and state.
            for game in (0,1):stage(game,new[game])
            assert call(1,'request',0)==0
            assert call(1,'boot')==1
            assert call(0,'boot')==0 and (app/'game-a.self').read_bytes()==old[0]
            # No H2 confirmation: its next boot returns the original slot.
            assert call(1,'boot')==0
            assert call(0,'request',0)==0 and call(0,'boot')==1
            assert call(0,'confirm',1)==0
            # A fresh H2 attempt does not retire or corrupt the confirmed CE update.
            call(1,'init');stage(1,new[1]);assert call(1,'request',0)==0
            assert call(1,'boot')==1 and call(1,'confirm',1)==0
            call(1,'init')
            out=C.create_string_buffer(1024);call(1,'json',out,len(out));status=json.loads(out.value)
            assert status['boot_slot']==-1 and status['installed_slot']==1
            assert status['installed_sha256']==hashlib.sha256(new[1]).hexdigest()
            assert call(1,'request',1)==0 and call(1,'boot')==0
            assert call(0,'boot')==1 and (app/'game-b.self').read_bytes()==new[0]
            assert (app/'halo2-a.self').read_bytes()==old[1]
        finally:
            os.chdir(original)
    print('PASS: CE/H2 contract isolation, interleaved staging, failed boot fallback, independent confirmation and rollback')

if __name__=='__main__':main()
