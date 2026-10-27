#include "camera_controller.hpp"

#include "hand_interaction.hpp"
#include "hand_preview.hpp"

#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <stdexcept>
#include <utility>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <cmath>

namespace neko::desktop {
namespace {

const char* gestureName(vision::HandGesture gesture)
{
    switch (gesture) {
    case vision::HandGesture::OpenPalm: return "OpenPalm";
    case vision::HandGesture::Fist: return "Fist";
    case vision::HandGesture::Pointing: return "Pointing";
    default: return "Unknown";
    }
}

class CameraFailure final : public std::runtime_error {
public:
    explicit CameraFailure(QString message)
        : std::runtime_error(message.toStdString()), message_(std::move(message)) {}
    QString message() const { return message_; }

private:
    QString message_;
};

class OpenCvCameraSession final : public CameraSession {
public:
    explicit OpenCvCameraSession(CameraOptions options) : options_(std::move(options)) {}

    void open() override
    {
        // Validate before accessing hardware, including when launched outside
        // the project directory.
        if (!QFileInfo(options_.modelPath).isFile()) {
            throw CameraFailure(QStringLiteral("手掌检测模型文件不存在，请检查 models 文件夹"));
        }
        if (!QFileInfo(options_.handPoseModelPath).isFile()) {
            throw CameraFailure(QStringLiteral("手部姿态模型文件不存在，请检查 models 文件夹"));
        }
        model_ = vision::loadPalmModel(options_.modelPath.toStdString());
        if (QThread::currentThread()->isInterruptionRequested()) return;
        handPoseModel_ = vision::loadHandPoseModel(options_.handPoseModelPath.toStdString());
        anchors_ = vision::createPalmAnchors();
        if (QThread::currentThread()->isInterruptionRequested()) return;
        if (!camera_.open(options_.cameraIndex, cv::CAP_DSHOW)) {
            throw CameraFailure(QStringLiteral("无法打开摄像头，请检查连接或是否被占用"));
        }
        clockStart_ = std::chrono::steady_clock::now();
    }

    // 采集和镜像由会话负责，识别顺序与预览使用同一个入口。
    bool readFrame(CameraFrameResult& result) override
    {
        result = CameraFrameResult{};
        if (!camera_.read(frame_) || frame_.empty()) return false;
        cv::flip(frame_, frame_, 1);
        const double nowSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - clockStart_).count();
        result = vision::analyzeVisionFrame(
            frame_, model_, handPoseModel_, anchors_, nowSeconds);
        return true;
    }

    cv::Mat frameForPreview() const override { return frame_; }

private:
    CameraOptions options_;
    cv::dnn::Net model_;
    cv::dnn::Net handPoseModel_;
    std::vector<cv::Point2f> anchors_;
    cv::VideoCapture camera_;
    cv::Mat frame_;
    std::chrono::steady_clock::time_point clockStart_;
};

} // namespace

// No QWidget or HighGUI calls are made in this thread. Capture, inference,
// presence state and resource destruction all remain on the same worker.
class CameraWorker final : public QThread {
    Q_OBJECT

public:
    explicit CameraWorker(CameraSessionFactory factory, QObject* parent, bool diagnosticsEnabled)
        : QThread(parent), factory_(std::move(factory)), diagnosticsEnabled_(diagnosticsEnabled) {}

