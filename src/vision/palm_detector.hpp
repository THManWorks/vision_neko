#pragma once

#include<string>
#include<opencv2/dnn.hpp>
#include <vector>
#include <opencv2/core.hpp>



namespace neko::vision{
    //获取神经网络函数
    cv::dnn::Net loadPalmModel(const std::string &modelPath);
    //这里的 cv::dnn::Net 表示神经网络对象
    //函数接收模型路径，返回加载好的网络。

    //预处理函数返回结构体
    struct PalmPreprocessResult {
        cv::Mat inputBlob;//处理后的图像数据
        int resizedWidth;
        int resizedHeight;//缩放后的尺寸

        int left;               // 左侧补边
        int top;                // 顶部补边
        cv::Size originalSize;  // 原图尺寸
    };

    //推理函数返回结构体
    struct PalmInferenceResult {
    cv::Mat rawScores;
    cv::Mat rawBoxes;
    };

    // 一只手掌的最终检测结果（解码返回结构体）
    struct PalmDetection {
        cv::Rect box;
        float score;
    };
    
    // 1. 预处理函数：原始画面 → 模型输入和坐标变换信息
    PalmPreprocessResult preprocessPalmFrame(
        const cv::Mat& frame);
    
    // 2. 推理函数：模型+输入 → 原始分数和坐标数据
    PalmInferenceResult runPalmInference(
        cv::dnn::Net& model,
        const cv::Mat& inputBlob);
    

    // 3. 解码：推理输出 + 预处理变换信息 + 锚点
    // → 最终手掌框和分数
    std::vector<PalmDetection> decodePalms(
        const PalmInferenceResult& raw,
        const PalmPreprocessResult& input,
        const std::vector<cv::Point2f>& anchors);

    // 获取锚点函数
    std::vector<cv::Point2f> createPalmAnchors();
}
