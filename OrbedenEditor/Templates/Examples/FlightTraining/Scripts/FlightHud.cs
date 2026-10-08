using Orbeden;

namespace FlightTraining;

/// <summary>自由飞行 HUD；用 Runtime GUI 自由绘制 API 在屏幕底部绘制 PFD 和仪表盘。</summary>
public sealed class FlightHud : Script
{
    private Native.FlightController? controller;

    private const float WindowHeight = 232.0f;
    private const float PixelsPerDegree = 3.0f;
    private const float DegToRad = MathF.PI / 180.0f;

    private static readonly uint ColorWhite = GUI.Rgba(240, 240, 240);
    private static readonly uint ColorGrey = GUI.Rgba(140, 145, 155);
    private static readonly uint ColorYellow = GUI.Rgba(235, 205, 60);
    private static readonly uint ColorRed = GUI.Rgba(220, 70, 70);
    private static readonly uint ColorSky = GUI.Rgba(40, 95, 195);
    private static readonly uint ColorGround = GUI.Rgba(126, 84, 46);
    private static readonly uint ColorDark = GUI.Rgba(22, 25, 32, 235);

    /// <summary>创建飞行 HUD。</summary>
    public FlightHud(Ens ens) : base(ens) {}

    private void OnStart()
    {
        controller = Ens.GetComponent<Native.FlightController>();
    }

    private void OnDrawGUI()
    {
        vector2 viewport = GUI.GetViewportSize();
        if (viewport.x <= 0.0f || viewport.y <= 0.0f) return;

        float width = MathF.Min(viewport.x, 900.0f);
        float x = (viewport.x - width) * 0.5f;
        float y = viewport.y - WindowHeight - 6.0f;
        GUI.BeginFixedWindow("FlightHud", x, y, width, WindowHeight);

        if (controller == null || !controller.IsValid)
        {
            GUI.Text("FlightController is unavailable. Build Game C++.", 12.0f, 12.0f, ColorRed);
            GUI.EndFixedWindow();
            return;
        }

        float airspeed = controller.GetAirspeed();
        float throttle = controller.GetThrottle();
        int crashCount = controller.GetCrashCount();
        float pitch = controller.GetPitchDegrees();
        float roll = controller.GetRollDegrees();
        float heading = controller.GetHeadingDegrees();
        float altitude = controller.GetAltitude();

        DrawStatusLine(controller.GetClimbRate(), controller.GetSideslipDegrees(), crashCount);
        DrawAirspeedDial(airspeed);
        DrawThrottleDial(width, throttle);
        DrawPfd(width, airspeed, altitude, heading, pitch, roll);

        //四色数值与世界空间箭头对应，单位为 kN。
        GUI.Text($"DRAG {controller.GetDragNewtons() / 1000:F1} kN", 12, 202, ColorRed, 0.7f);
        GUI.Text($"LIFT {controller.GetLiftNewtons() / 1000:F1} kN", width * 0.25f, 202, GUI.Rgba(60, 130, 255), 0.7f);
        GUI.Text($"THRUST {controller.GetThrustNewtons() / 1000:F1} kN", width * 0.5f, 202, ColorYellow, 0.7f);
        GUI.RectFilled(width * 0.75f - 4, 199, width - 8, 222, GUI.Rgba(205, 205, 205), 3);
        GUI.Text($"WEIGHT {controller.GetWeightNewtons() / 1000:F1} kN", width * 0.75f, 202, GUI.Rgba(0, 0, 0), 0.7f);
        GUI.EndFixedWindow();
    }

    /// <summary>顶部状态与按键提示行。</summary>
    private void DrawStatusLine(float climbRate, float sideslip, int crashCount)
    {
        GUI.Text($"FREE FLIGHT   V/S {climbRate:+0.0;-0.0;0.0} m/s   SLIP {sideslip:F1} deg   CRASHES {crashCount}",
            12, 6, ColorWhite, 0.75f);
        GUI.Text("W/S PITCH   A/D ROLL   Q/E YAW   SHIFT/CTRL THROTTLE   R RESET   F FORCES   RMB ORBIT   C VIEW", 12, 26, ColorGrey, 0.6f);
    }

    /// <summary>空速表盘。</summary>
    private void DrawAirspeedDial(float airspeed)
    {
        const float cx = 86.0f;
        const float cy = 118.0f;
        const float radius = 56.0f;
        DrawDialFace(cx, cy, radius);
        DrawDialArc(cx, cy, radius, 0.0f, 80.0f);
        DrawDialNeedle(cx, cy, radius, airspeed, 0.0f, 80.0f);

        DrawCenteredText("AIRSPEED", cx, cy - radius - 8.0f, ColorGrey, 0.62f);
        DrawCenteredText($"{airspeed:F0}", cx, cy + 10.0f, ColorWhite, 1.1f);
        DrawCenteredText("m/s", cx, cy + 27.0f, ColorGrey, 0.55f);
    }

