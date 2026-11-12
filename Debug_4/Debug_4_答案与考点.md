# Debug_4 · ROS 2 物品信息 Debug 题（第三套）答案与考点

> **正确版**：`VANGUARD_AlgoExam-main/Debug_1`（未改动）
> **考题版**：`C:\Users\Lenovo\Desktop\Debug_4`
> 共 **四处**，全部是环境 / 构建 / 配置类问题，没有一处逻辑错误（符合 `Debug_1/README.md` 的要求）。
> 与第一套、第二套**没有任何重复埋点**。

---

## 一、四处问题总表

| # | 位置 | 改动方式 | 触发阶段 | 现象 |
|---|------|---------|---------|------|
| ① | `src/messages_subscriber/COLCON_IGNORE` | **新增**一个空文件 | 构建发现 | 订阅包根本不参与构建；`ros2 run messages_subscriber` 报 `Package not found` |
| ② | `src/messages_publisher/CMakeLists.txt` | `yaml-cpp` → `yaml_cpp` | 链接 | `/usr/bin/ld: cannot find -lyaml_cpp` |
| ③ | `src/messages_publisher/src/publisher_cpp.cpp` | `"messages_publisher"` → `"message_publisher"` | 运行 | 抛 `PackageNotFoundError`，节点直接终止 |
| ④ | `src/messages_subscriber/src/subscriber_cpp.cpp` | `rclcpp::spin` → `rclcpp::spin_some` | 运行 | 订阅端打印启动信息后**立刻退出**，收不到任何消息 |

递进关系：② 不修，发送端压根跑不起来，③ 看不到；① 不修，订阅端跑不起来，④ 看不到。

---

## 二、逐处详解

### ① 一个空文件让整个包被跳过

`src/messages_subscriber/COLCON_IGNORE`（0 字节）

**现象**：
```bash
$ colcon build
Starting >>> messages_cpp
Starting >>> messages_publisher
Finished <<< messages_publisher
Summary: 2 packages finished          # ← 只有 2 个

$ ros2 run messages_subscriber subscriber_node
Package 'messages_subscriber' not found
```

**为什么**：`colcon` 有一个约定——**目录里存在名为 `COLCON_IGNORE` 的文件（内容无所谓，空的就行），这个目录就会被整个跳过**，不参与构建，也不会写进 ament 索引。所以它不是"编译失败"，而是"这个包从来不存在"。

**排查**：
```bash
colcon list                                    # 只列出两个包
ls -a src/messages_subscriber/                 # COLCON_IGNORE 就在这儿
cat src/messages_subscriber/COLCON_IGNORE      # 空的
```

**修复**：删掉这个文件，重新 `colcon build`。

**考点**：
- `COLCON_IGNORE` / `AMENT_IGNORE` 是 ROS 2 工程里真实存在的"临时屏蔽某个包"的机制，不是 bug 制造器；
- **"构建成功"不等于"所有包都构建了"**——`Summary: 2 packages finished` 这句话必须读，否则会一直以为订阅端是代码问题。

---

### ② 链接的库名少了一个连字符

```cmake
target_link_libraries(publisher_node yaml_cpp)     # 正确写法是 yaml-cpp
```

**报错**：
```
/usr/bin/ld: cannot find -lyaml_cpp
collect2: error: ld returned 1 exit status
```

**为什么**：`find_package(yaml-cpp REQUIRED)` 提供的 **CMake target 名字就叫 `yaml-cpp`（带连字符）**。写成 `yaml_cpp` 后，CMake 找不到同名 target，就当成**普通的库名**处理，于是给链接器传了 `-lyaml_cpp`——而系统里根本没有 `libyaml_cpp.so`，只有 `libyaml-cpp.so`。

**排查**：
```bash
grep -n "yaml" src/messages_publisher/CMakeLists.txt
ls /opt/ros/humble/lib/ | grep yaml
# libyaml-cpp.so  ← 连字符
```

**修复**：改回 `target_link_libraries(publisher_node yaml-cpp)`。

**考点**：
- `find_package` 找到的是**目标（target）**，名字必须原样引用，一个字符都不能改；
- 报错形式是 `cannot find -lxxx`（链接器层面的"找不到这个库文件"），和"undefined reference"（找到了库但符号没定义）是两种完全不同的错误，指向的问题也不同。

---

### ③ 包名少一个字母，share 目录就查不到

```cpp
ament_index_cpp::get_package_share_directory("message_publisher")   // 少了一个 s
```

**现象**（节点刚启动就挂）：
```
terminate called after throwing an instance of 'ament_index_cpp::PackageNotFoundError'
  what():  package 'message_publisher' not found
```

**为什么**：`get_package_share_directory()` 是**按包名去 ament 索引里查**安装路径的，返回值形如
`<workspace>/install/messages_publisher/share/messages_publisher`。
包名必须和 `package.xml` 里 `<name>` 完全一致。少一个 `s` 就查不到，函数直接抛异常，构造函数里没人接，程序终止。

**排查**：
```bash
ros2 pkg list | grep messages          # 真名：messages_publisher
ls install/                            # 目录名也是 messages_publisher
```

**修复**：把字符串改回 `"messages_publisher"`。

**考点**：
- share 路径 = `<install>/share/<包名>`，**包名来自 `package.xml`，不是文件夹名**；
- `get_package_share_directory` 失败是"抛异常"，不是"返回空字符串"——所以它会直接把节点打死，而不是打印一句警告。

