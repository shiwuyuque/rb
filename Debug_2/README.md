# VANGUARD 算法组招新考核 —— ROS 2 物品信息 Debug 任务

## 任务目标

在规定时间(推荐15min)内定位并修复 ROS 2 工作空间中的问题，使自定义消息、发送器和接收器能够正常构建、运行并通信。

考查重点是阅读已有工程、复现错误、逐步缩小问题范围并修复。

正式 Debug 版本将在当前代码中预先设置`⚠️五处⚠️`问题，你需要准确定位并修复。考核将设置环境、配置等错误，不设置逻辑错误。

## 项目背景

这是一个简化的物品信息传输示例：发送器从 YAML 文件读取多条物品记录，每秒向 `item_info` 话题发送一条 `ItemInfo` 消息；接收器订阅该话题并打印物品信息。重点考查 ROS 2 工程和消息通信基础。

## 考查范围

- ROS 2 工作空间的构建、安装和运行。
- `CMakeLists.txt`、`package.xml` 中的依赖声明和安装规则。
- 自定义消息接口的生成与使用。
- YAML 配置文件的基使用。
- Topic 发布与订阅。

## 目录结构

```text
src/
├── messages_cpp/                     # 自定义消息包
├── messages_publisher/
│   ├── config/items.yaml             # 物品记录yaml
│   └── src/publisher_cpp.cpp         # 读取并发布物品
└── messages_subscriber/
    └── src/subscriber_cpp.cpp        # 订阅并打印物品
```

## 构建与运行

在工作空间根目录执行：

```bash
source /opt/ros/humble/setup.bash
colcon build
source install/setup.bash
```

打开两个终端，在每个终端先执行以下环境初始化命令：

```bash
source /opt/ros/humble/setup.bash #如果不使用发布的虚拟环境，自行修改为对应的ROS版本
source install/setup.bash
```

终端一运行接收器，终端二运行发送器：

```bash
ros2 run messages_subscriber subscriber_node
ros2 run messages_publisher publisher_node
```

也可以使用以下命令检查话题：

```bash
ros2 topic list
ros2 topic info /item_info
ros2 topic echo /item_info
```

## 正确性判定

发送器应报告成功读取 `items.yaml`，接收器应持续收到 `item_info` 消息。正常情况下，物品按 YAML 顺序循环发送，例如 `101 → 202 → 303 → 404 → 505`，并且 `id`、`name`、`info` 三个字段内容一致。

调试时不要修改测试期望或删除功能来掩盖错误。允许联网查阅资料，但严禁使用任何 AI 工具、代码补全工具或代写服务。