    /// <summary>油门表盘。</summary>
    private void DrawThrottleDial(float width, float throttle)
    {
        float cx = width - 86.0f;
        const float cy = 118.0f;
        const float radius = 56.0f;
        DrawDialFace(cx, cy, radius);
        DrawDialArc(cx, cy, radius, 0.0f, 100.0f);
        DrawDialNeedle(cx, cy, radius, throttle * 100.0f, 0.0f, 100.0f);

        DrawCenteredText("THROTTLE", cx, cy - radius - 8.0f, ColorGrey, 0.62f);
        DrawCenteredText($"{throttle * 100.0f:F0}", cx, cy + 10.0f, ColorWhite, 1.1f);
        DrawCenteredText("%", cx, cy + 27.0f, ColorGrey, 0.55f);
    }

    /// <summary>仪表盘面与刻度弧。</summary>
    private void DrawDialFace(float cx, float cy, float radius)
    {
        GUI.CircleFilled(cx, cy, radius, ColorDark, 48);
        GUI.Circle(cx, cy, radius, ColorGrey, 2.0f, 48);
    }

    private void DrawDialArc(float cx, float cy, float radius, float minimum, float maximum)
    {
        const float startDegrees = 135.0f;
        const float sweepDegrees = 270.0f;
        float startRad = startDegrees * DegToRad;
        GUI.Arc(cx, cy, radius - 4.0f, startRad, (startDegrees + sweepDegrees) * DegToRad, ColorGrey, 2.5f, 56);

        float span = maximum - minimum;
        int minorSteps = (int)MathF.Round(span / 5.0f);
        for (int step = 0; step <= minorSteps; step++)
        {
            float value = minimum + step * 5.0f;
            bool major = step % 2 == 0;
            float angle = (startDegrees + sweepDegrees * (value - minimum) / span) * DegToRad;
            float inner = major ? radius - 18.0f : radius - 13.0f;
            float outer = radius - 8.0f;
            GUI.Line(cx + MathF.Cos(angle) * inner, cy + MathF.Sin(angle) * inner,
                cx + MathF.Cos(angle) * outer, cy + MathF.Sin(angle) * outer,
                major ? ColorWhite : ColorGrey, major ? 2.0f : 1.0f);

            if (major)
            {
                string label = $"{value:F0}";
                vector2 size = GUI.GetTextSize(label, 0.6f);
                float labelRadius = radius - 28.0f;
                GUI.Text(label, cx + MathF.Cos(angle) * labelRadius - size.x * 0.5f,
                    cy + MathF.Sin(angle) * labelRadius - size.y * 0.5f, ColorGrey, 0.6f);
            }
        }
    }

    private void DrawDialNeedle(float cx, float cy, float radius, float value, float minimum, float maximum)
    {
        const float startDegrees = 135.0f;
        const float sweepDegrees = 270.0f;
        float clamped = MathF.Max(minimum, MathF.Min(maximum, value));
        float span = maximum - minimum;
        float angle = (startDegrees + sweepDegrees * (clamped - minimum) / span) * DegToRad;
        GUI.Line(cx, cy, cx + MathF.Cos(angle) * (radius - 16.0f), cy + MathF.Sin(angle) * (radius - 16.0f), ColorRed, 2.5f);
        GUI.CircleFilled(cx, cy, 5.0f, ColorGrey, 16);
    }

    /// <summary>中央 PFD：姿态仪 + 速度带 + 高度带 + 航向带。</summary>
    private void DrawPfd(float width, float airspeed, float altitude, float heading, float pitch, float roll)
    {
        float cx = width * 0.5f;
        const float cy = 118.0f;
        const float attitudeRadius = 62.0f;
        float frameHalf = attitudeRadius + 14.0f;
        const float tapeWidth = 34.0f;
        const float tapeGap = 8.0f;

        GUI.RectFilled(cx - frameHalf, cy - frameHalf, cx + frameHalf, cy + frameHalf, ColorDark, 8.0f);
        GUI.Rect(cx - frameHalf, cy - frameHalf, cx + frameHalf, cy + frameHalf, ColorGrey, 1.5f, 8.0f);

        DrawAttitude(cx, cy, attitudeRadius, pitch, roll);

        float speedTapeX1 = cx - frameHalf - tapeGap;
        DrawSpeedTape(speedTapeX1 - tapeWidth, speedTapeX1, cy - attitudeRadius, cy + attitudeRadius, airspeed);
        float altitudeTapeX0 = cx + frameHalf + tapeGap;
        DrawAltitudeTape(altitudeTapeX0, altitudeTapeX0 + tapeWidth, cy - attitudeRadius, cy + attitudeRadius, altitude);

        DrawHeadingTape(cx, cy - frameHalf - 24.0f, heading);
    }

