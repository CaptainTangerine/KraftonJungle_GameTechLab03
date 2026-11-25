#include "Shader.h"
#include "Profiling/MemoryStats.h"
#include "Materials/Material.h"
#include "Editor/UI/EditorConsoleWidget.h"
#include <iostream>
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")


static thread_local bool GQuietCompileErrors = false;


namespace
{
    std::string GLastShaderCompileError;
    uint64      GLastShaderCompileErrorTimeMs = 0;
}

const std::string& FShader::GetLastCompileError()          { return GLastShaderCompileError; }
uint64             FShader::GetLastCompileErrorTimeMs()    { return GLastShaderCompileErrorTimeMs; }
void               FShader::ClearLastCompileError()
{
    GLastShaderCompileError.clear();
    GLastShaderCompileErrorTimeMs = 0;
}

FShader::FShader(FShader&& Other) noexcept
	: VertexShader(Other.VertexShader)
	, PixelShader(Other.PixelShader)
	, InputLayout(Other.InputLayout)
	, ShaderParameterLayout(std::move(Other.ShaderParameterLayout))
	, ShaderTextureLayout(std::move(Other.ShaderTextureLayout))
{
	Other.VertexShader = nullptr;
	Other.PixelShader = nullptr;
	Other.InputLayout = nullptr;
}

FShader& FShader::operator=(FShader&& Other) noexcept
{
	if (this != &Other)
	{
		Release();
		VertexShader = Other.VertexShader;
		PixelShader = Other.PixelShader;
		InputLayout = Other.InputLayout;
		ShaderParameterLayout = std::move(Other.ShaderParameterLayout);
		ShaderTextureLayout = std::move(Other.ShaderTextureLayout);
		Other.VertexShader = nullptr;
		Other.PixelShader = nullptr;
		Other.InputLayout = nullptr;
	}
	return *this;
}

void FShader::Create(ID3D11Device* InDevice, const wchar_t* InFilePath, const char* InVSEntryPoint, const char* InPSEntryPoint,
	const D3D_SHADER_MACRO* InDefines)
{
	Release();

	// Hot reload 용 입력 캐시
	CachedFilePath = InFilePath ? InFilePath : L"";
	CachedVSEntry  = InVSEntryPoint ? InVSEntryPoint : "";
	CachedPSEntry  = InPSEntryPoint ? InPSEntryPoint : "";
	CachedDefines.clear();
	if (InDefines)
	{
		for (const D3D_SHADER_MACRO* M = InDefines; M->Name != nullptr; ++M)
		{
			CachedDefines.emplace_back(
				std::string(M->Name),
				std::string(M->Definition ? M->Definition : "")
			);
		}
	}

	ID3DBlob* vertexShaderCSO = nullptr;
	ID3DBlob* pixelShaderCSO = nullptr;
	ID3DBlob* errorBlob = nullptr;

	// Vertex Shader 컴파일
	HRESULT hr = D3DCompileFromFile(InFilePath, InDefines, D3D_COMPILE_STANDARD_FILE_INCLUDE, InVSEntryPoint, "vs_5_0", 0, 0, &vertexShaderCSO, &errorBlob);
	if (FAILED(hr))
	{
		if (errorBlob)
		{
			const char* msg = (const char*)errorBlob->GetBufferPointer();
			if (GQuietCompileErrors)
			{
				UE_LOG("[Shader VS FAIL] %ls\n%s", InFilePath ? InFilePath : L"?", msg);
				GLastShaderCompileError = std::string("VS: ") + msg;
				GLastShaderCompileErrorTimeMs = GetTickCount64();
				std::cerr << "[Shader VS] " << msg << std::endl;
			}
			else
				MessageBoxA(nullptr, msg, "Vertex Shader Compile Error", MB_OK | MB_ICONERROR);
			errorBlob->Release();
		}
		return;
	}

	// Pixel Shader 컴파일
	hr = D3DCompileFromFile(InFilePath, InDefines, D3D_COMPILE_STANDARD_FILE_INCLUDE, InPSEntryPoint, "ps_5_0", 0, 0, &pixelShaderCSO, &errorBlob);
	if (FAILED(hr))
	{
		if (errorBlob)
		{
			const char* msg = (const char*)errorBlob->GetBufferPointer();
			if (GQuietCompileErrors)
			{
				UE_LOG("[Shader PS FAIL] %ls\n%s", InFilePath ? InFilePath : L"?", msg);
				GLastShaderCompileError = std::string("PS: ") + msg;
				GLastShaderCompileErrorTimeMs = GetTickCount64();
				std::cerr << "[Shader PS] " << msg << std::endl;
			}
			else
				MessageBoxA(nullptr, msg, "Pixel Shader Compile Error", MB_OK | MB_ICONERROR);
			errorBlob->Release();
		}
		vertexShaderCSO->Release();
		return;
	}

	// Vertex Shader 생성
	hr = InDevice->CreateVertexShader(vertexShaderCSO->GetBufferPointer(), vertexShaderCSO->GetBufferSize(), nullptr, &VertexShader);
	if (FAILED(hr))
	{
		std::cerr << "Failed to create Vertex Shader (HRESULT: " << hr << ")" << std::endl;
		vertexShaderCSO->Release();
		pixelShaderCSO->Release();
		return;
	}

	CachedVertexShaderSize = vertexShaderCSO->GetBufferSize();
	MemoryStats::AddVertexShaderMemory(static_cast<uint32>(CachedVertexShaderSize));

	// Pixel Shader 생성
	hr = InDevice->CreatePixelShader(pixelShaderCSO->GetBufferPointer(), pixelShaderCSO->GetBufferSize(), nullptr, &PixelShader);
	if (FAILED(hr))
	{
		std::cerr << "Failed to create Pixel Shader (HRESULT: " << hr << ")" << std::endl;
		Release();
		vertexShaderCSO->Release();
		pixelShaderCSO->Release();
		return;
	}

	CachedPixelShaderSize = pixelShaderCSO->GetBufferSize();
	MemoryStats::AddPixelShaderMemory(static_cast<uint32>(CachedPixelShaderSize));

	// Input Layout 생성 (VS input signature로부터 자동 추출)
	CreateInputLayoutFromReflection(InDevice, vertexShaderCSO);

	ExtractReflectionInfo(vertexShaderCSO);
	ExtractReflectionInfo(pixelShaderCSO);

	vertexShaderCSO->Release();
	pixelShaderCSO->Release();
}

