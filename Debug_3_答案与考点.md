# Debug_3 · ROS 2 物品信息 Debug 题（第二套）答案与考点

> **正确版**：`VANGUARD_AlgoExam-main/Debug_1`（未改动）
> **考题版**：`C:\Users\Lenovo\Desktop\Debug_3`
> 共 **四处**，全部是环境 / 构建 / 配置类问题，没有一处逻辑错误。
> 与第一套（`Debug_2` 那版的五处：缺 find_package、缺 yaml 链接、装错目录、config 装错位置、话题名不符）**完全不重复**。

---

## 一、四处问题总表

| # | 文件 | 改动 | 触发阶段 | 现象 |
|---|------|------|---------|------|
| ① | `src/messages_cpp/CMakeLists.txt` | `"msg/ItemInfo.msg"` → `"msg/iteminfo.msg"` | 构建 messages_cpp | 找不到 msg 文件，编译中断 |
| ② | `src/messages_publisher/CMakeLists.txt` | `ament_target_dependencies` 里删掉 `messages_cpp` | 构建 publisher | `messages_cpp/msg/item_info.hpp: No such file or directory` |
| ③ | `src/messages_publisher/config/items.yaml` | `items:` → `item:` | 运行 publisher | `Loaded 0 items` + `No items were loaded`，节点不起来发布 |
| ④ | `src/messages_publisher/src/publisher_cpp.cpp` | `10` → `rclcpp::SensorDataQoS()` | 运行（通信） | 两边都正常打印，但订阅端收不到任何消息 |

故障按 **构建 → 链接 → 配置 → 通信** 四层递进：不修 ① 就看不到 ②，不修 ③ 就观察不到 ④。

---

## 二、逐处详解

### ① 自定义消息的文件名大小写

```cmake
rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/iteminfo.msg"        # 实际文件是 msg/ItemInfo.msg
)
```

**报错**：生成接口时找不到该文件，报错信息里会带着 `.../messages_cpp/msg/iteminfo.msg` 这个路径。

**为什么**：Linux 文件名**区分大小写**。`ItemInfo.msg` 和 `iteminfo.msg` 是两个不同的名字，前者存在、后者不存在。

**排查**：
```bash
ls src/messages_cpp/msg/
# ItemInfo.msg          ← 实际叫这个
```

**修复**：改回 `"msg/ItemInfo.msg"`。

**考点**：Windows 上的文件系统不区分大小写（所以本地看着没问题），一进容器就炸——这是这套题最典型的"环境差异"陷阱。

---

### ② 漏了依赖链接，头文件路径没进来

```cmake
ament_target_dependencies(publisher_node ament_index_cpp rclcpp)   # 少了 messages_cpp
```

**报错**（编译阶段）：
```
fatal error: messages_cpp/msg/item_info.hpp: No such file or directory
```

**为什么**：`find_package(messages_cpp REQUIRED)` 只是把包"找到"，**并没有**把头文件目录加到目标上。真正干这件事的是 `ament_target_dependencies()` —— 它同时负责三件事：加 include 路径、加链接库、加编译定义。

`messages_cpp` 在 `package.xml` 和 `find_package` 里都还在，唯独这行漏了，所以 CMake 配置阶段毫无怨言，编译才炸。

**修复**：
```cmake
ament_target_dependencies(publisher_node ament_index_cpp rclcpp messages_cpp)
```

**考点**：ROS 2 的依赖要写三处——
`package.xml`（声明）→ `find_package`（查找）→ `ament_target_dependencies`（使用）。
少第三处的现象不是"找不到包"，而是"找不到头文件"，很容易误判成路径写错。

---

### ③ YAML 的 key 和代码读的对不上

```yaml
item:                    # 代码里读的是 config["items"]
  - id: 101
```

**现象**（节点能启动，但只有两行日志）：
```
[INFO]  [item_info_publisher]: Loaded 0 items from /.../install/messages_publisher/share/messages_publisher/config/items.yaml
[ERROR] [item_info_publisher]: No items were loaded
```
之后既不打印 `ItemInfo Publisher started`，也没有任何 `Publishing:`——因为构造函数在 `item_count_ == 0` 时直接 `return` 了，publisher 和 timer 都没建。

**为什么**：`config["items"]` 取不到节点（key 名不匹配），yaml-cpp 返回一个空节点，`size()` 是 0，于是循环一次都不执行。

**排查**：
```bash
# 先确认文件装到位了（这行日志里的路径是对的，说明不是路径问题）
find install -name items.yaml
# 再对比 key 名
grep -n "^item" src/messages_publisher/config/items.yaml
grep -n 'config\[' src/messages_publisher/src/publisher_cpp.cpp
```

**修复**：YAML 里改回 `items:`。

**考点**：日志里 `Loaded 0 items from <完整路径>` 这句话很关键——**路径打出来了，说明文件找到了，问题在内容**。新手常在这里去查安装规则，方向就错了。

---

### ④ QoS 不兼容，消息发不出去

```cpp
this->create_publisher<messages_cpp::msg::ItemInfo>("item_info",
                                                    rclcpp::SensorDataQoS());
```

