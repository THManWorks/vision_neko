#pragma once

#include "hand_position.hpp"
#include <opencv2/video/tracking.hpp>

namespace neko::vision {

// 跨帧保存的跟踪状态
struct HandPositionTrackerState {
    cv::KalmanFilter filter;  // 滤波器:内部保存位置x、y和速度vx、vy

    bool initialized = false;//是否初始化
    double lastFrameTime = 0.0;        // 上次更新时刻
    double lastMeasurementTime = 0.0;  // 最近一次真实坐标的时刻
};

// 跟踪结果
struct HandPositionTrackingResult {
    bool valid = false;//是否有效
    bool predicted = false;  // 本帧是否只有预测，没有真实测量
    cv::Point2f position{};//最终确立的二维位置
};

//更新状态函数
// 接收参数：跟踪状态、当前帧的位置测量、当前时刻（秒）。
// 返回值：校正或预测后的位置，以及是否有效。
HandPositionTrackingResult updateHandPositionTracker(
    HandPositionTrackerState& state,
    const HandPositionResult& measurement,
    double nowSeconds);

//清空状态函数
// 接收参数：需要清空的跟踪状态。
void resetHandPositionTracker(HandPositionTrackerState& state);

}