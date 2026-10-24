#include "pet_window.hpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QHideEvent>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace neko::desktop {

PetWindow::PetWindow(QWidget* parent) : QWidget(parent)
{
    setObjectName("nekoPetWindow");
    setWindowTitle(QStringLiteral("Project Neko · 摄像头未开启"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(260, 300);
    setCursor(Qt::OpenHandCursor);

    gazeStatusLabel_ = new QLabel(QStringLiteral("食指带路中"), this);
    gazeStatusLabel_->setObjectName("gazeStatusLabel");
    gazeStatusLabel_->setGeometry(54, 0, 152, 24);
    gazeStatusLabel_->setAlignment(Qt::AlignCenter);
    gazeStatusLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    gazeStatusLabel_->setStyleSheet(QStringLiteral(
        "QLabel { color: #255778; background: #e6f6ff; border: 1px solid #7bcdf3;"
        "border-radius: 11px; font-size: 13px; font-weight: 600; }"));
    gazeStatusLabel_->hide();

    clock_.start();
    timer_ = new QTimer(this);
    timer_->setObjectName("animationTimer");
    timer_->setInterval(33);
    connect(timer_, &QTimer::timeout, this, [this] { tick(); });
    buildMenuAndTray();
    setCameraStatus(QStringLiteral("未开启"));

    auto* exitShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(exitShortcut, &QShortcut::activated, qApp, &QCoreApplication::quit);

    if (auto* screen = QGuiApplication::primaryScreen()) {
        const auto area = screen->availableGeometry();
        move(area.right() - width() - 30, area.bottom() - height() - 20);
    }
    constrainToScreen();
    for (auto* screen : QGuiApplication::screens()) {
        connect(screen, &QScreen::availableGeometryChanged, this,
                [this] { constrainToScreen(); });
    }
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
        connect(screen, &QScreen::availableGeometryChanged, this,
                [this] { constrainToScreen(); });
        constrainToScreen();
    });
    connect(qApp, &QGuiApplication::screenRemoved, this,
            [this] { constrainToScreen(); });
}

void PetWindow::buildMenuAndTray()
{
    menu_ = new QMenu(this);
    cameraStatusAction_ = menu_->addAction(QStringLiteral("摄像头：未开启"));
    cameraStatusAction_->setObjectName("cameraStatusAction");
    cameraStatusAction_->setEnabled(false);
    cameraAction_ = menu_->addAction(QStringLiteral("开启手势互动"));
    cameraAction_->setObjectName("cameraAction");
    cameraAction_->setCheckable(true);
    connect(cameraAction_, &QAction::toggled, this, &PetWindow::cameraRequested);
    menu_->addSeparator();
    menu_->addAction(QStringLiteral("显示宠物"), this, [this] { restore(); });
    menu_->addAction(QStringLiteral("挥挥手"), this, [this] { showPose(PetPose::Wave); });
    menu_->addAction(QStringLiteral("开心一下"), this, [this] { showPose(PetPose::Happy); });
    menu_->addAction(QStringLiteral("睡一会儿"), this, [this] { showPose(PetPose::Sleep); });
    menu_->addAction(QStringLiteral("叫醒它"), this, [this] { showPose(PetPose::Idle); });
    hideAction_ = menu_->addAction(QStringLiteral("隐藏宠物"), this, [this] { hide(); });
    menu_->addSeparator();
    menu_->addAction(QStringLiteral("退出"), qApp, &QCoreApplication::quit);

    QPixmap iconImage(64, 64);
    iconImage.fill(Qt::transparent);
    {
        QPainter painter(&iconImage);
        paintRobot(painter, QRectF(0, 0, 64, 64), PetPose::Idle, 0.0);
    }
    const QIcon icon(iconImage);
    setWindowIcon(icon);
    tray_ = new QSystemTrayIcon(icon, this);
    tray_->setContextMenu(menu_);
    connect(tray_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) restore();
    });
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray_->show();
    } else {
        // Prevent hiding the only way back to the application.
        hideAction_->setEnabled(false);
    }
}

void PetWindow::setCameraStatus(const QString& status)
{
    Q_ASSERT(QThread::currentThread() == thread());
    cameraStatusAction_->setText(QStringLiteral("摄像头：") + status);
    setWindowTitle(QStringLiteral("Project Neko · 摄像头：") + status);
    setToolTip(QStringLiteral("点击互动 · 拖动移动 · 右键菜单\n摄像头：") + status);
    tray_->setToolTip(QStringLiteral("Project Neko · 摄像头：") + status);
}

void PetWindow::setCameraControls(bool checked, bool enabled)
{
    Q_ASSERT(QThread::currentThread() == thread());
    // 每次重新开启互动才允许新的欢迎动作；手离开不会重新计数。
    if (checked && !cameraSessionActive_) greetedThisSession_ = false;
    cameraSessionActive_ = checked;
    if (!checked) {
        handLostPending_ = false;
        setGazeTarget(false, QPointF(0.5, 0.5));
    }
    const QSignalBlocker blocker(cameraAction_);
    cameraAction_->setChecked(checked);
    cameraAction_->setEnabled(enabled);
}

