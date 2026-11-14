# Debug_7 答案与考点（ROS2 · 环境/配置类错误）

> 配套题目目录：`C:/Users/Lenovo/Desktop/Debug_7/`
> 基线：仓库正确版 `VANGUARD_AlgoExam-main/Debug_1`
> 出题依据：README「考查 ROS2 功能包构建、节点通信与自定义消息接口基础知识；设置环境、配置等错误，不设置逻辑错误；难度较为基础」

## 0. 这套题考什么

| 项目 | 说明 |
| --- | --- |
| 题目类型 | **环境 / 配置类**错误（不是逻辑错误），共 4 处，全部能在终端输出或节点/话题信息里看到明确线索 |
| 修改范围 | 只动配置文件与节点源码里的配置项（`CMakeLists.txt`、`publisher_cpp.cpp`）；`README.md`、`package.xml`、`items.yaml` 一律未动 |
| 难度 | 较为基础：3 处是构建期明确报错，1 处是运行期通信不匹配（用 `ros2 topic/node info` 即可定位） |
| 判定 | 按 CI 6 项（构建 3 项 + 启动/配置/收发 3 项）逐条验收；能复现现象、根据输出缩小范围、正确定位即得分 |

### 四个埋点一览（均避开 Debug_2~Debug_6 的已有坑）

| # | 文件 | 错误类型 | 现象 | 考查点 |
| --- | --- | --- | --- | --- |
| ① | `messages_publisher/CMakeLists.txt` | 漏写 `find_package(rclcpp REQUIRED)` | `colcon build` 配置阶段报找不到 `rclcpp` | 功能包构建 / 依赖三件套 |
| ② | `messages_subscriber/CMakeLists.txt` | `add_executable` 源文件名写错（`subscriber.cpp`） | CMake 报 `Cannot find source file: src/subscriber.cpp` | 功能包构建 / 源文件 |
| ③ | `messages_subscriber/CMakeLists.txt` | `ament_target_dependencies` 漏 `messages_cpp` | 编译报 `messages_cpp/msg/item_info.hpp: No such file` | 自定义消息接口 / 依赖链接 |
| ④ | `messages_publisher/src/publisher_cpp.cpp` | 发布话题名 `"item_info"` → `"item_data"` | 构建通过，但订阅端收不到；`ros2 topic list` 多出 `/item_data` | 节点通信 / 话题名匹配 |

> 设计原则：全部**报错醒目**，刻意避开了 `COLCON_IGNORE`、`spin_some`、QoS 这类静默/进阶坑（之前被反馈偏难）。

---

## 1. Bug ①：`messages_publisher` 漏写 `find_package(rclcpp REQUIRED)`

**位置**：`src/messages_publisher/CMakeLists.txt` 第 11 行附近。

**改动前（正确）**
```cmake
find_package(ament_cmake REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(rclcpp REQUIRED)
find_package(messages_cpp REQUIRED)
find_package(yaml_cpp_vendor REQUIRED)
find_package(yaml-cpp REQUIRED)
```
**改动后（故障）**：删掉了 `find_package(rclcpp REQUIRED)` 这一行。

**现象（复现）**
```bash
source /opt/ros/humble/setup.bash
colcon build
# 在 messages_publisher 这一包出现 CMake 错误，关键词：
#   "ament_target_dependencies() the passed package 'rclcpp' was not found"
# 或 "By not providing 'Findrclcpp.cmake' ..."
```

**如何定位**
- `colcon build` 后看输出里**失败的是哪个包**（messages_publisher）。
- 打开该包 `CMakeLists.txt`，确认 `find_package(rclcpp REQUIRED)` 是否缺失——ROS 包用 `rclcpp` 必须先在 CMake 里 `find_package` 它。
- 对照「依赖三件套」口诀：`package.xml` 声明 → `find_package()` 查找 → `ament_target_dependencies()` 使用。这里第二步丢了。

**原因与影响范围**
`rclcpp` 是发布/订阅节点的核心库。没有 `find_package(rclcpp REQUIRED)`，CMake 既找不到它的头文件目录，也无法用 `ament_target_dependencies` 把依赖挂到目标上，整个 publisher 包配置失败、无法编译。

**修复 diff**
```cmake
find_package(ament_cmake REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(rclcpp REQUIRED)          # ← 补回这一行
find_package(messages_cpp REQUIRED)
find_package(yaml_cpp_vendor REQUIRED)
find_package(yaml-cpp REQUIRED)
```

**禁止绕过**：不要去改 `package.xml` 或注释掉 `ament_target_dependencies` 里的 `rclcpp` 来“绕过”。

---

## 2. Bug ②：`messages_subscriber` 的 `add_executable` 源文件名写错

