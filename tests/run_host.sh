#!/bin/sh
set -eu
gb_root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$gb_root/tests/.build"
gb_cc=${CC:-cc}
gb_cxx=${CXX:-c++}
for gb_unit in peanut_core minigb_apu; do
  "$gb_cc" -std=c99 -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -c "$gb_root/src/vendor/$gb_unit.c" -o "$gb_root/tests/.build/$gb_unit.o"
done
"$gb_cxx" -std=c++17 -O2 -g -Wall -Wextra -Werror -Wno-misleading-indentation \
  -fsanitize=address,undefined -fno-omit-frame-pointer -pthread \
  "$gb_root/tests/core.cpp" "$gb_root/src/core/Cartridge.cpp" "$gb_root/src/core/GameBoy.cpp" \
  "$gb_root/src/platform/GameFiles.cpp" "$gb_root/src/platform/Settings.cpp" \
  "$gb_root/src/platform/SaveStore.cpp" "$gb_root/tests/.build/peanut_core.o" \
  "$gb_root/tests/.build/minigb_apu.o" -o "$gb_root/tests/.build/core"
"$gb_root/tests/.build/core" "$gb_root/tests/.build" "$@"
