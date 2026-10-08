using System;
using Orbeden;

namespace OrbedenEditor;

/// <summary>图片检视面板与九宫格源图切分预览。</summary>
[CustomEditor(typeof(Image))]
public sealed class ImageEditor : ComponentEditor
{
    private int selectedBorder;

    /// <summary>绘制图片、九宫格边框与图形属性。</summary>
    public override void OnDrawInspector()
    {
        if (Target.Ens.GetComponent<Image>() is not Image image) return;
        DrawProperty("enabled");
        DrawProperty("texture");
        DrawProperty("tint");
        DrawProperty("mode");
        DrawProperty("uvMin");
        DrawProperty("uvMax");
        Texture2D? texture = image.GetTexture();
        if (texture == null) EditorGUI.TextWrapped("No texture: draws a solid color. Nine-slice needs a source image to show preserved corners.");
        else if (image.GetMode() == UIImageMode.NineSlice) DrawSliceEditor(image, texture);
        else EditorGUI.TextWrapped("Simple stretches the whole source region. Choose NineSlice to edit the four source borders.");
        EditorGUI.Separator();
        DrawProperty("raycastTarget");
        DrawProperty("material");
        if (image.GetMaterial() != null)
            EditorGUI.TextWrapped("Custom material must use a UI-compatible shader. Leave Material empty to use the built-in image shader; ordinary lit 3D materials are not suitable.");
    }

    //绘制源图切线并将像素边框写入属性事务
    private void DrawSliceEditor(Image image, Texture2D texture)
    {
        vector2 uvMin = image.GetUvMin();
        vector2 uvMax = image.GetUvMax();
        float width = MathF.Max(0, (uvMax.x - uvMin.x) * texture.width);
        float height = MathF.Max(0, (uvMax.y - uvMin.y) * texture.height);
        EditorGUI.Label($"Nine-slice Source: {width:0.#} x {height:0.#} px");
        EditorGUI.TextWrapped("Borders are source-image pixels, not layout margins. Corners keep their size; edges stretch along one axis and the center stretches along both.");
        string[] names = ["borderLeft", "borderRight", "borderBottom", "borderTop"];
        string[] labels = ["Left", "Right", "Bottom", "Top"];
        float[] borders = [image.GetBorderLeft(), image.GetBorderRight(), image.GetBorderBottom(), image.GetBorderTop()];
        for (int index = 0; index < 4; ++index)
        {
            float value = borders[index];
            if (EditorGUI.SliderFloat(labels[index] + " (px)", ref value, 0, index < 2 ? width : height))
                SetValue(names[index], InteropValue.From(value));
        }
        if (EditorGUI.Button("Split into Thirds"))
        {
            for (int index = 0; index < 4; ++index)
                SetValue(names[index], InteropValue.From((index < 2 ? width : height) / 3));
        }
        if (borders[0] + borders[1] + borders[2] + borders[3] == 0)
            EditorGUI.TextWrapped("All borders are zero: the result is identical to Simple. Set borders here, then resize the layout to see the difference.");
        if (borders[0] + borders[1] > width || borders[2] + borders[3] > height)
            EditorGUI.TextWrapped("Opposing borders exceed the source size; the renderer scales them down proportionally.");

        //选择切线并点击源图定位
        EditorGUI.Label("Click source image to place selected border");
        for (int index = 0; index < 4; ++index)
        {
            if (index > 0) EditorGUI.SameLine();
            if (EditorGUI.ToggleButton(labels[index] + "##slice", selectedBorder == index)) selectedBorder = index;
        }
        if (texture.width <= 0 || texture.height <= 0) return;
        float scale = 240.0f / Math.Max(texture.width, texture.height);
        vector2 size = new(texture.width * scale, texture.height * scale);
        vector2 origin = EditorGUI.CursorScreenPosition;
        EditorGUI.DrawTexture(texture, size);
        if (EditorGUI.IsItemClicked())
        {
            vector2 mouse = EditorGUI.MousePosition;
            float x = (mouse.x - origin.x) / scale;
            float y = texture.height - (mouse.y - origin.y) / scale;
            float value = selectedBorder switch
            {
                0 => x - uvMin.x * texture.width,
                1 => uvMax.x * texture.width - x,
                2 => y - uvMin.y * texture.height,
                _ => uvMax.y * texture.height - y,
            };
            SetValue(names[selectedBorder], InteropValue.From(Math.Clamp(value, 0, selectedBorder < 2 ? width : height)));
        }

        //复用渲染切分公式标出实际九宫格边界
        Span<float> target = stackalloc float[4];
        Span<float> xs = stackalloc float[4];
        Span<float> ys = stackalloc float[4];
        Image.ComputeNineSliceAxis(0, width, uvMin.x, uvMax.x - uvMin.x, texture.width, borders[0], borders[1], target, xs);
        Image.ComputeNineSliceAxis(0, height, uvMin.y, uvMax.y - uvMin.y, texture.height, borders[2], borders[3], target, ys);
        color tint = new(0.3f, 1, 0.4f, 1);
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
                EditorGUI.DrawRectOutline(new vector2(origin.x + xs[column] * size.x, origin.y + (1 - ys[row + 1]) * size.y),
                    new vector2(origin.x + xs[column + 1] * size.x, origin.y + (1 - ys[row]) * size.y), tint, 1);
    }
}
