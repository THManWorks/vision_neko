#pragma once

#include "hand.hpp"
#include "hand_interaction.hpp"

namespace neko::vision {
//绘图函数
// 接收参数：当前原图、本帧识别结果、本帧交互结果。
// 返回值：绘制了手掌框、关键点和跟踪标记的图像。
cv::Mat drawHandPreview(
    const cv::Mat& frame,
    const VisionFrameResult& observations,
    const HandInteractionResult& interaction);

}