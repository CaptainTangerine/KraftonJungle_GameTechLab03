#include "Render/Proxy/StaticMeshSceneProxy.h"
#include "Component/StaticMeshComponent.h"
#include "Render/Resource/ShaderManager.h"
#include "Mesh/StaticMesh.h"
#include "Mesh/StaticMeshAsset.h"
#include "Materials/Material.h"
#include "Texture/Texture2D.h"

#include <algorithm>

namespace
{
	bool SectionMaterialLess(const FMeshSectionDraw& A, const FMeshSectionDraw& B)
	{
		const uintptr_t ASRV = reinterpret_cast<uintptr_t>(A.DiffuseSRV);
		const uintptr_t BSRV = reinterpret_cast<uintptr_t>(B.DiffuseSRV);
		if (ASRV != BSRV)
		{
			return ASRV < BSRV;
		}

		return A.FirstIndex < B.FirstIndex;
	}

	void SortSectionDrawsByMaterial(TArray<FMeshSectionDraw>& Draws)
	{
		if (Draws.size() > 1)
		{
			std::sort(Draws.begin(), Draws.end(), SectionMaterialLess);
		}
	}
}

// ============================================================
// FStaticMeshSceneProxy
// ============================================================
FStaticMeshSceneProxy::FStaticMeshSceneProxy(UStaticMeshComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
}

UStaticMeshComponent* FStaticMeshSceneProxy::GetStaticMeshComponent() const
{
	return static_cast<UStaticMeshComponent*>(Owner);
}

// ============================================================
// UpdateMaterial — 머티리얼만 변경된 경우 SectionDraws 재구축
// ============================================================
void FStaticMeshSceneProxy::UpdateMaterial()
{
	RebuildSectionDraws();
}

// ============================================================
// UpdateMesh — 메시 버퍼 + 셰이더 교체 후 SectionDraws 재구축
// ============================================================
void FStaticMeshSceneProxy::UpdateMesh()
{
	MeshBuffer = Owner->GetMeshBuffer();
	Pass = ERenderPass::Opaque;

	RebuildSectionDraws();
}

// ============================================================
// UpdateLOD — LOD 레벨 변경 시 MeshBuffer/SectionDraws 스왑
// ============================================================
void FStaticMeshSceneProxy::UpdateLOD(uint32 LODLevel)
{
	if (LODLevel >= LODCount) LODLevel = LODCount - 1;
	if (LODLevel == CurrentLOD) return;

	// 현재 활성 데이터를 LODData 슬롯에 swap (할당/해제 없는 O(1) 교환)
	std::swap(MeshBuffer, LODData[CurrentLOD].MeshBuffer);
	std::swap(SectionDraws, LODData[CurrentLOD].SectionDraws);

	// 새 LOD 데이터를 활성 슬롯에서 swap
	CurrentLOD = LODLevel;
	std::swap(MeshBuffer, LODData[LODLevel].MeshBuffer);
	std::swap(SectionDraws, LODData[LODLevel].SectionDraws);

}

