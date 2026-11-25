#include "Viewport/Viewport.h"

FViewport::~FViewport()
{
	ReleaseResources();
}

bool FViewport::Initialize(ID3D11Device* InDevice, uint32 InWidth, uint32 InHeight)
{
	Device = InDevice;
	Width = InWidth;
	Height = InHeight;

	return CreateResources();
}

void FViewport::Release()
{
	ReleaseResources();
	Device = nullptr;
	Width = 0;
	Height = 0;
}

void FViewport::Resize(uint32 InWidth, uint32 InHeight)
{
	if (InWidth == Width && InHeight == Height) return;

	Width = InWidth;
	Height = InHeight;

	ReleaseResources();
	CreateResources();
}

void FViewport::RequestResize(uint32 InWidth, uint32 InHeight)
{
	if (InWidth == Width && InHeight == Height)
	{
		bPendingResize = false;
		return;
	}

	PendingWidth = InWidth;
	PendingHeight = InHeight;
	bPendingResize = true;
}

bool FViewport::ApplyPendingResize()
{
	if (!bPendingResize) return false;

	bPendingResize = false;
	Resize(PendingWidth, PendingHeight);
	return true;
}

void FViewport::BeginRender(ID3D11DeviceContext* Ctx, const float ClearColor[4])
{
	if (!RTV) return;

	const float DefaultColor[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
	const float* Color = ClearColor ? ClearColor : DefaultColor;
	D3D11_VIEWPORT VPRect = GetViewportRect();

	Ctx->ClearRenderTargetView(RTV, Color);
	if (NormalRTV)
	{
		float ClearNormal[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; 
		Ctx->ClearRenderTargetView(NormalRTV, ClearNormal);
	}
	Ctx->ClearDepthStencilView(DSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.0f, 0);
	Ctx->OMSetRenderTargets(1, &RTV, DSV);
	Ctx->RSSetViewports(1, &VPRect);
}

bool FViewport::CreateResources()
{
	if (!Device || Width == 0 || Height == 0) return false;

	// ── 1. 메인 렌더 타깃 (RTTexture, RTV, SRV) ──
	D3D11_TEXTURE2D_DESC MainRTDesc = {};
	MainRTDesc.Width = Width;
	MainRTDesc.Height = Height;
	MainRTDesc.MipLevels = 1;
	MainRTDesc.ArraySize = 1;
	MainRTDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	MainRTDesc.SampleDesc.Count = 1;
	MainRTDesc.Usage = D3D11_USAGE_DEFAULT;
	MainRTDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	HRESULT hr = Device->CreateTexture2D(&MainRTDesc, nullptr, &RTTexture);
	if (FAILED(hr)) return false;

	hr = Device->CreateRenderTargetView(RTTexture, nullptr, &RTV);
	if (FAILED(hr)) return false;

	hr = Device->CreateShaderResourceView(RTTexture, nullptr, &SRV);
	if (FAILED(hr)) return false;

	// ── 2. 노말 버퍼 (NormalTexture, NormalRTV, NormalSRV) ──
	D3D11_TEXTURE2D_DESC NormalDesc = MainRTDesc;
	NormalDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
	
	hr = Device->CreateTexture2D(&NormalDesc, nullptr, &NormalTexture);
	if (FAILED(hr)) return false;

	hr = Device->CreateRenderTargetView(NormalTexture, nullptr, &NormalRTV);
	if (FAILED(hr)) return false;

	hr = Device->CreateShaderResourceView(NormalTexture, nullptr, &NormalSRV);
	if (FAILED(hr)) return false;

	// ── 3. SSAO 버퍼 (SSAOTexture, SSAORTV, SSAOSRV, SSAOUAV) ──
	D3D11_TEXTURE2D_DESC SSAODesc = MainRTDesc;
	SSAODesc.Format = DXGI_FORMAT_R8_UNORM;
	SSAODesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	hr = Device->CreateTexture2D(&SSAODesc, nullptr, &SSAOTexture);
	if (FAILED(hr)) return false;

	hr = Device->CreateRenderTargetView(SSAOTexture, nullptr, &SSAORTV);
	if (FAILED(hr)) return false;

	hr = Device->CreateShaderResourceView(SSAOTexture, nullptr, &SSAOSRV);
	if (FAILED(hr)) return false;

	hr = Device->CreateUnorderedAccessView(SSAOTexture, nullptr, &SSAOUAV);
	if (FAILED(hr)) return false;

	// SSAO Blurred
	hr = Device->CreateTexture2D(&SSAODesc, nullptr, &SSAOBlurredTexture);
	if (FAILED(hr)) return false;

	hr = Device->CreateRenderTargetView(SSAOBlurredTexture, nullptr, &SSAOBlurredRTV);
	if (FAILED(hr)) return false;

	hr = Device->CreateShaderResourceView(SSAOBlurredTexture, nullptr, &SSAOBlurredSRV);
	if (FAILED(hr)) return false;

	hr = Device->CreateUnorderedAccessView(SSAOBlurredTexture, nullptr, &SSAOBlurredUAV);
	if (FAILED(hr)) return false;

	// ── 4. SceneColor 복사본 (PostProcess 읽기용) ──
	D3D11_TEXTURE2D_DESC CopyRTDesc = MainRTDesc;
	CopyRTDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	hr = Device->CreateTexture2D(&CopyRTDesc, nullptr, &SceneColorCopyTexture);
	if (FAILED(hr)) return false;

	hr = Device->CreateShaderResourceView(SceneColorCopyTexture, nullptr, &SceneColorCopySRV);
	if (FAILED(hr)) return false;

	// ── 5. 뎁스/스텐실 버퍼 (DepthTexture, DSV) ──
	D3D11_TEXTURE2D_DESC DepthDesc = {};
	DepthDesc.Width = Width;
	DepthDesc.Height = Height;
	DepthDesc.MipLevels = 1;
	DepthDesc.ArraySize = 1;
	DepthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	DepthDesc.SampleDesc.Count = 1;
	DepthDesc.Usage = D3D11_USAGE_DEFAULT;
	DepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	hr = Device->CreateTexture2D(&DepthDesc, nullptr, &DepthTexture);
	if (FAILED(hr)) return false;

	D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
	DSVDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	DSVDesc.Texture2D.MipSlice = 0;

	hr = Device->CreateDepthStencilView(DepthTexture, &DSVDesc, &DSV);
	if (FAILED(hr)) return false;

	// ── 6. Depth 복사본 (SceneDepth/Stencil SRV용) ──
	D3D11_TEXTURE2D_DESC DepthCopyDesc = DepthDesc;
	DepthCopyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE; // CopyResource의 대상이지만 DSV로 사용하지 않으므로 BIND_SHADER_RESOURCE만 설정
	
	hr = Device->CreateTexture2D(&DepthCopyDesc, nullptr, &DepthCopyTexture);
	if (FAILED(hr)) return false;

	D3D11_SHADER_RESOURCE_VIEW_DESC DepthSRVDesc = {};
	DepthSRVDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	DepthSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	DepthSRVDesc.Texture2D.MipLevels = 1;
	DepthSRVDesc.Texture2D.MostDetailedMip = 0;

	hr = Device->CreateShaderResourceView(DepthCopyTexture, &DepthSRVDesc, &DepthCopySRV);
	if (FAILED(hr)) return false;

	D3D11_SHADER_RESOURCE_VIEW_DESC StencilSRVDesc = {};
	StencilSRVDesc.Format = DXGI_FORMAT_X24_TYPELESS_G8_UINT;
	StencilSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	StencilSRVDesc.Texture2D.MipLevels = 1;
	StencilSRVDesc.Texture2D.MostDetailedMip = 0;

	hr = Device->CreateShaderResourceView(DepthCopyTexture, &StencilSRVDesc, &StencilCopySRV);
	if (FAILED(hr)) return false;


	D3D11_TEXTURE2D_DESC GodRayOccDesc = MainRTDesc;

	D3D11_SHADER_RESOURCE_VIEW_DESC GodRayOccSRVDesc = {};
	GodRayOccSRVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	GodRayOccSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	GodRayOccSRVDesc.Texture2D.MipLevels = 1;
	GodRayOccSRVDesc.Texture2D.MostDetailedMip = 0;

	hr = Device->CreateTexture2D(&GodRayOccDesc, nullptr, &GodRayOcclusionTexture);
	if (FAILED(hr)) return false;

	hr = Device->CreateRenderTargetView(GodRayOcclusionTexture, nullptr, &GodRayOcculsionRTV);
	if (FAILED(hr)) return false;

	hr = Device->CreateShaderResourceView(GodRayOcclusionTexture, &GodRayOccSRVDesc, &GodRayOcclusionSRV);
	if (FAILED(hr)) return false;

	// ── 뷰포트 렉트 ──
	ViewportRect.TopLeftX = 0.0f;
	ViewportRect.TopLeftY = 0.0f;
	ViewportRect.Width = static_cast<float>(Width);
	ViewportRect.Height = static_cast<float>(Height);
	ViewportRect.MinDepth = 0.0f;
	ViewportRect.MaxDepth = 1.0f;

	return true;
}

void FViewport::ReleaseResources()
{
	if (StencilCopySRV) { StencilCopySRV->Release(); StencilCopySRV = nullptr; }
	if (DepthCopySRV) { DepthCopySRV->Release(); DepthCopySRV = nullptr; }
	if (DepthCopyTexture) { DepthCopyTexture->Release(); DepthCopyTexture = nullptr; }
	if (DSV) { DSV->Release(); DSV = nullptr; }
	if (DepthTexture) { DepthTexture->Release(); DepthTexture = nullptr; }

	if (NormalSRV) { NormalSRV->Release(); NormalSRV = nullptr; }
	if (NormalRTV) { NormalRTV->Release(); NormalRTV = nullptr; }
	if (NormalTexture) { NormalTexture->Release(); NormalTexture = nullptr; }

	if (SSAOBlurredUAV) { SSAOBlurredUAV->Release(); SSAOBlurredUAV = nullptr; }
	if (SSAOBlurredSRV) { SSAOBlurredSRV->Release(); SSAOBlurredSRV = nullptr; }
	if (SSAOBlurredRTV) { SSAOBlurredRTV->Release(); SSAOBlurredRTV = nullptr; }
	if (SSAOBlurredTexture) { SSAOBlurredTexture->Release(); SSAOBlurredTexture = nullptr; }

	if (SSAOUAV) { SSAOUAV->Release(); SSAOUAV = nullptr; }
	if (SSAOSRV) { SSAOSRV->Release(); SSAOSRV = nullptr; }
	if (SSAORTV) { SSAORTV->Release(); SSAORTV = nullptr; }
	if (SSAOTexture) { SSAOTexture->Release(); SSAOTexture = nullptr; }

	if (SRV) { SRV->Release(); SRV = nullptr; }
	if (RTV) { RTV->Release(); RTV = nullptr; }
	if (RTTexture) { RTTexture->Release(); RTTexture = nullptr; }
	if (SceneColorCopySRV) { SceneColorCopySRV->Release(); SceneColorCopySRV = nullptr; }
	if (SceneColorCopyTexture) { SceneColorCopyTexture->Release(); SceneColorCopyTexture = nullptr; }
	if (GodRayOcclusionSRV) { GodRayOcclusionSRV->Release(); GodRayOcclusionSRV = nullptr; }
	if (GodRayOcculsionRTV) { GodRayOcculsionRTV->Release(); GodRayOcculsionRTV = nullptr; }
	if (GodRayOcclusionTexture) { GodRayOcclusionTexture->Release(); GodRayOcclusionTexture = nullptr; };

}
