#pragma once

#include "robot_renderer.hpp"
#include "visual_event.hpp"

#include <QElapsedTimer>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QWidget>

class QAction;
class QCloseEvent;
class QContextMenuEvent;
class QHideEvent;
class QLabel;
class QMenu;
class QMouseEvent;
class QShowEvent;
class QScreen;
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
    // 接收参数：是否跟随、0～1的画面位置；有效时机器人追随，失效时停步。
    void setGazeTarget(bool active, const QPointF& position);
    bool gazeActive() const { return gazeActive_; }
    QPointF gazePosition() const { return gazePosition_; }

signals:
    void cameraRequested(bool enabled);
    void gazeModeChanged(bool active);
    void poseChanged();

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
    void updateFollowMotion(double deltaSeconds, qint64 now);
    void playPose(PetPose pose, int durationMs);
    void buildMenuAndTray();

    QTimer* timer_ = nullptr;
    QMenu* menu_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    QAction* hideAction_ = nullptr;
    QAction* cameraAction_ = nullptr;
    QAction* cameraStatusAction_ = nullptr;
    QLabel* gazeStatusLabel_ = nullptr;
    QElapsedTimer clock_;
    PetPose pose_ = PetPose::Idle;
    qint64 poseStarted_ = 0;
    qint64 poseDeadline_ = 0;
    bool gazeActive_ = false;
    QPointF gazeTarget_{0.5, 0.5};
    QPointF gazePosition_{0.5, 0.5};
    qint64 gazeUpdated_ = 0;
    qint64 lastTick_ = 0;
    QPointer<QScreen> followScreen_;
    QPointF followPosition_;
    FollowMotion followMotion_;
    bool cameraSessionActive_ = false;
    bool greetedThisSession_ = false;
    bool handLostPending_ = false;
    bool pressed_ = false;
    bool dragged_ = false;
    QPoint pressPosition_;
    QPoint originalPosition_;
};

} // namespace neko::desktop
