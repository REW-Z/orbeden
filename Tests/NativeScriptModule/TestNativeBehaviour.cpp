#include "TestNativeBehaviour.h"

OBJECT_TYPE_IMPLEMENT(TestNativeBehaviour, ScriptBehaviour)
OBJECT_TYPE_IMPLEMENT(TestDerivedNativeBehaviour, TestNativeBehaviour)

void TestNativeBehaviour::OnStart()
{
}

void TestNativeBehaviour::OnUpdate(float32 deltaTime)
{
    speed += deltaTime;
}

void TestNativeBehaviour::OnEnd()
{
}

void TestDerivedNativeBehaviour::OnLateUpdate(float32 deltaTime)
{
    privateValue += deltaTime;
}
