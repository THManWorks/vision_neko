#pragma once

#include "robot_renderer.hpp"
#include "visual_event.hpp"

#include <QElapsedTimer>
#include <QPoint>
#include <QWidget>

class QAction;
class QCloseEvent;
class QContextMenuEvent;
class QHideEvent;
class QMenu;
class QMouseEvent;
class QShowEvent;
class QSystemTrayIcon;
class QTimer;

namespace neko::desktop {

class PetWindow final : public QWidget {
    Q_OBJECT

public:
    explicit PetWindow(QWidget* parent = nullptr);
    PetPose pose() const { return pose_; }

    // The desktop UI has no knowledge of cameras, models, or OpenCV matrices.
    void reactTo(VisualEvent event);
    void showPose(PetPose pose);
    void restore();
    void constrainToScreen();
    void setCameraStatus(const QString& status);
    void setCameraControls(bool checked, bool enabled);

signals:
    void cameraRequested(bool enabled);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void tick();
    void playPose(PetPose pose, int durationMs);
    void buildMenuAndTray();

    QTimer* timer_ = nullptr;
    QMenu* menu_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    QAction* hideAction_ = nullptr;
    QAction* cameraAction_ = nullptr;
    QAction* cameraStatusAction_ = nullptr;
    QElapsedTimer clock_;
    PetPose pose_ = PetPose::Idle;
    qint64 poseStarted_ = 0;
    qint64 poseDeadline_ = 0;
    bool pressed_ = false;
    bool dragged_ = false;
    QPoint pressPosition_;
    QPoint originalPosition_;
};

} // namespace neko::desktop