    // 主线程只修改原子开关，不接触后台使用的图像或视觉状态。
    void setPreviewGeneration(quint64 generation) { previewGeneration_.store(generation); }
    void acknowledgePreview() { previewPending_.store(false); }

signals:
    void ready();
    void presenceChanged(bool present);
    // 接收参数：视觉模块确认后，准备发送给桌面的静态或动态手势事件。
    void gestureConfirmed(neko::desktop::VisualEvent event);
    void gazeChanged(bool active, const QPointF& position, bool predicted);
    void pinchChanged(bool active, const QPointF& position, bool measured);
    void pinchPointerChanged(bool visible, const QPointF& position);
    void frameDiagnostics(const QString& record);
    void previewReady(quint64 generation, const QImage& image);
    void failed(const QString& message);

protected:
    void run() override
    {
        try {
            if (isInterruptionRequested()) return;
            auto session = factory_();
            if (!session) throw std::runtime_error("Camera session factory returned null");
            session->open();
            if (isInterruptionRequested()) return;
            emit ready();

            // 跨帧状态归统一视觉模块管理，每次摄像头会话独立创建。
            vision::HandInteractionState interactionState;
            bool gazeActive = false;
            bool pinchActive = false;
            QPointF lastPinchPosition;
            QElapsedTimer diagnosticClock;
            diagnosticClock.start();
            QString previousDiagnosticState;
            QElapsedTimer previewClock;
            while (!isInterruptionRequested()) {
                CameraFrameResult frame;
                QElapsedTimer processingClock;
                if (diagnosticsEnabled_) processingClock.start();
                if (!session->readFrame(frame)) {
                    if (!isInterruptionRequested()) {
                        throw CameraFailure(QStringLiteral("摄像头读取中断，请关闭后重新开启"));
                    }
                    break;
                }
                if (isInterruptionRequested()) break;
                // 与预览共用整套跨帧处理，空手帧也更新预测和失踪计时。
                const auto interaction = vision::updateHandInteraction(interactionState, frame);
                // 对准光标复用本帧已有的真实测量，在确认捏合前也可显示。
                const auto* singleHand = frame.hands.size() == 1 ? &frame.hands.front() : nullptr;
                const bool pointerVisible = singleHand && singleHand->pinch.valid &&
                    std::isfinite(singleHand->pinch.position.x) &&
                    std::isfinite(singleHand->pinch.position.y);
                emit pinchPointerChanged(pointerVisible, pointerVisible
                    ? QPointF(singleHand->pinch.position.x, singleHand->pinch.position.y) : QPointF());
                // 直接使用统一视觉入口的状态和位置，不重复计算指尖或判断捏合。
                const auto& pinch = interaction.pinchPosition;
                if (interaction.pinching) {
                    const bool measured = pinch.valid &&
                        std::isfinite(pinch.position.x) && std::isfinite(pinch.position.y);
                    if (measured) {
                        lastPinchPosition = QPointF(std::clamp(pinch.position.x, 0.0f, 1.0f),
                                                   std::clamp(pinch.position.y, 0.0f, 1.0f));
                    }
                    pinchActive = true;
                    emit pinchChanged(true, lastPinchPosition, measured);
                } else if (pinchActive) {
                    pinchActive = false;
                    emit pinchChanged(false, QPointF(), false);
                }
                if (interaction.presenceEvent == vision::HandPresenceEvent::Appeared)
                    emit presenceChanged(true);
                else if (interaction.presenceEvent == vision::HandPresenceEvent::Disappeared)
                    emit presenceChanged(false);

                if (interaction.gestureEvent == vision::HandGestureEvent::OpenPalmConfirmed) {
                    emit gestureConfirmed(VisualEvent::OpenPalmConfirmed);
                } else if (interaction.gestureEvent == vision::HandGestureEvent::FistConfirmed) {
                    emit gestureConfirmed(VisualEvent::FistConfirmed);
                }
                // Cleared 仅清除内部状态，避免覆盖“手离开”的惊讶动作。

                // 直接传递视觉模块的校正/预测结果，不另加识别门槛或丢失计时。
                const auto& tracking = interaction.tracking;
                if (tracking.valid && !interaction.pinching) {
                    gazeActive = true;
                    emit gazeChanged(true,
                        QPointF(tracking.position.x, tracking.position.y), tracking.predicted);
                } else if (gazeActive) {
                    gazeActive = false;
                    emit gazeChanged(false, QPointF(0.5, 0.5), false);
                }
                if (interaction.waveEvent == vision::HandWaveEvent::Waved) {
                    emit gestureConfirmed(VisualEvent::HandWave);
                }
                // 同一帧、同一份识别及交互结果，仅追加绘图，不重复推理。
                // 限制刷新频率并只允许一帧排队，避免显示拖慢交互。
                const quint64 generation = previewGeneration_.load();
                if (generation != 0 &&
                    (!previewClock.isValid() || previewClock.elapsed() >= 80) &&
                    !previewPending_.exchange(true)) {
                    const cv::Mat source = session->frameForPreview();
                    if (!source.empty() && source.type() == CV_8UC3) {
                        const cv::Mat annotated = vision::drawHandPreview(source, frame, interaction);
                        cv::Mat rgb;
                        cv::cvtColor(annotated, rgb, cv::COLOR_BGR2RGB);
                        // copy() 让排队传递的图像不依赖下一帧复用的 Mat 内存。
                        const QImage image = QImage(rgb.data, rgb.cols, rgb.rows,
                            static_cast<qsizetype>(rgb.step), QImage::Format_RGB888).copy();
                        if (previewGeneration_.load() == generation && !isInterruptionRequested()) {
                            emit previewReady(generation, image);
                            previewClock.start();
                        } else {
                            acknowledgePreview();
                        }
                    } else {
                        acknowledgePreview();
                    }
                }
                if (diagnosticsEnabled_) {
                    const vision::HandFrameResult noHand{};
                    const auto& hand = frame.hands.size() == 1 ? frame.hands.front() : noHand;
                    const auto& wave = interactionState.wave;
                    const QString key = QStringLiteral("%1/%2/%3/%4/%5/%6/%7/%8/%9")
                        .arg(static_cast<qulonglong>(frame.hands.size())).arg(hand.pose.valid)
                        .arg(QString::fromLatin1(gestureName(frame.frameGesture)))
                        .arg(gazeActive).arg(wave.turnCount)
                        .arg(hand.pointing.valid).arg(tracking.predicted)
                        .arg(interaction.pinching).arg(pinch.valid);
                    if (key != previousDiagnosticState || diagnosticClock.elapsed() >= 250 ||
                        interaction.waveEvent == vision::HandWaveEvent::Waved) {
                        emit frameDiagnostics(QStringLiteral(
                            "frame t=%1 read_infer_ms=%2 size=%3x%4 hands=%5 pose_valid=%6 "
                            "confidence=%7 raw=%8 pointing_valid=%9 gaze_active=%10 "
                            "measurement=(%11,%12) turns=%13 waved=%14 wrist_x=%15 "
                            "wave_threshold=%16 wave_direction=%17 tracking_valid=%18 "
                            "predicted=%19 target=(%20,%21) pinching=%22 "
                            "pinch_position_valid=%23 pinch=(%24,%25) pinch_event=%26")
                            .arg(frame.timeSeconds, 0, 'f', 3).arg(processingClock.elapsed())
                            .arg(frame.frameSize.width).arg(frame.frameSize.height)
                            .arg(static_cast<qulonglong>(frame.hands.size())).arg(hand.pose.valid)
                            .arg(hand.pose.confidence, 0, 'f', 3)
                            .arg(QString::fromLatin1(gestureName(frame.frameGesture)))
                            .arg(hand.pointing.valid).arg(gazeActive).arg(hand.pointing.position.x)
                            .arg(hand.pointing.position.y).arg(wave.turnCount)
                            .arg(interaction.waveEvent == vision::HandWaveEvent::Waved)
                            .arg(hand.pose.landmarks[0].x).arg(wave.moveThreshold)
                            .arg(static_cast<int>(wave.direction)).arg(tracking.valid)
                            .arg(tracking.predicted).arg(tracking.position.x)
                            .arg(tracking.position.y).arg(interaction.pinching).arg(pinch.valid)
                            .arg(pinch.position.x).arg(pinch.position.y)
                            .arg(static_cast<int>(interaction.pinchEvent)));
                        previousDiagnosticState = key;
                        diagnosticClock.restart();
                    }
                }
            }
        } catch (const CameraFailure& error) {
            if (!isInterruptionRequested()) emit failed(error.message());
        } catch (const std::exception& error) {
            qWarning() << "Camera processing failed:" << error.what();
            if (!isInterruptionRequested()) {
                emit failed(QStringLiteral("视觉处理失败，请检查模型文件和摄像头"));
            }
        }
    }

private:
    CameraSessionFactory factory_;
    bool diagnosticsEnabled_ = false;
    std::atomic<quint64> previewGeneration_{0};
    std::atomic<bool> previewPending_{false};
};

