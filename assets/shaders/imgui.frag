#version 450

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;

layout(set = 0, binding = 0) uniform sampler2D fontSampler;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 texel = texture(fontSampler, inUV);
    float alpha = texel.a >= 0.999 ? 1.0 : (inColor.a * texel.a);
    outColor = vec4(inColor.rgb * texel.rgb, alpha);
}
