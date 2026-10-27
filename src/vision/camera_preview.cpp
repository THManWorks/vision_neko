#include "camera_preview.hpp"
#include "hand.hpp"
#include "hand_interaction.hpp"
#include "hand_preview.hpp"

#include <chrono>
#include <iostream>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

namespace neko::vision {

namespace {

// 本文件使用的辅助函数：集中打印本帧产生的事件
void printHandEvents(const HandInteractionResult& result)
{   
    //手部存在输出
    if (result.presenceEvent == HandPresenceEvent::Appeared) {
        std::cout << "Hand appeared\n";
    } else if (result.presenceEvent == HandPresenceEvent::Disappeared) {
        std::cout << "Hand disappeared\n";
    }
    //手势输出
    if (result.gestureEvent == HandGestureEvent::OpenPalmConfirmed) {
        std::cout << "OpenPalm confirmed\n";
    } else if (result.gestureEvent == HandGestureEvent::FistConfirmed) {
        std::cout << "Fist confirmed\n";
    } else if (result.gestureEvent == HandGestureEvent::PointingConfirmed) {
        std::cout << "Pointing confirmed\n";
    } else if (result.gestureEvent == HandGestureEvent::Cleared) {
        std::cout << "Gesture cleared\n";
    }
    //挥手输出
    if (result.waveEvent == HandWaveEvent::Waved) {
        std::cout << "Hand wave\n";
    }
    //捏合判断输出
    if (result.pinchEvent == HandPinchEvent::Pinched) {
        std::cout << "Pinched\n";
    } else if (result.pinchEvent == HandPinchEvent::Released) {
        std::cout << "Released\n";
    }
}

} // 匿名命名空间

int runCameraPreview(int cameraIndex)
{
    // 1. 初始化：模型、锚点只加载一次
    cv::dnn::Net palmModel = loadPalmModel(
        "models/palm_detection_mediapipe_2023feb.onnx");

    cv::dnn::Net handPoseModel = loadHandPoseModel(
        "models/handpose_estimation_mediapipe_2023feb.onnx");
    
    //锚点
    std::vector<cv::Point2f> anchors = createPalmAnchors();

    cv::VideoCapture camera(cameraIndex, cv::CAP_DSHOW);
    if (!camera.isOpened()) {
        std::cerr << "Failed to open camera\n";
        return 1;
    }

    // 四类跨帧状态集中保存，整个循环共用
    HandInteractionState interactionState;
    //记录当前时刻，作为后续计时的起点：
    //std::chrono：C++标准库中处理时间的部分。
    const auto clockStart = std::chrono::steady_clock::now();
    cv::Mat frame;

    while (true) {
        // 2. 采集并镜像当前帧
        if (!camera.read(frame) || frame.empty()) {
            std::cerr << "读取图像为空\n";
            break;
        }

        //镜像反转

        cv::flip(frame, frame, 1);

        // 本帧统一时间，后续状态判断共同使用
        //当前时刻 - clockStart(即从起点经过了多久)
        std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - clockStart;
        double nowSeconds = elapsed.count();

        // 3. 统一识别本帧，每只手的结果只计算一次
        VisionFrameResult observations = analyzeVisionFrame(
            frame,
            palmModel,
            handPoseModel,
            anchors,
            nowSeconds);

        // 4. 更新跨帧状态，包括没有检测到手的帧
        HandInteractionResult interaction =
            updateHandInteraction(interactionState, observations);

        // 5. 根据同一份识别结果和交互结果绘图
        cv::Mat preview =
            drawHandPreview(frame, observations, interaction);

        // 6. 输出事件、显示图像、处理按键
        printHandEvents(interaction);
        cv::imshow("Palm Detection Snapshot", preview);

        if ((cv::waitKey(1) & 0xFF) == 'q') {
            break;
        }
    }

    // 7. 释放资源
    camera.release();
    cv::destroyAllWindows();
    return 0;
}

} // namespace neko::vision