#include "pet_window.hpp"
#include "camera_controller.hpp"
#include "detection_preview.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPixmap>
#include <QTimer>
#include <QTextStream>

namespace {

// 接收参数：模型相对于项目目录的路径。
QString defaultModelPath(const QString& relative)
{
    const QDir executableDir(QCoreApplication::applicationDirPath());
    const QStringList candidates{
        executableDir.absoluteFilePath("../" + relative),
        executableDir.absoluteFilePath(relative),
        QDir::current().absoluteFilePath(relative)
    };
    for (const auto& candidate : candidates) {
        if (QFileInfo(candidate).isFile()) return QDir::cleanPath(candidate);
    }
    return QDir::cleanPath(candidates.front());
}

QString poseLabel(neko::desktop::PetPose pose)
{
    using neko::desktop::PetPose;
    switch (pose) {
    case PetPose::Happy: return QStringLiteral("Happy");
    case PetPose::Wave: return QStringLiteral("Wave");
    case PetPose::Sleep: return QStringLiteral("Sleep");
    case PetPose::Surprise: return QStringLiteral("Surprise");
    default: return QStringLiteral("Idle");
    }
}

QString visualEventLabel(neko::desktop::VisualEvent event)
{
    using neko::desktop::VisualEvent;
    switch (event) {
    case VisualEvent::HandDetected: return QStringLiteral("HandDetected");
    case VisualEvent::HandLost: return QStringLiteral("HandLost");
    case VisualEvent::HandWave: return QStringLiteral("HandWave");
    case VisualEvent::OpenPalmConfirmed: return QStringLiteral("OpenPalmConfirmed");
    case VisualEvent::FistConfirmed: return QStringLiteral("FistConfirmed");
    }
    return QStringLiteral("Unknown");
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("Project Neko Desktop");
    app.setApplicationVersion(NEKO_VERSION);
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Neko 桌面机器人；在右键菜单开启手势互动。"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"smoke-test", "Automatically exit after 1.2 seconds."});
    parser.addOption({"snapshot", "Save a robot window image.", "path"});
    parser.addOption({"pose", "Initial pose: idle, happy, wave, sleep, surprise.", "name", "idle"});
    parser.addOption({"camera", "Start hand interaction immediately (camera access)."});
    parser.addOption({"preview", "Show the detection preview; use --camera to start camera access."});
    parser.addOption({"vision-debug", "Write numeric recognition and UI events to desktop-vision.log."});
    parser.addOption({"camera-index", "Camera device index.", "index", "0"});
    parser.addOption({"model", "Palm detection ONNX model path.", "path"});
    parser.addOption({"hand-pose-model", "Hand pose ONNX model path.", "path"});
    parser.process(app);

    using namespace neko::desktop;
    const QString poseName = parser.value("pose");
    PetPose pose = PetPose::Idle;
    if (poseName == "idle") pose = PetPose::Idle;
    else if (poseName == "happy") pose = PetPose::Happy;
    else if (poseName == "wave") pose = PetPose::Wave;
    else if (poseName == "sleep") pose = PetPose::Sleep;
    else if (poseName == "surprise") pose = PetPose::Surprise;
    else parser.showHelp(2);

    bool validIndex = false;
    const int cameraIndex = parser.value("camera-index").toInt(&validIndex);
    if (!validIndex || cameraIndex < 0) parser.showHelp(2);

    // 可选诊断只记录数字和事件，不保存图像；所有文件写入都在GUI线程。
    QFile diagnostics(QDir(QCoreApplication::applicationDirPath()).filePath("desktop-vision.log"));
    const bool tracing = parser.isSet("vision-debug") &&
        diagnostics.open(QIODevice::WriteOnly | QIODevice::Text);
    const auto writeTrace = [&diagnostics, tracing](const QString& record) {
        if (!tracing) return;
        QTextStream output(&diagnostics);
        output << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
               << ' ' << record << '\n';
        output.flush();
    };
    writeTrace(QStringLiteral("start executable=%1 pid=%2 build=%3 %4")
        .arg(QCoreApplication::applicationFilePath()).arg(QCoreApplication::applicationPid())
        .arg(QString::fromLatin1(__DATE__)).arg(QString::fromLatin1(__TIME__)));

    PetWindow pet;
    DetectionPreviewWindow preview;

    // 准备摄像头模块需要的配置
    CameraOptions options;

    options.modelPath =
        parser.isSet("model")
            ? QFileInfo(parser.value("model")).absoluteFilePath()
            : defaultModelPath(
                QStringLiteral("models/palm_detection_mediapipe_2023feb.onnx"));

    options.cameraIndex = cameraIndex;

    options.handPoseModelPath =
        parser.isSet("hand-pose-model")
            ? QFileInfo(parser.value("hand-pose-model")).absoluteFilePath()
            : defaultModelPath(
                QStringLiteral("models/handpose_estimation_mediapipe_2023feb.onnx"));

    // 将配置交给摄像头控制器
    CameraController camera(options);
    camera.setDiagnosticsEnabled(tracing);
    writeTrace(QStringLiteral("models palm=%1 hand=%2 camera=%3")
        .arg(options.modelPath).arg(options.handPoseModelPath).arg(options.cameraIndex));

    QObject::connect(&pet, &PetWindow::cameraRequested,
                     &camera, &CameraController::setEnabled);
    const auto applyPreview = [&camera, &pet, &preview](bool enabled) {
        camera.setPreviewEnabled(enabled);
        pet.setPreviewChecked(camera.previewEnabled());
        if (camera.previewEnabled()) {
            preview.show();
            preview.raise();
        } else {
            preview.setFrame(QImage());
            preview.hide();
        }
    };
    QObject::connect(&pet, &PetWindow::previewRequested, &preview, applyPreview);
    QObject::connect(&preview, &DetectionPreviewWindow::closed, &pet,
                     [applyPreview] { applyPreview(false); });
    QObject::connect(&camera, &CameraController::previewFrameReady,
                     &preview, &DetectionPreviewWindow::setFrame);
    QObject::connect(&camera, &CameraController::visualEvent, &pet,
                     [&pet, writeTrace](VisualEvent event) {
        pet.reactTo(event);
        writeTrace(QStringLiteral("ui event=%1 pose=%2 gaze_active=%3")
            .arg(visualEventLabel(event)).arg(poseLabel(pet.pose())).arg(pet.gazeActive()));
    });
    QObject::connect(&camera, &CameraController::gazeChanged,
                     &pet, [&pet, writeTrace](bool active, const QPointF& position,
                                              bool predicted) {
        pet.setGazeTarget(active, position, predicted);
        writeTrace(QStringLiteral("ui gaze_target active=%1 predicted=%2 x=%3 y=%4")
            .arg(active).arg(predicted).arg(position.x(), 0, 'f', 3)
            .arg(position.y(), 0, 'f', 3));
    });
    QObject::connect(&camera, &CameraController::pinchChanged,
                     &pet, [&pet, writeTrace](bool active, const QPointF& position,
                                              bool measured) {
        pet.setPinchTarget(active, position, measured);
        writeTrace(QStringLiteral("ui pinch_target active=%1 measured=%2 x=%3 y=%4")
            .arg(active).arg(measured).arg(position.x(), 0, 'f', 3)
            .arg(position.y(), 0, 'f', 3));
    });
    QObject::connect(&camera, &CameraController::pinchPointerChanged,
                     &pet, &PetWindow::setPinchPointer);
    if (tracing) {
        QObject::connect(&pet, &PetWindow::pinchModeChanged, &pet,
                         [&pet, writeTrace](bool active) {
            writeTrace(QStringLiteral("ui pinch_active=%1 window=(%2,%3)")
                .arg(active).arg(pet.x()).arg(pet.y()));
        });
        QObject::connect(&camera, &CameraController::frameDiagnostics, &pet, writeTrace);
        QObject::connect(&pet, &PetWindow::gazeModeChanged, &pet,
                         [&pet, writeTrace](bool active) {
            writeTrace(QStringLiteral("ui gaze_active=%1 pose=%2")
                .arg(active).arg(poseLabel(pet.pose())));
        });
        QObject::connect(&pet, &PetWindow::poseChanged, &pet, [&pet, writeTrace] {
            writeTrace(QStringLiteral("ui pose_changed=%1").arg(poseLabel(pet.pose())));
        });
        QObject::connect(&camera, &CameraController::statusChanged, &pet,
                         [writeTrace](const QString& status) {
            writeTrace(QStringLiteral("status=%1").arg(status));
        });
    }
    QObject::connect(&camera, &CameraController::statusChanged,
                     &pet, &PetWindow::setCameraStatus);
    QObject::connect(&camera, &CameraController::statusChanged,
                     &preview, &DetectionPreviewWindow::setCameraStatus);
    QObject::connect(&camera, &CameraController::controlsChanged,
                     &pet, &PetWindow::setCameraControls);
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     &camera, &CameraController::shutdown);
    pet.showPose(pose);
    pet.show();
    applyPreview(parser.isSet("preview"));
    if (parser.isSet("camera")) camera.setEnabled(true);
    if (parser.isSet("snapshot")) {
        const QString path = parser.value("snapshot");
        QTimer::singleShot(300, &pet, [&pet, path] {
            if (!pet.grab().save(path)) QCoreApplication::exit(1);
        });
    }
    if (parser.isSet("smoke-test")) QTimer::singleShot(1200, &app, &QCoreApplication::quit);
    return app.exec();
}
