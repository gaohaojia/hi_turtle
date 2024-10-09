#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <behaviortree_ros2/bt_topic_pub_node.hpp>
#include <turtlesim/msg/pose.hpp>
#include <cmath>

class TurtleMover {
public:
    TurtleMover() : tolerance_(0.1) {}

    geometry_msgs::msg::Twist calculate_movement(const turtlesim::msg::Pose& current_pose, double target_x, double target_y, double speed) {
        geometry_msgs::msg::Twist move_cmd;
        double distance = get_distance(current_pose, target_x, target_y);

        if (distance < tolerance_) {
            move_cmd.linear.x = 0;
            move_cmd.angular.z = 0;
        } else {
            move_cmd.linear.x = speed;
            move_cmd.angular.z = get_angle_diff(current_pose, target_x, target_y) * 100;
        }
        return move_cmd;
    }

private:
    double tolerance_;
    double get_angle_diff(const turtlesim::msg::Pose& current_pose, double target_x, double target_y) const {
        double angle_to_target = std::atan2(target_y - current_pose.y, target_x - current_pose.x);
        return normalize_angle(angle_to_target - current_pose.theta);
    }
    double get_distance(const turtlesim::msg::Pose& current_pose, double target_x, double target_y) const {
        return std::hypot(current_pose.x - target_x, current_pose.y - target_y);
    }
    double normalize_angle(double angle) const {
        while (angle > M_PI) angle -= 2 * M_PI;
        while (angle < -M_PI) angle += 2 * M_PI;
        return angle;
    }
};

// 行为树自定义节点
class MoveTurtleForward : public BT::RosTopicPubNode<geometry_msgs::msg::Twist> {
public:
    MoveTurtleForward(const std::string& name, const BT::NodeConfiguration& config, const BT::RosNodeParams& params)
        : RosTopicPubNode<geometry_msgs::msg::Twist>(name, config, params)
    {
        std::string topic_name;
        double target_x, target_y, speed;

        if (getInput<std::string>("topic_name", topic_name) &&
            getInput<double>("target_x", target_x) &&
            getInput<double>("target_y", target_y) &&
            getInput<double>("speed", speed)) {
            cmd_vel_publisher_ = node_->create_publisher<geometry_msgs::msg::Twist>(topic_name + "/cmd_vel", 10);
            pose_subscription_ = node_->create_subscription<turtlesim::msg::Pose>(
                topic_name + "/pose",
                10,
                [this](const turtlesim::msg::Pose::SharedPtr msg){
                    this->current_pose_ = *msg;
                }
            );
            target_x_ = target_x;
            target_y_ = target_y;
            speed_ = speed;
            turtle_mover_ = std::make_shared<TurtleMover>();
        } else {
            RCLCPP_ERROR(node_->get_logger(), "Failed to get parameters from input port.");
            exit(1);
        }
    }

    static BT::PortsList providedPorts() {
        return {
            BT::InputPort<std::string>("topic_name"),
            BT::InputPort<double>("target_x"),
            BT::InputPort<double>("target_y"),
            BT::InputPort<double>("speed")
        };
    }

    bool setMessage(geometry_msgs::msg::Twist& msg) {
        msg = turtle_mover_->calculate_movement(current_pose_, target_x_, target_y_, speed_);
        return true; // 返回成功
    }

    BT::NodeStatus onTick() {
        geometry_msgs::msg::Twist msg;
        if (setMessage(msg)) {
            cmd_vel_publisher_->publish(msg);
            return BT::NodeStatus::RUNNING;
        }
        return BT::NodeStatus::FAILURE;
    }

private:
    double target_x_, target_y_, speed_;
    turtlesim::msg::Pose current_pose_;
    rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_publisher_;
    rclcpp::Subscription<turtlesim::msg::Pose>::SharedPtr pose_subscription_;
    std::shared_ptr<TurtleMover> turtle_mover_;
};

static const char* xml_text = R"(
  <root BTCPP_format="4">
    <BehaviorTree>
      <Sequence>
        <MoveTurtleForward topic_name="turtle1" target_x="1.0" target_y="1.0" speed="1.0"/>
      </Sequence>
    </BehaviorTree>
  </root>
)";

// 主函数
int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    auto nh = std::make_shared<rclcpp::Node>("turtle_control_node");

    BT::BehaviorTreeFactory factory;
    BT::RosNodeParams params;
    params.nh = nh; // 将节点传递给参数

    factory.registerNodeType<MoveTurtleForward>("MoveTurtleForward", params);
    auto tree = factory.createTreeFromText(xml_text);

    // 主要执行循环
    while (rclcpp::ok()) {
        tree.tickWhileRunning(); // 每次迭代调用 tick
        rclcpp::spin_some(nh); // 处理任何其他的 ROS 事件
    }

    rclcpp::shutdown();
    return 0;
}