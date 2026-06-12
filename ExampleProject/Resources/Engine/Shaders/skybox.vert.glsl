#version 430 core

layout(location = 0) in vec3 a_Position;

uniform mat4 u_ViewProjection;

out vec3 v_TexCoord;

void main()
{
    v_TexCoord = a_Position;
    vec4 position = u_ViewProjection * vec4(a_Position, 1.0);
    gl_Position = position.xyww;
}

