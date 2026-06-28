// write_cpp.cpp — the absolute minimum to WRITE an IMAGE to shared memory in C++.
// NO ROS. Just the library.
//
// It loads a real sample image (examples/sample_data/sample_640x480_rgb.bin — a
// plain file of width*height*3 raw RGB bytes) and publishes it into shared memory
// over and over. A reader (read_cpp / read_py) picks it up with ~0% CPU.
//
// HOW TO BUILD (two ways — see the README Quickstart for the beginner walk-through):
//   (1) after ./install_lib.sh copied the library into /usr/local:
//         g++ -std=c++17 write_cpp.cpp -o write_cpp -lshm_bridge_cpp -lpthread
//   (2) without installing, pointing g++ straight at the repo:
//         g++ -std=c++17 write_cpp.cpp -o write_cpp \
//             -I../../src/shm_bridge_cpp/include \
//             -L../../install/shm_bridge_cpp/lib -lshm_bridge_cpp -lpthread
//         (then at run time: export LD_LIBRARY_PATH=../../install/shm_bridge_cpp/lib)
//
// RUN:  ./write_cpp [path-to-rgb.bin] [width] [height]
//   defaults: examples/sample_data/sample_640x480_rgb.bin  640  480
#include <shm_bridge_cpp/shm_bridge.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    // 1) Decide which image to send and its shape.
    const char* path = argc > 1 ? argv[1] : "sample_data/sample_640x480_rgb.bin";
    const int W = argc > 2 ? std::atoi(argv[2]) : 640;
    const int H = argc > 3 ? std::atoi(argv[3]) : 480;
    const int CH = 3;                      // RGB
    const size_t bytes = (size_t)W * H * CH;

    // 2) Load the raw image bytes from disk into memory.
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); return 1; }
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(f)), {});
    if (image.size() < bytes) {
        std::fprintf(stderr, "file %s is %zu B but %dx%dx3 needs %zu B\n",
                     path, image.size(), W, H, bytes);
        return 1;
    }
    std::printf("[write_cpp] loaded %s (%zu bytes) -> publishing to /dev/shm/demo_*\n",
                path, image.size());

    // 3) Create the shared-memory stream named "demo".
    shm_bridge::Writer w("demo", bytes);

    // 4) Publish the image ~30 times a second, forever (Ctrl-C to stop).
    while (true) {
        // write_flat copies the bytes once into shared memory + records the shape.
        // Args: ptr, len, width, height, channels, dtype, ros-type-name, topic.
        w.write_flat(image.data(), bytes, W, H, CH, shm_bridge::DType::U8,
                     "sensor_msgs/msg/Image", "/demo");
        std::this_thread::sleep_for(std::chrono::milliseconds(33));   // ~30 Hz
    }
}