// ============================================================
// RebuildSectionDraws — 모든 LOD의 SectionDraws 재구축
// ============================================================
void FStaticMeshSceneProxy::RebuildSectionDraws()
{
	UStaticMeshComponent* SMC = GetStaticMeshComponent();
	UStaticMesh* Mesh = SMC->GetStaticMesh();
	if (!Mesh || !Mesh->GetStaticMeshAsset())
	{
		for (uint32 lod = 0; lod < MAX_LOD; ++lod)
		{
			LODData[lod].MeshBuffer = nullptr;
			LODData[lod].SectionDraws.clear();
		}

		LODCount = 1;
		CurrentLOD = 0;
		MeshBuffer = nullptr;
		SectionDraws.clear();
	
		return;
	}

	const auto& Slots = Mesh->GetStaticMaterials();
	const auto& Overrides = SMC->GetOverrideMaterials();
	LODCount = Mesh->GetLODCount();

	FShader* StaticMeshShader = FShaderManager::Get().GetShader(EShaderType::StaticMesh);

	// 각 LOD별 SectionDraws + MeshBuffer 구축
	for (uint32 lod = 0; lod < LODCount; ++lod)
	{
		const auto& Sections = Mesh->GetLODSections(lod);
		LODData[lod].MeshBuffer = Mesh->GetLODMeshBuffer(lod);
		LODData[lod].SectionDraws.clear();
		LODData[lod].SectionDraws.reserve(Sections.size());

		for (const FStaticMeshSection& Section : Sections)
		{
			FMeshSectionDraw Draw;
			Draw.Shader = StaticMeshShader;
			Draw.FirstIndex = Section.FirstIndex;
			Draw.IndexCount = Section.NumTriangles * 3;
			Draw.bSupportUberShader = true;

			int32 i = Section.MaterialIndex;
			if (i >= 0 && i < static_cast<int32>(Slots.size()))
			{
				UMaterial* Mat = nullptr;

				if (i < static_cast<int32>(Overrides.size()) && Overrides[i])
					Mat = Overrides[i];
				else if (Slots[i].MaterialInterface)
					Mat = Slots[i].MaterialInterface;

				if (Mat)
				{
					if (Mat->GetShader())
						Draw.Shader = Mat->GetShader();

					// 1. 기존 Diffuse/Normal 텍스처 유지 (하위호환)
					UTexture2D* DiffuseTex = nullptr;
					UTexture2D* NormalTex = nullptr;
					if (Mat->GetTextureParameter("DiffuseTexture", DiffuseTex))
						Draw.DiffuseSRV = DiffuseTex->GetSRV();
					else if (Mat->GetTextureParameter("g_txColor", DiffuseTex))
						Draw.DiffuseSRV = DiffuseTex->GetSRV();

					if (Mat->GetTextureParameter("NormalTexture", NormalTex))
						Draw.NormalSRV = NormalTex->GetSRV();
					else if (Mat->GetTextureParameter("g_txNormal", NormalTex))
						Draw.NormalSRV = NormalTex->GetSRV();

					// 2. 리플렉션된 텍스처 중 t2~t9 슬롯만 ExtraTextures에 수집
					const auto& TextureLayout = Mat->GetTextureLayout();
					Draw.ExtraTextureCount = 0;
					for (const auto& Pair : TextureLayout)
					{
						const FMaterialTextureInfo* Info = Pair.second;
						
						// t0, t1은 위에서 이미 처리했거나 엔진 고정 슬롯이므로 제외
						// t10 이상은 시스템 텍스처이므로 건드리지 않음
						if (Info->SlotIndex < 2 || Info->SlotIndex >= 10) continue;

						if (Draw.ExtraTextureCount < FMeshSectionDraw::kMaxExtraTextures)
						{
							UTexture2D* Tex = nullptr;
							if (Mat->GetTextureParameter(Info->Name, Tex) && Tex)
							{
								Draw.ExtraTextures[Draw.ExtraTextureCount].SRV = Tex->GetSRV();
								Draw.ExtraTextures[Draw.ExtraTextureCount].Slot = Info->SlotIndex;
								Draw.ExtraTextureCount++;
							}
							else
							{
								// JSON에 없으면 기본 텍스처 바인딩 (이름 힌트 활용)
								UTexture2D* Fallback = UTexture2D::GetDefaultWhite();
								if (Info->Name.find("Normal") != std::string::npos) Fallback = UTexture2D::GetDefaultNormal();
								// Shadow Mask는 기본적으로 흰색(그림자 없음)으로 처리
								else if (Info->Name.find("Shadow") != std::string::npos) Fallback = UTexture2D::GetDefaultWhite();

								Draw.ExtraTextures[Draw.ExtraTextureCount].SRV = Fallback->GetSRV();
								Draw.ExtraTextures[Draw.ExtraTextureCount].Slot = Info->SlotIndex;
								Draw.ExtraTextureCount++;
							}
						}
					}

					// 머티리얼 기반 렌더 상태 전파
					Draw.Blend = Mat->GetBlendState();
					Draw.DepthStencil = Mat->GetDepthStencilState();
					Draw.Rasterizer = Mat->GetRasterizerState();

					Mat->GetGPUBufferBindingBySlot(ECBSlot::PerShader0, Draw.MaterialCB[0]);
					Mat->GetGPUBufferBindingBySlot(ECBSlot::PerShader1, Draw.MaterialCB[1]);
				}
			}

			LODData[lod].SectionDraws.push_back(Draw);
		}

		SortSectionDrawsByMaterial(LODData[lod].SectionDraws);
	}

	// LOD0을 활성 슬롯으로 설정
	CurrentLOD = 0;
	std::swap(MeshBuffer, LODData[0].MeshBuffer);
	std::swap(SectionDraws, LODData[0].SectionDraws);

}
