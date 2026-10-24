#pragma once

#include "visual_event.hpp"
#include "hand_gesture.hpp"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace neko::desktop {

struct CameraOptions {
    QString modelPath;         // 手掌检测模型路径
    int cameraIndex = 0;       // 摄像头编号
    QString handPoseModelPath; // 手部姿态模型路径
};

// 一帧的视觉结果；静态手势已汇总多只手，单手姿态供动态挥手判断。
struct CameraFrameResult {
    bool hasHand = false;
    vision::HandGesture gesture = vision::HandGesture::Unknown;
    int handCount = 0;
    vision::HandPoseResult singleHand; // 只有一只手时提供，默认无效
    double timeSeconds = 0.0;         // 本次会话开始后的单调时间，单位为秒
};

// One session belongs entirely to the capture thread. Tests can supply frame
// results without accessing a physical camera.
class CameraSession {
public:
    virtual ~CameraSession() = default;
    virtual void open() = 0;
    // 接收参数：用于接收当前帧的存在情况、静态手势、单手姿态和时间。
    // 返回值：是否成功读取并处理一帧。
    virtual bool readFrame(CameraFrameResult& result) = 0;
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
