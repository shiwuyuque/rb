# Debug_2 答案与考点说明（讲师版）

> 工作空间：`C:\Users\Lenovo\Desktop\Debug_2`（正确版为 `Debug_1`）
> 共埋 **5 处** 问题，全部属于「环境 / 构建配置 / 安装规则 / 通信配置」类，**没有一处是算法逻辑错误**。
> 建议限时 15~20 分钟，允许用 `ros2` 命令行、`find`、`ls -l`、`colcon build --packages-select` 排查。

---

## 一、总览：五个 bug 的分布

| # | 文件 | 改动 | 触发阶段 | 现象关键词 | 考点 |
|---|------|------|----------|-----------|------|
| 1 | `src/messages_cpp/CMakeLists.txt` | 删掉 `find_package(rosidl_default_generators REQUIRED)` | 构建（configure） | `Unknown CMake command "rosidl_generate_interfaces"` | 自定义消息生成依赖 |
| 2 | `src/messages_publisher/CMakeLists.txt` | 删掉 `target_link_libraries(publisher_node yaml-cpp)` | 构建（link） | `undefined reference to YAML::LoadFile` | CMake 链接库 |
| 3 | `src/messages_subscriber/CMakeLists.txt` | `DESTINATION lib/${PROJECT_NAME}` → `DESTINATION lib` | 运行 | `No executable found` | 安装目录布局 `lib/<包名>/` |
| 4 | `src/messages_publisher/CMakeLists.txt` | `install(DIRECTORY config DESTINATION share/${PROJECT_NAME})` → `install(FILES config/items.yaml DESTINATION share/${PROJECT_NAME})` | 运行 | `terminate ... YAML::BadFile` | 资源文件安装路径 |
| 5 | `src/messages_subscriber/src/subscriber_cpp.cpp` | 话题名 `"item_info"` → `"item_infos"` | 运行 | 订阅端启动但收不到任何消息 | Topic 名配置 / 通信排查 |

故障是**层层递进**的：不修 1 就看不到 2，不修 3 就进不到 4、5。这本身就是缩小排查范围的训练。

---

## 二、逐处详解

### Bug 1 · 自定义消息包缺 `find_package(rosidl_default_generators)`

**报错**：
```
CMake Error at src/messages_cpp/CMakeLists.txt:10 (rosidl_generate_interfaces):
  Unknown CMake command "rosidl_generate_interfaces".
```

**为什么**：`rosidl_generate_interfaces()` 这个 CMake 宏由 `rosidl_default_generators` 提供。不 `find_package` 它，宏就不存在。注意 `package.xml` 里的
`<buildtool_depend>rosidl_default_generators</buildtool_depend>` 其实**还在**——这正是讲评重点：
**package.xml 管的是"装什么/谁先构建"，CMakeLists 管的是"这个包真正用到了什么"**，两者都要写，缺一不可。

**修复**：
```cmake
find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
```

---

### Bug 2 · 发送器找不到 yaml-cpp 链接

**报错**（很长，取关键行）：
```
/usr/bin/ld: publisher_cpp.cpp.o: in function `ItemInfoPublisher::load_items()':
undefined reference to `YAML::LoadFile(std::__cxx11::basic_string<...> const&)'
...
collect2: error: ld returned 1 exit status
```

**为什么**：`find_package(yaml-cpp REQUIRED)` 只是"找到"库，真正把库接到可执行文件上的是 `target_link_libraries`。少了这一行，
编译能过（头文件在 `/opt/ros/humble/include`），**链接必挂**。`ament_target_dependencies()` 里也没有 yaml-cpp，所以没有任何地方补上链接。

**讲评点**：区分三种错误位置——
- configure 错（找不到包）→ CMakeLists 的 `find_package`
- 编译错（找不到头文件）→ include 路径
- 链接错（undefined reference）→ `target_link_libraries`

**修复**：
```cmake
ament_target_dependencies(publisher_node ament_index_cpp rclcpp messages_cpp)
target_link_libraries(publisher_node yaml-cpp)
```

