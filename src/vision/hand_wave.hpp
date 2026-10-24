
// 挥手判别：张手时追踪手腕关键点的 x 坐标。
// 首次有效位置作为参考，净位移达到阈值后确认左右方向。
// 每轮开始时，以手腕到中指根部的二维距离乘0.2作为移动阈值，本轮保持固定。
// 向右时更新最大 x，向左时更新最小 x；从极值反向移动达到本轮阈值，记一次转向。
// 初步采用：1.5 秒内完成两次有效转向，判定为挥手。
// 超时则以当前有效位置和时刻重新开始，清空方向和转向次数。
// 短暂无效时保留记录、不更新坐标；距上次有效输入超过0.5秒才清空。
// 触发后冷却1秒，避免重复触发；多手出现时清空本轮记录。



#pragma once
#include <cmath>
#include "hand_gesture.hpp"

namespace neko::vision {

    // 当前有效移动方向
    enum class HandMoveDirection {
        None,
        Left,
        Right
    };
    // 挥手识别产生的事件
    //判别函数返回值
    enum class HandWaveEvent {
        None,
        Waved//挥手
    };

    // 保存跨帧的挥手判断状态
    //判别函数接收值
    struct HandWaveState {

        float moveThreshold = 0.0f; // 本轮挥手使用的移动阈值，单位像素

        bool initialized = false; // 是否已经记录起始位置

        float referenceX = 0.0f;   // 初始位置，或当前方向上的最远位置

        HandMoveDirection direction = HandMoveDirection::None;
        int turnCount = 0;         // 已确认的方向切换次数

        
        double lastValidTime = 0.0; // 最近一次接受真实位置的时刻（用来处理短暂丢失情况）

        double windowStarted = 0.0; // 本轮识别开始的时刻，单位为秒
        double cooldownUntil = 0.0; // 此时刻之前不重复触发
    };

    //挥手判别函数
    // 接收参数：挥手状态、当前手部姿态（手部姿态解码后的返回值）、当前手势、当前时刻（秒）。
    // 返回值：是否新识别到一次挥手。
    HandWaveEvent updateHandWave(
        HandWaveState& state,
        const HandPoseResult& hand,
        HandGesture gesture,
        double nowSeconds);

    //计算手腕(0号关键点)到中指根部(9号)的二维距离，简称掌长
    //衡量手掌在画面中的大小（反应与镜头的距离）
    // 接收参数：一只手的姿态解码结果。
    // 返回值：手腕到中指根部的二维像素距离；数据无效时返回0。
    float calculatePalmLength(const HandPoseResult& hand);

}//namespace
