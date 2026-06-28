// STAGE 3 of the end-to-end pipeline:  UDP network  ->  /dev/shm (on the FAR host)
//
//   [network] --> UDP datagrams --(this)--> /dev/shm/<stream>_* --> stage 4
//
// Binds a UDP port, reassembles fragmented frames (per stream_id + seq), reads the
// topic+type metadata from fragment 0, and writes each completed CDR frame into a
// LOCAL shared-memory stream via shm_bridge::Writer::write_cdr(). Also maintains a
// local registry (/dev/shm/e2e_streams.txt) so stage 4 knows what to re-publish.
//
// Run on the DESTINATION machine:
//   e2e_3_udp_to_shm --port 7000
#include <shm_bridge_cpp/shm_bridge.hpp>
#include "e2e_wire.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct StreamState {
    std::string stream, topic, type;
    std::shared_ptr<shm_bridge::Writer> writer;
    uint32_t cur_seq = 0xFFFFFFFFu;
    std::map<uint32_t, std::vector<uint8_t>> frags;   // frag idx -> bytes
    uint32_t nfrags = 0, total_len = 0, width = 0, height = 0;
};

static std::string topic_to_stream(const std::string& topic) {
    std::string s = topic;
    if (!s.empty() && s.front() == '/') s.erase(s.begin());
    for (auto& c : s) if (c == '/') c = '_';
    return s.empty() ? "root" : s;
}

int main(int argc, char** argv) {
    int port = 7000; size_t max_bytes = 8 * 1024 * 1024;
    for (int i = 1; i < argc - 1; ++i) {
        std::string a = argv[i];
        if (a == "--port") port = std::atoi(argv[++i]);
        else if (a == "--max_bytes") max_bytes = (size_t)std::atoll(argv[++i]);
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    int rb = 16 << 20; setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rb, sizeof(rb));
    sockaddr_in me{}; me.sin_family = AF_INET; me.sin_addr.s_addr = htonl(INADDR_ANY);
    me.sin_port = htons((uint16_t)port);
    if (bind(fd, reinterpret_cast<sockaddr*>(&me), sizeof(me)) < 0) { perror("bind"); return 1; }
    std::printf("stage3: udp://0.0.0.0:%d -> /dev/shm/* (+ registry)\n", port);

    std::map<uint32_t, StreamState> streams;   // by stream_id
    std::vector<uint8_t> buf(65535);

    auto write_registry = [&]() {
        std::ofstream f("/dev/shm/e2e_streams.txt", std::ios::trunc);
        for (auto& [id, s] : streams)
            f << id << '\t' << s.stream << '\t' << s.topic << '\t' << s.type << '\n';
    };

    while (true) {
        ssize_t n = recvfrom(fd, buf.data(), buf.size(), 0, nullptr, nullptr);
        if (n < (ssize_t)sizeof(E2EHdr)) continue;
        E2EHdr h; std::memcpy(&h, buf.data(), sizeof(h));
        if (h.magic != E2E_MAGIC) continue;

        StreamState& st = streams[h.stream_id];
        size_t p = sizeof(E2EHdr);
        if (h.frag == 0 && h.meta_len > 0) {            // parse metadata "topic\0type\0"
            const char* mp = reinterpret_cast<const char*>(buf.data() + p);
            std::string topic(mp);
            std::string type(mp + topic.size() + 1);
            p += h.meta_len;
            if (st.writer == nullptr) {                 // first time we see this stream
                st.topic = topic; st.type = type; st.stream = topic_to_stream(topic);
                try { st.writer = std::make_shared<shm_bridge::Writer>(st.stream, max_bytes); }
                catch (const std::exception& e) { std::fprintf(stderr, "writer %s: %s\n",
                                                  st.stream.c_str(), e.what()); continue; }
                write_registry();
                std::printf("+ [id %u] %s (%s) -> /dev/shm/%s_*\n",
                            h.stream_id, topic.c_str(), type.c_str(), st.stream.c_str());
            }
        }
        if (st.writer == nullptr) continue;             // haven't seen frag0 yet; skip

        if (h.seq != st.cur_seq) {                      // new frame
            st.cur_seq = h.seq; st.frags.clear();
            st.nfrags = h.nfrags; st.total_len = h.total_len;
            st.width = h.width; st.height = h.height;
        }
        st.frags[h.frag].assign(buf.data() + p, buf.data() + n);
        if (st.frags.size() != st.nfrags) continue;     // wait for all fragments

        std::vector<uint8_t> frame; frame.reserve(st.total_len);
        for (uint32_t i = 0; i < st.nfrags; ++i) {
            auto it = st.frags.find(i);
            if (it == st.frags.end()) { frame.clear(); break; }  // missing frag -> drop
            frame.insert(frame.end(), it->second.begin(), it->second.end());
        }
        if (frame.size() == st.total_len)
            st.writer->write_cdr(frame.data(), frame.size(), st.type, st.topic);
        st.cur_seq = 0xFFFFFFFFu;                        // ready for next frame
    }
    close(fd);
    return 0;
}
