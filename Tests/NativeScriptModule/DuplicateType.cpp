#include "Scripting/ScriptBehaviour.h"

//故意与 Core Camera 重名，用于验证动态模块类型提交会被拒绝。
class Camera final : public ScriptBehaviour
{
    OBJECT_TYPE_DECLARE(Camera)
};

OBJECT_TYPE_IMPLEMENT(Camera, ScriptBehaviour)
