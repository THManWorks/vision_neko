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
    void setPreviewChecked(bool checked);
    // 预测位置可以继续追随，但只有真实测量才能开启或延长跟随。
    void setGazeTarget(bool active, const QPointF& position, bool predicted = false);
    bool gazeActive() const { return gazeActive_; }
    QPointF gazePosition() const { return gazePosition_; }
    // 接收参数：持续捏合状态、归一化位置、本帧是否有真实测量。
    void setPinchTarget(bool active, const QPointF& position, bool measured = true);
    bool pinchActive() const { return pinchActive_; }
    // 显示真实两指中点的屏幕光标，供捏合前对准头顶；不参与手势判断。
    void setPinchPointer(bool visible, const QPointF& position);

signals:
    void cameraRequested(bool enabled);
    void previewRequested(bool enabled);
    void gazeModeChanged(bool active);
    void pinchModeChanged(bool active);
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
    void updatePinchMotion(double deltaSeconds);
    void endPinch(bool blockUntilRelease = false);
    void playPose(PetPose pose, int durationMs);
    void buildMenuAndTray();

    QTimer* timer_ = nullptr;
    QMenu* menu_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    QAction* hideAction_ = nullptr;
    QAction* cameraAction_ = nullptr;
    QAction* previewAction_ = nullptr;
    QAction* cameraStatusAction_ = nullptr;
    QLabel* gazeStatusLabel_ = nullptr;
    QLabel* pinchStatusLabel_ = nullptr;
    QWidget* pinchPointerWindow_ = nullptr;
    QElapsedTimer clock_;
    PetPose pose_ = PetPose::Idle;
    qint64 poseStarted_ = 0;
    qint64 poseDeadline_ = 0;
    bool gazeActive_ = false;
    QPointF gazeTarget_{0.5, 0.5};
    QPointF gazePosition_{0.5, 0.5};
    qint64 gazeTargetUpdated_ = 0;
    qint64 gazeMeasured_ = 0;
    qint64 lastTick_ = 0;
    QPointer<QScreen> followScreen_;
    QPointF followPosition_;
    FollowMotion followMotion_;
    bool pinchActive_ = false;
    bool pinchBlocked_ = false; // 手动干预或超时后，等待先松开再重新捏合
    qint64 pinchMeasured_ = 0;
    QPointF pinchAnchor_;
    QPointF pinchWindowAnchor_;
    QPointF pinchTarget_;
    QPointer<QScreen> pinchScreen_;
    bool pinchPointerVisible_ = false;
    bool headHover_ = false;
    qint64 pinchPointerUpdated_ = 0;
    QPointF pinchPointerGlobal_;
    double carryLift_ = 0.0;
    double carrySway_ = 0.0;
    bool cameraSessionActive_ = false;
    bool greetedThisSession_ = false;
    bool handLostPending_ = false;
    bool pressed_ = false;
    bool dragged_ = false;
    QPoint pressPosition_;
    QPoint originalPosition_;
};

} // namespace neko::desktop
