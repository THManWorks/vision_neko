#pragma once

#include "hand_interaction.hpp"

#include <opencv2/imgproc.hpp>
#include <cmath>
#include <string>

namespace neko::desktop {

// 只在桌面诊断预览上标注原始点和测量，不改变视觉结果或捏合判定。
inline void drawPinchDiagnostics(
    cv::Mat& preview, const vision::VisionFrameResult& frame,
    const vision::HandInteractionResult& interaction, int openFrames)
{
    if (preview.empty()) return;
    const auto text = [&preview](const std::string& value, cv::Point point, cv::Scalar color) {
        cv::putText(preview, value, point, cv::FONT_HERSHEY_SIMPLEX,
                    0.5, cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
        cv::putText(preview, value, point, cv::FONT_HERSHEY_SIMPLEX,
                    0.5, color, 1, cv::LINE_AA);
    };
    text(cv::format("4=Thumb  8=Index  pinch=%s  open frames=%d",
        interaction.pinching ? "CLOSED" : "OPEN", openFrames),
        cv::Point(10, 52), cv::Scalar(255, 255, 255));

    int row = 0;
    for (const auto& hand : frame.hands) {
        const cv::Point label(10, 76 + row++ * 42);
        if (!hand.pose.valid || !hand.pinch.valid) {
            text("Pinch unavailable: pose/measurement invalid", label, cv::Scalar(0, 165, 255));
            continue;
        }
        const auto& thumb = hand.pose.landmarks[4];
        const auto& index = hand.pose.landmarks[8];
        const auto drawable = [](const cv::Point3f& point) {
            return std::isfinite(point.x) && std::isfinite(point.y) &&
                std::abs(point.x) < 1e6f && std::abs(point.y) < 1e6f;
        };
        if (!drawable(thumb) || !drawable(index)) {
            text("Pinch unavailable: invalid tip coordinates", label, cv::Scalar(0, 165, 255));
            continue;
        }
        const cv::Point a(cvRound(thumb.x), cvRound(thumb.y));
        const cv::Point b(cvRound(index.x), cvRound(index.y));
        cv::line(preview, a, b, cv::Scalar(255, 255, 0), 2, cv::LINE_AA);
        cv::circle(preview, a, 11, cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
        cv::circle(preview, a, 9, cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
        cv::circle(preview, b, 6, cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
        cv::circle(preview, b, 4, cv::Scalar(255, 0, 255), cv::FILLED, cv::LINE_AA);
        text("4", a + cv::Point(-18, -10), cv::Scalar(0, 255, 255));
        text("8", b + cv::Point(10, -10), cv::Scalar(255, 0, 255));

        const auto& wrist = hand.pose.landmarks[0];
        const auto& middle = hand.pose.landmarks[9];
        const float tipDistance = std::hypot(thumb.x - index.x, thumb.y - index.y);
        const float palmLength = std::hypot(wrist.x - middle.x, wrist.y - middle.y);
        // 复用视觉测量的比例还原加权距离，避免在桌面重复定义识别权重。
        const float weightedDistance = hand.pinch.distanceRatio * palmLength;
        text(cv::format("ratio=%.3f", hand.pinch.distanceRatio), label,
            cv::Scalar(0, 255, 255));
        text(cv::format("tips=%.1f  weighted=%.1f  palm=%.1fpx",
                        tipDistance, weightedDistance, palmLength),
            label + cv::Point(0, 18),
            cv::Scalar(0, 255, 255));
    }
}

} // namespace neko::desktop
