#pragma once

#include "visual_event.hpp"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace neko::desktop {

struct CameraOptions {
    QString modelPath;
    int cameraIndex = 0;
};

// One session belongs entirely to the capture thread. Tests can supply frames
// as presence values without accessing a physical camera.
class CameraSession {
public:
    virtual ~CameraSession() = default;
    virtual void open() = 0;
    virtual bool readPresence(bool& hasHand) = 0;
};

using CameraSessionFactory = std::function<std::unique_ptr<CameraSession>()>;
class CameraWorker;

class CameraController final : public QObject {
    Q_OBJECT

public:
    explicit CameraController(CameraOptions options, QObject* parent = nullptr);
    explicit CameraController(CameraSessionFactory factory, QObject* parent = nullptr);
    ~CameraController() override;

    bool active() const { return active_; }
    bool stopping() const { return stopping_; }
    bool busy() const { return worker_ != nullptr; }

    void setEnabled(bool enabled);
    void shutdown();

signals:
    void visualEvent(neko::desktop::VisualEvent event);
    void statusChanged(const QString& status);
    void controlsChanged(bool checked, bool enabled);

private:
    void start();
    void stop();

    CameraSessionFactory factory_;
    CameraWorker* worker_ = nullptr;
    bool active_ = false;
    bool stopping_ = false;
    bool failed_ = false;
};

} // namespace neko::desktop
