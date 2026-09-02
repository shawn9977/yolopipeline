#!/bin/bash
source ../../scripts/setva.sh

mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cp query_device ../
