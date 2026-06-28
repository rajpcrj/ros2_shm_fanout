# examples/ — runnable starter programs

Every file here is a small, self-contained program that uses the core library
(`libshm_bridge_cpp.so`) or the matching Python contract. They are **adapters**: the
core moves bytes through `/dev/shm`; each example plugs one outside world (ROS 2, a
UDP socket, a file) into that core. See [../docs/06_modular_core.md](../docs/06_modular_core.md).

> All C++ examples are wired into the package as `ex_*` / `e2e_*` targets, so after
> `colcon build` you can `ros2 run shm_bridge_cpp <name>`. They also build standalone
> with `g++` once you've run `../install_lib.sh` (see [../docs/03_build_and_install.md](../docs/03_build_and_install.md)).

---

## Folder map

```
examples/
├── cpp/                 C++ examples
├── python/              Python examples (same /dev/shm contract, no .so linking)
├── sample_data/         a real sample image (PNG + raw .bin) for the no-ROS demos
├── end_to_end/          the 4-stage cross-machine pipeline (see its own README)
└── README.md            (this file)
```

---

## C++ examples (`cpp/`)

| file | what it does | needs ROS? | run as |
|---|---|---|---|
| `write_cpp.cpp` | load `sample_data/` image, publish to `/dev/shm/demo_*` | no | `ex_write_cpp` |
| `read_cpp.cpp` | attach to `/dev/shm/demo_*`, print each frame | no | `ex_read_cpp` |
| `ros2_to_shm.cpp` | subscribe ONE ROS topic → `/dev/shm` | yes | `ex_ros2_to_shm` |
| `ros2_all_to_shm.cpp` | subscribe **every** ROS topic → `/dev/shm` (CDR, any type) | yes | `ex_ros2_all_to_shm` |
| `shm_to_ros2.cpp` | `/dev/shm` → re-publish as a ROS topic | yes | `ex_shm_to_ros2` |
| `shm_to_network.cpp` | `/dev/shm` → fire-and-forget UDP to one host | no | `ex_shm_to_network` |
| `shm_to_udp.cpp` | `/dev/shm` → UDP **server**, streams raw memory to many clients | no | `ex_shm_to_udp` |

### How `write_cpp` / `read_cpp` work (the no-ROS demo)
`write_cpp` opens a shared-memory **stream** named `demo` (three files appear:
`/dev/shm/demo_header`, `demo_frame`, `demo_recipe.json`), loads the sample image
from `sample_data/sample_640x480_rgb.bin` (640×480 raw RGB = 921 600 bytes), and
calls `Writer::write_flat(...)` ~30×/s. `read_cpp` opens the same stream and calls
`Reader::wait_and_read(...)`, which sleeps at ~0 % CPU on a futex until a new frame
arrives, then prints its shape/size. That's the whole transport: one copy into shared
memory, every reader maps the same bytes.

```bash
./install_lib.sh                       # once: copy the .so + headers to /usr/local
cd examples/cpp
g++ -std=c++17 write_cpp.cpp -o write_cpp -lshm_bridge_cpp -lpthread
g++ -std=c++17 read_cpp.cpp  -o read_cpp  -lshm_bridge_cpp -lpthread
cd ..                                  # so the relative sample path resolves
cpp/write_cpp sample_data/sample_640x480_rgb.bin 640 480 &   # terminal 1
cpp/read_cpp                                                  # terminal 2
# -> seq=92  640x480x3  921600 bytes  type=sensor_msgs/msg/Image  first byte=0
```

### How `ros2_all_to_shm` works
It periodically lists the ROS graph (`get_topic_names_and_types()`), and for each new
topic creates a **generic** subscription (works for ANY message type without knowing
it at compile time) plus a `Writer`. Each incoming message's serialized CDR bytes go
straight into a per-topic stream (`/camera/image_raw` → `/dev/shm/camera_image_raw_*`).
Because it ships raw CDR, a matching reader can re-emit the exact same messages
elsewhere — which is exactly what the `end_to_end/` pipeline does.

---

## Python examples (`python/`)

| file | what it does |
|---|---|
| `write_py.py` | publish frames to a `/dev/shm` stream (same contract as C++) |
| `read_py.py` | read a `/dev/shm` stream (reads C++ OR Python writers) |
| `network_to_shm.py` | receive the `shm_to_network` UDP stream → local `/dev/shm` |
| `udp_client.py` | subscribe to a `shm_to_udp` server and receive frames |

Python does **not** link the `.so`; it talks to the same `/dev/shm` files via
`shm_bridge_python`. So C++ and Python freely read each other's streams (verified).
First: `export PYTHONPATH=<repo>/src/shm_bridge_python:$PYTHONPATH`. Details and the
"can Python call the .so directly?" answer: [../docs/05_python_and_cpp_interop.md](../docs/05_python_and_cpp_interop.md).

```bash
python3 python/write_py.py demo &     # Python writer
python3 python/read_py.py  demo       # Python reader  (or use the C++ read_cpp!)
```

---

## sample_data/
- `sample_640x480.png` — a viewable 640×480 RGB gradient (open it to see it).
- `sample_640x480_rgb.bin` — the same image as **raw** width×height×3 bytes
  (921 600 bytes), which `write_cpp` loads directly. Regenerate both with the snippet
  in this repo's history, or any tool that writes row-major RGB bytes.

---

## end_to_end/
A complete 4-stage pipeline that takes **all** ROS topics on machine A, ships them
over UDP, and reconstructs them as identical ROS topics on machine B. It has its own
[end_to_end/README.md](end_to_end/README.md) including the ssh / two-machine notes and
a localhost test you can run right now.
