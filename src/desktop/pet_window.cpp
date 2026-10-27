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

namespace {
// 桌面允许预测延续的上限，从最近一次真实测量开始计时。
constexpr qint64 gazePredictionTimeoutMs = 1000;
constexpr qint64 pinchMeasurementTimeoutMs = 500;

QPointF pinchScreenPoint(const QPointF& position, const QScreen* screen)
{
    const QRect area = screen->availableGeometry();
    return QPointF(area.left() + std::clamp(position.x(), 0.0, 1.0) * std::max(0, area.width() - 1),
                   area.top() + std::clamp(position.y(), 0.0, 1.0) * std::max(0, area.height() - 1));
}

// 独立透明光标只负责指示位置；不抢焦点，也不截获底下窗口的鼠标。
class PinchPointerWindow final : public QWidget {
public:
    explicit PinchPointerWindow(QWidget* owner) : QWidget(owner,
        Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
        Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus)
    {
        setObjectName("pinchPointerWindow");
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_QuitOnClose, false);
        setFixedSize(42, 42);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor color = property("grabbed").toBool() ? QColor("#a75bdb")
            : property("headReady").toBool() ? QColor("#31c6ad") : QColor("#bc8adf");
        painter.setPen(QPen(color, 2.5));
        painter.setBrush(QColor(color.red(), color.green(), color.blue(), 35));
        painter.drawEllipse(QPointF(21, 21), 13, 13);
        painter.drawLine(QPointF(21, 3), QPointF(21, 10));
        painter.drawLine(QPointF(21, 32), QPointF(21, 39));
        painter.drawLine(QPointF(3, 21), QPointF(10, 21));
        painter.drawLine(QPointF(32, 21), QPointF(39, 21));
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(21, 21), 3, 3);
    }
};
}

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

    pinchStatusLabel_ = new QLabel(QStringLiteral("捏合拖动中"), this);
    pinchStatusLabel_->setObjectName("pinchStatusLabel");
    pinchStatusLabel_->setGeometry(18, 0, 224, 24);
    pinchStatusLabel_->setAlignment(Qt::AlignCenter);
    pinchStatusLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    pinchStatusLabel_->setStyleSheet(QStringLiteral(
        "QLabel { color: #62338a; background: #f6eaff; border: 1px solid #c08ce5;"
        "border-radius: 11px; font-size: 13px; font-weight: 600; }"));
    pinchStatusLabel_->hide();
    pinchPointerWindow_ = new PinchPointerWindow(this);

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
    previewAction_ = menu_->addAction(QStringLiteral("显示检测画面"));
    previewAction_->setObjectName("previewAction");
    previewAction_->setCheckable(true);
    connect(previewAction_, &QAction::toggled, this, &PetWindow::previewRequested);
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
        setPinchTarget(false, QPointF(), false);
        setPinchPointer(false, QPointF());
        setGazeTarget(false, QPointF(0.5, 0.5));
    }
    const QSignalBlocker blocker(cameraAction_);
    cameraAction_->setChecked(checked);
    cameraAction_->setEnabled(enabled);
}

void PetWindow::setPreviewChecked(bool checked)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const QSignalBlocker blocker(previewAction_);
    previewAction_->setChecked(checked);
}

void PetWindow::tick()
{
    const qint64 now = clock_.elapsed();
    if (pinchPointerVisible_ && now - pinchPointerUpdated_ > 250) {
        setPinchPointer(false, QPointF());
    }
    // 后台停止送帧时也释放；无位置的保持信号不能延长此期限。
    if (pinchActive_ && now - pinchMeasured_ >= pinchMeasurementTimeoutMs) {
        endPinch(true);
    }
    // 推理或设备暂时卡住时，也不能让目光一直停在旧位置。
    if (gazeActive_ && now - gazeMeasured_ >= gazePredictionTimeoutMs) {
        setGazeTarget(false, QPointF(0.5, 0.5));
    }
    const double deltaSeconds = std::clamp((now - lastTick_) / 1000.0, 0.0, 0.1);
    lastTick_ = now;
    carryLift_ += ((pinchActive_ ? 1.0 : 0.0) - carryLift_) *
        (1.0 - std::exp(-deltaSeconds / 0.09));
    if (std::abs(carryLift_ - (pinchActive_ ? 1.0 : 0.0)) < 0.002)
        carryLift_ = pinchActive_ ? 1.0 : 0.0;
    carrySway_ *= std::exp(-deltaSeconds / 0.20);
    const double blend = 1.0 - std::exp(-deltaSeconds / 0.10);
    gazePosition_ += (gazeTarget_ - gazePosition_) * blend;
    if (std::hypot(gazeTarget_.x() - gazePosition_.x(),
                   gazeTarget_.y() - gazePosition_.y()) < 0.001) {
        gazePosition_ = gazeTarget_;
    }
    const bool playingWave = pose_ == PetPose::Wave && now < poseDeadline_;
    if (handLostPending_ && !pinchActive_ && !gazeActive_ && !playingWave) {
        handLostPending_ = false;
        playPose(PetPose::Surprise, 2200);
    } else if (poseDeadline_ > 0 && now >= poseDeadline_) {
        playPose(PetPose::Idle, 0);
    }
    updatePinchMotion(deltaSeconds);
    updateFollowMotion(deltaSeconds, now);
    update();
}

