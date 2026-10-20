#include "pet_window.hpp"
#include "camera_controller.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QTimer>

namespace {

QString defaultModelPath()
{
    const QString relative = QStringLiteral("models/palm_detection_mediapipe_2023feb.onnx");
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

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("Project Neko Desktop");
    app.setApplicationVersion("0.3.0");
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Neko 桌面机器人；在右键菜单开启手掌互动。"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"smoke-test", "Automatically exit after 1.2 seconds."});
    parser.addOption({"snapshot", "Save a robot window image.", "path"});
    parser.addOption({"pose", "Initial pose: idle, happy, wave, sleep, surprise.", "name", "idle"});
    parser.addOption({"camera", "Start hand interaction immediately (camera access)."});
    parser.addOption({"camera-index", "Camera device index.", "index", "0"});
    parser.addOption({"model", "Palm detection ONNX model path.", "path"});
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

    PetWindow pet;
    CameraController camera(CameraOptions{
        parser.isSet("model") ? QFileInfo(parser.value("model")).absoluteFilePath()
                              : defaultModelPath(),
        cameraIndex
    });
    QObject::connect(&pet, &PetWindow::cameraRequested,
                     &camera, &CameraController::setEnabled);
    QObject::connect(&camera, &CameraController::visualEvent,
                     &pet, &PetWindow::reactTo);
    QObject::connect(&camera, &CameraController::statusChanged,
                     &pet, &PetWindow::setCameraStatus);
    QObject::connect(&camera, &CameraController::controlsChanged,
                     &pet, &PetWindow::setCameraControls);
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     &camera, &CameraController::shutdown);
    pet.showPose(pose);
    pet.show();
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
