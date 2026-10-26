#pragma once

#include <QImage>
#include <QString>
#include <QWidget>

class QCloseEvent;
class QPaintEvent;

namespace neko::desktop {

// The preview receives GUI-owned images; it does not open or control a camera.
class DetectionPreviewWindow final : public QWidget {
    Q_OBJECT

public:
    explicit DetectionPreviewWindow(QWidget* parent = nullptr);
    void setFrame(const QImage& frame);
    void setCameraStatus(const QString& status);

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    QImage frame_;
    QString cameraStatus_;
};

} // namespace neko::desktop
