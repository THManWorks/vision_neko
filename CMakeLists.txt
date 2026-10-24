# Qt 6 要求 CMake 版本至少为 3.22
cmake_minimum_required(VERSION 3.22)

# 项目名为 ProjectNeko，使用 C++ 语言
project(ProjectNeko LANGUAGES CXX)

# 导出编译命令，供 VS Code 获取头文件路径等信息
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 查找 OpenCV 4，要求具备以下模块：
# core：图像容器等基础功能
# imgproc：图像处理，例如镜像
# highgui：预览窗口、显示图像、处理按键
# videoio：摄像头和视频读取
# dnn：加载和运行神经网络模型
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc highgui videoio dnn)


# 视觉流水线由预览程序和桌面程序共用，保持视觉实现独立。
add_library(neko_vision_pipeline STATIC
    src/vision/palm_detector.cpp
    src/vision/hand_presence.cpp
    src/vision/hand_pose.cpp
    src/vision/hand_gesture.cpp
    src/vision/hand_gesture_state.cpp
    src/vision/hand_wave.cpp
    src/vision/hand_position.cpp
)
target_compile_features(neko_vision_pipeline PUBLIC cxx_std_17)
target_include_directories(neko_vision_pipeline PUBLIC
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vision
)
target_link_libraries(neko_vision_pipeline PUBLIC opencv_core opencv_imgproc opencv_dnn)

# 创建名为 neko 的可执行程序，下面列出的源文件参与编译
add_executable(neko 
src/main.cpp
src/vision/camera_preview.cpp
)

# 要求 neko 至少使用 C++17 的语言能力
target_compile_features(neko PRIVATE cxx_std_17)

# 为 neko 配置 OpenCV 头文件的查找路径
target_include_directories(neko PRIVATE ${OpenCV_INCLUDE_DIRS})

# 将 neko 与所需的 OpenCV 库链接
target_link_libraries(neko PRIVATE neko_vision_pipeline ${OpenCV_LIBS})

# 桌面界面单独构建，不改动 neko 摄像头程序。
option(NEKO_BUILD_DESKTOP "Build the Qt desktop robot" ON)
if(NEKO_BUILD_DESKTOP)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)

    add_library(neko_desktop_ui STATIC
        src/desktop/pet_window.cpp
        src/desktop/robot_renderer.cpp
    )
    set_target_properties(neko_desktop_ui PROPERTIES AUTOMOC ON)
    target_compile_features(neko_desktop_ui PUBLIC cxx_std_17)
    target_include_directories(neko_desktop_ui PUBLIC src/desktop)
    target_link_libraries(neko_desktop_ui PUBLIC Qt6::Widgets)

    add_library(neko_desktop_camera STATIC src/desktop/camera_controller.cpp)
    set_target_properties(neko_desktop_camera PROPERTIES AUTOMOC ON)
    target_compile_features(neko_desktop_camera PUBLIC cxx_std_17)
    target_include_directories(neko_desktop_camera PUBLIC src/desktop)
    target_link_libraries(neko_desktop_camera PUBLIC Qt6::Core neko_vision_pipeline
        PRIVATE opencv_videoio)

    # WIN32 让桌面程序启动时不附带控制台窗口。
    add_executable(neko_desktop WIN32 src/desktop/desktop_main.cpp)
    target_link_libraries(neko_desktop PRIVATE neko_desktop_ui neko_desktop_camera)

    include(CTest)
    if(BUILD_TESTING)
        find_package(Qt6 REQUIRED COMPONENTS Test)
        add_executable(neko_desktop_tests tests/desktop_tests.cpp)
        set_target_properties(neko_desktop_tests PROPERTIES AUTOMOC ON)
        target_link_libraries(neko_desktop_tests PRIVATE neko_desktop_ui Qt6::Test)
        add_test(NAME desktop_interactions COMMAND neko_desktop_tests)
        set_tests_properties(desktop_interactions PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            TIMEOUT 30
        )

        add_executable(neko_camera_tests tests/camera_tests.cpp)
        set_target_properties(neko_camera_tests PROPERTIES AUTOMOC ON)
        target_link_libraries(neko_camera_tests PRIVATE
            neko_desktop_camera neko_desktop_ui Qt6::Test)
        add_test(NAME camera_integration COMMAND neko_camera_tests)
        set_tests_properties(camera_integration PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            TIMEOUT 20
        )
    endif()
endif()
