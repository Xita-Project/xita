#!/usr/bin/env python3
"""Real fixed-path storage transactions against synthetic SELF files."""
import ctypes as C
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
from package_vpk import update_record

ROOT=Path(__file__).resolve().parents[1]

class Sha(C.Structure):
    _fields_=[('h',C.c_uint32*8),('bytes',C.c_uint64),('used',C.c_uint),('block',C.c_ubyte*64)]

def main():
    before=Path.cwd()
    with tempfile.TemporaryDirectory(prefix='xita-update-test-') as temp:
        tmp=Path(temp)
        so=tmp/'update.so'
        subprocess.run(['cc','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-fPIC','-shared',
            str(ROOT/'runtime/xv_update.c'),str(ROOT/'runtime/xv_sha256.c'),'-o',str(so)],check=True)
        lib=C.CDLL(str(so))
        lib.xv_update_begin.argtypes=[C.c_uint,C.c_char_p,C.c_char_p]
        lib.xv_update_chunk.argtypes=[C.c_uint,C.c_void_p,C.c_uint]
        lib.xv_sha256_add.argtypes=[C.POINTER(Sha),C.c_void_p,C.c_size_t]
        for n in [0,1,3,55,56,63,64,65,119,120,128,4096,65536,1000000]:
            data=bytes((i*13+7)&255 for i in range(n))
            for step in [1,73,65536]:
                h=Sha();lib.xv_sha256_init(C.byref(h))
                for pos in range(0,n,step):
                    chunk=data[pos:pos+step];lib.xv_sha256_add(C.byref(h),chunk,len(chunk))
                out=C.create_string_buffer(65);lib.xv_sha256_end(C.byref(h),out)
                assert out.value.decode()==hashlib.sha256(data).hexdigest(),(n,step)
        app=tmp/'ux0:app/XITA00001';app.mkdir(parents=True)
        (tmp/'app0:').symlink_to(app,target_is_directory=True)
        store=tmp/'ux0:data/xita/update';store.mkdir(parents=True)
        abi=hashlib.sha256(b'fixed launcher and assets').hexdigest()
        (app/'update-contract.txt').write_text(abi+'\n')
        old=b'SCE\0'+bytes(4092)
        new=b'SCE\0'+bytes((i*7)&255 for i in range(140000))
        old_hash=hashlib.sha256(old).hexdigest()
        (app/'game-a.self').write_bytes(old)
        (app/'boot-game.txt').write_bytes(update_record(len(old),old_hash,abi))
        os.chdir(tmp)
        try:
            lib.xv_update_init()
            assert lib.xv_update_boot()==0
            assert lib.xv_update_confirm(0)==0 and lib.xv_update_confirm(2)==-1
            assert lib.xv_update_request(0)==-1
            assert lib.xv_update_begin(len(new),b'f'*64,b'0'*64)==-1
            assert lib.xv_update_begin(4095,b'f'*64,abi.encode())==-1
            def begin(data,sha=None):
                digest=sha or hashlib.sha256(data).hexdigest()
                assert lib.xv_update_begin(len(data),digest.encode(),abi.encode())==0
            def chunks(data):
                for pos in range(0,len(data),65536):
                    part=data[pos:pos+65536]
                    assert lib.xv_update_chunk(pos,part,len(part))==0
            def stage(data):
                begin(data);chunks(data);assert lib.xv_update_finish()==0
            begin(new)
            assert lib.xv_update_chunk(1,new[:20],20)==-1
            assert lib.xv_update_chunk(0,new,65537)==-1
            assert lib.xv_update_chunk(0,new[:64],64)==0
            assert lib.xv_update_finish()==-1
            lib.xv_update_close();lib.xv_update_init()
            assert lib.xv_update_boot()==0 and (app/'game-a.self').read_bytes()==old
            begin(new,'0'*64);chunks(new)
            assert lib.xv_update_finish()==-1 and lib.xv_update_request(0)==-1
            stage(new)
            # Corruption after staging is detected again by the boot helper.
            (store/'incoming.self').write_bytes(new[:-1]+b'x')
            assert lib.xv_update_request(0)==0
            assert lib.xv_update_boot()==0 and (app/'game-a.self').read_bytes()==old
            lib.xv_update_init();stage(new)
            # Destination failure cannot replace the active executable.
            (app/'game-b.self.next').mkdir()
            assert lib.xv_update_request(0)==0 and lib.xv_update_boot()==0
            (app/'game-b.self.next').rmdir()
            lib.xv_update_init();stage(new)
            assert lib.xv_update_request(0)==0 and lib.xv_update_boot()==1
            assert (app/'game-a.self').read_bytes()==old
            assert (app/'game-b.self').read_bytes()==new
            # No dashboard acknowledgement: next launch falls back to A.
            assert lib.xv_update_boot()==0
            lib.xv_update_init();stage(new)
            assert lib.xv_update_request(0)==0 and lib.xv_update_boot()==1
            assert lib.xv_update_confirm(1)==0 and lib.xv_update_boot()==1
            newer=b'SCE\0'+b'z'*8000
            lib.xv_update_init();stage(newer)
            assert lib.xv_update_request(0)==0 and lib.xv_update_boot()==0
            assert (app/'game-b.self').read_bytes()==new
            assert lib.xv_update_confirm(0)==0 and lib.xv_update_boot()==0
            # Explicit rollback retires the newer slot, preserving its file.
            lib.xv_update_init();assert lib.xv_update_request(1)==0
            assert lib.xv_update_boot()==1
            assert (app/'game-a.self').read_bytes()==newer
            # A torn inactive metadata record still leaves the confirmed B.
            (store/'slot-0.meta').write_bytes(b'XITA1 123')
            assert lib.xv_update_boot()==1
            # Damaged incoming metadata cannot be installed.
            lib.xv_update_init();stage(newer)
            (store/'incoming.meta').write_bytes(b'XITA1 0 9000')
            assert lib.xv_update_request(0)==0 and lib.xv_update_boot()==1
            print('PASS: SHA-256 differential boundaries; partial/wrong-offset uploads, corrupt content/metadata, failed destination writes, two-slot install, acknowledgement, failed-boot fallback and explicit rollback')
        finally:
            lib.xv_update_close();os.chdir(before)

if __name__=='__main__':main()
