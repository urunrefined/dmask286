#!/bin/sh

make clean
CXX=clang++ CXXEXTFLAGS="-MJ compile_commands.json.temp" make -j1 dmask286
echo "[" > compile_commands.json
cat compile_commands.json.temp >> compile_commands.json
echo "]" >> compile_commands.json

clang-tidy --quiet dmask.cpp File.cpp
