using System;
using System.Collections.Generic;
using Orbeden;

namespace OrbedenEditor;

/// <summary>
/// UI 控件的创建菜单。每种控件按固定配方建出根节点与子节点，子节点同样带 UILayout；
/// 创建与引用配置合成一条事务：中途失败会把本次建出来的对象整批撤回。
/// </summary>
public static class UICreationMenu
{
    //每种控件的默认尺寸。
    private const float CanvasDefaultWidth = 1920.0f;
    private const float CanvasDefaultHeight = 1080.0f;
    private const float ImageDefaultWidth = 100.0f;
    private const float TextDefaultWidth = 160.0f;
    private const float TextDefaultHeight = 30.0f;
    private const float ButtonDefaultWidth = 160.0f;
    private const float ButtonDefaultHeight = 30.0f;
    private const float ToggleDefaultWidth = 160.0f;
    private const float ToggleDefaultHeight = 24.0f;
    private const float ToggleMarkSize = 16.0f;
    private const float ToggleLabelIndent = 24.0f;
    private const float SliderDefaultWidth = 160.0f;
    private const float SliderDefaultHeight = 20.0f;
    private const float SliderTrackHeight = 4.0f;
    private const float ScrollBarDefaultWidth = 160.0f;
    private const float ScrollBarDefaultHeight = 16.0f;
    private const float ScrollBoxDefaultWidth = 200.0f;
    private const float ScrollBoxDefaultHeight = 160.0f;
    private const float ScrollBoxBarWidth = 16.0f;
    private const float ScrollBoxContentHeight = 320.0f;
    private const float ComboArrowSize = 12.0f;
    private const float ContainerDefaultSize = 200.0f;
    //文本子节点在父矩形里留出的边距。
    private const float TextPaddingX = 8.0f;
    private const float TextPaddingY = 4.0f;

    private static bool registered;

    /// <summary>登记全部创建菜单项；重复调用无副作用。</summary>
    public static void Register()
    {
        if (registered) return;
        registered = true;

        foreach (UIWidgetKind kind in Enum.GetValues<UIWidgetKind>())
        {
            UIWidgetKind captured = kind;
            EnsContextMenuRegistry.Register("UI/" + kind, context => Create(context, captured));
        }
    }

    /// <summary>
    /// 在选中节点下创建一种控件。没有选中节点时先建一块 Overlay 画布。
    /// 创建与引用配置合成一条事务：失败时把本次建出来的对象全部销毁。
    /// </summary>
    public static void Create(EnsContext context, UIWidgetKind kind)
    {
        List<Ens> created = [];
        List<Action> configure = [];
        try
        {
            Ens parent = ResolveParent(context, kind, created, configure);
            //世界空间画布可以挂在场景根上：它不在任何画布的逻辑矩形里。
            if (!parent.IsValid && kind is not (UIWidgetKind.Canvas or UIWidgetKind.WorldSpaceCanvas))
                throw new InvalidOperationException("No usable parent node.");

            //先建对象，再配引用：配置阶段失败时对象已经在 created 里，能整批撤回。
            Build(kind, parent, created, configure);

            foreach (Action action in configure) action();

            //成功之后记录一条撤销：撤回本次创建。重做按同一种控件再建一次。
            List<EnsId> createdIds = [];
            foreach (Ens ens in created) createdIds.Add(ens.Id);
            EnsId parentId = parent.Id;
            EditorPropertyHistory.RecordAction(
                $"Create {kind}",
                () =>
                {
                    foreach (EnsId id in createdIds)
                    {
                        Ens ens = Ens.FromId(id);
                        if (ens.IsValid) ens.Destroy();
                    }
                },
                () => Create(parentId, kind));
        }
        catch (Exception exception)
        {
            //失败全撤销：把本次建出来的对象按逆序销毁。
            for (int index = created.Count - 1; index >= 0; --index)
            {
                if (created[index].IsValid) created[index].Destroy();
            }
            Console.Error.WriteLine($"UICreationMenu: creating {kind} failed and was rolled back. {exception}");
        }
    }

    /// <summary>在指定父节点下重建一种控件；撤销记录的重做用它。</summary>
    private static void Create(EnsId parentId, UIWidgetKind kind)
    {
        Ens parent = Ens.FromId(parentId);
        if (!parent.IsValid && kind is not (UIWidgetKind.Canvas or UIWidgetKind.WorldSpaceCanvas)) return;

        List<Ens> created = [];
        List<Action> configure = [];
        Build(kind, parent, created, configure);
        foreach (Action action in configure) action();
    }

