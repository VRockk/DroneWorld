#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PilotStation.generated.h"

class UCameraComponent;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

// The pilot's seat inside the Void: an HMD camera and a screen mesh that shows the drone's Feed. It is
// runtime-spawned for the local VR pilot only and used as the controller's view target while the drone
// pawn stays possessed, so the world the pilot sees is the 2D Feed on the screen while
// head tracking stays live in the Void. Blueprintable so the screen mesh, its material, placement, and
// the OSD overlay are authored in the editor.
UCLASS(Blueprintable)
class DRONEWORLD_API APilotStation : public AActor
{
	GENERATED_BODY()

public:
	APilotStation();

	// Show a Feed on the screen: builds a dynamic instance of the screen's material and binds the render
	// target to its Feed texture parameter, so whatever the drone captures appears on the screen.
	UFUNCTION(BlueprintCallable, Category = "Pilot Station")
	void SetFeed(UTextureRenderTarget2D* Feed);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pilot Station")
	TObjectPtr<USceneComponent> StationRoot;

	// The HMD camera. Head tracking is live here within the Void, independent of the drone's motion.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pilot Station")
	TObjectPtr<UCameraComponent> HmdCamera;

	// The virtual screen the Feed is shown on; the mesh and its base material are assigned in Blueprint.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pilot Station")
	TObjectPtr<UStaticMeshComponent> ScreenMesh;

	// The texture parameter on the screen material that the Feed render target is bound to. Must match the
	// parameter name in the material assigned to the screen mesh.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pilot Station")
	FName FeedTextureParameter = TEXT("Feed");

	// How far in front of the pilot the screen sits, in cm. With the seated tracking origin the eye is at
	// the station camera, so the screen is placed this far ahead at the same height - a comfortable
	// viewing distance tunable without re-authoring the mesh.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pilot Station")
	float ScreenDistance = 100.f;
};
