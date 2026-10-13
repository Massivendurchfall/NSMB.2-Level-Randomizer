#!/bin/bash
# Compiles a copy of the plugin sources in build_host/plugin_check, so a build can be tried
# while the emulator test runs still use plugin/nsmb2rando.3gx.
#   tools/build_check.sh [test]
set -e
source /etc/profile.d/devkit-env.sh
cd "$(dirname "$0")/.."
rm -rf build_host/plugin_check
mkdir -p build_host/plugin_check
cp -r plugin/Sources plugin/Includes plugin/Makefile plugin/CTRPluginFramework.plgInfo plugin/3gx.ld build_host/plugin_check/
cd build_host/plugin_check
if [ "$1" = test ]; then
    make -j4 TESTBUILD=1
else
    make -j4
fi
ls -l ./*.3gx