    //父节点：选中的节点；没有选中时先建一块画布当父节点。
    private static Ens ResolveParent(EnsContext context, UIWidgetKind kind, List<Ens> created,
        List<Action> configure)
    {
        //将新画布直接放到场景根节点
        if (kind is UIWidgetKind.Canvas or UIWidgetKind.WorldSpaceCanvas) return Ens.Null;
        if (context.IsValid)
        {
            Ens selected = Ens.FromId(context.Id);
            //检查选中节点到画布的布局链
            for (Ens current = selected; current.IsValid; current = Ens.FromId(current.Transform.GetParent()))
            {
                if (current.GetComponent<UILayout>() == null) break;
                if (current.GetComponent<Canvas>() != null) return selected;
            }
        }

        //没有选中节点：建一块 Overlay 画布，本次要创建的控件落在它下面。
        Ens canvas = CreateNode(Ens.Null, "Canvas", created);
        UILayout? layout = canvas.AddComponent<UILayout>();
        layout?.SetSizeDelta(new vector2(CanvasDefaultWidth, CanvasDefaultHeight));
        Canvas? canvasComponent = canvas.AddComponent<Canvas>();
        configure.Add(() =>
        {
            canvasComponent?.SetRenderMode(CanvasRenderMode.Overlay);
            canvas.GetComponent<UILayout>()?.SetSizeDelta(new vector2(CanvasDefaultWidth, CanvasDefaultHeight));
            SetCanvasTransformScale(canvas, Canvas.DefaultOverlayTransformScale);
        });
        return canvas;
    }

    //按配方建出一种控件的根节点与子节点。
    private static void Build(UIWidgetKind kind, Ens parent, List<Ens> created, List<Action> configure)
    {
        switch (kind)
        {
        case UIWidgetKind.Canvas:
            BuildCanvas(parent, created, configure);
            break;
        case UIWidgetKind.WorldSpaceCanvas:
            BuildWorldSpaceCanvas(parent, created, configure);
            break;
        case UIWidgetKind.Image:
            BuildImage(parent, created, configure);
            break;
        case UIWidgetKind.Text:
            BuildText(parent, created, configure);
            break;
        case UIWidgetKind.Button:
            BuildButton(parent, created, configure);
            break;
        case UIWidgetKind.CheckBox:
            BuildToggle(parent, created, configure, radio: false);
            break;
        case UIWidgetKind.RadioButton:
            BuildToggle(parent, created, configure, radio: true);
            break;
        case UIWidgetKind.Slider:
            BuildSlider(parent, created, configure);
            break;
        case UIWidgetKind.ScrollBar:
            BuildScrollBar(parent, created, configure);
            break;
        case UIWidgetKind.ScrollBox:
            BuildScrollBox(parent, created, configure);
            break;
        case UIWidgetKind.ComboBox:
            BuildComboBox(parent, created, configure);
            break;
        case UIWidgetKind.TextField:
            BuildTextField(parent, created, configure);
            break;
        case UIWidgetKind.LayoutBox:
            BuildContainer(parent, created, configure, isGrid: false);
            break;
        case UIWidgetKind.GridBox:
            BuildContainer(parent, created, configure, isGrid: true);
            break;
        case UIWidgetKind.Mask:
            BuildMask(parent, created, configure);
            break;
        default:
            throw new ArgumentOutOfRangeException(nameof(kind));
        }
    }

    private static void BuildCanvas(Ens parent, List<Ens> created, List<Action> configure)
    {
        Ens root = CreateNode(parent, "Canvas", created);
        UILayout? layout = root.AddComponent<UILayout>();
        Canvas? canvas = root.AddComponent<Canvas>();
        configure.Add(() =>
        {
            layout?.SetSizeDelta(new vector2(CanvasDefaultWidth, CanvasDefaultHeight));
            canvas?.SetRenderMode(CanvasRenderMode.Overlay);
            SetCanvasTransformScale(root, Canvas.DefaultOverlayTransformScale);
        });
    }

    //世界空间画布：根矩形 800×600 逻辑单位，节点 Transform 缩放给出一单位折合多少世界单位。
    private static void BuildWorldSpaceCanvas(Ens parent, List<Ens> created, List<Action> configure)
    {
        Ens root = CreateNode(parent, "WorldSpaceCanvas", created);
        UILayout? layout = root.AddComponent<UILayout>();
        Canvas? canvas = root.AddComponent<Canvas>();
        configure.Add(() =>
        {
            layout?.SetSizeDelta(new vector2(Canvas.DefaultWorldSpaceWidth, Canvas.DefaultWorldSpaceHeight));
            canvas?.SetRenderMode(CanvasRenderMode.WorldSpace);
            SetCanvasTransformScale(root, Canvas.DefaultWorldSpaceTransformScale);
        });
    }

