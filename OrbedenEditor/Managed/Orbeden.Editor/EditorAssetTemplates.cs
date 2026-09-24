namespace OrbedenEditor;

/// <summary>新建资源时写入的起始内容。集中在一处，面板只负责创建、命名与刷新。</summary>
internal static class EditorAssetTemplates
{
    /// <summary>最小可用的 OrbShader：不透明队列，一个可由材质赋值的颜色槽。</summary>
    internal static string Shader() => """
        --------queue Opaque

        --------vert
        #version 430 core

        layout(location = 0) in vec3 a_Position;

        uniform mat4 u_Model;
        uniform mat4 u_ViewProjection;

        void main()
        {
            gl_Position = u_ViewProjection * u_Model * vec4(a_Position, 1.0);
        }

        --------frag
        #version 430 core

        //不重名的 uniform 会自动成为材质槽位；引擎内建的那批（u_Model、u_ViewProjection 等）不会
        uniform vec4 u_Color;

        out vec4 FragColor;

        void main()
        {
            FragColor = u_Color;
        }
        """;

    /// <summary>整段注释掉的脚本示例：示例类名固定，取消注释时自己改成想要的名字。</summary>
    internal static string Script(string projectName) => $$"""
        //using Orbeden;
        //
        //namespace {{projectName}};
        //
        ///// <summary>示例脚本。</summary>
        //public sealed class ExampleScript : Script
        //{
        //    public float speed = 1.0f;
        //
        //    public ExampleScript(Ens ens) : base(ens) {}
        //
        //    private void OnUpdate(float deltaTime)
        //    {
        //    }
        //}
        """;

    /// <summary>默认材质：绑定内建的布林冯着色器，槽位留空由 Shader 声明的默认值补上。</summary>
    internal static string Material() => """
        #材质资产：材质名取自文件名。引用一律写内容根相对的 Key
        #槽位按 Shader 声明的 uniform 名书写，例如：
        #color u_BaseColor 0.78 0.78 0.78 1
        #float u_Shininess 32
        #texture u_MainTexture Textures/rock.png
        shader Builtin/Shaders/blinn_phong.orbshader
        drawqueue Auto
        """;

    /// <summary>把项目名整理成可用的 C# 命名空间标识符。</summary>
    internal static string NamespaceFromProject(string projectName)
    {
        string name = projectName.Trim();
        if (name.Length == 0) return "Game";

        string result = string.Empty;
        foreach (char character in name)
        {
            //命名空间按标识符规则收口：非字母数字一律换成下划线，首字符不能是数字
            result += char.IsLetterOrDigit(character) || character == '_' ? character : '_';
        }
        return char.IsDigit(result[0]) ? "_" + result : result;
    }
}
