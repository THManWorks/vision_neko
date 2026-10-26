#include "detection_preview.hpp"

#include <QCloseEvent>
#include <QPainter>
#include <QThread>

namespace neko::desktop {

DetectionPreviewWindow::DetectionPreviewWindow(QWidget* parent) : QWidget(parent)
{
    setObjectName("detectionPreviewWindow");
    setAttribute(Qt::WA_QuitOnClose, false);
    setMinimumSize(320, 240);
    resize(800, 600);
    setCameraStatus(QStringLiteral("未开启"));
}

void DetectionPreviewWindow::setFrame(const QImage& frame)
{
    Q_ASSERT(QThread::currentThread() == thread());
    // Camera buffers may be reused as soon as this call returns.
    frame_ = frame.copy();
    update();
}

void DetectionPreviewWindow::setCameraStatus(const QString& status)
{
    Q_ASSERT(QThread::currentThread() == thread());
    cameraStatus_ = status;
    setWindowTitle(QStringLiteral("Project Neko · 检测画面 · 摄像头：") + status);
    update();
}

void DetectionPreviewWindow::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#161c26"));
    painter.setPen(QColor("#dce6f2"));
    painter.drawText(QRect(16, 12, width() - 32, 28),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("摄像头：") + cameraStatus_);

    const QRect viewport = rect().adjusted(16, 52, -16, -16);
    painter.fillRect(viewport, QColor("#0b1017"));
    if (frame_.isNull()) {
        painter.setPen(QColor("#a6b4c7"));
        painter.drawText(viewport.adjusted(12, 12, -12, -12),
                         Qt::AlignCenter | Qt::TextWordWrap,
                         QStringLiteral("等待摄像头画面\n在机器人右键菜单开启手势互动"));
        return;
    }

    const QSize fitted = frame_.size().scaled(viewport.size(), Qt::KeepAspectRatio);
    const QRect target(QPoint(viewport.x() + (viewport.width() - fitted.width()) / 2,
                              viewport.y() + (viewport.height() - fitted.height()) / 2), fitted);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(target, frame_);
}

void DetectionPreviewWindow::closeEvent(QCloseEvent* event)
{
    event->accept();
    hide();
    emit closed();
}

} // namespace neko::desktop