    private static void BuildImage(Ens parent, List<Ens> created, List<Action> configure)
    {
        Ens root = CreateNode(parent, "Image", created);
        UILayout? layout = root.AddComponent<UILayout>();
        Image? image = root.AddComponent<Image>();
        configure.Add(() =>
        {
            layout?.SetSizeDelta(new vector2(ImageDefaultWidth, ImageDefaultWidth));
            image?.SetMode(UIImageMode.Simple);
        });
    }

    private static void BuildText(Ens parent, List<Ens> created, List<Action> configure)
    {
        Ens root = CreateNode(parent, "Text", created);
        UILayout? layout = root.AddComponent<UILayout>();
        Text? text = root.AddComponent<Text>();
        configure.Add(() =>
        {
            layout?.SetSizeDelta(new vector2(TextDefaultWidth, TextDefaultHeight));
            text?.SetText("Text");
            //文字默认不阻挡指针。
            text?.SetRaycastTarget(false);
        });
    }

    private static void BuildButton(Ens parent, List<Ens> created, List<Action> configure)
    {
        Ens root = CreateNode(parent, "Button", created);
        UILayout? layout = root.AddComponent<UILayout>();
        root.AddComponent<Button>();

        //子文本：双轴拉伸、留出边距。
        Ens label = CreateNode(root, "Text", created);
        UILayout? labelLayout = label.AddComponent<UILayout>();
        Text? labelText = label.AddComponent<Text>();
        configure.Add(() =>
        {
            layout?.SetSizeDelta(new vector2(ButtonDefaultWidth, ButtonDefaultHeight));
            StretchChild(labelLayout, TextPaddingX, TextPaddingY);
            labelText?.SetText("Button");
            labelText?.SetRaycastTarget(false);
        });
    }

    private static void BuildToggle(Ens parent, List<Ens> created, List<Action> configure, bool radio)
    {
        string name = radio ? "RadioButton" : "CheckBox";
        Ens root = CreateNode(parent, name, created);
        UILayout? layout = root.AddComponent<UILayout>();
        root.AddComponent<Image>();

        //左侧标记：纯色矩形，默认隐藏由控件接管。
        Ens mark = CreateNode(root, "Mark", created);
        UILayout? markLayout = mark.AddComponent<UILayout>();
        Image? markImage = mark.AddComponent<Image>();

        //右侧标签：留出左边距。
        Ens label = CreateNode(root, "Label", created);
        UILayout? labelLayout = label.AddComponent<UILayout>();
        Text? labelText = label.AddComponent<Text>();

        configure.Add(() =>
        {
            layout?.SetSizeDelta(new vector2(ToggleDefaultWidth, ToggleDefaultHeight));
            SetLeftTopFixed(markLayout, ToggleMarkSize, ToggleMarkSize, 0.0f);
            StretchChild(labelLayout, ToggleLabelIndent, TextPaddingY);
            labelText?.SetText(name);
            labelText?.SetRaycastTarget(false);

            if (radio)
            {
                RadioButton? control = root.AddComponent<RadioButton>();
                control?.SetCheckmark(markImage);
            }
            else
            {
                CheckBox? control = root.AddComponent<CheckBox>();
                control?.SetCheckmark(markImage);
            }
        });
    }

    private static void BuildSlider(Ens parent, List<Ens> created, List<Action> configure)
    {
        Ens root = CreateNode(parent, "Slider", created);
        UILayout? layout = root.AddComponent<UILayout>();
        Slider? slider = root.AddComponent<Slider>();

        //轨道：双轴拉伸、固定高度，垂直居中。
        Ens track = CreateNode(root, "Track", created);
        UILayout? trackLayout = track.AddComponent<UILayout>();
        track.AddComponent<Image>();
        //填充：由滑条驱动。
        Ens fill = CreateNode(root, "Fill", created);
        UILayout? fillLayout = fill.AddComponent<UILayout>();
        fill.AddComponent<Image>();
        //拇指：固定尺寸。
        Ens thumb = CreateNode(root, "Thumb", created);
        UILayout? thumbLayout = thumb.AddComponent<UILayout>();
        thumb.AddComponent<Image>();

        configure.Add(() =>
        {
            layout?.SetSizeDelta(new vector2(SliderDefaultWidth, SliderDefaultHeight));
            SetHorizontalStretch(trackLayout, SliderTrackHeight);
            SetHorizontalStretch(fillLayout, SliderTrackHeight);
            fill?.GetComponent<Image>()?.SetTint(new color(0.3f, 0.6f, 1.0f, 1.0f));
            SetLeftTopFixed(thumbLayout, SliderDefaultHeight, SliderDefaultHeight, 0.0f);
            slider?.SetTrack(trackLayout);
            slider?.SetThumb(thumbLayout);
            slider?.SetFill(fillLayout);
        });
    }

