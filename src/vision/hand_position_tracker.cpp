//卡尔曼滤波改善指尖跟随

#include "hand_position_tracker.hpp"
#include <algorithm>
#include <cmath>

namespace neko::vision {
    //清除状态函数
    void resetHandPositionTracker(HandPositionTrackerState& state)
    {   
        //创建一个采用默认值的新状态
        state = HandPositionTrackerState{};
    }

    //初始化状态函数
    // 接收参数：跟踪状态、第一次有效的位置、当前时刻（秒）。
    void initializeTracker(
        HandPositionTrackerState& state,
        const cv::Point2f& position,
        double nowSeconds){
            //引用
            //filter：滤波器
            cv::KalmanFilter& filter = state.filter;
            
            //初始化滤波器：状态有四个数，测量有两个数 
            //4个状态：x、y、vx、vy；2个测量：x、y
            filter.init(4, 2, 0, CV_32F);

            //初始化状态转移矩阵
            //cv::setIdentity()：把矩阵把非对角线清零，对角线设为指定值；默认值是 1，
            //后面填入时间间隔，才能根据速度预测移动。
            cv::setIdentity(filter.transitionMatrix);

            // 设置测量矩阵 H
            //矩阵.setTo()：给矩阵中的所有元素赋值
            //矩阵.at<float>()：访问矩阵中的一个数
            filter.measurementMatrix.setTo(0);
            filter.measurementMatrix.at<float>(0, 0) = 1.0f;
            filter.measurementMatrix.at<float>(1, 1) = 1.0f;
            //得到               x  y  vx vy
            //    测量的 x        1  0  0  0
            //    测量的 y        0  1  0  0

            // 运动误差：先初始化为0，预测前设置固定的简化Q
            filter.processNoiseCov.setTo(0);

            // 测量误差R的初始设置为（0.0001对角矩阵）
            cv::setIdentity(
                filter.measurementNoiseCov, cv::Scalar(0.0001f));

            // 设置误差P的初始化：速度比位置更不确定
            cv::setIdentity(
                filter.errorCovPost, cv::Scalar(0.0001f));
            filter.errorCovPost.at<float>(2, 2) = 0.25f;
            filter.errorCovPost.at<float>(3, 3) = 0.25f;

            // 初始位置来自真实测量，初始速度为零
            filter.statePost.at<float>(0, 0) = position.x;
            filter.statePost.at<float>(1, 0) = position.y;
            filter.statePost.at<float>(2, 0) = 0.0f;
            filter.statePost.at<float>(3, 0) = 0.0f;
            
            //除滤波器外，状态剩余部分的初始化
            state.lastFrameTime = nowSeconds;
            state.lastMeasurementTime = nowSeconds;
            state.initialized = true;



        }//initializeTracker

        //更新状态函数
        // 接收参数：跟踪状态、当前帧的位置测量、当前时刻（秒）。
        HandPositionTrackingResult updateHandPositionTracker(
            HandPositionTrackerState& state,
            const HandPositionResult& measurement,
            double nowSeconds){
                
                //创建返回结果
                HandPositionTrackingResult result; // 默认无效

                // 1. 检查时间与测量数据
                //isfinite:判断是否是有限值
                if (!std::isfinite(nowSeconds)) {
                    resetHandPositionTracker(state);
                    return result;
                }
                //判断是否有效测量
                bool hasMeasurement =measurement.valid &&
                std::isfinite(measurement.position.x) &&
                std::isfinite(measurement.position.y);

                // 时间倒退，或超过0.5秒没有真实测量：清空旧状态
                if (state.initialized &&
                    (nowSeconds < state.lastFrameTime ||
                    nowSeconds - state.lastMeasurementTime > 0.5)) {
                    resetHandPositionTracker(state);
                }

                // 2. 尚未初始化：等待第一次有效测量
                if (!state.initialized) {
                    if (!hasMeasurement) {
                        return result;
                    }
                    //初始化
                    initializeTracker(state, measurement.position, nowSeconds);
                    result.valid = true;
                    result.position = measurement.position;
                    return result;
                }

                // 3. 根据实际时间间隔预测
                float dt = static_cast<float>(
                    nowSeconds - state.lastFrameTime);
                

                cv::KalmanFilter& filter = state.filter;
                //补充状态转移矩阵（初始化是仅设为了单位矩阵）
                // x下一时刻 = x + vx × dt
                // y下一时刻 = y + vy × dt
                filter.transitionMatrix.at<float>(0, 2) = dt;
                filter.transitionMatrix.at<float>(1, 3) = dt;

                // 设置简易的运动噪声Q：每次预测增加固定的不确定性
                // 状态顺序：x、y、vx、vy
                // 位置误差方差
                cv::setIdentity(
                    filter.processNoiseCov, cv::Scalar(0.0001f));
                // 速度误差方差
                filter.processNoiseCov.at<float>(2, 2) = 0.01f;
                filter.processNoiseCov.at<float>(3, 3) = 0.01f;

                /*Q = [0.0001    0       0       0
                        0    0.0001     0       0
                        0      0      0.01     0
                        0      0       0      0.01]*/

                // 按状态转移矩阵预测，返回4行1列：[x、y、vx、vy]
                cv::Mat estimate = filter.predict();
                // 本帧已经处理，更新处理时间
                state.lastFrameTime = nowSeconds;
                
                //如果测量值有效，则开始矫正
                if (hasMeasurement) {
                // 创建2行1列、保存float的矩阵
                cv::Mat observed(2, 1, CV_32F);

                // 装入本帧真正检测到的位置
                observed.at<float>(0, 0) = measurement.position.x;
                observed.at<float>(1, 0) = measurement.position.y;

                // 用真实测量校正预测，取得校正后的状态
                estimate = filter.correct(observed);

                // 只有真实测量才能刷新这个时间
                state.lastMeasurementTime = nowSeconds;
            }
            //取出位置
            float x = estimate.at<float>(0, 0);
            float y = estimate.at<float>(1, 0);

            // 防止异常数值继续传出去
            if (!std::isfinite(x) || !std::isfinite(y)) {
                resetHandPositionTracker(state);
                return result;
            }
            //设置返回结果
            result.valid = true;
            result.predicted = !hasMeasurement;

            // 输出位置限制在画面范围内
            result.position = cv::Point2f(
                std::clamp(x, 0.0f, 1.0f),
                std::clamp(y, 0.0f, 1.0f));

            return result;

            }/// updateHandPositionTracker

}//namespace