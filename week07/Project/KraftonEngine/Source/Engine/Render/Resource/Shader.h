#pragma once
#include "Render/Types/RenderTypes.h"
#include <string>
#include <vector>
#include <utility>

struct FMaterialTextureInfo;
struct FMaterialParameterInfo;

class FShader
{
public:
	FShader() = default;
	~FShader() { Release(); }

	FShader(const FShader&) = delete;
	FShader& operator=(const FShader&) = delete;
	FShader(FShader&& Other) noexcept;
	FShader& operator=(FShader&& Other) noexcept;

	void Create(ID3D11Device* InDevice, const wchar_t* InFilePath, const char* InVSEntryPoint, const char* InPSEntryPoint,
		const D3D_SHADER_MACRO* InDefines = nullptr);
	void Release();

	// Hot reload: 캐시된 경로/엔트리/매크로로 재컴파일. 실패 시 기존 셰이더 유지.
	bool Recompile(ID3D11Device* InDevice);
	bool HasSource() const { return !CachedFilePath.empty(); }
	const std::wstring& GetFilePath() const { return CachedFilePath; }

	void Bind(ID3D11DeviceContext* InDeviceContext) const;
	void BindVSOnly(ID3D11DeviceContext* InDeviceContext) const;

	// Hot reload 최근 실패 상태 조회 (ImGui 배너 표시용)
	static const std::string& GetLastCompileError();
	static uint64             GetLastCompileErrorTimeMs();
	static void               ClearLastCompileError();

	const TMap<FString, FMaterialParameterInfo*>& GetParameterLayout() const { return ShaderParameterLayout; }
	const TMap<FString, FMaterialTextureInfo*>& GetTextureLayout() const { return ShaderTextureLayout; }
private:
	ID3D11VertexShader* VertexShader = nullptr;
	ID3D11PixelShader* PixelShader = nullptr;
	ID3D11InputLayout* InputLayout = nullptr;

	size_t CachedVertexShaderSize = 0;
	size_t CachedPixelShaderSize = 0;

	// Create 시점 입력 캐시 
	std::wstring CachedFilePath;
	std::string  CachedVSEntry;
	std::string  CachedPSEntry;
	std::vector<std::pair<std::string, std::string>> CachedDefines;

	void CreateInputLayoutFromReflection(ID3D11Device* InDevice, ID3DBlob* VSBlob);
	void ExtractReflectionInfo(ID3DBlob* ShaderBlob);
	TMap<FString, FMaterialParameterInfo*> ShaderParameterLayout;
	TMap<FString, FMaterialTextureInfo*> ShaderTextureLayout;
};
