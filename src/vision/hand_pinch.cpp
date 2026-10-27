#include "hand_pinch.hpp"
#include <algorithm>
#include <cmath>

namespace neko::vision {
    //测量函数
    HandPinchMeasurement measureHandPinch(
    const HandPoseResult& hand,
    const cv::Size& frameSize){
        HandPinchMeasurement result; // 默认测量无效
        
        // 姿态或画面尺寸无效时，无法测量
        if (!hand.valid ||
            frameSize.width <= 0 ||
            frameSize.height <= 0) {
            return result;
        }
        // 引用已有关键点，坐标已经还原到原图
        const cv::Point3f& thumbTip = hand.landmarks[4];   // 拇指尖
        const cv::Point3f& indexTip = hand.landmarks[8];   // 食指尖
        //下面这两个是用来计算掌长的,距离不用绝对数值，用与掌长的比例
        const cv::Point3f& wrist = hand.landmarks[0];      // 手腕
        const cv::Point3f& middleBase = hand.landmarks[9]; // 中指根部

        // 计算两指尖的二维距离，只使用x、y
        float tipDistance = std::hypot(
            thumbTip.x - indexTip.x,
            thumbTip.y - indexTip.y);
        
        // 计算掌长：手腕到中指根部的二维距离
        float palmLength = std::hypot(
            middleBase.x - wrist.x,
            middleBase.y - wrist.y);

        // 排除异常数值，掌长也不能接近零
        if (!std::isfinite(tipDistance) ||
            !std::isfinite(palmLength) ||
            palmLength <= 1e-6f) {
            return result;
        }
        //距离与掌长的比例
        float ratio = tipDistance / palmLength;

        if (!std::isfinite(ratio)) {
            return result;
        }
//------测两指尖连线中点坐标在全图位置比例-----
        // 两指尖中点在原图中的像素坐标
        float centerX = thumbTip.x * 0.5f + indexTip.x * 0.5f;
        float centerY = thumbTip.y * 0.5f + indexTip.y * 0.5f;

        // 转换为画面中的位置比例
        float x = centerX / static_cast<float>(frameSize.width);
        float y = centerY / static_cast<float>(frameSize.height);

        if (!std::isfinite(x) || !std::isfinite(y)) {
            return result;
        }

        // 保存有效的测量结果
        // 限制到画面范围内
        result.position = cv::Point2f(
            std::clamp(x, 0.0f, 1.0f),
            std::clamp(y, 0.0f, 1.0f));


        result.valid = true;
        result.distanceRatio = ratio;

        return result;


    }//函数结束

    //捏合判断函数
    // 接收参数：跨帧状态、本帧捏合测量、当前时刻（秒）。
    HandPinchEvent updateHandPinch(
        HandPinchState& state,
        const HandPinchMeasurement& measurement,
        double nowSeconds){
            const float closeThreshold = 0.11f; // 捏合阈值
            const float openThreshold = 0.20f;  // 松开阈值
            const int confirmFrames = 2;        // 连续两帧确认

            const double maxMissingSeconds = 0.5;//允许短暂丢失

            // 有效测量间隔过长：清空记录；原来处于捏合则通知松开
            if (nowSeconds - state.lastValidTime > maxMissingSeconds) {

                bool wasPinching = state.pinching;
                state = HandPinchState{};

                if (wasPinching) {
                    //松开
                    return HandPinchEvent::Released;
                }

            }//有效测量间隔过长处理

            // 无效测量中断连续计数，但未超过丢失阈值保留已确认的状态
            if (!measurement.valid ||
                !std::isfinite(measurement.distanceRatio) ||
                measurement.distanceRatio < 0.0f) {
                state.closeFrames = 0;
                state.openFrames = 0;
                // 本帧没有新事件，已确认的状态由丢失超时逻辑处理
                return HandPinchEvent::None;
            }//无效测量但未超过丢失事件处理

            //开始处理有效测量，首先更新时间和距离比例
            state.lastValidTime = nowSeconds;
            float ratio = measurement.distanceRatio;

            if (!state.pinching) {
            // 尚未捏合：判断是否连续满足接触条件

            if (ratio <= closeThreshold) {
                //若距离比例小于捏合阈值，则累加一次连续捏合数
                ++state.closeFrames;
            } else {
                //反之归0
                state.closeFrames = 0;
            }
            //若连续捏合数达到规定次数，则判断为捏合
            if (state.closeFrames >= confirmFrames) {
                state.pinching = true;
                state.closeFrames = 0;
                return HandPinchEvent::Pinched;
            }
        } else {
            //判断松开同理
            // 已经捏合：判断是否连续满足松开条件
            if (ratio >= openThreshold) {
                ++state.openFrames;
            } else {
                state.openFrames = 0;
            }

            if (state.openFrames >= confirmFrames) {
                state.pinching = false;
                state.openFrames = 0;
                return HandPinchEvent::Released;
            }
        }
        // 未达到连续确认条件，或保持原有状态：本帧没有新事件
        return HandPinchEvent::None;
        }//捏合判断函数
}//namespace