#include "DefaultRenderPipeline.h"

#include "Renderer.h"
#include "Engine/Runtime/Engine.h"
#include "Component/CameraComponent.h"
#include "GameFramework/World.h"

FDefaultRenderPipeline::FDefaultRenderPipeline(UEngine* InEngine, FRenderer& InRenderer)
	: Engine(InEngine)
{
}

FDefaultRenderPipeline::~FDefaultRenderPipeline()
{
}

void FDefaultRenderPipeline::Execute(float DeltaTime, FRenderer& Renderer)
{
	Frame.ClearViewportResources();

	UWorld* World = Engine->GetWorld();
	UCameraComponent* Camera = World ? World->GetActiveCamera() : nullptr;
	FScene* Scene = nullptr;
	if (Camera)
	{
		FShowFlags ShowFlags;
		EViewMode ViewMode = EViewMode::Lit_BlinnPhong;

		Frame.SetCameraInfo(Camera);
		Frame.SetRenderSettings(ViewMode, ShowFlags);

		Scene = &World->GetScene();
		Scene->ClearFrameData();

		Renderer.BeginCollect(Frame, Scene->GetProxyCount());
		Collector.CollectWorld(World, Frame, Renderer);
		Collector.CollectAlwaysShowLightRanges(World, *Scene);
		Collector.CollectDebugDraw(Frame, *Scene);
		Renderer.BuildDynamicCommands(Frame, Scene);
	}
	else
	{
		Renderer.BeginCollect(Frame);
		Renderer.BuildDynamicCommands(Frame, nullptr);
	}

	Renderer.BeginFrame();
	Renderer.UpdateLightBuffer(Renderer.GetFD3DDevice().GetDeviceContext(), Scene, Frame.FrustumVolume);

	Renderer.RenderDepthPrePass(Frame);
	
	// Depth Copy for Culling (since DefaultRenderPipeline doesn't have Hi-Z/Occlusion logic yet, we do it here)
	ID3D11DeviceContext* Ctx = Renderer.GetFD3DDevice().GetDeviceContext();
	if (Frame.DepthTexture && Frame.DepthCopyTexture)
	{
		Ctx->CopyResource(Frame.DepthCopyTexture, Frame.DepthTexture);
	}

	Renderer.ExecuteLightCulling(Frame);

	Renderer.Render(Frame);
	Renderer.EndFrame();}
