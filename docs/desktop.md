# 桌面机器人与手掌互动

`neko_desktop.exe` 已接入视觉模块的预处理、推理、解码和跨帧状态判断。默认启动时摄像头关闭，通过机器人或托盘的右键菜单手动开启。原来的 `neko.exe` 继续用于摄像头画面、绿框和视觉代码调试。

## 编译与运行

在项目根目录的 CMD 中运行：

```bat
set "PATH=D:\msys64\ucrt64\bin;%PATH%"
cmake -S . -B build && cmake --build build --target neko_desktop && run-desktop.cmd
```

已经编译后，可直接双击项目根目录的 `run-desktop.cmd`。脚本只为本次启动设置 MSYS2 运行库和 Qt 插件路径，不改变系统环境变量。机器人默认出现在主屏幕右下角。

右键机器人，勾选“开启手掌互动”。状态从“正在开启”变为“正在检测手掌”后：

- 连续检测到手掌 3 帧：机器人挥手打招呼。
- 连续未检测到手掌 5 帧：机器人惊讶一下。
- 手一直存在或一直不存在：不重复触发动作。
- 取消勾选：关闭摄像头，停止视觉计算；状态变为“未开启”。

挥手是机器人收到“手出现”事件后的动画，当前没有识别人类挥手手势。图片在本机进行推理，此版本没有保存或上传画面。

请先退出原来的摄像头预览程序，再开启桌面程序的手掌互动，避免同时占用同一个摄像头。模型文件应位于 `models/palm_detection_mediapipe_2023feb.onnx`。默认根据可执行文件的位置查找模型，支持从其他工作目录启动。

若设备或模型出错，窗口提示和右键菜单会显示原因，摄像头选项恢复为未勾选；处理问题后可以再次开启。关闭时会等待正在执行的读取或推理结束，再释放资源；异常摄像头驱动的阻塞调用可能延迟关闭，不会强制终止线程。

## 窗口操作

- 左键点击：开心一下。
- 按住左键拖动：移动机器人，松开后限制在屏幕可用区域内。
- 右键机器人或系统托盘图标：开启/关闭手掌互动，或选择挥手、开心、睡觉、叫醒、隐藏、退出。
- 隐藏后：在托盘菜单选择“显示宠物”，或双击托盘图标。隐藏只暂停绘制，已经开启的摄像头仍然工作；需要停止时取消“开启手掌互动”。
- ESC：退出程序，需要机器人窗口获得键盘焦点。退出会关闭摄像头。
- 如果系统没有托盘，隐藏菜单会禁用，防止无法找回窗口。

机器人有待机、开心、挥手、睡眠、惊讶五种姿态；包含眨眼和轻微起伏动画。开心、挥手、惊讶约 2.2 秒后回到待机；睡眠保持到手动叫醒或收到新动作。外观为程序绘制的猫耳机器人，后续可替换为正式角色素材。

## 代码分工与事件路径

```text
摄像头后台线程
  采集 → preprocessPalmFrame → runPalmInference → decodePalms
       → updateHandPresence → Appeared / Disappeared
                                      ↓ 排队传递到主线程
桌面主线程
  HandDetected / HandLost → PetWindow::reactTo → 挥手 / 惊讶
```

- `src/vision/palm_detector.*`、`hand_presence.*`：用户实现的视觉算法，组成共用的 `neko_vision_pipeline` 库。
- `src/vision/camera_preview.*`：原有绿框预览程序。
- `src/desktop/camera_controller.*`：桌面侧的摄像头后台任务、开关、状态显示和事件转发。
- `src/desktop/visual_event.hpp`：桌面动作事件；`HandWave` 只作为未来手势接口保留。
- `src/desktop/pet_window.*`：透明置顶窗口、鼠标互动、托盘和动作切换。
- `src/desktop/robot_renderer.*`：绘制角色和动画。
- `src/desktop/desktop_main.cpp`：桌面入口和连接事件的位置。

视觉代码不依赖 Qt。摄像头、模型、锚点及跨帧状态均在后台线程使用和释放；窗口只从 GUI 主线程操作。关闭后尚在排队的视觉结果会被忽略，上一轮任务完成前不能再次开启。

若只想构建视觉预览，运行 `cmake --build build --target neko`；在没有 Qt 的环境中，可配置 `-DNEKO_BUILD_DESKTOP=OFF`。

## 检查

```bat
set "PATH=D:\msys64\ucrt64\bin;%PATH%"
set "QT_QPA_PLATFORM_PLUGIN_PATH=D:\msys64\ucrt64\share\qt6\plugins\platforms"
cmake --build build && ctest --test-dir build --output-on-failure
```

`desktop_interactions` 覆盖绘制、点击、拖动、动作恢复和窗口显示/隐藏。`camera_integration` 使用模拟帧检查跨帧事件、桌面动作、关闭与重启、异常恢复和线程资源释放；缺失模型的检查在访问设备之前结束。自动测试使用离屏窗口，不访问真实摄像头。

命令行选项：

- `--camera`：启动后立即开启摄像头，默认不开启。
- `--camera-index 1`：使用指定摄像头编号，默认 `0`。
- `--model 路径.onnx`：指定模型文件。
- `--smoke-test`：约 1.2 秒后自动退出。
- `--pose wave`：指定初始姿态。
- `--snapshot 路径.png`：保存窗口绘图。

例如 `run-desktop.cmd --camera-index 1` 可选择另一个设备，再从右键菜单开启。真实摄像头联动、托盘实际操作、多显示器拖动和不同缩放比例的观感仍需人工体验。目前依赖本机 MSYS2，尚未制作可复制到其他电脑直接运行的安装包。
