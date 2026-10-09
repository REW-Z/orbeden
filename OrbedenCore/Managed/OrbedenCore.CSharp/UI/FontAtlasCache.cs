using System;
using System.Collections.Generic;

namespace Orbeden;

/// <summary>
/// 字形缓存键。距离场模式的光栅尺寸固定，bitmapPixelSize 记 0；
/// 位图模式记投影像号，因此同一字形在不同字号下是不同条目。
/// </summary>
public readonly struct UIGlyphKey : IEquatable<UIGlyphKey>
{
    /// <summary>字体资源运行时 ID。</summary>
    public readonly int fontObjectId;

    /// <summary>字体内容版本；重新导入后旧条目自然失效。</summary>
    public readonly ulong fontRevision;

    /// <summary>字形下标。</summary>
    public readonly uint glyphIndex;

    /// <summary>光栅化模式。</summary>
    public readonly FontRasterMode rasterMode;

    /// <summary>位图模式的投影像号；距离场模式为 0。</summary>
    public readonly int bitmapPixelSize;

    /// <summary>创建字形缓存键。</summary>
    public UIGlyphKey(int fontObjectId, ulong fontRevision, uint glyphIndex,
        FontRasterMode rasterMode, int bitmapPixelSize)
    {
        this.fontObjectId = fontObjectId;
        this.fontRevision = fontRevision;
        this.glyphIndex = glyphIndex;
        this.rasterMode = rasterMode;
        this.bitmapPixelSize = bitmapPixelSize;
    }

    /// <summary>按值比较。</summary>
    public bool Equals(UIGlyphKey other) =>
        fontObjectId == other.fontObjectId
        && fontRevision == other.fontRevision
        && glyphIndex == other.glyphIndex
        && rasterMode == other.rasterMode
        && bitmapPixelSize == other.bitmapPixelSize;

    /// <summary>按值比较。</summary>
    public override bool Equals(object? other) => other is UIGlyphKey key && Equals(key);

    /// <summary>按值散列。</summary>
    public override int GetHashCode() =>
        HashCode.Combine(fontObjectId, fontRevision, glyphIndex, (uint)rasterMode, bitmapPixelSize);
}

/// <summary>一次字形请求：字体、标量、光栅化模式与投影像号。</summary>
public readonly struct UIGlyphDemand
{
    /// <summary>字体资源；空表示缺字，直接走统一方框。</summary>
    public readonly Font? font;

    /// <summary>Unicode 标量。</summary>
    public readonly uint scalar;

    /// <summary>光栅化模式。</summary>
    public readonly FontRasterMode rasterMode;

    /// <summary>位图模式的投影像号；距离场模式忽略。</summary>
    public readonly int pixelSize;

    /// <summary>创建一次字形请求。</summary>
    public UIGlyphDemand(Font? font, uint scalar, FontRasterMode rasterMode, int pixelSize)
    {
        this.font = font;
        this.scalar = scalar;
        this.rasterMode = rasterMode;
        this.pixelSize = pixelSize;
    }
}

/// <summary>
/// 一个已解析的字形条目。度量使用未 hint 的字体单位，按 fontSize/unitsPerEm 换算到逻辑像素；
/// 位图位置使用光栅 texel，按 fontSize/pixelSize 换算。缺字条目用同一套字段承载方框数据。
/// </summary>
public sealed class UIGlyphEntry
{
    internal UIGlyphEntry(UIGlyphKey key, uint glyphIndex, bool missing, int unitsPerEm)
    {
        Key = key;
        GlyphIndex = glyphIndex;
        IsMissing = missing;
        UnitsPerEm = unitsPerEm;
    }

    /// <summary>缺字条目：字形下标为 0，度量按 em 折算到固定的 1000 单位。</summary>
    internal static UIGlyphEntry CreateMissing(bool needsRetry = false) => new(default, 0, true, FontAtlasCache.MissingUnitsPerEm)
    {
        NeedsRetry = needsRetry,
        Advance = FontAtlasCache.MissingAdvanceEm * FontAtlasCache.MissingUnitsPerEm,
        Width = FontAtlasCache.MissingBoxWidthEm * FontAtlasCache.MissingUnitsPerEm,
        Height = FontAtlasCache.MissingBoxHeightEm * FontAtlasCache.MissingUnitsPerEm,
    };

    /// <summary>缓存键。</summary>
    public UIGlyphKey Key { get; }

    /// <summary>字形下标；缺字为 0。</summary>
    public uint GlyphIndex { get; }

    /// <summary>是否缺字。缺字时几何由控件画四条矩形组成的空心方框。</summary>
    public bool IsMissing { get; }

    /// <summary>字形资源暂时不可用，下次准备渲染时重试。</summary>
    public bool NeedsRetry { get; internal set; }

    /// <summary>度量单位；换算逻辑像素时用 fontSize/UnitsPerEm。</summary>
    public int UnitsPerEm { get; }

    /// <summary>水平步进，字体单位。</summary>
    public float Advance { get; internal set; }

    /// <summary>位图左下角相对字形原点的水平偏移，字体单位。</summary>
    public float BearingX { get; internal set; }

    /// <summary>位图左下角相对字形原点的垂直偏移，字体单位。</summary>
    public float BearingY { get; internal set; }

    /// <summary>字形包围盒宽度，字体单位。</summary>
    public float Width { get; internal set; }

    /// <summary>字形包围盒高度，字体单位。</summary>
    public float Height { get; internal set; }

