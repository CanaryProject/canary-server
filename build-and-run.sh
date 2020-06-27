#!/usr/bin/env bash

cd build
sudo cmake -DPACKAGE_TESTS=Off -DCMAKE_BUILD_TYPE=Release .. ; make
cd ..
rm -rf canary
cp build/bin/canary ./
./canary 