**现象**：
- publisher 一切正常，每秒 `Publishing: ID=101 ...`
- subscriber 一切正常，打印 `ItemInfo Subscriber started. Topic name: "/item_info"`
- **但一条 `Received Item Info:` 都没有**
- subscriber 侧通常会出现一条警告（Humble 默认会打；换版本或换 RMW 可能只静默丢弃）：
  ```
  [WARN] New publisher discovered on topic '/item_info', offering incompatible QoS. No messages will be received from it.
  ```

**为什么**：`SensorDataQoS()` 是 **best_effort** 可靠性，而 `create_subscription(..., 10)` 的默认 QoS 是 **reliable**。ROS 2 规定：**reliable 的订阅者无法接收 best_effort 发布者的数据**（反过来可以）。两边都认为自己在正常工作，只是永远握不上手。

**排查**（这条命令是 QoS 问题的标准工具）：
```bash
ros2 topic info -v /item_info
```
```
Publisher count: 1
  Reliability: BEST_EFFORT        ← 发布端
Subscriber count: 1
  Reliability: RELIABLE           ← 订阅端
```

**修复**：改回 `10`（即默认的 reliable + depth 10）。

**考点**：ROS 2 里"话题名对、类型对"不等于能通信，**QoS 也必须兼容**。三个判断依据：`ros2 topic info -v`、`ros2 topic hz`（有发布但订阅端收不到）、以及那条 incompatible QoS 警告。

---

## 三、完整修复 diff

```diff
--- a/src/messages_cpp/CMakeLists.txt
+++ b/src/messages_cpp/CMakeLists.txt
@@
-  "msg/iteminfo.msg"
+  "msg/ItemInfo.msg"

--- a/src/messages_publisher/CMakeLists.txt
+++ b/src/messages_publisher/CMakeLists.txt
@@
-ament_target_dependencies(publisher_node ament_index_cpp rclcpp)
+ament_target_dependencies(publisher_node ament_index_cpp rclcpp messages_cpp)

--- a/src/messages_publisher/config/items.yaml
+++ b/src/messages_publisher/config/items.yaml
@@
-items:
+item:

--- a/src/messages_publisher/src/publisher_cpp.cpp
+++ b/src/messages_publisher/src/publisher_cpp.cpp
@@
-      this->create_publisher<messages_cpp::msg::ItemInfo>("item_info",
-                                                          rclcpp::SensorDataQoS());
+      this->create_publisher<messages_cpp::msg::ItemInfo>("item_info", 10);
```

> ⚠️ 第三处的修复方向和另外三处**相反**：另外三处是"代码被改坏了，把代码改回来"；这一处是**配置写错了，把 YAML 改回来**（`item:` → `items:`），而不是去改 `publisher_cpp.cpp` 里的 `config["items"]`。改代码也能"通过测试"，但那是把错误固化进程序，讲评时要专门点出来。

---

## 四、验收（对应 CI 的 6 项）

```bash
cd <工作空间>
rm -rf build install log
source /opt/ros/humble/setup.bash
colcon build                 # 前两处修完才能过
source install/setup.bash
```

| CI 项 | 判定依据 | 会被哪几处卡住 |
|-------|---------|--------------|
| 1. build messages_cpp | `colcon build` 中该包成功 | ① |
| 2. build messages_publisher | 同上 | ①② |
| 3. build messages_subscriber | 同上 | ①（依赖消息包） |
| 4. subscriber startup output | 打印 `ItemInfo Subscriber started. Topic name: "/item_info"` | ①（订阅端也编不出来） |
| 5. publisher configuration output | 打印 `Loaded 5 items from .../config/items.yaml` | ①②③ |
| 6. publisher/subscriber exchange | 订阅端持续打印 `Received Item Info:` | ①②③④ |

最终目标：两个终端分别 `ros2 run`，发送端每秒 `Publishing: ID=101/202/303/404/505` 循环，接收端每秒 `Received Item Info:` + 三行字段。

---

## 五、讲评要点

1. **这四处的顺序是"编译器排好的"**：不修构建就看不到运行期问题。所以先让 `colcon build` 干净通过，再谈运行——这也是真实工程的排查顺序。
2. **"找不到头文件"有两个完全不同的原因**：文件名写错（大小写），或依赖没链上。前者靠 `ls` 就能否定，后者要查 `ament_target_dependencies`。
3. **日志里出现完整路径 = 文件找到了**。`Loaded 0 items from <path>` 这句话几乎把 ③ 的答案写在脸上，但只有读过这行日志的人才拿得到。
4. **QoS 是 ROS 2 独有的坑**：话题名对、类型对、两边都在跑，就是不通。`ros2 topic info -v` 是唯一能一眼看穿的命令。

---

## 六、两套题别混用

| | 第一套（`Debug_2`） | 第二套（本套 `Debug_3`） |
|---|---|---|
| 构建 | 缺 `find_package(rosidl_default_generators)` | msg 文件名大小写 |
| 链接 | 缺 `target_link_libraries(yaml-cpp)` | 缺 `ament_target_dependencies(messages_cpp)` |
| 安装 | 可执行文件装到 `lib/`、config 装到 `share/<pkg>/` | 无（本套不打安装规则） |
| 通信 | 话题名 `item_infos` 拼错 | QoS 不兼容 |
| 配置 | 无 | YAML key 名不符 |

两套合起来覆盖了：CMake 依赖三件套、rosidl 生成、install 规则、share 目录、话题名、QoS、YAML——基本把 ROS 2 新手会踩的坑铺全了。
