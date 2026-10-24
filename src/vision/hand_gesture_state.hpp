//更新手势状态
//逻辑和手部存在状态更新大致类似
#pragma once

#include "hand_gesture.hpp"

namespace neko::vision {

// 手势状态变化时产生的事件
enum class HandGestureEvent {
    None,               // 没有新事件
    OpenPalmConfirmed,  // 刚确认张手
    FistConfirmed,      // 刚确认握拳
    PointingConfirmed,  // 刚确认仅伸食指
    Cleared             // 已确认的手势被清除，不代表手离开
};

// 保存跨帧状态
struct HandGestureState {
    HandGesture stableGesture = HandGesture::Unknown;
    // 已经确认的手势
    //默认值为未确认

    HandGesture candidateGesture = HandGesture::Unknown;
    // 正在等待连续多帧确认的手势


    int consecutiveFrames = 0;
    // 候选手势连续出现的帧数
};

// 接收参数：手势状态（会被更新）、当前帧汇总后的手势。
// 返回值：本次更新产生的手势事件。
HandGestureEvent updateHandGesture(
    HandGestureState& state,
    HandGesture currentGesture);

} // namespace neko::vision
