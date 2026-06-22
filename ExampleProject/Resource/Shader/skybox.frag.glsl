#version 430 core

in vec3 v_TexCoord;

uniform samplerCube u_SkyboxTexture;

out vec4 FragColor;

void main()
{
    FragColor = texture(u_SkyboxTexture, v_TexCoord);
}
