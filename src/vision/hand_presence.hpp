//手部检测状态更新


#pragma once


namespace neko::vision {

    // 更新事件函数返回结构体
    //这里定义了一个新类型 HandPresenceEvent，提供三个选项（三种值）：
    enum class HandPresenceEvent {
        None,        // 没有状态变化（不是手不见了）
        Appeared,    // 手出现了
        Disappeared  // 手离开了
    };


    // 跨帧状态
    struct HandPresenceState {
        bool handPresent = false; // 稳定判断后的状态
        int detectedFrames = 0;   // 连续检测到手的帧数
        int missingFrames = 0;    // 连续没检测到手的帧数
    };

    // 根据当前帧的检测结果更新状态(更新函数)
    // 接收参数：手部存在状态（会被更新）、当前帧是否检测到手。
    HandPresenceEvent updateHandPresence(
        HandPresenceState& state,
        bool hasHandThisFrame);

}