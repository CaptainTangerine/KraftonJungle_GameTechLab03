#include "FrameContext.h"
#include "Component/CameraComponent.h"
#include "Viewport/Viewport.h"

void FFrameContext::SetCameraInfo(const UCameraComponent* Camera)
{
	View            = Camera->GetViewMatrix();
	Proj            = Camera->GetProjectionMatrix();
	CameraPosition  = Camera->GetWorldLocation();
	CameraForward   = Camera->GetForwardVector();
	CameraRight     = Camera->GetRightVector();
	CameraUp        = Camera->GetUpVector();
	bIsOrtho        = Camera->IsOrthogonal();
	OrthoWidth      = Camera->GetOrthoWidth();
	NearClip        = Camera->GetCameraState().NearZ;
	FarClip         = Camera->GetCameraState().FarZ;

	// Per-viewport frustum — used by RenderCollector for inline frustum culling
	FrustumVolume.UpdateFromMatrix(View * Proj);
}

void FFrameContext::SetViewportInfo(const FViewport* VP)
{
	ViewportWidth    = static_cast<float>(VP->GetWidth());
	ViewportHeight   = static_cast<float>(VP->GetHeight());
	ViewportOffsetX  = VP->GetViewportRect().TopLeftX;
	ViewportOffsetY  = VP->GetViewportRect().TopLeftY;
	ViewportRTV             = VP->GetRTV();
	ViewportDSV             = VP->GetDSV();
	NormalRTV               = VP->GetNormalRTV();
	NormalSRV               = VP->GetNormalSRV();
	SSAORTV                 = VP->GetSSAORTV();
	SSAOSRV                 = VP->GetSSAOSRV();
	SSAOUAV                 = VP->GetSSAOUAV();
	SSAOBlurredRTV          = VP->GetSSAOBlurredRTV();
	SSAOBlurredSRV          = VP->GetSSAOBlurredSRV();
	SSAOBlurredUAV          = VP->GetSSAOBlurredUAV();
	SceneColorCopySRV       = VP->GetSceneColorCopySRV();
	SceneColorCopyTexture   = VP->GetSceneColorCopyTexture();
	GodRayOcclusionSRV		= VP->GetGodRayOcclusionSRV();
	GodRayOcclusionRTV		= VP->GetGodRayOcclusionRTV();
	ViewportRenderTexture   = VP->GetRTTexture();
	DepthTexture            = VP->GetDepthTexture();
	DepthCopyTexture        = VP->GetDepthCopyTexture();
	DepthCopySRV            = VP->GetDepthCopySRV();
	StencilCopySRV          = VP->GetStencilCopySRV();
}
