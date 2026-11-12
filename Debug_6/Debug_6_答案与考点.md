# Debug_6 答案与考点（讲师版）

> 基线：`VANGUARD_AlgoExam-main/Debug_1`（正确版）
> 题目目录：`C:/Users/Lenovo/Desktop/Debug_6`
> 难度：入门 / 基础，建议 20 min
> 埋点数：**4 处**，全部为环境、配置、安装注册、依赖声明、路径与话题名类错误，**无逻辑错误**

## 0. 为什么这套是「基础」

与 `Debug_5` 一样，四处都有明确的终端报错或明确的话题信息，不用猜「为什么没反应」：

| 阶段 | 现象 | 第一现场关键词 |
| --- | --- | --- |
| `colcon build`（下游包） | 找不到自定义消息包 | `Could not find a package configuration file provided by "messages_cpp"` |
| `colcon build`（subscriber） | 依赖没被 find_package | `packages were not found: rclcpp` / `fatal error: rclcpp/rclcpp.hpp` |
| `ros2 run ...publisher_node` | 配置文件名对不上 | `YAML::BadFile ... config/item.yaml` |
| 通信 | 话题带命名空间，对不上 | `ros2 topic list` 出现 `/sensor/item_info` |

考点覆盖：功能包注册（`ament_package()`）→ 依赖三件套（`package.xml` / `find_package` / `ament_target_dependencies`）→ install 资源路径 → 话题名解析与 `ros2 node info`。

允许修改的文件：`src/**/CMakeLists.txt`、`src/**/package.xml`、`src/**/src/*.cpp`。
**禁止**：改 `items.yaml` 的数据、改 README、硬编码输出、删除节点或订阅。

---

## 1. 埋点总览

| # | 文件 | 埋的错 | 正确值 | 复现阶段 | 定位命令 |
| --- | --- | --- | --- | --- | --- |
| ① | `src/messages_cpp/CMakeLists.txt` | 删掉末尾 `ament_package()` | 加回 `ament_package()` | 构建 publisher / subscriber | `ls install/messages_cpp/share/messages_cpp/cmake/` |
| ② | `src/messages_subscriber/CMakeLists.txt` | 删掉 `find_package(rclcpp REQUIRED)` | 加回该行 | 构建 subscriber | `cat package.xml` 对比 `CMakeLists.txt` |
| ③ | `src/messages_publisher/src/publisher_cpp.cpp` | `"/config/items.yaml"` → `"/config/item.yaml"` | `"/config/items.yaml"` | 运行 publisher | `ls install/messages_publisher/share/messages_publisher/config/` |
| ④ | `src/messages_subscriber/src/subscriber_cpp.cpp` | `"item_info"` → `"/sensor/item_info"` | `"item_info"` | 通信 | `ros2 topic list` / `ros2 node info` |

---

## 2. 逐处详解

### ① 消息包没有 ament_package()（考查：功能包构建与注册）

**复现**

```bash
source /opt/ros/humble/setup.bash
rm -rf build install log
colcon build
```

`messages_cpp` 看起来「构建成功」，但两个依赖它的包在 CMake 配置阶段就失败：

```
CMake Error at CMakeLists.txt:12 (find_package):
  By not providing "Findmessages_cpp.cmake" in CMAKE_MODULE_PATH this project
  has asked CMake to find a package configuration file provided by
  "messages_cpp", but CMake did not find one.
  ...
  Add the installation prefix of "messages_cpp" to CMAKE_PREFIX_PATH ...
```

**定位**

```bash
ls install/messages_cpp/share/messages_cpp/cmake/
# 没有 messages_cppConfig.cmake
ros2 pkg list | grep messages_cpp
# 查不到
diff <(tail -3 src/messages_cpp/CMakeLists.txt) <(tail -3 src/messages_publisher/CMakeLists.txt)
# publisher / subscriber 末尾都有 ament_package()，messages_cpp 没有
```

「编过了却找不到这个包」→ 说明包**没被注册**，不是代码写错，也不是没 `source install/setup.bash`（source 了也没有 Config.cmake）。

**原因**：`ament_package()` 是 ament_cmake 包的收尾宏，负责生成 `<pkg>Config.cmake`、安装 `package.xml`、把包注册进 ament index。缺了它，`find_package(messages_cpp REQUIRED)` 和 `ros2 pkg list` 都找不到这个包。它看起来像一句「空话」，实际是包能不能被别人用的开关。

**修复**

```diff
   ament_lint_auto_find_test_dependencies()
 endif()
+
+ament_package()
```

> 修完 ① 才会暴露 ②：`colcon` 会跳过依赖失败的包，所以第一次构建可能只看到一批下游报错。推荐按 `colcon build --packages-select messages_cpp` → `messages_publisher` → `messages_subscriber` 逐个确认。

