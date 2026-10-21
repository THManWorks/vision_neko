#include <iostream>
#include<vector>
#include<cmath>
#include "camera_preview.hpp"
#include"palm_detector.hpp"
#include "hand_presence.hpp"
#include "hand_pose.hpp"
#include <opencv2/core.hpp>
#include<opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/videoio.hpp>
#include <algorithm>
using namespace std;

//实现runCameraPreview
int neko::vision::runCameraPreview(int cameraIndex){
    
    //加载手部推理模型
    cv::dnn::Net palmModel=loadPalmModel("models/palm_detection_mediapipe_2023feb.onnx");
    //手部姿势推理模型
    cv::dnn::Net handPoseModel =
    loadHandPoseModel("models/handpose_estimation_mediapipe_2023feb.onnx");

    //加载锚点
    std::vector<cv::Point2f> anchors = createPalmAnchors();

    //创建一个名为 camera 的视频采集对象，并尝试打开摄像头
    cv::VideoCapture camera(cameraIndex,cv::CAP_DSHOW);
    if (! camera.isOpened()){
        cerr << "Failed to open camera\n";
        return 1;
    }
    
    cv::Mat frame;//定义了一个名为 frame、类型为 cv::Mat 的对象
    //Mat：是 OpenCV 的多维数组容器，可以保存不同类型、不同形状的数值数据。

    bool inferenceChecked = true;
    HandPresenceState presenceState;//创建检测事件对象
    while (true){
        bool yes=camera.read(frame);//读取并赋给 frame,返回是否成功
        if (frame.empty() || ! yes){
            cout<<"读取图像为空"<<endl;
            break;
        }
        cv::flip(frame,frame,1);//镜像
        // 1. 预处理
         PalmPreprocessResult input = preprocessPalmFrame(frame);

        // 2. 推理
        PalmInferenceResult raw =
        runPalmInference(palmModel, input.inputBlob);

        // 3. 解码
        std::vector<PalmDetection> detections =
        decodePalms(raw, input, anchors); 

        //当前帧只要有一个手掌检测信息就认为这帧检测到了手
        bool hasHandThisFrame=!detections.empty();

        // 更新事件状态
        HandPresenceEvent event =
        updateHandPresence(presenceState, hasHandThisFrame);
        // 只在状态发生变化时打印
        if (event == HandPresenceEvent::Appeared) {
            cout << "Hand appeared\n";
        } else if (event == HandPresenceEvent::Disappeared) {
            cout << "Hand disappeared\n";
        }

        // 根据最终结果画框
        //画在新图上，不影响原始图像数据
        cv::Mat detectionPreview = frame.clone();


        for (const PalmDetection& detection :detections) {

            //画手掌框
            cv::rectangle(
                detectionPreview,
                detection.box,
                cv::Scalar(0, 255, 0),
                2);

            //内循环 画这只手掌的 7 个参考点
            for (const cv::Point2f& point : detection.landmarks) {
                //circle画圆
                cv::circle(
                    detectionPreview,
                    cv::Point(cvRound(point.x), cvRound(point.y)),
                    4,//半径
                    cv::Scalar(0, 0, 255),
                    cv::FILLED);//填满圆形
            }
        }
            cv::imshow("Palm Detection Snapshot", detectionPreview);


        if (!detections.empty()){
            // 先取第一只手掌的检测结果
            const PalmDetection& palm = detections.front();

            // 计算扩大后的裁剪范围
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
                    cv::imshow("Hand Crop Preview", handCrop);
                }                            
            }


        }
        if ((cv::waitKey(1)&0xFF)=='q'){
            break;
        }
    }
    camera.release();
    cv::destroyAllWindows();
    return 0;
}
