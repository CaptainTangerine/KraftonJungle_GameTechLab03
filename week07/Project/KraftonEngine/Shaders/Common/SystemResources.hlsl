#ifndef SYSTEM_RESOURCES_HLSL
#define SYSTEM_RESOURCES_HLSL

// ── System Textures ──
// Renderer가 패스 단위로 바인딩하는 프레임 공통 리소스.
// 슬롯 번호는 C++ ESystemTexSlot (RenderConstants.h)과 1:1 대응.

Texture2D<float>  SceneDepth  : register(t10);  // CopyResource된 Depth (R24_UNORM)
Texture2D<float4> SceneColor  : register(t11);  // CopyResource된 SceneColor (R8G8B8A8_UNORM)
Texture2D<float4> SceneNormal : register(t12);  // Pre-pass에서 렌더링된 Normal (R10G10B10A2_UNORM)
Texture2D<uint2>  StencilTex  : register(t13);  // CopyResource된 Stencil (X24_G8_UINT)
Texture2D<float4> SceneNormal_Noise : register(t14); // SSAO용 4x4 노이즈 텍스처

#endif // SYSTEM_RESOURCES_HLSL
