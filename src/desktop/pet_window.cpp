#include "pet_window.hpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QHideEvent>
#include <QIcon>
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

namespace neko::desktop {

PetWindow::PetWindow(QWidget* parent) : QWidget(parent)
{
    setObjectName("nekoPetWindow");
    setWindowTitle(QStringLiteral("Project Neko · 摄像头未开启"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(260, 300);
    setCursor(Qt::OpenHandCursor);

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
    const QSignalBlocker blocker(cameraAction_);
    cameraAction_->setChecked(checked);
    cameraAction_->setEnabled(enabled);
}

void PetWindow::tick()
{
    if (poseDeadline_ > 0 && clock_.elapsed() >= poseDeadline_) {
        playPose(PetPose::Idle, 0);
    }
    update();
}

void PetWindow::playPose(PetPose pose, int durationMs)
{
    Q_ASSERT(QThread::currentThread() == thread());
    pose_ = pose;
    poseStarted_ = clock_.elapsed();
    poseDeadline_ = durationMs > 0 ? poseStarted_ + durationMs : 0;
    update();
}

void PetWindow::showPose(PetPose pose)
{
    playPose(pose, pose == PetPose::Idle || pose == PetPose::Sleep ? 0 : 2200);
}

void PetWindow::reactTo(VisualEvent event)
{
    // Do not connect a camera worker by calling QWidget functions directly.
    Q_ASSERT(QThread::currentThread() == thread());
    switch (event) {
    case VisualEvent::HandDetected:
    case VisualEvent::FistConfirmed:
        // 手出现和握拳暂不对应动作。
        break;
    case VisualEvent::HandWave:
        showPose(PetPose::Wave);
        break;
    case VisualEvent::HandLost:
        showPose(PetPose::Surprise);
        break;
    case VisualEvent::OpenPalmConfirmed:
        // 动态挥手优先播放完，避免恢复张手分类时抢占挥手动画。
        if (pose_ != PetPose::Wave || clock_.elapsed() >= poseDeadline_) {
            showPose(PetPose::Happy);
        }
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
}

void PetWindow::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const double elapsed = (clock_.elapsed() - poseStarted_) / 1000.0;
    paintRobot(painter, QRectF(10, 24, 240, 256), pose_, elapsed);

    if (pose_ == PetPose::Happy || pose_ == PetPose::Wave || pose_ == PetPose::Surprise) {
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
    timer_->start();
    QWidget::showEvent(event);
}

void PetWindow::hideEvent(QHideEvent* event)
{
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