void PetWindow::tick()
{
    const qint64 now = clock_.elapsed();
    // 推理或设备暂时卡住时，也不能让目光一直停在旧位置。
    if (gazeActive_ && now - gazeUpdated_ >= 500) {
        setGazeTarget(false, QPointF(0.5, 0.5));
    }
    const double deltaSeconds = std::clamp((now - lastTick_) / 1000.0, 0.0, 0.1);
    lastTick_ = now;
    const double blend = 1.0 - std::exp(-deltaSeconds / 0.10);
    gazePosition_ += (gazeTarget_ - gazePosition_) * blend;
    if (std::hypot(gazeTarget_.x() - gazePosition_.x(),
                   gazeTarget_.y() - gazePosition_.y()) < 0.001) {
        gazePosition_ = gazeTarget_;
    }
    const bool playingWave = pose_ == PetPose::Wave && now < poseDeadline_;
    if (handLostPending_ && !gazeActive_ && !playingWave) {
        handLostPending_ = false;
        playPose(PetPose::Surprise, 2200);
    } else if (poseDeadline_ > 0 && now >= poseDeadline_) {
        playPose(PetPose::Idle, 0);
    }
    updateFollowMotion(deltaSeconds, now);
    update();
}

void PetWindow::updateFollowMotion(double deltaSeconds, qint64 now)
{
    followMotion_.running = false;
    if (!gazeActive_ || !isVisible()) {
        followPosition_ = QPointF(pos());
        return;
    }
    // 鼠标操作优先；挥手动画播放期间也不追逐。
    if (pressed_ || menu_->isVisible() || pose_ == PetPose::Wave) {
        followPosition_ = QPointF(pos());
        gazeStatusLabel_->setText(pose_ == PetPose::Wave
            ? QStringLiteral("挥完就来！") : QStringLiteral("等你继续指路"));
        return;
    }
    // 没有新的真实坐标时先停步；原有500ms超时负责退出跟随。
    if (now - gazeUpdated_ > 180) {
        followPosition_ = QPointF(pos());
        gazeStatusLabel_->setText(QStringLiteral("等你继续指路"));
        return;
    }
    if (!followScreen_) {
        followScreen_ = QGuiApplication::screenAt(frameGeometry().center());
        if (!followScreen_) followScreen_ = QGuiApplication::primaryScreen();
    }
    if (!followScreen_) return;
    const QRect area = followScreen_->availableGeometry();
    const double maxX = area.left() + std::max(0, area.width() - width());
    const double maxY = area.top() + std::max(0, area.height() - height());
    const QPointF target(
        area.left() + gazeTarget_.x() * std::max(0, area.width() - width()),
        area.top() + gazeTarget_.y() * std::max(0, area.height() - height()));
    // 同步手动拖动、屏幕变化或窗口管理器造成的位置改变。
    if (std::hypot(x() - followPosition_.x(), y() - followPosition_.y()) > 2.0) {
        followPosition_ = QPointF(pos());
    }
    const QPointF difference = target - followPosition_;
    const double distance = std::hypot(difference.x(), difference.y());
    if (distance <= 10.0 || deltaSeconds <= 0.0) {
        gazeStatusLabel_->setText(QStringLiteral("跟上啦，继续指路"));
        return;
    }
    // 限速并在接近目标时减速，避免瞬移和小幅坐标抖动。
    const double travel = std::min({distance, 700.0 * deltaSeconds,
        distance * (1.0 - std::exp(-deltaSeconds / 0.16))});
    const QPointF step = difference * (travel / distance);
    followPosition_ += step;
    followPosition_.setX(std::clamp(followPosition_.x(), static_cast<double>(area.left()), maxX));
    followPosition_.setY(std::clamp(followPosition_.y(), static_cast<double>(area.top()), maxY));
    move(qRound(followPosition_.x()), qRound(followPosition_.y()));
    followMotion_.running = travel > 0.1;
    if (std::abs(step.x()) > 0.1) followMotion_.direction = step.x() > 0.0 ? 1.0 : -1.0;
    followMotion_.phase = std::fmod(followMotion_.phase + travel / 30.0, 2.0 * 3.141592653589793);
    gazeStatusLabel_->setText(QStringLiteral("追着食指跑！"));
}

void PetWindow::setGazeTarget(bool active, const QPointF& position)
{
    Q_ASSERT(QThread::currentThread() == thread());
    active = active && std::isfinite(position.x()) && std::isfinite(position.y());
    const bool changed = gazeActive_ != active;
    gazeActive_ = active;
    gazeStatusLabel_->setVisible(active);
    gazeTarget_ = active
        ? QPointF(std::clamp(position.x(), 0.0, 1.0),
                  std::clamp(position.y(), 0.0, 1.0))
        : QPointF(0.5, 0.5);
    gazeUpdated_ = clock_.elapsed();
    if (changed) {
        followPosition_ = QPointF(pos());
        followMotion_ = FollowMotion{};
        if (active) {
            followScreen_ = QGuiApplication::screenAt(frameGeometry().center());
            if (!followScreen_) followScreen_ = QGuiApplication::primaryScreen();
            gazeStatusLabel_->setText(QStringLiteral("食指带路中"));
        } else {
            followScreen_.clear();
        }
    }
    if (active) handLostPending_ = false;
    // 张手后的开心闭眼、睡眠等会遮住跟随效果；挥手动画仍优先播放完。
    if (active && pose_ != PetPose::Idle && pose_ != PetPose::Wave) {
        playPose(PetPose::Idle, 0);
    }
    if (changed) emit gazeModeChanged(active);
}

