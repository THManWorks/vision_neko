#include "hand_gesture.hpp"
#include <array>
#include <algorithm>
#include <cmath>

namespace neko::vision{

    //计算两个方向之间的夹角
    // 接收参数：两个二维方向向量。
    // 返回值：夹角，单位为度；无法计算时返回 -1。
    double calculateVectorAngle(
    const cv::Point2f& v1,
    const cv::Point2f& v2){
        // 两个向量长度的乘积
        double lengthProduct =
        std::hypot(v1.x, v1.y) *
        std::hypot(v2.x, v2.y);

         // 长度接近零时，无法确定方向
        if (!std::isfinite(lengthProduct) ||
        lengthProduct <= 1e-6) {
        return -1.0;
        }
        // 点积 ÷ 长度乘积，得到夹角的余弦值
        double cosine = v1.dot(v2) / lengthProduct;

        if (!std::isfinite(cosine)) {
            return -1.0;
        }

        // 避免浮点误差使数值超出 acos 的有效范围
        cosine = std::clamp(cosine, -1.0, 1.0);

        // acos 得到弧度，再转换成度数
        return std::acos(cosine) * 180.0 / CV_PI;


    }

    //接下来两个辅助函数。
    //官方用来判断握拳还是张手的依据


    //计算五根手指各自的夹角(二维的，只考虑x和y)
    // 接收参数：一只手的姿态解码结果，包含21个关键点。
    // 返回值：五根手指的夹角，依次为拇指、食指、中指、无名指、小指。
    std::array<double, 5> calculateFingerAngles(
        const HandPoseResult& hand){

            const auto& points = hand.landmarks; // 引用已有的21个关键点
            // 每根手指用于计算方向的关键点编号
            //第i个元素就是第i根手指的
            //// 顺序：拇指、食指、中指、无名指、小指
            const int joints[] = {2, 6, 10, 14, 18};//选来与手腕构成第一个方向的关节点。
            const int nearTips[] = {3, 7, 11, 15, 19};//紧挨指尖的关节点
            const int tips[] = {4, 8, 12, 16, 20};//指尖

            //存五个角度
            std::array<double, 5> angles{};
            for (int i = 0; i < 5; ++i){
                 // 第一个方向：关节点指向手腕（0号点）
                cv::Point2f direction1(
                    points[0].x - points[joints[i]].x,
                    points[0].y - points[joints[i]].y);

                // 第二个方向：指尖指向它前面的关节点
                cv::Point2f direction2(
                    points[nearTips[i]].x - points[tips[i]].x,
                    points[nearTips[i]].y - points[tips[i]].y);

                //算角度
                angles[i] =
                    calculateVectorAngle(direction1, direction2);

            }
            return angles;
        }


        //计算指尖是否比参考关节点离手腕更远
        // 接收参数：一只手的姿态解码结果，使用关键点的 x、y 坐标。
        // 返回值：五指的伸展状态，依次为拇指、食指、中指、无名指、小指。
        std::array<bool, 5> calculateFingerStates(
            const HandPoseResult& hand){

                const auto& points = hand.landmarks;

                // 官方选定的参考点和指尖编号
                // 注意：拇指的参考点是5号点
                const int references[] = {5, 6, 10, 14, 18};
                const int tips[] = {4, 8, 12, 16, 20};

                //bool数组，判断是否更远
                std::array<bool, 5> extended{};

                for (int i = 0; i < 5; ++i) {
                // 手腕到指尖的距离
                double tipDistance = std::hypot(
                    points[tips[i]].x - points[0].x,
                    points[tips[i]].y - points[0].y);

                // 手腕到参考点的距离
                double referenceDistance = std::hypot(
                    points[references[i]].x - points[0].x,
                    points[references[i]].y - points[0].y);

                //指尖比参考点离手腕更远则判断为伸展
                extended[i] = tipDistance > referenceDistance;
            }

            return extended;
            }

    //判断手势函数
    //// 采用官方二维规则，使用关键点的 x、y 判断张手和握拳：
    // 接收参数：一只手的姿态解码结果。
    // 返回值：张手、握拳、只伸出食指或未知。
    HandGesture classifyHandGesture(const HandPoseResult& hand){

        if (!hand.valid) {
            return HandGesture::Unknown;
        }


        // 取得五个夹角和五个伸展状态
        std::array<double, 5> angles =
            calculateFingerAngles(hand);

        std::array<bool, 5> extended =
            calculateFingerStates(hand);

        // 无法计算夹角时，不进行分类
        for (double angle : angles) {
            if (!std::isfinite(angle) || angle < 0.0) {
                return HandGesture::Unknown;
            }
        }
        // 握拳：拇指角度大于30°（不用检查是否伸展，这是官方的规则）
        // 其余四指：角度大于65°，并且未伸展
        bool fist = angles[0] > 30.0;
        for (int i = 1; i < 5; ++i) {
            //累积判断条件：只要有一根手指不符合要求，fist 就变成 false
            fist = fist &&
                (angles[i] > 65.0) &&
                !extended[i];
        }

        if (fist) {
            return HandGesture::Fist;
        }

        // 张手：五指角度都小于49°，并且全部伸展
        bool openPalm = true;
        for (int i = 0; i < 5; ++i) {
        //累积判断条件：只要有一根手指不符合要求，openPalm就变成 false
            openPalm = openPalm &&
                    (angles[i] < 49.0) &&
                    extended[i];
        }

        if (openPalm) {
            return HandGesture::OpenPalm;
        }

        // 仅伸食指：拇指弯曲，食指伸直
        bool pointing =
            angles[0] > 30.0 &&
            angles[1] < 49.0 &&
            extended[1];

        // 中指、无名指、小指均弯曲且未伸展
        for (int i = 2; i < 5; ++i) {
            pointing = pointing &&
                    angles[i] > 65.0 &&
                    !extended[i];
        }

        if (pointing) {
            return HandGesture::Pointing;
        }
                //两个都不是，就未知
        return HandGesture::Unknown;
    }

}
