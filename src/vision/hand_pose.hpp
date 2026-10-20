//手部姿势

#pragma once
#include <string>
#include <opencv2/dnn.hpp>

namespace neko::vision {
//加载手部姿势模型
cv::dnn::Net loadHandPoseModel(const std::string& modelPath);

}