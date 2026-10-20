#include "hand_pose.hpp"

namespace neko::vision {

cv::dnn::Net loadHandPoseModel(const std::string& modelPath)
{   //读取模型
    cv::dnn::Net model = cv::dnn::readNetFromONNX(modelPath);
    return model;
}

}