//手指捏合判定以及两指尖中点的位置计算
/*捏合判断依据:
测量你的拇指尖（4号关键点）和食指尖（8号关键点）有多接近（用两指尖距离与掌长的比例）
*/

#pragma once

#include "hand_pose.hpp"
namespace neko::vision{

    // 一只手在当前帧的捏合距离测量结果
    struct HandPinchMeasurement {
    bool valid = false;       // 本次测量是否有效
    float distanceRatio = 0;  // 两指尖距离与掌长的比例
    cv::Point2f position{};    // 两指尖中点，使用0～1的画面位置比例
};

    // 跨帧保存的捏合判断状态
    struct HandPinchState {
        bool pinching = false;    // 当前是否已确认捏合
        int closeFrames = 0;      // 连续满足捏合条件的帧数
        int openFrames = 0;       // 连续满足松开条件的帧数
        double lastValidTime = 0; // 最近一次有效测量的时间，用于丢失超时处理
    };

    // 捏合状态变化时产生的事件
    enum class HandPinchEvent {
        None,     // 没有新事件
        Pinched,  // 刚确认捏合
        Released  // 刚确认松开
    };

    // 接收参数：一只手的姿态解码结果，包含21个关键点,当前原图尺寸。
    // 返回值：返回拇指尖与食指尖距离测量结果和两指尖中点位置。
    HandPinchMeasurement measureHandPinch(
        const HandPoseResult& hand,
        const cv::Size& frameSize);
    
    //手指捏合事件判断
    // 接收参数：跨帧状态、本帧捏合测量、当前时刻（秒）。
    // 返回值：本次是否刚确认捏合或松开。
    HandPinchEvent updateHandPinch(
        HandPinchState& state,
        const HandPinchMeasurement& measurement,
        double nowSeconds);
}//namespace