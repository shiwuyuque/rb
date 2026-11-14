#include <messages_cpp/msg/item_info.hpp>
#include <rclcpp/rclcpp.hpp>

class ItemInfoSubscriber : public rclcpp::Node {
public:
  ItemInfoSubscriber()
  : Node("item_info_subscriber")
  {
    // 使用 lambda 表达式替代 std::bind，避免函数重载解析问题
    subscription_ = this->create_subscription<messages_cpp::msg::ItemInfo>(
        "item_info", 10,
      [this](const messages_cpp::msg::ItemInfo::SharedPtr msg) {
        this->topic_callback(msg);
        });

    RCLCPP_INFO(this->get_logger(),
                "ItemInfo Subscriber started. Topic name: \"%s\". Waiting for messages...", subscription_->get_topic_name());
  }

private:
  void topic_callback(const messages_cpp::msg::ItemInfo::SharedPtr msg) const
  {
    RCLCPP_INFO(this->get_logger(), "Received Item Info:");
    RCLCPP_INFO(this->get_logger(), "  ID: %d", msg->id);
    RCLCPP_INFO(this->get_logger(), "  Name: %s", msg->name.c_str());
    RCLCPP_INFO(this->get_logger(), "  Info: %s", msg->info.c_str());
    RCLCPP_INFO(this->get_logger(), "-----------------------------------");
  }

  rclcpp::Subscription<messages_cpp::msg::ItemInfo>::SharedPtr subscription_;
};

int main(int argc, char *argv[])
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<ItemInfoSubscriber>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
