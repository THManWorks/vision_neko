//集中定义每只手在当前帧的识别结果

#pragma once

#include "palm_detector.hpp"
#include "hand_pose.hpp"
#include "hand_gesture.hpp"
#include "hand_position.hpp"
#include "hand_pinch.hpp"
#include <vector>

namespace neko::vision{

    // 一只手在当前帧的全部识别结果
    struct HandFrameResult {
        PalmDetection palm{};   // 手掌信息：手掌框、分数、7个参考点
        HandPoseResult pose;    // 姿态信息：姿态有效性、21个关键点
        HandGesture gesture = HandGesture::Unknown;//识别出的手势，默认未知
        HandPositionResult pointing; // 本帧食指尖真实的位置测量
        HandPinchMeasurement pinch; // 本帧的捏合距离测量比例结果
    };

    // 整帧的识别结果
    struct VisionFrameResult {
        cv::Size frameSize{};//当前原图的尺寸，包含 width 和 height
        double timeSeconds = 0.0;//本帧对应的时间，单位秒

        std::vector<HandFrameResult> hands;//保存本帧所有手掌结果的容器，

        // 根据各只手的手势，汇总出的本帧手势
        HandGesture frameGesture = HandGesture::Unknown;
    };


    // 接收参数：已镜像的当前帧、两个已加载模型、锚点、本帧时间（秒）。
    // 返回值：整帧的识别结果。
    VisionFrameResult analyzeVisionFrame(
        const cv::Mat& frame,
        cv::dnn::Net& palmModel,
        cv::dnn::Net& handPoseModel,
        const std::vector<cv::Point2f>& anchors,
        double timeSeconds);


    }//namespace