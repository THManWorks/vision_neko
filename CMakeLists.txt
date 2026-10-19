# 要求 CMake 版本至少为 3.20
cmake_minimum_required(VERSION 3.20)
# 项目名为 ProjectNeko，使用 C++ 语言
project(ProjectNeko LANGUAGES CXX)
# 导出编译命令，供 VS Code 获取头文件路径等信息
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# 查找 OpenCV 4，要求具备以下模块：
# core：图像容器等基础功能
# imgproc：图像处理，例如镜像
# highgui：预览窗口、显示图像、处理按键
# videoio：摄像头和视频读取
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc highgui videoio)
# 创建名为 neko 的可执行程序，下面列出的源文件参与编译
add_executable(neko 
src/main.cpp
src/vision/camera_preview.cpp)

# 要求 neko 至少使用 C++17 的语言能力
target_compile_features(neko PRIVATE cxx_std_17)
# 为 neko 配置 OpenCV 头文件的查找路径
target_include_directories(neko PRIVATE ${OpenCV_INCLUDE_DIRS})
# 将 neko 与所需的 OpenCV 库链接
target_link_libraries(neko PRIVATE ${OpenCV_LIBS})