---

### ④ `spin_some` 只轮询一次，节点打印完就退出

```cpp
rclcpp::spin_some(node);     // 正确写法是 rclcpp::spin(node)
```

**现象**：
```bash
$ ros2 run messages_subscriber subscriber_node
[INFO] [item_info_subscriber]: ItemInfo Subscriber started. Topic name: "/item_info". Waiting for messages...
$                                  # ← 立刻回到提示符，进程已经结束
```
发送端在那边每秒 `Publishing: ID=101 ...`，这边一条 `Received Item Info:` 也没有。

**为什么**：
- `rclcpp::spin(node)` 是**阻塞式自旋**：一直待在循环里处理回调，直到 `Ctrl+C`；
- `rclcpp::spin_some(node)` 只处理**当前已经就绪**的回调**一次**，然后立刻返回。

`main()` 里 `spin_some` 一返回，紧接着就是 `rclcpp::shutdown(); return 0;`——进程直接退出。订阅端是个"常驻服务"，它必须先活着，才可能收到后面发来的消息。

**排查**：
```bash
timeout 5 ros2 run messages_subscriber subscriber_node; echo "退出码 $?"
# 不到 1 秒就返回，说明节点没有常驻
grep -n "spin" src/messages_subscriber/src/subscriber_cpp.cpp
```

**修复**：改回 `rclcpp::spin(node);`。

**考点**：
- `spin` / `spin_some` / `spin_until_future_complete` 三个 API 的语义完全不同，这是 ROS 2 入门最常混的一组；
- 订阅端"启动正常打印、但收不到消息"时，第一件事是确认**它是否还活着**，而不是去查话题名。

---

## 三、完整修复 diff

```diff
--- a/src/messages_publisher/CMakeLists.txt
+++ b/src/messages_publisher/CMakeLists.txt
@@
-target_link_libraries(publisher_node yaml_cpp)
+target_link_libraries(publisher_node yaml-cpp)

--- a/src/messages_publisher/src/publisher_cpp.cpp
+++ b/src/messages_publisher/src/publisher_cpp.cpp
@@
-      ament_index_cpp::get_package_share_directory("message_publisher") +
+      ament_index_cpp::get_package_share_directory("messages_publisher") +

--- a/src/messages_subscriber/src/subscriber_cpp.cpp
+++ b/src/messages_subscriber/src/subscriber_cpp.cpp
@@
-  rclcpp::spin_some(node);
+  rclcpp::spin(node);

--- /dev/null
+++ b/src/messages_subscriber/COLCON_IGNORE   （删除该文件即可）
```

---

## 四、验收（对应 CI 的 6 项）

```bash
cd <工作空间>
rm -rf build install log
source /opt/ros/humble/setup.bash
colcon build          # 必须看到 "3 packages finished"
source install/setup.bash
```

| CI 项 | 判定依据 | 被哪几处卡住 |
|-------|---------|------------|
| 1. build messages_cpp | 构建成功 | 无（本套没有消息包问题） |
| 2. build messages_publisher | 构建成功 | ② |
| 3. build messages_subscriber | 构建成功 | ① |
| 4. subscriber startup output | 打印 `ItemInfo Subscriber started. Topic name: "/item_info"` | ① |
| 5. publisher configuration output | 打印 `Loaded 5 items from .../config/items.yaml` | ②③ |
| 6. publisher/subscriber exchange | 订阅端持续打印 `Received Item Info:` | ①②③④ |

最终目标：两个终端分别 `ros2 run`，两端都常驻，发送端每秒 `Publishing: ID=101/202/303/404/505` 循环，接收端每秒 `Received Item Info:` + 三行字段。

---

## 五、讲评要点

1. **"构建成功"是一个陷阱**。`Summary: 2 packages finished` 看着一切正常，其实少了一个包。学生如果只盯着 `colcon build` 有没有报错，会在这里卡很久。
2. **三处问题都藏在"名字"上**：库名叫 `yaml-cpp` 不是 `yaml_cpp`，包名叫 `messages_publisher` 不是 `message_publisher`，忽略文件叫 `COLCON_IGNORE`。ROS 2 里名字就是契约，差一个字符就查不到。
3. **两种链接错误要分清**：`cannot find -lxxx`（找不到库文件）vs `undefined reference`（找到库但符号没定义）。前者是"名字写错"，后者是"没链接"。
4. **节点要"活着"才能收消息**。`spin_some` 是典型陷阱——启动日志一切正常，人却已经走了。

---

## 六、三套题埋点对照（避免重复出题）

| 层 | 第一套 | 第二套（Debug_3） | 第三套（本套） |
|---|---|---|---|
| 消息包构建 | 缺 `find_package(rosidl_default_generators)` | msg 文件名大小写 | — |
| 功能包构建 | — | `ament_target_dependencies` 漏 messages_cpp | ① `COLCON_IGNORE` |
| 链接 | 缺 `target_link_libraries(yaml-cpp)` | — | ② `yaml_cpp` 少连字符 |
| 安装规则 | 装到 `lib/`、config 装错目录 | — | — |
| 运行时配置 | — | YAML key `items`→`item` | ③ 包名少一个 s |
| 通信 | 话题名 `item_infos` | QoS `SensorDataQoS` | ④ `spin_some` |
