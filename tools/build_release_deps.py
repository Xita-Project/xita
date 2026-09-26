#!/usr/bin/env python3
"""Build pinned HTTPS-only Vita dependencies locally, without changing VitaSDK."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile
import urllib.request

SOURCES = {
    'mbedtls-3.6.7': ('https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2',
                    'a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6'),
    'curl-8.22.0': ('https://curl.se/download/curl-8.22.0.tar.xz',
                  'f7ef3ae8a22e521f289803fe93543eb64c329b58aa73a9e224dfd915a2a5f4f7'),
}


def build(output, sdk):
    output, sdk = output.resolve(), sdk.resolve()
    work = output.parent / (output.name + '-build')
    work.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, VITASDK=str(sdk))
    def run(args):
        print(' '.join(map(str,args)),flush=True)
        subprocess.run(list(map(str,args)),check=True,env=env)
    for name,(url,sha) in SOURCES.items():
        archive=work/url.rsplit('/',1)[1]
        if not archive.exists():
            with urllib.request.urlopen(url,timeout=60) as r:
                archive.write_bytes(r.read(32*1024*1024))
        if hashlib.sha256(archive.read_bytes()).hexdigest()!=sha:
            raise ValueError('Source checksum mismatch: '+name)
        if not (work/name).exists():
            with tarfile.open(archive) as t:t.extractall(work,filter='data')
    source=work/'mbedtls-3.6.7'
    config=source/'include/mbedtls/mbedtls_config.h'
    text=config.read_text()
    for key in ['MBEDTLS_ENTROPY_HARDWARE_ALT','MBEDTLS_NO_PLATFORM_ENTROPY','MBEDTLS_PLATFORM_MS_TIME_ALT']:
        text=text.replace('//#define '+key,'#define '+key)
    for key in ['MBEDTLS_TIMING_C','MBEDTLS_NET_C']:
        text=text.replace('\n#define '+key+'\n','\n//#define '+key+'\n')
    config.write_text(text)
    base=['-DCMAKE_TOOLCHAIN_FILE='+str(sdk/'share/vita.toolchain.cmake'),'-DCMAKE_BUILD_TYPE=Release',
          '-DCMAKE_INSTALL_PREFIX='+str(output)]
    run(['cmake','-S',source,'-B',work/'mbed-build',*base,'-DCMAKE_C_FLAGS=-mcpu=cortex-a9',
         '-DENABLE_TESTING=OFF','-DENABLE_PROGRAMS=OFF'])
    run(['cmake','--build',work/'mbed-build','-j8'])
    run(['cmake','--install',work/'mbed-build'])
    options=['-DBUILD_SHARED_LIBS=OFF','-DBUILD_CURL_EXE=OFF','-DBUILD_TESTING=OFF',
             '-DCURL_USE_OPENSSL=OFF','-DCURL_USE_MBEDTLS=ON','-DCURL_USE_LIBPSL=OFF',
             '-DCURL_ZSTD=OFF','-DCURL_BROTLI=OFF','-DCURL_USE_LIBSSH2=OFF',
             '-DHAVE_PIPE2=0','-DENABLE_IPV6=OFF','-DENABLE_UNIX_SOCKETS=OFF','-DENABLE_THREADED_RESOLVER=OFF','-DCURL_DISABLE_LDAP=ON','-DCURL_DISABLE_LDAPS=ON','-DHTTP_ONLY=ON',
             '-DMBEDTLS_INCLUDE_DIR='+str(output/'include')]
    for key,lib in [('MBEDTLS','mbedtls'),('MBEDX509','mbedx509'),('MBEDCRYPTO','mbedcrypto')]:
        options.append('-D'+key+'_LIBRARY='+str(output/'lib'/('lib'+lib+'.a')))
    run(['cmake','-S',work/'curl-8.22.0','-B',work/'curl-build',*base,*options])
    run(['cmake','--build',work/'curl-build','-j8'])
    run(['cmake','--install',work/'curl-build'])
    (output/'xita-dependencies.json').write_text(json.dumps(SOURCES,indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--sdk',type=Path,default=Path(os.environ.get('VITASDK',str(Path.home()/'vitasdk'))))
    args=p.parse_args();build(args.output,args.sdk)