    /// <summary>光栅 texel 密度，texel per em：距离场为 64，位图为投影像号。</summary>
    public int PixelSize { get; internal set; }

    /// <summary>位图宽度，texel；0 表示没有像素（空格或空轮廓）。</summary>
    public int BitmapWidth { get; internal set; }

    /// <summary>位图高度，texel。</summary>
    public int BitmapHeight { get; internal set; }

    /// <summary>位图左下角相对字形原点的水平偏移，texel。</summary>
    public float OriginX { get; internal set; }

    /// <summary>位图左下角相对字形原点的垂直偏移，texel。</summary>
    public float OriginY { get; internal set; }

    /// <summary>字形在页内的 UV 范围；v 轴朝上，与 UI 网格约定一致。</summary>
    public UIRect Uv { get; internal set; }

    //所在 Atlas 页；没有像素时为空。页被回收时条目一并移除，不会留下悬空引用。
    internal UIFontAtlasPage? page;

    /// <summary>所在 Atlas 页的纹理；没有像素时为空。</summary>
    public Texture2D? Texture => page?.texture;

    /// <summary>所在 Atlas 页的代次；页被回收重用后推进，几何据此失效。</summary>
    public uint PageGeneration => page?.texture is { IsAlive: true } ? page.generation : 0;

    /// <summary>是否有可绘制的像素。空格没有像素，只贡献步进；页只在纹理创建成功后才会存在。</summary>
    public bool HasPixels => page?.texture is { IsAlive: true } && BitmapWidth > 0 && BitmapHeight > 0;

    /// <summary>按字号求水平步进，逻辑像素。</summary>
    public float GetAdvance(float fontSize) => Advance * (fontSize / UnitsPerEm);

    /// <summary>按字号求字形包围盒，逻辑像素。</summary>
    public vector2 GetBoxSize(float fontSize) =>
        new(Width * (fontSize / UnitsPerEm), Height * (fontSize / UnitsPerEm));

    /// <summary>求位图相对笔位的目标矩形，逻辑像素；没有像素时返回假。</summary>
    public bool TryGetTargetRect(vector2 penOrigin, float fontSize, out UIRect rect)
    {
        rect = default;
        if (!HasPixels || PixelSize <= 0) return false;
        float texelToLogical = fontSize / PixelSize;
        rect = new UIRect(
            new vector2(penOrigin.x + OriginX * texelToLogical, penOrigin.y + OriginY * texelToLogical),
            new vector2(BitmapWidth * texelToLogical, BitmapHeight * texelToLogical));
        return true;
    }

    /// <summary>取位图所在的纹理与 UV；没有像素时返回假，纹理为空。</summary>
    public bool TryGetTextureRegion(out Texture2D? texture, out UIRect uv, out uint generation)
    {
        texture = page?.texture;
        uv = Uv;
        generation = page?.generation ?? 0;
        return HasPixels;
    }
}

/// <summary>一页 Atlas。普通页逐行装箱；超大字形独占一个能容纳它的二次幂页。</summary>
internal sealed class UIFontAtlasPage
{
    internal int pageId;
    internal uint generation;
    internal Texture2D? texture;
    internal int width;
    internal int height;
    internal int channels;
    internal int fontObjectId;
    internal ulong fontRevision;
    internal long bytes;
    //写入游标：逐行装箱的位置。
    internal UIFontAtlasCursor cursor;
    internal ulong lastUsedFrame;
    internal ulong pinnedFrame;
    //独占页只放一个字形，不再参与逐行装箱。
    internal bool dedicated;
    //预烘焙页的来源索引；回收时解除运行时纹理引用。
    internal UIFontPrebakedAtlas? prebakedAtlas;
    internal int prebakedPageIndex = -1;
}

/// <summary>
/// 预烘焙与动态补字 Atlas。装箱、上传与回收由本类负责，排版在 UITextLayout。
/// 缓存实例按进程共享，纹理页与字形状态按当前世界维护；上下文切换时清理并重建。
/// 页纹理由本类持有强引用，原生 World 销毁时仍会失效。
/// </summary>
public sealed class FontAtlasCache : IDisposable, IUITextMetrics
{
    /// <summary>字体未指定图集参数时的默认普通页边长。</summary>
    public const int PageSize = 1024;

    /// <summary>Atlas 字节软预算：超出后按 LRU 回收未被本帧固定的页。</summary>
    public const long ByteBudget = 64L * 1024 * 1024;

    /// <summary>默认距离场像素密度，texel per em。</summary>
    public const int DistanceFieldPixelSize = 64;

    /// <summary>默认距离场取值范围，单位 texel。</summary>
    public const float DistanceFieldRange = 4.0f;

    /// <summary>位图投影字号的下限。</summary>
    public const int MinBitmapPixelSize = 1;

    /// <summary>位图投影字号的上限。</summary>
    public const int MaxBitmapPixelSize = 512;

    /// <summary>缺字方框的线宽，em。</summary>
    public const float MissingStrokeEm = 0.05f;

    //缺字方框的几何，单位 em。方框不依赖字体，固定以 1em=1000 单位表达。
    internal const int MissingUnitsPerEm = 1000;
    internal const float MissingAdvanceEm = 0.6f;
    internal const float MissingBoxWidthEm = 0.5f;
    internal const float MissingBoxHeightEm = 0.8f;

    //设备纹理尺寸未知时的保守回退值，保证超大字形不会去申请不可能存在的纹理。
    private const int FallbackMaximumSize = 2048;

    private static FontAtlasCache? shared;
    private static ulong sharedContext;

