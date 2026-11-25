#include "DrawCommandList.h"

#include <algorithm>
#include <cstring>
#include "Render/Resource/Shader.h"
#include "Render/Pipeline/RenderConstants.h"
#include "Profiling/Stats.h"

// ============================================================
// FStateCache
// ============================================================

void FStateCache::Reset()
{
	bForceAll = true;

	Shader       = nullptr;
	DepthStencil = {};
	Blend        = {};
	Rasterizer   = {};
	Topology     = {};
	StencilRef   = 0;
	MeshBuffer   = nullptr;
	RawVB        = nullptr;
	RawIB        = nullptr;
	PerObjectCB     = nullptr;
	PerShaderCB[0]  = nullptr;
	PerShaderCB[1]  = nullptr;
	DiffuseSRV   = nullptr;
	NormalSRV    = nullptr;

	RTV         = nullptr;
	DSV         = nullptr;
}

void FStateCache::Cleanup(ID3D11DeviceContext* Ctx)
{
	// Unbind t0, t1 SRVs to prevent potential hazards in next frame
	if (DiffuseSRV)
	{
		ID3D11ShaderResourceView* nullSRV = nullptr;
		Ctx->PSSetShaderResources(0, 1, &nullSRV);
		DiffuseSRV = nullptr;
	}
	if (NormalSRV)
	{
		ID3D11ShaderResourceView* nullSRV = nullptr;
		Ctx->PSSetShaderResources(1, 1, &nullSRV);
		NormalSRV = nullptr;
	}
}

// ============================================================
// FDrawCommandList
// ============================================================

FDrawCommand& FDrawCommandList::AddCommand()
{
	Commands.emplace_back();
	return Commands.back();
}

void FDrawCommandList::Sort()
{
	if (Commands.size() > 1)
	{
		std::sort(Commands.begin(), Commands.end(),
			[](const FDrawCommand& A, const FDrawCommand& B)
			{
				return A.SortKey < B.SortKey;
			});
	}

	// Build pass offsets - linear scan once after sorting
	std::memset(PassOffsets, 0, sizeof(PassOffsets));
	const uint32 Total = static_cast<uint32>(Commands.size());
	uint32 Idx = 0;
	for (uint32 P = 0; P < (uint32)ERenderPass::MAX; ++P)
	{
		PassOffsets[P] = Idx;
		while (Idx < Total && (uint32)Commands[Idx].Pass == P)
			++Idx;
	}
	PassOffsets[(uint32)ERenderPass::MAX] = Total;
}

void FDrawCommandList::GetPassRange(ERenderPass Pass, uint32& OutStart, uint32& OutEnd) const
{
	OutStart = PassOffsets[(uint32)Pass];
	OutEnd   = PassOffsets[(uint32)Pass + 1];
}

void FDrawCommandList::Submit(FD3DDevice& Device, ID3D11DeviceContext* Ctx)
{
	if (Commands.empty()) return;

	SCOPE_STAT_CAT("DrawCommandList::Submit", "4_ExecutePass");

	FStateCache Cache;
	Cache.Reset();

	for (const FDrawCommand& Cmd : Commands)
	{
		SubmitCommand(Cmd, Device, Ctx, Cache);
	}

	Cache.Cleanup(Ctx);
}

void FDrawCommandList::SubmitRange(uint32 StartIdx, uint32 EndIdx, FD3DDevice& Device,
	ID3D11DeviceContext* Ctx)
{
	if (StartIdx >= EndIdx) return;
	if (EndIdx > Commands.size()) EndIdx = static_cast<uint32>(Commands.size());

	FStateCache Cache;
	Cache.Reset();

	for (uint32 i = StartIdx; i < EndIdx; ++i)
	{
		SubmitCommand(Commands[i], Device, Ctx, Cache);
	}

	Cache.Cleanup(Ctx);
}

void FDrawCommandList::SubmitRange(uint32 StartIdx, uint32 EndIdx, FD3DDevice& Device,
	ID3D11DeviceContext* Ctx, FStateCache& Cache)
{
	if (StartIdx >= EndIdx) return;
	if (EndIdx > Commands.size()) EndIdx = static_cast<uint32>(Commands.size());

	for (uint32 i = StartIdx; i < EndIdx; ++i)
	{
		SubmitCommand(Commands[i], Device, Ctx, Cache);
	}
}

