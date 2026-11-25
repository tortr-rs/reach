#!/bin/sh
# builds reach with clang and lld on c.h, no libc under it. elsewhere: cc -DLIBC -O2 -o reach reach.c
set -e
cd "$(dirname "$0")"
[ "$1" = 32 ] && M32="-m32 -msse2"   # ./build.sh 32: one i386 binary, runs on 32 and 64-bit x86
${CC:-cc} -std=gnu11 -O2 -Wall -Wextra -Wno-missing-field-initializers -Wno-deprecated-non-prototype -Wno-unused-parameter -Wno-empty-body -Wno-comment \
    $M32 -static -nostdlib -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables \
    -fno-pie -ffunction-sections -fdata-sections -fuse-ld=lld -Wl,--gc-sections -Wl,-s -Wl,-z,norelro \
    -o reach reach.c
