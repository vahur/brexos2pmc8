#!/bin/bash

if [[ "$OSTYPE" == "darwin"* ]]; then
    CC=clang
    CXX=clang++
    LDFLAGS="-Wl,-dead_strip"
else
    CC=gcc
    CXX=g++
    LDFLAGS="-Wl,--as-needed"
fi

CFLAGS="-O2 -Isrc"
CXXFLAGS="$CFLAGS -fno-exceptions -fno-rtti -fvisibility=hidden"

if [ ! -d target ]; then
    mkdir target
fi

if [ ! -f target/mongoose.o ]; then
    $CC -c $CFLAGS src/mongoose.c -o target/mongoose.o
fi

$CXX $CXXFLAGS -o target/brexos2pmc8 -lpthread $LDFLAGS src/main.cpp target/mongoose.o
