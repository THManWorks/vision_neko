#pragma once

#include<string>
#include<opencv2/dnn.hpp>
#include <vector>
#include <opencv2/core.hpp>

namespace neko::vision{
    cv::dnn::Net loadPalmModel(const std::string &modelPath);
    //这里的 cv::dnn::Net 表示神经网络对象
    //函数接收模型路径，返回加载好的网络。

    std::vector<cv::Point2f> createPalmAnchors();
    //返回一对浮点坐标 x、y
}