    private static void BuildScrollBar(Ens parent, List<Ens> created, List<Action> configure)
    {
        Ens root = CreateNode(parent, "ScrollBar", created);
        UILayout? layout = root.AddComponent<UILayout>();
        ScrollBar? bar = root.AddComponent<ScrollBar>();

        Ens track = CreateNode(root, "Track", created);
        UILayout? trackLayout = track.AddComponent<UILayout>();
        track.AddComponent<Image>();
        Ens thumb = CreateNode(root, "Thumb", created);
        UILayout? thumbLayout = thumb.AddComponent<UILayout>();
        thumb.AddComponent<Image>();

        configure.Add(() =>
        {
            layout?.SetSizeDelta(new vector2(ScrollBarDefaultWidth, ScrollBarDefaultHeight));
            StretchChild(trackLayout, 0.0f, 0.0f);
            SetLeftTopFixed(thumbLayout, ScrollBarDefaultWidth, ScrollBarDefaultHeight, 0.0f);
            bar?.SetTrack(trackLayout);
            bar?.SetThumb(thumbLayout);
        });
    }

    private static void BuildScrollBox(Ens parent, List<Ens> created, List<Action> configure)
    {
        Ens root = CreateNode(parent, "ScrollBox", created);
        UILayout? layout = root.AddComponent<UILayout>();
        root.AddComponent<Image>();
        ScrollBox? box = root.AddComponent<ScrollBox>();

        //内容：占根减去右侧滚动条的视口，左上锚点，初始高于视口。
        Ens content = CreateNode(root, "Content", created);
        UILayout? contentLayout = content.AddComponent<UILayout>();
        //右侧纵向滚动条。
        Ens bar = CreateNode(root, "VerticalBar", created);
        UILayout? barLayout = bar.AddComponent<UILayout>();
        bar.AddComponent<Image>();
        ScrollBar? barComponent = bar.AddComponent<ScrollBar>();

        configure.Add(() =>
        {
            layout?.SetSizeDelta(new vector2(ScrollBoxDefaultWidth, ScrollBoxDefaultHeight));

            contentLayout?.SetAnchorMin(new vector2(0.0f, 1.0f));
            contentLayout?.SetAnchorMax(new vector2(1.0f, 1.0f));
            contentLayout?.SetPivot(new vector2(0.0f, 1.0f));
            contentLayout?.SetOffset(new vector2(0.0f, 0.0f));
            contentLayout?.SetSizeDelta(new vector2(-ScrollBoxBarWidth, ScrollBoxContentHeight));

            barLayout?.SetAnchorMin(new vector2(1.0f, 0.0f));
            barLayout?.SetAnchorMax(new vector2(1.0f, 1.0f));
            barLayout?.SetPivot(new vector2(1.0f, 0.5f));
            barLayout?.SetOffset(new vector2(0.0f, 0.0f));
            barLayout?.SetSizeDelta(new vector2(ScrollBoxBarWidth, 0.0f));
            barComponent?.SetOrientation(UIOrientation.Vertical);

            box?.SetContent(contentLayout);
            box?.SetVerticalBar(barComponent);
            box?.SynchronizeBars();
        });
    }

    private static void BuildComboBox(Ens parent, List<Ens> created, List<Action> configure)
    {
        Ens root = CreateNode(parent, "ComboBox", created);
        UILayout? layout = root.AddComponent<UILayout>();
        root.AddComponent<Image>();
        ComboBox? combo = root.AddComponent<ComboBox>();

        //标签在左，箭头在右；两者都由控件的布局配置。
        Ens label = CreateNode(root, "Label", created);
        UILayout? labelLayout = label.AddComponent<UILayout>();
        Text? labelText = label.AddComponent<Text>();
        Ens arrow = CreateNode(root, "Arrow", created);
        UILayout? arrowLayout = arrow.AddComponent<UILayout>();
        Image? arrowImage = arrow.AddComponent<Image>();

        configure.Add(() =>
        {
            layout?.SetSizeDelta(new vector2(ButtonDefaultWidth, ButtonDefaultHeight));
            //标签留出右侧箭头的宽度。
            StretchChild(labelLayout, TextPaddingX, TextPaddingY);
            arrowLayout?.SetAnchorMin(new vector2(1.0f, 0.5f));
            arrowLayout?.SetAnchorMax(new vector2(1.0f, 0.5f));
            arrowLayout?.SetPivot(new vector2(1.0f, 0.5f));
            arrowLayout?.SetOffset(new vector2(-TextPaddingX, 0.0f));
            arrowLayout?.SetSizeDelta(new vector2(ComboArrowSize, ComboArrowSize));
            arrowImage?.SetTint(new color(0.8f, 0.8f, 0.8f, 1.0f));
            arrowImage?.SetRaycastTarget(false);
            combo?.SetLabel(labelText);
        });
    }

