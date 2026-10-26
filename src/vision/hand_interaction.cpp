#include "hand_interaction.hpp"


namespace neko::vision{
    //集中交互处理函数
    // 接收参数：跨帧保留的状态、本帧统一识别结果。
    // 返回值：本帧产生的事件，以及位置跟踪结果。
HandInteractionResult updateHandInteraction(
    HandInteractionState& state,
    const VisionFrameResult& frame){
        //建返回值
        HandInteractionResult result;
        //取得当前帧的手掌数量
        //auto：让编译器根据右边的结果自动确定变量类型。
        const auto handCount = frame.hands.size();

//----手部存在状态更新---
    // 1. 更新手部存在状态
    result.presenceEvent = updateHandPresence(
        state.presence, handCount > 0);

//---静态手势状态更新-----
    // 2. 根据本帧汇总的手势，更新静态手势确认状态
    result.gestureEvent = updateHandGesture(
        state.gesture, frame.frameGesture);

//----挥手识别-----
    //一份“没有有效手部结果”的占位数据。
    // 当这一帧没有可供单手功能使用的手时，就把这份数据传进去。
        const HandFrameResult noHand{};
    
    //如果刚好只有一份手部数据，就直接引用。
    //如果没有或者有多个，就引用上面创建的无效数据
    const HandFrameResult& hand =
        handCount == 1 ? frame.hands.front() : noHand;

    // 4. 多手时清空本轮挥手记录，保留原有冷却信息
    if (handCount > 1) {
        state.wave.initialized = false;
        state.wave.direction = HandMoveDirection::None;
        state.wave.turnCount = 0;
    }

     // 更新挥手判断：只使用真实姿态数据
    // 无效输入也传进去，让函数处理短暂丢失和超时
    result.waveEvent = updateHandWave(
        state.wave,
        hand.pose,
        hand.gesture,
        frame.timeSeconds);

//------食指跟随-------
    //5.当前识别为张手或握拳，按交互规则停止食指跟踪
    bool anotherGesture =
        hand.pose.valid &&
        (hand.gesture == HandGesture::OpenPalm ||
            hand.gesture == HandGesture::Fist);

    // 多手或手势为张手或握拳时，立即结束位置跟踪
    if (handCount > 1 || anotherGesture) {
        resetHandPositionTracker(state.tracker);
    } else {
        // 直接复用识别阶段已经计算好的位置
        // 无效测量交给跟踪器处理短时预测与超时
        result.tracking = updateHandPositionTracker(
            state.tracker,
            hand.pointing,
            frame.timeSeconds);
    }

    return result;


    }//updateHandInteraction
}//namespace