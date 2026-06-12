#version 430 core

in vec3 v_WorldPosition;
in vec3 v_Normal;
in vec2 v_TexCoord;
in vec4 v_LightSpacePosition;

uniform vec3 u_CameraPosition;
uniform vec3 u_AmbientColor;
uniform vec3 u_MaterialAmbient;
uniform vec3 u_DiffuseColor;
uniform vec3 u_SpecularColor;
uniform float u_Shininess;
uniform vec3 u_LightDirection;
uniform vec3 u_LightColor;
uniform float u_LightIntensity;
uniform bool u_HasDiffuseTexture;
uniform sampler2D u_DiffuseTexture;
uniform sampler2D u_ShadowMap;
uniform bool u_UseShadowMap;
uniform bool u_ReceiveShadows;
uniform float u_ShadowBias;
uniform float u_ShadowStrength;

out vec4 FragColor;

float SampleShadow()
{
    if (!u_UseShadowMap || !u_ReceiveShadows)
    {
        return 0.0;
    }

    vec3 projected = v_LightSpacePosition.xyz / v_LightSpacePosition.w;
    projected = projected * 0.5 + 0.5;
    if (projected.x < 0.0 || projected.x > 1.0 || projected.y < 0.0 || projected.y > 1.0 || projected.z > 1.0)
    {
        return 0.0;
    }

    float closestDepth = texture(u_ShadowMap, projected.xy).r;
    float currentDepth = projected.z;
    return currentDepth - u_ShadowBias > closestDepth ? u_ShadowStrength : 0.0;
}

void main()
{
    vec3 albedo = u_DiffuseColor;
    if (u_HasDiffuseTexture)
    {
        albedo *= texture(u_DiffuseTexture, v_TexCoord).rgb;
    }

    vec3 normal = normalize(v_Normal);
    vec3 lightDir = normalize(-u_LightDirection);
    vec3 viewDir = normalize(u_CameraPosition - v_WorldPosition);
    vec3 halfDir = normalize(lightDir + viewDir);

    float diffuseTerm = max(dot(normal, lightDir), 0.0);
    float specularPower = max(u_Shininess, 1.0);
    float specularTerm = pow(max(dot(normal, halfDir), 0.0), specularPower);
    float shadow = SampleShadow();

    vec3 ambient = (u_AmbientColor + u_MaterialAmbient) * albedo;
    vec3 direct = (diffuseTerm * albedo + specularTerm * u_SpecularColor) * u_LightColor * u_LightIntensity;
    vec3 color = ambient + direct * (1.0 - shadow);
    FragColor = vec4(color, 1.0);
}

