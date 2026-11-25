#pragma once

#include "Core/Singleton.h"
#include "Render/Resource/Shader.h"
#include "Render/Resource/ShaderVarient.h"
#include "Core/CoreTypes.h"
#include <memory>

enum class EShaderType : uint32
{
	Default = 0,
	Primitive,
	Gizmo,
	Editor,
	StaticMesh,
	UberLit,
	Decal,
	OutlinePostProcess,
	Font,
	OverlayFont,
	SubUV,
	Billboard,
	HeightFog,
	SceneDepth,
	SceneNormal,
	FXAA,
	SSAOApply,
	Heatmap,
	LightComplexity,
	LightInfluence,
	GodRayOcclusion,
	GodRay,
	LensFlare,
	MAX,
};

class FShaderManager : public TSingleton<FShaderManager>
{
	friend class TSingleton<FShaderManager>;

public:
	void Initialize(ID3D11Device* InDevice);
	void Release();

	// Hot reload: Shaders/ 디렉토리의 .hlsl 파일 변경 감지 시 전체 셰이더 재컴파일
	void PollHotReload(ID3D11Device* InDevice);

	FShader* GetShader(EShaderType InType);
	FShader* GetCustomShader(const FString& Key);

	template<typename T>
	FShader* GetUberShader(const T& VarientKey);
	template<typename T>
	void WarmUpUberShader(ID3D11Device* InDevice, const T& VarientKey);

	FShader* CreateCustomShader(ID3D11Device* InDevice, const wchar_t* InFilePath);

private:
	FShaderManager() = default;

	//가능한 메크로 조합의 모든 Shader 컴파일
	void CompileUberLitShader(ID3D11Device* Device);

	FShader Shaders[(uint32)EShaderType::MAX];
	TMap<FString, std::unique_ptr<FShader>> CustomShaderCache; // 커스텀 셰이더 캐시 (경로 → 셰이더)
	TMap<int32, std::unique_ptr<FShader>> UberShaderCache; //우버 셰이더 캐시 (Varientkey -> 셰이더)

	bool bIsInitialized = false;

	// Hot reload 상태
	uint64 LastPollTickMs   = 0;
	int64  ShadersDirMaxMtime = 0; // filesystem::file_time_type::rep
};

template<typename T>
inline FShader* FShaderManager::GetUberShader(const T& VarientKey)
{
	uint32 EncodedKey = VarientKey.EnCodeKey();
	auto Iter = UberShaderCache.find(EncodedKey);
	// 사전에 가능한 모든 메크로 조합에 대해 컴파일 했는데 없는 게 말이 안됨.
	assert(Iter != UberShaderCache.end() && "Failed PreCompile UberShader");

	return Iter->second.get();
}

//T -> ShaderVarientKey
template<typename T>
inline void FShaderManager::WarmUpUberShader(ID3D11Device* InDevice, const T& VarientKey)
{
	uint32 EncodedKey = VarientKey.EnCodeKey();
	if (UberShaderCache.count(EncodedKey))
	{
		return;
	}

	const TArray<D3D_SHADER_MACRO> Macros = VarientKey.BuildMacroFromKey();
	std::unique_ptr<FShader> NewShader = std::make_unique<FShader>();
	NewShader->Create(InDevice, T::ShaderPath, "VS", "PS", Macros.data());
	UberShaderCache[EncodedKey] = std::move(NewShader);
}
