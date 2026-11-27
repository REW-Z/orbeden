#include "Rendering/Math.h"

#include <algorithm>
#include <cmath>
#include <limits>

#if defined(_M_X64) || defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define ORBEDEN_MATH_SSE2 1
#else
#define ORBEDEN_MATH_SSE2 0
#endif

namespace
{
    constexpr float32 Pi = 3.14159265358979323846f;

    vector3 AddVector(const vector3& a, const vector3& b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    vector3 SubVector(const vector3& a, const vector3& b)
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    vector3 ScaleVector(const vector3& value, float32 scale)
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    float32 Length(const vector3& value)
    {
        return std::sqrt(Orbeden::Math::Dot(value, value));
    }

    plane3 NormalizePlane(const plane3& plane)
    {
        float32 length = Length(plane.normal);
        if (length <= 0.000001f) return plane3();

        float32 invLength = 1.0f / length;
        return { ScaleVector(plane.normal, invLength), plane.distance * invLength };
    }

#if ORBEDEN_MATH_SSE2
    //按原有三项乘积和加减顺序计算四个余子式
    __m128 CalculateCofactors(__m128 first, __m128 second, __m128 third, __m128 signs)
    {
        const __m128 a = _mm_shuffle_ps(first, first, _MM_SHUFFLE(0, 0, 0, 1));
        const __m128 b = _mm_shuffle_ps(first, first, _MM_SHUFFLE(1, 1, 2, 2));
        const __m128 c = _mm_shuffle_ps(first, first, _MM_SHUFFLE(2, 3, 3, 3));
        const __m128 d = _mm_shuffle_ps(second, second, _MM_SHUFFLE(0, 0, 0, 1));
        const __m128 e = _mm_shuffle_ps(second, second, _MM_SHUFFLE(1, 1, 2, 2));
        const __m128 f = _mm_shuffle_ps(second, second, _MM_SHUFFLE(2, 3, 3, 3));
        const __m128 g = _mm_shuffle_ps(third, third, _MM_SHUFFLE(0, 0, 0, 1));
        const __m128 h = _mm_shuffle_ps(third, third, _MM_SHUFFLE(1, 1, 2, 2));
        const __m128 i = _mm_shuffle_ps(third, third, _MM_SHUFFLE(2, 3, 3, 3));

        __m128 result = _mm_mul_ps(_mm_mul_ps(_mm_xor_ps(a, signs), e), i);
        result = _mm_sub_ps(result, _mm_xor_ps(_mm_mul_ps(_mm_mul_ps(a, h), f), signs));
        result = _mm_sub_ps(result, _mm_xor_ps(_mm_mul_ps(_mm_mul_ps(b, d), i), signs));
        result = _mm_add_ps(result, _mm_xor_ps(_mm_mul_ps(_mm_mul_ps(b, g), f), signs));
        result = _mm_add_ps(result, _mm_xor_ps(_mm_mul_ps(_mm_mul_ps(c, d), h), signs));
        return _mm_sub_ps(result, _mm_xor_ps(_mm_mul_ps(_mm_mul_ps(c, g), e), signs));
    }
#endif

    //将四元数转换为列主序旋转矩阵
    matrix4x4 BuildRotationMatrix(const quaternion& q)
    {
#if ORBEDEN_MATH_SSE2
        //并行计算对角项和成对的非对角项
        const __m128 values = _mm_setr_ps(q.x, q.y, q.z, q.w);
        const __m128 squares = _mm_mul_ps(values, values);
        const __m128 left = _mm_shuffle_ps(squares, squares, _MM_SHUFFLE(3, 0, 0, 1));
        const __m128 right = _mm_shuffle_ps(squares, squares, _MM_SHUFFLE(3, 1, 2, 2));
        const __m128 pairs = _mm_mul_ps(_mm_shuffle_ps(values, values, _MM_SHUFFLE(3, 1, 0, 0)),
            _mm_shuffle_ps(values, values, _MM_SHUFFLE(3, 2, 2, 1)));
        const __m128 mixed = _mm_mul_ps(_mm_shuffle_ps(values, values, _MM_SHUFFLE(3, 3, 3, 3)),
            _mm_shuffle_ps(values, values, _MM_SHUFFLE(3, 0, 1, 2)));
        const __m128 two = _mm_set1_ps(2.0f);
        float32 diagonal[4], positive[4], negative[4];
        _mm_storeu_ps(diagonal, _mm_sub_ps(_mm_set1_ps(1.0f), _mm_mul_ps(two, _mm_add_ps(left, right))));
        _mm_storeu_ps(positive, _mm_mul_ps(two, _mm_add_ps(pairs, mixed)));
        _mm_storeu_ps(negative, _mm_mul_ps(two, _mm_sub_ps(pairs, mixed)));
#else
        const float32 xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const float32 xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        const float32 wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        const float32 diagonal[] = { 1.0f - 2.0f * (yy + zz), 1.0f - 2.0f * (xx + zz), 1.0f - 2.0f * (xx + yy) };
        const float32 positive[] = { 2.0f * (xy + wz), 2.0f * (xz + wy), 2.0f * (yz + wx) };
        const float32 negative[] = { 2.0f * (xy - wz), 2.0f * (xz - wy), 2.0f * (yz - wx) };
#endif

        matrix4x4 result;
        result.m[0] = diagonal[0];
        result.m[1] = positive[0];
        result.m[2] = negative[1];
        result.m[4] = negative[0];
        result.m[5] = diagonal[1];
        result.m[6] = positive[2];
        result.m[8] = positive[1];
        result.m[9] = negative[2];
        result.m[10] = diagonal[2];
        return result;
    }
}

namespace Orbeden
{
    float32 Math::Dot(const vector3& a, const vector3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    vector3 Math::Cross(const vector3& a, const vector3& b)
    {
        return
        {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x,
        };
    }

    vector3 Math::Normalize(const vector3& value)
    {
        float32 length = Length(value);
        if (length <= 0.000001f) return vector3();

        return ScaleVector(value, 1.0f / length);
    }

    quaternion Math::Mul(const quaternion& a, const quaternion& b)
    {
        quaternion result;
        result.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
        result.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
        result.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
        result.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
        return result;
    }

    quaternion Math::RotationZ(float32 degrees)
    {
        float32 half = degrees * 0.01745329251994329577f * 0.5f;
        quaternion result;
        result.x = 0.0f;
        result.y = 0.0f;
        result.z = std::sin(half);
        result.w = std::cos(half);
        return result;
    }

    float32 Math::GetMaximumBasisLength(const matrix4x4& matrix)
    {
        vector3 basis[3] =
        {
            { matrix.m[0], matrix.m[1], matrix.m[2] },
            { matrix.m[4], matrix.m[5], matrix.m[6] },
            { matrix.m[8], matrix.m[9], matrix.m[10] },
        };
        float32 maximum = 0.0f;
        for (const vector3& axis : basis)
        {
            float32 length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
            maximum = std::max(maximum, length);
        }

        return maximum;
    }

    matrix4x4 Math::Mul(const matrix4x4& a, const matrix4x4& b)
    {
        matrix4x4 result;
#if ORBEDEN_MATH_SSE2
        //并行计算每列的四个分量
        const __m128 a0 = _mm_loadu_ps(a.m);
        const __m128 a1 = _mm_loadu_ps(a.m + 4);
        const __m128 a2 = _mm_loadu_ps(a.m + 8);
        const __m128 a3 = _mm_loadu_ps(a.m + 12);
        for (int column = 0; column < 4; ++column)
        {
            const float32* values = b.m + column * 4;
            __m128 product = _mm_mul_ps(a0, _mm_set1_ps(values[0]));
            product = _mm_add_ps(product, _mm_mul_ps(a1, _mm_set1_ps(values[1])));
            product = _mm_add_ps(product, _mm_mul_ps(a2, _mm_set1_ps(values[2])));
            product = _mm_add_ps(product, _mm_mul_ps(a3, _mm_set1_ps(values[3])));
            _mm_storeu_ps(result.m + column * 4, product);
        }
#else
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                result.m[column * 4 + row] =
                    a.m[0 * 4 + row] * b.m[column * 4 + 0] +
                    a.m[1 * 4 + row] * b.m[column * 4 + 1] +
                    a.m[2 * 4 + row] * b.m[column * 4 + 2] +
                    a.m[3 * 4 + row] * b.m[column * 4 + 3];
            }
        }

#endif
        return result;
    }