void FDrawCommandList::SubmitDepthNormalRange(uint32 StartIdx, uint32 EndIdx, FD3DDevice& Device,
	ID3D11DeviceContext* Ctx, FStateCache& Cache)
{
	if (StartIdx >= EndIdx) return;
	if (EndIdx > Commands.size()) EndIdx = static_cast<uint32>(Commands.size());

	for (uint32 i = StartIdx; i < EndIdx; ++i)
	{
		SubmitDepthNormalCommand(Commands[i], Device, Ctx, Cache);
	}
}

void FDrawCommandList::SubmitWithStateOverrideRange(uint32 StartIdx, uint32 EndIdx, FD3DDevice& Device,
	ID3D11DeviceContext* Ctx, FStateCache& Cache,
	EDepthStencilState DepthOverride, EBlendState BlendOverride)
{
	if (StartIdx >= EndIdx) return;
	if (EndIdx > Commands.size()) EndIdx = static_cast<uint32>(Commands.size());

	for (uint32 i = StartIdx; i < EndIdx; ++i)
	{
		SubmitWithStateOverrideCommand(Commands[i], Device, Ctx, Cache, DepthOverride, BlendOverride);
	}
}

void FDrawCommandList::Reset()
{
	Commands.clear();
	std::memset(PassOffsets, 0, sizeof(PassOffsets));
}

uint32 FDrawCommandList::GetCommandCount(ERenderPass Pass) const
{
	return PassOffsets[(uint32)Pass + 1] - PassOffsets[(uint32)Pass];
}

// ============================================================
// Draw Pass Implementations
// ============================================================

void FDrawCommandList::SubmitCommand(const FDrawCommand& Cmd, FD3DDevice& Device,
	ID3D11DeviceContext* Ctx, FStateCache& Cache)
{
	BindPSO(Cmd, Device, Ctx, Cache);
	BindGeometry(Cmd, Ctx, Cache);
	BindShader(Cmd, Ctx, Cache);
	BindConstantBuffers(Cmd, Ctx, Cache);
	BindResources(Cmd, Ctx, Cache);
	
	ExecuteDraw(Cmd, Ctx);
}

void FDrawCommandList::SubmitDepthOnlyCommand(const FDrawCommand& Cmd, FD3DDevice& Device,
	ID3D11DeviceContext* Ctx, FStateCache& Cache)
{
	// Depth Pre-pass: Force Write Enable and No Color Blend
	BindPSO(Cmd, Device, Ctx, Cache, EDepthStencilState::Default, EBlendState::NoColor);
	BindGeometry(Cmd, Ctx, Cache);
	BindShader(Cmd, Ctx, Cache, true); // VS Only
	BindConstantBuffers(Cmd, Ctx, Cache);
	
	ExecuteDraw(Cmd, Ctx);
}

void FDrawCommandList::SubmitDepthNormalCommand(const FDrawCommand& Cmd, FD3DDevice& Device,
	ID3D11DeviceContext* Ctx, FStateCache& Cache)
{
	// Depth + Normal Pre-pass: Write Enable and Opaque (to write Normal RT)
	BindPSO(Cmd, Device, Ctx, Cache, EDepthStencilState::Default, EBlendState::Opaque);
	BindGeometry(Cmd, Ctx, Cache);

	// Use DepthNormalShader variant if available, otherwise fallback to regular shader
	FShader* TargetShader = Cmd.DepthNormalShader ? Cmd.DepthNormalShader : Cmd.Shader;

	const bool bForce = Cache.bForceAll;
	if (bForce || TargetShader != Cache.Shader)
	{
		if (TargetShader) TargetShader->Bind(Ctx);
		Cache.Shader = TargetShader;
	}

	BindConstantBuffers(Cmd, Ctx, Cache);
	BindResources(Cmd, Ctx, Cache);

	ExecuteDraw(Cmd, Ctx);
}

void FDrawCommandList::SubmitWithStateOverrideCommand(const FDrawCommand& Cmd, FD3DDevice& Device,
	ID3D11DeviceContext* Ctx, FStateCache& Cache,
	EDepthStencilState DepthOverride, EBlendState BlendOverride)
{
	BindPSO(Cmd, Device, Ctx, Cache, DepthOverride, BlendOverride);
	BindGeometry(Cmd, Ctx, Cache);
	BindShader(Cmd, Ctx, Cache);
	BindConstantBuffers(Cmd, Ctx, Cache);
	BindResources(Cmd, Ctx, Cache);
	
	ExecuteDraw(Cmd, Ctx);
}