CameraController::CameraController(CameraOptions options, QObject* parent)
    : CameraController(CameraSessionFactory([options = std::move(options)] {
          return std::make_unique<OpenCvCameraSession>(options);
      }), parent)
{}

CameraController::CameraController(CameraSessionFactory factory, QObject* parent)
    : QObject(parent), factory_(std::move(factory))
{
    qRegisterMetaType<VisualEvent>();
}

CameraController::~CameraController()
{
    shutdown();
}

void CameraController::setEnabled(bool enabled)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (enabled) start();
    else stop();
}

void CameraController::setPreviewEnabled(bool enabled)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (previewEnabled_ == enabled) return;
    previewEnabled_ = enabled;
    ++previewRevision_;
    if (worker_) worker_->setPreviewGeneration(enabled ? previewRevision_ : 0);
    emit previewFrameReady(QImage{});
}

void CameraController::start()
{
    if (worker_) return;
    const quint64 session = ++sessionRevision_;
    ++previewRevision_;
    auto* worker = new CameraWorker(factory_, this, diagnosticsEnabled_);
    worker->setPreviewGeneration(previewEnabled_ ? previewRevision_ : 0);
    worker_ = worker;
    active_ = true;
    stopping_ = false;
    failed_ = false;
    emit gazeChanged(false, QPointF(0.5, 0.5), false);
    emit pinchChanged(false, QPointF(), false);
    emit pinchPointerChanged(false, QPointF());
    emit previewFrameReady(QImage{});

    connect(worker, &CameraWorker::ready, this, [this, worker, session] {
        if (sessionRevision_ == session && worker_ == worker && active_ && !stopping_) {
            emit statusChanged(QStringLiteral("正在识别手势"));
        }
    }, Qt::QueuedConnection);
    connect(worker, &CameraWorker::presenceChanged, this, [this, worker, session](bool present) {
        if (sessionRevision_ == session && worker_ == worker && active_ && !stopping_) {
            emit visualEvent(present ? VisualEvent::HandDetected : VisualEvent::HandLost);
        }
    }, Qt::QueuedConnection);
    connect(worker, &CameraWorker::gestureConfirmed, this, [this, worker, session](VisualEvent event) {
        if (sessionRevision_ == session && worker_ == worker && active_ && !stopping_) {
            emit visualEvent(event);
        }
    }, Qt::QueuedConnection);
    connect(worker, &CameraWorker::gazeChanged, this,
            [this, worker, session](bool active, const QPointF& position, bool predicted) {
        if (sessionRevision_ == session && worker_ == worker && active_ && !stopping_) {
            emit gazeChanged(active, position, predicted);
        }
    }, Qt::QueuedConnection);
    connect(worker, &CameraWorker::frameDiagnostics, this, [this, worker, session](const QString& record) {
        if (sessionRevision_ == session && worker_ == worker && active_ && !stopping_)
            emit frameDiagnostics(record);
    }, Qt::QueuedConnection);
    connect(worker, &CameraWorker::pinchChanged, this,
            [this, worker, session](bool active, const QPointF& position, bool measured) {
        if (sessionRevision_ == session && worker_ == worker && active_ && !stopping_) {
            emit pinchChanged(active, position, measured);
        }
    }, Qt::QueuedConnection);
    connect(worker, &CameraWorker::pinchPointerChanged, this,
            [this, worker, session](bool visible, const QPointF& position) {
        if (sessionRevision_ == session && worker_ == worker && active_ && !stopping_) {
            emit pinchPointerChanged(visible, position);
        }
    }, Qt::QueuedConnection);
    connect(worker, &CameraWorker::previewReady, this,
            [this, worker, session](quint64 generation, const QImage& image) {
        if (sessionRevision_ != session || worker_ != worker) return;
        worker->acknowledgePreview();
        if (active_ && !stopping_ && previewEnabled_ && generation == previewRevision_) {
            emit previewFrameReady(image);
        }
    }, Qt::QueuedConnection);
    connect(worker, &CameraWorker::failed, this, [this, worker, session](const QString& message) {
        if (sessionRevision_ != session || worker_ != worker || !active_ || stopping_) return;
        failed_ = true;
        active_ = false;
        emit gazeChanged(false, QPointF(0.5, 0.5), false);
        emit pinchChanged(false, QPointF(), false);
        emit pinchPointerChanged(false, QPointF());
        emit previewFrameReady(QImage{});
        emit statusChanged(message);
        emit controlsChanged(false, false);
    }, Qt::QueuedConnection);
    connect(worker, &QThread::finished, this, [this, worker, session] {
        if (sessionRevision_ != session || worker_ != worker) return;
        worker_ = nullptr;
        active_ = false;
        stopping_ = false;
        emit gazeChanged(false, QPointF(0.5, 0.5), false);
        emit pinchChanged(false, QPointF(), false);
        emit pinchPointerChanged(false, QPointF());
        emit previewFrameReady(QImage{});
        if (!failed_) emit statusChanged(QStringLiteral("未开启"));
        emit controlsChanged(false, true);
        worker->deleteLater();
    }, Qt::QueuedConnection);

    emit statusChanged(QStringLiteral("正在开启"));
    emit controlsChanged(true, true);
    worker->start();
    if (stopping_) worker->requestInterruption();
}

