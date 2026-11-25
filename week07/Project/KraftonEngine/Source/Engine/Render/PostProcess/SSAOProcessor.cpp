#include "SSAOProcessor.h"
#include <d3dcompiler.h>
#include <random>
#include <cmath>
#include "Render/Pipeline/RenderConstants.h"

#pragma comment(lib, "d3dcompiler.lib")

static ID3D11ComputeShader* CompileCS(ID3D11Device* Dev, const wchar_t* Path, const char* Entry)
{
    ID3DBlob* csBlob = nullptr;
    ID3DBlob* errBlob = nullptr;

    HRESULT hr = D3DCompileFromFile(Path, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        Entry, "cs_5_0", 0, 0, &csBlob, &errBlob);

    if (FAILED(hr))
    {
        if (errBlob)
        {
            const char* errMsg = (const char*)errBlob->GetBufferPointer();
            OutputDebugStringA(errMsg);
            MessageBoxA(nullptr, errMsg, "CS Compile Error", MB_OK | MB_ICONERROR);
            errBlob->Release();
        }
        else
        {
        	MessageBoxA(nullptr, "File not found or unknown error.", "CS Compile Error", MB_OK | MB_ICONERROR);
        }
        return nullptr;
    }

    ID3D11ComputeShader* cs = nullptr;
    hr = Dev->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &cs);
    csBlob->Release();

    return SUCCEEDED(hr) ? cs : nullptr;
}

void FSSAOProcessor::Initialize(ID3D11Device* InDevice)
{
    Device = InDevice;

    // 1. Compile Compute Shaders
    SSAOCS = CompileCS(Device, L"Shaders/SSAO_CS.hlsl", "CSMain");
    BlurCS = CompileCS(Device, L"Shaders/SSAOBlur_CS.hlsl", "CSMain");

    if (!SSAOCS || !BlurCS) return;

    // 2. Generate Sample Kernel
    std::uniform_real_distribution<float> randomFloats(0.0, 1.0);
    std::default_random_engine generator;

    Kernel.clear();
    for (uint32 i = 0; i < 64; ++i)
    {
        FVector sample(
            randomFloats(generator) * 2.0f - 1.0f,
            randomFloats(generator) * 2.0f - 1.0f,
            randomFloats(generator)
        );
        sample.Normalize();
        sample *= randomFloats(generator);

        float scale = (float)i / 64.0f;
        scale = 0.1f + scale * scale * (1.0f - 0.1f);
        sample *= scale;

        Kernel.push_back(FVector4(sample.X, sample.Y, sample.Z, 0.0f));
    }

    // 3. Create Noise Texture
    TArray<FVector4> noiseData;
    for (uint32 i = 0; i < 16; i++)
    {
        FVector noise(
            randomFloats(generator) * 2.0f - 1.0f,
            randomFloats(generator) * 2.0f - 1.0f,
            0.0f);
        noiseData.push_back(FVector4(noise.X, noise.Y, noise.Z, 0.0f));
    }

    D3D11_TEXTURE2D_DESC noiseDesc = {};
    noiseDesc.Width = 4;
    noiseDesc.Height = 4;
    noiseDesc.MipLevels = 1;
    noiseDesc.ArraySize = 1;
    noiseDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    noiseDesc.SampleDesc.Count = 1;
    noiseDesc.Usage = D3D11_USAGE_IMMUTABLE;
    noiseDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subData = {};
    subData.pSysMem = noiseData.data();
    subData.SysMemPitch = 4 * sizeof(FVector4);

    Device->CreateTexture2D(&noiseDesc, &subData, &NoiseTexture);
    Device->CreateShaderResourceView(NoiseTexture, nullptr, &NoiseSRV);

    // 4. Create Params Constant Buffer
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(FSSAOConstants);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    Device->CreateBuffer(&cbDesc, nullptr, &ParamsCB);

    bInitialized = (ParamsCB != nullptr);
}

void FSSAOProcessor::Release()
{
    if (ParamsCB) { ParamsCB->Release(); ParamsCB = nullptr; }
    if (NoiseSRV) { NoiseSRV->Release(); NoiseSRV = nullptr; }
    if (NoiseTexture) { NoiseTexture->Release(); NoiseTexture = nullptr; }
    if (SSAOCS) { SSAOCS->Release(); SSAOCS = nullptr; }
    if (BlurCS) { BlurCS->Release(); BlurCS = nullptr; }
    bInitialized = false;
}

