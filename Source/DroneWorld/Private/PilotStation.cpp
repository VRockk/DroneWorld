#include "PilotStation.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/TextureRenderTarget2D.h"

APilotStation::APilotStation()
{
	PrimaryActorTick.bCanEverTick = false;

	StationRoot = CreateDefaultSubobject<USceneComponent>(TEXT("StationRoot"));
	SetRootComponent(StationRoot);

	HmdCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("HmdCamera"));
	HmdCamera->SetupAttachment(StationRoot);

	ScreenMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ScreenMesh"));
	ScreenMesh->SetupAttachment(StationRoot);
	ScreenMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Sit the screen directly ahead of the camera at the same height, so it lands at eye level under the
	// seated tracking origin rather than on top of the eye at the origin.
	ScreenMesh->SetRelativeLocation(FVector(ScreenDistance, 0.f, 0.f));
}

void APilotStation::SetFeed(UTextureRenderTarget2D* Feed)
{
	if (!Feed || !ScreenMesh)
	{
		return;
	}

	// Instance the screen's existing material so the Feed can be bound per spawn without disturbing the
	// shared asset, then drive its Feed texture parameter from the render target.
	if (UMaterialInstanceDynamic* Screen = ScreenMesh->CreateDynamicMaterialInstance(0))
	{
		Screen->SetTextureParameterValue(FeedTextureParameter, Feed);
	}
}
