#!/bin/bash

if [ ! -d target ]; then
    mkdir target
fi

g++ -O2 -fno-exceptions -fno-rtti -fvisibility=hidden -o target/brexos2pmc8 -Isrc -lpthread -Wl,--as-needed src/main.cpp