// ============================================================
// Low-level Binding Helpers (Private)
// ============================================================

void FDrawCommandList::BindPSO(const FDrawCommand& Cmd, FD3DDevice& Device, ID3D11DeviceContext* Ctx, FStateCache& Cache,
	EDepthStencilState DepthOverride, EBlendState BlendOverride)
{
	const bool bForce = Cache.bForceAll;

	// Render Target Override (SSAO etc.)
	if (Cmd.RenderTargetOverride)
	{
		if (bForce || Cmd.RenderTargetOverride != Cache.RTV)
		{
			ID3D11RenderTargetView* rtv = Cmd.RenderTargetOverride;
			Ctx->OMSetRenderTargets(1, &rtv, nullptr);
			Cache.RTV = rtv;
			Cache.DSV = nullptr;
		}
	}

	// Use overrides if provided, otherwise use command's own state
	const EDepthStencilState TargetDS = (DepthOverride != (EDepthStencilState)0xFF) ? DepthOverride : Cmd.DepthStencil;
	const EBlendState TargetBlend = (BlendOverride != (EBlendState)0xFF) ? BlendOverride : Cmd.Blend;

	if (bForce || TargetDS != Cache.DepthStencil)
	{
		Device.SetDepthStencilState(TargetDS);
		Cache.DepthStencil = TargetDS;
	}

	if (bForce || TargetBlend != Cache.Blend)
	{
		Device.SetBlendState(TargetBlend);
		Cache.Blend = TargetBlend;
	}

	if (bForce || Cmd.Rasterizer != Cache.Rasterizer)
	{
		Device.SetRasterizerState(Cmd.Rasterizer);
		Cache.Rasterizer = Cmd.Rasterizer;
	}

	if (bForce || Cmd.Topology != Cache.Topology)
	{
		Ctx->IASetPrimitiveTopology(Cmd.Topology);
		Cache.Topology = Cmd.Topology;
	}
}

void FDrawCommandList::BindShader(const FDrawCommand& Cmd, ID3D11DeviceContext* Ctx, FStateCache& Cache, bool bVSOnly)
{
	if (!Cmd.Shader) return;

	const bool bForce = Cache.bForceAll;
	if (bForce || Cmd.Shader != Cache.Shader)
	{
		if (bVSOnly)
			Cmd.Shader->BindVSOnly(Ctx);
		else
			Cmd.Shader->Bind(Ctx);

		Cache.Shader = Cmd.Shader;
	}
}

void FDrawCommandList::BindGeometry(const FDrawCommand& Cmd, ID3D11DeviceContext* Ctx, FStateCache& Cache)
{
	const bool bForce = Cache.bForceAll;

	if (Cmd.MeshBuffer)
	{
		if (bForce || Cmd.MeshBuffer != Cache.MeshBuffer)
		{
			uint32 Offset = 0;
			uint32 Stride = Cmd.MeshBuffer->GetVertexBuffer().GetStride();
			ID3D11Buffer* VB = Cmd.MeshBuffer->GetVertexBuffer().GetBuffer();

			if (VB && Stride > 0)
			{
				Ctx->IASetVertexBuffers(0, 1, &VB, &Stride, &Offset);
				ID3D11Buffer* IB = Cmd.MeshBuffer->GetIndexBuffer().GetBuffer();
				if (IB) Ctx->IASetIndexBuffer(IB, DXGI_FORMAT_R32_UINT, 0);
			}

			Cache.MeshBuffer = Cmd.MeshBuffer;
			Cache.RawVB = nullptr;
			Cache.RawIB = nullptr;
		}
	}
	else if (Cmd.RawVB)
	{
		if (bForce || Cmd.RawVB != Cache.RawVB)
		{
			uint32 Offset = 0;
			Ctx->IASetVertexBuffers(0, 1, &Cmd.RawVB, &Cmd.RawVBStride, &Offset);
			Cache.RawVB = Cmd.RawVB;
			Cache.MeshBuffer = nullptr;
		}
		if (bForce || Cmd.RawIB != Cache.RawIB)
		{
			Ctx->IASetIndexBuffer(Cmd.RawIB, DXGI_FORMAT_R32_UINT, 0);
			Cache.RawIB = Cmd.RawIB;
		}
	}
	else if (Cache.MeshBuffer || Cache.RawVB)
	{
		// SV_VertexID based draw - clear previous vertex/index buffers only.
		ID3D11Buffer* NullBuffer = nullptr;
		uint32 Stride = 0;
		uint32 Offset = 0;
		Ctx->IASetVertexBuffers(0, 1, &NullBuffer, &Stride, &Offset);
		Ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
		Cache.MeshBuffer = nullptr;
		Cache.RawVB = nullptr;
		Cache.RawIB = nullptr;
	}
}

