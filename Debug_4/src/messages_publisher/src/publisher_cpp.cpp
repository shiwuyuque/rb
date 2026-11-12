#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <messages_cpp/msg/item_info.hpp>
#include <rclcpp/rclcpp.hpp>
#include <yaml-cpp/yaml.h>

using namespace std::chrono_literals;

class ItemInfoPublisher : public rclcpp::Node {
public:
  ItemInfoPublisher()
  : Node("item_info_publisher"), item_count_(0), next_item_index_(0),
    publish_count_(0)
  {
    load_items();
    if (item_count_ == 0) {
      RCLCPP_ERROR(this->get_logger(), "No items were loaded");
      return;
    }

    publisher_ =
      this->create_publisher<messages_cpp::msg::ItemInfo>("item_info", 10);
    timer_ = this->create_wall_timer(
        1000ms, std::bind(&ItemInfoPublisher::timer_callback, this));

    RCLCPP_INFO(this->get_logger(), "ItemInfo Publisher started");
  }

private:
  void load_items()
  {
    std::string config_path =
      ament_index_cpp::get_package_share_directory("message_publisher") +
      "/config/items.yaml";

    YAML::Node config = YAML::LoadFile(config_path);
    YAML::Node items = config["items"];
    int yaml_item_count = items.size();

    for (int i = 0; i < yaml_item_count && item_count_ < 20; i++) {
      items_[item_count_].id = items[i]["id"].as<int32_t>();
      items_[item_count_].name = items[i]["name"].as<std::string>();
      items_[item_count_].info = items[i]["info"].as<std::string>();
      item_count_++;
    }

    RCLCPP_INFO(this->get_logger(), "Loaded %d items from %s", item_count_,
                config_path.c_str());
  }

  void timer_callback()
  {
    messages_cpp::msg::ItemInfo message = items_[next_item_index_];

    RCLCPP_INFO(this->get_logger(),
                "Publishing: ID=%d, Name='%s', Info='%s', count=%d", message.id,
                message.name.c_str(), message.info.c_str(), publish_count_);

    publisher_->publish(message);
    next_item_index_ = (next_item_index_ + 1) % item_count_;
    publish_count_++;
  }

  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Publisher<messages_cpp::msg::ItemInfo>::SharedPtr publisher_;
  messages_cpp::msg::ItemInfo items_[20];
  int item_count_;
  int next_item_index_;
  int publish_count_;
};

int main(int argc, char *argv[])
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<ItemInfoPublisher>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
