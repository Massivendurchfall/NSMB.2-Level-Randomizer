#!/bin/bash
# Builds the plugin. Run it from a devkitPro MSYS2 shell:
#   tools/build.sh [test]
# "test" builds the variant with the emulator self test (see plugin/Sources/TestMode.cpp).
set -e
source /etc/profile.d/devkit-env.sh
cd "$(dirname "$0")/../plugin"
# both variants share the build folder, so always start clean
make clean > /dev/null
if [ "$1" = test ]; then
    make -j4 TESTBUILD=1
else
    make -j4
fi
ls -l ./*.3gx
