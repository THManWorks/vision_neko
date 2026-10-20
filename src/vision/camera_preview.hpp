///.hpp 声明模块提供哪些接口，以及应该怎样使用它们
//摄像头循环函数
#pragma once

namespace neko::vision{
//命名空间，用来组织名称，避免与其他模块重名 
//这里是嵌套命名空间大空间neko包含小空间vision

    //函数声明
    int runCameraPreview(int cameraIndex = 0);
}