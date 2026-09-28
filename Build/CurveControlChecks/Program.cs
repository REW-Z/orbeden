
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Orbeden;

unsafe class Program
{
    internal static uint Events, Identity = 1;
    internal static vector2 Mouse;
    internal static string? PressedButton, EditedField, MenuCommand;
    internal static float EditedValue, Wheel;
    internal static bool ChangeColor, MenuOpen;
    internal static color NewColor;
    internal static int Disabled, HandleMask;
    internal static float ViewMinimum, ViewMaximum;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static uint Canvas(byte* text, int len, vector2* samples, int count, color* colors, vector2* keys, int keyCount, vector2* handles, int handleMask, int selected, float viewMinimum, float viewMaximum, float height, vector2* mouse, float* wheel, uint* identity)
    {
        *identity = Identity; *mouse = Mouse; *wheel = 0;
        if (height == 0) return 0;
        HandleMask = handleMask;
        ViewMinimum = viewMinimum; ViewMaximum = viewMaximum;
        *wheel = Wheel;
        return Events;
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static byte Color(byte* text, int len, color* value)
    { if (!ChangeColor || Disabled > 0) return 0; *value = NewColor; ChangeColor = false; return 1; }
    static void Check(bool condition, string text) { if (!condition) throw new Exception(text); Console.WriteLine("PASS " + text); }
    static void Main()
    {
        GUI.InitializeCurveApi(new RuntimeGuiCurveApi { Canvas = &Canvas, ColorField = &Color });
        ParticleCurve curve = new() { keys = [new() { time=0, value=1 }, new() { time=1, value=1 }] };
        Events=2|8; Mouse=new(0, .5f);
        Check(!GUI.AnimationCurve("Size", "size", ref curve, 0) && curve.keys[0].value == 1, "press only previews");
        Events=8; Mouse=new(0,.8f); GUI.AnimationCurve("Size", "size", ref curve, 0);
        Check(curve.keys[0].value == 1, "drag does not mutate source array");
        Events=0;
        Check(GUI.AnimationCurve("Size", "size", ref curve, 0) && curve.keys[0].value > 1, "release commits value");
        float saved=curve.keys[0].value;
        Events=2|8; Mouse=new(0,.7f); GUI.AnimationCurve("Size", "size", ref curve, 0);
        Events=8; Mouse=new(0,.1f); GUI.AnimationCurve("Size", "size", ref curve, 0);
        Events=16; GUI.AnimationCurve("Size", "size", ref curve, 0);
        Check(curve.keys[0].value == saved, "Escape preserves source");
        Events=0; PressedButton="Add Key After Selected";
        Check(GUI.AnimationCurve("Size", "size", ref curve, 0) && curve.keys.Length==3, "add midpoint key");
        Events=0; GUI.AnimationCurve("Size", "size", ref curve, 0);
        Check(curve.keys[1].interpolation == ParticleCurveInterpolation.Cubic && curve.keys[1].outTangent != 0, "inserted key auto-smooths");
        Check(HandleMask == 3, "selected cubic key exposes both tangent handles");
        EditedField="Value"; EditedValue=3;
        Check(GUI.AnimationCurve("Size", "size", ref curve, 0) && curve.keys[1].value==3, "numeric edit retains selected key");
        EditedField="Time"; EditedValue=2; GUI.AnimationCurve("Size", "size", ref curve, 0);
        Check(curve.keys[1].time < 1 && curve.keys[2].time == 1, "numeric time preserves ordering and endpoint");
        PressedButton="Delete Selected Key"; GUI.AnimationCurve("Size", "size", ref curve, 0);
        Check(curve.keys.Length==2, "delete interior key");
        //Delete 键与右键：都只删内部关键帧，端点留给曲线结构
        Identity=3;
        ParticleCurve editable = new() { keys = [new() { time=0, value=1 }, new() { time=.5f, value=3 }, new() { time=1, value=1 }] };
        Events=0; PressedButton="Add Key After Selected"; GUI.AnimationCurve("Size", "editable", ref editable, 0);
        Events=64; GUI.AnimationCurve("Size", "editable", ref editable, 0);
        Check(editable.keys.Length==3, "Delete key removes the selected interior key");
        Events=64; GUI.AnimationCurve("Size", "editable", ref editable, 0);
        Check(editable.keys.Length==3, "endpoint keys stay");
        //右键只开菜单，删除要经过菜单项；值域的归一化位置从上一帧画布拿到的视图算
        Identity=4;
        ParticleCurve picked = new() { keys = [new() { time=0, value=1 }, new() { time=.5f, value=3 }, new() { time=1, value=1 }] };
        Events=0; GUI.AnimationCurve("Size", "picked", ref picked, 0);
        float middleY = (3.0f - ViewMinimum) / (ViewMaximum - ViewMinimum);
        Events=32; Mouse=new(.5f, middleY); GUI.AnimationCurve("Size", "picked", ref picked, 0);
        Check(picked.keys.Length==3, "right-click alone does not delete");
        MenuOpen=true; MenuCommand="Delete Key";
        Check(GUI.AnimationCurve("Size", "picked", ref picked, 0) && picked.keys.Length==2, "key menu deletes the key");
        MenuOpen=false; Events=0;
        //菜单里换点类型
        MenuOpen=true; MenuCommand="Cubic";
        Check(GUI.AnimationCurve("Size", "picked", ref picked, 0)
            && picked.keys[1].interpolation==ParticleCurveInterpolation.Cubic, "key menu sets the point type");
        MenuOpen=false; Events=0;
        //滚轮缩放值轴：视图变化会写回给下一帧的画布，"Fit View" 再回到拟合范围
        Identity=6;
        ParticleCurve zoomed = new() { keys = [new() { time=0, value=1 }, new() { time=1, value=1 }] };
        GUI.AnimationCurve("Size", "zoomed", ref zoomed, 0);
        (float fitMinimum, float fitMaximum) = (ViewMinimum, ViewMaximum);
        Wheel=1.0f; Mouse=new(.5f,.5f);
        GUI.AnimationCurve("Size", "zoomed", ref zoomed, 0);
        Wheel=0.0f;
        //滚轮在下一帧才换算进视图
        GUI.AnimationCurve("Size", "zoomed", ref zoomed, 0);
        Check(ViewMaximum - ViewMinimum < fitMaximum - fitMinimum, "wheel zooms the value axis");
        Events=32; Mouse=new(.5f,.1f);
        MenuOpen=true; MenuCommand="Fit View";
        GUI.AnimationCurve("Size", "zoomed", ref zoomed, 0);
        MenuOpen=false; Events=0;
        GUI.AnimationCurve("Size", "zoomed", ref zoomed, 0);
        Check(MathF.Abs(ViewMinimum-fitMinimum)<1.0e-4f && MathF.Abs(ViewMaximum-fitMaximum)<1.0e-4f, "Fit View restores the fitted range");
        //值域由调用方限定：视图不越出，编辑也夹在里面
        Identity=7;
        ParticleCurve limited = new() { keys = [new() { time=0, value=1 }, new() { time=1, value=3 }] };
        Events=0; Check(!GUI.AnimationCurve("Size", "limited", ref limited, 0.5f, 2.0f), "view clamps into the allowed range");
        Check(ViewMinimum>=0.5f && ViewMaximum<=2.0f, "value axis stays inside the allowed range");
        //切线手柄：按下由原生侧按像素判定，128 = 入手柄、256 = 出手柄
        Identity=5;
        ParticleCurve shaped = new() { keys = [
            new() { time=0, value=1 },
            new() { time=.5f, value=1, interpolation=ParticleCurveInterpolation.Cubic },
            new() { time=1, value=1 }] };
        Events=2|8; Mouse=new(.5f,.5f); GUI.AnimationCurve("Size", "shaped", ref shaped, 0);
        Events=0; GUI.AnimationCurve("Size", "shaped", ref shaped, 0);
        Check(HandleMask==3, "middle key of three exposes both handles");
        Events=2|8|256; Mouse=new(.6f,.5f); GUI.AnimationCurve("Size", "shaped", ref shaped, 0);
        Events=8; Mouse=new(.75f,.75f); GUI.AnimationCurve("Size", "shaped", ref shaped, 0);
        Events=0;
        Check(GUI.AnimationCurve("Size", "shaped", ref shaped, 0) && MathF.Abs(shaped.keys[1].outTangent-1.0f)<.001f, "handle drag writes the tangent");
        Identity=2;
        ParticleGradient gradient = new() { keys = [new() { time=0, value=new(){r=1,g=1,b=1,a=1}},new(){time=1,value=new(){r=1,g=1,b=1,a=1}}] };
        ChangeColor=true; NewColor=new(){r=1,g=0,b=0,a=.25f};
        Check(GUI.ColorGradient("Color", "color", ref gradient) && gradient.keys[0].value.a==.25f && gradient.keys[0].value.g==0, "RGBA picker updates selected gradient key");
        Events=32; Mouse=new(0f,.5f); GUI.ColorGradient("Color", "color", ref gradient);
        MenuOpen=true; MenuCommand="Add Key After";
        Check(GUI.ColorGradient("Color", "color", ref gradient) && gradient.keys.Length==3, "gradient adds sampled midpoint");
        MenuOpen=false;
        Check(MathF.Abs(gradient.keys[1].value.a-.625f)<.0001f, "gradient interpolation preserves alpha");
        Events=2|8; Mouse=new(.5f,.5f); GUI.ColorGradient("Color", "color", ref gradient);
        Events=8; Mouse=new(.7f,.5f); GUI.ColorGradient("Color", "color", ref gradient);
        Check(gradient.keys[1].time==.5f, "gradient drag remains preview");
        Events=0;
        Check(GUI.ColorGradient("Color", "color", ref gradient) && gradient.keys[1].time==.7f, "gradient release commits once");
        Check(!GUI.ColorGradient("Color", "color", ref gradient), "idle frame creates no edit");
        Check(Disabled==0, "disabled scopes balanced");
    }
}
namespace Orbeden
{
    public static unsafe partial class GUI
    {
        public static void Label(string label) { }
        public static void BeginDisabled(bool disabled=true) { if(disabled) ++Program.Disabled; disableStack.Push(disabled); }
        private static Stack<bool> disableStack=[];
        public static void EndDisabled() { if(disableStack.Pop()) --Program.Disabled; }
        public static bool BeginCombo(string label,string preview)=>false;
        public static void EndCombo() { }
        public static bool Selectable(string text,bool selected=false)=>false;
        public static bool BeginPopupContextItem(string id)=>Program.MenuOpen;
        public static void EndPopup() { }
        public static void Separator() { }
        public static bool MenuItem(string label,bool enabled=true)
        {
            if(!Program.MenuOpen || Program.Disabled>0 || Program.MenuCommand==null || !label.StartsWith(Program.MenuCommand, StringComparison.Ordinal)) return false;
            Program.MenuCommand=null;return true;
        }
        public static bool Button(string label)
        {
            if(Program.Disabled>0 || Program.PressedButton==null || !label.StartsWith(Program.PressedButton+"##")) return false;
            Program.PressedButton=null;return true;
        }
        public static bool InputFloat(string label, ref float value)
        {
            if(Program.Disabled>0 || Program.EditedField==null || !label.StartsWith(Program.EditedField+"##")) return false;
            value=Program.EditedValue;Program.EditedField=null;return true;
        }
    }
}
