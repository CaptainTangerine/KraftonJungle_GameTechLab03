#pragma once

#include "Render/Types/RenderTypes.h"
#include "Render/Resource/Buffer.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"

class FSSAOProcessor
{
public:
    FSSAOProcessor() = default;
    ~FSSAOProcessor() { Release(); }

    FSSAOProcessor(const FSSAOProcessor&) = delete;
    FSSAOProcessor& operator=(const FSSAOProcessor&) = delete;

    void Initialize(ID3D11Device* InDevice);
    void Release();
    bool IsInitialized() const { return bInitialized; }

    void Execute(
        ID3D11DeviceContext* Ctx,
        ID3D11ShaderResourceView* DepthSRV,
        ID3D11ShaderResourceView* NormalSRV,
        ID3D11UnorderedAccessView* SSAOTargetUAV,
        ID3D11UnorderedAccessView* BlurTargetUAV,
        ID3D11ShaderResourceView* SSAOSRV, // For Blur input
        const FMatrix& View,
        const FMatrix& Projection,
        uint32 Width,
        uint32 Height);

private:
    void UpdateParamsCB(ID3D11DeviceContext* Ctx, const FMatrix& View, const FMatrix& Projection);

private:
    ID3D11Device* Device = nullptr;

    // Compute shaders
    ID3D11ComputeShader* SSAOCS = nullptr;
    ID3D11ComputeShader* BlurCS = nullptr;

    // Noise Texture (Managed internally)
    ID3D11Texture2D* NoiseTexture = nullptr;
    ID3D11ShaderResourceView* NoiseSRV = nullptr;

    // Sample Kernel
    TArray<FVector4> Kernel;

    // Constant buffer for SSAO parameters
    ID3D11Buffer* ParamsCB = nullptr;

    bool bInitialized = false;
};
