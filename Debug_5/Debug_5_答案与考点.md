# Debug_5 答案与考点（讲师版）

> 基线：`VANGUARD_AlgoExam-main/Debug_1`（正确版）
> 题目目录：`C:/Users/Lenovo/Desktop/Debug_5`
> 难度：入门 / 基础，建议 20 min
> 埋点数：**4 处**，全部为环境、配置、安装规则、接口定义类错误，**无逻辑错误**

## 0. 为什么这套是「基础」

四处的现象按学员必须经历的顺序依次出现，每一层都有醒目的终端关键词，不需要猜：

| 阶段 | 现象 | 第一现场关键词 |
| --- | --- | --- |
| `colcon build` | 编译 publisher / subscriber 失败 | `has no member named 'info'` |
| `ros2 run ...publisher_node` | 进程启动即崩溃 | `YAML::BadFile` / `bad file: .../config/items.yaml` |
| `ros2 run ...subscriber_node` | 找不到可执行文件 | `No executable found` |
| 两个节点都跑起来后 | 话题对不上，收不到消息 | `ros2 topic info -v` 里 Publisher count / Subscription count 不匹配 |

没有使用 `COLCON_IGNORE`、`spin_some`、QoS 不匹配这类「不报错、只是没反应」的静默坑，上一级 `Debug_4` 偏难的问题在这里已回避。

允许修改的文件：`src/**/CMakeLists.txt`、`src/**/package.xml`、`src/messages_cpp/msg/*.msg`、`src/**/src/*.cpp`。
**禁止**：改 `items.yaml` 里的数据、改 README、硬编码输出、删除节点或订阅、把配置路径改到源码目录。

---

## 1. 埋点总览

| # | 文件 | 埋的错 | 正确值 | 复现阶段 | 定位命令 |
| --- | --- | --- | --- | --- | --- |
| ① | `src/messages_cpp/msg/ItemInfo.msg` | `string info` → `string description` | `string info` | 编译 | `cat src/messages_cpp/msg/ItemInfo.msg` |
| ② | `src/messages_publisher/CMakeLists.txt` | 删掉 `install(DIRECTORY config ...)` | 恢复该 3 行 | 运行 publisher | `ls install/messages_publisher/share/messages_publisher/` |
| ③ | `src/messages_subscriber/CMakeLists.txt` | 删掉 `install(TARGETS subscriber_node ...)` | 恢复该 4 行 | 运行 subscriber | `ros2 pkg executables messages_subscriber` |
| ④ | `src/messages_publisher/src/publisher_cpp.cpp` | 话题 `"item_info"` → `"item_data"` | `"item_info"` | 通信 | `ros2 topic list` / `ros2 topic info -v` / `ros2 node info` |

---

## 2. 逐处详解

### ① 自定义消息字段名被改（考查：自定义消息接口）

**复现**

```bash
source /opt/ros/humble/setup.bash
rm -rf build install log
colcon build
```

`messages_cpp` 自己能编过（`.msg` 语法合法），但两个 C++ 包编译报错，典型输出：

```
error: 'messages_cpp::msg::ItemInfo_<std::allocator<void> >' has no member named 'info'
   items_[item_count_].info = items[i]["info"].as<std::string>();
                        ^~~~
```

`publisher_cpp.cpp` 的赋值和打印、`subscriber_cpp.cpp` 的打印一共 3 处会同时报错。

**定位**

- 报错说的是「`ItemInfo` 这个类型没有成员 `info`」，而代码里明明用了 `info` → 去看类型定义，不是去改代码。
- `cat src/messages_cpp/msg/ItemInfo.msg`：
  ```text
  int32 id
  string name
  string description
  ```
- README「正确性判定」明确写了 `id`、`name`、`info` 三个字段内容一致，`items.yaml` 里的 key 也是 `info`。

**原因**：`.msg` 是接口定义，`rosidl_generate_interfaces()` 会按它生成 C++ 类；字段名一改，所有使用方的成员访问都失效。这是接口配置错误，不是算法逻辑错误。

**修复**

```diff
 int32 id
 string name
-string description
+string info
```

> ⚠️ 常见错误修法：把 cpp 里的 `.info` 改成 `.description`。这样能编过，但 `ros2 topic echo /item_info` 输出的字段名会变成 `description`，与 README 的判定和 CI 检查不符。**必须改 `.msg`，让接口回到 `info`。**

