#include "robot_renderer.hpp"

#include <QColor>
#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>

namespace neko::desktop {
namespace {

constexpr double pi = 3.14159265358979323846;
const QColor outline("#446675");
const QColor mint("#62efd0");
const QColor dark("#142d40");

QLinearGradient shellGradient(double top, double bottom)
{
    QLinearGradient gradient(0, top, 0, bottom);
    gradient.setColorAt(0, QColor("#ffffff"));
    gradient.setColorAt(0.6, QColor("#e6f5f5"));
    gradient.setColorAt(1, QColor("#bcd7df"));
    return gradient;
}

void drawStar(QPainter& painter, QPointF center, double radius)
{
    QPolygonF points;
    for (int i = 0; i < 10; ++i) {
        const double angle = -pi / 2.0 + i * pi / 5.0;
        const double length = i % 2 == 0 ? radius : radius * 0.45;
        points << center + QPointF(std::cos(angle) * length,
                                   std::sin(angle) * length);
    }
    painter.drawPolygon(points);
}

// 接收参数：绘图对象、开心动作已播放的秒数。
void drawHappyBurst(QPainter& painter, double time)
{
    constexpr double duration = 1.3;
    const double progress = std::clamp(time / duration, 0.0, 1.0);
    if (progress >= 1.0) return;
    const double spread = 1.0 - std::pow(1.0 - progress, 3.0);
    const double radius = 35.0 + 66.0 * spread;
    const double fade = 1.0 - progress;
    const QPointF center(120, 108 - 10.0 * progress);

    painter.save();
    painter.setOpacity(0.65 * fade);
    painter.setPen(QPen(QColor("#f6d88b"), 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(center, radius, radius * 0.82);

    const QColor colors[]{QColor("#f6c671"), mint, QColor("#f3a8bd")};
    for (int i = 0; i < 12; ++i) {
        const double angle = -pi / 2.0 + i * pi / 6.0;
        const QPointF position = center + QPointF(
            std::cos(angle) * radius,
            std::sin(angle) * radius * 0.82);
        painter.save();
        painter.setOpacity(0.95 * fade);
        painter.translate(position);
        painter.rotate(i * 17.0 + progress * 80.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors[i % 3]);
        drawStar(painter, QPointF(0, 0), 3.5 + 3.0 * fade);
        painter.restore();
    }
    painter.restore();
}

// 接收参数：挥手动作已播放的秒数；返回右手掌中心的绘图坐标。
QPointF waveHandCenter(double time)
{
    const double angle = (-118.0 + std::sin(time * 8.0) * 20.0) * pi / 180.0;
    return QPointF(162.0 - 45.5 * std::sin(angle),
                   163.0 + 45.5 * std::cos(angle));
}

// 接收参数：绘图对象、挥手动作已播放的秒数。
void drawWaveTrail(QPainter& painter, double time)
{
    painter.save();
    painter.setPen(Qt::NoPen);
    const double appear = std::clamp(time / 0.2, 0.0, 1.0);
    for (int i = 10; i >= 1; --i) {
        const double previousTime = time - i * 0.035;
        if (previousTime < 0.0) continue;
        const double strength = (11.0 - i) / 10.0;
        const QPointF position = waveHandCenter(previousTime);
        painter.setBrush(QColor(87, 209, 241,
                                static_cast<int>(155.0 * strength * appear)));
        const double radius = 2.0 + 3.0 * strength;
        painter.drawEllipse(position, radius, radius);
    }

    const QPointF position = waveHandCenter(time);
    const double phase = std::fmod(time, 0.65) / 0.65;
    painter.setPen(QPen(QColor(98, 239, 208,
                              static_cast<int>(180.0 * (1.0 - phase) * appear)), 1.6));
    painter.setBrush(Qt::NoBrush);
    const double radius = 11.0 + phase * 12.0;
    painter.drawEllipse(position, radius, radius);
    painter.restore();
}

void drawArm(QPainter& painter, bool right, PetPose pose, double time,
             bool running, double runPhase, double carryLift)
{
    painter.save();
    painter.translate(right ? 162.0 : 78.0, 163.0 + 10.0 * carryLift);
    const bool waving = right && pose == PetPose::Wave;
    const double angle = waving
        ? -118.0 + std::sin(time * 8.0) * 20.0
        : (right ? -18.0 : 18.0) + (running
            ? std::sin(runPhase + (right ? 0.0 : pi)) * 28.0
            : std::sin(time * 2.0) * 3.0);
    const double danglingAngle = (right ? -3.0 : 3.0) + std::sin(time * 2.5) * 2.0;
    painter.rotate(angle * (1.0 - carryLift) + danglingAngle * carryLift);
    // 挥手时伸出手臂，让手掌和光点露在头部外侧。
    const double handOffset = (waving ? 10.0 : 0.0) + 7.0 * carryLift;

    painter.setPen(QPen(outline, 2.2, Qt::SolidLine, Qt::RoundCap));
    painter.setBrush(QColor("#6a94a1"));
    painter.drawEllipse(QPointF(0, 1), 8, 8);
    painter.setBrush(shellGradient(0, 39));
    painter.drawRoundedRect(QRectF(-7, 2, 14, 32 + handOffset), 6, 6);
    painter.setBrush(QColor("#d7ecec"));
    painter.drawEllipse(QRectF(-11, 25 + handOffset, 22, 21));
    painter.setPen(QPen(QColor("#8aaeb7"), 1.5));
    painter.drawLine(QPointF(-4, 37 + handOffset), QPointF(-4, 42 + handOffset));
    painter.drawLine(QPointF(3, 37 + handOffset), QPointF(3, 42 + handOffset));
    painter.restore();
}

void drawEyes(QPainter& painter, PetPose pose, double time, const QPointF& gaze,
              bool following)
{
    painter.save();
    const double x = std::isfinite(gaze.x()) ? std::clamp(gaze.x(), 0.0, 1.0) : 0.5;
    const double y = std::isfinite(gaze.y()) ? std::clamp(gaze.y(), 0.0, 1.0) : 0.5;
    // 只移动屏幕里的眼睛，嘴和身体保持原有动画。
    painter.translate(following ? std::clamp((x - 0.5) * 32.0, -10.0, 10.0)
                                : (x - 0.5) * 16.0,
                      following ? std::clamp((y - 0.5) * 20.0, -7.0, 7.0)
                                : (y - 0.5) * 10.0);
    const bool blink = std::fmod(std::max(0.0, time), 4.8) > 4.62;
    const QColor eyeColor = following ? QColor("#76d5ff") : mint;
    painter.setPen(QPen(eyeColor, 4, Qt::SolidLine, Qt::RoundCap));
    painter.setBrush(eyeColor);

    if (pose == PetPose::Sleep || blink) {
        for (double x : {94.0, 146.0}) {
            QPainterPath eyelid;
            eyelid.moveTo(x - 10, 113);
            eyelid.quadTo(x, 121, x + 10, 113);
            painter.drawPath(eyelid);
        }
    } else if (pose == PetPose::Happy) {
        for (double x : {94.0, 146.0}) {
            QPainterPath smile;
            smile.moveTo(x - 10, 116);
            smile.quadTo(x, 97, x + 10, 116);
            painter.drawPath(smile);
        }
    } else if (pose == PetPose::Surprise) {
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(94, 110), 10, 14);
        painter.drawEllipse(QPointF(146, 110), 10, 14);
        painter.setBrush(QColor("#eaffff"));
        painter.drawEllipse(QPointF(91, 105), 3, 4);
        painter.drawEllipse(QPointF(143, 105), 3, 4);
    } else {
        painter.setPen(Qt::NoPen);
        for (double x : {94.0, 146.0}) {
            painter.setBrush(eyeColor);
            painter.drawRoundedRect(QRectF(x - 7, 98, 14, 24), 6, 6);
            painter.setBrush(QColor("#d8fff7"));
            painter.drawRoundedRect(QRectF(x - 4, 100, 5, 9), 2, 2);
        }
    }

    painter.restore();
    painter.setPen(QPen(mint, 2.4, Qt::SolidLine, Qt::RoundCap));
    painter.setBrush(Qt::NoBrush);
    if (pose == PetPose::Surprise) {
        painter.drawEllipse(QRectF(116, 125, 8, 9));
    } else if (pose == PetPose::Sleep) {
        painter.drawLine(QPointF(117, 130), QPointF(123, 130));
    } else {
        QPainterPath mouth;
        mouth.moveTo(111, 127);
        mouth.quadTo(120, 137, 129, 127);
        painter.drawPath(mouth);
    }
}

} // namespace

QRectF robotHeadGrabArea(const QRectF& bounds)
{
    if (bounds.isEmpty()) return {};
    const double scale = std::min(bounds.width() / 240.0, bounds.height() / 256.0);
    const QPointF origin(bounds.center().x() - 120.0 * scale,
                         bounds.center().y() - 128.0 * scale);
    return QRectF(origin + QPointF(60.0, 38.0) * scale,
                  QSizeF(120.0, 50.0) * scale);
}

void paintRobot(QPainter& painter, const QRectF& bounds,
                PetPose pose, double elapsedSeconds, const QPointF& gazePosition,
                bool following, const FollowMotion& motion, const CarryMotion& carry)
{
    if (bounds.isEmpty()) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const double scale = std::min(bounds.width() / 240.0,
                                  bounds.height() / 256.0);
    painter.translate(bounds.center().x() - 120.0 * scale,
                      bounds.center().y() - 128.0 * scale);
    painter.scale(scale, scale);

    const double time = std::max(0.0, elapsedSeconds);
    const double lift = std::isfinite(carry.lift) ? std::clamp(carry.lift, 0.0, 1.0) : 0.0;
    const double sway = std::isfinite(carry.swayDegrees)
        ? std::clamp(carry.swayDegrees, -8.0, 8.0) * lift : 0.0;
    // 悬挂时采用睁大的眼睛，不再播放奔跑、挥手或闭眼动作。
    if (lift > 0.01) pose = PetPose::Surprise;
    following = following && lift <= 0.01;
    const bool sleeping = pose == PetPose::Sleep;
    const bool running = following && motion.running && !sleeping && pose != PetPose::Wave;
    const double runPhase = std::isfinite(motion.phase) ? motion.phase : 0.0;
    const double direction = motion.direction < 0.0 ? -1.0 : 1.0;
    const double bob = std::sin(time * (sleeping ? 1.4 : 2.2))
                     * (sleeping ? 1.5 : 3.0);

    // Ground glow stays still while the body breathes above it.
    const double shadowWidth = 124.0 - 42.0 * lift;
    const double shadowHeight = 18.0 - 12.0 * lift;
    const double shadowY = 235.0 + 17.0 * lift;
    const double shadowOpacity = 1.0 - 0.85 * lift;
    QRadialGradient shadow(QPointF(120, shadowY), shadowWidth / 2.0);
    shadow.setColorAt(0, QColor(20, 61, 77, qRound(65.0 * shadowOpacity)));
    shadow.setColorAt(0.65, QColor(20, 61, 77, qRound(22.0 * shadowOpacity)));
    shadow.setColorAt(1, QColor(20, 61, 77, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(shadow);
    painter.drawEllipse(QRectF(120 - shadowWidth / 2.0,
                              shadowY - shadowHeight / 2.0, shadowWidth, shadowHeight));

    painter.save();
    const double happyBounce = pose == PetPose::Happy && time < 0.65
        ? -7.0 * std::sin(pi * time / 0.65) : 0.0;
    const double runningBounce = running ? -5.0 * std::abs(std::sin(runPhase)) : 0.0;
    painter.translate(0, (bob + happyBounce + runningBounce + (sleeping ? 3.0 : 0.0))
                         * (1.0 - lift));
    if (lift > 0.0) {
        // 头顶是固定的悬挂支点，防止视觉抓取点随着动画离开指尖。
        painter.translate(120, 63);
        painter.rotate(sway);
        painter.translate(-120, -63);
    }
    if (running) {
        painter.translate(120, 190);
        painter.rotate(direction * 5.0);
        painter.translate(-120, -190);
        painter.setPen(QPen(QColor(118, 213, 255, 130), 2.0, Qt::SolidLine, Qt::RoundCap));
        for (int i = 0; i < 3; ++i) {
            const double trailX = 120 - direction * (60 + i * 9);
            painter.drawLine(QPointF(trailX, 215 - i * 5),
                             QPointF(trailX - direction * 12, 215 - i * 5));
        }
    }

    if (pose == PetPose::Happy) drawHappyBurst(painter, time);
    else if (pose == PetPose::Wave) drawWaveTrail(painter, time);

    // Feet and articulated arms sit behind the torso.
    for (int leg = 0; leg < 2; ++leg) {
        painter.save();
        const double phase = runPhase + leg * pi;
        if (running) painter.translate(std::sin(phase) * 8.0,
                                       -std::max(0.0, std::cos(phase)) * 4.0);
        const double legX = leg == 0 ? 87.0 : 127.0;
        if (lift > 0.0) {
            const double legCenter = legX + 13.0;
            painter.translate(legCenter, 207);
            painter.rotate((leg == 0 ? -13.0 : 13.0) * lift);
            painter.translate(-legCenter, -207);
            // 细腿从躯干下方垂下，脚尖朝下，不再保持站立的平脚。
            painter.setPen(QPen(outline, 7.0, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(legCenter, 209), QPointF(legCenter, 229));
            painter.setPen(QPen(QColor("#a9cbd1"), 3.0, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(legCenter, 210), QPointF(legCenter, 229));
        }
        painter.setPen(QPen(outline, 2.4));
        painter.setBrush(shellGradient(204, 230));
        painter.drawRoundedRect(QRectF(legX + 4.0 * lift, 206 + 12.0 * lift,
                                      26 - 8.0 * lift, 23 + 2.0 * lift), 9, 9);
        painter.setPen(QPen(QColor("#82a9b1"), 2, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(legX + 6 + 3.0 * lift, 224 + 12.0 * lift),
                         QPointF(legX + 20 - 3.0 * lift, 224 + 12.0 * lift));
        painter.restore();
    }
    drawArm(painter, false, pose, time, running, runPhase, lift);
    drawArm(painter, true, pose, time, running, runPhase, lift);

    painter.save();
    const double bodyDrop = 10.0 * lift;
    painter.translate(0, bodyDrop);
    painter.setPen(QPen(outline, 2.5));
    painter.setBrush(shellGradient(145, 215));
    painter.drawRoundedRect(QRectF(77, 145, 86, 70 + 6.0 * lift), 24, 24);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#93b9c2"));
    painter.drawRoundedRect(QRectF(105, 141 - bodyDrop, 30, 12 + bodyDrop), 4, 4);

    // A compact illuminated chest panel gives the robot its own identity.
    painter.setBrush(dark);
    painter.drawRoundedRect(QRectF(98, 167, 44, 29), 9, 9);
    painter.setBrush(QColor("#25485a"));
    painter.drawRoundedRect(QRectF(102, 171, 36, 21), 6, 6);
    painter.setPen(QPen(mint, 2.4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(111, 181), QPointF(117, 187));
    painter.drawLine(QPointF(117, 187), QPointF(129, 176));
    painter.setPen(QPen(QColor("#96b8c0"), 1.5));
    painter.drawLine(QPointF(110, 205), QPointF(130, 205));
    painter.restore();

    // Cat-like antenna fins, inset with mint light.
    painter.setPen(QPen(outline, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(shellGradient(37, 84));
    painter.drawPolygon(QPolygonF{QPointF(57, 83), QPointF(53, 41), QPointF(91, 67)});
    painter.drawPolygon(QPolygonF{QPointF(149, 67), QPointF(187, 41), QPointF(183, 83)});
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#8ee5d8"));
    painter.drawPolygon(QPolygonF{QPointF(64, 71), QPointF(62, 56), QPointF(77, 68)});
    painter.drawPolygon(QPolygonF{QPointF(163, 68), QPointF(178, 56), QPointF(176, 71)});

    painter.setPen(QPen(outline, 2.6));
    painter.setBrush(shellGradient(62, 153));
    painter.drawRoundedRect(QRectF(46, 62, 148, 92), 30, 30);

    // Side ports and face bezel.
    painter.setBrush(QColor("#a9cbd1"));
    painter.drawRoundedRect(QRectF(39, 94, 12, 28), 5, 5);
    painter.drawRoundedRect(QRectF(189, 94, 12, 28), 5, 5);
    painter.setBrush(QColor("#87afb9"));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRectF(57, 76, 126, 66), 22, 22);
    QLinearGradient screen(0, 80, 0, 139);
    screen.setColorAt(0, QColor("#254658"));
    screen.setColorAt(1, dark);
    painter.setBrush(screen);
    painter.drawRoundedRect(QRectF(60, 79, 120, 60), 19, 19);
    painter.setPen(QPen(QColor(255, 255, 255, 30), 2, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(77, 86), QPointF(108, 86));
    drawEyes(painter, pose, time, gazePosition, following);
    if (following) {
        // 固定的蓝色四角标记，让正看向中心时也能辨认跟随状态。
        painter.setPen(QPen(QColor("#76d5ff"), 1.8, Qt::SolidLine, Qt::RoundCap));
        for (double edgeX : {66.0, 174.0}) {
            for (double edgeY : {85.0, 133.0}) {
                const QPointF corner(edgeX, edgeY);
                painter.drawLine(corner, corner + QPointF(edgeX < 120 ? 6 : -6, 0));
                painter.drawLine(corner, corner + QPointF(0, edgeY < 110 ? 6 : -6));
            }
        }
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 152, 164, pose == PetPose::Happy ? 150 : 85));
    painter.drawEllipse(QRectF(70, 121, 12, 5));
    painter.drawEllipse(QRectF(158, 121, 12, 5));
    painter.setBrush(mint);
    painter.drawEllipse(QPointF(120, 70), 2, 2);

    if (lift > 0.0) {
        painter.save();
        painter.setOpacity(lift);
        // 两侧夹持臂抱住头顶，明确表示“被捏住提起”，而非跟随光标。
        const QColor grip("#ad78df");
        painter.setPen(QPen(grip, 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(QColor(225, 201, 250, 170));
        QPainterPath clasp;
        clasp.moveTo(120, 45);
        clasp.lineTo(120, 53);
        clasp.moveTo(120, 53);
        clasp.quadTo(104, 52, 103, 65);
        clasp.lineTo(109, 69);
        clasp.moveTo(120, 53);
        clasp.quadTo(136, 52, 137, 65);
        clasp.lineTo(131, 69);
        painter.drawPath(clasp);
        painter.setPen(QPen(QColor("#e9d9ff"), 2.0));
        painter.drawEllipse(QPointF(120, 63), 5, 5);
        painter.setPen(QPen(QColor(173, 120, 223, 150), 2.0, Qt::SolidLine, Qt::RoundCap));
        for (double side : {-1.0, 1.0}) {
            const double x = 120 + side * 67;
            painter.drawLine(QPointF(x, 183), QPointF(x, 169));
            painter.drawLine(QPointF(x, 169), QPointF(x - side * 4, 174));
        }
        painter.restore();
    }

    if (pose == PetPose::Happy) {
        painter.setBrush(QColor("#ffe599"));
        drawStar(painter, QPointF(26, 100 + std::sin(time * 4) * 3), 7);
        drawStar(painter, QPointF(212, 65 - std::sin(time * 4) * 3), 6);
    } else if (sleeping) {
        painter.setPen(QColor("#68a9b4"));
        QFont font = painter.font();
        font.setBold(true);
        font.setPixelSize(14);
        painter.setFont(font);
        painter.drawText(QPointF(197, 73 - std::fmod(time * 6.0, 12.0)),
                         QStringLiteral("z"));
        font.setPixelSize(19);
        painter.setFont(font);
        painter.drawText(QPointF(211, 55 - std::fmod(time * 6.0, 12.0)),
                         QStringLiteral("z"));
    } else if (pose == PetPose::Surprise) {
        painter.setPen(QPen(QColor("#f6c671"), 4, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(208, 63), QPointF(212, 48));
        painter.drawPoint(QPointF(205, 71));
    }

    painter.restore();
    painter.restore();
}

} // namespace neko::desktop