void PetWindow::playPose(PetPose pose, int durationMs)
{
    Q_ASSERT(QThread::currentThread() == thread());
    pose_ = pose;
    poseStarted_ = clock_.elapsed();
    poseDeadline_ = durationMs > 0 ? poseStarted_ + durationMs : 0;
    emit poseChanged();
    update();
}

void PetWindow::showPose(PetPose pose)
{
    handLostPending_ = false;
    playPose(pose, pose == PetPose::Idle || pose == PetPose::Sleep ? 0 : 2200);
}

void PetWindow::reactTo(VisualEvent event)
{
    // Do not connect a camera worker by calling QWidget functions directly.
    Q_ASSERT(QThread::currentThread() == thread());
    switch (event) {
    case VisualEvent::HandDetected:
        handLostPending_ = false;
        if (cameraSessionActive_ && !greetedThisSession_) {
            greetedThisSession_ = true;
            showPose(PetPose::Happy);
        }
        break;
    case VisualEvent::FistConfirmed:
        // 握拳暂不对应动作。
        setGazeTarget(false, QPointF(0.5, 0.5));
        break;
    case VisualEvent::HandWave:
        setGazeTarget(false, QPointF(0.5, 0.5));
        showPose(PetPose::Wave);
        break;
    case VisualEvent::HandLost:
        // 短暂丢手不能立刻覆盖已经触发的挥手或正在容错的跟随。
        if (gazeActive_ || (pose_ == PetPose::Wave && clock_.elapsed() < poseDeadline_)) {
            handLostPending_ = true;
            break;
        }
        setGazeTarget(false, QPointF(0.5, 0.5));
        showPose(PetPose::Surprise);
        break;
    case VisualEvent::OpenPalmConfirmed:
        setGazeTarget(false, QPointF(0.5, 0.5));
        // 张手只参与视觉判断，不再触发开心，避免和动态挥手交替。
        break;
    }
}

void PetWindow::restore()
{
    constrainToScreen();
    show();
    raise();
}

void PetWindow::constrainToScreen()
{
    auto* screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    const auto area = screen->availableGeometry();
    const int maxX = std::max(area.left(), area.right() - width() + 1);
    const int maxY = std::max(area.top(), area.bottom() - height() + 1);
    move(std::clamp(x(), area.left(), maxX), std::clamp(y(), area.top(), maxY));
    followPosition_ = QPointF(pos());
    if (gazeActive_) followScreen_ = screen;
}

void PetWindow::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const double elapsed = (clock_.elapsed() - poseStarted_) / 1000.0;
    paintRobot(painter, QRectF(10, 24, 240, 256), pose_, elapsed,
               gazePosition_, gazeActive_, followMotion_);

    if (!gazeActive_ &&
        (pose_ == PetPose::Happy || pose_ == PetPose::Wave || pose_ == PetPose::Surprise)) {
        QString text = pose_ == PetPose::Wave ? QStringLiteral("你好呀！")
                     : pose_ == PetPose::Happy ? QStringLiteral("今天也一起加油")
                                              : QStringLiteral("咦？");
        painter.setPen(QColor("#365b6b"));
        QFont font = painter.font();
        font.setPixelSize(15);
        painter.setFont(font);
        painter.drawText(QRectF(0, 0, width(), 24), Qt::AlignCenter, text);
    }
}

void PetWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    pressed_ = true;
    dragged_ = false;
    pressPosition_ = event->globalPosition().toPoint();
    originalPosition_ = pos();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void PetWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (!pressed_) return;
    const QPoint delta = event->globalPosition().toPoint() - pressPosition_;
    if (delta.manhattanLength() >= QApplication::startDragDistance()) dragged_ = true;
    if (dragged_) move(originalPosition_ + delta);
    event->accept();
}

void PetWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !pressed_) return;
    pressed_ = false;
    setCursor(Qt::OpenHandCursor);
    constrainToScreen();
    if (!dragged_) showPose(PetPose::Happy);
    event->accept();
}

void PetWindow::contextMenuEvent(QContextMenuEvent* event)
{
    menu_->exec(event->globalPos());
}

void PetWindow::showEvent(QShowEvent* event)
{
    lastTick_ = clock_.elapsed();
    timer_->start();
    QWidget::showEvent(event);
}

void PetWindow::hideEvent(QHideEvent* event)
{
    followMotion_.running = false;
    timer_->stop();
    QWidget::hideEvent(event);
}

void PetWindow::closeEvent(QCloseEvent* event)
{
    if (tray_->isVisible()) {
        hide();
        event->ignore();
    } else {
        event->accept();
        QCoreApplication::quit();
    }
}

} // namespace neko::desktop
