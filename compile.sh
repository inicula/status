#!/bin/sh

set -xe

CXXFLAGS="-std=c++20 -O3 -flto -fno-exceptions -fno-rtti -march=native -Wall -Wextra -Wpedantic -Wconversion"
CXX=g++

$CXX $CXXFLAGS "$@" statusd.cpp -o statusd -lX11
$CXX $CXXFLAGS "$@" status.cpp -o status
