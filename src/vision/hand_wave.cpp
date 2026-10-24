#include "hand_wave.hpp"
#include <iostream>
namespace neko::vision {

//计算0号手腕 → 9号中指根部 的二维距离
float calculatePalmLength(const HandPoseResult& hand)
{
    if (!hand.valid) {
        return 0.0f;
    }

    const cv::Point3f& wrist = hand.landmarks[0];
    const cv::Point3f& middleBase = hand.landmarks[9];

    float dx = middleBase.x - wrist.x;
    float dy = middleBase.y - wrist.y;

    // 根据横向差值和纵向差值，计算两点之间的距离
    float length = std::hypot(dx, dy);

    // 排除无效数值和零长度
    if (!std::isfinite(length) || length <= 0.0f) {
        return 0.0f;
    }

    return length;
}



// 接收参数：挥手状态、当前手部姿态、当前手势、当前时刻（秒）。
HandWaveEvent updateHandWave(
    HandWaveState& state,
    const HandPoseResult& hand,
    HandGesture gesture,
    double nowSeconds)
{
    //最大丢失时间
    const double maxMissingSeconds = 0.5;

    // 与上次有效输入间隔过长时，清空旧记录；不要求当前输入有效
    if (state.initialized &&
        nowSeconds - state.lastValidTime > maxMissingSeconds) {
        
        std::cout << "Wave reset: input gap, turns="
                << state.turnCount << '\n';

        state.initialized = false;
        state.direction = HandMoveDirection::None;
        state.turnCount = 0;
    }

    // 当前输入不可用则跳过本帧；是否清空记录已由上面的判断决定
    if (!hand.valid || gesture != HandGesture::OpenPalm) {
        return HandWaveEvent::None;
    }

    // 冷却期间不判断
    if (nowSeconds < state.cooldownUntil) {
    return HandWaveEvent::None; 
    }


    // 只有真实、有效的张手输入才能刷新这个时间
    state.lastValidTime = nowSeconds;

    // 0号关键点是手腕，取它在当前画面中的横坐标
    float x = hand.landmarks[0].x;

    // 还没有初始化 或 已经开始移动，并且计时满了1.5秒（超时)：：从当前位置重新开始
    // 每轮开始时，根据当前掌长确定移动阈值
    if (!state.initialized ||
        (state.direction != HandMoveDirection::None &&
        nowSeconds - state.windowStarted >= 1.5)) {


        //计算掌长
        float palmLength = calculatePalmLength(hand);

        // 无法取得有效掌长时，清空本轮，等待下一帧
        if (palmLength <= 0.0f) {
            state.initialized = false;
            state.direction = HandMoveDirection::None;
            state.turnCount = 0;
            return HandWaveEvent::None;
        }

        state.referenceX = x;


        // 移动0.2个掌长，才算一次有效移动
        //不用定值，避免与镜头的距离太近或者太远带来的问题
        state.moveThreshold = palmLength * 0.2f;

        state.direction = HandMoveDirection::None;
        state.turnCount = 0;
        state.initialized = true;

        return HandWaveEvent::None;
    }

    // 有效移动阈值
    const float moveThreshold = state.moveThreshold;

    // 尚未确定方向：计算相对于初始位置的净位移
    if (state.direction == HandMoveDirection::None) {
        float displacement = x - state.referenceX;

        if (displacement >= moveThreshold) {
            // x增大，确认向右移动
            state.direction = HandMoveDirection::Right;
            state.referenceX = x;
        } else if (displacement <= -moveThreshold) {
            // x减小，确认向左移动
            state.direction = HandMoveDirection::Left;
            state.referenceX = x;
        }
        // 首次确认有效移动方向，才开始1.5秒的挥手窗口
        if (state.direction != HandMoveDirection::None) {
            state.windowStarted = nowSeconds;
            std::cout << "Wave start\n";
        }

    }else if (state.direction == HandMoveDirection::Right) {
    if (x > state.referenceX) {
        // 继续向右，更新最右位置
        state.referenceX = x;
    } else if (state.referenceX - x >= moveThreshold) {
        // 从最右位置向左退了足够距离，确认转向
        state.direction = HandMoveDirection::Left;
        state.referenceX = x;
        ++state.turnCount;
        std::cout << "Wave turn: " << state.turnCount << '\n';
    }
    }
    else if (state.direction == HandMoveDirection::Left) {
        if (x < state.referenceX) {
            // 继续向左，更新最左位置
            state.referenceX = x;
        } else if (x - state.referenceX >= moveThreshold) {
            // 从最左位置向右移动了足够距离，确认转向
            state.direction = HandMoveDirection::Right;
            state.referenceX = x;
            ++state.turnCount;
            std::cout << "Wave turn: " << state.turnCount << '\n';
        }
    }
    // 本轮时间内已完成两次有效转向，确认挥手
    if (state.turnCount >= 2) {
        // 结束本轮，下次重新选取起点
        state.initialized = false;
        state.direction = HandMoveDirection::None;
        state.turnCount = 0;

        // 冷却1秒，避免重复触发
        state.cooldownUntil = nowSeconds + 1.0;

        return HandWaveEvent::Waved;
    }


        return HandWaveEvent::None;

}//updateHandWave

} // namespace neko::vision