void FShader::Release()
{
	if (InputLayout)
	{
		InputLayout->Release();
		InputLayout = nullptr;
	}
	if (PixelShader)
	{
		MemoryStats::SubPixelShaderMemory(static_cast<uint32>(CachedPixelShaderSize));
		CachedPixelShaderSize = 0;

		PixelShader->Release();
		PixelShader = nullptr;
	}
	if (VertexShader)
	{
		MemoryStats::SubVertexShaderMemory(static_cast<uint32>(CachedVertexShaderSize));
		CachedVertexShaderSize = 0;

		VertexShader->Release();
		VertexShader = nullptr;
	}

	for (auto& Pair : ShaderParameterLayout) delete Pair.second;
	ShaderParameterLayout.clear();

	for (auto& Pair : ShaderTextureLayout) delete Pair.second;
	ShaderTextureLayout.clear();
}

bool FShader::Recompile(ID3D11Device* InDevice)
{
	if (CachedFilePath.empty() || !InDevice) return false;

	// 기존 상태를 백업으로 옮기고 멤버를 null/빈 상태로 만든 뒤 Create 호출.
	ID3D11VertexShader* OldVS = VertexShader;
	ID3D11PixelShader*  OldPS = PixelShader;
	ID3D11InputLayout*  OldIL = InputLayout;
	size_t OldVSSize = CachedVertexShaderSize;
	size_t OldPSSize = CachedPixelShaderSize;
	TMap<FString, FMaterialParameterInfo*> OldParams = std::move(ShaderParameterLayout);
	TMap<FString, FMaterialTextureInfo*> OldTextures = std::move(ShaderTextureLayout);

	VertexShader = nullptr;
	PixelShader = nullptr;
	InputLayout = nullptr;
	CachedVertexShaderSize = 0;
	CachedPixelShaderSize = 0;
	ShaderParameterLayout.clear();
	ShaderTextureLayout.clear();

	// 캐시 보존을 위해 로컬에 복사.
	// 주의: Create() 내부에서 CachedDefines.clear()가 호출되어 원본 std::string이
	// 해제되므로, MacroList가 CachedDefines.c_str()를 가리키면 댕글링이 됨.
	// → 로컬에 소유권을 가진 std::string 복사본을 둬서 안전하게 만든다.
	const std::wstring Path = CachedFilePath;
	const std::string  VSE  = CachedVSEntry;
	const std::string  PSE  = CachedPSEntry;
	const bool bHasDefines = !CachedDefines.empty();
	std::vector<std::pair<std::string, std::string>> LocalDefines;
	LocalDefines.reserve(CachedDefines.size());
	for (const auto& D : CachedDefines)
	{
		LocalDefines.emplace_back(D.first, D.second); // 깊은 복사
	}

	std::vector<D3D_SHADER_MACRO> MacroList;
	MacroList.reserve(LocalDefines.size() + 1);
	for (const auto& D : LocalDefines)
	{
		MacroList.push_back({ D.first.c_str(), D.second.c_str() });
	}
	MacroList.push_back({ nullptr, nullptr });
	const D3D_SHADER_MACRO* MacroPtr = bHasDefines ? MacroList.data() : nullptr;

	GQuietCompileErrors = true;
	Create(InDevice, Path.c_str(), VSE.c_str(), PSE.c_str(), MacroPtr);
	GQuietCompileErrors = false;

	const bool bSuccess = (VertexShader != nullptr && PixelShader != nullptr);
	if (bSuccess)
	{
		// 백업 정리
		if (OldIL) OldIL->Release();
		if (OldPS)
		{
			MemoryStats::SubPixelShaderMemory(static_cast<uint32>(OldPSSize));
			OldPS->Release();
		}
		if (OldVS)
		{
			MemoryStats::SubVertexShaderMemory(static_cast<uint32>(OldVSSize));
			OldVS->Release();
		}
		for (auto& Pair : OldParams) delete Pair.second;
		for (auto& Pair : OldTextures) delete Pair.second;
		return true;
	}
	else
	{
		// 실패 시 새로 만든 부분 제거 후 백업 복구
		Release();
		VertexShader = OldVS;
		PixelShader  = OldPS;
		InputLayout  = OldIL;
		CachedVertexShaderSize = OldVSSize;
		CachedPixelShaderSize  = OldPSSize;
		ShaderParameterLayout = std::move(OldParams);
		ShaderTextureLayout = std::move(OldTextures);
		return false;
	}
}