---

### Bug 3 · 接收器装到了 `lib/` 而不是 `lib/<包名>/`

**现象**：
```bash
$ ros2 run messages_subscriber subscriber_node
No executable found
```
但文件明明存在：
```bash
$ ls install/messages_subscriber/lib/
subscriber_node          # 直接躺在 lib 下，没进子目录
```

**为什么**：`ros2 run <包名> <可执行文件名>` 会在安装空间里按 `lib/<包名>/<可执行文件>` 查找（Windows 是 `Scripts/`）。
`DESTINATION lib` 把它装到了上一层，`ros2 run` 就找不到。

**排查命令**（很好用的两个）：
```bash
ros2 pkg executables messages_subscriber      # 输出为空 → 说明索引层面就没有
find install/messages_subscriber -name "subscriber_node" -type f
```

**修复**：
```cmake
install(TARGETS
  subscriber_node
  DESTINATION lib/${PROJECT_NAME}
)
```

---

### Bug 4 · `items.yaml` 装错了目录，运行时读不到

**现象**：
```
terminate called after throwing an instance of 'YAML::BadFile'
  what():  bad file: /home/xxx/ros2_ws/install/messages_publisher/share/messages_publisher/config/items.yaml
```
（若节点里做了 try/catch，会变成 `RCLCPP_ERROR` 之类，本质相同。）

**为什么**：代码里路径是 `get_package_share_directory("messages_publisher") + "/config/items.yaml"`，
而安装规则变成了 `install(FILES config/items.yaml DESTINATION share/${PROJECT_NAME})`，
文件被放到 `share/messages_publisher/items.yaml`——**少了一层 `config/`**。文件装了，但装错了地方。

**排查命令**：
```bash
find install -name "items.yaml"
# install/messages_publisher/share/messages_publisher/items.yaml   ← 位置不对
```

**修复**（两种写法等价，推荐第一种，将来加文件不用改 CMake）：
```cmake
install(DIRECTORY config
  DESTINATION share/${PROJECT_NAME}
)
```
或：
```cmake
install(FILES config/items.yaml
  DESTINATION share/${PROJECT_NAME}/config
)
```

**讲评点**：源码树里 `src/.../config/items.yaml` 好好放着，程序却读不到——**因为程序读的是 install 空间，不是 src 空间**。
这是 ROS 2 新手最常踩的坑之一（"我文件明明在啊"）。

---

### Bug 5 · 订阅的话题名对不上

**现象**：发送器正常打印 `Loaded 5 items ...` 和 `Publishing: ID=101 ...`，接收器一行都收不到（启动日志里会显示 `Topic name: "/item_infos"`）。

**为什么**：发送器发 `item_info`，接收器订阅 `item_infos`。ROS 2 里话题名就是通信地址，差一个字母就是两条完全独立的话题，不会报任何错。

**排查命令**（重点训练"用工具看事实，而不是猜"）：
```bash
ros2 topic list                 # 会同时出现 /item_info 和 /item_infos
ros2 topic info /item_info      # Publisher count: 1, Subscription count: 0
ros2 topic info /item_infos     # Publisher count: 0, Subscription count: 1
ros2 node info /item_info_subscriber   # 看它到底订了哪个
ros2 topic echo /item_info      # 证明发送端是好的
```

**修复**：
```cpp
subscription_ = this->create_subscription<messages_cpp::msg::ItemInfo>(
    "item_info", 10,
    ...);
```

---

## 三、完整修复 diff（相对正确版 Debug_1）

