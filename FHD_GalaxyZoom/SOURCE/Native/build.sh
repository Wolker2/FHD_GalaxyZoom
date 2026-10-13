#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
gcc -m32 -std=c11 -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -fno-pic -fno-pie -c galaxy_zoom.c -o galaxy_zoom.o
python3 elf_to_coff.py galaxy_zoom.o galaxy_zoom.coff
ld -m i386pe --no-leading-underscore --shared --entry DllMain --subsystem console --image-base 0x13600000 --disable-auto-import --no-insert-timestamp galaxy_zoom.coff galaxy_zoom.def -o ../../Native/FHDGalaxyZoom.dll
