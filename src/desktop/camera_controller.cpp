#include "camera_controller.hpp"

#include "hand_presence.hpp"
#include "hand_pose.hpp"
#include "hand_gesture_state.hpp"
#include "hand_position.hpp"
#include "hand_wave.hpp"
#include "palm_detector.hpp"

#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <stdexcept>
#include <utility>
#include <chrono>
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

    // 接收参数：用于接收这一帧的存在情况、汇总手势、单手姿态和时间。
    bool readFrame(CameraFrameResult& result) override
    {
        result = CameraFrameResult{};
        cv::Mat frame;
        if (!camera_.read(frame) || frame.empty()) return false;
        cv::flip(frame, frame, 1);
        result.frameSize = frame.size();

        // Reuse the student's three-stage pipeline without changing it.
        const auto input = vision::preprocessPalmFrame(frame);
        const auto raw = vision::runPalmInference(model_, input.inputBlob);
        const auto detections = vision::decodePalms(raw, input, anchors_);
        result.hasHand = !detections.empty();
        result.handCount = static_cast<int>(detections.size());

        bool hasOpenPalm = false;
        bool hasFist = false;
        bool hasPointing = false;
        for (const auto& palm : detections) {
            if (QThread::currentThread()->isInterruptionRequested()) return true;
            const auto handInput = vision::preprocessHandPose(frame, palm);
            if (handInput.inputBlob.empty()) continue;
            const auto handRaw = vision::runHandPoseInference(handPoseModel_, handInput.inputBlob);
            const auto hand = vision::decodeHandPose(handRaw, handInput);
            const auto gesture = vision::classifyHandGesture(hand);
            if (result.handCount == 1) result.singleHand = hand;
            hasOpenPalm |= gesture == vision::HandGesture::OpenPalm;
            hasFist |= gesture == vision::HandGesture::Fist;
            hasPointing |= gesture == vision::HandGesture::Pointing;
        }

        // 与视觉预览一致：只接受整帧唯一的已识别类别。
        if (hasOpenPalm && !hasFist && !hasPointing)
            result.gesture = vision::HandGesture::OpenPalm;
        else if (hasFist && !hasOpenPalm && !hasPointing)
            result.gesture = vision::HandGesture::Fist;
        else if (hasPointing && !hasOpenPalm && !hasFist)
            result.gesture = vision::HandGesture::Pointing;
        result.timeSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - clockStart_).count();
        return true;
    }

private:
    CameraOptions options_;
    cv::dnn::Net model_;
    cv::dnn::Net handPoseModel_;
    std::vector<cv::Point2f> anchors_;
    cv::VideoCapture camera_;
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

