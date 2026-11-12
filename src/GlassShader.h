#pragma once

// Material approach inspired by Atrium Desktop (MIT). See THIRD_PARTY_NOTICES.md.
inline constexpr char kGlassShader[] = R"hlsl(
cbuffer Params : register(b0) {
    float2 size; float2 roiSize;
    float2 origin; float radius; float strength;
    float frosted; float light; float ready; float dpi;
    float2 screenOrigin; float2 screenSize;
    float rotation; float3 padding;
};
Texture2D background : register(t0);
SamplerState linearClamp : register(s0);
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex VS(uint id : SV_VertexID) {
    Vertex v;
    v.uv = float2((id << 1) & 2, id & 2);
    v.position = float4(v.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return v;
}
float distanceToGlass(float2 p) {
    float2 q = abs(p - size * 0.5) - size * 0.5 + radius + 1;
    return length(max(q, 0)) + min(max(q.x, q.y), 0) - radius;
}
float3 sampleBackground(float2 p, float lod) {
    return background.SampleLevel(linearClamp, (origin + p) / roiSize, lod).rgb;
}
float4 CopyPS(Vertex v) : SV_Target {
    float2 uv = (screenOrigin + v.uv * roiSize) / screenSize;
    uv = clamp(uv, 0.5 / screenSize, 1 - 0.5 / screenSize);
    if (rotation == 2) uv = float2(uv.y, 1 - uv.x);
    else if (rotation == 3) uv = 1 - uv;
    else if (rotation == 4) uv = float2(1 - uv.y, uv.x);
    return background.SampleLevel(linearClamp, uv, 0);
}
float3 linearColor(float3 value) {
    return float3(value.r <= 0.04045 ? value.r / 12.92 : pow((value.r + 0.055) / 1.055, 2.4),
                  value.g <= 0.04045 ? value.g / 12.92 : pow((value.g + 0.055) / 1.055, 2.4),
                  value.b <= 0.04045 ? value.b / 12.92 : pow((value.b + 0.055) / 1.055, 2.4));
}
float3 displayColor(float3 value) {
    return float3(value.r <= 0.0031308 ? value.r * 12.92 : 1.055 * pow(value.r, 1.0 / 2.4) - 0.055,
                  value.g <= 0.0031308 ? value.g * 12.92 : 1.055 * pow(value.g, 1.0 / 2.4) - 0.055,
                  value.b <= 0.0031308 ? value.b * 12.92 : 1.055 * pow(value.b, 1.0 / 2.4) - 0.055);
}
float4 PS(Vertex v) : SV_Target {
    float2 p = v.uv * size;
    float d = distanceToGlass(p);
    float coverage = saturate(0.5 - d / max(fwidth(d), 0.5));
    if (coverage <= 0) return 0;
    float2 gradient = float2(distanceToGlass(p + float2(1, 0)) - distanceToGlass(p - float2(1, 0)),
                            distanceToGlass(p + float2(0, 1)) - distanceToGlass(p - float2(0, 1)));
    float2 normal = gradient / max(length(gradient), 0.001);
    float depth = max(-d, 0);
    float edge = lerp(24, 10, frosted) * dpi;
    float t = saturate(1 - depth / edge);
    float profile = 1 - sqrt(saturate(1 - t * t));
    float2 offset = -normal * lerp(30, 6, frosted) * strength * dpi * profile;
    float dispersion = lerp(0.30, 0.04, frosted) * strength;
    float3 sharp;
    sharp.r = sampleBackground(p + offset * (1 + dispersion), 0).r;
    sharp.g = sampleBackground(p + offset, 0).g;
    sharp.b = sampleBackground(p + offset * (1 - dispersion), 0).b;
    float lod = lerp(1.5, 5.5, frosted) * strength;
    float stepSize = exp2(lod) * 0.75;
    float3 soft = sampleBackground(p + offset, lod) * 0.4;
    soft += sampleBackground(p + offset + float2(stepSize, 0), lod) * 0.15;
    soft += sampleBackground(p + offset - float2(stepSize, 0), lod) * 0.15;
    soft += sampleBackground(p + offset + float2(0, stepSize), lod) * 0.15;
    soft += sampleBackground(p + offset - float2(0, stepSize), lod) * 0.15;
    float3 tint = lerp(float3(0.055, 0.075, 0.10), float3(0.965, 0.978, 1), light);
    float3 color = lerp(sharp, soft, saturate(strength * 4));
    color = lerp(color, tint, lerp(0.30, 0.68, frosted) * (0.55 + 0.45 * strength));
    if (ready < 0.5) color = tint;
    float lit = saturate(dot(normal, normalize(float2(-0.6, -0.8))));
    float rim = 1 - smoothstep(0, 1.6 * dpi, depth);
    float glow = saturate(1 - depth / (edge * 1.6));
    float highlight = lerp(0.8, 0.45, frosted) * strength;
    color += highlight * (rim * (0.22 + 0.78 * lit) + glow * glow * lit * 0.16);
    color *= 1 - 0.05 * t * (1 - lit) * strength;
    // A regular glass surface adapts luminance for content contrast. Keep
    // refraction/chroma, but never put dark ink over an arbitrarily dark image.
    float3 sceneLinear = linearColor(saturate(color));
    float luma = dot(sceneLinear, float3(0.2126, 0.7152, 0.0722));
    if (light > 0.5 && luma < 0.64) sceneLinear = lerp(sceneLinear, 1, (0.64 - luma) / (1 - luma));
    if (light < 0.5 && luma > 0.065) sceneLinear *= 0.065 / luma;
    color = displayColor(sceneLinear);
    if (light < 0.5) color += rim * highlight * (0.15 + 0.45 * lit);
    return float4(saturate(color) * coverage, coverage);
}
)hlsl";
