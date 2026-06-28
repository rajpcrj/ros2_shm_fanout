// ros2_all_to_shm.cpp — bridge EVERY ROS 2 topic into /dev/shm (CDR, universal).
//
// Discovers all topics on the ROS graph, creates a GenericSubscription for each
// (so it works for ANY message type without compile-time knowledge), and writes the
// serialized CDR bytes of every message into a per-topic shared-memory stream via
// shm_bridge::Writer::write_cdr(). New topics that appear later are picked up by a
// periodic rescan.
//
// Stream naming: a topic "/camera/image_raw" -> stream "camera__image_raw"
// (slashes -> "__", leading slash dropped) so it's a legal /dev/shm filename.
//
// This is the "fan-in" front end of the end-to-end pipeline
// (see examples/end_to_end/). It is type-AGNOSTIC: it ships raw CDR, so a matching
// reader can re-publish the exact same messages elsewhere.
//
// Run:
//   ros2 run shm_bridge_cpp ex_ros2_all_to_shm
//   ros2 run shm_bridge_cpp ex_ros2_all_to_shm --ros-args -p max_bytes:=8388608 -p rescan_sec:=2.0
#include <shm_bridge_cpp/shm_bridge.hpp>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialized_message.hpp>

#include <map>
#include <memory>
#include <string>

static std::string topic_to_stream(const std::string& topic) {
    std::string s = topic;
    if (!s.empty() && s.front() == '/') s.erase(s.begin());
    for (auto& c : s) if (c == '/') c = '_';        // each '/' -> '_', so "//" -> "__"
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {         // collapse the doubled separators
        out.push_back(s[i]);
    }
    return out.empty() ? "root" : out;
}

class Ros2AllToShm : public rclcpp::Node {
public:
    Ros2AllToShm() : Node("ros2_all_to_shm") {
        max_bytes_ = static_cast<size_t>(declare_parameter<int>("max_bytes", 8 * 1024 * 1024));
        double rescan = declare_parameter<double>("rescan_sec", 2.0);
        RCLCPP_INFO(get_logger(), "bridging ALL topics -> /dev/shm/* (cap=%zu B/topic)",
                    max_bytes_);
        scan();   // initial
        timer_ = create_wall_timer(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::duration<double>(rescan)),
            [this] { scan(); });
    }

private:
    struct Stream {
        std::string type_name;
        std::shared_ptr<shm_bridge::Writer> writer;
        rclcpp::GenericSubscription::SharedPtr sub;
    };

    void scan() {
        auto names = get_topic_names_and_types();
        for (auto& [topic, types] : names) {
            if (types.empty() || streams_.count(topic)) continue;   // already bridged
            const std::string& type = types.front();
            const std::string stream = topic_to_stream(topic);

            auto st = std::make_shared<Stream>();
            st->type_name = type;
            try {
                st->writer = std::make_shared<shm_bridge::Writer>(stream, max_bytes_);
            } catch (const std::exception& e) {
                RCLCPP_WARN(get_logger(), "skip %s: %s", topic.c_str(), e.what());
                continue;
            }
            // capture by value: the Stream shared_ptr keeps writer alive in the cb
            st->sub = create_generic_subscription(
                topic, type, rclcpp::QoS(1),
                [this, st, topic, type, stream](std::shared_ptr<rclcpp::SerializedMessage> m) {
                    const auto& rcl = m->get_rcl_serialized_message();
                    if (!st->writer->write_cdr(rcl.buffer, rcl.buffer_length, type, topic)) {
                        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000,
                            "[%s] msg %zu B > cap, dropped", stream.c_str(), rcl.buffer_length);
                    }
                });
            streams_[topic] = st;
            RCLCPP_INFO(get_logger(), "+ %s (%s) -> /dev/shm/%s_*",
                        topic.c_str(), type.c_str(), stream.c_str());
        }
    }

    size_t max_bytes_;
    std::map<std::string, std::shared_ptr<Stream>> streams_;
    rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<Ros2AllToShm>());
    rclcpp::shutdown();
    return 0;
}