    private static void BuildTextField(Ens parent, List<Ens> created, List<Action> configure)
    {
        Ens root = CreateNode(parent, "TextField", created);
        UILayout? layout = root.AddComponent<UILayout>();
        root.AddComponent<Image>();
        TextField? field = root.AddComponent<TextField>();

        configure.Add(() =>
        {
            layout?.SetSizeDelta(new vector2(ButtonDefaultWidth, ButtonDefaultHeight));
            field?.SetPlaceholder("Enter text...");
        });
    }

    private static void BuildContainer(Ens parent, List<Ens> created, List<Action> configure, bool isGrid)
    {
        string name = isGrid ? "GridBox" : "LayoutBox";
        Ens root = CreateNode(parent, name, created);
        UILayout? layout = root.AddComponent<UILayout>();

        configure.Add(() =>
        {
            layout?.SetSizeDelta(new vector2(ContainerDefaultSize, ContainerDefaultSize));
            if (isGrid) root.AddComponent<GridBox>();
            else root.AddComponent<LayoutBox>();
        });
    }

    private static void BuildMask(Ens parent, List<Ens> created, List<Action> configure)
    {
        Ens root = CreateNode(parent, "Mask", created);
        UILayout? layout = root.AddComponent<UILayout>();
        Mask? mask = root.AddComponent<Mask>();

        configure.Add(() =>
        {
            layout?.SetSizeDelta(new vector2(ContainerDefaultSize, ContainerDefaultSize));
            mask?.SetMode(UIMaskMode.Rectangle);
        });
    }

    //建一个 UI 节点：默认作者变换为零、单位旋转缩放，并挂上父级。
    private static Ens CreateNode(Ens parent, string name, List<Ens> created)
    {
        Ens node = Ens.Create(name);
        if (!node.IsValid) return node;

        node.Transform.SetLocalPosition(new vector3(0.0f, 0.0f, 0.0f));
        node.Transform.SetLocalRotation(new quaternion(0.0f, 0.0f, 0.0f, 1.0f));
        node.Transform.SetLocalScale(new vector3(1.0f, 1.0f, 1.0f));
        if (parent.IsValid) node.Transform.SetParent(parent.Id);
        created.Add(node);
        return node;
    }

    //画布根缩放：一个逻辑单位折合多少世界单位。场景预览、矩形手柄与聚焦读的是同一个值。
    private static void SetCanvasTransformScale(Ens root, float scale)
    {
        if (!root.IsValid || root.Transform == null) return;
        root.Transform.SetLocalScale(new vector3(scale, scale, scale));
    }

    //子节点双轴拉伸，四周留边距。
    private static void StretchChild(UILayout? layout, float paddingX, float paddingY)
    {
        layout?.SetAnchorMin(new vector2(0.0f, 0.0f));
        layout?.SetAnchorMax(new vector2(1.0f, 1.0f));
        layout?.SetPivot(new vector2(0.5f, 0.5f));
        layout?.SetOffset(new vector2(0.0f, 0.0f));
        layout?.SetSizeDelta(new vector2(-paddingX * 2.0f, -paddingY * 2.0f));
    }

    //子节点横向拉伸、固定高度、垂直居中。
    private static void SetHorizontalStretch(UILayout? layout, float height)
    {
        layout?.SetAnchorMin(new vector2(0.0f, 0.5f));
        layout?.SetAnchorMax(new vector2(1.0f, 0.5f));
        layout?.SetPivot(new vector2(0.5f, 0.5f));
        layout?.SetOffset(new vector2(0.0f, 0.0f));
        layout?.SetSizeDelta(new vector2(0.0f, height));
    }

    //子节点固定尺寸、左上锚点。
    private static void SetLeftTopFixed(UILayout? layout, float width, float height, float left)
    {
        layout?.SetAnchorMin(new vector2(0.0f, 0.5f));
        layout?.SetAnchorMax(new vector2(0.0f, 0.5f));
        layout?.SetPivot(new vector2(0.0f, 0.5f));
        layout?.SetOffset(new vector2(left, 0.0f));
        layout?.SetSizeDelta(new vector2(width, height));
    }
}
