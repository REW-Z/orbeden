#include "Rendering/RenderMath.h"

#include <algorithm>
#include <cmath>
#include <limits>

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
        return std::sqrt(RenderMath::Dot(value, value));
    }

    plane3 NormalizePlane(const plane3& plane)
    {
        float32 length = Length(plane.normal);
        if (length <= 0.000001f) return plane3();

        float32 invLength = 1.0f / length;
        return { ScaleVector(plane.normal, invLength), plane.distance * invLength };
    }
}

namespace RenderMath
{
    float32 Dot(const vector3& a, const vector3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    vector3 Cross(const vector3& a, const vector3& b)
    {
        return
        {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x,
        };
    }

    vector3 Normalize(const vector3& value)
    {
        float32 length = Length(value);
        if (length <= 0.000001f) return vector3();

        return ScaleVector(value, 1.0f / length);
    }

    matrix4x4 Mul(const matrix4x4& a, const matrix4x4& b)
    {
        matrix4x4 result;
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

        return result;
    }

    matrix4x4 Translation(const vector3& value)
    {
        matrix4x4 result;
        result.m[12] = value.x;
        result.m[13] = value.y;
        result.m[14] = value.z;
        return result;
    }

    matrix4x4 Rotation(const quaternion& value)
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

        float32 xx = q.x * q.x;
        float32 yy = q.y * q.y;
        float32 zz = q.z * q.z;
        float32 xy = q.x * q.y;
        float32 xz = q.x * q.z;
        float32 yz = q.y * q.z;
        float32 wx = q.w * q.x;
        float32 wy = q.w * q.y;
        float32 wz = q.w * q.z;

        matrix4x4 result;
        result.m[0] = 1.0f - 2.0f * (yy + zz);
        result.m[1] = 2.0f * (xy + wz);
        result.m[2] = 2.0f * (xz - wy);

        result.m[4] = 2.0f * (xy - wz);
        result.m[5] = 1.0f - 2.0f * (xx + zz);
        result.m[6] = 2.0f * (yz + wx);

        result.m[8] = 2.0f * (xz + wy);
        result.m[9] = 2.0f * (yz - wx);
        result.m[10] = 1.0f - 2.0f * (xx + yy);
        return result;
    }

    matrix4x4 Scale(const vector3& value)
    {
        matrix4x4 result;
        result.m[0] = value.x;
        result.m[5] = value.y;
        result.m[10] = value.z;
        return result;
    }

    matrix4x4 TRS(const vector3& position, const quaternion& rotation, const vector3& scale)
    {
        return Mul(Mul(Translation(position), Rotation(rotation)), Scale(scale));
    }

    matrix4x4 Perspective(float32 fieldOfView, float32 aspect, float32 nearPlane, float32 farPlane)
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

    matrix4x4 Orthographic(float32 left, float32 right, float32 bottom, float32 top, float32 nearPlane, float32 farPlane)
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

    matrix4x4 LookAt(const vector3& eye, const vector3& target, const vector3& up)
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

    matrix4x4 Inverse(const matrix4x4& value)
    {
        const float32* m = value.m;
        matrix4x4 result;
        float32 inv[16];

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

        float32 determinant = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        if (std::abs(determinant) <= 0.000001f) return matrix4x4();

        determinant = 1.0f / determinant;
        for (int index = 0; index < 16; ++index)
        {
            result.m[index] = inv[index] * determinant;
        }

        return result;
    }

    vector3 TransformPoint(const matrix4x4& matrix, const vector3& point)
    {
        float32 x = matrix.m[0] * point.x + matrix.m[4] * point.y + matrix.m[8] * point.z + matrix.m[12];
        float32 y = matrix.m[1] * point.x + matrix.m[5] * point.y + matrix.m[9] * point.z + matrix.m[13];
        float32 z = matrix.m[2] * point.x + matrix.m[6] * point.y + matrix.m[10] * point.z + matrix.m[14];
        float32 w = matrix.m[3] * point.x + matrix.m[7] * point.y + matrix.m[11] * point.z + matrix.m[15];
        if (std::abs(w) > 0.000001f && std::abs(w - 1.0f) > 0.000001f)
        {
            float32 invW = 1.0f / w;
            x *= invW;
            y *= invW;
            z *= invW;
        }

        return { x, y, z };
    }

    vector3 TransformDirection(const matrix4x4& matrix, const vector3& direction)
    {
        return
        {
            matrix.m[0] * direction.x + matrix.m[4] * direction.y + matrix.m[8] * direction.z,
            matrix.m[1] * direction.x + matrix.m[5] * direction.y + matrix.m[9] * direction.z,
            matrix.m[2] * direction.x + matrix.m[6] * direction.y + matrix.m[10] * direction.z,
        };
    }

    vector3 GetTranslation(const matrix4x4& matrix)
    {
        return { matrix.m[12], matrix.m[13], matrix.m[14] };
    }

    bounds3 CalculateBounds(const List<vector3>& points)
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

        for (const vector3& point : points)
        {
            minValue.x = std::min(minValue.x, point.x);
            minValue.y = std::min(minValue.y, point.y);
            minValue.z = std::min(minValue.z, point.z);
            maxValue.x = std::max(maxValue.x, point.x);
            maxValue.y = std::max(maxValue.y, point.y);
            maxValue.z = std::max(maxValue.z, point.z);
        }

        bounds3 result;
        result.center = ScaleVector(AddVector(minValue, maxValue), 0.5f);
        result.extents = ScaleVector(SubVector(maxValue, minValue), 0.5f);
        result.valid = true;
        return result;
    }

    bounds3 TransformBounds(const matrix4x4& matrix, const bounds3& bounds)
    {
        if (!bounds.valid) return bounds3();

        bounds3 result;
        result.center = TransformPoint(matrix, bounds.center);
        result.extents.x = std::abs(matrix.m[0]) * bounds.extents.x + std::abs(matrix.m[4]) * bounds.extents.y + std::abs(matrix.m[8]) * bounds.extents.z;
        result.extents.y = std::abs(matrix.m[1]) * bounds.extents.x + std::abs(matrix.m[5]) * bounds.extents.y + std::abs(matrix.m[9]) * bounds.extents.z;
        result.extents.z = std::abs(matrix.m[2]) * bounds.extents.x + std::abs(matrix.m[6]) * bounds.extents.y + std::abs(matrix.m[10]) * bounds.extents.z;
        result.valid = true;
        return result;
    }

    frustum BuildFrustum(const matrix4x4& viewProjection)
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

    bool Intersects(const frustum& viewFrustum, const bounds3& bounds)
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
}
