#include "hand_pose.hpp"
#include <cmath>
namespace neko::vision {

    cv::dnn::Net loadHandPoseModel(const std::string& modelPath){   
        //读取模型
        cv::dnn::Net model = cv::dnn::readNetFromONNX(modelPath);
        return model;
    }

    // 计算把手掌转到朝上所需的角度，单位为度
    double calculatePalmRotation(const PalmDetection& palm){

        //官方参考点 landmarks[0]（手腕处）和 landmarks[2]（中指根部）。
        const cv::Point2f& base = palm.landmarks[0];
        const cv::Point2f& middle = palm.landmarks[2];

        // 从手腕指向中指根部的方向
        //pi/2就代表刚好垂直向上
        double dx = middle.x - base.x;
        double dy = middle.y - base.y;

        // 计算转到朝上所需的角度，当前单位是弧度
        //CV_PI 就是π,pi/2就代表刚好垂直向上
        //std::atan2()：根据水平、竖直两个方向的变化量，返回与水平向右方向的夹角（弧度）。有负角度.
        double radians = CV_PI / 2.0 - std::atan2(-dy, dx);


        // 将角度统一到 [-π, π) 范围
        //std::floor()：向下取整
        //减去整数个圆(2pi)
        //如270°变成-90°
        radians -= 2.0 * CV_PI *
        std::floor((radians + CV_PI) / (2.0 * CV_PI));


        // 弧度转换为度
        return radians * 180.0 / CV_PI;


    }

    cv::Rect expandPalmBox(
        const cv::Rect2f& palmBox,
        float factor,
        const cv::Size& imageSize)
    {
        // 原框中心
        float centerX = palmBox.x + palmBox.width / 2.0f;
        float centerY = palmBox.y + palmBox.height / 2.0f;

        // 扩大后的半宽、半高
        float halfWidth = palmBox.width * factor / 2.0f;
        float halfHeight = palmBox.height * factor / 2.0f;


        // 转为整数，并限制到图像边界
        //std::clamp(数值, 最小值, 最大值)
        //用于限制范围
        int x1 = std::clamp(
            static_cast<int>(centerX - halfWidth), 0, imageSize.width);
        int y1 = std::clamp(
            static_cast<int>(centerY - halfHeight), 0, imageSize.height);
        int x2 = std::clamp(
            static_cast<int>(centerX + halfWidth), 0, imageSize.width);
        int y2 = std::clamp(
            static_cast<int>(centerY + halfHeight), 0, imageSize.height);

        return cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2));
    }



}