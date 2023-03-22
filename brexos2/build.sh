#!/bin/bash

if [ ! -d target ]; then
    mkdir target
fi

g++ -DDEBUG -O2 -fno-exceptions -fno-rtti -fvisibility=hidden -o target/brexos2 -Isrc -lreadline -lpthread -Wl,--as-needed src/main.cpp

