#!/bin/sh
# Builds build/breeze.elf. Needs ps5-payload-sdk; set PS5_PAYLOAD_SDK or it
# defaults to /opt/ps5-payload-sdk.
set -e
cd "$(dirname "$0")"
: "${PS5_PAYLOAD_SDK:=/opt/ps5-payload-sdk}"
export PS5_PAYLOAD_SDK

if [ ! -f build/build.ninja ]; then
    cmake -S . -B build -G Ninja \
        -DPS5_PAYLOAD_SDK="$PS5_PAYLOAD_SDK" \
        -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake"
fi
cmake --build build
ls -l build/breeze.elf