    matrix4x4 Math::Translation(const vector3& value)
    {
        matrix4x4 result;
        result.m[12] = value.x;
        result.m[13] = value.y;
        result.m[14] = value.z;
        return result;
    }

    matrix4x4 Math::Rotation(const quaternion& value)
    {
        quaternion q = value;
        float32 length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (length > 0.000001f)
        {
            float32 invLength = 1.0f / length;
            q.x *= invLength;
            q.y *= invLength;
            q.z *= invLength;
            q.w *= invLength;
        }

        return BuildRotationMatrix(q);
    }

    matrix4x4 Math::Scale(const vector3& value)
    {
        matrix4x4 result;
        result.m[0] = value.x;
        result.m[5] = value.y;
        result.m[10] = value.z;
        return result;
    }

    matrix4x4 Math::TRS(const vector3& position, const quaternion& rotation, const vector3& scale)
    {
        return TRS(position, rotation, scale, false);
    }

    matrix4x4 Math::TRS(const vector3& position, const quaternion& rotation, const vector3& scale, bool unitRotation)
    {
        //缩放旋转矩阵的三个基向量
        matrix4x4 result = unitRotation ? BuildRotationMatrix(rotation) : Rotation(rotation);
#if ORBEDEN_MATH_SSE2
        _mm_storeu_ps(result.m, _mm_mul_ps(_mm_loadu_ps(result.m), _mm_set1_ps(scale.x)));
        _mm_storeu_ps(result.m + 4, _mm_mul_ps(_mm_loadu_ps(result.m + 4), _mm_set1_ps(scale.y)));
        _mm_storeu_ps(result.m + 8, _mm_mul_ps(_mm_loadu_ps(result.m + 8), _mm_set1_ps(scale.z)));
#else
        for (int row = 0; row < 4; ++row)
        {
            result.m[row] *= scale.x;
            result.m[4 + row] *= scale.y;
            result.m[8 + row] *= scale.z;
        }
#endif

        //直接填入平移分量
        result.m[12] = position.x;
        result.m[13] = position.y;
        result.m[14] = position.z;
        return result;
    }