**位置**：`src/messages_subscriber/CMakeLists.txt` 第 13 行。

**改动前**
```cmake
add_executable(subscriber_node src/subscriber_cpp.cpp)
ament_target_dependencies(subscriber_node rclcpp messages_cpp)
```
**改动后（故障）**
```cmake
add_executable(subscriber_node src/subscriber.cpp)      # ← 文件名写错，磁盘上是 subscriber_cpp.cpp
ament_target_dependencies(subscriber_node rclcpp)
```

**现象（复现）**
```bash
colcon build
# messages_subscriber 配置阶段报错，关键词：
#   "Cannot find source file: .../src/subscriber.cpp"
#   "add_executable called with a missing source file"
```

**如何定位**
- `colcon build` 报错的包是 messages_subscriber，且错误明确说**找不到源文件**。
- 去 `ls src/messages_subscriber/src/` 看真实文件名是 `subscriber_cpp.cpp`，而 `CMakeLists.txt` 写的是 `subscriber.cpp`——名字对不上。
- 这类错误**最先出现**（CMake 配置期），会挡在 Bug ③ 之前：先把源文件名改对，才会暴露 Bug ③ 的编译错误。

**原因与影响范围**
`CMakeLists.txt` 里声明的源文件路径和实际磁盘文件名不一致，CMake 在配置阶段就找不到文件，整个 subscriber 包无法生成可执行文件。属于最基础的「构建配置」错误。

**修复 diff**
```cmake
add_executable(subscriber_node src/subscriber_cpp.cpp)   # ← 改回正确文件名
```

**禁止绕过**：不要新建一个空 `subscriber.cpp` 或把真实文件改名来凑合。

---

## 3. Bug ③：`messages_subscriber` 漏 `messages_cpp` 依赖

**位置**：同一份 `src/messages_subscriber/CMakeLists.txt`，`ament_target_dependencies` 那一行。

**改动前**
```cmake
ament_target_dependencies(subscriber_node rclcpp messages_cpp)
```
**改动后（故障）**
```cmake
ament_target_dependencies(subscriber_node rclcpp)        # ← 少了 messages_cpp
```

**现象（复现）**
> 注意：这一处**只有在先修好 Bug ②**之后才会出现——因为 Bug ② 让 CMake 配置失败，编译根本没开始。修好 ② 后重新 `colcon build`，subscriber 进入编译，报：
```text
.../subscriber_cpp.cpp: 致命错误： messages_cpp/msg/item_info.hpp：没有那个文件或目录
```

**如何定位**
- 编译错误指向 `subscriber_cpp.cpp` 里的 `#include <messages_cpp/msg/item_info.hpp>` 找不到。
- 说明 subscriber **没有把自定义消息包 `messages_cpp` 作为依赖链接进来**。
- 回到 `CMakeLists.txt` 的 `ament_target_dependencies(subscriber_node rclcpp ...)`，确认是否漏了 `messages_cpp`。
- 同样对照「依赖三件套」：`package.xml` 里已声明 `<depend>messages_cpp</depend>`（本题没动它），`CMakeLists.txt` 里既要有 `find_package(messages_cpp REQUIRED)`（本题也没动），也要在 `ament_target_dependencies` 里写上 `messages_cpp`。

**原因与影响范围**
`ament_target_dependencies` 负责给目标加头文件搜索路径和链接库。漏掉 `messages_cpp`，编译器就找不到 `messages_cpp/msg/item_info.hpp` 这个**自定义消息接口**生成的头文件，subscriber 编译失败。

**修复 diff**
```cmake
ament_target_dependencies(subscriber_node rclcpp messages_cpp)   # ← 补回 messages_cpp
```

**禁止绕过**：不要在代码里改 `#include` 路径、也不要把 `item_info.hpp` 复制进来硬编码。

---

## 4. Bug ④：发布端话题名被改（`item_info` → `item_data`）

**位置**：`src/messages_publisher/src/publisher_cpp.cpp`，创建发布者的那一行（约 25 行）。

**改动前**
```cpp
publisher_ =
  this->create_publisher<messages_cpp::msg::ItemInfo>("item_info", 10);
```
**改动后（故障）**
```cpp
publisher_ =
  this->create_publisher<messages_cpp::msg::ItemInfo>("item_data", 10);
```

**现象（复现）**
```bash
source install/setup.bash
ros2 run messages_publisher publisher_node &     # 启动发布
ros2 run messages_subscriber subscriber_node      # 启动订阅

# 现象：发布端正常打印 "Publishing: ..."，但订阅端终端**始终不打印 "Received Item Info"**
# 排查：
ros2 topic list
#   /item_info
#   /item_data        ← 多出一个话题，且订阅端订阅的是 /item_info
ros2 topic echo /item_data      # 能看到消息在发
ros2 topic echo /item_info      # 什么都没有
ros2 node info /messages_publisher   # 发布的就是 /item_data
ros2 node info /messages_subscriber  # 订阅的是 /item_info → 两端对不上
```

