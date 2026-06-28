#!/usr/bin/env bash
# install_lib.sh — make libshm_bridge_cpp usable system-wide so any project can
# just do  #include <shm_bridge_cpp/shm_bridge.hpp>  and  -lshm_bridge_cpp
# WITHOUT sourcing a ROS 2 workspace.
#
# It copies the prebuilt shared object + public headers into a standard prefix
# (default /usr/local) and refreshes the linker cache.
#
# Usage:
#   ./install_lib.sh                 # install to /usr/local  (needs sudo)
#   PREFIX=$HOME/.local ./install_lib.sh   # user-local, no sudo
#   ./install_lib.sh --uninstall     # remove what was installed
#
# After install, build any program with:
#   g++ -std=c++17 my.cpp -o my -lshm_bridge_cpp -lpthread
# (If you used a non-standard PREFIX, add -I$PREFIX/include -L$PREFIX/lib and set
#  LD_LIBRARY_PATH=$PREFIX/lib at runtime.)
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${PREFIX:-/usr/local}"
# ALWAYS install from the committed prebuilt/ folder. This is the single source of
# truth that ships on GitHub, so the installer behaves identically on a fresh clone
# (no build/ or install/ needed). To refresh prebuilt/ after changing the library,
# rebuild and copy the .so+headers into prebuilt/ (see prebuilt/README.md).
SO_SRC="$REPO/prebuilt/lib/libshm_bridge_cpp.so"
INC_SRC="$REPO/prebuilt/include/shm_bridge_cpp"

# Decide whether sudo is needed: walk up to the first existing ancestor of PREFIX
# and test if WE can write there. (A non-existent PREFIX under $HOME must NOT
# trigger sudo just because the leaf dir doesn't exist yet.)
SUDO=""
probe="$PREFIX"
while [ ! -e "$probe" ]; do probe="$(dirname "$probe")"; done
[ -w "$probe" ] || SUDO="sudo"

if [ "${1:-}" = "--uninstall" ]; then
  echo "removing shm_bridge_cpp from $PREFIX"
  $SUDO rm -f  "$PREFIX/lib/libshm_bridge_cpp.so"
  $SUDO rm -rf "$PREFIX/include/shm_bridge_cpp"
  $SUDO ldconfig 2>/dev/null || true
  echo "done."
  exit 0
fi

if [ ! -f "$SO_SRC" ]; then
  echo "ERROR: $SO_SRC not found." >&2
  echo "  The prebuilt library is missing. Rebuild and refresh prebuilt/:" >&2
  echo "    cd $REPO && colcon build --packages-select shm_bridge_cpp" >&2
  echo "    cp install/shm_bridge_cpp/lib/libshm_bridge_cpp.so prebuilt/lib/" >&2
  echo "    cp src/shm_bridge_cpp/include/shm_bridge_cpp/*.hpp prebuilt/include/shm_bridge_cpp/" >&2
  exit 1
fi

echo "installing shm_bridge_cpp -> $PREFIX"
echo "  .so      : $SO_SRC"
echo "  headers  : $INC_SRC/*.hpp"

$SUDO install -d "$PREFIX/lib" "$PREFIX/include/shm_bridge_cpp"
$SUDO install -m 0644 "$SO_SRC" "$PREFIX/lib/libshm_bridge_cpp.so"
$SUDO install -m 0644 "$INC_SRC"/*.hpp "$PREFIX/include/shm_bridge_cpp/"

# refresh linker cache so -lshm_bridge_cpp resolves immediately (only for /usr*).
if [ "$PREFIX" = "/usr/local" ] || [ "$PREFIX" = "/usr" ]; then
  $SUDO ldconfig
fi

echo
echo "installed. Quick test:"
echo "  g++ -std=c++17 $REPO/examples/cpp/read_cpp.cpp -o /tmp/read_cpp -lshm_bridge_cpp -lpthread && echo BUILD_OK"