    matrix4x4 Math::Perspective(float32 fieldOfView, float32 aspect, float32 nearPlane, float32 farPlane)
    {
        float32 safeAspect = aspect > 0.000001f ? aspect : 1.0f;
        float32 safeNear = std::max(nearPlane, 0.0001f);
        float32 safeFar = std::max(farPlane, safeNear + 0.001f);
        float32 f = 1.0f / std::tan(fieldOfView * 0.5f * Pi / 180.0f);

        matrix4x4 result{};
        std::fill(std::begin(result.m), std::end(result.m), 0.0f);
        result.m[0] = f / safeAspect;
        result.m[5] = f;
        result.m[10] = (safeFar + safeNear) / (safeNear - safeFar);
        result.m[11] = -1.0f;
        result.m[14] = (2.0f * safeFar * safeNear) / (safeNear - safeFar);
        return result;
    }

    matrix4x4 Math::PerspectiveReversed(float32 fieldOfView, float32 aspect, float32 nearPlane, float32 farPlane)
    {
        float32 safeAspect = aspect > 0.000001f ? aspect : 1.0f;
        float32 safeNear = std::max(nearPlane, 0.0001f);
        float32 safeFar = std::max(farPlane, safeNear + 0.001f);
        float32 f = 1.0f / std::tan(fieldOfView * 0.5f * Pi / 180.0f);
        matrix4x4 result{};
        std::fill(std::begin(result.m), std::end(result.m), 0.0f);
        result.m[0] = f / safeAspect;
        result.m[5] = f;
        result.m[10] = safeNear / (safeFar - safeNear);
        result.m[11] = -1.0f;
        result.m[14] = safeNear * (safeFar / (safeFar - safeNear));
        return result;
    }

