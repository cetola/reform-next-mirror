#!/bin/bash

export PICO_SDK_PATH=/usr/src/pico-sdk
export PICO_EXTRAS_FETCH_FROM_GIT=1

mkdir -p build
cd build
cmake -DPICO_BOARD=pico2 ..

make
