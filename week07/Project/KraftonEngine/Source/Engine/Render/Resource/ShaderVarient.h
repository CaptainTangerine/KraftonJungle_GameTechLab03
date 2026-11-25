#pragma once
#include "Core/CoreTypes.h"
#include <d3dcompiler.h>

/*
* 메크로 조합에 따라서 셰이더를 만드는 구조라서 해당 매크로 조합을 ShaderVarietnKey로 추상화
* EnCodeKey -> ShaderManager에서 CacheKey를 만들 때 사용
* BuildMacroFromKey -> VarientKey 기반으로 D3DShaderMacro 생성
*/


enum class EKeyTypeId : uint32
{
	UberLit,
	StaticMesh,
};

enum class EUberLitFlags : uint32
{
	None = 0,
	Gouraud = 1 << 0,
	Lambert = 1 << 1,
	Phong = 1 << 2,
	BRDF = 1 << 3,
	Toon = 1 << 4,
	HasNormal = 1 << 5,
	DepthNormalPass = 1 << 6,
};

//하나의 우버 클래스를 만들때마다 새로운 Key 정의..
struct FUberLitVarientKey
{
	uint32 TypeID = static_cast<uint32>(EKeyTypeId::UberLit);
	uint32 Flags = static_cast<uint32>(EUberLitFlags::Phong);

	static constexpr const wchar_t* ShaderPath = L"Shaders/UberLit.hlsl";

	// ShagderManager에서 Cachekey 구별용
	uint32 EnCodeKey() const
	{
		return (static_cast<uint32>(TypeID) << 24 | Flags);
	}

	TArray<D3D_SHADER_MACRO> BuildMacroFromKey() const
	{
		TArray<D3D_SHADER_MACRO> Out;

		static constexpr const char* MacroNames[] = {
			"LIGHTING_MODEL_GOURAUD",
			"LIGHTING_MODEL_LAMBERT",
			"LIGHTING_MODEL_PHONG",
			"LIGHTING_MODEL_BRDF",
			"LIGHTING_MODEL_TOON",
			"HAS_NORMAL_MAP",
			"DEPTH_NORMAL_PASS",
		};

		for (uint32 i = 0; i < std::size(MacroNames); ++i)
		{
			if (Flags & (1 << i))
			{
				Out.push_back({ MacroNames[i], "1" });
			}
		}
		Out.push_back({ nullptr, nullptr });
		return Out;
	}
};

enum class EStaticMeshFlags : uint32
{
	None = 0,
	DepthNormalPass = 1 << 0,
};

struct FStaticMeshVarientKey
{
	uint32 TypeID = static_cast<uint32>(EKeyTypeId::StaticMesh);
	uint32 Flags = static_cast<uint32>(EStaticMeshFlags::None);

	static constexpr const wchar_t* ShaderPath = L"Shaders/StaticMeshShader.hlsl";

	uint32 EnCodeKey() const
	{
		return (static_cast<uint32>(TypeID) << 24 | Flags);
	}

	TArray<D3D_SHADER_MACRO> BuildMacroFromKey() const
	{
		TArray<D3D_SHADER_MACRO> Out;

		if (Flags & static_cast<uint32>(EStaticMeshFlags::DepthNormalPass))
		{
			Out.push_back({ "DEPTH_NORMAL_PASS", "1" });
		}

		Out.push_back({ nullptr, nullptr });
		return Out;
	}
};