    matrix4x4 Math::Orthographic(float32 left, float32 right, float32 bottom, float32 top, float32 nearPlane, float32 farPlane)
    {
        float32 width = std::max(right - left, 0.0001f);
        float32 height = std::max(top - bottom, 0.0001f);
        float32 depth = std::max(farPlane - nearPlane, 0.0001f);

        matrix4x4 result;
        result.m[0] = 2.0f / width;
        result.m[5] = 2.0f / height;
        result.m[10] = -2.0f / depth;
        result.m[12] = -(right + left) / width;
        result.m[13] = -(top + bottom) / height;
        result.m[14] = -(farPlane + nearPlane) / depth;
        return result;
    }

    matrix4x4 Math::LookAt(const vector3& eye, const vector3& target, const vector3& up)
    {
        vector3 forward = Normalize({ target.x - eye.x, target.y - eye.y, target.z - eye.z });
        if (Dot(forward, forward) <= 0.000001f) forward = { 0.0f, 0.0f, -1.0f };

        vector3 right = Normalize(Cross(forward, up));
        if (Dot(right, right) <= 0.000001f) right = { 1.0f, 0.0f, 0.0f };

        vector3 realUp = Cross(right, forward);

        matrix4x4 result;
        result.m[0] = right.x;
        result.m[1] = realUp.x;
        result.m[2] = -forward.x;
        result.m[4] = right.y;
        result.m[5] = realUp.y;
        result.m[6] = -forward.y;
        result.m[8] = right.z;
        result.m[9] = realUp.z;
        result.m[10] = -forward.z;
        result.m[12] = -Dot(right, eye);
        result.m[13] = -Dot(realUp, eye);
        result.m[14] = Dot(forward, eye);
        return result;
    }

    matrix4x4 Math::Inverse(const matrix4x4& value)
    {
        const float32* m = value.m;
        matrix4x4 result;
        float32 inv[16];

#if ORBEDEN_MATH_SSE2
        //转置输入矩阵，按列计算伴随矩阵
        __m128 row0 = _mm_loadu_ps(m);
        __m128 row1 = _mm_loadu_ps(m + 4);
        __m128 row2 = _mm_loadu_ps(m + 8);
        __m128 row3 = _mm_loadu_ps(m + 12);
        _MM_TRANSPOSE4_PS(row0, row1, row2, row3);
        const __m128 evenSigns = _mm_setr_ps(0.0f, -0.0f, 0.0f, -0.0f);
        const __m128 oddSigns = _mm_setr_ps(-0.0f, 0.0f, -0.0f, 0.0f);
        _mm_storeu_ps(inv, CalculateCofactors(row1, row2, row3, evenSigns));
        _mm_storeu_ps(inv + 4, CalculateCofactors(row0, row2, row3, oddSigns));
        _mm_storeu_ps(inv + 8, CalculateCofactors(row0, row1, row3, evenSigns));
        _mm_storeu_ps(inv + 12, CalculateCofactors(row0, row1, row2, oddSigns));
#else
        inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
        inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
        inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
        inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
        inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
        inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
        inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
        inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
        inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
        inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
        inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
        inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
        inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
        inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
        inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
        inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

#endif

        //按行列式展开项的相对误差判断退化
        float32 determinant = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        float32 determinantMagnitude = std::abs(m[0] * inv[0]) + std::abs(m[1] * inv[4])
            + std::abs(m[2] * inv[8]) + std::abs(m[3] * inv[12]);
        if (std::abs(determinant) <= 0.000001f * determinantMagnitude) return matrix4x4();

        determinant = 1.0f / determinant;
#if ORBEDEN_MATH_SSE2
        const __m128 scale = _mm_set1_ps(determinant);
        for (int index = 0; index < 16; index += 4)
        {
            _mm_storeu_ps(result.m + index, _mm_mul_ps(_mm_loadu_ps(inv + index), scale));
        }
#else
        for (int index = 0; index < 16; ++index)
        {
            result.m[index] = inv[index] * determinant;
        }
#endif

        return result;
    }