---

### ② 配置文件没有安装（考查：安装规则 / 运行期资源）

**复现**（① 修好、构建成功后）

```bash
source install/setup.bash
ros2 run messages_publisher publisher_node
```

```
terminate called after throwing an instance of 'YAML::BadFile'
  what():  bad file: /xxx/install/messages_publisher/share/messages_publisher/config/items.yaml
```

**定位**

```bash
ls install/messages_publisher/share/messages_publisher/
# 只有 cmake/ environment/ package.xml ... 没有 config/
ls src/messages_publisher/config/
# items.yaml  ← 源码里文件是存在的
```

源码里有、install 里没有 → 问题在「安装」，不在「文件丢失」或「YAML 格式」。再看 `CMakeLists.txt`，发现只有 `install(TARGETS ...)`，没有 `install(DIRECTORY config ...)`。

**原因**：`ament_index_cpp::get_package_share_directory()` 读的是 **install 后的 share 目录**，`colcon build` 不会自动把 `config/` 拷过去，必须写 `install(DIRECTORY ...)`。

**修复**

```diff
 install(TARGETS
   publisher_node
   DESTINATION lib/${PROJECT_NAME}
 )
+install(DIRECTORY config
+  DESTINATION share/${PROJECT_NAME}
+)
```

> ⚠️ 禁止绕过：把 YAML 内容硬编码进 cpp、把路径改成 `../../src/messages_publisher/config/items.yaml`。后者依赖当前工作目录，换终端 / 上 CI 就失效。

---

### ③ 可执行文件没有安装（考查：安装规则 / `ros2 run` 查找机制）

**复现**

```bash
ros2 run messages_subscriber subscriber_node
# No executable found
```

**定位**

```bash
ros2 pkg executables messages_subscriber          # 空
ls install/messages_subscriber/lib/messages_subscriber/   # 不存在或空
cat src/messages_subscriber/CMakeLists.txt        # 只有 add_executable + ament_target_dependencies
```

对比 `messages_publisher/CMakeLists.txt`，会发现它多了一段 `install(TARGETS ...)`。

**原因**：`add_executable()` 只把可执行文件产在 `build/` 里；`ros2 run` 只到 `install/<pkg>/lib/<pkg>/` 下找。没有 install 规则，包能构建成功、却「没有可执行程序」。

**修复**

```diff
 ament_target_dependencies(subscriber_node rclcpp messages_cpp)
+install(TARGETS
+  subscriber_node
+  DESTINATION lib/${PROJECT_NAME}
+)
 if(BUILD_TESTING)
```

> ⚠️ 增量构建的坑：`colcon build` 不会删除 install 里的旧文件。如果之前构建过正确版，残留的 `subscriber_node` 会掩盖这一处。**做题前务必 `rm -rf build install log`，改完也建议清一次再验收。**

---

### ④ 发布端话题名写错（考查：Topic 发布 / 订阅匹配）

**复现**（两个节点都启动后）

```bash
ros2 topic list
# /item_data          ← 多出来的
# /item_info          ← subscriber 订阅的
# /parameter_events
# /rosout

ros2 topic info -v /item_info
# Type: messages_cpp/msg/ItemInfo
# Publisher count: 0      ← 没人发
# Subscription count: 1

ros2 topic info -v /item_data
# Publisher count: 1
# Subscription count: 0   ← 没人收

ros2 node info /item_info_publisher    # Publishes to /item_data
ros2 node info /item_info_subscriber   # Subscribes to /item_info
```

subscriber 终端一直只有 `ItemInfo Subscriber started...`，不打印 `Received Item Info`。

**定位**：`ros2 topic list` 里出现两个相似话题 → 用 `ros2 topic info -v` 看发布/订阅计数 → 用 `ros2 node info` 确认错在哪一端 → `grep -rn "item_data" src/` 找到发布者。

**原因**：ROS2 靠「话题名 + 消息类型 + QoS」三者匹配；这里名字不同，DDS 不会把消息送过去。消息类型是对的，所以 `ros2 topic info` 显示两边类型一致，更容易误判「通信正常」。

**修复**

```diff
-      this->create_publisher<messages_cpp::msg::ItemInfo>("item_data", 10);
+      this->create_publisher<messages_cpp::msg::ItemInfo>("item_info", 10);
```

