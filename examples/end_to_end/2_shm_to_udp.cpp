// STAGE 2 of the end-to-end pipeline:  /dev/shm  ->  UDP network
//
//   /dev/shm/<stream>_* --(this)--> UDP datagrams --> [network] --> stage 3
//
// Reads the registry written by stage 1 (/dev/shm/e2e_streams.txt), attaches a
// shm_bridge::Reader to every stream, and ships each new CDR frame to the
// destination host:port. Fragment 0 of each frame carries the topic + type name
// (see e2e_wire.hpp) so the receiver is fully self-describing — no shared config.
//
// One reader thread per stream; each blocks 0% CPU on the futex until a new frame.
//
// Run on the SOURCE machine (after stage 1):
//   e2e_2_shm_to_udp --host <DEST_IP> --port 7000
#include <shm_bridge_cpp/shm_bridge.hpp>
#include "e2e_wire.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

struct Entry { uint32_t id; std::string stream, topic, type; };

static std::vector<Entry> read_registry() {
    std::vector<Entry> v;
    std::ifstream f("/dev/shm/e2e_streams.txt");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        Entry e; std::string id;
        if (std::getline(ss, id, '\t') && std::getline(ss, e.stream, '\t') &&
            std::getline(ss, e.topic, '\t') && std::getline(ss, e.type)) {
            e.id = static_cast<uint32_t>(std::stoul(id));
            v.push_back(e);
        }
    }
    return v;
}

int main(int argc, char** argv) {
    std::string host = "127.0.0.1"; int port = 7000;
    for (int i = 1; i < argc - 1; ++i) {
        std::string a = argv[i];
        if (a == "--host") host = argv[++i];
        else if (a == "--port") port = std::atoi(argv[++i]);
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in dst{}; dst.sin_family = AF_INET; dst.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, host.c_str(), &dst.sin_addr);
    std::printf("stage2: /dev/shm/* -> udp://%s:%d\n", host.c_str(), port);

    // wait for stage 1's registry to exist
    std::vector<Entry> entries;
    while ((entries = read_registry()).empty()) {
        std::printf("  waiting for /dev/shm/e2e_streams.txt (start stage 1) ...\n");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::atomic<bool> run{true};
    std::vector<std::thread> threads;
    for (const Entry& e : entries) {
        threads.emplace_back([&, e]() {
            shm_bridge::Reader reader(e.stream);
            shm_bridge::Frame f;
            // metadata block "topic\0type\0" — sent in fragment 0 of every frame
            std::string meta = e.topic; meta.push_back('\0');
            meta += e.type; meta.push_back('\0');
            std::vector<uint8_t> pkt(sizeof(E2EHdr) + meta.size() + E2E_MTU_PAYLOAD);
            while (run.load()) {
                if (!reader.wait_and_read(f, 200ull * 1000 * 1000)) continue;
                const size_t total = f.data.size();
                // frag 0 budget is smaller because it also carries the metadata
                const size_t budget0 = E2E_MTU_PAYLOAD > meta.size()
                                       ? E2E_MTU_PAYLOAD - meta.size() : 1;
                // compute fragment count: frag0 holds budget0, rest hold MTU each
                size_t nfrags = 1;
                if (total > budget0)
                    nfrags += (total - budget0 + E2E_MTU_PAYLOAD - 1) / E2E_MTU_PAYLOAD;
                size_t off = 0;
                for (uint32_t fi = 0; fi < nfrags; ++fi) {
                    const bool first = (fi == 0);
                    const size_t cap = first ? budget0 : E2E_MTU_PAYLOAD;
                    const size_t len = std::min(cap, total - off);
                    E2EHdr h{};
                    h.magic = E2E_MAGIC; h.seq = f.seq; h.frag = fi;
                    h.nfrags = (uint32_t)nfrags; h.total_len = (uint32_t)total;
                    h.meta_len = first ? (uint32_t)meta.size() : 0;
                    h.stream_id = e.id; h.encoding = 1 /*CDR*/;
                    h.width = f.width; h.height = f.height;
                    size_t p = 0;
                    std::memcpy(pkt.data() + p, &h, sizeof(h)); p += sizeof(h);
                    if (first) { std::memcpy(pkt.data() + p, meta.data(), meta.size());
                                 p += meta.size(); }
                    std::memcpy(pkt.data() + p, f.data.data() + off, len); p += len;
                    sendto(fd, pkt.data(), p, 0,
                           reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
                    off += len;
                }
            }
        });
    }
    std::printf("stage2: streaming %zu streams. Ctrl-C to stop.\n", entries.size());
    for (auto& t : threads) t.join();
    close(fd);
    return 0;
}
