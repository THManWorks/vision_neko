// 统一处理跨帧交互：根据当前帧识别结果和历史状态，
// 更新手部存在、手势确认、挥手判断、食指位置跟踪和指尖捏合，返回本次事件及跟踪结果。

#pragma once

#include "hand.hpp"
#include "hand_presence.hpp"
#include "hand_gesture_state.hpp"
#include "hand_wave.hpp"
#include "hand_position_tracker.hpp"
#include "hand_pinch.hpp"


namespace neko::vision{
    // 跨帧保留的状态(作为传入)：放在摄像头循环外
    struct HandInteractionState {
        HandPresenceState presence;// 手部存在判断跨帧状态
        HandGestureState gesture;//静态手势确认跨帧状态
        HandWaveState wave;// 动态挥手判断跨帧状态
        HandPositionTrackerState tracker;// 食指位置跟踪跨帧状态
        HandPinchState pinch; // 跨帧保存捏合状态
    };


    // 本次状态更新的结果(输出)：每帧重新产生
    //来自各自.hpp文件的定义结果汇总
    struct HandInteractionResult {
        HandPresenceEvent presenceEvent = HandPresenceEvent::None;//手部存在事件
        HandGestureEvent gestureEvent = HandGestureEvent::None;// 静态手势事件
        HandWaveEvent waveEvent = HandWaveEvent::None;// 动态挥手事件

        HandPositionTrackingResult tracking;// 食指跟踪结果：是否有效、是否仅为预测，以及校正或预测后的位置
        HandPinchEvent pinchEvent = HandPinchEvent::None; // 本帧的捏合状态事件
        bool pinching = false;// 当前是否已确认捏合，
        HandPositionResult pinchPosition; // 捏合期间，本帧真实测得的位置
        //捏合后还要持续拖动，所以同时输出事件和持续状态更方便。
    };

    // 接收参数：跨帧状态、本帧统一识别结果（来自hand.hpp)。
    // 返回值：本帧产生的事件及跟踪位置。
    HandInteractionResult updateHandInteraction(
        HandInteractionState& state,
        const VisionFrameResult& frame);

    
} //namespace