---

### ② 缺 find_package(rclcpp REQUIRED)（考查：依赖声明三件套）

**复现**（① 修好后）

```bash
colcon build --packages-select messages_subscriber
```

典型输出（两种之一或同时出现）：

```
CMake Error at ... ament_target_dependencies():
  the following packages were not found: rclcpp
```
```
fatal error: rclcpp/rclcpp.hpp: No such file or directory
```

**定位**

```bash
cat src/messages_subscriber/package.xml      # <depend>rclcpp</depend> 在
cat src/messages_subscriber/CMakeLists.txt   # find_package 里只有 ament_cmake 和 messages_cpp
```

`package.xml` 声明了依赖、`ament_target_dependencies()` 也用了 `rclcpp`，唯独中间 `find_package()` 没做 → 「三件套」断了一环：

```text
package.xml 声明  →  find_package() 查找  →  ament_target_dependencies() 给 target 加 include / 链接库
```

**原因**：没有 `find_package(rclcpp REQUIRED)`，CMake 既拿不到 `rclcpp` 的头文件路径，也没有 `rclcpp_FOUND` 变量，`ament_target_dependencies()` 会直接报错。对比 `Debug_3` 埋的「`ament_target_dependencies` 里漏 `messages_cpp`」，两处正好把这条链补完整。

**修复**

```diff
 find_package(ament_cmake REQUIRED)
+find_package(rclcpp REQUIRED)
 find_package(messages_cpp REQUIRED)
```

---

### ③ 配置文件名少一个 s（考查：install 资源路径）

**复现**（构建成功后）

```bash
source install/setup.bash
ros2 run messages_publisher publisher_node
```

```
terminate called after throwing an instance of 'YAML::BadFile'
  what():  bad file: /xxx/install/messages_publisher/share/messages_publisher/config/item.yaml
```

**定位**：报错里的完整路径已经把答案喂到嘴边了，照着 `ls` 一下：

```bash
ls install/messages_publisher/share/messages_publisher/config/
# items.yaml     ← 实际装的是复数
grep -n "config/" src/messages_publisher/src/publisher_cpp.cpp
# "/config/item.yaml"   ← 代码写的是单数
```

**原因**：`get_package_share_directory("messages_publisher")` 返回的是 **install 后的 share 目录**，后面拼的字符串必须和真实安装的文件名逐字一致。Linux 文件名严格区分，`items.yaml` ≠ `item.yaml`。

与 `Debug_4` 的区别：那里是 `get_package_share_directory()` 的**包名**写错，抛 `PackageNotFoundError`；这里是包名对、文件名错，抛 `YAML::BadFile`。看异常类型就能区分。

**修复**

```diff
-      "/config/item.yaml";
+      "/config/items.yaml";
```

> ⚠️ 禁止绕过：把 yaml 内容写进 cpp、把路径改到 `src/` 下、或者去改 `install(DIRECTORY ...)` 的安装目标来迁就错名字。安装规则本身是对的。

---

### ④ 订阅端话题名带命名空间（考查：Topic 匹配与话题名解析）

**复现**（两个节点都跑起来后）

```bash
ros2 topic list
# /item_info              ← publisher 在发
# /parameter_events
# /rosout
# /sensor/item_info       ← subscriber 在收，多出来的

ros2 node info /item_info_subscriber
# Subscribes to: /sensor/item_info (messages_cpp/msg/ItemInfo)

ros2 topic info -v /item_info
# Publisher count: 1
# Subscription count: 0
```

subscriber 终端一直只有 `ItemInfo Subscriber started...`，从不打印 `Received Item Info`。

**定位**：`ros2 topic list` 多出一个带前缀的话题 → `ros2 node info` 看订阅端到底订阅了什么 → `grep -rn "sensor/item_info" src/` 找到订阅者。

**原因**：ROS2 话题名解析规则 —— 相对名 `"item_info"` 在节点没有 namespace 时解析成 `/item_info`；而 `"/sensor/item_info"` 以 `/` 开头是**绝对名**，是另一个话题。名字不同，DDS 不会互通。消息类型两边都是 `messages_cpp/msg/ItemInfo`，QoS 也一致，所以只能靠话题名信息定位，正好练评分标准里的「节点信息和话题信息」。

**修复**

```diff
-        "/sensor/item_info", 10,
+        "item_info", 10,
```

