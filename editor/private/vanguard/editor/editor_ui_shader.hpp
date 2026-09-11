#pragma once

namespace vanguard::editor::detail
{
    // Built-in editor shader, compiled once with the existing shader-tools compiler.
    // Panel images supply display-ready content; scene tone mapping is not UI work.
    inline constexpr char EditorUiShader[] = R"(
struct Constants { float2 scale; float2 translate; uint textureIndex; uint samplerIndex; uint linearInput; uint padding; };
[push_constant] ConstantBuffer<Constants> constants;
struct Input { float2 position : POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; float4 color : COLOR0; };
[shader("vertex")]
Vertex UiVertex(Input input)
{
    Vertex output;
    output.position = float4(input.position * constants.scale + constants.translate, 0, 1);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}
float EncodeSrgb(float value)
{
    return value <= 0.0031308 ? 12.92 * value : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}
[shader("fragment")]
float4 UiFragment(Vertex input) : SV_Target0
{
    Texture2D<float4> image = ResourceDescriptorHeap[constants.textureIndex];
    SamplerState imageSampler = SamplerDescriptorHeap[constants.samplerIndex];
    float4 sampled = image.Sample(imageSampler, input.uv);
    if (constants.linearInput != 0)
    {
        float3 rgb = max(sampled.rgb, 0.0);
        sampled.rgb = float3(EncodeSrgb(rgb.r), EncodeSrgb(rgb.g), EncodeSrgb(rgb.b));
    }
    return input.color * sampled;
}
)";
}
