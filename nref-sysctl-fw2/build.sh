#!/bin/bash

export PICO_SDK_PATH=/usr/src/pico-sdk
if [ -z "$MNTRE_FIRMWARE_VERSION" ]; then
    export MNTRE_FIRMWARE_VERSION=$(date +%Y%m%d)
fi

mkdir -p build
cd build
cmake ..

make