void FShader::Bind(ID3D11DeviceContext* InDeviceContext) const
{
	InDeviceContext->IASetInputLayout(InputLayout);
	InDeviceContext->VSSetShader(VertexShader, nullptr, 0);
	InDeviceContext->PSSetShader(PixelShader, nullptr, 0);
}

void FShader::BindVSOnly(ID3D11DeviceContext* InDeviceContext) const
{
	InDeviceContext->IASetInputLayout(InputLayout);
	InDeviceContext->VSSetShader(VertexShader, nullptr, 0);
	InDeviceContext->PSSetShader(nullptr, nullptr, 0);
}


namespace
{
	DXGI_FORMAT MaskToFormat(D3D_REGISTER_COMPONENT_TYPE ComponentType, BYTE Mask)
	{
		// Mask 비트 수 세기 (사용되는 컴포넌트 개수)
		int Count = 0;
		if (Mask & 0x1) ++Count;
		if (Mask & 0x2) ++Count;
		if (Mask & 0x4) ++Count;
		if (Mask & 0x8) ++Count;

		if (ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
		{
			switch (Count)
			{
			case 1: return DXGI_FORMAT_R32_FLOAT;
			case 2: return DXGI_FORMAT_R32G32_FLOAT;
			case 3: return DXGI_FORMAT_R32G32B32_FLOAT;
			case 4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
			}
		}
		else if (ComponentType == D3D_REGISTER_COMPONENT_UINT32)
		{
			switch (Count)
			{
			case 1: return DXGI_FORMAT_R32_UINT;
			case 2: return DXGI_FORMAT_R32G32_UINT;
			case 3: return DXGI_FORMAT_R32G32B32_UINT;
			case 4: return DXGI_FORMAT_R32G32B32A32_UINT;
			}
		}
		else if (ComponentType == D3D_REGISTER_COMPONENT_SINT32)
		{
			switch (Count)
			{
			case 1: return DXGI_FORMAT_R32_SINT;
			case 2: return DXGI_FORMAT_R32G32_SINT;
			case 3: return DXGI_FORMAT_R32G32B32_SINT;
			case 4: return DXGI_FORMAT_R32G32B32A32_SINT;
			}
		}
		return DXGI_FORMAT_UNKNOWN;
	}
}

void FShader::CreateInputLayoutFromReflection(ID3D11Device* InDevice, ID3DBlob* VSBlob)
{
	ID3D11ShaderReflection* Reflector = nullptr;
	HRESULT hr = D3DReflect(VSBlob->GetBufferPointer(), VSBlob->GetBufferSize(),
		IID_ID3D11ShaderReflection, (void**)&Reflector);
	if (FAILED(hr)) return;

	D3D11_SHADER_DESC ShaderDesc;
	Reflector->GetDesc(&ShaderDesc);

	TArray<D3D11_INPUT_ELEMENT_DESC> Elements;

	for (UINT i = 0; i < ShaderDesc.InputParameters; ++i)
	{
		D3D11_SIGNATURE_PARAMETER_DESC ParamDesc;
		Reflector->GetInputParameterDesc(i, &ParamDesc);

		// SV_VertexID, SV_InstanceID 등 시스템 시맨틱은 스킵
		if (ParamDesc.SystemValueType != D3D_NAME_UNDEFINED)
			continue;

		D3D11_INPUT_ELEMENT_DESC Elem = {};
		Elem.SemanticName = ParamDesc.SemanticName;
		Elem.SemanticIndex = ParamDesc.SemanticIndex;
		Elem.Format = MaskToFormat(ParamDesc.ComponentType, ParamDesc.Mask);
		Elem.InputSlot = 0;
		Elem.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
		Elem.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		Elem.InstanceDataStepRate = 0;

		Elements.push_back(Elem);
	}

	// Fullscreen quad 등 vertex input이 없는 셰이더는 InputLayout 불필요
	if (Elements.empty())
	{
		Reflector->Release();
		return;
	}

	hr = InDevice->CreateInputLayout(Elements.data(), (UINT)Elements.size(),
		VSBlob->GetBufferPointer(), VSBlob->GetBufferSize(), &InputLayout);
	if (FAILED(hr))
	{
		std::cerr << "Failed to create Input Layout from reflection (HRESULT: " << hr << ")" << std::endl;
	}

	Reflector->Release();
}

void FShader::ExtractReflectionInfo(ID3DBlob* ShaderBlob)
{
	ID3D11ShaderReflection* Reflector = nullptr;
	D3DReflect(ShaderBlob->GetBufferPointer(), ShaderBlob->GetBufferSize(),
		IID_ID3D11ShaderReflection, (void**)&Reflector);

	D3D11_SHADER_DESC ShaderDesc;
	Reflector->GetDesc(&ShaderDesc);

	for (UINT i = 0; i < ShaderDesc.ConstantBuffers; ++i)
	{
		auto* CB = Reflector->GetConstantBufferByIndex(i);
		D3D11_SHADER_BUFFER_DESC CBDesc;
		CB->GetDesc(&CBDesc);

		FString BufferName = CBDesc.Name;

		D3D11_SHADER_INPUT_BIND_DESC BindDesc;
		Reflector->GetResourceBindingDescByName(CBDesc.Name, &BindDesc);
		UINT SlotIndex = BindDesc.BindPoint;

		if (SlotIndex != 2 && SlotIndex != 3)
			continue;

		for (UINT j = 0; j < CBDesc.Variables; ++j)
		{
			auto* Var = CB->GetVariableByIndex(j);
			D3D11_SHADER_VARIABLE_DESC VarDesc;
			Var->GetDesc(&VarDesc);

			if (ShaderParameterLayout.find(VarDesc.Name) != ShaderParameterLayout.end())
				continue;

			FMaterialParameterInfo* Info = new FMaterialParameterInfo();
			Info->BufferName = BufferName;
			Info->SlotIndex = SlotIndex;
			Info->Offset = VarDesc.StartOffset;
			Info->Size = VarDesc.Size;
			Info->BufferSize = CBDesc.Size;

			ShaderParameterLayout[VarDesc.Name] = Info;
		}
	}

	for (UINT i = 0; i < ShaderDesc.BoundResources; ++i)
	{
		D3D11_SHADER_INPUT_BIND_DESC BindDesc;
		Reflector->GetResourceBindingDesc(i, &BindDesc);

		if (BindDesc.Type == D3D_SIT_TEXTURE)
		{
			// 시스템 텍스처(t10 이상)는 리플렉션에서 제외
			if (BindDesc.BindPoint >= 10)
				continue;

			if (ShaderTextureLayout.find(BindDesc.Name) != ShaderTextureLayout.end())
				continue;

			FMaterialTextureInfo* Info = new FMaterialTextureInfo();
			Info->Name = BindDesc.Name;
			Info->SlotIndex = BindDesc.BindPoint;
			ShaderTextureLayout[BindDesc.Name] = Info;
		}
	}

	Reflector->Release();
}