    /// <summary>进程共享的字形缓存。</summary>
    public static FontAtlasCache Shared => shared ??= new FontAtlasCache { Context = sharedContext };

    private readonly Dictionary<UIGlyphKey, UIGlyphEntry> glyphs = [];
    //标量到字形下标的解析结果；0 表示该标量在字体里没有字形。
    private readonly Dictionary<ScalarKey, uint> scalarGlyphs = [];
    private readonly List<UIFontAtlasPage> pages = [];
    private readonly Dictionary<(int fontId, ulong revision), UIFontPrebakedAtlas?> prebakedAtlases = [];
    //已回收但还要跨帧保活的页：提交过的几何可能还引用着它们的纹理。
    private readonly List<RetiredPage> retired = [];
    private readonly Dictionary<UIGlyphKey, int> batchKeys = [];
    private readonly List<int> coldSlots = [];
    private readonly List<PendingGlyph> pending = [];
    private readonly List<int> rasterSlots = [];
    private readonly List<(int first, int target)> duplicates = [];

    private UIGlyphRequest[] requestBuffer = new UIGlyphRequest[16];
    private UIGlyphResult[] resultBuffer = new UIGlyphResult[16];
    private UIGlyphRequest[] rasterRequests = new UIGlyphRequest[16];
    private UIGlyphResult[] rasterResults = new UIGlyphResult[16];
    private byte[] pixelBuffer = new byte[64 * 1024];
    private readonly UIGlyphEntry?[] singleEntry = new UIGlyphEntry?[1];
    private readonly UIGlyphDemand[] singleDemand = new UIGlyphDemand[1];

    private UIGlyphEntry? missingEntry;
    private UIGlyphEntry? retryEntry;
    private ulong context;
    private ulong frameCounter;
    private bool frameOpen;
    private bool disposed;
    private int nextPageId = 1;
    private uint nextGeneration = 1;

    private readonly struct ScalarKey : IEquatable<ScalarKey>
    {
        private readonly int fontObjectId;
        private readonly ulong revision;
        private readonly uint scalar;

        internal ScalarKey(int fontObjectId, ulong revision, uint scalar)
        {
            this.fontObjectId = fontObjectId;
            this.revision = revision;
            this.scalar = scalar;
        }

        public bool Equals(ScalarKey other) =>
            fontObjectId == other.fontObjectId && revision == other.revision && scalar == other.scalar;

        public override bool Equals(object? other) => other is ScalarKey key && Equals(key);

        public override int GetHashCode() => HashCode.Combine(fontObjectId, revision, scalar);
    }

    //一批请求里已经解析出键、但还没有像素的字形。
    private struct PendingGlyph
    {
        internal int entryIndex;
        internal UIGlyphKey key;
        internal UIGlyphResult metrics;
        internal int unitsPerEm;
        internal Font font;
        internal uint scalar;
    }

    private readonly struct RetiredPage
    {
        internal readonly UIFontAtlasPage page;
        internal readonly ulong retireFrame;

        internal RetiredPage(UIFontAtlasPage page, ulong retireFrame)
        {
            this.page = page;
            this.retireFrame = retireFrame;
        }
    }

    /// <summary>当前原生上下文句柄；切换时清理属于旧世界的图集缓存。</summary>
    public ulong Context
    {
        get => context;
        set
        {
            if (context == value) return;
            ClearGlyphs();
            context = value;
        }
    }

    /// <summary>图集上下文版本，排版缓存据此检查世界切换。</summary>
    public ulong Revision { get; private set; } = 1;

    /// <summary>常驻 Atlas 字节数。</summary>
    public long ResidentBytes { get; private set; }

    /// <summary>普通页与独占页总数。</summary>
    public int PageCount => pages.Count;

    /// <summary>常驻字形条目数。</summary>
    public int GlyphCount => glyphs.Count;

    /// <summary>开启一帧：清空本帧去重表，丢弃已经跨过一帧的回收页。</summary>
    public void BeginFrame()
    {
        if (disposed) return;
        ++frameCounter;
        if (frameCounter == 0) frameCounter = 1;
        frameOpen = true;

        //移除已被世界销毁的纹理页
        for (int index = pages.Count - 1; index >= 0; --index)
            if (pages[index].texture is not { IsAlive: true }) ReleasePage(pages[index]);

        //回收页多留一帧：上一次提交的几何可能还引用着它们的纹理。
        for (int index = retired.Count - 1; index >= 0; --index)
        {
            if (retired[index].retireFrame + 2 <= frameCounter) retired.RemoveAt(index);
        }
    }

    /// <summary>
    /// 请求一个字形。命中直接返回；冷字形在主线程同步生成，一次调用之后条目必定可用。
    /// 上下文不可用时返回缺字条目，排版继续。
    /// </summary>
    public UIGlyphEntry RequestGlyph(Font? font, uint scalar, FontRasterMode rasterMode, int pixelSize)
    {
        singleDemand[0] = new UIGlyphDemand(font, scalar, rasterMode, pixelSize);
        ResolveRequests(singleDemand, singleEntry);
        return singleEntry[0] ?? Missing;
    }

    /// <summary>批量请求字形；结果与请求一一对应，缓冲小于请求数量时抛异常。</summary>
    public int ResolveRequests(ReadOnlySpan<UIGlyphDemand> demands, Span<UIGlyphEntry?> entries)
    {
        if (entries.Length < demands.Length)
            throw new ArgumentException("字形结果缓冲小于请求数量。", nameof(entries));
        if (disposed || demands.Length == 0) return 0;
        if (!frameOpen) BeginFrame();

        for (int index = 0; index < demands.Length; ++index) entries[index] = null;
        Resolve(demands, entries);
        return demands.Length;
    }