void FSSAOProcessor::UpdateParamsCB(ID3D11DeviceContext* Ctx, const FMatrix& View, const FMatrix& Projection)
{
    D3D11_MAPPED_SUBRESOURCE Mapped = {};
    if (SUCCEEDED(Ctx->Map(ParamsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
    {
        FSSAOConstants* Params = (FSSAOConstants*)Mapped.pData;
        for (int i = 0; i < 64; ++i) Params->Samples[i] = Kernel[i];
        Params->Projection = Projection;
        Params->InvProjection = Projection.GetInverse();
        Params->View = View;
        Params->Radius = 0.5f;
        Params->Bias = 0.025f;
        Params->Intensity = 1.0f;
        Ctx->Unmap(ParamsCB, 0);
    }
}

void FSSAOProcessor::Execute(
    ID3D11DeviceContext* Ctx,
    ID3D11ShaderResourceView* DepthSRV,
    ID3D11ShaderResourceView* NormalSRV,
    ID3D11UnorderedAccessView* SSAOTargetUAV,
    ID3D11UnorderedAccessView* BlurTargetUAV,
    ID3D11ShaderResourceView* SSAOSRV,
    const FMatrix& View,
    const FMatrix& Projection,
    uint32 Width,
    uint32 Height)
{
    if (!bInitialized) return;

    UpdateParamsCB(Ctx, View, Projection);


    // Pass 1: SSAO Main CS
    {
        Ctx->CSSetShader(SSAOCS, nullptr, 0);
        Ctx->CSSetConstantBuffers(0, 1, &ParamsCB);

        // SystemResources.hlsl 슬롯에 맞춰 바인딩 (t10, t12, t14)
        Ctx->PSSetShaderResources(ESystemTexSlot::SceneDepth, 1, &DepthSRV);
        Ctx->PSSetShaderResources(ESystemTexSlot::SceneNormal, 1, &NormalSRV);
        Ctx->PSSetShaderResources(ESystemTexSlot::SSAO_Noise, 1, &NoiseSRV);

        // Compute Shader용 바인딩
        Ctx->CSSetShaderResources(ESystemTexSlot::SceneDepth, 1, &DepthSRV);
        Ctx->CSSetShaderResources(ESystemTexSlot::SceneNormal, 1, &NormalSRV);
        Ctx->CSSetShaderResources(ESystemTexSlot::SSAO_Noise, 1, &NoiseSRV);

        Ctx->CSSetUnorderedAccessViews(0, 1, &SSAOTargetUAV, nullptr);

        uint32 DispatchX = (Width + 15) / 16;
        uint32 DispatchY = (Height + 15) / 16;
        Ctx->Dispatch(DispatchX, DispatchY, 1);

        // Unbind
        ID3D11ShaderResourceView* NullSRV = nullptr;
        Ctx->CSSetShaderResources(ESystemTexSlot::SceneDepth, 1, &NullSRV);
        Ctx->CSSetShaderResources(ESystemTexSlot::SceneNormal, 1, &NullSRV);
        Ctx->CSSetShaderResources(ESystemTexSlot::SSAO_Noise, 1, &NullSRV);
        
        ID3D11UnorderedAccessView* NullUAV = nullptr;
        Ctx->CSSetUnorderedAccessViews(0, 1, &NullUAV, nullptr);
    }

    // Pass 2: SSAO Blur CS
    {
        Ctx->CSSetShader(BlurCS, nullptr, 0);
        
        // Bilateral Blur needs Depth to preserve edges
        Ctx->CSSetShaderResources(ESystemTexSlot::SceneDepth, 1, &DepthSRV);
        Ctx->CSSetShaderResources(0, 1, &SSAOSRV);
        Ctx->CSSetUnorderedAccessViews(0, 1, &BlurTargetUAV, nullptr);

        uint32 DispatchX = (Width + 15) / 16;
        uint32 DispatchY = (Height + 15) / 16;
        Ctx->Dispatch(DispatchX, DispatchY, 1);

        // Unbind
        ID3D11ShaderResourceView* NullSRV = nullptr;
        Ctx->CSSetShaderResources(ESystemTexSlot::SceneDepth, 1, &NullSRV);
        Ctx->CSSetShaderResources(0, 1, &NullSRV);
        ID3D11UnorderedAccessView* NullUAV = nullptr;
        Ctx->CSSetUnorderedAccessViews(0, 1, &NullUAV, nullptr);
    }

    Ctx->CSSetShader(nullptr, nullptr, 0);
}
