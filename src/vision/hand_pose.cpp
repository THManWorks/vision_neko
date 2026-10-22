#include "hand_pose.hpp"
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <vector>
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


    //预处理函数
    HandPosePreprocessResult preprocessHandPose(
    const cv::Mat& frame,
    const PalmDetection& palm)
{
    if (frame.empty()) {
        return {};
    }

    // 预处理代码
    /*原图 frame
    → 第一次裁图
    → 第一次补黑边
    → 旋转，让手朝上
    → 第二次裁图
    → 第二次补成正方形
    → 缩放到224×224
    → 送入网络*/

    // 扩大裁剪范围
    //官方采用的放大系数为4
    cv::Rect cropBox = expandPalmBox(palm.box, 4.0f, frame.size());

    if (!cropBox.empty()) {
        // 取出这个矩形区域，并创建独立副本
        //这是 OpenCV 提供的“取图像区域”的写法。
        //图像数据来自 frame，cropBox 告诉它取哪一块
        cv::Mat cropped = frame(cropBox).clone();

        // 官方做法：计算对角线长度，再转为整数
        //对角线有多长，就把裁剪下来的图补为多长的正方形边框
        int sideLen = static_cast<int>(
        std::hypot(cropped.cols, cropped.rows));

        // 总共需要补多少宽度、高度
        int padWidth = sideLen - cropped.cols;
        int padHeight = sideLen - cropped.rows;

        // 分配到四周，余数留给右侧、底部
        int left = padWidth / 2;
        int top = padHeight / 2;
        int right = padWidth - left;
        int bottom = padHeight - top;

        //补图
        cv::Mat padded;
        cv::copyMakeBorder(
            cropped, padded,
            top, bottom, left, right,
            cv::BORDER_CONSTANT,
            cv::Scalar(0, 0, 0));

        //计算需要旋转角
        double angle = calculatePalmRotation(palm);

        // 裁剪区域的中心，加上左侧、顶部补边量
        cv::Point2f center(
        left + cropped.cols / 2.0f,
        top + cropped.rows / 2.0f);

        //生成旋转规则
        //center：围绕哪个点旋转。
        //angle：旋转多少度，正数逆时针，负数顺时针。
        //1.0：缩放比例为 1，保持原来的大小。
        cv::Mat rotationMatrix =
        cv::getRotationMatrix2D(center, angle, 1.0);


        // 旋转补边后的手部图像
        cv::Mat rotatedHand;
        //旋转函数
        warpAffine(
        padded, rotatedHand,
        rotationMatrix, padded.size());

        //把七个参考点的坐标从原图转到补边图
        // 原图坐标 → 裁图坐标 → 补边图坐标
        std::vector<cv::Point2f> paddedLandmarks;
        for (const cv::Point2f& point : palm.landmarks) {
            paddedLandmarks.push_back(cv::Point2f(
            point.x - cropBox.x + left,
            point.y - cropBox.y + top));
        }

        // 用与图像相同的旋转矩阵，计算旋转后的坐标
        //transform() 负责把旋转前的点坐标，换算成旋转后的点坐标。
        //根据规则rotationMatrix
        std::vector<cv::Point2f> rotatedLandmarks;
        cv::transform(paddedLandmarks, rotatedLandmarks, rotationMatrix);

        //能包住旋转后 7 个参考点的矩形。
        // 先用第一个点初始化范围
        float minX = rotatedLandmarks.front().x;
        float minY = rotatedLandmarks.front().y;
        float maxX = minX;
        float maxY = minY;

        // 遍历所有点，找出最左、最上、最右、最下的位置
        for (const cv::Point2f& point : rotatedLandmarks) {
            minX = std::min(minX, point.x);
            minY = std::min(minY, point.y);
            maxX = std::max(maxX, point.x);
            maxY = std::max(maxY, point.y);
        }

        // 保存为：左上角坐标 + 宽高
        //框
        cv::Rect2f rotatedPalmBox(
            minX,
            minY,
            maxX - minX,
            maxY - minY);

        //现在按官方流程，
        //把基础框向上移动原框高度的 0.4 倍，
        //再把宽、高扩大到 3 倍，以覆盖手指区域


        // 先向上移动基础框高度的 0.4 倍
        //注意，y变小是向上
        cv::Rect2f shiftedPalmBox = rotatedPalmBox;
        shiftedPalmBox.y -= 0.4f * rotatedPalmBox.height;

        // 再将宽、高扩大到 3 倍，并限制到旋转图像范围内
        //hand_pose里的函数
        cv::Rect handCropBox = expandPalmBox(
        shiftedPalmBox, 3.0f, rotatedHand.size());

        //取区域＋复制成独立图像
        if (!handCropBox.empty()) {
            cv::Mat handCrop = rotatedHand(handCropBox).clone();

            //把裁出的 handCrop 补成正方形，为后面缩到 224×224 做准备
            // 以较长的一边作为正方形边长
            int handSide = std::max(handCrop.cols, handCrop.rows);

            // 将需要补的黑边分配到两侧
            int handPadLeft = (handSide - handCrop.cols) / 2;
            int handPadTop = (handSide - handCrop.rows) / 2;
            int handPadRight = handSide - handCrop.cols - handPadLeft;
            int handPadBottom = handSide - handCrop.rows - handPadTop;

            cv::Mat handSquare;
            cv::copyMakeBorder(
            //cv::copyMakeBorder() 是 OpenCV 给图像四周添加边框的函数。
                handCrop, handSquare,
                handPadTop, handPadBottom, handPadLeft, handPadRight,
                cv::BORDER_CONSTANT,
                cv::Scalar(0, 0, 0));

            // 1. 缩放到模型要求的 224×224
            cv::Mat handResized;
            cv::resize(
                handSquare, handResized,
                cv::Size(224, 224),
                0, 0, cv::INTER_AREA);

            // 2. BGR → RGB
            cv::Mat handRgb;
            cv::cvtColor(handResized, handRgb, cv::COLOR_BGR2RGB);

            // 3. 转为 float，像素值从 0～255 缩放到 0～1
            cv::Mat handNormalized;
            //convertTo() 是 cv::Mat 的成员函数，
            //用来转换数据类型，也可以同时缩放数值。
            handRgb.convertTo(handNormalized, CV_32F, 1.0 / 255.0);

            // 4. 整理成 [批次, 高度, 宽度, 颜色通道]
            int handInputShape[] = {1, 224, 224, 3};
            //最终输入
            cv::Mat handInputBlob =
                handNormalized.reshape(1, 4, handInputShape);

            // 将处理结果装入返回结构体
            HandPosePreprocessResult result;
            result.inputBlob = handInputBlob;
            result.preview = handResized;
            result.rotationMatrix = rotationMatrix;

            result.padBias = cv::Point2f(
                cropBox.x - left,
                cropBox.y - top);

            result.handCropBox = handCropBox;
            result.handPadding =
                cv::Point(handPadLeft, handPadTop);
            result.handSide = handSide;

            return result; // 预处理成功，返回结果


            }// if (!handCropBox.empty())

        }// if (!cropBox.empty())

    return {}; // 没能裁出有效区域时，返回空结果

    } // preprocessHandPose

