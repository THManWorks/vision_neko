#include "camera_controller.hpp"

#include "hand_presence.hpp"
#include "palm_detector.hpp"

#include <QDebug>
#include <QFileInfo>
#include <QThread>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <stdexcept>
#include <utility>

namespace neko::desktop {
namespace {

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
            throw CameraFailure(QStringLiteral("模型文件不存在，请检查 models 文件夹"));
        }
        model_ = vision::loadPalmModel(options_.modelPath.toStdString());
        anchors_ = vision::createPalmAnchors();
        if (QThread::currentThread()->isInterruptionRequested()) return;
        if (!camera_.open(options_.cameraIndex, cv::CAP_DSHOW)) {
            throw CameraFailure(QStringLiteral("无法打开摄像头，请检查连接或是否被占用"));
        }
    }

    bool readPresence(bool& hasHand) override
    {
        cv::Mat frame;
        if (!camera_.read(frame) || frame.empty()) return false;
        cv::flip(frame, frame, 1);

        // Reuse the student's three-stage pipeline without changing it.
        const auto input = vision::preprocessPalmFrame(frame);
        const auto raw = vision::runPalmInference(model_, input.inputBlob);
        const auto detections = vision::decodePalms(raw, input, anchors_);
        hasHand = !detections.empty();
        return true;
    }

private:
    CameraOptions options_;
    cv::dnn::Net model_;
    std::vector<cv::Point2f> anchors_;
    cv::VideoCapture camera_;
};

} // namespace

// No QWidget or HighGUI calls are made in this thread. Capture, inference,
// presence state and resource destruction all remain on the same worker.
class CameraWorker final : public QThread {
    Q_OBJECT

public:
    explicit CameraWorker(CameraSessionFactory factory, QObject* parent)
        : QThread(parent), factory_(std::move(factory)) {}

signals:
    void ready();
    void presenceChanged(bool present);
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
            while (!isInterruptionRequested()) {
                bool hasHand = false;
                if (!session->readPresence(hasHand)) {
                    if (!isInterruptionRequested()) {
                        throw CameraFailure(QStringLiteral("摄像头读取中断，请关闭后重新开启"));
                    }
                    break;
                }
                if (isInterruptionRequested()) break;
                const auto event = vision::updateHandPresence(state, hasHand);
                if (event == vision::HandPresenceEvent::Appeared) emit presenceChanged(true);
                else if (event == vision::HandPresenceEvent::Disappeared) emit presenceChanged(false);
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
    auto* worker = new CameraWorker(factory_, this);
    worker_ = worker;
    active_ = true;
    stopping_ = false;
    failed_ = false;

    connect(worker, &CameraWorker::ready, this, [this, worker] {
        if (worker_ == worker && active_ && !stopping_) {
            emit statusChanged(QStringLiteral("正在检测手掌"));
        }
    }, Qt::QueuedConnection);
    connect(worker, &CameraWorker::presenceChanged, this, [this, worker](bool present) {
        if (worker_ == worker && active_ && !stopping_) {
            emit visualEvent(present ? VisualEvent::HandDetected : VisualEvent::HandLost);
        }
    }, Qt::QueuedConnection);
    connect(worker, &CameraWorker::failed, this, [this, worker](const QString& message) {
        if (worker_ != worker || !active_ || stopping_) return;
        failed_ = true;
        active_ = false;
        emit statusChanged(message);
        emit controlsChanged(false, false);
    }, Qt::QueuedConnection);
    connect(worker, &QThread::finished, this, [this, worker] {
        if (worker_ != worker) return;
        worker_ = nullptr;
        active_ = false;
        stopping_ = false;
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
    worker_->requestInterruption();
    emit controlsChanged(false, false);
    emit statusChanged(QStringLiteral("正在关闭"));
}

void CameraController::shutdown()
{
    Q_ASSERT(QThread::currentThread() == thread());
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
