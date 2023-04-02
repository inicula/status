#!/bin/sh

CXXFLAGS="-std=c++20 -O3 -flto -fno-exceptions -fno-rtti -march=native -Wall -Wextra -Wpedantic"

g++ $CXXFLAGS "$@" statusd.cpp -o statusd -lX11
g++ $CXXFLAGS "$@" status.cpp -o status
