using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 可复用的网格装配器。一次图形重建复用一个实例：Clear 之后按顺序添加顶点与三角形，
/// SetTexture 切分片段，最后 Complete 收尾。顶点与索引直接按提交布局排列，不再中途转换。
/// </summary>
public sealed class UIMeshBuilder
{
    private readonly List<UIVertex> vertices = [];
    private readonly List<uint> indices = [];
    private readonly List<UIMeshFragment> fragments = [];

    private UIDrawState currentState = UIDrawState.Default;
    private int fragmentFirstIndex;

    /// <summary>当前累计的顶点。</summary>
    public IReadOnlyList<UIVertex> Vertices => vertices;

    /// <summary>当前累计的索引，uint32。</summary>
    public IReadOnlyList<uint> Indices => indices;

    /// <summary>当前累计的片段；空片段不会被收录。</summary>
    public IReadOnlyList<UIMeshFragment> Fragments => fragments;

    /// <summary>
    /// 字形资源的光栅缩放；屏幕与离屏画布取画布缩放，世界空间固定为 1，不读取摄像机。
    /// </summary>
    public float ViewScale { get; internal set; } = 1.0f;

    /// <summary>清空全部内容并回到默认绘制状态，容量保留供下一次复用。</summary>
    public void Clear()
    {
        vertices.Clear();
        indices.Clear();
        fragments.Clear();
        currentState = UIDrawState.Default;
        fragmentFirstIndex = 0;
    }

    /// <summary>读取一个顶点；下标越界抛错。</summary>
    public UIVertex GetVertex(int index) => vertices[index];

    /// <summary>改写一个顶点；下标越界抛错。位置、UV 与顶点色都可以改。</summary>
    public void SetVertex(int index, in UIVertex value) => vertices[index] = value;

    /// <summary>添加一个顶点并返回它的下标。</summary>
    public int AddVertex(vector3 position, vector2 uv, color tint)
    {
        vertices.Add(new UIVertex { position = position, uv = uv, tint = tint });
        return vertices.Count - 1;
    }

    /// <summary>添加一个三角形；下标越界直接报错，不写入半个三角形。</summary>
    public void AddTriangle(int a, int b, int c)
    {
        int count = vertices.Count;
        if ((uint)a >= (uint)count || (uint)b >= (uint)count || (uint)c >= (uint)count)
            throw new ArgumentOutOfRangeException(nameof(a), "UI 三角形引用了不存在的顶点。");
        indices.Add((uint)a);
        indices.Add((uint)b);
        indices.Add((uint)c);
    }

    /// <summary>按矩形与 UV 范围添加一个四边形；从 +Z 观察为逆时针。</summary>
    public void AddQuad(UIRect rect, vector2 uvMin, vector2 uvMax, color tint)
    {
        int start = vertices.Count;
        vector2 max = rect.Max;
        AddVertex(new vector3(rect.min.x, rect.min.y, 0.0f), new vector2(uvMin.x, uvMin.y), tint);
        AddVertex(new vector3(max.x, rect.min.y, 0.0f), new vector2(uvMax.x, uvMin.y), tint);
        AddVertex(new vector3(max.x, max.y, 0.0f), new vector2(uvMax.x, uvMax.y), tint);
        AddVertex(new vector3(rect.min.x, max.y, 0.0f), new vector2(uvMin.x, uvMax.y), tint);
        AddTriangle(start, start + 1, start + 2);
        AddTriangle(start, start + 2, start + 3);
    }

    /// <summary>按像素行方向将 UI 的向上 UV 转换为纹理采样 UV。</summary>
    public static vector2 ToTextureUv(vector2 uv, bool flipY) => new(uv.x, flipY ? 1.0f - uv.y : uv.y);

    /// <summary>
    /// 切换绘制状态：结束当前片段并开始新片段。中间没有几何的空片段不会输出，
    /// 因此同一图形可以按纹理多次切换片段而不产生空批次。
    /// </summary>
    public void SetTexture(Texture2D? texture, UIMaterialKind kind)
    {
        CloseFragment();
        currentState.texture = texture;
        currentState.materialKind = kind;
    }

    /// <summary>整状态切换绘制状态；UIVisual.ModifyDrawState 的结果经这里进入片段。</summary>
    public void SetDrawState(in UIDrawState state)
    {
        CloseFragment();
        currentState = state;
    }

    /// <summary>设置当前片段的距离场范围；只对距离场材质有意义。</summary>
    public void SetDistanceRange(float value)
    {
        if (!float.IsFinite(value) || value <= 0.0f) return;
        currentState.distanceRange = value;
    }

    /// <summary>收尾：把最后一个片段提交出去。每次重建结束必须调用一次。</summary>
    public void Complete() => CloseFragment();

    /// <summary>三角形的条数。</summary>
    public int TriangleCount => indices.Count / 3;

    /// <summary>按序号读取三角形；越界返回假。</summary>
    public bool TryGetTriangle(int triangle, out int a, out int b, out int c)
    {
        a = 0;
        b = 0;
        c = 0;
        int first = triangle * 3;
        if (triangle < 0 || first + 2 >= indices.Count) return false;
        a = (int)indices[first];
        b = (int)indices[first + 1];
        c = (int)indices[first + 2];
        return true;
    }

    /// <summary>取某个三角形所在片段的绘制状态；越界时返回默认状态。</summary>
    public UIDrawState GetTriangleState(int triangle)
    {
        int index = triangle * 3;
        foreach (UIMeshFragment fragment in fragments)
        {
            if (index < fragment.firstIndex || index >= fragment.firstIndex + fragment.indexCount) continue;
            return fragment.state;
        }
        return UIDrawState.Default;
    }

    /// <summary>
    /// 清空索引与片段并回到默认状态，保留顶点：修改器据此重建三角流。
    /// 重建时先 SetDrawState 给出首段状态，之后每换一段再设一次，片段顺序与状态才不会丢。
    /// </summary>
    public void ClearTriangles()
    {
        indices.Clear();
        fragments.Clear();
        currentState = UIDrawState.Default;
        fragmentFirstIndex = 0;
    }

    //把 [fragmentFirstIndex, indices.Count) 之间的索引发成一个片段；空区间直接跳过。
    private void CloseFragment()
    {
        int count = indices.Count - fragmentFirstIndex;
        if (count > 0)
        {
            fragments.Add(new UIMeshFragment(fragmentFirstIndex, count, currentState));
            fragmentFirstIndex = indices.Count;
        }
    }
}
