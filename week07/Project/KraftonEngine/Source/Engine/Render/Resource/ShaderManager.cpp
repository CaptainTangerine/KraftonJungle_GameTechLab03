#include "ShaderManager.h"
#include "ShaderVarient.h"
#include <string>
#include <Windows.h>
#include <filesystem>
#include <chrono>
#include <iostream>
#include "Editor/UI/EditorConsoleWidget.h"

namespace
{
	inline std::string WStringToString(const std::wstring& wstr)
	{
		if (wstr.empty()) return std::string();
	
		int sizeNeeded = WideCharToMultiByte(
			CP_UTF8,
			0,
			wstr.data(),
			(int)wstr.size(),
			nullptr,
			0,
			nullptr,
			nullptr
		);
	
		if (sizeNeeded <= 0) return std::string();
	
		std::string result(sizeNeeded, 0);
	
		WideCharToMultiByte(
			CP_UTF8,
			0,
			wstr.data(),
			(int)wstr.size(),
			&result[0],
			sizeNeeded,
			nullptr,
			nullptr
		);
	
		return result;
	}
}

void FShaderManager::Initialize(ID3D11Device* InDevice)
{
	if (bIsInitialized) return;

	Shaders[(uint32)EShaderType::Primitive].Create(InDevice, L"Shaders/Primitive.hlsl", "VS", "PS");
	Shaders[(uint32)EShaderType::Gizmo].Create(InDevice, L"Shaders/Gizmo.hlsl", "VS", "PS");
	Shaders[(uint32)EShaderType::Editor].Create(InDevice, L"Shaders/Editor.hlsl", "VS", "PS");
	Shaders[(uint32)EShaderType::StaticMesh].Create(InDevice, L"Shaders/StaticMeshShader.hlsl", "VS", "PS");
	Shaders[(uint32)EShaderType::Decal].Create(InDevice, L"Shaders/DecalShader.hlsl", "VS", "PS");
	Shaders[(uint32)EShaderType::OutlinePostProcess].Create(InDevice, L"Shaders/OutlinePostProcess.hlsl", "VS", "PS");
	Shaders[(uint32)EShaderType::SceneDepth].Create(InDevice, L"Shaders/SceneDepth.hlsl", "VS", "PS");
	Shaders[(uint32)EShaderType::SceneNormal].Create(InDevice, L"Shaders/SceneNormal.hlsl", "VS", "PS");
	Shaders[(uint32)EShaderType::FXAA].Create(InDevice, L"Shaders/FXAA.hlsl", "VS", "PS");
	Shaders[(uint32)EShaderType::Font].Create(InDevice, L"Shaders/ShaderFont.hlsl", "VS", "PS");
	Shaders[(uint32)EShaderType::OverlayFont].Create(InDevice, L"Shaders/ShaderOverlayFont.hlsl", "VS", "PS");
	Shaders[(uint32)EShaderType::SubUV].Create(InDevice, L"Shaders/ShaderSubUV.hlsl", "VS", "PS");
	Shaders[(uint32)EShaderType::Billboard].Create(InDevice, L"Shaders/ShaderBillboard.hlsl", "VS", "PS");
	Shaders[(uint32)EShaderType::HeightFog].Create(InDevice, L"Shaders/HeightFog.hlsl", "VS", "PS");
	Shaders[(uint32)EShaderType::LightComplexity].Create(InDevice, L"Shaders/LightComplexity.hlsl", "VS", "PS");
	Shaders[(uint32)EShaderType::LightInfluence].Create(InDevice, L"Shaders/LightInfluence.hlsl", "VS", "PS");
	Shaders[(uint32)EShaderType::SSAOApply].Create(InDevice, L"Shaders/SSAOApply.hlsl", "VS", "PS");
	Shaders[(uint32)EShaderType::Heatmap].Create(InDevice, L"Shaders/HeatmapShader.hlsl", "VS", "PS");
	Shaders[(uint32)EShaderType::GodRayOcclusion].Create(InDevice, L"Shaders/GodRayOcclusion.hlsl", "VS", "PS");
	Shaders[(uint32)EShaderType::GodRay].Create(InDevice, L"Shaders/GodRay.hlsl", "VS", "PS");
	Shaders[(uint32)EShaderType::LensFlare].Create(InDevice, L"Shaders/LensFlare.hlsl", "VS", "PS");

	CompileUberLitShader(InDevice);

	bIsInitialized = true;
}



void FShaderManager::Release()
{
	for (uint32 i = 0; i < (uint32)EShaderType::MAX; ++i)
	{
		Shaders[i].Release();
	}

	for (auto& [Key, Shader] : CustomShaderCache)
		Shader->Release();

	for (auto& [Key, Shader] : UberShaderCache)
		Shader->Release();

	CustomShaderCache.clear();
	UberShaderCache.clear();

	bIsInitialized = false;
}

FShader* FShaderManager::GetShader(EShaderType InType)
{
	uint32 Idx = (uint32)InType;
	if (Idx < (uint32)EShaderType::MAX)
	{
		return &Shaders[Idx];
	}
	return nullptr;
}