    /// <summary>姿态仪：旋转地平线、天空/大地、俯仰梯和倾角指示。</summary>
    private void DrawAttitude(float cx, float cy, float radius, float pitch, float roll)
    {
        GUI.PushClipRect(cx - radius, cy - radius, cx + radius, cy + radius);

        GUI.CircleFilled(cx, cy, radius, ColorGround, 48);

        float rollRad = roll * DegToRad;
        float horizonAngle = -rollRad;
        float cosAngle = MathF.Cos(horizonAngle);
        float sinAngle = MathF.Sin(horizonAngle);
        //地平线方向 u 与"抬头"方向 n（y 向下的屏幕坐标）。
        float ux = cosAngle;
        float uy = sinAngle;
        float nx = -sinAngle;
        float ny = cosAngle;
        float horizonOffset = pitch * PixelsPerDegree;
        float hx = cx + nx * horizonOffset;
        float hy = cy + ny * horizonOffset;

        float halfLength = radius * 2.0f + 40.0f;
        float ax = hx + ux * halfLength;
        float ay = hy + uy * halfLength;
        float bx = hx - ux * halfLength;
        float by = hy - uy * halfLength;

        //天空三角形与地平线。
        GUI.TriangleFilled(ax, ay, bx, by, hx - nx * halfLength, hy - ny * halfLength, ColorSky);
        GUI.Line(ax, ay, bx, by, ColorWhite, 2.0f);

        //俯仰刻度梯。
        for (int pitchValue = -30; pitchValue <= 30; pitchValue += 5)
        {
            if (pitchValue == 0) continue;

            float offset = (pitchValue - pitch) * PixelsPerDegree;
            float barX = cx + nx * offset;
            float barY = cy + ny * offset;
            float half = 14.0f + 10.0f * (1.0f - MathF.Min(MathF.Abs(pitchValue - pitch) / 30.0f, 1.0f));
            GUI.Line(barX - ux * half, barY - uy * half, barX + ux * half, barY + uy * half, ColorWhite, 2.0f);

            if (pitchValue % 10 == 0)
            {
                string label = MathF.Abs(pitchValue).ToString();
                float labelX = barX + ux * (half + 9.0f);
                float labelY = barY + uy * (half + 9.0f);
                vector2 size = GUI.GetTextSize(label, 0.55f);
                GUI.Text(label, labelX - size.x * 0.5f, labelY - size.y * 0.5f, ColorWhite, 0.55f);
            }
        }

        GUI.PopClipRect();

        //倾角刻度（固定）与滚转指针（随飞机转动）。
        for (int bank = -45; bank <= 45; bank += 15)
        {
            float angle = bank * DegToRad;
            float dirX = MathF.Sin(angle);
            float dirY = -MathF.Cos(angle);
            GUI.Line(cx + dirX * (radius + 2.0f), cy + dirY * (radius + 2.0f),
                cx + dirX * (radius + 8.0f), cy + dirY * (radius + 8.0f), ColorGrey, 1.5f);
        }
        float pointerX = MathF.Sin(rollRad);
        float pointerY = -MathF.Cos(rollRad);
        GUI.Line(cx + pointerX * (radius + 4.0f), cy + pointerY * (radius + 4.0f),
            cx + pointerX * (radius + 12.0f), cy + pointerY * (radius + 12.0f), ColorYellow, 3.0f);

        //机首参考符号（固定）。
        GUI.Line(cx - 28.0f, cy, cx - 9.0f, cy, ColorYellow, 3.0f);
        GUI.Line(cx + 9.0f, cy, cx + 28.0f, cy, ColorYellow, 3.0f);
        GUI.TriangleFilled(cx - 4.5f, cy + 4.0f, cx + 4.5f, cy + 4.0f, cx, cy - 14.0f, ColorYellow);
    }

