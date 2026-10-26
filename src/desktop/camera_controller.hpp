#pragma once

#include "visual_event.hpp"
#include "hand.hpp"

#include <QObject>
#include <QImage>
#include <QPointF>
#include <QString>

#include <functional>
#include <memory>

namespace neko::desktop {

struct CameraOptions {
    QString modelPath;         // 手掌检测模型路径
    int cameraIndex = 0;       // 摄像头编号
    QString handPoseModelPath; // 手部姿态模型路径
};

// 桌面与预览共用同一种帧结果，保留名称供摄像头会话接口使用。
using CameraFrameResult = vision::VisionFrameResult;

// One session belongs entirely to the capture thread. Tests can supply frame
// results without accessing a physical camera.
class CameraSession {
public:
    virtual ~CameraSession() = default;
    virtual void open() = 0;
    // 接收参数：用于接收当前帧的全部手部记录、汇总手势、尺寸和时间。
    // 返回值：是否成功读取并处理一帧。
    virtual bool readFrame(CameraFrameResult& result) = 0;
    // 返回最近一次识别对应的原图；不重新采集，仅供当前后台线程绘图。
    virtual cv::Mat frameForPreview() const { return {}; }
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
    void setDiagnosticsEnabled(bool enabled) { diagnosticsEnabled_ = enabled; }
    // 只控制画面显示，不启动或关闭摄像头。
    void setPreviewEnabled(bool enabled);
    bool previewEnabled() const { return previewEnabled_; }

signals:
    void visualEvent(neko::desktop::VisualEvent event);
    // 接收参数：是否跟随、0～1的位置、是否仅有预测；不携带图像。
    void gazeChanged(bool active, const QPointF& position, bool predicted);
    void frameDiagnostics(const QString& record);
    // 图像拥有独立像素数据；空图像表示清除上一帧。
    void previewFrameReady(const QImage& image);
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
    bool diagnosticsEnabled_ = false;
    bool previewEnabled_ = false;
    quint64 previewRevision_ = 0;
    quint64 sessionRevision_ = 0;
};

} // namespace neko::desktop