//手部姿态推理函数
// 接收参数：加载好的手部姿态网络、预处理后的模型输入。
HandPoseInferenceResult runHandPoseInference(
    cv::dnn::Net& model,
    const cv::Mat& inputBlob)
    {
        model.setInput(inputBlob);

        //获取各输出的名称
        std::vector<std::string> outputNames =
        model.getUnconnectedOutLayersNames();

        //开始推理并接收推理结果
        std::vector<cv::Mat> outputs;
        model.forward(outputs, outputNames);

        //最终返回结果
        HandPoseInferenceResult result;

        // 按输出名称保存对应数据
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            if (outputNames[i] == "Identity") {
                result.rawLandmarks = outputs[i];
            } else if (outputNames[i] == "Identity_1") {
                result.rawConfidence = outputs[i];
            } else if (outputNames[i] == "Identity_2") {
                result.rawHandedness = outputs[i];
            } else if (outputNames[i] == "Identity_3") {
                result.rawWorldLandmarks = outputs[i];
            }
        }

            // 确认四组输出都已取得
            if (result.rawLandmarks.empty() ||
                result.rawConfidence.empty() ||
                result.rawHandedness.empty() ||
                result.rawWorldLandmarks.empty()) {
                CV_Error(cv::Error::StsError,
                        "Hand pose model outputs not found");
            }

            return result;

    }

    //解码函数
    // 接收参数：手部姿态网络的原始输出、预处理结果（用于还原坐标）。
    HandPoseResult decodeHandPose(
        const HandPoseInferenceResult& raw,
        const HandPosePreprocessResult& input)
        {
            //用于保存解码返回值
            HandPoseResult result;

            // 1. 读取置信度，低于阈值就返回无效结果
            result.confidence = raw.rawConfidence.at<float>(0, 0);
            if (result.confidence < 0.8f) {
                return result;
            }

            //解码
            //按照预处理的过程逆回去

            //左右手判断值，不用解码，保存即可
            result.handedness = raw.rawHandedness.at<float>(0, 0);

            // 2.21个点的坐标
            //从 单通道、21行，变为3列：每行是x、y、z
            cv::Mat points = raw.rawLandmarks.reshape(1, 21);

            // 从224×224还原到缩放前的正方形尺寸
            float scale = input.handSide / 224.0f;

            // 3. 撤销缩放、第二次补边和裁剪
            // 得到这些点在 rotatedHand 图像中的位置
            std::vector<cv::Point2f> rotatedPoints;

            for (int i = 0; i < 21; ++i) {
                float x = points.at<float>(i, 0) * scale
                        - input.handPadding.x
                        + input.handCropBox.x;

                float y = points.at<float>(i, 1) * scale
                        - input.handPadding.y
                        + input.handCropBox.y;

                rotatedPoints.push_back(cv::Point2f(x, y));
            }
            // 4. 生成逆变换矩阵，用来撤销之前的旋转
            cv::Mat inverseRotation;
            cv::invertAffineTransform(
                input.rotationMatrix, inverseRotation);

            // 撤销旋转，回到第一次补边图的坐标系
            std::vector<cv::Point2f> paddedPoints;
            cv::transform(
                rotatedPoints, paddedPoints, inverseRotation);

                    // 5. 加回第一次裁剪与补边的偏移，回到当前frame
            for (int i = 0; i < 21; ++i) {
                result.landmarks[i] = cv::Point3f(
                    paddedPoints[i].x + input.padBias.x,
                    paddedPoints[i].y + input.padBias.y,
                    points.at<float>(i, 2) * scale);
            }

            result.valid = true;
            return result;


        }

} // namespace neko::vision
