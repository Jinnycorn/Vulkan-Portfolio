#version 450

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;

layout(set = 0, binding = 0) uniform sampler2D fontSampler;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 texel = texture(fontSampler, inUV);

    // Solid ImGui primitives (windows, panels, buttons, tabs, sliders, etc.)
    // sample the atlas white pixel whose alpha is 1.0. Force those primitives
    // fully opaque so the editor never looks glassy even if a style color carries
    // an accidental sub-1 alpha. Font glyph edges keep their atlas alpha so text
    // remains properly anti-aliased.
    float alpha = texel.a >= 0.999 ? 1.0 : (inColor.a * texel.a);
    outColor = vec4(inColor.rgb * texel.rgb, alpha);
}
