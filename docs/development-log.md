# 开发记录

## 2026-10-19：接入 C++ OpenCV

- 工具链：MSYS2 UCRT64，GCC 16.1.0 / CMake 4.3.3 / Ninja 1.13.2。
- 已有 OpenCV 4.13.0 开发库，无需下载安装。
- CMake 已接入 core、imgproc、highgui、videoio；没有改动用户的 main.cpp。
- 配置和构建通过，当前程序仍输出用户原有的 `hell hello`。
- 独立依赖检查通过：创建图像、镜像、视频采集对象构造、HighGUI 后端查询和 DLL 加载。没有在本轮打开摄像头，真实采集由用户实现和验证。
- 当前运行环境需将 `D:\msys64\ucrt64\bin` 放在 PATH 前面，否则编译器子进程或运行库可能无法加载。只在当前终端修改，不修改系统环境变量。

项目根目录 CMD 命令：

```bat
set "PATH=D:\msys64\ucrt64\bin;%PATH%"
cmake -S . -B build -G Ninja -DOpenCV_DIR=D:/msys64/ucrt64/lib/cmake/opencv4
cmake --build build
build\neko.exe
```

下一项由用户实现：在 main.cpp 完成连续采集、读取失败处理、镜像显示、q 退出与资源释放。验证后再提取为独立视觉模块。
