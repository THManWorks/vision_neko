#include "hand_presence.hpp"

namespace neko::vision {
HandPresenceEvent updateHandPresence(
    HandPresenceState& state, bool hasHandThisFrame)
    {
    //注意区分 hasHandThisFrame：当前一帧有没有检测到手。
    //state.handPresent：经过连续多帧确认后，判断手是否存在。
        if (hasHandThisFrame){
            //如果当前帧检测到了手,中断“连续没检测到”的计数
           state.missingFrames=0;

           // 尚未确认手存在，才需要累积帧数
           //因为我们要连续几帧都检测到手才能确认手的存在
           if(!state.handPresent){
            ++state.detectedFrames;

                //以三帧为确认阈值
                if (state.detectedFrames >= 3) {
                    state.handPresent = true;//确认存在
                    state.detectedFrames = 0;
                    return HandPresenceEvent::Appeared;//返回“刚确认手出现”的事件
                }

           }
        }else{
                // 当前没检测到手，中断“连续检测到”的计数
                state.detectedFrames = 0;

                // 之前确认手存在，才需要判断是否离开
                if (state.handPresent) {
                    ++state.missingFrames;

                    //五帧为消失阈值
                    if (state.missingFrames >= 5) {
                        state.handPresent = false;
                        state.missingFrames = 0;

                        return HandPresenceEvent::Disappeared;
                        //返回消失事件
                    }

                }

        } 
    // 尚未达到条件，或稳定状态没有变化
    return HandPresenceEvent::None;           
    }

}
