#!/bin/sh
# Builds and runs the offline DSP test from the plugin root.
set -e
cd "$(dirname "$0")/.."
mkdir -p build
clang++ -std=c++17 -O2 -Wall -Wextra -o build/render-test tests/render.cpp
./build/render-test