    vector3 Math::TransformPoint(const matrix4x4& matrix, const vector3& point)
    {
#if ORBEDEN_MATH_SSE2
        //并行计算齐次坐标
        __m128 position = _mm_mul_ps(_mm_loadu_ps(matrix.m), _mm_set1_ps(point.x));
        position = _mm_add_ps(position, _mm_mul_ps(_mm_loadu_ps(matrix.m + 4), _mm_set1_ps(point.y)));
        position = _mm_add_ps(position, _mm_mul_ps(_mm_loadu_ps(matrix.m + 8), _mm_set1_ps(point.z)));
        position = _mm_add_ps(position, _mm_loadu_ps(matrix.m + 12));
        alignas(16) float32 values[4];
        _mm_store_ps(values, position);
        float32 x = values[0];
        float32 y = values[1];
        float32 z = values[2];
        float32 w = values[3];
#else
        float32 x = matrix.m[0] * point.x + matrix.m[4] * point.y + matrix.m[8] * point.z + matrix.m[12];
        float32 y = matrix.m[1] * point.x + matrix.m[5] * point.y + matrix.m[9] * point.z + matrix.m[13];
        float32 z = matrix.m[2] * point.x + matrix.m[6] * point.y + matrix.m[10] * point.z + matrix.m[14];
        float32 w = matrix.m[3] * point.x + matrix.m[7] * point.y + matrix.m[11] * point.z + matrix.m[15];
#endif
        if (std::abs(w) > 0.000001f && std::abs(w - 1.0f) > 0.000001f)
        {
            float32 invW = 1.0f / w;
            x *= invW;
            y *= invW;
            z *= invW;
        }

        return { x, y, z };
    }

    vector3 Math::TransformDirection(const matrix4x4& matrix, const vector3& direction)
    {
#if ORBEDEN_MATH_SSE2
        //并行计算方向分量
        __m128 result = _mm_mul_ps(_mm_loadu_ps(matrix.m), _mm_set1_ps(direction.x));
        result = _mm_add_ps(result, _mm_mul_ps(_mm_loadu_ps(matrix.m + 4), _mm_set1_ps(direction.y)));
        result = _mm_add_ps(result, _mm_mul_ps(_mm_loadu_ps(matrix.m + 8), _mm_set1_ps(direction.z)));
        alignas(16) float32 values[4];
        _mm_store_ps(values, result);
        return { values[0], values[1], values[2] };
#else
        return
        {
            matrix.m[0] * direction.x + matrix.m[4] * direction.y + matrix.m[8] * direction.z,
            matrix.m[1] * direction.x + matrix.m[5] * direction.y + matrix.m[9] * direction.z,
            matrix.m[2] * direction.x + matrix.m[6] * direction.y + matrix.m[10] * direction.z,
        };
#endif
    }

    vector3 Math::GetTranslation(const matrix4x4& matrix)
    {
        return { matrix.m[12], matrix.m[13], matrix.m[14] };
    }

    bounds3 Math::CalculateBounds(const List<vector3>& points)
    {
        return CalculateBounds(points, false);
    }

