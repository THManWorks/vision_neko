#pragma once

#include "hand_gesture.hpp"

namespace neko::vision {

// 食指根部的位置，使用0～1的画面坐标比例
//x比例 = 食指根部x / 画面宽度;
//y比例 = 食指根部y / 画面高度;
struct HandPositionResult {
    bool valid = false;
    cv::Point2f position{};
};


//识别食指指向位置函数
// 接收参数：手部姿态结果、当前手势、当前原图尺寸。
// 返回值：仅gesture==Pointing时有效的5号食指根部位置(采用0～1的画面坐标比例)。
HandPositionResult calculatePointingPosition(
    const HandPoseResult& hand,
    HandGesture gesture,
    const cv::Size& frameSize);

}