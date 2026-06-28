// STAGE 4 of the end-to-end pipeline:  /dev/shm  ->  ROS 2 topics (on the FAR host)
//
//   stage 3 --> /dev/shm/<stream>_* --(this)--> [ROS 2 graph]  (as if nothing happened)
//
// Reads the registry written by stage 3, attaches a shm_bridge::Reader to each
// stream, and re-publishes every CDR frame on the ORIGINAL topic with the ORIGINAL
// type via a GenericPublisher. To a ROS 2 node on this machine, the topics look
// exactly like they did on the source machine — same names, same types, same bytes.
//
// Run on the DESTINATION machine (after stage 3):
//   ros2 run shm_bridge_cpp e2e_4_shm_to_ros2
#include <shm_bridge_cpp/shm_bridge.hpp>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialized_message.hpp>

#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>

struct Entry { std::string stream, topic, type; };

class Stage4 : public rclcpp::Node {
public:
    Stage4() : Node("e2e_4_shm_to_ros2") {
        RCLCPP_INFO(get_logger(), "stage4: /dev/shm -> ROS 2 topics (re-publishing)");
        timer_ = create_wall_timer(std::chrono::seconds(1), [this] { scan(); });
    }

private:
    void scan() {
        std::ifstream f("/dev/shm/e2e_streams.txt");
        if (!f) return;
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            std::string id; Entry e;
            if (!(std::getline(ss, id, '\t') && std::getline(ss, e.stream, '\t') &&
                  std::getline(ss, e.topic, '\t') && std::getline(ss, e.type))) continue;
            if (started_.count(e.topic)) continue;
            started_.insert(e.topic);
            start_stream(e);
        }
    }

    void start_stream(const Entry& e) {
        rclcpp::GenericPublisher::SharedPtr pub;
        try { pub = create_generic_publisher(e.topic, e.type, rclcpp::QoS(1)); }
        catch (const std::exception& ex) {
            RCLCPP_WARN(get_logger(), "pub %s: %s", e.topic.c_str(), ex.what());
            started_.erase(e.topic); return;
        }
        RCLCPP_INFO(get_logger(), "+ re-publishing %s (%s)", e.topic.c_str(), e.type.c_str());
        // one thread per stream: futex-wait on the shm reader, publish on arrival.
        workers_.emplace_back([this, e, pub]() {
            std::shared_ptr<shm_bridge::Reader> r;
            while (rclcpp::ok() && !r) {
                try { r = std::make_shared<shm_bridge::Reader>(e.stream); }
                catch (...) { std::this_thread::sleep_for(std::chrono::milliseconds(200)); }
            }
            shm_bridge::Frame fr;
            while (rclcpp::ok()) {
                if (!r->wait_and_read(fr, 200ull * 1000 * 1000)) continue;
                rclcpp::SerializedMessage sm(fr.data.size());
                auto& rcl = sm.get_rcl_serialized_message();
                std::memcpy(rcl.buffer, fr.data.data(), fr.data.size());
                rcl.buffer_length = fr.data.size();
                pub->publish(sm);          // re-emit the exact CDR bytes on the topic
            }
        });
    }

    std::set<std::string> started_;
    std::vector<std::thread> workers_;
    rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<Stage4>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
