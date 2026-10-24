# 桌面机器人与手势互动

`neko_desktop.exe` 已接入手掌检测、21 点手部姿态、张手/握拳分类、跨帧确认和动态挥手。默认启动时摄像头关闭，通过机器人或托盘的右键菜单手动开启。原来的 `neko.exe` 继续用于摄像头画面、绿框和视觉代码调试。

视觉预览已新增Pointing（仅伸食指），连续3帧确认后输出 `Pointing confirmed`，用户验证通过。该类别尚未接入桌面动作或目光；下一步计划跟随5号食指根部，位置换算和目光动画仍待实现。

## 编译与运行

在项目根目录的 CMD 中运行：

```bat
set "PATH=D:\msys64\ucrt64\bin;%PATH%"
cmake -S . -B build && cmake --build build --target neko_desktop && run-desktop.cmd
```

已经编译后，可直接双击项目根目录的 `run-desktop.cmd`。脚本只为本次启动设置 MSYS2 运行库和 Qt 插件路径，不改变系统环境变量。机器人默认出现在主屏幕右下角。

右键机器人，勾选“开启手势互动”。状态从“正在开启”变为“正在识别手势”后：

- 连续检测到手掌 3 帧：确认手出现，暂不播放动作。
- 连续识别为张手 3 帧：机器人开心一下。
- 连续识别为握拳 3 帧：保留握拳事件，暂不播放动作。
- 单手张开，手腕在1.5秒内完成两次有效转向：机器人挥手回应。每轮开始时，取手腕（0号点）到中指根部（9号点）的二维像素距离，以其0.2倍作为移动阈值；整轮固定，重新开始时再计算。掌长80～110像素时对应16～22像素；触发后冷却1秒。
- 动态挥手允许有效输入之间短暂中断，不超过0.5秒时保留已有运动记录；无效坐标不参与计数，超过时限或出现多只手掌则重新开始。
- 连续未检测到手掌 5 帧：机器人惊讶一下。
- 保持同一静态手势：不重复触发；未知手势连续 5 帧会清除旧状态，之后可再次确认同一手势。继续动态挥动时，冷却结束并再次完成判定可重新触发挥手。
- 同时出现张手和握拳：整帧采用未知，不选择其中一只手。多只手为同一种已识别手势时，仍可触发该手势。
- 取消勾选：关闭摄像头，停止视觉计算；状态变为“未开启”。

当前动作映射为 `OpenPalmConfirmed → Happy`、`HandWave → Wave`、`HandLost → Surprise`；`HandDetected` 和 `FistConfirmed` 暂不触发动作。挥手动画播放约 2.2 秒期间，张手事件不会抢占挥手。静态手势状态清除仅用于重新确认，不会覆盖手离开时的惊讶动作。图片在本机进行推理，此版本没有保存或上传画面。

请先退出原来的摄像头预览程序，再开启桌面程序的手势互动，避免同时占用同一个摄像头。两个模型文件应位于：

- `models/palm_detection_mediapipe_2023feb.onnx`
- `models/handpose_estimation_mediapipe_2023feb.onnx`

默认根据可执行文件的位置查找模型，支持从其他工作目录启动。开启互动时加载一次两个模型，后续每帧复用；处理每只检测到的手掌后，汇总整帧静态手势，并用单手姿态和会话内时间更新一次挥手状态。无手帧也会更新状态，重启时重新计时和计数。

若设备或模型出错，窗口提示和右键菜单会显示原因，摄像头选项恢复为未勾选；处理问题后可以再次开启。关闭时会等待正在执行的读取或推理结束，再释放资源；异常摄像头驱动的阻塞调用可能延迟关闭，不会强制终止线程。

## 窗口操作

- 左键点击：开心一下。
- 按住左键拖动：移动机器人，松开后限制在屏幕可用区域内。
- 右键机器人或系统托盘图标：开启/关闭手势互动，或选择挥手、开心、睡觉、叫醒、隐藏、退出。
- 隐藏后：在托盘菜单选择“显示宠物”，或双击托盘图标。隐藏只暂停绘制，已经开启的摄像头仍然工作；需要停止时取消“开启手势互动”。
- ESC：退出程序，需要机器人窗口获得键盘焦点。退出会关闭摄像头。
- 如果系统没有托盘，隐藏菜单会禁用，防止无法找回窗口。

机器人有待机、开心、挥手、睡眠、惊讶五种姿态；包含眨眼和轻微起伏动画。开心、挥手、惊讶约 2.2 秒后回到待机；睡眠保持到手动叫醒或收到新动作。外观为程序绘制的猫耳机器人，后续可替换为正式角色素材。