void PetWindow::updateFollowMotion(double deltaSeconds, qint64 now)
{
    followMotion_.running = false;
    if (pinchActive_ || !gazeActive_ || !isVisible()) {
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
    // 最新跟踪目标过旧时先停步；真实测量的1秒期限负责退出跟随。
    if (now - gazeTargetUpdated_ > 180) {
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

void PetWindow::setGazeTarget(bool active, const QPointF& position, bool predicted)
{
    Q_ASSERT(QThread::currentThread() == thread());
    // 捏合期间不接收食指追逐目标，避免两种移动方式争夺窗口。
    if ((pinchActive_ || headHover_) && active) return;
    const qint64 now = clock_.elapsed();
    active = active && std::isfinite(position.x()) && std::isfinite(position.y());
    // 在入口也检查真实测量期限，防止动画计时器暂停时预测重新激活跟随。
    if (active && predicted)
        active = gazeActive_ && now - gazeMeasured_ < gazePredictionTimeoutMs;
    const bool changed = gazeActive_ != active;
    gazeActive_ = active;
    gazeStatusLabel_->setVisible(active);
    gazeTarget_ = active
        ? QPointF(std::clamp(position.x(), 0.0, 1.0),
                  std::clamp(position.y(), 0.0, 1.0))
        : QPointF(0.5, 0.5);
    if (active) {
        gazeTargetUpdated_ = now;
        if (!predicted) gazeMeasured_ = now;
    }
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
    if (active && !predicted) handLostPending_ = false;
    // 张手后的开心闭眼、睡眠等会遮住跟随效果；挥手动画仍优先播放完。
    if (active && pose_ != PetPose::Idle && pose_ != PetPose::Wave) {
        playPose(PetPose::Idle, 0);
    }
    if (changed) emit gazeModeChanged(active);
}

void PetWindow::endPinch(bool blockUntilRelease)
{
    if (blockUntilRelease) pinchBlocked_ = true;
    const bool wasActive = pinchActive_;
    pinchActive_ = false;
    pinchScreen_.clear();
    headHover_ = false;
    pinchTarget_ = QPointF(pos());
    pinchStatusLabel_->hide();
    pinchPointerWindow_->setProperty("grabbed", false);
    pinchPointerWindow_->update();
    followPosition_ = QPointF(pos());
    followMotion_ = FollowMotion{};
    if (wasActive) emit pinchModeChanged(false);
    update();
}

void PetWindow::setPinchTarget(bool active, const QPointF& position, bool measured)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!active) {
        pinchBlocked_ = false;
        endPinch();
        return;
    }
    if (pinchBlocked_) return;
    if (!isVisible() || pressed_ || menu_->isVisible()) {
        endPinch(true);
        return;
    }
    const qint64 now = clock_.elapsed();
    // 即使动画计时器暂停，过期输入也不能直接恢复之前的抓取。
    if (pinchActive_ && now - pinchMeasured_ >= pinchMeasurementTimeoutMs) {
        endPinch(true);
        return;
    }
    measured = measured && std::isfinite(position.x()) && std::isfinite(position.y());
    if (!measured) {
        if (pinchActive_) {
            pinchTarget_ = QPointF(pos()); // 短暂丢失时冻结，不拿旧坐标继续移动
            pinchStatusLabel_->setText(QStringLiteral("捏合保持 · 等待位置"));
        } else {
            // 首次捏合缺少真实位置也算抓取失败，必须松开后重新尝试。
            pinchBlocked_ = true;
        }
        return;
    }
    const QPointF target(std::clamp(position.x(), 0.0, 1.0),
                         std::clamp(position.y(), 0.0, 1.0));
    if (!pinchActive_) {
        pinchScreen_ = QGuiApplication::screenAt(frameGeometry().center());
        if (!pinchScreen_) pinchScreen_ = QGuiApplication::primaryScreen();
        if (!pinchScreen_) return;
        const QPointF pointer = pinchScreenPoint(target, pinchScreen_);
        const QRectF head = robotHeadGrabArea(QRectF(10, 24, 240, 256)).translated(QPointF(pos()));
        if (!head.contains(pointer)) {
            pinchScreen_.clear();
            pinchBlocked_ = true;
            pinchStatusLabel_->setText(QStringLiteral("先松开，再捏合头顶"));
            pinchStatusLabel_->show();
            return;
        }
        // 抓取首帧只建立参考点，不把机器人瞬移到手的位置。
        pinchAnchor_ = target;
        pinchWindowAnchor_ = QPointF(pos());
        pinchTarget_ = QPointF(pos());
        setGazeTarget(false, QPointF(0.5, 0.5));
        pinchActive_ = true;
        headHover_ = false;
        playPose(PetPose::Idle, 0);
        emit pinchModeChanged(true);
    }
    if (!pinchScreen_) {
        endPinch(true);
        return;
    }
    pinchMeasured_ = now;
    handLostPending_ = false;
    const QRect area = pinchScreen_->availableGeometry();
    const int rangeX = std::max(0, area.width() - width());
    const int rangeY = std::max(0, area.height() - height());
    const QPointF delta = target - pinchAnchor_;
    pinchTarget_ = QPointF(
        std::clamp(pinchWindowAnchor_.x() + delta.x() * std::max(0, area.width() - 1),
                   double(area.left()), double(area.left() + rangeX)),
        std::clamp(pinchWindowAnchor_.y() + delta.y() * std::max(0, area.height() - 1),
                   double(area.top()), double(area.top() + rangeY)));
    pinchStatusLabel_->setText(QStringLiteral("提起来啦 · 松开放下"));
    pinchStatusLabel_->show();
    pinchPointerWindow_->setProperty("grabbed", true);
    pinchPointerWindow_->update();
    update();
}

void PetWindow::setPinchPointer(bool visible, const QPointF& position)
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto* screen = pinchScreen_ ? pinchScreen_.data()
        : QGuiApplication::screenAt(frameGeometry().center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    visible = visible && isVisible() && screen &&
        std::isfinite(position.x()) && std::isfinite(position.y());
    pinchPointerVisible_ = visible;
    if (!visible) {
        headHover_ = false;
        pinchPointerWindow_->hide();
        if (!pinchActive_) pinchStatusLabel_->hide();
        update();
        return;
    }
    pinchPointerUpdated_ = clock_.elapsed();
    pinchPointerGlobal_ = pinchScreenPoint(position, screen);
    const QRectF head = robotHeadGrabArea(QRectF(10, 24, 240, 256)).translated(QPointF(pos()));
    headHover_ = !pinchActive_ && head.contains(pinchPointerGlobal_);
    if (headHover_) {
        // 对准头顶时停住食指追逐，方便完成抓取。
        setGazeTarget(false, QPointF(0.5, 0.5));
        pinchStatusLabel_->setText(pinchBlocked_ ? QStringLiteral("先松开，再捏合头顶")
                                               : QStringLiteral("捏住头顶，提起我"));
        pinchStatusLabel_->show();
    } else if (!pinchActive_) {
        pinchStatusLabel_->hide();
    }
    pinchPointerWindow_->setProperty("headReady", headHover_);
    pinchPointerWindow_->setProperty("grabbed", pinchActive_);
    pinchPointerWindow_->move(qRound(pinchPointerGlobal_.x()) - 21,
                              qRound(pinchPointerGlobal_.y()) - 21);
    pinchPointerWindow_->show();
    pinchPointerWindow_->update();
    update();
}

void PetWindow::updatePinchMotion(double deltaSeconds)
{
    if (!pinchActive_) return;
    if (!pinchScreen_ || !isVisible() || pressed_ || menu_->isVisible()) {
        endPinch(true);
        return;
    }
    const QRect area = pinchScreen_->availableGeometry();
    const QPointF current(pos());
    const QPointF difference = pinchTarget_ - current;
    const double distance = std::hypot(difference.x(), difference.y());
    carrySway_ = std::clamp(-difference.x() * 0.06, -10.0, 10.0);
    const double blend = distance < 1.5 ? 1.0 : 1.0 - std::exp(-deltaSeconds / 0.06);
    const QPointF next = current + difference * blend;
    move(qRound(std::clamp(next.x(), double(area.left()),
                          double(area.left() + std::max(0, area.width() - width())))),
         qRound(std::clamp(next.y(), double(area.top()),
                          double(area.top() + std::max(0, area.height() - height())))));
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
    if (pinchActive_) endPinch(true);
    handLostPending_ = false;
    playPose(pose, pose == PetPose::Idle || pose == PetPose::Sleep ? 0 : 2200);
}

void PetWindow::reactTo(VisualEvent event)
{
    // Do not connect a camera worker by calling QWidget functions directly.
    Q_ASSERT(QThread::currentThread() == thread());
    if (pinchActive_) {
        if (event == VisualEvent::HandDetected) {
            handLostPending_ = false;
            if (cameraSessionActive_) greetedThisSession_ = true;
        } else if (event == VisualEvent::HandLost) {
            handLostPending_ = true;
        }
        return;
    }
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
    if (pinchActive_) endPinch(true);
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
               gazePosition_, gazeActive_, followMotion_, CarryMotion{carryLift_, carrySway_});
    if (headHover_) {
        painter.setPen(QPen(QColor("#31c6ad"), 2.5, Qt::DashLine));
        painter.setBrush(QColor(49, 198, 173, 30));
        painter.drawRoundedRect(robotHeadGrabArea(QRectF(10, 24, 240, 256)), 18, 18);
    }

    if (!pinchActive_ && !gazeActive_ &&
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
    if (pinchActive_) endPinch(true);
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
    if (pinchActive_) endPinch(true);
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
    if (pinchActive_) endPinch(true);
    setPinchPointer(false, QPointF());
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