FShader* FShaderManager::GetCustomShader(const FString& Key)
{
	auto It = CustomShaderCache.find(Key);
	if (It != CustomShaderCache.end())
	{
		return It->second.get();  // 이미 캐시에 있으면 반환
	}
	return nullptr;
}

FShader* FShaderManager::CreateCustomShader(ID3D11Device* InDevice, const wchar_t* InFilePath)
{
	FString Key = WStringToString(InFilePath);

	auto It = CustomShaderCache.find(Key);
	if (It != CustomShaderCache.end())
	{
		return It->second.get();  // 이미 캐시에 있으면 반환
	}

	auto NewShader = std::make_unique<FShader>();
	NewShader->Create(InDevice, InFilePath, "VS", "PS");
	auto* RawPtr = NewShader.get();
	CustomShaderCache[Key] = std::move(NewShader);
	return RawPtr;
}

void FShaderManager::PollHotReload(ID3D11Device* InDevice)
{
	if (!bIsInitialized || !InDevice) return;

	const uint64 NowMs = static_cast<uint64>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()
		).count());
	if (NowMs - LastPollTickMs < 300) return;
	LastPollTickMs = NowMs;

	// Shaders/ 하위 .hlsl 파일들의 max mtime 계산
	namespace fs = std::filesystem;
	fs::path ShaderDir = "Shaders";
	if (!fs::exists(ShaderDir)) return;

	int64 MaxMtime = 0;
	std::error_code ec;
	for (fs::recursive_directory_iterator it(ShaderDir, ec), end; it != end; it.increment(ec))
	{
		if (ec) { ec.clear(); continue; }
		if (!it->is_regular_file()) continue;
		const fs::path& p = it->path();
		if (p.extension() != ".hlsl" && p.extension() != ".hlsli") continue;

		auto t = fs::last_write_time(p, ec);
		if (ec) { ec.clear(); continue; }
		int64 ticks = t.time_since_epoch().count();
		if (ticks > MaxMtime) MaxMtime = ticks;
	}

	// 첫 호출: 기준만 저장하고 리턴
	if (ShadersDirMaxMtime == 0)
	{
		ShadersDirMaxMtime = MaxMtime;
		return;
	}
	if (MaxMtime <= ShadersDirMaxMtime) return;

	//변경시 전체 컴파일
	ShadersDirMaxMtime = MaxMtime;

	uint32 Reloaded = 0, Failed = 0;
	auto Reload = [&](FShader& S)
	{
		if (!S.HasSource()) return;
		if (S.Recompile(InDevice)) ++Reloaded; else ++Failed;
	};

	for (uint32 i = 0; i < (uint32)EShaderType::MAX; ++i)
	{
		Reload(Shaders[i]);
	}
	for (auto& [Key, ShaderPtr] : CustomShaderCache)
	{
		if (ShaderPtr) Reload(*ShaderPtr);
	}
	
	for (auto& [Key, ShaderPtr] : UberShaderCache)
	{
		if (ShaderPtr) Reload(*ShaderPtr);
	}

	if (Failed > 0)
	{
		UE_LOG("[ShaderHotReload] Reloaded=%u  Failed=%u  (previous shader kept for failed variants)", Reloaded, Failed);
	}
	else
	{
		UE_LOG("[ShaderHotReload] Reloaded=%u  Failed=0", Reloaded);
	}
	std::cout << "[ShaderHotReload] Reloaded=" << Reloaded << " Failed=" << Failed << std::endl;
}

void FShaderManager::CompileUberLitShader(ID3D11Device* InDevice)
{
	const EUberLitFlags LightingModels[] = {
		EUberLitFlags::Gouraud,
		EUberLitFlags::Lambert,
		EUberLitFlags::BRDF,
		EUberLitFlags::Phong,
		EUberLitFlags::Toon,
		EUberLitFlags::DepthNormalPass,		// pre depth & normal pass에서 사용
	};
	const EUberLitFlags BoolCombinations[] = {
		EUberLitFlags::None,
		EUberLitFlags::HasNormal
	};

	for (EUberLitFlags LM : LightingModels)
		for (EUberLitFlags BF : BoolCombinations)
		{
			FUberLitVarientKey Key;
			Key.TypeID = static_cast<uint32>(EKeyTypeId::UberLit);
			Key.Flags = static_cast<uint32>(LM) | static_cast<uint32>(BF);
			WarmUpUberShader<FUberLitVarientKey>(InDevice, Key);
		}

	// StaticMesh 셰이더 변종 컴파일 (Default, DepthNormalPass)
	{
		FStaticMeshVarientKey Key;
		Key.Flags = static_cast<uint32>(EStaticMeshFlags::None);
		WarmUpUberShader<FStaticMeshVarientKey>(InDevice, Key);

		Key.Flags = static_cast<uint32>(EStaticMeshFlags::DepthNormalPass);
		WarmUpUberShader<FStaticMeshVarientKey>(InDevice, Key);
	}
}



