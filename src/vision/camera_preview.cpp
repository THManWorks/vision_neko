#include <iostream>
#include "camera_preview.hpp"
#include <opencv2/core.hpp>
#include<opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/videoio.hpp>

using namespace std;

//实现runCameraPreview
int neko::vision::runCameraPreview(int cameraIndex){
    //创建一个名为 camera 的视频采集对象，并尝试打开摄像头
    cv::VideoCapture camera(cameraIndex,cv::CAP_DSHOW);
    if (! camera.isOpened()){
        cerr << "Failed to open camera\n";
        return 1;
    }
    
    cv::Mat frame;//定义了一个名为 frame、类型为 cv::Mat 的对象
    //Mat：用于存储图像等数据的类型。
    bool yes;
    while (true){
        yes=camera.read(frame);//读取并赋给 frame,返回是否成功
        if (frame.empty() || ! yes){
            cout<<"读取图像为空"<<endl;
            break;
        }
        cv::flip(frame,frame,1);//镜像
        cv::imshow("Neko Camera",frame);
        if ((cv::waitKey(1)&0xFF)=='q'){
            break;

        }

    }
    camera.release();
    cv::destroyAllWindows();
    return 0;



}
