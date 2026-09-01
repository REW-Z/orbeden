#pragma once

#include "Scripting/ScriptBehaviour.h"

class TestNativeBehaviour : public ScriptBehaviour
{
    OBJECT_TYPE_DECLARE_BASE(TestNativeBehaviour)

public:
    float32 speed = 2.0f;

protected:
    void OnStart();
    void OnUpdate(float32 deltaTime);
    void OnEnd();
};

class TestDerivedNativeBehaviour final : public TestNativeBehaviour
{
    OBJECT_TYPE_DECLARE(TestDerivedNativeBehaviour)

private:
    ORBEDEN_SERIALIZE_FIELD
    float32 privateValue = 3.0f;

protected:
    void OnLateUpdate(float32 deltaTime);
};
