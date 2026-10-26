#include "hand_position.hpp"

#include <algorithm>
#include <cmath>

namespace neko::vision{

//识别食指指向位置函数
// 接收参数：手部姿态结果、当前手势、当前原图尺寸。
HandPositionResult calculatePointingPosition(
    const HandPoseResult& hand,
    HandGesture gesture,
    const cv::Size& frameSize){

        //返回结果
        HandPositionResult result; // 默认无效

        // 仅在姿态有效、手势为仅伸食指、画面尺寸有效时处理
        //有一项不满足直接返回无效值
        if (!hand.valid ||
            gesture != HandGesture::Pointing ||
            frameSize.width <= 0 ||
            frameSize.height <= 0){
            return result;
        }

        // 8号关键点是食指顶部，坐标已经还原到当前frame
        const cv::Point3f& point = hand.landmarks[8];

        // 排除无穷大、NaN等无效数值
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            return result;
        }

        // 像素坐标转换为画面中的位置比例
        float x = point.x / static_cast<float>(frameSize.width);
        float y = point.y / static_cast<float>(frameSize.height);

        // 将位置限制在0～1，避免关键点稍微超出画面
        result.position = cv::Point2f(
            std::clamp(x, 0.0f, 1.0f),
            std::clamp(y, 0.0f, 1.0f));

        result.valid = true;
        return result;
        
    }//calculatePointingPosition



}//namespace