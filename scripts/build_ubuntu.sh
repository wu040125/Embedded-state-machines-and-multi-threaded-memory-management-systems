#!/usr/bin/env bash
set -euo pipefail

cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug
