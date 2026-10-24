#pragma once

#include "hand_pose.hpp"



namespace neko::vision {

// 手势识别结果
//定义一个手势状态变量，有三种值
enum class HandGesture {
    Unknown,   // 尚未识别出支持的手势
    OpenPalm,  // 张开手掌
    Fist,       // 握拳
    Pointing   // 仅伸出食指，其余手指闭合
};






// 接收参数：一只手的姿态解码结果，包含有效标志和21个关键点。
// 返回值：识别出的手势类别。
HandGesture classifyHandGesture(const HandPoseResult& hand);

}
