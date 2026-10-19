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
    cout << "Anchor count: " << anchors.size() << endl;


    //创建一个名为 camera 的视频采集对象，并尝试打开摄像头
    cv::VideoCapture camera(cameraIndex,cv::CAP_DSHOW);
    if (! camera.isOpened()){
        cerr << "Failed to open camera\n";
        return 1;
    }
    
    cv::Mat frame;//定义了一个名为 frame、类型为 cv::Mat 的对象
    //Mat：是 OpenCV 的多维数组容器，可以保存不同类型、不同形状的数值数据。
    bool yes;

    bool inferenceChecked = false;

    while (true){
        yes=camera.read(frame);//读取并赋给 frame,返回是否成功
        if (frame.empty() || ! yes){
            cout<<"读取图像为空"<<endl;
            break;
        }
        cv::flip(frame,frame,1);//镜像
        //对图像预处理，使其尺寸适合传入神经网络
        //计算保持宽高比的缩放比例
        double scale=std::min(
            192.0/frame.cols,
            192.0/frame.rows);
        
        // 计算缩放后的尺寸
        //cvRoun是把浮点数取整
        int resizedWidth = cvRound(frame.cols * scale);
        int resizedHeight = cvRound(frame.rows * scale);
        
        cv::Mat resized;
        cv::resize(frame,resized,cv::Size(resizedWidth,resizedHeight));
        // 将剩余空间分配到两侧
        //计算缩放后的图片，四周分别需要补多少像素的黑边
        //才能居中放进 192×192 的区域。因为原始图片的长高不一样
        int left = (192 - resizedWidth) / 2;
        int right = 192 - resizedWidth - left;
        int top = (192 - resizedHeight) / 2;
        int bottom = 192 - resizedHeight - top;
        
        cv::Mat padded;  // 创建一个空图像对象，用来接收结果
        cv::copyMakeBorder(//这是 OpenCV 用来给图像四周添加边框的函数
            resized,                // 输入：缩放后的图片
            padded,                 // 输出：补边后的图片
            top, bottom, left, right,// 上、下、左、右分别补多少像素
            cv::BORDER_CONSTANT,    // 使用固定颜色填充边框
            cv::Scalar(0, 0, 0));   // 填充颜色为黑色
        // OpenCV 摄像头画面是 BGR，模型要求 RGB
        cv::Mat rgb;
        cv::cvtColor(padded,rgb,cv::COLOR_BGR2RGB);


        // 转为 float，并把像素范围从 0～255 缩放到 0～1（模型要求）
        //convertTo()是OpenCV 的图像数据转换函数
        //CV_32F：把数据转成 32 位浮点数。
        //1.0 / 255.0：每个像素通道的值乘以这个系数，即除以 255。
        cv::Mat normalized;
        rgb.convertTo(normalized, CV_32F, 1.0 / 255.0);

        // 模型要求的维度：[批次数量, 高度, 宽度, 通道数]
        int inputShape[] = {1, 192, 192, 3};
        cv::Mat inputBlob = normalized.reshape(1, 4, inputShape);
        //1:	新矩阵每个元素包含一个数值
        //4:	新矩阵有四个维度 即：哪张图片 → 哪一行 → 哪一列 → 哪个颜色
        
        //开始推理
        if (!inferenceChecked){
            palmModel.setInput(inputBlob);//设置网络输入。

            //获取所有输出的名称
            std::vector<std::string>outputNames=palmModel.getUnconnectedOutLayersNames();

            // 执行推理，将各个输出保存到 outputs
            std::vector<cv::Mat>outputs;
            palmModel.forward(outputs,outputNames);//执行网络计算，取得输出
            // 打印输出名称和形状

            cv::Mat rawScores;
            cv::Mat rawBoxes;
            for (std::size_t i = 0; i < outputs.size(); ++i) {
                //可以看有哪些输出，以及它们的维度
                //也就是下面我们为什么知道分数的名称是Identity_1
                cout << outputNames[i] << ": " << outputs[i].size << endl;
                
                //如果该输出的名称是分数的话就存到分数
                if(outputNames[i]=="Identity_1"){
                    rawScores=outputs[i];
                }
                //如果是边框信息就存到边框信息
                if (outputNames[i] == "Identity") {
                    rawBoxes = outputs[i];
                }
            }
            //没找到分数
            if (rawScores.empty()|| rawBoxes.empty()){
                cerr << "Score output not found\n";
                return 1;
            }
            double maxScore=0.0;//最大分数
            std::vector<int> candidateIndices;//保存超过阈值的候选编号
            //候选数量不等于手掌数量

            for (int j = 0; j < rawScores.size[1]; ++j) {

                // 从 [1, 2016, 1] 中读取第 j 个候选的原始分数
                //at() 是 cv::Mat 提供的成员函数，用来访问指定位置的数据
                //rawScores：要读取的矩阵。
                //<float>：告诉 OpenCV，里面存的是 float 类型。
                //(0, j, 0)：三个维度上的下标，从 0 开始。
                double logit = rawScores.at<float>(0, j, 0);

                // Sigmoid：将原始分数映射到 0～1
                double score = 1.0 / (1.0 + std::exp(-logit));

                maxScore = std::max(maxScore, score);


                //0.5：本次使用的初始筛选阈值。
                if (score >= 0.5) {
                    candidateIndices.push_back(j);
                }
            }

            //官方正确坐标变换：
            //锚点 × 192 + 偏移量 → 192×192 输入图中的中心
            // 减去补边、除以缩放比例 → 原始画面中的位置

            cv::Mat detectionPreview=frame.clone();//clone() 创建独立图像,画框不会修改原来的 frame

            // 使用实际缩放尺寸，考虑取整造成的细微差异
            double scaleX = static_cast<double>(resizedWidth) / frame.cols;
            double scaleY = static_cast<double>(resizedHeight) / frame.rows;

            //分别用来保存候选框和分数，同一下标必须对应同一个候选。
            std::vector<cv::Rect> candidateBoxes;
            std::vector<float> candidateScores;
            for(int index : candidateIndices){
                float dx = rawBoxes.at<float>(0, index, 0);
                float dy = rawBoxes.at<float>(0, index, 1);
                float width = rawBoxes.at<float>(0, index, 2);
                float height = rawBoxes.at<float>(0, index, 3);


                if (width <= 0 || height <= 0) {
                    continue;
                }
                // 对应候选的官方锚点，坐标范围为 0～1
                cv::Point2f anchor = anchors.at(index);
                
                // 中心位置：先转换成192×192 输入图坐标
                double centerX = anchor.x * 192.0 + dx;
                double centerY = anchor.y * 192.0 + dy;
                
                //撤销补边和缩放
                double x1 = (centerX - width / 2.0 - left) / scaleX;
                double y1 = (centerY - height / 2.0 - top) / scaleY;
                double x2 = (centerX + width / 2.0 - left) / scaleX;
                double y2 = (centerY + height / 2.0 - top) / scaleY;

                cv::Rect box(
                    cv::Point(cvRound(x1), cvRound(y1)),
                    cv::Point(cvRound(x2), cvRound(y2))
                );//Rect是矩形对象，此处两个参数：左上角、右下角

                // 将超出画面的部分裁掉
                box = box & cv::Rect(0, 0, frame.cols, frame.rows);
                if (!box.empty()) {
                    //根据候选编号 index，取出它的原始分数。
                    double logit = rawScores.at<float>(0, index, 0);
                    //用 Sigmoid 转成 0～1 的分数，再用 static_cast<float> 转成 float
                    float score = static_cast<float>(
                        1.0 / (1.0 + std::exp(-logit)));
                    
                    //装入容器，只保存先不画
                    candidateBoxes.push_back(box);
                    candidateScores.push_back(score);
                }
            }
            //保存“保留编号”的容器
            std::vector<int> keptIndices;

            //有候选框时，执行NMS
            if (!candidateBoxes.empty()) {
                cv::dnn::NMSBoxes(
                //opencv函数，用于去除重复检测框。

                candidateBoxes,   // 所有候选框
                candidateScores,  // 每个框的分数
                0.5f,            // 分数筛选阈值
                0.3f,            // 框重叠程度的阈值
                keptIndices);    // 将保留下来的下标写进这里
            }
            for (int i : keptIndices) {
                //画框
                cv::rectangle(
                    detectionPreview,
                    candidateBoxes[i],
                    cv::Scalar(0, 255, 0),
                    2);
            }
            cv::imshow("Palm Detection Snapshot", detectionPreview);
            cout << "Max score: " << maxScore << endl;
            inferenceChecked = true;
            cout << "Before NMS: " << candidateBoxes.size() << endl;
            cout << "After NMS: " << keptIndices.size() << endl;
        }
        cv::imshow("Palm Input Preview",padded);
        cv::imshow("Neko Camera",frame);
        if ((cv::waitKey(1)&0xFF)=='q'){
            break;

        }

    }
    camera.release();
    cv::destroyAllWindows();
    return 0;
}
