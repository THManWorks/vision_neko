#include <iostream>
#include<vector>
#include<cmath>
#include "camera_preview.hpp"
#include"palm_detector.hpp"
#include <opencv2/core.hpp>
#include<opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/videoio.hpp>
#include <algorithm>
using namespace std;

//实现runCameraPreview
int neko::vision::runCameraPreview(int cameraIndex){
    
    //加载推理模型
    cv::dnn::Net palmModel=loadPalmModel("models/palm_detection_mediapipe_2023feb.onnx");
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
    bool yes;

    bool inferenceChecked = true;

    while (true){
        yes=camera.read(frame);//读取并赋给 frame,返回是否成功
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

        // 根据最终结果画框
        cv::Mat detectionPreview = frame.clone();
            for (const PalmDetection& detection :detections) {
                //画框
                cv::rectangle(
                    detectionPreview,
                    detection.box,
                    cv::Scalar(0, 255, 0),
                    2);
            }
            cv::imshow("Palm Detection Snapshot", detectionPreview);
        if ((cv::waitKey(1)&0xFF)=='q'){
            break;
        }
    }
    camera.release();
    cv::destroyAllWindows();
    return 0;
}
