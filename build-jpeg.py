#!/usr/bin/python
import sys
import os
import ftplib
import glob


BASE_ADDRESS = 0x080C3EE0
CC = "arm-none-eabi-gcc"
CP = "arm-none-eabi-g++"
OC = "arm-none-eabi-objcopy" 
LD = "arm-none-eabi-ld"
CTRULIB = '/opt/devkitpro/libctru'
DEVKITARM = '/opt/devkitpro/devkitARM'
LIBPATH = '-L .'
COPY = 'cp'
WARNS = "-Wno-int-conversion -Wno-implicit-function-declaration -Wno-incompatible-pointer-types -Wno-implicit-int -Wno-return-type"

with open('include/gen.h', 'w') as f:
    f.write('#define HAS_JPEG (1)');

def allFile(pattern):
    s = "";
    for file in glob.glob(pattern):
        s += file + " ";
    return s;

def run(cmd):
	os.system(cmd)

cwd = os.getcwd() 
run("rm obj/*.o")
run("rm bin/*.elf")
run(CC+  " -O3 -s " + WARNS + " -g -I include -I include/jpeg -I/opt/devkitpro/portlibs/3ds/include " +  allFile('source/dsp/*.c') +  allFile('source/jpeg/*.c') + allFile('source/ns/*.c') + allFile('source/*.c') + allFile('source/libctru/*.c') + " -c  -march=armv6 -mlittle-endian   -fomit-frame-pointer -ffast-math -march=armv6k -mtune=mpcore -mfloat-abi=hard ");
run(CC+"  -O3 " +  allFile('source/ns/*.s')  + allFile('source/*.s') + allFile('source/libctru/*.s') + " -c -s -march=armv6 -mlittle-endian   -fomit-frame-pointer -ffast-math -march=armv6k -mtune=mpcore -mfloat-abi=hard ");

run(LD + ' ' + LIBPATH + " -g -A armv6k -pie --print-gc-sections  -T 3ds.ld -Map=test.map " + allFile("*.o") + " -lc -lm -lgcc --nostdlib"  )
run("cp -r *.o obj/ ")
run("cp a.out bin/homebrew.elf ")
run(OC+" -O binary a.out payload.bin -S")
run("rm *.o")
run("rm *.out")
# run('copy payload.bin  \\\\3DS-8141\\microSD\\ntr.bin');
run(COPY + ' payload.bin  release/ntr.n3ds.bin');
run(COPY + ' release/ntr.n3ds.bin ../BootNTR-Selector/romfs/');
