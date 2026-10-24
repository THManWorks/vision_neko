#pragma once

#include <QRectF>
#include <QPointF>

class QPainter;

namespace neko::desktop {

enum class PetPose { Idle, Happy, Wave, Sleep, Surprise };

// 桌面位移驱动跑步动画，不参与视觉识别或位置预测。
struct FollowMotion {
    bool running = false;
    double direction = 1.0;
    double phase = 0.0;
};

// Draws in logical coordinates, leaving breathing room around the character.
void paintRobot(QPainter& painter, const QRectF& bounds,
                PetPose pose, double elapsedSeconds,
                const QPointF& gazePosition = QPointF(0.5, 0.5),
                bool following = false,
                const FollowMotion& motion = FollowMotion{});

} // namespace neko::desktop
