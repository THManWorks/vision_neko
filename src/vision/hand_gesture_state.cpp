#include "hand_gesture_state.hpp"

namespace neko::vision {

// 接收参数：手势状态（会被更新）、当前帧汇总后的手势。
// 返回值：本次更新产生的手势事件。
HandGestureEvent updateHandGesture(
    HandGestureState& state,
    HandGesture currentGesture)
{
    // 1. 当前手势与稳定状态相同，不重复触发事件
    // 同时打断之前尚未完成的候选计数
    if (currentGesture == state.stableGesture) {
        state.candidateGesture = currentGesture;
        state.consecutiveFrames = 0;

        return HandGestureEvent::None;
    }

    // 2. 候选手势改变，从当前这一帧重新计数
    if (currentGesture != state.candidateGesture) {
        state.candidateGesture = currentGesture;
        state.consecutiveFrames = 1;
    } else {
        // 与上一帧候选相同，继续累积
        ++state.consecutiveFrames;
    }

    // 3. 新手势连续3帧确认，Unknown连续5帧清除
    int requiredFrames = 3;

    if (currentGesture == HandGesture::Unknown) {
        requiredFrames = 5;
    }

    // 尚未达到确认要求
    if (state.consecutiveFrames < requiredFrames) {
        return HandGestureEvent::None;
    }

    // 4. 达到要求，更新稳定状态
    state.stableGesture = currentGesture;
    state.consecutiveFrames = 0;

    // 根据刚确认的状态，返回一次事件
    if (currentGesture == HandGesture::OpenPalm) {
        return HandGestureEvent::OpenPalmConfirmed;
    }

    if (currentGesture == HandGesture::Fist) {
        return HandGestureEvent::FistConfirmed;
    }

    return HandGestureEvent::Cleared;
}

} // namespace neko::vision