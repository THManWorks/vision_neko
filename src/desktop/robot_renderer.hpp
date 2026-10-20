#pragma once

#include <QRectF>

class QPainter;

namespace neko::desktop {

enum class PetPose { Idle, Happy, Wave, Sleep, Surprise };

// Draws in logical coordinates, leaving breathing room around the character.
void paintRobot(QPainter& painter, const QRectF& bounds,
                PetPose pose, double elapsedSeconds);

} // namespace neko::desktop
