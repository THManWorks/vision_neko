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

// 头部被提住时的悬挂姿态；lift控制提起/放下过渡，摆动围绕头顶发生。
struct CarryMotion {
    double lift = 0.0;
    double swayDegrees = 0.0;
};

// 返回绘制区域内的头顶部抓取范围，与角色使用同一套缩放坐标。
QRectF robotHeadGrabArea(const QRectF& bounds);

// Draws in logical coordinates, leaving breathing room around the character.
void paintRobot(QPainter& painter, const QRectF& bounds,
                PetPose pose, double elapsedSeconds,
                const QPointF& gazePosition = QPointF(0.5, 0.5),
                bool following = false,
                const FollowMotion& motion = FollowMotion{},
                const CarryMotion& carry = CarryMotion{});

} // namespace neko::desktop
