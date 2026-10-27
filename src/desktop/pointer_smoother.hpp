#pragma once

#include <QPointF>

#include <algorithm>
#include <cmath>

namespace neko::desktop {

// 仅供桌面光标使用，不修改视觉原始测量，也不预测丢失期间的位置。
struct PointerSmoothingState {
    bool initialized = false;
    QPointF position;
    double lastTime = 0.0;
};

// 接收参数：持续状态、本帧真实归一化位置、摄像头会话内的采集时刻（秒）。
inline QPointF smoothDesktopPointer(
    PointerSmoothingState& state, const QPointF& measurement, double nowSeconds)
{
    if (!std::isfinite(nowSeconds) || !std::isfinite(measurement.x()) ||
        !std::isfinite(measurement.y())) {
        state = PointerSmoothingState{};
        return {};
    }
    const QPointF target(std::clamp(measurement.x(), 0.0, 1.0),
                         std::clamp(measurement.y(), 0.0, 1.0));
    const double dt = nowSeconds - state.lastTime;
    if (!state.initialized || dt < 0.0 || dt > 0.5) {
        state.initialized = true;
        state.position = target;
    } else {
        // 按实际采集间隔平滑，避免帧率或GUI排队改变处理强度。
        const double blend = 1.0 - std::exp(-dt / 0.06);
        state.position += (target - state.position) * blend;
    }
    state.lastTime = nowSeconds;
    return state.position;
}

} // namespace neko::desktop
