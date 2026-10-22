//手部姿势

#pragma once
#include <algorithm>
#include <string>
#include <array>
#include <opencv2/dnn.hpp>
#include "palm_detector.hpp"


namespace neko::vision {

// 手部姿势网络的预处理结果
//官方 _preprocess()操作 也会一起返回模型输入和坐标还原信息
struct HandPosePreprocessResult {
    cv::Mat inputBlob;       // 模型输入：[1,224,224,3]
    cv::Mat preview;         // 224×224 的 BGR 图像，用于预览

    cv::Mat rotationMatrix;  // 将手部转正时使用的旋转矩阵
    cv::Point2f padBias;     // 第一次补边图还原到原图时，需要加上的偏移

    cv::Rect handCropBox;    // 在旋转图中进行第二次裁剪的范围
    cv::Point handPadding;  // 第二次补边的左侧、顶部像素数
    int handSide = 0;       // 缩放到224之前，正方形图像的边长
};

// 手部姿态网络的原始推理函数返回结果
//raw 表示尚未解码、还原坐标的原始数据
struct HandPoseInferenceResult {
    cv::Mat rawLandmarks;      // Identity：21个点的原始坐标
    //[x0, y0, z0, x1, y1, z1, …… , x20, y20, z20]
    //z：表示相对手腕的深度。

    cv::Mat rawConfidence;     // Identity_1：一份手部姿态结果的置信度，是整只手的分数。

    cv::Mat rawHandedness;     // Identity_2：左右手判断值
    //接近 0 倾向左手，接近 1 倾向右手

    cv::Mat rawWorldLandmarks; // Identity_3：21个点的三维世界坐标
    //另一套三维世界坐标，用于描述手的三维形状。
};

// 一只手的姿态解码结果
struct HandPoseResult {
    bool valid = false;       // 结果是否有效
    float confidence = 0.0f;  // 手部置信度
    float handedness = 0.0f;  // 左右手判断值

    // x、y：还原到当前frame的像素坐标
    // z：相对手腕的深度，不是真实世界距离
    std::array<cv::Point3f, 21> landmarks{};
};


//加载手部姿势模型
// 接收参数:手部姿势模型的文件路径。
cv::dnn::Net loadHandPoseModel(const std::string& modelPath);

// 计算把手掌转到垂直朝上所需的角度，单位为度（预处理的一部分）
// 接收参数：一只手掌的检测结果（包含手掌参考点）。
double calculatePalmRotation(const PalmDetection& palm);

//扩大手掌框(预处理要用)
//接收参数：原手掌框、宽高放大倍数、原图尺寸（用于限制裁剪范围）
cv::Rect expandPalmBox(
    const cv::Rect2f& palmBox,
    float factor,
    const cv::Size& imageSize);

//预处理函数
// 原始画面 + 一只手掌的检测结果 → 手部网络输入及坐标变换信息
// 接收参数：当前帧的原始画面、一只手掌的检测结果。
HandPosePreprocessResult preprocessHandPose(
    const cv::Mat& frame,
    const PalmDetection& palm);


//手部姿态推理函数
// 接收参数：加载好的手部姿态网络、预处理后的模型输入。
// 返回值：模型的四组原始输出。
HandPoseInferenceResult runHandPoseInference(
    cv::dnn::Net& model,
    const cv::Mat& inputBlob);

//解码函数
// 接收参数：手部姿态网络的原始输出、预处理结果（用于还原坐标）。
HandPoseResult decodeHandPose(
    const HandPoseInferenceResult& raw,
    const HandPosePreprocessResult& input);

}