> ⚠️ **本套最容易误判的一点**：`ros2 topic echo /item_info` 自己会创建一个订阅者，所以即使 subscriber 节点订阅错了话题，`echo` 依然能打印出 publisher 发的数据，看起来「通信正常」。**判断标准是 subscriber 节点自己的终端有没有打印 `Received Item Info`，不是 `echo` 有没有输出。** 要配合 `ros2 topic info -v /item_info` 的 `Subscription count` 和 `ros2 node info` 一起看。
>
> 也不要把发布端改成 `"/sensor/item_info"` 去迁就订阅端：README 和 CI 都按 `/item_info` 检查。

---

## 3. 推荐排查流程（新手版，照抄即可）

```bash
cd ~/Debug_6
source /opt/ros/humble/setup.bash
rm -rf build install log
colcon build                                    # ① 让下游报「找不到 messages_cpp」
```

修 ① 后逐个确认，别一次改完：

```bash
colcon build --packages-select messages_cpp
colcon build --packages-select messages_publisher
colcon build --packages-select messages_subscriber    # ② 在这里暴露
colcon build
source install/setup.bash
```

```bash
ros2 run messages_publisher publisher_node            # ③ YAML::BadFile
```

修 ③ 后，两个终端分别跑 subscriber / publisher，第三个终端：

```bash
ros2 topic list
ros2 topic info -v /item_info
ros2 node info /item_info_publisher
ros2 node info /item_info_subscriber                  # ④ 订阅列表暴露
```

---

## 4. 完整修复 diff

```diff
--- a/src/messages_cpp/CMakeLists.txt
+++ b/src/messages_cpp/CMakeLists.txt
@@
   ament_lint_auto_find_test_dependencies()
 endif()
+
+ament_package()

--- a/src/messages_subscriber/CMakeLists.txt
+++ b/src/messages_subscriber/CMakeLists.txt
@@
 find_package(ament_cmake REQUIRED)
+find_package(rclcpp REQUIRED)
 find_package(messages_cpp REQUIRED)

--- a/src/messages_publisher/src/publisher_cpp.cpp
+++ b/src/messages_publisher/src/publisher_cpp.cpp
@@
-      "/config/item.yaml";
+      "/config/items.yaml";

--- a/src/messages_subscriber/src/subscriber_cpp.cpp
+++ b/src/messages_subscriber/src/subscriber_cpp.cpp
@@
-        "/sensor/item_info", 10,
+        "item_info", 10,
```

---

## 5. 验收（对齐 CI 的 6 项）

| CI 项 | 对应埋点 | 通过标志 |
| --- | --- | --- |
| Build `messages_cpp` | ① | `install/messages_cpp/share/messages_cpp/cmake/messages_cppConfig.cmake` 存在 |
| Build `messages_publisher` | ① | `find_package(messages_cpp REQUIRED)` 成功 |
| Build `messages_subscriber` | ①② | 编译通过，`rclcpp` 头文件可找到 |
| Subscriber startup output | ④ | 打印 `Topic name: "/item_info"` |
| Publisher configuration output | ③ | 打印 `Loaded 5 items from .../config/items.yaml` |
| Publisher / subscriber exchange | ④ | subscriber 持续打印 `Received Item Info`，`101 → 202 → 303 → 404 → 505` 循环 |

最终确认：

```bash
ros2 topic list                    # 只有 /item_info，没有 /sensor/item_info
ros2 topic info -v /item_info      # Publisher count: 1, Subscription count: 1
ros2 interface show messages_cpp/msg/ItemInfo
# int32 id
# string name
# string info
```

---

## 6. 评分要点

- **① 的过程分**：能说出「构建没报错但下游 find_package 失败 → 查 `install/.../cmake/` 缺 Config.cmake → 对比另外两个包的 CMakeLists 末尾」。只说「加一行就好了」不给满分。
- **② 的过程分**：能讲清 `package.xml` / `find_package()` / `ament_target_dependencies()` 三者的关系。
- **③ 的过程分**：能根据 `YAML::BadFile` 里的路径去 `ls` 真实安装目录，而不是靠肉眼在 cpp 里找拼写。
- **④ 的过程分**：必须用 `ros2 topic info -v` 或 `ros2 node info` 定位；能指出「`ros2 topic echo` 有输出 ≠ 两端连通」是加分项。
- **禁止的绕过**：改 `items.yaml` 数据、改安装规则迁就错文件名、把两端话题一起改成另一个名字、硬编码 YAML、删除订阅。
- **与前几套的关系**：①（`ament_package()` 缺失）、②（漏 `find_package(rclcpp)`）、③（配置文件名少一个 `s`）、④（订阅端话题改成绝对名 `/sensor/item_info`）均为新埋点；④ 仍是「话题名不一致」这一基础通信考点，但这次错在**命名空间形式**，排查时必须用 `ros2 node info` 才能看清，和 `Debug_2`（订阅端 `item_infos`）、`Debug_5`（发布端 `item_data`）形成三种不同形态。
