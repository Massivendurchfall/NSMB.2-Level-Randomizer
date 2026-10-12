#!/bin/bash
# Builds the host test of the randomizer core and runs it over every course of the game.
# Needs build_host/courses.blob, created with:
#   python tools/make_blob.py game_dump/romfs/Course build_host/courses.blob
set -e
cd "$(dirname "$0")/.."
mkdir -p build_host
g++ -std=gnu++11 -O2 -Wall -Wextra -Wno-unused-parameter -I plugin/Includes \
    tools/host_test.cpp plugin/Sources/Randomizer.cpp -o build_host/host_test.exe
build_host/host_test.exe build_host/courses.blob