signals:
    void ready();
    void presenceChanged(bool present);
    // 接收参数：视觉模块确认后，准备发送给桌面的静态或动态手势事件。
    void gestureConfirmed(neko::desktop::VisualEvent event);
    void gazeChanged(bool active, const QPointF& position);
    void frameDiagnostics(const QString& record);
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

            vision::HandPresenceState state;
            vision::HandGestureState gestureState;
            vision::HandWaveState waveState;
            bool gazeActive = false;
            double lastPointingSeconds = 0.0;
            QElapsedTimer diagnosticClock;
            diagnosticClock.start();
            QString previousDiagnosticState;
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
                const auto event = vision::updateHandPresence(state, frame.hasHand);
                if (event == vision::HandPresenceEvent::Appeared) emit presenceChanged(true);
                else if (event == vision::HandPresenceEvent::Disappeared) emit presenceChanged(false);

                // 无手帧同样更新，用于清除旧手势并允许下次重新确认。
                const auto gestureEvent = vision::updateHandGesture(
                    gestureState, frame.hasHand ? frame.gesture : vision::HandGesture::Unknown);
                if (gestureEvent == vision::HandGestureEvent::OpenPalmConfirmed) {
                    emit gestureConfirmed(VisualEvent::OpenPalmConfirmed);
                } else if (gestureEvent == vision::HandGestureEvent::FistConfirmed) {
                    emit gestureConfirmed(VisualEvent::FistConfirmed);
                }
                // Cleared 仅清除内部状态，避免覆盖“手离开”的惊讶动作。

                // 与预览一致：多手立即重置，短暂缺失由视觉函数处理。
                if (frame.handCount > 1) {
                    waveState.initialized = false;
                    waveState.direction = vision::HandMoveDirection::None;
                    waveState.turnCount = 0;
                }
                const bool singleHand = frame.hasHand && frame.handCount == 1;
                const auto position = vision::calculatePointingPosition(
                    frame.singleHand,
                    singleHand ? frame.gesture : vision::HandGesture::Unknown,
                    frame.frameSize);
                if (position.valid) {
                    // 与预览直接使用位置函数的结果一致，不再额外要求三帧。
                    gazeActive = true;
                    lastPointingSeconds = frame.timeSeconds;
                    emit gazeChanged(true, QPointF(position.position.x, position.position.y));
                } else {
                    const auto& root = frame.singleHand.landmarks[5];
                    const bool corruptPosition = frame.singleHand.valid &&
                        (!std::isfinite(root.x) || !std::isfinite(root.y));
                    const bool anotherGesture = singleHand && frame.singleHand.valid &&
                        (frame.gesture == vision::HandGesture::OpenPalm ||
                         frame.gesture == vision::HandGesture::Fist);
                    const bool stopImmediately = frame.handCount > 1 || anotherGesture ||
                        frame.frameSize.width <= 0 || frame.frameSize.height <= 0 || corruptPosition;
                    // 短暂丢失只保留最后的真实位置，不更新或预测坐标。
                    if (gazeActive && (stopImmediately ||
                        frame.timeSeconds - lastPointingSeconds > 0.5)) {
                        gazeActive = false;
                        emit gazeChanged(false, QPointF(0.5, 0.5));
                    }
                }
                const auto waveEvent = vision::updateHandWave(
                    waveState,
                    singleHand ? frame.singleHand : vision::HandPoseResult{},
                    singleHand ? frame.gesture : vision::HandGesture::Unknown,
                    frame.timeSeconds);
                if (waveEvent == vision::HandWaveEvent::Waved) {
                    emit gestureConfirmed(VisualEvent::HandWave);
                }
                if (diagnosticsEnabled_) {
                    const QString key = QStringLiteral("%1/%2/%3/%4/%5")
                        .arg(frame.handCount).arg(frame.singleHand.valid)
                        .arg(QString::fromLatin1(gestureName(frame.gesture)))
                        .arg(gazeActive).arg(waveState.turnCount);
                    if (key != previousDiagnosticState || diagnosticClock.elapsed() >= 250 ||
                        waveEvent == vision::HandWaveEvent::Waved) {
                        emit frameDiagnostics(QStringLiteral(
                            "frame t=%1 read_infer_ms=%2 size=%3x%4 hands=%5 pose_valid=%6 "
                            "confidence=%7 raw=%8 pointing_valid=%9 gaze_active=%10 "
                            "root=(%11,%12) turns=%13 waved=%14 wrist_x=%15 "
                            "wave_threshold=%16 wave_direction=%17")
                            .arg(frame.timeSeconds, 0, 'f', 3).arg(processingClock.elapsed())
                            .arg(frame.frameSize.width).arg(frame.frameSize.height)
                            .arg(frame.handCount).arg(frame.singleHand.valid)
                            .arg(frame.singleHand.confidence, 0, 'f', 3)
                            .arg(QString::fromLatin1(gestureName(frame.gesture)))
                            .arg(position.valid).arg(gazeActive).arg(frame.singleHand.landmarks[5].x)
                            .arg(frame.singleHand.landmarks[5].y).arg(waveState.turnCount)
                            .arg(waveEvent == vision::HandWaveEvent::Waved)
                            .arg(frame.singleHand.landmarks[0].x).arg(waveState.moveThreshold)
                            .arg(static_cast<int>(waveState.direction)));
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

void CameraController::start()
{
    if (worker_) return;
    auto* worker = new CameraWorker(factory_, this, diagnosticsEnabled_);
    worker_ = worker;
    active_ = true;
    stopping_ = false;
    failed_ = false;
    emit gazeChanged(false, QPointF(0.5, 0.5));

    connect(worker, &CameraWorker::ready, this, [this, worker] {
        if (worker_ == worker && active_ && !stopping_) {
            emit statusChanged(QStringLiteral("正在识别手势"));
        }
    }, Qt::QueuedConnection);
    connect(worker, &CameraWorker::presenceChanged, this, [this, worker](bool present) {
        if (worker_ == worker && active_ && !stopping_) {
            emit visualEvent(present ? VisualEvent::HandDetected : VisualEvent::HandLost);
        }
    }, Qt::QueuedConnection);
    connect(worker, &CameraWorker::gestureConfirmed, this, [this, worker](VisualEvent event) {
        if (worker_ == worker && active_ && !stopping_) {
            emit visualEvent(event);
        }
    }, Qt::QueuedConnection);
    connect(worker, &CameraWorker::gazeChanged, this,
            [this, worker](bool active, const QPointF& position) {
        if (worker_ == worker && active_ && !stopping_) {
            emit gazeChanged(active, position);
        }
    }, Qt::QueuedConnection);
    connect(worker, &CameraWorker::frameDiagnostics, this, [this, worker](const QString& record) {
        if (worker_ == worker && active_ && !stopping_) emit frameDiagnostics(record);
    }, Qt::QueuedConnection);
    connect(worker, &CameraWorker::failed, this, [this, worker](const QString& message) {
        if (worker_ != worker || !active_ || stopping_) return;
        failed_ = true;
        active_ = false;
        emit gazeChanged(false, QPointF(0.5, 0.5));
        emit statusChanged(message);
        emit controlsChanged(false, false);
    }, Qt::QueuedConnection);
    connect(worker, &QThread::finished, this, [this, worker] {
        if (worker_ != worker) return;
        worker_ = nullptr;
        active_ = false;
        stopping_ = false;
        emit gazeChanged(false, QPointF(0.5, 0.5));
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
    active_ = false;
    stopping_ = true;
    emit gazeChanged(false, QPointF(0.5, 0.5));
    worker_->requestInterruption();
    emit controlsChanged(false, false);
    emit statusChanged(QStringLiteral("正在关闭"));
}

void CameraController::shutdown()
{
    Q_ASSERT(QThread::currentThread() == thread());
    emit gazeChanged(false, QPointF(0.5, 0.5));
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
