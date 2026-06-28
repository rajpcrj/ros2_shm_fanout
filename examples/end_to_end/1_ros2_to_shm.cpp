// STAGE 1 of the end-to-end pipeline:  ALL ROS 2 topics  ->  /dev/shm
//
//   [ROS 2 graph] --(this)--> /dev/shm/<stream>_*  --> stage 2 (shm -> udp)
//
// Discovers every topic, generic-subscribes each (any type), and writes the raw CDR
// bytes into a per-topic shared-memory stream. Also maintains a small registry file
// /dev/shm/e2e_streams.txt mapping  stream_id  stream_name  topic  type  so the UDP
// stage knows what to ship and the far end knows what to re-publish.
//
// Run on the SOURCE machine:
//   ros2 run shm_bridge_cpp e2e_1_ros2_to_shm
#include <shm_bridge_cpp/shm_bridge.hpp>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialized_message.hpp>

#include <fstream>
#include <map>
#include <memory>
#include <string>

static std::string topic_to_stream(const std::string& topic) {
    std::string s = topic;
    if (!s.empty() && s.front() == '/') s.erase(s.begin());
    for (auto& c : s) if (c == '/') c = '_';
    return s.empty() ? "root" : s;
}

class Stage1 : public rclcpp::Node {
public:
    Stage1() : Node("e2e_1_ros2_to_shm") {
        max_bytes_ = static_cast<size_t>(declare_parameter<int>("max_bytes", 8 * 1024 * 1024));
        scan();
        timer_ = create_wall_timer(std::chrono::seconds(2), [this] { scan(); });
        RCLCPP_INFO(get_logger(), "stage1 up: ALL topics -> /dev/shm, registry "
                                  "/dev/shm/e2e_streams.txt");
    }

private:
    struct Stream {
        uint32_t id;
        std::string topic, type;
        std::shared_ptr<shm_bridge::Writer> writer;
        rclcpp::GenericSubscription::SharedPtr sub;
    };

    void scan() {
        for (auto& [topic, types] : get_topic_names_and_types()) {
            if (types.empty() || streams_.count(topic)) continue;
            const std::string type = types.front();
            const std::string sname = topic_to_stream(topic);
            auto st = std::make_shared<Stream>();
            st->id = next_id_++; st->topic = topic; st->type = type;
            try { st->writer = std::make_shared<shm_bridge::Writer>(sname, max_bytes_); }
            catch (const std::exception& e) {
                RCLCPP_WARN(get_logger(), "skip %s: %s", topic.c_str(), e.what());
                next_id_--; continue;
            }
            st->sub = create_generic_subscription(
                topic, type, rclcpp::QoS(1),
                [this, st, type, topic](std::shared_ptr<rclcpp::SerializedMessage> m) {
                    const auto& rcl = m->get_rcl_serialized_message();
                    st->writer->write_cdr(rcl.buffer, rcl.buffer_length, type, topic);
                });
            streams_[topic] = st;
            write_registry();
            RCLCPP_INFO(get_logger(), "+ [id %u] %s (%s) -> /dev/shm/%s_*",
                        st->id, topic.c_str(), type.c_str(), sname.c_str());
        }
    }

    void write_registry() {
        std::ofstream f("/dev/shm/e2e_streams.txt", std::ios::trunc);
        for (auto& [topic, st] : streams_)
            f << st->id << '\t' << topic_to_stream(topic) << '\t'
              << st->topic << '\t' << st->type << '\n';
    }

    size_t max_bytes_;
    uint32_t next_id_ = 0;
    std::map<std::string, std::shared_ptr<Stream>> streams_;
    rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<Stage1>());
    rclcpp::shutdown();
    return 0;
}