    /// <summary>按缓存键查询已经常驻的字形；命中会固定所在页。</summary>
    public bool TryGetGlyph(in UIGlyphKey key, out UIGlyphEntry entry)
    {
        if (!disposed && glyphs.TryGetValue(key, out UIGlyphEntry? found)
            && (found.page == null || found.HasPixels))
        {
            PinGlyph(found);
            entry = found;
            return true;
        }
        entry = Missing;
        return false;
    }

    /// <summary>按标量查询已经常驻的字形；不触发解析。</summary>
    public bool TryGetGlyph(Font? font, uint scalar, FontRasterMode rasterMode, int pixelSize, out UIGlyphEntry entry)
    {
        entry = Missing;
        if (disposed || font == null) return false;

        int fontObjectId = font.GetObjectId();
        ulong revision = font.GetRevision();
        if (!scalarGlyphs.TryGetValue(new ScalarKey(fontObjectId, revision, scalar), out uint glyphIndex)
            || glyphIndex == 0)
        {
            return false;
        }

        UIGlyphKey key = new(fontObjectId, revision, glyphIndex, rasterMode,
            KeyPixelSize(rasterMode, pixelSize));
        return TryGetGlyph(key, out entry);
    }

    /// <summary>结束一帧：回收未被固定且最久未使用的页，直到回到软预算以内。</summary>
    public void EndFrame()
    {
        if (disposed) return;
        frameOpen = false;
        if (ResidentBytes <= ByteBudget) return;

        //只回收本帧没有命中的页，并且一帧只回收一轮，避免同帧内反复抖动。
        List<UIFontAtlasPage> candidates = [];
        foreach (UIFontAtlasPage page in pages)
        {
            if (page.pinnedFrame != frameCounter) candidates.Add(page);
        }
        candidates.Sort(static (left, right) => left.lastUsedFrame.CompareTo(right.lastUsedFrame));
        foreach (UIFontAtlasPage page in candidates)
        {
            if (ResidentBytes <= ByteBudget) break;
            ReleasePage(page);
        }
    }

