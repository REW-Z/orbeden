using System;

namespace Orbeden;

/// <summary>
/// UI 的矩阵运算。节点矩阵的构造规则只在这里实现一次，帧构建与裁剪栈共用，
/// 避免同一条链在两处算出不同的结果。
/// </summary>
internal static class UIMatrix
{
    /// <summary>节点到画布根的局部矩阵连乘；画布根本身不参与投影。</summary>
    internal static matrix4x4 Compose(UINode node)
    {
        matrix4x4 result = Local(node);
        for (UINode? current = node.Parent; current != null; current = current.Parent)
        {
            if (current.Canvas != null) break;
            result = matrix4x4.Multiply(Local(current), result);
        }
        return result;
    }

    /// <summary>节点到世界空间的矩阵：连画布节点自己的变换一起乘进去，世界空间命中要用它。</summary>
    internal static matrix4x4 ComposeIncludingCanvas(UINode node)
    {
        matrix4x4 result = Local(node);
        UINode root = node;
        for (UINode? current = node.Parent; current != null; current = current.Parent)
        {
            result = matrix4x4.Multiply(Local(current), result);
            root = current;
        }
        //叠加画布外部父级变换
        EnsId parent = Ens.FromId(root.Ens).Transform.GetParent();
        while (!parent.IsNull)
        {
            Transform transform = Ens.FromId(parent).Transform;
            matrix4x4 local = matrix4x4.Trs(transform.GetLocalPosition(), transform.GetLocalRotation(), transform.GetLocalScale());
            result = matrix4x4.Multiply(local, result);
            parent = transform.GetParent();
        }
        return result;
    }

    /// <summary>使用布局合成位置，旋转与缩放取作者值。</summary>
    internal static matrix4x4 Local(UINode node)
    {
        Transform transform = Ens.FromId(node.Ens).Transform;
        vector3 position = node.Layout?.GetResolvedLocalPosition() ?? transform.GetLocalPosition();
        return matrix4x4.Trs(position, transform.GetLocalRotation(), transform.GetLocalScale());
    }

    /// <summary>
    /// 构造屏幕画布投影，把 XY 映射到 [-1,1]，最终裁剪深度固定为零。
    /// </summary>
    internal static matrix4x4 Ortho(float left, float right, float bottom, float top)
    {
        matrix4x4 result = matrix4x4.Identity;
        float width = right - left;
        float height = top - bottom;
        if (width == 0.0f || height == 0.0f) return result;

        result[0] = 2.0f / width;
        result[5] = 2.0f / height;
        result[10] = 0.0f;
        result[12] = -(right + left) / width;
        result[13] = -(top + bottom) / height;
        return result;
    }

    /// <summary>
    /// 通用 4×4 求逆，按列主序；奇异时返回假。
    /// 投影矩阵不是仿射矩阵（最后一行带着 -1/w），反投影必须用它而不是 TryInvertAffine。
    /// </summary>
    internal static bool TryInvert(in matrix4x4 matrix, out matrix4x4 inverse)
    {
        inverse = matrix4x4.Identity;
        //按行主序的中间副本做余子式展开，写回时再转回列主序。
        Span<float> a = stackalloc float[16];
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row) a[row * 4 + column] = matrix[column * 4 + row];
        }

        Span<float> inv = stackalloc float[16];
        inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15]
            + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
        inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15]
            - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
        inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15]
            + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
        inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14]
            - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
        inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15]
            - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
        inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15]
            + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
        inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15]
            - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
        inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14]
            + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
        inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15]
            + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
        inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15]
            - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
        inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15]
            + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
        inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14]
            - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
        inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11]
            - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
        inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11]
            + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
        inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11]
            - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
        inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10]
            + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

        float determinant = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
        if (!float.IsFinite(determinant) || MathF.Abs(determinant) < 1e-12f) return false;

        float scale = 1.0f / determinant;
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row) inverse[column * 4 + row] = inv[row * 4 + column] * scale;
        }
        return true;
    }

    /// <summary>按通用矩阵变换齐次点并做 w 归一；w 接近零时返回假。</summary>
    internal static bool TryTransformHomogeneous(in matrix4x4 matrix, in vector3 point, out vector3 result)
    {
        float x = matrix[0] * point.x + matrix[4] * point.y + matrix[8] * point.z + matrix[12];
        float y = matrix[1] * point.x + matrix[5] * point.y + matrix[9] * point.z + matrix[13];
        float z = matrix[2] * point.x + matrix[6] * point.y + matrix[10] * point.z + matrix[14];
        float w = matrix[3] * point.x + matrix[7] * point.y + matrix[11] * point.z + matrix[15];
        if (!float.IsFinite(w) || MathF.Abs(w) < 1e-8f)
        {
            result = default;
            return false;
        }
        result = new vector3(x / w, y / w, z / w);
        return true;
    }

    /// <summary>把一个点按列主序矩阵变换到目标空间；z 按零处理。</summary>
    internal static vector2 TransformPoint(in matrix4x4 matrix, vector2 point) => new(
        matrix[0] * point.x + matrix[4] * point.y + matrix[12],
        matrix[1] * point.x + matrix[5] * point.y + matrix[13]);

    /// <summary>
    /// 求仿射矩阵的逆；平移与线性部分分开处理。线性部分奇异时返回假，
    /// 调用方按不可见、不可命中处理。
    /// </summary>
    internal static bool TryInvertAffine(in matrix4x4 matrix, out matrix4x4 inverse)
    {
        inverse = matrix4x4.Identity;
        //列主序 3×3：列向量是 m[0..2]、m[4..6]、m[8..10]。
        float a = matrix[0], b = matrix[1], c = matrix[2];
        float d = matrix[4], e = matrix[5], f = matrix[6];
        float g = matrix[8], h = matrix[9], i = matrix[10];

        float determinant = a * (e * i - f * h) - d * (b * i - c * h) + g * (b * f - c * e);
        if (!float.IsFinite(determinant) || MathF.Abs(determinant) < 1e-12f) return false;

        float inv = 1.0f / determinant;
        //伴随矩阵转置后直接落到列主序的位置上。
        inverse[0] = (e * i - f * h) * inv;
        inverse[1] = (c * h - b * i) * inv;
        inverse[2] = (b * f - c * e) * inv;
        inverse[4] = (f * g - d * i) * inv;
        inverse[5] = (a * i - c * g) * inv;
        inverse[6] = (c * d - a * f) * inv;
        inverse[8] = (d * h - e * g) * inv;
        inverse[9] = (b * g - a * h) * inv;
        inverse[10] = (a * e - b * d) * inv;

        float tx = matrix[12], ty = matrix[13], tz = matrix[14];
        inverse[12] = -(inverse[0] * tx + inverse[4] * ty + inverse[8] * tz);
        inverse[13] = -(inverse[1] * tx + inverse[5] * ty + inverse[9] * tz);
        inverse[14] = -(inverse[2] * tx + inverse[6] * ty + inverse[10] * tz);
        return true;
    }
}