void CameraController::stop()
{
    if (!worker_ || stopping_) return;
    ++previewRevision_;
    worker_->setPreviewGeneration(0);
    active_ = false;
    stopping_ = true;
    emit gazeChanged(false, QPointF(0.5, 0.5), false);
    emit pinchChanged(false, QPointF(), false);
    emit pinchPointerChanged(false, QPointF());
    emit previewFrameReady(QImage{});
    worker_->requestInterruption();
    emit controlsChanged(false, false);
    emit statusChanged(QStringLiteral("正在关闭"));
}

void CameraController::shutdown()
{
    Q_ASSERT(QThread::currentThread() == thread());
    // 失效旧会话已排队的回调，避免快速重启时地址复用造成串帧。
    ++sessionRevision_;
    ++previewRevision_;
    if (worker_) worker_->setPreviewGeneration(0);
    emit gazeChanged(false, QPointF(0.5, 0.5), false);
    emit pinchChanged(false, QPointF(), false);
    emit pinchPointerChanged(false, QPointF());
    emit previewFrameReady(QImage{});
    if (!worker_) return;
    disconnect(worker_, nullptr, this, nullptr);
    worker_->requestInterruption();
    // Never destroy or terminate a running capture thread. A driver call
    // already in progress must return before its resources can be released.
    worker_->wait();
    delete worker_;
    worker_ = nullptr;
    active_ = false;
    stopping_ = false;
}

} // namespace neko::desktop

#include "camera_controller.moc"
