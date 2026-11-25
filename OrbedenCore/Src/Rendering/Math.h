#pragma once

#include "Rendering/RenderTypes.h"

namespace Orbeden
{
    //无状态的向量、矩阵与空间运算
    class Math final
    {
    public:
        Math() = delete;

        //向量点积
        static float32 Dot(const vector3& a, const vector3& b);

        //向量叉积
        static vector3 Cross(const vector3& a, const vector3& b);

        //向量归一化
        static vector3 Normalize(const vector3& value);

        //矩阵相乘
        static matrix4x4 Mul(const matrix4x4& a, const matrix4x4& b);

        //四元数相乘
        static quaternion Mul(const quaternion& a, const quaternion& b);

        //创建绕局部 +Z 的旋转四元数，角度单位是度
        static quaternion RotationZ(float32 degrees);

        //读取矩阵三个基向量的最大长度，用于把局部尺寸换算到世界尺度
        static float32 GetMaximumBasisLength(const matrix4x4& matrix);

        //创建平移矩阵
        static matrix4x4 Translation(const vector3& value);

        //创建旋转矩阵
        static matrix4x4 Rotation(const quaternion& value);

        //创建缩放矩阵
        static matrix4x4 Scale(const vector3& value);

        //创建本地变换矩阵
        static matrix4x4 TRS(const vector3& position, const quaternion& rotation, const vector3& scale);

        //创建透视投影矩阵
        static matrix4x4 Perspective(float32 fieldOfView, float32 aspect, float32 nearPlane, float32 farPlane);

        //创建近端为一远端为零的透视投影矩阵
        static matrix4x4 PerspectiveReversed(float32 fieldOfView, float32 aspect, float32 nearPlane, float32 farPlane);

        //创建正交投影矩阵
        static matrix4x4 Orthographic(float32 left, float32 right, float32 bottom, float32 top, float32 nearPlane, float32 farPlane);

        //创建右手系观察矩阵
        static matrix4x4 LookAt(const vector3& eye, const vector3& target, const vector3& up);

        //矩阵求逆，失败时返回单位矩阵
        static matrix4x4 Inverse(const matrix4x4& value);

        //变换点
        static vector3 TransformPoint(const matrix4x4& matrix, const vector3& point);

        //变换方向
        static vector3 TransformDirection(const matrix4x4& matrix, const vector3& direction);

        //读取矩阵平移
        static vector3 GetTranslation(const matrix4x4& matrix);

        //计算本地包围盒
        static bounds3 CalculateBounds(const List<vector3>& points);

        //从第一个顶点初始化网格包围盒
        static bounds3 CalculateBounds(const List<vector3>& points, bool startAtFirstPoint);

        //变换包围盒
        static bounds3 TransformBounds(const matrix4x4& matrix, const bounds3& bounds);

        //从 VP 矩阵提取视锥
        static frustum BuildFrustum(const matrix4x4& viewProjection);

        //检测视锥与包围盒是否相交
        static bool Intersects(const frustum& viewFrustum, const bounds3& bounds);

        //并行检测四个包围盒，返回对应的相交位
        static uint32 IntersectsFour(const frustum& viewFrustum, const bounds3* const (&bounds)[4]);
    };
}
