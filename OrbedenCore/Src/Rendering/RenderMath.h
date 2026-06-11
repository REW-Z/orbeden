#pragma once

#include "Rendering/RenderTypes.h"

namespace RenderMath
{
    //向量点积
    float32 Dot(const vector3& a, const vector3& b);

    //向量叉积
    vector3 Cross(const vector3& a, const vector3& b);

    //向量归一化
    vector3 Normalize(const vector3& value);

    //矩阵相乘
    matrix4x4 Mul(const matrix4x4& a, const matrix4x4& b);

    //创建平移矩阵
    matrix4x4 Translation(const vector3& value);

    //创建旋转矩阵
    matrix4x4 Rotation(const quaternion& value);

    //创建缩放矩阵
    matrix4x4 Scale(const vector3& value);

    //创建本地变换矩阵
    matrix4x4 TRS(const vector3& position, const quaternion& rotation, const vector3& scale);

    //创建透视投影矩阵
    matrix4x4 Perspective(float32 fieldOfView, float32 aspect, float32 nearPlane, float32 farPlane);

    //创建正交投影矩阵
    matrix4x4 Orthographic(float32 left, float32 right, float32 bottom, float32 top, float32 nearPlane, float32 farPlane);

    //创建右手系观察矩阵
    matrix4x4 LookAt(const vector3& eye, const vector3& target, const vector3& up);

    //矩阵求逆，失败时返回单位矩阵
    matrix4x4 Inverse(const matrix4x4& value);

    //变换点
    vector3 TransformPoint(const matrix4x4& matrix, const vector3& point);

    //变换方向
    vector3 TransformDirection(const matrix4x4& matrix, const vector3& direction);

    //读取矩阵平移
    vector3 GetTranslation(const matrix4x4& matrix);

    //计算本地包围盒
    bounds3 CalculateBounds(const List<vector3>& points);

    //变换包围盒
    bounds3 TransformBounds(const matrix4x4& matrix, const bounds3& bounds);

    //从 VP 矩阵提取视锥
    frustum BuildFrustum(const matrix4x4& viewProjection);

    //检测视锥与包围盒是否相交
    bool Intersects(const frustum& viewFrustum, const bounds3& bounds);
}