**如何定位**
- 构建、启动都正常（和前三处不同，这一处**不报错**），但订阅端收不到数据。
- 用 `ros2 topic list` 发现发布端其实在发 `/item_data`，而订阅端订阅的是 `/item_info`——**话题名不匹配**。
- 注意：`ros2 topic echo /item_data` 能收到，说明消息本身没问题，纯粹是“名字对不上”。
- 打开 `publisher_cpp.cpp` 找到 `create_publisher<...>("item_data", 10)`，把话题名改回 `"item_info"` 即可；订阅端无需改动（它本来就是对的）。

**原因与影响范围**
ROS2 的发布/订阅靠**话题名**匹配，名字不同就无法连通，这是「节点通信」最基础的知识点。本题故意让发布端话题名偏离，订阅端保持正确，考查能否用 `topic list / node info / topic echo` 把“收发不通”缩小到“话题名不一致”。

**修复 diff**
```cpp
  this->create_publisher<messages_cpp::msg::ItemInfo>("item_info", 10);   # ← 改回 item_info
```

**禁止绕过**：不要把订阅端也改成 `item_data` 来“凑连通”（虽然也能收到，但不是本题预期的修复点，且偏离 CI 期望的话题名）。

---

## 5. 新手排查流程（建议照做）

```bash
# 0) 每次从头构建，避免旧产物掩盖问题
rm -rf build install log

# 1) 构建，看哪个包失败、错误关键词是什么
source /opt/ros/humble/setup.bash
colcon build
colcon list                      # 确认 3 个包都被发现（本套没有 COLCON_IGNORE，应看到 3 个）

# 2) 按错误顺序修：②源文件名 → ①rclcpp → ③messages_cpp（①②是 publisher/subscriber 各自配置期，③在②修好后暴露）
colcon build --packages-select messages_publisher
colcon build --packages-select messages_subscriber

# 3) 构建全绿后，启动两端，用话题/节点信息查通信
source install/setup.bash
ros2 run messages_publisher publisher_node &
ros2 run messages_subscriber subscriber_node
# 若订阅端不打印 Received，做第 4 处的 ros2 topic/node info 排查
```

## 6. 与 CI 6 项验收的对应

| CI 项 | 受哪个 Bug 影响 | 修复后 |
| --- | --- | --- |
| 构建 messages_cpp | 无（本题没动消息包） | 通过 |
| 构建 messages_publisher | Bug ① 漏 rclcpp | 通过 |
| 构建 messages_subscriber | Bug ② 源文件名、Bug ③ 漏 messages_cpp | 通过 |
| subscriber 启动输出 | 同上 | 通过 |
| publisher 配置输出 | 同上 | 通过 |
| publisher / subscriber 收发 | Bug ④ 话题名 | 通过 |

## 7. 禁止的绕过方式

- ❌ 通过删除节点、注释掉 `create_publisher` 让“不报错”来掩盖（删节点 / 删发布）。
- ❌ 把订阅端也改成 `item_data` 来“连通”（偏离预期修复点）。
- ❌ 改 `package.xml` / `items.yaml` / `README.md` 来绕过（本题这些文件本就没问题）。
- ❌ 为过 CI 在源码里硬编码输出字符串。

---

## 附：与之前 ROS2 题的埋点区分（避免混淆）

| 题 | 4 处环境/配置 Bug |
| --- | --- |
| Debug_2（旧，5处） | 漏 rosidl generators、漏 target_link yaml-cpp、install DESTINATION lib、install FILES config、订阅端话题改名 |
| Debug_3 | msg 文件名大小写、publisher 漏 messages_cpp 依赖、yaml key、publisher QoS |
| Debug_4 | COLCON_IGNORE、target_link yaml_cpp 拼写、share 目录拼写、spin_some |
| Debug_5 | msg 字段不符、publisher install 删 config、subscriber install 改 DESTINATION、publisher 话题改名 |
| Debug_6 | messages_cpp ament_package 区、subscriber 漏 find messages_cpp、publisher 配置路径、订阅端话题绝对名 |
| **Debug_7（本套）** | **publisher 漏 find rclcpp、subscriber 源文件名错、subscriber 漏 messages_cpp 依赖、publisher 话题改 item_data** |

六套（含旧 Debug_2）合计 20+ 个配置坑，本套四处在“依赖查找 / 源文件 / 消息链接 / 话题名”四个角度均与前作不重复，且全部报错醒目。
