# Project Neko

基于 **C++17、OpenCV 和 Qt 6** 的 Windows 桌面互动角色。通过摄像头识别手势，让桌面角色回应挥手、追随食指，并支持捏住头顶提起和拖动。

**项目状态：一期功能完成，暂时停止新增功能。** 当前版本为 **v0.3.0**，使用程序绘制的猫耳机器人，无需下载角色图片。本次仅完善项目说明，程序与已发布的运行包保持原样。

[下载已发布版本](https://github.com/THManWorks/vision_neko/releases) · [详细操作说明](docs/desktop.md) · [运行包说明](docs/portable.md) · [开发记录](docs/development-log.md)

![Project Neko猫耳机器人](docs/images/desktop.png)

以上为实际程序的静态截图；挥手、开心、惊讶、跑步和悬挂均由程序连续绘制。

## 已实现的互动

| 操作 | 桌面表现 |
| --- | --- |
| 开启互动后首次检测到手 | 开心欢迎一次 |
| 单手张开并左右挥动 | 挥手回应 |
| 仅伸出食指 | 在屏幕范围内追随食指，移动时播放跑步动作 |
| 两指中点对准头顶，再捏合 | 提起角色、拖动，松开放下 |
| 关闭手势互动 | 释放摄像头、停止识别 |

支持透明置顶窗口、鼠标拖动、托盘菜单，以及可独立开关的检测预览。视觉流程为手掌检测 → 21点姿态 → 静态分类 → 跨帧交互；预览和桌面共用识别入口。食指位置采用卡尔曼滤波，捏合位置取4号拇指尖与8号食指尖的中点。

## 下载和运行

1. 在 [Releases](https://github.com/THManWorks/vision_neko/releases) 下载Windows x64运行包ZIP及对应`.sha256`文件，完整解压。
2. 双击包内`StartNeko.cmd`。默认不开启摄像头。
3. 右键角色或托盘图标，勾选“开启手势互动”；需要观察关键点时勾选“显示检测画面”。
4. 退出时使用右键菜单的“退出”。只关闭检测窗口不会停止识别；隐藏角色也不会自动关闭摄像头。

运行包包含依赖和模型，不需要安装Qt、OpenCV、MSYS2或Python。目标环境为Windows 10/11 x64，其他电脑兼容性尚未实测。源码页面的“Download ZIP”是源码，不是免安装运行包。

CMD中可用`certutil -hashfile 文件名.zip SHA256`计算下载包校验值，与`.sha256`中的内容比较。

## 从源码构建

依赖：C++17编译器、CMake 3.22或以上、OpenCV 4（core、imgproc、highgui、videoio、dnn、video）和Qt 6 Widgets。开发环境使用MSYS2 UCRT64；编译器、Qt和OpenCV应来自同一套环境。

模型权重不纳入Git。请从OpenCV Zoo下载真实模型文件，避免保存成Git LFS文本指针：

| 文件，放入`models/` | 官方来源 |
| --- | --- |
| `palm_detection_mediapipe_2023feb.onnx` | [手掌检测模型](https://github.com/opencv/opencv_zoo/tree/main/models/palm_detection_mediapipe) |
| `handpose_estimation_mediapipe_2023feb.onnx` | [21点姿态模型](https://github.com/opencv/opencv_zoo/tree/main/models/handpose_estimation_mediapipe) |

桌面角色由Qt直接绘制，源码已包含外观和动作，不需要额外角色素材。

在项目根目录的CMD中执行，以下开发环境路径应按本机安装位置调整：

```bat
set "PATH=D:\msys64\ucrt64\bin;%PATH%"
cmake -S . -B build
cmake --build build --target neko_desktop neko
run-desktop.cmd
```

`run-desktop.cmd`中的MSYS2和Qt插件路径同样需要与本机一致。只构建视觉预览：

```bat
cmake -S . -B build/vision -DNEKO_BUILD_DESKTOP=OFF
cmake --build build/vision --target neko
```

预览程序需要使用摄像头；请由操作者自行启动，并避免与桌面程序同时占用摄像头。

## 已知限制与验证范围

- 中低速捏合拖动已由用户体验确认；高速移动仍可能偶发松手。
- 主要互动接受单手；多手会中断食指跟随、挥手轨迹和捏合拖动。
- 未形成定量成功率、误触发率或延迟报告；其他电脑、多显示器和不同DPI仍需实测。
- 构建与离屏显示检查不等于真实摄像头验收。测试源码和验证脚本已按项目维护者要求移除。

摄像头默认关闭，推理在本机进行，不保存或上传图像；可选诊断日志只记录数值与事件。

## 文档与模型来源

- [桌面操作与调试](docs/desktop.md)、[便携包说明](docs/portable.md)、[开发记录](docs/development-log.md)。
- 模型许可证保留在`models/LICENSE.txt`；打包时收集第三方库版本、来源及许可证。
