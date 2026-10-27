#include "hand.hpp"

namespace neko::vision{
    //整帧识别
    VisionFrameResult analyzeVisionFrame(
    const cv::Mat& frame,
    cv::dnn::Net& palmModel,
    cv::dnn::Net& handPoseModel,
    const std::vector<cv::Point2f>& anchors,
    double timeSeconds){
        //定义函数返回值
        VisionFrameResult result;
        result.frameSize = frame.size();
        result.timeSeconds = timeSeconds;

        if (frame.empty()) {
            return result;
        }

        // 1. 整帧只执行一次手掌检测
        //预处理
        PalmPreprocessResult input = preprocessPalmFrame(frame);
        //传入手掌识别网络
        PalmInferenceResult raw =
    runPalmInference(palmModel, input.inputBlob);
        //解码，获得 手掌类 数组
        std::vector<PalmDetection> palms =
            decodePalms(raw, input, anchors);

        // 用来是否出现了不同的已知手势
        bool conflictingGestures = false;

        // 2. 遍历每个手掌
        for (const PalmDetection& palm : palms){

            //定义一只手在当前帧的全部识别结果类
            HandFrameResult hand;
            //手掌信息得到
            hand.palm = palm;

            //手势判别
            //预处理
            HandPosePreprocessResult handInput =
            preprocessHandPose(frame, palm);

            //预处理成功则传入手势判断网络
            if (!handInput.inputBlob.empty()) {
            HandPoseInferenceResult handRaw =
                runHandPoseInference(
                    handPoseModel, handInput.inputBlob);

            // 解码获得21个关键点、手势分类、食指位置计算，各执行一次
            //姿态信息得到
            hand.pose = decodeHandPose(handRaw, handInput);
            
            //识别出的手势得到
            hand.gesture = classifyHandGesture(hand.pose);
            
            //识别食指指向位置得到
            hand.pointing = calculatePointingPosition(
                hand.pose, hand.gesture, result.frameSize);

            // 测量捏合距离比例得到
            hand.pinch = measureHandPinch(hand.pose,result.frameSize);
            }

            // 保留这个手掌的记录(即使姿态识别失败)
            //汇总到本帧信息的手掌结果容器
            result.hands.push_back(hand);

             // 汇总本帧出现的已知手势
             //首先是手势不能未知才可以进入处理
            if (hand.gesture != HandGesture::Unknown) {
                //如果当前帧尚未有第一个手势
                if (result.frameGesture == HandGesture::Unknown) {
                    //则就用当前手掌的手势
                    result.frameGesture = hand.gesture;
                    
                }//若当前手掌手势与当前帧汇总手势不同
                 else if (result.frameGesture != hand.gesture) {
                    //标记出现了不同手势
                    conflictingGestures = true;
                }
            }

        }//每个手掌

        // 不同的已知手势发生冲突
        if (conflictingGestures) {
            //汇总手势设为未知
            result.frameGesture = HandGesture::Unknown;
        }

        //返回本帧信息
        return result;


    }//analyzeVisionFram
}//namespace