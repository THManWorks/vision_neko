#include <iostream>
#include<vector>
#include<cmath>
#include "camera_preview.hpp"
#include"palm_detector.hpp"
#include "hand_presence.hpp"
#include "hand_pose.hpp"
#include "hand_gesture.hpp"
#include <opencv2/core.hpp>
#include "hand_gesture_state.hpp"
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

    HandPresenceState presenceState;//创建手部检测事件对象
    HandGestureState gestureState;//创建手势检测事件对象
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
        bool hasOpenPalm = false;
        bool hasFist = false;
        //检测手掌姿势
        if (!detections.empty()){
            for(const PalmDetection& palm :detections){

            //调用预处理函数
            HandPosePreprocessResult handInput =
            preprocessHandPose(frame, palm);

            if (!handInput.inputBlob.empty()) {

                // 调用封装后的推理函数
                HandPoseInferenceResult handRaw =
                runHandPoseInference(handPoseModel, handInput.inputBlob);
                // 解码，还原到当前frame的坐标
                HandPoseResult hand = decodeHandPose(handRaw, handInput);

                // 结果有效时，绘制21个关键点
                if (hand.valid) {
                    for (const cv::Point3f& point : hand.landmarks) {
                        cv::circle(
                            detectionPreview,
                            cv::Point(cvRound(point.x), cvRound(point.y)),
                            3,
                            cv::Scalar(255, 0, 0), // 蓝色
                            cv::FILLED);
                    }
                } // if (hand.valid)

                // 根据这只手的21个关键点判断手势
                HandGesture gesture = classifyHandGesture(hand);
                // 汇总这一帧出现过的手势
                if (gesture == HandGesture::OpenPalm) {
                    hasOpenPalm = true;
                } else if (gesture == HandGesture::Fist) {
                    hasFist = true;
                }

            }// if (!handInput.inputBlob.empty())

        }// 每只手的处理结束
    }    // if (!detections.empty()

        // 整帧只有一种已识别手势时，采用该手势
        // 同时出现张手和握拳，或都没有时，保持 Unknown
        HandGesture frameGesture = HandGesture::Unknown;

        if (hasOpenPalm && !hasFist) {
            frameGesture = HandGesture::OpenPalm;
        } else if (hasFist && !hasOpenPalm) {
            frameGesture = HandGesture::Fist;
        }

        // 每帧更新一次，包括没有检测到手的帧
        HandGestureEvent gestureEvent =
            updateHandGesture(gestureState, frameGesture);

        // 只在产生新事件时打印
        if (gestureEvent == HandGestureEvent::OpenPalmConfirmed) {
            cout << "OpenPalm confirmed\n";
        } else if (gestureEvent == HandGestureEvent::FistConfirmed) {
            cout << "Fist confirmed\n";
        } else if (gestureEvent == HandGestureEvent::Cleared) {
            cout << "Gesture cleared\n";
        }

        cv::imshow("Palm Detection Snapshot", detectionPreview);
        if ((cv::waitKey(1)&0xFF)=='q'){
            break;
        }
    } // while 循环结束
    camera.release();
    cv::destroyAllWindows();
    return 0;
}