    bounds3 Math::CalculateBounds(const List<vector3>& points, bool startAtFirstPoint)
    {
        if (points.empty()) return bounds3();

        vector3 minValue =
        {
            std::numeric_limits<float32>::max(),
            std::numeric_limits<float32>::max(),
            std::numeric_limits<float32>::max(),
        };
        vector3 maxValue =
        {
            std::numeric_limits<float32>::lowest(),
            std::numeric_limits<float32>::lowest(),
            std::numeric_limits<float32>::lowest(),
        };

        if (startAtFirstPoint)
        {
            minValue = points.front();
            maxValue = points.front();
        }
        usize first = startAtFirstPoint ? 1 : 0;

#if ORBEDEN_MATH_SSE2
        //并行归约三个坐标，保留旧值应对相等或无序比较
        __m128 minimum = _mm_setr_ps(minValue.x, minValue.y, minValue.z, 0.0f);
        __m128 maximum = _mm_setr_ps(maxValue.x, maxValue.y, maxValue.z, 0.0f);
        for (usize index = first; index < points.size(); ++index)
        {
            const vector3& point = points[index];
            const __m128 values = _mm_setr_ps(point.x, point.y, point.z, 0.0f);
            minimum = _mm_min_ps(values, minimum);
            maximum = _mm_max_ps(values, maximum);
        }
        alignas(16) float32 minimumLanes[4], maximumLanes[4];
        _mm_store_ps(minimumLanes, minimum);
        _mm_store_ps(maximumLanes, maximum);
        minValue = { minimumLanes[0], minimumLanes[1], minimumLanes[2] };
        maxValue = { maximumLanes[0], maximumLanes[1], maximumLanes[2] };
#else
        for (usize index = first; index < points.size(); ++index)
        {
            const vector3& point = points[index];
            minValue.x = std::min(minValue.x, point.x);
            minValue.y = std::min(minValue.y, point.y);
            minValue.z = std::min(minValue.z, point.z);
            maxValue.x = std::max(maxValue.x, point.x);
            maxValue.y = std::max(maxValue.y, point.y);
            maxValue.z = std::max(maxValue.z, point.z);
        }
#endif

        bounds3 result;
        result.center = ScaleVector(AddVector(minValue, maxValue), 0.5f);
        result.extents = ScaleVector(SubVector(maxValue, minValue), 0.5f);
        result.valid = true;
        return result;
    }

    bounds3 Math::TransformBounds(const matrix4x4& matrix, const bounds3& bounds)
    {
        if (!bounds.valid) return bounds3();

        bounds3 result;
        result.center = TransformPoint(matrix, bounds.center);
#if ORBEDEN_MATH_SSE2
        //按矩阵列并行计算三个包围盒半尺寸
        const __m128 magnitudeMask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
        __m128 extents = _mm_mul_ps(_mm_and_ps(_mm_loadu_ps(matrix.m), magnitudeMask), _mm_set1_ps(bounds.extents.x));
        extents = _mm_add_ps(extents, _mm_mul_ps(_mm_and_ps(_mm_loadu_ps(matrix.m + 4), magnitudeMask), _mm_set1_ps(bounds.extents.y)));
        extents = _mm_add_ps(extents, _mm_mul_ps(_mm_and_ps(_mm_loadu_ps(matrix.m + 8), magnitudeMask), _mm_set1_ps(bounds.extents.z)));
        alignas(16) float32 lanes[4];
        _mm_store_ps(lanes, extents);
        result.extents = { lanes[0], lanes[1], lanes[2] };
#else
        result.extents.x = std::abs(matrix.m[0]) * bounds.extents.x + std::abs(matrix.m[4]) * bounds.extents.y + std::abs(matrix.m[8]) * bounds.extents.z;
        result.extents.y = std::abs(matrix.m[1]) * bounds.extents.x + std::abs(matrix.m[5]) * bounds.extents.y + std::abs(matrix.m[9]) * bounds.extents.z;
        result.extents.z = std::abs(matrix.m[2]) * bounds.extents.x + std::abs(matrix.m[6]) * bounds.extents.y + std::abs(matrix.m[10]) * bounds.extents.z;
#endif
        result.valid = true;
        return result;
    }

