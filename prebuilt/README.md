# prebuilt/ — ship-ready library, no build needed

This folder contains a **prebuilt** copy of the core library so the repo is usable
straight from a GitHub clone **without** the `build/` or `install/` directories
(which are git-ignored).

```
prebuilt/
├── lib/libshm_bridge_cpp.so        the compiled core library
└── include/shm_bridge_cpp/*.hpp    the public headers
```

## Use it directly (no colcon, no ROS)

```bash
# compile any program against the prebuilt artifacts:
g++ -std=c++17 my.cpp -o my \
    -I prebuilt/include -L prebuilt/lib -lshm_bridge_cpp -lpthread
# at run time, point the loader at the .so:
export LD_LIBRARY_PATH=$PWD/prebuilt/lib:$LD_LIBRARY_PATH
./my
```

Or install these system-wide in one step. **`install_lib.sh` always installs from
this `prebuilt/` folder** — it is the single source of truth, so the installer behaves
identically on a fresh clone with no `build/`/`install/`:
```bash
./install_lib.sh        # copies prebuilt/lib + prebuilt/include into /usr/local
```

## IMPORTANT: architecture

The `.so` here was built for **x86-64 / Linux** (check with `file
prebuilt/lib/libshm_bridge_cpp.so`). A shared object is **architecture-specific** —
it will NOT load on a different arch (e.g. ARM64 / Jetson / Raspberry Pi). The
**source is portable**; on a different machine, rebuild and refresh this folder:

```bash
colcon build --packages-select shm_bridge_cpp
cp install/shm_bridge_cpp/lib/libshm_bridge_cpp.so   prebuilt/lib/
cp src/shm_bridge_cpp/include/shm_bridge_cpp/*.hpp   prebuilt/include/shm_bridge_cpp/
```

The `/dev/shm` byte layout is little-endian and works across x86-64 and arm64, so a
stream written on one arch is readable on another — only the `.so` itself is per-arch.