> ⚠️ 常见错误修法：把订阅端也改成 `"item_data"`。这样两端能通，但 README 和 CI 都按 `/item_info` 检查（`ros2 topic echo /item_info`），属于改题目要求之外的东西来绕过。要改发布端，回到约定话题名。

---

## 3. 推荐排查流程（新手版，照抄即可）

```bash
cd ~/Debug_5
source /opt/ros/humble/setup.bash
rm -rf build install log
colcon build                        # 第 ① 处在这里暴露
```

修完 ① 后：

```bash
rm -rf build install log && colcon build
source install/setup.bash

ros2 run messages_publisher publisher_node    # 第 ② 处
ros2 run messages_subscriber subscriber_node  # 第 ③ 处（另一个终端）
```

修完 ②③ 后，两个终端各跑一个节点，第三个终端：

```bash
ros2 topic list
ros2 topic info -v /item_info
ros2 node info /item_info_publisher
ros2 node info /item_info_subscriber
ros2 topic echo /item_info                   # 第 ④ 处
```

**每修一处就重新 `colcon build` + `source install/setup.bash` 再验一次**，不要一次改四个地方。

---

## 4. 完整修复 diff

```diff
--- a/src/messages_cpp/msg/ItemInfo.msg
+++ b/src/messages_cpp/msg/ItemInfo.msg
@@ -1,3 +1,3 @@
 int32 id
 string name
-string description
+string info

--- a/src/messages_publisher/CMakeLists.txt
+++ b/src/messages_publisher/CMakeLists.txt
@@ install(TARGETS
   publisher_node
   DESTINATION lib/${PROJECT_NAME}
 )
+install(DIRECTORY config
+  DESTINATION share/${PROJECT_NAME}
+)

--- a/src/messages_publisher/src/publisher_cpp.cpp
+++ b/src/messages_publisher/src/publisher_cpp.cpp
@@
-      this->create_publisher<messages_cpp::msg::ItemInfo>("item_data", 10);
+      this->create_publisher<messages_cpp::msg::ItemInfo>("item_info", 10);

--- a/src/messages_subscriber/CMakeLists.txt
+++ b/src/messages_subscriber/CMakeLists.txt
@@ ament_target_dependencies(subscriber_node rclcpp messages_cpp)
+install(TARGETS
+  subscriber_node
+  DESTINATION lib/${PROJECT_NAME}
+)
```

---

## 5. 验收（对齐 CI 的 6 项）

| CI 项 | 对应埋点 | 通过标志 |
| --- | --- | --- |
| Build `messages_cpp` | ① | 生成 `ItemInfo` 头文件 |
| Build `messages_publisher` | ① | 编译通过 |
| Build `messages_subscriber` | ① | 编译通过 |
| Subscriber startup output | ③ | 打印 `ItemInfo Subscriber started. Topic name: "/item_info"` |
| Publisher configuration output | ② | 打印 `Loaded 5 items from .../config/items.yaml` |
| Publisher / subscriber exchange | ④ | subscriber 持续打印 `Received Item Info`，`101 → 202 → 303 → 404 → 505` 循环 |

最终确认命令：

```bash
ros2 topic info -v /item_info
# Publisher count: 1, Subscription count: 1
ros2 topic echo /item_info
# id: 101 / name: armor_plate / info: front armor plate ...
```

---

## 6. 评分要点

- **过程分**：每处能否说出「现象 → 用了哪条命令 → 得出什么结论 → 改了哪个文件」。例如 ③ 必须提到 `ros2 pkg executables` 或 `ls install/.../lib/...`，而不是「我看着 CMakeLists 觉得少了点东西」。
- **④ 必须用话题信息定位**：能说出 `ros2 topic info -v` 的 Publisher/Subscription count 或 `ros2 node info` 的订阅列表，才算「根据节点信息和话题信息缩小范围」。
- **禁止的绕过**：改 `items.yaml` 的数据、改 cpp 字段名迁就 `.msg`、改订阅端话题名迁就发布端、硬编码 YAML 内容、删掉订阅/节点、把路径指向源码目录。
- **与前三套的关系**：①（改 `.msg` 字段名）、②（整条删 `install(DIRECTORY config)`）、③（整条删 `install(TARGETS)`）都是新埋点；④ 是「话题名不一致」这一基础通信考点，但这次改的是**发布端**且换了名字（`Debug_2` 改的是订阅端 `item_infos`），排查方向相反，用来练 `ros2 node info` 定位错在哪一端。
