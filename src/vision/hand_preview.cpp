#include "hand_preview.hpp"

#include <algorithm>
#include <opencv2/imgproc.hpp>

namespace neko::vision{
    //绘图
    cv::Mat drawHandPreview(
    const cv::Mat& frame,
    const VisionFrameResult& observations,
    const HandInteractionResult& interaction)
    {
        if (frame.empty()) {
            return cv::Mat{};
        }

        // 在副本上绘制，保留原图供其他功能使用
        cv::Mat preview = frame.clone();
        // 1. 绘制每只手的检测结果
        for (const HandFrameResult& hand : observations.hands) {

        // 手掌框：绿色
        cv::rectangle(
            preview, hand.palm.box,
            cv::Scalar(0, 255, 0), 2);


        // 手掌框：绿色
        cv::rectangle(
            preview, hand.palm.box,
            cv::Scalar(0, 255, 0), 2);

        // 7个手掌参考点：红色
        for (const cv::Point2f& point : hand.palm.landmarks) {
            cv::circle(
                preview,
                cv::Point(cvRound(point.x), cvRound(point.y)),
                4, cv::Scalar(0, 0, 255), cv::FILLED);
        }//绘制手掌参考点

        // 有效姿态的21个关键点：蓝色
        if (hand.pose.valid) {
            for (const cv::Point3f& point : hand.pose.landmarks) {
                cv::circle(
                    preview,
                    cv::Point(cvRound(point.x), cvRound(point.y)),
                    3, cv::Scalar(255, 0, 0), cv::FILLED);
            }//遍历每个点
        }//绘制手部姿态点
    }//遍历每个手



    // 2. 绘制本帧的食指跟踪结果
    // 放在手掌循环外：没有检测到手时，也能显示预测位置
    const HandPositionTrackingResult& tracking =
        interaction.tracking;

    if (tracking.valid) {
        // 将归一化位置转换回画面像素坐标
        //创建一个点 marker
        cv::Point marker(
            std::clamp(
                cvRound(tracking.position.x * frame.cols),
                0, frame.cols - 1),
            std::clamp(
                cvRound(tracking.position.y * frame.rows),
                0, frame.rows - 1));

        // 绿色表示有真实测量参与，橙色表示只有预测
        cv::Scalar color = tracking.predicted
            ? cv::Scalar(0, 165, 255)
            : cv::Scalar(0, 255, 0);

        cv::circle(preview, marker, 8, color, 2);

        cv::putText(
            preview,
            tracking.predicted ? "Predicting" : "Tracking",
            cv::Point(10, 30),
            cv::FONT_HERSHEY_SIMPLEX,
            0.7, color, 2);
    }

    return preview;



    }//drawHandPreview

}//namespace