    /// <summary>释放全部页与条目；共享实例在下一次访问时重新建立。</summary>
    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        ClearGlyphs();
        if (ReferenceEquals(shared, this)) shared = null;
    }

    //清理世界所属的图集和字形状态
    private void ClearGlyphs()
    {
        foreach (UIFontAtlasPage page in pages)
        {
            page.generation = NextGeneration();
            page.texture = null;
            page.prebakedAtlas = null;
        }
        foreach (RetiredPage item in retired)
        {
            item.page.generation = NextGeneration();
            item.page.texture = null;
            item.page.prebakedAtlas = null;
        }
        pages.Clear();
        retired.Clear();
        glyphs.Clear();
        prebakedAtlases.Clear();
        scalarGlyphs.Clear();
        batchKeys.Clear();
        pending.Clear();
        coldSlots.Clear();
        rasterSlots.Clear();
        ResidentBytes = 0;
        missingEntry = null;
        retryEntry = null;
        frameOpen = false;
        ++Revision;
        if (Revision == 0) Revision = 1;
    }

    /// <summary>写入共享实例的原生上下文；未接入时字形查询退化成缺字方框。</summary>
    internal static void SetSharedContext(ulong context)
    {
        sharedContext = context;
        if (shared != null) shared.Context = context;
    }

    //排版直接使用缓存作为度量来源：字形与像素都从这里取。
    void IUITextMetrics.ResolveGlyphs(ReadOnlySpan<UIGlyphDemand> demands, Span<UIGlyphEntry?> entries) =>
        ResolveRequests(demands, entries);

    float IUITextMetrics.GetKerning(Font? font, uint leftGlyph, uint rightGlyph)
    {
        //字距查询不经过 UI 上下文，没有上下文时也能拿到。
        if (font == null || leftGlyph == 0 || rightGlyph == 0) return 0.0f;
        return RetainedGuiNative.GetKerning(font.GetObjectId(), leftGlyph, rightGlyph);
    }

    void IUITextMetrics.GetFontMetrics(Font? font, out float ascender, out float lineHeight, out int unitsPerEm)
    {
        //字体为空时按 1em 行高、0.8em 上升部回退，与缺字方框一致。
        if (font == null)
        {
            unitsPerEm = MissingUnitsPerEm;
            ascender = MissingBoxHeightEm * MissingUnitsPerEm;
            lineHeight = MissingUnitsPerEm;
            return;
        }

        unitsPerEm = NormalizeUnits(font.unitsPerEm);
        ascender = font.ascender > 0.0f ? font.ascender : MissingBoxHeightEm * unitsPerEm;
        lineHeight = font.lineHeight > 0.0f ? font.lineHeight : unitsPerEm;
    }

    //缺字条目的实例在整个缓存内共享。
    private UIGlyphEntry Missing => missingEntry ??= UIGlyphEntry.CreateMissing();
    private UIGlyphEntry RetryMissing => retryEntry ??= UIGlyphEntry.CreateMissing(true);

    //位图模式取投影字号并夹紧到取值范围；距离场模式尺寸固定，键里记 0。
    private static int KeyPixelSize(FontRasterMode rasterMode, int pixelSize)
    {
        if (rasterMode != FontRasterMode.Bitmap) return 0;
        return Math.Clamp(pixelSize, MinBitmapPixelSize, MaxBitmapPixelSize);
    }

    //读取字形光栅密度
    private static int EntryPixelSize(Font font, FontRasterMode rasterMode, int pixelSize) =>
        rasterMode == FontRasterMode.Bitmap ? KeyPixelSize(rasterMode, pixelSize) : (int)font.distanceFieldSize;

    private static int ModeChannels(FontRasterMode rasterMode) =>
        rasterMode == FontRasterMode.MSDF ? 3 : 1;

    private static int NormalizeUnits(uint unitsPerEm) => unitsPerEm == 0 ? MissingUnitsPerEm : (int)unitsPerEm;

    private static int MaximumTextureSize()
    {
        int maximum = Texture2D.GetMaximumSize();
        return maximum > 0 ? maximum : FallbackMaximumSize;
    }

    //一次批量解析：查驻留 → 取度量 → 挑冷字形 → 光栅化 → 装箱局部上传。
    private void Resolve(ReadOnlySpan<UIGlyphDemand> demands, Span<UIGlyphEntry?> entries)
    {
        coldSlots.Clear();
        for (int index = 0; index < demands.Length; ++index)
        {
            if (TryGetDemand(demands[index], out UIGlyphEntry? resident))
            {
                entries[index] = resident;
                continue;
            }
            coldSlots.Add(index);
        }
        if (coldSlots.Count == 0) return;

        int coldCount = coldSlots.Count;
        EnsureBuffer(ref requestBuffer, coldCount);
        EnsureBuffer(ref resultBuffer, coldCount);
        for (int index = 0; index < coldCount; ++index)
        {
            UIGlyphDemand demand = demands[coldSlots[index]];
            requestBuffer[index] = new UIGlyphRequest
            {
                fontObjectId = demand.font?.GetObjectId() ?? 0,
                scalar = demand.scalar,
                glyphIndex = 0,
                rasterMode = (uint)demand.rasterMode,
                pixelSize = (uint)KeyPixelSize(demand.rasterMode, demand.pixelSize),
                fontRevision = demand.font?.GetRevision() ?? 0,
            };
        }

        if (!QueryMetrics(coldCount, out int metricsCount) || metricsCount < coldCount)
        {
            //保留临时失败状态并在后续帧重试
            for (int index = 0; index < coldCount; ++index) entries[coldSlots[index]] = RetryMissing;
            return;
        }

        pending.Clear();
        batchKeys.Clear();
        duplicates.Clear();
        for (int index = 0; index < coldCount; ++index)
        {
            UIGlyphDemand demand = demands[coldSlots[index]];
            UIGlyphResult metrics = resultBuffer[index];
            int fontObjectId = requestBuffer[index].fontObjectId;
            ulong revision = requestBuffer[index].fontRevision;

            if (demand.font == null || metrics.glyphIndex == 0)
            {
                RememberScalar(fontObjectId, revision, demand.scalar, 0);
                entries[coldSlots[index]] = Missing;
                continue;
            }

            RememberScalar(fontObjectId, revision, demand.scalar, metrics.glyphIndex);
            UIGlyphKey key = new(fontObjectId, revision, metrics.glyphIndex, demand.rasterMode,
                KeyPixelSize(demand.rasterMode, demand.pixelSize));
            //批内可能有多个标量映射到同一字形：先查驻留表，再查批内表。
            if (glyphs.TryGetValue(key, out UIGlyphEntry? resident)
                && (resident.page == null || resident.HasPixels))
            {
                PinGlyph(resident);
                entries[coldSlots[index]] = resident;
                continue;
            }
            if (batchKeys.TryGetValue(key, out int first))
            {
                //同一个键在批内出现多次：记下第一处，提交后回填同一份结果。
                duplicates.Add((first, coldSlots[index]));
                continue;
            }

            batchKeys.Add(key, pending.Count);
            pending.Add(new PendingGlyph
            {
                entryIndex = coldSlots[index],
                key = key,
                metrics = metrics,
                unitsPerEm = NormalizeUnits(demand.font.unitsPerEm),
                font = demand.font,
                scalar = demand.scalar,
            });
        }

        Rasterize(entries);
    }

    //查询已经常驻的条目；命中会推动 LRU 并固定所在页。
    private bool TryGetDemand(in UIGlyphDemand demand, out UIGlyphEntry? entry)
    {
        entry = null;
        if (demand.font == null) return false;

        if (TryGetPrebakedGlyph(demand, out entry)) return true;

        int fontObjectId = demand.font.GetObjectId();
        ulong revision = demand.font.GetRevision();
        if (!scalarGlyphs.TryGetValue(new ScalarKey(fontObjectId, revision, demand.scalar), out uint glyphIndex))
            return false;
        if (glyphIndex == 0)
        {
            //已知缺字，不必再问原生侧。
            entry = Missing;
            return true;
        }

        UIGlyphKey key = new(fontObjectId, revision, glyphIndex, demand.rasterMode,
            KeyPixelSize(demand.rasterMode, demand.pixelSize));
        if (!glyphs.TryGetValue(key, out UIGlyphEntry? found)) return false;
        if (found.page != null && !found.HasPixels) return false;
        PinGlyph(found);
        entry = found;
        return true;
    }

    private void RememberScalar(int fontObjectId, ulong revision, uint scalar, uint glyphIndex) =>
        scalarGlyphs[new ScalarKey(fontObjectId, revision, scalar)] = glyphIndex;

    //加载预烘焙载荷并移除同字体的旧版本
    private unsafe UIFontPrebakedAtlas? LoadPrebakedAtlas(Font font)
    {
        int fontId = font.GetObjectId();
        ulong revision = font.GetRevision();
        if (prebakedAtlases.TryGetValue((fontId, revision), out UIFontPrebakedAtlas? resident)) return resident;
        if (Context == 0) return null;

        //释放重新导入前的图集状态
        List<(int fontId, ulong revision)> obsolete = [];
        foreach (var key in prebakedAtlases.Keys)
            if (key.fontId == fontId && key.revision != revision) obsolete.Add(key);
        for (int index = pages.Count - 1; index >= 0; --index)
            if (pages[index].fontObjectId == fontId && pages[index].fontRevision != revision) ReleasePage(pages[index]);
        foreach (var key in obsolete) prebakedAtlases.Remove(key);

        //读取完整预烘焙载荷
        int required = RetainedGuiNative.ReadPrebakedAtlas(Context, fontId, revision, null, 0);
        if (required < 0) return null;
        if (required == 0) { prebakedAtlases[(fontId, revision)] = null; return null; }
        if (required > UIFontPrebakedAtlas.MaximumPayloadBytes)
        {
            Console.Error.WriteLine("FontAtlasCache: 预烘焙载荷超出字节限制。");
            prebakedAtlases[(fontId, revision)] = null;
            return null;
        }
        byte[] data = new byte[required];
        fixed (byte* output = data)
            if (RetainedGuiNative.ReadPrebakedAtlas(Context, fontId, revision, output, data.Length) != required) return null;
        try
        {
            UIFontPrebakedAtlas atlas = UIFontPrebakedAtlas.Read(data);
            prebakedAtlases[(fontId, revision)] = atlas;
            return atlas;
        }
        catch (System.IO.IOException exception)
        {
            Console.Error.WriteLine($"FontAtlasCache: 预烘焙载荷无效：{exception.Message}");
            prebakedAtlases[(fontId, revision)] = null;
            return null;
        }
    }

    //加载预烘焙字形所在的图集页并建立缓存条目
    private bool TryGetPrebakedGlyph(in UIGlyphDemand demand, out UIGlyphEntry? entry)
    {
        entry = null;
        Font? font = demand.font;
        if (font is not { IsAlive: true }) return false;
        UIFontPrebakedAtlas? atlas = LoadPrebakedAtlas(font);
        if (atlas == null || atlas.rasterMode != demand.rasterMode
            || (demand.rasterMode == FontRasterMode.Bitmap && atlas.pixelSize != KeyPixelSize(demand.rasterMode, demand.pixelSize))
            || !atlas.glyphs.TryGetValue(demand.scalar, out UIFontPrebakedAtlas.Glyph glyph)) return false;

        UIGlyphKey key = new(font.GetObjectId(), font.GetRevision(), glyph.GlyphIndex, demand.rasterMode,
            KeyPixelSize(demand.rasterMode, demand.pixelSize));
        if (glyphs.TryGetValue(key, out entry) && (entry.page == null || entry.HasPixels))
        {
            RememberScalar(key.fontObjectId, key.fontRevision, demand.scalar, glyph.GlyphIndex);
            PinGlyph(entry);
            return true;
        }

        //按需上传烘焙页并恢复装箱游标
        UIFontAtlasPage? page = null;
        if (glyph.PageIndex >= 0)
        {
            UIFontPrebakedAtlas.Page source = atlas.pages[glyph.PageIndex];
            if (source.Width > MaximumTextureSize() || source.Height > MaximumTextureSize()) return false;
            page = atlas.runtimePages[glyph.PageIndex];
            if (page?.texture is not { IsAlive: true })
            {
                if (page != null) ReleasePage(page);
                page = CreatePage(font, source.Width, source.Height, source.Channels, source.Width != (int)font.atlasSize);
                if (page == null || page.texture == null || !page.texture.UpdateRegion(0, 0, source.Width, source.Height,
                    atlas.data.AsSpan(source.PixelOffset, source.ByteCount), source.Width * source.Channels))
                {
                    if (page != null) ReleasePage(page);
                    entry = RetryMissing;
                    return true;
                }
                page.cursor = source.Cursor;
                page.prebakedAtlas = atlas;
                page.prebakedPageIndex = glyph.PageIndex;
                atlas.runtimePages[glyph.PageIndex] = page;
            }
        }

        //复用导入时的度量和字形区域
        entry = new UIGlyphEntry(key, glyph.GlyphIndex, false, atlas.unitsPerEm)
        {
            Advance = glyph.Advance, BearingX = glyph.BearingX, BearingY = glyph.BearingY,
            Width = glyph.MetricWidth, Height = glyph.MetricHeight, PixelSize = atlas.pixelSize,
            BitmapWidth = glyph.Width, BitmapHeight = glyph.Height, OriginX = glyph.OriginX, OriginY = glyph.OriginY,
            page = page,
        };
        if (page != null)
            entry.Uv = new UIRect(new vector2((float)glyph.X / page.width, 1.0f - (float)(glyph.Y + glyph.Height) / page.height),
                new vector2((float)glyph.Width / page.width, (float)glyph.Height / page.height));
        glyphs[key] = entry;
        RememberScalar(key.fontObjectId, key.fontRevision, demand.scalar, glyph.GlyphIndex);
        PinGlyph(entry);
        return true;
    }

    //命中即固定所在页：本帧不再回收，同时推进 LRU 时间戳。
    internal void PinGlyph(UIGlyphEntry entry)
    {
        UIFontAtlasPage? page = entry.page;
        if (page?.texture is not { IsAlive: true }) return;
        page.lastUsedFrame = frameCounter;
        page.pinnedFrame = frameCounter;
    }

    //批量取度量。
    private bool QueryMetrics(int count, out int written)
    {
        written = 0;
        if (Context == 0) return false;
        unsafe
        {
            fixed (UIGlyphRequest* requests = requestBuffer)
            {
                int reported = CallQuery(requests, count);
                if (reported < 0)
                {
                    Console.Error.WriteLine("FontAtlasCache: 字形度量查询失败。");
                    return false;
                }
                if (reported > resultBuffer.Length)
                {
                    //容量不足时原生侧只报所需条数，扩容后再来一次。
                    EnsureBuffer(ref resultBuffer, reported);
                    reported = CallQuery(requests, count);
                    if (reported < 0) return false;
                }
                written = Math.Min(reported, count);
                return true;
            }
        }
    }

    private unsafe int CallQuery(UIGlyphRequest* requests, int count)
    {
        fixed (UIGlyphResult* results = resultBuffer)
        {
            return RetainedGuiNative.QueryGlyphs(Context, requests, count, results, count);
        }
    }

    //批量光栅化；结果与 pending 一一对应，没有轮廓的字形不占 Atlas。
    private void Rasterize(Span<UIGlyphEntry?> entries)
    {
        if (pending.Count == 0) return;

        EnsureBuffer(ref rasterRequests, pending.Count);
        EnsureBuffer(ref rasterResults, pending.Count);
        rasterResults.AsSpan(0, pending.Count).Clear();
        rasterSlots.Clear();
        int count = 0;
        for (int index = 0; index < pending.Count; ++index)
        {
            PendingGlyph glyph = pending[index];
            //度量的包围盒为空说明是空格一类没有轮廓的字形，不必光栅化。
            if (glyph.metrics.width <= 0.0f || glyph.metrics.height <= 0.0f) continue;
            rasterRequests[count] = new UIGlyphRequest
            {
                fontObjectId = glyph.key.fontObjectId,
                scalar = glyph.scalar,
                glyphIndex = glyph.key.glyphIndex,
                rasterMode = (uint)glyph.key.rasterMode,
                pixelSize = (uint)glyph.key.bitmapPixelSize,
                fontRevision = glyph.key.fontRevision,
            };
            rasterSlots.Add(index);
            ++count;
        }

        int available = 0;
        if (count > 0)
        {
            available = RasterizeBatch(count);
            if (available > pixelBuffer.Length)
            {
                //容量不足时原生侧只报所需字节数，扩容后再来一次。
                pixelBuffer = new byte[available];
                available = RasterizeBatch(count);
            }
            if (available < 0)
            {
                Console.Error.WriteLine("FontAtlasCache: 字形光栅化失败。");
                available = 0;
            }
        }

        CommitPending(entries, available);
    }

    //一次光栅化调用；返回所需像素字节数，失败返回负值。
    private unsafe int RasterizeBatch(int count)
    {
        if (Context == 0) return -1;
        fixed (UIGlyphRequest* requests = rasterRequests)
        fixed (UIGlyphResult* results = rasterResults)
        fixed (byte* pixels = pixelBuffer)
        {
            return RetainedGuiNative.RasterizeGlyphs(Context, requests, count, results,
                rasterResults.Length, pixels, pixelBuffer.Length);
        }
    }

    //把光栅化结果写进驻留表。没有像素的字形只留度量；上传失败的条目不入驻，下一帧重试。
    private void CommitPending(Span<UIGlyphEntry?> entries, int availableBytes)
    {
        int rasterIndex = 0;
        for (int index = 0; index < pending.Count; ++index)
        {
            PendingGlyph glyph = pending[index];
            bool rendered = rasterIndex < rasterSlots.Count && rasterSlots[rasterIndex] == index;
            UIGlyphResult raster = rendered ? rasterResults[rasterIndex] : default;
            if (rendered) ++rasterIndex;

            UIGlyphEntry entry = new(glyph.key, glyph.key.glyphIndex, false, glyph.unitsPerEm)
            {
                Advance = glyph.metrics.advance,
                BearingX = glyph.metrics.bearingX,
                BearingY = glyph.metrics.bearingY,
                Width = glyph.metrics.width,
                Height = glyph.metrics.height,
                PixelSize = EntryPixelSize(glyph.font, glyph.key.rasterMode, glyph.key.bitmapPixelSize),
            };

            bool hasPixels = rendered && raster.bitmapWidth > 0 && raster.bitmapHeight > 0;
            bool needsRetry = rendered && !hasPixels;
            if (hasPixels)
            {
                bool complete = (long)raster.byteOffset + raster.byteCount <= availableBytes;
                int channels = raster.channels > 0 ? raster.channels : ModeChannels(glyph.key.rasterMode);
                UIFontAtlasPage? page = null;
                int x = 0;
                int y = 0;
                if (complete
                    && TryPack(glyph.font, raster.bitmapWidth, raster.bitmapHeight, channels, out page, out x, out y)
                    && page != null
                    && page.texture != null
                    && page.texture.UpdateRegion(x, y, raster.bitmapWidth, raster.bitmapHeight,
                        pixelBuffer.AsSpan((int)raster.byteOffset, (int)raster.byteCount), raster.rowStride))
                {
                    page.lastUsedFrame = frameCounter;
                    page.pinnedFrame = frameCounter;
                    entry.page = page;
                    entry.BitmapWidth = raster.bitmapWidth;
                    entry.BitmapHeight = raster.bitmapHeight;
                    entry.OriginX = raster.originX;
                    entry.OriginY = raster.originY;
                    //UV 的 v 轴朝上：位图行 0 是字形顶部，所以顶部对应较大的 v。
                    float left = (float)x / page.width;
                    float bottom = 1.0f - (float)(y + raster.bitmapHeight) / page.height;
                    float right = (float)(x + raster.bitmapWidth) / page.width;
                    float top = 1.0f - (float)y / page.height;
                    entry.Uv = new UIRect(new vector2(left, bottom), new vector2(right - left, top - bottom));
                }
                else
                {
                    //装箱或上传失败：不驻留，下一帧重试；超大字形已经报过错误。
                    needsRetry = true;
                }
            }

            entry.NeedsRetry = needsRetry;
            if (!needsRetry) glyphs[glyph.key] = entry;
            entries[glyph.entryIndex] = entry;
        }

        //批内重复的标量回填第一次解析出的条目。
        foreach ((int first, int target) in duplicates) entries[target] = entries[pending[first].entryIndex];
    }

    //在页上分配一块区域：普通页逐行装箱，超大字形用能容纳它的二次幂独占页。
    private bool TryPack(Font font, int width, int height, int channels, out UIFontAtlasPage? page, out int x, out int y)
    {
        page = null;
        x = 0;
        y = 0;
        if (width <= 0 || height <= 0) return false;
        if (width > MaximumTextureSize() || height > MaximumTextureSize()) return false;

        int pageSize = Math.Min((int)font.atlasSize, MaximumTextureSize());
        int fontObjectId = font.GetObjectId();
        ulong fontRevision = font.GetRevision();
        if (width > pageSize || height > pageSize)
        {
            //超大字形独占一页：边长取能容纳它的二次幂，页内不再放别的字形。
            int size = UIFontAtlasCursor.DedicatedPageSize(width, height, pageSize);
            if (size > MaximumTextureSize()) return false;
            UIFontAtlasPage? dedicated = CreatePage(font, size, size, channels, true);
            if (dedicated == null) return false;
            x = 0;
            y = 0;
            dedicated.cursor.cursorX = width;
            dedicated.cursor.rowHeight = height;
            page = dedicated;
            return true;
        }

        for (int index = pages.Count - 1; index >= 0; --index)
        {
            UIFontAtlasPage candidate = pages[index];
            if (candidate.dedicated || candidate.channels != channels || candidate.width != pageSize
                || candidate.fontObjectId != fontObjectId || candidate.fontRevision != fontRevision) continue;
            if (!candidate.cursor.TryAllocate(candidate.width, candidate.height, width, height, out x, out y))
                continue;
            page = candidate;
            return true;
        }

        UIFontAtlasPage? created = CreatePage(font, pageSize, pageSize, channels, false);
        if (created == null) return false;
        if (!created.cursor.TryAllocate(created.width, created.height, width, height, out x, out y)) return false;
        page = created;
        return true;
    }

    private UIFontAtlasPage? CreatePage(Font font, int width, int height, int channels, bool dedicated)
    {
        Texture2D? texture = Texture2D.CreateDynamic(width, height, channels);
        if (texture == null)
        {
            Console.Error.WriteLine($"FontAtlasCache: 创建 {width}×{height} 字形页失败。");
            return null;
        }

        UIFontAtlasPage page = new()
        {
            pageId = nextPageId++,
            generation = NextGeneration(),
            texture = texture,
            width = width,
            height = height,
            channels = channels,
            fontObjectId = font.GetObjectId(),
            fontRevision = font.GetRevision(),
            bytes = (long)width * height * channels,
            lastUsedFrame = frameCounter,
            pinnedFrame = frameCounter,
            dedicated = dedicated,
        };
        pages.Add(page);
        ResidentBytes += page.bytes;
        return page;
    }

    //回收一页：条目随页一起移除，代次推进让依赖它的几何失效。
    private void ReleasePage(UIFontAtlasPage page)
    {
        if (page.prebakedAtlas != null)
        {
            page.prebakedAtlas.runtimePages[page.prebakedPageIndex] = null;
            page.prebakedAtlas = null;
            page.prebakedPageIndex = -1;
        }
        pages.Remove(page);
        ResidentBytes -= page.bytes;
        page.generation = NextGeneration();
        retired.Add(new RetiredPage(page, frameCounter));

        List<UIGlyphKey> stale = [];
        foreach ((UIGlyphKey key, UIGlyphEntry entry) in glyphs)
        {
            if (ReferenceEquals(entry.page, page)) stale.Add(key);
        }
        foreach (UIGlyphKey key in stale) glyphs.Remove(key);
    }

    private uint NextGeneration()
    {
        uint value = nextGeneration++;
        if (nextGeneration == 0) nextGeneration = 1;
        return value == 0 ? NextGeneration() : value;
    }

    private static void EnsureBuffer(ref UIGlyphRequest[] buffer, int count)
    {
        if (buffer.Length >= count) return;
        int size = buffer.Length;
        while (size < count) size *= 2;
        buffer = new UIGlyphRequest[size];
    }

    private static void EnsureBuffer(ref UIGlyphResult[] buffer, int count)
    {
        if (buffer.Length >= count) return;
        int size = buffer.Length;
        while (size < count) size *= 2;
        buffer = new UIGlyphResult[size];
    }
}