```diff
--- a/src/messages_cpp/CMakeLists.txt
+++ b/src/messages_cpp/CMakeLists.txt
@@
 find_package(ament_cmake REQUIRED)
+find_package(rosidl_default_generators REQUIRED)

--- a/src/messages_publisher/CMakeLists.txt
+++ b/src/messages_publisher/CMakeLists.txt
@@
 ament_target_dependencies(publisher_node ament_index_cpp rclcpp messages_cpp)
+target_link_libraries(publisher_node yaml-cpp)
 install(TARGETS
@@
-install(FILES config/items.yaml
+install(DIRECTORY config
   DESTINATION share/${PROJECT_NAME}
 )

--- a/src/messages_subscriber/CMakeLists.txt
+++ b/src/messages_subscriber/CMakeLists.txt
@@
 install(TARGETS
   subscriber_node
-  DESTINATION lib
+  DESTINATION lib/${PROJECT_NAME}
 )

--- a/src/messages_subscriber/src/subscriber_cpp.cpp
+++ b/src/messages_subscriber/src/subscriber_cpp.cpp
@@
-        "item_infos", 10,
+        "item_info", 10,
```

---

## 四、验收步骤（全过即通关）

```bash
cd ~/ros2_ws            # 你的工作空间
rm -rf build install log          # 干净重建，避免旧产物掩盖问题
source /opt/ros/humble/setup.bash
colcon build
source install/setup.bash
```

终端 A：
```bash
source /opt/ros/humble/setup.bash && source install/setup.bash
ros2 run messages_subscriber subscriber_node
```
终端 B：
```bash
source /opt/ros/humble/setup.bash && source install/setup.bash
ros2 run messages_publisher publisher_node
```

期望：
1. 发送器打印 `Loaded 5 items from .../install/messages_publisher/share/messages_publisher/config/items.yaml`
2. 发送器每秒打印 `Publishing: ID=101/202/303/404/505 ...` 循环
3. 接收器每秒打印 `Received Item Info:` + `ID/Name/Info` 三行
4. `ros2 topic info /item_info` 显示 Publisher count: 1，Subscription count: 1

⚠️ 提醒学员：**改完 CMakeLists 或源码后必须重新 `colcon build`**，只 `source` 是不会生效的——这是最容易出现的"改了没用"。

---

## 五、如果你想现场加"纯环境类"故障（不属于 5 处之内）

这类问题没法写进代码仓库，适合在演示/考核时由你手动制造，考 Linux 基本功：

| 手法 | 现象 | 考什么 |
|------|------|--------|
| 新终端里只 `source install/setup.bash`，不 source ROS | `Package 'messages_publisher' not found` 或 `ros2: command not found` | overlay 依赖 underlay |
| 两个终端设不同 `ROS_DOMAIN_ID` | 两边都跑起来了但互相看不见 | DDS 域隔离 |
| `export ROS_LOCALHOST_ONLY=1` 只设一边 | 同上，跨机/跨进程不可见 | 环境变量作用域 |
| 把 `install/` 整个 `rm -rf` 后直接 `ros2 run` | 找不到包/可执行文件 | 理解 install 空间从哪来 |
| `mv src/messages_cpp src/messages_cpp_bak` | `find_package(messages_cpp)` 失败 | 包名与工作空间布局 |

---

## 六、讲评时值得强调的三句话

1. **看报错的第一行，别从最后一行看**。CMake 的 `Unknown CMake command` / 链接器的 `undefined reference` / 运行时的 `YAML::BadFile`，各自指向完全不同的层次。
2. **ROS 2 的程序跑的是 install 空间**，`src` 里的文件不会自动出现在运行时能读到的位置——`install(...)` 规则决定一切。
3. **通信不通时不要读代码，先看事实**：`ros2 topic list` / `ros2 topic info` / `ros2 node info` 三连，能定位 90% 的"收不到消息"。

---

## 七、想缩回"四处"

如果宣传物料上已经写死"四处"，把 Bug 5 还原即可（保留 1~4，构成"构建 → 安装 → 运行"的完整链路）：

```bash
sed -i 's/"item_infos", 10,/"item_info", 10,/' Debug_2/src/messages_subscriber/src/subscriber_cpp.cpp
```
同步把 `README.md` 里的`⚠️五处⚠️`改回`⚠️四处⚠️`。
