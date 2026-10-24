#pragma once

#include <QMetaType>

namespace neko::desktop {

// Detection and debouncing stay in vision.
// Deliver these events to PetWindow on the Qt GUI thread, using a queued call.
enum class VisualEvent {
    HandDetected,
    HandLost,
    HandWave,         // 确认动态挥手
    OpenPalmConfirmed, // 确认张手
    FistConfirmed      // 确认握拳
};

} // namespace neko::desktop

Q_DECLARE_METATYPE(neko::desktop::VisualEvent)
