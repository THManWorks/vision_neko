//手部姿势

#pragma once
#include <algorithm>
#include <string>
#include <opencv2/dnn.hpp>
#include "palm_detector.hpp"


namespace neko::vision {
//加载手部姿势模型
cv::dnn::Net loadHandPoseModel(const std::string& modelPath);

// 计算把手掌转到垂直朝上所需的角度，单位为度（预处理的一部分）
double calculatePalmRotation(const PalmDetection& palm);

//扩大手掌框(预处理的一部分)
cv::Rect expandPalmBox(
    const cv::Rect2f& palmBox,
    float factor,
    const cv::Size& imageSize);
}