    /// <summary>左侧速度带。</summary>
    private void DrawSpeedTape(float x0, float x1, float y0, float y1, float airspeed)
    {
        GUI.RectFilled(x0, y0, x1, y1, ColorDark, 4.0f);
        GUI.PushClipRect(x0, y0, x1, y1);

        const float pixelsPerUnit = 2.6f;
        float centerY = (y0 + y1) * 0.5f;
        int start = (int)MathF.Floor(airspeed / 5.0f) * 5 - 15;
        for (int value = start; value <= start + 45; value += 5)
        {
            if (value < 0) continue;
            float tickY = centerY - (value - airspeed) * pixelsPerUnit;
            bool major = value % 10 == 0;
            GUI.Line(x0 + 2.0f, tickY, x1 - (major ? 16.0f : 6.0f), tickY, major ? ColorWhite : ColorGrey, major ? 2.0f : 1.0f);
            if (major)
            {
                vector2 size = GUI.GetTextSize(value.ToString(), 0.6f);
                GUI.Text(value.ToString(), x1 - 13.0f - size.x, tickY - size.y * 0.5f, ColorWhite, 0.6f);
            }
        }

        GUI.TriangleFilled(x0 + 2.0f, centerY - 5.0f, x0 + 2.0f, centerY + 5.0f, x0 + 9.0f, centerY, ColorYellow);
        GUI.PopClipRect();
    }

    /// <summary>右侧高度带。</summary>
    private void DrawAltitudeTape(float x0, float x1, float y0, float y1, float altitude)
    {
        GUI.RectFilled(x0, y0, x1, y1, ColorDark, 4.0f);
        GUI.PushClipRect(x0, y0, x1, y1);

        const float pixelsPerUnit = 1.1f;
        float centerY = (y0 + y1) * 0.5f;
        int start = (int)MathF.Floor(altitude / 10.0f) * 10 - 50;
        for (int value = start; value <= start + 150; value += 10)
        {
            if (value < 0) continue;
            float tickY = centerY - (value - altitude) * pixelsPerUnit;
            bool major = value % 20 == 0;
            GUI.Line(x0 + (major ? 2.0f : 8.0f), tickY, x1 - 2.0f, tickY, major ? ColorWhite : ColorGrey, major ? 2.0f : 1.0f);
            if (major)
            {
                vector2 size = GUI.GetTextSize(value.ToString(), 0.6f);
                GUI.Text(value.ToString(), x0 + 6.0f, tickY - size.y * 0.5f, ColorWhite, 0.6f);
            }
        }

        GUI.TriangleFilled(x1 - 9.0f, centerY - 5.0f, x1 - 9.0f, centerY + 5.0f, x1 - 2.0f, centerY, ColorYellow);
        GUI.PopClipRect();
    }

    /// <summary>PFD 顶部航向带。</summary>
    private void DrawHeadingTape(float cx, float tapeY, float heading)
    {
        const float halfWidth = 64.0f;
        const float tapeHeight = 14.0f;
        float x0 = cx - halfWidth;
        float x1 = cx + halfWidth;
        float y1 = tapeY + tapeHeight;

        GUI.RectFilled(x0, tapeY, x1, y1, ColorDark, 3.0f);
        GUI.PushClipRect(x0, tapeY, x1, y1);

        const float pixelsPerDegree = 0.32f;
        float wrapped = ((heading % 360.0f) + 360.0f) % 360.0f;
        int start = (int)MathF.Floor((wrapped - 180.0f) / 10.0f) * 10;
        for (int value = start; value <= start + 360; value += 10)
        {
            float delta = value - wrapped;
            if (delta < -180.0f) delta += 360.0f;
            if (delta > 180.0f) delta -= 360.0f;
            float tickX = cx + delta * pixelsPerDegree;
            if (tickX < x0 - 20.0f || tickX > x1 + 20.0f) continue;

            bool major = ((value % 30) + 30) % 30 == 0;
            GUI.Line(tickX, tapeY + (major ? 0.0f : 3.0f), tickX, y1, major ? ColorWhite : ColorGrey, major ? 2.0f : 1.0f);
            if (major)
            {
                int degrees = ((value % 360) + 360) % 360;
                string label = (degrees / 10).ToString("D2");
                vector2 size = GUI.GetTextSize(label, 0.55f);
                GUI.Text(label, tickX - size.x * 0.5f, tapeY + 1.0f, ColorWhite, 0.55f);
            }
        }

        GUI.PopClipRect();

        //中心游标画在裁剪区外。
        GUI.TriangleFilled(cx - 5.0f, tapeY - 6.0f, cx + 5.0f, tapeY - 6.0f, cx, tapeY - 1.0f, ColorYellow);
    }

    private static void DrawCenteredText(string text, float cx, float y, uint color, float fontScale)
    {
        vector2 size = GUI.GetTextSize(text, fontScale);
        GUI.Text(text, cx - size.x * 0.5f, y - size.y * 0.5f, color, fontScale);
    }
}