    frustum Math::BuildFrustum(const matrix4x4& viewProjection)
    {
        const float32* m = viewProjection.m;
        frustum result;
        result.planes[0] = NormalizePlane({ { m[3] + m[0], m[7] + m[4], m[11] + m[8] }, m[15] + m[12] });
        result.planes[1] = NormalizePlane({ { m[3] - m[0], m[7] - m[4], m[11] - m[8] }, m[15] - m[12] });
        result.planes[2] = NormalizePlane({ { m[3] + m[1], m[7] + m[5], m[11] + m[9] }, m[15] + m[13] });
        result.planes[3] = NormalizePlane({ { m[3] - m[1], m[7] - m[5], m[11] - m[9] }, m[15] - m[13] });
        result.planes[4] = NormalizePlane({ { m[3] + m[2], m[7] + m[6], m[11] + m[10] }, m[15] + m[14] });
        result.planes[5] = NormalizePlane({ { m[3] - m[2], m[7] - m[6], m[11] - m[10] }, m[15] - m[14] });
        return result;
    }

    bool Math::Intersects(const frustum& viewFrustum, const bounds3& bounds)
    {
        if (!bounds.valid) return false;

        for (const plane3& plane : viewFrustum.planes)
        {
            float32 signedDistance = Dot(plane.normal, bounds.center) + plane.distance;
            float32 radius =
                std::abs(plane.normal.x) * bounds.extents.x +
                std::abs(plane.normal.y) * bounds.extents.y +
                std::abs(plane.normal.z) * bounds.extents.z;

            if (signedDistance + radius < 0.0f) return false;
        }

        return true;
    }

    uint32 Math::IntersectsFour(const frustum& viewFrustum, const bounds3* const (&bounds)[4])
    {
        uint32 visible = 0;
#if ORBEDEN_MATH_SSE2
        //装载四个包围盒的中心和半尺寸
        for (uint32 index = 0; index < 4; ++index)
        {
            if (bounds[index]->valid)
            {
                visible |= 1u << index;
            }
        }
        const __m128 cx = _mm_setr_ps(bounds[0]->center.x, bounds[1]->center.x, bounds[2]->center.x, bounds[3]->center.x);
        const __m128 cy = _mm_setr_ps(bounds[0]->center.y, bounds[1]->center.y, bounds[2]->center.y, bounds[3]->center.y);
        const __m128 cz = _mm_setr_ps(bounds[0]->center.z, bounds[1]->center.z, bounds[2]->center.z, bounds[3]->center.z);
        const __m128 ex = _mm_setr_ps(bounds[0]->extents.x, bounds[1]->extents.x, bounds[2]->extents.x, bounds[3]->extents.x);
        const __m128 ey = _mm_setr_ps(bounds[0]->extents.y, bounds[1]->extents.y, bounds[2]->extents.y, bounds[3]->extents.y);
        const __m128 ez = _mm_setr_ps(bounds[0]->extents.z, bounds[1]->extents.z, bounds[2]->extents.z, bounds[3]->extents.z);

        //对四个包围盒使用同一平面并排除外部包围盒
        for (const plane3& plane : viewFrustum.planes)
        {
            __m128 distance = _mm_mul_ps(_mm_set1_ps(plane.normal.x), cx);
            distance = _mm_add_ps(distance, _mm_mul_ps(_mm_set1_ps(plane.normal.y), cy));
            distance = _mm_add_ps(distance, _mm_mul_ps(_mm_set1_ps(plane.normal.z), cz));
            distance = _mm_add_ps(distance, _mm_set1_ps(plane.distance));

            __m128 radius = _mm_mul_ps(_mm_set1_ps(std::abs(plane.normal.x)), ex);
            radius = _mm_add_ps(radius, _mm_mul_ps(_mm_set1_ps(std::abs(plane.normal.y)), ey));
            radius = _mm_add_ps(radius, _mm_mul_ps(_mm_set1_ps(std::abs(plane.normal.z)), ez));
            const __m128 outside = _mm_cmplt_ps(_mm_add_ps(distance, radius), _mm_setzero_ps());
            visible &= ~static_cast<uint32>(_mm_movemask_ps(outside));
            if (visible == 0)
            {
                break;
            }
        }
#else
        for (uint32 index = 0; index < 4; ++index)
        {
            if (Intersects(viewFrustum, *bounds[index]))
            {
                visible |= 1u << index;
            }
        }
#endif
        return visible;
    }
}
