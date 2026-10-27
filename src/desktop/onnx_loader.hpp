#pragma once

#include <QFile>
#include <QString>
#include <opencv2/dnn.hpp>
#include <stdexcept>

namespace neko::desktop {

// Qt handles Unicode filenames; OpenCV receives the unchanged model bytes.
// Loading is still done once per camera session, not once per frame.
inline cv::dnn::Net loadDesktopOnnx(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("Cannot open ONNX model: " + file.errorString().toStdString());
    }
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty() || file.error() != QFileDevice::NoError) {
        throw std::runtime_error("Cannot read ONNX model data");
    }
    return cv::dnn::readNetFromONNX(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

} // namespace neko::desktop