void FDrawCommandList::BindConstantBuffers(const FDrawCommand& Cmd, ID3D11DeviceContext* Ctx, FStateCache& Cache)
{
	const bool bForce = Cache.bForceAll;

	// PerObject CB (Slot b1)
	if (bForce || Cmd.PerObjectCB != Cache.PerObjectCB)
	{
		ID3D11Buffer* RawCB = Cmd.PerObjectCB ? Cmd.PerObjectCB->GetBuffer() : nullptr;
		Ctx->VSSetConstantBuffers(ECBSlot::PerObject, 1, &RawCB);
		Ctx->PSSetConstantBuffers(ECBSlot::PerObject, 1, &RawCB);
		Cache.PerObjectCB = Cmd.PerObjectCB;
	}

	// PerShader CBs (Slots b2, b3)
	for (uint32 i = 0; i < 2; ++i)
	{
		if (bForce || Cmd.PerShaderCB[i] != Cache.PerShaderCB[i])
		{
			ID3D11Buffer* RawCB = Cmd.PerShaderCB[i] ? Cmd.PerShaderCB[i]->GetBuffer() : nullptr;
			uint32 Slot = ECBSlot::PerShader0 + i;
			Ctx->VSSetConstantBuffers(Slot, 1, &RawCB);
			Ctx->PSSetConstantBuffers(Slot, 1, &RawCB);
			Cache.PerShaderCB[i] = Cmd.PerShaderCB[i];
		}
	}
}

void FDrawCommandList::BindResources(const FDrawCommand& Cmd, ID3D11DeviceContext* Ctx, FStateCache& Cache)
{
	const bool bForce = Cache.bForceAll;

	// Diffuse SRV (Slot t0)
	if (bForce || Cmd.DiffuseSRV != Cache.DiffuseSRV)
	{
		ID3D11ShaderResourceView* SRV = Cmd.DiffuseSRV;
		Ctx->PSSetShaderResources(0, 1, &SRV);
		Cache.DiffuseSRV = Cmd.DiffuseSRV;
	}

	// Normal SRV (Slot t1)
	if (bForce || Cmd.NormalSRV != Cache.NormalSRV)
	{
		ID3D11ShaderResourceView* SRV = Cmd.NormalSRV;
		Ctx->PSSetShaderResources(1, 1, &SRV);
		Cache.NormalSRV = Cmd.NormalSRV;
	}

	// Extra Textures (t2~t9)
	// 이전 드로우콜의 상태가 남지 않도록 t2~t9 슬롯을 먼저 초기화.
	ID3D11ShaderResourceView* NullSRVs[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	Ctx->PSSetShaderResources(2, 8, NullSRVs);

	for (uint32 i = 0; i < Cmd.ExtraTextureCount; ++i)
	{
		const FTextureBinding& Binding = Cmd.ExtraTextures[i];
		// t0, t1은 위에서 이미 처리했으므로 건너뜀
		if (Binding.Slot < 2 || Binding.Slot >= 10) continue;

		Ctx->PSSetShaderResources(Binding.Slot, 1, &Binding.SRV);
	}
}

void FDrawCommandList::ExecuteDraw(const FDrawCommand& Cmd, ID3D11DeviceContext* Ctx)
{
	if (Cmd.IndexCount > 0)
	{
		Ctx->DrawIndexed(Cmd.IndexCount, Cmd.FirstIndex, Cmd.BaseVertex);
	}
	else if (Cmd.VertexCount > 0)
	{
		Ctx->Draw(Cmd.VertexCount, 0);
	}
	else if (Cmd.MeshBuffer)
	{
		// Draw full buffer if counts are zero
		uint32 IdxCount = Cmd.MeshBuffer->GetIndexBuffer().GetIndexCount();
		if (IdxCount > 0)
			Ctx->DrawIndexed(IdxCount, 0, 0);
		else
			Ctx->Draw(Cmd.MeshBuffer->GetVertexBuffer().GetVertexCount(), 0);
	}

	FDrawCallStats::Increment();
}