开心动作新增短暂轻跳，以及约 1.3 秒向外扩散并淡出的星光圆环；挥手动作新增手边蓝色光点拖尾和扩散光环。用户已体验本次桌面效果。

## 代码分工与事件路径

```text
摄像头后台线程
  采集 → preprocessPalmFrame → runPalmInference → decodePalms
       ├→ updateHandPresence → Appeared / Disappeared
       └→ 每只手：preprocessHandPose → runHandPoseInference → decodeHandPose
                  → classifyHandGesture
          汇总整帧 → updateHandGesture → OpenPalmConfirmed / FistConfirmed
          单手姿态和时间 → updateHandWave → Waved → HandWave
                                          ↓ 排队传递到主线程
桌面主线程
  OpenPalmConfirmed → PetWindow::reactTo → Happy（开心）
  HandWave          → PetWindow::reactTo → Wave（挥手）
  HandLost          → PetWindow::reactTo → Surprise（惊讶）
  HandDetected / FistConfirmed → 暂不播放动作
```

- `src/vision/palm_detector.*`、`hand_presence.*`、`hand_pose.*`、`hand_gesture.*`、`hand_gesture_state.*`、`hand_wave.*`：用户实现的视觉算法，组成共用的 `neko_vision_pipeline` 库。
- `src/vision/camera_preview.*`：原有绿框预览程序。
- `src/desktop/camera_controller.*`：桌面侧的摄像头后台任务、开关、状态显示和事件转发。
- `src/desktop/visual_event.hpp`：桌面动作事件；`HandWave` 对应视觉模块确认的动态挥手。
- `src/desktop/pet_window.*`：透明置顶窗口、鼠标互动、托盘和动作切换。
- `src/desktop/robot_renderer.*`：绘制角色和动画。
- `src/desktop/desktop_main.cpp`：桌面入口和连接事件的位置。

视觉代码不依赖 Qt。摄像头、模型、锚点及跨帧状态均在后台线程使用和释放；窗口只从 GUI 主线程操作。关闭后尚在排队的视觉结果会被忽略，上一轮任务完成前不能再次开启。

当前分工：用户继续开发视觉算法，助手负责 Qt 界面、桌面接入和事件连接。`CameraFrameResult` 把手掌数量、静态手势、单手姿态和会话内单调时间传给后台状态判断；已有的视觉函数保持原样供两个程序共用，挥手不会增加一次模型推理。

若只想构建视觉预览，运行 `cmake --build build --target neko`；在没有 Qt 的环境中，可配置 `-DNEKO_BUILD_DESKTOP=OFF`。

## 检查

```bat
set "PATH=D:\msys64\ucrt64\bin;%PATH%"
set "QT_QPA_PLATFORM_PLUGIN_PATH=D:\msys64\ucrt64\share\qt6\plugins\platforms"
cmake --build build && ctest --test-dir build --output-on-failure
```

`desktop_interactions` 覆盖绘制、点击、拖动、动作恢复、手势动作映射和窗口显示/隐藏。`camera_integration` 使用模拟帧检查存在与手势事件、3 帧确认、5 帧清除、保持不重复、冲突/无手后的重新确认、关闭与重启、异常恢复和后台资源释放。动态挥手测试使用确定的会话时间与手腕坐标，覆盖事件到 GUI 和动作映射、短暂丢失、静止/单向不触发、冷却、长中断、多手立即重置、1.5秒边界，以及关闭时抑制即将触发的结果、重启后重新计数。缺失任一模型的检查在访问设备之前结束。本机模型存在时，额外用合成输入检查两个真实网络的输出形状；未安装模型时跳过该项。自动测试使用离屏窗口，不访问真实摄像头。

距离适配检查额外覆盖同样移动幅度在不同掌长下的触发差异、同一轮内阈值锁定、超时/长丢失后采用新尺度，以及零或非有限初始尺度不触发且恢复后重新计数。模拟中的手腕和中指根部同步横移，避免把位置变化误当作掌长变化。

命令行选项：

- `--camera`：启动后立即开启摄像头，默认不开启。
- `--camera-index 1`：使用指定摄像头编号，默认 `0`。
- `--model 路径.onnx`：指定手掌检测模型文件。
- `--hand-pose-model 路径.onnx`：指定手部姿态模型文件。
- `--smoke-test`：约 1.2 秒后自动退出。
- `--pose wave`：指定初始姿态。
- `--snapshot 路径.png`：保存窗口绘图。

例如 `run-desktop.cmd --camera-index 1` 可选择另一个设备，再从右键菜单开启。用户已体验动作映射与光效；新增距离适配的真实远近效果待对照，托盘、多显示器拖动和不同缩放比例也需结合真实桌面检查。目前依赖本机 MSYS2，尚未制作可复制到其他电脑直接运行的安装包。
