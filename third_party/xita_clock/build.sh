#!/bin/sh
# Build xita_clock.skprx (kernel module) and the user-side stub library libXitaClock_stub.a.
set -e; cd "$(dirname "$0")"; SDK=${VITASDK:-$HOME/vitasdk}
$SDK/bin/arm-vita-eabi-gcc -mcpu=cortex-a9 -mthumb-interwork -O2 -Wall -Wno-attribute-alias -nostdlib -nostartfiles -fno-builtin -static -Wl,-q -o xita_clock.elf xita_clock.c -lScePowerForDriver_stub -lSceSysmemForDriver_stub -lSceModulemgrForKernel_stub -lSceThreadmgrForDriver_stub -lSceProcessmgrForDriver_stub
$SDK/bin/vita-elf-create -e exports.yml xita_clock.elf xita_clock.velf
$SDK/bin/vita-make-fself -c xita_clock.velf xita_clock.skprx
$SDK/bin/vita-elf-create -g gen.yml xita_clock.elf /tmp/xita_clock_gen.velf >/dev/null 2>&1 || true
rm -rf stubs && mkdir stubs && $SDK/bin/vita-libs-gen stub.yml stubs && (cd stubs && make -s) && cp stubs/libXitaClock2_stub.a stubs/libXitaClock2_stub_weak.a . && rm -rf stubs xita_clock.elf xita_clock.velf
ls -la xita_clock.skprx libXitaClock2_stub.a
